#include "storage_manager.h"

#include <stdio.h>
#include <string.h>

#include "board_config.h"
#include "fatfs_port.h"
#include "resource_arbiter.h"
#include "pico/time.h"

#define STORAGE_MANAGER_SD_INIT_BAUD_HZ 400000u
#define STORAGE_MANAGER_SD_RUN_BAUD_HZ 12500000u
#define STORAGE_MANAGER_ERROR_NONE 0u
#define STORAGE_MANAGER_ERROR_RESOURCE_BUSY 1u
#define STORAGE_MANAGER_ERROR_CARD 2u
#define STORAGE_MANAGER_ERROR_NO_FS 3u
#define STORAGE_MANAGER_ERROR_PATH 4u

static storage_manager_vector_t s_storage_vector;

static uint32_t storage_now_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}

bool storage_manager_init(void)
{
    memset(&s_storage_vector, 0, sizeof(s_storage_vector));
    s_storage_vector.state = STORAGE_MANAGER_STATE_IDLE;
    s_storage_vector.fatfs_available = fatfs_port_is_available();
    s_storage_vector.card_status = SD_CARD_STATUS_NOT_INITIALIZED;
    return true;
}

bool storage_manager_probe(void)
{
    if (s_storage_vector.state == STORAGE_MANAGER_STATE_UNINITIALIZED) {
        return false;
    }
    if (s_storage_vector.state == STORAGE_MANAGER_STATE_CARD_READY &&
        s_storage_vector.card_present &&
        s_storage_vector.fs_mounted &&
        s_storage_vector.card_status == SD_CARD_STATUS_OK) {
        return true;
    }

    if (!resource_arbiter_acquire(RESOURCE_ARBITER_RESOURCE_SPI0 |
                                  RESOURCE_ARBITER_RESOURCE_SD)) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    const sd_card_config_t config = {
        .spi = BOARD_SD_SPI_PORT,
        .sck_pin = BOARD_SD_SPI_CLK_PIN,
        .mosi_pin = BOARD_SD_SPI_MOSI_PIN,
        .miso_pin = BOARD_SD_SPI_MISO_PIN,
        .cs_pin = BOARD_SD_SPI_CS_PIN,
        .init_baud_hz = STORAGE_MANAGER_SD_INIT_BAUD_HZ,
        .run_baud_hz = STORAGE_MANAGER_SD_RUN_BAUD_HZ,
    };

    sd_card_info_t info;
    sd_card_status_t status = SD_CARD_STATUS_NOT_INITIALIZED;
    if (sd_card_init(&config)) {
        status = sd_card_probe(&info);
    } else {
        memset(&info, 0, sizeof(info));
        status = SD_CARD_STATUS_NOT_INITIALIZED;
    }
    resource_arbiter_release(RESOURCE_ARBITER_RESOURCE_SPI0 |
                             RESOURCE_ARBITER_RESOURCE_SD);

    s_storage_vector.probe_count++;
    s_storage_vector.last_probe_ms = storage_now_ms();
    s_storage_vector.card_status = status;
    s_storage_vector.card_present = info.present;
    s_storage_vector.card_type = info.type;
    s_storage_vector.high_capacity = info.high_capacity;
    s_storage_vector.block_count = info.block_count;
    s_storage_vector.capacity_kib = info.capacity_kib;
    s_storage_vector.fatfs_available = fatfs_port_is_available();
    s_storage_vector.fs_mounted = false;

    if (status != SD_CARD_STATUS_OK) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_NO_CARD;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_CARD;
        return false;
    }

    if (!s_storage_vector.fatfs_available) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_NO_FILESYSTEM;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NO_FS;
        return true;
    }

    const fatfs_port_status_t mount_status = fatfs_port_mount();
    s_storage_vector.fs_mounted = mount_status == FATFS_PORT_STATUS_OK;
    s_storage_vector.state = s_storage_vector.fs_mounted ?
                                 STORAGE_MANAGER_STATE_CARD_READY :
                                 STORAGE_MANAGER_STATE_NO_FILESYSTEM;
    s_storage_vector.storage_error = s_storage_vector.fs_mounted ?
                                         STORAGE_MANAGER_ERROR_NONE :
                                         STORAGE_MANAGER_ERROR_NO_FS;
    return s_storage_vector.fs_mounted;
}

bool storage_manager_catalog(const char *path, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0u) {
        return false;
    }
    buffer[0] = '\0';

    if (!storage_manager_probe()) {
        (void)snprintf(buffer, buffer_size, "%s", storage_manager_state_string(s_storage_vector.state));
        return false;
    }

    if (!resource_arbiter_acquire(RESOURCE_ARBITER_RESOURCE_SPI0 |
                                  RESOURCE_ARBITER_RESOURCE_SD)) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        (void)snprintf(buffer, buffer_size, "RESOURCE_BUSY");
        return false;
    }

    const fatfs_port_status_t status = fatfs_port_catalog(path, buffer, buffer_size);
    resource_arbiter_release(RESOURCE_ARBITER_RESOURCE_SPI0 |
                             RESOURCE_ARBITER_RESOURCE_SD);

    if (status != FATFS_PORT_STATUS_OK) {
        if (status == FATFS_PORT_STATUS_PATH_NOT_FOUND) {
            s_storage_vector.fs_mounted = true;
            s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_ERROR;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        } else {
            s_storage_vector.fs_mounted = false;
            s_storage_vector.state = STORAGE_MANAGER_STATE_NO_FILESYSTEM;
            s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NO_FS;
        }
        return false;
    }

    s_storage_vector.fs_mounted = true;
    s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
    s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
    return true;
}

void storage_manager_service(uint32_t budget_us)
{
    (void)budget_us;
}

void storage_manager_get_vector(storage_manager_vector_t *vector)
{
    if (vector == NULL) {
        return;
    }
    *vector = s_storage_vector;
}

const char *storage_manager_state_string(storage_manager_state_t state)
{
    switch (state) {
    case STORAGE_MANAGER_STATE_UNINITIALIZED: return "UNINITIALIZED";
    case STORAGE_MANAGER_STATE_IDLE: return "IDLE";
    case STORAGE_MANAGER_STATE_CARD_READY: return "CARD_READY";
    case STORAGE_MANAGER_STATE_NO_CARD: return "NO_CARD";
    case STORAGE_MANAGER_STATE_NO_FILESYSTEM: return "NO_FS";
    case STORAGE_MANAGER_STATE_PATH_ERROR: return "NO_PATH";
    case STORAGE_MANAGER_STATE_FAILED: return "FAILED";
    default: return "UNKNOWN";
    }
}
