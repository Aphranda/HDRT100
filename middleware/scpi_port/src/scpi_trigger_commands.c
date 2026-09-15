#include "scpi_trigger_commands.h"

#include "sync_trigger.h"
#include "scpi_config_commands.h"
#include "trigger_sequence_service.h"

static uint32_t s_product_trigger_mode;

static const char *scpi_trigger_control_mode_to_string(uint32_t mode)
{
    switch (mode) {
    case 0u: return "IDLE";
    case 1u: return "TRIG";
    case 2u: return "CAL";
    case 3u: return "SYNC";
    case 4u: return "SIM";
    default: return "UNKNOWN";
    }
}

static const char *scpi_trigger_control_state_to_string(trig_state_t state)
{
    switch (state) {
    case TRIG_STATE_IDLE:            return "IDLE";
    case TRIG_STATE_SEQ_CONFIGURED:
    case TRIG_STATE_ENC_CONFIGURED:
    case TRIG_STATE_BISS_CONFIGURED: return "ARMED";
    case TRIG_STATE_SEQ_ARMED:
    case TRIG_STATE_ENC_ARMED:
    case TRIG_STATE_BISS_ARMED:      return "RUN";
    case TRIG_STATE_FAULT:           return "FAULT";
    default:                         return "UNKNOWN";
    }
}

uint32_t scpi_trigger_product_mode(void)
{
    return s_product_trigger_mode;
}

scpi_result_t scpi_cmd_trigger_mode(scpi_t *context)
{
    uint32_t mode;
    if (!scpi_sequence_param_u32(context, &mode) ||
        !scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    if (mode > 4u) {
        SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
        return SCPI_RES_ERR;
    }
    if (mode != 0u && trigger_sequence_service_is_active()) {
        scpi_port_push_exec_error(context, "CONFIG_FROZEN");
        return SCPI_RES_ERR;
    }
    if (mode == 0u) {
        const trig_event_t event = { .type = TRIG_EVENT_RESET };
        if (!sync_trigger_post(&event)) return SCPI_RES_ERR;
    }
    s_product_trigger_mode = mode;
    scpi_port_set_trigger_debug_mode(mode);

    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_mode_q(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    trigger_sequence_service_status_t status;
    trigger_sequence_service_get_status(&status);
    SCPI_ResultText(context, scpi_trigger_control_mode_to_string(s_product_trigger_mode));
    SCPI_ResultUInt32(context, s_product_trigger_mode);
    SCPI_ResultText(context, trigger_sequence_service_is_active()
        ? trigger_sequence_service_state_name(status.state)
        : scpi_trigger_control_state_to_string(vector.state));
    SCPI_ResultText(context, "ALLOW");
    SCPI_ResultText(context, "NONE");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_start(scpi_t *context)
{
    char plan[TRIGGER_SEQUENCE_PLAN_ID_MAX + 1u];
    bool present;
    if (!scpi_sequence_param_plan(context, plan, false, &present) ||
        !scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    const trigger_sequence_service_result_t result =
        trigger_sequence_service_start(present ? plan : NULL);
    if (result != TRIGGER_SEQUENCE_SERVICE_OK) {
        scpi_port_push_exec_error(context, trigger_sequence_service_result_name(result));
        return SCPI_RES_ERR;
    }
    s_product_trigger_mode = 1u;
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_stop(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    const trigger_sequence_service_result_t result = trigger_sequence_service_stop();
    if (result != TRIGGER_SEQUENCE_SERVICE_OK) {
        scpi_port_push_exec_error(context, trigger_sequence_service_result_name(result));
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_pause(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    const trigger_sequence_service_result_t result = trigger_sequence_service_pause();
    if (result != TRIGGER_SEQUENCE_SERVICE_OK) {
        scpi_port_push_exec_error(context, trigger_sequence_service_result_name(result));
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_continue(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    const trigger_sequence_service_result_t result = trigger_sequence_service_continue();
    if (result != TRIGGER_SEQUENCE_SERVICE_OK) {
        scpi_port_push_exec_error(context, trigger_sequence_service_result_name(result));
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_abort(scpi_t *context)
{
    return scpi_cmd_trigger_stop(context);
}

scpi_result_t scpi_trigger_state_q(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    trigger_sequence_service_status_t status;
    trigger_sequence_service_get_status(&status);
    const trigger_sequence_plan_t *plan = trigger_sequence_get_plan(
        trigger_sequence_service_config(), NULL);
    SCPI_ResultText(context, scpi_trigger_control_mode_to_string(s_product_trigger_mode));
    SCPI_ResultText(context, trigger_sequence_service_state_name(status.state));
    SCPI_ResultUInt32(context, status.run_id);
    SCPI_ResultText(context, plan != NULL ? plan->id : "NONE");
    SCPI_ResultInt32(context, 0);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, status.current_index);
    SCPI_ResultUInt32(context, status.count);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, status.alarm_late_us != 0u);
    SCPI_ResultUInt32(context, (uint32_t)status.error);
    SCPI_ResultText(context, trigger_sequence_service_result_name(status.error));
    return SCPI_RES_OK;
}
