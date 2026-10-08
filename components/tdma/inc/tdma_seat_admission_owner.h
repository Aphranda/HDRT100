#ifndef TDMA_SEAT_ADMISSION_OWNER_H
#define TDMA_SEAT_ADMISSION_OWNER_H

#include "tdma_seat_admission.h"

#include <stdbool.h>
#include <stdint.h>

/* Owner-side wiring for the seat admission model.
 *
 * The model answers state questions; this layer is what the TDMA runtime owner
 * actually holds. It binds one measured resource table to one window identity
 * and forwards the physical facts in the order they really occur:
 *
 *   open            <- configuration generation installed, window known
 *   priority_selected <- an exact image/token was selected
 *   priority_committed<- the backend confirmed the local handoff (not SENT)
 *   grant           <- a first-class load request is admitted against budget
 *   select_load     <- revalidated immediately before physical submission
 *   commit_load     <- the backend confirmed that exact load's handoff
 *
 * Nothing here opens a window, submits a frame or claims delivery, and no
 * resource is invented: a dimension the owner has not measured stays out of
 * `defined_mask`, and the model then refuses admission instead of assuming
 * headroom. That refusal is counted, per dimension class, as evidence. */
typedef struct {
    /* Bits are `TDMA_SEAT_*` resource indices measured for this generation.
     * A bit set here asserts a real bound, never a nominal or free-slot guess. */
    uint32_t defined_mask;
    uint32_t remaining[TDMA_SEAT_RESOURCE_COUNT];
} tdma_seat_owner_resources_t;

typedef struct {
    uint32_t bound;
    uint32_t epoch;
    uint32_t config_generation;
    uint32_t profile_generation;
    uint32_t binding_generation;
    uint32_t defined_mask;
    uint32_t remaining[TDMA_SEAT_RESOURCE_COUNT];
} tdma_seat_owner_binding_t;

typedef struct {
    tdma_seat_admission_t seat;
    tdma_seat_identity_t identity;
    tdma_seat_owner_resources_t resources;
    tdma_seat_owner_binding_t binding;
    /* Evidence counters: what the physical side reported and why the owner
     * refused. Selection is never counted as delivery and vice versa. */
    uint32_t open_count;
    uint32_t selected_reports;
    uint32_t commit_reports;
    uint32_t grant_count;
    uint32_t select_count;
    uint32_t load_commit_count;
    uint32_t deny_budget_undefined;
    uint32_t deny_budget_exhausted;
    uint32_t deny_window;
    uint32_t deny_wrong_state;
    uint32_t deny_stale_identity;
    tdma_seat_result_t last_result;
} tdma_seat_admission_owner_t;

typedef struct {
    uint32_t bound;
    tdma_seat_state_t state;
    tdma_seat_result_t last_result;
    tdma_seat_revoke_reason_t revoke_reason;
    tdma_seat_identity_t identity;
    tdma_seat_owner_binding_t binding;
    uint32_t selected_token;
    uint32_t load_id;
    uint32_t priority_committed;
    uint32_t load_committed;
    uint32_t open_count;
    uint32_t selected_reports;
    uint32_t commit_reports;
    uint32_t grant_count;
    uint32_t select_count;
    uint32_t load_commit_count;
    uint32_t deny_budget_undefined;
    uint32_t deny_budget_exhausted;
    uint32_t deny_window;
    uint32_t deny_wrong_state;
    uint32_t deny_stale_identity;
} tdma_seat_admission_owner_snapshot_t;

bool tdma_seat_admission_owner_init(tdma_seat_admission_owner_t *owner);

/* Bind one measured resource table to one window identity. The binding is what
 * later grants are judged against; rebinding a different identity retires the
 * previous window through the model, and a binding with no defined dimension is
 * accepted so the refusal, not an accidental permissive default, is visible. */
tdma_seat_result_t tdma_seat_admission_owner_bind(
    tdma_seat_admission_owner_t *owner, const tdma_seat_identity_t *identity,
    const tdma_seat_owner_resources_t *resources);

/* Open a window. The bound identity must match the window identity: a window
 * opened under another generation is a stale identity, not a fresh start. */
tdma_seat_result_t tdma_seat_admission_owner_open(
    tdma_seat_admission_owner_t *owner, const tdma_seat_identity_t *identity,
    const tdma_seat_window_t *window, uint64_t now_ns);

tdma_seat_result_t tdma_seat_admission_owner_priority_selected(
    tdma_seat_admission_owner_t *owner, uint32_t selected_token, uint64_t now_ns);
tdma_seat_result_t tdma_seat_admission_owner_priority_committed(
    tdma_seat_admission_owner_t *owner, uint32_t selected_token, uint64_t now_ns);

/* Admit a first-class load request against the bound table. Undefined budget
 * dimensions are refused here and counted; the request is never widened. */
tdma_seat_result_t tdma_seat_admission_owner_grant(
    tdma_seat_admission_owner_t *owner, const tdma_seat_request_t *request,
    uint64_t now_ns);
tdma_seat_result_t tdma_seat_admission_owner_select_load(
    tdma_seat_admission_owner_t *owner, uint32_t load_id, uint64_t now_ns);
tdma_seat_result_t tdma_seat_admission_owner_commit_load(
    tdma_seat_admission_owner_t *owner, uint32_t load_id, uint64_t now_ns);

/* Decision boundary and revocation, forwarded with evidence counting. */
tdma_seat_result_t tdma_seat_admission_owner_tick(
    tdma_seat_admission_owner_t *owner, uint64_t now_ns);
tdma_seat_result_t tdma_seat_admission_owner_revoke(
    tdma_seat_admission_owner_t *owner, tdma_seat_revoke_reason_t reason);

void tdma_seat_admission_owner_snapshot(
    const tdma_seat_admission_owner_t *owner,
    tdma_seat_admission_owner_snapshot_t *snapshot);

#endif
