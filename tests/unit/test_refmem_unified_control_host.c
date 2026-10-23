#include "refmem_action_registry.h"
#include "refmem_cmd_codec.h"
#include "refmem_command.h"
#include "refmem_control_route.h"
#include "refmem_trigger_session.h"

#include <stdio.h>
#include <string.h>

static void dummy_handler(void *context, const refmem_action_request_t *request)
{
    (void)context;
    (void)request;
}

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

static refmem_command_request_t make_command(void)
{
    refmem_command_request_t r;
    (void)memset(&r, 0, sizeof(r));
    r.command_seq = 42u;
    r.source_node = 1u;
    r.source_instance = 0u;
    r.target_mask = 0x06u;
    r.required_mask = 0x02u;
    r.command_type = REFMEM_COMMAND_TYPE_START;
    r.command_class = REFMEM_COMMAND_CLASS_RUN;
    r.payload_kind = REFMEM_COMMAND_PAYLOAD_STAGING_REF;
    r.payload_ref = 0x100u;
    r.payload_size = 32u;
    r.payload_crc32 = 0x200u;
    r.issue_epoch = 3u;
    r.run_id = 4u;
    r.timeout_us = 100000u;
    return r;
}

int main(void)
{
    refmem_command_request_t req = make_command();
    refmem_command_request_t decoded;
    uint8_t wire[REFMEM_CMD_WIRE_SIZE];
    refmem_action_registry_t reg;
    refmem_action_dispatcher_t dispatcher;
    refmem_action_entry_t entry;
    refmem_action_request_t action;
    refmem_action_request_t popped;
    refmem_trigger_session_t session;
    refmem_trigger_session_config_t config;
    int rc = 0;

    rc |= expect_u32("route unified",
                     (uint32_t)refmem_control_route_decide(1u, req.target_mask, true),
                     (uint32_t)REFMEM_CONTROL_ROUTE_UNIFIED);
    rc |= expect_u32("encode",
                     (uint32_t)refmem_cmd_codec_encode(&req, wire),
                     (uint32_t)REFMEM_CMD_CODEC_OK);
    (void)memset(&decoded, 0, sizeof(decoded));
    rc |= expect_u32("decode",
                     (uint32_t)refmem_cmd_codec_decode(wire, &decoded),
                     (uint32_t)REFMEM_CMD_CODEC_OK);
    rc |= expect_u32("seq", decoded.command_seq, req.command_seq);

    refmem_action_registry_init(&reg);
    refmem_action_dispatcher_init(&dispatcher);
    (void)memset(&entry, 0, sizeof(entry));
    entry.action_id = 1u;
    entry.command_type = REFMEM_COMMAND_TYPE_START;
    entry.owner_id = 5u;
    entry.required = true;
    entry.version = 1u;
    entry.crc = 0x300u;
    entry.handler = dummy_handler;
    rc |= expect_u32("register",
                     (uint32_t)refmem_action_registry_register(&reg, &entry),
                     (uint32_t)REFMEM_ACTION_OK);
    (void)memset(&action, 0, sizeof(action));
    action.action_id = 1u;
    action.command_seq = decoded.command_seq;
    action.target_mask = decoded.target_mask;
    action.required_mask = decoded.required_mask;
    action.payload_ref = decoded.payload_ref;
    action.payload_size = decoded.payload_size;
    action.payload_crc32 = decoded.payload_crc32;
    rc |= expect_u32("dispatch",
                     (uint32_t)refmem_action_dispatcher_post(
                         &dispatcher, &reg, REFMEM_ACTION_SOURCE_EVENT, 1u, &action),
                     (uint32_t)REFMEM_ACTION_OK);
    rc |= expect_u32("pop",
                     (uint32_t)refmem_action_dispatcher_pop(&dispatcher, &popped),
                     (uint32_t)REFMEM_ACTION_OK);
    rc |= expect_u32("pop seq", popped.command_seq, 42u);

    rc |= expect_u32("local route",
                     (uint32_t)refmem_control_route_decide(1u, 0x02u, false),
                     (uint32_t)REFMEM_CONTROL_ROUTE_LOCAL);
    rc |= expect_u32("remote reject",
                     (uint32_t)refmem_control_route_decide(1u, 0x04u, false),
                     (uint32_t)REFMEM_CONTROL_ROUTE_REJECT_UNIFIED_UNAVAILABLE);

    refmem_trigger_session_init(&session);
    (void)memset(&config, 0, sizeof(config));
    config.session_id = 7u;
    config.run_id = 8u;
    config.source_slot = 0u;
    config.target_mask = 0x06u;
    config.required_mask = 0x02u;
    rc |= expect_u32("session mount",
                     (uint32_t)refmem_trigger_session_mount(&session, &config),
                     (uint32_t)REFMEM_TRIGGER_SESSION_OK);
    rc |= expect_u32("session running", session.state,
                     (uint32_t)REFMEM_TRIGGER_SESSION_RUNNING);
    rc |= expect_u32("session degraded",
                     (uint32_t)refmem_trigger_session_target_failed(&session, 1u),
                     (uint32_t)REFMEM_TRIGGER_SESSION_TARGET_FAILED);

    refmem_sync_command_payload_t sync_payload;
    refmem_command_request_t from_sync;
    refmem_command_slot_t slot;
    refmem_command_snapshot_t snapshot;
    rc |= expect_u32("sync encode",
                     refmem_command_request_to_sync_payload(&req, &sync_payload) ? 0u : 1u,
                     0u);
    rc |= expect_u32("sync decode",
                     refmem_command_request_from_sync_payload(
                         &sync_payload, req.source_node,
                         req.issue_epoch, req.run_id, &from_sync) ? 0u : 1u,
                     0u);
    rc |= expect_u32("sync seq", from_sync.command_seq, req.command_seq);
    rc |= expect_u32("sync target", from_sync.target_mask, req.target_mask);
    rc |= expect_u32("slot init", refmem_command_init(&slot, 0u) ? 0u : 1u, 0u);
    rc |= expect_u32("slot post",
                     refmem_command_try_post(&slot, &from_sync, 1u) ? 0u : 1u,
                     0u);
    rc |= expect_u32("slot take",
                     (uint32_t)refmem_command_try_take(
                         &slot, 1u, from_sync.issue_epoch, from_sync.run_id,
                         from_sync.payload_crc32, 0u),
                     (uint32_t)REFMEM_COMMAND_TAKE_TAKEN);
    rc |= expect_u32("slot ack",
                     refmem_command_ack(&slot, 1u, 0u) ? 0u : 1u, 0u);
    rc |= expect_u32("slot snapshot",
                     refmem_command_get_snapshot(&slot, &snapshot) ? 0u : 1u,
                     0u);
    rc |= expect_u32("ack bit", (snapshot.ack_flags >> 1u) & 1u, 1u);

    if (rc == 0) {
        (void)printf("refmem_unified_control_host tests passed\n");
    }
    return rc;
}
