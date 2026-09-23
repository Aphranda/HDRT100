#include "trigger_counter_service.h"

scpi_result_t scpi_cmd_trigger_counter_load(scpi_t *context)
{
    uint32_t generation,crc;
    if (!scpi_trigger_deployment_u32(context,&generation) || !scpi_trigger_deployment_u32(context,&crc) ||
        !scpi_trigger_grid_end(context)) return scpi_trigger_grid_result(context,COUNTER_ARGUMENT);
    return scpi_trigger_grid_result(context,trigger_counter_load(generation,crc));
}
scpi_result_t scpi_cmd_trigger_counter_start(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_counter_start():COUNTER_ARGUMENT);
}
scpi_result_t scpi_cmd_trigger_counter_stop(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_counter_stop():COUNTER_ARGUMENT);
}
scpi_result_t scpi_cmd_trigger_counter_clear(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_counter_clear():COUNTER_ARGUMENT);
}
scpi_result_t scpi_cmd_trigger_counter_unload(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_counter_unload():COUNTER_ARGUMENT);
}
scpi_result_t scpi_cmd_trigger_counter_q(scpi_t *context)
{
    trigger_counter_snapshot_t s;
    const uint32_t result=scpi_trigger_grid_end(context)?trigger_counter_snapshot(&s):COUNTER_ARGUMENT;
    scpi_trigger_grid_result(context,result);
    if (result) return SCPI_RES_OK;
    SCPI_ResultUInt32(context,s.loaded);
    SCPI_ResultUInt32(context,s.binding.table.generation);
    SCPI_ResultUInt32(context,s.binding.crc);
    SCPI_ResultUInt64(context,s.binding.local_uid);
    SCPI_ResultUInt32(context,s.binding.local_role);
    SCPI_ResultUInt32(context,s.binding.local_slot);
    SCPI_ResultUInt32(context,s.binding.ring_config);
    SCPI_ResultUInt32(context,s.binding.schedule_crc);
    SCPI_ResultUInt32(context,s.io.schema);
    SCPI_ResultUInt32(context,s.io.generation);
    SCPI_ResultUInt32(context,s.io.clear_generation);
    SCPI_ResultUInt32(context,s.io.state);
    SCPI_ResultUInt32(context,s.io.input_port);
    SCPI_ResultUInt32(context,s.io.input_pin);
    SCPI_ResultUInt32(context,s.io.count32);
    SCPI_ResultUInt32(context,s.io.valid);
    SCPI_ResultUInt32(context,s.io.tick_hz);
    SCPI_ResultUInt32(context,s.io.timestamp_valid);
    SCPI_ResultUInt64(context,s.io.sample_before_tick);
    SCPI_ResultUInt64(context,s.io.sample_after_tick);
    return SCPI_RES_OK;
}
