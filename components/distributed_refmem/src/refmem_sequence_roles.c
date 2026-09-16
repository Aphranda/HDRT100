#include "refmem_sequence_roles.h"

#include <string.h>

static refmem_sequence_role_error_t validate_slot(
    const refmem_slot_claim_map_t *claims, uint32_t slot,
    const refmem_sequence_role_request_t *request)
{
    if (slot >= claims->slot_count || claims->slot[slot].slot_id != slot)
        return REFMEM_SEQUENCE_ROLE_BAD_SLOT;
    const refmem_slot_claim_assignment_t *a = &claims->slot[slot];
    if (a->claim_state != REFMEM_SLOT_CLAIM_CLAIMED)
        return REFMEM_SEQUENCE_ROLE_SLOT_NOT_CLAIMED;
    if (a->board_id != request->local_board_id)
        return REFMEM_SEQUENCE_ROLE_SLOT_NOT_LOCAL;
    if (a->claim_epoch != request->claim_epoch)
        return REFMEM_SEQUENCE_ROLE_STALE_CLAIM;
    return REFMEM_SEQUENCE_ROLE_OK;
}

static refmem_sequence_role_error_t find_role(
    const refmem_node_load_table_t *loads,
    const refmem_fb_instance_table_t *instances,
    const refmem_slot_claim_assignment_t *slot, uint32_t role, uint32_t fb_type,
    refmem_sequence_role_error_t missing, uint32_t *instance_id)
{
    bool found = false;
    for (uint32_t i = 0u; i < loads->load_count; ++i) {
        const refmem_node_load_entry_t *l = &loads->load[i];
        if (l->enabled == 0u || l->node_id != slot->slot_id ||
            (l->role_mask & role) == 0u)
            continue;
        if (found)
            return REFMEM_SEQUENCE_ROLE_DUPLICATE_ROLE;
        const refmem_fb_instance_entry_t *fb = NULL;
        for (uint32_t j = 0u; j < instances->instance_count; ++j) {
            if (instances->instance[j].instance_id != l->instance_id)
                continue;
            if (fb != NULL)
                return REFMEM_SEQUENCE_ROLE_DUPLICATE_ROLE;
            fb = &instances->instance[j];
        }
        if (fb == NULL || fb->enable_condition == 0u || fb->fb_type != fb_type ||
            l->instance_id >= REFMEM_APP_MODEL_INSTANCE_COUNT ||
            l->instance_id >= 32u ||
            (slot->loaded_instance_mask & (1u << l->instance_id)) == 0u)
            return missing;
        found = true;
        *instance_id = l->instance_id;
    }
    return found ? REFMEM_SEQUENCE_ROLE_OK : missing;
}

static bool supports_io(const refmem_slot_claim_assignment_t *slot,
                        uint32_t capabilities, uint32_t io, uint32_t ip)
{
    return (slot->capability_mask & capabilities) == capabilities &&
           (slot->io_constraint_mask & io) == io &&
           (slot->ip_core_mask & ip) == ip;
}

static bool single_input(uint32_t mask)
{
    return (mask & ~REFMEM_SEQUENCE_ROLE_IO_MASK) == 0u &&
           (mask & (mask - 1u)) == 0u;
}

refmem_sequence_role_error_t refmem_sequence_roles_resolve(
    const refmem_node_load_table_t *loads,
    const refmem_fb_instance_table_t *instances,
    const refmem_slot_claim_map_t *claims,
    const refmem_sequence_role_request_t *request,
    refmem_sequence_role_binding_t *binding)
{
    if (loads == NULL || instances == NULL || claims == NULL || request == NULL || binding == NULL ||
        loads->version != REFMEM_APP_MODEL_VERSION ||
        instances->version != REFMEM_APP_MODEL_VERSION ||
        claims->version != REFMEM_SLOT_CLAIM_VERSION ||
        loads->load_count > REFMEM_APP_MODEL_NODE_LOAD_COUNT ||
        instances->instance_count > REFMEM_APP_MODEL_INSTANCE_COUNT ||
        claims->slot_count > REFMEM_APP_MODEL_NODE_COUNT)
        return REFMEM_SEQUENCE_ROLE_BAD_ARGUMENT;
    const bool has_vna = request->vna_slot != REFMEM_SEQUENCE_ROLE_SLOT_NONE;
    if (request->dut_slot == REFMEM_SEQUENCE_ROLE_SLOT_NONE ||
        request->dut_slot >= claims->slot_count ||
        (has_vna && (request->vna_slot >= claims->slot_count ||
                     request->vna_slot == request->dut_slot)))
        return REFMEM_SEQUENCE_ROLE_BAD_SLOT;
    if (request->active_generation == 0u)
        return REFMEM_SEQUENCE_ROLE_STALE_GENERATION;
    if (request->claim_epoch == 0u || claims->claim_epoch != request->claim_epoch)
        return REFMEM_SEQUENCE_ROLE_STALE_CLAIM;
    refmem_sequence_role_error_t result = validate_slot(claims, request->dut_slot, request);
    if (result != REFMEM_SEQUENCE_ROLE_OK) return result;
    uint32_t dut = UINT32_MAX;
    result = find_role(loads, instances, &claims->slot[request->dut_slot],
                       REFMEM_APP_ROLE_LINK_SWITCHER, REFMEM_APP_FB_LINK_SWITCHER,
                       REFMEM_SEQUENCE_ROLE_DUT_NOT_LOADED, &dut);
    if (result != REFMEM_SEQUENCE_ROLE_OK) return result;
    if (!single_input(request->dut_trigger_input_mask) ||
        request->dut_link_output_mask == 0u ||
        (request->dut_link_output_mask & ~REFMEM_SEQUENCE_ROLE_IO_MASK) != 0u)
        return REFMEM_SEQUENCE_ROLE_IO_REQUIRED;
    uint32_t vna = UINT32_MAX;
    if (has_vna) {
        result = validate_slot(claims, request->vna_slot, request);
        if (result != REFMEM_SEQUENCE_ROLE_OK) return result;
        result = find_role(loads, instances, &claims->slot[request->vna_slot],
                           REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER,
                           REFMEM_APP_FB_INSTRUMENT_CONTROLLER,
                           REFMEM_SEQUENCE_ROLE_VNA_NOT_LOADED, &vna);
        if (result != REFMEM_SEQUENCE_ROLE_OK) return result;
        if (request->vna_trigger_output_mask == 0u ||
            (request->vna_trigger_output_mask & ~REFMEM_SEQUENCE_ROLE_IO_MASK) != 0u ||
            request->vna_ready_input_mask == 0u ||
            !single_input(request->vna_ready_input_mask))
            return REFMEM_SEQUENCE_ROLE_IO_REQUIRED;
    } else if (request->vna_trigger_output_mask != 0u || request->vna_ready_input_mask != 0u) {
        return REFMEM_SEQUENCE_ROLE_IO_REQUIRED;
    }
    if ((request->dut_trigger_input_mask & request->vna_ready_input_mask) != 0u ||
        (request->dut_link_output_mask & request->vna_trigger_output_mask) != 0u)
        return REFMEM_SEQUENCE_ROLE_IO_CONFLICT;
    const bool external = request->dut_trigger_input_mask != 0u;
    if (!supports_io(&claims->slot[request->dut_slot],
                     REFMEM_APP_CAP_SMA_OUT | REFMEM_APP_CAP_LINK_CONTROL |
                         (external ? REFMEM_APP_CAP_SMA_IN : 0u),
                     REFMEM_APP_IO_SMA_OUT | REFMEM_APP_IO_LINK_CONTROL |
                         (external ? REFMEM_APP_IO_SMA_IN : 0u),
                     REFMEM_APP_IP_LINK_SEQUENCE |
                         (external ? REFMEM_APP_IP_PULSE_CAPTURE : 0u)) ||
        (has_vna && !supports_io(&claims->slot[request->vna_slot],
                                REFMEM_APP_CAP_SMA_IN | REFMEM_APP_CAP_SMA_OUT,
                                REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT,
                                REFMEM_APP_IP_PULSE_CAPTURE | REFMEM_APP_IP_PULSE_FIRE)))
        return REFMEM_SEQUENCE_ROLE_CAPABILITY_MISMATCH;
    memset(binding, 0, sizeof(*binding));
    binding->version = REFMEM_SEQUENCE_ROLE_BINDING_VERSION;
    binding->generation = request->active_generation;
    binding->claim_epoch = request->claim_epoch;
    binding->local_board_id = request->local_board_id;
    binding->dut_slot = request->dut_slot;
    binding->vna_slot = request->vna_slot;
    binding->dut_instance_id = dut;
    binding->vna_instance_id = vna;
    binding->dut_trigger_input_mask = request->dut_trigger_input_mask;
    binding->dut_link_output_mask = request->dut_link_output_mask;
    binding->vna_trigger_output_mask = request->vna_trigger_output_mask;
    binding->vna_ready_input_mask = request->vna_ready_input_mask;
    return REFMEM_SEQUENCE_ROLE_OK;
}
