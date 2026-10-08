#ifndef TDMA_SEAT_ADMISSION_H
#define TDMA_SEAT_ADMISSION_H

#include <stdbool.h>
#include <stdint.h>

/* Pure TDMA-owner model. No provider/PIO/FIFO/SCPI calls, locks or allocation.
 * One owner serializes all calls and copies snapshots across cores using its
 * existing guarded handoff. This module alone never opens a hardware window. */
typedef enum {
    TDMA_SEAT_CLOSED = 0,
    TDMA_SEAT_WAIT_PRIORITY,
    TDMA_SEAT_PRIORITY_SELECTED,
    TDMA_SEAT_PRIORITY_TX_COMMITTED,
    TDMA_SEAT_FIRST_CLASS_WINDOW_GRANTED,
    TDMA_SEAT_LOAD_SELECTED,
    TDMA_SEAT_LOAD_TX_COMMITTED,
    TDMA_SEAT_REVOKED,
} tdma_seat_state_t;

typedef enum {
    TDMA_SEAT_OK = 0,
    TDMA_SEAT_DUPLICATE,
    TDMA_SEAT_BAD_ARGUMENT,
    TDMA_SEAT_STALE_IDENTITY,
    TDMA_SEAT_WRONG_STATE,
    TDMA_SEAT_TIME_INVALID,
    TDMA_SEAT_WINDOW_WAIT,
    TDMA_SEAT_WINDOW_EXPIRED,
    TDMA_SEAT_BUDGET_UNDEFINED,
    TDMA_SEAT_BUDGET_EXHAUSTED,
} tdma_seat_result_t;

typedef enum {
    TDMA_SEAT_REVOKE_NONE = 0,
    TDMA_SEAT_REVOKE_STOP,
    TDMA_SEAT_REVOKE_NEW_PRIORITY,
    TDMA_SEAT_REVOKE_BINDING_CHANGED,
    TDMA_SEAT_REVOKE_FAULT,
    TDMA_SEAT_REVOKE_WINDOW,
    TDMA_SEAT_REVOKE_BUDGET,
} tdma_seat_revoke_reason_t;

typedef enum {
    TDMA_SEAT_TX_BYTES = 0,
    TDMA_SEAT_TX_FRAMES,
    TDMA_SEAT_RX_BYTES,
    TDMA_SEAT_RX_FRAMES,
    TDMA_SEAT_RX_RETENTION_SLOTS,
    TDMA_SEAT_CORE1_CYCLES, /* Includes IRQ and handoff cost. */
    TDMA_SEAT_CORE0_CYCLES, /* Includes consume, CRC and reassembly. */
    TDMA_SEAT_REVERSE_TX_BYTES,
    TDMA_SEAT_REVERSE_TX_FRAMES,
    TDMA_SEAT_RESOURCE_COUNT,
} tdma_seat_resource_t;

#define TDMA_SEAT_BUDGET_ALL ((1u << TDMA_SEAT_RESOURCE_COUNT) - 1u)

typedef struct {
    /* Strictly increasing owner epoch across STOP/configuration boundaries.
     * Exhaustion requires cold initialization; never wrap/reuse an epoch. */
    uint64_t epoch;
    uint64_t cycle;
    uint32_t config_generation;
    uint32_t profile_generation;
    uint32_t binding_generation;
    uint32_t priority_sequence; /* Zero is valid on transport wrap. */
} tdma_seat_identity_t;

typedef struct {
    uint64_t start_ns;
    uint64_t end_ns;
    uint32_t close_guard_ns;
} tdma_seat_window_t;

typedef struct {
    tdma_seat_identity_t identity;
    /* Each bit attests an explicitly admitted owner budget in its own unit.
     * RX is not derived from TX, nominal FIFO free slots or host idle time.
     * Remaining resources already exclude higher-priority reservations. */
    uint32_t defined_mask;
    uint32_t remaining[TDMA_SEAT_RESOURCE_COUNT];
} tdma_seat_budget_t;

typedef struct {
    uint32_t load_id; /* Nonzero local immutable request token, not a feature ID. */
    uint32_t duration_ns; /* Full local transfer cost before close guard. */
    uint32_t cost[TDMA_SEAT_RESOURCE_COUNT];
} tdma_seat_request_t;

typedef struct {
    tdma_seat_state_t state;
    tdma_seat_result_t last_result;
    tdma_seat_revoke_reason_t revoke_reason;
    tdma_seat_identity_t identity;
    tdma_seat_window_t window;
    uint64_t epoch_high_water;
    uint64_t last_time_ns;
    uint64_t selected_time_ns;
    uint64_t priority_commit_time_ns;
    uint64_t load_commit_time_ns;
    uint64_t load_selected_time_ns;
    uint32_t selected_token; /* Exact hardware-selected image identity. */
    bool priority_committed;
    bool load_committed; /* Local handoff fact retained even after revoke. */
    tdma_seat_request_t request;
    tdma_seat_budget_t budget;
} tdma_seat_admission_t;

bool tdma_seat_admission_init(tdma_seat_admission_t *seat);
tdma_seat_result_t tdma_seat_admission_begin(tdma_seat_admission_t *seat,
    const tdma_seat_identity_t *identity, const tdma_seat_window_t *window,
    uint64_t now_ns);
/* Only the physical owner supplies these facts. Offer READY/EMPTY, enqueue,
 * publish(), generic scheduler dispatch and returned RX are not substitutes. */
tdma_seat_result_t tdma_seat_admission_priority_selected(
    tdma_seat_admission_t *seat, const tdma_seat_identity_t *identity,
    uint32_t selected_token, uint64_t now_ns);
tdma_seat_result_t tdma_seat_admission_priority_committed(
    tdma_seat_admission_t *seat, const tdma_seat_identity_t *identity,
    uint32_t selected_token, uint64_t now_ns);
tdma_seat_result_t tdma_seat_admission_grant(tdma_seat_admission_t *seat,
    const tdma_seat_budget_t *budget, const tdma_seat_request_t *request,
    uint64_t now_ns);
/* Revalidate immediately before physical submission and reserve once. The
 * owner serializes selection with its backend operation. Selection is NOT a
 * backend commit. Failure must revoke FAULT without refunding this epoch. */
tdma_seat_result_t tdma_seat_admission_select_load(tdma_seat_admission_t *seat,
    const tdma_seat_budget_t *live_budget, uint32_t load_id, uint64_t now_ns);
/* Report the backend's confirmed local handoff of exactly this selected load.
 * now_ns is actual handoff time, not a delayed callback/query time. The owner
 * reports before advancing this model's clock again. Late handoff is retained
 * as a fact but revokes the window with WINDOW_EXPIRED. Not SENT/RX/APPLIED. */
tdma_seat_result_t tdma_seat_admission_commit_load(tdma_seat_admission_t *seat,
    const tdma_seat_identity_t *identity, uint32_t load_id, uint64_t now_ns);
bool tdma_seat_admission_revoke(tdma_seat_admission_t *seat,
    tdma_seat_revoke_reason_t reason);
/* Owner must call at the decision boundary; stored state is not a live clock. */
tdma_seat_result_t tdma_seat_admission_expire(tdma_seat_admission_t *seat,
    uint64_t now_ns);

#endif
