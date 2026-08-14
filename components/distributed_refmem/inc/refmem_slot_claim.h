#ifndef REFMEM_SLOT_CLAIM_H
#define REFMEM_SLOT_CLAIM_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_application_model.h"

#define REFMEM_SLOT_CLAIM_VERSION 1u
#define REFMEM_SLOT_CLAIM_EVIDENCE_MAX REFMEM_APP_MODEL_CLAIM_CANDIDATE_MAX

typedef enum {
    REFMEM_SLOT_CLAIM_UNCLAIMED = 0u,
    REFMEM_SLOT_CLAIM_CLAIMED = 1u,
    REFMEM_SLOT_CLAIM_CONFLICT = 2u,
    REFMEM_SLOT_CLAIM_RESOLVING = 3u,
    REFMEM_SLOT_CLAIM_STALE = 4u,
    REFMEM_SLOT_CLAIM_MISMATCH = 5u,
    REFMEM_SLOT_CLAIM_OVERFLOW = 6u,
    REFMEM_SLOT_CLAIM_DISABLED = 7u,
} refmem_slot_claim_state_t;

typedef enum {
    REFMEM_SLOT_CLAIM_REASON_OK = 0u,
    REFMEM_SLOT_CLAIM_REASON_BAD_ARGUMENT = 1u,
    REFMEM_SLOT_CLAIM_REASON_BAD_SLOT = 2u,
    REFMEM_SLOT_CLAIM_REASON_DISABLED_SLOT = 3u,
    REFMEM_SLOT_CLAIM_REASON_DUPLICATE_SLOT = 4u,
    REFMEM_SLOT_CLAIM_REASON_UUID_MISMATCH = 5u,
    REFMEM_SLOT_CLAIM_REASON_HW_PROFILE_MISMATCH = 6u,
    REFMEM_SLOT_CLAIM_REASON_OVERFLOW = 7u,
    REFMEM_SLOT_CLAIM_REASON_STALE = 8u,
    REFMEM_SLOT_CLAIM_REASON_CLAIM_CRC = 9u,
    REFMEM_SLOT_CLAIM_REASON_MAP_CRC = 10u,
} refmem_slot_claim_reason_t;

typedef struct {
    uint32_t candidate_id;
    uint32_t board_id;
    uint32_t board_uuid_crc32;
    uint32_t preferred_slot_id;
    uint32_t capability_mask;
    uint32_t io_constraint_mask;
    uint32_t ip_core_mask;
    uint32_t default_persona_mask;
    uint32_t hw_profile_crc32;
    uint32_t loaded_instance_mask;
    uint32_t online_required;
    uint32_t claim_policy;
    uint32_t claim_priority;
} refmem_slot_claim_proposal_t;

typedef struct {
    uint32_t slot_id;
    uint32_t board_id;
    uint32_t board_uuid_crc32;
    uint32_t capability_mask;
    uint32_t io_constraint_mask;
    uint32_t ip_core_mask;
    uint32_t loaded_instance_mask;
    uint32_t claim_count;
    uint32_t claim_state;
    uint32_t reason;
    uint32_t claim_policy;
    uint32_t claim_priority;
    uint32_t online_required;
    uint32_t claim_epoch;
    uint32_t last_claim_seq;
    uint32_t claim_crc32;
} refmem_slot_claim_assignment_t;

typedef struct {
    uint32_t evidence_id;
    uint32_t candidate_id;
    uint32_t slot_id;
    uint32_t board_id;
    uint32_t board_uuid_crc32;
    uint32_t preferred_slot_id;
    uint32_t claim_state;
    uint32_t reason;
    uint32_t claim_policy;
    uint32_t claim_priority;
    uint32_t claim_epoch;
    uint32_t evidence_crc32;
} refmem_slot_claim_evidence_t;

typedef struct {
    uint32_t version;
    uint32_t claim_epoch;
    uint32_t slot_count;
    uint32_t candidate_count;
    uint32_t assigned_count;
    uint32_t conflict_count;
    uint32_t overflow_count;
    uint32_t disabled_count;
    uint32_t evidence_count;
    uint32_t map_crc32;
    refmem_slot_claim_assignment_t slot[REFMEM_APP_MODEL_NODE_COUNT];
    refmem_slot_claim_evidence_t evidence[REFMEM_SLOT_CLAIM_EVIDENCE_MAX];
} refmem_slot_claim_map_t;

typedef struct {
    uint32_t version;
    uint32_t ready;
    uint32_t first_bad_slot;
    uint32_t first_reason;
    uint32_t conflict_count;
    uint32_t overflow_count;
    uint32_t required_missing_count;
    uint32_t mismatch_count;
    uint32_t map_crc32;
} refmem_slot_claim_gate_status_t;

bool refmem_slot_claim_derive_map(const refmem_generic_node_table_t *node_table,
                                  const refmem_board_capability_table_t *board_table,
                                  const refmem_node_load_table_t *node_load_table,
                                  const refmem_fb_instance_table_t *instance_table,
                                  refmem_slot_claim_map_t *map);
const refmem_slot_claim_assignment_t *refmem_slot_claim_find_assignment(
    const refmem_slot_claim_map_t *map,
    uint32_t slot_id);
const refmem_slot_claim_evidence_t *refmem_slot_claim_find_evidence(
    const refmem_slot_claim_map_t *map,
    uint32_t evidence_id);
bool refmem_slot_claim_gate_evaluate(const refmem_slot_claim_map_t *map,
                                     refmem_slot_claim_gate_status_t *status);

#endif
