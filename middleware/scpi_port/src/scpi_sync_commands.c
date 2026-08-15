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
    SCPI_ResultText(context, "OK");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 1u);
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
