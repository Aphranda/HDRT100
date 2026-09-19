#include "scpi_sync_commands.h"
#include "vdc_reference.h"

static bool reference_u32(scpi_t *context, uint32_t *value)
{
    scpi_parameter_t parameter;
    if (!SCPI_Parameter(context, &parameter, TRUE) ||
        parameter.type != SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA || parameter.len <= 0 ||
        context->param_list.lex_state.pos != parameter.ptr + parameter.len) return false;
    size_t pos = parameter.ptr[0] == '+' ? 1u : 0u;
    if (pos == (size_t)parameter.len) return false;
    uint32_t result = 0u;
    for (; pos < (size_t)parameter.len; ++pos) {
        const unsigned char digit = (unsigned char)parameter.ptr[pos];
        if (digit < '0' || digit > '9' || result > (UINT32_MAX - (digit - '0')) / 10u) return false;
        result = result * 10u + digit - '0';
    }
    *value = result;
    return true;
}

static bool reference_no_extra(scpi_t *context)
{
    scpi_parameter_t extra;
    return !SCPI_Parameter(context, &extra, FALSE) && !SCPI_ParamErrorOccurred(context);
}

static scpi_result_t reference_error(scpi_t *context)
{
    scpi_port_push_exec_error(context, "Reference parameters invalid, owner busy or operation unavailable");
    return SCPI_RES_ERR;
}

static void reference_config_result(scpi_t *context, const sync_io_reference_config_t *config)
{
    SCPI_ResultUInt32(context, config->input_port);
    SCPI_ResultUInt32(context, config->nominal_hz);
    SCPI_ResultUInt32(context, config->edge);
    SCPI_ResultUInt32(context, config->window_ms);
    SCPI_ResultUInt32(context, config->timeout_ms);
}

scpi_result_t scpi_cmd_vdc_reference_config(scpi_t *context)
{
    sync_io_reference_config_t config;
    if (!reference_u32(context, &config.input_port) ||
        !reference_u32(context, &config.nominal_hz) ||
        !reference_u32(context, &config.edge) ||
        !reference_u32(context, &config.window_ms) ||
        !reference_u32(context, &config.timeout_ms) || !reference_no_extra(context) ||
        !sync_io_reference_config_valid(&config) ||
        !vdc_dpll_manager_set_reference_config(&config)) return reference_error(context);
    reference_config_result(context, &config);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_reference_config_q(scpi_t *context)
{
    sync_io_reference_config_t config;
    if (!reference_no_extra(context) || !vdc_dpll_manager_get_reference_config(&config))
        return reference_error(context);
    reference_config_result(context, &config);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_reference_enable(scpi_t *context)
{
    uint32_t enabled;
    if (!reference_u32(context, &enabled) || !reference_no_extra(context) || enabled > 1u ||
        !vdc_dpll_manager_set_reference_enabled(enabled != 0u)) return reference_error(context);
    SCPI_ResultUInt32(context, enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_reference_enable_q(scpi_t *context)
{
    vdc_reference_status_t status;
    if (!reference_no_extra(context) || !vdc_dpll_manager_get_reference_status(&status))
        return reference_error(context);
    SCPI_ResultUInt32(context, status.enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_reference_discipline(scpi_t *context)
{
    uint32_t enabled;
    if (!reference_u32(context, &enabled) || !reference_no_extra(context) || enabled > 1u ||
        !vdc_dpll_manager_set_reference_discipline(enabled != 0u)) return reference_error(context);
    SCPI_ResultUInt32(context, enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_reference_discipline_q(scpi_t *context)
{
    vdc_reference_discipline_status_t s;
    if (!reference_no_extra(context) || !vdc_dpll_manager_get_reference_discipline(&s))
        return reference_error(context);
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.request);
    SCPI_ResultUInt32(context, s.enabled);
    SCPI_ResultUInt32(context, s.state);
    SCPI_ResultUInt32(context, s.reason);
    SCPI_ResultUInt32(context, s.reference_generation);
    SCPI_ResultUInt32(context, s.sample_seq);
    SCPI_ResultUInt32(context, s.accepted);
    SCPI_ResultUInt32(context, s.rejected);
    SCPI_ResultUInt32(context, s.applied);
    SCPI_ResultInt32(context, s.measured_ppb);
    SCPI_ResultInt32(context, s.filtered_ppb);
    SCPI_ResultInt32(context, s.baseline_ppb);
    SCPI_ResultUInt32(context, s.session);
    SCPI_ResultUInt32(context, s.role_generation);
    SCPI_ResultUInt32(context, s.clock_epoch);
    SCPI_ResultUInt32(context, s.clock_run);
    SCPI_ResultUInt32(context, s.origin_epoch);
    SCPI_ResultUInt32(context, s.origin_sequence);
    SCPI_ResultUInt32(context, s.dco_update_seq);
    return SCPI_RES_OK;
}

static void reference_discipline_config_result(scpi_t *context, const vdc_reference_discipline_config_t *config)
{
    SCPI_ResultUInt32(context, config->slew_ppb_per_s);
    SCPI_ResultUInt32(context, config->filter_divisor);
    SCPI_ResultUInt32(context, config->max_ppb);
}

scpi_result_t scpi_cmd_vdc_reference_discipline_config(scpi_t *context)
{
    vdc_reference_discipline_config_t config;
    if (!reference_u32(context, &config.slew_ppb_per_s) ||
        !reference_u32(context, &config.filter_divisor) ||
        !reference_u32(context, &config.max_ppb) || !reference_no_extra(context) ||
        !vdc_dpll_manager_set_reference_discipline_config(&config)) return reference_error(context);
    reference_discipline_config_result(context, &config);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_reference_discipline_config_q(scpi_t *context)
{
    vdc_reference_discipline_config_t config;
    if (!reference_no_extra(context) || !vdc_dpll_manager_get_reference_discipline_config(&config))
        return reference_error(context);
    reference_discipline_config_result(context, &config);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_reference_discipline_active_q(scpi_t *context)
{
    vdc_reference_discipline_status_t s;
    if (!reference_no_extra(context) || !vdc_dpll_manager_get_reference_discipline(&s))
        return reference_error(context);
    SCPI_ResultUInt32(context, s.schema);
    SCPI_ResultUInt32(context, s.request);
    SCPI_ResultUInt32(context, s.config_generation);
    SCPI_ResultUInt32(context, s.config_crc32);
    reference_discipline_config_result(context, &s.config);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_reference_discipline_default(scpi_t *context)
{
    if (!reference_no_extra(context) || !vdc_dpll_manager_default_reference_discipline())
        return reference_error(context);
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_reference_discipline_recall(scpi_t *context)
{
    if (!reference_no_extra(context) || !vdc_dpll_manager_recall_reference_discipline())
        return reference_error(context);
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_reference_discipline_store(scpi_t *context)
{
    if (!reference_no_extra(context) || !vdc_dpll_manager_store_reference_discipline())
        return reference_error(context);
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_reference_status_q(scpi_t *context)
{
    vdc_reference_status_t status;
    if (!reference_no_extra(context) || !vdc_dpll_manager_get_reference_status(&status))
        return reference_error(context);
    const sync_io_reference_snapshot_t *m = &status.monitor;
    SCPI_ResultUInt32(context, SYNC_IO_REFERENCE_SCHEMA);
    SCPI_ResultUInt32(context, status.config_generation);
    SCPI_ResultUInt32(context, status.enabled);
    SCPI_ResultUInt32(context, status.resource_held);
    SCPI_ResultUInt32(context, m->state);
    SCPI_ResultUInt32(context, m->reason);
    SCPI_ResultUInt32(context, m->generation);
    SCPI_ResultUInt32(context, m->sample_seq);
    SCPI_ResultUInt32(context, m->valid);
    reference_config_result(context, &m->config);
    SCPI_ResultUInt32(context, m->input_pin);
    SCPI_ResultUInt32(context, m->tick_hz);
    SCPI_ResultUInt32(context, m->reference_cycles);
    SCPI_ResultUInt32(context, m->start_raw32);
    SCPI_ResultUInt32(context, m->end_raw32);
    SCPI_ResultUInt32(context, m->elapsed_ticks);
    SCPI_ResultUInt32(context, m->pio_bias_ticks);
    SCPI_ResultInt32(context, m->frequency_error_ppb);
    SCPI_ResultUInt32(context, m->measurement_flags);
    SCPI_ResultUInt32(context, (uint32_t)(m->completed_raw >> 32u));
    SCPI_ResultUInt32(context, (uint32_t)m->completed_raw);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_reference_default(scpi_t *context)
{
    if (!reference_no_extra(context) || !vdc_dpll_manager_default_reference()) return reference_error(context);
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_reference_recall(scpi_t *context)
{
    if (!reference_no_extra(context) || !vdc_dpll_manager_recall_reference()) return reference_error(context);
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_reference_store(scpi_t *context)
{
    if (!reference_no_extra(context) || !vdc_dpll_manager_store_reference()) return reference_error(context);
    SCPI_ResultText(context, "OK");
    return SCPI_RES_OK;
}
