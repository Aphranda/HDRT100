#ifndef SCPI_CALIBRATION_COMMANDS_INTERNAL_H
#define SCPI_CALIBRATION_COMMANDS_INTERNAL_H

#include "scpi_calibration_commands.h"

/* Shared readback helper: one definition lives in
 * scpi_calibration_commands.c; the capture/data unit reuses it. */
void scpi_calibration_result_u64(scpi_t *context, uint64_t value);

#endif /* SCPI_CALIBRATION_COMMANDS_INTERNAL_H */

