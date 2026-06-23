#include "pota_metadata.h"

uint32_t pota_metadata_crc32(const pota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return 0u;
    }

    pota_metadata_t copy = *metadata;
    copy.metadata_crc32 = 0u;
    return pota_crc32_compute(&copy, sizeof(copy));
}

bool pota_metadata_is_valid(const pota_metadata_t *metadata)
{
    if (metadata == NULL) {
        return false;
    }

    if (metadata->magic != POTA_METADATA_MAGIC ||
        metadata->version != POTA_METADATA_VERSION) {
        return false;
    }

    return pota_metadata_crc32(metadata) == metadata->metadata_crc32;
}

const pota_metadata_t *pota_metadata_select_newest(const pota_metadata_t *copies,
                                                   size_t copy_count)
{
    if (copies == NULL || copy_count == 0u) {
        return NULL;
    }

    const pota_metadata_t *selected = NULL;
    for (size_t i = 0u; i < copy_count; i++) {
        const pota_metadata_t *candidate = &copies[i];
        if (!pota_metadata_is_valid(candidate)) {
            continue;
        }
        if (selected == NULL || candidate->sequence > selected->sequence) {
            selected = candidate;
        }
    }

    return selected;
}
