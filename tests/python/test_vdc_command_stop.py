"""Execute retained RefMem commands through the real manager and Domain.

STOP/ARM, FIFO admission, guarded retention, time mapping and Domain DCO
updates are production code. Adapter callbacks provide synthetic hardware
acknowledgements/clock observations; no board or physical DCO is exercised.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess

import pytest

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import ROOT, function_body


def build_stop_executable(directory, production_root=ROOT):
    """Build the current admission contract; frozen before C/exe are separate evidence."""
    def production(path):
        candidate = production_root / path
        return candidate if candidate.is_file() else ROOT / path

    refmem = production("components/distributed_refmem/src/distributed_refmem.c").read_text(encoding="utf-8")
    manager = production("components/vdc_dpll_manager/src/vdc_dpll_manager.c").read_text(encoding="utf-8")
    state = re.search(r"typedef struct \{[^}]*\} distributed_refmem_tdma_flight_sync_t;", refmem, re.S)
    assert state
    node_load_state = re.search(r"typedef struct \{[^}]*\} distributed_refmem_node_load_auto_sync_t;", refmem, re.S)
    assert node_load_state
    receiver_header = production("components/distributed_refmem/inc/refmem_sync.h").read_text(encoding="utf-8")
    receiver_api = ("refmem_sync_vdc_receive_admitted_frame"
                    if "refmem_sync_vdc_receive_admitted_frame" in receiver_header
                    else "refmem_sync_vdc_receive_frame")
    capture_defines = "\n".join(re.findall(
        r"^#define VDC_DPLL_MANAGER_DPLL_CAPTURE_KIND_FOLLOWER_\w+\s+\d+u", manager, re.M))
    harness = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#define DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED 1
#include "refmem_sync.h"
#include "refmem_application_model.h"
#include "refmem_realtime_tdma.h"
#include "vdc_dpll_manager.h"
#include "vdc_time_mapping.h"
#include "tdma_process_image_layout.h"
#include "pota_types.h"
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_PAYLOAD_SIZE TDMA_FLIGHT_SHORT_PAYLOAD_SIZE
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE TDMA_FLIGHT_SHORT_SLOT_SIZE
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT TDMA_FLIGHT_SHORT_SLOT_COUNT
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_MAGIC TDMA_FLIGHT_MAILBOX_MAGIC
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_VERSION TDMA_FLIGHT_MAILBOX_VERSION
#define DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_EPOCH 1u
#define DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_RUN 1u
#define DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_QUEUE_COUNT 8u
#define DISTRIBUTED_REFMEM_AUTO_INTENT_NONE 0u
#define DISTRIBUTED_REFMEM_AUTO_INTENT_TX_NODE_LOAD 1u
#define DISTRIBUTED_REFMEM_AUTO_INTENT_RX_WINDOW 2u
static tdma_service_service_t owner;
static vdc_domain_context_t s_vdc_domain;
static refmem_sync_vdc_context_t s_vdc_command_context;
static bool s_vdc_command_context_initialized, s_vdc_command_context_ready;
static uint8_t s_vdc_command_context_local_slot;
static uint32_t s_vdc_command_context_epoch_id, s_vdc_command_context_run_id;
static uint32_t s_vdc_command_context_schedule_epoch;
static uint32_t s_vdc_follower_last_applied_seq, s_vdc_follower_last_generation;
static uint32_t s_vdc_follower_last_epoch_id, s_vdc_follower_last_run_id;
static uint32_t s_vdc_follower_capture_kind_hint;
static uint64_t now_ns = 11000;
static uint32_t starts, stops;
static refmem_realtime_tdma_service_t s_refmem_realtime_tdma;
static refmem_sync_context_t s_refmem_sync_context;
static uint8_t command_frame[REFMEM_SYNC_FRAME_HEADER_SIZE + sizeof(refmem_sync_vdc_command_payload_t)];
static size_t command_frame_size;
static uint32_t node_frame_reads;
/* Only the completed-frame supplier is a stub; COMMAND classification,
 * dispatch and admission below execute their complete production bodies. */
bool refmem_realtime_tdma_get_result_frame_for_payload_class(
    const refmem_realtime_tdma_service_t *service, uint32_t payload_class,
    uint8_t *frame, size_t frame_capacity, size_t *frame_size)
{
    assert(service == &s_refmem_realtime_tdma);
    assert(payload_class == REFMEM_REALTIME_TDMA_PAYLOAD_REFMEM_DELTA);
    assert(frame_capacity >= command_frame_size);
    memcpy(frame, command_frame, command_frame_size);
    *frame_size = command_frame_size; ++node_frame_reads;
    return true;
}
static void distributed_refmem_node_load_auto_pop_front(void)
{ assert(!"unexpected node-load TX branch"); }
static bool distributed_refmem_apply_node_load_sync_payload_internal(const uint8_t *payload, uint16_t size)
{ (void)payload; (void)size; assert(!"unexpected node configuration application"); return false; }
static uint32_t copy_action, copy_interleavings;
static void interrupt_command_copy(void);
/* Only refmem_sync.c redirects memcpy; invoke real Core0 STOP/ARM/binding
 * APIs during its guarded read without adding a production injection hook. */
void *stop_test_memcpy(void *destination, const void *source, size_t size)
{
    unsigned char *dst = destination;
    const unsigned char *src = source;
    size_t copied = 0;
    if (copy_action != 0 && source == &s_vdc_command_context.vdc_command[1] &&
        size == sizeof(refmem_sync_vdc_command_snapshot_t)) {
        for (; copied < size / 2; ++copied) dst[copied] = src[copied];
        interrupt_command_copy();
        ++copy_interleavings;
    }
    for (; copied < size; ++copied) dst[copied] = src[copied];
    return destination;
}
static tdma_service_service_t *tdma_runtime_owner_get(void) { return &owner; }
static bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *snapshot)
{ return tdma_ring_runtime_get_clock_snapshot(&owner.ring_runtime, snapshot); }
static uint64_t vdc_dpll_manager_now_ns(void) { return now_ns; }
''' + state.group(0) + "\nstatic distributed_refmem_tdma_flight_sync_t s_tdma_flight_sync;\n"
    harness += node_load_state.group(0) + "\nstatic distributed_refmem_node_load_auto_sync_t s_node_load_auto_sync;\n"
    harness += f"#define STOP_TEST_RECEIVE {receiver_api}\n"
    for path, names in (
        ("middleware/portable_ota_port/src/portable_ota_core_port.c",
         ("portable_ota_port_crc32_update", "portable_ota_port_crc32_compute")),
        ("components/ota_manager/src/ota_crc32.c", ("ota_crc32_update", "ota_crc32_compute")),
    ):
        source = production(path).read_text(encoding="utf-8")
        harness += "\n" + "\n".join(ingress_definition(source, name) for name in names)
    # The refresh owns retained invalidation; the getter and consume boundary
    # are copied with their actual signatures/bodies, without a Domain stub.
    for name in (
        "distributed_refmem_next_nonzero_sequence", "distributed_refmem_put_le16",
        "distributed_refmem_put_le32", "distributed_refmem_get_le16",
        "distributed_refmem_get_i16", "distributed_refmem_get_le32",
        "distributed_refmem_tdma_reset_resident_command_state",
        "distributed_refmem_refresh_vdc_command_context_from_snapshot",
        "distributed_refmem_tdma_flight_expand_compact_delta",
        "distributed_refmem_tdma_flight_parse_vdc_fragment",
        "distributed_refmem_tdma_flight_parse_mailbox",
        "distributed_refmem_node_load_auto_process_completed",
    ):
        harness += "\n" + ingress_definition(refmem, name)
    harness += "\n" + ingress_definition(refmem, "distributed_refmem_get_vdc_follower_command")
    harness += "\n" + capture_defines + "\nstatic void consume(void) {\n"
    harness += function_body(manager, "vdc_dpll_manager_consume_follower_command") + "\n}\n"
    harness += r'''
static bool adapter_start(void *context, const tdma_ring_runtime_config_t *config)
{ (void)context; ++starts; return config->enabled != 0; }
static bool adapter_stop(void *context)
{ (void)context; ++stops; return true; }
static bool adapter_service(void *context, uint64_t time, tdma_ring_adapter_status_t *status)
{
    (void)context; (void)time;
    status->up_configured = status->down_configured = 1;
    status->up_running = status->down_running = 1;
    status->clock_observation.valid = 1;
    status->clock_observation.correlated_frame_evidence = 1;
    status->clock_observation.correlation_flags = TDMA_RING_CLOCK_OBSERVATION_FLAG_CYCLE_PHASE |
        TDMA_RING_CLOCK_OBSERVATION_FLAG_COMMON_TIME;
    status->clock_observation.timestamp_flags = TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED;
    status->clock_observation.local_rx_timestamp_ns = 10000;
    status->clock_observation.common_effective_time_ns = 50000;
    return true;
}
static bool try_refresh(void)
{
    vdc_dpll_manager_refmem_snapshot_t snapshot = {0};
    snapshot.schedule = s_vdc_domain.schedule;
    snapshot.clock_epoch_id = s_vdc_domain.clock.epoch_id;
    snapshot.clock_run_id = s_vdc_domain.clock.run_id;
    snapshot.control_profile = s_vdc_domain.control.profile;
    s_vdc_command_context_ready =
        distributed_refmem_refresh_vdc_command_context_from_snapshot(&snapshot);
    return s_vdc_command_context_ready;
}
static void refresh(void) { assert(try_refresh()); }
static void ordinary_mailbox(void)
{
    const uint32_t sequence = s_tdma_flight_sync.rx_accept_count + 1;
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE] = {0};
    distributed_refmem_put_le16(mailbox, TDMA_FLIGHT_MAILBOX_MAGIC);
    mailbox[2] = TDMA_FLIGHT_MAILBOX_VERSION;
    mailbox[3] = TDMA_PROCESS_IMAGE_MESSAGE_CLASS;
    mailbox[4] = 1; mailbox[5] = 1;
    distributed_refmem_put_le16(&mailbox[6], (uint16_t)sequence);
    distributed_refmem_put_le32(&mailbox[TDMA_PROCESS_IMAGE_REFMEM_GENERATION_OFFSET], sequence);
    distributed_refmem_put_le16(&mailbox[TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET], 1);
    distributed_refmem_put_le32(&mailbox[TDMA_PROCESS_IMAGE_REFMEM_VALUE_OFFSET], 1000 + sequence);
    distributed_refmem_put_le16(&mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET],
        tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET));
    distributed_refmem_tdma_flight_parse_mailbox(mailbox, sizeof(mailbox));
    assert(s_tdma_flight_sync.rx_accept_count == sequence);
    assert(s_tdma_flight_sync.context.mirror[1].visible);
    assert(s_tdma_flight_sync.context.mirror[1].value_u32 == 1000 + sequence);
}
static void finish_arm(void)
{
    tdma_ring_runtime_service(&owner.ring_runtime);
    tdma_service_core0_lifecycle_service(&owner);
    assert(owner.ring_runtime.config_seq == owner.ring_runtime.applied_config_seq);
    assert(owner.ring_runtime.enabled && owner.ring_runtime.adapter_started);
    assert(tdma_service_ring_start(&owner));
    tdma_ring_runtime_service(&owner.ring_runtime);
}
static void arm(void)
{
    assert(tdma_service_ring_arm(&owner));
    finish_arm();
    tdma_ring_clock_snapshot_t snapshot;
    assert(tdma_runtime_owner_get_ring_clock_snapshot(&snapshot));
    uint64_t common;
    assert(vdc_time_mapping_map_local_to_common_time(&snapshot,
        s_vdc_domain.schedule.schedule_crc32, now_ns, &common));
    assert(common == 50000 + now_ns - 10000);
}
static void interrupt_command_copy(void)
{
    const uint32_t action = copy_action;
    copy_action = 0;
    if (action == 3) {
        /* Model a receiver-owner rebind inside the actual reader memcpy. */
        assert(refmem_sync_vdc_set_consumer_binding(&s_vdc_command_context,
            s_vdc_command_context.consumer_generation + 1,
            s_vdc_command_context.consumer_ring_config_seq));
        return;
    }
    assert(tdma_service_ring_stop(&owner));
    if (action == 2) {
        tdma_ring_runtime_service(&owner.ring_runtime);
        tdma_service_core0_lifecycle_service(&owner);
        arm();
    }
}
static void fixture(void)
{
    assert(vdc_domain_init(&s_vdc_domain));
    vdc_domain_set_ready(&s_vdc_domain, true);
    vdc_dpll_control_profile_t control = s_vdc_domain.control.profile;
    control.mode = VDC_DPLL_CONTROL_MODE_FOLLOWER;
    control.follow_master_slot_id = 1;
    assert(vdc_domain_set_dpll_control_profile(&s_vdc_domain, &control));
    assert(s_vdc_domain.schedule.local_slot_id == 0);
    vdc_clock_model_t clock = s_vdc_domain.clock;
    clock.epoch_id = 7; clock.run_id = 8;
    clock.servo_profile_crc32 = s_vdc_domain.servo.servo_profile_crc32;
    assert(vdc_domain_publish_clock_model(&s_vdc_domain, &clock));
    assert(tdma_service_init(&owner));
    assert(refmem_sync_delta_init(&s_tdma_flight_sync.context, 0, 1, 1));
    static const tdma_ring_adapter_ops_t operations = {
        .start = adapter_start, .stop = adapter_stop, .service = adapter_service};
    assert(tdma_service_bind_ring_adapter(&owner, &operations, NULL));
    owner.ring_staged_config = (tdma_ring_runtime_config_t){
        .enabled = 1, .node_count = s_vdc_domain.schedule.ring_binding.node_count,
        .local_slot_id = 0, .reference_slot_id = 1, .up_group_id = 1, .down_group_id = 2,
        .flags = TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN, .ring_profile_crc32 = 1,
        .schedule_crc32 = s_vdc_domain.schedule.schedule_crc32, .operating_profile_crc32 = 3,
        .baud_hz = 10000000, .cycle_period_ns = 2000, .feedback_timeout_ns = 8000,
        .tx_dma_channel_id = 4, .rx_dma_channel_id = 5};
    arm(); refresh();
}
static void build_command(uint32_t sequence, uint64_t effective)
{
    refmem_sync_vdc_command_payload_t command = {0};
    command.version = REFMEM_SYNC_VDC_COMMAND_VERSION;
    command.source_slot = 1; command.target_slot = 0;
    command.control_generation = 77; command.command_seq = sequence;
    command.schedule_crc32 = s_vdc_domain.schedule.schedule_crc32;
    command.epoch_id = s_vdc_domain.clock.epoch_id; command.run_id = s_vdc_domain.clock.run_id;
    command.effective_vdc_time_ns = effective; command.period_adjust_ppb = 123;
    command.phase_offset_ns = -45; command.lock_state = VDC_DOMAIN_LOCK_FREQ_LOCK;
    command.quality = VDC_DOMAIN_HEALTH_HEALTHY;
    command.payload_crc32 = refmem_sync_vdc_command_payload_crc32(&command);
    refmem_sync_frame_header_t header;
    assert(refmem_sync_frame_header_init(&header, REFMEM_SYNC_FRAME_COMMAND,
        0, 1, 1, command.epoch_id, command.run_id, sequence, 0, 0, &command, sizeof(command)));
    assert(refmem_sync_frame_encode(&header, &command, sizeof(command), command_frame,
        sizeof(command_frame), &command_frame_size));
}
static void receive_command(uint32_t sequence, uint64_t effective)
{
    build_command(sequence, effective);
    assert(STOP_TEST_RECEIVE(&s_vdc_command_context, command_frame, command_frame_size, NULL) ==
        REFMEM_SYNC_RX_ACCEPTED);
}
static void receive_node_load_command(uint32_t sequence, uint64_t effective)
{
    build_command(sequence, effective);
    const uint32_t completion = s_node_load_auto_sync.last_processed_completed_seq + 1;
    s_node_load_auto_sync.active_intent = DISTRIBUTED_REFMEM_AUTO_INTENT_RX_WINDOW;
    s_node_load_auto_sync.active_intent_seq = completion;
    refmem_realtime_tdma_snapshot_t completed = {0};
    completed.completed_seq = completion;
    completed.last_result = REFMEM_REALTIME_TDMA_RESULT_FRAME_READY;
    distributed_refmem_node_load_auto_process_completed(&completed);
    assert(s_node_load_auto_sync.active_intent == DISTRIBUTED_REFMEM_AUTO_INTENT_NONE);
    assert(s_node_load_auto_sync.last_processed_completed_seq == completion);
}
int main(int argc, char **argv)
{
    assert(argc == 2); fixture();
    if (!strcmp(argv[1], "closed_large_seq") || !strcmp(argv[1], "node_load_closed")) {
        const bool node_load = !strcmp(argv[1], "node_load_closed");
        receive_command(41, 52000);
        assert(tdma_service_ring_stop(&owner)); refresh();
        assert(s_vdc_command_context.consumer_ring_config_seq == 0);
        const refmem_sync_vdc_context_t before = s_vdc_command_context;
        if (node_load) {
            receive_node_load_command(1000000, 60000);
            assert(node_frame_reads == 1);
            assert(s_node_load_auto_sync.last_rx_result == REFMEM_SYNC_RX_COMMAND_INVALID);
            assert(s_node_load_auto_sync.failed_apply_count == 1);
        } else {
            build_command(1000000, 60000);
            assert(STOP_TEST_RECEIVE(&s_vdc_command_context, command_frame, command_frame_size, NULL) ==
                REFMEM_SYNC_RX_COMMAND_INVALID);
        }
        assert(memcmp(&s_vdc_command_context, &before, sizeof(before)) == 0);
        assert(!before.vdc_command[1].valid && before.vdc_command[1].command_seq == 41);
        ordinary_mailbox(); consume();
        assert(!s_vdc_domain.control.follower_apply_count && !s_vdc_follower_last_applied_seq);
        tdma_ring_runtime_service(&owner.ring_runtime);
        tdma_service_core0_lifecycle_service(&owner);
        arm(); refresh();
        if (node_load) {
            receive_node_load_command(42, 52000);
            assert(node_frame_reads == 2);
            assert(s_node_load_auto_sync.last_rx_result == REFMEM_SYNC_RX_ACCEPTED);
            assert(s_node_load_auto_sync.failed_apply_count == 1);
        } else receive_command(42, 52000);
        now_ns = 12000; consume();
        assert(s_vdc_domain.control.follower_apply_count == 1);
        assert(s_vdc_domain.control.last_follower_command_seq == 42);
        assert(s_vdc_command_context.vdc_command[1].command_seq == 42);
        puts("closed binding rejects large command without changing receiver; seq42 and real Domain recover");
        return 0;
    }
    if (!strcmp(argv[1], "busy_config") || !strcmp(argv[1], "busy_result")) {
        receive_command(41, 52000); consume();
        assert(s_vdc_domain.control.follower_apply_count == 0);
        const refmem_sync_vdc_context_t before = s_vdc_command_context;
        const distributed_refmem_tdma_flight_sync_t before_sync = s_tdma_flight_sync;
        const uint32_t admission = owner.flight_fifo.rx_admission_epoch;
        volatile uint32_t *guard = !strcmp(argv[1], "busy_config")
            ? &owner.ring_runtime.config_guard : &owner.ring_runtime.result_guard;
        ++*guard;
        assert(!try_refresh());
        consume();
        assert(s_vdc_follower_last_applied_seq == 0);
        assert(memcmp(&s_vdc_command_context, &before, sizeof(before)) == 0);
        assert(memcmp(&s_tdma_flight_sync, &before_sync, sizeof(before_sync)) == 0);
        assert(owner.flight_fifo.rx_admission_epoch == admission);
        assert(s_vdc_domain.control.follower_apply_count == 0);
        ++*guard;
        refresh();
        assert(memcmp(&s_vdc_command_context, &before, sizeof(before)) == 0);
        assert(owner.flight_fifo.rx_admission_epoch == admission);
        now_ns = 12000; consume();
        assert(s_vdc_domain.control.follower_apply_count == 1);
        assert(s_vdc_domain.control.last_follower_command_seq == 41);
        receive_command(42, 53000); now_ns = 13000; consume();
        assert(s_vdc_domain.control.follower_apply_count == 2);
        assert(s_vdc_domain.control.last_follower_command_seq == 42);
        puts("busy ring snapshot preserves pending command/admission; same binding and fresh command apply");
        return 0;
    }
    if (!strcmp(argv[1], "binding_copy_old") || !strcmp(argv[1], "binding_copy_new")) {
        receive_command(41, 52000);
        const uint32_t old_role = s_vdc_command_context.consumer_generation;
        const uint32_t ring_sequence = s_vdc_command_context.consumer_ring_config_seq;
        const bool read_new = !strcmp(argv[1], "binding_copy_new");
        refmem_sync_vdc_command_snapshot_t copied;
        copy_action = 3;
        const bool accepted = refmem_sync_vdc_copy_command_for_binding(&s_vdc_command_context,
            1, read_new ? old_role + 1 : old_role, ring_sequence, &copied);
        assert(copy_interleavings == 1 && !copy_action);
        assert(accepted == read_new);
        /* A failed copy is unspecified; only the successful new-binding
         * read may inspect output, and it must see invalidated data. */
        if (accepted) assert(!copied.valid && copied.command_seq == 41);
        assert(refmem_sync_vdc_copy_command_for_binding(&s_vdc_command_context,
            1, old_role + 1, ring_sequence, &copied));
        assert(!copied.valid && copied.command_seq == 41 && copied.control_generation == 77);
        assert(!refmem_sync_vdc_copy_command_for_binding(&s_vdc_command_context,
            1, 0, ring_sequence, &copied));
        assert(!refmem_sync_vdc_copy_command_for_binding(&s_vdc_command_context,
            1, old_role, ring_sequence, &copied));
        assert(!refmem_sync_vdc_copy_command_for_binding(&s_vdc_command_context,
            1, old_role + 1, 0, &copied));
        assert(!refmem_sync_vdc_copy_command_for_binding(&s_vdc_command_context,
            1, old_role + 1, ring_sequence + 1, &copied));
        assert(refmem_sync_vdc_set_consumer_binding(&s_vdc_command_context, old_role + 1, 0));
        assert(!refmem_sync_vdc_copy_command_for_binding(&s_vdc_command_context,
            1, old_role + 1, ring_sequence, &copied));
        assert(!refmem_sync_vdc_copy_command_for_binding(&s_vdc_command_context,
            1, old_role + 1, 0, &copied));
        assert(s_vdc_command_context.vdc_command[1].command_seq == 41);
        ordinary_mailbox();
        /* Restore the receiver binding from the active real Domain/ring. */
        refresh();
        assert(s_vdc_command_context.consumer_generation == old_role);
        assert(s_vdc_command_context.consumer_ring_config_seq == ring_sequence);
        receive_command(42, 52000); now_ns = 12000; consume();
        assert(s_vdc_domain.control.follower_apply_count == 1);
        assert(s_vdc_domain.control.last_follower_command_seq == 42);
        assert(s_vdc_domain.dco.period_adjust_ppb == 123 && s_vdc_domain.dco.phase_offset_ns == -45);
        puts("guarded rebind cannot return old valid command; invalid identities reject and fresh command recovers");
        return 0;
    }
    if (!strcmp(argv[1], "copy_stop") || !strcmp(argv[1], "copy_rearm") ||
        !strcmp(argv[1], "copy_stop_wrongtarget")) {
        const bool wrong_target = !strcmp(argv[1], "copy_stop_wrongtarget");
        const uint32_t initial_ring = owner.ring_runtime.config_seq;
        const uint32_t initial_dco = s_vdc_domain.dco.dco_update_seq;
        receive_command(41, 51000);
        /* An adversarial retained metadata fixture exercises the manager's
         * secondary target defense; the actual receiver validated the frame. */
        if (wrong_target) s_vdc_command_context.vdc_command[1].target_slot = 2;
        copy_action = !strcmp(argv[1], "copy_rearm") ? 2u : 1u;
        consume();
        assert(copy_interleavings == 1 && !copy_action);
        assert(s_vdc_domain.control.follower_apply_count == 0);
        assert(s_vdc_domain.dco.dco_update_seq == initial_dco);
        assert(s_vdc_follower_last_applied_seq == 0);
        /* Core0 has not refreshed: this specifically checks the second ring
         * snapshot after a successful copy under the old binding. */
        assert(s_vdc_command_context.consumer_ring_config_seq == initial_ring);
        assert(s_vdc_command_context.vdc_command[1].valid);
        if (strcmp(argv[1], "copy_rearm")) {
            tdma_ring_runtime_service(&owner.ring_runtime);
            tdma_service_core0_lifecycle_service(&owner);
            arm();
        }
        refresh();
        assert(!s_vdc_command_context.vdc_command[1].valid);
        ordinary_mailbox();
        receive_command(42, 51000); consume();
        assert(s_vdc_domain.control.follower_apply_count == 1);
        assert(s_vdc_domain.control.last_follower_command_seq == 42);
        assert(s_vdc_domain.dco.dco_update_seq == initial_dco + 1);
        puts("mid-copy STOP/config change rejected without consuming sequence; fresh Domain apply succeeds");
        return 0;
    }
    const bool no_stop = !strcmp(argv[1], "no_stop");
    const bool already_applied = !strcmp(argv[1], "already_applied");
    const bool missed_stop = !strcmp(argv[1], "missed_stop");
    const bool unacked = !strcmp(argv[1], "arm_unacked");
    const bool wrap = !strcmp(argv[1], "config_wrap");
    if (wrap) {
        /* Place the otherwise unchanged live host fixture near rollover;
         * every following publication is a real service configuration. */
        owner.ring_runtime.config_seq = UINT32_MAX - 1;
        owner.ring_runtime.applied_config_seq = UINT32_MAX - 1;
        owner.ring_runtime.adapter_config_seq = UINT32_MAX - 1;
        assert(tdma_service_configure_ring_runtime(&owner, &owner.ring_staged_config));
        finish_arm(); refresh();
        assert(owner.ring_runtime.config_seq == UINT32_MAX);
        assert(s_vdc_command_context.consumer_ring_config_seq == UINT32_MAX);
    }
    const uint32_t epoch = s_vdc_domain.clock.epoch_id;
    const uint32_t run = s_vdc_domain.clock.run_id;
    const uint32_t generation = s_vdc_domain.control.profile.generation;
    const uint32_t ring_config_before = owner.ring_runtime.config_seq;
    const uint32_t dco_sequence_before = s_vdc_domain.dco.dco_update_seq;
    receive_command(41, already_applied ? 51000 : 52000);
    consume();
    ordinary_mailbox();
    assert(s_vdc_domain.control.follower_apply_count == (already_applied ? 1u : 0u));
    assert(s_vdc_domain.control.last_follower_command_seq == (already_applied ? 41u : 0u));
    if (!no_stop) {
        assert(tdma_service_ring_stop(&owner));
        if (!missed_stop) {
            refresh();
            assert(s_vdc_command_context.consumer_ring_config_seq == 0);
            assert(!s_vdc_command_context.vdc_command[1].valid);
            assert(s_vdc_command_context.vdc_command[1].command_seq == 41);
            refmem_sync_vdc_command_snapshot_t rejected;
            assert(!refmem_sync_vdc_copy_command_for_binding(&s_vdc_command_context,
                1, generation, 0, &rejected));
            assert(!refmem_sync_vdc_copy_command_for_binding(&s_vdc_command_context,
                1, generation, ring_config_before, &rejected));
        }
        ordinary_mailbox();
        consume();
        assert(s_vdc_domain.control.follower_apply_count == (already_applied ? 1u : 0u));
        tdma_ring_runtime_service(&owner.ring_runtime);
        tdma_service_core0_lifecycle_service(&owner);
        assert(!owner.ring_runtime.enabled && !owner.ring_runtime.adapter_started);
        assert(owner.ring_runtime.config_seq == owner.ring_runtime.applied_config_seq);
        if (!missed_stop) refresh();
        if (wrap) assert(owner.ring_runtime.config_seq == 0);
        if (unacked) {
            assert(tdma_service_ring_arm(&owner));
            assert(owner.ring_runtime.config_seq != owner.ring_runtime.applied_config_seq);
            now_ns = 12000;
            refresh(); consume(); ordinary_mailbox();
            assert(s_vdc_command_context.consumer_ring_config_seq == 0);
            assert(s_vdc_domain.control.follower_apply_count == 0);
            assert(s_vdc_follower_last_applied_seq == 0);
            finish_arm();
        } else arm();
        assert(owner.ring_runtime.config_seq != ring_config_before);
        assert(starts == (wrap ? 3u : 2u) && stops >= 1);
        if (wrap) assert(owner.ring_runtime.config_seq == 1);
    }
    refresh();
    assert(s_vdc_command_context.consumer_ring_config_seq == owner.ring_runtime.config_seq);
    assert(s_vdc_domain.clock.epoch_id == epoch && s_vdc_domain.clock.run_id == run);
    assert(s_vdc_domain.control.profile.generation == generation);
    now_ns = 12000; consume();
    const uint32_t expected = no_stop || already_applied ? 1u : 0u;
    printf("%s: config=%u->%u VDCepoch=%u run=%u role=%u actual_Domain_apply=%u seq=%u DCO_delta=%u expected=%u\n",
        argv[1], ring_config_before, owner.ring_runtime.config_seq, epoch, run, generation,
        s_vdc_domain.control.follower_apply_count, s_vdc_domain.control.last_follower_command_seq,
        s_vdc_domain.dco.dco_update_seq - dco_sequence_before, expected);
    fflush(stdout);
    assert(s_vdc_domain.control.follower_apply_count == expected);
    assert(s_vdc_domain.dco.dco_update_seq - dco_sequence_before == expected);
    consume();
    assert(s_vdc_domain.control.follower_apply_count == expected);
    if (!no_stop) {
        assert(!s_vdc_command_context.vdc_command[1].valid);
        assert(s_vdc_command_context.vdc_command[1].command_seq == 41);
        receive_command(42, 52000); consume();
        assert(s_vdc_domain.control.follower_apply_count == expected + 1);
        assert(s_vdc_domain.control.last_follower_command_seq == 42);
        assert(s_vdc_domain.dco.dco_update_seq == dco_sequence_before + expected + 1);
        ordinary_mailbox();
    }
    return 0;
}
'''
    directory.mkdir(parents=True, exist_ok=True)
    harness_path = directory / "command_stop.c"
    harness_path.write_text(harness, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler and Path("D:/Microsoft/mingw64/bin/gcc.exe").is_file():
        compiler = "D:/Microsoft/mingw64/bin/gcc.exe"
    assert compiler
    includes = [base / f"components/{component}/inc" for base in (production_root, ROOT)
                for component in ("tdma", "vdc_domain", "vdc_dpll_manager", "distributed_refmem",
                                  "calibration_manager", "ota_manager")]
    includes.append(ROOT / "third_party/portable_ota/include")
    linked = [production(f"components/tdma/src/{name}.c") for name in (
        "tdma_service", "tdma_profile", "tdma_operating_profile", "tdma_payload_registry",
        "tdma_flight_fifo", "tdma_flight_engine", "tdma_process_image_map", "tdma_ring_runtime",
        "tdma_traffic_scheduler", "tdma_service_timing")]
    linked += [production(path) for path in (
        "components/distributed_refmem/src/refmem_sync_frame.c",
        "components/vdc_domain/src/vdc_domain.c", "components/vdc_domain/src/vdc_timestamp.c",
        "components/vdc_dpll_manager/src/vdc_time_mapping.c", "third_party/portable_ota/src/pota_crc32.c")]
    exe = directory / ("command_stop.exe" if os.name == "nt" else "command_stop")
    common = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
              *[f"-I{path}" for path in includes]]
    receiver_object = directory / "refmem_sync.o"
    compiled_receiver = subprocess.run([
        *common, "-Dmemcpy=stop_test_memcpy", "-c",
        str(production("components/distributed_refmem/src/refmem_sync.c")),
        "-o", str(receiver_object)], capture_output=True, text=True, timeout=60)
    (directory / "compile-receiver.stdout.txt").write_text(compiled_receiver.stdout, encoding="utf-8")
    (directory / "compile-receiver.stderr.txt").write_text(compiled_receiver.stderr, encoding="utf-8")
    assert compiled_receiver.returncode == 0, compiled_receiver.stdout + compiled_receiver.stderr
    command = [*common, str(harness_path), str(receiver_object), *map(str, linked), "-o", str(exe)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    (directory / "compile.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (directory / "compile.stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


@pytest.fixture(scope="module")
def command_stop_executable(tmp_path_factory):
    return build_stop_executable(tmp_path_factory.mktemp("command-stop"))


@pytest.mark.parametrize("scenario", [
    "stop_seen", "missed_stop", "no_stop", "already_applied", "arm_unacked", "config_wrap",
    "copy_stop", "copy_rearm",
    "busy_config", "busy_result", "binding_copy_old", "binding_copy_new",
    "closed_large_seq", "node_load_closed", "copy_stop_wrongtarget",
])
def test_pending_command_does_not_survive_stop(command_stop_executable, scenario):
    result = subprocess.run([str(command_stop_executable), scenario], capture_output=True, text=True, timeout=10)
    (command_stop_executable.parent / f"{scenario}.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (command_stop_executable.parent / f"{scenario}.stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
