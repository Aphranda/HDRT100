#ifndef OTA_AO_PRIVATE_H
#define OTA_AO_PRIVATE_H

#include <stdint.h>
#include <stdbool.h>

#include "ota_partition.h"
#include "ota_metadata.h"
#include "ota_vector.h"

typedef struct ota_ao_context {
    ota_vector_t vector;
    ota_slot_t target_slot;
    uint32_t target_offset;
    uint32_t target_size;
    uint32_t target_run_offset;
    uint32_t erase_offset;
    bool package_mode;
    bool package_header_received;
    uint32_t package_received_size;
    uint32_t selected_image_offset;
    uint32_t selected_image_size;
    uint32_t selected_image_crc32;
    uint32_t selected_image_crc32_running;
    uint32_t selected_image_received_size;
    ota_metadata_t metadata_snapshot;
    bool metadata_snapshot_valid;
} ota_ao_context_t;

void ota_ao_publish_vector(const ota_ao_context_t *context);
void ota_ao_publish_metadata(ota_ao_context_t *context,
                             const ota_metadata_t *metadata);

#endif
