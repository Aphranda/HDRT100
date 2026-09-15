#include "scpi_sequence_commands.h"

#include "scpi_config_commands.h"
#include "scpi_port_internal.h"
#include "sync_io_sequence.h"
#include "trigger_sequence_service.h"

static const scpi_choice_def_t sources[] = {
    {"BUS", 0}, {"IN1", 1}, {"IN2", 2}, {"IN3", 3}, {"IN4", 4},
    SCPI_CHOICE_LIST_END
};
static const scpi_choice_def_t outputs[] = {
    {"OUT1", 1}, {"OUT2", 2}, {"OUT3", 3}, {"OUT4", 4},
    SCPI_CHOICE_LIST_END
};
static const scpi_choice_def_t edges[] = {
    {"RISing", 0}, {"FALLing", 1}, SCPI_CHOICE_LIST_END
};
static const scpi_choice_def_t status_modes[] = {
    {"LEVel", TRIGGER_SEQUENCE_STATUS_LEVEL},
    {"PULSe", TRIGGER_SEQUENCE_STATUS_PULSE},
    SCPI_CHOICE_LIST_END
};

static scpi_result_t result(scpi_t *context, trigger_sequence_service_result_t code)
{
    if (code != TRIGGER_SEQUENCE_SERVICE_OK) {
        scpi_port_push_exec_error(context, trigger_sequence_service_result_name(code));
        return SCPI_RES_ERR;
    }
    return scpi_port_result_accepted(context);
}

scpi_result_t scpi_sequence_step(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    return result(context, trigger_sequence_service_step());
}

scpi_result_t scpi_sequence_source(scpi_t *context)
{
    int32_t source, edge;
    if (!SCPI_ParamChoice(context, sources, &source, TRUE) ||
        !SCPI_ParamChoice(context, edges, &edge, TRUE) ||
        !scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    return result(context, trigger_sequence_service_set_source((uint32_t)source, edge != 0));
}

scpi_result_t scpi_sequence_source_q(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    trigger_sequence_service_io_t io;
    trigger_sequence_service_get_io(&io);
    SCPI_ResultText(context, io.source <= 4u ? sources[io.source].name : "UNKNOWN");
    SCPI_ResultText(context, io.falling ? "FALLING" : "RISING");
    return SCPI_RES_OK;
}

scpi_result_t scpi_sequence_io(scpi_t *context)
{
    uint32_t mask, settle, width;
    int32_t output;
    if (!scpi_sequence_param_u32(context, &mask) ||
        !SCPI_ParamChoice(context, outputs, &output, TRUE) ||
        !scpi_sequence_param_u32(context, &settle) ||
        !scpi_sequence_param_u32(context, &width) ||
        !scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    return result(context, trigger_sequence_service_set_io(mask, (uint32_t)output, settle, width));
}

scpi_result_t scpi_sequence_io_q(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    trigger_sequence_service_io_t io;
    trigger_sequence_service_get_io(&io);
    uint32_t channel = 0u;
    for (uint32_t i = 0u; i < 4u; ++i) {
        if (io.status_output_mask == (1u << i)) channel = i + 1u;
    }
    SCPI_ResultUInt32(context, io.sequence_output_mask);
    SCPI_ResultUInt32(context, channel);
    SCPI_ResultUInt32(context, io.settle_us);
    SCPI_ResultUInt32(context, io.pulse_us);
    SCPI_ResultUInt32(context, io.generation);
    SCPI_ResultBool(context, io.valid);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sequence_output_config(scpi_t *context)
{
    uint32_t sequence_mask, status_mask, settle, width;
    int32_t mode;
    if (!scpi_sequence_param_u32(context, &sequence_mask) ||
        !scpi_sequence_param_u32(context, &status_mask) ||
        !SCPI_ParamChoice(context, status_modes, &mode, TRUE) ||
        !scpi_sequence_param_u32(context, &settle) ||
        !scpi_sequence_param_u32(context, &width) ||
        !scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    return result(context, trigger_sequence_service_set_outputs(
        sequence_mask, status_mask, (trigger_sequence_status_mode_t)mode,
        settle, width));
}

scpi_result_t scpi_sequence_output_config_q(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    trigger_sequence_service_io_t io;
    trigger_sequence_service_get_io(&io);
    SCPI_ResultUInt32(context, io.sequence_output_mask);
    SCPI_ResultUInt32(context, io.status_output_mask);
    SCPI_ResultText(context, io.status_mode == TRIGGER_SEQUENCE_STATUS_PULSE ?
                    "PULSE" : "LEVEL");
    SCPI_ResultUInt32(context, io.settle_us);
    SCPI_ResultUInt32(context, io.pulse_us);
    SCPI_ResultUInt32(context, io.generation);
    SCPI_ResultBool(context, io.valid);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sequence_code(scpi_t *context)
{
    uint32_t state, value;
    if (!scpi_sequence_param_u32(context, &state) ||
        !scpi_sequence_param_u32(context, &value) ||
        !scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    return result(context, trigger_sequence_service_set_code(state, value));
}

scpi_result_t scpi_sequence_code_q(scpi_t *context)
{
    uint32_t state, value;
    if (!scpi_sequence_param_u32(context, &state) ||
        !scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    if (!trigger_sequence_service_get_code(state, &value)) {
        scpi_port_push_exec_error(context, "CODE_MISSING");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, state);
    SCPI_ResultUInt32(context, value);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sequence_status_q(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    trigger_sequence_service_status_t status;
    trigger_sequence_service_get_status(&status);
    SCPI_ResultText(context, trigger_sequence_service_state_name(status.state));
    SCPI_ResultUInt32(context, status.run_id);
    SCPI_ResultUInt32(context, status.generation);
    SCPI_ResultUInt32(context, status.count);
    SCPI_ResultUInt32(context, status.current_index);
    SCPI_ResultUInt32(context, status.current_state);
    SCPI_ResultUInt32(context, status.next_index);
    SCPI_ResultUInt32(context, status.executed_index);
    SCPI_ResultUInt32(context, status.executed_state);
    SCPI_ResultUInt32(context, status.completed_index);
    SCPI_ResultUInt32(context, status.completed_state);
    SCPI_ResultUInt32(context, status.cycles);
    SCPI_ResultUInt32(context, status.accepted);
    SCPI_ResultUInt32(context, status.completed);
    SCPI_ResultUInt32(context, status.busy_rejected);
    SCPI_ResultUInt32(context, status.notready_rejected);
    SCPI_ResultUInt32(context, status.cancelled);
    SCPI_ResultUInt32(context, status.faults);
    SCPI_ResultText(context, trigger_sequence_service_result_name(status.error));
    SCPI_ResultUInt64(context, status.written_at_us);
    SCPI_ResultUInt64(context, status.completion_rise_at_us);
    SCPI_ResultUInt64(context, status.completed_at_us);
    SCPI_ResultUInt32(context, status.alarm_late_us);
    SCPI_ResultUInt32(context, status.backend_fault);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sequence_timing_q(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    trigger_sequence_service_status_t status;
    trigger_sequence_service_get_status(&status);
    SCPI_ResultText(context, status.timing_kind == TRIGGER_SEQUENCE_TIMING_PIO0 ? "PIO0" : "NONE");
    SCPI_ResultUInt32(context, status.tick_ns);
    SCPI_ResultBool(context, false);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sequence_rejections_q(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    trigger_sequence_service_status_t status;
    trigger_sequence_service_get_status(&status);
    SCPI_ResultUInt32(context, status.run_id);
    SCPI_ResultUInt32(context, status.generation);
    SCPI_ResultUInt32(context, status.busy_rejected);
    SCPI_ResultUInt32(context, status.notready_rejected);
    SCPI_ResultBool(context, status.rejection_counts_pending);
    return SCPI_RES_OK;
}

static scpi_result_t read_levels(scpi_t *context, bool output)
{
    scpi_parameter_t parameter;
    uint32_t channel = 0u;
    if (SCPI_Parameter(context, &parameter, FALSE)) {
        /* Use the parsed token as an unsigned decimal channel, rejecting signs. */
        if (parameter.type != SCPI_TOKEN_DECIMAL_NUMERIC_PROGRAM_DATA ||
            parameter.len != 1u || parameter.ptr[0] < '1' || parameter.ptr[0] > '4') {
            SCPI_ErrorPush(context, SCPI_ERROR_ILLEGAL_PARAMETER_VALUE);
            return SCPI_RES_ERR;
        }
        channel = (uint32_t)(parameter.ptr[0] - '0');
    }
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    const uint32_t mask = output ? sync_io_sequence_read_outputs() : sync_io_sequence_read_inputs();
    SCPI_ResultUInt32(context, channel != 0u ? (mask >> (channel - 1u)) & 1u : mask);
    return SCPI_RES_OK;
}

scpi_result_t scpi_sequence_input_q(scpi_t *context)
{
    return read_levels(context, false);
}

scpi_result_t scpi_sequence_output_q(scpi_t *context)
{
    return read_levels(context, true);
}

scpi_result_t scpi_sequence_io_state_q(scpi_t *context)
{
    if (!scpi_sequence_params_end(context)) return SCPI_RES_ERR;
    sync_io_sequence_snapshot_t snapshot;
    sync_io_sequence_get_snapshot(&snapshot);
    SCPI_ResultUInt32(context, sync_io_sequence_read_inputs());
    SCPI_ResultUInt32(context, sync_io_sequence_read_outputs());
    SCPI_ResultUInt32(context, sync_io_sequence_owned_mask());
    SCPI_ResultBool(context, snapshot.armed);
    SCPI_ResultBool(context, snapshot.busy);
    return SCPI_RES_OK;
}
