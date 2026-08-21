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
scpi_result_t scpi_calibration_loopback_start(scpi_t *context);
scpi_result_t scpi_calibration_loopback_stop(scpi_t *context);
scpi_result_t scpi_calibration_loopback_q(scpi_t *context);
scpi_result_t scpi_calibration_clk_coded_start(scpi_t *context);
scpi_result_t scpi_calibration_clk_coded_stop(scpi_t *context);
scpi_result_t scpi_calibration_clk_coded_q(scpi_t *context);
scpi_result_t scpi_calibration_p3_start(scpi_t *context);
scpi_result_t scpi_calibration_p3_stop(scpi_t *context);
scpi_result_t scpi_calibration_p3_q(scpi_t *context);

#define SCPI_CALIBRATION_COMMANDS \
    {.pattern = "CONFigure:CALibration:LINK:ADD", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:CALibration:LINK:UPDate", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:CALibration:LINK:DELete", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:CALibration:LINK:CLEAr", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:CALibration:LINK?", .callback = scpi_calibration_link_q}, \
    {.pattern = "CALibration:STARt", .callback = scpi_calibration_result_q}, \
    {.pattern = "CALibration:LOOPback:STARt", .callback = scpi_calibration_loopback_start}, \
    {.pattern = "CALibration:LOOPback:STOP", .callback = scpi_calibration_loopback_stop}, \
    {.pattern = "READ:CALibration:LOOPback?", .callback = scpi_calibration_loopback_q}, \
    {.pattern = "CALibration:CLOCk:CODEd:STARt", .callback = scpi_calibration_clk_coded_start}, \
    {.pattern = "CALibration:CLOCk:CODEd:STOP", .callback = scpi_calibration_clk_coded_stop}, \
    {.pattern = "READ:CALibration:CLOCk:CODEd?", .callback = scpi_calibration_clk_coded_q}, \
    {.pattern = "CALibration:P3:STARt", .callback = scpi_calibration_p3_start}, \
    {.pattern = "CALibration:P3:STOP", .callback = scpi_calibration_p3_stop}, \
    {.pattern = "READ:CALibration:P3?", .callback = scpi_calibration_p3_q}, \
    {.pattern = "READ:CALibration:STATe?", .callback = scpi_calibration_result_q}, \
    {.pattern = "READ:CALibration:RESult?", .callback = scpi_calibration_result_q}, \
    {.pattern = "CONFigure:CALibration:PARameter:ADD", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:CALibration:PARameter:SET", .callback = scpi_port_result_accepted}, \
    {.pattern = "CONFigure:CALibration:PARameter:DELete", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:CALibration:PARameter?", .callback = scpi_calibration_parameter_q}, \
    {.pattern = "CALibration:SAVE", .callback = scpi_port_result_accepted}, \
    {.pattern = "CALibration:STOP", .callback = scpi_port_result_accepted}, \
    {.pattern = "CALibration:LOAD", .callback = scpi_port_result_accepted}, \
    {.pattern = "CALibration:ACTivate", .callback = scpi_port_result_accepted}, \
    {.pattern = "CALibration:ROLLback", .callback = scpi_port_result_accepted}, \
    {.pattern = "CALibration:CLEAr", .callback = scpi_port_result_accepted}, \
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
