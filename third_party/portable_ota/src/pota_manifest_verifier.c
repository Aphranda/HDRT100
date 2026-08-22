#include "pota_manifest_verifier.h"

#include <string.h>

const pota_public_key_entry_t *pota_public_key_registry_find(
    const pota_public_key_registry_t *registry,
    uint32_t key_id)
{
    if (registry == NULL || registry->entries == NULL || key_id == 0u ||
        registry->allowed_role_mask == 0u) {
        return NULL;
    }

    const pota_public_key_entry_t *match = NULL;
    for (uint32_t index = 0u; index < registry->entry_count; index++) {
        const pota_public_key_entry_t *entry = &registry->entries[index];
        if (entry->key_id != key_id) {
            continue;
        }
        if (match != NULL || entry->public_key[0] != 0x04u ||
            (entry->flags & POTA_KEY_FLAG_REVOKED) != 0u ||
            (entry->role_mask & registry->allowed_role_mask) == 0u) {
            return NULL;
        }
        match = entry;
    }
    return match;
}

bool pota_manifest_verifier_verify(void *context,
                                   const pota_package_manifest_t *manifest,
                                   const uint8_t *header,
                                   uint32_t header_size)
{
    pota_manifest_verifier_t *verifier = context;
    if (verifier == NULL || manifest == NULL || header == NULL ||
        manifest->signature_length != POTA_MANIFEST_SIGNATURE_MAX_SIZE ||
        verifier->sha256 == NULL || verifier->verify_p256 == NULL) {
        return false;
    }

    const pota_public_key_entry_t *key =
        pota_public_key_registry_find(&verifier->registry, manifest->key_id);
    if (key == NULL) {
        return false;
    }

    uint8_t transcript[POTA_MANIFEST_SIGNING_TRANSCRIPT_SIZE];
    uint8_t digest[POTA_SHA256_SIZE];
    if (!pota_package_build_signing_transcript(header, header_size, transcript) ||
        !verifier->sha256(verifier->crypto_context, transcript,
                          sizeof(transcript), digest)) {
        return false;
    }

    const bool verified = verifier->verify_p256(
        verifier->crypto_context, key->public_key, digest, manifest->signature);
    memset(digest, 0, sizeof(digest));
    memset(transcript, 0, sizeof(transcript));
    return verified;
}
