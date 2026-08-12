#include "scpi_realtime_status_commands.h"

#include "sync_trigger.h"

static const char *scpi_trig_mode_to_string(trig_mode_t mode)
{
    switch (mode) {
    case TRIG_MODE_IDLE:             return "IDLE";
    case TRIG_MODE_SEQ_STEP:         return "SEQ_STEP";
    case TRIG_MODE_ENC_COUNT:        return "ENC_COUNT";
    case TRIG_MODE_PROTOCOL_TRIGGER: return "PROTOCOL_TRIGGER";
    default:                         return "UNKNOWN";
    }
}

scpi_result_t scpi_cmd_trigger_status_q(scpi_t *context)
{
    trigger_vector_t vector;
    sync_trigger_get_vector(&vector);

    SCPI_ResultText(context, scpi_trig_mode_to_string(vector.active_mode));
    SCPI_ResultUInt32(context, (uint32_t)vector.state);
    SCPI_ResultUInt32(context, vector.trigger_source_pin);
    SCPI_ResultUInt32(context, vector.seq_index);
    SCPI_ResultUInt32(context, vector.enc_target);
    SCPI_ResultUInt32(context, vector.enc_count);
    SCPI_ResultUInt32(context, vector.trigger_count);
    SCPI_ResultUInt32(context, vector.rollover_count);
    SCPI_ResultUInt32(context, vector.error_code);
    return SCPI_RES_OK;
}
