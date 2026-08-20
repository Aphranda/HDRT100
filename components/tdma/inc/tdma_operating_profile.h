#ifndef TDMA_OPERATING_PROFILE_H
#define TDMA_OPERATING_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#define TDMA_OPERATING_PROFILE_VERSION 1u
#define TDMA_OPERATING_PROFILE_COUNT 20u
#define TDMA_OPERATING_PROFILE_DEFAULT_LEVEL 0u
#define TDMA_OPERATING_PROFILE_MIN_CYCLE_PERIOD_NS 100000u
#define TDMA_OPERATING_PROFILE_MAX_CYCLE_PERIOD_NS 2000000u
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
bool tdma_operating_profile_validate(
    const tdma_operating_profile_t *profile,
    tdma_operating_profile_result_t *result);
uint32_t tdma_operating_profile_schedule_crc32(
    uint32_t base_schedule_crc32,
    const tdma_operating_profile_t *profile);
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
