/* Core1/SyncDpllFB alone owns this sparse reference cache and matcher. It
 * consumes Core0-decoded feedback and TDMA-owned normalized raw references;
 * it never reads DMA/PIO, parses wire data, or updates a control/DCO model. */
#define VDC_FEEDBACK_MATCH_MAX_AGE_MS 120u
typedef struct {
    uint32_t config, role, schedule, clock_epoch, clock_run, local, nodes;
} vdc_feedback_match_binding_t;
typedef struct {
    vdc_feedback_match_peer_t peer;
    uint32_t words[sizeof(vdc_dpll_manager_feedback_match_status_t) / 4u];
    uint32_t guard, consumed;
} vdc_feedback_match_source_t;
static vdc_feedback_match_cache_t s_feedback_match_cache;
static vdc_feedback_match_binding_t s_feedback_match_binding;
static vdc_feedback_match_source_t s_feedback_matches[PROJECT_NODE_CAPACITY];
static uint32_t s_feedback_match_cursor, s_feedback_match_insert_count, s_feedback_match_reject_count;
static uint32_t s_feedback_match_active_generation;
_Static_assert(sizeof(vdc_dpll_manager_feedback_match_status_t) % 4u == 0u, "atomic match words");

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

/* Keep these additions in XIP: inlining into the RAM-resident service can
 * advance the aligned BSS start by an entire page on this target. */
static __attribute__((noinline)) void vdc_dpll_manager_feedback_match_retire(void)
{
    if (s_feedback_match_cache.reference_epoch) vdc_feedback_match_cache_retire(&s_feedback_match_cache);
    __atomic_store_n(&s_feedback_match_active_generation, 0u, __ATOMIC_RELEASE);
    s_feedback_match_binding = (vdc_feedback_match_binding_t){0};
}

static __attribute__((noinline)) void vdc_dpll_manager_feedback_match_service(void)
{
    tdma_ring_clock_snapshot_t ring;
    if (s_vdc_tdma_service == NULL ||
        !tdma_ring_runtime_get_clock_snapshot(&s_vdc_tdma_service->ring_runtime, &ring)) return;
    const vdc_dpll_control_profile_t *role = &s_vdc_domain.control.profile;
    const bool admitted = ring.enabled && ring.data_enabled && ring.adapter_started &&
        ring.config_seq && ring.config_seq == ring.applied_config_seq &&
        ring.node_count >= 2u && ring.node_count <= PROJECT_NODE_CAPACITY &&
        ring.local_slot_id < ring.node_count && ring.local_slot_id == ring.reference_slot_id &&
        role->valid == 1u && role->mode == VDC_DPLL_CONTROL_MODE_MASTER && role->generation &&
        ring.local_slot_id == s_vdc_domain.schedule.local_slot_id &&
        ring.schedule_crc32 == s_vdc_domain.schedule.schedule_crc32;
    const vdc_feedback_match_binding_t binding = admitted ? (vdc_feedback_match_binding_t){
        .config = ring.config_seq, .role = role->generation, .schedule = ring.schedule_crc32,
        .clock_epoch = s_vdc_domain.clock.epoch_id, .clock_run = s_vdc_domain.clock.run_id,
        .local = ring.local_slot_id, .nodes = ring.node_count} : (vdc_feedback_match_binding_t){0};
    if (memcmp(&binding, &s_feedback_match_binding, sizeof(binding)) != 0) {
        vdc_feedback_match_cache_retire(&s_feedback_match_cache);
        __atomic_store_n(&s_feedback_match_active_generation, 0u, __ATOMIC_RELEASE);
        s_feedback_match_binding = binding;
        for (uint32_t i = 0; i < PROJECT_NODE_CAPACITY; ++i) s_feedback_matches[i].consumed = 0u;
    }
    if (!admitted) return;

    tdma_origin_raw_reference_t ref;
    if (tdma_runtime_owner_get_origin_raw_reference(&ref)) {
        const uint32_t old_version = s_feedback_match_cache.latest_published_version;
        const uint32_t old_generation = s_feedback_match_cache.generation;
        if (vdc_feedback_match_cache_bind(&s_feedback_match_cache, ref.epoch, ref.tick_hz) &&
            vdc_feedback_match_cache_put(&s_feedback_match_cache, ref.sequence, ref.identity,
                ref.published_version, ref.timer_lower, ref.timer_upper)) {
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
        rx.sample.target_slot != ring.local_slot_id) return;
    vdc_feedback_match_source_t *source = &s_feedback_matches[slot];
    if (!rx.receive_count || source->consumed == rx.receive_count) return;
    uint64_t now;
    if (!vdc_timestamp_clock_try_read_ticks64(s_feedback_match_cache.tick_hz, &now)) return;
    source->consumed = rx.receive_count;
    vdc_dpll_manager_feedback_match_status_t status;
    match_load_words(source->words, &status, sizeof(status));
    status.schema = 1u; status.source_slot = slot; status.target_slot = ring.local_slot_id;
    status.ring_config_seq = binding.config; status.role_generation = binding.role;
    status.schedule_crc32 = binding.schedule; status.clock_epoch_id = binding.clock_epoch;
    status.clock_run_id = binding.clock_run; status.receive_count = rx.receive_count;
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
    } else if (now < reference.tx_hi || now - reference.tx_lo >
            (uint64_t)s_feedback_match_cache.tick_hz * VDC_FEEDBACK_MATCH_MAX_AGE_MS / 1000u) {
        status.stale_count = match_inc(status.stale_count);
        status.last_result = VDC_FEEDBACK_MATCH_STALE;
    } else {
        /* Conservative maximum age across the reference ARM bracket. */
        status.last_age_ticks = (uint32_t)(now - reference.tx_lo);
        if (status.last_age_ticks > status.max_age_ticks) status.max_age_ticks = status.last_age_ticks;
        const vdc_feedback_match_sample_t sample = {
            .source_arm_epoch = rx.sample.source_arm_epoch,
            .rx_elapsed_cycles = rx.sample.rx_elapsed_cycles,
            .source_clock_epoch_id = rx.sample.source_clock_epoch_id,
            .source_clock_run_id = rx.sample.source_clock_run_id,
            .observer_epoch = rx.sample.observer_epoch,
            .measurement_sequence = rx.sample.measurement_sequence, .tick_hz = rx.sample.tick_hz};
        const vdc_feedback_match_result_t result = vdc_feedback_match_update(
            &s_feedback_match_cache, &source->peer, &sample);
        status.last_result = (uint32_t)result;
        if (result == VDC_FEEDBACK_MATCH_MATCHED) {
            (void)vdc_feedback_match_peer_snapshot(&source->peer, &status.result);
            status.match_count = match_inc(status.match_count);
            status.active = 1u;
        } else if (result == VDC_FEEDBACK_MATCH_BASELINED || result == VDC_FEEDBACK_MATCH_INTERVAL_REBASED) {
            status.baseline_count = match_inc(status.baseline_count);
            status.active = 0u;
        } else if (result != VDC_FEEDBACK_MATCH_DUPLICATE) status.invalid_count = match_inc(status.invalid_count);
    }
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
    result.schema = 1u; result.source_slot = slot;
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
    result.active = current && result.active && result.result.has_pair &&
        result.result.reference_generation ==
            __atomic_load_n(&s_feedback_match_active_generation, __ATOMIC_ACQUIRE);
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&source->guard, __ATOMIC_ACQUIRE)) return false;
    *out = result;
    return true;
}
