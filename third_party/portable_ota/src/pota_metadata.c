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
