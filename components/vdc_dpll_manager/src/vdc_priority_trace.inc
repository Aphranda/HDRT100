/* Shared 76 x 100-byte maintenance pool, no second record ring. Core0 only
 * reserves the pool and publishes a stopped command. Core1 processes that
 * command between producer calls; its ACK is the ownership-transfer proof.
 * FROZEN bytes and metadata remain immutable until another acknowledged
 * command. READ holds a lease that excludes both submission and consumption. */
typedef struct {
    uint32_t sequence, command, capture_id, session, generation, mode, boundary_request;
} vdc_priority_trace_request_t;
static uint32_t s_priority_trace_request_guard, s_priority_trace_ack;
static vdc_priority_trace_request_t s_priority_trace_request;
static uint32_t s_priority_trace_last_capture; /* Core0, never reused. */
static uint32_t s_priority_trace_status_guard;
static uint32_t s_priority_trace_status_words[sizeof(vdc_priority_trace_status_t) / 4u];
typedef struct {
    vdc_priority_trace_status_t status;
    uint64_t last_sample_raw;
    uint32_t boundary_request, role_generation, seen_running, bound;
    uint32_t have_match, last_match_sequence, have_decision, last_decision_sequence;
} vdc_priority_trace_work_t;
static vdc_priority_trace_work_t s_priority_trace_work;
_Static_assert(sizeof(vdc_priority_trace_status_t) == 148u, "typed trace status ABI");
_Static_assert(sizeof(vdc_priority_trace_header_t) == 168u, "typed trace header ABI");
_Static_assert(sizeof(vdc_priority_trace_record_t) == sizeof(s_dpll_capture_records[0]),
    "typed trace reuses each complete legacy slot");

static bool priority_trace_request_copy(vdc_priority_trace_request_t *out)
{
    const uint32_t guard = __atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    const vdc_priority_trace_request_t copy = {
        .sequence = __atomic_load_n(&s_priority_trace_request.sequence, __ATOMIC_RELAXED),
        .command = __atomic_load_n(&s_priority_trace_request.command, __ATOMIC_RELAXED),
        .capture_id = __atomic_load_n(&s_priority_trace_request.capture_id, __ATOMIC_RELAXED),
        .session = __atomic_load_n(&s_priority_trace_request.session, __ATOMIC_RELAXED),
        .generation = __atomic_load_n(&s_priority_trace_request.generation, __ATOMIC_RELAXED),
        .mode = __atomic_load_n(&s_priority_trace_request.mode, __ATOMIC_RELAXED),
        .boundary_request = __atomic_load_n(&s_priority_trace_request.boundary_request, __ATOMIC_RELAXED)};
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE)) return false;
    *out = copy; return true;
}

static void priority_trace_publish(void)
{
    const uint32_t guard = __atomic_load_n(&s_priority_trace_status_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_trace_status_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    boundary_store(s_priority_trace_status_words, &s_priority_trace_work.status,
        sizeof(s_priority_trace_work.status));
    __atomic_store_n(&s_priority_trace_status_guard, guard + 2u, __ATOMIC_RELEASE);
}

static bool priority_trace_status_copy(vdc_priority_trace_status_t *out)
{
    const uint32_t guard = __atomic_load_n(&s_priority_trace_status_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_priority_trace_status_t copy;
    match_load_words(s_priority_trace_status_words, &copy, sizeof(copy));
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_trace_status_guard, __ATOMIC_ACQUIRE)) return false;
    *out = copy; return true;
}

bool vdc_dpll_manager_get_priority_trace(vdc_priority_trace_status_t *out)
{
    if (!out) return false;
    vdc_priority_trace_request_t request;
    vdc_priority_trace_status_t copy;
    if (!priority_trace_request_copy(&request) || !priority_trace_status_copy(&copy)) return false;
    /* The requested identity is distinct from the owner's acknowledged one. */
    copy.schema = VDC_PRIORITY_TRACE_SCHEMA;
    copy.request_seq = request.sequence;
    copy.command = request.command;
    if (copy.ack_seq != __atomic_load_n(&s_priority_trace_ack, __ATOMIC_ACQUIRE)) return false;
    if (copy.ack_seq == request.sequence &&
        (copy.state == VDC_PRIORITY_TRACE_REJECTED ||
         (copy.state == VDC_PRIORITY_TRACE_IDLE && copy.reason == VDC_PRIORITY_TRACE_RELEASED))) {
        const uint32_t owner = __atomic_load_n(&s_dpll_capture_pool_owner, __ATOMIC_ACQUIRE);
        if (owner != DPLL_CAPTURE_POOL_LEGACY && owner != DPLL_CAPTURE_POOL_LEGACY_ACCESS) return false;
    }
    if (request.sequence != __atomic_load_n(&s_priority_trace_request.sequence, __ATOMIC_ACQUIRE) ||
        (__atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE) & 1u)) return false;
    *out = copy; return true;
}

typedef struct { uint32_t command, capture_id; } vdc_priority_trace_submit_t;
static bool priority_trace_submit_stopped(void *context)
{
    const vdc_priority_trace_submit_t *input = context;
    uint32_t guard = __atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE);
    if ((guard & 1u) || guard > UINT32_MAX - 2u ||
        !__atomic_compare_exchange_n(&s_priority_trace_request_guard, &guard, guard + 1u,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    bool accepted = false, reserved = false;
    const uint32_t previous = __atomic_load_n(&s_priority_trace_request.sequence, __ATOMIC_RELAXED);
    const uint32_t owner = __atomic_load_n(&s_dpll_capture_pool_owner, __ATOMIC_ACQUIRE);
    vdc_priority_trace_status_t status;
    uint32_t request, boundary_session, mode;
    const uint32_t session = vdc_dpll_manager_feedback_session();
    int32_t delta;
    if (previous == UINT32_MAX || previous != __atomic_load_n(&s_priority_trace_ack, __ATOMIC_ACQUIRE) ||
        (owner != DPLL_CAPTURE_POOL_LEGACY && owner != DPLL_CAPTURE_POOL_TYPED) ||
        !priority_trace_status_copy(&status)) goto done;
    if (input->command == VDC_PRIORITY_TRACE_COMMAND_ARM) {
        if (!input->capture_id || input->capture_id <= s_priority_trace_last_capture ||
            (owner == DPLL_CAPTURE_POOL_TYPED && status.state != VDC_PRIORITY_TRACE_FROZEN) ||
            !boundary_request(&request, &boundary_session, &delta, &mode) || !session ||
            session != vdc_dpll_manager_feedback_session() ||
            !vdc_dpll_manager_priority_match_generation() ||
            __atomic_load_n(&s_priority_match_request_session, __ATOMIC_ACQUIRE) != session) goto done;
        if (owner == DPLL_CAPTURE_POOL_LEGACY) {
            uint32_t expected = DPLL_CAPTURE_POOL_LEGACY;
            if (!__atomic_compare_exchange_n(&s_dpll_capture_pool_owner, &expected,
                    DPLL_CAPTURE_POOL_TYPED, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) goto done;
            reserved = true;
        }
        __atomic_store_n(&s_priority_trace_request.capture_id, input->capture_id, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.session, session, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.generation,
            vdc_dpll_manager_priority_match_generation(), __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.mode, mode, __ATOMIC_RELAXED);
        __atomic_store_n(&s_priority_trace_request.boundary_request, request, __ATOMIC_RELAXED);
        s_priority_trace_last_capture = input->capture_id;
    } else if (owner != DPLL_CAPTURE_POOL_TYPED) goto done;
    __atomic_store_n(&s_priority_trace_request.command, input->command, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_trace_request.sequence, previous + 1u, __ATOMIC_RELAXED);
    accepted = true;
done:
    if (reserved && !accepted)
        __atomic_store_n(&s_dpll_capture_pool_owner, DPLL_CAPTURE_POOL_LEGACY, __ATOMIC_RELEASE);
    __atomic_store_n(&s_priority_trace_request_guard, guard + 2u, __ATOMIC_RELEASE);
    return accepted;
}

static bool priority_trace_submit(uint32_t command, uint32_t capture_id)
{
    vdc_priority_trace_submit_t input = {command, capture_id};
    return get_core_num() == 0u && s_vdc_tdma_service &&
        tdma_service_update_stopped_metadata(s_vdc_tdma_service, priority_trace_submit_stopped, &input);
}
bool vdc_dpll_manager_priority_trace_arm(uint32_t capture_id)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_ARM, capture_id); }
bool vdc_dpll_manager_priority_trace_stop(void)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_STOP, 0u); }
bool vdc_dpll_manager_priority_trace_release(void)
{ return priority_trace_submit(VDC_PRIORITY_TRACE_COMMAND_RELEASE, 0u); }

static bool priority_trace_active(void)
{
    const uint32_t state = s_priority_trace_work.status.state;
    return state == VDC_PRIORITY_TRACE_ARMED || state == VDC_PRIORITY_TRACE_RUNNING;
}
static void priority_trace_freeze(uint32_t reason)
{
    vdc_priority_trace_status_t *s = &s_priority_trace_work.status;
    if (!priority_trace_active()) return;
    s->state = VDC_PRIORITY_TRACE_FROZEN; s->reason = reason; s->freeze_ms = board_uptime_ms();
    priority_trace_publish();
}

static __attribute__((noinline)) void priority_trace_service_core1(void)
{
    if (get_core_num() != 1u ||
        __atomic_load_n(&s_dpll_capture_pool_owner, __ATOMIC_ACQUIRE) != DPLL_CAPTURE_POOL_TYPED) return;
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    vdc_priority_trace_status_t *s = &w->status;
    vdc_priority_trace_request_t command;
    if (priority_trace_request_copy(&command) &&
        command.sequence != __atomic_load_n(&s_priority_trace_ack, __ATOMIC_RELAXED)) {
        if (command.command == VDC_PRIORITY_TRACE_COMMAND_ARM) {
            /* Same Core1 boundary follows every earlier legacy append. The
             * reservation never cancelled a live legacy capture on Core0. */
            if (s_dpll_capture_armed) {
                memset(w, 0, sizeof(*w));
                s->schema = VDC_PRIORITY_TRACE_SCHEMA; s->capture_id = command.capture_id;
                s->state = VDC_PRIORITY_TRACE_REJECTED; s->reason = VDC_PRIORITY_TRACE_LEGACY_BUSY;
            } else {
                /* No pool memset: only the next committed record becomes
                 * visible, so ARM work does not scale with capture capacity. */
                /* The pool's old bytes will change format. Retire legacy
                 * completion metadata here, after its sole producer has
                 * quiesced, so RELEASE cannot export typed bytes as schema 6. */
                s_dpll_capture_complete = false;
                s_dpll_capture_count = s_dpll_capture_dropped = 0u;
                s_dpll_capture_first_update_seq = s_dpll_capture_last_update_seq = 0u;
                s_dpll_capture_start_ms = s_dpll_capture_end_ms = 0u;
                memset(w, 0, sizeof(*w));
                s->schema = VDC_PRIORITY_TRACE_SCHEMA; s->capture_id = command.capture_id;
                s->session = command.session; s->generation = command.generation; s->mode = command.mode;
                s->capacity = VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES;
                s->sample_interval_ms = VDC_PRIORITY_TRACE_MATCH_INTERVAL_MS;
                s->state = VDC_PRIORITY_TRACE_ARMED;
                w->boundary_request = command.boundary_request;
            }
        } else if (command.command == VDC_PRIORITY_TRACE_COMMAND_STOP) {
            priority_trace_freeze(VDC_PRIORITY_TRACE_STOP);
        } else if (command.command == VDC_PRIORITY_TRACE_COMMAND_RELEASE) {
            priority_trace_freeze(VDC_PRIORITY_TRACE_RELEASED);
            s->state = VDC_PRIORITY_TRACE_IDLE; s->reason = VDC_PRIORITY_TRACE_RELEASED;
        }
        s->request_seq = s->ack_seq = command.sequence; s->command = command.command;
        const bool release_pool = s->state == VDC_PRIORITY_TRACE_REJECTED ||
            command.command == VDC_PRIORITY_TRACE_COMMAND_RELEASE;
        /* No new command can enter between ACK publication and pool return.
         * The public getter withholds this ACK until return is complete. */
        if (release_pool)
            __atomic_store_n(&s_dpll_capture_pool_owner, DPLL_CAPTURE_POOL_RELEASING, __ATOMIC_RELEASE);
        priority_trace_publish();
        /* ACK release makes every record/status write precede Core0 reuse. */
        __atomic_store_n(&s_priority_trace_ack, command.sequence, __ATOMIC_RELEASE);
        if (release_pool)
            __atomic_store_n(&s_dpll_capture_pool_owner, DPLL_CAPTURE_POOL_LEGACY, __ATOMIC_RELEASE);
    }
    if (!priority_trace_active()) return;
    uint32_t request, session, mode;
    int32_t delta;
    if (vdc_dpll_manager_feedback_session() != s->session ||
        vdc_dpll_manager_priority_match_generation() != s->generation ||
        __atomic_load_n(&s_priority_match_request_session, __ATOMIC_ACQUIRE) != s->session) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_SESSION); return;
    }
    if (boundary_request(&request, &session, &delta, &mode) &&
        (request != w->boundary_request || mode != s->mode)) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_MODE); return;
    }
    tdma_ring_clock_snapshot_t *ring = &s_priority_match_work.ring_after;
    if (tdma_runtime_owner_get_ring_clock_snapshot(ring)) {
        if (ring->enabled && ring->adapter_started) w->seen_running = 1u;
        else if (w->seen_running || w->bound) priority_trace_freeze(VDC_PRIORITY_TRACE_STOP);
    }
}

static bool priority_trace_binding(const vdc_priority_match_snapshot_t *m)
{
    const vdc_priority_trace_work_t *w = &s_priority_trace_work;
    const vdc_priority_trace_status_t *s = &w->status;
    const uint64_t arm = ((uint64_t)s->arm_epoch_hi << 32u) | s->arm_epoch_lo;
    return m->session == s->session && m->generation == s->generation &&
        m->ring_config_seq == s->ring_config_seq && m->ring_applied_seq == s->ring_applied_seq &&
        m->local_slot == s->local_slot && m->reference_slot == s->reference_slot &&
        m->node_count == s->node_count && m->schedule_crc32 == s->schedule_crc32 &&
        m->profile_crc32 == s->profile_crc32 && m->path_crc32 == s->path_crc32 &&
        m->delay_ns == s->delay_ns && m->clock_epoch == s->clock_epoch && m->clock_run == s->clock_run &&
        m->arm_epoch == arm && m->observer_epoch == s->observer_epoch && m->rx_epoch == s->rx_epoch &&
        m->tick_hz == s->tick_hz && m->role_generation == w->role_generation &&
        s_vdc_domain.path_delay.table_crc32 == s->path_table_crc32;
}

static void priority_trace_bind(const vdc_priority_match_snapshot_t *m)
{
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    vdc_priority_trace_status_t *s = &w->status;
    s->ring_config_seq = m->ring_config_seq; s->ring_applied_seq = m->ring_applied_seq;
    s->local_slot = m->local_slot; s->reference_slot = m->reference_slot; s->node_count = m->node_count;
    s->schedule_crc32 = m->schedule_crc32; s->profile_crc32 = m->profile_crc32;
    s->path_table_crc32 = s_vdc_domain.path_delay.table_crc32;
    s->path_crc32 = m->path_crc32; s->delay_ns = m->delay_ns;
    s->clock_epoch = m->clock_epoch; s->clock_run = m->clock_run;
    s->arm_epoch_lo = (uint32_t)m->arm_epoch; s->arm_epoch_hi = (uint32_t)(m->arm_epoch >> 32u);
    s->observer_epoch = m->observer_epoch; s->rx_epoch = m->rx_epoch; s->tick_hz = m->tick_hz;
    w->role_generation = m->role_generation; w->bound = 1u; s->state = VDC_PRIORITY_TRACE_RUNNING;
}

static uint8_t *priority_trace_record_begin(uint32_t kind, uint32_t event, uint32_t carrier)
{
    vdc_priority_trace_status_t *s = &s_priority_trace_work.status;
    if (!priority_trace_active() ||
        __atomic_load_n(&s_dpll_capture_pool_owner, __ATOMIC_ACQUIRE) != DPLL_CAPTURE_POOL_TYPED) return NULL;
    if (s->record_count >= VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES) {
        s->dropped_count = priority_match_inc(s->dropped_count);
        priority_trace_freeze(VDC_PRIORITY_TRACE_FULL); return NULL;
    }
    uint8_t *record = (uint8_t *)&s_dpll_capture_records[s->record_count];
    memset(record, 0, VDC_PRIORITY_TRACE_RECORD_BYTES);
    boundary_capture_u32(record, s->record_count); boundary_capture_u32(record + 4u, kind);
    boundary_capture_u32(record + 8u, board_uptime_ms());
    boundary_capture_u32(record + 12u, event); boundary_capture_u32(record + 16u, carrier);
    return record;
}

static void priority_trace_record_commit(const uint8_t *record, bool decision)
{
    vdc_priority_trace_status_t *s = &s_priority_trace_work.status;
    uint32_t now;
    memcpy(&now, record + 8u, 4u);
    if (!s->record_count) s->first_ms = now;
    s->last_ms = now; ++s->record_count;
    if (decision) s->decision_count = priority_match_inc(s->decision_count);
    else s->match_count = priority_match_inc(s->match_count);
    priority_trace_publish();
}

static __attribute__((noinline)) void priority_trace_match_core1(void)
{
    if (get_core_num() != 1u || !priority_trace_active()) return;
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    const vdc_priority_match_work_t *match = &s_priority_match_work;
    const vdc_priority_match_snapshot_t *m = &match->status;
    if (m->generation == w->status.generation && m->retired) {
        priority_trace_freeze(m->last_reason == VDC_PRIORITY_MATCH_STOP ?
            VDC_PRIORITY_TRACE_STOP : VDC_PRIORITY_TRACE_BINDING); return;
    }
    if (!match->fresh) return;
    if (!m->active || m->retired || !m->have_match || m->last_reason != VDC_PRIORITY_MATCH_OK ||
        m->session != w->status.session || m->generation != w->status.generation) return;
    if (!w->bound) priority_trace_bind(m);
    else if (!priority_trace_binding(m)) { priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING); return; }
    if (w->have_match && m->event_sequence <= w->last_match_sequence) return;
    w->last_match_sequence = m->event_sequence;
    const uint64_t period = (uint64_t)m->tick_hz * VDC_PRIORITY_TRACE_MATCH_INTERVAL_MS / 1000u;
    if (w->have_match && (m->raw_lo < w->last_sample_raw || m->raw_lo - w->last_sample_raw < period)) {
        w->status.skipped_count = priority_match_inc(w->status.skipped_count); return;
    }
    uint8_t *p = priority_trace_record_begin(VDC_PRIORITY_TRACE_MATCH, m->event_sequence, m->carrier_sequence);
    if (!p) return;
    boundary_capture_u64(p + 20u, m->raw_lo); boundary_capture_u64(p + 28u, m->raw_hi);
    boundary_capture_u64(p + 36u, (uint64_t)m->residual_lo);
    boundary_capture_u64(p + 44u, (uint64_t)m->residual_hi);
    boundary_capture_u64(p + 52u, m->local_lo); boundary_capture_u64(p + 60u, m->local_hi);
    boundary_capture_u64(p + 68u, m->remote_lo); boundary_capture_u64(p + 76u, m->remote_hi);
    boundary_capture_u32(p + 84u, m->model_token);
    boundary_capture_u32(p + 88u, match->model.dco.dco_update_seq);
    boundary_capture_u32(p + 92u, (uint32_t)match->model.dco.period_adjust_ppb);
    boundary_capture_u32(p + 96u, m->last_reason);
    w->have_match = 1u; w->last_sample_raw = m->raw_lo;
    priority_trace_record_commit(p, false);
}

static void priority_trace_decision_core1(const vdc_priority_follow_snapshot_t *decision)
{
    if (get_core_num() != 1u || !priority_trace_active() || !decision) return;
    vdc_priority_trace_work_t *w = &s_priority_trace_work;
    if (!w->bound || decision->session != w->status.session || decision->generation != w->status.generation ||
        !priority_trace_binding(&s_priority_follow_work.ticket.match)) {
        priority_trace_freeze(VDC_PRIORITY_TRACE_BINDING); return;
    }
    if (w->have_decision && decision->event_sequence <= w->last_decision_sequence) return;
    uint8_t *p = priority_trace_record_begin(VDC_PRIORITY_TRACE_DECISION,
        decision->event_sequence, decision->carrier_sequence);
    if (!p) return;
    boundary_capture_u32(p + 20u, decision->baseline_sequence);
    boundary_capture_u32(p + 24u, decision->model_token);
    boundary_capture_u32(p + 28u, decision->before_dco_seq);
    boundary_capture_u32(p + 32u, decision->after_dco_seq);
    boundary_capture_u32(p + 36u, (uint32_t)decision->before_ppb);
    boundary_capture_u32(p + 40u, (uint32_t)decision->after_ppb);
    boundary_capture_u32(p + 44u, (uint32_t)decision->selected_delta_ppb);
    boundary_capture_u32(p + 48u, decision->last_reason);
    boundary_capture_u64(p + 52u, (uint64_t)decision->error_lo_ppb);
    boundary_capture_u64(p + 60u, (uint64_t)decision->error_hi_ppb);
    boundary_capture_u64(p + 68u, decision->interval_lo); boundary_capture_u64(p + 76u, decision->interval_hi);
    boundary_capture_u64(p + 84u, decision->local_interval_lo); boundary_capture_u64(p + 92u, decision->local_interval_hi);
    w->have_decision = 1u; w->last_decision_sequence = decision->event_sequence;
    priority_trace_record_commit(p, true);
}

static bool priority_trace_read_frozen(uint32_t capture_id, uint32_t offset,
    uint8_t *data, uint32_t size, uint32_t *total_bytes, uint32_t *file_crc32)
{
    vdc_priority_trace_header_t header;
    tdma_ring_clock_snapshot_t ring;
    if (!vdc_dpll_manager_get_priority_trace(&header.status) ||
        header.status.state != VDC_PRIORITY_TRACE_FROZEN || header.status.capture_id != capture_id ||
        !capture_id || header.status.request_seq != header.status.ack_seq ||
        header.status.ack_seq != __atomic_load_n(&s_priority_trace_ack, __ATOMIC_ACQUIRE) ||
        header.status.record_count > VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES ||
        !tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled || ring.adapter_started ||
        ring.config_seq != ring.applied_config_seq) return false;
    const uint32_t config = ring.config_seq;
    const uint32_t bytes = header.status.record_count * VDC_PRIORITY_TRACE_RECORD_BYTES;
    const uint32_t total = (uint32_t)sizeof(header) + bytes;
    if (offset > total || size > total - offset) return false;
    header.magic = VDC_PRIORITY_TRACE_MAGIC; header.schema = VDC_PRIORITY_TRACE_SCHEMA;
    header.header_size = sizeof(header); header.record_size = VDC_PRIORITY_TRACE_RECORD_BYTES;
    header.payload_crc32 = ota_crc32_compute((const uint8_t *)s_dpll_capture_records, bytes);
    uint32_t crc = ota_crc32_update(0u, (const uint8_t *)&header, sizeof(header));
    crc = ota_crc32_update(crc, (const uint8_t *)s_dpll_capture_records, bytes);
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring) || ring.enabled || ring.adapter_started ||
        ring.config_seq != config || ring.applied_config_seq != config) return false;
    uint32_t copied = 0u;
    if (offset < sizeof(header)) {
        copied = (uint32_t)sizeof(header) - offset;
        if (copied > size) copied = size;
        memcpy(data, (const uint8_t *)&header + offset, copied);
    }
    if (copied < size) memcpy(data + copied, (const uint8_t *)s_dpll_capture_records +
        offset + copied - sizeof(header), size - copied);
    *total_bytes = total; *file_crc32 = crc; return true;
}

bool vdc_dpll_manager_priority_trace_read(uint32_t capture_id, uint32_t offset,
    uint8_t *data, uint32_t size, uint32_t *total_bytes, uint32_t *file_crc32)
{
    if (get_core_num() != 0u || !data || !total_bytes || !file_crc32 || !size ||
        size > VDC_PRIORITY_TRACE_READ_MAX_BYTES) return false;
    /* Exclude a submission while proving no command is in flight. Taking a
     * READ lease from RUNNING/pending state could otherwise cross a Core1
     * command that already passed its owner check and is about to RELEASE. */
    uint32_t guard = __atomic_load_n(&s_priority_trace_request_guard, __ATOMIC_ACQUIRE);
    if ((guard & 1u) || guard > UINT32_MAX - 2u ||
        !__atomic_compare_exchange_n(&s_priority_trace_request_guard, &guard, guard + 1u,
            false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) return false;
    vdc_priority_trace_status_t status;
    uint32_t expected = DPLL_CAPTURE_POOL_TYPED;
    const bool leased = priority_trace_status_copy(&status) &&
        status.state == VDC_PRIORITY_TRACE_FROZEN && status.capture_id == capture_id &&
        status.ack_seq == __atomic_load_n(&s_priority_trace_ack, __ATOMIC_ACQUIRE) &&
        status.ack_seq == __atomic_load_n(&s_priority_trace_request.sequence, __ATOMIC_RELAXED) &&
        __atomic_compare_exchange_n(&s_dpll_capture_pool_owner, &expected,
            DPLL_CAPTURE_POOL_TYPED_READ, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
    __atomic_store_n(&s_priority_trace_request_guard, guard + 2u, __ATOMIC_RELEASE);
    if (!leased) return false;
    const bool result = priority_trace_read_frozen(capture_id, offset, data, size, total_bytes, file_crc32);
    expected = DPLL_CAPTURE_POOL_TYPED_READ;
    return __atomic_compare_exchange_n(&s_dpll_capture_pool_owner, &expected,
        DPLL_CAPTURE_POOL_TYPED, false, __ATOMIC_RELEASE, __ATOMIC_RELAXED) && result;
}
