#include "portable_ota_crypto.h"

#include "portable_ota_key_registry.generated.h"
#include "pota_manifest_verifier.h"

#include "mbedtls/bignum.h"
#include "mbedtls/ecdsa.h"
#include "mbedtls/ecp.h"
#include "mbedtls/sha256.h"

static bool portable_ota_sha256(void *context, const uint8_t *data,
                                uint32_t size,
                                uint8_t digest[POTA_SHA256_SIZE])
{
    (void)context;
    return data != NULL && digest != NULL &&
           mbedtls_sha256(data, size, digest, 0) == 0;
}

static bool portable_ota_verify_p256(
    void *context,
    const uint8_t public_key[POTA_PUBLIC_KEY_P256_SIZE],
    const uint8_t digest[POTA_SHA256_SIZE],
    const uint8_t signature[POTA_MANIFEST_SIGNATURE_MAX_SIZE])
{
    (void)context;
    if (public_key == NULL || digest == NULL || signature == NULL) {
        return false;
    }

    mbedtls_ecp_group group;
    mbedtls_ecp_point point;
    mbedtls_mpi r;
    mbedtls_mpi s;
    mbedtls_mpi half_order;
    mbedtls_ecp_group_init(&group);
    mbedtls_ecp_point_init(&point);
    mbedtls_mpi_init(&r);
    mbedtls_mpi_init(&s);
    mbedtls_mpi_init(&half_order);

    int result = mbedtls_ecp_group_load(&group, MBEDTLS_ECP_DP_SECP256R1);
    if (result == 0) {
        result = mbedtls_ecp_point_read_binary(
            &group, &point, public_key, POTA_PUBLIC_KEY_P256_SIZE);
    }
    if (result == 0) {
        result = mbedtls_mpi_read_binary(&r, signature, POTA_SHA256_SIZE);
    }
    if (result == 0) {
        result = mbedtls_mpi_read_binary(
            &s, &signature[POTA_SHA256_SIZE], POTA_SHA256_SIZE);
    }
    if (result == 0) {
        result = mbedtls_mpi_copy(&half_order, &group.N);
    }
    if (result == 0) {
        result = mbedtls_mpi_shift_r(&half_order, 1u);
    }
    if (result == 0 && mbedtls_mpi_cmp_mpi(&s, &half_order) > 0) {
        result = -1;
    }
    if (result == 0) {
        result = mbedtls_ecdsa_verify(
            &group, digest, POTA_SHA256_SIZE, &point, &r, &s);
    }

    mbedtls_mpi_free(&half_order);
    mbedtls_mpi_free(&s);
    mbedtls_mpi_free(&r);
    mbedtls_ecp_point_free(&point);
    mbedtls_ecp_group_free(&group);
    return result == 0;
}

bool portable_ota_crypto_verify_manifest(
    void *context,
    const pota_package_manifest_t *manifest,
    const uint8_t *header,
    uint32_t header_size)
{
    (void)context;
    pota_manifest_verifier_t verifier = {
        .registry = g_portable_ota_key_registry,
        .sha256 = portable_ota_sha256,
        .verify_p256 = portable_ota_verify_p256,
    };
    return pota_manifest_verifier_verify(
        &verifier, manifest, header, header_size);
}
