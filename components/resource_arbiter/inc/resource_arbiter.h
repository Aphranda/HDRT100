#ifndef RESOURCE_ARBITER_H
#define RESOURCE_ARBITER_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    RESOURCE_ARBITER_MODE_BOOT = 0,
    RESOURCE_ARBITER_MODE_RUN,
    RESOURCE_ARBITER_MODE_OTA,
    RESOURCE_ARBITER_MODE_FAULT,
} resource_arbiter_mode_t;

typedef enum {
    RESOURCE_ARBITER_RESOURCE_FLASH = 1u << 0,
    RESOURCE_ARBITER_RESOURCE_SPI0 = 1u << 1,
    RESOURCE_ARBITER_RESOURCE_USB = 1u << 2,
    RESOURCE_ARBITER_RESOURCE_PIO0 = 1u << 3,
    RESOURCE_ARBITER_RESOURCE_PIO1 = 1u << 4,
    RESOURCE_ARBITER_RESOURCE_PIO2 = 1u << 5,
    RESOURCE_ARBITER_RESOURCE_DMA = 1u << 6,
    RESOURCE_ARBITER_RESOURCE_LCD = 1u << 7,
    RESOURCE_ARBITER_RESOURCE_SD = 1u << 8,
    RESOURCE_ARBITER_RESOURCE_AUX = 1u << 9,
} resource_arbiter_resource_t;

typedef struct {
    resource_arbiter_mode_t mode;
    uint32_t active_resources;
    bool trigger_capture_running;
    bool trigger_clock_running;
} resource_arbiter_snapshot_t;

bool resource_arbiter_init(void);
void resource_arbiter_publish_trigger_activity(bool capture_running, bool clock_running);
bool resource_arbiter_can_begin_ota(void);
bool resource_arbiter_acquire(uint32_t resources);
void resource_arbiter_release(uint32_t resources);
void resource_arbiter_get_snapshot(resource_arbiter_snapshot_t *snapshot);

#endif
