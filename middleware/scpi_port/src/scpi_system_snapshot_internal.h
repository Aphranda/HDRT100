#ifndef SCPI_SYSTEM_SNAPSHOT_INTERNAL_H
#define SCPI_SYSTEM_SNAPSHOT_INTERNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "scpi_system_snapshot_commands.h"
/* This header declares helpers whose signatures name RefMem snapshot types.
 * Include their definitions here so the header stands on its own: it used to
 * rely on the including .c having included these first, which broke as soon as
 * a single command family became its own translation unit. */
#include "refmem_application_model.h"
#include "refmem_pio_spi_adapter.h"
#include "refmem_sync.h"
#include "refmem_table_registry.h"

#define SCPI_REFMEM_PACKAGE_PATH "/refmem/app_model.rmtp"
#define SCPI_REFMEM_PACKAGE_READ_CHUNK 512u

bool scpi_vdc_run_u32(scpi_t *context, uint32_t *out);
bool scpi_vdc_run_end(scpi_t *context);

bool scpi_refmem_wait_storage_job(uint32_t job_id);
bool scpi_refmem_read_package(const char *path, uint8_t *buffer,
                              size_t buffer_size, size_t *returned_size);
bool scpi_refmem_model_mode_idle(void);
bool scpi_refmem_realtime_idle(void);
void scpi_refmem_result_load_snapshot(
    scpi_t *context, const refmem_application_model_load_snapshot_t *snapshot);
void scpi_refmem_result_board_load_snapshot(
    scpi_t *context, const refmem_board_capability_load_snapshot_t *snapshot);
void scpi_refmem_result_table_image_descriptor(
    scpi_t *context, const refmem_table_image_descriptor_t *descriptor);
uint32_t scpi_refmem_read_u32_le(const uint8_t *data);
bool scpi_refmem_sync_ensure_initialized(void);
bool scpi_refmem_sync_hex_decode(const char *hex, size_t hex_len,
                                 uint8_t *output, size_t output_size,
                                 size_t *decoded_size);
bool scpi_refmem_get_adapter_snapshot(refmem_pio_spi_adapter_snapshot_t *snapshot);
refmem_sync_context_t *scpi_refmem_sync_context(void);


#endif
