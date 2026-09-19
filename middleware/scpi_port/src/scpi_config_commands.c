#include "scpi_config_commands.h"

#include <string.h>
#include <math.h>
#include <float.h>

#include "distributed_config.h"
#include "trigger_sequence_service.h"
#include "sync_io_sequence.h"
#include "trigger_sequence_link.h"

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

static bool sequence_configuration_begin(scpi_t *context)
{
    if (trigger_sequence_service_configuration_begin()) return true;
    scpi_port_push_exec_error(context, "SEQUENCE_CONFIGURATION_BUSY");
    return false;
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
    if (!sequence_configuration_begin(context)) return SCPI_RES_ERR;
    const trigger_sequence_result_t result =
        trigger_sequence_configure(trigger_sequence_service_config(), &params);
    trigger_sequence_service_configuration_end();
    return sequence_result(context, result);
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

/* Core0 angle metadata only. Floating point conversion never enters the
 * realtime plane: Core1 continues to consume integer N and finite repeats.
 * Different SCPI transports can run on different Core0 tasks. The short
 * try-lock protects metadata while the service gate serializes with START. */
typedef struct {
    double start, stop, step, speed, pulses_per_degree, input_hz;
    uint32_t input, count, threshold, binding_epoch, model_epoch;
    bool sweep_set, input_set;
} angle_config_t;
static angle_config_t s_angle;
static uint32_t s_angle_guard;

static bool angle_take(scpi_t *context)
{
    if (__atomic_exchange_n(&s_angle_guard, 1u, __ATOMIC_ACQUIRE) == 0u) return true;
    scpi_port_push_exec_error(context, "ANGLE_CONFIGURATION_BUSY");
    return false;
}
static void angle_release(void) { __atomic_store_n(&s_angle_guard, 0u, __ATOMIC_RELEASE); }

static bool angle_number(scpi_t *context, double *value)
{
    if (!SCPI_ParamDouble(context, value, TRUE)) return false;
    if (isfinite(*value)) return true;
    SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
    return false;
}

static bool angle_integer(double value, uint32_t *result)
{
    if (!isfinite(value) || value < 0.0 || value > UINT32_MAX) return false;
    const double nearest = floor(value + 0.5);
    const double tolerance = 4.0 * DBL_EPSILON * fmax(1.0, fabs(value));
    if (fabs(value - nearest) > tolerance || nearest > UINT32_MAX) return false;
    *result = (uint32_t)nearest;
    return true;
}

static bool angle_derive(angle_config_t *value)
{
    if (value->sweep_set) {
        uint32_t intervals;
        if (!(value->speed > 0.0) || !isfinite(value->speed) || value->step == 0.0 || !angle_integer(
                (value->stop - value->start) / value->step, &intervals) ||
            intervals == UINT32_MAX) return false;
        value->count = intervals + 1u;
        if (!intervals && value->start != value->stop) return false;
        if (intervals && (value->start + value->step == value->start ||
            value->stop - value->step == value->stop)) return false;
    }
    if (value->input_set && (!(value->pulses_per_degree > 0.0) ||
        !isfinite(value->pulses_per_degree))) return false;
    if (value->sweep_set && value->input_set) {
        value->input_hz = value->speed * value->pulses_per_degree;
        if (!isfinite(value->input_hz) || value->input_hz <= 0.0) return false;
        if (!angle_integer(fabs(value->step) * value->pulses_per_degree, &value->threshold) ||
            !value->threshold || (uint64_t)value->threshold * value->count >=
                SYNC_IO_SEQUENCE_COUNTER_LIMIT ||
            !isfinite(value->threshold / value->input_hz) ||
            value->threshold / value->input_hz <= 0.0 ||
            !isfinite(value->input_hz / value->threshold) ||
            value->input_hz / value->threshold <= 0.0) return false;
    }
    return true;
}

static bool angle_bound(const angle_config_t *value, const trigger_sequence_link_status_t *link)
{
    return value->sweep_set && value->input_set && value->binding_epoch &&
        value->binding_epoch == link->binding_epoch && value->model_epoch == link->model_epoch &&
        trigger_sequence_link_binding_is_current(value->binding_epoch, value->model_epoch) &&
        link->config.enabled && link->config.counter_enabled &&
        value->input == link->config.counter_input && value->threshold == link->config.counter_threshold &&
        value->count == trigger_sequence_service_get_repeat();
}

/* Caller owns both angle and service configuration guards. A partial pair is
 * retained as a draft; a complete pair binds to an existing POSITION role
 * configuration, preserving READY, output timing and TDMA slot assignment. */
static bool angle_apply(angle_config_t *value)
{
    if (!angle_derive(value)) return false;
    if (!value->sweep_set || !value->input_set) {
        value->binding_epoch = 0u;
        return true;
    }
    trigger_sequence_link_status_t link;
    trigger_sequence_link_get_status(&link);
    if (!link.config.enabled || !link.config.counter_enabled) return false;
    const trigger_sequence_plan_t *plan = trigger_sequence_get_plan(
        trigger_sequence_service_config(), NULL);
    if (plan && (uint64_t)plan->count * value->count > UINT32_MAX) return false;
    link.config.counter_input = value->input;
    link.config.counter_threshold = value->threshold;
    if (!trigger_sequence_link_configure_position_locked(&link.config, value->count)) return false;
    trigger_sequence_link_get_status(&link);
    value->binding_epoch = link.binding_epoch;
    value->model_epoch = link.model_epoch;
    return true;
}

static bool angle_write_begin(scpi_t *context)
{
    if (scpi_port_reject_if_run_forbidden(context, DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG) ||
        !angle_take(context)) return false;
    if (sequence_configuration_begin(context)) return true;
    angle_release();
    return false;
}
static scpi_result_t angle_write_end(scpi_t *context, const angle_config_t *value, bool valid)
{
    if (valid) s_angle = *value;
    trigger_sequence_service_configuration_end();
    angle_release();
    if (valid) return scpi_port_result_accepted(context);
    scpi_port_push_exec_error(context, "ANGLE_INVALID_OR_POSITION_UNAVAILABLE");
    return SCPI_RES_ERR;
}

scpi_result_t scpi_config_angle_sweep(scpi_t *context)
{
    double start, stop, step, speed;
    if (!angle_number(context, &start) || !angle_number(context, &stop) ||
        !angle_number(context, &step) || !angle_number(context, &speed) ||
        !sequence_end_parameters(context)) return SCPI_RES_ERR;
    if (!angle_write_begin(context)) return SCPI_RES_ERR;
    angle_config_t value = s_angle;
    trigger_sequence_link_status_t link;
    trigger_sequence_link_get_status(&link);
    if (value.binding_epoch && !angle_bound(&value, &link)) {
        value.input_set = false; value.input = 0u;
        value.pulses_per_degree = value.input_hz = 0.0;
        value.threshold = value.binding_epoch = 0u;
    }
    value.start = start; value.stop = stop; value.step = step; value.speed = speed; value.sweep_set = true;
    const bool valid = angle_apply(&value);
    return angle_write_end(context, &value, valid);
}

scpi_result_t scpi_config_angle_input(scpi_t *context)
{
    const char *input; size_t length;
    double ppd;
    if (!SCPI_ParamCharacters(context, &input, &length, TRUE) ||
        !angle_number(context, &ppd) ||
        !sequence_end_parameters(context)) return SCPI_RES_ERR;
    if (length != 3u || (input[0] != 'I' && input[0] != 'i') ||
        (input[1] != 'N' && input[1] != 'n') || input[2] < '1' || input[2] > '4') {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    if (!angle_write_begin(context)) return SCPI_RES_ERR;
    angle_config_t value = s_angle;
    value.input = (uint32_t)(input[2] - '0'); value.pulses_per_degree = ppd;
    value.input_set = true;
    const bool valid = angle_apply(&value);
    return angle_write_end(context, &value, valid);
}

scpi_result_t scpi_config_angle_speed(scpi_t *context)
{
    double speed;
    if (!angle_number(context, &speed) || !sequence_end_parameters(context)) return SCPI_RES_ERR;
    if (!angle_write_begin(context)) return SCPI_RES_ERR;
    angle_config_t value = s_angle;
    value.speed = speed;
    const bool valid = value.sweep_set && angle_derive(&value);
    /* Expected speed is metadata, not motor control or a hardware timer. */
    return angle_write_end(context, &value, valid);
}

static bool angle_read(scpi_t *context, angle_config_t *value,
    trigger_sequence_link_status_t *link, bool *bound)
{
    if (!sequence_end_parameters(context) || !angle_take(context)) return false;
    *value = s_angle;
    trigger_sequence_link_get_status(link);
    *bound = angle_bound(value, link);
    angle_release();
    return true;
}

scpi_result_t scpi_config_angle_sweep_q(scpi_t *context)
{
    angle_config_t value; trigger_sequence_link_status_t link; bool bound;
    if (!angle_read(context, &value, &link, &bound)) return SCPI_RES_ERR;
    SCPI_ResultDouble(context, value.start); SCPI_ResultDouble(context, value.stop);
    SCPI_ResultDouble(context, value.step); SCPI_ResultDouble(context, value.speed);
    SCPI_ResultUInt32(context, value.count);
    SCPI_ResultBool(context, bound);
    return SCPI_RES_OK;
}
scpi_result_t scpi_config_angle_input_q(scpi_t *context)
{
    angle_config_t value; trigger_sequence_link_status_t link; bool bound;
    if (!angle_read(context, &value, &link, &bound)) return SCPI_RES_ERR;
    const char *inputs[] = {"NONE", "IN1", "IN2", "IN3", "IN4"};
    SCPI_ResultText(context, inputs[value.input]);
    SCPI_ResultDouble(context, value.pulses_per_degree); SCPI_ResultDouble(context, value.input_hz);
    SCPI_ResultUInt32(context, value.threshold);
    SCPI_ResultDouble(context, value.threshold && value.input_set ? value.threshold / value.input_hz : 0.0);
    SCPI_ResultDouble(context, value.threshold ? value.input_hz / value.threshold : 0.0);
    SCPI_ResultBool(context, bound);
    return SCPI_RES_OK;
}
scpi_result_t scpi_config_angle_speed_q(scpi_t *context)
{
    angle_config_t value; trigger_sequence_link_status_t link; bool bound;
    if (!angle_read(context, &value, &link, &bound)) return SCPI_RES_ERR;
    SCPI_ResultDouble(context, value.speed);
    return SCPI_RES_OK;
}
scpi_result_t scpi_config_angle_position_q(scpi_t *context)
{
    angle_config_t value; trigger_sequence_link_status_t link; bool bound;
    if (!angle_read(context, &value, &link, &bound)) return SCPI_RES_ERR;
    const uint32_t admitted = bound ? link.counter_consumed : 0u;
    const bool current = bound && admitted > 0u && admitted <= value.count;
    const bool next = bound && admitted < value.count;
    SCPI_ResultText(context, "POSITION"); SCPI_ResultUInt32(context, admitted);
    SCPI_ResultUInt32(context, value.count);
    SCPI_ResultDouble(context, current ? value.start + (admitted - 1u) * value.step : 0.0);
    SCPI_ResultDouble(context, next ? value.start + admitted * value.step : 0.0);
    SCPI_ResultUInt32(context, link.counter_events); SCPI_ResultUInt32(context, link.counter_partial);
    SCPI_ResultBool(context, current); SCPI_ResultBool(context, next);
    SCPI_ResultUInt32(context, link.error); SCPI_ResultBool(context, bound);
    return SCPI_RES_OK;
}
scpi_result_t scpi_config_angle_unsupported(scpi_t *context)
{
    scpi_port_push_exec_error(context, "ANGLE_COMMAND_NOT_IMPLEMENTED");
    return SCPI_RES_ERR;
}
scpi_result_t scpi_config_angle_pulse_q(scpi_t *context) { return scpi_config_angle_unsupported(context); }
scpi_result_t scpi_config_angle_breakpoint_q(scpi_t *context) { return scpi_config_angle_unsupported(context); }

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
    if (!sequence_configuration_begin(context)) return SCPI_RES_ERR;
    const trigger_sequence_result_t result =
        trigger_sequence_write_plan(trigger_sequence_service_config(), id, ids, count);
    trigger_sequence_service_configuration_end();
    return sequence_result(context, result);
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
    if (!sequence_configuration_begin(context)) return SCPI_RES_ERR;
    const trigger_sequence_result_t result =
        trigger_sequence_activate(trigger_sequence_service_config(), id);
    trigger_sequence_service_configuration_end();
    return sequence_result(context, result);
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
