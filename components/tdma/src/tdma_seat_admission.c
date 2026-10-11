#include "tdma_seat_admission.h"

#include <stddef.h>
#include <string.h>

static tdma_seat_result_t result(tdma_seat_admission_t *seat, tdma_seat_result_t value)
{
    if (seat != NULL) seat->last_result = value;
    return value;
}

static bool same_identity(const tdma_seat_identity_t *a, const tdma_seat_identity_t *b)
{
    return a != NULL && b != NULL && a->epoch == b->epoch && a->cycle == b->cycle &&
        a->config_generation == b->config_generation &&
        a->profile_generation == b->profile_generation &&
        a->binding_generation == b->binding_generation &&
        a->priority_sequence == b->priority_sequence;
}

bool tdma_seat_admission_init(tdma_seat_admission_t *seat)
{
    if (seat == NULL) return false;
    memset(seat, 0, sizeof(*seat));
    return true;
}

bool tdma_seat_admission_revoke(tdma_seat_admission_t *seat,
    tdma_seat_revoke_reason_t reason)
{
    if (seat == NULL || reason < TDMA_SEAT_REVOKE_STOP ||
        reason > TDMA_SEAT_REVOKE_BUDGET) return false;
    seat->state = TDMA_SEAT_REVOKED;
    seat->revoke_reason = reason;
    return true;
}

tdma_seat_result_t tdma_seat_admission_expire(tdma_seat_admission_t *seat, uint64_t now_ns)
{
    if (seat == NULL) return TDMA_SEAT_BAD_ARGUMENT;
    if (seat->state == TDMA_SEAT_CLOSED || seat->state == TDMA_SEAT_REVOKED)
        return result(seat, TDMA_SEAT_WRONG_STATE);
    if (now_ns < seat->last_time_ns) {
        tdma_seat_admission_revoke(seat, TDMA_SEAT_REVOKE_FAULT);
        return result(seat, TDMA_SEAT_TIME_INVALID);
    }
    seat->last_time_ns = now_ns;
    if (now_ns >= seat->window.end_ns - seat->window.close_guard_ns) {
        tdma_seat_admission_revoke(seat, TDMA_SEAT_REVOKE_WINDOW);
        return result(seat, TDMA_SEAT_WINDOW_EXPIRED);
    }
    return result(seat, TDMA_SEAT_OK);
}

tdma_seat_result_t tdma_seat_admission_begin(tdma_seat_admission_t *seat,
    const tdma_seat_identity_t *identity, const tdma_seat_window_t *window, uint64_t now_ns)
{
    if (seat == NULL || identity == NULL || window == NULL)
        return result(seat, TDMA_SEAT_BAD_ARGUMENT);
    /* Callers may reuse the current window/identity stored in this object. */
    const tdma_seat_identity_t next_identity = *identity;
    const tdma_seat_window_t next_window = *window;
    identity = &next_identity;
    window = &next_window;
    if (identity->epoch <= seat->epoch_high_water)
        return result(seat, TDMA_SEAT_STALE_IDENTITY);
    seat->epoch_high_water = identity->epoch;
    /* A new owner epoch always retires an old grant, even if its inputs fail. */
    tdma_seat_admission_revoke(seat, TDMA_SEAT_REVOKE_BINDING_CHANGED);
    if (!identity->config_generation || !identity->profile_generation ||
        !identity->binding_generation || window->end_ns <= window->start_ns ||
        !window->close_guard_ns ||
        window->close_guard_ns >= window->end_ns - window->start_ns)
        return result(seat, TDMA_SEAT_BAD_ARGUMENT);
    if (now_ns < seat->last_time_ns) return result(seat, TDMA_SEAT_TIME_INVALID);
    if (now_ns >= window->end_ns - window->close_guard_ns)
        return result(seat, TDMA_SEAT_WINDOW_EXPIRED);
    memset(seat, 0, sizeof(*seat));
    seat->identity = *identity;
    seat->epoch_high_water = identity->epoch;
    seat->window = *window;
    seat->last_time_ns = now_ns;
    seat->state = TDMA_SEAT_WAIT_PRIORITY;
    return TDMA_SEAT_OK;
}

static tdma_seat_result_t current(tdma_seat_admission_t *seat,
    const tdma_seat_identity_t *identity, uint64_t now_ns)
{
    if (seat == NULL || identity == NULL) return result(seat, TDMA_SEAT_BAD_ARGUMENT);
    if (!same_identity(&seat->identity, identity))
        return result(seat, TDMA_SEAT_STALE_IDENTITY);
    return tdma_seat_admission_expire(seat, now_ns);
}

tdma_seat_result_t tdma_seat_admission_priority_selected(tdma_seat_admission_t *seat,
    const tdma_seat_identity_t *identity, uint32_t selected_token, uint64_t now_ns)
{
    tdma_seat_result_t checked = current(seat, identity, now_ns);
    if (checked != TDMA_SEAT_OK) return checked;
    if (!selected_token) return result(seat, TDMA_SEAT_BAD_ARGUMENT);
    if (seat->selected_token) {
        if (seat->selected_token == selected_token) return result(seat, TDMA_SEAT_DUPLICATE);
        tdma_seat_admission_revoke(seat, TDMA_SEAT_REVOKE_NEW_PRIORITY);
        return result(seat, TDMA_SEAT_STALE_IDENTITY);
    }
    if (seat->state != TDMA_SEAT_WAIT_PRIORITY) return result(seat, TDMA_SEAT_WRONG_STATE);
    seat->selected_token = selected_token;
    seat->selected_time_ns = now_ns;
    seat->state = TDMA_SEAT_PRIORITY_SELECTED;
    return result(seat, TDMA_SEAT_OK);
}

tdma_seat_result_t tdma_seat_admission_priority_committed(tdma_seat_admission_t *seat,
    const tdma_seat_identity_t *identity, uint32_t selected_token, uint64_t now_ns)
{
    tdma_seat_result_t checked = current(seat, identity, now_ns);
    if (checked != TDMA_SEAT_OK) return checked;
    if (!selected_token || selected_token != seat->selected_token)
        return result(seat, TDMA_SEAT_STALE_IDENTITY);
    if (seat->priority_committed) return result(seat, TDMA_SEAT_DUPLICATE);
    if (seat->state != TDMA_SEAT_PRIORITY_SELECTED) return result(seat, TDMA_SEAT_WRONG_STATE);
    seat->priority_committed = true;
    seat->priority_commit_time_ns = now_ns;
    seat->state = TDMA_SEAT_PRIORITY_TX_COMMITTED;
    return result(seat, TDMA_SEAT_OK);
}

static tdma_seat_result_t fits(tdma_seat_admission_t *seat,
    const tdma_seat_budget_t *budget, const tdma_seat_request_t *request, uint64_t now_ns)
{
    if (budget->defined_mask != TDMA_SEAT_BUDGET_ALL) return TDMA_SEAT_BUDGET_UNDEFINED;
    for (uint32_t i = 0; i < TDMA_SEAT_RESOURCE_COUNT; ++i)
        if (request->cost[i] > budget->remaining[i]) return TDMA_SEAT_BUDGET_EXHAUSTED;
    if (now_ns < seat->window.start_ns) return TDMA_SEAT_WINDOW_WAIT;
    /* Subtract, never add to a potentially near-UINT64_MAX timestamp. */
    if (request->duration_ns > seat->window.end_ns - seat->window.close_guard_ns - now_ns)
        return TDMA_SEAT_WINDOW_EXPIRED;
    return TDMA_SEAT_OK;
}

static bool same_request(const tdma_seat_request_t *a, const tdma_seat_request_t *b)
{
    if (a->load_id != b->load_id || a->duration_ns != b->duration_ns) return false;
    for (uint32_t i = 0; i < TDMA_SEAT_RESOURCE_COUNT; ++i)
        if (a->cost[i] != b->cost[i]) return false;
    return true;
}

static tdma_seat_result_t deny_grant(tdma_seat_admission_t *seat, tdma_seat_result_t why)
{
    if (seat->state == TDMA_SEAT_FIRST_CLASS_WINDOW_GRANTED)
        tdma_seat_admission_revoke(seat, why == TDMA_SEAT_WINDOW_EXPIRED
            ? TDMA_SEAT_REVOKE_WINDOW : TDMA_SEAT_REVOKE_BUDGET);
    return result(seat, why);
}

tdma_seat_result_t tdma_seat_admission_grant(tdma_seat_admission_t *seat,
    const tdma_seat_budget_t *budget, const tdma_seat_request_t *request, uint64_t now_ns)
{
    if (budget == NULL || request == NULL) return result(seat, TDMA_SEAT_BAD_ARGUMENT);
    tdma_seat_result_t checked = current(seat, &budget->identity, now_ns);
    if (checked != TDMA_SEAT_OK) return checked;
    if (seat->state != TDMA_SEAT_PRIORITY_TX_COMMITTED &&
        seat->state != TDMA_SEAT_FIRST_CLASS_WINDOW_GRANTED)
        return result(seat, TDMA_SEAT_WRONG_STATE);
    if (!request->load_id || !request->duration_ns ||
        !request->cost[TDMA_SEAT_TX_BYTES] || !request->cost[TDMA_SEAT_TX_FRAMES])
        return result(seat, TDMA_SEAT_BAD_ARGUMENT);
    if (seat->state == TDMA_SEAT_FIRST_CLASS_WINDOW_GRANTED &&
        !same_request(request, &seat->request)) return result(seat, TDMA_SEAT_WRONG_STATE);
    checked = fits(seat, budget, request, now_ns);
    if (checked != TDMA_SEAT_OK) return deny_grant(seat, checked);
    if (seat->state == TDMA_SEAT_FIRST_CLASS_WINDOW_GRANTED)
        return result(seat, TDMA_SEAT_DUPLICATE);
    seat->budget = *budget;
    seat->request = *request;
    seat->state = TDMA_SEAT_FIRST_CLASS_WINDOW_GRANTED;
    return result(seat, TDMA_SEAT_OK);
}

tdma_seat_result_t tdma_seat_admission_select_load(tdma_seat_admission_t *seat,
    const tdma_seat_budget_t *live_budget, uint32_t load_id, uint64_t now_ns)
{
    if (live_budget == NULL) return result(seat, TDMA_SEAT_BAD_ARGUMENT);
    tdma_seat_result_t checked = current(seat, &live_budget->identity, now_ns);
    if (checked != TDMA_SEAT_OK) return checked;
    if (!load_id || load_id != seat->request.load_id)
        return result(seat, TDMA_SEAT_STALE_IDENTITY);
    if (seat->state == TDMA_SEAT_LOAD_SELECTED || seat->state == TDMA_SEAT_LOAD_TX_COMMITTED)
        return result(seat, TDMA_SEAT_DUPLICATE);
    if (seat->state != TDMA_SEAT_FIRST_CLASS_WINDOW_GRANTED) return result(seat, TDMA_SEAT_WRONG_STATE);
    checked = fits(seat, live_budget, &seat->request, now_ns);
    if (checked != TDMA_SEAT_OK) return deny_grant(seat, checked);
    seat->budget = *live_budget;
    for (uint32_t i = 0; i < TDMA_SEAT_RESOURCE_COUNT; ++i)
        seat->budget.remaining[i] -= seat->request.cost[i];
    seat->load_selected_time_ns = now_ns;
    seat->state = TDMA_SEAT_LOAD_SELECTED;
    return result(seat, TDMA_SEAT_OK);
}

tdma_seat_result_t tdma_seat_admission_commit_load(tdma_seat_admission_t *seat,
    const tdma_seat_identity_t *identity, uint32_t load_id, uint64_t now_ns)
{
    if (seat == NULL || identity == NULL) return result(seat, TDMA_SEAT_BAD_ARGUMENT);
    if (!same_identity(&seat->identity, identity))
        return result(seat, TDMA_SEAT_STALE_IDENTITY);
    if (!load_id || load_id != seat->request.load_id)
        return result(seat, TDMA_SEAT_STALE_IDENTITY);
    if (seat->state == TDMA_SEAT_LOAD_TX_COMMITTED) return result(seat, TDMA_SEAT_DUPLICATE);
    if (seat->state != TDMA_SEAT_LOAD_SELECTED) return result(seat, TDMA_SEAT_WRONG_STATE);
    if (now_ns < seat->last_time_ns) {
        tdma_seat_admission_revoke(seat, TDMA_SEAT_REVOKE_FAULT);
        return result(seat, TDMA_SEAT_TIME_INVALID);
    }
    /* Retain a real late handoff as evidence while denying its timing. */
    seat->load_commit_time_ns = now_ns;
    seat->load_committed = true;
    tdma_seat_result_t checked = tdma_seat_admission_expire(seat, now_ns);
    if (checked != TDMA_SEAT_OK) return checked;
    if (seat->request.duration_ns >
        seat->window.end_ns - seat->window.close_guard_ns - now_ns) {
        tdma_seat_admission_revoke(seat, TDMA_SEAT_REVOKE_WINDOW);
        return result(seat, TDMA_SEAT_WINDOW_EXPIRED);
    }
    seat->state = TDMA_SEAT_LOAD_TX_COMMITTED;
    return result(seat, TDMA_SEAT_OK);
}

tdma_seat_result_t tdma_loop_plan(const tdma_loop_plan_request_t *request,
    tdma_loop_plan_t *out)
{
    if (request == NULL || out == NULL || request->loop_period_ns == 0u ||
        request->emission_period_ns == 0u ||
        request->emission_period_ns > request->loop_period_ns)
        return TDMA_SEAT_BAD_ARGUMENT;
    out->count = 0u;
    out->loop_period_ns = request->loop_period_ns;
    out->dropped_mask = 0u;
    for (uint32_t index = 0u; index < 3u; ++index) {
        out->slots[index].cls = TDMA_LOOP_SLOT_NONE;
        out->slots[index].window.start_ns = 0u;
        out->slots[index].window.end_ns = 0u;
        out->slots[index].window.close_guard_ns = 0u;
    }
    const tdma_loop_slot_class_t classes[3] = {TDMA_LOOP_SLOT_PRIORITY,
        TDMA_LOOP_SLOT_FIRST_CLASS, TDMA_LOOP_SLOT_LOAD};
    const bool pending[3] = {request->priority_pending,
        request->first_class_pending, request->load_pending};
    const uint64_t guard = (uint64_t)request->close_guard_ns;
    uint64_t cursor_ns = request->start_offset_ns;
    for (uint32_t index = 0u; index < 3u; ++index) {
        if (!pending[index]) continue;
        const uint64_t width_ns = request->emission_period_ns;
        if (cursor_ns + width_ns + guard > request->loop_period_ns) {
            /* No room left: every remaining requested seat is dropped, not just
             * the one that failed to fit. Report it so a caller never defers a
             * mounted load forever without evidence. */
            for (uint32_t rest = index; rest < 3u; ++rest)
                if (pending[rest])
                    out->dropped_mask |= tdma_loop_plan_seat_bit(classes[rest]);
            break;
        }
        tdma_loop_slot_t *slot = &out->slots[out->count];
        slot->cls = classes[index];
        slot->window.start_ns = cursor_ns;
        slot->window.end_ns = cursor_ns + width_ns;
        slot->window.close_guard_ns = request->close_guard_ns;
        cursor_ns += width_ns;
        out->count++;
    }
    return TDMA_SEAT_OK;
}

void tdma_loop_plan_default_request(tdma_loop_plan_request_t *out,
    bool priority_pending, bool first_class_pending, bool load_pending)
{
    if (out == NULL) return;
    out->loop_period_ns = (uint64_t)TDMA_LOOP_PLAN_LOOP_PERIOD_NS;
    out->emission_period_ns = (uint64_t)TDMA_LOOP_PLAN_EMISSION_PERIOD_NS;
    out->close_guard_ns = TDMA_LOOP_PLAN_CLOSE_GUARD_NS;
    out->priority_pending = priority_pending;
    out->first_class_pending = first_class_pending;
    out->load_pending = TDMA_LOOP_PLAN_LOAD_ENABLED != 0 ? load_pending : false;
    out->start_offset_ns = 0u;
}

tdma_loop_slot_class_t tdma_loop_plan_slot_for_time(const tdma_loop_plan_t *plan,
    uint64_t now_ns)
{
    if (plan == NULL) return TDMA_LOOP_SLOT_NONE;
    for (uint32_t index = 0u; index < plan->count && index < 3u; ++index) {
        const tdma_loop_slot_t *slot = &plan->slots[index];
        const uint64_t close_ns = slot->window.end_ns - slot->window.close_guard_ns;
        if (now_ns >= slot->window.start_ns && now_ns < close_ns) return slot->cls;
    }
    return TDMA_LOOP_SLOT_NONE;
}

tdma_loop_slot_class_t tdma_loop_plan_slot_for_index(const tdma_loop_plan_t *plan,
    uint64_t slot_index, uint64_t slot_period_ns)
{
    if (plan == NULL || slot_period_ns == 0u || plan->loop_period_ns == 0u)
        return TDMA_LOOP_SLOT_NONE;
    const uint64_t now_ns = (slot_index * slot_period_ns) % plan->loop_period_ns;
    return tdma_loop_plan_slot_for_time(plan, now_ns);
}
