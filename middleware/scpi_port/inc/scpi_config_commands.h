#ifndef SCPI_CONFIG_COMMANDS_H
#define SCPI_CONFIG_COMMANDS_H

#include "scpi/scpi.h"
#include "scpi_port_internal.h"
#include "trigger_sequence_config.h"

/* Complete parsing before mutating configuration or posting runtime commands. */
bool scpi_sequence_param_u32(scpi_t *context, uint32_t *value);
bool scpi_sequence_params_end(scpi_t *context);
bool scpi_sequence_param_plan(scpi_t *context,
    char id[TRIGGER_SEQUENCE_PLAN_ID_MAX + 1u], bool required, bool *present);

scpi_result_t scpi_config_trigger_parameter(scpi_t *context);
scpi_result_t scpi_config_trigger_parameter_q(scpi_t *context);
scpi_result_t scpi_config_angle_sweep_q(scpi_t *context);
scpi_result_t scpi_config_angle_pulse_q(scpi_t *context);
scpi_result_t scpi_config_angle_position_q(scpi_t *context);
scpi_result_t scpi_config_angle_breakpoint_q(scpi_t *context);
scpi_result_t scpi_config_sequence(scpi_t *context);
scpi_result_t scpi_config_sequence_q(scpi_t *context);
scpi_result_t scpi_config_sequence_map_q(scpi_t *context);
scpi_result_t scpi_config_sequence_check_q(scpi_t *context);
scpi_result_t scpi_config_sequence_active(scpi_t *context);
scpi_result_t scpi_config_sequence_active_q(scpi_t *context);
scpi_result_t scpi_config_switch_q(scpi_t *context);

#define SCPI_CONFIG_COMMANDS \
    {.pattern = "CONFigure:TRIGger", .callback = scpi_config_trigger_parameter}, \
    {.pattern = "READ:TRIGger:PARameter?", .callback = scpi_config_trigger_parameter_q}, \
    {.pattern = "CONFigure:ANGLe:SWEEp", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:ANGLe:SWEEp?", .callback = scpi_config_angle_sweep_q}, \
    {.pattern = "CONFigure:ANGLe:PULSe", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:ANGLe:PULSe?", .callback = scpi_config_angle_pulse_q}, \
    {.pattern = "READ:ANGLe:POSition?", .callback = scpi_config_angle_position_q}, \
    {.pattern = "CONFigure:ANGLe:BREAkpoint", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:ANGLe:BREAkpoint:CLEAr", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:ANGLe:BREAkpoint?", .callback = scpi_config_angle_breakpoint_q}, \
    {.pattern = "CONFigure:SEQuence", .callback = scpi_config_sequence}, \
    {.pattern = "READ:SEQuence?", .callback = scpi_config_sequence_q}, \
    {.pattern = "READ:SEQuence:MAP?", .callback = scpi_config_sequence_map_q}, \
    {.pattern = "READ:SEQuence:CHECk?", .callback = scpi_config_sequence_check_q}, \
    {.pattern = "CONFigure:SEQuence:ACTive", .callback = scpi_config_sequence_active}, \
    {.pattern = "READ:SEQuence:ACTive?", .callback = scpi_config_sequence_active_q}, \
    {.pattern = "CONFigure:SWITch#", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:SWITch#?", .callback = scpi_config_switch_q}

#endif
