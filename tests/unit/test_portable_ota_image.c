#include "pota_image.h"

#include <stdio.h>
#include <string.h>

static uint8_t fake_flash[64];

static bool fake_flash_read(uint32_t offset, void *buffer, size_t size)
{
    if (offset > sizeof(fake_flash) || size > (sizeof(fake_flash) - offset)) {
        return false;
    }

    memcpy(buffer, &fake_flash[offset], size);
    return true;
}

static void write_u32(uint32_t offset, uint32_t value)
{
    fake_flash[offset + 0u] = (uint8_t)(value & 0xFFu);
    fake_flash[offset + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
    fake_flash[offset + 2u] = (uint8_t)((value >> 16u) & 0xFFu);
    fake_flash[offset + 3u] = (uint8_t)((value >> 24u) & 0xFFu);
}

static int expect_true(const char *name, bool value)
{
    if (!value) {
        (void)printf("%s: expected true\n", name);
        return 1;
    }
    return 0;
}

static int expect_false(const char *name, bool value)
{
    if (value) {
        (void)printf("%s: expected false\n", name);
        return 1;
    }
    return 0;
}

int main(void)
{
    const pota_image_vector_constraints_t constraints = {
        .sram_base = 0x20000000u,
        .sram_end = 0x20082000u,
        .xip_base = 0x10000000u,
        .flash_read = fake_flash_read,
    };

    int failed = 0;

    memset(fake_flash, 0, sizeof(fake_flash));
    write_u32(0u, 0x20001000u);
    write_u32(4u, 0x10000101u);

    failed += expect_true("valid vector",
                          pota_image_validate_app_vector(0u, 0x200u, 0u, &constraints));
    failed += expect_false("small image",
                           pota_image_validate_app_vector(0u, 7u, 0u, &constraints));

    write_u32(0u, 0x1FFFFFFCu);
    failed += expect_false("stack below sram",
                           pota_image_validate_app_vector(0u, 0x200u, 0u, &constraints));

    write_u32(0u, 0x20001000u);
    write_u32(4u, 0x10000100u);
    failed += expect_false("reset handler not thumb",
                           pota_image_validate_app_vector(0u, 0x200u, 0u, &constraints));

    write_u32(4u, 0x10000201u);
    failed += expect_false("reset handler outside image",
                           pota_image_validate_app_vector(0u, 0x200u, 0u, &constraints));

    write_u32(4u, 0x10010101u);
    failed += expect_true("run offset vector",
                          pota_image_validate_app_vector(0u, 0x200u, 0x10000u, &constraints));

    failed += expect_false("null constraints",
                           pota_image_validate_app_vector(0u, 0x200u, 0u, NULL));

    return failed;
}
