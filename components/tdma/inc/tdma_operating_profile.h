#ifndef TDMA_OPERATING_PROFILE_H
#define TDMA_OPERATING_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#define TDMA_OPERATING_PROFILE_VERSION 1u

/* Base levels 0..22 are frozen and keep their existing profile CRCs. The
 * low-rate group is generated for the 4-node image only (levels 23..25);
 * node counts above 4 are rejected by the ring slot check. Slot/baud/guard
 * margins remain data, not hard-coded at call sites. */
#define TDMA_OPERATING_PROFILE_BASE_COUNT 23u
#define TDMA_OPERATING_PROFILE_LOWRATE_COUNT 3u
#define TDMA_OPERATING_PROFILE_LOWRATE_LEVEL_BASE \
    TDMA_OPERATING_PROFILE_BASE_COUNT
#define TDMA_OPERATING_PROFILE_COUNT \
    (TDMA_OPERATING_PROFILE_BASE_COUNT + TDMA_OPERATING_PROFILE_LOWRATE_COUNT)
#define TDMA_OPERATING_PROFILE_DEFAULT_LEVEL 0u
#define TDMA_OPERATING_PROFILE_MIN_CYCLE_PERIOD_NS 100000u
#define TDMA_OPERATING_PROFILE_MAX_CYCLE_PERIOD_NS 150000000u
/* Must match TDMA_PIO_SPI_FLIGHT_MAX_TAIL_BYTES; kept local so the profile
 * arithmetic also builds in the host unit-test gate without the PIO headers. */
#define TDMA_OPERATING_PROFILE_MAX_TAIL_BYTES 11u
#define TDMA_OPERATING_PROFILE_LOWRATE_LOOP_DELAY_NS 1000u
#define TDMA_OPERATING_PROFILE_FLAG_HIL_VALIDATED 0x00000001u

typedef enum {
    TDMA_OPERATING_PROFILE_OK = 0u,
    TDMA_OPERATING_PROFILE_BAD_ARGUMENT = 1u,
    TDMA_OPERATING_PROFILE_BAD_LEVEL = 2u,
    TDMA_OPERATING_PROFILE_BUSY = 3u,
    TDMA_OPERATING_PROFILE_CRC_MISMATCH = 4u,
} tdma_operating_profile_result_t;

typedef struct {
    uint32_t version;
    uint32_t level;
    uint32_t baud_hz;
    uint32_t cycle_period_ns;
    uint32_t train_cycles;
    uint32_t flags;
    uint32_t profile_crc32;
} tdma_operating_profile_t;

typedef struct {
    tdma_operating_profile_t active;
    tdma_operating_profile_t staged;
    uint32_t stage_count;
    uint32_t apply_count;
    uint32_t reject_count;
    uint32_t last_result;
} tdma_operating_profile_manager_t;

uint32_t tdma_operating_profile_crc32(
    const tdma_operating_profile_t *profile);
bool tdma_operating_profile_get(uint32_t level,
                                tdma_operating_profile_t *profile);
bool tdma_operating_profile_find_baud(uint32_t baud_hz,
                                      tdma_operating_profile_t *profile);
/* Exact (baud, slot) lookup. A baud may now have more than one entry (4/8
 * node groups), so find_baud() alone is not sufficient for selection. */
bool tdma_operating_profile_find_pair(uint32_t baud_hz,
                                      uint32_t cycle_period_ns,
                                      tdma_operating_profile_t *profile);
/* Minimum slot for one node count: frame time plus guard quanta plus an
 * optional turnaround margin. Returns false on bad input or when the derived
 * tail exceeds the frozen physical bound. */
bool tdma_operating_profile_slot_min_ns(uint32_t baud_hz,
                                        uint32_t node_count,
                                        uint32_t loop_delay_ns,
                                        uint32_t guard_quanta,
                                        uint32_t turnaround_ns,
                                        uint64_t *slot_min_ns);
bool tdma_operating_profile_slot_sufficient(uint32_t baud_hz,
                                            uint32_t cycle_period_ns,
                                            uint32_t node_count);
/* Ring admission: legacy 1..50 MHz range stays accepted for existing
 * configurations; lower rates must select a low-rate catalog pair whose
 * profile CRC matches and whose slot covers the admitted node count. */
bool tdma_operating_profile_admitted_for_ring(uint32_t baud_hz,
                                              uint32_t cycle_period_ns,
                                              uint32_t operating_profile_crc32,
                                              uint32_t node_count);
bool tdma_operating_profile_validate(
    const tdma_operating_profile_t *profile,
    tdma_operating_profile_result_t *result);
uint32_t tdma_operating_profile_schedule_crc32(
    uint32_t base_schedule_crc32,
    const tdma_operating_profile_t *profile);
static inline uint32_t tdma_operating_profile_compose_schedule_crc32(
    uint32_t base_schedule_crc32,
    uint32_t operating_profile_crc32)
{
    if (base_schedule_crc32 == 0u || operating_profile_crc32 == 0u) {
        return 0u;
    }
    uint32_t hash = 2166136261u;
    const uint32_t values[2] = {
        base_schedule_crc32,
        operating_profile_crc32,
    };
    for (uint32_t value_index = 0u; value_index < 2u; value_index++) {
        for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
            hash ^= (values[value_index] >> shift) & 0xFFu;
            hash *= 16777619u;
        }
    }
    return hash;
}
bool tdma_operating_profile_manager_init(
    tdma_operating_profile_manager_t *manager,
    uint32_t default_level);
bool tdma_operating_profile_manager_stage(
    tdma_operating_profile_manager_t *manager,
    uint32_t level);
bool tdma_operating_profile_manager_apply(
    tdma_operating_profile_manager_t *manager,
    bool ring_stopped);

#endif
