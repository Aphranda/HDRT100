#ifndef SCPI_PRODUCT_COMMANDS_H
#define SCPI_PRODUCT_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_product_result_accepted(scpi_t *context);
scpi_result_t scpi_product_run_last_q(scpi_t *context);
scpi_result_t scpi_product_run_summary_q(scpi_t *context);
scpi_result_t scpi_product_page_block_q(scpi_t *context);
scpi_result_t scpi_product_count_zero_q(scpi_t *context);
scpi_result_t scpi_product_trigger_parameter_q(scpi_t *context);
scpi_result_t scpi_product_angle_sweep_q(scpi_t *context);
scpi_result_t scpi_product_angle_pulse_q(scpi_t *context);
scpi_result_t scpi_product_angle_position_q(scpi_t *context);
scpi_result_t scpi_product_angle_breakpoint_q(scpi_t *context);
scpi_result_t scpi_product_sequence_q(scpi_t *context);
scpi_result_t scpi_product_sequence_map_q(scpi_t *context);
scpi_result_t scpi_product_sequence_check_q(scpi_t *context);
scpi_result_t scpi_product_sequence_active_q(scpi_t *context);
scpi_result_t scpi_product_trigger_state_q(scpi_t *context);
scpi_result_t scpi_product_switch_q(scpi_t *context);
scpi_result_t scpi_product_permission_q(scpi_t *context);
scpi_result_t scpi_product_role_q(scpi_t *context);

#define SCPI_PRODUCT_SYSTEM_LOG_COMMANDS \
    {.pattern = "SYSTem:RUN:LAST?", .callback = scpi_product_run_last_q}, \
    {.pattern = "SYSTem:RUN:SUMMary?", .callback = scpi_product_run_summary_q}, \
    {.pattern = "SYSTem:RUN:LOG?", .callback = scpi_product_page_block_q}, \
    {.pattern = "SYSTem:LOG:PAGE?", .callback = scpi_product_page_block_q}, \
    {.pattern = "SYSTem:TRACe:DATA?", .callback = scpi_product_page_block_q}, \
    {.pattern = "SYSTem:SNAPshot:DATA?", .callback = scpi_product_page_block_q}, \
    {.pattern = "SYSTem:T2:DATA?", .callback = scpi_product_page_block_q}, \
    {.pattern = "SYSTem:FAULT:CLEAr", .callback = scpi_product_result_accepted}

#define SCPI_PRODUCT_SYSTEM_PERMISSION_COMMANDS \
    {.pattern = "SYSTem:SCPI:PERMission?", .callback = scpi_product_permission_q}, \
    {.pattern = "SYSTem:SCPI:PERMission", .callback = scpi_product_result_accepted}, \
    {.pattern = "SYSTem:SCPI:ROLE?", .callback = scpi_product_role_q}, \
    {.pattern = "SYSTem:SCPI:ROLE", .callback = scpi_product_result_accepted}

#define SCPI_PRODUCT_BUSINESS_COMMANDS \
    {.pattern = "CONFigure:TRIGger", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:TRIGger:PARameter?", .callback = scpi_product_trigger_parameter_q}, \
    {.pattern = "CONFigure:ANGLe:SWEEp", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:ANGLe:SWEEp?", .callback = scpi_product_angle_sweep_q}, \
    {.pattern = "CONFigure:ANGLe:PULSe", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:ANGLe:PULSe?", .callback = scpi_product_angle_pulse_q}, \
    {.pattern = "READ:ANGLe:POSition?", .callback = scpi_product_angle_position_q}, \
    {.pattern = "CONFigure:ANGLe:BPOint", .callback = scpi_product_result_accepted}, \
    {.pattern = "CONFigure:ANGLe:BPOint:CLEAr", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:ANGLe:BPOint?", .callback = scpi_product_angle_breakpoint_q}, \
    {.pattern = "CONFigure:SEQuence", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:SEQuence?", .callback = scpi_product_sequence_q}, \
    {.pattern = "READ:SEQuence:MAP?", .callback = scpi_product_sequence_map_q}, \
    {.pattern = "READ:SEQuence:CHECk?", .callback = scpi_product_sequence_check_q}, \
    {.pattern = "CONFigure:SEQuence:ACTive", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:SEQuence:ACTive?", .callback = scpi_product_sequence_active_q}, \
    {.pattern = "CONFigure:SWITch#", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:SWITch#?", .callback = scpi_product_switch_q}, \
    {.pattern = "READ:RUN:SUMMary?", .callback = scpi_product_run_summary_q}, \
    {.pattern = "READ:T2:COUNt?", .callback = scpi_product_count_zero_q}, \
    {.pattern = "READ:T2:DATA?", .callback = scpi_product_page_block_q}

#define SCPI_PRODUCT_TRIGGER_COMMANDS \
    {.pattern = "TRIGger:STARt", .callback = scpi_product_result_accepted}, \
    {.pattern = "TRIGger:STOP", .callback = scpi_product_result_accepted}, \
    {.pattern = "TRIGger:PAUSe", .callback = scpi_product_result_accepted}, \
    {.pattern = "TRIGger:CONTinue", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:TRIGger:STATe?", .callback = scpi_product_trigger_state_q}

#endif
