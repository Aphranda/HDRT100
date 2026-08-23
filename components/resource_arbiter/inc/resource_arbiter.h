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
    uint32_t last_conflict_resources;
    const char *last_conflict_owner;
    const char *last_conflict_holder;
    const char *resource_owners[32];
    bool trigger_capture_running;
    bool trigger_clock_running;
    bool calibration_training_active;
    bool tdma_clock_training_active;
} resource_arbiter_snapshot_t;

bool resource_arbiter_init(void);
void resource_arbiter_publish_trigger_activity(bool capture_running, bool clock_running);
void resource_arbiter_publish_calibration_training(bool active);
void resource_arbiter_publish_tdma_clock_training(bool active);
void resource_arbiter_publish_training_activity(bool calibration_active,
                                                 bool tdma_clock_training_active);
bool resource_arbiter_can_begin_ota(void);
/* Explicit maintenance admission owned by the system/control plane.  OTA AO
 * and FlashTransactionAO may consume the admission but must not create it. */
bool resource_arbiter_request_ota_admission(void);
void resource_arbiter_release_ota_admission(void);
bool resource_arbiter_ota_admission_active(void);
bool resource_arbiter_acquire(uint32_t resources);
bool resource_arbiter_acquire_owned(uint32_t resources, const char *owner);
void resource_arbiter_release(uint32_t resources);
void resource_arbiter_release_owned(uint32_t resources, const char *owner);
void resource_arbiter_get_snapshot(resource_arbiter_snapshot_t *snapshot);

#endif
