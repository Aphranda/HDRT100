/* Core1 consumes one hardware window once, inside the committed-model guard.
 * No transport, configuration, Flash, or reference resource ownership here. */
static vdc_reference_discipline_status_t s_reference_discipline;
static uint32_t s_reference_discipline_guard;
static uint32_t s_reference_discipline_words[sizeof(vdc_reference_discipline_status_t) / 4u];
static uint32_t s_reference_bound_config, s_reference_bound_profile, s_reference_bound_servo;
static uint64_t s_reference_bound_raw;
static bool s_reference_filter_valid;

static void reference_discipline_publish(void)
{
    (void)__atomic_add_fetch(&s_reference_discipline_guard, 1u, __ATOMIC_ACQ_REL);
    for (size_t i = 0; i < sizeof(s_reference_discipline) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&s_reference_discipline + i * 4u, 4u);
        __atomic_store_n(s_reference_discipline_words + i, word, __ATOMIC_RELAXED);
    }
    (void)__atomic_add_fetch(&s_reference_discipline_guard, 1u, __ATOMIC_RELEASE);
}

bool vdc_dpll_manager_get_reference_discipline(vdc_reference_discipline_status_t *out)
{
    if (!out) return false;
    const uint32_t guard = __atomic_load_n(&s_reference_discipline_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_reference_discipline_status_t value;
    for (size_t i = 0; i < sizeof(value) / 4u; ++i) {
        const uint32_t word = __atomic_load_n(s_reference_discipline_words + i, __ATOMIC_RELAXED);
        memcpy((uint8_t *)&value + i * 4u, &word, 4u);
    }
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&s_reference_discipline_guard, __ATOMIC_ACQUIRE)) return false;
    /* Requested enable is distinct from the Core1 acknowledged request. */
    value.schema = VDC_REFERENCE_DISCIPLINE_SCHEMA;
    value.enabled = __atomic_load_n(&s_reference_discipline_request, __ATOMIC_ACQUIRE) & 1u;
    *out = value;
    return true;
}

static void reference_discipline_hold(uint32_t reason, bool retire)
{
    s_reference_discipline.state = retire ? VDC_REFERENCE_DISCIPLINE_RETIRED : VDC_REFERENCE_DISCIPLINE_HOLD;
    s_reference_discipline.reason = reason;
    s_reference_filter_valid = false;
    reference_discipline_publish();
}

static void reference_discipline_service_core1(uint32_t session)
{
    const uint32_t request = __atomic_load_n(&s_reference_discipline_request, __ATOMIC_ACQUIRE);
    vdc_reference_discipline_status_t *s = &s_reference_discipline;
    if (request != s->request) {
        const uint32_t generation = __atomic_load_n(&s_reference_discipline_capture_generation, __ATOMIC_RELAXED);
        uint32_t armed[5];
        for (size_t i = 0; i < 5u; ++i)
            armed[i] = __atomic_load_n(s_reference_discipline_armed + i, __ATOMIC_RELAXED);
        if (request != __atomic_load_n(&s_reference_discipline_request, __ATOMIC_ACQUIRE)) return;
        if (request & 1u) {
            *s = (vdc_reference_discipline_status_t){.schema = VDC_REFERENCE_DISCIPLINE_SCHEMA,
                .request = request, .enabled = 1u, .state = VDC_REFERENCE_DISCIPLINE_WAIT,
                .reference_generation = generation, .baseline_ppb = s_vdc_domain.reference_baseline_ppb,
                .config_generation = armed[0], .config_crc32 = armed[1],
                .config = {armed[2], armed[3], armed[4]}};
        } else {
            s->request = request; s->enabled = 0u; s->state = VDC_REFERENCE_DISCIPLINE_OFF;
        }
        s_reference_filter_valid = false;
        reference_discipline_publish();
    }
    if (!(request & 1u) || s->state == VDC_REFERENCE_DISCIPLINE_RETIRED) return;
    if (!session) {
        if (s->session) reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_STOP, true);
        return;
    }
    sync_io_reference_snapshot_t m;
    if (!sync_io_reference_get_snapshot(&m)) return;
    if (m.generation != s->reference_generation || m.state != SYNC_IO_REFERENCE_RUNNING ||
        m.reason != SYNC_IO_REFERENCE_OK) {
        if (s->state != VDC_REFERENCE_DISCIPLINE_HOLD)
            reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_NO_INPUT, false);
        return;
    }
    uint64_t now;
    if (!vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &now)) return;
    if (!m.valid || !m.sample_seq) {
        if (s->session && s->state != VDC_REFERENCE_DISCIPLINE_HOLD)
            reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_NO_INPUT, false);
        return;
    }
    if (m.tick_hz != BOARD_SYS_CLOCK_HZ || m.completed_raw > now ||
        now - m.completed_raw > (uint64_t)m.tick_hz * m.config.timeout_ms / 1000u) {
        if (s->reason != VDC_REFERENCE_DISCIPLINE_STALE)
            reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_STALE, false);
        return;
    }
    if (m.sample_seq == s->sample_seq) return;
    if (m.sample_seq < s->sample_seq) {
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_BINDING, true); return;
    }
    s->sample_seq = m.sample_seq;
    s->measured_ppb = m.frequency_error_ppb;
    tdma_ring_clock_snapshot_t ring;
    tdma_origin_raw_reference_t origin;
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring)) return;
    if (!ring.enabled || !ring.adapter_started) {
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_STOP, s->session != 0u); return;
    }
    /* MASTER is bound to its retained autonomous TX record, not the follower
     * RX observer persona. The origin getter rejects inactive/retired records. */
    if (!tdma_runtime_owner_get_origin_raw_reference(&origin)) {
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_NO_INPUT, false); return;
    }
    if (!origin.epoch || !origin.published_version || (origin.published_version & 1u) ||
        origin.tick_hz != m.tick_hz || origin.timer_upper < origin.timer_lower ||
        origin.timer_upper > now || now - origin.timer_lower > (uint64_t)m.tick_hz * 2u) {
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_STALE, false); return;
    }
    if (!s_vdc_domain.ready || !s_vdc_domain.control.profile.valid ||
        s_vdc_domain.control.profile.mode != VDC_DPLL_CONTROL_MODE_MASTER ||
        ring.local_slot_id != ring.reference_slot_id ||
        ring.local_slot_id != s_vdc_domain.schedule.local_slot_id ||
        ring.reference_slot_id != s_vdc_domain.schedule.reference_slot_id ||
        ring.config_seq != ring.applied_config_seq ||
        ring.schedule_crc32 != s_vdc_domain.schedule.schedule_crc32) {
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_BINDING, s->session != 0u); return;
    }
    if (!s->session) {
        s->session = session;
        s->role_generation = s_vdc_domain.control.profile.generation;
        s->clock_epoch = s_vdc_domain.clock.epoch_id;
        s->clock_run = s_vdc_domain.clock.run_id;
        s->origin_epoch = origin.epoch;
        s->origin_sequence = origin.sequence;
        s_reference_bound_config = ring.config_seq;
        s_reference_bound_profile = s_vdc_tdma_service->ring_runtime.ring_profile_crc32;
        s_reference_bound_servo = s_vdc_domain.servo.servo_profile_crc32;
        s_reference_bound_raw = now;
        s->state = VDC_REFERENCE_DISCIPLINE_WAIT;
        reference_discipline_publish(); return;
    }
    if (session != s->session || origin.epoch != s->origin_epoch ||
        ring.config_seq != s_reference_bound_config ||
        s_vdc_tdma_service->ring_runtime.ring_profile_crc32 != s_reference_bound_profile ||
        s_vdc_domain.servo.servo_profile_crc32 != s_reference_bound_servo ||
        s_vdc_domain.control.profile.generation != s->role_generation ||
        s_vdc_domain.clock.epoch_id != s->clock_epoch || s_vdc_domain.clock.run_id != s->clock_run) {
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_BINDING, true); return;
    }
    s->origin_sequence = origin.sequence;
    /* Reconstruct the actual first edge from the completed TIMER1 low word.
     * Exclude windows straddling this RUN binding, including restart races. */
    const uint64_t age = (uint32_t)((uint32_t)m.completed_raw - m.end_raw32);
    const uint64_t span = age + m.elapsed_ticks + m.pio_bias_ticks;
    if (span > m.completed_raw || m.completed_raw - span < s_reference_bound_raw) {
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_STALE, false); return;
    }
    if ((int64_t)m.frequency_error_ppb < -(int64_t)s->config.max_ppb ||
        (int64_t)m.frequency_error_ppb > (int64_t)s->config.max_ppb) {
        if (s->rejected != UINT32_MAX) ++s->rejected;
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_RANGE, false); return;
    }
    const int64_t difference = (int64_t)m.frequency_error_ppb - s->filtered_ppb;
    const uint32_t divisor = s->config.filter_divisor > 1u ? s->config.filter_divisor : 1u;
    const int64_t filter_step = difference / divisor;
    const int32_t filtered = s_reference_filter_valid
        ? (int32_t)((int64_t)s->filtered_ppb + (filter_step ? filter_step : (difference > 0 ? 1 : difference < 0 ? -1 : 0)))
        : m.frequency_error_ppb;
    const int32_t baseline = s_vdc_domain.reference_baseline_ppb;
    const uint64_t step_budget = (uint64_t)s->config.slew_ppb_per_s * m.config.window_ms / 1000u;
    const int64_t step_limit = step_budget > INT32_MAX ? INT32_MAX : (int64_t)step_budget;
    int64_t step = (int64_t)filtered - baseline;
    if (step > step_limit) step = step_limit;
    if (step < -step_limit) step = -step_limit;
    const int32_t target = (int32_t)((int64_t)baseline + step);
    const uint64_t local_ns = vdc_dpll_manager_now_ns();
    if (local_ns == UINT64_MAX || request != __atomic_load_n(&s_reference_discipline_request, __ATOMIC_ACQUIRE) ||
        !vdc_domain_apply_reference_baseline(&s_vdc_domain, target, local_ns)) {
        if (s->rejected != UINT32_MAX) ++s->rejected;
        reference_discipline_hold(VDC_REFERENCE_DISCIPLINE_MODEL, false); return;
    }
    s->filtered_ppb = filtered;
    s_reference_filter_valid = true;
    s->baseline_ppb = s_vdc_domain.reference_baseline_ppb;
    s->dco_update_seq = s_vdc_domain.dco.dco_update_seq;
    if (s->accepted != UINT32_MAX) ++s->accepted;
    if (step && s->applied != UINT32_MAX) ++s->applied;
    s->state = VDC_REFERENCE_DISCIPLINE_TRACK;
    s->reason = VDC_REFERENCE_DISCIPLINE_OK;
    if (step && s_published_snapshot_valid) {
        /* The actuator is committed even between servo and finalize beats.
         * Publish its model pair without promoting unfinished evidence or
         * invoking the capture side effects of the full runtime publisher. */
        (void)__atomic_add_fetch(&s_published_snapshot_guard, 1u, __ATOMIC_ACQ_REL);
        s_published_snapshot.clock = s_vdc_domain.clock;
        s_published_snapshot.dco = s_vdc_domain.dco;
        (void)__atomic_add_fetch(&s_published_snapshot_guard, 1u, __ATOMIC_RELEASE);
    }
    reference_discipline_publish();
}
