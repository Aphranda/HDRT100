#ifndef POTA_IMAGE_H
#define POTA_IMAGE_H

#include "pota_types.h"

typedef bool (*pota_flash_read_fn_t)(uint32_t offset, void *buffer, size_t size);

typedef struct {
    uint32_t sram_base;
    uint32_t sram_end;
    uint32_t xip_base;
    pota_flash_read_fn_t flash_read;
} pota_image_vector_constraints_t;

bool pota_image_validate_app_vector(uint32_t image_offset,
                                    uint32_t image_size,
                                    uint32_t run_offset,
                                    const pota_image_vector_constraints_t *constraints);

#endif
