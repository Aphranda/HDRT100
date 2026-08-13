#ifndef SCPI_SYSTEM_SNAPSHOT_COMMANDS_H
#define SCPI_SYSTEM_SNAPSHOT_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_refmem_status_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_node_q(scpi_t *context);
scpi_result_t scpi_cmd_refmem_load_sd(scpi_t *context);
scpi_result_t scpi_cmd_refmem_load_node(scpi_t *context);
scpi_result_t scpi_cmd_refmem_load_status_q(scpi_t *context);
scpi_result_t scpi_cmd_core_vector_q(scpi_t *context);
scpi_result_t scpi_cmd_runtime_protection_q(scpi_t *context);
scpi_result_t scpi_cmd_config_gate_status_q(scpi_t *context);
scpi_result_t scpi_cmd_config_ack_q(scpi_t *context);
scpi_result_t scpi_cmd_config_nack_reason_q(scpi_t *context);
scpi_result_t scpi_cmd_scpi_run_allow_q(scpi_t *context);
scpi_result_t scpi_cmd_config_role_q(scpi_t *context);
scpi_result_t scpi_cmd_config_loop_q(scpi_t *context);
scpi_result_t scpi_cmd_config_action_q(scpi_t *context);
scpi_result_t scpi_cmd_config_calibration_q(scpi_t *context);
scpi_result_t scpi_cmd_system_mode_table_q(scpi_t *context);
scpi_result_t scpi_cmd_resource_arbiter_table_q(scpi_t *context);
scpi_result_t scpi_cmd_fault_code_table_q(scpi_t *context);

#define SCPI_SYSTEM_SNAPSHOT_COMMANDS \
    {.pattern = "SYSTem:CONFigure:STAT?", .callback = scpi_cmd_config_gate_status_q}, \
    {.pattern = "SYSTem:CONFigure:ROLE?", .callback = scpi_cmd_config_role_q}, \
    {.pattern = "SYSTem:CONFigure:LOOP?", .callback = scpi_cmd_config_loop_q}, \
    {.pattern = "SYSTem:CONFigure:ACT?", .callback = scpi_cmd_config_action_q}, \
    {.pattern = "SYSTem:CONFigure:CAL?", .callback = scpi_cmd_config_calibration_q}, \
    {.pattern = "SYSTem:CONFigure:ACK?", .callback = scpi_cmd_config_ack_q}, \
    {.pattern = "SYSTem:CONFigure:NACK?", .callback = scpi_cmd_config_nack_reason_q}, \
    {.pattern = "SYSTem:SCPI:RUN:ALLOW?", .callback = scpi_cmd_scpi_run_allow_q}, \
    {.pattern = "SYSTem:REFMEM:STATus?", .callback = scpi_cmd_refmem_status_q}, \
    {.pattern = "SYSTem:REFMEM:NODE?", .callback = scpi_cmd_refmem_node_q}, \
    {.pattern = "SYSTem:REFMEM:LOAD:SD", .callback = scpi_cmd_refmem_load_sd}, \
    {.pattern = "SYSTem:REFMEM:LOAD:NODE", .callback = scpi_cmd_refmem_load_node}, \
    {.pattern = "SYSTem:REFMEM:LOAD:STATus?", .callback = scpi_cmd_refmem_load_status_q}, \
    {.pattern = "SYSTem:CORE:VECTOR?", .callback = scpi_cmd_core_vector_q}, \
    {.pattern = "SYSTem:PROTection:STATus?", .callback = scpi_cmd_runtime_protection_q}, \
    {.pattern = "SYSTem:MODE:TABle?", .callback = scpi_cmd_system_mode_table_q}, \
    {.pattern = "SYSTem:RESource:TABle?", .callback = scpi_cmd_resource_arbiter_table_q}, \
    {.pattern = "SYSTem:FAULT:TABle?", .callback = scpi_cmd_fault_code_table_q}

#endif
