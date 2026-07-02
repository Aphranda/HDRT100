#include "sd_card.h"

#include <stddef.h>
#include <string.h>

#include "hardware/gpio.h"
#include "pico/time.h"

#define SD_DUMMY_BYTE 0xFFu
#define SD_TOKEN_START_BLOCK 0xFEu
#define SD_CMD0_GO_IDLE_STATE 0u
#define SD_CMD8_SEND_IF_COND 8u
#define SD_CMD9_SEND_CSD 9u
#define SD_CMD16_SET_BLOCKLEN 16u
#define SD_CMD17_READ_SINGLE_BLOCK 17u
#define SD_CMD55_APP_CMD 55u
#define SD_CMD58_READ_OCR 58u
#define SD_ACMD41_SEND_OP_COND 41u
#define SD_INIT_TIMEOUT_MS 1200u
#define SD_READY_TIMEOUT_US 100000u

typedef struct {
    bool initialized;
    sd_card_config_t config;
    sd_card_info_t info;
} sd_card_context_t;

static sd_card_context_t s_sd;

static uint8_t sd_xfer(uint8_t value)
{
    uint8_t response = SD_DUMMY_BYTE;
    (void)spi_write_read_blocking(s_sd.config.spi, &value, &response, 1);
    return response;
}

static void sd_select(bool selected)
{
    gpio_put(s_sd.config.cs_pin, selected ? 0 : 1);
    if (!selected) {
        (void)sd_xfer(SD_DUMMY_BYTE);
    }
}

static bool sd_wait_ready(uint32_t timeout_us)
{
    const absolute_time_t deadline = make_timeout_time_us((int64_t)timeout_us);
    do {
        if (sd_xfer(SD_DUMMY_BYTE) == SD_DUMMY_BYTE) {
            return true;
        }
        tight_loop_contents();
    } while (!time_reached(deadline));
    return false;
}

static sd_card_status_t sd_finish_probe(sd_card_info_t *info, sd_card_status_t status)
{
    sd_select(false);
    spi_set_baudrate(s_sd.config.spi, s_sd.config.run_baud_hz);
    info->status = status;
    s_sd.info = *info;
    return status;
}

static sd_card_status_t sd_read_data_block(uint8_t *buffer, size_t length)
{
    const absolute_time_t deadline = make_timeout_time_ms(100);
    uint8_t token;
    do {
        token = sd_xfer(SD_DUMMY_BYTE);
        if (token == SD_TOKEN_START_BLOCK) {
            break;
        }
        tight_loop_contents();
    } while (!time_reached(deadline));

    if (token != SD_TOKEN_START_BLOCK) {
        return SD_CARD_STATUS_TIMEOUT;
    }

    for (size_t i = 0u; i < length; i++) {
        buffer[i] = sd_xfer(SD_DUMMY_BYTE);
    }
    (void)sd_xfer(SD_DUMMY_BYTE);
    (void)sd_xfer(SD_DUMMY_BYTE);
    return SD_CARD_STATUS_OK;
}

static uint8_t sd_command(uint8_t command, uint32_t argument, uint8_t crc)
{
    if (!sd_wait_ready(SD_READY_TIMEOUT_US)) {
        return 0xFFu;
    }

    (void)sd_xfer((uint8_t)(0x40u | command));
    (void)sd_xfer((uint8_t)(argument >> 24u));
    (void)sd_xfer((uint8_t)(argument >> 16u));
    (void)sd_xfer((uint8_t)(argument >> 8u));
    (void)sd_xfer((uint8_t)argument);
    (void)sd_xfer(crc);

    for (uint32_t i = 0u; i < 10u; i++) {
        const uint8_t response = sd_xfer(SD_DUMMY_BYTE);
        if ((response & 0x80u) == 0u) {
            return response;
        }
    }

    return 0xFFu;
}

static sd_card_status_t sd_read_register(uint8_t command, uint8_t *buffer, size_t length)
{
    uint8_t response = sd_command(command, 0u, 0x01u);
    if (response != 0u) {
        return SD_CARD_STATUS_BAD_RESPONSE;
    }

    return sd_read_data_block(buffer, length);
}

static void sd_parse_csd(const uint8_t *csd, sd_card_info_t *info)
{
    const uint8_t csd_structure = (uint8_t)((csd[0] >> 6u) & 0x03u);
    if (csd_structure == 1u) {
        const uint32_t c_size = (((uint32_t)(csd[7] & 0x3Fu)) << 16u) |
                                (((uint32_t)csd[8]) << 8u) |
                                (uint32_t)csd[9];
        info->block_count = (c_size + 1u) * 1024u;
        info->capacity_kib = (c_size + 1u) * 512u;
        return;
    }

    if (csd_structure == 0u) {
        const uint32_t read_bl_len = (uint32_t)(csd[5] & 0x0Fu);
        const uint32_t c_size = (((uint32_t)(csd[6] & 0x03u)) << 10u) |
                                (((uint32_t)csd[7]) << 2u) |
                                (((uint32_t)(csd[8] & 0xC0u)) >> 6u);
        const uint32_t c_size_mult = (((uint32_t)(csd[9] & 0x03u)) << 1u) |
                                     (((uint32_t)(csd[10] & 0x80u)) >> 7u);
        const uint32_t block_len = 1u << read_bl_len;
        const uint32_t mult = 1u << (c_size_mult + 2u);
        const uint32_t blocknr = (c_size + 1u) * mult;
        const uint64_t capacity_bytes = (uint64_t)blocknr * (uint64_t)block_len;
        info->block_count = (uint32_t)(capacity_bytes / 512u);
        info->capacity_kib = (uint32_t)(capacity_bytes / 1024u);
    }
}

bool sd_card_init(const sd_card_config_t *config)
{
    if (config == NULL || config->spi == NULL) {
        return false;
    }

    s_sd.config = *config;
    gpio_put(config->cs_pin, 1);
    gpio_init(config->cs_pin);
    gpio_set_dir(config->cs_pin, GPIO_OUT);
    spi_init(config->spi, config->init_baud_hz);
    spi_set_format(config->spi, 8, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(config->sck_pin, GPIO_FUNC_SPI);
    gpio_set_function(config->mosi_pin, GPIO_FUNC_SPI);
    gpio_set_function(config->miso_pin, GPIO_FUNC_SPI);
    gpio_set_pulls(config->sck_pin, true, false);
    gpio_set_pulls(config->mosi_pin, true, false);
    gpio_set_pulls(config->miso_pin, true, false);
    s_sd.initialized = true;
    return true;
}

sd_card_status_t sd_card_probe(sd_card_info_t *info)
{
    if (info == NULL) {
        return SD_CARD_STATUS_BAD_RESPONSE;
    }
    memset(info, 0, sizeof(*info));

    if (!s_sd.initialized) {
        info->status = SD_CARD_STATUS_NOT_INITIALIZED;
        return info->status;
    }

    spi_set_baudrate(s_sd.config.spi, s_sd.config.init_baud_hz);
    sd_select(false);
    for (uint32_t i = 0u; i < 10u; i++) {
        (void)sd_xfer(SD_DUMMY_BYTE);
    }

    sd_select(true);
    uint8_t response = 0xFFu;
    for (uint32_t i = 0u; i < 20u; i++) {
        response = sd_command(SD_CMD0_GO_IDLE_STATE, 0u, 0x95u);
        if (response == 0x01u) {
            break;
        }
    }

    if (response != 0x01u) {
        return sd_finish_probe(info, SD_CARD_STATUS_TIMEOUT);
    }

    bool sd_v2 = false;
    response = sd_command(SD_CMD8_SEND_IF_COND, 0x000001AAu, 0x87u);
    if (response == 0x01u) {
        uint8_t r7[4];
        for (uint32_t i = 0u; i < sizeof(r7); i++) {
            r7[i] = sd_xfer(SD_DUMMY_BYTE);
        }
        if (r7[2] != 0x01u || r7[3] != 0xAAu) {
            return sd_finish_probe(info, SD_CARD_STATUS_UNSUPPORTED);
        }
        sd_v2 = true;
    }

    const uint32_t arg = sd_v2 ? 0x40000000u : 0u;
    const absolute_time_t deadline = make_timeout_time_ms(SD_INIT_TIMEOUT_MS);
    do {
        response = sd_command(SD_CMD55_APP_CMD, 0u, 0x01u);
        if (response > 0x01u) {
            break;
        }
        response = sd_command(SD_ACMD41_SEND_OP_COND, arg, 0x01u);
        if (response == 0u) {
            break;
        }
        tight_loop_contents();
    } while (!time_reached(deadline));

    if (response != 0u) {
        return sd_finish_probe(info, SD_CARD_STATUS_TIMEOUT);
    }

    response = sd_command(SD_CMD58_READ_OCR, 0u, 0x01u);
    if (response != 0u) {
        return sd_finish_probe(info, SD_CARD_STATUS_BAD_RESPONSE);
    }

    uint8_t ocr[4];
    for (uint32_t i = 0u; i < sizeof(ocr); i++) {
        ocr[i] = sd_xfer(SD_DUMMY_BYTE);
    }
    info->high_capacity = (ocr[0] & 0x40u) != 0u;
    info->type = info->high_capacity ? SD_CARD_TYPE_SDHC_SDXC : SD_CARD_TYPE_SDSC;
    info->present = true;

    if (!info->high_capacity) {
        response = sd_command(SD_CMD16_SET_BLOCKLEN, 512u, 0x01u);
        if (response != 0u) {
            return sd_finish_probe(info, SD_CARD_STATUS_BAD_RESPONSE);
        }
    }

    uint8_t csd[16];
    const sd_card_status_t csd_status = sd_read_register(SD_CMD9_SEND_CSD, csd, sizeof(csd));
    if (csd_status == SD_CARD_STATUS_OK) {
        sd_parse_csd(csd, info);
    }

    return sd_finish_probe(info, SD_CARD_STATUS_OK);
}

sd_card_status_t sd_card_read_blocks(uint32_t sector, uint32_t count, uint8_t *buffer)
{
    if (!s_sd.initialized || buffer == NULL || count == 0u) {
        return SD_CARD_STATUS_BAD_RESPONSE;
    }
    if (!s_sd.info.present || s_sd.info.status != SD_CARD_STATUS_OK) {
        return SD_CARD_STATUS_NOT_INITIALIZED;
    }

    sd_select(true);
    for (uint32_t i = 0u; i < count; i++) {
        const uint32_t current_sector = sector + i;
        const uint32_t address = s_sd.info.high_capacity ? current_sector : current_sector * 512u;
        const uint8_t response = sd_command(SD_CMD17_READ_SINGLE_BLOCK, address, 0x01u);
        if (response != 0u) {
            return sd_finish_probe(&s_sd.info, SD_CARD_STATUS_BAD_RESPONSE);
        }
        const sd_card_status_t status = sd_read_data_block(buffer + (i * 512u), 512u);
        if (status != SD_CARD_STATUS_OK) {
            return sd_finish_probe(&s_sd.info, status);
        }
    }
    sd_select(false);
    return SD_CARD_STATUS_OK;
}

void sd_card_get_info(sd_card_info_t *info)
{
    if (info == NULL) {
        return;
    }
    *info = s_sd.info;
}

const char *sd_card_status_string(sd_card_status_t status)
{
    switch (status) {
    case SD_CARD_STATUS_OK: return "OK";
    case SD_CARD_STATUS_NOT_INITIALIZED: return "NOT_INITIALIZED";
    case SD_CARD_STATUS_TIMEOUT: return "TIMEOUT";
    case SD_CARD_STATUS_BAD_RESPONSE: return "BAD_RESPONSE";
    case SD_CARD_STATUS_UNSUPPORTED: return "UNSUPPORTED";
    default: return "UNKNOWN";
    }
}

const char *sd_card_type_string(sd_card_type_t type)
{
    switch (type) {
    case SD_CARD_TYPE_SDHC_SDXC: return "SDHC_SDXC";
    case SD_CARD_TYPE_SDSC: return "SDSC";
    case SD_CARD_TYPE_UNKNOWN:
    default: return "UNKNOWN";
    }
}
