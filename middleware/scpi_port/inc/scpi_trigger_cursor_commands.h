#ifndef SCPI_TRIGGER_CURSOR_COMMANDS_H
#define SCPI_TRIGGER_CURSOR_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_trigger_sim_source_configure(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sim_source_start(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sim_source_stop(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sim_source_release(scpi_t *context);
scpi_result_t scpi_cmd_trigger_sim_source_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_timing_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_cursor_q(scpi_t *context);

#endif /* SCPI_TRIGGER_CURSOR_COMMANDS_H */
