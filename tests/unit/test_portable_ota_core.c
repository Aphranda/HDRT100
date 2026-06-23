#include "pota_core.h"

#include <stdio.h>
#include <string.h>

#define MOCK_FLASH_SIZE 8192u
#define MOCK_SLOT_A_OFFSET 0u
#define MOCK_SLOT_B_OFFSET 4096u
#define MOCK_SLOT_SIZE 2048u
#define MOCK_SLOT_A_RUN_OFFSET 0x10040000u
#define MOCK_SLOT_B_RUN_OFFSET 0x101C0000u
#define MOCK_PAGE_SIZE 16u
#define MOCK_SECTOR_SIZE 256u

static uint8_t s_flash[MOCK_FLASH_SIZE];
static bool s_validate_vector_result;
static bool s_mark_pending_result;
static uint32_t s_erase_count;
static uint32_t s_program_count;
static uint32_t s_last_program_offset;
static uint32_t s_last_program_size;
static uint32_t s_watchdog_count;
static uint32_t s_yield_count;
static pota_slot_t s_pending_slot;
static uint32_t s_pending_size;
static uint32_t s_pending_crc32;
static uint32_t s_validate_offset;
static uint32_t s_validate_size;
static uint32_t s_validate_run_offset;

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

static bool mock_flash_read(uint32_t offset, void *buffer, uint32_t size)
{
    if (offset > MOCK_FLASH_SIZE || size > (MOCK_FLASH_SIZE - offset)) {
        return false;
    }
    memcpy(buffer, &s_flash[offset], size);
    return true;
}

static bool mock_flash_erase(uint32_t offset, uint32_t size)
{
    if (offset > MOCK_FLASH_SIZE || size > (MOCK_FLASH_SIZE - offset)) {
        return false;
    }
    memset(&s_flash[offset], 0xFF, size);
    s_erase_count++;
    return true;
}

static bool mock_flash_program(uint32_t offset, const void *data, uint32_t size)
{
    if (data == NULL || offset > MOCK_FLASH_SIZE || size > (MOCK_FLASH_SIZE - offset)) {
        return false;
    }
    memcpy(&s_flash[offset], data, size);
    s_program_count++;
    s_last_program_offset = offset;
    s_last_program_size = size;
    return true;
}

static bool mock_mark_pending(pota_slot_t slot, uint32_t image_size, uint32_t image_crc32)
{
    if (!s_mark_pending_result) {
        return false;
    }
    s_pending_slot = slot;
    s_pending_size = image_size;
    s_pending_crc32 = image_crc32;
    return true;
}

static bool mock_confirm_active(void)
{
    return true;
}

static bool mock_validate_vector(uint32_t slot_offset, uint32_t image_size, uint32_t run_offset)
{
    s_validate_offset = slot_offset;
    s_validate_size = image_size;
    s_validate_run_offset = run_offset;
    return s_validate_vector_result;
}

static void mock_feed_watchdog(void)
{
    s_watchdog_count++;
}

static void mock_yield_or_delay(void)
{
    s_yield_count++;
}

static void reset_mock(void)
{
    memset(s_flash, 0xA5, sizeof(s_flash));
    s_validate_vector_result = true;
    s_mark_pending_result = true;
    s_erase_count = 0u;
    s_program_count = 0u;
    s_last_program_offset = 0u;
    s_last_program_size = 0u;
    s_watchdog_count = 0u;
    s_yield_count = 0u;
    s_pending_slot = POTA_SLOT_NONE;
    s_pending_size = 0u;
    s_pending_crc32 = 0u;
    s_validate_offset = 0u;
    s_validate_size = 0u;
    s_validate_run_offset = 0u;
}

static pota_platform_t make_platform(pota_boot_mode_t boot_mode)
{
    const pota_platform_t platform = {
        .info = {
            .product_id = "RP2350_TRIG",
            .hardware_id = "PICO2",
            .bootloader_version = POTA_PACK_VERSION(0u, 1u, 0u),
            .boot_mode = boot_mode,
            .active_slot = POTA_SLOT_A,
            .slot_a = {
                .offset = MOCK_SLOT_A_OFFSET,
                .size = MOCK_SLOT_SIZE,
                .run_offset = MOCK_SLOT_A_RUN_OFFSET,
            },
            .slot_b = {
                .offset = MOCK_SLOT_B_OFFSET,
                .size = MOCK_SLOT_SIZE,
                .run_offset = MOCK_SLOT_B_RUN_OFFSET,
            },
            .flash_page_size = MOCK_PAGE_SIZE,
            .flash_sector_size = MOCK_SECTOR_SIZE,
        },
        .ops = {
            .flash_read = mock_flash_read,
            .flash_erase = mock_flash_erase,
            .flash_program = mock_flash_program,
            .mark_pending = mock_mark_pending,
            .confirm_active = mock_confirm_active,
            .validate_vector = mock_validate_vector,
            .ota_yield_or_delay = mock_yield_or_delay,
            .feed_watchdog = mock_feed_watchdog,
        },
    };
    return platform;
}

static void fill_image(uint8_t *image, uint32_t size, uint8_t seed)
{
    for (uint32_t i = 0u; i < size; i++) {
        image[i] = (uint8_t)(seed + i);
    }
}

static void make_package(uint8_t *package,
                         const uint8_t *image_a,
                         uint32_t image_a_size,
                         const uint8_t *image_b,
                         uint32_t image_b_size)
{
    const uint32_t image_a_offset = POTA_PACKAGE_HEADER_SIZE;
    const uint32_t image_b_offset = image_a_offset + image_a_size;
    const uint32_t package_size = image_b_offset + image_b_size;

    memset(package, 0, package_size);
    write_le32(package, 0u, POTA_PACKAGE_MAGIC);
    write_le32(package, 4u, POTA_PACKAGE_VERSION);
    write_le32(package, 8u, POTA_PACKAGE_HEADER_SIZE);
    write_le32(package, 12u, package_size);
    write_le32(package, 16u, pota_crc32_compute(package, package_size));
    write_le32(package, 20u, 2u);
    fill_text(package, 32u, "RP2350_TRIG");
    fill_text(package, 64u, "PICO2");
    write_le32(package, 108u, POTA_PACK_VERSION(0u, 1u, 0u));
    fill_text(package, 112u, "core-test");

    write_le32(package, 192u, (uint32_t)POTA_SLOT_A);
    write_le32(package, 196u, image_a_offset);
    write_le32(package, 200u, image_a_size);
    write_le32(package, 204u, pota_crc32_compute(image_a, image_a_size));
    write_le32(package, 208u, MOCK_SLOT_A_RUN_OFFSET);

    write_le32(package, 224u, (uint32_t)POTA_SLOT_B);
    write_le32(package, 228u, image_b_offset);
    write_le32(package, 232u, image_b_size);
    write_le32(package, 236u, pota_crc32_compute(image_b, image_b_size));
    write_le32(package, 240u, MOCK_SLOT_B_RUN_OFFSET);

    memcpy(&package[image_a_offset], image_a, image_a_size);
    memcpy(&package[image_b_offset], image_b, image_b_size);
    write_le32(package, 16u, pota_crc32_compute(package, package_size));
}

static int expect_error(const char *name, pota_error_t actual, pota_error_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n", name, (int)expected, (int)actual);
        return 1;
    }
    return 0;
}

static int service_until_receiving(pota_context_t *context, const char *name)
{
    for (uint32_t i = 0u; i < 16u; i++) {
        if (context->status.state == (uint32_t)POTA_STATE_RECEIVING) {
            return 0;
        }
        const pota_error_t error = pota_service(context, 0u);
        if (error != POTA_ERR_NONE) {
            (void)printf("%s: service error %d\n", name, (int)error);
            return 1;
        }
    }

    (void)printf("%s: did not reach receiving\n", name);
    return 1;
}

static int test_raw_positive(void)
{
    reset_mock();

    uint8_t image[128];
    fill_image(image, sizeof(image), 0x10u);
    const uint32_t image_crc32 = pota_crc32_compute(image, sizeof(image));

    pota_context_t context;
    pota_platform_t platform = make_platform(POTA_BOOT_MODE_COPY_TO_ACTIVE);
    int failed = 0;

    if (!pota_init(&context, &platform)) {
        (void)printf("raw init failed\n");
        return 1;
    }

    const pota_begin_t begin = {
        .size = sizeof(image),
        .crc32 = image_crc32,
        .package_mode = false,
    };

    failed += expect_error("raw begin", pota_begin(&context, &begin), POTA_ERR_NONE);
    failed += service_until_receiving(&context, "raw service");
    failed += expect_error("raw write", pota_write(&context, image, sizeof(image)), POTA_ERR_NONE);
    failed += expect_error("raw end", pota_end(&context), POTA_ERR_NONE);

    if (memcmp(&s_flash[MOCK_SLOT_B_OFFSET], image, sizeof(image)) != 0 ||
        s_pending_slot != POTA_SLOT_B ||
        s_pending_size != sizeof(image) ||
        s_pending_crc32 != image_crc32 ||
        s_validate_offset != MOCK_SLOT_B_OFFSET ||
        s_validate_size != sizeof(image) ||
        s_validate_run_offset != MOCK_SLOT_A_RUN_OFFSET ||
        s_erase_count == 0u ||
        s_program_count == 0u ||
        s_watchdog_count == 0u ||
        s_yield_count == 0u) {
        (void)printf("raw positive side effects failed\n");
        failed++;
    }

    return failed;
}

static int test_package_positive_copy_to_active(void)
{
    reset_mock();

    uint8_t image_a[128];
    uint8_t image_b[128];
    uint8_t package[POTA_PACKAGE_HEADER_SIZE + sizeof(image_a) + sizeof(image_b)];
    fill_image(image_a, sizeof(image_a), 0x20u);
    fill_image(image_b, sizeof(image_b), 0x80u);
    make_package(package, image_a, sizeof(image_a), image_b, sizeof(image_b));

    pota_context_t context;
    pota_platform_t platform = make_platform(POTA_BOOT_MODE_COPY_TO_ACTIVE);
    int failed = 0;

    if (!pota_init(&context, &platform)) {
        (void)printf("package init failed\n");
        return 1;
    }

    const pota_begin_t begin = {
        .size = sizeof(package),
        .crc32 = pota_crc32_compute(package, sizeof(package)),
        .package_mode = true,
    };

    failed += expect_error("package begin", pota_begin(&context, &begin), POTA_ERR_NONE);
    failed += expect_error("package header",
                           pota_write(&context, package, POTA_PACKAGE_HEADER_SIZE),
                           POTA_ERR_NONE);
    failed += service_until_receiving(&context, "package service");
    failed += expect_error("package payload",
                           pota_write(&context,
                                      &package[POTA_PACKAGE_HEADER_SIZE],
                                      sizeof(package) - POTA_PACKAGE_HEADER_SIZE),
                           POTA_ERR_NONE);
    failed += expect_error("package end", pota_end(&context), POTA_ERR_NONE);

    if (memcmp(&s_flash[MOCK_SLOT_B_OFFSET], image_a, sizeof(image_a)) != 0 ||
        s_pending_slot != POTA_SLOT_B ||
        s_pending_size != sizeof(image_a) ||
        s_pending_crc32 != pota_crc32_compute(image_a, sizeof(image_a)) ||
        s_validate_run_offset != MOCK_SLOT_A_RUN_OFFSET) {
        (void)printf("package positive side effects failed\n");
        failed++;
    }

    return failed;
}

static int test_crc_failure(void)
{
    reset_mock();

    uint8_t image[64];
    fill_image(image, sizeof(image), 0x30u);

    pota_context_t context;
    pota_platform_t platform = make_platform(POTA_BOOT_MODE_COPY_TO_ACTIVE);
    int failed = 0;

    (void)pota_init(&context, &platform);
    const pota_begin_t begin = {
        .size = sizeof(image),
        .crc32 = 0x12345678u,
        .package_mode = false,
    };
    failed += expect_error("crc begin", pota_begin(&context, &begin), POTA_ERR_NONE);
    failed += service_until_receiving(&context, "crc service");
    failed += expect_error("crc write", pota_write(&context, image, sizeof(image)), POTA_ERR_NONE);
    failed += expect_error("crc end", pota_end(&context), POTA_ERR_CRC);

    if (s_pending_slot != POTA_SLOT_NONE) {
        (void)printf("crc failure marked pending unexpectedly\n");
        failed++;
    }

    return failed;
}

static int test_raw_final_block_padding(void)
{
    reset_mock();

    uint8_t image[18];
    fill_image(image, sizeof(image), 0x70u);

    pota_context_t context;
    pota_platform_t platform = make_platform(POTA_BOOT_MODE_COPY_TO_ACTIVE);
    int failed = 0;

    (void)pota_init(&context, &platform);
    const pota_begin_t begin = {
        .size = sizeof(image),
        .crc32 = pota_crc32_compute(image, sizeof(image)),
        .package_mode = false,
    };

    failed += expect_error("padding begin", pota_begin(&context, &begin), POTA_ERR_NONE);
    failed += service_until_receiving(&context, "padding service");
    failed += expect_error("padding write", pota_write(&context, image, sizeof(image)), POTA_ERR_NONE);
    failed += expect_error("padding end", pota_end(&context), POTA_ERR_NONE);

    if (s_last_program_offset != MOCK_SLOT_B_OFFSET ||
        s_last_program_size != 32u ||
        memcmp(&s_flash[MOCK_SLOT_B_OFFSET], image, sizeof(image)) != 0) {
        (void)printf("raw final padding side effects failed\n");
        failed++;
    }

    for (uint32_t i = sizeof(image); i < s_last_program_size; i++) {
        if (s_flash[MOCK_SLOT_B_OFFSET + i] != 0xFFu) {
            (void)printf("raw final padding fill failed\n");
            failed++;
            break;
        }
    }

    return failed;
}

static int test_raw_unaligned_nonfinal_rejected(void)
{
    reset_mock();

    uint8_t image[32];
    fill_image(image, sizeof(image), 0x80u);

    pota_context_t context;
    pota_platform_t platform = make_platform(POTA_BOOT_MODE_COPY_TO_ACTIVE);
    int failed = 0;

    (void)pota_init(&context, &platform);
    const pota_begin_t begin = {
        .size = sizeof(image),
        .crc32 = pota_crc32_compute(image, sizeof(image)),
        .package_mode = false,
    };

    failed += expect_error("unaligned begin", pota_begin(&context, &begin), POTA_ERR_NONE);
    failed += service_until_receiving(&context, "unaligned service");
    failed += expect_error("unaligned write", pota_write(&context, image, 18u), POTA_ERR_BAD_ARGUMENT);

    if (s_program_count != 0u || s_pending_slot != POTA_SLOT_NONE) {
        (void)printf("unaligned nonfinal had side effects\n");
        failed++;
    }

    return failed;
}

static int test_vector_failure(void)
{
    reset_mock();
    s_validate_vector_result = false;

    uint8_t image[64];
    fill_image(image, sizeof(image), 0x40u);

    pota_context_t context;
    pota_platform_t platform = make_platform(POTA_BOOT_MODE_COPY_TO_ACTIVE);
    int failed = 0;

    (void)pota_init(&context, &platform);
    const pota_begin_t begin = {
        .size = sizeof(image),
        .crc32 = pota_crc32_compute(image, sizeof(image)),
        .package_mode = false,
    };
    failed += expect_error("vector begin", pota_begin(&context, &begin), POTA_ERR_NONE);
    failed += service_until_receiving(&context, "vector service");
    failed += expect_error("vector write", pota_write(&context, image, sizeof(image)), POTA_ERR_NONE);
    failed += expect_error("vector end", pota_end(&context), POTA_ERR_VECTOR);

    if (s_pending_slot != POTA_SLOT_NONE) {
        (void)printf("vector failure marked pending unexpectedly\n");
        failed++;
    }

    return failed;
}

static int test_metadata_failure(void)
{
    reset_mock();
    s_mark_pending_result = false;

    uint8_t image[64];
    fill_image(image, sizeof(image), 0x50u);

    pota_context_t context;
    pota_platform_t platform = make_platform(POTA_BOOT_MODE_COPY_TO_ACTIVE);
    int failed = 0;

    (void)pota_init(&context, &platform);
    const pota_begin_t begin = {
        .size = sizeof(image),
        .crc32 = pota_crc32_compute(image, sizeof(image)),
        .package_mode = false,
    };
    failed += expect_error("metadata begin", pota_begin(&context, &begin), POTA_ERR_NONE);
    failed += service_until_receiving(&context, "metadata service");
    failed += expect_error("metadata write", pota_write(&context, image, sizeof(image)), POTA_ERR_NONE);
    failed += expect_error("metadata end", pota_end(&context), POTA_ERR_METADATA);

    return failed;
}

static int test_package_header_crc_mismatch(void)
{
    reset_mock();

    uint8_t image_a[128];
    uint8_t image_b[128];
    uint8_t package[POTA_PACKAGE_HEADER_SIZE + sizeof(image_a) + sizeof(image_b)];
    fill_image(image_a, sizeof(image_a), 0x60u);
    fill_image(image_b, sizeof(image_b), 0xA0u);
    make_package(package, image_a, sizeof(image_a), image_b, sizeof(image_b));

    pota_context_t context;
    pota_platform_t platform = make_platform(POTA_BOOT_MODE_COPY_TO_ACTIVE);
    int failed = 0;

    (void)pota_init(&context, &platform);
    const pota_begin_t begin = {
        .size = sizeof(package),
        .crc32 = pota_crc32_compute(package, sizeof(package)) ^ 0xFFFFFFFFu,
        .package_mode = true,
    };

    failed += expect_error("package crc mismatch begin",
                           pota_begin(&context, &begin),
                           POTA_ERR_NONE);
    failed += expect_error("package crc mismatch header",
                           pota_write(&context, package, POTA_PACKAGE_HEADER_SIZE),
                           POTA_ERR_BAD_HEADER);

    if (s_erase_count != 0u || s_program_count != 0u || s_pending_slot != POTA_SLOT_NONE) {
        (void)printf("package crc mismatch had side effects\n");
        failed++;
    }

    return failed;
}

static int test_invalid_flash_geometry_rejected(void)
{
    reset_mock();

    uint8_t image[64];
    fill_image(image, sizeof(image), 0x90u);

    const pota_begin_t begin = {
        .size = sizeof(image),
        .crc32 = pota_crc32_compute(image, sizeof(image)),
        .package_mode = false,
    };
    int failed = 0;

    pota_context_t context;
    pota_platform_t platform = make_platform(POTA_BOOT_MODE_COPY_TO_ACTIVE);
    platform.info.flash_page_size = 24u;
    (void)pota_init(&context, &platform);
    failed += expect_error("invalid page geometry begin",
                           pota_begin(&context, &begin),
                           POTA_ERR_BAD_ARGUMENT);

    if (s_erase_count != 0u || s_program_count != 0u || s_pending_slot != POTA_SLOT_NONE) {
        (void)printf("invalid page geometry had side effects\n");
        failed++;
    }

    reset_mock();
    platform = make_platform(POTA_BOOT_MODE_COPY_TO_ACTIVE);
    platform.info.flash_sector_size = 384u;
    (void)pota_init(&context, &platform);
    failed += expect_error("invalid sector geometry begin",
                           pota_begin(&context, &begin),
                           POTA_ERR_BAD_ARGUMENT);

    if (s_erase_count != 0u || s_program_count != 0u || s_pending_slot != POTA_SLOT_NONE) {
        (void)printf("invalid sector geometry had side effects\n");
        failed++;
    }

    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_raw_positive();
    failed += test_package_positive_copy_to_active();
    failed += test_crc_failure();
    failed += test_raw_final_block_padding();
    failed += test_raw_unaligned_nonfinal_rejected();
    failed += test_vector_failure();
    failed += test_metadata_failure();
    failed += test_package_header_crc_mismatch();
    failed += test_invalid_flash_geometry_rejected();
    return failed == 0 ? 0 : 1;
}
