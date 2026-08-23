#ifndef PORTABLE_OTA_CRYPTO_H
#define PORTABLE_OTA_CRYPTO_H

#include "pota_package.h"

typedef bool (*portable_ota_flash_read_fn)(void *context,
                                           uint32_t offset,
                                           void *data,
                                           uint32_t size);

bool portable_ota_crypto_sha256(
    const uint8_t *data,
    uint32_t size,
    uint8_t digest[POTA_SHA256_SIZE]);
bool portable_ota_crypto_sha256_flash(
    portable_ota_flash_read_fn read,
    void *context,
    uint32_t offset,
    uint32_t size,
    uint8_t digest[POTA_SHA256_SIZE]);

bool portable_ota_crypto_verify_manifest(
    void *context,
    const pota_package_manifest_t *manifest,
    const uint8_t *header,
    uint32_t header_size);

#endif
