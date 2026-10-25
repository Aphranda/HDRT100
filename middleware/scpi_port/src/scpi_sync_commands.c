#include "scpi_sync_commands_internal.h"
#include "scpi_sync_commands.h"

#include "app.h"
#include "project_config.h"
#include "tdma_runtime_owner.h"
#include "vdc_dpll_manager.h"
#include "vdc_output_delay.h"
#include "vdc_output_timing.h"
#include "vdc_priority_ingress.h"
#include "distributed_refmem.h"

/* The published lock state is LOCKED(1)/UNLOCK(0), written as the two words the
 * system uses. The Domain ladder (vdc_domain_lock_state_t) is the MASTER's
 * internal acquisition process and must not leak into a published state field:
 * a follower's own PI never runs (single-PI rule, VDC_DOMAIN_TODO.md:24), so
 * its ladder rests at CHECKING forever and publishing that value claimed "not
 * locked" for a node that was in fact tracking. One source: the role-aware
 * verdict.
 *
 * The DPLL is dynamic and the outside world can break: this returns what is
 * true now, so a node whose samples stopped, whose gate failed, or whose
 * tracking stopped reads UNLOCK rather than reporting a state it no longer
 * holds. */
uint32_t scpi_sync_vdc_published_lock(void)
{
    vdc_dpll_lock_verdict_t verdict;
    memset(&verdict, 0, sizeof(verdict));
    (void)vdc_dpll_manager_lock_verdict(&verdict);
    return verdict.locked != 0u ? 1u : 0u;
}

scpi_result_t scpi_sync_state_q(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0x20000001u);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultUInt32(context, 0u);
    /* The lock word is the real published state, not a constant. This page used
     * to answer a fixed "LOCKED" whatever the node was doing, which is a false
     * publication (measured 2026-10-14). The remaining fields of this page are
     * still placeholders and are not evidence of anything. */
    SCPI_ResultText(context, scpi_sync_vdc_published_lock() != 0u ? "LOCKED"
                                                                  : "UNLOCK");
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
    const char *state_text = "UNLOCK";

    if (quality != NULL) {
        /* Field 0 is the published lock state written as the two words the
         * system uses: LOCKED when this node is locked right now, UNLOCK when it
         * is not. It is NOT the health ladder: a follower's health reads
         * CHECKING because its own PI never runs (single-PI rule,
         * VDC_DOMAIN_TODO.md:24), which is a health fact, not a lock fact. The
         * raw health value is still published numerically further down this
         * page, so nothing is lost by naming the lock state here.
         *
         * The DPLL is dynamic: if the outside world breaks and samples stop
         * arriving, this word must say UNLOCK - the state is reported as it is
         * now, never as it once was. */
        state_text = scpi_sync_vdc_published_lock() != 0u ? "LOCKED" : "UNLOCK";
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
    /* Field 0 is the published lock state, LOCKED(1)/UNLOCK(0) - not the Domain
     * ladder (the master's internal acquisition process). The remaining fields
     * are the closed loop's own numbers: the explicit integrator field lets the
     * host distinguish FLL slope from Type-II filter memory while tuning a debug
     * profile. */
    SCPI_ResultUInt32(context, scpi_sync_vdc_published_lock());
    SCPI_ResultUInt32(context, snapshot.dpll.update_seq);
    SCPI_ResultInt32(context, snapshot.dpll.last_phase_error_ns);
    SCPI_ResultInt32(context, snapshot.dpll.last_frequency_error_ppb);
    SCPI_ResultInt32(context, snapshot.dpll.loop_filter_integrator_ppb);
    SCPI_ResultInt32(context, snapshot.clock.period_adjust_ppb);
    SCPI_ResultUInt32(context, snapshot.quality.consecutive_good_samples);
    SCPI_ResultUInt32(context, snapshot.quality.rejected_sample_count);
    return SCPI_RES_OK;
}

void scpi_sync_result_u64_parts(scpi_t *context, uint64_t value)
{
    SCPI_ResultUInt32(context, (uint32_t)(value & 0xFFFFFFFFull));
    SCPI_ResultUInt32(context, (uint32_t)(value >> 32u));
}


bool scpi_sync_vdc_timestamp_is_dpll_eligible(uint32_t source,
                                                     uint32_t resolution_ns,
                                                     uint32_t flags)
{
    return vdc_dpll_manager_timestamp_dpll_eligible(source, resolution_ns,
                                                    flags);
}

scpi_result_t scpi_cmd_sync_vdc_status_q(scpi_t *context)
{
    vdc_dpll_manager_vdc_status_t status;
    vdc_dpll_manager_get_vdc_status(&status);

    SCPI_ResultBool(context, status.ready ? TRUE : FALSE);
    SCPI_ResultUInt32(context, scpi_sync_vdc_published_lock());
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
    SCPI_ResultUInt32(context, scpi_sync_vdc_published_lock());
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
    /* Reject attribution by mailbox class: the three buckets partition
     * reject_count; last_reject_class is 0xFF when it could not be attributed. */
    SCPI_ResultUInt32(context, s.first_class_reject_count);
    SCPI_ResultUInt32(context, s.other_class_reject_count);
    SCPI_ResultUInt32(context, s.unknown_class_reject_count);
    SCPI_ResultUInt32(context, s.last_reject_class);
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

static bool scpi_priority_summary_u32(scpi_t *context, uint32_t *value,
    scpi_bool_t required, bool *present)
{
    scpi_parameter_t param;
    *present = SCPI_Parameter(context, &param, required);
    if (!*present) return !required && !SCPI_ParamErrorOccurred(context);
    if (param.type != SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA || param.len <= 0 ||
        context->param_list.lex_state.pos != param.ptr + param.len) return false;
    size_t pos = param.ptr[0] == '+' ? 1u : 0u;
    if (pos == (size_t)param.len) return false;
    uint32_t parsed = 0u;
    for (; pos < (size_t)param.len; ++pos) {
        const unsigned char digit = (unsigned char)param.ptr[pos];
        if (digit < '0' || digit > '9' || parsed > (UINT32_MAX-(digit-'0'))/10u) return false;
        parsed = parsed*10u+(digit-'0');
    }
    *value = parsed;
    return true;
}

static scpi_result_t scpi_priority_summary_arm(scpi_t *context, bool origin)
{
    uint32_t capture_id = 0u, interval_ms = 0u;
    bool present, has_interval;
    scpi_parameter_t extra;
    if (!scpi_priority_summary_u32(context, &capture_id, TRUE, &present) || !capture_id ||
        !scpi_priority_summary_u32(context, &interval_ms, FALSE, &has_interval) ||
        SCPI_Parameter(context, &extra, FALSE) || SCPI_ParamErrorOccurred(context) ||
        (has_interval && (interval_ms < VDC_PRIORITY_TRACE_SUMMARY_INTERVAL_MS ||
         interval_ms > VDC_PRIORITY_TRACE_SUMMARY_MAX_INTERVAL_MS ||
         interval_ms % VDC_PRIORITY_TRACE_SUMMARY_INTERVAL_MS != 0u))) {
        scpi_port_push_exec_error(context, "Priority summary ARM parameters rejected");
        return SCPI_RES_ERR;
    }
    const bool accepted = has_interval ?
        vdc_dpll_manager_priority_trace_summary_window_arm(capture_id, origin, interval_ms) :
        vdc_dpll_manager_priority_trace_summary_arm(capture_id, origin);
    if (!accepted) {
        scpi_port_push_exec_error(context, "Priority summary ARM rejected");
        return SCPI_RES_ERR;
    }
    /* Admission only; the existing STOP-only status ACK completes ownership. */
    SCPI_ResultUInt32(context, capture_id);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_priority_trace_summary_phase_arm(scpi_t *context)
{ return scpi_priority_summary_arm(context, false); }

scpi_result_t scpi_cmd_vdc_priority_trace_summary_origin_arm(scpi_t *context)
{ return scpi_priority_summary_arm(context, true); }

static scpi_result_t scpi_priority_guard_arm(scpi_t *context, bool origin)
{
    uint32_t capture=0u, seconds=0u;
    bool present;
    scpi_parameter_t extra;
    if (!scpi_priority_summary_u32(context,&capture,TRUE,&present) ||
        !scpi_priority_summary_u32(context,&seconds,TRUE,&present) ||
        SCPI_Parameter(context,&extra,FALSE) || SCPI_ParamErrorOccurred(context) ||
        !vdc_dpll_manager_priority_trace_guard_arm(capture,origin,seconds)) {
        scpi_port_push_exec_error(context,"Priority guard ARM rejected"); return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context,capture); return SCPI_RES_OK;
}
scpi_result_t scpi_cmd_vdc_priority_guard_phase_arm(scpi_t *context)
{ return scpi_priority_guard_arm(context,false); }
scpi_result_t scpi_cmd_vdc_priority_guard_origin_arm(scpi_t *context)
{ return scpi_priority_guard_arm(context,true); }

scpi_result_t scpi_cmd_vdc_priority_guard_q(scpi_t *context)
{
    vdc_priority_guard_status_t s;
    if (!vdc_dpll_manager_get_priority_guard(&s)) {
        SCPI_ResultText(context,"UNAVAILABLE"); return SCPI_RES_OK;
    }
    /* Bounded metadata only. No read lease, record scan, CRC or hardware access. */
    SCPI_ResultUInt32(context,s.schema);
    SCPI_ResultUInt32(context,s.capture_id);
    SCPI_ResultUInt32(context,s.session);
    SCPI_ResultUInt32(context,s.generation);
    SCPI_ResultUInt32(context,s.target_s);
    SCPI_ResultUInt32(context,s.state);
    SCPI_ResultUInt32(context,s.reason_mask);
    SCPI_ResultUInt32(context,s.first_failure_ms);
    SCPI_ResultUInt32(context,s.checked_s);
    SCPI_ResultUInt32(context,s.passed_mask);
    SCPI_ResultUInt32(context,s.elapsed_ms);
    SCPI_ResultUInt32(context,s.ring_config_seq);
    SCPI_ResultUInt32(context,s.stop_config_seq);
    SCPI_ResultUInt32(context,s.stop_accepted);
    SCPI_ResultUInt32(context,s.ring_retired);
    SCPI_ResultUInt32(context,s.output_retired);
    SCPI_ResultUInt32(context,s.output_request);
    SCPI_ResultUInt32(context,s.output_state);
    SCPI_ResultUInt32(context,s.output_reason);
    SCPI_ResultUInt32(context,s.output_ordinal_lo);
    SCPI_ResultUInt32(context,s.output_ordinal_hi);
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
