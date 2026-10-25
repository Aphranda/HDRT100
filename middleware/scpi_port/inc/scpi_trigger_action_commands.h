#ifndef SCPI_TRIGGER_ACTION_COMMANDS_H
#define SCPI_TRIGGER_ACTION_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_trigger_action_install(scpi_t *context);
scpi_result_t scpi_cmd_trigger_action_clear(scpi_t *context);
scpi_result_t scpi_cmd_trigger_action_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_action_row_q(scpi_t *context);

#endif /* SCPI_TRIGGER_ACTION_COMMANDS_H */
