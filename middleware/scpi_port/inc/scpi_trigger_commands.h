#ifndef SCPI_TRIGGER_COMMANDS_H
#define SCPI_TRIGGER_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_product_result_accepted(scpi_t *context);
scpi_result_t scpi_product_trigger_state_q(scpi_t *context);

#define SCPI_TRIGGER_COMMANDS \
    {.pattern = "TRIGger:STARt", .callback = scpi_product_result_accepted}, \
    {.pattern = "TRIGger:STOP", .callback = scpi_product_result_accepted}, \
    {.pattern = "TRIGger:PAUSe", .callback = scpi_product_result_accepted}, \
    {.pattern = "TRIGger:CONTinue", .callback = scpi_product_result_accepted}, \
    {.pattern = "READ:TRIGger:STATe?", .callback = scpi_product_trigger_state_q}

#endif
