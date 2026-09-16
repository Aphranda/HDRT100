#include "scpi_config_commands.h"

#include <string.h>

#include "distributed_config.h"
#include "trigger_sequence_service.h"
#include "sync_io_sequence.h"

/* Host-side SCPI parser tests do not link the hardware IO backend.  Firmware
 * provides the strong implementations from sync_io_sequence.c; these weak
 * fallbacks keep parser tests linkable and retain the same range semantics. */
#if defined(__GNUC__)
static uint32_t s_host_switch1 = 1u;
static uint32_t s_host_switch2;
__attribute__((weak)) bool sync_io_sequence_set_switch(uint32_t number, uint32_t value)
{
    if (number == 1u && value >= 1u && value <= 8u) { s_host_switch1 = value; return true; }
    if (number == 2u && value <= 1u) { s_host_switch2 = value; return true; }
    return false;
}
__attribute__((weak)) uint32_t sync_io_sequence_get_switch(uint32_t number)
{ return number == 1u ? s_host_switch1 : (number == 2u ? s_host_switch2 : 0u); }
#endif

static bool sequence_end_parameters(scpi_t *context)
{
    /* The vendored scanner can count a trailing empty field without exposing
     * it through SCPI_Parameter. Check its parsed count before committing. */
    if (context->parser_state.numberOfParameters != context->input_count) {
        SCPI_ErrorPush(context, SCPI_ERROR_PARAMETER_NOT_ALLOWED);
        return false;
    }
    scpi_parameter_t extra;
    if (SCPI_Parameter(context, &extra, FALSE)) {
        SCPI_ErrorPush(context, SCPI_ERROR_PARAMETER_NOT_ALLOWED);
        return false;
    }
    return !SCPI_ParamErrorOccurred(context);
}

/* Decimal integer tokens are checked before conversion: the general SCPI
 * unsigned parser accepts negative values through unsigned conversion. */
static bool sequence_read_u32(scpi_t *context, uint32_t *value, bool mandatory)
{
    scpi_parameter_t token;
    if (!SCPI_Parameter(context, &token, mandatory ? TRUE : FALSE)) return false;
    if (token.type != SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA || token.len <= 0) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_TYPE_ERROR);
        return false;
    }
    int index = token.ptr[0] == '+' ? 1 : 0;
    if (index == token.len) {
        SCPI_ErrorPush(context, SCPI_ERROR_DATA_TYPE_ERROR);
        return false;
    }
    uint32_t number = 0u;
    for (; index < token.len; ++index) {
        const unsigned char c = (unsigned char)token.ptr[index];
        if (c < '0' || c > '9' || number > (UINT32_MAX - (c - '0')) / 10u) {
            SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
            return false;
        }
        number = number * 10u + (c - '0');
    }
    *value = number;
    return true;
}

static bool sequence_read_id(scpi_t *context, char *id, bool mandatory)
{
    const char *text = NULL;
    size_t length = 0u;
    id[0] = '\0';
    if (!SCPI_ParamCharacters(context, &text, &length, mandatory ? TRUE : FALSE)) {
        return !mandatory && !SCPI_ParamErrorOccurred(context);
    }
    if (length == 0u || length > TRIGGER_SEQUENCE_PLAN_ID_MAX) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return false;
    }
    for (size_t i = 0u; i < length; ++i) {
        const unsigned char c = (unsigned char)text[i];
        const bool letter = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
        if (!(letter || c == '_' || (i > 0u && c >= '0' && c <= '9'))) {
            SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
            return false;
        }
        id[i] = (char)(c >= 'a' && c <= 'z' ? c - 'a' + 'A' : c);
    }
    id[length] = '\0';
    return true;
}

static scpi_result_t sequence_result(scpi_t *context, trigger_sequence_result_t result)
{
    if (result != TRIGGER_SEQUENCE_OK) {
        scpi_port_push_exec_error(context, trigger_sequence_result_name(result));
        return SCPI_RES_ERR;
    }
    return scpi_port_result_accepted(context);
}

bool scpi_sequence_param_u32(scpi_t *context, uint32_t *value)
{
    return sequence_read_u32(context, value, true);
}

bool scpi_sequence_params_end(scpi_t *context)
{
    return sequence_end_parameters(context);
}

bool scpi_sequence_param_plan(scpi_t *context,
    char id[TRIGGER_SEQUENCE_PLAN_ID_MAX + 1u], bool required, bool *present)
{
    const bool valid = sequence_read_id(context, id, required);
    if (present != NULL) *present = valid && id[0] != '\0';
    return valid;
}

scpi_result_t scpi_config_trigger_parameter(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(context, DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }
    trigger_sequence_params_t params;
    if (!sequence_read_u32(context, &params.chan_count, true) ||
        !sequence_read_u32(context, &params.pol, true) ||
        !sequence_read_u32(context, &params.freq_count, true) ||
        !sequence_read_u32(context, &params.wave_count, true) ||
        !scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    return sequence_result(context,
        trigger_sequence_configure(trigger_sequence_service_config(), &params));
}

scpi_result_t scpi_config_trigger_parameter_q(scpi_t *context)
{
    if (!sequence_end_parameters(context)) return SCPI_RES_ERR;
    const trigger_sequence_store_t *store = trigger_sequence_service_config();
    SCPI_ResultText(context, "TRIGGER");
    SCPI_ResultUInt32(context, store->parameter_crc);
    SCPI_ResultUInt32(context, TRIGGER_SEQUENCE_CRC_SCHEMA);
    SCPI_ResultBool(context, store->configured);
    SCPI_ResultUInt32(context, store->generation);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, store->params.chan_count);
    SCPI_ResultUInt32(context, store->params.pol);
    SCPI_ResultUInt32(context, store->params.freq_count);
    SCPI_ResultUInt32(context, store->params.wave_count);
    SCPI_ResultUInt32(context, store->state_count);
    SCPI_ResultUInt32(context, store->map_crc);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_angle_sweep_q(scpi_t *context)
{
    SCPI_ResultInt32(context, -10);
    SCPI_ResultInt32(context, 370);
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, 381u);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_angle_pulse_q(scpi_t *context)
{
    SCPI_ResultText(context, "RISING");
    SCPI_ResultUInt32(context, 10u);
    SCPI_ResultUInt32(context, 30000u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultText(context, "OK");
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_angle_position_q(scpi_t *context)
{
    SCPI_ResultText(context, "DTC_SWEEP");
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 381u);
    SCPI_ResultInt32(context, -10);
    SCPI_ResultInt32(context, -9);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultBool(context, TRUE);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_angle_breakpoint_q(scpi_t *context)
{
    SCPI_ResultInt32(context, 0);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_sequence(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(context, DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }
    char id[TRIGGER_SEQUENCE_PLAN_ID_MAX + 1u];
    uint32_t ids[TRIGGER_SEQUENCE_STATE_MAX];
    uint32_t count = 0u;
    if (!sequence_read_id(context, id, true)) return SCPI_RES_ERR;
    do {
        if (!sequence_read_u32(context, &ids[count], count == 0u)) {
            if (count == 0u || SCPI_ParamErrorOccurred(context)) return SCPI_RES_ERR;
            break;
        }
        ++count;
    } while (count < TRIGGER_SEQUENCE_STATE_MAX);
    if (!sequence_end_parameters(context)) return SCPI_RES_ERR;
    return sequence_result(context,
        trigger_sequence_write_plan(trigger_sequence_service_config(), id, ids, count));
}

scpi_result_t scpi_config_sequence_q(scpi_t *context)
{
    char id[TRIGGER_SEQUENCE_PLAN_ID_MAX + 1u];
    uint32_t index = UINT32_MAX;
    if (!sequence_read_id(context, id, false)) return SCPI_RES_ERR;
    const bool has_index = id[0] && sequence_read_u32(context, &index, false);
    if (SCPI_ParamErrorOccurred(context) || !sequence_end_parameters(context)) return SCPI_RES_ERR;
    const trigger_sequence_plan_t *plan = trigger_sequence_get_plan(
        trigger_sequence_service_config(), id[0] ? id : NULL);
    if (plan == NULL) return sequence_result(context, TRIGGER_SEQUENCE_PLAN_NOT_FOUND);
    if (has_index && index >= plan->count) return sequence_result(context, TRIGGER_SEQUENCE_INDEX_RANGE);
    SCPI_ResultText(context, plan->id);
    SCPI_ResultUInt32(context, index);
    SCPI_ResultUInt32(context, plan->crc);
    SCPI_ResultUInt32(context, plan->count);
    SCPI_ResultBool(context, has_index);
    if (has_index) SCPI_ResultUInt32(context, plan->state_ids[index]);
    else for (uint32_t i = 0u; i < plan->count; ++i) SCPI_ResultUInt32(context, plan->state_ids[i]);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_sequence_map_q(scpi_t *context)
{
    char id[TRIGGER_SEQUENCE_PLAN_ID_MAX + 1u];
    if (!sequence_read_id(context, id, false) || !sequence_end_parameters(context)) return SCPI_RES_ERR;
    const trigger_sequence_store_t *store = trigger_sequence_service_config();
    trigger_sequence_check_t check;
    const trigger_sequence_result_t result = trigger_sequence_check(store, id[0] ? id : NULL, &check);
    if (result != TRIGGER_SEQUENCE_OK) return sequence_result(context, result);
    const trigger_sequence_plan_t *plan = trigger_sequence_get_plan(store, id[0] ? id : NULL);
    for (uint32_t i = 0u; i < plan->count; ++i) {
        trigger_sequence_state_t state;
        if (trigger_sequence_map_state(store, plan->state_ids[i], &state) != TRIGGER_SEQUENCE_OK) return SCPI_RES_ERR;
        SCPI_ResultUInt32(context, state.state_id);
        SCPI_ResultUInt32(context, state.switch1_ch);
        SCPI_ResultUInt32(context, state.switch2_sel);
        SCPI_ResultUInt32(context, state.pol);
        SCPI_ResultUInt32(context, state.freq_idx);
        SCPI_ResultUInt32(context, state.wave_idx);
    }
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_sequence_check_q(scpi_t *context)
{
    char id[TRIGGER_SEQUENCE_PLAN_ID_MAX + 1u];
    if (!sequence_read_id(context, id, false) || !sequence_end_parameters(context)) return SCPI_RES_ERR;
    const trigger_sequence_store_t *store = trigger_sequence_service_config();
    trigger_sequence_check_t check;
    (void)trigger_sequence_check(store, id[0] ? id : NULL, &check);
    const trigger_sequence_plan_t *plan = trigger_sequence_get_plan(store, id[0] ? id : NULL);
    SCPI_ResultText(context, plan ? plan->id : (id[0] ? id : "NONE"));
    SCPI_ResultUInt32(context, check.state_count);
    SCPI_ResultUInt32(context, check.sequence_crc);
    SCPI_ResultBool(context, check.state_range_ok);
    SCPI_ResultBool(context, check.map_crc_ok);
    SCPI_ResultBool(context, check.switch_range_ok);
    SCPI_ResultBool(context, check.pol_range_ok);
    SCPI_ResultBool(context, check.freq_range_ok);
    SCPI_ResultBool(context, check.wave_range_ok);
    SCPI_ResultBool(context, check.duplicate_state);
    SCPI_ResultBool(context, check.missing_state);
    SCPI_ResultText(context, trigger_sequence_result_name(check.result));
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_sequence_active(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(context, DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG)) {
        return SCPI_RES_ERR;
    }
    char id[TRIGGER_SEQUENCE_PLAN_ID_MAX + 1u];
    if (!sequence_read_id(context, id, true) || !sequence_end_parameters(context)) return SCPI_RES_ERR;
    return sequence_result(context, trigger_sequence_activate(trigger_sequence_service_config(), id));
}

scpi_result_t scpi_config_sequence_active_q(scpi_t *context)
{
    if (!sequence_end_parameters(context)) return SCPI_RES_ERR;
    trigger_sequence_active_t active;
    (void)trigger_sequence_get_active(trigger_sequence_service_config(), &active);
    SCPI_ResultText(context, active.id[0] ? active.id : "NONE");
    SCPI_ResultUInt32(context, active.crc);
    SCPI_ResultUInt32(context, active.count);
    SCPI_ResultBool(context, active.valid);
    SCPI_ResultText(context, active.valid ? "PASS" : (active.id[0] ? "FAIL" : "NONE"));
    SCPI_ResultText(context, trigger_sequence_result_name(active.result));
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_switch_q(scpi_t *context)
{
    int32_t numbers[1];
    SCPI_CommandNumbers(context, numbers, 1u, 1);
    if (numbers[0] < 1 || numbers[0] > 2) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    SCPI_ResultInt32(context, numbers[0]);
    SCPI_ResultUInt32(context, sync_io_sequence_get_switch((uint32_t)numbers[0]));
    SCPI_ResultBool(context, FALSE);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_config_switch(scpi_t *context)
{
    int32_t numbers[1];
    uint32_t value;
    SCPI_CommandNumbers(context, numbers, 1u, 1);
    if (!scpi_sequence_param_u32(context, &value) ||
        !scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    if (numbers[0] != 1 ||
        !sync_io_sequence_set_switch((uint32_t)numbers[0], value)) {
        scpi_port_push_exec_error(context, "SP8T_BUSY_OR_RANGE");
        return SCPI_RES_ERR;
    }
    return scpi_port_result_accepted(context);
}
