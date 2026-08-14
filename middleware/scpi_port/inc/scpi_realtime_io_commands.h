#ifndef SCPI_REALTIME_IO_COMMANDS_H
#define SCPI_REALTIME_IO_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_trigger_width(scpi_t *context);
scpi_result_t scpi_cmd_trigger_width_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_fire(scpi_t *context);
scpi_result_t scpi_cmd_pulse_width(scpi_t *context);
scpi_result_t scpi_cmd_pulse_width_q(scpi_t *context);
scpi_result_t scpi_cmd_pulse_fire(scpi_t *context);
scpi_result_t scpi_cmd_marker_width(scpi_t *context);
scpi_result_t scpi_cmd_marker_width_q(scpi_t *context);
scpi_result_t scpi_cmd_marker_fire(scpi_t *context);
scpi_result_t scpi_cmd_rj45_trigger_width(scpi_t *context);
scpi_result_t scpi_cmd_rj45_trigger_width_q(scpi_t *context);
scpi_result_t scpi_cmd_rj45_trigger_fire(scpi_t *context);
scpi_result_t scpi_cmd_rj45_trigger_pins_q(scpi_t *context);
scpi_result_t scpi_cmd_io_profile_q(scpi_t *context);
scpi_result_t scpi_cmd_input_level_q(scpi_t *context);
scpi_result_t scpi_cmd_output_mask(scpi_t *context);
scpi_result_t scpi_cmd_output_mask_q(scpi_t *context);
scpi_result_t scpi_cmd_output_release(scpi_t *context);
scpi_result_t scpi_cmd_sample_rate(scpi_t *context);
scpi_result_t scpi_cmd_sample_rate_q(scpi_t *context);
scpi_result_t scpi_cmd_sample_state(scpi_t *context);
scpi_result_t scpi_cmd_sample_state_q(scpi_t *context);
scpi_result_t scpi_cmd_clock_freq(scpi_t *context);
scpi_result_t scpi_cmd_clock_freq_q(scpi_t *context);
scpi_result_t scpi_cmd_clock_state(scpi_t *context);
scpi_result_t scpi_cmd_clock_state_q(scpi_t *context);
scpi_result_t scpi_cmd_status_q(scpi_t *context);

#define SCPI_REALTIME_IO_COMMANDS \
    {.pattern = "REALtime:IO:OUTPut:WIDTh", .callback = scpi_cmd_trigger_width}, \
    {.pattern = "REALtime:IO:OUTPut:WIDTh?", .callback = scpi_cmd_trigger_width_q}, \
    {.pattern = "REALtime:IO:OUTPut:IMMediate", .callback = scpi_cmd_trigger_fire}, \
    {.pattern = "REALtime:IO:PULSe:WIDTh", .callback = scpi_cmd_pulse_width}, \
    {.pattern = "REALtime:IO:PULSe:WIDTh?", .callback = scpi_cmd_pulse_width_q}, \
    {.pattern = "REALtime:IO:PULSe:IMMediate", .callback = scpi_cmd_pulse_fire}, \
    {.pattern = "REALtime:IO:MARKer:WIDTh", .callback = scpi_cmd_marker_width}, \
    {.pattern = "REALtime:IO:MARKer:WIDTh?", .callback = scpi_cmd_marker_width_q}, \
    {.pattern = "REALtime:IO:MARKer:IMMediate", .callback = scpi_cmd_marker_fire}, \
    {.pattern = "REALtime:IO:RJ45:WIDTh", .callback = scpi_cmd_rj45_trigger_width}, \
    {.pattern = "REALtime:IO:RJ45:WIDTh?", .callback = scpi_cmd_rj45_trigger_width_q}, \
    {.pattern = "REALtime:IO:RJ45:IMMediate", .callback = scpi_cmd_rj45_trigger_fire}, \
    {.pattern = "REALtime:IO:RJ45:PINs?", .callback = scpi_cmd_rj45_trigger_pins_q}, \
    {.pattern = "REALtime:IO:PROFile?", .callback = scpi_cmd_io_profile_q}, \
    {.pattern = "REALtime:IO:INPut:LEVel?", .callback = scpi_cmd_input_level_q}, \
    {.pattern = "REALtime:IO:OUTPut:MASK", .callback = scpi_cmd_output_mask}, \
    {.pattern = "REALtime:IO:OUTPut:MASK?", .callback = scpi_cmd_output_mask_q}, \
    {.pattern = "REALtime:IO:OUTPut:RELease", .callback = scpi_cmd_output_release}, \
    {.pattern = "REALtime:IO:SAMPle:RATE", .callback = scpi_cmd_sample_rate}, \
    {.pattern = "REALtime:IO:SAMPle:RATE?", .callback = scpi_cmd_sample_rate_q}, \
    {.pattern = "REALtime:IO:SAMPle:STATe", .callback = scpi_cmd_sample_state}, \
    {.pattern = "REALtime:IO:SAMPle:STATe?", .callback = scpi_cmd_sample_state_q}, \
    {.pattern = "REALtime:IO:CLOCk:FREQuency", .callback = scpi_cmd_clock_freq}, \
    {.pattern = "REALtime:IO:CLOCk:FREQuency?", .callback = scpi_cmd_clock_freq_q}, \
    {.pattern = "REALtime:IO:CLOCk:STATe", .callback = scpi_cmd_clock_state}, \
    {.pattern = "REALtime:IO:CLOCk:STATe?", .callback = scpi_cmd_clock_state_q}, \
    {.pattern = "REALtime:IO:SYNC?", .callback = scpi_cmd_status_q}

#endif
