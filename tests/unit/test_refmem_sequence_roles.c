#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "refmem_sequence_roles.h"

typedef struct {
    refmem_node_load_table_t loads;
    refmem_fb_instance_table_t instances;
    refmem_slot_claim_map_t claims;
    refmem_sequence_role_request_t request;
} fixture_t;

static fixture_t fixture(void)
{
    fixture_t f = {0};
    f.loads.version = REFMEM_APP_MODEL_VERSION;
    f.loads.load_count = 2u;
    f.instances.version = REFMEM_APP_MODEL_VERSION;
    f.instances.instance_count = 2u;
    f.claims.version = REFMEM_SLOT_CLAIM_VERSION;
    f.claims.claim_epoch = 7u;
    f.claims.slot_count = REFMEM_APP_MODEL_NODE_COUNT;
    f.request = (refmem_sequence_role_request_t){
        .local_board_id = 4u, .active_generation = 42u, .claim_epoch = 7u,
        .dut_slot = REFMEM_APP_MODEL_NODE_COUNT - 1u, .vna_slot = 0u,
        .dut_trigger_input_mask = 1u,
        .dut_link_output_mask = 7u, .vna_trigger_output_mask = 8u, .vna_ready_input_mask = 2u };
    f.loads.load[0] = (refmem_node_load_entry_t){
        .node_id = f.request.dut_slot, .instance_id = 5u,
        .role_mask = REFMEM_APP_ROLE_LINK_SWITCHER, .enabled = 1u };
    f.loads.load[1] = (refmem_node_load_entry_t){
        .node_id = f.request.vna_slot, .instance_id = 7u,
        .role_mask = REFMEM_APP_ROLE_INSTRUMENT_CONTROLLER, .enabled = 1u };
    /* Sparse IDs and deliberately reordered entries: ID is never array index. */
    f.instances.instance[0] = (refmem_fb_instance_entry_t){
        .instance_id = 7u, .fb_type = REFMEM_APP_FB_INSTRUMENT_CONTROLLER, .enable_condition = 1u };
    f.instances.instance[1] = (refmem_fb_instance_entry_t){
        .instance_id = 5u, .fb_type = REFMEM_APP_FB_LINK_SWITCHER, .enable_condition = 1u };
    for (uint32_t i = 0u; i < f.claims.slot_count; ++i) {
        f.claims.slot[i] = (refmem_slot_claim_assignment_t){
            .slot_id = i, .board_id = 4u, .claim_epoch = 7u,
            .claim_state = REFMEM_SLOT_CLAIM_CLAIMED,
            .claim_policy = REFMEM_APP_CLAIM_ALLOW_SAME_BOARD_MULTI_SLOT,
            .capability_mask = REFMEM_APP_CAP_SMA_IN | REFMEM_APP_CAP_SMA_OUT | REFMEM_APP_CAP_LINK_CONTROL,
            .io_constraint_mask = REFMEM_APP_IO_SMA_IN | REFMEM_APP_IO_SMA_OUT | REFMEM_APP_IO_LINK_CONTROL,
            .ip_core_mask = REFMEM_APP_IP_PULSE_CAPTURE | REFMEM_APP_IP_PULSE_FIRE | REFMEM_APP_IP_LINK_SEQUENCE,
        };
    }
    f.claims.slot[f.request.dut_slot].loaded_instance_mask = 1u << 5;
    f.claims.slot[f.request.vna_slot].loaded_instance_mask = 1u << 7;
    return f;
}

static refmem_sequence_role_error_t resolve(fixture_t *f, refmem_sequence_role_binding_t *b)
{
    return refmem_sequence_roles_resolve(&f->loads, &f->instances, &f->claims, &f->request, b);
}

static void rejected(fixture_t *f, refmem_sequence_role_error_t error)
{
    refmem_sequence_role_binding_t b, before;
    memset(&b, 0xa5, sizeof(b));
    memcpy(&before, &b, sizeof(b));
    assert(resolve(f, &b) == error);
    assert(memcmp(&before, &b, sizeof(b)) == 0);
}

#define REJECT(change, error) do { fixture_t f = fixture(); change; rejected(&f, error); } while (0)

static void valid_bindings(void)
{
    fixture_t f = fixture();
    refmem_sequence_role_binding_t b;
    assert(resolve(&f, &b) == REFMEM_SEQUENCE_ROLE_OK);
    assert(b.version == REFMEM_SEQUENCE_ROLE_BINDING_VERSION);
    assert(b.generation == 42u && b.claim_epoch == 7u);
    assert(b.local_board_id == 4u);
    assert(b.dut_slot == f.request.dut_slot && b.vna_slot == 0u);
    assert(b.dut_instance_id == 5u && b.vna_instance_id == 7u);
    assert(b.dut_trigger_input_mask == 1u && b.dut_link_output_mask == 7u);
    assert(b.vna_trigger_output_mask == 8u && b.vna_ready_input_mask == 2u);
    /* MANUAL accepts no external DUT input and needs no capture capability. */
    f.request.dut_trigger_input_mask = 0u;
    f.claims.slot[f.request.dut_slot].capability_mask &= ~REFMEM_APP_CAP_SMA_IN;
    f.claims.slot[f.request.dut_slot].io_constraint_mask &= ~REFMEM_APP_IO_SMA_IN;
    f.claims.slot[f.request.dut_slot].ip_core_mask &= ~REFMEM_APP_IP_PULSE_CAPTURE;
    assert(resolve(&f, &b) == REFMEM_SEQUENCE_ROLE_OK);
    f.request.vna_slot = REFMEM_SEQUENCE_ROLE_SLOT_NONE;
    f.request.vna_trigger_output_mask = 0u;
    f.request.vna_ready_input_mask = 0u;
    assert(resolve(&f, &b) == REFMEM_SEQUENCE_ROLE_OK);
    assert(b.vna_slot == REFMEM_SEQUENCE_ROLE_SLOT_NONE && b.vna_instance_id == UINT32_MAX);
    assert(b.vna_ready_input_mask == 0u && b.vna_trigger_output_mask == 0u);
    f.request.dut_link_output_mask = REFMEM_SEQUENCE_ROLE_IO_MASK;
    assert(resolve(&f, &b) == REFMEM_SEQUENCE_ROLE_OK);
}

static void malformed_tables(void)
{
    fixture_t f = fixture();
    refmem_sequence_role_binding_t b;
    assert(refmem_sequence_roles_resolve(NULL, &f.instances, &f.claims, &f.request, &b) == REFMEM_SEQUENCE_ROLE_BAD_ARGUMENT);
    assert(refmem_sequence_roles_resolve(&f.loads, NULL, &f.claims, &f.request, &b) == REFMEM_SEQUENCE_ROLE_BAD_ARGUMENT);
    assert(refmem_sequence_roles_resolve(&f.loads, &f.instances, NULL, &f.request, &b) == REFMEM_SEQUENCE_ROLE_BAD_ARGUMENT);
    assert(refmem_sequence_roles_resolve(&f.loads, &f.instances, &f.claims, NULL, &b) == REFMEM_SEQUENCE_ROLE_BAD_ARGUMENT);
    assert(refmem_sequence_roles_resolve(&f.loads, &f.instances, &f.claims, &f.request, NULL) == REFMEM_SEQUENCE_ROLE_BAD_ARGUMENT);
    REJECT(f.loads.version++, REFMEM_SEQUENCE_ROLE_BAD_ARGUMENT);
    REJECT(f.instances.version++, REFMEM_SEQUENCE_ROLE_BAD_ARGUMENT);
    REJECT(f.claims.version++, REFMEM_SEQUENCE_ROLE_BAD_ARGUMENT);
    REJECT(f.loads.load_count = REFMEM_APP_MODEL_NODE_LOAD_COUNT + 1u, REFMEM_SEQUENCE_ROLE_BAD_ARGUMENT);
    REJECT(f.instances.instance_count = REFMEM_APP_MODEL_INSTANCE_COUNT + 1u, REFMEM_SEQUENCE_ROLE_BAD_ARGUMENT);
    REJECT(f.claims.slot_count = REFMEM_APP_MODEL_NODE_COUNT + 1u, REFMEM_SEQUENCE_ROLE_BAD_ARGUMENT);
}

static void slot_identity_and_epochs(void)
{
    REJECT(f.request.dut_slot = REFMEM_SEQUENCE_ROLE_SLOT_NONE, REFMEM_SEQUENCE_ROLE_BAD_SLOT);
    REJECT(f.request.vna_slot = f.request.dut_slot, REFMEM_SEQUENCE_ROLE_BAD_SLOT);
    REJECT(f.request.vna_slot = f.claims.slot_count, REFMEM_SEQUENCE_ROLE_BAD_SLOT);
    REJECT(f.request.active_generation = 0u, REFMEM_SEQUENCE_ROLE_STALE_GENERATION);
    REJECT(f.request.claim_epoch = 0u, REFMEM_SEQUENCE_ROLE_STALE_CLAIM);
    REJECT(f.claims.claim_epoch++, REFMEM_SEQUENCE_ROLE_STALE_CLAIM);
    for (uint32_t role = 0u; role < 2u; ++role) {
        fixture_t original = fixture();
        uint32_t slot = role == 0u ? original.request.dut_slot : original.request.vna_slot;
        REJECT(f.claims.slot[slot].slot_id = UINT32_MAX, REFMEM_SEQUENCE_ROLE_BAD_SLOT);
        REJECT(f.claims.slot[slot].board_id++, REFMEM_SEQUENCE_ROLE_SLOT_NOT_LOCAL);
        REJECT(f.claims.slot[slot].claim_state = REFMEM_SLOT_CLAIM_CONFLICT, REFMEM_SEQUENCE_ROLE_SLOT_NOT_CLAIMED);
        REJECT(f.claims.slot[slot].claim_epoch++, REFMEM_SEQUENCE_ROLE_STALE_CLAIM);
    }
}

static void role_identity(void)
{
    REJECT(f.loads.load[0].enabled = 0u, REFMEM_SEQUENCE_ROLE_DUT_NOT_LOADED);
    REJECT(f.loads.load[1].enabled = 0u, REFMEM_SEQUENCE_ROLE_VNA_NOT_LOADED);
    REJECT(f.loads.load[1].role_mask = REFMEM_APP_ROLE_MODEL_VNA, REFMEM_SEQUENCE_ROLE_VNA_NOT_LOADED);
    REJECT(f.instances.instance[0].fb_type = REFMEM_APP_FB_MODEL_VNA, REFMEM_SEQUENCE_ROLE_VNA_NOT_LOADED);
    REJECT(f.instances.instance[1].fb_type = REFMEM_APP_FB_TRIGGER_AO, REFMEM_SEQUENCE_ROLE_DUT_NOT_LOADED);
    REJECT(f.instances.instance[0].enable_condition = 0u, REFMEM_SEQUENCE_ROLE_VNA_NOT_LOADED);
    REJECT(f.instances.instance[1].enable_condition = 0u, REFMEM_SEQUENCE_ROLE_DUT_NOT_LOADED);
    REJECT(f.loads.load[0].instance_id = 32u, REFMEM_SEQUENCE_ROLE_DUT_NOT_LOADED);
    REJECT(f.instances.instance[1].instance_id = 4u, REFMEM_SEQUENCE_ROLE_DUT_NOT_LOADED);
    REJECT(f.claims.slot[f.request.dut_slot].loaded_instance_mask = 0u, REFMEM_SEQUENCE_ROLE_DUT_NOT_LOADED);
    REJECT(f.claims.slot[0].loaded_instance_mask = 0u, REFMEM_SEQUENCE_ROLE_VNA_NOT_LOADED);
    REJECT(f.loads.load[2] = f.loads.load[0]; f.loads.load_count++, REFMEM_SEQUENCE_ROLE_DUPLICATE_ROLE);
    REJECT(f.loads.load[2] = f.loads.load[1]; f.loads.load_count++, REFMEM_SEQUENCE_ROLE_DUPLICATE_ROLE);
    REJECT(f.instances.instance[2] = f.instances.instance[1]; f.instances.instance_count++, REFMEM_SEQUENCE_ROLE_DUPLICATE_ROLE);
}

static void io_masks(void)
{
    REJECT(f.request.dut_trigger_input_mask = 3u, REFMEM_SEQUENCE_ROLE_IO_REQUIRED);
    REJECT(f.request.dut_trigger_input_mask = 16u, REFMEM_SEQUENCE_ROLE_IO_REQUIRED);
    REJECT(f.request.dut_link_output_mask = 0u, REFMEM_SEQUENCE_ROLE_IO_REQUIRED);
    REJECT(f.request.dut_link_output_mask = 16u, REFMEM_SEQUENCE_ROLE_IO_REQUIRED);
    REJECT(f.request.vna_ready_input_mask = 0u, REFMEM_SEQUENCE_ROLE_IO_REQUIRED);
    REJECT(f.request.vna_ready_input_mask = 3u, REFMEM_SEQUENCE_ROLE_IO_REQUIRED);
    REJECT(f.request.vna_ready_input_mask = 16u, REFMEM_SEQUENCE_ROLE_IO_REQUIRED);
    REJECT(f.request.vna_trigger_output_mask = 0u, REFMEM_SEQUENCE_ROLE_IO_REQUIRED);
    REJECT(f.request.vna_trigger_output_mask = 16u, REFMEM_SEQUENCE_ROLE_IO_REQUIRED);
    REJECT(f.request.vna_ready_input_mask = 1u, REFMEM_SEQUENCE_ROLE_IO_CONFLICT);
    REJECT(f.request.vna_trigger_output_mask = 1u, REFMEM_SEQUENCE_ROLE_IO_CONFLICT);
    REJECT(f.request.vna_slot = REFMEM_SEQUENCE_ROLE_SLOT_NONE, REFMEM_SEQUENCE_ROLE_IO_REQUIRED);
    for (uint32_t input = 1u; input <= 8u; input <<= 1) {
        fixture_t f = fixture();
        refmem_sequence_role_binding_t b;
        f.request.dut_trigger_input_mask = input;
        f.request.vna_ready_input_mask = input == 1u ? 2u : 1u;
        assert(resolve(&f, &b) == REFMEM_SEQUENCE_ROLE_OK);
    }
}

static void board_masks(void)
{
    REJECT(f.claims.slot[f.request.dut_slot].capability_mask &= ~REFMEM_APP_CAP_LINK_CONTROL,
           REFMEM_SEQUENCE_ROLE_CAPABILITY_MISMATCH);
    REJECT(f.claims.slot[f.request.dut_slot].io_constraint_mask &= ~REFMEM_APP_IO_LINK_CONTROL,
           REFMEM_SEQUENCE_ROLE_CAPABILITY_MISMATCH);
    REJECT(f.claims.slot[f.request.dut_slot].ip_core_mask &= ~REFMEM_APP_IP_LINK_SEQUENCE,
           REFMEM_SEQUENCE_ROLE_CAPABILITY_MISMATCH);
    REJECT(f.claims.slot[f.request.dut_slot].capability_mask &= ~REFMEM_APP_CAP_SMA_IN,
           REFMEM_SEQUENCE_ROLE_CAPABILITY_MISMATCH);
    REJECT(f.claims.slot[0].capability_mask &= ~REFMEM_APP_CAP_SMA_OUT,
           REFMEM_SEQUENCE_ROLE_CAPABILITY_MISMATCH);
    REJECT(f.claims.slot[0].io_constraint_mask &= ~REFMEM_APP_IO_SMA_IN,
           REFMEM_SEQUENCE_ROLE_CAPABILITY_MISMATCH);
    REJECT(f.claims.slot[0].ip_core_mask &= ~REFMEM_APP_IP_PULSE_FIRE,
           REFMEM_SEQUENCE_ROLE_CAPABILITY_MISMATCH);
}

int main(void)
{
    valid_bindings();
    malformed_tables();
    slot_identity_and_epochs();
    role_identity();
    io_masks();
    board_masks();
    puts("role identity, claims, wiring and failure atomicity passed");
    return 0;
}
