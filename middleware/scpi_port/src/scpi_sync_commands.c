#include "scpi_sync_commands.h"

#include "project_config.h"
#include "vdc_dpll_manager.h"

scpi_result_t scpi_sync_state_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0x20000001u);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "LOCKED");
    SCPI_ResultText(context, "FIELD_SYNC_DEFAULT");
    SCPI_ResultUInt32(context, 0x20000002u);
    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultUInt32(context, 0x10000003u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultText(context, "A0>A1>A2>A3>A0");
    SCPI_ResultText(context, "A0");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "NONE");
    return SCPI_RES_OK;
}

scpi_result_t scpi_sync_parameter_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultText(context, "FIELD_SYNC_DEFAULT");
    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultUInt32(context, 0x10000003u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 86400u);
    SCPI_ResultText(context, "A0");
    SCPI_ResultText(context, "A0>A1>A2>A3>A0");
    SCPI_ResultUInt32(context, 1000u);
    SCPI_ResultUInt32(context, 12500000u);
    SCPI_ResultUInt32(context, 300u);
    SCPI_ResultUInt32(context, 200u);
    SCPI_ResultUInt32(context, 1000u);
    SCPI_ResultText(context, "DEFAULT");
    SCPI_ResultText(context, "DEFAULT");
    return SCPI_RES_OK;
}

scpi_result_t scpi_sync_health_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "READY");
    SCPI_ResultText(context, "NONE");
    return SCPI_RES_OK;
}

scpi_result_t scpi_sync_node_q(scpi_t *context)
{
    SCPI_ResultText(context, "A0");
    SCPI_ResultText(context, "ORIGIN");
    SCPI_ResultText(context, "OK");
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "LOCKED");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sync_check_q(scpi_t *context)
{
    SCPI_ResultText(context, "PASS");
    SCPI_ResultText(context, "ACTIVE");
    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultUInt32(context, 0x10000003u);
    SCPI_ResultText(context, "FIELD_SYNC_DEFAULT");
    SCPI_ResultUInt32(context, 0x20000002u);
    SCPI_ResultText(context, "A0>A1>A2>A3>A0");
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultText(context, "");
    SCPI_ResultText(context, "");
    SCPI_ResultText(context, "");
    SCPI_ResultText(context, "OK");
    SCPI_ResultText(context, "");
    SCPI_ResultText(context, "");
    SCPI_ResultText(context, "");
    SCPI_ResultText(context, "NONE");
    return SCPI_RES_OK;
}

scpi_result_t scpi_sync_list_q(scpi_t *context)
{
    SCPI_ResultText(context, "FIELD_SYNC_DEFAULT");
    SCPI_ResultUInt32(context, 0x20000002u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "ALL");
    SCPI_ResultBool(context, TRUE);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sync_active_q(scpi_t *context)
{
    SCPI_ResultText(context, "FIELD_SYNC_DEFAULT");
    SCPI_ResultText(context, "FIELD_SYNC_DEFAULT");
    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultUInt32(context, 0x20000002u);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultText(context, "ACK");
    SCPI_ResultText(context, "PASS");
    return SCPI_RES_OK;
}

scpi_result_t scpi_sync_quality_q(scpi_t *context)
{
    vdc_domain_snapshot_t snapshot;
    const bool has_snapshot = vdc_dpll_manager_get_snapshot(&snapshot);
    const vdc_quality_table_t *quality =
        has_snapshot ? &snapshot.quality : NULL;
    const vdc_error_budget_t *budget =
        has_snapshot ? &snapshot.error_budget : NULL;
    const char *state_text = "UNAVAILABLE";

    if (quality != NULL) {
        switch ((vdc_domain_health_state_t)quality->health_state) {
        case VDC_DOMAIN_HEALTH_HEALTHY:
            state_text = "OK";
            break;
        case VDC_DOMAIN_HEALTH_LOCK_CANDIDATE:
            state_text = "LOCK_CANDIDATE";
            break;
        case VDC_DOMAIN_HEALTH_DEGRADED:
            state_text = "DEGRADED";
            break;
        case VDC_DOMAIN_HEALTH_FAULT:
            state_text = "FAULT";
            break;
        case VDC_DOMAIN_HEALTH_CHECKING:
            state_text = "CHECKING";
            break;
        case VDC_DOMAIN_HEALTH_UNKNOWN:
        default:
            state_text = "UNKNOWN";
            break;
        }
    }

    SCPI_ResultText(context, state_text);
    SCPI_ResultInt32(context, budget != NULL ? budget->last_offset_ns : 0);
    SCPI_ResultUInt32(context, budget != NULL ? budget->rms_offset_ns : 0u);
    SCPI_ResultUInt32(context, budget != NULL ? budget->max_abs_offset_ns : 0u);
    SCPI_ResultInt32(context, budget != NULL ? budget->freq_offset_ppb : 0);
    SCPI_ResultUInt32(context, quality != NULL ? quality->jitter_pk_ns : 0u);
    SCPI_ResultUInt32(context, quality != NULL ? quality->last_sample_age_1e3ns : 0u);
    SCPI_ResultUInt32(context, quality != NULL ? quality->last_reject_code : 0u);
    SCPI_ResultUInt32(context, quality != NULL ? quality->accepted_sample_count : 0u);
    SCPI_ResultUInt32(context, quality != NULL ? quality->rejected_sample_count : 0u);
    SCPI_ResultUInt32(context, quality != NULL ? quality->last_timestamp_resolution_ns : 0u);
    SCPI_ResultUInt32(context, quality != NULL ? quality->health_state : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sync_version_q(scpi_t *context)
{
    SCPI_ResultText(context, "FIELD_SYNC_DEFAULT");
    SCPI_ResultText(context, "FIELD_DEFAULT");
    SCPI_ResultText(context, PROJECT_VERSION_STRING);
    SCPI_ResultText(context, PICO_TARGET_NAME);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sync_override_q(scpi_t *context)
{
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultText(context, "PROFILE");
    SCPI_ResultText(context, "IDLE");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "NONE");
    return SCPI_RES_OK;
}

scpi_result_t scpi_sync_coef_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "PROFILE");
    SCPI_ResultBool(context, TRUE);
    return SCPI_RES_OK;
}

static void scpi_sync_result_u64_parts(scpi_t *context, uint64_t value)
{
    SCPI_ResultUInt32(context, (uint32_t)(value & 0xFFFFFFFFull));
    SCPI_ResultUInt32(context, (uint32_t)(value >> 32u));
}

scpi_result_t scpi_cmd_sync_vdc_status_q(scpi_t *context)
{
    vdc_dpll_manager_vdc_status_t status;
    vdc_dpll_manager_get_vdc_status(&status);

    SCPI_ResultBool(context, status.ready ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.lock_state);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.first_service_ms);
    SCPI_ResultUInt32(context, status.last_service_ms);
    SCPI_ResultUInt32(context, status.sync_seq);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_status_q(scpi_t *context)
{
    vdc_dpll_manager_dpll_status_t status;
    vdc_dpll_manager_get_dpll_status(&status);

    SCPI_ResultBool(context, status.ready ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.state);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.first_service_ms);
    SCPI_ResultUInt32(context, status.last_service_ms);
    SCPI_ResultUInt32(context, status.update_seq);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_tdma_plan_q(scpi_t *context)
{
    uint32_t window_class = VDC_DOMAIN_WINDOW_REFMEM_DATA;
    uint32_t now_lo = 0u;
    uint32_t now_hi = 0u;
    uint64_t now_ns = VDC_DPLL_MANAGER_PLAN_NOW_NS;
    vdc_tdma_window_plan_t plan;
    vdc_gate_result_t gate;

    (void)SCPI_ParamUInt32(context, &window_class, FALSE);
    const scpi_bool_t has_now_lo = SCPI_ParamUInt32(context, &now_lo, FALSE);
    const scpi_bool_t has_now_hi = SCPI_ParamUInt32(context, &now_hi, FALSE);
    if (has_now_lo == TRUE || has_now_hi == TRUE) {
        now_ns = ((uint64_t)now_hi << 32u) | (uint64_t)now_lo;
    }

    if (!vdc_dpll_manager_plan_tdma_window(window_class,
                                           now_ns,
                                           &plan,
                                           &gate)) {
        SCPI_ResultText(context, "REJECTED");
        SCPI_ResultUInt32(context, gate.reject_code);
        SCPI_ResultUInt32(context, gate.reject_slot);
        SCPI_ResultUInt32(context, gate.reject_evidence);
        return SCPI_RES_OK;
    }

    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, plan.window_class);
    SCPI_ResultUInt32(context, plan.schedule_epoch);
    SCPI_ResultUInt32(context, plan.slot_index);
    SCPI_ResultUInt32(context, plan.source_slot_id);
    SCPI_ResultUInt32(context, plan.reference_slot_id);
    scpi_sync_result_u64_parts(context, plan.now_ns);
    scpi_sync_result_u64_parts(context, plan.window_start_ns);
    scpi_sync_result_u64_parts(context, plan.window_end_ns);
    scpi_sync_result_u64_parts(context, plan.guard_start_ns);
    scpi_sync_result_u64_parts(context, plan.guard_end_ns);
    SCPI_ResultUInt32(context, plan.wait_ns);
    SCPI_ResultUInt32(context, plan.late_ns);
    SCPI_ResultUInt32(context, plan.in_guarded_window);
    SCPI_ResultUInt32(context, plan.inside_payload_window);
    SCPI_ResultUInt32(context, plan.missed_current_window);
    SCPI_ResultUInt32(context, plan.schedule_crc32);
    SCPI_ResultUInt32(context, gate.reject_code);
    SCPI_ResultUInt32(context, gate.reject_slot);
    SCPI_ResultUInt32(context, gate.reject_evidence);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_observer(scpi_t *context)
{
    vdc_dpll_manager_sync_io_observer_config_t config = {0};
    uint32_t enabled = 0u;
    uint32_t expected_window_start_lo = 0u;
    uint32_t expected_window_start_hi = 0u;
    uint32_t sample0_lsb = 0u;

    const scpi_bool_t has_enabled =
        SCPI_ParamUInt32(context, &enabled, FALSE);
    if (has_enabled != TRUE || enabled == 0u) {
        config.enabled = false;
        if (!vdc_dpll_manager_configure_sync_io_observer(&config)) {
            return SCPI_RES_ERR;
        }
        goto accepted;
    }

    config.enabled = true;
    if (!scpi_port_read_u32(context, &config.max_words_per_service) ||
        !scpi_port_read_u32(context, &config.rising_event_id) ||
        !scpi_port_read_u32(context, &config.falling_event_id) ||
        !scpi_port_read_u32(context, &config.observed_mask) ||
        !scpi_port_read_u32(context, &config.initial_sample_mask) ||
        !scpi_port_read_u32(context, &config.next_base_time_l32_ns) ||
        !scpi_port_read_u32(context, &config.sample_period_ns) ||
        !scpi_port_read_u32(context, &expected_window_start_lo) ||
        !scpi_port_read_u32(context, &expected_window_start_hi) ||
        !scpi_port_read_u32(context, &config.frame_crc32)) {
        return SCPI_RES_ERR;
    }

    (void)SCPI_ParamUInt32(context, &config.max_backward_ticks, FALSE);
    (void)SCPI_ParamUInt32(context, &config.quality_flags, FALSE);
    (void)SCPI_ParamUInt32(context, &sample0_lsb, FALSE);

    config.expected_window_start_ns =
        ((uint64_t)expected_window_start_hi << 32u) |
        (uint64_t)expected_window_start_lo;
    config.sample0_lsb = sample0_lsb != 0u;

    if (!vdc_dpll_manager_configure_sync_io_observer(&config)) {
        scpi_port_push_exec_error(context, "VDC_OBSERVER_CONFIG");
        return SCPI_RES_ERR;
    }

accepted:
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_observer_q(scpi_t *context)
{
    vdc_dpll_manager_sync_io_observer_status_t status;
    vdc_dpll_manager_get_sync_io_observer_status(&status);

    SCPI_ResultBool(context, status.enabled ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.max_words_per_service);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.raw_word_count);
    SCPI_ResultUInt32(context, status.no_edge_count);
    SCPI_ResultUInt32(context, status.ambiguous_edge_count);
    SCPI_ResultUInt32(context, status.bad_argument_count);
    SCPI_ResultUInt32(context, status.submitted_count);
    SCPI_ResultUInt32(context, status.accepted_count);
    SCPI_ResultUInt32(context, status.rejected_count);
    SCPI_ResultUInt32(context, status.last_capture_result);
    SCPI_ResultUInt32(context, status.last_raw_word);
    SCPI_ResultUInt32(context, status.last_sample_seq);
    SCPI_ResultUInt32(context, status.last_event_id);
    SCPI_ResultUInt32(context, status.last_tick_l32);
    SCPI_ResultUInt32(context, status.last_gate_reject_code);
    SCPI_ResultUInt32(context, status.previous_sample_mask);
    SCPI_ResultUInt32(context, status.next_base_time_l32_ns);
    SCPI_ResultUInt32(context, status.rising_event_id);
    SCPI_ResultUInt32(context, status.falling_event_id);
    SCPI_ResultUInt32(context, status.observed_mask);
    SCPI_ResultUInt32(context, status.initial_sample_mask);
    SCPI_ResultUInt32(context, status.sample_period_ns);
    SCPI_ResultUInt32(context, status.expected_window_start_lo);
    SCPI_ResultUInt32(context, status.expected_window_start_hi);
    SCPI_ResultUInt32(context, status.frame_crc32);
    SCPI_ResultUInt32(context, status.max_backward_ticks);
    SCPI_ResultUInt32(context, status.quality_flags);
    SCPI_ResultUInt32(context, status.sample0_lsb);
    SCPI_ResultUInt32(context, status.schedule_crc32);
    SCPI_ResultUInt32(context, status.dictionary_crc32);
    SCPI_ResultUInt32(context, status.dictionary_entry_count);
    SCPI_ResultUInt32(context, status.dictionary_profile_crc32);
    SCPI_ResultUInt32(context, status.last_edge_index);
    SCPI_ResultUInt32(context, status.last_timestamp_source);
    SCPI_ResultUInt32(context, status.last_timestamp_resolution_ns);
    SCPI_ResultUInt32(context, status.last_timestamp_flags);
    SCPI_ResultUInt32(context, status.last_source_slot_id);
    SCPI_ResultUInt32(context, status.last_reference_slot_id);
    SCPI_ResultUInt32(context, status.last_payload_class);
    return SCPI_RES_OK;
}
