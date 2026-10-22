#include "refmem_action_registry.h"

#include <stddef.h>

static uint32_t mix_u32(uint32_t hash, uint32_t value)
{
    hash ^= value;
    hash *= 16777619u;
    return hash;
}

static uint32_t compute_registry_crc(const refmem_action_registry_t *registry)
{
    uint32_t hash = 2166136261u;
    hash = mix_u32(hash, registry->count);
    for (uint32_t i = 0u; i < registry->count; ++i) {
        const refmem_action_entry_t *e = &registry->entries[i];
        hash = mix_u32(hash, e->action_id);
        hash = mix_u32(hash, e->command_type);
        hash = mix_u32(hash, e->owner_id);
        hash = mix_u32(hash, e->required ? 1u : 0u);
        hash = mix_u32(hash, e->version);
        hash = mix_u32(hash, e->crc);
    }
    return hash;
}

static bool entry_id_exists(const refmem_action_registry_t *registry,
                            uint32_t action_id, uint32_t command_type)
{
    for (uint32_t i = 0u; i < registry->count; ++i) {
        const refmem_action_entry_t *e = &registry->entries[i];
        if (action_id != 0u && e->action_id == action_id) {
            return true;
        }
        if (command_type != 0u && e->command_type == command_type) {
            return true;
        }
    }
    return false;
}

void refmem_action_registry_init(refmem_action_registry_t *registry)
{
    if (registry == NULL) {
        return;
    }
    *registry = (refmem_action_registry_t){0};
}

refmem_action_result_t refmem_action_registry_register(
    refmem_action_registry_t *registry,
    const refmem_action_entry_t *entry)
{
    if (registry == NULL || entry == NULL || entry->action_id == 0u ||
        entry->owner_id == 0u) {
        return REFMEM_ACTION_BAD_ARGUMENT;
    }
    if (entry->required && entry->handler == NULL) {
        return REFMEM_ACTION_REQUIRED_HANDLER_MISSING;
    }
    if (entry_id_exists(registry, entry->action_id, entry->command_type)) {
        return REFMEM_ACTION_DUPLICATE;
    }
    if (registry->count >= REFMEM_ACTION_REGISTRY_MAX) {
        return REFMEM_ACTION_TABLE_FULL;
    }
    registry->entries[registry->count++] = *entry;
    registry->registry_crc = compute_registry_crc(registry);
    return REFMEM_ACTION_OK;
}

bool refmem_action_registry_required_handlers_ok(
    const refmem_action_registry_t *registry)
{
    if (registry == NULL) {
        return false;
    }
    for (uint32_t i = 0u; i < registry->count; ++i) {
        const refmem_action_entry_t *e = &registry->entries[i];
        if (e->required && e->handler == NULL) {
            return false;
        }
    }
    return true;
}

uint32_t refmem_action_registry_crc(const refmem_action_registry_t *registry)
{
    return registry == NULL ? 0u : registry->registry_crc;
}

void refmem_action_dispatcher_init(refmem_action_dispatcher_t *dispatcher)
{
    if (dispatcher == NULL) {
        return;
    }
    *dispatcher = (refmem_action_dispatcher_t){0};
}

static const refmem_action_entry_t *find_entry(
    const refmem_action_registry_t *registry,
    refmem_action_source_t source, uint32_t id)
{
    for (uint32_t i = 0u; i < registry->count; ++i) {
        const refmem_action_entry_t *e = &registry->entries[i];
        if (source == REFMEM_ACTION_SOURCE_EVENT && e->action_id == id) {
            return e;
        }
        if (source == REFMEM_ACTION_SOURCE_COMMAND && e->command_type == id) {
            return e;
        }
    }
    return NULL;
}

refmem_action_result_t refmem_action_dispatcher_post(
    refmem_action_dispatcher_t *dispatcher,
    const refmem_action_registry_t *registry,
    refmem_action_source_t source,
    uint32_t id,
    const refmem_action_request_t *request)
{
    const refmem_action_entry_t *entry;

    if (dispatcher == NULL || registry == NULL || request == NULL || id == 0u) {
        return REFMEM_ACTION_BAD_ARGUMENT;
    }
    if (source != REFMEM_ACTION_SOURCE_EVENT &&
        source != REFMEM_ACTION_SOURCE_COMMAND) {
        return REFMEM_ACTION_BAD_ARGUMENT;
    }
    entry = find_entry(registry, source, id);
    if (entry == NULL) {
        return REFMEM_ACTION_UNHANDLED;
    }
    if (entry->required && entry->handler == NULL) {
        return REFMEM_ACTION_REQUIRED_HANDLER_MISSING;
    }
    if (dispatcher->count >= REFMEM_ACTION_QUEUE_DEPTH) {
        dispatcher->dropped++;
        return REFMEM_ACTION_QUEUE_FULL;
    }
    dispatcher->queue[dispatcher->tail] = *request;
    if (source == REFMEM_ACTION_SOURCE_EVENT) {
        dispatcher->queue[dispatcher->tail].action_id = entry->action_id;
    } else {
        dispatcher->queue[dispatcher->tail].command_type = entry->command_type;
    }
    dispatcher->tail = (dispatcher->tail + 1u) % REFMEM_ACTION_QUEUE_DEPTH;
    dispatcher->count++;
    return REFMEM_ACTION_OK;
}

refmem_action_result_t refmem_action_dispatcher_pop(
    refmem_action_dispatcher_t *dispatcher,
    refmem_action_request_t *request)
{
    if (dispatcher == NULL || request == NULL) {
        return REFMEM_ACTION_BAD_ARGUMENT;
    }
    if (dispatcher->count == 0u) {
        return REFMEM_ACTION_NOT_FOUND;
    }
    *request = dispatcher->queue[dispatcher->head];
    dispatcher->head = (dispatcher->head + 1u) % REFMEM_ACTION_QUEUE_DEPTH;
    dispatcher->count--;
    return REFMEM_ACTION_OK;
}

uint32_t refmem_action_dispatcher_depth(
    const refmem_action_dispatcher_t *dispatcher)
{
    return dispatcher == NULL ? 0u : dispatcher->count;
}
