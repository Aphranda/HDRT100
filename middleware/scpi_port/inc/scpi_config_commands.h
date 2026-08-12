#ifndef SCPI_CONFIG_COMMANDS_H
#define SCPI_CONFIG_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_product_result_accepted(scpi_t *context);
scpi_result_t scpi_product_trigger_parameter_q(scpi_t *context);
scpi_result_t scpi_product_angle_sweep_q(scpi_t *context);
scpi_result_t scpi_product_angle_pulse_q(scpi_t *context);
scpi_result_t scpi_product_angle_position_q(scpi_t *context);
scpi_result_t scpi_product_angle_breakpoint_q(scpi_t *context);
scpi_result_t scpi_product_sequence_q(scpi_t *context);
scpi_result_t scpi_product_sequence_map_q(scpi_t *context);
scpi_result_t scpi_product_sequence_check_q(scpi_t *context);
scpi_result_t scpi_product_sequence_active_q(scpi_t *context);
scpi_result_t scpi_product_switch_q(scpi_t *context);

#define SCPI_CONFIG_COMMANDS \
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
    {.pattern = "READ:SWITch#?", .callback = scpi_product_switch_q}

#endif
