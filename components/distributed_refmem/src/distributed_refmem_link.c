#include "distributed_refmem.h"
#include "distributed_refmem_internal.h"

#include <string.h>

#include "drv_flash.h"
#include "osal.h"
#include "board_config.h"
#include "board_identity.h"
#include "project_config.h"

#include "distributed_refmem_vdc_bridge.h"
#include "diagnostics.h"
#include "ota_ao.h"
#if PROJECT_REFMEM_BOOTSTRAP_ENABLED
#include "refmem_bootstrap_hello.h"
#include "refmem_bootstrap_relay.h"
#include "refmem_bootstrap_transport.h"
#include "refmem_cmd_reply.h"
#include "tdma_second_class_parser.h"
#endif
#include "refmem_application_model.h"
#include "refmem_command.h"
#include "refmem_cmd_load.h"
#include "refmem_lease_table.h"
#include "refmem_control_route.h"
#include "refmem_node_control.h"
#include "refmem_param_pool.h"
#include "refmem_cmd_frame.h"
#include "refmem_cmd_reply.h"
#include "tdma_maintenance_runtime.h"
#include "refmem_node_load_sync.h"
#include "refmem_quality.h"
#include "refmem_realtime_tdma.h"
#include "tdma_runtime_owner.h"
#include "refmem_slot_claim.h"
#include "refmem_spi_physical_adapter.h"
#include "refmem_sync.h"
#include "refmem_sync_vdc_feedback.h"
#include "refmem_table_registry.h"
#include "refmem_vector_table.h"
#include "tdma_flight_engine.h"
#include "tdma_process_image_layout.h"
#include "tdma_process_image_map.h"
#include "tdma_profile.h"
#include "trigger_position_mailbox.h"
#include "vdc_dpll_manager.h"

_Static_assert(TRIGGER_POSITION_SLICE_WIRE_SIZE ==
                   TDMA_PROCESS_IMAGE_REFMEM_SIZE + TDMA_PROCESS_IMAGE_VDC_SIZE,
               "A T2 position record spans the freed VDC observation area plus the RefMem region");

/* RAM placement macros live in a header: the split command units need them. */
#include "distributed_refmem_time_critical.h"

#include "distributed_refmem_split_internal.h"
bool distributed_refmem_read_dpll_vector_snapshot(
    distributed_refmem_dpll_vector_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    refmem_dpll_vector_region_t *region = distributed_refmem_dpll_vector_region();
    for (uint32_t attempt = 0u; attempt < 8u; attempt++) {
        const uint32_t begin = __atomic_load_n(&region->seqlock,
                                               __ATOMIC_ACQUIRE);
        if (begin == 0u || (begin & 1u) != 0u) {
            continue;
        }
        refmem_dpll_vector_payload_t copy;
        distributed_refmem_copy_from_volatile(
            &copy,
            (const volatile uint8_t *)&region->payload,
            sizeof(copy));
        const uint32_t end = __atomic_load_n(&region->seqlock,
                                             __ATOMIC_ACQUIRE);
        if (begin != end || (end & 1u) != 0u || copy.stable_sequence != end ||
            !refmem_dpll_vector_payload_validate(&copy)) {
            continue;
        }
        *snapshot = copy;
        return true;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    return false;
}

bool distributed_refmem_mark_init_failed(distributed_refmem_init_stage_t stage)
{
    s_refmem_core_initialized = false;
    s_status.initialized = 0u;
    s_status.init_stage = (uint32_t)stage;
    s_status.init_error = (uint32_t)stage;
    return false;
}

uint32_t distributed_refmem_deadline_us_to_ms(uint32_t deadline_us)
{
    if (deadline_us == 0u) {
        return 1u;
    }
    const uint32_t rounded_ms = (deadline_us + 999u) / 1000u;
    return rounded_ms == 0u ? 1u : rounded_ms;
}

bool distributed_refmem_tdma_transmit(void *context,
                                             const uint8_t *frame,
                                             size_t frame_size,
                                             refmem_spi_physical_role_t role,
                                             uint32_t baud_hz,
                                             const refmem_spi_physical_pin_config_t *pins,
                                             uint32_t deadline_us,
                                             refmem_realtime_tdma_exec_status_t *status)
{
    (void)deadline_us;
    refmem_spi_physical_adapter_t *adapter = (refmem_spi_physical_adapter_t *)context;
    if (adapter == NULL || status == NULL ||
        role != REFMEM_SPI_PHYSICAL_ROLE_MASTER ||
        !refmem_spi_physical_adapter_arm(adapter, role, baud_hz, pins)) {
        if (status != NULL) {
            status->result = REFMEM_REALTIME_TDMA_EXEC_ERROR;
            status->error = REFMEM_REALTIME_TDMA_RESULT_BAD_ARGUMENT;
        }
        return false;
    }

    const bool ok = refmem_spi_physical_adapter_transmit(adapter, frame, frame_size);
    refmem_spi_physical_snapshot_t snapshot;
    (void)refmem_spi_physical_adapter_get_snapshot(adapter, &snapshot);
    status->frame_size = frame_size;
    status->error = snapshot.last_error;
    status->result = ok ? REFMEM_REALTIME_TDMA_EXEC_TX_OK
                        : REFMEM_REALTIME_TDMA_EXEC_ERROR;
    return ok;
}

bool distributed_refmem_tdma_receive(void *context,
                                            uint8_t *frame,
                                            size_t frame_capacity,
                                            refmem_spi_physical_role_t role,
                                            uint32_t baud_hz,
                                            const refmem_spi_physical_pin_config_t *pins,
                                            uint32_t deadline_us,
                                            refmem_realtime_tdma_exec_status_t *status)
{
    refmem_spi_physical_adapter_t *adapter = (refmem_spi_physical_adapter_t *)context;
    if (adapter == NULL || status == NULL || role != REFMEM_SPI_PHYSICAL_ROLE_SLAVE) {
        if (status != NULL) {
            status->result = REFMEM_REALTIME_TDMA_EXEC_ERROR;
            status->error = REFMEM_REALTIME_TDMA_RESULT_BAD_ARGUMENT;
        }
        return false;
    }

    size_t frame_size = 0u;
    refmem_spi_physical_snapshot_t snapshot;
    if (!adapter->rx_capture_active) {
        if (!refmem_spi_physical_adapter_arm(adapter, role, baud_hz, pins) ||
            !refmem_spi_physical_adapter_receive_begin(
                adapter,
                frame_capacity,
                distributed_refmem_deadline_us_to_ms(deadline_us))) {
            (void)refmem_spi_physical_adapter_get_snapshot(adapter, &snapshot);
            status->frame_size = 0u;
            status->error = snapshot.last_error;
            status->result = REFMEM_REALTIME_TDMA_EXEC_ERROR;
            return false;
        }

        (void)refmem_spi_physical_adapter_get_snapshot(adapter, &snapshot);
        status->frame_size = 0u;
        status->error = snapshot.last_error;
        status->result = REFMEM_REALTIME_TDMA_EXEC_PENDING;
        return false;
    }

    const refmem_spi_physical_rx_poll_result_t poll_result =
        refmem_spi_physical_adapter_receive_poll(adapter,
                                                 frame,
                                                 frame_capacity,
                                                 &frame_size);
    (void)refmem_spi_physical_adapter_get_snapshot(adapter, &snapshot);
    status->frame_size = frame_size;
    status->error = snapshot.last_error;
    if (poll_result == REFMEM_SPI_PHYSICAL_RX_POLL_PENDING) {
        status->result = REFMEM_REALTIME_TDMA_EXEC_PENDING;
        return false;
    }
    status->result = poll_result == REFMEM_SPI_PHYSICAL_RX_POLL_DONE
                         ? REFMEM_REALTIME_TDMA_EXEC_RX_OK
                         : REFMEM_REALTIME_TDMA_EXEC_TIMEOUT;
    return poll_result == REFMEM_SPI_PHYSICAL_RX_POLL_DONE;
}

bool distributed_refmem_apply_node_load_sync_payload_internal(
    const uint8_t *payload,
    uint16_t payload_size)
{
    refmem_node_load_entry_t entry;
    if (!refmem_node_load_sync_decode_delta_payload(payload,
                                                   payload_size,
                                                   NULL,
                                                   &entry)) {
        return false;
    }

    return refmem_application_model_stage_scpi_node_config(entry.node_id,
                                                           entry.instance_id,
                                                           entry.role_mask,
                                                           entry.persona_mask,
                                                           entry.enabled,
                                                           entry.required,
                                                           entry.load_order);
}

void distributed_refmem_node_load_auto_process_completed(
    const refmem_realtime_tdma_snapshot_t *snapshot)
{
    if (snapshot == NULL ||
        s_node_load_auto_sync.active_intent == DISTRIBUTED_REFMEM_AUTO_INTENT_NONE ||
        snapshot->completed_seq == s_node_load_auto_sync.last_processed_completed_seq ||
        snapshot->completed_seq < s_node_load_auto_sync.active_intent_seq) {
        return;
    }

    s_node_load_auto_sync.last_processed_completed_seq = snapshot->completed_seq;
    if (s_node_load_auto_sync.active_intent ==
        DISTRIBUTED_REFMEM_AUTO_INTENT_TX_NODE_LOAD) {
        distributed_refmem_node_load_auto_pop_front();
        s_node_load_auto_sync.active_intent = DISTRIBUTED_REFMEM_AUTO_INTENT_NONE;
        s_node_load_auto_sync.active_instance_id = 0u;
        return;
    }

    if (s_node_load_auto_sync.active_intent ==
        DISTRIBUTED_REFMEM_AUTO_INTENT_RX_WINDOW) {
        uint8_t frame[REFMEM_REALTIME_TDMA_FRAME_MAX];
        size_t frame_size = 0u;
        if (snapshot->last_result == REFMEM_REALTIME_TDMA_RESULT_FRAME_READY &&
            refmem_realtime_tdma_get_result_frame_for_payload_class(
                &s_refmem_realtime_tdma,
                REFMEM_REALTIME_TDMA_PAYLOAD_REFMEM_DELTA,
                frame,
                sizeof(frame),
                &frame_size)) {
            refmem_sync_frame_header_t command_header;
            const uint8_t *command_payload = NULL;
            uint16_t command_payload_size = 0u;
            const bool is_vdc_command =
                refmem_sync_frame_validate(frame,
                                           frame_size,
                                           &command_header,
                                           &command_payload,
                                           &command_payload_size) ==
                    REFMEM_SYNC_FRAME_OK &&
                command_header.frame_type ==
                    (uint8_t)REFMEM_SYNC_FRAME_COMMAND &&
                command_payload_size == sizeof(refmem_sync_vdc_command_payload_t);
            refmem_sync_rx_snapshot_t rx;
            const refmem_sync_rx_result_t result = is_vdc_command
                ? refmem_sync_vdc_receive_admitted_frame(&s_vdc_command_context,
                                            frame,
                                            frame_size,
                                            &rx)
                : refmem_sync_receive_frame(&s_refmem_sync_context,
                                            frame,
                                            frame_size,
                                            &rx);
            s_node_load_auto_sync.last_rx_result = result;
            s_node_load_auto_sync.last_frame_type = rx.header.frame_type;
            s_node_load_auto_sync.last_source_slot = rx.source_slot;
            if (result == REFMEM_SYNC_RX_ACCEPTED &&
                rx.header.frame_type == (uint8_t)REFMEM_SYNC_FRAME_DELTA &&
                distributed_refmem_apply_node_load_sync_payload_internal(
                    rx.payload,
                    rx.payload_size)) {
                s_node_load_auto_sync.applied_rx_count++;
                s_node_load_auto_sync.last_error = 0u;
            } else if (result == REFMEM_SYNC_RX_ACCEPTED &&
                       rx.header.frame_type != (uint8_t)REFMEM_SYNC_FRAME_DELTA) {
                s_node_load_auto_sync.last_error = 0u;
            } else {
                s_node_load_auto_sync.failed_apply_count++;
                s_node_load_auto_sync.last_error = 2u;
            }
        }
        s_node_load_auto_sync.active_intent = DISTRIBUTED_REFMEM_AUTO_INTENT_NONE;
        s_node_load_auto_sync.active_instance_id = 0u;
    }
}

bool distributed_refmem_node_load_auto_submit_tx(void)
{
    if (s_node_load_auto_sync.pending_count == 0u) {
        return false;
    }

    refmem_node_load_entry_t entry;
    const uint32_t instance_id = s_node_load_auto_sync.pending_instance[0];
    if (!refmem_application_model_get_staging_node_load_entry(instance_id, &entry)) {
        distributed_refmem_node_load_auto_pop_front();
        s_node_load_auto_sync.failed_apply_count++;
        s_node_load_auto_sync.last_error = 3u;
        return false;
    }

    uint8_t frame[REFMEM_REALTIME_TDMA_FRAME_MAX];
    size_t frame_size = 0u;
    const uint32_t seq32 = s_node_load_auto_sync.next_seq32++;
    if (s_node_load_auto_sync.next_seq32 == 0u) {
        s_node_load_auto_sync.next_seq32 = 1u;
    }

    refmem_application_model_load_snapshot_t load;
    refmem_application_model_get_load_snapshot(&load);
    if (!refmem_node_load_sync_build_delta_frame(&entry,
                                                 s_node_load_auto_sync.local_slot,
                                                 s_node_load_auto_sync.target_mask,
                                                 s_node_load_auto_sync.epoch_id,
                                                 s_node_load_auto_sync.run_id,
                                                 seq32,
                                                 load.load_seq,
                                                 osal_tick_ms(),
                                                 frame,
                                                 sizeof(frame),
                                                 &frame_size)) {
        s_node_load_auto_sync.last_error = 4u;
        return false;
    }

    refmem_realtime_tdma_intent_config_t config = {
        .window_epoch = s_node_load_auto_sync.epoch_id,
        .window_index = seq32,
        .deadline_us = s_node_load_auto_sync.deadline_us,
        .role = REFMEM_SPI_PHYSICAL_ROLE_MASTER,
        .baud_hz = s_node_load_auto_sync.baud_hz,
        .pins = s_node_load_auto_sync.downlink_adapter_pins,
        .payload_class = REFMEM_REALTIME_TDMA_PAYLOAD_REFMEM_DELTA,
        .frame = frame,
        .frame_size = frame_size,
    };
    if (!distributed_refmem_fill_vdc_data_window_plan(&config)) {
        s_node_load_auto_sync.last_error = 12u;
        return false;
    }
    if (!refmem_realtime_tdma_submit_tx(&s_refmem_realtime_tdma, &config)) {
        s_node_load_auto_sync.last_error = 5u;
        return false;
    }

    refmem_realtime_tdma_snapshot_t snapshot;
    (void)refmem_realtime_tdma_get_snapshot_for_payload_class(
        &s_refmem_realtime_tdma,
        REFMEM_REALTIME_TDMA_PAYLOAD_REFMEM_DELTA,
        &snapshot);
    s_node_load_auto_sync.active_intent = DISTRIBUTED_REFMEM_AUTO_INTENT_TX_NODE_LOAD;
    s_node_load_auto_sync.active_instance_id = instance_id;
    s_node_load_auto_sync.active_intent_seq = snapshot.intent_seq;
    s_node_load_auto_sync.submitted_tx_count++;
    s_node_load_auto_sync.last_error = 0u;
    return true;
}

bool distributed_refmem_node_load_auto_submit_rx(void)
{
    const uint32_t seq32 = s_node_load_auto_sync.next_seq32++;
    if (s_node_load_auto_sync.next_seq32 == 0u) {
        s_node_load_auto_sync.next_seq32 = 1u;
    }

    refmem_realtime_tdma_intent_config_t config = {
        .window_epoch = s_node_load_auto_sync.epoch_id,
        .window_index = seq32,
        .deadline_us = s_node_load_auto_sync.deadline_us,
        .role = REFMEM_SPI_PHYSICAL_ROLE_SLAVE,
        .baud_hz = s_node_load_auto_sync.baud_hz,
        .pins = s_node_load_auto_sync.uplink_adapter_pins,
        .payload_class = REFMEM_REALTIME_TDMA_PAYLOAD_REFMEM_DELTA,
        .frame = NULL,
        .frame_size = 0u,
    };
    if (!distributed_refmem_fill_vdc_data_window_plan(&config)) {
        s_node_load_auto_sync.last_error = 13u;
        return false;
    }
    if (!refmem_realtime_tdma_submit_rx(&s_refmem_realtime_tdma, &config)) {
        s_node_load_auto_sync.last_error = 6u;
        return false;
    }

    refmem_realtime_tdma_snapshot_t snapshot;
    (void)refmem_realtime_tdma_get_snapshot_for_payload_class(
        &s_refmem_realtime_tdma,
        REFMEM_REALTIME_TDMA_PAYLOAD_REFMEM_DELTA,
        &snapshot);
    s_node_load_auto_sync.active_intent = DISTRIBUTED_REFMEM_AUTO_INTENT_RX_WINDOW;
    s_node_load_auto_sync.active_instance_id = 0u;
    s_node_load_auto_sync.active_intent_seq = snapshot.intent_seq;
    s_node_load_auto_sync.submitted_rx_count++;
    s_node_load_auto_sync.last_error = 0u;
    return true;
}

void distributed_refmem_node_load_auto_service(void)
{
    if (board_identity_get_no() == 5u || s_node_load_auto_sync.enabled == 0u) {
        return;
    }

    refmem_realtime_tdma_snapshot_t snapshot;
    if (!refmem_realtime_tdma_get_snapshot_for_payload_class(
            &s_refmem_realtime_tdma,
            REFMEM_REALTIME_TDMA_PAYLOAD_REFMEM_DELTA,
            &snapshot)) {
        s_node_load_auto_sync.last_error = 7u;
        return;
    }

    if (!distributed_refmem_refresh_vdc_command_context()) {
        s_node_load_auto_sync.last_error = 8u;
        return;
    }

    distributed_refmem_node_load_auto_process_completed(&snapshot);

    if (distributed_refmem_tdma_busy(&snapshot)) {
        /* The shared scheduler owns the active VDC/node-load intent.  A
         * maintenance request waits for completion and never aborts a
         * command window already admitted by Core1. */
        return;
    }

    if (s_node_load_auto_sync.pending_count != 0u) {
        (void)distributed_refmem_node_load_auto_submit_tx();
        return;
    }

    if (s_node_load_auto_sync.active_intent ==
        DISTRIBUTED_REFMEM_AUTO_INTENT_NONE) {
        (void)distributed_refmem_node_load_auto_submit_rx();
    }
}

/* VDC follower command reception deliberately has its own intent lifecycle.
 * Node-load sync is a maintenance channel and defaults disabled; coupling this
 * receive path to it left followers permanently without a command source. */
void distributed_refmem_vdc_follower_rx_process_completed(
    const refmem_realtime_tdma_snapshot_t *tdma,
    uint32_t expected_source_slot)
{
    if (tdma == NULL || s_vdc_follower_rx.active == 0u ||
        tdma->completed_seq == s_vdc_follower_rx.last_processed_completed_seq ||
        tdma->completed_seq < s_vdc_follower_rx.active_intent_seq) {
        return;
    }

    s_vdc_follower_rx.last_processed_completed_seq = tdma->completed_seq;
    s_vdc_follower_rx.last_result = tdma->last_result;
    if (tdma->last_result == REFMEM_REALTIME_TDMA_RESULT_FRAME_READY) {
        uint8_t frame[REFMEM_REALTIME_TDMA_FRAME_MAX];
        size_t frame_size = 0u;
        if (!refmem_realtime_tdma_get_result_frame_for_payload_class(
                &s_refmem_realtime_tdma,
                REFMEM_REALTIME_TDMA_PAYLOAD_VDC_COMMAND,
                frame,
                sizeof(frame),
                &frame_size)) {
            s_vdc_follower_rx.invalid_count++;
            s_vdc_follower_rx.last_error = 5u;
        } else {
            refmem_sync_rx_snapshot_t rx;
            const refmem_sync_rx_result_t result =
                refmem_sync_vdc_receive_admitted_frame(&s_vdc_command_context,
                                               frame,
                                               frame_size,
                                               &rx);
            s_vdc_follower_rx.frame_ready_count++;
            s_vdc_follower_rx.last_rx_result = result;
            s_vdc_follower_rx.last_source_slot = rx.source_slot;
            if (result == REFMEM_SYNC_RX_ACCEPTED) {
                s_vdc_follower_rx.accepted_count++;
                s_vdc_follower_rx.last_error =
                    rx.source_slot == expected_source_slot ? 0u : 6u;
                if (rx.source_slot == expected_source_slot) {
                    refmem_sync_vdc_command_snapshot_t command;
                    if (refmem_sync_vdc_copy_command(
                            &s_vdc_command_context,
                            (uint8_t)rx.source_slot,
                            &command)) {
                        s_vdc_follower_rx.last_command_seq = command.command_seq;
                    }
                }
            } else {
                s_vdc_follower_rx.invalid_count++;
                s_vdc_follower_rx.last_error = 6u;
            }
        }
    } else if (tdma->last_result == REFMEM_REALTIME_TDMA_RESULT_TIMEOUT) {
        s_vdc_follower_rx.timeout_count++;
        s_vdc_follower_rx.last_error = 7u;
    } else if (tdma->last_result == REFMEM_REALTIME_TDMA_RESULT_WINDOW_MISSED) {
        s_vdc_follower_rx.window_miss_count++;
        s_vdc_follower_rx.last_error = 8u;
    } else {
        s_vdc_follower_rx.invalid_count++;
        s_vdc_follower_rx.last_error = 9u;
    }

    s_vdc_follower_rx.active = 0u;
    s_vdc_follower_rx.active_intent_seq = 0u;
}

void distributed_refmem_vdc_follower_rx_service(void)
{
    if (board_identity_get_no() == 5u) {
        return;
    }

    /* Resident process-image fragments are the product command path.  Do not
     * also submit the legacy standalone VDC window: a late frame from that
     * path could race the resident source/sequence retention and overwrite a
     * newer command.  The standalone service remains available to explicit
     * diagnostics when resident flight sync is disabled. */
    if (s_tdma_flight_sync.enabled != 0u) {
        return;
    }

    vdc_dpll_manager_refmem_snapshot_t vdc;
    refmem_realtime_tdma_snapshot_t tdma;
    if (!vdc_dpll_manager_get_refmem_snapshot(&vdc) ||
        !refmem_realtime_tdma_get_snapshot_for_payload_class(
            &s_refmem_realtime_tdma,
            REFMEM_REALTIME_TDMA_PAYLOAD_VDC_COMMAND,
            &tdma)) {
        s_vdc_follower_rx.last_error = 1u;
        return;
    }
    if (!distributed_refmem_refresh_vdc_command_context_from_snapshot(&vdc)) {
        s_vdc_follower_rx.last_error = 2u;
        return;
    }

    const bool follower = vdc.control_profile.valid == 1u &&
        vdc.control_profile.mode == VDC_DPLL_CONTROL_MODE_FOLLOWER &&
        vdc.control_profile.follow_master_slot_id < REFMEM_SYNC_NODE_COUNT;
    const uint32_t expected_source_slot =
        follower ? vdc.control_profile.follow_master_slot_id : UINT32_MAX;
    distributed_refmem_vdc_follower_rx_process_completed(&tdma,
                                                          expected_source_slot);
    if (!follower || s_vdc_follower_rx.active != 0u ||
        distributed_refmem_tdma_busy(&tdma)) {
        return;
    }

    vdc_tdma_window_plan_t plan;
    vdc_gate_result_t gate;
    if (!vdc_dpll_manager_plan_published_tdma_window(
            &vdc,
            VDC_DOMAIN_WINDOW_REFMEM_DATA,
            VDC_DPLL_MANAGER_PLAN_NOW_NS,
            &plan,
            &gate) ||
        plan.valid == 0u || plan.window_start_ns == 0u ||
        plan.schedule_crc32 != vdc.schedule.schedule_crc32) {
        (void)gate;
        s_vdc_follower_rx.last_error = 3u;
        return;
    }

    const uint32_t window_seq =
        distributed_refmem_next_nonzero_sequence(&s_vdc_follower_rx.next_window_seq);
    const refmem_realtime_tdma_intent_config_t config = {
        .window_epoch = plan.schedule_epoch,
        .window_index = window_seq,
        .deadline_us = 1000000u,
        .role = REFMEM_SPI_PHYSICAL_ROLE_SLAVE,
        .baud_hz = BOARD_REFMEM_SPI_BAUD_HZ,
        .pins = {
            .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
            .csn_pin = REFMEM_SPI_PHYSICAL_PIN_UNUSED,
            .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
            .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
        },
        .payload_class = REFMEM_REALTIME_TDMA_PAYLOAD_VDC_COMMAND,
        .vdc_window_plan_valid = 1u,
        .vdc_window_class = VDC_DOMAIN_WINDOW_REFMEM_DATA,
        .vdc_schedule_crc32 = plan.schedule_crc32,
        .vdc_window_start_ns = plan.window_start_ns,
        .vdc_window_end_ns = plan.window_end_ns,
        .vdc_guard_start_ns = plan.guard_start_ns,
        .vdc_guard_end_ns = plan.guard_end_ns,
        .frame = NULL,
        .frame_size = 0u,
    };
    if (!refmem_realtime_tdma_submit_rx(&s_refmem_realtime_tdma, &config)) {
        s_vdc_follower_rx.submit_reject_count++;
        s_vdc_follower_rx.last_error = 4u;
        return;
    }

    (void)refmem_realtime_tdma_get_snapshot_for_payload_class(
        &s_refmem_realtime_tdma,
        REFMEM_REALTIME_TDMA_PAYLOAD_VDC_COMMAND,
        &tdma);
    s_vdc_follower_rx.active = 1u;
    s_vdc_follower_rx.active_intent_seq = tdma.intent_seq;
    s_vdc_follower_rx.submitted_count++;
    s_vdc_follower_rx.last_error = 0u;
}

void distributed_refmem_refresh_directory_flags_locked(void)
{
    refmem_vector_header_region_t *header = distributed_refmem_header();
    const uint32_t directory_crc32 = refmem_vector_directory_crc(&s_distributed_refmem_table);

    if (refmem_vector_table_validate_directory(&s_distributed_refmem_table)) {
        header->flags |= DISTRIBUTED_REFMEM_FLAG_DIRECTORY_VALID;
    } else {
        header->flags &= ~DISTRIBUTED_REFMEM_FLAG_DIRECTORY_VALID;
    }

    if (header->directory_crc32 == directory_crc32) {
        header->flags |= DISTRIBUTED_REFMEM_FLAG_DIRECTORY_CRC_VALID;
    } else {
        header->flags &= ~DISTRIBUTED_REFMEM_FLAG_DIRECTORY_CRC_VALID;
    }

    if (refmem_application_model_get_snapshot()->valid != 0u) {
        header->flags |= DISTRIBUTED_REFMEM_FLAG_APP_MODEL_VALID;
    } else {
        header->flags &= ~DISTRIBUTED_REFMEM_FLAG_APP_MODEL_VALID;
    }
}

void distributed_refmem_publish_runtime_locked(void)
{
    refmem_vector_header_region_t *header = distributed_refmem_header();
    drv_flash_lockout_status_t flash_status;
    drv_flash_get_lockout_status(&flash_status);

#if PROJECT_USE_MULTICORE
    header->core_count = 2u;
    header->core1_vtor_owner = DISTRIBUTED_REFMEM_OWNER_CORE1;
    header->core1_irq_owner_mask = DISTRIBUTED_REFMEM_IRQ_PIO_MASK |
                                   DISTRIBUTED_REFMEM_IRQ_DMA_MASK |
                                   DISTRIBUTED_REFMEM_IRQ_CAPTURE_MASK |
                                   DISTRIBUTED_REFMEM_IRQ_TIMER_MASK;
    header->ram_resident_required = 1u;
#else
    header->core_count = 1u;
    header->core1_vtor_owner = DISTRIBUTED_REFMEM_OWNER_CORE0;
    header->core1_irq_owner_mask = 0u;
    header->ram_resident_required = 0u;
#endif

    header->core0_vtor_owner = DISTRIBUTED_REFMEM_OWNER_CORE0;
    header->core0_irq_owner_mask = DISTRIBUTED_REFMEM_IRQ_USB_MASK |
                                   DISTRIBUTED_REFMEM_IRQ_STORAGE_MASK |
                                   DISTRIBUTED_REFMEM_IRQ_OTA_MASK |
                                   DISTRIBUTED_REFMEM_IRQ_UI_MASK;
    header->entry_table_owner = DISTRIBUTED_REFMEM_OWNER_SHARED;
    header->flash_lockout_supported = flash_status.core1_lockout_supported ? 1u : 0u;
    header->flash_lockout_online = flash_status.core1_lockout_online ? 1u : 0u;
    header->flash_lockout_requested = flash_status.core1_lockout_requested ? 1u : 0u;
    header->flash_lockout_acknowledged = flash_status.core1_lockout_acknowledged ? 1u : 0u;
    header->core1_park_state = flash_status.park_state;
    header->flash_lockout_last_result = flash_status.last_result;
    header->flash_lockout_last_elapsed_us = flash_status.last_elapsed_us;
    header->flash_lockout_request_seq = flash_status.request_seq;
    header->flash_lockout_ack_seq = flash_status.ack_seq;
    header->flash_lockout_release_seq = flash_status.release_seq;
    header->flash_lockout_timeout_count = flash_status.timeout_count;
    header->flash_lockout_release_timeout_count = flash_status.release_timeout_count;
    header->runtime_protection_flags = 0u;
    if (header->ram_resident_required != 0u) {
        header->runtime_protection_flags |= DISTRIBUTED_REFMEM_PROT_RAM_RESIDENT_REQUIRED;
    }
    if (header->flash_lockout_supported != 0u && header->flash_lockout_online != 0u) {
        header->runtime_protection_flags |= DISTRIBUTED_REFMEM_PROT_FLASH_LOCKOUT_READY;
    }
    if (header->core1_park_state == DRV_FLASH_LOCKOUT_PARK_PARKED) {
        header->runtime_protection_flags |= DISTRIBUTED_REFMEM_PROT_CORE1_PARKED;
    }
    if (header->entry_table_owner == DISTRIBUTED_REFMEM_OWNER_SHARED) {
        header->runtime_protection_flags |= DISTRIBUTED_REFMEM_PROT_ENTRY_OWNER_VALID;
    }
    header->table_owner = REFMEM_VECTOR_TABLE_OWNER;
    header->header_stale = REFMEM_VECTOR_HEADER_STALE;
    distributed_refmem_refresh_directory_flags_locked();
    header->header_crc32 = distributed_refmem_header_crc_locked();
}

bool distributed_refmem_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    refmem_lease_table_init();
    s_refmem_core_initialized = false;
    s_vdc_vector_publish_sequence = 0u;
    s_dpll_vector_publish_sequence = 0u;
    s_tdma_ring_log_enabled = PROJECT_ENABLE_TDMA_RING_LOG ? true : false;
    s_tdma_ring_log_last_ms = 0u;
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_APP_MODEL;
    if (!refmem_application_model_init()) {
        return distributed_refmem_mark_init_failed(
            DISTRIBUTED_REFMEM_INIT_STAGE_APP_MODEL);
    }
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_OWNER;
    tdma_service_service_t *tdma_owner = tdma_runtime_owner_get();
    if (tdma_owner == NULL ||
        !refmem_realtime_tdma_init_shared(&s_refmem_realtime_tdma,
                                          tdma_owner)) {
        return distributed_refmem_mark_init_failed(
            DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_OWNER);
    }
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_COMMAND_SLOT;
    if (!refmem_command_init(&s_refmem_command_slot, 0u)) {
        return distributed_refmem_mark_init_failed(
            DISTRIBUTED_REFMEM_INIT_STAGE_COMMAND_SLOT);
    }
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_NODE_LOAD_AUTO;
    distributed_refmem_node_load_auto_init();
    memset(&s_vdc_follower_rx, 0, sizeof(s_vdc_follower_rx));
    s_vdc_follower_rx.next_window_seq = 1u;
    distributed_refmem_tdma_flight_sync_init();
    /* Non-fatal: without the lane receiver Core0 keeps parsing T2 records. */
    (void)tdma_runtime_owner_set_priority_first_class_rx_sink(
        distributed_refmem_position_priority_rx);
    static const refmem_realtime_tdma_ops_t tdma_ops = {
        .transmit = distributed_refmem_tdma_transmit,
        .receive = distributed_refmem_tdma_receive,
    };
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_OPS;
    if (!refmem_realtime_tdma_bind_ops(&s_refmem_realtime_tdma,
                                       &tdma_ops,
                                       &s_refmem_realtime_spi)) {
        return distributed_refmem_mark_init_failed(
            DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_OPS);
    }
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_PROFILE;
    uint32_t tdma_schedule_crc32 = 0u;
    const tdma_foundation_profile_t *tdma_profile =
        refmem_application_model_get_tdma_foundation_profile();
    if (!distributed_refmem_tdma_profile_activation_ready(
            tdma_profile,
            &tdma_schedule_crc32) ||
        !distributed_refmem_apply_tdma_foundation_profile(
            tdma_profile,
            tdma_schedule_crc32)) {
        return distributed_refmem_mark_init_failed(
            DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_PROFILE);
    }
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_VECTOR_TABLE;

    osal_critical_enter();

    refmem_vector_table_clear(&s_distributed_refmem_table);

    refmem_vector_header_region_t *header = distributed_refmem_header();
    header->magic = REFMEM_VECTOR_MAGIC;
    header->end_magic = REFMEM_VECTOR_END_MAGIC;
    header->layout_version = DISTRIBUTED_REFMEM_LAYOUT_VERSION;
    header->table_size = DISTRIBUTED_REFMEM_TABLE_SIZE;
    header->table_seq = 1u;
    header->local_node_id = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    header->node_count = DISTRIBUTED_REFMEM_NODE_COUNT;
    header->header_size = DISTRIBUTED_REFMEM_HEADER_SIZE;
    header->region_count = REFMEM_VECTOR_REGION_COUNT;
    header->flags = 0u;
    header->table_owner = REFMEM_VECTOR_TABLE_OWNER;
    header->header_stale = REFMEM_VECTOR_HEADER_STALE;
    refmem_vector_table_init_directory(&s_distributed_refmem_table);
    refmem_param_pool_init(s_distributed_refmem_table.tlv,
                           sizeof(s_distributed_refmem_table.tlv));
    header->directory_crc32 = refmem_vector_directory_crc(&s_distributed_refmem_table);
    distributed_refmem_publish_runtime_locked();

    for (uint32_t i = 0u; i < DISTRIBUTED_REFMEM_NODE_COUNT; i++) {
        refmem_vector_node_region_t *node = distributed_refmem_node_region(i);
        node->node_id = i;
        node->state = DISTRIBUTED_REFMEM_NODE_MISSING;
        node->node_type = DISTRIBUTED_REFMEM_NODE_TYPE_BOARD;
    }

    refmem_vector_node_region_t *local_node =
        distributed_refmem_node_region(DISTRIBUTED_REFMEM_LOCAL_NODE_ID);
    local_node->state = DISTRIBUTED_REFMEM_NODE_OK;
    local_node->node_type = DISTRIBUTED_REFMEM_NODE_TYPE_BOARD;
    local_node->slot_version = 1u;
    local_node->last_update_ms = osal_tick_ms();

    s_service_count = 0u;
    s_vdc_vector_source_update_seq = UINT32_MAX;
    s_dpll_vector_source_update_seq = UINT32_MAX;
    s_next_runtime_vector = 0u;
    s_refmem_core_initialized = true;
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_READY;
    s_status.init_error = 0u;
    distributed_refmem_publish_status_locked();

    osal_critical_exit();
    return true;
}


void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_realtime_run_once)(void)
{
    if (!s_refmem_core_initialized) {
        return;
    }

    /* The single TdmaSchedulerAO is advanced by tdma_component_core1_service.
     * RefMem consumes its result here and must not run a second scheduler.
     * Only the already-published, core1-owned VDC snapshot crosses this phase;
     * no SCPI, storage, logging, or scheduler work is allowed here. */
    vdc_dpll_manager_refmem_snapshot_t snapshot;
    const bool snapshot_valid = vdc_dpll_manager_get_refmem_snapshot(&snapshot);
    if (!snapshot_valid) {
        return;
    }

    /* Command retention and identity retirement belong to Core0. Core1
     * consumes a guarded copy and validates its epoch/run at application. */

    const bool master_profile =
        snapshot.control_profile.valid == 1u &&
        snapshot.control_profile.mode == VDC_DPLL_CONTROL_MODE_MASTER;
    if (master_profile && s_tdma_flight_sync.enabled == 0u) {
        /* RefMem only freezes MASTER output fields in this phase.  The TDMA
         * owner later selects the common future window and admits one target
         * to its single scheduler. */
        (void)distributed_refmem_capture_vdc_command_batch(&snapshot);
    } else if (s_vdc_command_batch.active != 0u) {
        distributed_refmem_reset_vdc_command_batch();
    }

    distributed_refmem_publish_runtime_vector_if_pending(
        snapshot.publication_revision);
}

/* Legacy vectors retain wire/evidence fields. The local dedup tokens track
 * the complete publication, including DCO-only commits with unchanged DPLL
 * evidence. Each beat still publishes at most one vector. UINT32_MAX is an
 * unconsumed sentinel: a successfully copied publication guard is even.
 * The VDC owner omits path/observation tables from this realtime copy. */
void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_publish_runtime_vector_if_pending)(
    uint32_t source_update_seq)
{
    /* The lock declaration travels inside the snapshot and moves its revision
     * when it changes, so the revision dedup alone also carries a lock gained
     * or lost (VDC-LOCKPUB-002). */
    const bool vdc_pending =
        source_update_seq != s_vdc_vector_source_update_seq;
    const bool dpll_pending =
        source_update_seq != s_dpll_vector_source_update_seq;
    if (!vdc_pending && !dpll_pending) {
        return;
    }

    vdc_dpll_manager_vector_snapshot_t vector_snapshot;
    if (!vdc_dpll_manager_get_vector_snapshot(&vector_snapshot)) {
        return;
    }
    source_update_seq = vector_snapshot.publication_revision;
    const bool current_vdc_pending =
        source_update_seq != s_vdc_vector_source_update_seq;
    const bool current_dpll_pending =
        source_update_seq != s_dpll_vector_source_update_seq;
    if (!current_vdc_pending && !current_dpll_pending) {
        return;
    }

    const bool publish_vdc = current_vdc_pending &&
        (!current_dpll_pending || s_next_runtime_vector == 0u);
    if (publish_vdc) {
        refmem_vdc_vector_payload_t payload;
        distributed_refmem_fill_vdc_vector_payload(
            &payload,
            &vector_snapshot,
            distributed_refmem_next_publish_sequence(
                &s_vdc_vector_publish_sequence),
            true);
        distributed_refmem_publish_vdc_vector_payload(
            distributed_refmem_vdc_vector_region(), &payload);
        s_vdc_vector_source_update_seq = source_update_seq;
        s_next_runtime_vector = 1u;
        return;
    }

    refmem_dpll_vector_payload_t payload;
    distributed_refmem_fill_dpll_vector_payload(
        &payload,
        &vector_snapshot,
        distributed_refmem_next_publish_sequence(
            &s_dpll_vector_publish_sequence),
        true);
    distributed_refmem_publish_dpll_vector_payload(
        distributed_refmem_dpll_vector_region(), &payload);
    s_dpll_vector_source_update_seq = source_update_seq;
    s_next_runtime_vector = 0u;
}


#if PROJECT_REFMEM_BOOTSTRAP_ENABLED
#define REFMEM_BOOTSTRAP_KIND_COMMAND 0x01u
#define REFMEM_BOOTSTRAP_KIND_REPLY 0x02u
#define REFMEM_BOOTSTRAP_ENVELOPE_SIZE 4u

static uint8_t s_bootstrap_peer_slot;
static uint32_t s_bootstrap_reply_timeouts_seen;

/* Inbound application frame: the relay only knows bytes; the REFMEM binding
 * owns the envelope and the command/reply semantics. */
static bool refmem_bootstrap_frame_handler(const uint8_t *frame,
                                           size_t frame_size)
{
    if (frame == NULL || frame_size < 2u) {
        return false;
    }
    if (frame[0] == REFMEM_BOOTSTRAP_KIND_COMMAND) {
        if (frame_size < REFMEM_BOOTSTRAP_ENVELOPE_SIZE ||
            frame[1] != (uint8_t)TDMA_SECOND_CLASS_TYPE_REFMEM_CMD) {
            return false;
        }
        return distributed_refmem_cmd_frame_receive(
            frame + REFMEM_BOOTSTRAP_ENVELOPE_SIZE,
            frame_size - REFMEM_BOOTSTRAP_ENVELOPE_SIZE);
    }
    if (frame[0] == REFMEM_BOOTSTRAP_KIND_REPLY) {
        if (frame_size < 6u) {
            return false;
        }
        const uint16_t seq16 =
            (uint16_t)((uint16_t)frame[2] | ((uint16_t)frame[3] << 8u));
        distributed_refmem_cmd_reply_store(frame[1], seq16, frame[4], frame[5]);
        return true;
    }
    return false;
}

static bool refmem_bootstrap_submit_frame(const uint8_t *payload,
                                          uint32_t payload_size,
                                          uint8_t type,
                                          uint8_t generation,
                                          uint8_t target_mask)
{
    if (payload == NULL || payload_size == 0u ||
        payload_size > (REFMEM_BOOTSTRAP_RELAY_FRAME_MAX -
                        REFMEM_BOOTSTRAP_ENVELOPE_SIZE)) {
        return false;
    }
    uint8_t frame[REFMEM_BOOTSTRAP_RELAY_FRAME_MAX];
    frame[0] = REFMEM_BOOTSTRAP_KIND_COMMAND;
    frame[1] = type;
    frame[2] = generation;
    frame[3] = target_mask;
    memcpy(frame + REFMEM_BOOTSTRAP_ENVELOPE_SIZE, payload, payload_size);
    return refmem_bootstrap_relay_submit(
        frame, (size_t)payload_size + REFMEM_BOOTSTRAP_ENVELOPE_SIZE,
        osal_tick_ms());
}

static bool refmem_bootstrap_carrier_ready(void)
{
    return refmem_bootstrap_relay_ready();
}

static const refmem_bootstrap_transport_t s_refmem_bootstrap_carrier = {
    .submit = refmem_bootstrap_submit_frame,
    .ready = refmem_bootstrap_carrier_ready,
};

/* A command frame with no reply must not stay POSTED forever: fail the local
 * record closed with the peer timeout bit and the TIMEOUT reason. */
static void refmem_bootstrap_timeout_local_command(void)
{
    const uint32_t local = distributed_refmem_local_slot();
    refmem_command_snapshot_t snapshot;
    uint32_t params[REFMEM_NODE_CONTROL_PARAM_WORDS];
    if (!distributed_refmem_node_control_read_params(
            local, &snapshot, params, NULL, NULL)) {
        return;
    }
    if (snapshot.state != REFMEM_COMMAND_STATE_POSTED &&
        snapshot.state != REFMEM_COMMAND_STATE_BUSY) {
        return;
    }
    (void)distributed_refmem_node_control_update_status(
        local, REFMEM_COMMAND_STATE_TIMED_OUT, snapshot.ack_flags,
        snapshot.nack_flags, snapshot.busy_flags,
        snapshot.timeout_flags | (1u << s_bootstrap_peer_slot),
        REFMEM_COMMAND_REASON_TIMEOUT, local, 0u, snapshot.command_seq);
}

bool distributed_refmem_bootstrap_start(bool responder)
{
    refmem_bootstrap_relay_config_t config;
    memset(&config, 0, sizeof(config));
    const uint32_t local_slot = distributed_refmem_local_slot();
    if (local_slot > 7u) {
        return false;
    }
    config.local_slot = (uint8_t)local_slot;
    /* The P4.5 bench link is point-to-point: the peer is the other slot of a
     * two-node ring.  Multi-link topology is deliberately not modelled yet. */
    config.peer_slot = (uint8_t)(local_slot == 0u ? 1u : 0u);
    config.hello_interval_ms = 1000u;
    config.ack_timeout_ms = 50u;
    config.peer_timeout_ms = 5000u;
    config.reply_guard_ms = 20u;
    config.reply_timeout_ms = 100u;
    config.max_retries = 3u;
    config.responder = responder;
    config.build_probe = refmem_bootstrap_hello_build;
    config.parse_probe = refmem_bootstrap_hello_parse;
    config.frame_handler = refmem_bootstrap_frame_handler;
    if (!refmem_bootstrap_relay_init(&config)) {
        return false;
    }
    if (!refmem_control_transport_register_bootstrap(
            &s_refmem_bootstrap_carrier)) {
        refmem_bootstrap_relay_reset();
        return false;
    }
    s_bootstrap_peer_slot = config.peer_slot;
    s_bootstrap_reply_timeouts_seen =
        refmem_bootstrap_relay_reply_timeout_count();
    return true;
}

void distributed_refmem_bootstrap_stop(void)
{
    refmem_control_transport_unregister_bootstrap();
    refmem_bootstrap_relay_reset();
}
#endif

void distributed_refmem_service(void)
{
    if (!s_refmem_core_initialized) {
        return;
    }
    static bool cmd_load_ready;
    if (!cmd_load_ready) {
        tdma_pio_spi_ring_adapter_t *adapter =
            tdma_runtime_owner_get_ring_adapter();
        if (adapter != NULL) {
            cmd_load_ready = refmem_cmd_load_init(adapter);
        }
    }
    refmem_cmd_load_service();
#if PROJECT_REFMEM_BOOTSTRAP_ENABLED
    /* STOP-state bootstrap relay: inert until SCPI arms it.  It only probes
     * the peer and publishes readiness; command admission stays ring-only
     * until the carrier is explicitly registered. */
    refmem_bootstrap_relay_service(osal_tick_ms());
    /* Ring is the primary control transport: once it carries commands, retire
     * the bootstrap carrier instead of keeping two active paths.  Re-arming
     * after a ring stop stays an explicit operator action. */
    if (refmem_bootstrap_transport_armed() &&
        distributed_refmem_ring_link_ready()) {
        refmem_control_transport_unregister_bootstrap();
        refmem_bootstrap_relay_handover();
        refmem_bootstrap_transport_disarm();
    }
#endif
    distributed_refmem_node_control_service();
#if PROJECT_REFMEM_BOOTSTRAP_ENABLED
    /* Target side: a reply armed by the node-control service must go back over
     * the bootstrap carrier (the ring is not carrying commands here). */
    if (refmem_bootstrap_transport_armed() && refmem_bootstrap_relay_ready()) {
        refmem_cmd_reply_t reply;
        if (distributed_refmem_cmd_reply_tx_get(&reply)) {
            refmem_cmd_reply_wire_t wire;
            if (refmem_cmd_reply_encode(&reply, &wire)) {
                uint8_t frame[6];
                frame[0] = REFMEM_BOOTSTRAP_KIND_REPLY;
                frame[1] = (uint8_t)distributed_refmem_local_slot();
                frame[2] = (uint8_t)(wire.seq16 & 0xFFu);
                frame[3] = (uint8_t)(wire.seq16 >> 8u);
                frame[4] = wire.flags;
                frame[5] = wire.reason;
                if (refmem_bootstrap_relay_submit(frame, sizeof(frame),
                                                  osal_tick_ms())) {
                    distributed_refmem_cmd_reply_note_published();
                }
            }
        }
    }
    /* Source side: a lost reply fails the local record closed. */
    {
        const uint32_t seen = refmem_bootstrap_relay_reply_timeout_count();
        if (seen != s_bootstrap_reply_timeouts_seen) {
            s_bootstrap_reply_timeouts_seen = seen;
            refmem_bootstrap_timeout_local_command();
        }
    }
#endif

    osal_critical_enter();

    refmem_vector_header_region_t *header = distributed_refmem_header();
    refmem_vector_node_region_t *local_node =
        distributed_refmem_node_region(DISTRIBUTED_REFMEM_LOCAL_NODE_ID);

    s_service_count++;
    header->table_seq++;
    distributed_refmem_publish_runtime_locked();
    local_node->heartbeat++;
    local_node->slot_version++;
    local_node->last_update_ms = osal_tick_ms();
    local_node->state = DISTRIBUTED_REFMEM_NODE_OK;

    distributed_refmem_publish_status_locked();

    osal_critical_exit();
    if (ota_ao_is_active()) {
        return;
    }
    /* Asynchronous diagnostics belong to the sole Core0 RefMem task. Never
     * add this copy/checksum to the deterministic Core1 publication quota. */
    distributed_refmem_publish_priority_health_core0();
    /* The sole RefMem task prepares at most one reference/peer outside the
     * critical section and before any RX drain/borrow window. Core1 owns
     * authorization and eventual control, not this cache or arithmetic. */
    vdc_dpll_manager_feedback_prepare_core0();
    s_vdc_command_context_ready = distributed_refmem_refresh_vdc_command_context();
    if (s_vdc_command_context_ready) {
        distributed_refmem_vdc_follower_rx_service();
        distributed_refmem_node_load_auto_service();
    }
    distributed_refmem_tdma_flight_sync_service();
    distributed_refmem_cmd_reply_service();
    distributed_refmem_log_tdma_ring_service();
}

void distributed_refmem_tdma_core1_service(void)
{
    if (!s_refmem_core_initialized) {
        return;
    }
    refmem_realtime_tdma_core1_service(&s_refmem_realtime_tdma);
}

/* Low-frequency read-only TDMA ring maintenance log (P0.5-3 monitoring).
 * The resident ring is owned and driven by the core1 TDMA service; this core0
 * path only mirrors ring state into the LOG system every few seconds for
 * post-hoc diagnosis, and never submits TX/RX intents. */
void distributed_refmem_log_tdma_ring_service(void)
{
    if (ota_ao_is_active() || !s_tdma_ring_log_enabled) {
        return;
    }
    const uint32_t now_ms = osal_tick_ms();
    if (now_ms - s_tdma_ring_log_last_ms < 5000u) {
        return;
    }
    s_tdma_ring_log_last_ms = now_ms;

    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_snapshot(&ring)) {
        return;
    }
    LOG_INFO("tdma_ring",
             "en=%lu up=%lu down=%lu seq=%lu reason=%lu ev=%lu "
             "adapt_start=%lu svc=%lu beacon_tx=%lu beacon_rx=%lu rt_ns=%lu",
             (unsigned long)ring.enabled,
             (unsigned long)ring.up_running,
             (unsigned long)ring.down_running,
             (unsigned long)ring.ring_seq,
             (unsigned long)ring.last_reason,
             (unsigned long)ring.simultaneous_feedback_loop_evidence,
             (unsigned long)ring.adapter_started,
             (unsigned long)ring.adapter_service_count,
             (unsigned long)ring.idle_beacon_tx_count,
             (unsigned long)ring.idle_beacon_rx_count,
             (unsigned long)ring.feedback_round_trip_ns);
}

bool distributed_refmem_get_realtime_tdma(refmem_realtime_tdma_snapshot_t *snapshot)
{
    return refmem_realtime_tdma_get_snapshot(&s_refmem_realtime_tdma, snapshot);
}

void distributed_refmem_get_vdc_follower_rx(
    distributed_refmem_vdc_follower_rx_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    snapshot->active = s_vdc_follower_rx.active;
    snapshot->active_intent_seq = s_vdc_follower_rx.active_intent_seq;
    snapshot->next_window_seq = s_vdc_follower_rx.next_window_seq;
    snapshot->last_processed_completed_seq =
        s_vdc_follower_rx.last_processed_completed_seq;
    snapshot->submitted_count = s_vdc_follower_rx.submitted_count;
    snapshot->frame_ready_count = s_vdc_follower_rx.frame_ready_count;
    snapshot->accepted_count = s_vdc_follower_rx.accepted_count;
    snapshot->invalid_count = s_vdc_follower_rx.invalid_count;
    snapshot->timeout_count = s_vdc_follower_rx.timeout_count;
    snapshot->window_miss_count = s_vdc_follower_rx.window_miss_count;
    snapshot->submit_reject_count = s_vdc_follower_rx.submit_reject_count;
    snapshot->last_result = s_vdc_follower_rx.last_result;
    snapshot->last_rx_result = s_vdc_follower_rx.last_rx_result;
    snapshot->last_error = s_vdc_follower_rx.last_error;
    snapshot->last_source_slot = s_vdc_follower_rx.last_source_slot;
    snapshot->last_command_seq = s_vdc_follower_rx.last_command_seq;
}

bool distributed_refmem_get_vdc_vector_snapshot(
    distributed_refmem_vdc_vector_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (!s_refmem_core_initialized) {
        return false;
    }
    return distributed_refmem_read_vdc_vector_snapshot(snapshot);
}

bool distributed_refmem_get_dpll_vector_snapshot(
    distributed_refmem_dpll_vector_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (!s_refmem_core_initialized) {
        return false;
    }
    return distributed_refmem_read_dpll_vector_snapshot(snapshot);
}

bool distributed_refmem_get_realtime_tdma_frame(uint8_t *frame,
                                               size_t frame_capacity,
                                               size_t *frame_size)
{
    return refmem_realtime_tdma_get_result_frame(&s_refmem_realtime_tdma,
                                                frame,
                                                frame_capacity,
                                                frame_size);
}

bool distributed_refmem_build_realtime_tdma_vdc_envelope(
    const vdc_tdma_schedule_profile_t *schedule,
    vdc_tdma_frame_envelope_t *envelope,
    refmem_vdc_bridge_status_t *status)
{
    uint8_t frame[REFMEM_REALTIME_TDMA_FRAME_MAX];
    size_t frame_size = 0u;
    refmem_realtime_tdma_snapshot_t snapshot;

    if (!refmem_realtime_tdma_get_snapshot_for_payload_class(
            &s_refmem_realtime_tdma,
            REFMEM_REALTIME_TDMA_PAYLOAD_VDC_COMMAND,
            &snapshot) ||
        !refmem_realtime_tdma_get_result_frame_for_payload_class(
            &s_refmem_realtime_tdma,
            REFMEM_REALTIME_TDMA_PAYLOAD_VDC_COMMAND,
            frame,
            sizeof(frame),
            &frame_size)) {
        if (status != NULL) {
            memset(status, 0, sizeof(*status));
            status->result = REFMEM_VDC_BRIDGE_BAD_TDMA_SNAPSHOT;
        }
        return false;
    }

    return refmem_vdc_bridge_build_data_envelope(schedule,
                                                 &snapshot,
                                                 frame,
                                                 frame_size,
                                                 envelope,
                                                 status);
}

bool distributed_refmem_submit_realtime_tdma_tx(
    const refmem_realtime_tdma_intent_config_t *config)
{
    return board_identity_get_no() != 5u &&
           refmem_realtime_tdma_submit_tx(&s_refmem_realtime_tdma, config);
}

bool distributed_refmem_submit_realtime_tdma_rx(
    const refmem_realtime_tdma_intent_config_t *config)
{
    return board_identity_get_no() != 5u &&
           refmem_realtime_tdma_submit_rx(&s_refmem_realtime_tdma, config);
}

void distributed_refmem_abort_realtime_tdma(void)
{
    refmem_realtime_tdma_abort(&s_refmem_realtime_tdma);
}

bool distributed_refmem_quality_gate_ready(void)
{
    const refmem_quality_gate_threshold_t threshold = {
        .max_crc_error_count = 0u,
        .max_stale_count = 0u,
        .max_late_count = 0u,
        .max_drop_count = 0u,
        .max_timeout_count = 0u,
        .require_no_last_error = 1u,
    };

    refmem_realtime_tdma_snapshot_t tdma;
    if (!refmem_realtime_tdma_get_snapshot(&s_refmem_realtime_tdma, &tdma)) {
        return false;
    }

    refmem_quality_runtime_table_t table;
    memset(&table, 0, sizeof(table));
    table.version = REFMEM_APP_MODEL_VERSION;
    table.entry_count = 1u;
    table.local_slot = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    if (!refmem_quality_map_realtime_tdma_slot(DISTRIBUTED_REFMEM_LOCAL_NODE_ID,
                                               &tdma,
                                               &table.entry[0])) {
        return false;
    }

    refmem_quality_gate_result_t gate;
    if (!refmem_quality_evaluate_deployment_gate(&table, &threshold, &gate)) {
        return false;
    }
    return gate.last_state == REFMEM_APP_GATE_PASS;
}

bool distributed_refmem_command_set_reason_table_crc32(uint32_t reason_table_crc32)
{
    osal_critical_enter();
    const bool ok = refmem_command_set_reason_table_crc32(&s_refmem_command_slot,
                                                          reason_table_crc32);
    osal_critical_exit();
    return ok;
}

void distributed_refmem_mirror_local_control(void);

bool distributed_refmem_command_try_post(const refmem_command_request_t *request,
                                         uint32_t issue_tick32)
{
    if (request == NULL) {
        return false;
    }
    const uint32_t board_no = board_identity_get_no();
    const uint32_t local_slot = board_no > 0u ? board_no - 1u : 0u;
    tdma_ring_runtime_snapshot_t ring;
    const bool unified_available =
        tdma_runtime_owner_get_ring_snapshot(&ring) &&
        ring.enabled != 0u && ring.adapter_started != 0u;
    const refmem_control_route_t route = refmem_control_route_decide(
        local_slot, request->target_mask, unified_available);
    if (route == REFMEM_CONTROL_ROUTE_REJECT_NO_TARGET ||
        route == REFMEM_CONTROL_ROUTE_REJECT_UNIFIED_UNAVAILABLE ||
        route == REFMEM_CONTROL_ROUTE_REJECT_BAD_SLOT) {
        return false;
    }
    osal_critical_enter();
    const bool ok = refmem_command_try_post(&s_refmem_command_slot,
                                            request,
                                            issue_tick32);
    osal_critical_exit();
    if (ok) {
        distributed_refmem_mirror_local_control();
    }
    return ok;
}

refmem_command_take_result_t distributed_refmem_command_try_take(
    uint32_t target_node,
    uint32_t active_epoch,
    uint32_t active_run_id,
    uint32_t observed_payload_crc32,
    uint32_t evidence_index)
{
    osal_critical_enter();
    const refmem_command_take_result_t result = refmem_command_try_take(
        &s_refmem_command_slot, target_node, active_epoch, active_run_id,
        observed_payload_crc32, evidence_index);
    osal_critical_exit();
    distributed_refmem_mirror_local_control();
    return result;
}

bool distributed_refmem_command_ack(uint32_t target_node,
                                    uint32_t evidence_index)
{
    osal_critical_enter();
    const bool ok = refmem_command_ack(&s_refmem_command_slot,
                                       target_node,
                                       evidence_index);
    osal_critical_exit();
    distributed_refmem_mirror_local_control();
    return ok;
}

/* The 0x52 command transport keeps the wire command_seq (the ACK seat reply
 * must carry it), so it cannot use
 * distributed_refmem_post_command_replacing_complete(), which rewrites the
 * sequence locally.  A slot left holding a completed command would otherwise
 * reject every later command for the rest of the boot; replace it only when
 * the previous one is terminal and the sequence actually differs, so a late
 * retransmission of the same command stays a no-op instead of re-executing. */
bool distributed_refmem_command_try_post_replacing_complete(
    const refmem_command_request_t *request,
    uint32_t issue_tick32)
{
    if (request == NULL) {
        return false;
    }
    const uint32_t board_no = board_identity_get_no();
    const uint32_t local_slot = board_no > 0u ? board_no - 1u : 0u;
    tdma_ring_runtime_snapshot_t ring;
    const bool unified_available =
        tdma_runtime_owner_get_ring_snapshot(&ring) &&
        ring.enabled != 0u && ring.adapter_started != 0u;
    const refmem_control_route_t route = refmem_control_route_decide(
        local_slot, request->target_mask, unified_available);
    if (route == REFMEM_CONTROL_ROUTE_REJECT_NO_TARGET ||
        route == REFMEM_CONTROL_ROUTE_REJECT_UNIFIED_UNAVAILABLE ||
        route == REFMEM_CONTROL_ROUTE_REJECT_BAD_SLOT) {
        return false;
    }
    osal_critical_enter();
    refmem_command_snapshot_t snapshot;
    bool ok = false;
    if (refmem_command_get_snapshot(&s_refmem_command_slot, &snapshot)) {
        if (snapshot.command_seq == request->command_seq) {
            ok = true; /* same command: already posted, never re-execute */
        } else if (snapshot.command_seq == 0u ||
                   (distributed_refmem_command_state_is_complete(
                        snapshot.state) &&
                    refmem_command_clear(&s_refmem_command_slot,
                                         snapshot.command_seq))) {
            ok = refmem_command_try_post(&s_refmem_command_slot, request,
                                         issue_tick32);
        }
    }
    osal_critical_exit();
    if (ok) {
        distributed_refmem_mirror_local_control();
    }
    return ok;
}

bool distributed_refmem_command_nack(uint32_t target_node,
                                     refmem_command_reason_t reason,
                                     uint32_t evidence_index)
{
    osal_critical_enter();
    const bool ok = refmem_command_nack(&s_refmem_command_slot,
                                        target_node,
                                        reason,
                                        evidence_index);
    osal_critical_exit();
    distributed_refmem_mirror_local_control();
    return ok;
}

bool distributed_refmem_command_mark_timeout(uint32_t now_tick32,
                                             uint32_t evidence_index)
{
    osal_critical_enter();
    const bool ok = refmem_command_mark_timeout(&s_refmem_command_slot,
                                                now_tick32,
                                                evidence_index);
    osal_critical_exit();
    distributed_refmem_mirror_local_control();
    return ok;
}

bool distributed_refmem_command_clear(uint32_t clear_seq)
{
    osal_critical_enter();
    const bool ok = refmem_command_clear(&s_refmem_command_slot, clear_seq);
    osal_critical_exit();
    distributed_refmem_mirror_local_control();
    return ok;
}

/* C0: mirror the single local command slot into the local node's REFMEM
 * control record.  The record is the replicated fact; the static command slot
 * remains the execution edge until the write path is switched to REFMEM. */
uint32_t s_node_control_seq;

void distributed_refmem_mirror_local_control(void);

bool distributed_refmem_node_control_write(uint32_t node_id,
                                           const refmem_command_snapshot_t *snapshot,
                                           uint32_t seq)
{
    refmem_vector_node_region_t *node = distributed_refmem_node_region(node_id);
    return node != NULL &&
           refmem_node_control_write(&node->command, node->params,
                                     &node->control_seq,
                                     &node->control_crc32, snapshot, NULL, seq);
}

void distributed_refmem_mirror_local_control(void)
{
    refmem_command_snapshot_t snapshot;
    if (!refmem_command_get_snapshot(&s_refmem_command_slot, &snapshot)) {
        return;
    }
    refmem_vector_node_region_t *node =
        distributed_refmem_node_region(distributed_refmem_local_slot());
    if (node == NULL) {
        return;
    }
    refmem_command_snapshot_t current;
    if (refmem_node_control_read(&node->command, node->params,
                                 node->control_seq, node->control_crc32,
                                 &current, NULL)) {
        /* Request/status split: the issuer owns the request fields; the local
         * owner only refreshes status, so two writers cannot clobber.  A
         * remote-fragment record has no local command slot with the same
         * command_seq; mirroring an older local slot would fabricate ACK/NACK
         * for the remote command. */
        if (current.command_seq != snapshot.command_seq) {
            return;
        }
        (void)refmem_node_control_update_status(
            &node->command, node->params, &node->control_seq,
            &node->control_crc32, snapshot.state, snapshot.ack_flags,
            snapshot.nack_flags, snapshot.busy_flags, snapshot.timeout_flags,
            snapshot.last_reason, snapshot.last_reason_slot,
            snapshot.evidence_index, snapshot.last_completed_seq);
    } else {
        s_node_control_seq++;
        (void)refmem_node_control_write(&node->command, node->params,
                                        &node->control_seq,
                                        &node->control_crc32, &snapshot, NULL,
                                        s_node_control_seq);
    }
}

/* Local REFMEM control: consume a command record that the issuer wrote into
 * its own node slot.  The record is the input fact; the command slot remains
 * the execution edge (post/take/ack) and its snapshot is mirrored back. */
bool distributed_refmem_node_control_apply_request(
    uint32_t node_id,
    const refmem_command_snapshot_t *request,
    const uint32_t *params,
    uint32_t seq)
{
    refmem_vector_node_region_t *node = distributed_refmem_node_region(node_id);
    return node != NULL &&
           refmem_node_control_apply_request(&node->command, node->params,
                                             &node->control_seq,
                                             &node->control_crc32, request,
                                             params, seq);
}

uint32_t distributed_refmem_local_slot(void)
{
    const uint32_t no = board_identity_get_no();
    const uint32_t slot = no > 0u ? no - 1u : 0u;
    return slot < DISTRIBUTED_REFMEM_NODE_COUNT
               ? slot
               : (uint32_t)DISTRIBUTED_REFMEM_NODE_COUNT - 1u;
}

bool distributed_refmem_param_pool_access(uint8_t **region, size_t *size)
{
    if (region == NULL || size == NULL) {
        return false;
    }
    *region = s_distributed_refmem_table.tlv;
    *size = sizeof(s_distributed_refmem_table.tlv);
    return true;
}

bool distributed_refmem_get_node_status(uint32_t node_id,
                                        refmem_node_status_t *status)
{
    if (status == NULL || node_id >= DISTRIBUTED_REFMEM_NODE_COUNT) {
        return false;
    }
    const refmem_vector_node_region_t *node =
        distributed_refmem_node_region(node_id);
    if (node == NULL) {
        return false;
    }
    const uint32_t now_ms = osal_tick_ms();
    memset(status, 0, sizeof(*status));
    status->node_id = node_id;
    status->state = node->state;
    status->heartbeat = node->heartbeat;
    status->slot_version = node->slot_version;
    status->last_update_ms = node->last_update_ms;
    status->stale_count = node->stale_count;
    status->fault_code = node->fault_code;
    status->flags = node->flags;
    status->node_type = node->node_type;
    const uint32_t age = now_ms >= node->last_update_ms
                             ? now_ms - node->last_update_ms
                             : 0u;
    status->age_ms = node->last_update_ms == 0u ? 0xFFFFFFFFu : age;
    status->stale = (node->last_update_ms == 0u || age > 1000u) ? 1u : 0u;
    status->control_state = node->command.state;
    status->ack_flags = node->command.ack_flags;
    status->nack_flags = node->command.nack_flags;
    status->last_reason = node->command.last_reason;
    return true;
}

bool distributed_refmem_get_command_snapshot(refmem_command_snapshot_t *snapshot)
{
    return refmem_command_get_snapshot(&s_refmem_command_slot, snapshot);
}

