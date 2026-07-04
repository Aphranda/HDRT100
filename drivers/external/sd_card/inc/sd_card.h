#ifndef SD_CARD_H
#define SD_CARD_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/spi.h"

typedef enum {
    SD_CARD_STATUS_OK = 0,
    SD_CARD_STATUS_NOT_INITIALIZED,
    SD_CARD_STATUS_TIMEOUT,
    SD_CARD_STATUS_BAD_RESPONSE,
    SD_CARD_STATUS_UNSUPPORTED,
} sd_card_status_t;

typedef enum {
    SD_CARD_TYPE_UNKNOWN = 0,
    SD_CARD_TYPE_SDHC_SDXC,
    SD_CARD_TYPE_SDSC,
} sd_card_type_t;

typedef struct {
    spi_inst_t *spi;
    uint32_t sck_pin;
    uint32_t mosi_pin;
    uint32_t miso_pin;
    uint32_t cs_pin;
    uint32_t init_baud_hz;
    uint32_t run_baud_hz;
} sd_card_config_t;

typedef struct {
    bool present;
    bool high_capacity;
    sd_card_type_t type;
    uint32_t block_count;
    uint32_t capacity_kib;
    sd_card_status_t status;
} sd_card_info_t;

bool sd_card_init(const sd_card_config_t *config);
sd_card_status_t sd_card_probe(sd_card_info_t *info);
sd_card_status_t sd_card_read_blocks(uint32_t sector, uint32_t count, uint8_t *buffer);
sd_card_status_t sd_card_write_blocks(uint32_t sector, uint32_t count, const uint8_t *buffer);
void sd_card_get_info(sd_card_info_t *info);
const char *sd_card_status_string(sd_card_status_t status);
const char *sd_card_type_string(sd_card_type_t type);

#endif
