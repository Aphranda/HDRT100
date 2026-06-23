#include "pota_package.h"

#include <stdio.h>
#include <string.h>

static void write_le32(uint8_t *data, uint32_t offset, uint32_t value)
{
    data[offset + 0u] = (uint8_t)(value & 0xFFu);
    data[offset + 1u] = (uint8_t)((value >> 8u) & 0xFFu);
    data[offset + 2u] = (uint8_t)((value >> 16u) & 0xFFu);
    data[offset + 3u] = (uint8_t)((value >> 24u) & 0xFFu);
}

static void fill_text(uint8_t *data, uint32_t offset, const char *text)
{
    memset(&data[offset], 0, POTA_TEXT_FIELD_SIZE);
    (void)snprintf((char *)&data[offset], POTA_TEXT_FIELD_SIZE, "%s", text);
}

static void make_valid_header(uint8_t *header)
{
    memset(header, 0, POTA_PACKAGE_HEADER_SIZE);
    write_le32(header, 0u, POTA_PACKAGE_MAGIC);
    write_le32(header, 4u, POTA_PACKAGE_VERSION);
    write_le32(header, 8u, POTA_PACKAGE_HEADER_SIZE);
    write_le32(header, 12u, 4096u);
    write_le32(header, 20u, 2u);
    fill_text(header, 32u, "RP2350_TRIG");
    fill_text(header, 64u, "PICO2");
    write_le32(header, 96u, 0u);
    write_le32(header, 100u, 1u);
    write_le32(header, 104u, 0u);
    write_le32(header, 108u, POTA_PACK_VERSION(0u, 1u, 0u));
    fill_text(header, 112u, "unit-test");

    write_le32(header, 192u, (uint32_t)POTA_SLOT_A);
    write_le32(header, 196u, POTA_PACKAGE_HEADER_SIZE);
    write_le32(header, 200u, 1024u);
    write_le32(header, 204u, 0x11223344u);
    write_le32(header, 208u, 0x00040000u);

    write_le32(header, 224u, (uint32_t)POTA_SLOT_B);
    write_le32(header, 228u, POTA_PACKAGE_HEADER_SIZE + 1024u);
    write_le32(header, 232u, 1536u);
    write_le32(header, 236u, 0x55667788u);
    write_le32(header, 240u, 0x001C0000u);
}

static int expect_error(const char *name, pota_error_t actual, pota_error_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n", name, (int)expected, (int)actual);
        return 1;
    }
    return 0;
}

int main(void)
{
    uint8_t header[POTA_PACKAGE_HEADER_SIZE];
    pota_package_manifest_t manifest;
    const pota_package_constraints_t constraints = {
        .product_id = "RP2350_TRIG",
        .hardware_id = "PICO2",
        .bootloader_version = POTA_PACK_VERSION(0u, 1u, 0u),
    };

    int failed = 0;
    make_valid_header(header);
    failed += expect_error("valid",
                           pota_package_parse_header(header, sizeof(header), &constraints, &manifest),
                           POTA_ERR_NONE);
    if (pota_package_find_image(&manifest, POTA_SLOT_A) == NULL ||
        pota_package_find_image(&manifest, POTA_SLOT_B) == NULL ||
        pota_package_find_image(&manifest, POTA_SLOT_NONE) != NULL) {
        (void)printf("find_image: unexpected result\n");
        failed++;
    }
    uint32_t image_index = 99u;
    if (!pota_package_find_image_index(&manifest, POTA_SLOT_A, &image_index) ||
        image_index != 0u ||
        !pota_package_find_image_index(&manifest, POTA_SLOT_B, NULL) ||
        pota_package_find_image_index(&manifest, POTA_SLOT_NONE, &image_index)) {
        (void)printf("find_image_index: unexpected result index=%lu\n",
                     (unsigned long)image_index);
        failed++;
    }

    make_valid_header(header);
    write_le32(header, 0u, 0u);
    failed += expect_error("bad magic",
                           pota_package_parse_header(header, sizeof(header), &constraints, &manifest),
                           POTA_ERR_BAD_HEADER);

    make_valid_header(header);
    write_le32(header, 4u, POTA_PACKAGE_VERSION + 1u);
    failed += expect_error("bad version",
                           pota_package_parse_header(header, sizeof(header), &constraints, &manifest),
                           POTA_ERR_BAD_HEADER);

    make_valid_header(header);
    write_le32(header, 12u, POTA_PACKAGE_HEADER_SIZE);
    failed += expect_error("bad package size",
                           pota_package_parse_header(header, sizeof(header), &constraints, &manifest),
                           POTA_ERR_BAD_HEADER);

    make_valid_header(header);
    write_le32(header, 192u, 99u);
    failed += expect_error("bad slot",
                           pota_package_parse_header(header, sizeof(header), &constraints, &manifest),
                           POTA_ERR_BAD_HEADER);

    make_valid_header(header);
    fill_text(header, 32u, "OTHER");
    failed += expect_error("product mismatch",
                           pota_package_parse_header(header, sizeof(header), &constraints, &manifest),
                           POTA_ERR_PRODUCT_MISMATCH);

    make_valid_header(header);
    fill_text(header, 64u, "OTHER");
    failed += expect_error("hardware mismatch",
                           pota_package_parse_header(header, sizeof(header), &constraints, &manifest),
                           POTA_ERR_HARDWARE_MISMATCH);

    make_valid_header(header);
    write_le32(header, 108u, POTA_PACK_VERSION(9u, 0u, 0u));
    failed += expect_error("bootloader too old",
                           pota_package_parse_header(header, sizeof(header), &constraints, &manifest),
                           POTA_ERR_BOOTLOADER_TOO_OLD);

    return failed == 0 ? 0 : 1;
}
