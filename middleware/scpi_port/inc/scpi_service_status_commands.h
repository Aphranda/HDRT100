#ifndef SCPI_SERVICE_STATUS_COMMANDS_H
#define SCPI_SERVICE_STATUS_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_loop_status_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_status_q(scpi_t *context);
scpi_result_t scpi_cmd_dpll_status_q(scpi_t *context);

#define SCPI_SERVICE_STATUS_COMMANDS \
    {.pattern = "LOOP:STATus?", .callback = scpi_cmd_loop_status_q}, \
    {.pattern = "LOOP:STAT?", .callback = scpi_cmd_loop_status_q}, \
    {.pattern = "STATus:LOOP?", .callback = scpi_cmd_loop_status_q}, \
    {.pattern = "VDC:STATus?", .callback = scpi_cmd_vdc_status_q}, \
    {.pattern = "VDC:STAT?", .callback = scpi_cmd_vdc_status_q}, \
    {.pattern = "STATus:VDC?", .callback = scpi_cmd_vdc_status_q}, \
    {.pattern = "DPLL:STATus?", .callback = scpi_cmd_dpll_status_q}, \
    {.pattern = "DPLL:STAT?", .callback = scpi_cmd_dpll_status_q}, \
    {.pattern = "STATus:DPLL?", .callback = scpi_cmd_dpll_status_q}

#endif
