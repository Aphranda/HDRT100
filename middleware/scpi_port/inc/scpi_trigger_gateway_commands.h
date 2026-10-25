#ifndef SCPI_TRIGGER_GATEWAY_COMMANDS_H
#define SCPI_TRIGGER_GATEWAY_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_trigger_gateway_load(scpi_t *context);
scpi_result_t scpi_cmd_trigger_gateway_stop(scpi_t *context);
scpi_result_t scpi_cmd_trigger_gateway_unload(scpi_t *context);
scpi_result_t scpi_cmd_trigger_gateway_q(scpi_t *context);

#endif /* SCPI_TRIGGER_GATEWAY_COMMANDS_H */
