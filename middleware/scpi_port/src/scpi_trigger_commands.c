#include "scpi_trigger_commands.h"

#include "sync_trigger.h"
#include "trigger_sequence_config.h"
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
    if (SCPI_ParamUInt32(context, &mode, FALSE) == TRUE) {
        if (mode > 4u) {
            return SCPI_RES_ERR;
        }

        s_product_trigger_mode = mode;
        scpi_port_set_trigger_debug_mode(mode);

        if (mode == 0u) {
            const trig_event_t event = { .type = TRIG_EVENT_RESET };
            if (!sync_trigger_post(&event)) {
                return SCPI_RES_ERR;
            }
        }
    }

    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_mode_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);
    SCPI_ResultText(context, scpi_trigger_control_mode_to_string(s_product_trigger_mode));
    SCPI_ResultUInt32(context, s_product_trigger_mode);
    SCPI_ResultText(context, scpi_trigger_control_state_to_string(vector.state));
    SCPI_ResultText(context, "ALLOW");
    SCPI_ResultText(context, "NONE");
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_start(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_stop(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_pause(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_continue(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_abort(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

scpi_result_t scpi_trigger_state_q(scpi_t *context)
{
    trigger_sequence_service_status_t status;
    trigger_sequence_angle_t angle;
    trigger_sequence_active_t active;
    trigger_sequence_service_get_status(&status);
    trigger_sequence_service_get_angle(&angle);

    const char *plan_id = "";
    if (trigger_sequence_get_active(trigger_sequence_service_config(), &active) ==
            TRIGGER_SEQUENCE_OK) {
        plan_id = active.id;
    }

    uint32_t angle_count = 0u;
    int32_t current_angle = 0;
    if (angle.sweep_valid && angle.step_deg != 0.0f) {
        const float span = (angle.stop_deg - angle.start_deg) / angle.step_deg;
        if (span >= 0.0f && span < 1000000.0f) {
            angle_count = (uint32_t)span + 1u;
        }
        current_angle = (int32_t)angle.start_deg;
    }

    SCPI_ResultText(context, "TRIG");
    SCPI_ResultText(context, trigger_sequence_service_state_name(status.state));
    SCPI_ResultUInt32(context, status.run_id);
    SCPI_ResultText(context, plan_id);
    SCPI_ResultInt32(context, current_angle);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, angle_count);
    SCPI_ResultUInt32(context, status.current_index == TRIGGER_SEQUENCE_NO_INDEX
                                 ? 0u : status.current_index);
    SCPI_ResultUInt32(context, status.count);
    SCPI_ResultUInt32(context, status.accepted);
    SCPI_ResultUInt32(context, 0u);
    SCPI_ResultUInt32(context, status.alarm_late_us);
    SCPI_ResultUInt32(context, (uint32_t)status.error);
    SCPI_ResultText(context, trigger_sequence_service_result_name(status.error));
    return SCPI_RES_OK;
}
