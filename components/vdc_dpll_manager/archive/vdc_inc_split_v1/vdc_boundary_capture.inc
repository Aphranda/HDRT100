/* Schema 6 reuses the 100-byte maintenance records. Kinds 1..4 keep their
 * schema-5 layout; AUTO kinds never pretend to be legacy DPLL updates.
 * Core1 appends only; arm/stop/export use the existing capture lifecycle.
 * No new SRAM array, storage call, formatting or allocation is introduced.
 * All offsets below are little endian on RP2350 and the host test target.
 * Common: index@0, uptime_ms@4, kind/context@20. Decoded command or ACK wire@32.
 * Command is the fixed decoded struct, NOT raw TX/RX bytes (no Core1 CRC).
 * Observation: lo@8, session@16, hi@24, six coordinates@32..79,
 * source/reference tokens@80/84, first/last measurements@88/92, domain@96.
 */
enum {
    VDC_CAPTURE_AUTO_OFFER = 5u,
    VDC_CAPTURE_AUTO_APPLY = 6u,
    VDC_CAPTURE_AUTO_ACK = 7u,
    VDC_CAPTURE_AUTO_HOLD = 8u,
    VDC_CAPTURE_AUTO_OBSERVATION = 9u,
    VDC_CAPTURE_LOCAL_OBSERVATION = 10u,
    VDC_CAPTURE_LOCAL_COMMIT = 11u,
};

_Static_assert(sizeof(refmem_sync_vdc_boundary_command_t) == 64u &&
    offsetof(refmem_sync_vdc_boundary_command_t, control_session) == 16u &&
    offsetof(refmem_sync_vdc_boundary_command_t, signed_delta_rate_ppb) == 52u &&
    offsetof(refmem_sync_vdc_boundary_command_t, schema_version) == 60u,
    "schema6 decoded command layout changed");

static void boundary_capture_u32(uint8_t *p, uint32_t value)
{ memcpy(p, &value, sizeof(value)); }
static void boundary_capture_u64(uint8_t *p, uint64_t value)
{ memcpy(p, &value, sizeof(value)); }

static bool boundary_capture_room(uint32_t needed)
{
    if (!s_dpll_capture_armed) return false;
    if (needed > VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES - s_dpll_capture_count) {
        s_dpll_capture_dropped += needed;
        s_dpll_capture_armed = false;
        s_dpll_capture_complete = true;
        return false;
    }
    return true;
}

static uint8_t *boundary_capture_begin(uint32_t offset, uint32_t kind, uint32_t now)
{
    uint8_t *p = (uint8_t *)&s_dpll_capture_records[s_dpll_capture_count + offset];
    memset(p, 0, sizeof(s_dpll_capture_records[0]));
    boundary_capture_u32(p, s_dpll_capture_count + offset + 1u);
    boundary_capture_u32(p + 4, now);
    boundary_capture_u32(p + 20, kind);
    return p;
}

static void boundary_capture_commit(uint32_t count, uint32_t now)
{
    if (!s_dpll_capture_count) s_dpll_capture_start_ms = now;
    s_dpll_capture_count += count;
    s_dpll_capture_end_ms = now;
    /* Do not change last_update_seq: it gates legacy DPLL appends. */
    if (s_dpll_capture_count == VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES) {
        s_dpll_capture_armed = false;
        s_dpll_capture_complete = true;
    }
}

static void boundary_capture_observation(uint8_t *p, uint32_t session,
    const vdc_feedback_match_snapshot_t *m)
{
    boundary_capture_u64(p + 8, (uint64_t)m->raw_ppb_lo);
    boundary_capture_u32(p + 16, session);
    boundary_capture_u64(p + 24, (uint64_t)m->raw_ppb_hi);
    boundary_capture_u64(p + 32, m->pairs[0].rx_elapsed_cycles);
    boundary_capture_u64(p + 40, m->pairs[1].rx_elapsed_cycles);
    boundary_capture_u64(p + 48, m->pairs[0].reference_tx_lo);
    boundary_capture_u64(p + 56, m->pairs[0].reference_tx_hi);
    boundary_capture_u64(p + 64, m->pairs[1].reference_tx_lo);
    boundary_capture_u64(p + 72, m->pairs[1].reference_tx_hi);
    boundary_capture_u32(p + 80, m->pairs[1].source_model_token);
    boundary_capture_u32(p + 84, m->pairs[1].reference_identity_crc32);
    boundary_capture_u32(p + 88, m->pairs[0].measurement_sequence);
    boundary_capture_u32(p + 92, m->pairs[1].measurement_sequence);
    boundary_capture_u32(p + 96, m->reserved);
}

static void vdc_boundary_capture_offer_core1(
    const refmem_sync_vdc_boundary_command_t *c, const vdc_feedback_match_snapshot_t *m)
{
    /* Reserve both records together so a full buffer cannot orphan an offer. */
    if (!boundary_capture_room(2u)) return;
    const uint32_t now = board_uptime_ms();
    uint8_t *offer = boundary_capture_begin(1u, VDC_CAPTURE_AUTO_OFFER, now);
    memcpy(offer + 32, c, sizeof(*c));
    boundary_capture_u64(offer + 8, (uint64_t)m->raw_ppb_lo);
    boundary_capture_u32(offer + 16, m->pairs[0].source_model_token);
    boundary_capture_u64(offer + 24, (uint64_t)m->raw_ppb_hi);
    boundary_capture_u32(offer + 96, m->pairs[0].reference_identity_crc32);
    uint8_t *observation = boundary_capture_begin(0u,
        VDC_CAPTURE_AUTO_OBSERVATION | ((uint32_t)c->target_slot << 8u) |
        ((uint32_t)c->source_slot << 16u), now);
    boundary_capture_observation(observation, c->control_session, m);
    boundary_capture_commit(2u, now);
}

static void vdc_boundary_capture_apply_core1(
    const refmem_sync_vdc_boundary_command_t *c, int32_t actual_rate, uint32_t dco_seq)
{
    if (!boundary_capture_room(1u)) return;
    const uint32_t now = board_uptime_ms();
    uint8_t *p = boundary_capture_begin(0u, VDC_CAPTURE_AUTO_APPLY, now);
    memcpy(p + 32, c, sizeof(*c));
    boundary_capture_u32(p + 8, (uint32_t)actual_rate);
    boundary_capture_u32(p + 12, dco_seq);
    boundary_capture_commit(1u, now);
}

static void vdc_boundary_capture_ack_core1(const uint8_t *wire,
    uint32_t command, uint32_t prior_model)
{
    if (!boundary_capture_room(1u)) return;
    const uint32_t now = board_uptime_ms();
    uint8_t *p = boundary_capture_begin(0u, VDC_CAPTURE_AUTO_ACK, now);
    boundary_capture_u32(p + 8, command);
    boundary_capture_u32(p + 12, prior_model);
    memcpy(p + 32, wire, REFMEM_VDC_FEEDBACK_RECORD_SIZE);
    boundary_capture_commit(1u, now);
}

static void vdc_boundary_capture_hold_core1(uint32_t session, uint32_t source,
    uint32_t reference, const vdc_feedback_match_snapshot_t *m)
{
    if (!s_dpll_capture_armed) return;
    /* A mature HOLD can be read many times before another Core0 publication.
     * Search the bounded existing history, adding no persistent peer state. */
    const uint32_t context = VDC_CAPTURE_AUTO_HOLD | (source << 8u) | (reference << 16u);
    for (uint32_t i = s_dpll_capture_count; i; --i) {
        const uint8_t *p = (const uint8_t *)&s_dpll_capture_records[i - 1u];
        uint32_t previous_context, previous_session, previous_measurement;
        memcpy(&previous_context, p + 20, 4u);
        if (previous_context != context) continue;
        memcpy(&previous_session, p + 16, 4u);
        memcpy(&previous_measurement, p + 92, 4u);
        if (previous_session == session && previous_measurement == m->pairs[1].measurement_sequence)
            return;
        break;
    }
    if (!boundary_capture_room(1u)) return;
    const uint32_t now = board_uptime_ms();
    uint8_t *p = boundary_capture_begin(0u, context, now);
    boundary_capture_observation(p, session, m);
    boundary_capture_commit(1u, now);
}

/* Two fixed records keep the local/NO1 uncertainty bounds and actual Domain
 * outcome together. No remote command is synthesized, no GPIO claim made.
 * LOCAL_COMMIT: before rate/seq@8/12, session@16, after rate/seq@24/28,
 * owner/serial@32/36, role/config/schedule@40/44/48, model tokens@52/56,
 * reference receive@60, local widths@64/68, step@72, remote seq@76,
 * path CRC/delay@80/84, local ARM/observer@88/96. Context bit24=applied. */
static void vdc_boundary_capture_local_core1(const vdc_local_follow_candidate_t *c,
    int32_t delta, int32_t before_rate, uint32_t before_seq,
    int32_t after_rate, uint32_t after_seq, uint32_t remote_command_seq, bool applied)
{
    if (!boundary_capture_room(2u)) return;
    const uint32_t now = board_uptime_ms();
    const uint32_t context = (c->local_slot << 8u) | (c->reference_slot << 16u);
    uint8_t *o = boundary_capture_begin(0u, VDC_CAPTURE_LOCAL_OBSERVATION | context, now);
    boundary_capture_observation(o, c->session, &c->result);
    uint8_t *p = boundary_capture_begin(1u, VDC_CAPTURE_LOCAL_COMMIT | context |
        (applied ? (1u << 24u) : 0u), now);
    boundary_capture_u32(p + 8, (uint32_t)before_rate);
    boundary_capture_u32(p + 12, before_seq);
    boundary_capture_u32(p + 16, c->session);
    boundary_capture_u32(p + 24, (uint32_t)after_rate);
    boundary_capture_u32(p + 28, after_seq);
    boundary_capture_u32(p + 32, c->owner_token);
    boundary_capture_u32(p + 36, c->serial);
    boundary_capture_u32(p + 40, c->role_generation);
    boundary_capture_u32(p + 44, c->ring_config_seq);
    boundary_capture_u32(p + 48, c->schedule_crc32);
    boundary_capture_u32(p + 52, c->local_model_token);
    boundary_capture_u32(p + 56, c->reference_model_token);
    boundary_capture_u32(p + 60, c->reference_receive_count);
    boundary_capture_u32(p + 64, c->result.pairs[0].rx_width_ns);
    boundary_capture_u32(p + 68, c->result.pairs[1].rx_width_ns);
    boundary_capture_u32(p + 72, (uint32_t)delta);
    boundary_capture_u32(p + 76, remote_command_seq);
    boundary_capture_u32(p + 80, c->path_table_crc32);
    boundary_capture_u32(p + 84, c->directed_delay_ns);
    boundary_capture_u64(p + 88, c->result.source.source_arm_epoch);
    boundary_capture_u32(p + 96, c->result.source.observer_epoch);
    boundary_capture_commit(2u, now);
}
