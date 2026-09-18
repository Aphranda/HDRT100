#include "vdc_priority_match.h"
#include "vdc_priority_rx.h"
#include <limits.h>
#include <string.h>

static uint32_t s_priority_match_request, s_priority_match_last_request;
static uint32_t s_priority_match_request_session;
static uint32_t s_priority_match_guard;
static uint32_t s_priority_match_words[sizeof(vdc_priority_match_snapshot_t) / 4u];
typedef struct {
    vdc_priority_match_snapshot_t status;
    tdma_ring_clock_snapshot_t ring, ring_after;
    tdma_pio_spi_event_live_snapshot_t live, live_after;
    tdma_pio_spi_event_exact_t exact;
    vdc_priority_rx_snapshot_t rx, rx_after;
    vdc_dpll_manager_committed_model_t model, model_after;
    vdc_dpll_manager_mapping_projection_t projection;
    vdc_clock_mapping_cache_t mapping_cache;
    vdc_path_delay_entry_t path, path_after;
    uint64_t raw_lo, raw_hi, remote_hi, expected_lo, expected_hi;
    int64_t residual_lo, residual_hi;
    uint32_t profile_crc32, session, bound, unresolved, candidate_sequence, path_checked_crc;
    /* Private, one-service Core1 ticket. Historical snapshot reads never set it. */
    uint32_t fresh;
} vdc_priority_match_work_t;
static vdc_priority_match_work_t s_priority_match_work;
_Static_assert(sizeof(vdc_priority_match_snapshot_t) % 4u == 0u,
    "match snapshot publishes atomic words");

static uint32_t priority_match_inc(uint32_t n) { return n == UINT32_MAX ? n : n + 1u; }
uint32_t vdc_dpll_manager_priority_match_generation(void)
{ return __atomic_load_n(&s_priority_match_request, __ATOMIC_ACQUIRE); }
static bool priority_match_set_stopped(void *context)
{
    const uint32_t generation = *(const uint32_t *)context;
    const uint32_t session = vdc_dpll_manager_feedback_session();
    if (generation && (generation <= s_priority_match_last_request ||
            !session)) return false;
    if (generation) s_priority_match_last_request = generation;
    __atomic_store_n(&s_priority_match_request_session, generation ? session : 0u, __ATOMIC_RELEASE);
    __atomic_store_n(&s_priority_match_request, generation, __ATOMIC_RELEASE);
    return true;
}
bool vdc_dpll_manager_set_priority_match(uint32_t generation)
{
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service, priority_match_set_stopped, &generation);
}
static void priority_match_publish(void)
{
    const uint32_t guard = __atomic_load_n(&s_priority_match_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_match_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for (size_t i = 0u; i < sizeof(s_priority_match_work.status) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&s_priority_match_work.status + 4u * i, 4u);
        __atomic_store_n(s_priority_match_words + i, word, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_priority_match_guard, guard + 2u, __ATOMIC_RELEASE);
}
bool vdc_dpll_manager_get_priority_match(vdc_priority_match_snapshot_t *out)
{
    if (!out) return false;
    const uint32_t guard = __atomic_load_n(&s_priority_match_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_priority_match_snapshot_t value;
    for (size_t i = 0u; i < sizeof(value) / 4u; ++i) {
        const uint32_t word = __atomic_load_n(s_priority_match_words + i, __ATOMIC_RELAXED);
        memcpy((uint8_t *)&value + 4u * i, &word, 4u);
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_match_guard, __ATOMIC_ACQUIRE)) return false;
    *out = value;
    return true;
}
static void priority_match_skip(uint32_t reason, bool retire)
{
    vdc_priority_match_snapshot_t *s = &s_priority_match_work.status;
    s->last_reason = reason;
    if (reason == VDC_PRIORITY_MATCH_BUSY) s->busy = priority_match_inc(s->busy);
    else if (reason == VDC_PRIORITY_MATCH_PENDING) s->pending = priority_match_inc(s->pending);
    else if (reason == VDC_PRIORITY_MATCH_EVICTED) s->history_miss = priority_match_inc(s->history_miss);
    else s->rejected = priority_match_inc(s->rejected);
    if (retire) {
        s->active = 0u; s->retired = 1u;
        memset(&s_priority_match_work.mapping_cache, 0,
            sizeof(s_priority_match_work.mapping_cache));
    }
    priority_match_publish();
}
static bool priority_match_difference(uint64_t a, uint64_t b, int64_t *out)
{
    if (a >= b) {
        if (a - b > INT64_MAX) return false;
        *out = (int64_t)(a - b);
    } else {
        const uint64_t magnitude = b - a;
        if (magnitude > (UINT64_C(1) << 63u)) return false;
        *out = magnitude == (UINT64_C(1) << 63u) ? INT64_MIN : -(int64_t)magnitude;
    }
    return true;
}
static bool priority_match_live_valid(const tdma_pio_spi_event_live_snapshot_t *live)
{
    const uint32_t required = TDMA_EVENT_LIVE_RETAINED | TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_VALID;
    return (live->flags & required) == required && live->arm_epoch && live->record.epoch &&
        live->tick_hz && live->timer1_enable_after >= live->timer1_enable_before;
}
static bool priority_match_ring_equal(const tdma_ring_clock_snapshot_t *a,
    const tdma_ring_clock_snapshot_t *b)
{
    return a->enabled == b->enabled && a->adapter_started == b->adapter_started &&
        a->config_seq == b->config_seq && a->applied_config_seq == b->applied_config_seq &&
        a->node_count == b->node_count && a->local_slot_id == b->local_slot_id &&
        a->reference_slot_id == b->reference_slot_id && a->schedule_crc32 == b->schedule_crc32;
}
static bool priority_match_model_valid(const vdc_dpll_manager_committed_model_t *m)
{
    const vdc_priority_match_work_t *w = &s_priority_match_work;
    return w->session && m->session == w->session && m->token && m->role_generation &&
        m->role_generation == s_vdc_domain.control.profile.generation && m->clock_epoch_id &&
        m->clock_run_id && m->local_slot == w->ring.local_slot_id && m->dco.valid == 1u &&
        m->dco.nominal_period_ns && m->dco.lock_state <= VDC_DOMAIN_LOCK_FAULT &&
        m->dco.tdma_schedule_crc32 == w->ring.schedule_crc32;
}
static bool priority_match_path(vdc_path_delay_entry_t *path)
{
    vdc_priority_match_work_t *w = &s_priority_match_work;
    const tdma_ring_clock_snapshot_t *r = &w->ring;
    const vdc_path_delay_table_t *table = &s_vdc_domain.path_delay;
    /* The existing matrix describes reverse DATA. Its transpose gives the
     * forward-CS path using the same measured per-link scalars, provisionally.
     * Never walk topology or claim independently measured CS endpoints. */
    if (!table->table_crc32 || table->schedule_crc32 != r->schedule_crc32) return false;
    if (w->path_checked_crc != table->table_crc32) {
        if (table->entry_count != r->node_count || table->entry_count > VDC_DOMAIN_PATH_DELAY_ENTRY_COUNT)
            return false;
        /* Once per immutable table: bounded direction checks, no ring walk or
         * delay accumulation. A native forward matrix must not be transposed. */
        for (uint32_t i = 0u; i < table->entry_count; ++i)
            if (!table->entries[i].valid ||
                table->entries[i].direction != VDC_PATH_DELAY_DIRECTION_TDMA_DATA_REVERSE) return false;
        w->path_checked_crc = table->table_crc32;
    }
    return vdc_domain_active_observation_path_delay_lookup(table,
            r->reference_slot_id, r->local_slot_id, path);
}
static bool priority_match_binding_current(void)
{
    const vdc_priority_match_work_t *w = &s_priority_match_work;
    const vdc_priority_match_snapshot_t *s = &w->status;
    return s->ring_config_seq == w->ring.config_seq && s->ring_applied_seq == w->ring.applied_config_seq &&
        s->local_slot == w->ring.local_slot_id && s->reference_slot == w->ring.reference_slot_id &&
        s->node_count == w->ring.node_count && s->schedule_crc32 == w->ring.schedule_crc32 &&
        s->profile_crc32 == w->profile_crc32 && s->session == w->session &&
        s->role_generation == w->model.role_generation && s->clock_epoch == w->model.clock_epoch_id &&
        s->clock_run == w->model.clock_run_id && s->rx_epoch == w->rx.epoch &&
        s->arm_epoch == w->live.arm_epoch && s->observer_epoch == w->live.record.epoch &&
        s->tick_hz == w->live.tick_hz && s->path_crc32 == w->path.cal_crc32 && s->delay_ns == w->path.delay_ns;
}

void __attribute__((noinline)) vdc_priority_match_core1(void)
{
    if (get_core_num() != 1u) return;
    vdc_priority_match_work_t *w = &s_priority_match_work;
    vdc_priority_match_snapshot_t *s = &w->status;
    w->fresh = 0u;
    const uint32_t request = vdc_dpll_manager_priority_match_generation();
    if (!request) {
        if (s->generation && !s->retired) priority_match_skip(VDC_PRIORITY_MATCH_DISABLED, true);
        return;
    }
    if (request != s->generation) {
        memset(w, 0, sizeof(*w)); s->schema = 1u; s->generation = request;
        s->session = __atomic_load_n(&s_priority_match_request_session, __ATOMIC_ACQUIRE);
    }
    if (s->retired) return;
    s->calls = priority_match_inc(s->calls);
    w->session = vdc_dpll_manager_feedback_session();
    if (!s->session || s->session != w->session)
        return priority_match_skip(VDC_PRIORITY_MATCH_BINDING, true);
    if (!s_vdc_tdma_service || !tdma_runtime_owner_get_ring_clock_snapshot(&w->ring))
        return priority_match_skip(VDC_PRIORITY_MATCH_BUSY, false);
    if (!w->ring.enabled || !w->ring.adapter_started)
        return priority_match_skip(VDC_PRIORITY_MATCH_STOP, w->bound != 0u);
    w->profile_crc32 = s_vdc_tdma_service->ring_runtime.ring_profile_crc32;
    if (!w->ring.config_seq || w->ring.config_seq != w->ring.applied_config_seq ||
        w->ring.node_count < 2u || w->ring.node_count > TDMA_FLIGHT_SHORT_SLOT_COUNT ||
        w->ring.local_slot_id >= w->ring.node_count || w->ring.reference_slot_id >= w->ring.node_count ||
        w->ring.local_slot_id == w->ring.reference_slot_id || !w->ring.schedule_crc32 || !w->profile_crc32)
        return priority_match_skip(VDC_PRIORITY_MATCH_CONFIG, w->bound != 0u);
    if (s_vdc_domain.control.profile.valid != 1u ||
        s_vdc_domain.control.profile.mode != VDC_DPLL_CONTROL_MODE_FOLLOWER ||
        s_vdc_domain.control.profile.follow_master_slot_id != w->ring.reference_slot_id)
        return priority_match_skip(VDC_PRIORITY_MATCH_ROLE, w->bound != 0u);
    if (w->bound && (s->ring_config_seq != w->ring.config_seq ||
        s->ring_applied_seq != w->ring.applied_config_seq || s->local_slot != w->ring.local_slot_id ||
        s->reference_slot != w->ring.reference_slot_id || s->node_count != w->ring.node_count ||
        s->schedule_crc32 != w->ring.schedule_crc32 || s->profile_crc32 != w->profile_crc32 ||
        s->session != w->session || s->role_generation != s_vdc_domain.control.profile.generation))
        return priority_match_skip(VDC_PRIORITY_MATCH_BINDING, true);
    if (!vdc_dpll_manager_get_committed_model(&w->model))
        return priority_match_skip(VDC_PRIORITY_MATCH_BUSY, false);
    if (w->bound && (s->session != w->session || s->session != w->model.session ||
        s->role_generation != w->model.role_generation || s->clock_epoch != w->model.clock_epoch_id ||
        s->clock_run != w->model.clock_run_id || s->local_slot != w->model.local_slot))
        return priority_match_skip(VDC_PRIORITY_MATCH_BINDING, true);
    if (!priority_match_model_valid(&w->model))
        return priority_match_skip(VDC_PRIORITY_MATCH_MODEL, false);
    if (!tdma_runtime_owner_get_event_live_snapshot(&w->live))
        return priority_match_skip(VDC_PRIORITY_MATCH_BUSY, false);
    if (!priority_match_live_valid(&w->live))
        return priority_match_skip(VDC_PRIORITY_MATCH_SOURCE, w->bound != 0u);
    if (w->bound && (s->arm_epoch != w->live.arm_epoch ||
        s->observer_epoch != w->live.record.epoch || s->tick_hz != w->live.tick_hz))
        return priority_match_skip(VDC_PRIORITY_MATCH_BINDING, true);
    if (!vdc_priority_rx_copy_live(&w->rx))
        return priority_match_skip(VDC_PRIORITY_MATCH_RX, false);
    if (!priority_match_path(&w->path))
        return priority_match_skip(VDC_PRIORITY_MATCH_PATH, w->bound != 0u);
    if (w->bound && !priority_match_binding_current())
        return priority_match_skip(VDC_PRIORITY_MATCH_BINDING, true);
    if (w->rx.typed_record.binding_generation != request)
        return priority_match_skip(VDC_PRIORITY_MATCH_GENERATION, false);
    if (!w->rx.epoch || w->rx.source_slot != w->ring.reference_slot_id ||
        !(w->rx.target_mask & (1u << w->ring.local_slot_id)))
        return priority_match_skip(VDC_PRIORITY_MATCH_RX, false);
    const uint32_t sequence = w->rx.typed_record.event_sequence;
    if (s->have_match && sequence == s->event_sequence) {
        s->repeated = priority_match_inc(s->repeated); s->last_reason = VDC_PRIORITY_MATCH_OK;
        priority_match_publish(); return;
    }
    if (s->have_match && (sequence < s->event_sequence || sequence - s->event_sequence > INT32_MAX))
        return priority_match_skip(VDC_PRIORITY_MATCH_SEQUENCE, false);
    if (w->unresolved && sequence != w->candidate_sequence)
        s->superseded = priority_match_inc(s->superseded);
    w->candidate_sequence = sequence; w->unresolved = 1u;
    s->history_result = tdma_runtime_owner_copy_event_history_exact(w->live.arm_epoch,
        w->live.record.epoch, sequence, &w->exact);
    if (s->history_result != TDMA_EVENT_EXACT_OK) {
        if (s->history_result == TDMA_EVENT_EXACT_BUSY)
            return priority_match_skip(VDC_PRIORITY_MATCH_BUSY, false);
        if (s->history_result == TDMA_EVENT_EXACT_PENDING)
            return priority_match_skip(VDC_PRIORITY_MATCH_PENDING, false);
        if (s->history_result == TDMA_EVENT_EXACT_EVICTED) {
            w->unresolved = 0u; return priority_match_skip(VDC_PRIORITY_MATCH_EVICTED, false);
        }
        return priority_match_skip(VDC_PRIORITY_MATCH_SOURCE,
            w->bound && s->history_result == TDMA_EVENT_EXACT_RETIRED);
    }
    const uint32_t required = TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_VALID | TDMA_EVENT_LIVE_RETAINED;
    if (w->exact.arm_epoch != w->live.arm_epoch || w->exact.observer_epoch != w->live.record.epoch ||
        w->exact.tick_hz != w->live.tick_hz || w->exact.record.sequence != sequence ||
        (w->exact.flags & required) != required ||
        w->exact.timer1_enable_before != w->live.timer1_enable_before ||
        w->exact.timer1_enable_after != w->live.timer1_enable_after ||
        w->exact.timer1_enable_after < w->exact.timer1_enable_before)
        return priority_match_skip(VDC_PRIORITY_MATCH_SOURCE, false);
    if (w->exact.record.rx_elapsed_cycles > UINT64_MAX - w->exact.timer1_enable_after)
        return priority_match_skip(VDC_PRIORITY_MATCH_OVERFLOW, false);
    w->raw_lo = w->exact.timer1_enable_before + w->exact.record.rx_elapsed_cycles;
    w->raw_hi = w->exact.timer1_enable_after + w->exact.record.rx_elapsed_cycles;
    if (w->raw_lo < w->model.valid_from_raw)
        return priority_match_skip(VDC_PRIORITY_MATCH_MODEL, false);
    const vdc_clock_mapping_status_t mapping_status =
        vdc_dpll_manager_project_mapped_feedback_event(w->session, w->model.role_generation,
            w->model.clock_epoch_id, w->model.clock_run_id, w->ring.local_slot_id,
            w->ring.schedule_crc32, w->live.tick_hz, w->raw_lo, w->raw_hi,
            &w->mapping_cache, &w->projection);
    if (mapping_status != VDC_CLOCK_MAPPING_OK)
        return priority_match_skip(VDC_PRIORITY_MATCH_PROJECTION,
            mapping_status == VDC_CLOCK_MAPPING_CONTRADICTION ||
            mapping_status == VDC_CLOCK_MAPPING_EXHAUSTED);
    if (!vdc_dpll_manager_get_committed_model(&w->model_after) ||
        !tdma_runtime_owner_get_ring_clock_snapshot(&w->ring_after) ||
        !tdma_runtime_owner_get_event_live_snapshot(&w->live_after) ||
        !vdc_priority_rx_copy_live(&w->rx_after))
        return priority_match_skip(VDC_PRIORITY_MATCH_BUSY, false);
    if (request != vdc_dpll_manager_priority_match_generation() ||
        w->session != vdc_dpll_manager_feedback_session() ||
        w->session != __atomic_load_n(&s_priority_match_request_session, __ATOMIC_ACQUIRE) ||
        s_vdc_domain.control.profile.valid != 1u ||
        s_vdc_domain.control.profile.mode != VDC_DPLL_CONTROL_MODE_FOLLOWER ||
        s_vdc_domain.control.profile.follow_master_slot_id != w->ring.reference_slot_id ||
        !priority_match_ring_equal(&w->ring, &w->ring_after) ||
        w->profile_crc32 != s_vdc_tdma_service->ring_runtime.ring_profile_crc32 ||
        w->model_after.session != w->model.session || w->model_after.role_generation != w->model.role_generation ||
        w->model_after.clock_epoch_id != w->model.clock_epoch_id || w->model_after.clock_run_id != w->model.clock_run_id ||
        w->model_after.local_slot != w->model.local_slot ||
        !priority_match_live_valid(&w->live_after) || w->live_after.arm_epoch != w->live.arm_epoch ||
        w->live_after.record.epoch != w->live.record.epoch || w->live_after.tick_hz != w->live.tick_hz ||
        w->live_after.timer1_enable_before != w->live.timer1_enable_before ||
        w->live_after.timer1_enable_after != w->live.timer1_enable_after ||
        w->rx_after.epoch != w->rx.epoch || !priority_match_path(&w->path_after) ||
        w->rx_after.typed_record.binding_generation != request ||
        w->rx_after.source_slot != w->ring.reference_slot_id ||
        !(w->rx_after.target_mask & (1u << w->ring.local_slot_id)) ||
        w->path_after.cal_crc32 != w->path.cal_crc32 || w->path_after.delay_ns != w->path.delay_ns)
        return priority_match_skip(VDC_PRIORITY_MATCH_BINDING, true);
    if (w->projection.model.token != w->model.token ||
        w->projection.model.applied_command_seq != w->model.applied_command_seq ||
        memcmp(&w->model, &w->model_after, sizeof(w->model)) != 0 ||
        !priority_match_model_valid(&w->model_after))
        return priority_match_skip(VDC_PRIORITY_MATCH_CHANGED, false);
    const uint64_t remote = w->rx.typed_record.event_time_lower;
    const uint32_t width = w->rx.typed_record.uncertainty_width;
    if (!width || remote > UINT64_MAX - width || remote + width > UINT64_MAX - w->path.delay_ns ||
        w->projection.mapping.output_hi < w->projection.mapping.output_lo)
        return priority_match_skip(VDC_PRIORITY_MATCH_OVERFLOW, false);
    w->remote_hi = remote + width; w->expected_lo = remote + w->path.delay_ns;
    w->expected_hi = w->remote_hi + w->path.delay_ns;
    if (!priority_match_difference(w->projection.mapping.output_lo, w->expected_hi, &w->residual_lo) ||
        !priority_match_difference(w->projection.mapping.output_hi, w->expected_lo, &w->residual_hi))
        return priority_match_skip(VDC_PRIORITY_MATCH_OVERFLOW, false);
    s->source_slot = w->rx.source_slot; s->event_sequence = sequence;
    s->carrier_sequence = w->rx.carrier_sequence; s->rx_epoch = w->rx.epoch;
    s->session = w->session; s->role_generation = w->model.role_generation;
    s->clock_epoch = w->model.clock_epoch_id; s->clock_run = w->model.clock_run_id;
    s->model_token = w->model.token; s->applied_command_seq = w->model.applied_command_seq;
    s->ring_config_seq = w->ring.config_seq; s->ring_applied_seq = w->ring.applied_config_seq;
    s->local_slot = w->ring.local_slot_id; s->reference_slot = w->ring.reference_slot_id;
    s->node_count = w->ring.node_count; s->schedule_crc32 = w->ring.schedule_crc32;
    s->profile_crc32 = w->profile_crc32; s->observer_epoch = w->live.record.epoch;
    s->tick_hz = w->live.tick_hz; s->path_crc32 = w->path.cal_crc32; s->delay_ns = w->path.delay_ns;
    s->path_direction = VDC_PATH_DELAY_DIRECTION_MARKER_FORWARD;
    s->path_qualification = VDC_PRIORITY_MATCH_PATH_PROVISIONAL_TRANSPOSE;
    s->arm_epoch = w->live.arm_epoch; s->valid_from_raw = w->model.valid_from_raw;
    s->raw_lo = w->raw_lo; s->raw_hi = w->raw_hi;
    s->local_lo = w->projection.mapping.output_lo; s->local_hi = w->projection.mapping.output_hi;
    s->remote_lo = remote; s->remote_hi = w->remote_hi;
    s->expected_lo = w->expected_lo; s->expected_hi = w->expected_hi;
    s->residual_lo = w->residual_lo; s->residual_hi = w->residual_hi;
    s->matched = priority_match_inc(s->matched); s->have_match = s->active = w->bound = 1u;
    s->last_reason = VDC_PRIORITY_MATCH_OK; w->unresolved = 0u;
    /* Only a distinct, fully accepted event teaches the Core1 mapping.
     * Projection failures and all post-projection rejections leave it alone. */
    w->mapping_cache = w->projection.mapping.next;
    w->fresh = 1u;
    priority_match_publish();
}
