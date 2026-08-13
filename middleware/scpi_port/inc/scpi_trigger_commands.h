#ifndef SCPI_TRIGGER_COMMANDS_H
#define SCPI_TRIGGER_COMMANDS_H

#include "scpi/scpi.h"
#include "scpi_port_internal.h"

scpi_result_t scpi_trigger_state_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_mode(scpi_t *context);
scpi_result_t scpi_cmd_trigger_mode_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_start(scpi_t *context);
scpi_result_t scpi_cmd_trigger_stop(scpi_t *context);
scpi_result_t scpi_cmd_trigger_pause(scpi_t *context);
scpi_result_t scpi_cmd_trigger_continue(scpi_t *context);
scpi_result_t scpi_cmd_trigger_abort(scpi_t *context);
uint32_t scpi_trigger_product_mode(void);

#define SCPI_TRIGGER_COMMANDS \
    {.pattern = "TRIGger:MODE", .callback = scpi_cmd_trigger_mode}, \
    {.pattern = "TRIGger:MODE?", .callback = scpi_cmd_trigger_mode_q}, \
    {.pattern = "TRIGger:STARt", .callback = scpi_cmd_trigger_start}, \
    {.pattern = "TRIGger:STOP", .callback = scpi_cmd_trigger_stop}, \
    {.pattern = "TRIGger:PAUSe", .callback = scpi_cmd_trigger_pause}, \
    {.pattern = "TRIGger:CONTinue", .callback = scpi_cmd_trigger_continue}, \
    {.pattern = "TRIGger:ABORt", .callback = scpi_cmd_trigger_abort}, \
    {.pattern = "READ:TRIGger:STATe?", .callback = scpi_trigger_state_q}

#endif
