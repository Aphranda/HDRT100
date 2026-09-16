/* Core1 owns commands and per-peer application state. Core0 owns only the
 * STOP request and TX-done mailbox. All shared payload accesses are atomic
 * words; neither reader retries nor the realtime owner waits for transport. */
typedef struct {
    uint64_t arm, source_output_lo;
    uint32_t clock_epoch, clock_run, observer, model;
    uint32_t command, measurement, offered_ms;
    uint16_t state;
    int16_t delta;
} vdc_boundary_peer_t;
typedef struct {
    uint64_t offer_reference_lo;
    uint32_t session, probe, consumed, config, role, schedule, source, nodes;
    int32_t delta;
    uint32_t offer, serial, offer_slot, authorization;
    uint32_t tx_count, apply_count, reject_count, last_reject, cursor, halted;
    uint32_t offer_reference_model;
    uint32_t first_reject, reject_mask, mode;
} vdc_boundary_meta_t;
static uint32_t s_boundary_peer_words[PROJECT_NODE_CAPACITY][sizeof(vdc_boundary_peer_t) / 4u];
static uint32_t s_boundary_meta_words[sizeof(vdc_boundary_meta_t) / 4u];
static uint32_t s_boundary_guard, s_boundary_request, s_boundary_request_session;
static int32_t s_boundary_request_delta;
static uint32_t s_boundary_request_mode;
static uint32_t s_boundary_tx_done;
_Static_assert(sizeof(vdc_boundary_peer_t) == 48u, "bounded peer history");
_Static_assert(VDC_BOUNDARY_PROBE_MAX_DELTA_PPB <= INT16_MAX &&
               VDC_BOUNDARY_AUTO_MAX_DELTA_PPB <= INT16_MAX,
               "immutable peer delta fits its signed storage");
_Static_assert(VDC_BOUNDARY_APPLIED <= UINT16_MAX, "peer state fits storage");
_Static_assert(sizeof(vdc_boundary_meta_t) % 4u == 0u, "atomic boundary words");

enum {
    BOUNDARY_REJECT_NONE = 0, BOUNDARY_REJECT_BINDING, BOUNDARY_REJECT_MODEL,
    BOUNDARY_REJECT_PAIR, BOUNDARY_REJECT_AGE, BOUNDARY_REJECT_IDENTITY,
    BOUNDARY_REJECT_CANCELLED, BOUNDARY_REJECT_APPLY, BOUNDARY_REJECT_EXHAUSTED,
};
_Static_assert(BOUNDARY_REJECT_EXHAUSTED < 32u, "boundary rejection bitmap width");

static void boundary_store(uint32_t *words, const void *input, size_t size)
{
    for (size_t i = 0; i < size / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)input + i * 4u, 4u);
        __atomic_store_n(words + i, word, __ATOMIC_RELAXED);
    }
}

static bool boundary_request(uint32_t *generation, uint32_t *session, int32_t *delta,
    uint32_t *mode)
{
    const uint32_t before = __atomic_load_n(&s_boundary_request, __ATOMIC_ACQUIRE);
    if (before & 1u) return false;
    *session = __atomic_load_n(&s_boundary_request_session, __ATOMIC_RELAXED);
    *delta = __atomic_load_n(&s_boundary_request_delta, __ATOMIC_RELAXED);
    *mode = __atomic_load_n(&s_boundary_request_mode, __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (before != __atomic_load_n(&s_boundary_request, __ATOMIC_ACQUIRE)) return false;
    *generation = before;
    return true;
}

typedef struct { int32_t delta; uint32_t mode; } vdc_boundary_request_t;

static bool boundary_set_stopped(void *context)
{
    const vdc_boundary_request_t *request = context;
    const uint32_t session = vdc_dpll_manager_feedback_session();
    const uint32_t generation = __atomic_load_n(&s_boundary_request, __ATOMIC_RELAXED);
    if ((!session && (request->delta || request->mode != VDC_BOUNDARY_MODE_PROBE)) ||
        generation > UINT32_MAX - 2u || (generation & 1u)) return false;
    (void)__atomic_add_fetch(&s_boundary_request, 1u, __ATOMIC_ACQ_REL);
    __atomic_store_n(&s_boundary_request_session, session, __ATOMIC_RELAXED);
    __atomic_store_n(&s_boundary_request_delta, request->delta, __ATOMIC_RELAXED);
    __atomic_store_n(&s_boundary_request_mode, request->mode, __ATOMIC_RELAXED);
    (void)__atomic_add_fetch(&s_boundary_request, 1u, __ATOMIC_RELEASE);
    return true;
}

bool vdc_dpll_manager_set_boundary_probe(int32_t delta_ppb)
{
    if (delta_ppb < -VDC_BOUNDARY_PROBE_MAX_DELTA_PPB ||
        delta_ppb > VDC_BOUNDARY_PROBE_MAX_DELTA_PPB) return false;
    vdc_boundary_request_t request = {.delta = delta_ppb, .mode = VDC_BOUNDARY_MODE_PROBE};
    return tdma_service_update_stopped_metadata(s_vdc_tdma_service, boundary_set_stopped, &request);
}

bool vdc_dpll_manager_set_boundary_auto(bool enabled)
{
    vdc_boundary_request_t request = {.mode = enabled ? VDC_BOUNDARY_MODE_AUTO : VDC_BOUNDARY_MODE_PROBE};
    return tdma_service_update_stopped_metadata(s_vdc_tdma_service, boundary_set_stopped, &request);
}

bool vdc_dpll_manager_set_reference_publish(bool enabled)
{
    vdc_boundary_request_t request = {
        .mode = enabled ? VDC_BOUNDARY_MODE_REFERENCE : VDC_BOUNDARY_MODE_PROBE};
    return tdma_service_update_stopped_metadata(s_vdc_tdma_service, boundary_set_stopped, &request);
}

bool vdc_dpll_manager_try_reference_publish_enabled(bool *enabled)
{
    if (enabled == NULL) return false;
    uint32_t request, session, mode;
    int32_t delta;
    if (!boundary_request(&request, &session, &delta, &mode)) return false;
    const uint32_t current = vdc_dpll_manager_feedback_session();
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (request != __atomic_load_n(&s_boundary_request, __ATOMIC_ACQUIRE) ||
        current != vdc_dpll_manager_feedback_session()) return false;
    *enabled = request && mode == VDC_BOUNDARY_MODE_REFERENCE && session && session == current;
    return true;
}

bool vdc_dpll_manager_try_boundary_auto_enabled(bool *enabled)
{
    if (enabled == NULL) return false;
    uint32_t request, session, mode;
    int32_t delta;
    if (!boundary_request(&request, &session, &delta, &mode)) return false;
    const uint32_t guard = __atomic_load_n(&s_boundary_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    const uint32_t live_session = vdc_dpll_manager_feedback_session();
    vdc_boundary_meta_t m;
    match_load_words(s_boundary_meta_words, &m, sizeof(m));
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&s_boundary_guard, __ATOMIC_ACQUIRE) ||
        request != __atomic_load_n(&s_boundary_request, __ATOMIC_ACQUIRE) ||
        live_session != vdc_dpll_manager_feedback_session()) return false;
    *enabled = request && mode == VDC_BOUNDARY_MODE_AUTO && session &&
        session == live_session && (m.probe != request || !m.halted);
    return true;
}

bool vdc_dpll_manager_boundary_auto_enabled(void)
{
    bool enabled;
    return vdc_dpll_manager_try_boundary_auto_enabled(&enabled) && enabled;
}

/* Peer storage keeps every wire field not shared by the immutable request.
 * expected_applied is losslessly command-1: only that succession is issued. */
static void boundary_command(const vdc_boundary_meta_t *m, const vdc_boundary_peer_t *p,
    uint32_t slot, refmem_sync_vdc_boundary_command_t *out)
{
    *out = (refmem_sync_vdc_boundary_command_t){0};
    if (!p->command) return;
    *out = (refmem_sync_vdc_boundary_command_t){
        .target_arm_epoch = p->arm, .basis_source_output_ns_lo = p->source_output_lo,
        .control_session = m->session, .command_seq = p->command,
        .schedule_crc32 = m->schedule, .target_clock_epoch_id = p->clock_epoch,
        .target_clock_run_id = p->clock_run, .target_observer_epoch = p->observer,
        .basis_measurement_sequence = p->measurement, .expected_target_model_token = p->model,
        .expected_applied_command_seq = p->command - 1u, .signed_delta_rate_ppb = p->delta,
        .schema_version = REFMEM_VDC_BOUNDARY_COMMAND_SCHEMA,
        .source_slot = (uint8_t)m->source, .target_slot = (uint8_t)slot,
        .flags = m->mode == VDC_BOUNDARY_MODE_AUTO ? REFMEM_VDC_BOUNDARY_COMMAND_AUTO_FLAGS :
            REFMEM_VDC_BOUNDARY_COMMAND_FLAGS};
}

static bool boundary_ring(tdma_ring_clock_snapshot_t *ring)
{
    return s_vdc_tdma_service != NULL &&
        tdma_ring_runtime_get_clock_snapshot(&s_vdc_tdma_service->ring_runtime, ring);
}

static bool boundary_running(const tdma_ring_clock_snapshot_t *r)
{
    return r->enabled && r->adapter_started && r->data_enabled && r->config_seq &&
        r->config_seq == r->applied_config_seq && r->node_count >= 2u &&
        r->node_count <= PROJECT_NODE_CAPACITY && r->local_slot_id < r->node_count &&
        r->reference_slot_id < r->node_count;
}

static bool boundary_request_current(const vdc_boundary_meta_t *m)
{
    return m->probe && !m->halted && (m->delta || m->mode == VDC_BOUNDARY_MODE_AUTO) &&
        m->probe == __atomic_load_n(&s_boundary_request, __ATOMIC_ACQUIRE) &&
        m->session == vdc_dpll_manager_feedback_session();
}

static bool boundary_role_current(const vdc_boundary_meta_t *m)
{
    return m->role &&
        __atomic_load_n(&s_dpll_role_requested_generation, __ATOMIC_ACQUIRE) == m->role &&
        __atomic_load_n(&s_dpll_role_applied_generation, __ATOMIC_ACQUIRE) == m->role;
}

static bool boundary_fresh(const vdc_dco_control_t *dco, uint64_t lower, uint64_t now);
static bool boundary_reference_fresh(const vdc_boundary_meta_t *m,
    const vdc_dco_control_t *dco, uint64_t lower);

/* Core0 validates its independently copied local binding as well. The
 * matcher authorization prevents an old offer surviving STOP/role ABA. */
int vdc_dpll_manager_copy_boundary_command_offer(uint32_t config, uint32_t role,
    uint32_t source, uint32_t schedule, uint32_t session,
    refmem_sync_vdc_boundary_command_t *out, uint32_t *offer_id)
{
    if (out == NULL || offer_id == NULL) return 0;
    const uint32_t guard = __atomic_load_n(&s_boundary_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return -1;
    vdc_boundary_meta_t m;
    match_load_words(s_boundary_meta_words, &m, sizeof(m));
    if (!m.offer || m.offer_slot >= PROJECT_NODE_CAPACITY || !boundary_request_current(&m) ||
        m.config != config || m.role != role || m.source != source || m.schedule != schedule ||
        m.session != session || !m.authorization ||
        m.authorization != __atomic_load_n(&s_feedback_match_owner_token, __ATOMIC_ACQUIRE)) return 0;
    vdc_boundary_peer_t p;
    match_load_words(s_boundary_peer_words[m.offer_slot], &p, sizeof(p));
    tdma_ring_clock_snapshot_t ring;
    if (!boundary_ring(&ring)) return -1;
    if (!boundary_running(&ring) || ring.config_seq != config || ring.local_slot_id != source ||
        ring.reference_slot_id != source || ring.node_count != m.nodes ||
        ring.schedule_crc32 != schedule || p.state != VDC_BOUNDARY_PENDING ||
        (uint32_t)(board_uptime_ms() - p.offered_ms) >= VDC_BOUNDARY_ACK_TIMEOUT_MS) return 0;
    /* Revalidate at every fragment publication, not just command selection.
     * A later master model cannot lend freshness to the selected reference. */
    const uint32_t model_guard = __atomic_load_n(&s_committed_model_guard, __ATOMIC_ACQUIRE);
    vdc_dpll_manager_committed_model_t model;
    if ((model_guard & 1u) || !vdc_dpll_manager_get_committed_model(&model)) return -1;
    if (model.token != m.offer_reference_model || model.session != session ||
        model.role_generation != role || model.local_slot != source ||
        model.dco.tdma_schedule_crc32 != schedule ||
        !boundary_reference_fresh(&m, &model.dco, m.offer_reference_lo)) return 0;
    refmem_sync_vdc_boundary_command_t command;
    boundary_command(&m, &p, m.offer_slot, &command);
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&s_boundary_guard, __ATOMIC_ACQUIRE) ||
        model_guard != __atomic_load_n(&s_committed_model_guard, __ATOMIC_ACQUIRE)) return -1;
    if (!boundary_request_current(&m) || !boundary_role_current(&m) || m.authorization !=
        __atomic_load_n(&s_feedback_match_owner_token, __ATOMIC_ACQUIRE)) return 0;
    *out = command; *offer_id = m.offer;
    return 1;
}

void vdc_dpll_manager_boundary_command_tx_done_core0(uint32_t offer_id)
{
    /* Core0 reports completion of one immutable offer's entire group budget.
     * Core1 compares the full non-reused identity; a stale notification is inert. */
    __atomic_store_n(&s_boundary_tx_done, offer_id, __ATOMIC_RELEASE);
}

bool vdc_dpll_manager_get_boundary_status(uint32_t slot, vdc_dpll_boundary_status_t *out)
{
    if (out == NULL || slot >= PROJECT_NODE_CAPACITY) return false;
    const uint32_t guard = __atomic_load_n(&s_boundary_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_boundary_meta_t m;
    vdc_boundary_peer_t p;
    match_load_words(s_boundary_meta_words, &m, sizeof(m));
    match_load_words(s_boundary_peer_words[slot], &p, sizeof(p));
    uint32_t request, session, mode;
    int32_t delta;
    if (!boundary_request(&request, &session, &delta, &mode)) return false;
    tdma_ring_clock_snapshot_t ring;
    if (!boundary_ring(&ring)) return false;
    vdc_dpll_boundary_status_t status = {
        .schema = m.mode == VDC_BOUNDARY_MODE_AUTO ? 3u : 2u,
        .active = boundary_request_current(&m) && boundary_running(&ring) &&
            ring.config_seq == m.config && ring.schedule_crc32 == m.schedule,
        .requested_probe_generation = request, .requested_delta_ppb = delta,
        .control_session = m.session, .probe_generation = m.probe, .consumed_mask = m.consumed,
        .offer_id = m.offer, .offer_serial = m.serial, .tx_count = m.tx_count,
        .apply_count = m.apply_count, .reject_count = m.reject_count, .last_reject = m.last_reject,
        .peer_state = p.state, .offered_ms = p.offered_ms,
        .first_reject = m.first_reject, .reject_mask = m.reject_mask};
    boundary_command(&m, &p, slot, &status.command);
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&s_boundary_guard, __ATOMIC_ACQUIRE) ||
        request != __atomic_load_n(&s_boundary_request, __ATOMIC_ACQUIRE)) return false;
    *out = status;
    return true;
}

static void boundary_reject(vdc_boundary_meta_t *m, uint32_t reason)
{
    m->reject_count = match_inc(m->reject_count);
    m->last_reject = reason;
    /* Core1-only diagnostic history for this explicit probe. STOP may update
     * last_reject, but neither STOP nor withdrawal erases earlier branches. */
    if (reason != BOUNDARY_REJECT_NONE) {
        if (m->first_reject == BOUNDARY_REJECT_NONE) m->first_reject = reason;
        if (reason < 32u) m->reject_mask |= UINT32_C(1) << reason;
    }
}

static bool boundary_owner_current(const vdc_boundary_meta_t *m, const tdma_ring_clock_snapshot_t *r)
{
    const vdc_dpll_control_profile_t *role = &s_vdc_domain.control.profile;
    return boundary_request_current(m) && boundary_running(r) && r->config_seq == m->config &&
        r->schedule_crc32 == m->schedule && r->node_count == m->nodes &&
        r->reference_slot_id == m->source && role->valid == 1u && role->generation == m->role &&
        boundary_role_current(m) &&
        s_vdc_domain.ready && r->local_slot_id == s_vdc_domain.schedule.local_slot_id &&
        r->schedule_crc32 == s_vdc_domain.schedule.schedule_crc32 &&
        (r->local_slot_id == m->source ? role->mode == VDC_DPLL_CONTROL_MODE_MASTER :
            (role->mode == VDC_DPLL_CONTROL_MODE_FOLLOWER && role->follow_master_slot_id == m->source));
}

/* Called only by the Core1 writer while the committed-model guard is odd.
 * The public reader would always reject here. A change by this beat's RAM
 * step defers control until end_core1 publishes a new honest model token. */
static bool boundary_model(vdc_dpll_manager_committed_model_t *model, const vdc_boundary_meta_t *m)
{
    model_feedback_load(model);
    return model->token && model->token != UINT32_MAX && model->session == m->session &&
        model->role_generation == m->role && model->clock_epoch_id == s_vdc_domain.clock.epoch_id &&
        model->clock_run_id == s_vdc_domain.clock.run_id &&
        model->local_slot == s_vdc_domain.schedule.local_slot_id &&
        model->applied_command_seq == s_vdc_domain.control.last_follower_command_seq &&
        memcmp(&model->dco, &s_vdc_domain.dco, sizeof(model->dco)) == 0;
}

static bool boundary_fresh(const vdc_dco_control_t *dco, uint64_t lower, uint64_t now)
{
    uint64_t upper;
    /* Manager now is a microsecond counter expressed in ns. Include its
     * quantization upper bound, not a packet-arrival timestamp. */
    return now <= UINT64_MAX - 999u &&
        vdc_domain_dco_local_to_output_ns(dco, now + 999u, &upper) &&
        upper >= lower && upper - lower <= VDC_BOUNDARY_COMMAND_MAX_AGE_NS;
}

static bool boundary_reference_fresh(const vdc_boundary_meta_t *m,
    const vdc_dco_control_t *dco, uint64_t lower)
{
    if (m->mode != VDC_BOUNDARY_MODE_AUTO)
        return boundary_fresh(dco, lower, vdc_dpll_manager_now_ns());
    uint64_t raw, upper;
    return vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &raw) &&
        vdc_model_rate_coordinate_ns(dco->period_adjust_ppb, BOARD_SYS_CLOCK_HZ, raw, true, &upper) &&
        upper >= lower && upper - lower <= VDC_BOUNDARY_COMMAND_MAX_AGE_NS;
}

static bool boundary_feedback(const vdc_boundary_meta_t *m, uint32_t slot,
    distributed_refmem_vdc_feedback_rx_snapshot_t *rx)
{
    return distributed_refmem_copy_vdc_feedback_rx(slot, rx) && rx->active && rx->retained &&
        rx->source_slot == slot && rx->ring_config_seq == m->config && rx->role_generation == m->role &&
        rx->schedule_crc32 == m->schedule && rx->clock_epoch_id == s_vdc_domain.clock.epoch_id &&
        rx->clock_run_id == s_vdc_domain.clock.run_id &&
        (m->mode == VDC_BOUNDARY_MODE_AUTO ?
            (rx->sample.schema_version == REFMEM_VDC_FEEDBACK_RATE_SCHEMA &&
             rx->sample.domain_flags == REFMEM_VDC_FEEDBACK_RATE_FLAGS &&
             rx->sample.rate.control_session == m->session) :
            (rx->sample.schema_version == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA &&
             rx->sample.model.control_session == m->session)) &&
        rx->sample.source_slot == slot && rx->sample.target_slot == m->source &&
        rx->sample.tick_hz == BOARD_SYS_CLOCK_HZ;
}

static bool boundary_lifetime(const vdc_boundary_peer_t *p, const refmem_sync_vdc_feedback_record_t *r)
{
    return p->arm == r->source_arm_epoch && p->observer == r->observer_epoch &&
        p->clock_epoch == r->source_clock_epoch_id && p->clock_run == r->source_clock_run_id;
}

/* Core1 already has its live Domain/ring binding. Copy directly into the
 * caller's one result, avoiding the public diagnostic reader's extra large
 * result + runtime snapshot on the small Core1 stack. Same atomic protocol. */
static bool boundary_match(uint32_t slot, const vdc_boundary_meta_t *m,
    vdc_dpll_manager_feedback_match_status_t *out)
{
    vdc_feedback_match_source_t *source = &s_feedback_matches[slot];
    const uint32_t guard = __atomic_load_n(&source->guard, __ATOMIC_ACQUIRE);
    const uint32_t token = __atomic_load_n(&s_feedback_match_owner_token, __ATOMIC_ACQUIRE);
    if ((guard & 1u) || !token) return false;
    match_load_words(source->words, out, sizeof(*out));
    uint32_t epoch;
    const bool current = out->active && out->source_slot == slot && out->target_slot == m->source &&
        out->clock_epoch_id == s_vdc_domain.clock.epoch_id && out->clock_run_id == s_vdc_domain.clock.run_id &&
        out->preparation_generation == token && out->result.reference_generation ==
            __atomic_load_n(&s_feedback_match_active_generation, __ATOMIC_ACQUIRE) &&
        tdma_runtime_owner_get_origin_reference_epoch(&epoch) && epoch && epoch == out->result.reference_epoch;
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    return current && guard == __atomic_load_n(&source->guard, __ATOMIC_ACQUIRE) &&
        token == __atomic_load_n(&s_feedback_match_owner_token, __ATOMIC_ACQUIRE);
}

static __attribute__((noinline)) void boundary_master(vdc_boundary_meta_t *m, uint32_t slot)
{
    if (slot == m->source) return;
    vdc_boundary_peer_t p;
    match_load_words(s_boundary_peer_words[slot], &p, sizeof(p));
    const uint32_t now_ms = board_uptime_ms();
    if (p.state == VDC_BOUNDARY_PENDING &&
        (uint32_t)(now_ms - p.offered_ms) >= VDC_BOUNDARY_ACK_TIMEOUT_MS) {
        p.state = VDC_BOUNDARY_EXPIRED_UNRESOLVED;
        if (m->offer && m->offer_slot == slot) m->offer = 0u;
        boundary_store(s_boundary_peer_words[slot], &p, sizeof(p));
    }
    distributed_refmem_vdc_feedback_rx_snapshot_t rx;
    if (!boundary_feedback(m, slot, &rx)) return;
    const refmem_sync_vdc_feedback_record_t *r = &rx.sample;
    const bool automatic = m->mode == VDC_BOUNDARY_MODE_AUTO;
    const uint32_t applied_sequence = automatic ? r->rate.applied_command_seq : r->model.applied_command_seq;
    const uint32_t source_model = automatic ? r->rate.model_token : r->model.model_token;
    if (p.state == VDC_BOUNDARY_PENDING || p.state == VDC_BOUNDARY_EXPIRED_UNRESOLVED) {
        if (boundary_lifetime(&p, r) && applied_sequence == p.command &&
            source_model > p.model && r->measurement_sequence > p.measurement) {
            p.state = p.state == VDC_BOUNDARY_PENDING ? VDC_BOUNDARY_ACKED : VDC_BOUNDARY_LATE_APPLIED;
            /* Exact application ACK withdraws any unsent/partial repeats.
             * The immutable old peer remains available until a qualified new window. */
            if (m->offer && m->offer_slot == slot) m->offer = 0u;
            if (automatic) {
                m->consumed &= ~(1u << slot);
                vdc_boundary_capture_ack_core1(rx.record, p.command, p.model);
            }
            boundary_store(s_boundary_peer_words[slot], &p, sizeof(p));
        }
        return;
    }
    if (!automatic && (m->consumed & (1u << slot))) return;
    if (m->offer) return;
    vdc_dpll_manager_feedback_match_status_t match;
    vdc_dpll_manager_committed_model_t model;
    if (!boundary_model(&model, m)) { boundary_reject(m, BOUNDARY_REJECT_MODEL); return; }
    if (!boundary_match(slot, m, &match) || match.schema != (automatic ? 3u : 2u) ||
        match.control_session != m->session || match.ring_config_seq != m->config ||
        match.role_generation != m->role || match.schedule_crc32 != m->schedule ||
        match.receive_count != rx.receive_count || match.last_result != VDC_FEEDBACK_MATCH_MATCHED ||
        !match.result.has_pair || match.result.reserved !=
            (automatic ? VDC_FEEDBACK_RATE_DOMAIN : VDC_FEEDBACK_MODEL_DOMAIN)) {
        boundary_reject(m, BOUNDARY_REJECT_PAIR); return;
    }
    const vdc_feedback_match_pair_t *first = &match.result.pairs[0], *last = &match.result.pairs[1];
    const vdc_feedback_match_lifetime_t *life = &match.result.source;
    if (first->source_model_token != last->source_model_token ||
        first->reference_identity_crc32 != last->reference_identity_crc32 ||
        last->reference_identity_crc32 != model.token || last->source_model_token != source_model ||
        last->measurement_sequence != r->measurement_sequence ||
        (automatic ? (last->rx_elapsed_cycles != r->rate.coordinate_ns || last->rx_width_ns != 1u ||
            r->rate.reserved != 0u) :
            (last->rx_elapsed_cycles != r->model.output_ns_lo || r->model.output_ns_hi < r->model.output_ns_lo ||
             r->model.output_ns_hi - r->model.output_ns_lo != last->rx_width_ns)) ||
        life->source_arm_epoch != r->source_arm_epoch || life->observer_epoch != r->observer_epoch ||
        life->source_clock_epoch_id != r->source_clock_epoch_id ||
        life->source_clock_run_id != r->source_clock_run_id || life->tick_hz != r->tick_hz) {
        boundary_reject(m, BOUNDARY_REJECT_IDENTITY); return;
    }
    /* A completed automatic command only releases a new observation window
     * of the applied model. Old pre-apply pairs never integrate twice. */
    if (automatic && p.command &&
        (!boundary_lifetime(&p, r) || applied_sequence != p.command || source_model <= p.model ||
         first->measurement_sequence <= p.measurement)) return;
    if (!boundary_reference_fresh(m, &model.dco, last->reference_tx_lo)) {
        boundary_reject(m, BOUNDARY_REJECT_AGE); return;
    }
    if (applied_sequence == UINT32_MAX || m->serial == UINT32_MAX) {
        boundary_reject(m, BOUNDARY_REJECT_EXHAUSTED); return;
    }
    tdma_ring_clock_snapshot_t final_ring;
    if (!boundary_ring(&final_ring) || !boundary_owner_current(m, &final_ring) ||
        match.preparation_generation != __atomic_load_n(&s_feedback_match_owner_token, __ATOMIC_ACQUIRE) ||
        !match.preparation_generation) { boundary_reject(m, BOUNDARY_REJECT_CANCELLED); return; }
    int32_t delta = m->delta;
    if (automatic) {
        /* The nearest-to-zero certified bound selects negative feedback.
         * Divide before clamping; no INT64_MIN negation or narrowing wrap. */
        const int64_t lo = match.result.raw_ppb_lo, hi = match.result.raw_ppb_hi;
        if (lo > hi) { boundary_reject(m, BOUNDARY_REJECT_PAIR); return; }
        int64_t step;
        if (lo > VDC_BOUNDARY_AUTO_DEADBAND_PPB) step = -(lo / 4);
        else if (hi < -VDC_BOUNDARY_AUTO_DEADBAND_PPB) step = -(hi / 4);
        else {
            vdc_boundary_capture_hold_core1(m->session, slot, m->source, &match.result);
            return;
        }
        if (step > VDC_BOUNDARY_AUTO_MAX_DELTA_PPB) step = VDC_BOUNDARY_AUTO_MAX_DELTA_PPB;
        if (step < -VDC_BOUNDARY_AUTO_MAX_DELTA_PPB) step = -VDC_BOUNDARY_AUTO_MAX_DELTA_PPB;
        if (!step) {
            vdc_boundary_capture_hold_core1(m->session, slot, m->source, &match.result);
            return;
        }
        delta = (int32_t)step;
    }
    p = (vdc_boundary_peer_t){.arm = r->source_arm_epoch,
        .source_output_lo = automatic ? r->rate.absolute_output_ns_lo : r->model.output_ns_lo,
        .clock_epoch = r->source_clock_epoch_id, .clock_run = r->source_clock_run_id,
        .observer = r->observer_epoch, .model = source_model,
        .command = applied_sequence + 1u, .measurement = r->measurement_sequence,
        .state = VDC_BOUNDARY_PENDING, .offered_ms = now_ms, .delta = (int16_t)delta};
    m->consumed |= 1u << slot;
    m->offer = ++m->serial; m->offer_slot = slot; m->authorization = match.preparation_generation;
    m->offer_reference_lo = last->reference_tx_lo; m->offer_reference_model = model.token;
    m->last_reject = BOUNDARY_REJECT_NONE;
    boundary_store(s_boundary_peer_words[slot], &p, sizeof(p));
    if (automatic) {
        refmem_sync_vdc_boundary_command_t command;
        boundary_command(m, &p, slot, &command);
        vdc_boundary_capture_offer_core1(&command, &match.result);
    }
}

static bool boundary_live(const refmem_sync_vdc_boundary_command_t *command)
{
    tdma_pio_spi_event_live_snapshot_t live;
    const uint32_t flags = TDMA_EVENT_LIVE_RETAINED | TDMA_EVENT_LIVE_ACTIVE | TDMA_EVENT_LIVE_ANCHOR_VALID;
    return tdma_runtime_owner_get_event_live_snapshot(&live) && (live.flags & flags) == flags &&
        live.arm_epoch == command->target_arm_epoch && live.record.epoch == command->target_observer_epoch &&
        live.record.sequence >= command->basis_measurement_sequence;
}

static __attribute__((noinline)) void boundary_follower(vdc_boundary_meta_t *m, uint32_t slot)
{
    const bool automatic = m->mode == VDC_BOUNDARY_MODE_AUTO;
    if (!automatic && (m->consumed & (1u << slot))) return;
    distributed_refmem_vdc_boundary_view_t view;
    if (!distributed_refmem_copy_vdc_boundary_command(m->source, &view) || !view.active) return;
    const refmem_sync_vdc_boundary_command_t *c = &view.command;
    vdc_dpll_manager_committed_model_t model;
    if (!boundary_model(&model, m)) { boundary_reject(m, BOUNDARY_REJECT_MODEL); return; }
    /* A retained already-applied command is routinely visible for many beats.
     * Its sequence cannot cause another application or rejection-counter flood. */
    if (automatic && c->control_session == m->session && c->source_slot == m->source &&
        c->target_slot == slot && c->command_seq <= model.applied_command_seq) return;
    if (view.ring_config_seq != m->config || view.role_generation != m->role ||
        view.schedule_crc32 != m->schedule || view.clock_epoch_id != model.clock_epoch_id ||
        view.clock_run_id != model.clock_run_id || c->schema_version != REFMEM_VDC_BOUNDARY_COMMAND_SCHEMA ||
        c->flags != (automatic ? REFMEM_VDC_BOUNDARY_COMMAND_AUTO_FLAGS :
            REFMEM_VDC_BOUNDARY_COMMAND_FLAGS) || c->reserved || c->source_slot != m->source ||
        c->target_slot != slot || c->control_session != m->session || c->schedule_crc32 != m->schedule ||
        c->target_clock_epoch_id != model.clock_epoch_id || c->target_clock_run_id != model.clock_run_id ||
        c->expected_target_model_token != model.token || c->expected_applied_command_seq != model.applied_command_seq ||
        model.applied_command_seq == UINT32_MAX || c->command_seq != model.applied_command_seq + 1u ||
        (automatic ? (!c->signed_delta_rate_ppb ||
            c->signed_delta_rate_ppb < -VDC_BOUNDARY_AUTO_MAX_DELTA_PPB ||
            c->signed_delta_rate_ppb > VDC_BOUNDARY_AUTO_MAX_DELTA_PPB) :
            c->signed_delta_rate_ppb != m->delta) || !boundary_live(c)) {
        boundary_reject(m, BOUNDARY_REJECT_IDENTITY); return;
    }
    tdma_ring_clock_snapshot_t final_ring;
    if (!boundary_ring(&final_ring) || !boundary_owner_current(m, &final_ring) || !boundary_live(c)) {
        boundary_reject(m, BOUNDARY_REJECT_CANCELLED); return;
    }
    const uint64_t now = vdc_dpll_manager_now_ns();
    if (!boundary_fresh(&model.dco, c->basis_source_output_ns_lo, now)) {
        boundary_reject(m, BOUNDARY_REJECT_AGE); return;
    }
    const vdc_dpll_follower_rate_delta_t command = {
        .source_slot_id = m->source, .target_slot_id = slot, .expected_control_generation = m->role,
        .schedule_crc32 = m->schedule, .servo_profile_crc32 = s_vdc_domain.servo.servo_profile_crc32,
        .clock_epoch_id = model.clock_epoch_id, .clock_run_id = model.clock_run_id,
        .expected_dco_update_seq = model.dco.dco_update_seq,
        .expected_applied_command_seq = c->expected_applied_command_seq,
        .command_seq = c->command_seq, .delta_rate_ppb = c->signed_delta_rate_ppb};
    if (!boundary_ring(&final_ring) || !boundary_owner_current(m, &final_ring) ||
        !boundary_live(c) ||
        !vdc_domain_apply_follower_rate_delta(&s_vdc_domain, &command, now)) {
        boundary_reject(m, BOUNDARY_REJECT_APPLY); return;
    }
    const vdc_boundary_peer_t p = {.arm = c->target_arm_epoch, .source_output_lo = c->basis_source_output_ns_lo,
        .clock_epoch = c->target_clock_epoch_id, .clock_run = c->target_clock_run_id,
        .observer = c->target_observer_epoch, .model = c->expected_target_model_token,
        .command = c->command_seq, .measurement = c->basis_measurement_sequence,
        .state = VDC_BOUNDARY_APPLIED, .offered_ms = board_uptime_ms(),
        .delta = (int16_t)c->signed_delta_rate_ppb};
    boundary_store(s_boundary_peer_words[slot], &p, sizeof(p));
    m->consumed |= 1u << slot; m->apply_count = match_inc(m->apply_count);
    m->last_reject = BOUNDARY_REJECT_NONE;
    if (automatic) vdc_boundary_capture_apply_core1(c, s_vdc_domain.dco.period_adjust_ppb,
        s_vdc_domain.dco.dco_update_seq);
    vdc_dpll_manager_publish_runtime_snapshot_locked();
}

static __attribute__((noinline)) void boundary_service_step(vdc_boundary_meta_t *m,
    uint32_t request, uint32_t session, int32_t delta, uint32_t mode)
{
    if (request != m->probe) {
        /* Only an explicit nonzero STOP probe opens a new diagnostic round.
         * Disabling preserves command history for post-STOP inspection. */
        if (!delta && mode != VDC_BOUNDARY_MODE_AUTO) { m->halted = 1u; m->offer = 0u; return; }
        const uint32_t serial = m->serial;
        *m = (vdc_boundary_meta_t){.session = session, .probe = request,
            .delta = delta, .serial = serial, .mode = mode};
        const vdc_boundary_peer_t empty = {0};
        for (uint32_t i = 0; i < PROJECT_NODE_CAPACITY; ++i)
            boundary_store(s_boundary_peer_words[i], &empty, sizeof(empty));
    }
    if (!boundary_request_current(m)) { m->offer = 0u; m->halted = 1u; return; }
    tdma_ring_clock_snapshot_t ring;
    if (!boundary_ring(&ring)) return;
    if (!m->config) {
        /* Probe may be staged while stopped; bind once the whole local ARM
         * and role request have become active. Never rebind this probe. */
        if (!boundary_running(&ring) || !s_vdc_domain.ready ||
            s_vdc_domain.control.profile.valid != 1u || !s_vdc_domain.control.profile.generation ||
            ring.local_slot_id != s_vdc_domain.schedule.local_slot_id ||
            ring.schedule_crc32 != s_vdc_domain.schedule.schedule_crc32 ||
            (ring.local_slot_id == ring.reference_slot_id ?
                s_vdc_domain.control.profile.mode != VDC_DPLL_CONTROL_MODE_MASTER :
                (s_vdc_domain.control.profile.mode != VDC_DPLL_CONTROL_MODE_FOLLOWER ||
                    s_vdc_domain.control.profile.follow_master_slot_id != ring.reference_slot_id)) ||
            __atomic_load_n(&s_dpll_role_requested_generation, __ATOMIC_ACQUIRE) !=
                __atomic_load_n(&s_dpll_role_applied_generation, __ATOMIC_ACQUIRE)) return;
        m->config = ring.config_seq; m->role = s_vdc_domain.control.profile.generation;
        m->schedule = ring.schedule_crc32; m->source = ring.reference_slot_id; m->nodes = ring.node_count;
    }
    if (!boundary_owner_current(m, &ring)) {
        m->offer = 0u; m->halted = 1u;
        boundary_reject(m, BOUNDARY_REJECT_BINDING);
        return;
    }
    if (m->offer && m->offer == __atomic_load_n(&s_boundary_tx_done, __ATOMIC_ACQUIRE)) {
        m->offer = 0u; m->tx_count = match_inc(m->tx_count);
    }
    if (ring.local_slot_id == m->source) {
        const uint32_t slot = m->cursor;
        m->cursor = slot + 1u == m->nodes ? 0u : slot + 1u;
        boundary_master(m, slot);
    } else boundary_follower(m, ring.local_slot_id);
}

/* XIP wrapper hook: keep new control code out of the almost-full RAM text
 * section. Even with feedback disabled, a STOP/config change retires offers. */
static __attribute__((noinline)) void vdc_boundary_service_core1(void)
{
    uint32_t request, session, mode;
    int32_t delta;
    if (!boundary_request(&request, &session, &delta, &mode) || !request) return;
    (void)__atomic_add_fetch(&s_boundary_guard, 1u, __ATOMIC_ACQ_REL);
    vdc_boundary_meta_t m;
    match_load_words(s_boundary_meta_words, &m, sizeof(m));
    boundary_service_step(&m, request, session, delta, mode);
    boundary_store(s_boundary_meta_words, &m, sizeof(m));
    (void)__atomic_add_fetch(&s_boundary_guard, 1u, __ATOMIC_RELEASE);
}
