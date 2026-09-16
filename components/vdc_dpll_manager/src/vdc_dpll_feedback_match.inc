/* The sole Core0 RefMem task owns the sparse cache and matcher. Core1 only
 * publishes/retires a small authorization; no cache or peer state is shared
 * for mutation. Core0 prepares diagnostic or explicitly admitted LOCAL_FOLLOW
 * candidates. Only Core1 may consume a candidate and update the DCO. */
#include "vdc_local_follow.h"
#define VDC_FEEDBACK_MATCH_MAX_AGE_MS 120u
typedef struct {
    uint32_t config, role, schedule, clock_epoch, clock_run, local, nodes, reference_epoch, session, rate_mode;
} vdc_feedback_match_binding_t;
typedef struct {
    uint64_t next_ordinal, arm_epoch;
    vdc_feedback_match_lifetime_t reference;
    uint32_t insertion_serial, serial, latest_sequence, has_sequence;
    uint32_t delay_ns, path_crc, missed, projection_rejects;
} vdc_local_follow_cursor_t;
typedef struct {
    union { vdc_feedback_match_peer_t peer; vdc_local_follow_cursor_t local; };
    uint32_t words[sizeof(vdc_dpll_manager_feedback_match_status_t) / 4u];
    uint32_t guard, consumed;
} vdc_feedback_match_source_t;
static vdc_feedback_match_cache_t s_feedback_match_cache;
static vdc_feedback_match_binding_t s_feedback_match_binding;
static vdc_feedback_match_source_t s_feedback_matches[PROJECT_NODE_CAPACITY];
static uint32_t s_feedback_match_cursor, s_feedback_match_insert_count, s_feedback_match_reject_count;
static uint32_t s_feedback_match_active_generation;
static uint32_t s_feedback_match_preparation_generation; /* Core0 private. */
/* Core1 alone writes these words. Zero token revokes authorization; every
 * reauthorization uses a fresh token, including an identical binding. */
static uint32_t s_feedback_match_owner_words[sizeof(vdc_feedback_match_binding_t) / 4u];
static uint32_t s_feedback_match_owner_token;
static uint32_t s_feedback_match_owner_serial;
_Static_assert(sizeof(vdc_dpll_manager_feedback_match_status_t) % 4u == 0u, "atomic match words");
_Static_assert(sizeof(vdc_local_follow_cursor_t) <= sizeof(vdc_feedback_match_peer_t), "local cursor reuses peer");
_Static_assert(sizeof(vdc_local_follow_candidate_t) == sizeof(vdc_dpll_manager_feedback_match_status_t),
    "local candidate exactly reuses match publication");

static uint32_t match_inc(uint32_t value) { return value == UINT32_MAX ? value : value + 1u; }

static void match_load_words(const uint32_t *words, void *out, size_t size)
{
    for (size_t i = 0; i < size / 4u; ++i) {
        uint32_t word = __atomic_load_n(words + i, __ATOMIC_RELAXED);
        memcpy((uint8_t *)out + i * 4u, &word, 4u);
    }
}

static void match_publish(vdc_feedback_match_source_t *source,
    const vdc_dpll_manager_feedback_match_status_t *status)
{
    (void)__atomic_add_fetch(&source->guard, 1u, __ATOMIC_ACQ_REL);
    for (size_t i = 0; i < sizeof(*status) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)status + i * 4u, 4u);
        __atomic_store_n(source->words + i, word, __ATOMIC_RELAXED);
    }
    (void)__atomic_add_fetch(&source->guard, 1u, __ATOMIC_RELEASE);
}

/* Keep authorization in XIP: inlining into the RAM-resident service can
 * advance the aligned BSS start by an entire page on this target. Core1
 * retirement never mutates a cache/peer that Core0 may be preparing. */
static __attribute__((noinline)) void vdc_dpll_manager_feedback_match_retire(void)
{
    __atomic_store_n(&s_feedback_match_owner_token, 0u, __ATOMIC_RELEASE);
}

static __attribute__((noinline)) void vdc_dpll_manager_feedback_match_service(void)
{
    tdma_ring_clock_snapshot_t ring;
    if (s_vdc_tdma_service == NULL) {
        vdc_dpll_manager_feedback_match_retire();
        return;
    }
    if (!tdma_ring_runtime_get_clock_snapshot(&s_vdc_tdma_service->ring_runtime, &ring)) return;
    uint32_t local_request;
    if (!vdc_dpll_manager_try_local_follow_request(&local_request)) return;
    const vdc_dpll_control_profile_t *role = &s_vdc_domain.control.profile;
    const bool admitted = ring.enabled && ring.data_enabled && ring.adapter_started &&
        ring.config_seq && ring.config_seq == ring.applied_config_seq &&
        ring.node_count >= 2u && ring.node_count <= PROJECT_NODE_CAPACITY &&
        ring.local_slot_id < ring.node_count && ring.reference_slot_id < ring.node_count &&
        (local_request ? (ring.local_slot_id != ring.reference_slot_id &&
            role->mode == VDC_DPLL_CONTROL_MODE_FOLLOWER &&
            role->follow_master_slot_id == ring.reference_slot_id &&
            vdc_dpll_manager_feedback_session() != 0u) :
            (ring.local_slot_id == ring.reference_slot_id && role->mode == VDC_DPLL_CONTROL_MODE_MASTER)) &&
        role->valid == 1u && role->generation &&
        ring.local_slot_id == s_vdc_domain.schedule.local_slot_id &&
        ring.schedule_crc32 == s_vdc_domain.schedule.schedule_crc32;
    if (!admitted) {
        vdc_dpll_manager_feedback_match_retire();
        return;
    }
    uint32_t reference_epoch;
    if (local_request) {
        tdma_pio_spi_event_live_snapshot_t live;
        if (!tdma_runtime_owner_get_event_live_snapshot(&live)) return;
        const uint32_t required = TDMA_EVENT_LIVE_RETAINED | TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_VALID;
        reference_epoch = (live.flags & required) == required && live.arm_epoch ? live.record.epoch : 0u;
    } else if (!tdma_runtime_owner_get_origin_reference_epoch(&reference_epoch)) return;
    if (!reference_epoch) {
        vdc_dpll_manager_feedback_match_retire();
        return;
    }
    bool rate_mode;
    if (!vdc_dpll_manager_try_boundary_auto_enabled(&rate_mode)) return;
    const vdc_feedback_match_binding_t binding = {
        .config = ring.config_seq, .role = role->generation, .schedule = ring.schedule_crc32,
        .clock_epoch = s_vdc_domain.clock.epoch_id, .clock_run = s_vdc_domain.clock.run_id,
        .local = ring.local_slot_id, .nodes = ring.node_count, .reference_epoch = reference_epoch,
        .session = vdc_dpll_manager_feedback_session(),
        .rate_mode = local_request ? local_request : (rate_mode ? 1u : 0u)};
    vdc_feedback_match_binding_t previous;
    match_load_words(s_feedback_match_owner_words, &previous, sizeof(previous));
    if (__atomic_load_n(&s_feedback_match_owner_token, __ATOMIC_RELAXED) &&
        memcmp(&binding, &previous, sizeof(binding)) == 0) return;
    /* The acq_rel exchange orders payload writes after revocation. Readers
     * accept only identical nonzero tokens around their bounded word copy. */
    (void)__atomic_exchange_n(&s_feedback_match_owner_token, 0u, __ATOMIC_ACQ_REL);
    if (s_feedback_match_owner_serial == UINT32_MAX) return;
    for (size_t i = 0; i < sizeof(binding) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&binding + i * 4u, 4u);
        __atomic_store_n(s_feedback_match_owner_words + i, word, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_feedback_match_owner_token, ++s_feedback_match_owner_serial, __ATOMIC_RELEASE);
}

/* One bounded authorization copy. Never consult Core1's mutable domain
 * context from this Core0 path. The token also covers pending-role retire. */
static uint32_t match_owner_binding(vdc_feedback_match_binding_t *binding)
{
    const uint32_t token = __atomic_load_n(&s_feedback_match_owner_token, __ATOMIC_ACQUIRE);
    if (!token) return 0u;
    match_load_words(s_feedback_match_owner_words, binding, sizeof(*binding));
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    return token == __atomic_load_n(&s_feedback_match_owner_token, __ATOMIC_ACQUIRE) ? token : 0u;
}

static bool local_follow_current(const vdc_feedback_match_binding_t *binding,
    uint32_t token, tdma_ring_clock_snapshot_t *ring, tdma_pio_spi_event_live_snapshot_t *live)
{
    uint32_t request;
    const uint32_t required = TDMA_EVENT_LIVE_RETAINED | TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_VALID;
    return token && binding->rate_mode >= 2u && s_vdc_tdma_service != NULL &&
        vdc_dpll_manager_try_local_follow_request(&request) && request == binding->rate_mode &&
        token == __atomic_load_n(&s_feedback_match_owner_token, __ATOMIC_ACQUIRE) &&
        binding->session && binding->session == vdc_dpll_manager_feedback_session() &&
        tdma_ring_runtime_get_clock_snapshot(&s_vdc_tdma_service->ring_runtime, ring) &&
        ring->enabled && ring->data_enabled && ring->adapter_started &&
        ring->config_seq == binding->config && ring->applied_config_seq == binding->config &&
        ring->local_slot_id == binding->local && ring->node_count == binding->nodes &&
        ring->reference_slot_id < ring->node_count && ring->reference_slot_id != binding->local &&
        ring->schedule_crc32 == binding->schedule &&
        tdma_runtime_owner_get_event_live_snapshot(live) &&
        (live->flags & required) == required &&
        live->arm_epoch && live->record.epoch == binding->reference_epoch && live->tick_hz;
}

static void local_follow_publish(vdc_feedback_match_source_t *source,
    const vdc_local_follow_candidate_t *candidate)
{
    (void)__atomic_add_fetch(&source->guard, 1u, __ATOMIC_ACQ_REL);
    for (size_t i = 0u; i < sizeof(*candidate) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)candidate + i * 4u, 4u);
        __atomic_store_n(source->words + i, word, __ATOMIC_RELAXED);
    }
    (void)__atomic_add_fetch(&source->guard, 1u, __ATOMIC_RELEASE);
}

static bool local_follow_reference(const vdc_feedback_match_binding_t *b, uint32_t slot,
    distributed_refmem_vdc_feedback_rx_snapshot_t *rx)
{
    return distributed_refmem_copy_vdc_feedback_rx(slot, rx) && rx->active && rx->retained &&
        rx->source_slot == slot && rx->ring_config_seq == b->config && rx->role_generation == b->role &&
        rx->schedule_crc32 == b->schedule && rx->clock_epoch_id == b->clock_epoch &&
        rx->clock_run_id == b->clock_run && rx->sample.source_slot == slot &&
        rx->sample.target_slot == b->local && rx->sample.schema_version == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA &&
        rx->sample.model.control_session == b->session && rx->sample.source_arm_epoch &&
        rx->sample.observer_epoch && rx->sample.tick_hz && rx->sample.model.model_token &&
        rx->sample.model.output_ns_lo <= rx->sample.model.output_ns_hi && rx->receive_count;
}

static void local_follow_prepare(const vdc_feedback_match_binding_t *binding, uint32_t token)
{
    tdma_ring_clock_snapshot_t ring;
    tdma_pio_spi_event_live_snapshot_t live;
    if (!local_follow_current(binding, token, &ring, &live)) return;
    if (token != s_feedback_match_preparation_generation ||
        memcmp(binding, &s_feedback_match_binding, sizeof(*binding)) != 0) {
        vdc_feedback_match_cache_retire(&s_feedback_match_cache);
        __atomic_store_n(&s_feedback_match_active_generation, 0u, __ATOMIC_RELEASE);
        s_feedback_match_binding = *binding;
        s_feedback_match_preparation_generation = token;
        for (uint32_t i = 0u; i < PROJECT_NODE_CAPACITY; ++i) {
            memset(&s_feedback_matches[i].peer, 0, sizeof(s_feedback_matches[i].peer));
            s_feedback_matches[i].consumed = 0u;
        }
    }
    vdc_local_follow_cursor_t *cursor = &s_feedback_matches[binding->local].local;
    vdc_feedback_match_source_t *source = &s_feedback_matches[ring.reference_slot_id];
    if (cursor->arm_epoch && cursor->arm_epoch != live.arm_epoch) return;
    cursor->arm_epoch = live.arm_epoch;
    tdma_pio_spi_event_window_t window;
    if (tdma_runtime_owner_copy_event_history_window(binding->reference_epoch, cursor->next_ordinal,
            &window) != TDMA_EVENT_WINDOW_OK || window.arm_epoch != cursor->arm_epoch ||
        window.observer_epoch != binding->reference_epoch || window.tick_hz != live.tick_hz) return;
    if (!vdc_feedback_match_cache_bind(&s_feedback_match_cache, binding->reference_epoch, window.tick_hz)) return;
    cursor->missed = UINT32_MAX - cursor->missed < window.lost_count ? UINT32_MAX : cursor->missed + window.lost_count;
    for (uint32_t i = 0u; i < window.count; ++i) {
        const tdma_event_history_record_t *record = &window.records[i];
        /* Numeric sequence wrap opens a new cache generation; an old reference
         * cannot join entries left behind in the same sparse index. */
        if (cursor->has_sequence && record->sequence < cursor->latest_sequence) {
            vdc_feedback_match_cache_retire(&s_feedback_match_cache);
            if (!vdc_feedback_match_cache_bind(&s_feedback_match_cache, binding->reference_epoch, window.tick_hz)) return;
            source->peer.has_baseline = 0u;
            source->consumed = 0u;
            cursor->insertion_serial = 0u;
        }
        cursor->has_sequence = 1u; cursor->latest_sequence = record->sequence;
        const uint64_t lo = window.timer1_enable_before + record->rx_elapsed_cycles;
        const uint64_t hi = window.timer1_enable_after + record->rx_elapsed_cycles;
        vdc_dpll_manager_projected_event_t projected;
        uint64_t now;
        const uint32_t prepared = board_uptime_ms();
        bool inserted = false;
        if (cursor->insertion_serial <= UINT32_MAX - 2u &&
            lo >= window.timer1_enable_before && hi >= window.timer1_enable_after &&
            vdc_dpll_manager_project_feedback_event(binding->session, binding->role,
                binding->clock_epoch, binding->clock_run, binding->local, binding->schedule,
                window.tick_hz, lo, hi, &projected) &&
            vdc_timestamp_clock_try_read_ticks64(window.tick_hz, &now) && now >= hi &&
            now - lo <= (uint64_t)window.tick_hz * VDC_LOCAL_FOLLOW_MAX_AGE_MS / 1000u) {
            const uint32_t age = (uint32_t)(((now - lo) * 1000u + window.tick_hz - 1u) / window.tick_hz);
            cursor->insertion_serial += 2u;
            inserted = vdc_feedback_model_cache_put(&s_feedback_match_cache, record->sequence,
                projected.model_token, cursor->insertion_serial, projected.output_ns_lo,
                projected.output_ns_hi, prepared - age - 1u);
        }
        if (inserted) s_feedback_match_insert_count = match_inc(s_feedback_match_insert_count);
        else {
            cursor->projection_rejects = match_inc(cursor->projection_rejects);
            s_feedback_match_reject_count = match_inc(s_feedback_match_reject_count);
        }
    }
    cursor->next_ordinal = window.next_ordinal;
    __atomic_store_n(&s_feedback_match_active_generation, s_feedback_match_cache.generation, __ATOMIC_RELEASE);
    distributed_refmem_vdc_feedback_rx_snapshot_t rx;
    if (!local_follow_reference(binding, ring.reference_slot_id, &rx) || source->consumed == rx.receive_count) return;
    const vdc_feedback_match_lifetime_t remote_lifetime = {
        .source_arm_epoch = rx.sample.source_arm_epoch,
        .source_clock_epoch_id = rx.sample.source_clock_epoch_id, .source_clock_run_id = rx.sample.source_clock_run_id,
        .observer_epoch = rx.sample.observer_epoch, .tick_hz = rx.sample.tick_hz};
    /* Retirement precedes lookup: a first record in a new remote lifetime
     * may miss the local cache; that miss must still discard the old baseline. */
    if (memcmp(&cursor->reference, &remote_lifetime, sizeof(remote_lifetime)) != 0 ||
        (source->peer.has_baseline && source->peer.previous.reference_identity_crc32 != rx.sample.model.model_token))
        source->peer.has_baseline = 0u;
    cursor->reference = remote_lifetime;
    vdc_feedback_match_reference_t local;
    if (!vdc_feedback_match_cache_lookup(&s_feedback_match_cache, rx.sample.measurement_sequence, &local)) {
        /* Core0 prepares before draining transport. A reference can be visible
         * before this bounded harvest reaches it: keep it pending while fresh.
         * RefMem stamps last_rx_ms with osal_tick_ms(), whose epoch/rate need
         * not match board_uptime_ms(). Use the producer's clock for this age. */
        if ((!cursor->has_sequence || cursor->latest_sequence < rx.sample.measurement_sequence) &&
            (uint32_t)(osal_tick_ms() - rx.last_rx_ms) <= VDC_LOCAL_FOLLOW_MAX_AGE_MS) return;
        source->consumed = rx.receive_count;
        cursor->missed = match_inc(cursor->missed);
        return;
    }
    if (cursor->serial == UINT32_MAX) return;
    vdc_dpll_manager_committed_model_t model;
    /* Contended snapshots are retryable with the same retained reference.
     * Never mark it consumed merely because Core1 is publishing its model. */
    uint32_t delay, path_crc;
    if (!vdc_dpll_manager_get_committed_model(&model) ||
        !vdc_dpll_manager_copy_local_follow_path(binding->local, ring.reference_slot_id, binding->schedule,
            &delay, &path_crc)) return;
    source->consumed = rx.receive_count;
    vdc_local_follow_candidate_t candidate = {
        .tag = VDC_LOCAL_FOLLOW_CANDIDATE_TAG, .serial = ++cursor->serial, .owner_token = token,
        .session = binding->session, .role_generation = binding->role, .schedule_crc32 = binding->schedule,
        .ring_config_seq = binding->config, .local_slot = binding->local, .reference_slot = ring.reference_slot_id,
        .local_model_token = local.identity_crc32, .reference_model_token = rx.sample.model.model_token,
        .reference_receive_count = rx.receive_count, .prepared_ms = board_uptime_ms(), .local_event_ms = local.prepared_ms,
        .reference = remote_lifetime, .directed_delay_ns = delay, .path_table_crc32 = path_crc,
    };
    if ((uint32_t)(candidate.prepared_ms - local.prepared_ms) > VDC_LOCAL_FOLLOW_MAX_AGE_MS ||
        model.token != local.identity_crc32 ||
        model.session != binding->session || model.role_generation != binding->role ||
        model.clock_epoch_id != binding->clock_epoch || model.clock_run_id != binding->clock_run ||
        model.local_slot != binding->local || model.dco.tdma_schedule_crc32 != binding->schedule ||
        local.tx_hi < local.tx_lo || local.tx_hi - local.tx_lo > UINT32_MAX ||
        rx.sample.model.output_ns_hi > UINT64_MAX - candidate.directed_delay_ns) {
        local_follow_publish(source, &candidate); return;
    }
    candidate.expected_dco_update_seq = model.dco.dco_update_seq;
    if (cursor->path_crc != candidate.path_table_crc32 || cursor->delay_ns != candidate.directed_delay_ns) {
        source->peer.has_baseline = 0u;
        cursor->path_crc = candidate.path_table_crc32; cursor->delay_ns = candidate.directed_delay_ns;
    }
    const vdc_feedback_match_lifetime_t lifetime = {.source_arm_epoch = cursor->arm_epoch,
        .source_clock_epoch_id = binding->clock_epoch, .source_clock_run_id = binding->clock_run,
        .observer_epoch = binding->reference_epoch, .tick_hz = window.tick_hz};
    const vdc_feedback_match_pair_t pair = {.rx_elapsed_cycles = local.tx_lo,
        .reference_tx_lo = rx.sample.model.output_ns_lo + candidate.directed_delay_ns,
        .reference_tx_hi = rx.sample.model.output_ns_hi + candidate.directed_delay_ns,
        .measurement_sequence = rx.sample.measurement_sequence,
        .reference_identity_crc32 = rx.sample.model.model_token,
        .rx_width_ns = (uint32_t)(local.tx_hi - local.tx_lo), .source_model_token = local.identity_crc32};
    if (vdc_feedback_local_follow_update(&source->peer, binding->reference_epoch,
            s_feedback_match_cache.generation, &lifetime, &pair) == VDC_FEEDBACK_MATCH_MATCHED) {
        candidate.result = source->peer.snapshot;
        candidate.active = 1u;
    }
    if (!local_follow_current(binding, token, &ring, &live) || live.arm_epoch != cursor->arm_epoch ||
        !vdc_dpll_manager_get_committed_model(&model) || model.token != candidate.local_model_token ||
        model.dco.dco_update_seq != candidate.expected_dco_update_seq) candidate.active = 0u;
    local_follow_publish(source, &candidate);
}

bool vdc_dpll_manager_get_local_follow_candidate(vdc_local_follow_candidate_t *out)
{
    if (out == NULL) return false;
    vdc_feedback_match_binding_t binding;
    const uint32_t token = match_owner_binding(&binding);
    tdma_ring_clock_snapshot_t ring;
    tdma_pio_spi_event_live_snapshot_t live;
    if (!local_follow_current(&binding, token, &ring, &live)) return false;
    vdc_feedback_match_source_t *source = &s_feedback_matches[ring.reference_slot_id];
    const uint32_t guard = __atomic_load_n(&source->guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_local_follow_candidate_t candidate;
    match_load_words(source->words, &candidate, sizeof(candidate));
    if (candidate.tag != VDC_LOCAL_FOLLOW_CANDIDATE_TAG) return false;
    distributed_refmem_vdc_feedback_rx_snapshot_t rx;
    candidate.active = candidate.active && candidate.owner_token == token &&
        candidate.session == binding.session && candidate.role_generation == binding.role &&
        candidate.ring_config_seq == binding.config && candidate.schedule_crc32 == binding.schedule &&
        candidate.local_slot == binding.local && candidate.reference_slot == ring.reference_slot_id &&
        candidate.result.reserved == VDC_FEEDBACK_LOCAL_FOLLOW_DOMAIN && candidate.result.has_pair &&
        candidate.result.source.source_arm_epoch == live.arm_epoch &&
        candidate.result.source.observer_epoch == live.record.epoch &&
        candidate.result.source.tick_hz == live.tick_hz &&
        candidate.result.source.source_clock_epoch_id == binding.clock_epoch &&
        candidate.result.source.source_clock_run_id == binding.clock_run &&
        candidate.result.reference_generation == __atomic_load_n(&s_feedback_match_active_generation, __ATOMIC_ACQUIRE) &&
        (uint32_t)(board_uptime_ms() - candidate.local_event_ms) <= VDC_LOCAL_FOLLOW_MAX_AGE_MS &&
        local_follow_reference(&binding, candidate.reference_slot, &rx) &&
        rx.receive_count == candidate.reference_receive_count &&
        rx.sample.source_arm_epoch == candidate.reference.source_arm_epoch &&
        rx.sample.observer_epoch == candidate.reference.observer_epoch &&
        rx.sample.source_clock_epoch_id == candidate.reference.source_clock_epoch_id &&
        rx.sample.source_clock_run_id == candidate.reference.source_clock_run_id &&
        rx.sample.tick_hz == candidate.reference.tick_hz &&
        rx.sample.measurement_sequence == candidate.result.pairs[1].measurement_sequence &&
        rx.sample.model.model_token == candidate.reference_model_token &&
        rx.sample.model.output_ns_hi <= UINT64_MAX - candidate.directed_delay_ns &&
        rx.sample.model.output_ns_lo + candidate.directed_delay_ns == candidate.result.pairs[1].reference_tx_lo &&
        rx.sample.model.output_ns_hi + candidate.directed_delay_ns == candidate.result.pairs[1].reference_tx_hi;
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&source->guard, __ATOMIC_ACQUIRE)) return false;
    if (!local_follow_current(&binding, token, &ring, &live) ||
        live.arm_epoch != candidate.result.source.source_arm_epoch) candidate.active = 0u;
    *out = candidate;
    return true;
}

void vdc_dpll_manager_feedback_prepare_core0(void)
{
    vdc_feedback_match_binding_t binding;
    const uint32_t token = match_owner_binding(&binding);
    tdma_ring_clock_snapshot_t ring;
    if (!token || s_vdc_tdma_service == NULL ||
        !tdma_ring_runtime_get_clock_snapshot(&s_vdc_tdma_service->ring_runtime, &ring)) return;
    if (binding.rate_mode >= 2u) { local_follow_prepare(&binding, token); return; }
    uint32_t local_request;
    if (!vdc_dpll_manager_try_local_follow_request(&local_request) || local_request) return;
    bool rate_mode;
    if (!vdc_dpll_manager_try_boundary_auto_enabled(&rate_mode)) return;
    if (!ring.enabled || !ring.data_enabled || !ring.adapter_started ||
        ring.config_seq != binding.config || ring.applied_config_seq != binding.config ||
        ring.local_slot_id != binding.local || ring.reference_slot_id != binding.local ||
        ring.node_count != binding.nodes || ring.schedule_crc32 != binding.schedule ||
        binding.session != vdc_dpll_manager_feedback_session() ||
        binding.rate_mode != (rate_mode ? 1u : 0u)) return;
    if (token != s_feedback_match_preparation_generation ||
        memcmp(&binding, &s_feedback_match_binding, sizeof(binding)) != 0) {
        vdc_feedback_match_cache_retire(&s_feedback_match_cache);
        __atomic_store_n(&s_feedback_match_active_generation, 0u, __ATOMIC_RELEASE);
        s_feedback_match_binding = binding;
        s_feedback_match_preparation_generation = token;
        for (uint32_t i = 0; i < PROJECT_NODE_CAPACITY; ++i) s_feedback_matches[i].consumed = 0u;
    }

    tdma_origin_raw_reference_t ref;
    if (tdma_runtime_owner_get_origin_raw_reference(&ref)) {
        if (ref.epoch != binding.reference_epoch) return;
        const uint32_t old_version = s_feedback_match_cache.latest_published_version;
        const uint32_t old_generation = s_feedback_match_cache.generation;
        bool inserted = false;
        if (vdc_feedback_match_cache_bind(&s_feedback_match_cache, ref.epoch, ref.tick_hz)) {
            if (!binding.session) {
                inserted = vdc_feedback_match_cache_put(&s_feedback_match_cache, ref.sequence,
                    ref.identity, ref.published_version, ref.timer_lower, ref.timer_upper);
            } else if (old_generation == s_feedback_match_cache.generation &&
                       old_version == ref.published_version) {
                /* Freeze the first projection; a later bridge is not a new event. */
                inserted = true;
            } else {
                vdc_dpll_manager_projected_event_t event;
                uint64_t raw_now;
                const uint32_t prepared_ms = board_uptime_ms();
                const bool projected = binding.rate_mode ?
                    vdc_dpll_manager_project_rate_reference(binding.session, binding.role,
                        binding.clock_epoch, binding.clock_run, binding.local, binding.schedule,
                        ref.tick_hz, ref.timer_lower, ref.timer_upper, &event) :
                    vdc_dpll_manager_project_feedback_event(binding.session, binding.role,
                        binding.clock_epoch, binding.clock_run, binding.local, binding.schedule,
                        ref.tick_hz, ref.timer_lower, ref.timer_upper, &event);
                if (projected &&
                    vdc_timestamp_clock_try_read_ticks64(ref.tick_hz, &raw_now) &&
                    raw_now >= ref.timer_upper && raw_now - ref.timer_lower <=
                        (uint64_t)ref.tick_hz * VDC_FEEDBACK_MATCH_MAX_AGE_MS / 1000u) {
                    /* Include the event's age before preparation. Ceiling preserves
                     * the conservative bound across integer-ms quantization. */
                    const uint32_t age_ms = (uint32_t)(((raw_now - ref.timer_lower) * 1000u +
                        ref.tick_hz - 1u) / ref.tick_hz);
                    inserted = vdc_feedback_model_cache_put(&s_feedback_match_cache, ref.sequence,
                        event.model_token, ref.published_version, event.output_ns_lo,
                        event.output_ns_hi, prepared_ms - age_ms - 1u);
                }
            }
        }
        if (inserted) {
            if (old_generation != s_feedback_match_cache.generation || old_version != ref.published_version)
                s_feedback_match_insert_count = match_inc(s_feedback_match_insert_count);
            __atomic_store_n(&s_feedback_match_active_generation,
                s_feedback_match_cache.generation, __ATOMIC_RELEASE);
        } else s_feedback_match_reject_count = match_inc(s_feedback_match_reject_count);
    }
    if (!s_feedback_match_cache.reference_epoch) return;
    /* One source per service, including a harmless local-source turn. */
    const uint32_t slot = s_feedback_match_cursor++ % ring.node_count;
    if (slot == ring.local_slot_id) return;
    distributed_refmem_vdc_feedback_rx_snapshot_t rx;
    if (!distributed_refmem_copy_vdc_feedback_rx(slot, &rx)) return;
    if (!rx.active || !rx.retained || rx.source_slot != slot ||
        rx.ring_config_seq != binding.config || rx.role_generation != binding.role ||
        rx.schedule_crc32 != binding.schedule || rx.clock_epoch_id != binding.clock_epoch ||
        rx.clock_run_id != binding.clock_run || rx.sample.source_slot != slot ||
        rx.sample.target_slot != ring.local_slot_id ||
        (binding.rate_mode ? (rx.sample.schema_version != REFMEM_VDC_FEEDBACK_RATE_SCHEMA ||
            rx.sample.rate.control_session != binding.session) :
         binding.session ? (rx.sample.schema_version != REFMEM_VDC_FEEDBACK_MODEL_SCHEMA ||
            rx.sample.model.control_session != binding.session) :
            rx.sample.schema_version != REFMEM_VDC_FEEDBACK_SCHEMA)) return;
    vdc_feedback_match_source_t *source = &s_feedback_matches[slot];
    if (!rx.receive_count || source->consumed == rx.receive_count) return;
    uint64_t now = 0u;
    if (!binding.session &&
        !vdc_timestamp_clock_try_read_ticks64(s_feedback_match_cache.tick_hz, &now)) return;
    source->consumed = rx.receive_count;
    vdc_dpll_manager_feedback_match_status_t status;
    match_load_words(source->words, &status, sizeof(status));
    if (status.schema == VDC_LOCAL_FOLLOW_CANDIDATE_TAG) memset(&status, 0, sizeof(status));
    const uint32_t schema = binding.rate_mode ? 3u : (binding.session ? 2u : 1u);
    if (status.schema != schema || status.control_session != binding.session) {
        /* Retained coordinates and age units belong to their original domain
         * and session. Keep lifetime counters, never relabel an old pair. */
        status.active = 0u;
        status.last_age_ticks = 0u;
        status.max_age_ticks = 0u;
        memset(&status.result, 0, sizeof(status.result));
        source->peer.has_baseline = 0u;
    }
    status.schema = schema;
    /* A RATE wait/miss cannot keep the previous estimate eligible. */
    if (binding.rate_mode) status.active = 0u;
    status.control_session = binding.session;
    status.source_slot = slot; status.target_slot = ring.local_slot_id;
    status.ring_config_seq = binding.config; status.role_generation = binding.role;
    status.schedule_crc32 = binding.schedule; status.clock_epoch_id = binding.clock_epoch;
    status.clock_run_id = binding.clock_run; status.receive_count = rx.receive_count;
    status.preparation_generation = token;
    status.cache_insert_count = s_feedback_match_insert_count;
    status.cache_reject_count = s_feedback_match_reject_count;
    status.cache_latest_sequence = s_feedback_match_cache.latest_sequence;
    /* A new source lifetime retires eligibility even when its first record
     * misses the sparse reference cache. Keep the previous pair as history,
     * but never join a later sample to the old acquisition baseline. */
    const vdc_feedback_match_lifetime_t *prior = &source->peer.source;
    if (prior->source_arm_epoch != rx.sample.source_arm_epoch ||
        prior->source_clock_epoch_id != rx.sample.source_clock_epoch_id ||
        prior->source_clock_run_id != rx.sample.source_clock_run_id ||
        prior->observer_epoch != rx.sample.observer_epoch ||
        prior->tick_hz != rx.sample.tick_hz) {
        source->peer.has_baseline = 0u;
        status.active = 0u;
    }
    vdc_feedback_match_reference_t reference;
    if (!vdc_feedback_match_cache_lookup(&s_feedback_match_cache, rx.sample.measurement_sequence, &reference)) {
        status.miss_count = match_inc(status.miss_count);
        status.last_result = VDC_FEEDBACK_MATCH_NO_REFERENCE;
    } else if (binding.session ?
            (uint32_t)(board_uptime_ms() - reference.prepared_ms) > VDC_FEEDBACK_MATCH_MAX_AGE_MS :
            (now < reference.tx_hi || now - reference.tx_lo >
                (uint64_t)s_feedback_match_cache.tick_hz * VDC_FEEDBACK_MATCH_MAX_AGE_MS / 1000u)) {
        status.stale_count = match_inc(status.stale_count);
        status.last_result = VDC_FEEDBACK_MATCH_STALE;
    } else {
        /* Conservative maximum age across the reference ARM bracket. */
        /* Schema 2 retains the legacy field positions but age units are ms. */
        status.last_age_ticks = binding.session ?
            (uint32_t)(board_uptime_ms() - reference.prepared_ms) : (uint32_t)(now - reference.tx_lo);
        if (status.last_age_ticks > status.max_age_ticks) status.max_age_ticks = status.last_age_ticks;
        const vdc_feedback_match_sample_t sample = {
            .source_arm_epoch = rx.sample.source_arm_epoch,
            .rx_elapsed_cycles = binding.rate_mode ? rx.sample.rate.coordinate_ns :
                (binding.session ? rx.sample.model.output_ns_lo : rx.sample.rx_elapsed_cycles),
            .source_clock_epoch_id = rx.sample.source_clock_epoch_id,
            .source_clock_run_id = rx.sample.source_clock_run_id,
            .observer_epoch = rx.sample.observer_epoch,
            .measurement_sequence = rx.sample.measurement_sequence, .tick_hz = rx.sample.tick_hz};
        const uint64_t width = binding.rate_mode ? 1u : binding.session ?
            rx.sample.model.output_ns_hi - rx.sample.model.output_ns_lo : 0u;
        const vdc_feedback_match_result_t result = binding.rate_mode ?
            vdc_feedback_rate_update(&s_feedback_match_cache, &source->peer, &sample,
                rx.sample.rate.model_token) : binding.session ?
            (width > UINT32_MAX ? VDC_FEEDBACK_MATCH_INVALID :
                vdc_feedback_model_update(&s_feedback_match_cache, &source->peer, &sample,
                    (uint32_t)width, rx.sample.model.model_token)) :
            vdc_feedback_match_update(&s_feedback_match_cache, &source->peer, &sample);
        status.last_result = (uint32_t)result;
        if (result == VDC_FEEDBACK_MATCH_MATCHED) {
            (void)vdc_feedback_match_peer_snapshot(&source->peer, &status.result);
            status.match_count = match_inc(status.match_count);
            status.active = 1u;
        } else if (result == VDC_FEEDBACK_MATCH_BASELINED || result == VDC_FEEDBACK_MATCH_INTERVAL_REBASED) {
            status.baseline_count = match_inc(status.baseline_count);
            status.active = 0u;
        } else if (result != VDC_FEEDBACK_MATCH_DUPLICATE &&
                   result != VDC_FEEDBACK_MATCH_WAIT_WINDOW)
            status.invalid_count = match_inc(status.invalid_count);
    }
    /* A paused Core0 worker may finish after STOP or a role transition. Its
     * result can remain as history, but cannot acquire the new authorization.
     * The getter repeats this check after publication as well. */
    uint32_t current_reference_epoch;
    if (!vdc_dpll_manager_try_boundary_auto_enabled(&rate_mode)) return;
    if (!tdma_runtime_owner_get_origin_reference_epoch(&current_reference_epoch) ||
        current_reference_epoch != binding.reference_epoch ||
        token != __atomic_load_n(&s_feedback_match_owner_token, __ATOMIC_ACQUIRE) ||
        binding.session != vdc_dpll_manager_feedback_session() ||
        binding.rate_mode != (rate_mode ? 1u : 0u)) status.active = 0u;
    match_publish(source, &status);
}

bool vdc_dpll_manager_get_feedback_match(uint32_t slot,
    vdc_dpll_manager_feedback_match_status_t *out)
{
    if (out == NULL || slot >= PROJECT_NODE_CAPACITY) return false;
    vdc_feedback_match_source_t *source = &s_feedback_matches[slot];
    const uint32_t guard = __atomic_load_n(&source->guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_dpll_manager_feedback_match_status_t result;
    match_load_words(source->words, &result, sizeof(result));
    if (result.schema == VDC_LOCAL_FOLLOW_CANDIDATE_TAG) return false;
    if (!result.schema) result.schema = 1u;
    result.source_slot = slot;
    tdma_ring_clock_snapshot_t ring;
    vdc_dpll_manager_refmem_snapshot_t vdc;
    const bool current = s_vdc_tdma_service != NULL &&
        tdma_ring_runtime_get_clock_snapshot(&s_vdc_tdma_service->ring_runtime, &ring) &&
        vdc_dpll_manager_get_refmem_snapshot(&vdc) &&
        ring.enabled && ring.data_enabled && ring.adapter_started &&
        ring.config_seq == ring.applied_config_seq && ring.config_seq == result.ring_config_seq &&
        ring.node_count >= 2u && ring.node_count <= PROJECT_NODE_CAPACITY &&
        ring.local_slot_id == result.target_slot && slot < ring.node_count &&
        ring.local_slot_id == ring.reference_slot_id && slot != ring.local_slot_id &&
        ring.schedule_crc32 == result.schedule_crc32 &&
        vdc.schedule.local_slot_id == ring.local_slot_id &&
        vdc.schedule.schedule_crc32 == ring.schedule_crc32 &&
        vdc.control_profile.valid == 1u && vdc.control_profile.mode == VDC_DPLL_CONTROL_MODE_MASTER &&
        vdc.control_profile.generation == result.role_generation &&
        vdc.clock_epoch_id == result.clock_epoch_id && vdc.clock_run_id == result.clock_run_id;
    const uint32_t token = __atomic_load_n(&s_feedback_match_owner_token, __ATOMIC_ACQUIRE);
    bool rate_mode;
    if (!vdc_dpll_manager_try_boundary_auto_enabled(&rate_mode)) return false;
    result.active = current && token && result.preparation_generation == token &&
        result.control_session == vdc_dpll_manager_feedback_session() &&
        (result.schema == 3u) == rate_mode &&
        result.active && result.result.has_pair &&
        result.result.reference_generation ==
            __atomic_load_n(&s_feedback_match_active_generation, __ATOMIC_ACQUIRE);
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&source->guard, __ATOMIC_ACQUIRE)) return false;
    if (!vdc_dpll_manager_try_boundary_auto_enabled(&rate_mode)) return false;
    uint32_t reference_epoch;
    if (!tdma_runtime_owner_get_origin_reference_epoch(&reference_epoch) || !reference_epoch ||
        reference_epoch != result.result.reference_epoch ||
        token != __atomic_load_n(&s_feedback_match_owner_token, __ATOMIC_ACQUIRE) ||
        result.control_session != vdc_dpll_manager_feedback_session() ||
        (result.schema == 3u) != rate_mode) result.active = 0u;
    *out = result;
    return true;
}
