#include "pota_package.h"

#include <string.h>

#define POTA_PACKAGE_PRODUCT_ID_OFFSET        32u
#define POTA_PACKAGE_HARDWARE_ID_OFFSET       64u
#define POTA_PACKAGE_VERSION_MAJOR_OFFSET     96u
#define POTA_PACKAGE_VERSION_MINOR_OFFSET     100u
#define POTA_PACKAGE_VERSION_PATCH_OFFSET     104u
#define POTA_PACKAGE_MIN_BOOTLOADER_OFFSET    108u
#define POTA_PACKAGE_BUILD_ID_OFFSET          112u
#define POTA_PACKAGE_SHA256_OFFSET            144u
#define POTA_PACKAGE_CRC32_OFFSET             16u
#define POTA_MANIFEST_EXT_VERSION_OFFSET     (POTA_MANIFEST_EXTENSION_OFFSET + 4u)
#define POTA_MANIFEST_EXT_FLAGS_OFFSET       (POTA_MANIFEST_EXTENSION_OFFSET + 8u)
#define POTA_MANIFEST_EXT_COUNTER_OFFSET     (POTA_MANIFEST_EXTENSION_OFFSET + 12u)
#define POTA_MANIFEST_EXT_KEY_ID_OFFSET      (POTA_MANIFEST_EXTENSION_OFFSET + 16u)
#define POTA_MANIFEST_EXT_SIG_LENGTH_OFFSET  (POTA_MANIFEST_EXTENSION_OFFSET + 20u)
#define POTA_MANIFEST_EXT_SIG_OFFSET         (POTA_MANIFEST_EXTENSION_OFFSET + 24u)
#define POTA_MANIFEST_EXT_IMAGE_HASH_OFFSET  \
    (POTA_MANIFEST_EXT_SIG_OFFSET + POTA_MANIFEST_SIGNATURE_MAX_SIZE)

static uint32_t pota_read_le32(const uint8_t *data)
{
    return ((uint32_t)data[0]) |
           ((uint32_t)data[1] << 8u) |
           ((uint32_t)data[2] << 16u) |
           ((uint32_t)data[3] << 24u);
}

static void pota_copy_text(char *destination, const uint8_t *source)
{
    memcpy(destination, source, POTA_TEXT_FIELD_SIZE);
    destination[POTA_TEXT_FIELD_SIZE - 1u] = '\0';
}

static bool pota_text_matches(const char *field, const char *expected)
{
    if (expected == NULL) {
        return true;
    }
    return strncmp(field, expected, POTA_TEXT_FIELD_SIZE) == 0;
}

bool pota_package_build_signing_transcript(
    const uint8_t *header,
    uint32_t header_size,
    uint8_t transcript[POTA_MANIFEST_SIGNING_TRANSCRIPT_SIZE])
{
    if (header == NULL || transcript == NULL ||
        header_size < POTA_PACKAGE_HEADER_SIZE) {
        return false;
    }

    memcpy(transcript, header, POTA_MANIFEST_SIGNING_TRANSCRIPT_SIZE);
    memset(&transcript[POTA_PACKAGE_CRC32_OFFSET], 0, sizeof(uint32_t));
    memset(&transcript[POTA_MANIFEST_EXT_SIG_OFFSET], 0,
           POTA_MANIFEST_SIGNATURE_MAX_SIZE);
    return true;
}

pota_error_t pota_package_parse_header(const uint8_t *data,
                                       uint32_t length,
                                       const pota_package_constraints_t *constraints,
                                       pota_package_manifest_t *manifest)
{
    if (data == NULL || manifest == NULL || length < POTA_PACKAGE_HEADER_SIZE) {
        return POTA_ERR_BAD_ARGUMENT;
    }

    manifest->magic = pota_read_le32(&data[0]);
    manifest->version = pota_read_le32(&data[4]);
    manifest->header_size = pota_read_le32(&data[8]);
    manifest->package_size = pota_read_le32(&data[12]);
    manifest->package_crc32 = pota_read_le32(&data[16]);
    manifest->image_count = pota_read_le32(&data[20]);

    if (manifest->magic != POTA_PACKAGE_MAGIC ||
        manifest->version != POTA_PACKAGE_VERSION ||
        manifest->header_size != POTA_PACKAGE_HEADER_SIZE ||
        manifest->image_count == 0u ||
        manifest->image_count > POTA_PACKAGE_MAX_IMAGES ||
        manifest->package_size <= POTA_PACKAGE_HEADER_SIZE) {
        return POTA_ERR_BAD_HEADER;
    }

    pota_copy_text(manifest->product_id, &data[POTA_PACKAGE_PRODUCT_ID_OFFSET]);
    pota_copy_text(manifest->hardware_id, &data[POTA_PACKAGE_HARDWARE_ID_OFFSET]);
    manifest->app_version_major = pota_read_le32(&data[POTA_PACKAGE_VERSION_MAJOR_OFFSET]);
    manifest->app_version_minor = pota_read_le32(&data[POTA_PACKAGE_VERSION_MINOR_OFFSET]);
    manifest->app_version_patch = pota_read_le32(&data[POTA_PACKAGE_VERSION_PATCH_OFFSET]);
    manifest->min_bootloader_version = pota_read_le32(&data[POTA_PACKAGE_MIN_BOOTLOADER_OFFSET]);
    pota_copy_text(manifest->build_id, &data[POTA_PACKAGE_BUILD_ID_OFFSET]);
    memcpy(manifest->payload_sha256,
           &data[POTA_PACKAGE_SHA256_OFFSET],
           sizeof(manifest->payload_sha256));

    memset(manifest->signature, 0, sizeof(manifest->signature));
    manifest->extension_version = 0u;
    manifest->required_flags = 0u;
    manifest->security_counter = 0u;
    manifest->key_id = 0u;
    manifest->signature_length = 0u;
    const uint32_t extension_magic =
        pota_read_le32(&data[POTA_MANIFEST_EXTENSION_OFFSET]);
    if (extension_magic != 0u && extension_magic != 0xFFFFFFFFu) {
        if (extension_magic != POTA_MANIFEST_EXTENSION_MAGIC) {
            return POTA_ERR_BAD_HEADER;
        }
        manifest->extension_version =
            pota_read_le32(&data[POTA_MANIFEST_EXT_VERSION_OFFSET]);
        manifest->required_flags =
            pota_read_le32(&data[POTA_MANIFEST_EXT_FLAGS_OFFSET]);
        manifest->security_counter =
            pota_read_le32(&data[POTA_MANIFEST_EXT_COUNTER_OFFSET]);
        manifest->key_id =
            pota_read_le32(&data[POTA_MANIFEST_EXT_KEY_ID_OFFSET]);
        manifest->signature_length =
            pota_read_le32(&data[POTA_MANIFEST_EXT_SIG_LENGTH_OFFSET]);
        if ((manifest->extension_version !=
                 POTA_MANIFEST_EXTENSION_VERSION &&
             manifest->extension_version !=
                 POTA_MANIFEST_EXTENSION_VERSION_SLOT_HASHES) ||
            (manifest->required_flags &
             ~(POTA_MANIFEST_REQUIRED_SIGNATURE |
               POTA_MANIFEST_REQUIRED_IMAGE_HASHES)) != 0u ||
            (manifest->extension_version ==
                 POTA_MANIFEST_EXTENSION_VERSION &&
             (manifest->required_flags &
              POTA_MANIFEST_REQUIRED_IMAGE_HASHES) != 0u) ||
            (manifest->signature_length != 0u &&
             manifest->signature_length != POTA_MANIFEST_SIGNATURE_MAX_SIZE)) {
            return POTA_ERR_BAD_HEADER;
        }
        memcpy(manifest->signature, &data[POTA_MANIFEST_EXT_SIG_OFFSET],
               manifest->signature_length);
    }

    if (((manifest->required_flags & POTA_MANIFEST_REQUIRED_SIGNATURE) != 0u ||
         manifest->security_counter != 0u || manifest->key_id != 0u) &&
        manifest->signature_length != POTA_MANIFEST_SIGNATURE_MAX_SIZE) {
        return POTA_ERR_SIGNATURE_INVALID;
    }
    if (manifest->signature_length != 0u && manifest->key_id == 0u) {
        return POTA_ERR_SIGNATURE_INVALID;
    }
    if (constraints != NULL) {
        if (!pota_text_matches(manifest->product_id, constraints->product_id)) {
            return POTA_ERR_PRODUCT_MISMATCH;
        }
        if (!pota_text_matches(manifest->hardware_id, constraints->hardware_id)) {
            return POTA_ERR_HARDWARE_MISMATCH;
        }
        if (manifest->min_bootloader_version > constraints->bootloader_version) {
            return POTA_ERR_BOOTLOADER_TOO_OLD;
        }
        if (manifest->security_counter < constraints->minimum_security_counter) {
            return POTA_ERR_SECURITY_COUNTER_ROLLBACK;
        }
        if ((constraints->require_signature ||
             (manifest->required_flags & POTA_MANIFEST_REQUIRED_SIGNATURE) != 0u) &&
            manifest->signature_length == 0u) {
            return POTA_ERR_SIGNATURE_INVALID;
        }
        if (constraints->require_image_hashes &&
            (manifest->extension_version !=
                 POTA_MANIFEST_EXTENSION_VERSION_SLOT_HASHES ||
             (manifest->required_flags &
              POTA_MANIFEST_REQUIRED_IMAGE_HASHES) == 0u)) {
            return POTA_ERR_BAD_HEADER;
        }
    }
    uint32_t cursor = POTA_IMAGE_TABLE_OFFSET;
    for (uint32_t i = 0u; i < POTA_PACKAGE_MAX_IMAGES; i++) {
        pota_package_image_t *image = &manifest->images[i];
        image->slot = pota_read_le32(&data[cursor + 0u]);
        image->offset = pota_read_le32(&data[cursor + 4u]);
        image->size = pota_read_le32(&data[cursor + 8u]);
        image->crc32 = pota_read_le32(&data[cursor + 12u]);
        image->run_offset = pota_read_le32(&data[cursor + 16u]);
        image->flags = pota_read_le32(&data[cursor + 20u]);
        memset(image->sha256, 0, sizeof(image->sha256));
        if (manifest->extension_version ==
            POTA_MANIFEST_EXTENSION_VERSION_SLOT_HASHES) {
            memcpy(image->sha256,
                   &data[POTA_MANIFEST_EXT_IMAGE_HASH_OFFSET +
                         i * POTA_SHA256_SIZE],
                   sizeof(image->sha256));
        }
        cursor += POTA_IMAGE_ENTRY_SIZE;
    }

    for (uint32_t i = 0u; i < manifest->image_count; i++) {
        const pota_package_image_t *image = &manifest->images[i];
        if ((image->slot != (uint32_t)POTA_SLOT_A &&
             image->slot != (uint32_t)POTA_SLOT_B) ||
            image->size == 0u ||
            image->offset < POTA_PACKAGE_HEADER_SIZE ||
            image->offset > manifest->package_size ||
            image->size > (manifest->package_size - image->offset)) {
            return POTA_ERR_BAD_HEADER;
        }
        /* A package image is selected by slot.  Duplicate slot entries would
         * make selection order-dependent and could cause the verifier to
         * approve one image while the installer consumes another. */
        for (uint32_t previous = 0u; previous < i; previous++) {
            if (manifest->images[previous].slot == image->slot) {
                return POTA_ERR_BAD_HEADER;
            }
        }
        if ((manifest->required_flags &
             POTA_MANIFEST_REQUIRED_IMAGE_HASHES) != 0u) {
            uint8_t hash_or = 0u;
            for (uint32_t byte = 0u; byte < POTA_SHA256_SIZE; byte++) {
                hash_or |= image->sha256[byte];
            }
            if (hash_or == 0u) {
                return POTA_ERR_BAD_HEADER;
            }
        }
    }

    /* The verifier must observe a fully parsed and range-checked manifest.
     * A signed extension is never accepted without an explicit verifier. */
    if (manifest->signature_length != 0u &&
        (constraints == NULL || constraints->verify_signature == NULL ||
         !constraints->verify_signature(constraints->verify_context,
                                        manifest, data, length))) {
        return POTA_ERR_SIGNATURE_INVALID;
    }

    return POTA_ERR_NONE;
}

const pota_package_image_t *pota_package_find_image(const pota_package_manifest_t *manifest,
                                                    pota_slot_t slot)
{
    uint32_t index = 0u;
    if (!pota_package_find_image_index(manifest, slot, &index)) {
        return NULL;
    }

    return &manifest->images[index];
}

bool pota_package_find_image_index(const pota_package_manifest_t *manifest,
                                   pota_slot_t slot,
                                   uint32_t *index)
{
    if (manifest == NULL) {
        return false;
    }

    for (uint32_t i = 0u; i < manifest->image_count; i++) {
        if (manifest->images[i].slot == (uint32_t)slot) {
            if (index != NULL) {
                *index = i;
            }
            return true;
        }
    }

    return false;
}
