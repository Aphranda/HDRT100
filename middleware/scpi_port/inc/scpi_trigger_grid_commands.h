#ifndef SCPI_TRIGGER_GRID_COMMANDS_H
#define SCPI_TRIGGER_GRID_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_trigger_grid_configure(scpi_t *context);
scpi_result_t scpi_cmd_trigger_grid_clear(scpi_t *context);
scpi_result_t scpi_cmd_trigger_grid_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_grid_plan_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_grid_anchor(scpi_t *context);
scpi_result_t scpi_cmd_trigger_grid_anchor_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_grid_anchor_plan_q(scpi_t *context);

/* Shared with sibling command modules (was a static helper). */
bool scpi_trigger_grid_uint(scpi_t *context, uint64_t maximum, uint64_t *out);

/* Shared with sibling command modules (was a static helper). */
bool scpi_trigger_grid_end(scpi_t *context);

/* Shared with sibling command modules (was a static helper). */
scpi_result_t scpi_trigger_grid_result(scpi_t *context, uint32_t result);

#endif /* SCPI_TRIGGER_GRID_COMMANDS_H */
