#ifndef TDMA_BOOT_STAGE_H
#define TDMA_BOOT_STAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Boot-time decision between the guide profile (10 kHz bring-up) and a
 * persisted TRN-03 calibration stage.
 *
 * A committed stage carries the operating-profile CRC32 it was measured
 * against, not the catalog level. The boot path therefore has to map that CRC
 * back to a level through the profile catalog before it can stage/apply the
 * profile that the stage belongs to. This module is the pure part of that
 * decision: it never touches flash, the store or the profile manager.
 *
 * Precedence is deliberately fail-closed: a stage that cannot be matched to a
 * catalog level is a REJECT (stay on the guide profile), never a silent
 * fallback to the stage's own numbers.
 */

typedef enum {
    TDMA_BOOT_STAGE_ACTION_NONE = 0u,   /* no persisted stage: guide profile */
    TDMA_BOOT_STAGE_ACTION_STAGE = 1u,  /* boot from the persisted stage */
    TDMA_BOOT_STAGE_ACTION_REJECT = 2u, /* stage exists but is unusable */
} tdma_boot_stage_action_t;

typedef enum {
    TDMA_BOOT_STAGE_REASON_NONE = 0u,
    TDMA_BOOT_STAGE_REASON_NO_STAGE = 1u,
    TDMA_BOOT_STAGE_REASON_NO_MATCH = 2u,    /* CRC not in the catalog */
    TDMA_BOOT_STAGE_REASON_BAD_PROFILE = 3u, /* zero CRC or empty catalog */
} tdma_boot_stage_reason_t;

typedef struct {
    uint32_t level;
    uint32_t profile_crc32;
} tdma_boot_stage_level_t;

typedef struct {
    tdma_boot_stage_action_t action;
    tdma_boot_stage_reason_t reason;
    uint32_t level; /* meaningful only for ACTION_STAGE */
} tdma_boot_stage_plan_t;

void tdma_boot_stage_decide(bool has_stage,
                            uint32_t stage_profile_crc32,
                            const tdma_boot_stage_level_t *levels,
                            size_t level_count,
                            tdma_boot_stage_plan_t *out);

#ifdef __cplusplus
}
#endif

#endif /* TDMA_BOOT_STAGE_H */
