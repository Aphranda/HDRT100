#ifndef SCPI_SYSTEM_SNAPSHOT_INTERNAL_H
#define SCPI_SYSTEM_SNAPSHOT_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi_system_snapshot_commands.h"

bool scpi_vdc_run_u32(scpi_t *context, uint32_t *out);
bool scpi_vdc_run_end(scpi_t *context);

#endif
