#ifndef SCPI_CALIBRATION_COMMANDS_H
#define SCPI_CALIBRATION_COMMANDS_H

#include "scpi/scpi.h"
#include "scpi_port_internal.h"

scpi_result_t scpi_calibration_link_q(scpi_t *context);
scpi_result_t scpi_calibration_parameter_q(scpi_t *context);
scpi_result_t scpi_calibration_result_q(scpi_t *context);
scpi_result_t scpi_calibration_list_q(scpi_t *context);
scpi_result_t scpi_calibration_active_q(scpi_t *context);
scpi_result_t scpi_calibration_meta_q(scpi_t *context);
scpi_result_t scpi_calibration_health_q(scpi_t *context);
scpi_result_t scpi_calibration_limit_q(scpi_t *context);

#define SCPI_CALIBRATION_COMMANDS \
    {.pattern = "CONFigure:CALibration:LINK:ADD", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:CALibration:LINK:SET", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:CALibration:LINK:DELete", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:CALibration:LINK?", .callback = scpi_calibration_link_q}, \
    {.pattern = "CALibration:STARt", .callback = scpi_calibration_result_q}, \
    {.pattern = "READ:CALibration:STATe?", .callback = scpi_calibration_result_q}, \
    {.pattern = "READ:CALibration:RESult?", .callback = scpi_calibration_result_q}, \
    {.pattern = "CONFigure:CALibration:PARameter:ADD", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:CALibration:PARameter:SET", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:CALibration:PARameter:DELete", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:CALibration:PARameter?", .callback = scpi_calibration_parameter_q}, \
    {.pattern = "CALibration:SAVE", .callback = scpi_port_result_accepted}, \
    {.pattern = "CALibration:LOAD", .callback = scpi_port_result_accepted}, \
    {.pattern = "CALibration:ACTivate", .callback = scpi_port_result_accepted}, \
    {.pattern = "CALibration:ROLLback", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:CALibration:LIST?", .callback = scpi_calibration_list_q}, \
    {.pattern = "READ:CALibration:ACTive?", .callback = scpi_calibration_active_q}, \
    {.pattern = "CONFigure:CALibration:META", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:CALibration:META?", .callback = scpi_calibration_meta_q}, \
    {.pattern = "CONFigure:CALibration:LIMit", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:CALibration:HEALth?", .callback = scpi_calibration_health_q}, \
    {.pattern = "SYSTem:CALibration:LIMit:OVERRide", .callback = scpi_port_result_accepted}, \
    {.pattern = "SYSTem:CALibration:LIMit:OVERRide?", .callback = scpi_calibration_limit_q}, \
    {.pattern = "SYSTem:CALibration:LIMit:DEFAult", .callback = scpi_port_result_accepted}

#endif
