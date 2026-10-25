#ifndef SCPI_TRIGGER_SWITCH_COMMANDS_H
#define SCPI_TRIGGER_SWITCH_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_trigger_switch_load(scpi_t *context);
scpi_result_t scpi_cmd_trigger_switch_stage(scpi_t *context);
scpi_result_t scpi_cmd_trigger_switch_apply(scpi_t *context);
scpi_result_t scpi_cmd_trigger_switch_stop(scpi_t *context);
scpi_result_t scpi_cmd_trigger_switch_unload(scpi_t *context);
scpi_result_t scpi_cmd_trigger_switch_q(scpi_t *context);

#endif /* SCPI_TRIGGER_SWITCH_COMMANDS_H */
