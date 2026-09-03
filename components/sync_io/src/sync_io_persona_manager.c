#include "sync_io_persona_manager.h"

#include <string.h>

#include "resource_arbiter.h"

#define SYNC_IO_PERSONA_MANAGER_INVALID_SLOT UINT8_MAX

static uint32_t sync_io_persona_manager_popcount(uint32_t value)
{
    uint32_t count = 0u;
    while (value != 0u) {
        count += value & 1u;
        value >>= 1u;
    }
    return count;
}

static uint32_t sync_io_persona_manager_pio_resource(uint8_t pio_block_id)
{
    switch (pio_block_id) {
    case 0u:
        return RESOURCE_ARBITER_RESOURCE_PIO0;
    case 1u:
        return RESOURCE_ARBITER_RESOURCE_PIO1;
    case 2u:
        return RESOURCE_ARBITER_RESOURCE_PIO2;
    default:
        return 0u;
    }
}

static void sync_io_persona_manager_clear_handle(
    sync_io_persona_manager_handle_t *handle)
{
    if (handle != NULL) {
        memset(handle, 0, sizeof(*handle));
        handle->slot = SYNC_IO_PERSONA_MANAGER_INVALID_SLOT;
    }
}

static void sync_io_persona_manager_set_error(
    sync_io_persona_manager_t *manager,
    sync_io_persona_manager_error_t error,
    uint32_t conflict_mask)
{
    manager->last_error = error;
    manager->last_conflict_mask = conflict_mask;
}

static bool sync_io_persona_manager_slot_for_handle(
    sync_io_persona_manager_t *manager,
    const sync_io_persona_manager_handle_t *handle,
    size_t *slot_index)
{
    if (manager == NULL || handle == NULL || slot_index == NULL ||
        !manager->initialized ||
        handle->slot >= SYNC_IO_PERSONA_MANAGER_MAX_ACTIVE) {
        return false;
    }

    const size_t index = handle->slot;
    if (!manager->leases[index].active ||
        manager->leases[index].generation != handle->generation) {
        return false;
    }
    *slot_index = index;
    return true;
}

static bool sync_io_persona_manager_conflict_with_used(
    const sync_io_persona_manager_t *manager,
    const sync_io_persona_descriptor_t *descriptor,
    sync_io_persona_compatibility_t *compatibility,
    uint32_t *dma_channel_mask)
{
    sync_io_persona_compatibility_t local = {0};
    const uint32_t pio_resource =
        sync_io_persona_manager_pio_resource(descriptor->pio_block_id);
    if (pio_resource == 0u ||
        (manager->pio_resource != 0u &&
         manager->pio_resource != pio_resource)) {
        local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_INVALID_DESCRIPTOR;
    }

    if ((descriptor->flags & SYNC_IO_PERSONA_FLAG_EXCLUSIVE_PIO) != 0u ||
        manager->active_count != 0u) {
        for (size_t index = 0u;
             index < SYNC_IO_PERSONA_MANAGER_MAX_ACTIVE;
             ++index) {
            if (manager->leases[index].active &&
                (descriptor->flags & SYNC_IO_PERSONA_FLAG_EXCLUSIVE_PIO) != 0u) {
                local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_EXCLUSIVE_PIO;
                break;
            }
            if (manager->leases[index].active &&
                (manager->leases[index].descriptor->flags &
                 SYNC_IO_PERSONA_FLAG_EXCLUSIVE_PIO) != 0u) {
                local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_EXCLUSIVE_PIO;
                break;
            }
        }
    }

    local.sm_conflict_mask = manager->used_sm_mask & descriptor->sm_mask;
    local.gpio_conflict_mask =
        manager->used_gpio_write_mask & descriptor->gpio_write_mask;
    local.fifo_conflict_mask =
        manager->used_fifo_sm_mask &
        (descriptor->rx_fifo_sm_mask | descriptor->tx_fifo_sm_mask);
    local.dreq_conflict_mask =
        manager->used_dreq_sm_mask &
        (descriptor->rx_dreq_sm_mask | descriptor->tx_dreq_sm_mask);
    local.irq_conflict_mask = manager->used_irq_mask & descriptor->irq_mask;
    local.workspace_conflict_mask =
        manager->used_workspace_mask & descriptor->workspace_mask;
    local.instruction_words_total =
        manager->instruction_words_total + descriptor->instruction_words;
    if (local.sm_conflict_mask != 0u) {
        local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_SM;
    }
    if (local.gpio_conflict_mask != 0u) {
        local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_GPIO;
    }
    if (local.fifo_conflict_mask != 0u) {
        local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_FIFO;
    }
    if (local.dreq_conflict_mask != 0u) {
        local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_DREQ;
    }
    if (local.irq_conflict_mask != 0u) {
        local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_IRQ;
    }
    if (local.workspace_conflict_mask != 0u) {
        local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_WORKSPACE;
    }
    if (local.instruction_words_total >
        SYNC_IO_PERSONA_PIO_INSTRUCTION_CAPACITY) {
        local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_INSTRUCTION_SPACE;
    }

    const uint32_t available_dma =
        descriptor->dma_channel_mask & ~manager->used_dma_channel_mask;
    if (descriptor->dma_channel_count != 0u &&
        sync_io_persona_manager_popcount(available_dma) <
            descriptor->dma_channel_count) {
        local.dma_conflict_mask =
            descriptor->dma_channel_mask & manager->used_dma_channel_mask;
        local.conflict_mask |= SYNC_IO_PERSONA_CONFLICT_DMA;
    }
    if (dma_channel_mask != NULL) {
        uint32_t selected = 0u;
        uint32_t remaining = descriptor->dma_channel_count;
        for (uint32_t bit = 0u;
             bit < SYNC_IO_PERSONA_DMA_CHANNEL_COUNT && remaining != 0u;
             ++bit) {
            const uint32_t mask = 1u << bit;
            if ((available_dma & mask) != 0u) {
                selected |= mask;
                --remaining;
            }
        }
        *dma_channel_mask = selected;
    }

    local.compatible = local.conflict_mask == SYNC_IO_PERSONA_CONFLICT_NONE;
    if (compatibility != NULL) {
        *compatibility = local;
    }
    return local.compatible;
}

static void sync_io_persona_manager_add_used(
    sync_io_persona_manager_t *manager,
    const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    manager->used_sm_mask |= descriptor->sm_mask;
    manager->used_gpio_write_mask |= descriptor->gpio_write_mask;
    manager->used_fifo_sm_mask |= descriptor->rx_fifo_sm_mask |
                                  descriptor->tx_fifo_sm_mask;
    manager->used_dreq_sm_mask |= descriptor->rx_dreq_sm_mask |
                                  descriptor->tx_dreq_sm_mask;
    manager->used_dma_channel_mask |= dma_channel_mask;
    manager->used_irq_mask |= descriptor->irq_mask;
    manager->used_workspace_mask |= descriptor->workspace_mask;
    manager->instruction_words_total += descriptor->instruction_words;
}

static void sync_io_persona_manager_rebuild_used(
    sync_io_persona_manager_t *manager)
{
    manager->used_sm_mask = 0u;
    manager->used_gpio_write_mask = 0u;
    manager->used_fifo_sm_mask = 0u;
    manager->used_dreq_sm_mask = 0u;
    manager->used_dma_channel_mask = 0u;
    manager->used_irq_mask = 0u;
    manager->used_workspace_mask = 0u;
    manager->instruction_words_total = 0u;
    for (size_t index = 0u;
         index < SYNC_IO_PERSONA_MANAGER_MAX_ACTIVE;
         ++index) {
        if (manager->leases[index].active) {
            sync_io_persona_manager_add_used(
                manager,
                manager->leases[index].descriptor,
                manager->leases[index].dma_channel_mask);
        }
    }
}

static void sync_io_persona_manager_cleanup_slot(
    sync_io_persona_manager_t *manager,
    size_t slot_index)
{
    const uint32_t dma_channel_mask =
        manager->leases[slot_index].dma_channel_mask;
    const sync_io_persona_descriptor_t *descriptor =
        manager->leases[slot_index].descriptor;
    if (descriptor != NULL && manager->hooks.cleanup != NULL) {
        manager->hooks.cleanup(manager->hook_context,
                               descriptor,
                               dma_channel_mask);
    }

    memset(&manager->leases[slot_index], 0,
           sizeof(manager->leases[slot_index]));
    manager->active_count--;
    sync_io_persona_manager_rebuild_used(manager);
    if (manager->active_count == 0u && manager->pio_resource_held) {
        resource_arbiter_release_owned(manager->pio_resource,
                                       SYNC_IO_PERSONA_MANAGER_OWNER);
        manager->pio_resource_held = false;
        manager->pio_resource = 0u;
    }
}

void sync_io_persona_manager_init(
    sync_io_persona_manager_t *manager,
    const sync_io_persona_manager_hooks_t *hooks,
    void *hook_context)
{
    if (manager == NULL) {
        return;
    }
    memset(manager, 0, sizeof(*manager));
    manager->initialized = true;
    if (hooks != NULL) {
        manager->hooks = *hooks;
    }
    manager->hook_context = hook_context;
    for (size_t index = 0u;
         index < SYNC_IO_PERSONA_MANAGER_MAX_ACTIVE;
         ++index) {
        manager->leases[index].state =
            SYNC_IO_PERSONA_MANAGER_STATE_STOPPED;
    }
}

bool sync_io_persona_manager_deinit(sync_io_persona_manager_t *manager)
{
    if (manager == NULL || !manager->initialized ||
        manager->active_count != 0u) {
        return false;
    }
    if (manager->pio_resource_held) {
        resource_arbiter_release_owned(manager->pio_resource,
                                       SYNC_IO_PERSONA_MANAGER_OWNER);
    }
    memset(manager, 0, sizeof(*manager));
    return true;
}

bool sync_io_persona_manager_claim(
    sync_io_persona_manager_t *manager,
    sync_io_persona_id_t id,
    sync_io_persona_manager_handle_t *handle,
    sync_io_persona_compatibility_t *compatibility)
{
    sync_io_persona_manager_clear_handle(handle);
    if (manager == NULL || !manager->initialized) {
        if (manager != NULL) {
            sync_io_persona_manager_set_error(
                manager, SYNC_IO_PERSONA_MANAGER_ERROR_NOT_INITIALIZED, 0u);
        }
        return false;
    }
    if (handle == NULL) {
        sync_io_persona_manager_set_error(
            manager, SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_HANDLE, 0u);
        return false;
    }

    const sync_io_persona_descriptor_t *descriptor =
        sync_io_persona_descriptor(id);
    if (!sync_io_persona_descriptor_valid(descriptor)) {
        sync_io_persona_manager_set_error(
            manager, SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_DESCRIPTOR,
            SYNC_IO_PERSONA_CONFLICT_INVALID_DESCRIPTOR);
        if (compatibility != NULL) {
            memset(compatibility, 0, sizeof(*compatibility));
            compatibility->conflict_mask =
                SYNC_IO_PERSONA_CONFLICT_INVALID_DESCRIPTOR;
        }
        return false;
    }

    size_t slot_index = SYNC_IO_PERSONA_MANAGER_MAX_ACTIVE;
    for (size_t index = 0u;
         index < SYNC_IO_PERSONA_MANAGER_MAX_ACTIVE;
         ++index) {
        if (!manager->leases[index].active) {
            slot_index = index;
            break;
        }
    }
    if (slot_index == SYNC_IO_PERSONA_MANAGER_MAX_ACTIVE) {
        sync_io_persona_manager_set_error(
            manager, SYNC_IO_PERSONA_MANAGER_ERROR_NO_SLOT, 0u);
        return false;
    }

    uint32_t dma_channel_mask = 0u;
    sync_io_persona_compatibility_t computed_compatibility;
    if (!sync_io_persona_manager_conflict_with_used(
            manager,
            descriptor,
            compatibility != NULL ? compatibility : &computed_compatibility,
            &dma_channel_mask)) {
        const uint32_t conflict_mask = compatibility != NULL
            ? compatibility->conflict_mask
            : computed_compatibility.conflict_mask;
        sync_io_persona_manager_set_error(
            manager, SYNC_IO_PERSONA_MANAGER_ERROR_RESOURCE_CONFLICT,
            conflict_mask);
        return false;
    }

    const uint32_t pio_resource =
        sync_io_persona_manager_pio_resource(descriptor->pio_block_id);
    if (manager->active_count == 0u) {
        if (!resource_arbiter_acquire_owned(
                pio_resource, SYNC_IO_PERSONA_MANAGER_OWNER)) {
            sync_io_persona_manager_set_error(
                manager, SYNC_IO_PERSONA_MANAGER_ERROR_RESOURCE_ARBITER,
                pio_resource);
            return false;
        }
        manager->pio_resource = pio_resource;
        manager->pio_resource_held = true;
    }

    manager->leases[slot_index].active = true;
    manager->leases[slot_index].descriptor = descriptor;
    manager->leases[slot_index].state =
        SYNC_IO_PERSONA_MANAGER_STATE_CLAIMED;
    manager->generation++;
    if (manager->generation == 0u) {
        manager->generation = 1u;
    }
    manager->leases[slot_index].generation = manager->generation;
    manager->leases[slot_index].dma_channel_mask = dma_channel_mask;
    manager->active_count++;
    sync_io_persona_manager_add_used(manager, descriptor, dma_channel_mask);
    manager->last_error = SYNC_IO_PERSONA_MANAGER_ERROR_NONE;
    manager->last_conflict_mask = SYNC_IO_PERSONA_CONFLICT_NONE;

    if (handle != NULL) {
        handle->slot = (uint8_t)slot_index;
        handle->generation = manager->generation;
    }
    return true;
}

bool sync_io_persona_manager_load(
    sync_io_persona_manager_t *manager,
    sync_io_persona_manager_handle_t *handle)
{
    size_t slot_index = 0u;
    if (!sync_io_persona_manager_slot_for_handle(manager, handle,
                                                  &slot_index)) {
        if (manager != NULL) {
            sync_io_persona_manager_set_error(
                manager, SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_HANDLE, 0u);
        }
        return false;
    }
    if (manager->leases[slot_index].state !=
        SYNC_IO_PERSONA_MANAGER_STATE_CLAIMED) {
        sync_io_persona_manager_set_error(
            manager, SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_STATE, 0u);
        return false;
    }
    const sync_io_persona_descriptor_t *descriptor =
        manager->leases[slot_index].descriptor;
    const uint32_t dma_channel_mask =
        manager->leases[slot_index].dma_channel_mask;
    if (manager->hooks.load != NULL &&
        !manager->hooks.load(manager->hook_context,
                             descriptor,
                             dma_channel_mask)) {
        sync_io_persona_manager_set_error(
            manager, SYNC_IO_PERSONA_MANAGER_ERROR_LOAD, 0u);
        sync_io_persona_manager_cleanup_slot(manager, slot_index);
        sync_io_persona_manager_clear_handle(handle);
        return false;
    }
    manager->leases[slot_index].state =
        SYNC_IO_PERSONA_MANAGER_STATE_LOADED;
    manager->last_error = SYNC_IO_PERSONA_MANAGER_ERROR_NONE;
    return true;
}

bool sync_io_persona_manager_arm(
    sync_io_persona_manager_t *manager,
    sync_io_persona_manager_handle_t *handle)
{
    size_t slot_index = 0u;
    if (!sync_io_persona_manager_slot_for_handle(manager, handle,
                                                  &slot_index)) {
        if (manager != NULL) {
            sync_io_persona_manager_set_error(
                manager, SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_HANDLE, 0u);
        }
        return false;
    }
    if (manager->leases[slot_index].state !=
        SYNC_IO_PERSONA_MANAGER_STATE_LOADED) {
        sync_io_persona_manager_set_error(
            manager, SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_STATE, 0u);
        return false;
    }
    const sync_io_persona_descriptor_t *descriptor =
        manager->leases[slot_index].descriptor;
    const uint32_t dma_channel_mask =
        manager->leases[slot_index].dma_channel_mask;
    if (manager->hooks.arm != NULL &&
        !manager->hooks.arm(manager->hook_context,
                            descriptor,
                            dma_channel_mask)) {
        sync_io_persona_manager_set_error(
            manager, SYNC_IO_PERSONA_MANAGER_ERROR_ARM, 0u);
        sync_io_persona_manager_cleanup_slot(manager, slot_index);
        sync_io_persona_manager_clear_handle(handle);
        return false;
    }
    manager->leases[slot_index].state =
        SYNC_IO_PERSONA_MANAGER_STATE_ARMED;
    manager->last_error = SYNC_IO_PERSONA_MANAGER_ERROR_NONE;
    return true;
}

bool sync_io_persona_manager_start(
    sync_io_persona_manager_t *manager,
    sync_io_persona_manager_handle_t *handle)
{
    size_t slot_index = 0u;
    if (!sync_io_persona_manager_slot_for_handle(manager, handle,
                                                  &slot_index)) {
        if (manager != NULL) {
            sync_io_persona_manager_set_error(
                manager, SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_HANDLE, 0u);
        }
        return false;
    }
    if (manager->leases[slot_index].state !=
        SYNC_IO_PERSONA_MANAGER_STATE_ARMED) {
        sync_io_persona_manager_set_error(
            manager, SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_STATE, 0u);
        return false;
    }
    const sync_io_persona_descriptor_t *descriptor =
        manager->leases[slot_index].descriptor;
    const uint32_t dma_channel_mask =
        manager->leases[slot_index].dma_channel_mask;
    if (manager->hooks.start != NULL &&
        !manager->hooks.start(manager->hook_context,
                              descriptor,
                              dma_channel_mask)) {
        sync_io_persona_manager_set_error(
            manager, SYNC_IO_PERSONA_MANAGER_ERROR_START, 0u);
        sync_io_persona_manager_cleanup_slot(manager, slot_index);
        sync_io_persona_manager_clear_handle(handle);
        return false;
    }
    manager->leases[slot_index].state =
        SYNC_IO_PERSONA_MANAGER_STATE_RUNNING;
    manager->last_error = SYNC_IO_PERSONA_MANAGER_ERROR_NONE;
    return true;
}

bool sync_io_persona_manager_stop(
    sync_io_persona_manager_t *manager,
    sync_io_persona_manager_handle_t *handle)
{
    size_t slot_index = 0u;
    if (!sync_io_persona_manager_slot_for_handle(manager, handle,
                                                  &slot_index)) {
        if (manager != NULL) {
            sync_io_persona_manager_set_error(
                manager, SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_HANDLE, 0u);
        }
        return false;
    }
    const sync_io_persona_manager_state_t state =
        manager->leases[slot_index].state;
    if (state == SYNC_IO_PERSONA_MANAGER_STATE_CLAIMED ||
        state == SYNC_IO_PERSONA_MANAGER_STATE_LOADED) {
        return true;
    }
    if (state != SYNC_IO_PERSONA_MANAGER_STATE_ARMED &&
        state != SYNC_IO_PERSONA_MANAGER_STATE_RUNNING) {
        sync_io_persona_manager_set_error(
            manager, SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_STATE, 0u);
        return false;
    }
    if (manager->hooks.stop != NULL) {
        manager->hooks.stop(manager->hook_context,
                            manager->leases[slot_index].descriptor,
                            manager->leases[slot_index].dma_channel_mask);
    }
    manager->leases[slot_index].state =
        SYNC_IO_PERSONA_MANAGER_STATE_LOADED;
    manager->last_error = SYNC_IO_PERSONA_MANAGER_ERROR_NONE;
    return true;
}

bool sync_io_persona_manager_release(
    sync_io_persona_manager_t *manager,
    sync_io_persona_manager_handle_t *handle)
{
    size_t slot_index = 0u;
    if (!sync_io_persona_manager_slot_for_handle(manager, handle,
                                                  &slot_index)) {
        if (manager != NULL) {
            sync_io_persona_manager_set_error(
                manager, SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_HANDLE, 0u);
        }
        return false;
    }
    const sync_io_persona_manager_state_t state =
        manager->leases[slot_index].state;
    if (state == SYNC_IO_PERSONA_MANAGER_STATE_ARMED ||
        state == SYNC_IO_PERSONA_MANAGER_STATE_RUNNING) {
        if (!sync_io_persona_manager_stop(manager, handle)) {
            return false;
        }
    }
    if (manager->leases[slot_index].state !=
            SYNC_IO_PERSONA_MANAGER_STATE_CLAIMED &&
        manager->leases[slot_index].state !=
            SYNC_IO_PERSONA_MANAGER_STATE_LOADED) {
        sync_io_persona_manager_set_error(
            manager, SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_STATE, 0u);
        return false;
    }
    sync_io_persona_manager_cleanup_slot(manager, slot_index);
    sync_io_persona_manager_clear_handle(handle);
    manager->last_error = SYNC_IO_PERSONA_MANAGER_ERROR_NONE;
    return true;
}

bool sync_io_persona_manager_handle_valid(
    const sync_io_persona_manager_t *manager,
    const sync_io_persona_manager_handle_t *handle)
{
    size_t slot_index = 0u;
    return sync_io_persona_manager_slot_for_handle(
        (sync_io_persona_manager_t *)manager, handle, &slot_index);
}

void sync_io_persona_manager_get_snapshot(
    const sync_io_persona_manager_t *manager,
    sync_io_persona_manager_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (manager == NULL) {
        return;
    }
    snapshot->initialized = manager->initialized;
    snapshot->active_count = manager->active_count;
    snapshot->used_sm_mask = manager->used_sm_mask;
    snapshot->used_gpio_write_mask = manager->used_gpio_write_mask;
    snapshot->used_fifo_sm_mask = manager->used_fifo_sm_mask;
    snapshot->used_dreq_sm_mask = manager->used_dreq_sm_mask;
    snapshot->used_dma_channel_mask = manager->used_dma_channel_mask;
    snapshot->used_irq_mask = manager->used_irq_mask;
    snapshot->used_workspace_mask = manager->used_workspace_mask;
    snapshot->instruction_words_total = manager->instruction_words_total;
    snapshot->pio_resource = manager->pio_resource;
    snapshot->last_error = (uint32_t)manager->last_error;
    snapshot->last_conflict_mask = manager->last_conflict_mask;
    for (size_t index = 0u;
         index < SYNC_IO_PERSONA_MANAGER_MAX_ACTIVE;
         ++index) {
        snapshot->leases[index].active = manager->leases[index].active;
        snapshot->leases[index].slot = (uint8_t)index;
        snapshot->leases[index].id = manager->leases[index].descriptor == NULL
            ? SYNC_IO_PERSONA_ID_NONE
            : manager->leases[index].descriptor->id;
        snapshot->leases[index].state = manager->leases[index].state;
        snapshot->leases[index].generation = manager->leases[index].generation;
        snapshot->leases[index].dma_channel_mask =
            manager->leases[index].dma_channel_mask;
    }
}
