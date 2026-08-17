#include "tdma_payload_registry.h"

#include <string.h>

static uint32_t tdma_payload_registry_load(const volatile uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void tdma_payload_registry_write_guard(volatile uint32_t *guard)
{
    (void)__atomic_add_fetch(guard, 1u, __ATOMIC_RELEASE);
}

static bool tdma_payload_registry_class_valid(uint32_t frame_class)
{
    return frame_class == TDMA_PAYLOAD_FRAME_CLASS_SHORT ||
           frame_class == TDMA_PAYLOAD_FRAME_CLASS_LONG;
}

static bool tdma_payload_registry_payload_allowed(uint32_t whitelist,
                                                  uint32_t payload_class)
{
    return payload_class != 0u && payload_class < 32u &&
           (whitelist == 0u ||
            (whitelist & TDMA_PAYLOAD_BIT(payload_class)) != 0u);
}

static bool tdma_payload_registry_binding_fits(
    const tdma_payload_binding_t *binding,
    uint32_t whitelist,
    uint32_t short_capacity,
    uint32_t long_capacity)
{
    if (binding == NULL ||
        !tdma_payload_registry_class_valid(binding->frame_class) ||
        !tdma_payload_registry_payload_allowed(whitelist,
                                               binding->payload_class)) {
        return false;
    }
    const uint32_t capacity =
        binding->frame_class == TDMA_PAYLOAD_FRAME_CLASS_SHORT
            ? short_capacity
            : long_capacity;
    return binding->max_payload_size != 0u &&
           binding->max_payload_size <= capacity;
}

static void tdma_payload_registry_note_result(
    tdma_payload_registry_t *registry,
    uint32_t payload_class,
    tdma_payload_registry_result_t result,
    bool rejected)
{
    tdma_payload_registry_write_guard(&registry->guard);
    registry->last_payload_class = payload_class;
    registry->last_result = (uint32_t)result;
    if (rejected) {
        registry->reject_count++;
    }
    tdma_payload_registry_write_guard(&registry->guard);
}

bool tdma_payload_registry_init(tdma_payload_registry_t *registry,
                                uint32_t short_frame_capacity,
                                uint32_t long_frame_capacity)
{
    if (registry == NULL || short_frame_capacity == 0u ||
        long_frame_capacity < short_frame_capacity) {
        return false;
    }

    memset(registry, 0, sizeof(*registry));
    registry->short_frame_capacity = short_frame_capacity;
    registry->long_frame_capacity = long_frame_capacity;
    registry->last_result = TDMA_PAYLOAD_REGISTRY_OK;
    return true;
}

bool tdma_payload_registry_configure(tdma_payload_registry_t *registry,
                                     uint32_t payload_whitelist_mask,
                                     uint32_t short_frame_capacity,
                                     uint32_t long_frame_capacity)
{
    if (registry == NULL || short_frame_capacity == 0u ||
        long_frame_capacity < short_frame_capacity) {
        return false;
    }

    for (uint32_t i = 0u; i < TDMA_PAYLOAD_REGISTRY_COUNT; i++) {
        const tdma_payload_binding_t *binding = &registry->binding[i];
        if (binding->used != 0u &&
            !tdma_payload_registry_binding_fits(binding,
                                                payload_whitelist_mask,
                                                short_frame_capacity,
                                                long_frame_capacity)) {
            tdma_payload_registry_note_result(
                registry,
                binding->payload_class,
                TDMA_PAYLOAD_REGISTRY_CLASS_REJECTED,
                true);
            return false;
        }
    }

    tdma_payload_registry_write_guard(&registry->guard);
    registry->payload_whitelist_mask = payload_whitelist_mask;
    registry->short_frame_capacity = short_frame_capacity;
    registry->long_frame_capacity = long_frame_capacity;
    registry->config_seq++;
    registry->last_result = TDMA_PAYLOAD_REGISTRY_OK;
    registry->last_payload_class = 0u;
    tdma_payload_registry_write_guard(&registry->guard);
    return true;
}

bool tdma_payload_registry_register(tdma_payload_registry_t *registry,
                                    const tdma_payload_binding_t *binding)
{
    if (registry == NULL || binding == NULL) {
        return false;
    }
    if (!tdma_payload_registry_binding_fits(
            binding,
            tdma_payload_registry_load(&registry->payload_whitelist_mask),
            tdma_payload_registry_load(&registry->short_frame_capacity),
            tdma_payload_registry_load(&registry->long_frame_capacity))) {
        tdma_payload_registry_note_result(
            registry,
            binding->payload_class,
            TDMA_PAYLOAD_REGISTRY_CAPACITY_REJECTED,
            true);
        return false;
    }

    uint32_t target_index = TDMA_PAYLOAD_REGISTRY_COUNT;
    bool replacing = false;
    for (uint32_t i = 0u; i < TDMA_PAYLOAD_REGISTRY_COUNT; i++) {
        tdma_payload_binding_t *entry = &registry->binding[i];
        if (entry->used != 0u &&
            entry->producer_id == binding->producer_id &&
            entry->consumer_id == binding->consumer_id &&
            entry->payload_class == binding->payload_class) {
            target_index = i;
            replacing = true;
            break;
        }
        if (target_index == TDMA_PAYLOAD_REGISTRY_COUNT && entry->used == 0u) {
            target_index = i;
        }
    }
    if (target_index == TDMA_PAYLOAD_REGISTRY_COUNT) {
        tdma_payload_registry_note_result(registry,
                                          binding->payload_class,
                                          TDMA_PAYLOAD_REGISTRY_FULL,
                                          true);
        return false;
    }

    tdma_payload_registry_write_guard(&registry->guard);
    registry->binding[target_index] = *binding;
    registry->binding[target_index].used = 1u;
    registry->registration_seq++;
    if (!replacing) {
        registry->used_count++;
    }
    registry->last_result = TDMA_PAYLOAD_REGISTRY_OK;
    registry->last_payload_class = binding->payload_class;
    tdma_payload_registry_write_guard(&registry->guard);
    return true;
}

bool tdma_payload_registry_admit(tdma_payload_registry_t *registry,
                                 uint32_t frame_class,
                                 uint32_t payload_class,
                                 size_t frame_size)
{
    if (registry == NULL || !tdma_payload_registry_class_valid(frame_class)) {
        if (registry != NULL) {
            tdma_payload_registry_note_result(registry,
                                              payload_class,
                                              TDMA_PAYLOAD_REGISTRY_BAD_ARGUMENT,
                                              true);
        }
        return false;
    }

    for (uint32_t i = 0u; i < TDMA_PAYLOAD_REGISTRY_COUNT; i++) {
        const tdma_payload_binding_t *binding = &registry->binding[i];
        if (binding->used != 0u &&
            binding->payload_class == payload_class &&
            binding->frame_class == frame_class) {
            if (frame_size > binding->max_payload_size) {
                tdma_payload_registry_note_result(
                    registry,
                    payload_class,
                    TDMA_PAYLOAD_REGISTRY_CAPACITY_REJECTED,
                    true);
                return false;
            }
            tdma_payload_registry_write_guard(&registry->guard);
            registry->admitted_count++;
            registry->last_result = TDMA_PAYLOAD_REGISTRY_OK;
            registry->last_payload_class = payload_class;
            tdma_payload_registry_write_guard(&registry->guard);
            return true;
        }
    }

    tdma_payload_registry_note_result(registry,
                                      payload_class,
                                      TDMA_PAYLOAD_REGISTRY_NOT_REGISTERED,
                                      true);
    return false;
}

bool tdma_payload_registry_get_snapshot(
    const tdma_payload_registry_t *registry,
    tdma_payload_registry_snapshot_t *snapshot)
{
    if (registry == NULL || snapshot == NULL) {
        return false;
    }

    while (true) {
        const uint32_t guard_begin = tdma_payload_registry_load(&registry->guard);
        if ((guard_begin & 1u) != 0u) {
            continue;
        }
        snapshot->version = TDMA_PAYLOAD_REGISTRY_VERSION;
        snapshot->config_seq = registry->config_seq;
        snapshot->registration_seq = registry->registration_seq;
        snapshot->payload_whitelist_mask = registry->payload_whitelist_mask;
        snapshot->short_frame_capacity = registry->short_frame_capacity;
        snapshot->long_frame_capacity = registry->long_frame_capacity;
        snapshot->used_count = registry->used_count;
        snapshot->admitted_count = registry->admitted_count;
        snapshot->reject_count = registry->reject_count;
        snapshot->last_result = registry->last_result;
        snapshot->last_payload_class = registry->last_payload_class;
        const uint32_t guard_end = tdma_payload_registry_load(&registry->guard);
        if (guard_begin == guard_end && (guard_end & 1u) == 0u) {
            return true;
        }
    }
}
