#ifndef SCPI_VDC_OUTPUT_SLICES_H
#define SCPI_VDC_OUTPUT_SLICES_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_vdc_subdivision_after_sync_run_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_subdivision_after_sync_q(scpi_t *context);
scpi_result_t scpi_cmd_vdc_subdivision_after_sync(scpi_t *context);
scpi_result_t scpi_cmd_vdc_subdivision_after_sync_configure(scpi_t *context);

#endif /* SCPI_VDC_OUTPUT_SLICES_H */
