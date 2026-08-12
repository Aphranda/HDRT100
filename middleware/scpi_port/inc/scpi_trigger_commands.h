#ifndef SCPI_TRIGGER_COMMANDS_H
#define SCPI_TRIGGER_COMMANDS_H

#include "scpi/scpi.h"
#include "scpi_port_internal.h"

scpi_result_t scpi_trigger_state_q(scpi_t *context);

#define SCPI_TRIGGER_COMMANDS \
    {.pattern = "TRIGger:STARt", .callback = scpi_port_result_accepted}, \
    {.pattern = "TRIGger:STOP", .callback = scpi_port_result_accepted}, \
    {.pattern = "TRIGger:PAUSe", .callback = scpi_port_result_accepted}, \
    {.pattern = "TRIGger:CONTinue", .callback = scpi_port_result_accepted}, \
    {.pattern = "READ:TRIGger:STATe?", .callback = scpi_trigger_state_q}

#endif
