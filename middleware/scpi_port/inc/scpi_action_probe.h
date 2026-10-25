#ifndef SCPI_ACTION_PROBE_H
#define SCPI_ACTION_PROBE_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_action_probe_configure(scpi_t *context);
scpi_result_t scpi_cmd_action_probe_send(scpi_t *context);
scpi_result_t scpi_cmd_action_probe_clear(scpi_t *context);
scpi_result_t scpi_cmd_action_probe_q(scpi_t *context);

#endif /* SCPI_ACTION_PROBE_H */
