#ifndef OTA_AO_PRIVATE_H
#define OTA_AO_PRIVATE_H

#include <stdint.h>

#include "ota_partition.h"
#include "ota_vector.h"

typedef struct ota_ao_context {
    ota_vector_t vector;
    ota_slot_t target_slot;
    uint32_t target_offset;
    uint32_t target_size;
    uint32_t erase_offset;
} ota_ao_context_t;

#endif
