#ifndef SCPI_TRIGGER_TEMPLATE_COMMANDS_H
#define SCPI_TRIGGER_TEMPLATE_COMMANDS_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_trigger_template_install(scpi_t *context);
scpi_result_t scpi_cmd_trigger_template_clear(scpi_t *context);
scpi_result_t scpi_cmd_trigger_template_activate(scpi_t *context);
scpi_result_t scpi_cmd_trigger_template_release(scpi_t *context);
scpi_result_t scpi_cmd_trigger_template_q(scpi_t *context);

#endif /* SCPI_TRIGGER_TEMPLATE_COMMANDS_H */
