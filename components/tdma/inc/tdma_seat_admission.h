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

/* Streaming loop plan: one ring loop may carry several emission windows.
 * Slot 1 is the priority seat, slot 2 the first-class seat; a LOAD slot is
 * appended only when a complete window (including the close guard) still fits
 * inside the loop period. Pure arithmetic: it never touches hardware, budgets
 * or the single-seat state machine above. */
typedef enum {
    TDMA_LOOP_SLOT_NONE = 0,
    TDMA_LOOP_SLOT_PRIORITY,
    TDMA_LOOP_SLOT_FIRST_CLASS,
    TDMA_LOOP_SLOT_LOAD,
} tdma_loop_slot_class_t;

typedef struct {
    uint64_t loop_period_ns;
    uint64_t emission_period_ns;
    uint32_t close_guard_ns;
    bool priority_pending;
    bool first_class_pending;
    bool load_pending;
    /* Windows may be compressed forward inside the loop when a later need
     * appears; 0 keeps the plan anchored at the loop start. */
    uint64_t start_offset_ns;
} tdma_loop_plan_request_t;

typedef struct {
    tdma_loop_slot_class_t cls;
    tdma_seat_window_t window;
} tdma_loop_slot_t;

typedef struct {
    uint32_t count;
    tdma_loop_slot_t slots[3];
    /* Echoed from the request so callers can reduce an absolute slot index
     * into the loop without carrying the period themselves. */
    uint64_t loop_period_ns;
} tdma_loop_plan_t;

tdma_seat_result_t tdma_loop_plan(const tdma_loop_plan_request_t *request,
    tdma_loop_plan_t *out);

/* Production wiring defaults for the streaming loop plan. The loop period and
 * the emission period are the measured ring numbers; the flexible LOAD seat is
 * supported but deliberately disabled today, so a single switch turns the
 * third emission on when the owner starts requesting the load seat. */
#define TDMA_LOOP_PLAN_LOOP_PERIOD_NS 4000000u
#define TDMA_LOOP_PLAN_EMISSION_PERIOD_NS 1500000u
/* The seat contract rejects a zero close guard, so the planner must publish a
 * real one; 30 us is the current placeholder pending the owner's confirmation. */
#define TDMA_LOOP_PLAN_CLOSE_GUARD_NS 30000u
#define TDMA_LOOP_PLAN_LOAD_ENABLED 0

/* Streaming emission gate at the ring origin. 0 keeps the origin byte-for-byte
 * as before (no window decision is taken); 1 lets the priority/first-class
 * providers publish only inside their planned windows. */
#ifndef TDMA_LOOP_SCHEDULE_ENABLED
#define TDMA_LOOP_SCHEDULE_ENABLED 0
#endif

/* Fill the request from the production defaults above. load_pending is honoured
 * only while TDMA_LOOP_PLAN_LOAD_ENABLED is nonzero. */
void tdma_loop_plan_default_request(tdma_loop_plan_request_t *out,
    bool priority_pending, bool first_class_pending, bool load_pending);

/* Which seat class may emit at now_ns according to the plan. now_ns is relative
 * to the loop start; a window closes close_guard_ns before its end, so the guard
 * interval and everything outside every window answer TDMA_LOOP_SLOT_NONE. */
tdma_loop_slot_class_t tdma_loop_plan_slot_for_time(const tdma_loop_plan_t *plan,
    uint64_t now_ns);

/* Slot-index form for the origin's emission cadence: the slot at absolute
 * index slot_index owns now_ns = (slot_index * slot_period_ns) reduced into the
 * loop period. slot_period_ns == 0 answers TDMA_LOOP_SLOT_NONE. */
tdma_loop_slot_class_t tdma_loop_plan_slot_for_index(const tdma_loop_plan_t *plan,
    uint64_t slot_index, uint64_t slot_period_ns);

#endif
