#ifndef SCPI_TDMA_COMMANDS_H
#define SCPI_TDMA_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_tdma_opmode_catalog_q(scpi_t *context);
scpi_result_t scpi_cmd_tdma_opmode_q(scpi_t *context);
scpi_result_t scpi_cmd_tdma_opmode_stage(scpi_t *context);
scpi_result_t scpi_cmd_tdma_opmode_apply(scpi_t *context);
scpi_result_t scpi_cmd_tdma_event_tap(scpi_t *context);
scpi_result_t scpi_cmd_tdma_event_tap_q(scpi_t *context);
scpi_result_t scpi_cmd_tdma_event_recovery_q(scpi_t *context);
scpi_result_t scpi_cmd_tdma_event_live_q(scpi_t *context);

#define SCPI_TDMA_COMMANDS \
    {.pattern = "SYSTem:TDMA:EVENt:LIVE?", \
     .callback = scpi_cmd_tdma_event_live_q}, \
    {.pattern = "SYSTem:TDMA:EVENt:RECovery?", \
     .callback = scpi_cmd_tdma_event_recovery_q}, \
    {.pattern = "SYSTem:TDMA:EVENt:TAP", \
     .callback = scpi_cmd_tdma_event_tap}, \
    {.pattern = "SYSTem:TDMA:EVENt:TAP?", \
     .callback = scpi_cmd_tdma_event_tap_q}, \
    {.pattern = "SYSTem:TDMA:OPMode:CATalog?", \
     .callback = scpi_cmd_tdma_opmode_catalog_q}, \
    {.pattern = "SYSTem:TDMA:OPMode?", \
     .callback = scpi_cmd_tdma_opmode_q}, \
    {.pattern = "SYSTem:TDMA:OPMode:STAGe", \
     .callback = scpi_cmd_tdma_opmode_stage}, \
    {.pattern = "SYSTem:TDMA:OPMode:APPLy", \
     .callback = scpi_cmd_tdma_opmode_apply}

#endif
