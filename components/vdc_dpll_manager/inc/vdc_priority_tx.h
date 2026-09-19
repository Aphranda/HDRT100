#ifndef VDC_PRIORITY_TX_H
#define VDC_PRIORITY_TX_H

#include "tdma_priority_tx.h"
#include "vdc_dpll_manager.h"

typedef enum {
    VDC_PRIORITY_TX_REJECT_NONE = 0,
    VDC_PRIORITY_TX_REJECT_STOP,
    VDC_PRIORITY_TX_REJECT_RETIRED,
    VDC_PRIORITY_TX_REJECT_CONFIG,
    VDC_PRIORITY_TX_REJECT_SOURCE,
    VDC_PRIORITY_TX_REJECT_MODEL,
    VDC_PRIORITY_TX_REJECT_BINDING,
    VDC_PRIORITY_TX_REJECT_SEQUENCE,
    VDC_PRIORITY_TX_REJECT_PROJECTION,
    VDC_PRIORITY_TX_REJECT_WIDTH,
    VDC_PRIORITY_TX_REJECT_CHANGED,
    VDC_PRIORITY_TX_REJECT_MAPPING_CONTRADICTION,
    VDC_PRIORITY_TX_REJECT_MAPPING_EXHAUSTED
} vdc_priority_tx_reject_t;

/* Borrowed after successful fresh encoding. Raw interval, observation and
 * committed model support independent TIMER1-coordinate replay. */
typedef struct {
    uint32_t generation, session, role_generation, clock_epoch, clock_run;
    uint32_t local_slot, node_count, schedule_crc32, profile_crc32;
    uint32_t source_epoch, event_sequence, source_identity, published_version, tick_hz;
    uint64_t source_raw_lo, source_raw_hi, encoded_output_lo;
    uint32_t encoded_width;
    vdc_dpll_manager_timer1_projection_t projection;
} vdc_priority_tx_origin_evidence_t;

/* Core1 offer diagnostics, retained after STOP. Counts describe software
 * offers only, never DMA selection, wire delivery or timestamp accuracy. */
typedef struct {
    uint32_t schema, generation, active, retired, have_offer;
    uint32_t calls, encoded, repeated, rejected, last_reject;
    uint32_t source_epoch, event_sequence, source_identity, published_version;
    uint32_t model_token, session, role_generation, clock_epoch, clock_run;
    uint32_t local_slot, node_count, schedule_crc32, profile_crc32;
    uint32_t uncertainty_width;
    uint64_t event_time_lower;
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
} vdc_priority_tx_snapshot_t;

/* Core0, STOP only. A nonzero request requires an existing feedback session
 * and a generation strictly newer than every previous nonzero request.
 * Zero disables. This never changes the existing feedback session. */
bool vdc_dpll_manager_set_priority_sync(uint32_t generation);
uint32_t vdc_dpll_manager_priority_sync_generation(void);
/* One atomic-word snapshot attempt; failure leaves *out unchanged. */
bool vdc_dpll_manager_get_priority_tx(vdc_priority_tx_snapshot_t *out);
/* Volatile, one-generation diagnostic pause of fresh offers. EMPTY leaves
 * TDMA running and may repeat its last DMA mailbox. Never saved to Flash. */
typedef struct {
    uint32_t generation, session, after_ms, duration_ms;
} vdc_priority_tx_gap_config_t;
enum {
    VDC_PRIORITY_GAP_DISABLED, VDC_PRIORITY_GAP_WAITING,
    VDC_PRIORITY_GAP_ACTIVE, VDC_PRIORITY_GAP_RECOVERING,
    VDC_PRIORITY_GAP_DONE, VDC_PRIORITY_GAP_CANCELLED,
    VDC_PRIORITY_GAP_MISSED
};
typedef struct {
    uint32_t schema, generation, session, state, suppressed;
    uint32_t before_sequence, resume_sequence, tick_hz, after_ms, duration_ms;
    uint64_t anchor_raw, first_suppressed_raw, last_suppressed_raw, resume_raw;
} vdc_priority_tx_gap_snapshot_t;
/* Core0 STOP only, current nonzero SYNC generation/session. All-zero clears.
 * Times are relative to the first valid source observation in this run. */
bool vdc_dpll_manager_set_priority_tx_gap(uint32_t generation, uint32_t after_ms,
    uint32_t duration_ms);
bool vdc_dpll_manager_get_priority_tx_gap_config(vdc_priority_tx_gap_config_t *out);
bool vdc_dpll_manager_get_priority_tx_gap(vdc_priority_tx_gap_snapshot_t *out);
/* Core1 trace ARM/ACK check: this stopped-installed generation must not have
 * encoded an event or committed a cache yet. Core0 must use atomic getters,
 * never inspect the owner-private TX work to authorize its capture. */
bool vdc_priority_tx_origin_trace_eligible_core1(uint32_t generation, uint32_t session);

/* Registered Core1 TDMA callback. NULL config retires the current request;
 * only a new generation may bind again. EMPTY keeps the priority reservation.
 * Every non-READY result preserves caller mailbox bytes. */
tdma_priority_tx_result_t vdc_priority_tx_core1(
    const tdma_ring_runtime_config_t *config,
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE]);

#endif
