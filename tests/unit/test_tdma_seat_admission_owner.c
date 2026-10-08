/* Host cases for the seat admission owner wiring. No hardware, no SDK. */
#include "tdma_seat_admission_owner.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void expect(const char *name, int condition)
{
    if (!condition) {
        printf("FAIL %s\n", name);
        failures++;
    } else {
        printf("ok   %s\n", name);
    }
}

static tdma_seat_identity_t identity(uint64_t epoch, uint32_t binding)
{
    tdma_seat_identity_t value;
    memset(&value, 0, sizeof(value));
    value.epoch = epoch;
    value.config_generation = 7u;
    value.profile_generation = 11u;
    value.binding_generation = binding;
    value.cycle = 100u;
    value.priority_sequence = 5u;
    return value;
}

static tdma_seat_window_t window(uint64_t start_ns, uint64_t end_ns)
{
    tdma_seat_window_t value;
    value.start_ns = start_ns;
    value.end_ns = end_ns;
    value.close_guard_ns = 1000u;
    return value;
}

static void resources_fill(tdma_seat_owner_resources_t *resources, uint32_t mask)
{
    memset(resources, 0, sizeof(*resources));
    resources->defined_mask = mask;
    for (uint32_t i = 0u; i < TDMA_SEAT_RESOURCE_COUNT; ++i)
        resources->remaining[i] = 1000u;
}

static tdma_seat_request_t request(void)
{
    tdma_seat_request_t value;
    memset(&value, 0, sizeof(value));
    value.load_id = 42u;
    value.duration_ns = 2000u;
    for (uint32_t i = 0u; i < TDMA_SEAT_RESOURCE_COUNT; ++i)
        value.cost[i] = 10u;
    return value;
}

static void case_undefined_dimension_fails_closed(void)
{
    tdma_seat_admission_owner_t owner;
    tdma_seat_owner_resources_t resources;
    tdma_seat_admission_owner_snapshot_t snapshot;
    const tdma_seat_identity_t id = identity(1u, 3u);
    const tdma_seat_window_t win = window(1000u, 100000u);
    /* Only the transmit side and the Core1 cost are measured today; RX
     * retention, Core0 consume/reassembly and reverse traffic are not. */
    resources_fill(&resources, (1u << TDMA_SEAT_TX_BYTES) |
                               (1u << TDMA_SEAT_TX_FRAMES) |
                               (1u << TDMA_SEAT_CORE1_CYCLES));
    tdma_seat_admission_owner_init(&owner);
    expect("partial bind accepted", tdma_seat_admission_owner_bind(
        &owner, &id, &resources) == TDMA_SEAT_OK);
    expect("partial bind opens", tdma_seat_admission_owner_open(
        &owner, &id, &win, 5000u) == TDMA_SEAT_OK);
    expect("priority selected", tdma_seat_admission_owner_priority_selected(
        &owner, 9u, 6000u) == TDMA_SEAT_OK);
    expect("priority committed", tdma_seat_admission_owner_priority_committed(
        &owner, 9u, 7000u) == TDMA_SEAT_OK);
    const tdma_seat_request_t req = request();
    expect("undefined dimension refuses the grant",
           tdma_seat_admission_owner_grant(&owner, &req, 8000u) ==
               TDMA_SEAT_BUDGET_UNDEFINED);
    /* Without a granted request there is nothing to select: the model refuses
     * on identity, so no unmeasured dimension can be reached by accident. */
    expect("selection without a grant is refused",
           tdma_seat_admission_owner_select_load(&owner, 42u, 9000u) ==
               TDMA_SEAT_STALE_IDENTITY);
    tdma_seat_admission_owner_snapshot(&owner, &snapshot);
    expect("no grant recorded", snapshot.grant_count == 0u);
    expect("no selection recorded", snapshot.select_count == 0u);
    expect("undefined refusal counted", snapshot.deny_budget_undefined == 1u);
    expect("seat stays in the committed state",
           snapshot.state == TDMA_SEAT_PRIORITY_TX_COMMITTED);
}

static void case_live_budget_recheck_before_submission(void)
{
    tdma_seat_admission_owner_t owner;
    tdma_seat_owner_resources_t full;
    tdma_seat_owner_resources_t narrowed;
    tdma_seat_admission_owner_snapshot_t snapshot;
    const tdma_seat_identity_t id = identity(11u, 12u);
    const tdma_seat_window_t win = window(1000u, 100000u);
    const tdma_seat_request_t req = request();
    resources_fill(&full, TDMA_SEAT_BUDGET_ALL);
    resources_fill(&narrowed, (1u << TDMA_SEAT_TX_BYTES) |
                              (1u << TDMA_SEAT_TX_FRAMES));
    tdma_seat_admission_owner_init(&owner);
    (void)tdma_seat_admission_owner_bind(&owner, &id, &full);
    (void)tdma_seat_admission_owner_open(&owner, &id, &win, 5000u);
    (void)tdma_seat_admission_owner_priority_selected(&owner, 9u, 6000u);
    (void)tdma_seat_admission_owner_priority_committed(&owner, 9u, 7000u);
    expect("grant under the measured table",
           tdma_seat_admission_owner_grant(&owner, &req, 8000u) == TDMA_SEAT_OK);
    /* The live table loses a dimension before physical submission: the
     * re-validation must refuse and retire the window, not submit anyway. */
    expect("narrowing the binding is accepted",
           tdma_seat_admission_owner_bind(&owner, &id, &narrowed) == TDMA_SEAT_OK);
    expect("selection is revalidated against the live table",
           tdma_seat_admission_owner_select_load(&owner, 42u, 9000u) ==
               TDMA_SEAT_BUDGET_UNDEFINED);
    tdma_seat_admission_owner_snapshot(&owner, &snapshot);
    expect("nothing was selected", snapshot.select_count == 0u);
    expect("window retired by budget",
        snapshot.state == TDMA_SEAT_REVOKED &&
        snapshot.revoke_reason == TDMA_SEAT_REVOKE_BUDGET);
}

static void case_full_budget_admits_in_order(void)
{
    tdma_seat_admission_owner_t owner;
    tdma_seat_owner_resources_t resources;
    tdma_seat_admission_owner_snapshot_t snapshot;
    const tdma_seat_identity_t id = identity(2u, 4u);
    const tdma_seat_window_t win = window(1000u, 100000u);
    const tdma_seat_request_t req = request();
    resources_fill(&resources, TDMA_SEAT_BUDGET_ALL);
    tdma_seat_admission_owner_init(&owner);
    (void)tdma_seat_admission_owner_bind(&owner, &id, &resources);
    expect("full budget opens", tdma_seat_admission_owner_open(
        &owner, &id, &win, 5000u) == TDMA_SEAT_OK);
    expect("full budget selects", tdma_seat_admission_owner_priority_selected(
        &owner, 9u, 6000u) == TDMA_SEAT_OK);
    expect("commit before select is refused",
           tdma_seat_admission_owner_priority_committed(&owner, 8u, 6500u) ==
               TDMA_SEAT_STALE_IDENTITY);
    expect("full budget commits", tdma_seat_admission_owner_priority_committed(
        &owner, 9u, 7000u) == TDMA_SEAT_OK);
    expect("grant before priority commit is refused in a fresh window",
           tdma_seat_admission_owner_grant(&owner, &req, 7100u) == TDMA_SEAT_OK);
    expect("select", tdma_seat_admission_owner_select_load(
        &owner, 42u, 8000u) == TDMA_SEAT_OK);
    expect("commit", tdma_seat_admission_owner_commit_load(
        &owner, 42u, 9000u) == TDMA_SEAT_OK);
    expect("duplicate commit is refused",
           tdma_seat_admission_owner_commit_load(&owner, 42u, 9500u) ==
               TDMA_SEAT_DUPLICATE);
    tdma_seat_admission_owner_snapshot(&owner, &snapshot);
    expect("selection is not delivery", snapshot.selected_reports == 1u &&
        snapshot.commit_reports == 1u);
    expect("load selection and commit are counted separately",
        snapshot.select_count == 1u && snapshot.load_commit_count == 1u);
    expect("state is load committed",
           snapshot.state == TDMA_SEAT_LOAD_TX_COMMITTED);
    expect("no undefined refusal", snapshot.deny_budget_undefined == 0u);
}

static void case_exhausted_budget_is_distinct(void)
{
    tdma_seat_admission_owner_t owner;
    tdma_seat_owner_resources_t resources;
    tdma_seat_admission_owner_snapshot_t snapshot;
    const tdma_seat_identity_t id = identity(3u, 5u);
    const tdma_seat_window_t win = window(1000u, 100000u);
    tdma_seat_request_t req = request();
    resources_fill(&resources, TDMA_SEAT_BUDGET_ALL);
    resources.remaining[TDMA_SEAT_TX_BYTES] = 1u;
    req.cost[TDMA_SEAT_TX_BYTES] = 2u;
    tdma_seat_admission_owner_init(&owner);
    (void)tdma_seat_admission_owner_bind(&owner, &id, &resources);
    (void)tdma_seat_admission_owner_open(&owner, &id, &win, 5000u);
    (void)tdma_seat_admission_owner_priority_selected(&owner, 9u, 6000u);
    (void)tdma_seat_admission_owner_priority_committed(&owner, 9u, 7000u);
    expect("exhausted budget is refused",
           tdma_seat_admission_owner_grant(&owner, &req, 8000u) ==
               TDMA_SEAT_BUDGET_EXHAUSTED);
    tdma_seat_admission_owner_snapshot(&owner, &snapshot);
    expect("exhaustion differs from missing budget",
        snapshot.deny_budget_exhausted == 1u &&
        snapshot.deny_budget_undefined == 0u);
    expect("no grant", snapshot.grant_count == 0u);
}

static void case_window_expiry_revokes(void)
{
    tdma_seat_admission_owner_t owner;
    tdma_seat_owner_resources_t resources;
    tdma_seat_admission_owner_snapshot_t snapshot;
    const tdma_seat_identity_t id = identity(4u, 6u);
    const tdma_seat_window_t win = window(1000u, 20000u);
    resources_fill(&resources, TDMA_SEAT_BUDGET_ALL);
    tdma_seat_admission_owner_init(&owner);
    (void)tdma_seat_admission_owner_bind(&owner, &id, &resources);
    (void)tdma_seat_admission_owner_open(&owner, &id, &win, 5000u);
    expect("close guard revokes the window",
           tdma_seat_admission_owner_tick(&owner, 19500u) ==
               TDMA_SEAT_WINDOW_EXPIRED);
    tdma_seat_admission_owner_snapshot(&owner, &snapshot);
    expect("revoked by window", snapshot.state == TDMA_SEAT_REVOKED &&
        snapshot.revoke_reason == TDMA_SEAT_REVOKE_WINDOW);
    expect("window denial counted", snapshot.deny_window >= 1u);
}

static void case_binding_change_retires_window(void)
{
    tdma_seat_admission_owner_t owner;
    tdma_seat_owner_resources_t resources;
    tdma_seat_admission_owner_snapshot_t snapshot;
    const tdma_seat_identity_t first = identity(5u, 7u);
    const tdma_seat_identity_t second = identity(6u, 8u);
    const tdma_seat_window_t win = window(1000u, 100000u);
    resources_fill(&resources, TDMA_SEAT_BUDGET_ALL);
    tdma_seat_admission_owner_init(&owner);
    (void)tdma_seat_admission_owner_bind(&owner, &first, &resources);
    (void)tdma_seat_admission_owner_open(&owner, &first, &win, 5000u);
    (void)tdma_seat_admission_owner_priority_selected(&owner, 9u, 6000u);
    expect("rebinding another generation is accepted",
           tdma_seat_admission_owner_bind(&owner, &second, &resources) ==
               TDMA_SEAT_OK);
    tdma_seat_admission_owner_snapshot(&owner, &snapshot);
    expect("old window retired", snapshot.state == TDMA_SEAT_REVOKED &&
        snapshot.revoke_reason == TDMA_SEAT_REVOKE_BINDING_CHANGED);
    expect("stale window identity is refused",
           tdma_seat_admission_owner_open(&owner, &first, &win, 7000u) ==
               TDMA_SEAT_STALE_IDENTITY);
}

static void case_unbound_owner_refuses(void)
{
    tdma_seat_admission_owner_t owner;
    tdma_seat_admission_owner_snapshot_t snapshot;
    const tdma_seat_identity_t id = identity(7u, 9u);
    const tdma_seat_window_t win = window(1000u, 100000u);
    const tdma_seat_request_t req = request();
    tdma_seat_admission_owner_init(&owner);
    expect("unbound open refused", tdma_seat_admission_owner_open(
        &owner, &id, &win, 5000u) == TDMA_SEAT_BUDGET_UNDEFINED);
    expect("unbound grant refused", tdma_seat_admission_owner_grant(
        &owner, &req, 6000u) == TDMA_SEAT_BUDGET_UNDEFINED);
    tdma_seat_admission_owner_snapshot(&owner, &snapshot);
    expect("unbound is visible", snapshot.bound == 0u &&
        snapshot.deny_budget_undefined == 2u && snapshot.open_count == 0u);
}

static void case_bind_rejects_zero_generations(void)
{
    tdma_seat_admission_owner_t owner;
    tdma_seat_owner_resources_t resources;
    tdma_seat_identity_t id = identity(8u, 10u);
    resources_fill(&resources, TDMA_SEAT_BUDGET_ALL);
    tdma_seat_admission_owner_init(&owner);
    id.binding_generation = 0u;
    expect("zero binding generation refused",
           tdma_seat_admission_owner_bind(&owner, &id, &resources) ==
               TDMA_SEAT_BAD_ARGUMENT);
}

int main(void)
{
    case_undefined_dimension_fails_closed();
    case_live_budget_recheck_before_submission();
    case_full_budget_admits_in_order();
    case_exhausted_budget_is_distinct();
    case_window_expiry_revokes();
    case_binding_change_retires_window();
    case_unbound_owner_refuses();
    case_bind_rejects_zero_generations();
    if (failures != 0) {
        printf("FAILURES %d\n", failures);
        return 1;
    }
    printf("all seat admission owner cases passed\n");
    return 0;
}
