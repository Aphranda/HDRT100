/* Core1 publishes its actual committed DCO after every service step, including
 * servo steps that intentionally defer the large runtime snapshot. Core0
 * projects fresh events; it never substitutes a later model for an old event.
 * The guard spans the entire step, not just the final copy: a reader cannot
 * observe the old model during a commit-before-publication gap. */
static uint32_t s_model_feedback_session;
static uint32_t s_model_feedback_last_session; /* Core0 stopped gate, never wraps. */
static uint32_t s_committed_model_guard, s_committed_model_serial;
static uint32_t s_committed_model_words[sizeof(vdc_dpll_manager_committed_model_t) / 4u];

static void model_feedback_load(vdc_dpll_manager_committed_model_t *out)
{
    for (size_t i = 0; i < sizeof(*out) / 4u; ++i) {
        const uint32_t word = __atomic_load_n(s_committed_model_words + i, __ATOMIC_RELAXED);
        memcpy((uint8_t *)out + i * 4u, &word, 4u);
    }
}

static __attribute__((noinline)) void model_feedback_end_core1(uint32_t session)
{
    vdc_dpll_manager_committed_model_t model;
    model_feedback_load(&model);
    const bool changed = !model.token || model.session != session ||
        memcmp(&model.dco, &s_vdc_domain.dco, sizeof(model.dco)) != 0 ||
        model.role_generation != s_vdc_domain.control.profile.generation ||
        model.clock_epoch_id != s_vdc_domain.clock.epoch_id ||
        model.clock_run_id != s_vdc_domain.clock.run_id ||
        model.local_slot != s_vdc_domain.schedule.local_slot_id;
    if (changed) {
        model.token = 0u;
        uint64_t now;
        if (s_committed_model_serial != UINT32_MAX && s_vdc_domain.dco.valid == 1u &&
            vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ, &now)) {
            model.token = ++s_committed_model_serial;
            model.valid_from_raw = now;
        }
        model.dco = s_vdc_domain.dco;
        model.session = session;
        model.role_generation = s_vdc_domain.control.profile.generation;
        model.clock_epoch_id = s_vdc_domain.clock.epoch_id;
        model.clock_run_id = s_vdc_domain.clock.run_id;
        model.local_slot = s_vdc_domain.schedule.local_slot_id;
    }
    model.applied_command_seq = s_vdc_domain.control.last_follower_command_seq;
    for (size_t i = 0; i < sizeof(model) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&model + i * 4u, 4u);
        __atomic_store_n(s_committed_model_words + i, word, __ATOMIC_RELAXED);
    }
    (void)__atomic_add_fetch(&s_committed_model_guard, 1u, __ATOMIC_RELEASE);
}

uint32_t vdc_dpll_manager_feedback_session(void)
{
    return __atomic_load_n(&s_model_feedback_session, __ATOMIC_ACQUIRE);
}

static bool model_feedback_set_stopped(void *context)
{
    const uint32_t session = *(const uint32_t *)context;
    const uint32_t current = vdc_dpll_manager_feedback_session();
    if (session && session != current && session <= s_model_feedback_last_session) return false;
    if (session) s_model_feedback_last_session = session;
    __atomic_store_n(&s_model_feedback_session, session, __ATOMIC_RELEASE);
    return true;
}

bool vdc_dpll_manager_set_feedback_session(uint32_t session)
{
    return tdma_service_update_stopped_metadata(s_vdc_tdma_service,
        model_feedback_set_stopped, &session);
}

bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out)
{
    if (out == NULL) return false;
    const uint32_t guard = __atomic_load_n(&s_committed_model_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_dpll_manager_committed_model_t model;
    model_feedback_load(&model);
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&s_committed_model_guard, __ATOMIC_ACQUIRE)) return false;
    *out = model;
    return true;
}

static bool model_feedback_project(uint32_t session,
    uint32_t role_generation, uint32_t clock_epoch_id, uint32_t clock_run_id,
    uint32_t local_slot, uint32_t schedule_crc32, uint32_t tick_hz,
    uint64_t raw_lo, uint64_t raw_hi, uint32_t mode, uint64_t elapsed_ticks,
    vdc_dpll_manager_projected_event_t *out)
{
    bool rate_mode = false;
    if (out == NULL || !session || session != vdc_dpll_manager_feedback_session() ||
        (mode && (!vdc_dpll_manager_try_boundary_auto_enabled(&rate_mode) || !rate_mode)) ||
        (mode == 1u && elapsed_ticks > raw_lo)) return false;
    const uint32_t guard = __atomic_load_n(&s_committed_model_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_dpll_manager_committed_model_t model;
    model_feedback_load(&model);
    if (!model.token || model.session != session || raw_lo < model.valid_from_raw ||
        model.role_generation != role_generation || model.clock_epoch_id != clock_epoch_id ||
        model.clock_run_id != clock_run_id || model.local_slot != local_slot ||
        model.dco.tdma_schedule_crc32 != schedule_crc32) return false;
    vdc_timestamp_clock_bridge_t bridge;
    vdc_dpll_manager_projected_event_t event;
    if (!vdc_timestamp_clock_try_read_bridge(tick_hz, &bridge)) return false;
    if (mode == 2u) {
        /* The read still qualifies the common TIMER0/TIMER1 clock ratio and
         * raw age. Its read-placement bracket is not a RATE coordinate. */
        if (raw_lo > raw_hi || raw_hi > bridge.raw_before ||
            bridge.raw_after < bridge.raw_before ||
            bridge.raw_after - raw_lo > (uint64_t)tick_hz * 2u ||
            !vdc_model_rate_coordinate_ns(model.dco.period_adjust_ppb, tick_hz,
                raw_lo, false, &event.output_ns_lo) ||
            !vdc_model_rate_coordinate_ns(model.dco.period_adjust_ppb, tick_hz,
                raw_hi, true, &event.output_ns_hi)) return false;
    } else {
        if (!vdc_model_project_interval(&model.dco, &bridge, raw_lo, raw_hi,
                &event.output_ns_lo, &event.output_ns_hi)) return false;
        if (mode == 1u &&
            (!vdc_model_rate_coordinate_ns(model.dco.period_adjust_ppb, tick_hz,
                elapsed_ticks, false, &event.output_ns_hi) ||
             event.output_ns_hi == UINT64_MAX)) return false;
    }
    event.model_token = model.token;
    event.applied_command_seq = model.applied_command_seq;
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&s_committed_model_guard, __ATOMIC_ACQUIRE) ||
        session != vdc_dpll_manager_feedback_session() ||
        (mode && (!vdc_dpll_manager_try_boundary_auto_enabled(&rate_mode) || !rate_mode))) return false;
    *out = event;
    return true;
}

bool vdc_dpll_manager_project_feedback_event(uint32_t session,
    uint32_t role_generation, uint32_t clock_epoch_id, uint32_t clock_run_id,
    uint32_t local_slot, uint32_t schedule_crc32, uint32_t tick_hz,
    uint64_t raw_lo, uint64_t raw_hi, vdc_dpll_manager_projected_event_t *out)
{
    return model_feedback_project(session, role_generation, clock_epoch_id, clock_run_id,
        local_slot, schedule_crc32, tick_hz, raw_lo, raw_hi, 0u, 0u, out);
}

vdc_clock_mapping_status_t vdc_dpll_manager_project_mapped_feedback_event(
    uint32_t session, uint32_t role_generation, uint32_t clock_epoch_id,
    uint32_t clock_run_id, uint32_t local_slot, uint32_t schedule_crc32,
    uint32_t tick_hz, uint64_t raw_lo, uint64_t raw_hi,
    const vdc_clock_mapping_cache_t *cache,
    vdc_dpll_manager_mapping_projection_t *projection)
{
    if (get_core_num() != 1u || !cache || !projection || !session ||
        session != vdc_dpll_manager_feedback_session()) return VDC_CLOCK_MAPPING_UNAVAILABLE;
    const uint32_t guard = __atomic_load_n(&s_committed_model_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return VDC_CLOCK_MAPPING_UNAVAILABLE;
    /* The typed owner supplies static work, keeping this full model and the
     * bridge off the deeply nested TDMA stack. Partial evidence is never a
     * publication, and no tentative mapping is committed by this function. */
    vdc_dpll_manager_committed_model_t *model = &projection->model;
    model_feedback_load(model);
    if (!model->token || model->session != session || raw_lo < model->valid_from_raw ||
        model->role_generation != role_generation || model->clock_epoch_id != clock_epoch_id ||
        model->clock_run_id != clock_run_id || model->local_slot != local_slot ||
        model->dco.tdma_schedule_crc32 != schedule_crc32 ||
        !vdc_timestamp_clock_try_read_bridge(tick_hz, &projection->bridge))
        return VDC_CLOCK_MAPPING_UNAVAILABLE;
    const vdc_clock_mapping_status_t status = vdc_clock_mapping_project(cache, &model->dco,
        &projection->bridge, model->token, raw_lo, raw_hi, &projection->mapping);
    __atomic_thread_fence(__ATOMIC_ACQ_REL);
    if (guard != __atomic_load_n(&s_committed_model_guard, __ATOMIC_ACQUIRE) ||
        session != vdc_dpll_manager_feedback_session()) return VDC_CLOCK_MAPPING_UNAVAILABLE;
    return status;
}

bool vdc_dpll_manager_project_rate_feedback_event(uint32_t session,
    uint32_t role_generation, uint32_t clock_epoch_id, uint32_t clock_run_id,
    uint32_t local_slot, uint32_t schedule_crc32, uint32_t tick_hz,
    uint64_t raw_lo, uint64_t raw_hi, uint64_t elapsed_ticks,
    vdc_dpll_manager_rate_event_t *out)
{
    if (!out) return false;
    vdc_dpll_manager_projected_event_t event;
    if (!model_feedback_project(session, role_generation, clock_epoch_id, clock_run_id,
            local_slot, schedule_crc32, tick_hz, raw_lo, raw_hi, 1u, elapsed_ticks, &event))
        return false;
    *out = (vdc_dpll_manager_rate_event_t){.absolute_output_ns_lo = event.output_ns_lo,
        .coordinate_ns = event.output_ns_hi, .model_token = event.model_token,
        .applied_command_seq = event.applied_command_seq};
    return true;
}

bool vdc_dpll_manager_project_rate_reference(uint32_t session,
    uint32_t role_generation, uint32_t clock_epoch_id, uint32_t clock_run_id,
    uint32_t local_slot, uint32_t schedule_crc32, uint32_t tick_hz,
    uint64_t raw_lo, uint64_t raw_hi, vdc_dpll_manager_projected_event_t *out)
{
    return model_feedback_project(session, role_generation, clock_epoch_id, clock_run_id,
        local_slot, schedule_crc32, tick_hz, raw_lo, raw_hi, 2u, 0u, out);
}
