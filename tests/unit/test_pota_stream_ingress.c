#include "pota_stream_ingress.h"

#include <stdio.h>
#include <string.h>

#define FLASH_SIZE 4096u
#define SLOT_A 0u
#define SLOT_B 2048u
#define SLOT_SIZE 1024u

static uint8_t s_flash[FLASH_SIZE];

static bool read_flash(uint32_t offset, void *data, uint32_t size)
{
    if (data == NULL || offset > FLASH_SIZE || size > FLASH_SIZE - offset) {
        return false;
    }
    memcpy(data, &s_flash[offset], size);
    return true;
}

static bool erase_flash(uint32_t offset, uint32_t size)
{
    if (offset > FLASH_SIZE || size > FLASH_SIZE - offset ||
        (offset % 256u) != 0u || (size % 256u) != 0u) {
        return false;
    }
    memset(&s_flash[offset], 0xFF, size);
    return true;
}

static bool program_flash(uint32_t offset, const void *data, uint32_t size)
{
    if (data == NULL || offset > FLASH_SIZE || size > FLASH_SIZE - offset ||
        (offset % 16u) != 0u || (size % 16u) != 0u) {
        return false;
    }
    memcpy(&s_flash[offset], data, size);
    return true;
}

static bool mark_pending(pota_slot_t slot, uint32_t size, uint32_t crc32)
{
    (void)slot;
    (void)size;
    (void)crc32;
    return true;
}

static bool confirm_active(void)
{
    return true;
}

static bool validate_vector(uint32_t offset, uint32_t size, uint32_t run_offset)
{
    return offset == SLOT_B && size != 0u && run_offset != 0u;
}

static bool expect(const char *name, bool value)
{
    if (!value) {
        (void)printf("FAIL %s\n", name);
    }
    return value;
}

int main(void)
{
    memset(s_flash, 0xFF, sizeof(s_flash));
    const pota_platform_t platform = {
        .info = {
            .product_id = "DHRT100",
            .hardware_id = "dhrt100",
            .bootloader_version = POTA_PACK_VERSION(0, 1, 0),
            .boot_mode = POTA_BOOT_MODE_DIRECT_AB,
            .active_slot = POTA_SLOT_A,
            .slot_a = {SLOT_A, SLOT_SIZE, 0x10000000u},
            .slot_b = {SLOT_B, SLOT_SIZE, 0x10000800u},
            .flash_page_size = 16u,
            .flash_sector_size = 256u,
        },
        .ops = {
            .flash_read = read_flash,
            .flash_erase = erase_flash,
            .flash_program = program_flash,
            .mark_pending = mark_pending,
            .confirm_active = confirm_active,
            .validate_vector = validate_vector,
        },
    };
    pota_stream_session_t session;
    pota_stream_ingress_t ingress;
    int failed = 0;
    failed += !expect("session init", pota_stream_session_init(&session, &platform));
    failed += !expect("ingress init", pota_stream_ingress_init(
        &ingress, &session,
        POTA_STREAM_INGRESS_SOURCE_BIT(POTA_STREAM_INGRESS_USB_CDC) |
            POTA_STREAM_INGRESS_SOURCE_BIT(POTA_STREAM_INGRESS_USBTMC) |
            POTA_STREAM_INGRESS_SOURCE_BIT(POTA_STREAM_INGRESS_SD) |
            POTA_STREAM_INGRESS_SOURCE_BIT(POTA_STREAM_INGRESS_UART) |
            POTA_STREAM_INGRESS_SOURCE_BIT(POTA_STREAM_INGRESS_RS485),
        32u));

    pota_stream_open_t open;
    memset(&open, 0, sizeof(open));
    open.session_id = 9u;
    open.generation = 1u;
    open.capability_mask = POTA_STREAM_CAP_INACTIVE_WRITE | POTA_STREAM_CAP_DURABLE_ACK;
    open.map_version = 1u;
    open.partition_id = POTA_STREAM_PARTITION_APP_B;
    open.destination_slot = POTA_SLOT_B;
    open.object_id = 4u;
    open.total_size = 16u;
    open.package_crc32 = pota_crc32_compute("0123456789abcdef", 16u);
    open.identity[0] = 1u;
    open.package_hash[0] = 2u;

    failed += !expect("open USB CDC", pota_stream_ingress_open(
        &ingress, POTA_STREAM_INGRESS_USB_CDC, &open) == POTA_STREAM_INGRESS_OK);
    while (pota_stream_session_state(&session) == POTA_STREAM_STATE_OPEN) {
        failed += !expect("service", pota_stream_ingress_service(
            &ingress, POTA_STREAM_INGRESS_USB_CDC, 100u) == POTA_STREAM_INGRESS_OK);
    }
    const uint8_t payload[16] = {
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f',
    };
    failed += !expect("bad crc", pota_stream_ingress_write(
        &ingress, POTA_STREAM_INGRESS_USB_CDC, 0u, payload, sizeof(payload), true,
        0u) == POTA_STREAM_INGRESS_CRC_MISMATCH);
    failed += !expect("oversize", pota_stream_ingress_write(
        &ingress, POTA_STREAM_INGRESS_USBTMC, 0u, payload, sizeof(payload), false,
        0u) == POTA_STREAM_INGRESS_SOURCE_REJECTED);
    failed += !expect("wrong source", pota_stream_ingress_write(
        &ingress, POTA_STREAM_INGRESS_SD, 0u, payload, sizeof(payload), false,
        0u) == POTA_STREAM_INGRESS_SOURCE_REJECTED);
    failed += !expect("write", pota_stream_ingress_write(
        &ingress, POTA_STREAM_INGRESS_USB_CDC, 0u, payload, sizeof(payload), true,
        pota_crc32_compute(payload, sizeof(payload))) == POTA_STREAM_INGRESS_OK);
    failed += !expect("close", pota_stream_ingress_close(
        &ingress, POTA_STREAM_INGRESS_USB_CDC) == POTA_STREAM_INGRESS_OK);
    pota_stream_ingress_status_t status;
    failed += !expect("status", pota_stream_ingress_get_status(&ingress, &status));
    failed += !expect("status state", status.state == POTA_STREAM_STATE_READY_TO_REBOOT &&
                      status.durable_offset == sizeof(payload) &&
                      status.stream_token != 0u &&
                      status.last_result == POTA_STREAM_INGRESS_OK);

    if (failed != 0) {
        return 1;
    }
    (void)printf("pota stream ingress tests passed\n");
    return 0;
}
