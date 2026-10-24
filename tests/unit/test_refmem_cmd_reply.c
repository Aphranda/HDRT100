#include "refmem_cmd_reply.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n", name,
                     (unsigned long)expected, (unsigned long)actual);
        return 1;
    }
    return 0;
}

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n", name, expected ? 1 : 0,
                     actual ? 1 : 0);
        return 1;
    }
    return 0;
}

static refmem_cmd_reply_t make_reply(uint32_t seq, uint32_t slot,
                                     uint32_t state, uint32_t reason)
{
    refmem_cmd_reply_t reply;
    (void)memset(&reply, 0, sizeof(reply));
    reply.command_seq = seq;
    reply.source_slot = slot;
    reply.state = state;
    reply.reason = reason;
    return reply;
}

static int test_encode_decode(void)
{
    int failed = 0;
    refmem_cmd_reply_t ack =
        make_reply(0x12345u, 1u, REFMEM_COMMAND_STATE_ACKED,
                   REFMEM_COMMAND_REASON_NONE);
    refmem_cmd_reply_wire_t wire;
    failed += expect_bool("encode ack", refmem_cmd_reply_encode(&ack, &wire),
                          true);
    failed += expect_u32("ack seq16", wire.seq16, 0x2345u);
    failed += expect_u32("ack valid",
                         wire.flags & REFMEM_CMD_REPLY_FLAG_VALID, 1u);
    failed += expect_u32("ack command",
                         wire.flags & REFMEM_CMD_REPLY_FLAG_COMMAND,
                         REFMEM_CMD_REPLY_FLAG_COMMAND);
    failed += expect_u32("ack no nack",
                         wire.flags & REFMEM_CMD_REPLY_FLAG_NACK, 0u);

    refmem_cmd_reply_t decoded;
    failed += expect_bool("decode ack", refmem_cmd_reply_decode(&wire, &decoded),
                          true);
    failed += expect_u32("decoded seq", decoded.command_seq, 0x2345u);
    failed += expect_u32("decoded state", decoded.state,
                         REFMEM_COMMAND_STATE_ACKED);
    failed += expect_u32("decoded reason", decoded.reason,
                         REFMEM_COMMAND_REASON_NONE);

    refmem_cmd_reply_t nack =
        make_reply(9u, 2u, REFMEM_COMMAND_STATE_NACKED,
                   REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
    failed += expect_bool("encode nack", refmem_cmd_reply_encode(&nack, &wire),
                          true);
    failed += expect_u32("nack bit", wire.flags & REFMEM_CMD_REPLY_FLAG_NACK,
                         REFMEM_CMD_REPLY_FLAG_NACK);
    failed += expect_bool("decode nack", refmem_cmd_reply_decode(&wire, &decoded),
                          true);
    failed += expect_u32("decoded nack state", decoded.state,
                         REFMEM_COMMAND_STATE_NACKED);
    failed += expect_u32("decoded nack reason", decoded.reason,
                         REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);

    refmem_cmd_reply_t busy =
        make_reply(3u, 1u, REFMEM_COMMAND_STATE_BUSY,
                   REFMEM_COMMAND_REASON_NONE);
    failed += expect_bool("encode non-terminal",
                          refmem_cmd_reply_encode(&busy, &wire), false);
    refmem_cmd_reply_t zero =
        make_reply(0u, 1u, REFMEM_COMMAND_STATE_ACKED,
                   REFMEM_COMMAND_REASON_NONE);
    failed += expect_bool("encode zero seq",
                          refmem_cmd_reply_encode(&zero, &wire), false);

    wire.seq16 = 1u;
    wire.flags = 0u;
    wire.reason = 0u;
    failed += expect_bool("decode missing flags",
                          refmem_cmd_reply_decode(&wire, &decoded), false);
    return failed;
}

static int test_aggregate(void)
{
    int failed = 0;
    refmem_cmd_reply_aggregate_t aggregate;
    refmem_cmd_reply_aggregate_begin(&aggregate, 0x20001u, 0x0Eu, 0x0Eu);

    refmem_cmd_reply_t reply =
        make_reply(0x20001u, 1u, REFMEM_COMMAND_STATE_ACKED,
                   REFMEM_COMMAND_REASON_NONE);
    failed += expect_bool("apply slot1", refmem_cmd_reply_aggregate_apply(
                                           &aggregate, &reply), true);
    failed += expect_u32("partial state", aggregate.state,
                         REFMEM_COMMAND_STATE_POSTED);
    reply = make_reply(0x20001u, 2u, REFMEM_COMMAND_STATE_ACKED,
                       REFMEM_COMMAND_REASON_NONE);
    failed += expect_bool("apply slot2", refmem_cmd_reply_aggregate_apply(
                                           &aggregate, &reply), true);
    failed += expect_u32("still partial", aggregate.state,
                         REFMEM_COMMAND_STATE_POSTED);
    reply = make_reply(0x20001u, 3u, REFMEM_COMMAND_STATE_ACKED,
                       REFMEM_COMMAND_REASON_NONE);
    failed += expect_bool("apply slot3", refmem_cmd_reply_aggregate_apply(
                                           &aggregate, &reply), true);
    failed += expect_u32("all required acked", aggregate.state,
                         REFMEM_COMMAND_STATE_ACKED);
    failed += expect_u32("acked mask", aggregate.acked_mask, 0x0Eu);
    failed += expect_u32("applied count", aggregate.applied_count, 3u);

    /* Duplicate and stale replies must not change the terminal result. */
    reply = make_reply(0x20001u, 1u, REFMEM_COMMAND_STATE_NACKED,
                       REFMEM_COMMAND_REASON_RESOURCE_BUSY);
    failed += expect_bool("duplicate rejected",
                          refmem_cmd_reply_aggregate_apply(&aggregate, &reply),
                          false);
    failed += expect_u32("duplicate count", aggregate.duplicate_count, 1u);
    failed += expect_u32("state unchanged", aggregate.state,
                         REFMEM_COMMAND_STATE_ACKED);
    reply = make_reply(0x1FFFFu, 3u, REFMEM_COMMAND_STATE_ACKED,
                       REFMEM_COMMAND_REASON_NONE);
    failed += expect_bool("stale rejected",
                          refmem_cmd_reply_aggregate_apply(&aggregate, &reply),
                          false);
    failed += expect_u32("stale count", aggregate.stale_count, 1u);
    reply = make_reply(0x20001u, 7u, REFMEM_COMMAND_STATE_ACKED,
                       REFMEM_COMMAND_REASON_NONE);
    failed += expect_bool("bad target rejected",
                          refmem_cmd_reply_aggregate_apply(&aggregate, &reply),
                          false);
    failed += expect_u32("bad target count", aggregate.bad_target_count, 1u);

    /* One required NACK dominates and keeps the reason. */
    refmem_cmd_reply_aggregate_begin(&aggregate, 0x30002u, 0x0Eu, 0x0Eu);
    reply = make_reply(0x30002u, 1u, REFMEM_COMMAND_STATE_ACKED,
                       REFMEM_COMMAND_REASON_NONE);
    (void)refmem_cmd_reply_aggregate_apply(&aggregate, &reply);
    reply = make_reply(0x30002u, 2u, REFMEM_COMMAND_STATE_NACKED,
                       REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
    (void)refmem_cmd_reply_aggregate_apply(&aggregate, &reply);
    failed += expect_u32("nack dominates", aggregate.state,
                         REFMEM_COMMAND_STATE_NACKED);
    failed += expect_u32("nack reason", aggregate.last_reason,
                         REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED);
    failed += expect_u32("nack mask", aggregate.nacked_mask, 0x04u);

    /* Only one of two required targets reports: not complete. */
    refmem_cmd_reply_aggregate_begin(&aggregate, 0x40003u, 0x06u, 0x06u);
    reply = make_reply(0x40003u, 1u, REFMEM_COMMAND_STATE_ACKED,
                       REFMEM_COMMAND_REASON_NONE);
    (void)refmem_cmd_reply_aggregate_apply(&aggregate, &reply);
    failed += expect_u32("missing required stays posted", aggregate.state,
                         REFMEM_COMMAND_STATE_POSTED);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_encode_decode();
    failed += test_aggregate();
    if (failed != 0) {
        (void)printf("refmem_cmd_reply tests failed: %d\n", failed);
        return 1;
    }
    (void)printf("refmem_cmd_reply tests passed\n");
    return 0;
}
