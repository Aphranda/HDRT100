#include "trigger_gateway_service.h"

scpi_result_t scpi_cmd_trigger_gateway_load(scpi_t *context)
{
    uint32_t generation,crc,pulse_ns;
    if (!scpi_trigger_deployment_u32(context,&generation) ||
        !scpi_trigger_deployment_u32(context,&crc) ||
        !scpi_trigger_deployment_u32(context,&pulse_ns) || !scpi_trigger_grid_end(context))
        return scpi_trigger_grid_result(context,GATEWAY_ARGUMENT);
    return scpi_trigger_grid_result(context,trigger_gateway_load(generation,crc,pulse_ns));
}
scpi_result_t scpi_cmd_trigger_gateway_stop(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_gateway_stop():GATEWAY_ARGUMENT);
}
scpi_result_t scpi_cmd_trigger_gateway_unload(scpi_t *context)
{
    return scpi_trigger_grid_result(context,scpi_trigger_grid_end(context)?
        trigger_gateway_unload():GATEWAY_ARGUMENT);
}
scpi_result_t scpi_cmd_trigger_gateway_q(scpi_t *context)
{
    trigger_gateway_snapshot_t s;
    const uint32_t result=scpi_trigger_grid_end(context)?trigger_gateway_snapshot(&s):GATEWAY_ARGUMENT;
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
    SCPI_ResultUInt32(context,s.io.state);
    SCPI_ResultUInt32(context,s.io.loaded);
    SCPI_ResultUInt32(context,s.io.input_port);
    SCPI_ResultUInt32(context,s.io.input_pin);
    SCPI_ResultUInt32(context,s.io.output_port);
    SCPI_ResultUInt32(context,s.io.output_pin);
    SCPI_ResultUInt32(context,s.io.pulse_ns);
    SCPI_ResultUInt32(context,s.io.pulse_ticks);
    SCPI_ResultUInt32(context,s.io.tick_hz);
    SCPI_ResultUInt32(context,s.io.ready_level);
    SCPI_ResultUInt32(context,s.io.sm_claimed_mask);
    SCPI_ResultUInt32(context,s.io.sm_enabled_mask);
    SCPI_ResultUInt32(context,s.io.instruction_words);
    SCPI_ResultUInt32(context,s.io.output_program_offset);
    SCPI_ResultUInt32(context,s.io.ready_program_offset);
    SCPI_ResultUInt32(context,s.io.grant_pending);
    SCPI_ResultUInt32(context,s.io.output_level);
    SCPI_ResultUInt32(context,s.io.trigger_count);
    SCPI_ResultUInt32(context,s.io.ready_count);
    SCPI_ResultUInt32(context,s.io.output_tx_level);
    SCPI_ResultUInt32(context,s.io.output_rx_level);
    SCPI_ResultUInt32(context,s.io.ready_tx_level);
    SCPI_ResultUInt32(context,s.io.ready_rx_level);
    /* Appended: READY answer-vs-next-action evidence (see sync_io_gateway.h). */
    SCPI_ResultUInt32(context,s.io.ready_collision_count);
    SCPI_ResultUInt32(context,s.io.ready_margin_min_ticks);
    return SCPI_RES_OK;
}
