#include "scpi_trigger_cursor_commands.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "trigger_time_cursor.h"
#include "sync_trigger.h"
#include "sync_io_turntable_source.h"
#include "scpi_trigger_grid_commands.h"

scpi_result_t scpi_cmd_trigger_sim_source_configure(scpi_t *context)
{
    uint64_t frequency;
    if (!scpi_trigger_grid_uint(context,UINT32_MAX,&frequency) ||
        !scpi_trigger_grid_end(context))
        return scpi_trigger_grid_result(context,TRIGGER_TIME_GRID_ARGUMENT);
    return scpi_trigger_grid_result(context,
        sync_io_turntable_source_prepare((uint32_t)frequency) ? 0u :
        TRIGGER_TIME_GRID_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_sim_source_start(scpi_t *context)
{
    return scpi_trigger_grid_result(context,
        scpi_trigger_grid_end(context) && sync_io_turntable_source_start() ?
        0u : TRIGGER_TIME_GRID_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_sim_source_stop(scpi_t *context)
{
    return scpi_trigger_grid_result(context,
        scpi_trigger_grid_end(context) && sync_io_turntable_source_stop() ?
        0u : TRIGGER_TIME_GRID_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_sim_source_release(scpi_t *context)
{
    return scpi_trigger_grid_result(context,
        scpi_trigger_grid_end(context) && sync_io_turntable_source_release() ?
        0u : TRIGGER_TIME_GRID_ARGUMENT);
}

scpi_result_t scpi_cmd_trigger_sim_source_q(scpi_t *context)
{
    sync_io_turntable_source_status_t s;
    if (!scpi_trigger_grid_end(context) || !sync_io_turntable_source_get_status(&s))
        return SCPI_RES_ERR;
    SCPI_ResultUInt32(context,s.prepared);
    SCPI_ResultUInt32(context,s.running);
    SCPI_ResultUInt32(context,s.output_port);
    SCPI_ResultUInt32(context,s.output_pin);
    SCPI_ResultUInt32(context,s.requested_hz);
    SCPI_ResultUInt32(context,s.actual_hz);
    SCPI_ResultUInt32(context,s.system_clock_hz);
    SCPI_ResultUInt32(context,s.pwm_slice);
    SCPI_ResultUInt32(context,s.pwm_top);
    SCPI_ResultUInt32(context,s.pwm_div16);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_trigger_timing_q(scpi_t *context)
{
    sync_trigger_timing_t s;
    if (!scpi_trigger_grid_end(context) || !sync_trigger_get_timing(&s)) return SCPI_RES_ERR;
    SCPI_ResultUInt32(context,s.calls);
    SCPI_ResultUInt32(context,s.runtime_last);
    SCPI_ResultUInt32(context,s.runtime_max);
    SCPI_ResultUInt32(context,s.cursor_last);
    SCPI_ResultUInt32(context,s.cursor_max);
    SCPI_ResultUInt32(context,s.queue_last);
    SCPI_ResultUInt32(context,s.queue_max);
    return SCPI_RES_OK;
}

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
