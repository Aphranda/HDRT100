#ifndef SCPI_PORT_INTERNAL_H
#define SCPI_PORT_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

bool scpi_port_read_u32(scpi_t *context, uint32_t *value);
scpi_result_t scpi_port_result_ok(scpi_t *context);
bool scpi_port_reject_if_run_forbidden(scpi_t *context, uint32_t class_id);

#endif
