#ifndef POTA_MANIFEST_VERIFIER_H
#define POTA_MANIFEST_VERIFIER_H

#include "pota_package.h"

#define POTA_PUBLIC_KEY_P256_SIZE 65u
#define POTA_KEY_ROLE_DEV         (1u << 0)
#define POTA_KEY_ROLE_RELEASE     (1u << 1)
#define POTA_KEY_ROLE_FACTORY     (1u << 2)
#define POTA_KEY_ROLE_ALL         (POTA_KEY_ROLE_DEV | POTA_KEY_ROLE_RELEASE | \
                                   POTA_KEY_ROLE_FACTORY)
#define POTA_KEY_FLAG_REVOKED     (1u << 0)

typedef struct {
    uint32_t key_id;
    uint32_t role_mask;
    uint32_t flags;
    uint8_t public_key[POTA_PUBLIC_KEY_P256_SIZE];
} pota_public_key_entry_t;

typedef struct {
    const pota_public_key_entry_t *entries;
    uint32_t entry_count;
    uint32_t allowed_role_mask;
} pota_public_key_registry_t;

typedef bool (*pota_sha256_fn)(void *context,
                               const uint8_t *data,
                               uint32_t size,
                               uint8_t digest[POTA_SHA256_SIZE]);
typedef bool (*pota_p256_verify_fn)(
    void *context,
    const uint8_t public_key[POTA_PUBLIC_KEY_P256_SIZE],
    const uint8_t digest[POTA_SHA256_SIZE],
    const uint8_t signature[POTA_MANIFEST_SIGNATURE_MAX_SIZE]);

typedef struct {
    pota_public_key_registry_t registry;
    pota_sha256_fn sha256;
    pota_p256_verify_fn verify_p256;
    void *crypto_context;
} pota_manifest_verifier_t;

const pota_public_key_entry_t *pota_public_key_registry_find(
    const pota_public_key_registry_t *registry,
    uint32_t key_id);
bool pota_manifest_verifier_verify(void *context,
                                   const pota_package_manifest_t *manifest,
                                   const uint8_t *header,
                                   uint32_t header_size);

#endif
