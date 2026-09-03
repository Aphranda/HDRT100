#ifndef SYNC_IO_PERSONA_MANAGER_H
#define SYNC_IO_PERSONA_MANAGER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sync_io_persona_resources.h"

#define SYNC_IO_PERSONA_MANAGER_MAX_ACTIVE \
    (SYNC_IO_PERSONA_ID_COUNT - 1u)
#define SYNC_IO_PERSONA_MANAGER_OWNER "sync_io.persona_manager"

typedef enum {
    SYNC_IO_PERSONA_MANAGER_STATE_STOPPED = 0u,
    SYNC_IO_PERSONA_MANAGER_STATE_CLAIMED,
    SYNC_IO_PERSONA_MANAGER_STATE_LOADED,
    SYNC_IO_PERSONA_MANAGER_STATE_ARMED,
    SYNC_IO_PERSONA_MANAGER_STATE_RUNNING,
} sync_io_persona_manager_state_t;

typedef enum {
    SYNC_IO_PERSONA_MANAGER_ERROR_NONE = 0u,
    SYNC_IO_PERSONA_MANAGER_ERROR_NOT_INITIALIZED,
    SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_DESCRIPTOR,
    SYNC_IO_PERSONA_MANAGER_ERROR_NO_SLOT,
    SYNC_IO_PERSONA_MANAGER_ERROR_RESOURCE_CONFLICT,
    SYNC_IO_PERSONA_MANAGER_ERROR_RESOURCE_ARBITER,
    SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_HANDLE,
    SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_STATE,
    SYNC_IO_PERSONA_MANAGER_ERROR_LOAD,
    SYNC_IO_PERSONA_MANAGER_ERROR_ARM,
    SYNC_IO_PERSONA_MANAGER_ERROR_START,
} sync_io_persona_manager_error_t;

typedef struct {
    bool (*load)(void *context,
                 const sync_io_persona_descriptor_t *descriptor,
                 uint32_t dma_channel_mask);
    bool (*arm)(void *context,
                const sync_io_persona_descriptor_t *descriptor,
                uint32_t dma_channel_mask);
    bool (*start)(void *context,
                  const sync_io_persona_descriptor_t *descriptor,
                  uint32_t dma_channel_mask);
    void (*stop)(void *context,
                 const sync_io_persona_descriptor_t *descriptor,
                 uint32_t dma_channel_mask);
    /* Cleanup must be idempotent and safe after a partial load/arm/start. */
    void (*cleanup)(void *context,
                    const sync_io_persona_descriptor_t *descriptor,
                    uint32_t dma_channel_mask);
} sync_io_persona_manager_hooks_t;

typedef struct {
    uint8_t slot;
    uint8_t reserved[3];
    uint32_t generation;
} sync_io_persona_manager_handle_t;

typedef struct {
    bool active;
    uint8_t slot;
    uint8_t reserved[2];
    sync_io_persona_id_t id;
    sync_io_persona_manager_state_t state;
    uint32_t generation;
    uint32_t dma_channel_mask;
} sync_io_persona_manager_lease_t;

typedef struct {
    bool initialized;
    uint8_t active_count;
    uint8_t reserved[2];
    uint32_t used_sm_mask;
    uint32_t used_gpio_write_mask;
    uint32_t used_fifo_sm_mask;
    uint32_t used_dreq_sm_mask;
    uint32_t used_dma_channel_mask;
    uint32_t used_irq_mask;
    uint32_t used_workspace_mask;
    uint32_t instruction_words_total;
    uint32_t pio_resource;
    uint32_t last_error;
    uint32_t last_conflict_mask;
    sync_io_persona_manager_lease_t leases[SYNC_IO_PERSONA_MANAGER_MAX_ACTIVE];
} sync_io_persona_manager_snapshot_t;

typedef struct {
    bool initialized;
    bool pio_resource_held;
    uint8_t active_count;
    uint8_t reserved;
    uint32_t generation;
    uint32_t used_sm_mask;
    uint32_t used_gpio_write_mask;
    uint32_t used_fifo_sm_mask;
    uint32_t used_dreq_sm_mask;
    uint32_t used_dma_channel_mask;
    uint32_t used_irq_mask;
    uint32_t used_workspace_mask;
    uint32_t instruction_words_total;
    uint32_t pio_resource;
    sync_io_persona_manager_hooks_t hooks;
    void *hook_context;
    struct {
        bool active;
        uint8_t reserved[3];
        const sync_io_persona_descriptor_t *descriptor;
        sync_io_persona_manager_state_t state;
        uint32_t generation;
        uint32_t dma_channel_mask;
    } leases[SYNC_IO_PERSONA_MANAGER_MAX_ACTIVE];
    sync_io_persona_manager_error_t last_error;
    uint32_t last_conflict_mask;
} sync_io_persona_manager_t;

void sync_io_persona_manager_init(
    sync_io_persona_manager_t *manager,
    const sync_io_persona_manager_hooks_t *hooks,
    void *hook_context);
bool sync_io_persona_manager_deinit(sync_io_persona_manager_t *manager);

bool sync_io_persona_manager_claim(
    sync_io_persona_manager_t *manager,
    sync_io_persona_id_t id,
    sync_io_persona_manager_handle_t *handle,
    sync_io_persona_compatibility_t *compatibility);
bool sync_io_persona_manager_load(
    sync_io_persona_manager_t *manager,
    sync_io_persona_manager_handle_t *handle);
bool sync_io_persona_manager_arm(
    sync_io_persona_manager_t *manager,
    sync_io_persona_manager_handle_t *handle);
bool sync_io_persona_manager_start(
    sync_io_persona_manager_t *manager,
    sync_io_persona_manager_handle_t *handle);
bool sync_io_persona_manager_stop(
    sync_io_persona_manager_t *manager,
    sync_io_persona_manager_handle_t *handle);
bool sync_io_persona_manager_release(
    sync_io_persona_manager_t *manager,
    sync_io_persona_manager_handle_t *handle);

bool sync_io_persona_manager_handle_valid(
    const sync_io_persona_manager_t *manager,
    const sync_io_persona_manager_handle_t *handle);
void sync_io_persona_manager_get_snapshot(
    const sync_io_persona_manager_t *manager,
    sync_io_persona_manager_snapshot_t *snapshot);

#endif
