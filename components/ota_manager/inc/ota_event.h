#ifndef OTA_EVENT_H
#define OTA_EVENT_H

#include <stdint.h>

#define OTA_EVENT_MAX_DATA_SIZE 512u
#define OTA_BEGIN_FLAG_PACKAGE  0x00000001u

typedef enum {
    OTA_EVENT_BEGIN = 0,
    OTA_EVENT_DATA_BLOCK,
    OTA_EVENT_END,
    OTA_EVENT_ABORT,
    OTA_EVENT_VERIFY,
    OTA_EVENT_COMMIT,
    OTA_EVENT_BOOT,
    OTA_EVENT_TICK,
    OTA_EVENT_FLASH_JOB_DONE,
    OTA_EVENT_FLASH_JOB_FAILED,
    OTA_EVENT_BOOT_RESULT,
} ota_event_type_t;

typedef struct {
    uint32_t size;
    uint32_t crc32;
    uint32_t image_version;
    uint32_t flags;
} ota_begin_event_t;

typedef struct {
    const uint8_t *data;
    uint32_t length;
    uint32_t block_index;
} ota_data_event_t;

typedef struct {
    ota_event_type_t type;
    union {
        ota_begin_event_t begin;
        ota_data_event_t data;
        uint32_t value;
    } payload;
} ota_event_t;

#endif
