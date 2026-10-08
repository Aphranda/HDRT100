#include "tdma_seat_admission_owner.h"

#include <stddef.h>
#include <string.h>

static tdma_seat_result_t record(tdma_seat_admission_owner_t *owner,
                                 tdma_seat_result_t value)
{
    if (owner == NULL) return TDMA_SEAT_BAD_ARGUMENT;
    owner->last_result = value;
    switch (value) {
        case TDMA_SEAT_BUDGET_UNDEFINED: owner->deny_budget_undefined++; break;
        case TDMA_SEAT_BUDGET_EXHAUSTED: owner->deny_budget_exhausted++; break;
        case TDMA_SEAT_WINDOW_WAIT:
        case TDMA_SEAT_WINDOW_EXPIRED: owner->deny_window++; break;
        case TDMA_SEAT_WRONG_STATE: owner->deny_wrong_state++; break;
        case TDMA_SEAT_STALE_IDENTITY: owner->deny_stale_identity++; break;
        default: break;
    }
    return value;
}

static bool same_identity(const tdma_seat_identity_t *a,
                          const tdma_seat_identity_t *b)
{
    return a != NULL && b != NULL && a->epoch == b->epoch &&
        a->cycle == b->cycle &&
        a->config_generation == b->config_generation &&
        a->profile_generation == b->profile_generation &&
        a->binding_generation == b->binding_generation &&
        a->priority_sequence == b->priority_sequence;
}

static tdma_seat_budget_t bound_budget(const tdma_seat_admission_owner_t *owner)
{
    tdma_seat_budget_t budget;
    memset(&budget, 0, sizeof(budget));
    budget.identity = owner->identity;
    budget.defined_mask = owner->resources.defined_mask;
    for (uint32_t i = 0u; i < TDMA_SEAT_RESOURCE_COUNT; ++i)
        budget.remaining[i] = owner->resources.remaining[i];
    return budget;
}

bool tdma_seat_admission_owner_init(tdma_seat_admission_owner_t *owner)
{
    if (owner == NULL) return false;
    memset(owner, 0, sizeof(*owner));
    return tdma_seat_admission_init(&owner->seat);
}

tdma_seat_result_t tdma_seat_admission_owner_bind(
    tdma_seat_admission_owner_t *owner, const tdma_seat_identity_t *identity,
    const tdma_seat_owner_resources_t *resources)
{
    if (owner == NULL || identity == NULL || resources == NULL)
        return record(owner, TDMA_SEAT_BAD_ARGUMENT);
    if (!identity->config_generation || !identity->profile_generation ||
        !identity->binding_generation)
        return record(owner, TDMA_SEAT_BAD_ARGUMENT);
    if (owner->binding.bound != 0u &&
        (owner->binding.epoch != identity->epoch ||
         owner->binding.binding_generation != identity->binding_generation)) {
        /* A new generation retires the previous window before it is bound. */
        (void)tdma_seat_admission_revoke(&owner->seat,
                                         TDMA_SEAT_REVOKE_BINDING_CHANGED);
    }
    owner->identity = *identity;
    owner->resources = *resources;
    owner->binding.bound = 1u;
    owner->binding.epoch = (uint32_t)identity->epoch;
    owner->binding.config_generation = identity->config_generation;
    owner->binding.profile_generation = identity->profile_generation;
    owner->binding.binding_generation = identity->binding_generation;
    owner->binding.defined_mask = resources->defined_mask;
    for (uint32_t i = 0u; i < TDMA_SEAT_RESOURCE_COUNT; ++i)
        owner->binding.remaining[i] = resources->remaining[i];
    return record(owner, TDMA_SEAT_OK);
}

tdma_seat_result_t tdma_seat_admission_owner_open(
    tdma_seat_admission_owner_t *owner, const tdma_seat_identity_t *identity,
    const tdma_seat_window_t *window, uint64_t now_ns)
{
    if (owner == NULL || identity == NULL || window == NULL)
        return record(owner, TDMA_SEAT_BAD_ARGUMENT);
    if (owner->binding.bound == 0u)
        return record(owner, TDMA_SEAT_BUDGET_UNDEFINED);
    if (!same_identity(&owner->identity, identity))
        return record(owner, TDMA_SEAT_STALE_IDENTITY);
    const tdma_seat_result_t opened =
        tdma_seat_admission_begin(&owner->seat, identity, window, now_ns);
    if (opened == TDMA_SEAT_OK) {
        owner->identity = *identity;
        owner->open_count++;
    }
    return record(owner, opened);
}

tdma_seat_result_t tdma_seat_admission_owner_priority_selected(
    tdma_seat_admission_owner_t *owner, uint32_t selected_token, uint64_t now_ns)
{
    if (owner == NULL) return TDMA_SEAT_BAD_ARGUMENT;
    const tdma_seat_result_t value = tdma_seat_admission_priority_selected(
        &owner->seat, &owner->identity, selected_token, now_ns);
    if (value == TDMA_SEAT_OK) owner->selected_reports++;
    return record(owner, value);
}

tdma_seat_result_t tdma_seat_admission_owner_priority_committed(
    tdma_seat_admission_owner_t *owner, uint32_t selected_token, uint64_t now_ns)
{
    if (owner == NULL) return TDMA_SEAT_BAD_ARGUMENT;
    const tdma_seat_result_t value = tdma_seat_admission_priority_committed(
        &owner->seat, &owner->identity, selected_token, now_ns);
    if (value == TDMA_SEAT_OK) owner->commit_reports++;
    return record(owner, value);
}

tdma_seat_result_t tdma_seat_admission_owner_grant(
    tdma_seat_admission_owner_t *owner, const tdma_seat_request_t *request,
    uint64_t now_ns)
{
    if (owner == NULL || request == NULL)
        return record(owner, TDMA_SEAT_BAD_ARGUMENT);
    if (owner->binding.bound == 0u)
        return record(owner, TDMA_SEAT_BUDGET_UNDEFINED);
    const tdma_seat_budget_t budget = bound_budget(owner);
    const tdma_seat_result_t value =
        tdma_seat_admission_grant(&owner->seat, &budget, request, now_ns);
    if (value == TDMA_SEAT_OK) owner->grant_count++;
    return record(owner, value);
}

tdma_seat_result_t tdma_seat_admission_owner_select_load(
    tdma_seat_admission_owner_t *owner, uint32_t load_id, uint64_t now_ns)
{
    if (owner == NULL) return TDMA_SEAT_BAD_ARGUMENT;
    if (owner->binding.bound == 0u)
        return record(owner, TDMA_SEAT_BUDGET_UNDEFINED);
    const tdma_seat_budget_t budget = bound_budget(owner);
    const tdma_seat_result_t value = tdma_seat_admission_select_load(
        &owner->seat, &budget, load_id, now_ns);
    if (value == TDMA_SEAT_OK) owner->select_count++;
    return record(owner, value);
}

tdma_seat_result_t tdma_seat_admission_owner_commit_load(
    tdma_seat_admission_owner_t *owner, uint32_t load_id, uint64_t now_ns)
{
    if (owner == NULL) return TDMA_SEAT_BAD_ARGUMENT;
    const tdma_seat_result_t value = tdma_seat_admission_commit_load(
        &owner->seat, &owner->identity, load_id, now_ns);
    if (value == TDMA_SEAT_OK) owner->load_commit_count++;
    return record(owner, value);
}

tdma_seat_result_t tdma_seat_admission_owner_tick(
    tdma_seat_admission_owner_t *owner, uint64_t now_ns)
{
    if (owner == NULL) return TDMA_SEAT_BAD_ARGUMENT;
    return record(owner, tdma_seat_admission_expire(&owner->seat, now_ns));
}

tdma_seat_result_t tdma_seat_admission_owner_revoke(
    tdma_seat_admission_owner_t *owner, tdma_seat_revoke_reason_t reason)
{
    if (owner == NULL) return TDMA_SEAT_BAD_ARGUMENT;
    if (!tdma_seat_admission_revoke(&owner->seat, reason))
        return record(owner, TDMA_SEAT_BAD_ARGUMENT);
    return record(owner, TDMA_SEAT_OK);
}

void tdma_seat_admission_owner_snapshot(
    const tdma_seat_admission_owner_t *owner,
    tdma_seat_admission_owner_snapshot_t *snapshot)
{
    if (owner == NULL || snapshot == NULL) return;
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->bound = owner->binding.bound;
    snapshot->state = owner->seat.state;
    snapshot->last_result = owner->last_result;
    snapshot->revoke_reason = owner->seat.revoke_reason;
    snapshot->identity = owner->identity;
    snapshot->binding = owner->binding;
    snapshot->selected_token = owner->seat.selected_token;
    snapshot->load_id = owner->seat.request.load_id;
    snapshot->priority_committed = owner->seat.priority_committed ? 1u : 0u;
    snapshot->load_committed = owner->seat.load_committed ? 1u : 0u;
    snapshot->open_count = owner->open_count;
    snapshot->selected_reports = owner->selected_reports;
    snapshot->commit_reports = owner->commit_reports;
    snapshot->grant_count = owner->grant_count;
    snapshot->select_count = owner->select_count;
    snapshot->load_commit_count = owner->load_commit_count;
    snapshot->deny_budget_undefined = owner->deny_budget_undefined;
    snapshot->deny_budget_exhausted = owner->deny_budget_exhausted;
    snapshot->deny_window = owner->deny_window;
    snapshot->deny_wrong_state = owner->deny_wrong_state;
    snapshot->deny_stale_identity = owner->deny_stale_identity;
}
