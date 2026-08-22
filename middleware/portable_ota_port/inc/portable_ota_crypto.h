#ifndef PORTABLE_OTA_CRYPTO_H
#define PORTABLE_OTA_CRYPTO_H

#include "pota_package.h"

bool portable_ota_crypto_verify_manifest(
    void *context,
    const pota_package_manifest_t *manifest,
    const uint8_t *header,
    uint32_t header_size);

#endif
