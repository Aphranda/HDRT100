#include "trigger_deployment_service.h"

#include <string.h>
#include "board_identity.h"
#include "pico.h"
#include "sync_trigger.h"
#include "tdma_runtime_owner.h"

static uint32_t s_deployment_busy;
static trigger_deployment_snapshot_t s_deployment;

static bool deployment_enter(void)
{
    uint32_t idle = 0u;
    return get_core_num() == 0u && __atomic_compare_exchange_n(
        &s_deployment_busy, &idle, 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}

static void deployment_leave(void)
{
    __atomic_store_n(&s_deployment_busy, 0u, __ATOMIC_RELEASE);
}

static bool local_uid(uint64_t *out)
{
    const char *serial = board_identity_serial();
    if (!serial) return false;
    uint64_t uid = 0u;
    for (uint32_t i = 0u; i < 16u; ++i) {
        const unsigned char c = (unsigned char)serial[i];
        uint32_t nibble;
        if (c >= '0' && c <= '9') nibble = c - '0';
        else if (c >= 'A' && c <= 'F') nibble = c - 'A' + 10u;
        else if (c >= 'a' && c <= 'f') nibble = c - 'a' + 10u;
        else return false;
        uid = (uid << 4u) | nibble;
    }
    if (serial[16] || !uid) return false;
    *out = uid;
    return true;
}

static bool deployment_idle(tdma_ring_clock_snapshot_t *ring,
    tdma_service_ring_runtime_config_t *staged)
{
    tdma_ring_clock_snapshot_t after;
    sync_trigger_status_t trigger;
    sync_trigger_get_status(&trigger);
    return trigger.state == TRIG_STATE_IDLE &&
        tdma_runtime_owner_get_ring_clock_snapshot(ring) && !ring->enabled &&
        !ring->adapter_started && ring->config_seq == ring->applied_config_seq &&
        tdma_runtime_owner_get_staged_ring_config(staged) && staged->enabled &&
        tdma_runtime_owner_get_ring_clock_snapshot(&after) && !after.enabled &&
        !after.adapter_started && after.config_seq == after.applied_config_seq &&
        after.config_seq == ring->config_seq;
}

static bool tables_equal(const trigger_deployment_t *a, const trigger_deployment_t *b)
{
    if (a->generation != b->generation) return false;
    for (uint32_t i = 0u; i < TRIGGER_DEPLOYMENT_ROLE_COUNT; ++i) {
        const trigger_deployment_row_t *x = &a->rows[i], *y = &b->rows[i];
        if (x->board_uid != y->board_uid || x->tdma_slot != y->tdma_slot ||
            x->input_mask != y->input_mask || x->output_mask != y->output_mask ||
            x->feedback_mode != y->feedback_mode) return false;
    }
    return true;
}

static uint32_t bind_local(const trigger_deployment_t *table,
    trigger_deployment_binding_t *out)
{
    tdma_ring_clock_snapshot_t ring;
    tdma_service_ring_runtime_config_t staged;
    uint64_t uid;
    if (!deployment_idle(&ring, &staged)) return TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE;
    if (!local_uid(&uid)) return TRIGGER_DEPLOYMENT_IDENTITY;
    trigger_deployment_row_t row;
    for (uint32_t role = 0u; role < TRIGGER_DEPLOYMENT_ROLE_COUNT; ++role) {
        if (table->rows[role].board_uid != uid) continue;
        const uint32_t result = trigger_deployment_resolve(table, role, uid,
            staged.local_slot_id, PROJECT_NODE_CAPACITY, &row);
        if (result) return result;
        *out = (trigger_deployment_binding_t){
            .table = *table, .local_uid = uid, .valid = 1u,
            .crc = trigger_deployment_crc(table), .local_role = role,
            .ring_config = ring.config_seq, .schedule_crc = staged.schedule_crc32,
            .local_slot = staged.local_slot_id,
        };
        return TRIGGER_DEPLOYMENT_OK;
    }
    return TRIGGER_DEPLOYMENT_NOT_LOCAL;
}

typedef struct {
    const trigger_deployment_t *table;
    uint32_t generation, crc, result;
} deployment_request_t;

static bool stage_stopped(void *context)
{
    deployment_request_t *request = context;
    trigger_deployment_binding_t binding;
    request->result = bind_local(request->table, &binding);
    if (request->result) return false;
    if (request->table->generation <= s_deployment.last_generation) {
        const bool same = s_deployment.staged_valid &&
            tables_equal(request->table, &s_deployment.staged);
        request->result = same ? TRIGGER_DEPLOYMENT_OK : TRIGGER_DEPLOYMENT_GENERATION;
        return same;
    }
    s_deployment.staged = *request->table;
    s_deployment.staged_crc = request->crc;
    s_deployment.staged_valid = 1u;
    s_deployment.last_generation = request->table->generation;
    return true;
}

uint32_t trigger_deployment_stage(const trigger_deployment_t *table, uint32_t expected_crc)
{
    uint32_t result = trigger_deployment_validate(table, PROJECT_NODE_CAPACITY);
    if (result) return result;
    if (trigger_deployment_crc(table) != expected_crc) return TRIGGER_DEPLOYMENT_SERVICE_DIGEST;
    if (!deployment_enter()) return TRIGGER_DEPLOYMENT_SERVICE_BUSY;
    deployment_request_t request = {.table = table, .crc = expected_crc,
        .result = TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE};
    (void)tdma_service_update_stopped_metadata(tdma_runtime_owner_get(), stage_stopped, &request);
    deployment_leave();
    return request.result;
}

static bool activate_stopped(void *context)
{
    deployment_request_t *request = context;
    tdma_ring_clock_snapshot_t ring;
    tdma_service_ring_runtime_config_t staged;
    if (!deployment_idle(&ring, &staged)) return false;
    if (!s_deployment.staged_valid) {
        request->result = TRIGGER_DEPLOYMENT_SERVICE_NO_STAGE;
        return false;
    }
    if (request->generation != s_deployment.staged.generation) {
        request->result = TRIGGER_DEPLOYMENT_GENERATION;
        return false;
    }
    if (request->crc != s_deployment.staged_crc) {
        request->result = TRIGGER_DEPLOYMENT_SERVICE_DIGEST;
        return false;
    }
    trigger_deployment_binding_t binding;
    request->result = bind_local(&s_deployment.staged, &binding);
    if (request->result) return false;
    s_deployment.active = binding;
    s_deployment.staged_valid = 0u;
    return true;
}

uint32_t trigger_deployment_activate(uint32_t generation, uint32_t expected_crc)
{
    if (!deployment_enter()) return TRIGGER_DEPLOYMENT_SERVICE_BUSY;
    deployment_request_t request = {.generation = generation, .crc = expected_crc,
        .result = TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE};
    (void)tdma_service_update_stopped_metadata(tdma_runtime_owner_get(), activate_stopped, &request);
    deployment_leave();
    return request.result;
}

static bool clear_stopped(void *unused)
{
    (void)unused;
    tdma_ring_clock_snapshot_t ring;
    tdma_service_ring_runtime_config_t staged;
    if (!deployment_idle(&ring, &staged)) return false;
    const uint32_t last = s_deployment.last_generation;
    memset(&s_deployment, 0, sizeof(s_deployment));
    s_deployment.last_generation = last;
    return true;
}

uint32_t trigger_deployment_clear(void)
{
    if (!deployment_enter()) return TRIGGER_DEPLOYMENT_SERVICE_BUSY;
    const bool result = tdma_service_update_stopped_metadata(
        tdma_runtime_owner_get(), clear_stopped, NULL);
    deployment_leave();
    return result ? TRIGGER_DEPLOYMENT_OK : TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE;
}

uint32_t trigger_deployment_snapshot(trigger_deployment_snapshot_t *out)
{
    if (!out) return TRIGGER_DEPLOYMENT_ARGUMENT;
    if (!deployment_enter()) return TRIGGER_DEPLOYMENT_SERVICE_BUSY;
    tdma_ring_clock_snapshot_t ring;
    tdma_service_ring_runtime_config_t staged;
    const bool idle = deployment_idle(&ring, &staged);
    if (idle) *out = s_deployment;
    deployment_leave();
    return idle ? TRIGGER_DEPLOYMENT_OK : TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE;
}

uint32_t trigger_deployment_plan(uint32_t generation, uint32_t role,
    uint32_t grid_epoch, uint32_t grid_generation, uint64_t ordinal,
    uint64_t minimum_lead_ns, uint64_t maximum_ahead_ns, trigger_deployment_plan_t *out)
{
    if (!out) return TRIGGER_DEPLOYMENT_ARGUMENT;
    if (!deployment_enter()) return TRIGGER_DEPLOYMENT_SERVICE_BUSY;
    uint32_t result = TRIGGER_DEPLOYMENT_SERVICE_UNCONFIGURED;
    trigger_deployment_plan_t candidate = {0};
    if (!s_deployment.active.valid) goto done;
    result = TRIGGER_DEPLOYMENT_GENERATION;
    if (generation != s_deployment.active.table.generation) goto done;
    candidate.deployment = s_deployment.active;
    trigger_deployment_binding_t current;
    result = bind_local(&candidate.deployment.table, &current);
    if (result) goto done;
    result = TRIGGER_DEPLOYMENT_SERVICE_CHANGED;
    if (current.local_uid != candidate.deployment.local_uid ||
        current.ring_config != candidate.deployment.ring_config ||
        current.schedule_crc != candidate.deployment.schedule_crc ||
        current.local_slot != candidate.deployment.local_slot) goto done;
    result = trigger_deployment_resolve(&current.table, role, current.local_uid,
        current.local_slot, PROJECT_NODE_CAPACITY, &candidate.route);
    if (result) goto done;
    candidate.role = role;
    result = trigger_grid_preview_plan_anchored(grid_epoch, grid_generation, ordinal,
        minimum_lead_ns, maximum_ahead_ns, &candidate.clock);
    if (result) { result += TRIGGER_DEPLOYMENT_SERVICE_GRID_BASE; goto done; }
    result = TRIGGER_DEPLOYMENT_SERVICE_CHANGED;
    if (candidate.clock.configuration.local_slot != current.local_slot ||
        candidate.clock.configuration.schedule_crc != current.schedule_crc ||
        candidate.clock.configuration.ring_config != current.ring_config) goto done;
    trigger_deployment_binding_t after;
    if (bind_local(&candidate.deployment.table, &after) != TRIGGER_DEPLOYMENT_OK ||
        after.local_uid != current.local_uid || after.local_slot != current.local_slot ||
        after.ring_config != current.ring_config || after.schedule_crc != current.schedule_crc)
        goto done;
    *out = candidate;
    result = TRIGGER_DEPLOYMENT_OK;
done:
    deployment_leave();
    return result;
}
