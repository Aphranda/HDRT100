#include "refmem_sync_vdc_feedback.h"

#include <limits.h>
#include <string.h>

_Static_assert(sizeof(refmem_sync_vdc_feedback_record_t) == 64u,
               "Decoded feedback remains bounded across schemas");
_Static_assert(sizeof(refmem_sync_vdc_feedback_assembly_t) == 80u,
               "One raw-feedback assembly uses 80 bytes");
_Static_assert(sizeof(refmem_sync_vdc_boundary_command_t) == 64u,
               "Decoded boundary command must reuse feedback storage");
_Static_assert(offsetof(refmem_sync_vdc_boundary_command_t, schema_version) == 60u,
               "Decoded command header location");

static void feedback_put32(uint8_t *out, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; ++i) out[i] = (uint8_t)(value >> (i * 8u));
}

static uint32_t feedback_get32(const uint8_t *in)
{
    return (uint32_t)in[0] | ((uint32_t)in[1] << 8u) |
        ((uint32_t)in[2] << 16u) | ((uint32_t)in[3] << 24u);
}

static void feedback_put64(uint8_t *out, uint64_t value)
{
    feedback_put32(out, (uint32_t)value);
    feedback_put32(out + 4u, (uint32_t)(value >> 32u));
}

static uint64_t feedback_get64(const uint8_t *in)
{
    return (uint64_t)feedback_get32(in) | ((uint64_t)feedback_get32(in + 4u) << 32u);
}

static bool feedback_identity(uint32_t node_count, uint32_t source, uint32_t target)
{
    return node_count >= 2u && node_count <= REFMEM_VDC_FEEDBACK_MAX_NODES &&
        source < node_count && target < node_count && source != target;
}

static bool feedback_record_valid(const refmem_sync_vdc_feedback_record_t *r,
                                  uint32_t node_count)
{
    if (r == NULL || !feedback_identity(node_count, r->source_slot, r->target_slot) ||
        r->source_arm_epoch == 0u || r->observer_epoch == 0u || r->tick_hz == 0u) return false;
    if (r->schema_version == REFMEM_VDC_FEEDBACK_SCHEMA)
        return r->domain_flags == REFMEM_VDC_FEEDBACK_DOMAIN_FLAGS &&
            r->timer1_enable_after >= r->timer1_enable_before &&
            r->timer1_enable_after - r->timer1_enable_before <= UINT32_MAX;
    if (r->schema_version == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA)
        return r->domain_flags == REFMEM_VDC_FEEDBACK_MODEL_DOMAIN_FLAGS &&
            r->model.output_ns_lo <= r->model.output_ns_hi &&
            r->model.model_token != 0u && r->model.control_session != 0u &&
            r->model.reserved == 0u;
    if (r->schema_version == REFMEM_VDC_FEEDBACK_RATE_SCHEMA)
        return r->domain_flags == REFMEM_VDC_FEEDBACK_RATE_FLAGS &&
            r->rate.coordinate_ns != UINT64_MAX &&
            r->rate.model_token != 0u && r->rate.control_session != 0u &&
            r->rate.reserved == 0u;
    return false;
}

uint32_t refmem_sync_vdc_feedback_crc32(const uint8_t *data, size_t size)
{
    if (data == NULL && size != 0u) return 0u;
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0u; i < size; ++i) {
        crc ^= data[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((0u - (crc & 1u)) & UINT32_C(0xedb88320));
    }
    return crc ^ UINT32_MAX;
}

uint32_t refmem_sync_vdc_feedback_next_sequence(uint32_t sequence)
{
    return sequence == UINT32_MAX ? 1u : sequence + 1u;
}

bool refmem_sync_vdc_feedback_encode(
    const refmem_sync_vdc_feedback_record_t *record, uint32_t node_count,
    uint8_t output[REFMEM_VDC_FEEDBACK_RECORD_SIZE])
{
    if (output == NULL || !feedback_record_valid(record, node_count)) return false;
    uint8_t wire[REFMEM_VDC_FEEDBACK_RECORD_SIZE];
    wire[0] = record->schema_version;
    wire[1] = record->source_slot;
    wire[2] = record->target_slot;
    wire[3] = record->domain_flags;
    feedback_put32(wire + 4u, record->source_clock_epoch_id);
    feedback_put32(wire + 8u, record->source_clock_run_id);
    feedback_put64(wire + 12u, record->source_arm_epoch);
    feedback_put32(wire + 20u, record->observer_epoch);
    feedback_put32(wire + 24u, record->measurement_sequence);
    feedback_put32(wire + 28u, record->tick_hz);
    if (record->schema_version == REFMEM_VDC_FEEDBACK_SCHEMA) {
        feedback_put64(wire + 32u, record->rx_elapsed_cycles);
        feedback_put64(wire + 40u, record->tx_elapsed_cycles);
        feedback_put64(wire + 48u, record->timer1_enable_before);
        feedback_put32(wire + 56u,
            (uint32_t)(record->timer1_enable_after - record->timer1_enable_before));
    } else if (record->schema_version == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA) {
        feedback_put64(wire + 32u, record->model.output_ns_lo);
        feedback_put64(wire + 40u, record->model.output_ns_hi);
        feedback_put32(wire + 48u, record->model.model_token);
        feedback_put32(wire + 52u, record->model.applied_command_seq);
        feedback_put32(wire + 56u, record->model.control_session);
    } else {
        feedback_put64(wire + 32u, record->rate.absolute_output_ns_lo);
        feedback_put64(wire + 40u, record->rate.coordinate_ns);
        feedback_put32(wire + 48u, record->rate.model_token);
        feedback_put32(wire + 52u, record->rate.applied_command_seq);
        feedback_put32(wire + 56u, record->rate.control_session);
    }
    feedback_put32(wire + REFMEM_VDC_FEEDBACK_CRC_OFFSET,
        refmem_sync_vdc_feedback_crc32(wire, REFMEM_VDC_FEEDBACK_CRC_OFFSET));
    memcpy(output, wire, sizeof(wire));
    return true;
}

bool refmem_sync_vdc_feedback_decode(
    const uint8_t wire[REFMEM_VDC_FEEDBACK_RECORD_SIZE], uint32_t node_count,
    uint32_t expected_source, uint32_t expected_target,
    refmem_sync_vdc_feedback_record_t *record)
{
    if (wire == NULL || record == NULL ||
        !feedback_identity(node_count, expected_source, expected_target) ||
        wire[1] != expected_source || wire[2] != expected_target ||
        feedback_get32(wire + REFMEM_VDC_FEEDBACK_CRC_OFFSET) !=
            refmem_sync_vdc_feedback_crc32(wire, REFMEM_VDC_FEEDBACK_CRC_OFFSET)) return false;
    refmem_sync_vdc_feedback_record_t value = {
        .source_arm_epoch = feedback_get64(wire + 12u),
        .source_clock_epoch_id = feedback_get32(wire + 4u),
        .source_clock_run_id = feedback_get32(wire + 8u),
        .observer_epoch = feedback_get32(wire + 20u),
        .measurement_sequence = feedback_get32(wire + 24u),
        .tick_hz = feedback_get32(wire + 28u),
        .schema_version = wire[0], .source_slot = wire[1],
        .target_slot = wire[2], .domain_flags = wire[3],
    };
    if (value.schema_version == REFMEM_VDC_FEEDBACK_SCHEMA) {
        value.rx_elapsed_cycles = feedback_get64(wire + 32u);
        value.tx_elapsed_cycles = feedback_get64(wire + 40u);
        value.timer1_enable_before = feedback_get64(wire + 48u);
        const uint32_t width = feedback_get32(wire + 56u);
        if (UINT64_MAX - value.timer1_enable_before < width) return false;
        value.timer1_enable_after = value.timer1_enable_before + width;
    } else if (value.schema_version == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA) {
        value.model.output_ns_lo = feedback_get64(wire + 32u);
        value.model.output_ns_hi = feedback_get64(wire + 40u);
        value.model.model_token = feedback_get32(wire + 48u);
        value.model.applied_command_seq = feedback_get32(wire + 52u);
        value.model.control_session = feedback_get32(wire + 56u);
        value.model.reserved = 0u;
    } else if (value.schema_version == REFMEM_VDC_FEEDBACK_RATE_SCHEMA) {
        value.rate.absolute_output_ns_lo = feedback_get64(wire + 32u);
        value.rate.coordinate_ns = feedback_get64(wire + 40u);
        value.rate.model_token = feedback_get32(wire + 48u);
        value.rate.applied_command_seq = feedback_get32(wire + 52u);
        value.rate.control_session = feedback_get32(wire + 56u);
        value.rate.reserved = 0u;
    }
    if (!feedback_record_valid(&value, node_count)) return false;
    *record = value;
    return true;
}

static bool boundary_command_valid(const refmem_sync_vdc_boundary_command_t *c,
                                    uint32_t node_count)
{
    return c != NULL && feedback_identity(node_count, c->source_slot, c->target_slot) &&
        c->schema_version == REFMEM_VDC_BOUNDARY_COMMAND_SCHEMA &&
        (c->flags == REFMEM_VDC_BOUNDARY_COMMAND_FLAGS ||
         c->flags == REFMEM_VDC_BOUNDARY_COMMAND_AUTO_FLAGS) && c->reserved == 0u &&
        c->control_session != 0u && c->command_seq != 0u &&
        c->command_seq > c->expected_applied_command_seq && c->target_arm_epoch != 0u &&
        c->target_observer_epoch != 0u && c->expected_target_model_token != 0u;
}

bool refmem_sync_vdc_boundary_command_encode(
    const refmem_sync_vdc_boundary_command_t *command, uint32_t node_count,
    uint8_t output[REFMEM_VDC_FEEDBACK_RECORD_SIZE])
{
    if (output == NULL || !boundary_command_valid(command, node_count)) return false;
    uint8_t wire[REFMEM_VDC_FEEDBACK_RECORD_SIZE];
    wire[0] = command->schema_version; wire[1] = command->source_slot;
    wire[2] = command->target_slot; wire[3] = command->flags;
    feedback_put32(wire + 4u, command->control_session);
    feedback_put32(wire + 8u, command->command_seq);
    feedback_put32(wire + 12u, command->schedule_crc32);
    feedback_put32(wire + 16u, command->target_clock_epoch_id);
    feedback_put32(wire + 20u, command->target_clock_run_id);
    feedback_put64(wire + 24u, command->target_arm_epoch);
    feedback_put32(wire + 32u, command->target_observer_epoch);
    feedback_put32(wire + 36u, command->basis_measurement_sequence);
    feedback_put32(wire + 40u, command->expected_target_model_token);
    feedback_put32(wire + 44u, command->expected_applied_command_seq);
    feedback_put32(wire + 48u, (uint32_t)command->signed_delta_rate_ppb);
    feedback_put64(wire + 52u, command->basis_source_output_ns_lo);
    feedback_put32(wire + REFMEM_VDC_FEEDBACK_CRC_OFFSET,
        refmem_sync_vdc_feedback_crc32(wire, REFMEM_VDC_FEEDBACK_CRC_OFFSET));
    memcpy(output, wire, sizeof(wire));
    return true;
}

bool refmem_sync_vdc_boundary_command_decode(
    const uint8_t wire[REFMEM_VDC_FEEDBACK_RECORD_SIZE], uint32_t node_count,
    uint32_t expected_source, uint32_t expected_target,
    refmem_sync_vdc_boundary_command_t *command)
{
    if (wire == NULL || command == NULL ||
        !feedback_identity(node_count, expected_source, expected_target) ||
        wire[0] != REFMEM_VDC_BOUNDARY_COMMAND_SCHEMA ||
        wire[1] != expected_source || wire[2] != expected_target ||
        feedback_get32(wire + REFMEM_VDC_FEEDBACK_CRC_OFFSET) !=
            refmem_sync_vdc_feedback_crc32(wire, REFMEM_VDC_FEEDBACK_CRC_OFFSET)) return false;
    const uint32_t rate_bits = feedback_get32(wire + 48u);
    /* Avoid implementation-defined u32 -> i32 conversion for negative rates. */
    const int32_t rate = rate_bits <= INT32_MAX ? (int32_t)rate_bits :
        -1 - (int32_t)(UINT32_MAX - rate_bits);
    const refmem_sync_vdc_boundary_command_t value = {
        .target_arm_epoch = feedback_get64(wire + 24u),
        .basis_source_output_ns_lo = feedback_get64(wire + 52u),
        .control_session = feedback_get32(wire + 4u),
        .command_seq = feedback_get32(wire + 8u),
        .schedule_crc32 = feedback_get32(wire + 12u),
        .target_clock_epoch_id = feedback_get32(wire + 16u),
        .target_clock_run_id = feedback_get32(wire + 20u),
        .target_observer_epoch = feedback_get32(wire + 32u),
        .basis_measurement_sequence = feedback_get32(wire + 36u),
        .expected_target_model_token = feedback_get32(wire + 40u),
        .expected_applied_command_seq = feedback_get32(wire + 44u),
        .signed_delta_rate_ppb = rate, .reserved = 0u,
        .schema_version = wire[0], .source_slot = wire[1],
        .target_slot = wire[2], .flags = wire[3],
    };
    if (!boundary_command_valid(&value, node_count)) return false;
    *command = value;
    return true;
}

void refmem_sync_vdc_feedback_reset(refmem_sync_vdc_feedback_assembly_t *assembly)
{
    if (assembly != NULL) memset(assembly, 0, sizeof(*assembly));
}

static void feedback_cancel(refmem_sync_vdc_feedback_assembly_t *assembly)
{
    assembly->state &= (uint8_t)~REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE;
    assembly->next_fragment = 0u;
}

bool refmem_sync_vdc_feedback_expire(
    refmem_sync_vdc_feedback_assembly_t *assembly, uint32_t now_ms)
{
    if (assembly == NULL ||
        (assembly->state & REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE) == 0u ||
        (uint32_t)(now_ms - assembly->first_ms) <
            REFMEM_VDC_FEEDBACK_ASSEMBLY_TIMEOUT_MS) return false;
    feedback_cancel(assembly);
    return true;
}

static bool feedback_sequence_newer(uint32_t candidate, uint32_t previous)
{
    const uint32_t delta = candidate - previous;
    return delta != 0u && delta <= INT32_MAX;
}

/* index is bounded to 0..15, so at most one wrap/skip can occur. */
static uint32_t feedback_sequence_at(uint32_t first, uint8_t index)
{
    uint32_t value = first + index;
    if (value < first || value == 0u) ++value;
    return value;
}

static bool feedback_schema_matches(uint8_t schema, bool command)
{
    return command ? schema == REFMEM_VDC_BOUNDARY_COMMAND_SCHEMA :
        (schema == REFMEM_VDC_FEEDBACK_SCHEMA || schema == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA ||
         schema == REFMEM_VDC_FEEDBACK_RATE_SCHEMA);
}

static refmem_sync_vdc_feedback_result_t feedback_typed_push(
    refmem_sync_vdc_feedback_assembly_t *assembly,
    uint32_t source_slot, uint32_t target_slot, uint32_t node_count,
    uint32_t transport_sequence, uint8_t fragment_index, uint8_t fragment_count,
    const uint8_t data[REFMEM_VDC_FEEDBACK_FRAGMENT_SIZE], uint32_t now_ms,
    uint8_t complete_wire[REFMEM_VDC_FEEDBACK_RECORD_SIZE], bool command)
{
    if (assembly == NULL) return REFMEM_VDC_FEEDBACK_BAD_ARGUMENT;
    const bool expired = refmem_sync_vdc_feedback_expire(assembly, now_ms);
    refmem_sync_vdc_feedback_result_t rejected = REFMEM_VDC_FEEDBACK_PROGRESS;
    if (data == NULL || complete_wire == NULL) rejected = REFMEM_VDC_FEEDBACK_BAD_ARGUMENT;
    else if (!feedback_identity(node_count, source_slot, target_slot) ||
        ((assembly->state & REFMEM_VDC_FEEDBACK_ASSEMBLY_SEEN) != 0u &&
         (assembly->source_slot != source_slot || assembly->target_slot != target_slot)))
        rejected = REFMEM_VDC_FEEDBACK_BAD_IDENTITY;
    else if (transport_sequence == 0u) rejected = REFMEM_VDC_FEEDBACK_BAD_SEQUENCE;
    else if (fragment_count != REFMEM_VDC_FEEDBACK_FRAGMENT_COUNT ||
             fragment_index >= REFMEM_VDC_FEEDBACK_FRAGMENT_COUNT)
        rejected = REFMEM_VDC_FEEDBACK_BAD_FRAGMENT;
    else if ((fragment_index == 0u && !feedback_schema_matches(data[0], command)) ||
        ((assembly->state & REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE) != 0u &&
         !feedback_schema_matches(assembly->payload[0], command)))
        rejected = REFMEM_VDC_FEEDBACK_BAD_RECORD;
    if (rejected != REFMEM_VDC_FEEDBACK_PROGRESS) {
        feedback_cancel(assembly);
        return expired ? REFMEM_VDC_FEEDBACK_EXPIRED : rejected;
    }

    const bool seen = (assembly->state & REFMEM_VDC_FEEDBACK_ASSEMBLY_SEEN) != 0u;
    const bool active = (assembly->state & REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE) != 0u;
    const bool newer = !seen || feedback_sequence_newer(transport_sequence, assembly->last_sequence);
    if (expired && !(fragment_index == 0u && newer)) return REFMEM_VDC_FEEDBACK_EXPIRED;

    const uint32_t expected = feedback_sequence_at(assembly->first_sequence, fragment_index);
    if (seen && fragment_index < assembly->next_fragment && transport_sequence == expected) {
        if (memcmp(assembly->payload + (size_t)fragment_index * REFMEM_VDC_FEEDBACK_FRAGMENT_SIZE,
                   data, REFMEM_VDC_FEEDBACK_FRAGMENT_SIZE) == 0)
            return REFMEM_VDC_FEEDBACK_DUPLICATE;
        feedback_cancel(assembly);
        return REFMEM_VDC_FEEDBACK_CONFLICT;
    }

    refmem_sync_vdc_feedback_result_t result = REFMEM_VDC_FEEDBACK_PROGRESS;
    if (fragment_index == 0u) {
        /* Compare with the latest copied sequence, not only the group start.
         * An old zero must not reset a group that has already advanced. */
        if (!newer) return REFMEM_VDC_FEEDBACK_STALE;
        result = expired ? REFMEM_VDC_FEEDBACK_EXPIRED_RESTARTED :
            (active ? REFMEM_VDC_FEEDBACK_RESTARTED : REFMEM_VDC_FEEDBACK_PROGRESS);
        assembly->first_sequence = transport_sequence;
        assembly->first_ms = now_ms;
        assembly->next_fragment = 0u;
        assembly->source_slot = (uint8_t)source_slot;
        assembly->target_slot = (uint8_t)target_slot;
        assembly->state = REFMEM_VDC_FEEDBACK_ASSEMBLY_SEEN | REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE;
    } else {
        if (seen && !newer) return REFMEM_VDC_FEEDBACK_STALE;
        if (!active || fragment_index != assembly->next_fragment || transport_sequence != expected) {
            feedback_cancel(assembly);
            return REFMEM_VDC_FEEDBACK_BAD_SEQUENCE;
        }
    }
    memcpy(assembly->payload + (size_t)fragment_index * REFMEM_VDC_FEEDBACK_FRAGMENT_SIZE,
           data, REFMEM_VDC_FEEDBACK_FRAGMENT_SIZE);
    assembly->last_sequence = transport_sequence;
    assembly->next_fragment = (uint8_t)(fragment_index + 1u);
    if (assembly->next_fragment != REFMEM_VDC_FEEDBACK_FRAGMENT_COUNT) return result;

    union {
        refmem_sync_vdc_feedback_record_t feedback;
        refmem_sync_vdc_boundary_command_t command;
    } decoded;
    const bool valid = command
        ? refmem_sync_vdc_boundary_command_decode(assembly->payload, node_count,
            source_slot, target_slot, &decoded.command)
        : refmem_sync_vdc_feedback_decode(assembly->payload, node_count,
            source_slot, target_slot, &decoded.feedback);
    if (!valid) {
        feedback_cancel(assembly);
        return REFMEM_VDC_FEEDBACK_BAD_RECORD;
    }
    assembly->state &= (uint8_t)~REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE;
    memcpy(complete_wire, assembly->payload, REFMEM_VDC_FEEDBACK_RECORD_SIZE);
    return REFMEM_VDC_FEEDBACK_COMPLETE;
}

refmem_sync_vdc_feedback_result_t refmem_sync_vdc_feedback_push(
    refmem_sync_vdc_feedback_assembly_t *assembly,
    uint32_t source_slot, uint32_t target_slot, uint32_t node_count,
    uint32_t transport_sequence, uint8_t fragment_index, uint8_t fragment_count,
    const uint8_t data[REFMEM_VDC_FEEDBACK_FRAGMENT_SIZE], uint32_t now_ms,
    uint8_t complete_wire[REFMEM_VDC_FEEDBACK_RECORD_SIZE])
{
    return feedback_typed_push(assembly, source_slot, target_slot, node_count,
        transport_sequence, fragment_index, fragment_count, data, now_ms, complete_wire, false);
}

refmem_sync_vdc_feedback_result_t refmem_sync_vdc_boundary_command_push(
    refmem_sync_vdc_feedback_assembly_t *assembly,
    uint32_t source_slot, uint32_t target_slot, uint32_t node_count,
    uint32_t transport_sequence, uint8_t fragment_index, uint8_t fragment_count,
    const uint8_t data[REFMEM_VDC_FEEDBACK_FRAGMENT_SIZE], uint32_t now_ms,
    uint8_t complete_wire[REFMEM_VDC_FEEDBACK_RECORD_SIZE])
{
    return feedback_typed_push(assembly, source_slot, target_slot, node_count,
        transport_sequence, fragment_index, fragment_count, data, now_ms, complete_wire, true);
}

refmem_sync_vdc_feedback_order_t refmem_sync_vdc_feedback_compare(
    const uint8_t candidate[REFMEM_VDC_FEEDBACK_RECORD_SIZE],
    const uint8_t previous[REFMEM_VDC_FEEDBACK_RECORD_SIZE],
    uint32_t node_count, uint32_t expected_source, uint32_t expected_target)
{
    refmem_sync_vdc_feedback_record_t next, old;
    if (!refmem_sync_vdc_feedback_decode(candidate, node_count, expected_source, expected_target, &next))
        return REFMEM_VDC_FEEDBACK_ORDER_INVALID;
    if (previous == NULL) return REFMEM_VDC_FEEDBACK_ORDER_FIRST;
    if (!refmem_sync_vdc_feedback_decode(previous, node_count, expected_source, expected_target, &old))
        return REFMEM_VDC_FEEDBACK_ORDER_INVALID;
    if (memcmp(candidate, previous, REFMEM_VDC_FEEDBACK_RECORD_SIZE) == 0)
        return REFMEM_VDC_FEEDBACK_ORDER_DUPLICATE;
    if (next.schema_version != old.schema_version ||
        next.source_clock_epoch_id != old.source_clock_epoch_id ||
        next.source_clock_run_id != old.source_clock_run_id ||
        next.source_arm_epoch != old.source_arm_epoch || next.observer_epoch != old.observer_epoch ||
        (next.schema_version == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA &&
         next.model.control_session != old.model.control_session) ||
        (next.schema_version == REFMEM_VDC_FEEDBACK_RATE_SCHEMA &&
         next.rate.control_session != old.rate.control_session))
        return REFMEM_VDC_FEEDBACK_ORDER_NEW_NAMESPACE;
    if (next.measurement_sequence == old.measurement_sequence)
        return REFMEM_VDC_FEEDBACK_ORDER_CONFLICT;
    if (next.measurement_sequence < old.measurement_sequence)
        return REFMEM_VDC_FEEDBACK_ORDER_STALE;
    if (next.tick_hz != old.tick_hz)
        return REFMEM_VDC_FEEDBACK_ORDER_CONFLICT;
    if (next.schema_version == REFMEM_VDC_FEEDBACK_SCHEMA) {
        if (next.timer1_enable_before != old.timer1_enable_before ||
            next.timer1_enable_after != old.timer1_enable_after)
            return REFMEM_VDC_FEEDBACK_ORDER_CONFLICT;
    } else if (next.schema_version == REFMEM_VDC_FEEDBACK_MODEL_SCHEMA) {
        if (next.model.model_token < old.model.model_token ||
            next.model.applied_command_seq < old.model.applied_command_seq)
            return REFMEM_VDC_FEEDBACK_ORDER_STALE;
    } else if (next.rate.model_token < old.rate.model_token ||
               next.rate.applied_command_seq < old.rate.applied_command_seq)
        return REFMEM_VDC_FEEDBACK_ORDER_STALE;
    return REFMEM_VDC_FEEDBACK_ORDER_NEWER;
}
