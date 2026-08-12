#ifndef SCPI_SYSTEM_DIAGNOSTICS_COMMANDS_H
#define SCPI_SYSTEM_DIAGNOSTICS_COMMANDS_H

#include "scpi/scpi.h"
#include "scpi_port_internal.h"

scpi_result_t scpi_system_diagnostics_run_last_q(scpi_t *context);
scpi_result_t scpi_system_diagnostics_run_summary_q(scpi_t *context);
scpi_result_t scpi_system_diagnostics_page_block_q(scpi_t *context);
scpi_result_t scpi_system_diagnostics_count_zero_q(scpi_t *context);
scpi_result_t scpi_system_diagnostics_statistics_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_debug_q(scpi_t *context);
scpi_result_t scpi_cmd_resource_status_q(scpi_t *context);

#define SCPI_SYSTEM_DIAGNOSTICS_COMMANDS \
    {.pattern = "SYSTem:RUN:LAST?", .callback = scpi_system_diagnostics_run_last_q}, \
    {.pattern = "SYSTem:RUN:SUMMary?", .callback = scpi_system_diagnostics_run_summary_q}, \
    {.pattern = "SYSTem:RUN:LOG?", .callback = scpi_system_diagnostics_page_block_q}, \
    {.pattern = "SYSTem:LOG:PAGE?", .callback = scpi_system_diagnostics_page_block_q}, \
    {.pattern = "SYSTem:TRACe:DATA?", .callback = scpi_system_diagnostics_page_block_q}, \
    {.pattern = "SYSTem:SNAPshot:DATA?", .callback = scpi_system_diagnostics_page_block_q}, \
    {.pattern = "SYSTem:T2:DATA?", .callback = scpi_system_diagnostics_page_block_q}, \
    {.pattern = "SYSTem:TRIGger:DBG?", .callback = scpi_cmd_trigger_debug_q}, \
    {.pattern = "SYSTem:RESource?", .callback = scpi_cmd_resource_status_q}, \
    {.pattern = "SYSTem:FAULT:CLEAr", .callback = scpi_port_result_accepted}

#define SCPI_SYSTEM_DIAGNOSTICS_READ_COMMANDS \
    {.pattern = "READ:RUN:SUMMary?", .callback = scpi_system_diagnostics_run_summary_q}, \
    {.pattern = "READ:STATistics?", .callback = scpi_system_diagnostics_statistics_q}, \
    {.pattern = "READ:T2:COUNt?", .callback = scpi_system_diagnostics_count_zero_q}, \
    {.pattern = "READ:T2:DATA?", .callback = scpi_system_diagnostics_page_block_q}

#endif
