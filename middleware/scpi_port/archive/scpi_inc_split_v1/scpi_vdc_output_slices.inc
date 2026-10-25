/* RUN output configuration; parsing completes before any owner mutation. */
scpi_result_t scpi_cmd_vdc_subdivision_after_sync_run_q(scpi_t *context)
{
    vdc_run_output_status_t status;
    if (!scpi_vdc_run_end(context) || !vdc_run_output_status(&status)) {
        scpi_port_push_exec_error(context, "VDC_OUTPUT_SLICES_RUN_NOT_RETIRED");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, 1u);
    SCPI_ResultUInt32(context, status.request);
    SCPI_ResultUInt32(context, status.slices.enabled);
    SCPI_ResultUInt32(context, status.slices.interval_ns);
    SCPI_ResultUInt32(context, status.slices.high_ns);
    SCPI_ResultUInt32(context, status.slices.marker_ns);
    SCPI_ResultUInt32(context, status.period_ns);
    SCPI_ResultUInt32(context, status.hardware.tick_hz);
    SCPI_ResultUInt32(context, status.hardware.state);
    SCPI_ResultUInt32(context, status.hardware.reason);
    SCPI_ResultUInt64(context, status.hardware.last_rising_tick);
    SCPI_ResultUInt64(context, status.hardware.last_falling_tick);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_subdivision_after_sync_q(scpi_t *context)
{
    vdc_run_output_slices_t profile;
    if (!scpi_vdc_run_end(context) || !vdc_run_output_get_slices(&profile)) {
        scpi_port_push_exec_error(context, "VDC_OUTPUT_SLICES_QUERY_BUSY_OR_PARAMETERS");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, profile.enabled);
    SCPI_ResultUInt32(context, profile.interval_ns);
    SCPI_ResultUInt32(context, profile.high_ns);
    SCPI_ResultUInt32(context, profile.marker_ns);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_subdivision_after_sync(scpi_t *context)
{
    uint32_t enabled;
    if (!scpi_vdc_run_u32(context, &enabled) || enabled > 1u ||
        !scpi_vdc_run_end(context) || !vdc_run_output_set_slices_enabled(enabled != 0u)) {
        scpi_port_push_exec_error(context, "VDC_OUTPUT_SLICES_STOP_IDLE_OR_PARAMETERS");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, enabled);
    return SCPI_RES_OK;
}

scpi_result_t scpi_cmd_vdc_subdivision_after_sync_configure(scpi_t *context)
{
    uint32_t interval, high, marker;
    if (!scpi_vdc_run_u32(context, &interval) || !scpi_vdc_run_u32(context, &high) ||
        !scpi_vdc_run_u32(context, &marker) || !scpi_vdc_run_end(context) ||
        !vdc_run_output_configure_slices(interval, high, marker)) {
        scpi_port_push_exec_error(context, "VDC_OUTPUT_SLICES_STOP_IDLE_OR_GEOMETRY");
        return SCPI_RES_ERR;
    }
    SCPI_ResultUInt32(context, interval);
    SCPI_ResultUInt32(context, high);
    SCPI_ResultUInt32(context, marker);
    return SCPI_RES_OK;
}
