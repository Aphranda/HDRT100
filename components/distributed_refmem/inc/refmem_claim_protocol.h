#ifndef REFMEM_CLAIM_PROTOCOL_H
#define REFMEM_CLAIM_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_slot_claim.h"

#define REFMEM_CLAIM_FRAME_MAGIC 0x52434C4Du
#define REFMEM_CLAIM_FRAME_VERSION 1u
#define REFMEM_CLAIM_FRAME_PROPOSAL_MAX REFMEM_SLOT_CLAIM_EVIDENCE_MAX

typedef enum {
    REFMEM_CLAIM_FRAME_HELLO = 1u,
    REFMEM_CLAIM_FRAME_PROPOSE = 2u,
    REFMEM_CLAIM_FRAME_CONFLICT = 3u,
    REFMEM_CLAIM_FRAME_RELEASE = 4u,
    REFMEM_CLAIM_FRAME_RESOLVE = 5u,
    REFMEM_CLAIM_FRAME_COMMIT = 6u,
} refmem_claim_frame_type_t;

typedef enum {
    REFMEM_CLAIM_FRAME_OK = 0u,
    REFMEM_CLAIM_FRAME_BAD_ARGUMENT = 1u,
    REFMEM_CLAIM_FRAME_BAD_MAGIC = 2u,
    REFMEM_CLAIM_FRAME_BAD_VERSION = 3u,
    REFMEM_CLAIM_FRAME_BAD_TYPE = 4u,
    REFMEM_CLAIM_FRAME_BAD_COUNT = 5u,
    REFMEM_CLAIM_FRAME_BAD_PAYLOAD_CRC = 6u,
    REFMEM_CLAIM_FRAME_BAD_HEADER_CRC = 7u,
} refmem_claim_frame_result_t;

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t frame_type;
    uint32_t claim_epoch;
    uint32_t claim_seq;
    uint32_t source_board_id;
    uint32_t source_board_uuid_crc32;
    uint32_t payload_count;
    uint32_t payload_crc32;
    uint32_t header_crc32;
} refmem_claim_frame_header_t;

typedef struct {
    refmem_claim_frame_header_t header;
    refmem_slot_claim_proposal_t proposal[REFMEM_CLAIM_FRAME_PROPOSAL_MAX];
} refmem_claim_propose_frame_t;

typedef struct {
    uint32_t board_id;
    uint32_t board_uuid_crc32;
    uint32_t capability_mask;
    uint32_t io_constraint_mask;
    uint32_t ip_core_mask;
    uint32_t hw_profile_crc32;
    uint32_t active_slot_id;
    uint32_t loaded_instance_mask;
    uint32_t baseline_ready;
    uint32_t vdc_ready;
    uint32_t claim_crc32;
} refmem_claim_hello_payload_t;

typedef struct {
    refmem_claim_frame_header_t header;
    refmem_claim_hello_payload_t hello;
} refmem_claim_hello_frame_t;

typedef struct {
    uint32_t map_crc32;
    uint32_t slot_count;
    uint32_t assigned_count;
    uint32_t conflict_count;
    uint32_t overflow_count;
    uint32_t evidence_count;
    uint32_t committed_node_mask;
    uint32_t gate_ready;
} refmem_claim_commit_payload_t;

typedef struct {
    refmem_claim_frame_header_t header;
    refmem_claim_commit_payload_t commit;
} refmem_claim_commit_frame_t;

bool refmem_claim_propose_frame_init(
    refmem_claim_propose_frame_t *frame,
    uint32_t claim_epoch,
    uint32_t claim_seq,
    uint32_t source_board_id,
    uint32_t source_board_uuid_crc32,
    const refmem_slot_claim_proposal_t *proposal,
    uint32_t proposal_count);
refmem_claim_frame_result_t refmem_claim_propose_frame_validate(
    const refmem_claim_propose_frame_t *frame);
bool refmem_claim_hello_frame_init(
    refmem_claim_hello_frame_t *frame,
    uint32_t claim_epoch,
    uint32_t claim_seq,
    const refmem_claim_hello_payload_t *hello);
refmem_claim_frame_result_t refmem_claim_hello_frame_validate(
    const refmem_claim_hello_frame_t *frame);
bool refmem_claim_commit_frame_init(
    refmem_claim_commit_frame_t *frame,
    uint32_t claim_epoch,
    uint32_t claim_seq,
    uint32_t source_board_id,
    uint32_t source_board_uuid_crc32,
    const refmem_claim_commit_payload_t *commit);
refmem_claim_frame_result_t refmem_claim_commit_frame_validate(
    const refmem_claim_commit_frame_t *frame);

#endif
