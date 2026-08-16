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
    SCPI_ResultUInt32(context, quality != NULL ? quality->lock_quality_tier : 0u);
    SCPI_ResultUInt32(context, quality != NULL ? quality->fine_lock_threshold_ns : 0u);
    SCPI_ResultUInt32(context, quality != NULL ? quality->debug_lock_threshold_ns : 0u);
    SCPI_ResultUInt32(context, quality != NULL ? quality->coarse_lock_threshold_ns : 0u);
    SCPI_ResultUInt32(context, quality != NULL ? quality->lock_acceptance_threshold_ns : 0u);
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

typedef enum {
    SCPI_SYNC_VDC_LOCK_READY = 0u,
    SCPI_SYNC_VDC_LOCK_SNAPSHOT_UNAVAILABLE = 1u,
    SCPI_SYNC_VDC_LOCK_OBSERVER_DISABLED = 2u,
    SCPI_SYNC_VDC_LOCK_DICTIONARY_EMPTY = 3u,
    SCPI_SYNC_VDC_LOCK_NO_ACCEPTED_SAMPLE = 4u,
    SCPI_SYNC_VDC_LOCK_TIMESTAMP_NOT_ELIGIBLE = 5u,
    SCPI_SYNC_VDC_LOCK_GATE_REJECTED = 6u,
    SCPI_SYNC_VDC_LOCK_NOT_LOCKED = 7u,
} scpi_sync_vdc_lock_readiness_reason_t;

static bool scpi_sync_vdc_timestamp_is_dpll_eligible(uint32_t source,
                                                     uint32_t resolution_ns,
                                                     uint32_t flags)
{
    return source == VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK &&
           resolution_ns > 0u &&
           resolution_ns <= VDC_DOMAIN_DEFAULT_TIMESTAMP_RESOLUTION_LIMIT_NS &&
           (flags & VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE) != 0u &&
           (flags & VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0u;
}

static scpi_sync_vdc_lock_readiness_reason_t scpi_sync_vdc_lock_readiness_reason(
    bool has_snapshot,
    const vdc_domain_snapshot_t *snapshot,
    const vdc_dpll_manager_sync_io_observer_status_t *observer)
{
    if (!has_snapshot || snapshot == NULL || snapshot->quality.valid == 0u) {
        return SCPI_SYNC_VDC_LOCK_SNAPSHOT_UNAVAILABLE;
    }
    if (observer == NULL || !observer->enabled) {
        return SCPI_SYNC_VDC_LOCK_OBSERVER_DISABLED;
    }
    if (observer->dictionary_entry_count == 0u ||
        observer->dictionary_crc32 == 0u ||
        observer->dictionary_profile_crc32 != snapshot->schedule.schedule_crc32) {
        return SCPI_SYNC_VDC_LOCK_DICTIONARY_EMPTY;
    }
    if (!scpi_sync_vdc_timestamp_is_dpll_eligible(
            observer->last_timestamp_source,
            observer->last_timestamp_resolution_ns,
            observer->last_timestamp_flags)) {
        return SCPI_SYNC_VDC_LOCK_TIMESTAMP_NOT_ELIGIBLE;
    }
    if (snapshot->quality.accepted_sample_count == 0u) {
        return SCPI_SYNC_VDC_LOCK_NO_ACCEPTED_SAMPLE;
    }
    if (snapshot->quality.last_reject_code != VDC_DOMAIN_GATE_PASS ||
        observer->last_gate_reject_code != VDC_DOMAIN_GATE_PASS) {
        return SCPI_SYNC_VDC_LOCK_GATE_REJECTED;
    }
    if (snapshot->dpll.state != VDC_DOMAIN_LOCK_LOCKED) {
        return SCPI_SYNC_VDC_LOCK_NOT_LOCKED;
    }
    return SCPI_SYNC_VDC_LOCK_READY;
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

scpi_result_t scpi_cmd_sync_vdc_tdma_status_q(scpi_t *context)
{
    tdma_service_snapshot_t snapshot;
    if (!vdc_dpll_manager_get_tdma_snapshot(&snapshot)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }

    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, snapshot.state);
    SCPI_ResultUInt32(context, snapshot.owner_core);
    SCPI_ResultUInt32(context, snapshot.armed);
    SCPI_ResultUInt32(context, snapshot.service_count);
    SCPI_ResultUInt32(context, snapshot.intent_seq);
    SCPI_ResultUInt32(context, snapshot.completed_seq);
    SCPI_ResultUInt32(context, snapshot.intent_type);
    SCPI_ResultUInt32(context, snapshot.frame_class);
    SCPI_ResultUInt32(context, snapshot.payload_class);
    SCPI_ResultUInt32(context, snapshot.ready_count);
    SCPI_ResultUInt32(context, snapshot.timeout_count);
    SCPI_ResultUInt32(context, snapshot.overrun_count);
    SCPI_ResultUInt32(context, snapshot.reject_count);
    SCPI_ResultUInt32(context, snapshot.last_result);
    SCPI_ResultUInt32(context, snapshot.last_error);
    SCPI_ResultUInt32(context, snapshot.timestamp_source);
    SCPI_ResultUInt32(context, snapshot.timestamp_resolution_ns);
    SCPI_ResultUInt32(context, snapshot.timestamp_flags);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_lock_readiness_q(scpi_t *context)
{
    vdc_domain_snapshot_t snapshot;
    vdc_dpll_manager_sync_io_observer_status_t observer;
    const bool has_snapshot = vdc_dpll_manager_get_snapshot(&snapshot);
    bool timestamp_eligible = false;
    bool input_ready = false;
    bool locked = false;
    scpi_sync_vdc_lock_readiness_reason_t reason;

    vdc_dpll_manager_get_sync_io_observer_status(&observer);
    timestamp_eligible =
        scpi_sync_vdc_timestamp_is_dpll_eligible(
            observer.last_timestamp_source,
            observer.last_timestamp_resolution_ns,
            observer.last_timestamp_flags);
    reason = scpi_sync_vdc_lock_readiness_reason(has_snapshot,
                                                 has_snapshot ? &snapshot : NULL,
                                                 &observer);
    input_ready = has_snapshot &&
                  observer.enabled &&
                  observer.dictionary_entry_count != 0u &&
                  observer.dictionary_crc32 != 0u &&
                  observer.dictionary_profile_crc32 ==
                      snapshot.schedule.schedule_crc32 &&
                  timestamp_eligible &&
                  snapshot.quality.accepted_sample_count != 0u &&
                  snapshot.quality.last_reject_code == VDC_DOMAIN_GATE_PASS &&
                  observer.last_gate_reject_code == VDC_DOMAIN_GATE_PASS;
    locked = has_snapshot &&
             snapshot.dpll.state == VDC_DOMAIN_LOCK_LOCKED &&
             input_ready;

    SCPI_ResultBool(context, input_ready ? TRUE : FALSE);
    SCPI_ResultBool(context, locked ? TRUE : FALSE);
    SCPI_ResultUInt32(context, (uint32_t)reason);
    SCPI_ResultUInt32(context, has_snapshot ? snapshot.dpll.state : 0u);
    SCPI_ResultUInt32(context, has_snapshot ? snapshot.quality.health_state : 0u);
    SCPI_ResultUInt32(context, has_snapshot ? snapshot.quality.accepted_sample_count : 0u);
    SCPI_ResultUInt32(context, has_snapshot ? snapshot.quality.rejected_sample_count : 0u);
    SCPI_ResultUInt32(context, has_snapshot ? snapshot.quality.last_reject_code : 0u);
    SCPI_ResultUInt32(context, observer.enabled ? 1u : 0u);
    SCPI_ResultUInt32(context, observer.submitted_count);
    SCPI_ResultUInt32(context, observer.accepted_count);
    SCPI_ResultUInt32(context, observer.rejected_count);
    SCPI_ResultUInt32(context, observer.last_gate_reject_code);
    SCPI_ResultUInt32(context, observer.last_timestamp_source);
    SCPI_ResultUInt32(context, observer.last_timestamp_resolution_ns);
    SCPI_ResultUInt32(context, observer.last_timestamp_flags);
    SCPI_ResultBool(context, timestamp_eligible ? TRUE : FALSE);
    SCPI_ResultUInt32(context, observer.dictionary_entry_count);
    SCPI_ResultUInt32(context, observer.dictionary_crc32);
    SCPI_ResultUInt32(context, observer.dictionary_profile_crc32);
    SCPI_ResultUInt32(context, has_snapshot ? snapshot.schedule.schedule_crc32 : 0u);
    SCPI_ResultUInt32(context, observer.last_payload_class);
    SCPI_ResultUInt32(context, observer.last_source_slot_id);
    SCPI_ResultUInt32(context, observer.last_reference_slot_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_observer_tdma(scpi_t *context)
{
    uint32_t enabled = 1u;
    uint32_t initial_sample_mask = 0u;
    uint32_t sample_period_ns = 1000u;
    uint32_t frame_crc32 = 0u;

    (void)SCPI_ParamUInt32(context, &enabled, FALSE);
    (void)SCPI_ParamUInt32(context, &initial_sample_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &sample_period_ns, FALSE);
    (void)SCPI_ParamUInt32(context, &frame_crc32, FALSE);

    if (!vdc_dpll_manager_configure_sync_io_observer_tdma(
            enabled != 0u,
            initial_sample_mask,
            sample_period_ns,
            frame_crc32)) {
        scpi_port_push_exec_error(context, "VDC_OBSERVER_TDMA_CONFIG");
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_observer_tdma_selftest(scpi_t *context)
{
    vdc_dpll_manager_observation_self_test_config_t config = {0};

    config.role = VDC_DPLL_MANAGER_SELF_TEST_ROLE_RX;
    config.output_index = 0u;
    config.observed_mask = 1u;
    config.initial_sample_mask = 0u;
    config.sample_period_ns = 100u;
    config.pulse_period_ns = 2000u;
    config.pulse_high_ns = 1000u;
    config.pulse_count = VDC_DPLL_MANAGER_SELF_TEST_MAX_PULSES;
    config.frame_crc32 = 0u;
    config.start_delay_ns = 1000000000u;

    (void)SCPI_ParamUInt32(context, &config.role, FALSE);
    (void)SCPI_ParamUInt32(context, &config.output_index, FALSE);
    (void)SCPI_ParamUInt32(context, &config.observed_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &config.initial_sample_mask, FALSE);
    (void)SCPI_ParamUInt32(context, &config.sample_period_ns, FALSE);
    (void)SCPI_ParamUInt32(context, &config.pulse_period_ns, FALSE);
    (void)SCPI_ParamUInt32(context, &config.pulse_high_ns, FALSE);
    (void)SCPI_ParamUInt32(context, &config.pulse_count, FALSE);
    (void)SCPI_ParamUInt32(context, &config.frame_crc32, FALSE);
    (void)SCPI_ParamUInt32(context, &config.start_delay_ns, FALSE);

    if (!vdc_dpll_manager_start_observation_self_test(&config)) {
        scpi_port_push_exec_error(context, "VDC_OBSERVER_TDMA_SELFTEST");
        return SCPI_RES_ERR;
    }

    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_observer_tdma_selftest_q(scpi_t *context)
{
    vdc_dpll_manager_observation_self_test_status_t status;
    vdc_dpll_manager_get_observation_self_test_status(&status);

    SCPI_ResultBool(context, status.active ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.role);
    SCPI_ResultUInt32(context, status.output_index);
    SCPI_ResultUInt32(context, status.observed_mask);
    SCPI_ResultUInt32(context, status.initial_sample_mask);
    SCPI_ResultUInt32(context, status.sample_period_ns);
    SCPI_ResultUInt32(context, status.pulse_period_ns);
    SCPI_ResultUInt32(context, status.pulse_high_ns);
    SCPI_ResultUInt32(context, status.pulse_count);
    SCPI_ResultUInt32(context, status.frame_crc32);
    SCPI_ResultUInt32(context, status.schedule_crc32);
    SCPI_ResultUInt32(context, status.last_error);
    SCPI_ResultUInt32(context, status.started_ms);
    SCPI_ResultUInt32(context, status.start_delay_ns);
    SCPI_ResultUInt32(context,
                      (uint32_t)(status.first_window_start_ns & 0xFFFFFFFFull));
    SCPI_ResultUInt32(context, (uint32_t)(status.first_window_start_ns >> 32u));
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
