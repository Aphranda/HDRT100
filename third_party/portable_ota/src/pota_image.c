#include "pota_image.h"

bool pota_image_validate_app_vector(uint32_t image_offset,
                                    uint32_t image_size,
                                    uint32_t run_offset,
                                    const pota_image_vector_constraints_t *constraints)
{
    if (image_size < 8u ||
        constraints == NULL ||
        constraints->flash_read == NULL ||
        constraints->sram_base > constraints->sram_end) {
        return false;
    }

    uint32_t vector[2];
    if (!constraints->flash_read(image_offset, vector, sizeof(vector))) {
        return false;
    }

    const uint32_t initial_sp = vector[0];
    const uint32_t reset_handler = vector[1];
    const uint32_t app_xip_base = constraints->xip_base + run_offset;
    const uint32_t app_xip_end = app_xip_base + image_size;

    if (initial_sp < constraints->sram_base || initial_sp > constraints->sram_end) {
        return false;
    }

    if ((reset_handler & 1u) == 0u) {
        return false;
    }

    const uint32_t reset_addr = reset_handler & ~1u;
    return reset_addr >= app_xip_base && reset_addr < app_xip_end;
}
