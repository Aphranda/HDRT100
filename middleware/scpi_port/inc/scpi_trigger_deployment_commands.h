#ifndef SCPI_TRIGGER_DEPLOYMENT_COMMANDS_H
#define SCPI_TRIGGER_DEPLOYMENT_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_trigger_deployment_stage(scpi_t *context);
scpi_result_t scpi_cmd_trigger_deployment_activate(scpi_t *context);
scpi_result_t scpi_cmd_trigger_deployment_clear(scpi_t *context);
scpi_result_t scpi_cmd_trigger_deployment_q(scpi_t *context);
scpi_result_t scpi_cmd_trigger_deployment_plan_q(scpi_t *context);

/* Shared with sibling command modules (was a static helper). */
bool scpi_trigger_deployment_u32(scpi_t *context, uint32_t *out);

#endif /* SCPI_TRIGGER_DEPLOYMENT_COMMANDS_H */
