#ifndef SCPI_PORT_INTERNAL_H
#define SCPI_PORT_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

bool scpi_port_read_u32(scpi_t *context, uint32_t *value);
scpi_result_t scpi_port_result_ok(scpi_t *context);
scpi_result_t scpi_port_result_accepted(scpi_t *context);
void scpi_port_push_exec_error(scpi_t *context, const char *info);
bool scpi_port_reject_if_run_forbidden(scpi_t *context, uint32_t class_id);
void scpi_port_get_trigger_debug_snapshot(uint32_t *stage,
                                          uint32_t *mode,
                                          uint32_t *posted);

#endif
