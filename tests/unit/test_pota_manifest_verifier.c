#include "pota_manifest_verifier.h"

#include <stdio.h>
#include <string.h>

typedef struct {
    uint32_t hash_calls;
    uint32_t verify_calls;
    bool hash_result;
    bool verify_result;
} mock_crypto_t;

static bool mock_sha256(void *context, const uint8_t *data, uint32_t size,
                        uint8_t digest[POTA_SHA256_SIZE])
{
    mock_crypto_t *mock = context;
    if (mock == NULL || data == NULL || size != POTA_PACKAGE_HEADER_SIZE ||
        digest == NULL) {
        return false;
    }
    mock->hash_calls++;
    memset(digest, 0xA5, POTA_SHA256_SIZE);
    return mock->hash_result;
}

static bool mock_verify(void *context,
                        const uint8_t public_key[POTA_PUBLIC_KEY_P256_SIZE],
                        const uint8_t digest[POTA_SHA256_SIZE],
                        const uint8_t signature[POTA_MANIFEST_SIGNATURE_MAX_SIZE])
{
    mock_crypto_t *mock = context;
    if (mock == NULL || public_key == NULL || digest == NULL ||
        signature == NULL || public_key[0] != 0x04u || digest[0] != 0xA5u) {
        return false;
    }
    mock->verify_calls++;
    return mock->verify_result;
}

static int expect(const char *name, bool condition)
{
    if (!condition) {
        (void)printf("%s failed\n", name);
        return 1;
    }
    return 0;
}

static pota_package_manifest_t make_manifest(uint32_t key_id)
{
    pota_package_manifest_t manifest;
    memset(&manifest, 0, sizeof(manifest));
    manifest.key_id = key_id;
    manifest.signature_length = POTA_MANIFEST_SIGNATURE_MAX_SIZE;
    memset(manifest.signature, 0x5A, sizeof(manifest.signature));
    return manifest;
}

int main(void)
{
    pota_public_key_entry_t keys[2];
    memset(keys, 0, sizeof(keys));
    keys[0].key_id = 7u;
    keys[0].role_mask = POTA_KEY_ROLE_RELEASE;
    keys[0].public_key[0] = 0x04u;
    keys[1].key_id = 9u;
    keys[1].role_mask = POTA_KEY_ROLE_FACTORY;
    keys[1].public_key[0] = 0x04u;

    mock_crypto_t mock = {.hash_result = true, .verify_result = true};
    pota_manifest_verifier_t verifier = {
        .registry = {
            .entries = keys,
            .entry_count = 2u,
            .allowed_role_mask = POTA_KEY_ROLE_RELEASE,
        },
        .sha256 = mock_sha256,
        .verify_p256 = mock_verify,
        .crypto_context = &mock,
    };
    uint8_t header[POTA_PACKAGE_HEADER_SIZE];
    memset(header, 0xFF, sizeof(header));
    pota_package_manifest_t manifest = make_manifest(7u);
    int failed = 0;

    failed += expect("valid", pota_manifest_verifier_verify(
        &verifier, &manifest, header, sizeof(header)) &&
        mock.hash_calls == 1u && mock.verify_calls == 1u);

    manifest.key_id = 99u;
    failed += expect("unknown key", !pota_manifest_verifier_verify(
        &verifier, &manifest, header, sizeof(header)) &&
        mock.hash_calls == 1u && mock.verify_calls == 1u);

    manifest.key_id = 9u;
    failed += expect("role denied", !pota_manifest_verifier_verify(
        &verifier, &manifest, header, sizeof(header)) &&
        mock.hash_calls == 1u && mock.verify_calls == 1u);

    manifest.key_id = 7u;
    keys[0].flags = POTA_KEY_FLAG_REVOKED;
    failed += expect("revoked", !pota_manifest_verifier_verify(
        &verifier, &manifest, header, sizeof(header)) &&
        mock.hash_calls == 1u && mock.verify_calls == 1u);
    keys[0].flags = 0u;

    mock.hash_result = false;
    failed += expect("hash failure", !pota_manifest_verifier_verify(
        &verifier, &manifest, header, sizeof(header)) &&
        mock.hash_calls == 2u && mock.verify_calls == 1u);
    mock.hash_result = true;

    mock.verify_result = false;
    failed += expect("bad signature", !pota_manifest_verifier_verify(
        &verifier, &manifest, header, sizeof(header)) &&
        mock.hash_calls == 3u && mock.verify_calls == 2u);

    keys[1] = keys[0];
    failed += expect("duplicate key", pota_public_key_registry_find(
        &verifier.registry, 7u) == NULL);

    return failed == 0 ? 0 : 1;
}
