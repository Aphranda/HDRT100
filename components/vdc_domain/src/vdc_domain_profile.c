#include "vdc_domain.h"

#include "tdma_ring_runtime.h"

#include <limits.h>
#include <string.h>

#include "tdma_operating_profile.h"

#include "vdc_domain_internal.h"

bool vdc_domain_dco_output_phase_residual_ns(const vdc_dco_control_t *dco,
                                             uint64_t local_rx_ns,
                                             uint32_t reference_output_phase_ns,
                                             uint32_t directed_delay_ns,
                                             uint32_t period_ns,
                                             int32_t *residual_ns)
{
    if (residual_ns == NULL || period_ns == 0u ||
        reference_output_phase_ns >= period_ns) return false;
    uint64_t output_ns;
    if (!vdc_domain_dco_local_to_output_ns(dco, local_rx_ns, &output_ns)) return false;
    const uint64_t expected_phase =
        ((uint64_t)reference_output_phase_ns + directed_delay_ns) % period_ns;
    int64_t residual = (int64_t)(output_ns % period_ns) - (int64_t)expected_phase;
    const int64_t half = (int64_t)period_ns / 2ll;
    /* Both phases are already reduced, so at most one correction is needed.
     * For every uint32_t period the centered result fits int32_t. */
    if (residual > half) residual -= period_ns;
    else if (residual < -half) residual += period_ns;
    *residual_ns = (int32_t)residual;
    return true;
}

static bool vdc_domain_validate_tdma_timestamp_evidence_window(
    const vdc_tdma_schedule_profile_t *profile,
    const vdc_tdma_timestamp_evidence_t *evidence,
    bool require_dpll_eligible,
    uint64_t admission_window_start_ns,
    uint32_t observation_window_width_ns,
    uint32_t guard_before_ns,
    uint32_t guard_after_ns,
    bool validate_static_schedule,
    vdc_gate_result_t *gate)
{
    if (gate != NULL) {
        memset(gate, 0, sizeof(*gate));
    }
    if (profile == NULL || evidence == NULL) {
        vdc_domain_gate_fail(gate, VDC_DOMAIN_GATE_BAD_ARGUMENT, 0u, 0u);
        return false;
    }
    if (profile->enabled == 0u) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_DISABLED,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (!(validate_static_schedule
              ? vdc_domain_schedule_validate(profile)
              : vdc_domain_active_schedule_validate(profile))) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_SCHEDULE,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (evidence->schedule_crc32 != profile->schedule_crc32) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_SCHEDULE_CRC_MISMATCH,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (evidence->schedule_epoch != profile->schedule_epoch) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_EPOCH_MISMATCH,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (evidence->reference_slot_id != profile->reference_slot_id) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_REFERENCE_MISMATCH,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (evidence->source_slot_id >= VDC_DOMAIN_NODE_COUNT) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_SOURCE_OUT_OF_RANGE,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (require_dpll_eligible &&
        evidence->payload_class != VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE &&
        evidence->payload_class != VDC_DOMAIN_PAYLOAD_IDLE_BEACON) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_PAYLOAD_NOT_DPLL_SAMPLE,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }
    if (require_dpll_eligible) {
        vdc_timestamp_admission_code_t timestamp_code =
            VDC_TIMESTAMP_ADMISSION_PASS;
        if (!vdc_timestamp_dpll_admission_check(
                evidence->timestamp_source,
                evidence->timestamp_resolution_ns,
                evidence->timestamp_flags,
                VDC_DOMAIN_DPLL_ADMISSION_TIMESTAMP_RESOLUTION_LIMIT_NS,
                &timestamp_code)) {
            vdc_domain_gate_fail(
                gate,
                vdc_domain_timestamp_admission_to_gate(timestamp_code),
                evidence->source_slot_id,
                evidence->sample_seq);
            return false;
        }
    }

    const uint64_t cycle_offset =
        evidence->expected_window_start_ns % (uint64_t)profile->period_ns;
    if (cycle_offset != profile->observation_window_offset_ns ||
        !vdc_timestamp_observed_in_window(
            admission_window_start_ns,
            evidence->observed_time_ns,
            observation_window_width_ns,
            guard_before_ns,
            guard_after_ns)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_WINDOW_BOUND,
                             evidence->source_slot_id,
                             evidence->sample_seq);
        return false;
    }

    if (gate != NULL) {
        gate->passed = 1u;
        gate->reject_code = VDC_DOMAIN_GATE_PASS;
        gate->reject_slot = evidence->source_slot_id;
        gate->reject_evidence = evidence->sample_seq;
        gate->last_pass_seq = evidence->sample_seq;
    }
    return true;
}

bool vdc_domain_validate_tdma_timestamp_evidence(
    const vdc_tdma_schedule_profile_t *profile,
    const vdc_tdma_timestamp_evidence_t *evidence,
    bool require_dpll_eligible,
    vdc_gate_result_t *gate)
{
    if (profile == NULL) {
        if (gate != NULL) {
            memset(gate, 0, sizeof(*gate));
        }
        vdc_domain_gate_fail(gate, VDC_DOMAIN_GATE_BAD_ARGUMENT, 0u, 0u);
        return false;
    }

    return vdc_domain_validate_tdma_timestamp_evidence_window(
        profile,
        evidence,
        require_dpll_eligible,
        evidence != NULL ? evidence->expected_window_start_ns : 0u,
        profile->observation_window_width_ns,
        profile->guard_before_ns,
        profile->guard_after_ns,
        true,
        gate);
}

bool vdc_domain_validate_tdma_frame_envelope(
    const vdc_tdma_schedule_profile_t *profile,
    const vdc_tdma_frame_envelope_t *frame,
    bool require_dpll_eligible,
    vdc_gate_result_t *gate)
{
    uint32_t expected_offset_ns = 0u;
    uint32_t expected_width_ns = 0u;

    if (gate != NULL) {
        memset(gate, 0, sizeof(*gate));
    }
    if (profile == NULL || frame == NULL) {
        vdc_domain_gate_fail(gate, VDC_DOMAIN_GATE_BAD_ARGUMENT, 0u, 0u);
        return false;
    }
    if (!vdc_domain_schedule_validate(profile)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_SCHEDULE,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (frame->frame_version != VDC_DOMAIN_TDMA_FRAME_VERSION ||
        frame->frame_crc32 == 0u ||
        frame->payload_crc32 == 0u) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_FRAME,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (frame->schedule_crc32 != profile->schedule_crc32) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_SCHEDULE_CRC_MISMATCH,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (frame->schedule_epoch != profile->schedule_epoch) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_EPOCH_MISMATCH,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (frame->reference_slot_id != profile->reference_slot_id) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_REFERENCE_MISMATCH,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (frame->source_slot_id >= VDC_DOMAIN_NODE_COUNT ||
        frame->slot_index >= VDC_DOMAIN_NODE_COUNT) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_SOURCE_OUT_OF_RANGE,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (!vdc_domain_window_contract(profile,
                                    frame->window_class,
                                    &expected_offset_ns,
                                    &expected_width_ns)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_WINDOW_CLASS,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (!vdc_domain_payload_allowed_for_window(frame->window_class,
                                               frame->payload_class)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_PAYLOAD_WINDOW_FORBIDDEN,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }
    if (vdc_domain_payload_requires_reference_sync(frame->payload_class)) {
        const uint64_t expected_next_frame_start_ns =
            UINT64_MAX - frame->window_start_ns < (uint64_t)profile->period_ns
                ? UINT64_MAX
                : frame->window_start_ns + (uint64_t)profile->period_ns;
        if (frame->reference_sync_valid == 0u ||
            frame->reference_seq_id != frame->frame_seq ||
            frame->reference_frame_id != frame->frame_seq ||
            frame->reference_sync_slot_id != frame->reference_slot_id ||
            frame->reference_time_ns != frame->window_start_ns ||
            frame->next_frame_start_ns != expected_next_frame_start_ns ||
            frame->reference_schedule_crc32 != profile->schedule_crc32) {
            vdc_domain_gate_fail(gate,
                                 VDC_DOMAIN_GATE_BAD_FRAME,
                                 frame->source_slot_id,
                                 frame->frame_seq);
            return false;
        }
    } else if (frame->reference_sync_valid != 0u) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_FRAME,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }

    const uint64_t cycle_offset =
        frame->window_start_ns % (uint64_t)profile->period_ns;
    if (cycle_offset != expected_offset_ns) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_WINDOW_BOUND,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }

    if (!vdc_timestamp_observed_in_window(frame->window_start_ns,
                                          frame->timestamp.observed_time_ns,
                                          expected_width_ns,
                                          profile->guard_before_ns,
                                          profile->guard_after_ns) ||
        frame->timestamp.timestamp_source == VDC_DOMAIN_TIMESTAMP_SOURCE_NONE ||
        frame->timestamp.timestamp_resolution_ns == 0u ||
        frame->timestamp.schedule_crc32 != profile->schedule_crc32 ||
        frame->timestamp.schedule_epoch != profile->schedule_epoch ||
        frame->timestamp.source_slot_id != frame->source_slot_id ||
        frame->timestamp.reference_slot_id != frame->reference_slot_id ||
        frame->timestamp.payload_class != frame->payload_class ||
        frame->timestamp.frame_crc32 != frame->frame_crc32 ||
        frame->timestamp.sample_seq != frame->frame_seq ||
        frame->timestamp.sample_crc32 == 0u) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_FRAME,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }

    if (frame->window_class == VDC_DOMAIN_WINDOW_VDC_OBSERVATION) {
        if (frame->timestamp.expected_window_start_ns != frame->window_start_ns) {
            vdc_domain_gate_fail(gate,
                                 VDC_DOMAIN_GATE_WINDOW_BOUND,
                                 frame->source_slot_id,
                                 frame->frame_seq);
            return false;
        }
        return vdc_domain_validate_tdma_timestamp_evidence(profile,
                                                           &frame->timestamp,
                                                           require_dpll_eligible,
                                                           gate);
    }

    if (require_dpll_eligible) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_PAYLOAD_NOT_DPLL_SAMPLE,
                             frame->source_slot_id,
                             frame->frame_seq);
        return false;
    }

    if (gate != NULL) {
        gate->passed = 1u;
        gate->reject_code = VDC_DOMAIN_GATE_PASS;
        gate->reject_slot = frame->source_slot_id;
        gate->reject_evidence = frame->frame_seq;
        gate->last_pass_seq = frame->frame_seq;
    }
    return true;
}

bool vdc_domain_observation_path_delay_lookup(
    const vdc_path_delay_table_t *table,
    uint32_t source_slot_id,
    uint32_t reference_slot_id,
    vdc_path_delay_entry_t *entry)
{
    if ((!vdc_domain_path_delay_table_validate(table) &&
         !vdc_domain_path_delay_table_validate_provisional(table)) ||
        (table->flags & VDC_PATH_DELAY_FLAG_OBSERVATION_MATRIX_VALID) == 0u ||
        table->observation_matrix.valid == 0u ||
        source_slot_id >= table->observation_matrix.node_count ||
        reference_slot_id >= table->observation_matrix.node_count) {
        return false;
    }

    const uint32_t index = source_slot_id * VDC_DOMAIN_NODE_COUNT +
                           reference_slot_id;
    if ((table->observation_matrix.valid_bitmap[index / 32u] &
         (1u << (index % 32u))) == 0u) {
        return false;
    }
    if (entry != NULL) {
        memset(entry, 0, sizeof(*entry));
        entry->valid = 1u;
        entry->source_slot_id = source_slot_id;
        entry->reference_slot_id = reference_slot_id;
        entry->delay_ns = table->observation_matrix.delay_ns[index];
        entry->cal_crc32 = table->table_crc32;
        entry->freshness_us = table->freshness_us;
        entry->writer = table->bias_generation;
        entry->update_seq = table->update_seq;
    }
    return true;
}

bool VDC_DOMAIN_TIME_CRITICAL(
    vdc_domain_active_observation_path_delay_lookup)(
    const vdc_path_delay_table_t *table,
    uint32_t source_slot_id,
    uint32_t reference_slot_id,
    vdc_path_delay_entry_t *entry)
{
    if (table == NULL || table->valid == 0u ||
        table->version != VDC_DOMAIN_PATH_DELAY_TABLE_VERSION ||
        table->schedule_crc32 == 0u || table->table_crc32 == 0u ||
        (table->flags & VDC_PATH_DELAY_FLAG_OBSERVATION_MATRIX_VALID) == 0u ||
        table->observation_matrix.valid == 0u ||
        table->observation_matrix.node_count < 2u ||
        table->observation_matrix.node_count > VDC_DOMAIN_NODE_COUNT ||
        source_slot_id >= table->observation_matrix.node_count ||
        reference_slot_id >= table->observation_matrix.node_count) {
        return false;
    }

    const uint32_t index = source_slot_id * VDC_DOMAIN_NODE_COUNT +
                           reference_slot_id;
    if ((table->observation_matrix.valid_bitmap[index / 32u] &
         (1u << (index % 32u))) == 0u) {
        return false;
    }
    if (entry != NULL) {
        memset(entry, 0, sizeof(*entry));
        entry->valid = 1u;
        entry->source_slot_id = source_slot_id;
        entry->reference_slot_id = reference_slot_id;
        entry->delay_ns = table->observation_matrix.delay_ns[index];
        entry->cal_crc32 = table->table_crc32;
        entry->freshness_us = table->freshness_us;
        entry->writer = table->bias_generation;
        entry->update_seq = table->update_seq;
    }
    return true;
}

static bool vdc_domain_expand_compact_observation_window(
    const vdc_tdma_schedule_profile_t *profile,
    const vdc_timestamp_dictionary_t *dictionary,
    const vdc_path_delay_table_t *path_delay,
    vdc_wrap_tracker_t *wrap_tracker,
    const vdc_compact_observation_sample_t *compact,
    vdc_tdma_timestamp_evidence_t *evidence,
    uint64_t admission_window_start_ns,
    uint32_t observation_window_width_ns,
    uint32_t guard_before_ns,
    uint32_t guard_after_ns,
    vdc_gate_result_t *gate)
{
    vdc_timestamp_dictionary_entry_t entry;
    vdc_path_delay_entry_t path_entry;
    uint64_t observed_tick64 = 0u;
    uint32_t active_path_delay_ns = 0u;

    if (gate != NULL) {
        memset(gate, 0, sizeof(*gate));
    }
    if (evidence != NULL) {
        memset(evidence, 0, sizeof(*evidence));
    }
    if (profile == NULL || dictionary == NULL || wrap_tracker == NULL ||
        compact == NULL || evidence == NULL || compact->valid == 0u) {
        vdc_domain_gate_fail(gate, VDC_DOMAIN_GATE_BAD_ARGUMENT, 0u, 0u);
        return false;
    }
    if (!vdc_domain_schedule_validate(profile)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_SCHEDULE,
                             profile->local_slot_id,
                             compact->sample_seq);
        return false;
    }
    if (!vdc_timestamp_dictionary_find(dictionary, compact->event_id, &entry)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_FRAME,
                             profile->local_slot_id,
                             compact->sample_seq);
        return false;
    }
    if (compact->frame_crc32 == 0u || compact->sample_crc32 == 0u ||
        compact->timestamp_source == VDC_DOMAIN_TIMESTAMP_SOURCE_NONE ||
        compact->timestamp_resolution_ns == 0u ||
        entry.source_slot_id >= VDC_DOMAIN_NODE_COUNT ||
        entry.reference_slot_id != profile->reference_slot_id ||
        compact->timestamp_source != entry.source ||
        (entry.payload_class != VDC_DOMAIN_PAYLOAD_SYNC_SAMPLE &&
         entry.payload_class != VDC_DOMAIN_PAYLOAD_IDLE_BEACON)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_FRAME,
                             entry.source_slot_id,
                             compact->sample_seq);
        return false;
    }
    if (!vdc_wrap_tracker_extend_tick(wrap_tracker,
                                      compact->tick_l32,
                                      compact->max_backward_ticks,
                                      &observed_tick64)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_FRAME,
                             entry.source_slot_id,
                             compact->sample_seq);
        return false;
    }
    if (path_delay != NULL &&
        !vdc_domain_path_delay_lookup(path_delay,
                                      entry.source_slot_id,
                                      entry.reference_slot_id,
                                      &path_entry) &&
        !vdc_domain_observation_path_delay_lookup(
            path_delay, entry.source_slot_id, entry.reference_slot_id,
            &path_entry)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_FRAME,
                             entry.source_slot_id,
                             compact->sample_seq);
        return false;
    }
    if (path_delay != NULL) {
        active_path_delay_ns = path_entry.delay_ns;
    }

    evidence->sample_seq = compact->sample_seq;
    evidence->schedule_epoch = profile->schedule_epoch;
    evidence->slot_index = entry.source_slot_id;
    evidence->source_slot_id = entry.source_slot_id;
    evidence->reference_slot_id = entry.reference_slot_id;
    evidence->payload_class = entry.payload_class;
    evidence->expected_window_start_ns = compact->expected_window_start_ns;
    evidence->arm_time_ns = observed_tick64;
    evidence->start_time_ns = observed_tick64;
    evidence->observed_time_ns = observed_tick64;
    evidence->done_time_ns = observed_tick64;
    evidence->apply_time_ns = observed_tick64;
    evidence->late_ns =
        observed_tick64 > compact->expected_window_start_ns
            ? vdc_domain_saturate_u64_to_u32(
                  observed_tick64 - compact->expected_window_start_ns)
            : 0u;
    evidence->jitter_ns = compact->jitter_ns;
    evidence->delay_ns =
        path_delay != NULL ? active_path_delay_ns : compact->delay_ns;
    evidence->phase_error_ns =
        vdc_domain_phase_error_ns(profile,
                                  compact->expected_window_start_ns,
                                  active_path_delay_ns,
                                  observed_tick64);
    evidence->timestamp_source = compact->timestamp_source;
    evidence->timestamp_resolution_ns = compact->timestamp_resolution_ns;
    evidence->timestamp_flags = compact->timestamp_flags;
    evidence->schedule_crc32 = profile->schedule_crc32;
    evidence->frame_crc32 = compact->frame_crc32;
    evidence->sample_crc32 = compact->sample_crc32;
    evidence->quality_flags = compact->quality_flags;
    if (path_delay != NULL) {
        evidence->delay_generation = path_delay->calibration_generation;
        evidence->bias_generation = path_delay->bias_generation;
    }

    return vdc_domain_validate_tdma_timestamp_evidence_window(
        profile,
        evidence,
        true,
        admission_window_start_ns,
        observation_window_width_ns,
        guard_before_ns,
        guard_after_ns,
        true,
        gate);
}

bool vdc_domain_expand_compact_observation(
    const vdc_tdma_schedule_profile_t *profile,
    const vdc_timestamp_dictionary_t *dictionary,
    vdc_wrap_tracker_t *wrap_tracker,
    const vdc_compact_observation_sample_t *compact,
    vdc_tdma_timestamp_evidence_t *evidence,
    vdc_gate_result_t *gate)
{
    if (profile == NULL) {
        if (gate != NULL) {
            memset(gate, 0, sizeof(*gate));
        }
        if (evidence != NULL) {
            memset(evidence, 0, sizeof(*evidence));
        }
        vdc_domain_gate_fail(gate, VDC_DOMAIN_GATE_BAD_ARGUMENT, 0u, 0u);
        return false;
    }

    return vdc_domain_expand_compact_observation_window(
        profile,
        dictionary,
        NULL,
        wrap_tracker,
        compact,
        evidence,
        compact != NULL ? compact->expected_window_start_ns : 0u,
        profile->observation_window_width_ns,
        profile->guard_before_ns,
        profile->guard_after_ns,
        gate);
}

bool vdc_domain_plan_tdma_window(const vdc_tdma_schedule_profile_t *profile,
                                 uint32_t window_class,
                                 uint64_t now_ns,
                                 vdc_tdma_window_plan_t *plan,
                                 vdc_gate_result_t *gate)
{
    uint32_t offset_ns = 0u;
    uint32_t width_ns = 0u;

    if (gate != NULL) {
        memset(gate, 0, sizeof(*gate));
    }
    if (plan != NULL) {
        memset(plan, 0, sizeof(*plan));
    }
    if (profile == NULL || plan == NULL) {
        vdc_domain_gate_fail(gate, VDC_DOMAIN_GATE_BAD_ARGUMENT, 0u, 0u);
        return false;
    }
    if (!vdc_domain_schedule_validate(profile)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_SCHEDULE,
                             profile->local_slot_id,
                             profile->schedule_epoch);
        return false;
    }
    if (!vdc_domain_window_contract(profile,
                                    window_class,
                                    &offset_ns,
                                    &width_ns)) {
        vdc_domain_gate_fail(gate,
                             VDC_DOMAIN_GATE_BAD_WINDOW_CLASS,
                             profile->local_slot_id,
                             profile->schedule_epoch);
        return false;
    }

    const uint64_t period_ns = profile->period_ns;
    uint64_t cycle_start_ns = now_ns - (now_ns % period_ns);
    uint64_t window_start_ns = cycle_start_ns + offset_ns;
    uint64_t window_end_ns = window_start_ns + width_ns;
    uint64_t guard_start_ns =
        window_start_ns > profile->guard_before_ns
            ? window_start_ns - profile->guard_before_ns
            : 0u;
    uint64_t guard_end_ns = window_end_ns + profile->guard_after_ns;

    if (now_ns > guard_end_ns) {
        cycle_start_ns += period_ns;
        window_start_ns = cycle_start_ns + offset_ns;
        window_end_ns = window_start_ns + width_ns;
        guard_start_ns = window_start_ns > profile->guard_before_ns
                             ? window_start_ns - profile->guard_before_ns
                             : 0u;
        guard_end_ns = window_end_ns + profile->guard_after_ns;
        plan->missed_current_window = 1u;
    }

    plan->valid = 1u;
    plan->window_class = window_class;
    plan->schedule_epoch = profile->schedule_epoch;
    plan->slot_index = profile->local_slot_id;
    plan->source_slot_id = profile->local_slot_id;
    plan->reference_slot_id = profile->reference_slot_id;
    plan->now_ns = now_ns;
    plan->window_start_ns = window_start_ns;
    plan->window_end_ns = window_end_ns;
    plan->guard_start_ns = guard_start_ns;
    plan->guard_end_ns = guard_end_ns;
    plan->schedule_crc32 = profile->schedule_crc32;

    if (now_ns < window_start_ns) {
        plan->wait_ns =
            vdc_domain_saturate_u64_to_u32(window_start_ns - now_ns);
    } else {
        plan->late_ns =
            vdc_domain_saturate_u64_to_u32(now_ns - window_start_ns);
    }
    if (now_ns >= guard_start_ns && now_ns <= guard_end_ns) {
        plan->in_guarded_window = 1u;
    }
    if (now_ns >= window_start_ns && now_ns <= window_end_ns) {
        plan->inside_payload_window = 1u;
    }

    if (gate != NULL) {
        gate->passed = 1u;
        gate->reject_code = VDC_DOMAIN_GATE_PASS;
        gate->reject_slot = profile->local_slot_id;
        gate->reject_evidence = profile->schedule_epoch;
        gate->last_pass_seq = profile->schedule_epoch;
    }
    return true;
}

bool vdc_domain_plan_tdma_ring(const vdc_tdma_schedule_profile_t *profile,
                               vdc_tdma_ring_plan_t *plan)
{
    if (plan != NULL) {
        memset(plan, 0, sizeof(*plan));
    }
    if (profile == NULL || plan == NULL ||
        !vdc_domain_schedule_validate(profile)) {
        return false;
    }

    plan->valid = 1u;
    plan->ring_node_count = profile->ring_binding.node_count;
    plan->local_slot_id = profile->local_slot_id;
    plan->reference_slot_id = profile->reference_slot_id;
    plan->upstream_slot_id = profile->ring_binding.upstream_slot_id;
    plan->downstream_slot_id = profile->ring_binding.downstream_slot_id;
    plan->feedback_slot_id = profile->ring_binding.feedback_slot_id;
    plan->from_reference_hops =
        vdc_domain_ring_forward_distance(profile->reference_slot_id,
                                         profile->local_slot_id,
                                         profile->ring_binding.node_count);
    plan->to_feedback_hops =
        vdc_domain_ring_forward_distance(profile->local_slot_id,
                                         profile->ring_binding.feedback_slot_id,
                                         profile->ring_binding.node_count);
    plan->is_reference_slot =
        profile->local_slot_id == profile->reference_slot_id ? 1u : 0u;
    plan->ring_flags = profile->ring_binding.flags;
    plan->ring_profile_crc32 = profile->ring_binding.profile_crc32;
    plan->schedule_crc32 = profile->schedule_crc32;
    plan->cycle_period_ns = profile->period_ns;
    return true;
}

bool vdc_domain_init(vdc_domain_context_t *context)
{
    if (context == NULL) {
        return false;
    }

    memset(context, 0, sizeof(*context));
    vdc_domain_default_schedule(&context->schedule, 0u, 0u);
    vdc_domain_default_servo(&context->servo);
    vdc_domain_default_dpll_control_profile(&context->control.profile);
    vdc_domain_default_oscillator_discipline_profile(
        &context->oscillator_discipline.profile);
    context->oscillator_discipline.freeze_reason =
        VDC_OSCILLATOR_DISCIPLINE_FREEZE_DISABLED;
    vdc_domain_default_clock_model(&context->clock,
                                   context->schedule.schedule_epoch,
                                   0u,
                                   0u,
                                   0u,
                                   context->schedule.schedule_crc32);
    context->clock.slew_limit_ppb = context->servo.sanity_freq_limit_ppb;
    context->dpll.state = VDC_DOMAIN_LOCK_OFF;
    context->dpll.schedule_crc32 = context->schedule.schedule_crc32;
    context->dpll.servo_profile_crc32 = context->servo.servo_profile_crc32;
    vdc_domain_init_quality(context);
    vdc_domain_default_dco_control(&context->dco,
                                   &context->clock,
                                   context->dpll.state);
    vdc_domain_default_timestamp_dictionary(&context->timestamp_dictionary,
                                            &context->schedule);
    vdc_domain_default_path_delay_table(&context->path_delay,
                                        &context->schedule);
    vdc_wrap_tracker_init_open(&context->wrap_tracker);
    return true;
}

static bool vdc_domain_activate_tdma_configuration_checked(
    vdc_domain_context_t *context,
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_timestamp_dictionary_t *dictionary,
    const vdc_path_delay_table_t *path_delay,
    bool provisional)
{
    if (context == NULL || schedule == NULL || dictionary == NULL ||
        path_delay == NULL || !vdc_domain_schedule_validate(schedule) ||
        !vdc_timestamp_dictionary_validate(dictionary) ||
        !(provisional
              ? vdc_domain_path_delay_table_validate_provisional(path_delay)
              : vdc_domain_path_delay_table_validate(path_delay)) ||
        dictionary->profile_crc32 != schedule->schedule_crc32 ||
        path_delay->schedule_crc32 != schedule->schedule_crc32 ||
        path_delay->observation_matrix.node_count !=
            schedule->ring_binding.node_count) {
        return false;
    }
    if (vdc_domain_is_follower(context) &&
        (context->control.profile.follow_master_slot_id >=
             schedule->ring_binding.node_count ||
         context->control.profile.follow_master_slot_id ==
             schedule->local_slot_id)) {
        return false;
    }

    const uint32_t next_run_id = context->clock.run_id + 1u;
    context->reference_baseline_ppb = 0;
    const uint32_t ready = context->ready;
    const uint32_t debug_continue_enabled =
        context->dpll.debug_continue_enabled;
    const uint32_t debug_continue_generation =
        context->dpll.debug_continue_generation;
    context->schedule = *schedule;
    context->timestamp_dictionary = *dictionary;
    context->path_delay = *path_delay;
    memset(&context->dpll, 0, sizeof(context->dpll));
    memset(&context->gate, 0, sizeof(context->gate));
    context->phase_observed_anchor_ns = 0u;
    context->phase_observed_anchor_valid = 0u;
    vdc_domain_default_clock_model(&context->clock,
                                   schedule->schedule_epoch,
                                   next_run_id,
                                   0u,
                                   0u,
                                   schedule->schedule_crc32);
    context->clock.nominal_period_ns = schedule->period_ns;
    context->clock.slew_limit_ppb = context->servo.sanity_freq_limit_ppb;
    /* Activation retains the active servo, including persisted/debug tuning. */
    context->clock.servo_profile_crc32 = context->servo.servo_profile_crc32;
    context->dpll.state = ready != 0u ? VDC_DOMAIN_LOCK_CHECKING
                                     : VDC_DOMAIN_LOCK_OFF;
    context->dpll.schedule_crc32 = schedule->schedule_crc32;
    context->dpll.servo_profile_crc32 = context->servo.servo_profile_crc32;
    context->dpll.debug_continue_enabled = debug_continue_enabled;
    context->dpll.debug_continue_generation = debug_continue_generation;
    context->control.last_follower_source_slot_id = 0u;
    context->control.last_follower_control_generation = 0u;
    context->control.last_follower_command_seq = 0u;
    context->control.last_follower_quality = 0u;
    context->control.last_follower_effective_vdc_time_ns = 0u;
    vdc_domain_init_quality(context);
    vdc_domain_default_dco_control(&context->dco,
                                   &context->clock,
                                   context->dpll.state);
    vdc_wrap_tracker_init_open(&context->wrap_tracker);
    vdc_domain_refresh_quality_state(context);
    return true;
}

bool vdc_domain_activate_tdma_configuration(
    vdc_domain_context_t *context,
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_timestamp_dictionary_t *dictionary,
    const vdc_path_delay_table_t *path_delay)
{
    return vdc_domain_activate_tdma_configuration_checked(
        context, schedule, dictionary, path_delay, false);
}

bool vdc_domain_activate_tdma_provisional_configuration(
    vdc_domain_context_t *context,
    const vdc_tdma_schedule_profile_t *schedule,
    const vdc_timestamp_dictionary_t *dictionary,
    const vdc_path_delay_table_t *path_delay)
{
    return vdc_domain_activate_tdma_configuration_checked(
        context, schedule, dictionary, path_delay, true);
}

void vdc_domain_set_ready(vdc_domain_context_t *context, bool ready)
{
    if (context == NULL) {
        return;
    }
    context->ready = ready ? 1u : 0u;
    if (!ready) {
        context->dpll.state = VDC_DOMAIN_LOCK_OFF;
        context->phase_observed_anchor_ns = 0u;
        context->phase_observed_anchor_valid = 0u;
        context->quality.last_sample_time_ns = 0u;
        context->quality.last_sample_age_us = 0u;
    } else if (context->dpll.state == VDC_DOMAIN_LOCK_OFF) {
        context->dpll.state = VDC_DOMAIN_LOCK_CHECKING;
    }
    vdc_domain_sync_dco_lock_state(context);
    vdc_domain_refresh_quality_state(context);
}

void vdc_domain_service(vdc_domain_context_t *context, uint64_t now_ns)
{
    if (context == NULL) {
        return;
    }

    if (context->service_count == 0u) {
        context->first_service_time_ns = now_ns;
    }
    context->service_count++;
    context->last_service_time_ns = now_ns;

    if (context->ready == 0u) {
        context->dpll.state = VDC_DOMAIN_LOCK_OFF;
        vdc_domain_sync_dco_lock_state(context);
        vdc_domain_refresh_quality_state(context);
        return;
    }
    if (context->dpll.state == VDC_DOMAIN_LOCK_OFF) {
        context->dpll.state = VDC_DOMAIN_LOCK_CHECKING;
        vdc_domain_sync_dco_lock_state(context);
    }
    vdc_domain_refresh_quality_age(context, now_ns);
}

bool vdc_domain_publish_clock_model(vdc_domain_context_t *context,
                                    const vdc_clock_model_t *model)
{
    const uint32_t next_dco_seq =
        context != NULL ? context->dco.dco_update_seq + 1u : 0u;

    if (context == NULL || model == NULL || model->valid == 0u ||
        model->tdma_schedule_crc32 != context->schedule.schedule_crc32 ||
        model->servo_profile_crc32 != context->servo.servo_profile_crc32) {
        return false;
    }

    context->clock = *model;
    context->clock.model_seq++;
    context->clock.slew_limit_ppb = context->servo.sanity_freq_limit_ppb;
    vdc_domain_default_dco_control(&context->dco,
                                   &context->clock,
                                   context->dpll.state);
    context->dco.dco_update_seq = next_dco_seq;
    context->dpll.update_seq++;
    return true;
}

bool vdc_domain_publish_dco_control(vdc_domain_context_t *context,
                                    const vdc_dco_control_t *dco)
{
    const uint32_t next_dco_seq =
        context != NULL ? context->dco.dco_update_seq + 1u : 0u;

    if (context == NULL || dco == NULL ||
        !vdc_domain_dco_control_validate(&context->schedule,
                                         &context->servo,
                                         dco)) {
        return false;
    }

    context->dco = *dco;
    context->dco.dco_update_seq = next_dco_seq;
    context->dpll.update_seq++;
    return true;
}

bool vdc_domain_publish_timestamp_dictionary(
    vdc_domain_context_t *context,
    const vdc_timestamp_dictionary_t *dictionary,
    uint32_t initial_tick_l32)
{
    if (context == NULL || dictionary == NULL ||
        !vdc_timestamp_dictionary_validate(dictionary) ||
        dictionary->profile_crc32 != context->schedule.schedule_crc32) {
        return false;
    }

    context->timestamp_dictionary = *dictionary;
    vdc_wrap_tracker_reanchor(&context->wrap_tracker, initial_tick_l32);
    return true;
}

bool vdc_domain_publish_path_delay_table(
    vdc_domain_context_t *context,
    const vdc_path_delay_table_t *table)
{
    if (context == NULL || table == NULL ||
        !vdc_domain_path_delay_table_validate(table) ||
        table->schedule_crc32 != context->schedule.schedule_crc32) {
        return false;
    }

    context->path_delay = *table;
    return true;
}

static bool vdc_domain_prepare_tdma_evidence_checked(
    const vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    bool validate_static_schedule,
    vdc_tdma_evidence_preparation_t *preparation)
{
    if (context == NULL || evidence == NULL || preparation == NULL) {
        return false;
    }
    memset(preparation, 0, sizeof(*preparation));

    vdc_gate_result_t gate;
    const bool acquisition_window =
        vdc_domain_context_uses_acquisition_window(context);
    const uint32_t tracking_half_width_ns =
        context->schedule.period_ns != 0u
            ? context->schedule.period_ns
            : context->schedule.observation_window_width_ns;
    const uint32_t admission_window_width_ns =
        acquisition_window
            ? vdc_domain_acquisition_window_width_ns(&context->schedule)
            : vdc_domain_tracking_window_width_ns(tracking_half_width_ns);
    const uint32_t admission_guard_before_ns =
        acquisition_window ? VDC_DOMAIN_ACQUISITION_GUARD_NS : 0u;
    const uint32_t admission_guard_after_ns =
        acquisition_window ? VDC_DOMAIN_ACQUISITION_GUARD_NS : 0u;
    const uint64_t admission_window_start_ns =
        acquisition_window
            ? evidence->expected_window_start_ns
            : vdc_domain_tracking_window_start_ns(
                  context, evidence->expected_window_start_ns,
                  tracking_half_width_ns);

    if (!vdc_domain_validate_tdma_timestamp_evidence_window(
            &context->schedule,
            evidence,
            true,
            admission_window_start_ns,
            admission_window_width_ns,
            admission_guard_before_ns,
            admission_guard_after_ns,
            validate_static_schedule,
            &gate)) {
        preparation->valid = 1u;
        preparation->schedule_crc32 = context->schedule.schedule_crc32;
        preparation->dpll_update_seq = context->dpll.update_seq;
        preparation->gate = gate;
        preparation->follower_bypassed =
            vdc_domain_is_follower(context) ? 1u : 0u;
        preparation->continued =
            context->dpll.debug_continue_enabled != 0u &&
            vdc_domain_debug_gate_recoverable(gate.reject_code) ? 1u : 0u;
        return true;
    }

    if (!vdc_domain_validate_evidence_generations(context, evidence, &gate)) {
        preparation->valid = 1u;
        preparation->schedule_crc32 = context->schedule.schedule_crc32;
        preparation->dpll_update_seq = context->dpll.update_seq;
        preparation->gate = gate;
        preparation->follower_bypassed =
            vdc_domain_is_follower(context) ? 1u : 0u;
        /* Generation mismatches are provenance failures, never recoverable
         * debug continuation.  The caller records raw evidence and keeps it
         * out of corrected jitter/formal lock. */
        preparation->continued = 0u;
        return true;
    }

    if (!vdc_domain_validate_evidence_phase_domain(context, evidence, &gate)) {
        preparation->valid = 1u;
        preparation->schedule_crc32 = context->schedule.schedule_crc32;
        preparation->dpll_update_seq = context->dpll.update_seq;
        preparation->gate = gate;
        preparation->follower_bypassed =
            vdc_domain_is_follower(context) ? 1u : 0u;
        preparation->continued = 0u;
        return true;
    }

    const int32_t input_residual_ns =
        vdc_domain_corrected_phase_error_ns(context, evidence);
    preparation->valid = 1u;
    preparation->schedule_crc32 = context->schedule.schedule_crc32;
    preparation->dpll_update_seq = context->dpll.update_seq;
    preparation->accepted = 1u;
    preparation->input_residual_ns = input_residual_ns;
    preparation->gate = gate;
    preparation->follower_bypassed =
        vdc_domain_is_follower(context) ? 1u : 0u;
    return true;
}

bool VDC_DOMAIN_TIME_CRITICAL(vdc_domain_apply_prepared_tdma_evidence_servo)(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation)
{
    if (context == NULL || evidence == NULL || preparation == NULL ||
        preparation->valid == 0u ||
        preparation->servo_applied != 0u ||
        preparation->applied != 0u ||
        preparation->schedule_crc32 != context->schedule.schedule_crc32 ||
        preparation->schedule_crc32 != evidence->schedule_crc32 ||
        preparation->dpll_update_seq != context->dpll.update_seq) {
        return false;
    }

    if (preparation->follower_bypassed == 0u &&
        preparation->accepted != 0u &&
        preparation->local_apply_time_ns != 0u &&
        context->dco.valid != 0u &&
        preparation->local_apply_time_ns < context->dco.base_local_tick64) {
        /* A backwards owner clock would mix a new rate with an old local
         * epoch. Reject the prepared sample and keep the previous model. */
        return false;
    }

    if (preparation->follower_bypassed != 0u) {
        vdc_domain_increment_saturating(
            &context->control.follower_local_evidence_bypass_count);
    } else if (preparation->accepted != 0u) {
        context->dpll.accepted_sample_count++;
        vdc_domain_update_clock_from_evidence(
            context, evidence, preparation->input_residual_ns,
            preparation->local_apply_time_ns);
    }
    preparation->servo_applied = 1u;
    preparation->post_servo_dpll_update_seq = context->dpll.update_seq;
    return true;
}

bool VDC_DOMAIN_TIME_CRITICAL(vdc_domain_apply_prepared_tdma_evidence_state)(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation,
    bool *accepted)
{
    if (accepted != NULL) {
        *accepted = false;
    }
    if (context == NULL || evidence == NULL || preparation == NULL ||
        preparation->valid == 0u ||
        preparation->servo_applied == 0u ||
        preparation->applied != 0u ||
        preparation->schedule_crc32 != context->schedule.schedule_crc32 ||
        preparation->schedule_crc32 != evidence->schedule_crc32 ||
        preparation->post_servo_dpll_update_seq !=
            context->dpll.update_seq) {
        return false;
    }

    const vdc_gate_result_t gate = preparation->gate;
    context->gate = gate;
    vdc_domain_record_observation_metadata(context, evidence);
    if (preparation->follower_bypassed != 0u) {
        /* FOLLOWER runs the same timestamp/window and path-delay
         * observation as MASTER.  The result is diagnostic state only: do
         * not touch the local PI, integrator, DCO command or lock state. */
        context->dpll.last_sample_seq = evidence->sample_seq;
        context->dpll.last_raw_phase_error_ns = evidence->phase_error_ns;
        context->dpll.last_phase_error_ns = evidence->phase_error_ns;
        context->dpll.last_offset_ns = evidence->phase_error_ns;
        context->dpll.last_expected_window_start_ns =
            evidence->expected_window_start_ns;
        context->dpll.last_observed_time_ns = evidence->observed_time_ns;
        context->dpll.schedule_crc32 = context->schedule.schedule_crc32;
        context->dpll.servo_profile_crc32 = context->servo.servo_profile_crc32;
        context->quality.valid = 1u;
        context->quality.lock_state = context->dpll.state;
        context->quality.update_seq =
            vdc_domain_increment_nonzero(context->quality.update_seq);
        context->quality.last_sample_seq = evidence->sample_seq;
        context->quality.last_timestamp_source = evidence->timestamp_source;
        context->quality.last_timestamp_resolution_ns =
            evidence->timestamp_resolution_ns;
        context->quality.last_timestamp_flags = evidence->timestamp_flags;
        context->quality.last_jitter_ns = evidence->jitter_ns;
        context->quality.last_offset_ns = evidence->phase_error_ns;
        context->quality.gate_reject_code = gate.reject_code;
        context->quality.gate_reject_slot = gate.reject_slot;
        context->quality.gate_reject_evidence = gate.reject_evidence;
        context->quality.jitter_rms_ns =
            vdc_domain_avg_u32(context->quality.jitter_rms_ns,
                               evidence->jitter_ns);
        if (preparation->accepted != 0u) {
            context->quality.last_sample_time_ns =
                preparation->local_apply_time_ns != 0u
                    ? preparation->local_apply_time_ns
                    : vdc_domain_evidence_time_ns(evidence);
            context->quality.last_sample_age_us = 0u;
            const int32_t residual_ns = preparation->input_residual_ns;
            const uint32_t abs_phase = vdc_domain_abs_i32(residual_ns);
            context->dpll.last_reject_code = VDC_DOMAIN_GATE_PASS;
            context->dpll.last_phase_error_ns = residual_ns;
            context->dpll.last_offset_ns = residual_ns;
            context->dpll.max_abs_offset_ns =
                abs_phase > context->dpll.max_abs_offset_ns
                    ? abs_phase
                    : context->dpll.max_abs_offset_ns;
            context->dpll.rms_offset_ns =
                context->dpll.rms_offset_ns == 0u
                    ? abs_phase
                    : vdc_domain_avg_u32(context->dpll.rms_offset_ns,
                                         abs_phase);
            context->dpll.jitter_pk_ns =
                evidence->jitter_ns > context->dpll.jitter_pk_ns
                    ? evidence->jitter_ns
                    : context->dpll.jitter_pk_ns;
            context->quality.last_offset_ns = residual_ns;
            context->quality.gate_reject_slot = evidence->source_slot_id;
            context->quality.gate_reject_evidence = evidence->sample_seq;
            context->quality.rms_offset_ns = context->dpll.rms_offset_ns;
            context->quality.max_abs_offset_ns =
                context->dpll.max_abs_offset_ns;
            context->quality.jitter_pk_ns = context->dpll.jitter_pk_ns;
            context->quality.quality_flags = 0u;
            if (abs_phase >
                vdc_domain_effective_lock_acceptance_threshold_ns(context)) {
                context->quality.quality_flags |=
                    VDC_DOMAIN_QUALITY_FLAG_PHASE_OUT_OF_LOCK;
            }
            const uint32_t diagnostic_threshold_ns =
                vdc_domain_effective_phase_diagnostic_threshold_ns(context);
            if (diagnostic_threshold_ns != 0u &&
                abs_phase > diagnostic_threshold_ns) {
                context->quality.quality_flags |=
                    VDC_DOMAIN_QUALITY_FLAG_PHASE_LARGE;
            }
            if (vdc_domain_abs_i32(context->clock.period_adjust_ppb) >=
                context->servo.sanity_freq_limit_ppb) {
                context->quality.quality_flags |=
                    VDC_DOMAIN_QUALITY_FLAG_RATE_LIMITED;
            }
            context->quality.last_reject_code = VDC_DOMAIN_GATE_PASS;
        } else {
            context->dpll.last_reject_code = gate.reject_code;
            context->quality.last_reject_code = gate.reject_code;
        }
        /* update_seq is an observation sequence for FOLLOWER. It is not a
         * lock or control sequence and therefore cannot authorize local PI. */
        context->dpll.update_seq =
            vdc_domain_increment_nonzero(context->dpll.update_seq);
        preparation->applied = 1u;
        preparation->post_apply_dpll_update_seq = context->dpll.update_seq;
        if (accepted != NULL) {
            *accepted = preparation->accepted != 0u;
        }
        return true;
    }
    if (preparation->accepted == 0u) {
        if (preparation->continued != 0u) {
            vdc_domain_record_debug_continue(context, &gate);
            preparation->applied = 1u;
            preparation->post_apply_dpll_update_seq = context->dpll.update_seq;
            return true;
        }
        const bool reacquire =
            vdc_domain_reject_requires_reacquire(gate.reject_code);
        context->dpll.rejected_sample_count++;
        context->dpll.last_reject_code = gate.reject_code;
        context->dpll.update_seq++;
        if (reacquire) {
            vdc_domain_reset_lock_acquisition(context);
        }
        context->dpll.state =
            vdc_domain_lock_state_after_reject(context, reacquire);
        vdc_domain_sync_dco_lock_state(context);
        preparation->applied = 1u;
        preparation->post_apply_dpll_update_seq = context->dpll.update_seq;
        return true;
    }

    vdc_tdma_timestamp_evidence_t input_evidence = *evidence;
    input_evidence.phase_error_ns = preparation->input_residual_ns;
    const uint32_t abs_phase =
        vdc_domain_abs_i32(input_evidence.phase_error_ns);

    context->dpll.last_sample_seq = evidence->sample_seq;
    context->dpll.last_reject_code = VDC_DOMAIN_GATE_PASS;
    context->dpll.last_phase_error_ns = input_evidence.phase_error_ns;
    context->dpll.last_offset_ns = input_evidence.phase_error_ns;
    context->dpll.jitter_pk_ns =
        evidence->jitter_ns > context->dpll.jitter_pk_ns
            ? evidence->jitter_ns
            : context->dpll.jitter_pk_ns;
    context->dpll.max_abs_offset_ns =
        abs_phase > context->dpll.max_abs_offset_ns
            ? abs_phase
            : context->dpll.max_abs_offset_ns;
    context->dpll.rms_offset_ns =
        (context->dpll.rms_offset_ns == 0u)
            ? abs_phase
            : (context->dpll.rms_offset_ns + abs_phase) / 2u;
    context->dpll.schedule_crc32 = context->schedule.schedule_crc32;
    context->dpll.servo_profile_crc32 = context->servo.servo_profile_crc32;
    context->dpll.update_seq++;

    const uint32_t lock_threshold_ns =
        vdc_domain_effective_lock_acceptance_threshold_ns(context);
    const uint32_t good_streak =
        abs_phase <= lock_threshold_ns
            ? (context->quality.consecutive_good_samples == UINT32_MAX
                   ? UINT32_MAX
                   : context->quality.consecutive_good_samples + 1u)
            : 0u;
    const bool phase_in_lock = abs_phase <= lock_threshold_ns;
    const bool was_relocking =
        context->dpll.state == VDC_DOMAIN_LOCK_RELOCKING;
    if (context->dpll.state == VDC_DOMAIN_LOCK_LOCKED && !phase_in_lock) {
        context->dpll.state = VDC_DOMAIN_LOCK_RELOCKING;
    } else if (context->dpll.accepted_sample_count <
               VDC_DOMAIN_INITIAL_LOCK_SAMPLES) {
        context->dpll.state = VDC_DOMAIN_LOCK_CHECKING;
    } else if (context->dpll.accepted_sample_count < VDC_DOMAIN_FREQ_LOCK_SAMPLES) {
        context->dpll.state = VDC_DOMAIN_LOCK_INITIAL_SYNC;
    } else if (context->dpll.accepted_sample_count < VDC_DOMAIN_PHASE_LOCK_SAMPLES) {
        context->dpll.state = VDC_DOMAIN_LOCK_FREQ_LOCK;
    } else if (!phase_in_lock ||
               good_streak < context->servo.lock_sample_count) {
        context->dpll.state = was_relocking
            ? VDC_DOMAIN_LOCK_RELOCKING
            : VDC_DOMAIN_LOCK_PHASE_LOCK;
    } else {
        context->dpll.state = VDC_DOMAIN_LOCK_LOCKED;
    }

    vdc_domain_sync_dco_lock_state(context);
    preparation->applied = 1u;
    preparation->post_apply_dpll_update_seq = context->dpll.update_seq;
    if (accepted != NULL) {
        *accepted = true;
    }
    return true;
}

bool vdc_domain_apply_prepared_tdma_evidence_core(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation,
    bool *accepted)
{
    return vdc_domain_apply_prepared_tdma_evidence_servo(
               context, evidence, preparation) &&
           vdc_domain_apply_prepared_tdma_evidence_state(
               context, evidence, preparation, accepted);
}

bool VDC_DOMAIN_TIME_CRITICAL(vdc_domain_finalize_prepared_tdma_evidence)(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    const vdc_tdma_evidence_preparation_t *preparation)
{
    if (context == NULL || evidence == NULL || preparation == NULL ||
        preparation->valid == 0u || preparation->applied == 0u ||
        preparation->schedule_crc32 != context->schedule.schedule_crc32 ||
        preparation->schedule_crc32 != evidence->schedule_crc32 ||
        preparation->post_apply_dpll_update_seq != context->dpll.update_seq) {
        return false;
    }
    if (preparation->follower_bypassed != 0u) {
        return true;
    }
    if (preparation->accepted == 0u) {
        if (preparation->continued != 0u) {
            return true;
        }
        vdc_domain_record_rejected_sample(
            context, evidence, &preparation->gate);
        return true;
    }

    vdc_tdma_timestamp_evidence_t input_evidence = *evidence;
    input_evidence.phase_error_ns = preparation->input_residual_ns;
    vdc_domain_record_accepted_sample(
        context, &input_evidence, preparation->local_apply_time_ns);
    return true;
}

bool vdc_domain_apply_prepared_tdma_evidence(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation,
    bool *accepted)
{
    return vdc_domain_apply_prepared_tdma_evidence_core(
               context, evidence, preparation, accepted) &&
           vdc_domain_finalize_prepared_tdma_evidence(
               context, evidence, preparation);
}

static bool vdc_domain_submit_tdma_evidence_checked(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    bool validate_static_schedule)
{
    vdc_tdma_evidence_preparation_t preparation;
    bool accepted = false;
    return vdc_domain_prepare_tdma_evidence_checked(
               context, evidence, validate_static_schedule, &preparation) &&
           vdc_domain_apply_prepared_tdma_evidence(
               context, evidence, &preparation, &accepted) &&
           (accepted || preparation.continued != 0u);
}

bool vdc_domain_submit_tdma_evidence(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence)
{
    return vdc_domain_submit_tdma_evidence_checked(context, evidence, true);
}

bool vdc_domain_submit_active_tdma_evidence(
    vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence)
{
    vdc_tdma_evidence_preparation_t preparation;
    bool accepted = false;
    return vdc_domain_prepare_active_tdma_evidence(
               context, evidence, &preparation) &&
           vdc_domain_apply_prepared_tdma_evidence(
               context, evidence, &preparation, &accepted) &&
           (accepted || preparation.continued != 0u);
}

bool vdc_domain_prepare_active_tdma_evidence(
    const vdc_domain_context_t *context,
    const vdc_tdma_timestamp_evidence_t *evidence,
    vdc_tdma_evidence_preparation_t *preparation)
{
    if (context == NULL || evidence == NULL ||
        preparation == NULL ||
        context->path_delay.valid == 0u ||
        context->path_delay.table_crc32 == 0u ||
        context->schedule.schedule_crc32 == 0u ||
        context->dpll.schedule_crc32 != context->schedule.schedule_crc32 ||
        context->clock.tdma_schedule_crc32 !=
            context->schedule.schedule_crc32 ||
        context->timestamp_dictionary.profile_crc32 !=
            context->schedule.schedule_crc32 ||
        context->path_delay.schedule_crc32 !=
            context->schedule.schedule_crc32) {
        return false;
    }
    return vdc_domain_prepare_tdma_evidence_checked(
        context, evidence, false, preparation);
}

bool vdc_domain_submit_compact_observation(
    vdc_domain_context_t *context,
    const vdc_compact_observation_sample_t *compact)
{
    vdc_tdma_timestamp_evidence_t evidence;
    vdc_gate_result_t gate;

    if (context == NULL || compact == NULL) {
        return false;
    }

    const bool acquisition_window =
        vdc_domain_context_uses_acquisition_window(context);
    const uint32_t tracking_half_width_ns =
        context->schedule.period_ns != 0u
            ? context->schedule.period_ns
            : context->schedule.observation_window_width_ns;
    const uint32_t admission_window_width_ns =
        acquisition_window
            ? vdc_domain_acquisition_window_width_ns(&context->schedule)
            : vdc_domain_tracking_window_width_ns(tracking_half_width_ns);
    const uint32_t admission_guard_before_ns =
        acquisition_window ? VDC_DOMAIN_ACQUISITION_GUARD_NS : 0u;
    const uint32_t admission_guard_after_ns =
        acquisition_window ? VDC_DOMAIN_ACQUISITION_GUARD_NS : 0u;
    const uint64_t admission_window_start_ns =
        acquisition_window
            ? compact->expected_window_start_ns
            : vdc_domain_tracking_window_start_ns(
                  context, compact->expected_window_start_ns,
                  tracking_half_width_ns);

    if (!vdc_domain_expand_compact_observation_window(
            &context->schedule,
            &context->timestamp_dictionary,
            &context->path_delay,
            &context->wrap_tracker,
            compact,
            &evidence,
            admission_window_start_ns,
            admission_window_width_ns,
            admission_guard_before_ns,
            admission_guard_after_ns,
        &gate)) {
        if (vdc_domain_is_follower(context)) {
            context->gate = gate;
            vdc_domain_increment_saturating(
                &context->control.follower_local_evidence_bypass_count);
            return false;
        }
        if (context->dpll.debug_continue_enabled != 0u &&
            vdc_domain_debug_gate_recoverable(gate.reject_code)) {
            vdc_domain_record_debug_continue(context, &gate);
            return true;
        }
        context->gate = gate;
        context->dpll.rejected_sample_count++;
        context->dpll.last_reject_code = gate.reject_code;
        context->dpll.update_seq++;
        context->dpll.state = vdc_domain_lock_state_after_reject(
            context, vdc_domain_reject_requires_reacquire(gate.reject_code));
        vdc_domain_record_rejected_sample(context, &evidence, &gate);
        return false;
    }

    return vdc_domain_submit_tdma_evidence(context, &evidence);
}

bool vdc_domain_get_snapshot(const vdc_domain_context_t *context,
                             vdc_domain_snapshot_t *snapshot)
{
    if (context == NULL || snapshot == NULL) {
        return false;
    }
    snapshot->ready = context->ready;
    snapshot->service_count = context->service_count;
    snapshot->first_service_time_ns = context->first_service_time_ns;
    snapshot->last_service_time_ns = context->last_service_time_ns;
    snapshot->schedule = context->schedule;
    snapshot->servo = context->servo;
    snapshot->clock = context->clock;
    snapshot->dco = context->dco;
    snapshot->control = context->control;
    snapshot->oscillator_discipline = context->oscillator_discipline;
    snapshot->dpll = context->dpll;
    snapshot->quality = context->quality;
    snapshot->error_budget = context->error_budget;
    snapshot->path_delay = context->path_delay;
    snapshot->gate = context->gate;
    return true;
}
