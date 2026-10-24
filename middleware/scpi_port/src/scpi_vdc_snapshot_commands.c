#include "scpi_system_snapshot_commands.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "app.h"
#include "diagnostics_tdma_record.h"
#include "pico/time.h"
#include "board_config.h"
#include "board_identity.h"
#include "refmem_control_route.h"
#include "refmem_cmd_load.h"
#include "refmem_cmd_codec.h"
#include "refmem_cmd_frame.h"
#include "refmem_command_catalog.h"
#include "refmem_node_control.h"
#include "distributed_config.h"
#include "distributed_refmem.h"
#include "distributed_refmem_vdc_bridge.h"
#include "osal.h"
#include "project_build_info.h"
#include "refmem_application_model.h"
#include "refmem_pio_spi_adapter.h"
#include "refmem_quality.h"
#include "refmem_spi_physical_adapter.h"
#include "refmem_slot_claim.h"
#include "refmem_sync.h"
#include "refmem_sync_hello.h"
#include "refmem_table_registry.h"
#include "refmem_lease_table.h"
#include "tdma_maintenance_runtime.h"
#include "scpi_port_internal.h"
#include "storage_manager.h"
#include "system_manager.h"
#include "sync_trigger.h"
#include "tdma_runtime_owner.h"
#include "tdma_rx_start_cut.h"
#include "tdma_service_timing.h"
#include "tdma_long_frame.h"
#include "tdma_maintenance.h"
#include "tdma_maintenance_stream.h"
#include "tdma_maintenance_runtime.h"
#include "tdma_process_image_layout.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "vdc_dpll_manager.h"
#include "vdc_timestamp_clock.h"


static void scpi_feedback_record(scpi_t *context, const uint8_t record[64])
{
    static const char digits[] = "0123456789abcdef";
    char encoded[129];
    for (size_t i = 0; i < 64u; ++i) {
        encoded[i * 2u] = digits[record[i] >> 4u];
        encoded[i * 2u + 1u] = digits[record[i] & 15u];
    }
    encoded[128] = '\0';
    SCPI_ResultText(context, encoded);
}

scpi_result_t scpi_cmd_refmem_vdc_feedback_rx_q(scpi_t *context)
{
    uint32_t source;
    if (!SCPI_ParamUInt32(context, &source, TRUE)) return SCPI_RES_ERR;
    distributed_refmem_vdc_feedback_rx_snapshot_t s;
    if (!distributed_refmem_get_vdc_feedback_rx(source, &s)) return SCPI_RES_ERR;
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.active);
    SCPI_ResultUInt32(context, s.retained);
    SCPI_ResultUInt32(context, s.source_slot);
    SCPI_ResultUInt32(context, s.ring_config_seq);
    SCPI_ResultUInt32(context, s.role_generation);
    SCPI_ResultUInt32(context, s.schedule_crc32);
    SCPI_ResultUInt32(context, s.clock_epoch_id);
    SCPI_ResultUInt32(context, s.clock_run_id);
    SCPI_ResultUInt32(context, s.receive_count);
    SCPI_ResultUInt32(context, s.reject_count);
    SCPI_ResultUInt32(context, s.duplicate_count);
    SCPI_ResultUInt32(context, s.timeout_count);
    SCPI_ResultUInt32(context, s.last_transport_seq);
    SCPI_ResultUInt32(context, s.first_rx_ms);
    SCPI_ResultUInt32(context, s.last_rx_ms);
    scpi_feedback_record(context, s.record);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_vdc_reference_proof_q(scpi_t *context)
{
    uint32_t source;
    if (!SCPI_ParamUInt32(context, &source, TRUE)) return SCPI_RES_ERR;
    distributed_refmem_vdc_feedback_rx_snapshot_t s;
    if (!distributed_refmem_get_vdc_reference_proof(source, &s)) return SCPI_RES_ERR;
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.active);
    SCPI_ResultUInt32(context, s.retained);
    SCPI_ResultUInt32(context, s.source_slot);
    SCPI_ResultUInt32(context, s.ring_config_seq);
    SCPI_ResultUInt32(context, s.role_generation);
    SCPI_ResultUInt32(context, s.schedule_crc32);
    SCPI_ResultUInt32(context, s.clock_epoch_id);
    SCPI_ResultUInt32(context, s.clock_run_id);
    SCPI_ResultUInt32(context, s.receive_count);
    SCPI_ResultUInt32(context, s.reject_count);
    SCPI_ResultUInt32(context, s.duplicate_count);
    SCPI_ResultUInt32(context, s.timeout_count);
    SCPI_ResultUInt32(context, s.last_transport_seq);
    SCPI_ResultUInt32(context, s.first_rx_ms);
    SCPI_ResultUInt32(context, s.last_rx_ms);
    scpi_feedback_record(context, s.record);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_refmem_vdc_feedback_tx_q(scpi_t *context)
{
    distributed_refmem_vdc_feedback_tx_snapshot_t s;
    if (!distributed_refmem_get_vdc_feedback_tx(&s)) return SCPI_RES_ERR;
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.active);
    SCPI_ResultUInt32(context, s.fragment_index);
    SCPI_ResultUInt32(context, s.fragments_published);
    SCPI_ResultUInt32(context, s.groups_published);
    SCPI_ResultUInt32(context, s.cancel_count);
    SCPI_ResultUInt32(context, s.reject_count);
    SCPI_ResultUInt32(context, s.history_count);
    SCPI_ResultUInt32(context, s.ring_config_seq);
    SCPI_ResultUInt32(context, s.role_generation);
    SCPI_ResultUInt32(context, s.source_slot);
    SCPI_ResultUInt32(context, s.target_slot);
    SCPI_ResultUInt32(context, s.schedule_crc32);
    SCPI_ResultUInt32(context, s.clock_epoch_id);
    SCPI_ResultUInt32(context, s.clock_run_id);
    scpi_feedback_record(context, s.history[0]);
    scpi_feedback_record(context, s.history[1]);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_feedback_session(scpi_t *context)
{
    uint32_t session;
    if (!scpi_port_read_u32(context, &session) ||
        !vdc_dpll_manager_set_feedback_session(session)) {
        scpi_port_push_exec_error(context, "VDC_FEEDBACK_SESSION_STOP_REQUIRED_OR_BUSY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, session);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_feedback_session_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, vdc_dpll_manager_feedback_session());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_feedback_probe(scpi_t *context)
{
    int32_t delta;
    if (SCPI_ParamInt32(context, &delta, TRUE) != TRUE ||
        delta < -VDC_BOUNDARY_PROBE_MAX_DELTA_PPB ||
        delta > VDC_BOUNDARY_PROBE_MAX_DELTA_PPB ||
        !vdc_dpll_manager_set_boundary_probe(delta)) {
        scpi_port_push_exec_error(context, "VDC_FEEDBACK_PROBE_STOP_SESSION_OR_RANGE");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultInt32(context, delta);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_feedback_auto(scpi_t *context)
{
    uint32_t enabled;
    if (!scpi_port_read_u32(context, &enabled) || enabled > 1u ||
        !vdc_dpll_manager_set_boundary_auto(enabled != 0u)) {
        scpi_port_push_exec_error(context, "VDC_FEEDBACK_AUTO_STOP_SESSION_OR_RANGE");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_feedback_auto_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, vdc_dpll_manager_boundary_auto_enabled() ? 1u : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_feedback_reference(scpi_t *context)
{
    uint32_t enabled;
    if (!scpi_port_read_u32(context, &enabled) || enabled > 1u ||
        !vdc_dpll_manager_set_reference_publish(enabled != 0u)) {
        scpi_port_push_exec_error(context, "VDC_FEEDBACK_REFERENCE_STOP_SESSION_OR_RANGE");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_feedback_reference_q(scpi_t *context)
{
    bool enabled;
    if (!vdc_dpll_manager_try_reference_publish_enabled(&enabled)) {
        scpi_port_push_exec_error(context, "VDC_FEEDBACK_REFERENCE_BUSY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, enabled ? 1u : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_feedback_local_follow(scpi_t *context)
{
    uint32_t enabled;
    if (!scpi_port_read_u32(context, &enabled) || enabled > 1u ||
        !vdc_dpll_manager_set_local_follow(enabled != 0u)) {
        scpi_port_push_exec_error(context, "VDC_FEEDBACK_LOCAL_FOLLOW_STOP_SESSION_OR_RANGE");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_feedback_local_follow_q(scpi_t *context)
{
    bool enabled;
    if (!vdc_dpll_manager_try_local_follow_enabled(&enabled)) {
        scpi_port_push_exec_error(context, "VDC_FEEDBACK_LOCAL_FOLLOW_BUSY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, enabled ? 1u : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_feedback_boundary_q(scpi_t *context)
{
    uint32_t slot;
    tdma_ring_runtime_snapshot_t before, after;
    vdc_dpll_boundary_status_t s;
    uint8_t record[REFMEM_VDC_FEEDBACK_RECORD_SIZE] = {0};
    if (!scpi_port_read_u32(context, &slot)) return SCPI_RES_ERR;
    /* A retained post-STOP export only. Never sample this control state while
     * RUN or while STOP is still awaiting its Core1 application. */
    if (!tdma_runtime_owner_get_ring_snapshot(&before) ||
        before.enabled != 0u || before.adapter_started != 0u ||
        before.config_seq != before.applied_config_seq ||
        !vdc_dpll_manager_get_boundary_status(slot, &s) ||
        !tdma_runtime_owner_get_ring_snapshot(&after) ||
        after.enabled != 0u || after.adapter_started != 0u ||
        after.config_seq != before.config_seq ||
        after.applied_config_seq != before.applied_config_seq) {
        scpi_port_push_exec_error(context, "VDC_FEEDBACK_BOUNDARY_STOP_REQUIRED_OR_BUSY");
        return SCPI_RES_ERR;
    }
    /* A zero sequence means no retained command; its all-zero record is an
     * explicit empty value, not a CRC-valid schema3 command. Encode before
     * emitting the header so a malformed command cannot yield a partial row.
     * STOP clears runtime node_count; retained history uses compile capacity,
     * not the cleared runtime topology, and grants no fresh admission. */
    if (s.command.command_seq != 0u &&
        !refmem_sync_vdc_boundary_command_encode(&s.command, PROJECT_NODE_CAPACITY, record)) {
        scpi_port_push_exec_error(context, "VDC_FEEDBACK_BOUNDARY_INVALID_RECORD");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.active);
    SCPI_ResultUInt32(context, s.requested_probe_generation);
    SCPI_ResultInt32(context, s.requested_delta_ppb);
    SCPI_ResultUInt32(context, s.control_session);
    SCPI_ResultUInt32(context, s.probe_generation);
    SCPI_ResultUInt32(context, s.consumed_mask);
    SCPI_ResultUInt32(context, s.offer_id);
    SCPI_ResultUInt32(context, s.offer_serial);
    SCPI_ResultUInt32(context, s.tx_count);
    SCPI_ResultUInt32(context, s.apply_count);
    SCPI_ResultUInt32(context, s.reject_count);
    SCPI_ResultUInt32(context, s.last_reject);
    SCPI_ResultUInt32(context, s.peer_state);
    SCPI_ResultUInt32(context, s.offered_ms);
    SCPI_ResultUInt32(context, s.first_reject);
    SCPI_ResultUInt32(context, s.reject_mask);
    scpi_feedback_record(context, record);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_feedback_bridge_q(scpi_t *context)
{
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_snapshot(&ring) || ring.enabled || ring.adapter_started) {
        scpi_port_push_exec_error(context, "VDC_FEEDBACK_BRIDGE_STOP_REQUIRED_OR_BUSY");
        return SCPI_RES_ERR;
    }
    vdc_timestamp_clock_bridge_diagnostic_t s;
    if (!vdc_timestamp_clock_read_bridge_diagnostic(BOARD_SYS_CLOCK_HZ, &s)) return SCPI_RES_ERR;
    /* STOP-only configuration evidence; individual counter reads are not an
     * accepted bridge and do not authorize a model projection. */
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.platform_supported);
    SCPI_ResultUInt32(context, s.configuration_supported);
    SCPI_ResultUInt32(context, s.bridge_valid);
    SCPI_ResultUInt32(context, s.clock_ready);
    SCPI_ResultUInt32(context, s.expected_hz);
    SCPI_ResultUInt32(context, s.cached_hz);
    SCPI_ResultUInt32(context, s.sdk_sys_hz);
    SCPI_ResultUInt32(context, s.default_timer);
    SCPI_ResultUInt32(context, s.proc_config);
    SCPI_ResultUInt32(context, s.timer0_source);
    SCPI_ResultUInt32(context, s.timer0_pause);
    SCPI_ResultUInt32(context, s.timer0_dbgpause);
    SCPI_ResultUInt32(context, s.timer1_source);
    SCPI_ResultUInt32(context, s.timer1_pause);
    SCPI_ResultUInt32(context, s.timer1_dbgpause);
    SCPI_ResultUInt32(context, s.ref_ctrl);
    SCPI_ResultUInt32(context, s.ref_div);
    SCPI_ResultUInt32(context, s.ref_selected);
    SCPI_ResultUInt32(context, s.sys_ctrl);
    SCPI_ResultUInt32(context, s.sys_div);
    SCPI_ResultUInt32(context, s.sys_selected);
    SCPI_ResultUInt32(context, s.resus_ctrl);
    SCPI_ResultUInt32(context, s.resus_status);
    SCPI_ResultUInt32(context, s.pll_cs);
    SCPI_ResultUInt32(context, s.pll_pwr);
    SCPI_ResultUInt32(context, s.pll_fbdiv);
    SCPI_ResultUInt32(context, s.pll_prim);
    SCPI_ResultUInt32(context, s.tick_ctrl);
    SCPI_ResultUInt32(context, s.tick_cycles);
    SCPI_ResultUInt32(context, s.xosc_ctrl);
    SCPI_ResultUInt32(context, s.xosc_status);
    SCPI_ResultUInt32(context, s.xosc_dormant);
    SCPI_ResultUInt32(context, s.timer0_sample_valid);
    SCPI_ResultUInt32(context, s.timer1_sample_valid);
    SCPI_ResultUInt64(context, s.timer0_sample_us);
    SCPI_ResultUInt64(context, s.timer1_sample_ticks);
    SCPI_ResultUInt64(context, s.bridge.raw_before);
    SCPI_ResultUInt64(context, s.bridge.local_ns);
    SCPI_ResultUInt64(context, s.bridge.raw_after);
    SCPI_ResultUInt32(context, s.bridge.tick_hz);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_feedback_model_q(scpi_t *context)
{
    vdc_dpll_manager_committed_model_t model;
    if (!vdc_dpll_manager_get_committed_model(&model)) return SCPI_RES_ERR;
    /* Schema 1 is a retained Core1 commit, including an empty token before
     * publication. It is not an assertion of current/GPIO output validity. */
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, model.token);
    SCPI_ResultUInt32(context, model.session);
    SCPI_ResultUInt32(context, model.role_generation);
    SCPI_ResultUInt32(context, model.applied_command_seq);
    SCPI_ResultUInt32(context, model.clock_epoch_id);
    SCPI_ResultUInt32(context, model.clock_run_id);
    SCPI_ResultUInt32(context, model.local_slot);
    SCPI_ResultUInt64(context, model.valid_from_raw);
    SCPI_ResultUInt32(context, model.dco.valid);
    SCPI_ResultUInt32(context, model.dco.dco_update_seq);
    SCPI_ResultUInt32(context, model.dco.source_model_seq);
    SCPI_ResultUInt32(context, model.dco.epoch_id);
    SCPI_ResultUInt32(context, model.dco.run_id);
    SCPI_ResultUInt64(context, model.dco.base_local_tick64);
    SCPI_ResultUInt64(context, model.dco.base_vdc_time64_ns);
    SCPI_ResultUInt32(context, model.dco.nominal_period_ns);
    SCPI_ResultInt32(context, model.dco.period_adjust_ppb);
    SCPI_ResultInt32(context, model.dco.phase_offset_ns);
    SCPI_ResultUInt32(context, model.dco.slew_limit_ppb);
    SCPI_ResultUInt32(context, model.dco.lock_state);
    SCPI_ResultUInt32(context, model.dco.tdma_schedule_crc32);
    SCPI_ResultUInt32(context, model.dco.servo_profile_crc32);
    return SCPI_RES_OK;
}

#include "vdc_run_output.h"

bool scpi_vdc_run_u32(scpi_t *context,uint32_t *out)
{
    scpi_parameter_t param;
    if (!SCPI_Parameter(context,&param,TRUE) ||
        param.type!=SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA || param.len<=0) return false;
    /* This libscpi version omits consumed numeric trailing whitespace from
     * its aggregate parameter length. Reject that spelling before it can
     * silently truncate a later value ("1000000 ,1000,1000" -> duration 100).
     * Whitespace before a token remains supported. Do not repair the lexer
     * or reinterpret an incomplete command at this resource boundary. */
    if (context->param_list.lex_state.pos!=param.ptr+param.len) return false;
    int pos=param.ptr[0]=='+' ? 1 : 0;
    if (pos==param.len) return false;
    uint32_t value=0u;
    for (;pos<param.len;++pos) {
        const unsigned char digit=(unsigned char)param.ptr[pos];
        if (digit<'0' || digit>'9' || value>(UINT32_MAX-(digit-'0'))/10u) return false;
        value=value*10u+(digit-'0');
    }
    *out=value; return true;
}

bool scpi_vdc_run_end(scpi_t *context)
{
    scpi_parameter_t extra;
    return !SCPI_Parameter(context,&extra,FALSE) && !SCPI_ParamErrorOccurred(context);
}

scpi_result_t scpi_cmd_vdc_run_output(scpi_t *context)
{
    uint32_t period,high,duration,request;
    if (!scpi_vdc_run_u32(context,&period) || !scpi_vdc_run_u32(context,&high) ||
        !scpi_vdc_run_u32(context,&duration) || !scpi_vdc_run_end(context) ||
        !vdc_run_output_prepare(period,high,duration,&request)) {
        scpi_port_push_exec_error(context,"VDC_RUN_OUTPUT_REJECTED"); return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context,request); return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_run_output_stop(scpi_t *context)
{
    if (!scpi_vdc_run_end(context)) {
        scpi_port_push_exec_error(context,"VDC_RUN_OUTPUT_STOP_PARAMETERS"); return SCPI_RES_ERR;
    }
    vdc_run_output_cancel(); return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_vdc_run_output_q(scpi_t *context)
{
    vdc_run_output_status_t s;
    if (!vdc_run_output_status(&s)) {
        scpi_port_push_exec_error(context,"VDC_RUN_OUTPUT_NOT_RETIRED"); return SCPI_RES_ERR;
    }
    const sync_io_run_output_snapshot_t *h=&s.hardware;
    SCPI_ResultUInt32(context,VDC_RUN_OUTPUT_SCHEMA); SCPI_ResultUInt32(context,h->generation);
    SCPI_ResultUInt32(context,h->state); SCPI_ResultUInt32(context,h->reason);
    SCPI_ResultUInt32(context,h->blocks); SCPI_ResultUInt32(context,h->edges);
    SCPI_ResultUInt32(context,h->source_retirements);
    SCPI_ResultUInt32(context,h->first_model); SCPI_ResultUInt32(context,h->last_model);
    SCPI_ResultUInt32(context,h->model_changes); SCPI_ResultUInt32(context,h->tick_hz);
    SCPI_ResultUInt32(context,h->transfer_count); SCPI_ResultUInt32(context,h->pio_enabled);
    SCPI_ResultUInt32(context,h->dma_busy);
    SCPI_ResultUInt32(context,s.session); SCPI_ResultUInt32(context,s.period_ns);
    SCPI_ResultUInt32(context,s.high_ns); SCPI_ResultUInt32(context,s.duration_ms);
    SCPI_ResultInt32(context,s.delay_ns); SCPI_ResultUInt32(context,s.ring_config);
    SCPI_ResultUInt32(context,s.role_generation); SCPI_ResultUInt32(context,s.clock_epoch);
    SCPI_ResultUInt32(context,s.clock_run); SCPI_ResultUInt32(context,s.blocks_planned);
    SCPI_ResultUInt32(context,s.plan_rejects); SCPI_ResultUInt32(context,s.binding_rejects);
    SCPI_ResultUInt64(context,h->anchor_before); SCPI_ResultUInt64(context,h->anchor_after);
    SCPI_ResultUInt64(context,h->last_rising_tick); SCPI_ResultUInt64(context,h->last_falling_tick);
    SCPI_ResultUInt64(context,h->first_ordinal); SCPI_ResultUInt64(context,h->last_ordinal);
    SCPI_ResultUInt64(context,h->expires_tick); SCPI_ResultUInt64(context,s.last_local_ns);
    SCPI_ResultUInt64(context,s.last_target_vdc_ns); SCPI_ResultUInt64(context,s.maximum_bridge_width_ticks);
    SCPI_ResultUInt32(context,s.prepared_config); SCPI_ResultUInt32(context,s.arm_config);
    SCPI_ResultUInt32(context,h->start_pc); SCPI_ResultUInt32(context,h->program_offset);
    SCPI_ResultUInt32(context,h->start_raw_flags);
    SCPI_ResultUInt64(context,h->start_raw_observed); SCPI_ResultUInt64(context,h->start_raw_after);
    SCPI_ResultUInt64(context,h->service_last_tick); SCPI_ResultUInt64(context,h->service_last_gap_ticks);
    SCPI_ResultUInt64(context,h->service_max_gap_ticks); SCPI_ResultUInt64(context,h->submit_last_tick);
    SCPI_ResultUInt64(context,h->submit_max_gap_ticks); SCPI_ResultUInt64(context,h->refill_min_margin_ticks);
    SCPI_ResultUInt64(context,h->retire_raw_tick);
    SCPI_ResultUInt32(context,h->service_observations); SCPI_ResultUInt32(context,h->submit_service_observation);
    SCPI_ResultUInt32(context,h->retire_raw_valid); SCPI_ResultUInt32(context,h->retire_pc);
    SCPI_ResultUInt32(context,h->retire_fstat); SCPI_ResultUInt32(context,h->retire_fdebug);
    SCPI_ResultUInt32(context,h->retire_dma_ctrl); SCPI_ResultUInt32(context,h->retire_dma_remaining);
    SCPI_ResultUInt32(context,h->retire_pio_ctrl);
    SCPI_ResultUInt32(context,s.last_phase); SCPI_ResultUInt32(context,s.last_outcome);
    for (uint32_t phase=0u;phase<VDC_RUN_OUTPUT_PHASE_COUNT;++phase)
        for (uint32_t outcome=0u;outcome<VDC_RUN_OUTPUT_OUTCOME_COUNT;++outcome)
            SCPI_ResultUInt32(context,s.outcomes[phase][outcome]);
    SCPI_ResultUInt32(context,s.prefetched_blocks);
    SCPI_ResultUInt32(context,s.cache_hits);
    SCPI_ResultUInt32(context,s.cache_invalidations);
    SCPI_ResultUInt32(context,s.fast_calls);
    SCPI_ResultUInt32(context,s.fast_submissions);
    SCPI_ResultUInt32(context,s.fast_empty);
    SCPI_ResultUInt32(context,s.fast_body_max_us);
    SCPI_ResultUInt32(context,s.fast_wall_samples);
    SCPI_ResultUInt32(context,s.fast_wall_max_cycles);
    SCPI_ResultUInt32(context,s.fast_budget_overruns);
    SCPI_ResultUInt32(context,s.plan_ahead_us);
    SCPI_ResultUInt32(context,s.commit_ahead_us);
    SCPI_ResultUInt32(context,s.refill_low_us);
    SCPI_ResultUInt32(context,s.timeline_bridge_samples);
    SCPI_ResultUInt32(context,s.partial_plan_steps);
    SCPI_ResultUInt32(context,s.plan_waits);
    SCPI_ResultUInt32(context,s.refill_waits);
    SCPI_ResultUInt32(context,s.commit_waits);
    SCPI_ResultUInt32(context,s.block_edges);
    SCPI_ResultUInt32(context,s.schedule_cycles);
    SCPI_ResultUInt64(context,s.timeline_raw_before);
    SCPI_ResultUInt64(context,s.timeline_local_ns);
    SCPI_ResultUInt64(context,s.timeline_raw_after);
    SCPI_ResultUInt32(context,h->fifo_words_per_edge);
    SCPI_ResultUInt32(context,h->fixed_high_ticks);
    SCPI_ResultUInt32(context,s.service_sequence);
    SCPI_ResultUInt32(context,s.last_outcome_service_sequence);
    SCPI_ResultUInt32(context,s.last_invalidation_service_sequence);
    SCPI_ResultUInt64(context,s.last_outcome_tick);
    SCPI_ResultUInt64(context,s.last_invalidation_tick);
    SCPI_ResultUInt32(context,s.last_submit_failure);
    SCPI_ResultUInt32(context,s.timebase);
    SCPI_ResultUInt64(context,s.initial_raw_tick);
    SCPI_ResultUInt64(context,s.initial_local_ns);
    SCPI_ResultUInt32(context,s.planned_calls);
    SCPI_ResultUInt32(context,s.planned_submissions);
    SCPI_ResultUInt32(context,s.planned_rebuilds);
    SCPI_ResultUInt32(context,s.planned_wall_samples);
    SCPI_ResultUInt32(context,s.planned_wall_max_cycles);
    SCPI_ResultUInt32(context,s.planned_budget_overruns);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_fixed_output(scpi_t *context)
{
    uint32_t period_ns, high_ns, pulse_count, tick_period_ns, request_id;
    if (!scpi_port_read_u32(context, &period_ns) ||
        !scpi_port_read_u32(context, &high_ns) ||
        !scpi_port_read_u32(context, &pulse_count) ||
        !scpi_port_read_u32(context, &tick_period_ns) ||
        !vdc_dpll_manager_fixed_output_start(period_ns, high_ns,
            pulse_count, tick_period_ns, &request_id)) {
        scpi_port_push_exec_error(context, "VDC_FIXED_OUTPUT_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, request_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_fixed_output_stop(scpi_t *context)
{
    if (!vdc_dpll_manager_fixed_output_stop()) {
        scpi_port_push_exec_error(context, "VDC_FIXED_OUTPUT_BUSY");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

static scpi_result_t scpi_vdc_output_status(scpi_t *context,
    vdc_dpll_manager_fixed_output_status_t s)
{
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.request_id);
    SCPI_ResultUInt32(context, s.state);
    SCPI_ResultUInt32(context, s.last_reason);
    SCPI_ResultUInt32(context, s.model.token);
    SCPI_ResultUInt32(context, s.model.session);
    SCPI_ResultUInt32(context, s.model.role_generation);
    SCPI_ResultUInt32(context, s.model.applied_command_seq);
    SCPI_ResultUInt32(context, s.model.clock_epoch_id);
    SCPI_ResultUInt32(context, s.model.clock_run_id);
    SCPI_ResultUInt32(context, s.model.local_slot);
    SCPI_ResultUInt32(context, s.model.dco.dco_update_seq);
    SCPI_ResultInt32(context, s.model.dco.period_adjust_ppb);
    SCPI_ResultUInt32(context, s.model.dco.tdma_schedule_crc32);
    SCPI_ResultUInt32(context, s.model.dco.servo_profile_crc32);
    SCPI_ResultUInt32(context, s.period_ns);
    SCPI_ResultUInt32(context, s.high_ns);
    SCPI_ResultUInt32(context, s.pulse_count);
    SCPI_ResultUInt32(context, s.tick_period_ns);
    SCPI_ResultUInt32(context, s.system_clock_hz);
    SCPI_ResultUInt32(context, s.pio_divider256);
    SCPI_ResultUInt32(context, s.min_period_ticks);
    SCPI_ResultUInt32(context, s.max_period_ticks);
    SCPI_ResultUInt32(context, s.high_ticks);
    SCPI_ResultUInt64(context, s.first_edge_ticks);
    SCPI_ResultUInt64(context, s.last_edge_ticks);
    SCPI_ResultUInt64(context, s.total_ticks);
    SCPI_ResultUInt32(context, s.completed_pulses);
    SCPI_ResultUInt32(context, s.transfer_count);
    SCPI_ResultUInt32(context, s.pio_enabled);
    SCPI_ResultUInt32(context, s.dma_busy);
    SCPI_ResultUInt32(context, s.model_unchanged);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_fixed_output_q(scpi_t *context)
{
    vdc_dpll_manager_fixed_output_status_t s;
    if (!vdc_dpll_manager_get_fixed_output(&s)) {
        scpi_port_push_exec_error(context, "VDC_FIXED_OUTPUT_STOP_REQUIRED_OR_BUSY");
        return SCPI_RES_ERR;
    }
    return scpi_vdc_output_status(context, s);
}

scpi_result_t scpi_cmd_vdc_subdivision_output(scpi_t *context)
{
    uint32_t interval_ns, pulse_high_ns, subdivision_count;
    uint32_t marker_high_ns, tick_period_ns, request_id;
    if (!scpi_port_read_u32(context, &interval_ns) ||
        !scpi_port_read_u32(context, &pulse_high_ns) ||
        !scpi_port_read_u32(context, &subdivision_count) ||
        !scpi_port_read_u32(context, &marker_high_ns) ||
        !scpi_port_read_u32(context, &tick_period_ns) ||
        !vdc_dpll_manager_subdivision_output_start(interval_ns, pulse_high_ns,
            subdivision_count, marker_high_ns, tick_period_ns, &request_id)) {
        scpi_port_push_exec_error(context, "VDC_SUBDIVISION_OUTPUT_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, request_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_subdivision_output_stop(scpi_t *context)
{
    if (!vdc_dpll_manager_subdivision_output_stop()) {
        scpi_port_push_exec_error(context, "VDC_SUBDIVISION_OUTPUT_BUSY_OR_MODE");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

static scpi_result_t scpi_vdc_subdivision_status(scpi_t *context,
    vdc_dpll_manager_subdivision_output_status_t s)
{
    /* SUBDIV schema 1: schema, mode, marker_ns, marker_ticks, subdivision_count,
     * followed by the unchanged 32-field FIXED batch schema (including schema).
     * pulse_count includes one marker in mode 1, two in window mode 2. */
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, s.mode);
    SCPI_ResultUInt32(context, s.marker_high_ns);
    SCPI_ResultUInt32(context, s.marker_high_ticks);
    SCPI_ResultUInt32(context, s.subdivision_count);
    return scpi_vdc_output_status(context, s.batch);
}

scpi_result_t scpi_cmd_vdc_subdivision_output_q(scpi_t *context)
{
    vdc_dpll_manager_subdivision_output_status_t s;
    if (!vdc_dpll_manager_get_subdivision_output(&s)) {
        scpi_port_push_exec_error(context, "VDC_SUBDIVISION_OUTPUT_STOP_REQUIRED_OR_BUSY_OR_MODE");
        return SCPI_RES_ERR;
    }
    return scpi_vdc_subdivision_status(context, s);
}

scpi_result_t scpi_cmd_vdc_subdivision_window(scpi_t *context)
{
    uint32_t sync_period_ns, interval_ns, pulse_high_ns;
    uint32_t marker_high_ns, tick_period_ns, request_id;
    if (!scpi_port_read_u32(context, &sync_period_ns) ||
        !scpi_port_read_u32(context, &interval_ns) ||
        !scpi_port_read_u32(context, &pulse_high_ns) ||
        !scpi_port_read_u32(context, &marker_high_ns) ||
        !scpi_port_read_u32(context, &tick_period_ns) ||
        !vdc_dpll_manager_subdivision_window_start(sync_period_ns, interval_ns,
            pulse_high_ns, marker_high_ns, tick_period_ns, &request_id)) {
        scpi_port_push_exec_error(context, "VDC_SUBDIVISION_WINDOW_REJECTED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, request_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_subdivision_window_stop(scpi_t *context)
{
    if (!vdc_dpll_manager_subdivision_window_stop()) {
        scpi_port_push_exec_error(context, "VDC_SUBDIVISION_WINDOW_BUSY_OR_MODE");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_ok(context);
}

scpi_result_t scpi_cmd_vdc_subdivision_window_q(scpi_t *context)
{
    vdc_dpll_manager_subdivision_output_status_t s;
    if (!vdc_dpll_manager_get_subdivision_window(&s)) {
        scpi_port_push_exec_error(context, "VDC_SUBDIVISION_WINDOW_STOP_REQUIRED_OR_BUSY_OR_MODE");
        return SCPI_RES_ERR;
    }
    return scpi_vdc_subdivision_status(context, s);
}

scpi_result_t scpi_cmd_vdc_feedback_match_q(scpi_t *context)
{
    uint32_t source;
    vdc_dpll_manager_feedback_match_status_t s;
    if (!scpi_port_read_u32(context, &source) ||
        !vdc_dpll_manager_get_feedback_match(source, &s)) return SCPI_RES_ERR;
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.active);
    SCPI_ResultUInt32(context, s.source_slot);
    SCPI_ResultUInt32(context, s.target_slot);
    SCPI_ResultUInt32(context, s.ring_config_seq);
    SCPI_ResultUInt32(context, s.role_generation);
    SCPI_ResultUInt32(context, s.schedule_crc32);
    SCPI_ResultUInt32(context, s.clock_epoch_id);
    SCPI_ResultUInt32(context, s.clock_run_id);
    SCPI_ResultUInt32(context, s.receive_count);
    SCPI_ResultUInt32(context, s.baseline_count);
    SCPI_ResultUInt32(context, s.match_count);
    SCPI_ResultUInt32(context, s.miss_count);
    SCPI_ResultUInt32(context, s.stale_count);
    SCPI_ResultUInt32(context, s.invalid_count);
    SCPI_ResultUInt32(context, s.last_result);
    SCPI_ResultUInt32(context, s.cache_insert_count);
    SCPI_ResultUInt32(context, s.cache_reject_count);
    SCPI_ResultUInt32(context, s.cache_latest_sequence);
    /* Schemas 2/3 use ms ages and model-token identities. Schema 2 carries
     * projected absolute output intervals; schema 3 carries rate coordinates. */
    SCPI_ResultUInt32(context, s.last_age_ticks);
    SCPI_ResultUInt32(context, s.max_age_ticks);
    SCPI_ResultUInt32(context, s.result.has_pair);
    SCPI_ResultUInt32(context, s.result.reference_epoch);
    SCPI_ResultUInt32(context, s.result.reference_generation);
    SCPI_ResultUInt64(context, s.result.source.source_arm_epoch);
    SCPI_ResultUInt32(context, s.result.source.source_clock_epoch_id);
    SCPI_ResultUInt32(context, s.result.source.source_clock_run_id);
    SCPI_ResultUInt32(context, s.result.source.observer_epoch);
    SCPI_ResultUInt32(context, s.result.source.tick_hz);
    SCPI_ResultInt64(context, s.result.raw_ppb_lo);
    SCPI_ResultInt64(context, s.result.raw_ppb_hi);
    for (uint32_t i = 0; i < 2u; ++i) {
        SCPI_ResultUInt32(context, s.result.pairs[i].measurement_sequence);
        SCPI_ResultUInt32(context, s.result.pairs[i].reference_identity_crc32);
        SCPI_ResultUInt64(context, s.result.pairs[i].rx_elapsed_cycles);
        SCPI_ResultUInt64(context, s.result.pairs[i].reference_tx_lo);
        SCPI_ResultUInt64(context, s.result.pairs[i].reference_tx_hi);
    }
    if (s.schema == 2u || s.schema == 3u) {
        SCPI_ResultUInt32(context, s.control_session);
        SCPI_ResultUInt32(context, s.result.reserved);
        for (uint32_t i = 0; i < 2u; ++i) {
            SCPI_ResultUInt32(context, s.result.pairs[i].rx_width_ns);
            SCPI_ResultUInt32(context, s.result.pairs[i].source_model_token);
        }
    }
    return SCPI_RES_OK;
}

