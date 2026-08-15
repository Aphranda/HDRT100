#include "refmem_command.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n",
                     name,
                     (unsigned long)expected,
                     (unsigned long)actual);
        return 1;
    }
    return 0;
}

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n",
                     name,
                     expected ? 1 : 0,
                     actual ? 1 : 0);
        return 1;
    }
    return 0;
}

static refmem_command_request_t make_request(uint32_t seq)
{
    refmem_command_request_t request;
    (void)memset(&request, 0, sizeof(request));
    request.command_seq = seq;
    request.source_node = 0u;
    request.source_instance = 3u;
    request.target_mask = 0x06u;
    request.required_mask = 0x06u;
    request.command_type = REFMEM_COMMAND_TYPE_CONFIG_STAGE;
    request.command_class = REFMEM_COMMAND_CLASS_CONFIG;
    request.payload_kind = REFMEM_COMMAND_PAYLOAD_STAGING_REF;
    request.payload_ref = 0x1234u;
    request.payload_size = 16u;
    request.payload_crc32 = 0xAABBCCDDu;
    request.issue_epoch = 7u;
    request.run_id = 9u;
    request.timeout_us = 100u;
    return request;
}

static int test_post_and_snapshot(void)
{
    int failed = 0;
    refmem_command_slot_t slot;
    refmem_command_snapshot_t snapshot;
    refmem_command_request_t request = make_request(1u);

    failed += expect_bool("init", refmem_command_init(&slot, 0x11223344u), true);
    failed += expect_bool("post", refmem_command_try_post(&slot, &request, 1000u), true);
    failed += expect_bool("snapshot", refmem_command_get_snapshot(&slot, &snapshot), true);
    failed += expect_u32("state", snapshot.state, REFMEM_COMMAND_STATE_POSTED);
    failed += expect_u32("seq", snapshot.command_seq, 1u);
    failed += expect_u32("target", snapshot.target_mask, 0x06u);
    failed += expect_u32("required", snapshot.required_mask, 0x06u);
    failed += expect_u32("reason crc", snapshot.reason_table_crc32, 0x11223344u);
    failed += expect_bool("busy post rejected",
                          refmem_command_try_post(&slot, &request, 1001u),
                          false);
    return failed;
}

static int test_take_and_ack_required_nodes(void)
{
    int failed = 0;
    refmem_command_slot_t slot;
    refmem_command_snapshot_t snapshot;
    refmem_command_request_t request = make_request(2u);

    (void)refmem_command_init(&slot, 0u);
    (void)refmem_command_try_post(&slot, &request, 10u);
    failed += expect_u32("take node1",
                         refmem_command_try_take(&slot, 1u, 7u, 9u, 0xAABBCCDDu, 4u),
                         REFMEM_COMMAND_TAKE_TAKEN);
    failed += expect_bool("ack node1", refmem_command_ack(&slot, 1u, 5u), true);
    (void)refmem_command_get_snapshot(&slot, &snapshot);
    failed += expect_u32("node1 taken", snapshot.taken_flags, 0x02u);
    failed += expect_u32("node1 ack", snapshot.ack_flags, 0x02u);
    failed += expect_u32("state partial ack", snapshot.state, REFMEM_COMMAND_STATE_TAKEN);

    failed += expect_u32("take node2",
                         refmem_command_try_take(&slot, 2u, 7u, 9u, 0xAABBCCDDu, 6u),
                         REFMEM_COMMAND_TAKE_TAKEN);
    failed += expect_bool("ack node2", refmem_command_ack(&slot, 2u, 7u), true);
    (void)refmem_command_get_snapshot(&slot, &snapshot);
    failed += expect_u32("all ack", snapshot.ack_flags, 0x06u);
    failed += expect_u32("state acked", snapshot.state, REFMEM_COMMAND_STATE_ACKED);
    return failed;
}

static int test_take_rejects_epoch_and_payload_crc(void)
{
    int failed = 0;
    refmem_command_slot_t slot;
    refmem_command_snapshot_t snapshot;
    refmem_command_request_t request = make_request(3u);

    (void)refmem_command_init(&slot, 0u);
    (void)refmem_command_try_post(&slot, &request, 10u);
    failed += expect_u32("bad epoch",
                         refmem_command_try_take(&slot, 1u, 8u, 9u, 0xAABBCCDDu, 11u),
                         REFMEM_COMMAND_TAKE_EPOCH_MISMATCH);
    (void)refmem_command_get_snapshot(&slot, &snapshot);
    failed += expect_u32("epoch nack", snapshot.nack_flags, 0x02u);
    failed += expect_u32("epoch reason", snapshot.last_reason, REFMEM_COMMAND_REASON_EPOCH_MISMATCH);
    failed += expect_u32("epoch state", snapshot.state, REFMEM_COMMAND_STATE_NACKED);

    (void)refmem_command_clear(&slot, 3u);
    request.command_seq = 4u;
    (void)refmem_command_try_post(&slot, &request, 20u);
    failed += expect_u32("bad payload",
                         refmem_command_try_take(&slot, 2u, 7u, 9u, 0x11111111u, 12u),
                         REFMEM_COMMAND_TAKE_PAYLOAD_CRC_MISMATCH);
    (void)refmem_command_get_snapshot(&slot, &snapshot);
    failed += expect_u32("payload nack", snapshot.nack_flags, 0x04u);
    failed += expect_u32("payload reason",
                         snapshot.last_reason,
                         REFMEM_COMMAND_REASON_PAYLOAD_CRC_MISMATCH);
    return failed;
}

static int test_timeout_and_clear_seq(void)
{
    int failed = 0;
    refmem_command_slot_t slot;
    refmem_command_snapshot_t snapshot;
    refmem_command_request_t request = make_request(5u);

    (void)refmem_command_init(&slot, 0x99u);
    (void)refmem_command_try_post(&slot, &request, 100u);
    failed += expect_bool("early timeout", refmem_command_mark_timeout(&slot, 150u, 1u), false);
    failed += expect_bool("timeout", refmem_command_mark_timeout(&slot, 201u, 2u), true);
    (void)refmem_command_get_snapshot(&slot, &snapshot);
    failed += expect_u32("timeout flags", snapshot.timeout_flags, 0x06u);
    failed += expect_u32("timeout reason", snapshot.last_reason, REFMEM_COMMAND_REASON_TIMEOUT);
    failed += expect_u32("timeout state", snapshot.state, REFMEM_COMMAND_STATE_TIMED_OUT);
    failed += expect_bool("wrong clear", refmem_command_clear(&slot, 4u), false);
    failed += expect_bool("right clear", refmem_command_clear(&slot, 5u), true);
    (void)refmem_command_get_snapshot(&slot, &snapshot);
    failed += expect_u32("idle after clear", snapshot.state, REFMEM_COMMAND_STATE_IDLE);
    failed += expect_u32("clear seq", snapshot.clear_seq, 5u);
    failed += expect_u32("last completed", snapshot.last_completed_seq, 5u);
    failed += expect_u32("reason crc kept", snapshot.reason_table_crc32, 0x99u);
    return failed;
}

static int test_sync_payload_mapping(void)
{
    int failed = 0;
    refmem_command_slot_t slot;
    refmem_command_snapshot_t snapshot;
    refmem_sync_command_payload_t command_payload;
    refmem_sync_ack_nack_payload_t ack_payload;
    refmem_command_request_t request = make_request(6u);

    (void)refmem_command_init(&slot, 0u);
    (void)refmem_command_try_post(&slot, &request, 10u);
    (void)refmem_command_try_take(&slot, 1u, 7u, 9u, 0xAABBCCDDu, 4u);
    (void)refmem_command_nack(&slot, 1u, REFMEM_COMMAND_REASON_RESOURCE_BUSY, 14u);
    (void)refmem_command_get_snapshot(&slot, &snapshot);

    failed += expect_bool("command payload",
                          refmem_command_to_sync_command_payload(&snapshot, &command_payload),
                          true);
    failed += expect_u32("payload seq", command_payload.command_seq, 6u);
    failed += expect_u32("payload type", command_payload.command_type, request.command_type);
    failed += expect_u32("payload crc", command_payload.payload_crc32, request.payload_crc32);

    failed += expect_bool("ack payload",
                          refmem_command_to_sync_ack_payload(&snapshot, &ack_payload),
                          true);
    failed += expect_u32("ack seq", ack_payload.command_seq, 6u);
    failed += expect_u32("ack taken", ack_payload.taken_flags, 0x02u);
    failed += expect_u32("ack nack", ack_payload.nack_flags, 0x02u);
    failed += expect_u32("ack reason", ack_payload.last_reason, REFMEM_COMMAND_REASON_RESOURCE_BUSY);
    failed += expect_u32("ack evidence", ack_payload.evidence_index, 14u);
    return failed;
}

static int test_invalid_requests(void)
{
    int failed = 0;
    refmem_command_slot_t slot;
    refmem_command_request_t request = make_request(7u);

    (void)refmem_command_init(&slot, 0u);
    request.required_mask = 0x08u;
    failed += expect_bool("required outside target",
                          refmem_command_try_post(&slot, &request, 0u),
                          false);
    request = make_request(7u);
    request.source_node = REFMEM_COMMAND_NODE_COUNT;
    failed += expect_bool("bad source",
                          refmem_command_try_post(&slot, &request, 0u),
                          false);
    return failed;
}

static int test_take_rejects_idle_and_non_target(void)
{
    int failed = 0;
    refmem_command_slot_t slot;
    refmem_command_request_t request = make_request(8u);

    (void)refmem_command_init(&slot, 0u);
    failed += expect_u32("idle take",
                         refmem_command_try_take(&slot, 1u, 7u, 9u, 0xAABBCCDDu, 0u),
                         REFMEM_COMMAND_TAKE_NO_COMMAND);
    (void)refmem_command_try_post(&slot, &request, 10u);
    failed += expect_u32("non target take",
                         refmem_command_try_take(&slot, 0u, 7u, 9u, 0xAABBCCDDu, 0u),
                         REFMEM_COMMAND_TAKE_NOT_TARGET);
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_post_and_snapshot();
    failed += test_take_and_ack_required_nodes();
    failed += test_take_rejects_epoch_and_payload_crc();
    failed += test_timeout_and_clear_seq();
    failed += test_sync_payload_mapping();
    failed += test_invalid_requests();
    failed += test_take_rejects_idle_and_non_target();

    if (failed != 0) {
        (void)printf("refmem_command tests failed: %d\n", failed);
        return 1;
    }
    (void)printf("refmem_command tests passed\n");
    return 0;
}
