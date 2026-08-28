#include "tdma_operating_profile.h"

#include <stddef.h>
#include <string.h>

#define TDMA_OPERATING_PROFILE_FNV_OFFSET 2166136261u
#define TDMA_OPERATING_PROFILE_FNV_PRIME 16777619u

typedef struct {
    uint32_t baud_hz;
    uint32_t cycle_period_ns;
    uint32_t train_cycles;
    uint32_t flags;
} tdma_operating_profile_entry_t;

/* Keep levels 0..6 backward compatible, then expose the same frequency
 * ladder at 1 ms and 100 us. All entries remain candidates until they pass
 * the strict two-board HIL gate. */
static const tdma_operating_profile_entry_t
    s_tdma_operating_profiles[TDMA_OPERATING_PROFILE_COUNT] = {
        {10000000u, 2000000u, 4096u, 0u},
        {25000000u, 2000000u, 4096u, 0u},
        {30000000u, 2000000u, 4096u, 0u},
        {35000000u, 2000000u, 4096u, 0u},
        {40000000u, 2000000u, 4096u, 0u},
        {45000000u, 2000000u, 4096u, 0u},
        {50000000u, 2000000u, 4096u, 0u},
        {10000000u, 1000000u, 4096u, 0u},
        {25000000u, 1000000u, 4096u, 0u},
        {30000000u, 1000000u, 4096u, 0u},
        {35000000u, 1000000u, 4096u, 0u},
        {40000000u, 1000000u, 4096u, 0u},
        {45000000u, 1000000u, 4096u, 0u},
        {50000000u, 1000000u, 4096u, 0u},
        {30000000u, 100000u, 4096u, 0u},
        {35000000u, 100000u, 4096u, 0u},
        {40000000u, 100000u, 4096u, 0u},
        {45000000u, 100000u, 4096u, 0u},
        {50000000u, 100000u, 4096u, 0u},
        /* Level 19: deliberately conservative 1 MHz bring-up profile.  It
         * preserves all existing level numbers while providing maximum wire
         * timing margin for multi-board path-delay characterization. */
        {1000000u, 2000000u, 4096u, 0u},
    };

static uint32_t tdma_operating_profile_hash_u32(uint32_t hash,
                                                 uint32_t value)
{
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        hash ^= (value >> shift) & 0xFFu;
        hash *= TDMA_OPERATING_PROFILE_FNV_PRIME;
    }
    return hash;
}

static void tdma_operating_profile_set_result(
    tdma_operating_profile_result_t *result,
    tdma_operating_profile_result_t value)
{
    if (result != NULL) {
        *result = value;
    }
}

uint32_t tdma_operating_profile_crc32(
    const tdma_operating_profile_t *profile)
{
    if (profile == NULL) {
        return 0u;
    }
    uint32_t hash = TDMA_OPERATING_PROFILE_FNV_OFFSET;
    hash = tdma_operating_profile_hash_u32(hash, profile->version);
    hash = tdma_operating_profile_hash_u32(hash, profile->level);
    hash = tdma_operating_profile_hash_u32(hash, profile->baud_hz);
    hash = tdma_operating_profile_hash_u32(hash, profile->cycle_period_ns);
    hash = tdma_operating_profile_hash_u32(hash, profile->train_cycles);
    hash = tdma_operating_profile_hash_u32(hash, profile->flags);
    return hash;
}

bool tdma_operating_profile_get(uint32_t level,
                                tdma_operating_profile_t *profile)
{
    if (profile == NULL || level >= TDMA_OPERATING_PROFILE_COUNT) {
        return false;
    }
    const tdma_operating_profile_entry_t *entry =
        &s_tdma_operating_profiles[level];
    memset(profile, 0, sizeof(*profile));
    profile->version = TDMA_OPERATING_PROFILE_VERSION;
    profile->level = level;
    profile->baud_hz = entry->baud_hz;
    profile->cycle_period_ns = entry->cycle_period_ns;
    profile->train_cycles = entry->train_cycles;
    profile->flags = entry->flags;
    profile->profile_crc32 = tdma_operating_profile_crc32(profile);
    return true;
}

bool tdma_operating_profile_find_baud(uint32_t baud_hz,
                                      tdma_operating_profile_t *profile)
{
    for (uint32_t level = 0u; level < TDMA_OPERATING_PROFILE_COUNT; level++) {
        if (s_tdma_operating_profiles[level].baud_hz == baud_hz) {
            return tdma_operating_profile_get(level, profile);
        }
    }
    return false;
}

bool tdma_operating_profile_validate(
    const tdma_operating_profile_t *profile,
    tdma_operating_profile_result_t *result)
{
    tdma_operating_profile_set_result(
        result, TDMA_OPERATING_PROFILE_BAD_ARGUMENT);
    if (profile == NULL) {
        return false;
    }
    tdma_operating_profile_t expected;
    if (profile->version != TDMA_OPERATING_PROFILE_VERSION ||
        !tdma_operating_profile_get(profile->level, &expected) ||
        profile->baud_hz != expected.baud_hz ||
        profile->cycle_period_ns != expected.cycle_period_ns ||
        profile->train_cycles != expected.train_cycles ||
        profile->flags != expected.flags) {
        tdma_operating_profile_set_result(
            result, TDMA_OPERATING_PROFILE_BAD_LEVEL);
        return false;
    }
    if (profile->profile_crc32 != tdma_operating_profile_crc32(profile)) {
        tdma_operating_profile_set_result(
            result, TDMA_OPERATING_PROFILE_CRC_MISMATCH);
        return false;
    }
    tdma_operating_profile_set_result(result, TDMA_OPERATING_PROFILE_OK);
    return true;
}

uint32_t tdma_operating_profile_schedule_crc32(
    uint32_t base_schedule_crc32,
    const tdma_operating_profile_t *profile)
{
    tdma_operating_profile_result_t result;
    if (base_schedule_crc32 == 0u ||
        !tdma_operating_profile_validate(profile, &result)) {
        return 0u;
    }
    return tdma_operating_profile_compose_schedule_crc32(
        base_schedule_crc32, profile->profile_crc32);
}

bool tdma_operating_profile_manager_init(
    tdma_operating_profile_manager_t *manager,
    uint32_t default_level)
{
    if (manager == NULL) {
        return false;
    }
    memset(manager, 0, sizeof(*manager));
    if (!tdma_operating_profile_get(default_level, &manager->active)) {
        manager->last_result = TDMA_OPERATING_PROFILE_BAD_LEVEL;
        return false;
    }
    manager->staged = manager->active;
    manager->last_result = TDMA_OPERATING_PROFILE_OK;
    return true;
}

bool tdma_operating_profile_manager_stage(
    tdma_operating_profile_manager_t *manager,
    uint32_t level)
{
    tdma_operating_profile_t profile;
    if (manager == NULL || !tdma_operating_profile_get(level, &profile)) {
        if (manager != NULL) {
            manager->reject_count++;
            manager->last_result = TDMA_OPERATING_PROFILE_BAD_LEVEL;
        }
        return false;
    }
    manager->staged = profile;
    manager->stage_count++;
    manager->last_result = TDMA_OPERATING_PROFILE_OK;
    return true;
}

bool tdma_operating_profile_manager_apply(
    tdma_operating_profile_manager_t *manager,
    bool ring_stopped)
{
    if (manager == NULL) {
        return false;
    }
    if (!ring_stopped) {
        manager->reject_count++;
        manager->last_result = TDMA_OPERATING_PROFILE_BUSY;
        return false;
    }
    manager->active = manager->staged;
    manager->apply_count++;
    manager->last_result = TDMA_OPERATING_PROFILE_OK;
    return true;
}
