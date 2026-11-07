#ifndef TDMA_BOOT_SEQUENCE_H
#define TDMA_BOOT_SEQUENCE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_boot_plan.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Ordered local calls a board must make for the decided boot action.
 *
 * The sequence is data, not code: the boot caller walks it and stops on the
 * first failure. It is deliberately local (no cross-board command) because the
 * guide ring does not exist yet at boot.
 */

typedef enum {
    TDMA_BOOT_STEP_NONE = 0u,
    TDMA_BOOT_STEP_SET_BRINGUP_NO_CAL = 1u,
    TDMA_BOOT_STEP_SET_BRINGUP_TOPOLOGY = 2u,
    TDMA_BOOT_STEP_STAGE_PROFILE = 3u,
    TDMA_BOOT_STEP_APPLY_PROFILE = 4u,
    TDMA_BOOT_STEP_SET_FLIGHT_MODE = 5u, /* resident process image */
    TDMA_BOOT_STEP_RING_ARM = 6u,
    TDMA_BOOT_STEP_RING_START = 7u,   /* coordinator: start TX */
    TDMA_BOOT_STEP_RING_LISTEN = 8u,  /* follower: RX listen only */
    TDMA_BOOT_STEP_STOP = 9u,         /* reject: stay STOPPED */
} tdma_boot_step_t;

#define TDMA_BOOT_SEQUENCE_MAX 8u

/* Fills out[] with the ordered steps; returns the count (0 for NONE or a NULL
 * output). Never writes past capacity. */
size_t tdma_boot_sequence_build(const tdma_boot_plan_t *plan,
                                tdma_boot_step_t *out,
                                size_t capacity);

/* Human-readable name for diagnostics and tests. */
const char *tdma_boot_step_name(tdma_boot_step_t step);

#ifdef __cplusplus
}
#endif

#endif /* TDMA_BOOT_SEQUENCE_H */
