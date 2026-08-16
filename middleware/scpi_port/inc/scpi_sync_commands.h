#ifndef SCPI_SYNC_COMMANDS_H
#define SCPI_SYNC_COMMANDS_H

#include "scpi/scpi.h"

#include "scpi_calibration_commands.h"
#include "scpi_port_internal.h"

scpi_result_t scpi_sync_state_q(scpi_t *context);
scpi_result_t scpi_sync_parameter_q(scpi_t *context);
scpi_result_t scpi_sync_health_q(scpi_t *context);
scpi_result_t scpi_sync_node_q(scpi_t *context);
scpi_result_t scpi_sync_check_q(scpi_t *context);
scpi_result_t scpi_sync_list_q(scpi_t *context);
scpi_result_t scpi_sync_active_q(scpi_t *context);
scpi_result_t scpi_sync_quality_q(scpi_t *context);
scpi_result_t scpi_sync_version_q(scpi_t *context);
scpi_result_t scpi_sync_override_q(scpi_t *context);
scpi_result_t scpi_sync_coef_q(scpi_t *context);
scpi_result_t scpi_cmd_sync_vdc_status_q(scpi_t *context);
scpi_result_t scpi_cmd_sync_vdc_dpll_status_q(scpi_t *context);
scpi_result_t scpi_cmd_sync_vdc_tdma_plan_q(scpi_t *context);
scpi_result_t scpi_cmd_sync_vdc_observer(scpi_t *context);
scpi_result_t scpi_cmd_sync_vdc_observer_q(scpi_t *context);

#define SCPI_SYNC_COMMANDS \
    {.pattern = "SYNC:CHECk", .callback = scpi_sync_check_q}, \
    {.pattern = "SYNC:STARt", .callback = scpi_port_result_accepted}, \
    {.pattern = "SYNC:STOP", .callback = scpi_port_result_accepted}, \
    {.pattern = "SYNC:RELock", .callback = scpi_port_result_accepted}, \
    {.pattern = "SYNC:HOLDover", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:SYNC:STATe?", .callback = scpi_sync_state_q}, \
    {.pattern = "READ:SYNC:PARameter?", .callback = scpi_sync_parameter_q}, \
    {.pattern = "READ:SYNC:HEALth?", .callback = scpi_sync_health_q}, \
    {.pattern = "READ:SYNC:NODE?", .callback = scpi_sync_node_q}, \
    {.pattern = "READ:SYNC:LINK?", .callback = scpi_calibration_link_q}, \
    {.pattern = "READ:SYNC:CHECk?", .callback = scpi_sync_check_q}, \
    {.pattern = "CONFigure:SYNC:CALibration", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:SYNC:RING", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:SYNC:VDC:DPLL", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:SYNC:GATE", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:SYNC:LIMit", .callback = scpi_port_result_accepted}, \
    {.pattern = "SYNC:SAVE", .callback = scpi_port_result_accepted}, \
    {.pattern = "SYNC:LOAD", .callback = scpi_port_result_accepted}, \
    {.pattern = "SYNC:ACTivate", .callback = scpi_port_result_accepted}, \
    {.pattern = "SYNC:ROLLback", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:SYNC:LIST?", .callback = scpi_sync_list_q}, \
    {.pattern = "READ:SYNC:ACTive?", .callback = scpi_sync_active_q}, \
    {.pattern = "READ:SYNC:QUALity?", .callback = scpi_sync_quality_q}, \
    {.pattern = "READ:SYNC:VERSion?", .callback = scpi_sync_version_q}, \
    {.pattern = "SYSTem:SYNC:VDC:STATus?", .callback = scpi_cmd_sync_vdc_status_q}, \
    {.pattern = "SYSTem:SYNC:VDC:DPLL:STATus?", .callback = scpi_cmd_sync_vdc_dpll_status_q}, \
    {.pattern = "SYSTem:SYNC:VDC:TDMA:PLAN?", .callback = scpi_cmd_sync_vdc_tdma_plan_q}, \
    {.pattern = "SYSTem:SYNC:VDC:OBServer", .callback = scpi_cmd_sync_vdc_observer}, \
    {.pattern = "SYSTem:SYNC:VDC:OBServer?", .callback = scpi_cmd_sync_vdc_observer_q}, \
    {.pattern = "SYSTem:SYNC:VDC:DPLL:TUNE", .callback = scpi_port_result_accepted}, \
    {.pattern = "SYSTem:SYNC:VDC:DPLL:COEFficient", .callback = scpi_port_result_accepted}, \
    {.pattern = "SYSTem:SYNC:VDC:DPLL:OVERRide?", .callback = scpi_sync_override_q}, \
    {.pattern = "SYSTem:SYNC:VDC:DPLL:COEFficient?", .callback = scpi_sync_coef_q}, \
    {.pattern = "SYSTem:SYNC:VDC:DPLL:DEFAult", .callback = scpi_port_result_accepted}

#endif
