#ifndef SCPI_TRIGGER_COUNTER_COMMANDS_H
#define SCPI_TRIGGER_COUNTER_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_trigger_counter_load(scpi_t *context);
scpi_result_t scpi_cmd_trigger_counter_start(scpi_t *context);
scpi_result_t scpi_cmd_trigger_counter_stop(scpi_t *context);
scpi_result_t scpi_cmd_trigger_counter_clear(scpi_t *context);
scpi_result_t scpi_cmd_trigger_counter_unload(scpi_t *context);
scpi_result_t scpi_cmd_trigger_counter_q(scpi_t *context);

#endif /* SCPI_TRIGGER_COUNTER_COMMANDS_H */
