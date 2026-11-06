#ifndef FLASH_STORE_AB_H
#define FLASH_STORE_AB_H

#include <stdbool.h>
#include <stdint.h>

#include "flash_store_nvs.h"
#include "flash_store_record.h"

/* Two-sector A/B selection and record-sequence rules shared by the NVS stores
 * (boot profile, calibration training, seat mount). Only the read/plan helpers
 * live here; erase/program/commit sequencing stays in each store because the
 * commit-page and verify order is part of its power-loss contract. */

static inline bool flash_store_ab_has_latest(const flash_store_nvs_scan_t *scan,
                                             bool usable)
{
    return scan != NULL && usable && scan->has_latest;
}

/* Index (0/1) of the newest valid record; false when neither sector holds one.
 * Ties prefer sector 0, matching the stores' previous inline rule. */
static inline bool flash_store_ab_select_latest(
    const flash_store_nvs_scan_t scans[2],
    const bool usable[2],
    uint32_t *latest_sector)
{
    const bool first = flash_store_ab_has_latest(&scans[0], usable[0]);
    const bool second = flash_store_ab_has_latest(&scans[1], usable[1]);
    if (!first && !second) {
        return false;
    }
    *latest_sector = (!second || (first && !flash_store_record_is_newer(
        &scans[1].latest, &scans[0].latest))) ? 0u : 1u;
    return true;
}

/* Next record sequence for a save: 1 for a fresh sector/generation, otherwise
 * last+1 with wrap to 1 (0 is reserved). */
static inline uint32_t flash_store_ab_next_sequence(bool has_latest,
                                                    uint32_t generation,
                                                    uint32_t latest_generation,
                                                    uint32_t latest_sequence)
{
    if (!has_latest || generation != latest_generation) {
        return 1u;
    }
    const uint32_t next = latest_sequence + 1u;
    return next == 0u ? 1u : next;
}

#endif /* FLASH_STORE_AB_H */
