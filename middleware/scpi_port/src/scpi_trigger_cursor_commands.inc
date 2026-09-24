#include "trigger_time_cursor.h"

scpi_result_t scpi_cmd_trigger_cursor_q(scpi_t *context)
{
    trigger_time_cursor_snapshot_t s;
    if (!trigger_time_cursor_snapshot(&s)) return SCPI_RES_ERR;
    SCPI_ResultUInt32(context, s.configured);
    SCPI_ResultUInt32(context, s.valid);
    SCPI_ResultUInt32(context, s.grid_epoch);
    SCPI_ResultUInt32(context, s.grid_generation);
    SCPI_ResultUInt32(context, s.subdivisions);
    SCPI_ResultUInt32(context, s.session);
    SCPI_ResultUInt32(context, s.sample_seq);
    SCPI_ResultUInt32(context, s.last_event_id);
    SCPI_ResultUInt32(context, s.model_token);
    SCPI_ResultUInt32(context, s.dco_update_seq);
    SCPI_ResultUInt32(context, s.missed_sync_count);
    SCPI_ResultUInt64(context, s.pulse_index);
    SCPI_ResultUInt32(context, s.fine_slice_index);
    SCPI_ResultUInt64(context, s.action_cursor);
    SCPI_ResultUInt32(context, s.last_error);
    return SCPI_RES_OK;
}
