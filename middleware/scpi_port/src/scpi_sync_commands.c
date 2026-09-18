#include "scpi_sync_commands.h"

#include "app.h"
#include "project_config.h"
#include "tdma_runtime_owner.h"
#include "vdc_dpll_manager.h"
#include "vdc_output_delay.h"
#include "vdc_output_timing.h"
#include "vdc_priority_ingress.h"

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
    SCPI_ResultUInt32(context, quality != NULL ? quality->last_sample_age_us : 0u);
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
    vdc_dpll_manager_debug_admission_status_t status;
    vdc_dpll_manager_get_debug_admission_status(&status);
    SCPI_ResultBool(context, status.enabled ? TRUE : FALSE);
    SCPI_ResultText(context, "DEBUG_ADMISSION");
    SCPI_ResultText(context, status.pending ? "PENDING" :
                    (status.enabled ? "ACTIVE" : "IDLE"));
    SCPI_ResultUInt32(context, status.continued_count);
    SCPI_ResultUInt32(context, status.last_gate_code);
    SCPI_ResultUInt32(context, status.last_gate_slot);
    SCPI_ResultUInt32(context, status.last_gate_evidence);
    SCPI_ResultUInt32(context, status.requested_generation);
    SCPI_ResultUInt32(context, status.applied_generation);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_override(scpi_t *context)
{
    uint32_t enabled = 0u;
    uint32_t generation = 0u;
    if (!scpi_port_read_u32(context, &enabled) || enabled > 1u ||
        !vdc_dpll_manager_request_debug_continue(enabled != 0u,
                                                 &generation)) {
        scpi_port_push_exec_error(context, "VDC_DPLL_OVERRIDE");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultBool(context, enabled != 0u ? TRUE : FALSE);
    SCPI_ResultUInt32(context, generation);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sync_coef_q(scpi_t *context)
{
    vdc_dpll_manager_debug_servo_tune_status_t status;
    vdc_dpll_manager_get_debug_servo_tune_status(&status);
    SCPI_ResultInt32(context, status.profile.kp_q16);
    SCPI_ResultInt32(context, status.profile.ki_q16);
    SCPI_ResultUInt32(context, status.profile.update_period_us);
    SCPI_ResultUInt32(context, status.profile.step_threshold_ns);
    SCPI_ResultUInt32(context, status.profile.sanity_freq_limit_ppb);
    SCPI_ResultUInt32(context, status.profile.servo_profile_crc32);
    SCPI_ResultUInt32(context, status.requested_generation);
    SCPI_ResultUInt32(context, status.applied_generation);
    SCPI_ResultBool(context, status.pending ? TRUE : FALSE);
    return SCPI_RES_OK;
}

static void scpi_sync_result_debug_servo_tune(
    scpi_t *context,
    uint32_t generation)
{
    vdc_dpll_manager_debug_servo_tune_status_t status;
    vdc_dpll_manager_get_debug_servo_tune_status(&status);
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, generation);
    SCPI_ResultInt32(context, status.profile.kp_q16);
    SCPI_ResultInt32(context, status.profile.ki_q16);
    SCPI_ResultUInt32(context, status.profile.update_period_us);
    SCPI_ResultUInt32(context, status.profile.step_threshold_ns);
    SCPI_ResultUInt32(context, status.profile.sanity_freq_limit_ppb);
    SCPI_ResultUInt32(context, status.profile.servo_profile_crc32);
}

scpi_result_t scpi_cmd_sync_vdc_dpll_tune(scpi_t *context)
{
    int32_t kp_q16 = 0;
    int32_t ki_q16 = 0;
    uint32_t update_period_us = 0u;
    uint32_t step_threshold_ns = 0u;
    uint32_t sanity_freq_limit_ppb = 0u;
    uint32_t generation = 0u;
    if (SCPI_ParamInt32(context, &kp_q16, TRUE) != TRUE ||
        SCPI_ParamInt32(context, &ki_q16, TRUE) != TRUE ||
        !scpi_port_read_u32(context, &update_period_us) ||
        !scpi_port_read_u32(context, &step_threshold_ns) ||
        !scpi_port_read_u32(context, &sanity_freq_limit_ppb) ||
        !vdc_dpll_manager_request_debug_servo_tune(
            kp_q16, ki_q16, update_period_us, step_threshold_ns,
            sanity_freq_limit_ppb, &generation)) {
        scpi_port_push_exec_error(context, "VDC_DPLL_TUNE");
        return SCPI_RES_ERR;
    }
    scpi_sync_result_debug_servo_tune(context, generation);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_default(scpi_t *context)
{
    uint32_t generation = 0u;
    if (!vdc_dpll_manager_request_default_debug_servo_tune(&generation)) {
        scpi_port_push_exec_error(context, "VDC_DPLL_DEFAULT");
        return SCPI_RES_ERR;
    }
    scpi_sync_result_debug_servo_tune(context, generation);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_store(scpi_t *context)
{
    (void)context;
    if (!vdc_dpll_manager_store_debug_servo_profile()) {
        scpi_port_push_exec_error(context, "VDC_DPLL_STORE");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_role_q(scpi_t *context)
{
    vdc_dpll_manager_dpll_role_status_t status;
    vdc_dpll_manager_get_dpll_role_status(&status);
    SCPI_ResultUInt32(context, status.mode);
    SCPI_ResultUInt32(context, status.follow_master_slot_id);
    SCPI_ResultUInt32(context, status.requested_generation);
    SCPI_ResultUInt32(context, status.applied_generation);
    SCPI_ResultBool(context, status.pending ? TRUE : FALSE);
    return SCPI_RES_OK;
}

/* ROLE? remains the compact configuration readback.  This extended query is
 * intentionally a distinct, append-only diagnostic contract so a follower
 * can prove command consumption or bounded hold without sampling local PI. */
scpi_result_t scpi_cmd_sync_vdc_dpll_role_status_q(scpi_t *context)
{
    vdc_dpll_manager_dpll_role_status_t role;
    vdc_domain_snapshot_t snapshot;
    if (!vdc_dpll_manager_get_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }
    vdc_dpll_manager_get_dpll_role_status(&role);
    SCPI_ResultUInt32(context, role.mode);
    SCPI_ResultUInt32(context, role.follow_master_slot_id);
    SCPI_ResultUInt32(context, role.requested_generation);
    SCPI_ResultUInt32(context, role.applied_generation);
    SCPI_ResultBool(context, role.pending ? TRUE : FALSE);
    SCPI_ResultUInt32(context, snapshot.control.follower_apply_count);
    SCPI_ResultUInt32(context, snapshot.control.follower_no_command_count);
    SCPI_ResultUInt32(context, snapshot.control.follower_wrong_source_count);
    SCPI_ResultUInt32(context, snapshot.control.follower_stale_command_count);
    SCPI_ResultUInt32(context, snapshot.control.follower_invalid_command_count);
    SCPI_ResultUInt32(context, snapshot.control.follower_local_evidence_bypass_count);
    SCPI_ResultUInt32(context, snapshot.control.last_follower_source_slot_id);
    SCPI_ResultUInt32(context,
                      snapshot.control.last_follower_control_generation);
    SCPI_ResultUInt32(context, snapshot.control.last_follower_command_seq);
    SCPI_ResultUInt32(context, snapshot.control.last_follower_quality);
    SCPI_ResultUInt32(context, (uint32_t)(
        snapshot.control.last_follower_effective_vdc_time_ns & UINT32_MAX));
    SCPI_ResultUInt32(context, (uint32_t)(
        snapshot.control.last_follower_effective_vdc_time_ns >> 32u));
    SCPI_ResultUInt32(context, snapshot.control.follower_late_command_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_role(scpi_t *context)
{
    uint32_t mode = 0u;
    uint32_t source = 0u;
    uint32_t generation = 0u;
    if (!scpi_port_read_u32(context, &mode) ||
        !scpi_port_read_u32(context, &source) ||
        !vdc_dpll_manager_request_dpll_role(mode, source, &generation)) {
        scpi_port_push_exec_error(context, "VDC_DPLL_ROLE");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, mode);
    SCPI_ResultUInt32(context, source);
    SCPI_ResultUInt32(context, generation);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_role_store(scpi_t *context)
{
    if (!vdc_dpll_manager_store_dpll_role()) {
        scpi_port_push_exec_error(context, "VDC_DPLL_ROLE_STORE");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_filter_q(scpi_t *context)
{
    vdc_domain_snapshot_t snapshot;
    if (!vdc_dpll_manager_get_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }
    /* This is the closed-loop state, not a product lock claim.  The explicit
     * integrator field lets the host distinguish FLL slope from Type-II
     * filter memory while tuning a debug profile. */
    SCPI_ResultUInt32(context, snapshot.dpll.state);
    SCPI_ResultUInt32(context, snapshot.dpll.update_seq);
    SCPI_ResultInt32(context, snapshot.dpll.last_phase_error_ns);
    SCPI_ResultInt32(context, snapshot.dpll.last_frequency_error_ppb);
    SCPI_ResultInt32(context, snapshot.dpll.loop_filter_integrator_ppb);
    SCPI_ResultInt32(context, snapshot.clock.period_adjust_ppb);
    SCPI_ResultUInt32(context, snapshot.quality.consecutive_good_samples);
    SCPI_ResultUInt32(context, snapshot.quality.rejected_sample_count);
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
           resolution_ns <=
               VDC_DOMAIN_DPLL_ADMISSION_TIMESTAMP_RESOLUTION_LIMIT_NS &&
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
    const bool ring_input = snapshot->quality.accepted_sample_count != 0u;
    const bool sync_io_input = observer != NULL && observer->enabled;
    if (!ring_input && !sync_io_input) {
        return SCPI_SYNC_VDC_LOCK_OBSERVER_DISABLED;
    }
    if (!ring_input &&
        (observer->dictionary_entry_count == 0u ||
         observer->dictionary_crc32 == 0u ||
         observer->dictionary_profile_crc32 !=
             snapshot->schedule.schedule_crc32)) {
        return SCPI_SYNC_VDC_LOCK_DICTIONARY_EMPTY;
    }
    if (!scpi_sync_vdc_timestamp_is_dpll_eligible(
            ring_input ? snapshot->quality.last_timestamp_source
                       : observer->last_timestamp_source,
            ring_input ? snapshot->quality.last_timestamp_resolution_ns
                       : observer->last_timestamp_resolution_ns,
            ring_input ? snapshot->quality.last_timestamp_flags
                       : observer->last_timestamp_flags)) {
        return SCPI_SYNC_VDC_LOCK_TIMESTAMP_NOT_ELIGIBLE;
    }
    if (snapshot->quality.accepted_sample_count == 0u) {
        return SCPI_SYNC_VDC_LOCK_NO_ACCEPTED_SAMPLE;
    }
    if (snapshot->quality.last_reject_code != VDC_DOMAIN_GATE_PASS ||
        (!ring_input &&
         observer->last_gate_reject_code != VDC_DOMAIN_GATE_PASS)) {
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

scpi_result_t scpi_cmd_sync_vdc_dco_q(scpi_t *context)
{
    vdc_dpll_manager_dco_consumer_status_t status;
    vdc_dpll_manager_get_dco_consumer_status(&status);

    SCPI_ResultBool(context, status.valid ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.accepted_update_count);
    SCPI_ResultUInt32(context, status.unchanged_count);
    SCPI_ResultUInt32(context, status.invalid_count);
    SCPI_ResultUInt32(context, status.last_error);
    SCPI_ResultUInt32(context, status.last_service_ms);
    SCPI_ResultUInt32(context, status.last_dco_update_seq);
    SCPI_ResultUInt32(context, status.source_model_seq);
    SCPI_ResultUInt32(context, status.lock_state);
    SCPI_ResultInt32(context, status.phase_offset_ns);
    SCPI_ResultInt32(context, status.period_adjust_ppb);
    scpi_sync_result_u64_parts(context, status.base_local_tick64);
    scpi_sync_result_u64_parts(context, status.base_vdc_time64_ns);
    SCPI_ResultUInt32(context, status.nominal_period_ns);
    SCPI_ResultUInt32(context, status.slew_limit_ppb);
    SCPI_ResultUInt32(context, status.tdma_schedule_crc32);
    SCPI_ResultUInt32(context, status.servo_profile_crc32);
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

scpi_result_t scpi_cmd_sync_vdc_tdma_ring_q(scpi_t *context)
{
    vdc_tdma_ring_plan_t plan;

    if (!vdc_dpll_manager_plan_tdma_ring(&plan)) {
        SCPI_ResultText(context, "REJECTED");
        return SCPI_RES_OK;
    }

    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, plan.valid);
    SCPI_ResultUInt32(context, plan.ring_node_count);
    SCPI_ResultUInt32(context, plan.local_slot_id);
    SCPI_ResultUInt32(context, plan.reference_slot_id);
    SCPI_ResultUInt32(context, plan.upstream_slot_id);
    SCPI_ResultUInt32(context, plan.downstream_slot_id);
    SCPI_ResultUInt32(context, plan.feedback_slot_id);
    SCPI_ResultUInt32(context, plan.from_reference_hops);
    SCPI_ResultUInt32(context, plan.to_feedback_hops);
    SCPI_ResultUInt32(context, plan.is_reference_slot);
    SCPI_ResultUInt32(context, plan.ring_flags);
    SCPI_ResultUInt32(context, plan.ring_profile_crc32);
    SCPI_ResultUInt32(context, plan.schedule_crc32);
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

scpi_result_t scpi_cmd_sync_vdc_tdma_phys_q(scpi_t *context)
{
    tdma_pio_spi_phys_snapshot_t snapshot;
    if (!tdma_runtime_owner_get_phys_snapshot(&snapshot)) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }

    SCPI_ResultUInt32(context, snapshot.armed);
    SCPI_ResultUInt32(context, snapshot.role);
    SCPI_ResultUInt32(context, snapshot.baud_hz);
    SCPI_ResultUInt32(context, snapshot.tx_count);
    SCPI_ResultUInt32(context, snapshot.rx_count);
    SCPI_ResultUInt32(context, snapshot.rx_bad_count);
    SCPI_ResultUInt32(context, snapshot.tx_busy_count);
    SCPI_ResultUInt32(context, snapshot.rx_partial_count);
    SCPI_ResultUInt32(context, snapshot.rx_stall_count);
    SCPI_ResultUInt32(context, snapshot.tx_timeout_count);
    SCPI_ResultUInt32(context, snapshot.last_error);
    SCPI_ResultUInt32(context, snapshot.last_rx_size);
    SCPI_ResultUInt32(context, snapshot.tx_sck_pin);
    SCPI_ResultUInt32(context, snapshot.tx_pin);
    SCPI_ResultUInt32(context, snapshot.rx_sck_pin);
    SCPI_ResultUInt32(context, snapshot.rx_pin);
    SCPI_ResultUInt32(context, snapshot.last_bad_header0);
    SCPI_ResultUInt32(context, snapshot.last_bad_header1);
    SCPI_ResultUInt32(context, snapshot.last_bad_header2);
    SCPI_ResultUInt32(context, snapshot.last_bad_header3);
    SCPI_ResultUInt32(context, snapshot.last_bad_words);
    SCPI_ResultUInt32(context, snapshot.rx_busy_count);
    SCPI_ResultUInt32(context, snapshot.rx_magic_fail_count);
    SCPI_ResultUInt32(context, snapshot.rx_busy_word0);
    SCPI_ResultUInt32(context, snapshot.rx_busy_word1);
    SCPI_ResultUInt32(context, snapshot.rx_busy_word2);
    SCPI_ResultUInt32(context, snapshot.rx_busy_word3);
    SCPI_ResultUInt32(context, snapshot.rx_busy_moved);
    SCPI_ResultUInt32(context, snapshot.rx_magic_at_zero);
    SCPI_ResultUInt32(context, snapshot.rx_magic_at_shift);
    SCPI_ResultUInt32(context, snapshot.tx_csn_pin);
    SCPI_ResultUInt32(context, snapshot.rx_csn_pin);
    SCPI_ResultUInt32(context, snapshot.rx_ring_overrun_count);
    SCPI_ResultUInt32(context, snapshot.rx_dma_produced_words);
    SCPI_ResultUInt32(context, snapshot.rx_scan_produced_words);
    SCPI_ResultUInt32(context, snapshot.rx_dma_write_index);
    SCPI_ResultUInt32(context, snapshot.rx_dma_channel);
    SCPI_ResultUInt32(context, snapshot.tx_edge_count);
    SCPI_ResultUInt32(context, snapshot.rx_edge_count);
    scpi_sync_result_u64_parts(context, snapshot.last_tx_edge_timestamp_ns);
    scpi_sync_result_u64_parts(context, snapshot.last_tx_done_timestamp_ns);
    scpi_sync_result_u64_parts(context, snapshot.last_rx_edge_timestamp_ns);
    scpi_sync_result_u64_parts(context, snapshot.last_rx_extract_timestamp_ns);
    SCPI_ResultUInt32(context, snapshot.program_persona);
    SCPI_ResultUInt32(context, snapshot.program_switch_count);
    SCPI_ResultUInt32(context, snapshot.program_switch_fail_count);
    SCPI_ResultInt32(context, snapshot.flight_marker_offset_sample_count);
    SCPI_ResultInt32(context, snapshot.flight_sck_offset_sample_count);
    SCPI_ResultInt32(context, snapshot.flight_data_offset_sample_count);
    SCPI_ResultUInt32(context, snapshot.flight_marker_phase_delay_cycles);
    SCPI_ResultUInt32(context, snapshot.flight_sck_phase_delay_cycles);
    SCPI_ResultUInt32(context, snapshot.flight_data_phase_delay_cycles);
    SCPI_ResultUInt32(context, snapshot.pio_irq_flags);
    SCPI_ResultUInt32(context, snapshot.pio_fdebug);
    SCPI_ResultUInt32(context, snapshot.tx_sm_pc);
    SCPI_ResultUInt32(context, snapshot.rx_sm_pc);
    SCPI_ResultUInt32(context, snapshot.tx_sm_tx_fifo_level);
    SCPI_ResultUInt32(context, snapshot.tx_sm_rx_fifo_level);
    SCPI_ResultUInt32(context, snapshot.rx_sm_tx_fifo_level);
    SCPI_ResultUInt32(context, snapshot.rx_sm_rx_fifo_level);
    SCPI_ResultUInt32(context, snapshot.gpio_input_levels);
    SCPI_ResultUInt32(context, snapshot.origin_done_irq_count);
    SCPI_ResultUInt32(context, snapshot.origin_done_txstall_count);
    SCPI_ResultUInt32(context, snapshot.origin_clock_timeout_count);
    SCPI_ResultUInt32(context, snapshot.origin_data_timeout_count);
    SCPI_ResultUInt32(context, snapshot.origin_recovery_count);
    SCPI_ResultUInt32(context, snapshot.overlay_prepare_count);
    SCPI_ResultUInt32(context, snapshot.overlay_prepare_fail_count);
    SCPI_ResultUInt32(context, snapshot.overlay_replacement_byte_count);
    SCPI_ResultUInt32(context, snapshot.overlay_alignment_byte_shift);
    SCPI_ResultUInt32(context, snapshot.overlay_alignment_bit_shift);
    SCPI_ResultUInt32(context, snapshot.overlay_physical_byte_count);
    SCPI_ResultUInt32(context, snapshot.overlay_last_error);
    SCPI_ResultUInt32(context, snapshot.overlay_tx_dma_remaining);
    SCPI_ResultUInt32(context, snapshot.overlay_tx_dma_busy);
    SCPI_ResultUInt32(context, snapshot.overlay_tx_fifo_level_at_fail);
    SCPI_ResultUInt32(context, snapshot.overlay_prepare_wait_us);
    SCPI_ResultUInt32(context, snapshot.overlay_program_offset);
    SCPI_ResultUInt32(context, snapshot.overlay_tx_dma_read_index);
    SCPI_ResultUInt32(context, snapshot.overlay_tx_dma_ctrl);
    SCPI_ResultUInt32(context, snapshot.overlay_sm_shiftctrl);
    SCPI_ResultUInt32(context, snapshot.overlay_sm_execctrl);
    SCPI_ResultUInt32(context, snapshot.overlay_sm_pc_at_fail);
    SCPI_ResultUInt32(context, snapshot.overlay_pio_ctrl_at_fail);
    SCPI_ResultUInt32(context, snapshot.overlay_pio_fstat_at_fail);
    SCPI_ResultUInt32(context, snapshot.overlay_pio_fdebug_at_fail);
    SCPI_ResultUInt32(context, snapshot.overlay_frame_boundary_count);
    SCPI_ResultUInt32(context, snapshot.overlay_pass_recovery_count);
    SCPI_ResultUInt32(context, snapshot.overlay_late_coalesce_count);
    SCPI_ResultUInt32(context, snapshot.clock_latch_resolution_ns);
    SCPI_ResultUInt32(context, snapshot.clock_latch_count);
    SCPI_ResultUInt32(context, snapshot.clock_latch_miss_count);
    SCPI_ResultUInt32(context, snapshot.program_lifecycle_state);
    SCPI_ResultUInt32(context, snapshot.program_target_persona);
    SCPI_ResultUInt32(context, snapshot.program_previous_persona);
    SCPI_ResultUInt32(context, snapshot.program_transition_seq);
    SCPI_ResultUInt32(context, snapshot.program_lifecycle_error);
    SCPI_ResultUInt32(context, snapshot.overlay_published_generation);
    SCPI_ResultUInt32(context, snapshot.overlay_selected_generation);
    SCPI_ResultUInt32(context, snapshot.overlay_selection_pending);
    SCPI_ResultUInt32(context, snapshot.overlay_reuse_observation_count);
    SCPI_ResultUInt32(context, snapshot.rx_observation_drop_count);
    SCPI_ResultUInt32(context, snapshot.rx_scan_yield_count);
    SCPI_ResultUInt32(context, snapshot.rx_dma_transfer_count);
    SCPI_ResultUInt32(context, snapshot.flight_origin_capture_phase_delay_cycles);
    return SCPI_RES_OK;
}

/* Maintenance readback only. These commands never harvest hardware, advance
 * a queue tail or create a realtime sample through SCPI. Status also exposes
 * a lane left active after the transport STOP; records still require inactive. */
scpi_result_t scpi_cmd_system_tdma_priority_rx_q(scpi_t *context)
{
    tdma_ring_clock_snapshot_t ring;
    tdma_priority_rx_snapshot_t s;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled ||
        ring.adapter_started || !tdma_runtime_owner_get_priority_rx_snapshot(&s)) {
        scpi_port_push_exec_error(context, "TDMA_PRIORITY_READ_REQUIRES_STOP");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.active);
    SCPI_ResultUInt32(context, s.epoch);
    SCPI_ResultUInt32(context, s.irq_count);
    SCPI_ResultUInt32(context, s.publish_count);
    SCPI_ResultUInt32(context, s.overwrite_count);
    SCPI_ResultUInt32(context, s.duplicate_count);
    SCPI_ResultUInt32(context, s.sequence_gap_count);
    SCPI_ResultUInt32(context, s.reject_count);
    SCPI_ResultUInt32(context, s.last_reject);
    SCPI_ResultUInt32(context, s.latest_sequence);
    SCPI_ResultUInt32(context, s.latest_mailbox_seq16);
    SCPI_ResultUInt32(context, s.irq_last_cycles);
    SCPI_ResultUInt32(context, s.irq_max_cycles);
    scpi_sync_result_u64_parts(context, s.irq_total_cycles);
    scpi_sync_result_u64_parts(context, s.last_entry_ticks);
    SCPI_ResultUInt32(context, s.retained_mask);
    for (uint32_t i = 0u; i < TDMA_PRIORITY_RX_CAPACITY; ++i)
        SCPI_ResultUInt32(context, s.sequence[i]);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_priority_rx_budget_q(scpi_t *context)
{
    tdma_ring_clock_snapshot_t ring;
    tdma_priority_rx_snapshot_t lane;
    app_realtime_priority_snapshot_t budget;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled ||
        ring.adapter_started || !tdma_runtime_owner_get_priority_rx_snapshot(&lane) ||
        lane.active || !app_realtime_get_priority_snapshot(&budget)) {
        scpi_port_push_exec_error(context, "TDMA_PRIORITY_BUDGET_REQUIRES_STOP");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, budget.schema);
    SCPI_ResultUInt32(context, budget.candidate_irq_cycles);
    SCPI_ResultUInt32(context, budget.close_lead_cycles);
    SCPI_ResultUInt32(context, budget.physical_min_cycles);
    SCPI_ResultUInt32(context, budget.sample_failures);
    SCPI_ResultUInt32(context, budget.close_misses);
    for (uint32_t i = 0u; i < APP_REALTIME_PHASE_COUNT; ++i) {
        SCPI_ResultUInt32(context, budget.irq_max_cycles[i]);
        SCPI_ResultUInt32(context, budget.background_max_cycles[i]);
        SCPI_ResultUInt32(context, budget.budget_misses[i]);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_priority_rx_timing_q(scpi_t *context)
{
    tdma_ring_clock_snapshot_t ring;
    tdma_priority_rx_snapshot_t lane;
    tdma_priority_rx_timing_t timing;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled ||
        ring.adapter_started || !tdma_runtime_owner_get_priority_rx_snapshot(&lane) ||
        lane.active || !tdma_runtime_owner_get_priority_rx_timing(&timing)) {
        scpi_port_push_exec_error(context, "TDMA_PRIORITY_TIMING_REQUIRES_STOP");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, timing.samples);
    for (uint32_t i = 0u; i < 4u; ++i)
        SCPI_ResultUInt32(context, timing.max_cycles[i]);
    return SCPI_RES_OK;
}

#include "vdc_priority_tx.h"
#include "vdc_priority_rx.h"
#include "vdc_priority_match.h"
#include "vdc_priority_follow.h"
#include "vdc_priority_trace.h"
#include "vdc_priority_phase.h"

scpi_result_t scpi_cmd_vdc_priority_follow_phase(scpi_t *context)
{
    uint32_t enabled;
    if (!SCPI_ParamUInt32(context, &enabled, TRUE) || enabled > 1u ||
        !vdc_dpll_manager_set_priority_follow_phase(enabled != 0u)) {
        scpi_port_push_exec_error(context, "Priority phase configuration rejected");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_follow_phase_q(scpi_t *context)
{
    bool enabled;
    if (!vdc_dpll_manager_try_priority_follow_phase_enabled(&enabled)) {
        scpi_port_push_exec_error(context, "Priority phase configuration unavailable");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, enabled ? 1u : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_follow_phase_status_q(scpi_t *context)
{
    tdma_ring_clock_snapshot_t ring;
    vdc_priority_phase_snapshot_t snapshot;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled ||
        ring.adapter_started || !vdc_dpll_manager_get_priority_follow_phase(&snapshot)) {
        scpi_port_push_exec_error(context, "Priority phase status requires STOP");
        return SCPI_RES_ERR;
    }
    /* Word ABI: signed 64-bit values use little-endian two's-complement parts. */
    for (size_t i = 0u; i < sizeof(snapshot) / sizeof(uint32_t); ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&snapshot + i * sizeof(word), sizeof(word));
        SCPI_ResultUInt32(context, word);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_trace_phase_arm(scpi_t *context)
{
    uint32_t capture_id;
    if (!SCPI_ParamUInt32(context, &capture_id, TRUE) || capture_id == 0u ||
        !vdc_dpll_manager_priority_trace_phase_arm(capture_id)) {
        scpi_port_push_exec_error(context, "Priority phase trace ARM rejected");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, capture_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_trace_summary_phase_arm(scpi_t *context)
{
    uint32_t capture_id;
    if (!SCPI_ParamUInt32(context, &capture_id, TRUE) || capture_id == 0u ||
        !vdc_dpll_manager_priority_trace_summary_arm(capture_id, false)) {
        scpi_port_push_exec_error(context, "Priority summary phase ARM rejected");
        return SCPI_RES_ERR;
    }
    /* Admission only; the existing STOP-only status ACK completes ownership. */
    SCPI_ResultUInt32(context, capture_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_trace_summary_origin_arm(scpi_t *context)
{
    uint32_t capture_id;
    if (!SCPI_ParamUInt32(context, &capture_id, TRUE) || capture_id == 0u ||
        !vdc_dpll_manager_priority_trace_summary_arm(capture_id, true)) {
        scpi_port_push_exec_error(context, "Priority summary origin ARM rejected");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, capture_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_trace_arm(scpi_t *context)
{
    uint32_t capture_id;
    if (!SCPI_ParamUInt32(context, &capture_id, TRUE) || capture_id == 0u ||
        !vdc_dpll_manager_priority_trace_arm(capture_id)) {
        scpi_port_push_exec_error(context, "Priority trace ARM rejected");
        return SCPI_RES_ERR;
    }
    /* Admission only. STOP-only STATUS must acknowledge this request before ARM. */
    SCPI_ResultUInt32(context, capture_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_trace_origin_arm(scpi_t *context)
{
    uint32_t capture_id;
    if (!SCPI_ParamUInt32(context, &capture_id, TRUE) || capture_id == 0u ||
        !vdc_dpll_manager_priority_trace_origin_arm(capture_id)) {
        scpi_port_push_exec_error(context, "Priority origin trace ARM rejected");
        return SCPI_RES_ERR;
    }
    /* Admission only. STOP-only STATUS must acknowledge this request before ARM. */
    SCPI_ResultUInt32(context, capture_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_trace_stop(scpi_t *context)
{
    if (!vdc_dpll_manager_priority_trace_stop()) {
        scpi_port_push_exec_error(context, "Priority trace STOP rejected");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_trace_release(scpi_t *context)
{
    if (!vdc_dpll_manager_priority_trace_release()) {
        scpi_port_push_exec_error(context, "Priority trace RELEASE rejected");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_trace_status_q(scpi_t *context)
{
    tdma_ring_clock_snapshot_t ring;
    vdc_priority_trace_status_t status;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled ||
        ring.adapter_started || !vdc_dpll_manager_get_priority_trace(&status)) {
        scpi_port_push_exec_error(context, "Priority trace status requires STOP");
        return SCPI_RES_ERR;
    }
    _Static_assert(sizeof(status) == 37u * sizeof(uint32_t), "Trace status word schema");
    for (uint32_t i = 0u; i < sizeof(status) / sizeof(uint32_t); ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&status + i * sizeof(word), sizeof(word));
        SCPI_ResultUInt32(context, word);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_trace_read_q(scpi_t *context)
{
    uint32_t capture_id, offset, size, total_bytes, crc32;
    uint8_t data[VDC_PRIORITY_TRACE_READ_MAX_BYTES];
    char hex[sizeof(data) * 2u + 1u];
    static const char digits[] = "0123456789abcdef";
    if (!scpi_port_read_u32(context, &capture_id) || !scpi_port_read_u32(context, &offset) ||
        !scpi_port_read_u32(context, &size) || capture_id == 0u || size == 0u || size > sizeof(data) ||
        !vdc_dpll_manager_priority_trace_read(capture_id, offset, data, size, &total_bytes, &crc32)) {
        scpi_port_push_exec_error(context, "Priority trace READ requires matching frozen capture and range");
        return SCPI_RES_ERR;
    }
    for (uint32_t i = 0u; i < size; ++i) {
        hex[i * 2u] = digits[data[i] >> 4u];
        hex[i * 2u + 1u] = digits[data[i] & 15u];
    }
    hex[size * 2u] = '\0';
    SCPI_ResultUInt32(context, offset);
    SCPI_ResultUInt32(context, size);
    SCPI_ResultUInt32(context, total_bytes);
    SCPI_ResultUInt32(context, crc32);
    SCPI_ResultText(context, hex);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_follow(scpi_t *context)
{
    uint32_t enabled;
    if (!SCPI_ParamUInt32(context, &enabled, TRUE) || enabled > 1u ||
        !vdc_dpll_manager_set_priority_follow(enabled != 0u)) {
        scpi_port_push_exec_error(context, "Priority follow configuration rejected");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_follow_q(scpi_t *context)
{
    bool enabled;
    if (!vdc_dpll_manager_try_priority_follow_enabled(&enabled)) {
        scpi_port_push_exec_error(context, "Priority follow configuration busy");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, enabled ? 1u : 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_output_delay(scpi_t *context)
{
    /* Generic integer conversion saturates and accepts numeric prefixes.
     * Consume the whole decimal ns token, with checked magnitude arithmetic. */
    scpi_parameter_t param;
    if (!SCPI_Parameter(context, &param, TRUE) ||
        param.type != SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA || !param.len) {
        scpi_port_push_exec_error(context, "Output delay requires signed decimal ns");
        return SCPI_RES_ERR;
    }
    const char *text = param.ptr;
    const size_t length = param.len;
    const bool negative = text[0] == '-';
    size_t pos = (negative || text[0] == '+') ? 1u : 0u;
    const uint32_t limit = negative ? UINT32_C(2147483648) : INT32_MAX;
    uint32_t magnitude = 0u;
    bool valid = pos < length;
    for (; valid && pos < length; ++pos) {
        const unsigned char c = (unsigned char)text[pos];
        if (c < '0' || c > '9' || magnitude > (limit - (c - '0')) / 10u) {
            valid = false;
        } else magnitude = magnitude * 10u + (c - '0');
    }
    const int32_t requested = negative ? (int32_t)(-(int64_t)magnitude) : (int32_t)magnitude;
    if (valid) {
        scpi_parameter_t extra;
        valid = !SCPI_Parameter(context, &extra, FALSE) && !SCPI_ParamErrorOccurred(context);
    }
    if (!valid || !vdc_dpll_manager_set_output_delay_ns(requested)) {
        scpi_port_push_exec_error(context, "Output delay requires signed ns and STOP");
        return SCPI_RES_ERR;
    }
    SCPI_ResultInt32(context, requested);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_output_delay_q(scpi_t *context)
{
    int32_t value;
    if (!vdc_dpll_manager_get_output_delay_ns(&value)) {
        scpi_port_push_exec_error(context, "Output delay unavailable");
        return SCPI_RES_ERR;
    }
    SCPI_ResultInt32(context, value);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_output_delay_default(scpi_t *context)
{
    if (!vdc_dpll_manager_default_output_delay()) {
        scpi_port_push_exec_error(context, "Output delay default requires STOP");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_output_delay_recall(scpi_t *context)
{
    if (!vdc_dpll_manager_recall_output_delay()) {
        scpi_port_push_exec_error(context, "Output delay recall requires saved value and STOP");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_output_delay_store(scpi_t *context)
{
    if (!vdc_dpll_manager_store_output_delay()) {
        scpi_port_push_exec_error(context, "Output delay store rejected or failed");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

/* Output timing uses exact decimal us tokens and a complete tuple. Generic
 * integer conversion may accept prefixes or saturate before range checks. */
static bool scpi_output_timing_read_us(scpi_t *context, uint32_t *value)
{
    scpi_parameter_t param;
    if (!SCPI_Parameter(context, &param, TRUE) ||
        param.type != SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA || param.len <= 0) return false;
    /* Match RUN's strict boundary: this lexer can shorten aggregate length
     * after numeric trailing whitespace and truncate a later parameter. */
    if (context->param_list.lex_state.pos != param.ptr + param.len) return false;
    const size_t length = (size_t)param.len;
    size_t pos = param.ptr[0] == '+' ? 1u : 0u;
    if (pos == length) return false;
    uint32_t parsed = 0u;
    for (; pos < length; ++pos) {
        const unsigned char digit = (unsigned char)param.ptr[pos];
        if (digit < '0' || digit > '9' || parsed > (UINT32_MAX - (digit - '0')) / 10u) return false;
        parsed = parsed * 10u + (digit - '0');
    }
    *value = parsed;
    return true;
}

static bool scpi_output_timing_no_extra(scpi_t *context)
{
    scpi_parameter_t extra;
    return !SCPI_Parameter(context, &extra, FALSE) && !SCPI_ParamErrorOccurred(context);
}

scpi_result_t scpi_cmd_vdc_output_timing(scpi_t *context)
{
    vdc_output_timing_profile_t profile;
    if (!scpi_output_timing_read_us(context, &profile.plan_ahead_us) ||
        !scpi_output_timing_read_us(context, &profile.commit_ahead_us) ||
        !scpi_output_timing_read_us(context, &profile.refill_low_us) ||
        !scpi_output_timing_no_extra(context) || !vdc_output_timing_profile_valid(&profile) ||
        !vdc_dpll_manager_set_output_timing_profile(&profile)) {
        scpi_port_push_exec_error(context, "Output timing requires plan,commit,low decimal us, STOP and idle output");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, profile.plan_ahead_us);
    SCPI_ResultUInt32(context, profile.commit_ahead_us);
    SCPI_ResultUInt32(context, profile.refill_low_us);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_output_timing_q(scpi_t *context)
{
    vdc_output_timing_profile_t profile;
    if (!scpi_output_timing_no_extra(context) || !vdc_dpll_manager_get_output_timing_profile(&profile)) {
        scpi_port_push_exec_error(context, "Output timing unavailable");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, profile.plan_ahead_us);
    SCPI_ResultUInt32(context, profile.commit_ahead_us);
    SCPI_ResultUInt32(context, profile.refill_low_us);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_output_timing_default(scpi_t *context)
{
    if (!scpi_output_timing_no_extra(context) || !vdc_dpll_manager_default_output_timing()) {
        scpi_port_push_exec_error(context, "Output timing default requires STOP and idle output");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_output_timing_recall(scpi_t *context)
{
    if (!scpi_output_timing_no_extra(context) || !vdc_dpll_manager_recall_output_timing()) {
        scpi_port_push_exec_error(context, "Output timing recall requires valid saved profile, STOP and idle output");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_output_timing_store(scpi_t *context)
{
    if (!scpi_output_timing_no_extra(context) || !vdc_dpll_manager_store_output_timing()) {
        scpi_port_push_exec_error(context, "Output timing store rejected or failed");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_follow_baseline(scpi_t *context)
{
    vdc_priority_follow_baseline_config_t config;
    if (!scpi_port_read_u32(context, &config.max_replacements) ||
        !scpi_port_read_u32(context, &config.window_ns) ||
        !vdc_dpll_manager_set_priority_follow_baseline(&config)) {
        scpi_port_push_exec_error(context, "Priority baseline requires valid parameters and STOP");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, config.max_replacements);
    SCPI_ResultUInt32(context, config.window_ns);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_follow_baseline_q(scpi_t *context)
{
    vdc_priority_follow_baseline_config_t config;
    if (!vdc_dpll_manager_get_priority_follow_baseline(&config)) {
        scpi_port_push_exec_error(context, "Priority baseline unavailable");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, config.max_replacements);
    SCPI_ResultUInt32(context, config.window_ns);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_follow_baseline_default(scpi_t *context)
{
    if (!vdc_dpll_manager_default_priority_follow_baseline()) {
        scpi_port_push_exec_error(context, "Priority baseline default requires STOP");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_follow_baseline_recall(scpi_t *context)
{
    if (!vdc_dpll_manager_recall_priority_follow_baseline()) {
        scpi_port_push_exec_error(context, "Priority baseline recall requires valid saved profile and STOP");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_follow_baseline_store(scpi_t *context)
{
    if (!vdc_dpll_manager_store_priority_follow_baseline()) {
        scpi_port_push_exec_error(context, "Priority baseline store rejected or failed");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_follow_status_q(scpi_t *context)
{
    tdma_ring_clock_snapshot_t ring;
    vdc_priority_follow_snapshot_t s;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled ||
        ring.adapter_started || !vdc_dpll_manager_get_priority_follow(&s)) {
        scpi_port_push_exec_error(context, "Priority follow evidence requires STOP");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.active);
    SCPI_ResultUInt32(context, s.mode);
    SCPI_ResultUInt32(context, s.state);
    SCPI_ResultUInt32(context, s.last_reason);
    SCPI_ResultUInt32(context, s.request);
    SCPI_ResultUInt32(context, s.session);
    SCPI_ResultUInt32(context, s.generation);
    SCPI_ResultUInt32(context, s.calls);
    SCPI_ResultUInt32(context, s.tickets);
    SCPI_ResultUInt32(context, s.baselines);
    SCPI_ResultUInt32(context, s.waits);
    SCPI_ResultUInt32(context, s.prepared);
    SCPI_ResultUInt32(context, s.applied);
    SCPI_ResultUInt32(context, s.no_adjust);
    SCPI_ResultUInt32(context, s.rejected);
    SCPI_ResultUInt32(context, s.cancelled);
    SCPI_ResultUInt32(context, s.repeated);
    SCPI_ResultUInt32(context, s.baseline_sequence);
    SCPI_ResultUInt32(context, s.event_sequence);
    SCPI_ResultUInt32(context, s.carrier_sequence);
    SCPI_ResultUInt32(context, s.observer_epoch);
    SCPI_ResultUInt32(context, s.rx_epoch);
    SCPI_ResultUInt32(context, s.model_token);
    SCPI_ResultUInt32(context, s.expected_dco_seq);
    SCPI_ResultUInt32(context, s.before_dco_seq);
    SCPI_ResultUInt32(context, s.after_dco_seq);
    SCPI_ResultUInt32(context, s.local_slot);
    SCPI_ResultUInt32(context, s.reference_slot);
    SCPI_ResultUInt32(context, s.reserved);
    SCPI_ResultUInt32(context, (uint32_t)s.before_ppb);
    SCPI_ResultUInt32(context, (uint32_t)s.after_ppb);
    SCPI_ResultUInt32(context, (uint32_t)s.selected_delta_ppb);
    SCPI_ResultUInt32(context, (uint32_t)s.reserved_signed);
    scpi_sync_result_u64_parts(context, s.arm_epoch);
    scpi_sync_result_u64_parts(context, s.baseline_raw);
    scpi_sync_result_u64_parts(context, s.raw_lo);
    scpi_sync_result_u64_parts(context, s.raw_hi);
    scpi_sync_result_u64_parts(context, s.interval_lo);
    scpi_sync_result_u64_parts(context, s.interval_hi);
    scpi_sync_result_u64_parts(context, s.local_interval_lo);
    scpi_sync_result_u64_parts(context, s.local_interval_hi);
    scpi_sync_result_u64_parts(context, (uint64_t)s.error_lo_ppb);
    scpi_sync_result_u64_parts(context, (uint64_t)s.error_hi_ppb);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_match(scpi_t *context)
{
    uint32_t generation;
    if (!SCPI_ParamUInt32(context, &generation, TRUE) ||
        !vdc_dpll_manager_set_priority_match(generation)) {
        scpi_port_push_exec_error(context, "Priority match configuration rejected");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, generation);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_match_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, vdc_dpll_manager_priority_match_generation());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_match_status_q(scpi_t *context)
{
    tdma_ring_clock_snapshot_t ring;
    vdc_priority_match_snapshot_t s;
    /* Ring STOP is authoritative. active describes the last Core1 service. */
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled ||
        ring.adapter_started || !vdc_dpll_manager_get_priority_match(&s)) {
        scpi_port_push_exec_error(context, "Priority match evidence requires STOP");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.generation);
    SCPI_ResultUInt32(context, s.active);
    SCPI_ResultUInt32(context, s.retired);
    SCPI_ResultUInt32(context, s.have_match);
    SCPI_ResultUInt32(context, s.calls);
    SCPI_ResultUInt32(context, s.matched);
    SCPI_ResultUInt32(context, s.repeated);
    SCPI_ResultUInt32(context, s.superseded);
    SCPI_ResultUInt32(context, s.busy);
    SCPI_ResultUInt32(context, s.pending);
    SCPI_ResultUInt32(context, s.history_miss);
    SCPI_ResultUInt32(context, s.rejected);
    SCPI_ResultUInt32(context, s.last_reason);
    SCPI_ResultUInt32(context, s.history_result);
    SCPI_ResultUInt32(context, s.source_slot);
    SCPI_ResultUInt32(context, s.event_sequence);
    SCPI_ResultUInt32(context, s.carrier_sequence);
    SCPI_ResultUInt32(context, s.rx_epoch);
    SCPI_ResultUInt32(context, s.session);
    SCPI_ResultUInt32(context, s.role_generation);
    SCPI_ResultUInt32(context, s.clock_epoch);
    SCPI_ResultUInt32(context, s.clock_run);
    SCPI_ResultUInt32(context, s.model_token);
    SCPI_ResultUInt32(context, s.applied_command_seq);
    SCPI_ResultUInt32(context, s.ring_config_seq);
    SCPI_ResultUInt32(context, s.ring_applied_seq);
    SCPI_ResultUInt32(context, s.local_slot);
    SCPI_ResultUInt32(context, s.reference_slot);
    SCPI_ResultUInt32(context, s.node_count);
    SCPI_ResultUInt32(context, s.schedule_crc32);
    SCPI_ResultUInt32(context, s.profile_crc32);
    SCPI_ResultUInt32(context, s.observer_epoch);
    SCPI_ResultUInt32(context, s.tick_hz);
    SCPI_ResultUInt32(context, s.path_crc32);
    SCPI_ResultUInt32(context, s.delay_ns);
    SCPI_ResultUInt32(context, s.path_direction);
    SCPI_ResultUInt32(context, s.path_qualification);
    scpi_sync_result_u64_parts(context, s.arm_epoch);
    scpi_sync_result_u64_parts(context, s.valid_from_raw);
    scpi_sync_result_u64_parts(context, s.raw_lo);
    scpi_sync_result_u64_parts(context, s.raw_hi);
    scpi_sync_result_u64_parts(context, s.local_lo);
    scpi_sync_result_u64_parts(context, s.local_hi);
    scpi_sync_result_u64_parts(context, s.remote_lo);
    scpi_sync_result_u64_parts(context, s.remote_hi);
    scpi_sync_result_u64_parts(context, s.expected_lo);
    scpi_sync_result_u64_parts(context, s.expected_hi);
    scpi_sync_result_u64_parts(context, (uint64_t)s.residual_lo);
    scpi_sync_result_u64_parts(context, (uint64_t)s.residual_hi);
    return SCPI_RES_OK;
}


scpi_result_t scpi_cmd_vdc_priority_rx_q(scpi_t *context)
{
    tdma_ring_clock_snapshot_t ring;
    vdc_priority_rx_snapshot_t s;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled ||
        ring.adapter_started || !vdc_dpll_manager_get_priority_rx(&s) || s.active) {
        scpi_port_push_exec_error(context, "VDC_PRIORITY_RX_REQUIRES_STOP");
        return SCPI_RES_ERR;
    }
    const uint32_t fields[] = {s.schema, s.active, s.epoch, s.have_record,
        s.carrier_count, s.typed_accept_count, s.typed_reject_count,
        s.duplicate_count, s.unique_count, s.conflict_count, s.last_status,
        s.source_slot, s.target_mask, s.carrier_sequence};
    for (size_t i = 0u; i < sizeof(fields) / sizeof(fields[0]); ++i)
        SCPI_ResultUInt32(context, fields[i]);
    scpi_sync_result_u64_parts(context, s.irq_entry_ticks);
    SCPI_ResultUInt32(context, s.typed_record.binding_generation);
    SCPI_ResultUInt32(context, s.typed_record.event_sequence);
    scpi_sync_result_u64_parts(context, s.typed_record.event_time_lower);
    SCPI_ResultUInt32(context, s.typed_record.uncertainty_width);
    SCPI_ResultUInt32(context, s.typed_record.flags);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_sync(scpi_t *context)
{
    uint32_t generation;
    if (!SCPI_ParamUInt32(context, &generation, TRUE) ||
        !vdc_dpll_manager_set_priority_sync(generation)) {
        scpi_port_push_exec_error(context, "VDC_PRIORITY_SYNC_STOP_SESSION_OR_GENERATION");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, generation);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_sync_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, vdc_dpll_manager_priority_sync_generation());
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_tx_q(scpi_t *context)
{
    tdma_ring_clock_snapshot_t ring;
    vdc_priority_tx_snapshot_t s;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled ||
        ring.adapter_started || !vdc_dpll_manager_get_priority_tx(&s)) {
        scpi_port_push_exec_error(context, "VDC_PRIORITY_TX_REQUIRES_STOP");
        return SCPI_RES_ERR;
    }
    const uint32_t fields[] = {s.schema, s.generation, s.active, s.retired, s.have_offer,
        s.calls, s.encoded, s.repeated, s.rejected, s.last_reject, s.source_epoch,
        s.event_sequence, s.source_identity, s.published_version, s.model_token,
        s.session, s.role_generation, s.clock_epoch, s.clock_run, s.local_slot,
        s.node_count, s.schedule_crc32, s.profile_crc32, s.uncertainty_width};
    for (size_t i = 0u; i < sizeof(fields) / sizeof(fields[0]); ++i)
        SCPI_ResultUInt32(context, fields[i]);
    scpi_sync_result_u64_parts(context, s.event_time_lower);
    static const char digits[] = "0123456789abcdef";
    char hex[TDMA_FLIGHT_SHORT_SLOT_SIZE * 2u + 1u];
    for (size_t i = 0u; i < TDMA_FLIGHT_SHORT_SLOT_SIZE; ++i) {
        hex[2u * i] = digits[s.mailbox[i] >> 4u];
        hex[2u * i + 1u] = digits[s.mailbox[i] & 15u];
    }
    hex[sizeof(hex) - 1u] = '\0';
    SCPI_ResultText(context, hex);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_priority_rx_consumer_q(scpi_t *context)
{
    tdma_ring_clock_snapshot_t ring;
    tdma_priority_rx_snapshot_t lane;
    vdc_priority_ingress_snapshot_t s;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled ||
        ring.adapter_started || !tdma_runtime_owner_get_priority_rx_snapshot(&lane) ||
        lane.active || !vdc_dpll_manager_get_priority_ingress(&s)) {
        scpi_port_push_exec_error(context, "VDC_PRIORITY_CONSUMER_REQUIRES_STOP");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.active);
    SCPI_ResultUInt32(context, s.epoch);
    SCPI_ResultUInt32(context, s.have_record);
    SCPI_ResultUInt32(context, s.service_count);
    SCPI_ResultUInt32(context, s.copied_count);
    SCPI_ResultUInt32(context, s.skipped_carrier_count);
    SCPI_ResultUInt32(context, s.duplicate_polls);
    SCPI_ResultUInt32(context, s.copy_retries);
    SCPI_ResultUInt32(context, s.empty_polls);
    SCPI_ResultUInt32(context, s.order_rejects);
    SCPI_ResultUInt32(context, s.clock_failures);
    SCPI_ResultUInt32(context, s.last_class);
    SCPI_ResultUInt32(context, s.body_last_cycles);
    SCPI_ResultUInt32(context, s.body_max_cycles);
    SCPI_ResultUInt32(context, s.timing_samples);
    scpi_sync_result_u64_parts(context, s.arrival_last_cycles);
    scpi_sync_result_u64_parts(context, s.arrival_max_cycles);
    SCPI_ResultUInt32(context, s.record.epoch);
    SCPI_ResultUInt32(context, s.record.sequence);
    scpi_sync_result_u64_parts(context, s.record.irq_entry_ticks);
    scpi_sync_result_u64_parts(context, s.record.candidate_word);
    static const char digits[] = "0123456789abcdef";
    char hex[TDMA_PRIORITY_RX_MAILBOX_BYTES * 2u + 1u];
    for (uint32_t field = 0u; field < 2u; ++field) {
        const uint8_t *bytes = field ? s.record.mailbox : s.record.header;
        for (uint32_t i = 0u; i < TDMA_PRIORITY_RX_MAILBOX_BYTES; ++i) {
            hex[2u * i] = digits[bytes[i] >> 4u];
            hex[2u * i + 1u] = digits[bytes[i] & 15u];
        }
        hex[sizeof(hex) - 1u] = '\0';
        SCPI_ResultText(context, hex);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_system_tdma_priority_rx_record_q(scpi_t *context)
{
    uint32_t epoch, sequence;
    if (!SCPI_ParamUInt32(context, &epoch, TRUE) ||
        !SCPI_ParamUInt32(context, &sequence, TRUE)) return SCPI_RES_ERR;
    tdma_ring_clock_snapshot_t ring;
    tdma_priority_rx_snapshot_t s;
    tdma_priority_rx_record_t r;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled ||
        ring.adapter_started || !tdma_runtime_owner_get_priority_rx_snapshot(&s) || s.active ||
        !tdma_runtime_owner_copy_priority_rx(epoch, sequence, &r)) {
        scpi_port_push_exec_error(context, "TDMA_PRIORITY_RECORD_STOP_OR_IDENTITY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, r.epoch);
    SCPI_ResultUInt32(context, r.sequence);
    scpi_sync_result_u64_parts(context, r.irq_entry_ticks);
    scpi_sync_result_u64_parts(context, r.candidate_word);
    static const char digits[] = "0123456789abcdef";
    char hex[TDMA_PRIORITY_RX_MAILBOX_BYTES * 2u + 1u];
    _Static_assert(TDMA_PRIORITY_RX_HEADER_BYTES == TDMA_PRIORITY_RX_MAILBOX_BYTES,
        "fixed record fields share the readback scratch buffer");
    for (uint32_t field = 0u; field < 2u; ++field) {
        const uint8_t *bytes = field ? r.mailbox : r.header;
        for (uint32_t i = 0u; i < TDMA_PRIORITY_RX_MAILBOX_BYTES; ++i) {
            hex[2u * i] = digits[bytes[i] >> 4u];
            hex[2u * i + 1u] = digits[bytes[i] & 15u];
        }
        hex[sizeof(hex) - 1u] = '\0';
        SCPI_ResultText(context, hex);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_path_delay_q(scpi_t *context)
{
    vdc_domain_snapshot_t snapshot;
    vdc_path_delay_entry_t entry;
    uint32_t source_slot_id = 0u;
    uint32_t reference_slot_id = 0u;
    const bool has_snapshot = vdc_dpll_manager_get_snapshot(&snapshot);

    if (!has_snapshot) {
        SCPI_ResultText(context, "UNAVAILABLE");
        return SCPI_RES_OK;
    }

    source_slot_id = snapshot.schedule.local_slot_id;
    reference_slot_id = snapshot.schedule.reference_slot_id;
    (void)SCPI_ParamUInt32(context, &source_slot_id, FALSE);
    (void)SCPI_ParamUInt32(context, &reference_slot_id, FALSE);

    if (!vdc_domain_observation_path_delay_lookup(&snapshot.path_delay,
                                                  source_slot_id,
                                                  reference_slot_id,
                                                  &entry)) {
        SCPI_ResultText(context, "MISSING");
        SCPI_ResultUInt32(context, source_slot_id);
        SCPI_ResultUInt32(context, reference_slot_id);
        SCPI_ResultUInt32(context, snapshot.path_delay.version);
        SCPI_ResultUInt32(context, snapshot.path_delay.update_seq);
        SCPI_ResultUInt32(context, snapshot.path_delay.entry_count);
        SCPI_ResultUInt32(context, snapshot.path_delay.schedule_crc32);
        SCPI_ResultUInt32(context, snapshot.path_delay.table_crc32);
        return SCPI_RES_OK;
    }

    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, snapshot.path_delay.version);
    SCPI_ResultUInt32(context, snapshot.path_delay.update_seq);
    SCPI_ResultUInt32(context, snapshot.path_delay.entry_count);
    SCPI_ResultUInt32(context, snapshot.path_delay.schedule_crc32);
    SCPI_ResultUInt32(context, snapshot.path_delay.table_crc32);
    SCPI_ResultUInt32(context, entry.valid);
    SCPI_ResultUInt32(context, entry.source_slot_id);
    SCPI_ResultUInt32(context, entry.reference_slot_id);
    SCPI_ResultUInt32(context, entry.direction);
    SCPI_ResultUInt32(context, entry.delay_ns);
    SCPI_ResultUInt32(context, entry.jitter_ns);
    SCPI_ResultUInt32(context, entry.stddev_ns);
    SCPI_ResultUInt32(context, entry.cal_crc32);
    SCPI_ResultUInt32(context, entry.freshness_us);
    SCPI_ResultUInt32(context, entry.writer);
    SCPI_ResultUInt32(context, entry.update_seq);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_provisional(scpi_t *context)
{
    uint32_t enabled = 0u;
    if (!scpi_port_read_u32(context, &enabled) || enabled != 1u ||
        !vdc_dpll_manager_activate_tdma_provisional_training()) {
        scpi_port_push_exec_error(context, "VDC_DPLL_PROVISIONAL");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_provisional_q(scpi_t *context)
{
    vdc_domain_snapshot_t snapshot;
    if (!vdc_dpll_manager_get_snapshot(&snapshot)) {
        return SCPI_RES_ERR;
    }
    const bool provisional =
        vdc_domain_path_delay_table_validate_provisional(
            &snapshot.path_delay);
    SCPI_ResultBool(context, provisional ? TRUE : FALSE);
    SCPI_ResultUInt32(context, snapshot.path_delay.flags);
    SCPI_ResultUInt32(context, snapshot.path_delay.entry_count);
    SCPI_ResultUInt32(context, snapshot.path_delay.calibration_generation);
    SCPI_ResultUInt32(context, snapshot.path_delay.topology_generation);
    SCPI_ResultUInt32(context, snapshot.path_delay.schedule_crc32);
    SCPI_ResultUInt32(context, snapshot.path_delay.table_crc32);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_trace_arm(scpi_t *context)
{
    if (!vdc_dpll_manager_dpll_capture_arm()) {
        scpi_port_push_exec_error(context, "VDC_DPLL_TRACE_ARM");
        return SCPI_RES_ERR;
    }
    vdc_dpll_manager_dpll_capture_status_t status;
    vdc_dpll_manager_get_dpll_capture_status(&status);
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, status.sample_count);
    SCPI_ResultUInt32(context, VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_trace_stop(scpi_t *context)
{
    if (!vdc_dpll_manager_dpll_capture_stop()) {
        scpi_port_push_exec_error(context, "VDC_DPLL_TRACE_STOP");
        return SCPI_RES_ERR;
    }
    vdc_dpll_manager_dpll_capture_status_t status;
    vdc_dpll_manager_get_dpll_capture_status(&status);
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, status.sample_count);
    SCPI_ResultUInt32(context, status.dropped_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_trace_status_q(scpi_t *context)
{
    vdc_dpll_manager_dpll_capture_status_t status;
    vdc_dpll_manager_get_dpll_capture_status(&status);
    SCPI_ResultBool(context, status.armed ? TRUE : FALSE);
    SCPI_ResultBool(context, status.complete ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.sample_count);
    SCPI_ResultUInt32(context, status.dropped_count);
    SCPI_ResultUInt32(context, status.first_update_seq);
    SCPI_ResultUInt32(context, status.last_update_seq);
    SCPI_ResultUInt32(context, status.start_ms);
    SCPI_ResultUInt32(context, status.end_ms);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_trace_read_q(scpi_t *context)
{
    uint32_t offset, size, total_bytes, crc32;
    uint8_t data[VDC_DPLL_MANAGER_DPLL_CAPTURE_READ_MAX_BYTES];
    char hex[sizeof(data) * 2u + 1u];
    static const char digits[] = "0123456789abcdef";
    if (!scpi_port_read_u32(context, &offset) || !scpi_port_read_u32(context, &size) ||
        size == 0u || size > sizeof(data) ||
        !vdc_dpll_manager_dpll_capture_read(offset, data, size, &total_bytes, &crc32)) {
        scpi_port_push_exec_error(context, "VDC_DPLL_TRACE_READ_STOP_FROZEN_OR_RANGE");
        return SCPI_RES_ERR;
    }
    for (uint32_t i = 0u; i < size; ++i) {
        hex[i * 2u] = digits[data[i] >> 4u];
        hex[i * 2u + 1u] = digits[data[i] & 15u];
    }
    hex[size * 2u] = '\0';
    SCPI_ResultUInt32(context, offset);
    SCPI_ResultUInt32(context, size);
    SCPI_ResultUInt32(context, total_bytes);
    SCPI_ResultUInt32(context, crc32);
    SCPI_ResultText(context, hex);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_dpll_trace_save(scpi_t *context)
{
    uint32_t job_id = 0u;
    char path[96];
    if (!vdc_dpll_manager_dpll_capture_save(&job_id, path, sizeof(path))) {
        scpi_port_push_exec_error(context, "VDC_DPLL_TRACE_SAVE");
        return SCPI_RES_ERR;
    }
    vdc_dpll_manager_dpll_capture_status_t status;
    vdc_dpll_manager_get_dpll_capture_status(&status);
    SCPI_ResultText(context, "QUEUED");
    SCPI_ResultUInt32(context, job_id);
    SCPI_ResultText(context, path);
    SCPI_ResultUInt32(context, status.sample_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_observer_waveform_arm(scpi_t *context)
{
    if (!vdc_dpll_manager_waveform_capture_arm()) {
        scpi_port_push_exec_error(context, "VDC_OBSERVER_WAVEFORM_ARM");
        return SCPI_RES_ERR;
    }
    vdc_dpll_manager_waveform_capture_status_t status;
    vdc_dpll_manager_get_waveform_capture_status(&status);
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, status.session_id);
    SCPI_ResultUInt32(context, VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_observer_waveform_stop(scpi_t *context)
{
    if (!vdc_dpll_manager_waveform_capture_stop()) {
        scpi_port_push_exec_error(context, "VDC_OBSERVER_WAVEFORM_STOP");
        return SCPI_RES_ERR;
    }
    vdc_dpll_manager_waveform_capture_status_t status;
    vdc_dpll_manager_get_waveform_capture_status(&status);
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, status.record_count);
    SCPI_ResultUInt32(context, status.dropped_count);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_observer_waveform_status_q(scpi_t *context)
{
    vdc_dpll_manager_waveform_capture_status_t status;
    vdc_dpll_manager_get_waveform_capture_status(&status);
    SCPI_ResultBool(context, status.armed ? TRUE : FALSE);
    SCPI_ResultBool(context, status.stopping ? TRUE : FALSE);
    SCPI_ResultBool(context, status.complete ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.session_id);
    SCPI_ResultUInt32(context, status.record_count);
    SCPI_ResultUInt32(context, status.dropped_count);
    SCPI_ResultUInt32(context, status.source_dropped_count);
    SCPI_ResultUInt32(context, status.segment_count);
    SCPI_ResultUInt32(context, status.pending_record_count);
    SCPI_ResultUInt32(context, status.first_sample_seq);
    SCPI_ResultUInt32(context, status.last_sample_seq);
    SCPI_ResultUInt32(context, status.start_ms);
    SCPI_ResultUInt32(context, status.end_ms);
    SCPI_ResultUInt32(context, status.last_error);
    SCPI_ResultUInt32(context, status.last_job_id);
    SCPI_ResultText(context, status.last_path);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_observer_waveform_save(scpi_t *context)
{
    uint32_t segment_count = 0u;
    char prefix[96];
    if (!vdc_dpll_manager_waveform_capture_manifest(
            prefix, sizeof(prefix), &segment_count)) {
        scpi_port_push_exec_error(context, "VDC_OBSERVER_WAVEFORM_SAVE");
        return SCPI_RES_ERR;
    }
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, segment_count);
    SCPI_ResultText(context, prefix);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_sync_vdc_observer_ring_q(scpi_t *context)
{
    vdc_dpll_manager_ring_observer_status_t status;
    vdc_dpll_manager_get_ring_observer_status(&status);
    SCPI_ResultUInt32(context, status.service_count);
    SCPI_ResultUInt32(context, status.snapshot_count);
    SCPI_ResultUInt32(context, status.eligible_count);
    SCPI_ResultUInt32(context, status.path_count);
    SCPI_ResultUInt32(context, status.expand_count);
    SCPI_ResultUInt32(context, status.submitted_count);
    SCPI_ResultUInt32(context, status.accepted_count);
    SCPI_ResultUInt32(context, status.rejected_count);
    SCPI_ResultUInt32(context, status.last_sequence);
    SCPI_ResultUInt32(context, status.last_config_seq);
    SCPI_ResultUInt32(context, status.last_result);
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
    bool ring_input = false;
    bool sync_io_input = false;
    uint32_t timestamp_source = 0u;
    uint32_t timestamp_resolution_ns = 0u;
    uint32_t timestamp_flags = 0u;
    scpi_sync_vdc_lock_readiness_reason_t reason;

    vdc_dpll_manager_get_sync_io_observer_status(&observer);
    ring_input = has_snapshot &&
                 snapshot.quality.accepted_sample_count != 0u;
    sync_io_input = observer.enabled;
    timestamp_source = ring_input
        ? snapshot.quality.last_timestamp_source
        : observer.last_timestamp_source;
    timestamp_resolution_ns = ring_input
        ? snapshot.quality.last_timestamp_resolution_ns
        : observer.last_timestamp_resolution_ns;
    timestamp_flags = ring_input
        ? snapshot.quality.last_timestamp_flags
        : observer.last_timestamp_flags;
    timestamp_eligible =
        scpi_sync_vdc_timestamp_is_dpll_eligible(
            timestamp_source,
            timestamp_resolution_ns,
            timestamp_flags);
    reason = scpi_sync_vdc_lock_readiness_reason(has_snapshot,
                                                 has_snapshot ? &snapshot : NULL,
                                                 &observer);
    input_ready = has_snapshot &&
                  (ring_input || sync_io_input) &&
                  (ring_input ||
                   (observer.dictionary_entry_count != 0u &&
                    observer.dictionary_crc32 != 0u &&
                    observer.dictionary_profile_crc32 ==
                        snapshot.schedule.schedule_crc32)) &&
                  timestamp_eligible &&
                  snapshot.quality.accepted_sample_count != 0u &&
                  snapshot.quality.last_reject_code == VDC_DOMAIN_GATE_PASS &&
                  (ring_input ||
                   observer.last_gate_reject_code == VDC_DOMAIN_GATE_PASS);
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
    SCPI_ResultUInt32(context, (ring_input || sync_io_input) ? 1u : 0u);
    SCPI_ResultUInt32(context, observer.submitted_count);
    SCPI_ResultUInt32(context, observer.accepted_count);
    SCPI_ResultUInt32(context, observer.rejected_count);
    SCPI_ResultUInt32(context, ring_input
        ? snapshot.quality.last_reject_code
        : observer.last_gate_reject_code);
    SCPI_ResultUInt32(context, timestamp_source);
    SCPI_ResultUInt32(context, timestamp_resolution_ns);
    SCPI_ResultUInt32(context, timestamp_flags);
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
    uint32_t sample_period_ns = 100u;
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
    config.pulse_period_ns = VDC_DOMAIN_DEFAULT_PERIOD_NS;
    config.pulse_high_ns = 1000u;
    config.pulse_count = VDC_DPLL_MANAGER_SELF_TEST_DEFAULT_PULSES;
    config.frame_crc32 = 0u;
    config.start_delay_ns = 1000000000u;
    config.phase_only = true;
    config.phase_max_span_ns = 500u;
    config.phase_min_stable_rounds = 3u;

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
    {
        uint32_t phase_only = 1u;
        (void)SCPI_ParamUInt32(context, &phase_only, FALSE);
        config.phase_only = phase_only != 0u;
    }
    (void)SCPI_ParamUInt32(context, &config.phase_max_span_ns, FALSE);
    (void)SCPI_ParamUInt32(context, &config.phase_min_stable_rounds, FALSE);

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
    SCPI_ResultUInt32(context, status.phase_max_span_ns);
    SCPI_ResultUInt32(context, status.phase_min_stable_rounds);
    SCPI_ResultUInt32(context, status.scheduled_pulse_count);
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

scpi_result_t scpi_cmd_sync_vdc_observer_phase_q(scpi_t *context)
{
    vdc_dpll_manager_sync_io_observer_status_t status;
    vdc_dpll_manager_get_sync_io_observer_status(&status);

    SCPI_ResultBool(context, status.enabled ? TRUE : FALSE);
    SCPI_ResultUInt32(context, status.phase_round_count);
    SCPI_ResultUInt32(context, status.phase_complete_count);
    SCPI_ResultUInt32(context, status.phase_missing_count);
    SCPI_ResultUInt32(context, status.phase_ambiguous_count);
    SCPI_ResultUInt32(context, status.phase_last_edge_mask);
    SCPI_ResultUInt32(context, status.phase_last_span_ns);
    for (uint32_t channel = 0u; channel < 4u; channel++) {
        SCPI_ResultInt32(context, status.phase_last_offset_ns[channel]);
    }
    SCPI_ResultUInt32(context, status.phase_initial_span_ns);
    for (uint32_t channel = 0u; channel < 4u; channel++) {
        SCPI_ResultInt32(context, status.phase_initial_offset_ns[channel]);
    }
    SCPI_ResultUInt32(context, status.phase_peak_span_ns);
    SCPI_ResultUInt32(context, status.phase_min_span_ns);
    SCPI_ResultUInt32(context, status.phase_stable_round_count);
    SCPI_ResultUInt32(context, status.phase_stable_streak);
    SCPI_ResultUInt32(context, status.phase_max_stable_streak);
    SCPI_ResultUInt32(context, status.phase_stable_jitter_ns);
    SCPI_ResultUInt32(context, status.phase_first_stable_round);
    SCPI_ResultUInt32(context, status.phase_converged);
    SCPI_ResultUInt32(context, status.phase_max_span_ns);
    SCPI_ResultUInt32(context, status.phase_min_stable_rounds);
    SCPI_ResultUInt32(context, status.phase_last_window_start_lo);
    SCPI_ResultUInt32(context, status.phase_last_window_start_hi);
    SCPI_ResultUInt32(context, status.phase_dropped_word_count);
    return SCPI_RES_OK;
}
