#ifndef POTA_PACKAGE_H
#define POTA_PACKAGE_H

#include "pota_types.h"

typedef struct {
    uint32_t slot;
    uint32_t offset;
    uint32_t size;
    uint32_t crc32;
    uint32_t run_offset;
    uint32_t flags;
} pota_package_image_t;

typedef struct pota_package_manifest pota_package_manifest_t;

typedef bool (*pota_package_signature_verify_fn)(
    void *context,
    const pota_package_manifest_t *manifest,
    const uint8_t *header,
    uint32_t header_size);

struct pota_package_manifest {
    uint32_t magic;
    uint32_t version;
    uint32_t header_size;
    uint32_t package_size;
    uint32_t package_crc32;
    uint32_t image_count;
    char product_id[POTA_TEXT_FIELD_SIZE];
    char hardware_id[POTA_TEXT_FIELD_SIZE];
    uint32_t app_version_major;
    uint32_t app_version_minor;
    uint32_t app_version_patch;
    uint32_t min_bootloader_version;
    char build_id[POTA_TEXT_FIELD_SIZE];
    uint8_t payload_sha256[POTA_SHA256_SIZE];
    pota_package_image_t images[POTA_PACKAGE_MAX_IMAGES];
    uint32_t extension_version;
    uint32_t required_flags;
    uint32_t security_counter;
    uint32_t key_id;
    uint32_t signature_length;
    uint8_t signature[POTA_MANIFEST_SIGNATURE_MAX_SIZE];
};

typedef struct {
    const char *product_id;
    const char *hardware_id;
    uint32_t bootloader_version;
    uint32_t minimum_security_counter;
    bool require_signature;
    pota_package_signature_verify_fn verify_signature;
    void *verify_context;
} pota_package_constraints_t;

pota_error_t pota_package_parse_header(const uint8_t *data,
                                       uint32_t length,
                                       const pota_package_constraints_t *constraints,
                                       pota_package_manifest_t *manifest);
const pota_package_image_t *pota_package_find_image(const pota_package_manifest_t *manifest,
                                                    pota_slot_t slot);
bool pota_package_find_image_index(const pota_package_manifest_t *manifest,
                                   pota_slot_t slot,
                                   uint32_t *index);

#endif
