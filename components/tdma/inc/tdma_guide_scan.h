#ifndef TDMA_GUIDE_SCAN_H
#define TDMA_GUIDE_SCAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* P0T pair-scan model for the single-port guide ring (M1).
 *
 * The scan is data, not code: every ordered pair (driver, receiver) is derived
 * from the node count, each board derives its own role from its persisted
 * slot, and the only thing a board records locally is "did the receiver see
 * activity for this pair". Nothing here touches hardware; the caller owns the
 * STOP/TOPology/ARM/START/STOP sequence and the sampling window.
 *
 * Aggregation mirrors docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md
 * section 9.6 and tests/python/test_cal_guide_topology_table.py: only a single
 * closed loop covering every slot is accepted.
 */

#define TDMA_GUIDE_SCAN_NODE_MIN 2u
#define TDMA_GUIDE_SCAN_NODE_MAX 8u
#define TDMA_GUIDE_SCAN_PAIR_MAX \
    (TDMA_GUIDE_SCAN_NODE_MAX * (TDMA_GUIDE_SCAN_NODE_MAX - 1u))

typedef enum {
    TDMA_GUIDE_SCAN_ROLE_IDLE = 0u,     /* not part of this pair */
    TDMA_GUIDE_SCAN_ROLE_DRIVER = 1u,   /* pair slot 0: transmits the probe */
    TDMA_GUIDE_SCAN_ROLE_RECEIVER = 2u, /* pair slot 1: counts the activity */
} tdma_guide_scan_role_t;

typedef enum {
    TDMA_GUIDE_SCAN_VERDICT_NONE = 0u, /* not aggregated yet */
    TDMA_GUIDE_SCAN_VERDICT_ACCEPTED = 1u,
    TDMA_GUIDE_SCAN_VERDICT_NO_ACTIVITY = 2u,
    TDMA_GUIDE_SCAN_VERDICT_OPEN_CHAIN = 3u,
    TDMA_GUIDE_SCAN_VERDICT_BRANCH = 4u,
    TDMA_GUIDE_SCAN_VERDICT_DUPLICATE_EDGE = 5u,
    TDMA_GUIDE_SCAN_VERDICT_MULTI_LOOP = 6u,
    TDMA_GUIDE_SCAN_VERDICT_INCOMPLETE = 7u, /* scan itself did not finish */
    TDMA_GUIDE_SCAN_VERDICT_ARGUMENT = 8u,
} tdma_guide_scan_verdict_t;

typedef struct {
    uint32_t node_count;
    uint32_t pair_count;
    uint64_t activity_mask; /* bit i = pair i observed; capped at 56 pairs */
} tdma_guide_scan_table_t;

/* Slot ids are 0..node_count-1 (the persisted boot-profile slot, not the board
 * number). Returns false for an unsupported node count. */
bool tdma_guide_scan_init(tdma_guide_scan_table_t *table, uint32_t node_count);

/* Pair order is fixed: for each driver ascending, every other receiver
 * ascending. Returns false when the index does not belong to the table. */
bool tdma_guide_scan_pair_at(const tdma_guide_scan_table_t *table,
                             uint32_t index,
                             uint32_t *driver,
                             uint32_t *receiver);

/* Index of the ordered pair, or UINT32_MAX when it is not part of the table. */
uint32_t tdma_guide_scan_pair_index(const tdma_guide_scan_table_t *table,
                                    uint32_t driver,
                                    uint32_t receiver);

/* Role of one local slot in one pair; IDLE for a slot outside the pair. */
tdma_guide_scan_role_t tdma_guide_scan_role(
    const tdma_guide_scan_table_t *table, uint32_t index, uint32_t local_slot);

/* Pair-topology slot the role must configure (driver 0, receiver 1). Returns
 * false for IDLE. */
bool tdma_guide_scan_pair_slot(tdma_guide_scan_role_t role, uint32_t *slot);

/* Records the receiver's verdict for one pair. Returns true when the bit
 * actually changed (a repeated observation of the same pair is idempotent). */
bool tdma_guide_scan_record(tdma_guide_scan_table_t *table,
                            uint32_t index,
                            bool observed);

/* Number of observed pairs. */
uint32_t tdma_guide_scan_observed_count(const tdma_guide_scan_table_t *table);

/* True once every pair of the table holds a result. */
bool tdma_guide_scan_complete(const tdma_guide_scan_table_t *table);

/* Aggregates the activity mask into an accepted ring order.
 *
 * ring_order receives the slot walk starting at anchor; capacity must be at
 * least node_count. *out_len is set to 0 unless the verdict is ACCEPTED. */
tdma_guide_scan_verdict_t tdma_guide_scan_aggregate(
    const tdma_guide_scan_table_t *table,
    uint32_t anchor,
    uint8_t *ring_order,
    size_t capacity,
    uint32_t *out_len);

const char *tdma_guide_scan_verdict_name(tdma_guide_scan_verdict_t verdict);

#ifdef __cplusplus
}
#endif

#endif /* TDMA_GUIDE_SCAN_H */
