/* STOP-installed intent; the offer provider is the sole runtime writer.
 * Only transitions and retirement publish diagnostics, not every offer. */
static uint32_t s_priority_gap_config_guard, s_priority_gap_status_guard;
static vdc_priority_tx_gap_config_t s_priority_gap_config;
static vdc_priority_tx_gap_snapshot_t s_priority_gap;
static uint32_t s_priority_gap_words[sizeof(vdc_priority_tx_gap_snapshot_t) / 4u];

bool vdc_dpll_manager_get_priority_tx_gap_config(vdc_priority_tx_gap_config_t *out)
{
    if (!out) return false;
    const uint32_t guard = __atomic_load_n(&s_priority_gap_config_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    const vdc_priority_tx_gap_config_t value = {
        .generation = __atomic_load_n(&s_priority_gap_config.generation, __ATOMIC_RELAXED),
        .session = __atomic_load_n(&s_priority_gap_config.session, __ATOMIC_RELAXED),
        .after_ms = __atomic_load_n(&s_priority_gap_config.after_ms, __ATOMIC_RELAXED),
        .duration_ms = __atomic_load_n(&s_priority_gap_config.duration_ms, __ATOMIC_RELAXED)};
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_gap_config_guard, __ATOMIC_ACQUIRE)) return false;
    *out = value;
    return true;
}

static bool priority_gap_set_stopped(void *context)
{
    vdc_priority_tx_gap_config_t *c = context;
    c->session = c->generation ? vdc_dpll_manager_feedback_session() : 0u;
    if (c->generation && (!c->session ||
            c->generation != vdc_dpll_manager_priority_sync_generation())) return false;
    const uint32_t guard = __atomic_load_n(&s_priority_gap_config_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_gap_config_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    __atomic_store_n(&s_priority_gap_config.generation, c->generation, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_gap_config.session, c->session, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_gap_config.after_ms, c->after_ms, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_gap_config.duration_ms, c->duration_ms, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_gap_config_guard, guard + 2u, __ATOMIC_RELEASE);
    return true;
}

bool vdc_dpll_manager_set_priority_tx_gap(uint32_t generation, uint32_t after_ms,
    uint32_t duration_ms)
{
    if (get_core_num() != 0u || !s_vdc_tdma_service ||
        (generation ? !duration_ms : (after_ms || duration_ms))) return false;
    vdc_priority_tx_gap_config_t c = {generation, 0u, after_ms, duration_ms};
    return tdma_service_update_stopped_metadata(s_vdc_tdma_service, priority_gap_set_stopped, &c);
}

static void priority_gap_publish(void)
{
    const uint32_t guard = __atomic_load_n(&s_priority_gap_status_guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_priority_gap_status_guard, guard + 1u, __ATOMIC_RELEASE);
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    for (size_t i = 0u; i < sizeof(s_priority_gap) / 4u; ++i) {
        uint32_t word;
        memcpy(&word, (const uint8_t *)&s_priority_gap + 4u * i, 4u);
        __atomic_store_n(s_priority_gap_words + i, word, __ATOMIC_RELAXED);
    }
    __atomic_store_n(&s_priority_gap_status_guard, guard + 2u, __ATOMIC_RELEASE);
}

bool vdc_dpll_manager_get_priority_tx_gap(vdc_priority_tx_gap_snapshot_t *out)
{
    if (!out) return false;
    const uint32_t guard = __atomic_load_n(&s_priority_gap_status_guard, __ATOMIC_ACQUIRE);
    if (guard & 1u) return false;
    vdc_priority_tx_gap_snapshot_t value;
    for (size_t i = 0u; i < sizeof(value) / 4u; ++i) {
        const uint32_t word = __atomic_load_n(s_priority_gap_words + i, __ATOMIC_RELAXED);
        memcpy((uint8_t *)&value + 4u * i, &word, 4u);
    }
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (guard != __atomic_load_n(&s_priority_gap_status_guard, __ATOMIC_ACQUIRE)) return false;
    *out = value;
    return true;
}

static void priority_gap_reset(uint32_t generation)
{
    vdc_priority_tx_gap_config_t c;
    s_priority_gap = (vdc_priority_tx_gap_snapshot_t){.schema = 1u};
    if (vdc_dpll_manager_get_priority_tx_gap_config(&c) && c.generation == generation) {
        s_priority_gap.generation = generation;
        s_priority_gap.session = c.session;
        s_priority_gap.after_ms = c.after_ms;
        s_priority_gap.duration_ms = c.duration_ms;
        s_priority_gap.state = c.session == vdc_dpll_manager_feedback_session()
            ? VDC_PRIORITY_GAP_WAITING : VDC_PRIORITY_GAP_CANCELLED;
    }
    priority_gap_publish();
}

static void priority_gap_cancel(void)
{
    if (s_priority_gap.state < VDC_PRIORITY_GAP_WAITING ||
        s_priority_gap.state > VDC_PRIORITY_GAP_RECOVERING) return;
    s_priority_gap.state = VDC_PRIORITY_GAP_CANCELLED;
    priority_gap_publish();
}

static bool priority_gap_suppress(uint64_t now)
{
    vdc_priority_tx_gap_snapshot_t *g = &s_priority_gap;
    const vdc_priority_tx_work_t *w = &s_priority_tx_work;
    if (g->state < VDC_PRIORITY_GAP_WAITING || g->state > VDC_PRIORITY_GAP_RECOVERING)
        return false;
    if (!g->tick_hz) {
        g->tick_hz = w->source.tick_hz;
        g->anchor_raw = now;
        priority_gap_publish();
    }
    if (now < g->anchor_raw) { priority_gap_cancel(); return false; }
    /* Each multiplication fits uint64 even for maximum uint32 inputs.
     * Divide before adding the two durations to keep the sum bounded. */
    const uint64_t begin = (uint64_t)g->tick_hz * g->after_ms / 1000u;
    const uint64_t end = begin + (uint64_t)g->tick_hz * g->duration_ms / 1000u;
    const uint64_t elapsed = now - g->anchor_raw;
    if (elapsed < begin) return false;
    if (elapsed >= end) {
        if (!g->suppressed) {
            g->state = VDC_PRIORITY_GAP_MISSED;
            priority_gap_publish();
            return false;
        }
        g->state = VDC_PRIORITY_GAP_RECOVERING;
        /* Do not label a cached or in-gap source as recovery. */
        if (w->source.timer_lower >= g->anchor_raw &&
            w->source.timer_lower - g->anchor_raw >= end &&
            w->source.sequence > g->before_sequence) {
            g->resume_raw = now;
            return false;
        }
    }
    const bool first = !g->suppressed;
    if (first) {
        g->state = VDC_PRIORITY_GAP_ACTIVE;
        g->before_sequence = w->status.event_sequence;
        g->first_suppressed_raw = now;
    }
    g->last_suppressed_raw = now;
    g->suppressed = priority_tx_inc(g->suppressed);
    if (first) priority_gap_publish();
    return true;
}

static void priority_gap_resumed(void)
{
    if (s_priority_gap.state != VDC_PRIORITY_GAP_RECOVERING) return;
    s_priority_gap.resume_sequence = s_priority_tx_work.status.event_sequence;
    s_priority_gap.state = VDC_PRIORITY_GAP_DONE;
    priority_gap_publish();
}
