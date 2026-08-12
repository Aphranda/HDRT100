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
    {.pattern = "REALtime:IO:TRIGger:WIDTh", .callback = scpi_cmd_trigger_width}, \
    {.pattern = "REALtime:IO:TRIGger:WIDTh?", .callback = scpi_cmd_trigger_width_q}, \
    {.pattern = "REALtime:IO:TRIGger:IMMediate", .callback = scpi_cmd_trigger_fire}, \
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
    {.pattern = "REALtime:IO:SAMPle:RATE", .callback = scpi_cmd_sample_rate}, \
    {.pattern = "REALtime:IO:SAMPle:RATE?", .callback = scpi_cmd_sample_rate_q}, \
    {.pattern = "REALtime:IO:SAMPle:STATe", .callback = scpi_cmd_sample_state}, \
    {.pattern = "REALtime:IO:SAMPle:STATe?", .callback = scpi_cmd_sample_state_q}, \
    {.pattern = "REALtime:IO:CLOCk:FREQuency", .callback = scpi_cmd_clock_freq}, \
    {.pattern = "REALtime:IO:CLOCk:FREQuency?", .callback = scpi_cmd_clock_freq_q}, \
    {.pattern = "REALtime:IO:CLOCk:STATe", .callback = scpi_cmd_clock_state}, \
    {.pattern = "REALtime:IO:CLOCk:STATe?", .callback = scpi_cmd_clock_state_q}, \
    {.pattern = "REALtime:IO:SYNC?", .callback = scpi_cmd_status_q}, \
    {.pattern = "TRIGger:WIDTh", .callback = scpi_cmd_trigger_width}, \
    {.pattern = "TRIGger:WIDTh?", .callback = scpi_cmd_trigger_width_q}, \
    {.pattern = "TRIGger:IMMediate", .callback = scpi_cmd_trigger_fire}, \
    {.pattern = "PULSe:WIDTh", .callback = scpi_cmd_pulse_width}, \
    {.pattern = "PULSe:WIDTh?", .callback = scpi_cmd_pulse_width_q}, \
    {.pattern = "PULSe:IMMediate", .callback = scpi_cmd_pulse_fire}, \
    {.pattern = "MARKer:WIDTh", .callback = scpi_cmd_marker_width}, \
    {.pattern = "MARKer:WIDTh?", .callback = scpi_cmd_marker_width_q}, \
    {.pattern = "MARKer:IMMediate", .callback = scpi_cmd_marker_fire}, \
    {.pattern = "RJ45:TRIGger:WIDTh", .callback = scpi_cmd_rj45_trigger_width}, \
    {.pattern = "RJ45:TRIGger:WIDTh?", .callback = scpi_cmd_rj45_trigger_width_q}, \
    {.pattern = "RJ45:TRIGger:IMMediate", .callback = scpi_cmd_rj45_trigger_fire}, \
    {.pattern = "RJ45:TRIGger:PINs?", .callback = scpi_cmd_rj45_trigger_pins_q}, \
    {.pattern = "SAMPle:RATE", .callback = scpi_cmd_sample_rate}, \
    {.pattern = "SAMPle:RATE?", .callback = scpi_cmd_sample_rate_q}, \
    {.pattern = "SAMPle:STATe", .callback = scpi_cmd_sample_state}, \
    {.pattern = "SAMPle:STATe?", .callback = scpi_cmd_sample_state_q}, \
    {.pattern = "OUTPut:CLOCk:FREQuency", .callback = scpi_cmd_clock_freq}, \
    {.pattern = "OUTPut:CLOCk:FREQuency?", .callback = scpi_cmd_clock_freq_q}, \
    {.pattern = "OUTPut:CLOCk:STATe", .callback = scpi_cmd_clock_state}, \
    {.pattern = "OUTPut:CLOCk:STATe?", .callback = scpi_cmd_clock_state_q}, \
    {.pattern = "STATus:SYNC?", .callback = scpi_cmd_status_q}

#endif
