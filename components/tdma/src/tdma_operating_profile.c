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

/* Low-rate entries carry the node budget they were generated for. The slot is
 * data (configurable), while validate() proves it still covers the frame plus
 * at least one guard quantum for that node count. */
typedef struct {
    uint32_t baud_hz;
    uint32_t cycle_period_ns;
    uint32_t train_cycles;
    uint32_t flags;
    uint32_t node_count;
} tdma_operating_profile_lowrate_entry_t;

/* Keep levels 0..6 backward compatible, then expose the same frequency
 * ladder at 1 ms and 100 us. All entries remain candidates until they pass
 * the strict two-board HIL gate. */
static const tdma_operating_profile_entry_t
    s_tdma_operating_profiles[TDMA_OPERATING_PROFILE_BASE_COUNT] = {
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
        /* Levels 20/21: shorter per-node slots so a four-node ring
         * revolution matches the 1.5 ms table period (375 us) or sits at
         * a 2 ms revolution (500 us).  The reference emits once per
         * revolution, so the emission interval equals the revolution. */
        {10000000u, 375000u, 4096u, 0u},
        {10000000u, 500000u, 4096u, 0u},
        /* Level 22: 750 us slot, so a two-node ring completes one
         * revolution in 1.5 ms while the slot stays wide enough for
         * the Core1 TDMA phase service budget. */
        {10000000u, 750000u, 4096u, 0u},
    };

/* Generated for the 4-node image. The low-rate group stays deliberately
 * small: 10 kHz/100 kHz/5 MHz with slots that cover a 174 B frame. Node
 * counts above 4 are rejected by the ring slot check. */
static const tdma_operating_profile_lowrate_entry_t
    s_tdma_lowrate_profiles[TDMA_OPERATING_PROFILE_LOWRATE_COUNT] = {
        {10000u, 150000000u, 4096u, 0u, 4u},
        {100000u, 15000000u, 4096u, 0u, 4u},
        {5000000u, 500000u, 4096u, 0u, 4u},
    };

_Static_assert(sizeof(s_tdma_lowrate_profiles) /
                   sizeof(s_tdma_lowrate_profiles[0]) ==
               TDMA_OPERATING_PROFILE_LOWRATE_COUNT,
               "low-rate profile count must match the generated table");

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

static bool tdma_operating_profile_lowrate_entry(
    uint32_t level,
    tdma_operating_profile_lowrate_entry_t *entry)
{
    if (entry == NULL ||
        level < TDMA_OPERATING_PROFILE_LOWRATE_LEVEL_BASE ||
        level >= TDMA_OPERATING_PROFILE_COUNT) {
        return false;
    }
    *entry = s_tdma_lowrate_profiles[
        level - TDMA_OPERATING_PROFILE_LOWRATE_LEVEL_BASE];
    return true;
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
    tdma_operating_profile_entry_t entry;
    if (level < TDMA_OPERATING_PROFILE_BASE_COUNT) {
        entry = s_tdma_operating_profiles[level];
    } else {
        tdma_operating_profile_lowrate_entry_t low;
        if (!tdma_operating_profile_lowrate_entry(level, &low)) {
            return false;
        }
        entry.baud_hz = low.baud_hz;
        entry.cycle_period_ns = low.cycle_period_ns;
        entry.train_cycles = low.train_cycles;
        entry.flags = low.flags;
    }
    memset(profile, 0, sizeof(*profile));
    profile->version = TDMA_OPERATING_PROFILE_VERSION;
    profile->level = level;
    profile->baud_hz = entry.baud_hz;
    profile->cycle_period_ns = entry.cycle_period_ns;
    profile->train_cycles = entry.train_cycles;
    profile->flags = entry.flags;
    profile->profile_crc32 = tdma_operating_profile_crc32(profile);
    return true;
}

bool tdma_operating_profile_find_baud(uint32_t baud_hz,
                                      tdma_operating_profile_t *profile)
{
    for (uint32_t level = 0u; level < TDMA_OPERATING_PROFILE_COUNT; level++) {
        tdma_operating_profile_t candidate;
        if (tdma_operating_profile_get(level, &candidate) &&
            candidate.baud_hz == baud_hz) {
            if (profile != NULL) {
                *profile = candidate;
            }
            return true;
        }
    }
    return false;
}

bool tdma_operating_profile_find_pair(uint32_t baud_hz,
                                      uint32_t cycle_period_ns,
                                      tdma_operating_profile_t *profile)
{
    for (uint32_t level = 0u; level < TDMA_OPERATING_PROFILE_COUNT; level++) {
        tdma_operating_profile_t candidate;
        if (tdma_operating_profile_get(level, &candidate) &&
            candidate.baud_hz == baud_hz &&
            candidate.cycle_period_ns == cycle_period_ns) {
            if (profile != NULL) {
                *profile = candidate;
            }
            return true;
        }
    }
    return false;
}

bool tdma_operating_profile_slot_min_ns(uint32_t baud_hz,
                                        uint32_t node_count,
                                        uint32_t loop_delay_ns,
                                        uint32_t guard_quanta,
                                        uint32_t turnaround_ns,
                                        uint64_t *slot_min_ns)
{
    if (slot_min_ns == NULL || baud_hz == 0u || node_count < 2u ||
        node_count > 8u || guard_quanta == 0u) {
        return false;
    }
    /* Same frame shape as tdma_pio_spi_phys_arm(): PIO header + transport
     * header + per-node 32 B mailboxes + trailer + elastic tail. */
    const uint64_t loop_bits =
        ((uint64_t)loop_delay_ns * baud_hz + 999999999ull) / 1000000000ull;
    const uint64_t loop_bytes = (loop_bits + 7ull) / 8ull;
    const uint64_t tail = (uint64_t)(node_count - 1u) + loop_bytes + 2ull;
    if (tail > TDMA_OPERATING_PROFILE_MAX_TAIL_BYTES) {
        return false;
    }
    const uint64_t physical =
        4ull + 32ull + 32ull * (uint64_t)node_count + 4ull + tail;
    const uint64_t wire_ns =
        (physical * 8ull * 1000000000ull + (uint64_t)baud_hz - 1ull) /
        (uint64_t)baud_hz;
    /* 32 PIO control cycles at six control cycles per bit. */
    const uint64_t guard_ns =
        (32ull * 1000000000ull + (uint64_t)baud_hz * 6ull - 1ull) /
        ((uint64_t)baud_hz * 6ull);
    *slot_min_ns = wire_ns + (uint64_t)guard_quanta * guard_ns +
                   (uint64_t)turnaround_ns;
    return true;
}

bool tdma_operating_profile_slot_sufficient(uint32_t baud_hz,
                                            uint32_t cycle_period_ns,
                                            uint32_t node_count)
{
    uint64_t slot_min_ns = 0u;
    return tdma_operating_profile_slot_min_ns(
               baud_hz, node_count,
               TDMA_OPERATING_PROFILE_LOWRATE_LOOP_DELAY_NS, 1u, 0u,
               &slot_min_ns) &&
           (uint64_t)cycle_period_ns >= slot_min_ns;
}

bool tdma_operating_profile_admitted_for_ring(uint32_t baud_hz,
                                              uint32_t cycle_period_ns,
                                              uint32_t operating_profile_crc32,
                                              uint32_t node_count)
{
    /* Preserve the existing 1..50 MHz admission for already-working
     * configurations; low rates must come from the generated catalog. */
    if (baud_hz >= 1000000u && baud_hz <= 50000000u) {
        return true;
    }
    tdma_operating_profile_t profile;
    return tdma_operating_profile_find_pair(baud_hz, cycle_period_ns,
                                            &profile) &&
           profile.profile_crc32 == operating_profile_crc32 &&
           tdma_operating_profile_slot_sufficient(baud_hz, cycle_period_ns,
                                                  node_count);
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
    if (profile->level >= TDMA_OPERATING_PROFILE_LOWRATE_LEVEL_BASE) {
        tdma_operating_profile_lowrate_entry_t low;
        if (!tdma_operating_profile_lowrate_entry(profile->level, &low) ||
            !tdma_operating_profile_slot_sufficient(
                profile->baud_hz, profile->cycle_period_ns,
                low.node_count)) {
            tdma_operating_profile_set_result(
                result, TDMA_OPERATING_PROFILE_BAD_LEVEL);
            return false;
        }
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
