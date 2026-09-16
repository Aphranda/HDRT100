#ifndef REFMEM_SEQUENCE_ROLES_H
#define REFMEM_SEQUENCE_ROLES_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_application_model.h"
#include "refmem_slot_claim.h"

#define REFMEM_SEQUENCE_ROLE_SLOT_NONE UINT32_MAX
#define REFMEM_SEQUENCE_ROLE_IO_MASK 0x0Fu
#define REFMEM_SEQUENCE_ROLE_BINDING_VERSION 2u

typedef enum {
    REFMEM_SEQUENCE_ROLE_OK = 0u,
    REFMEM_SEQUENCE_ROLE_BAD_ARGUMENT,
    REFMEM_SEQUENCE_ROLE_BAD_SLOT,
    REFMEM_SEQUENCE_ROLE_SLOT_NOT_LOCAL,
    REFMEM_SEQUENCE_ROLE_SLOT_NOT_CLAIMED,
    REFMEM_SEQUENCE_ROLE_STALE_GENERATION,
    REFMEM_SEQUENCE_ROLE_DUT_NOT_LOADED,
    REFMEM_SEQUENCE_ROLE_VNA_NOT_LOADED,
    REFMEM_SEQUENCE_ROLE_DUPLICATE_ROLE,
    REFMEM_SEQUENCE_ROLE_IO_REQUIRED,
    REFMEM_SEQUENCE_ROLE_IO_CONFLICT,
    REFMEM_SEQUENCE_ROLE_RESOURCE_CONFLICT,
    REFMEM_SEQUENCE_ROLE_STALE_CLAIM,
    REFMEM_SEQUENCE_ROLE_CAPABILITY_MISMATCH,
} refmem_sequence_role_error_t;

/* Core0-only pure validation of a coherent, already validated active model and
 * claim snapshot. The caller owns snapshot lifetime and verifies that
 * active_generation still identifies those tables before publishing a binding.
 * This does not acquire runtime resource leases or prove hardware readiness. */
typedef struct {
    uint32_t local_board_id;
    uint32_t active_generation;
    uint32_t claim_epoch; /* Independent of active/sequence configuration generation. */
    uint32_t dut_slot;
    uint32_t vna_slot; /* SLOT_NONE means DUT-only; no automatic allocation. */
    uint32_t dut_trigger_input_mask; /* Zero permits BUS software stepping. */
    uint32_t dut_link_output_mask;
    uint32_t vna_trigger_output_mask;
    uint32_t vna_ready_input_mask;
} refmem_sequence_role_request_t;

typedef struct {
    uint32_t version;
    uint32_t generation;
    uint32_t claim_epoch;
    uint32_t local_board_id;
    uint32_t dut_slot;
    uint32_t vna_slot;
    uint32_t dut_instance_id;
    uint32_t vna_instance_id;
    uint32_t dut_trigger_input_mask;
    uint32_t dut_link_output_mask;
    uint32_t vna_trigger_output_mask;
    uint32_t vna_ready_input_mask;
} refmem_sequence_role_binding_t;

/* On failure binding is unchanged. IN and OUT masks are separate namespaces;
 * roles cannot share an input or an output within the same namespace. */
refmem_sequence_role_error_t refmem_sequence_roles_resolve(
    const refmem_node_load_table_t *loads,
    const refmem_fb_instance_table_t *instances,
    const refmem_slot_claim_map_t *claims,
    const refmem_sequence_role_request_t *request,
    refmem_sequence_role_binding_t *binding);

#endif
