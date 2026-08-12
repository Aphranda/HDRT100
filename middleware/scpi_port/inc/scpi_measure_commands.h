#ifndef SCPI_MEASURE_COMMANDS_H
#define SCPI_MEASURE_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_meas_freq_q(scpi_t *context);
scpi_result_t scpi_cmd_meas_period_q(scpi_t *context);
scpi_result_t scpi_cmd_meas_jitter_q(scpi_t *context);
scpi_result_t scpi_cmd_meas_pulse_width_q(scpi_t *context);
scpi_result_t scpi_cmd_meas_link_delay_q(scpi_t *context);
scpi_result_t scpi_cmd_meas_t2_q(scpi_t *context);
scpi_result_t scpi_cmd_meas_report_q(scpi_t *context);

#define SCPI_MEASURE_COMMANDS \
    {.pattern = "MEASure:FREQuency?", .callback = scpi_cmd_meas_freq_q}, \
    {.pattern = "MEASure:PERiod?", .callback = scpi_cmd_meas_period_q}, \
    {.pattern = "MEASure:JITTer?", .callback = scpi_cmd_meas_jitter_q}, \
    {.pattern = "MEASure:PULSe:WIDTh?", .callback = scpi_cmd_meas_pulse_width_q}, \
    {.pattern = "MEASure:LINK:DELay?", .callback = scpi_cmd_meas_link_delay_q}, \
    {.pattern = "MEASure:T2?", .callback = scpi_cmd_meas_t2_q}, \
    {.pattern = "MEASure:REPort?", .callback = scpi_cmd_meas_report_q}

#endif
