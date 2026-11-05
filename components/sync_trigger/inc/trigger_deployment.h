#ifndef TRIGGER_DEPLOYMENT_H
#define TRIGGER_DEPLOYMENT_H

#include <stddef.h>
#include <stdint.h>

#include "algo_checksum.h"

#define TRIGGER_DEPLOYMENT_ROLE_COUNT 4u
#define TRIGGER_DEPLOYMENT_IO_MASK 0x0fu

typedef enum {
    TRIGGER_DEPLOYMENT_SLOT_COUNTER = 0u,
    TRIGGER_DEPLOYMENT_SLOT_SWITCH_A = 1u,
    TRIGGER_DEPLOYMENT_SLOT_GATEWAY = 2u,
    TRIGGER_DEPLOYMENT_SLOT_SWITCH_B = 3u,
} trigger_deployment_role_t;

typedef enum {
    TRIGGER_DEPLOYMENT_KIND_COUNTER = 0u,
    TRIGGER_DEPLOYMENT_KIND_SWITCH = 1u,
    TRIGGER_DEPLOYMENT_KIND_GATEWAY = 2u,
} trigger_deployment_role_kind_t;

static inline uint32_t trigger_deployment_role_kind(uint32_t slot)
{
    switch (slot) {
    case TRIGGER_DEPLOYMENT_SLOT_COUNTER: return TRIGGER_DEPLOYMENT_KIND_COUNTER;
    case TRIGGER_DEPLOYMENT_SLOT_SWITCH_A:
    case TRIGGER_DEPLOYMENT_SLOT_SWITCH_B: return TRIGGER_DEPLOYMENT_KIND_SWITCH;
    case TRIGGER_DEPLOYMENT_SLOT_GATEWAY: return TRIGGER_DEPLOYMENT_KIND_GATEWAY;
    default: return UINT32_MAX;
    }
}

typedef enum {
    TRIGGER_DEPLOYMENT_NONE_UNOBSERVED = 0u,
    TRIGGER_DEPLOYMENT_REQUIRED_EDGE = 1u,
} trigger_deployment_feedback_mode_t;

typedef enum {
    TRIGGER_DEPLOYMENT_OK = 0u,
    TRIGGER_DEPLOYMENT_ARGUMENT = 1u,
    TRIGGER_DEPLOYMENT_IDENTITY = 2u,
    TRIGGER_DEPLOYMENT_IO = 3u,
    TRIGGER_DEPLOYMENT_ROLE = 4u,
    TRIGGER_DEPLOYMENT_NOT_LOCAL = 5u,
    TRIGGER_DEPLOYMENT_GENERATION = 6u,
} trigger_deployment_result_t;

typedef struct {
    uint64_t board_uid;
    uint32_t tdma_slot, input_mask, output_mask, feedback_mode;
} trigger_deployment_row_t;

/* Business slot is the row index, independent of board identity and TDMA
 * slot. Two different business slots may have the same SWITCH role type.
 * Input/output masks refer to separate local four-line namespaces. This
 * routing description proves no remote presence, feedback polarity, device
 * readiness or permission to ARM. NONE_UNOBSERVED never means completion. */
typedef struct {
    uint32_t generation;
    trigger_deployment_row_t rows[TRIGGER_DEPLOYMENT_ROLE_COUNT];
} trigger_deployment_t;

static inline uint32_t trigger_deployment_bit_count(uint32_t mask)
{
    uint32_t count = 0u;
    for (; mask; mask &= mask - 1u) ++count;
    return count;
}

static inline trigger_deployment_result_t trigger_deployment_validate(
    const trigger_deployment_t *table, uint32_t node_capacity)
{
    if (!table || node_capacity < TRIGGER_DEPLOYMENT_ROLE_COUNT) {
        return TRIGGER_DEPLOYMENT_ARGUMENT;
    }
    if (!table->generation) return TRIGGER_DEPLOYMENT_GENERATION;
    for (uint32_t role = 0u; role < TRIGGER_DEPLOYMENT_ROLE_COUNT; ++role) {
        const trigger_deployment_row_t *row = &table->rows[role];
        if (!row->board_uid || row->tdma_slot >= node_capacity) {
            return TRIGGER_DEPLOYMENT_IDENTITY;
        }
        for (uint32_t previous = 0u; previous < role; ++previous) {
            if (table->rows[previous].board_uid == row->board_uid ||
                table->rows[previous].tdma_slot == row->tdma_slot) {
                return TRIGGER_DEPLOYMENT_IDENTITY;
            }
        }
        if ((row->input_mask | row->output_mask) & ~TRIGGER_DEPLOYMENT_IO_MASK) {
            return TRIGGER_DEPLOYMENT_IO;
        }
        const uint32_t inputs = trigger_deployment_bit_count(row->input_mask);
        const uint32_t outputs = trigger_deployment_bit_count(row->output_mask);
        switch (trigger_deployment_role_kind(role)) {
        case TRIGGER_DEPLOYMENT_KIND_COUNTER:
            if (inputs != 1u || outputs != 0u) return TRIGGER_DEPLOYMENT_IO;
            break;
        case TRIGGER_DEPLOYMENT_KIND_SWITCH:
            if (inputs != 0u || outputs == 0u) return TRIGGER_DEPLOYMENT_IO;
            break;
        case TRIGGER_DEPLOYMENT_KIND_GATEWAY:
            if (inputs != 1u || outputs != 1u) return TRIGGER_DEPLOYMENT_IO;
            break;
        }
        const uint32_t expected_feedback = role == TRIGGER_DEPLOYMENT_SLOT_GATEWAY
            ? TRIGGER_DEPLOYMENT_REQUIRED_EDGE : TRIGGER_DEPLOYMENT_NONE_UNOBSERVED;
        if (row->feedback_mode != expected_feedback) return TRIGGER_DEPLOYMENT_ROLE;
    }
    return TRIGGER_DEPLOYMENT_OK;
}

/* Standard reflected IEEE CRC32 over canonical little-endian words. Never
 * hash native structure padding. CRC is a content fingerprint, not approval;
 * even an invalid table can be fingerprinted and must be validated separately. */
static inline uint32_t trigger_deployment_crc_word(uint32_t crc, uint32_t word)
{
    const uint8_t bytes[4] = {
        (uint8_t)(word & 0xffu),
        (uint8_t)((word >> 8u) & 0xffu),
        (uint8_t)((word >> 16u) & 0xffu),
        (uint8_t)((word >> 24u) & 0xffu),
    };
    return algo_crc32_raw_update(crc, bytes, sizeof(bytes));
}

static inline uint32_t trigger_deployment_crc(const trigger_deployment_t *table)
{
    if (!table) return 0u;
    uint32_t crc = trigger_deployment_crc_word(UINT32_MAX, table->generation);
    for (uint32_t role = 0u; role < TRIGGER_DEPLOYMENT_ROLE_COUNT; ++role) {
        const trigger_deployment_row_t *row = &table->rows[role];
        crc = trigger_deployment_crc_word(crc, (uint32_t)row->board_uid);
        crc = trigger_deployment_crc_word(crc, (uint32_t)(row->board_uid >> 32u));
        crc = trigger_deployment_crc_word(crc, row->tdma_slot);
        crc = trigger_deployment_crc_word(crc, row->input_mask);
        crc = trigger_deployment_crc_word(crc, row->output_mask);
        crc = trigger_deployment_crc_word(crc, row->feedback_mode);
    }
    return crc ^ UINT32_MAX;
}

/* Resolve exactly the requested role. Never select a different row when
 * local identity disagrees. Rejection leaves *out unchanged; all objects
 * must be distinct. Remote online/installed state belongs to the owner. */
static inline trigger_deployment_result_t trigger_deployment_resolve(
    const trigger_deployment_t *table, uint32_t role, uint64_t local_uid,
    uint32_t local_slot, uint32_t node_capacity, trigger_deployment_row_t *out)
{
    if (!out) return TRIGGER_DEPLOYMENT_ARGUMENT;
    const trigger_deployment_result_t result = trigger_deployment_validate(table, node_capacity);
    if (result != TRIGGER_DEPLOYMENT_OK) return result;
    if (role >= TRIGGER_DEPLOYMENT_ROLE_COUNT) return TRIGGER_DEPLOYMENT_ROLE;
    if (!local_uid || local_slot >= node_capacity) return TRIGGER_DEPLOYMENT_IDENTITY;
    const trigger_deployment_row_t *row = &table->rows[role];
    if (row->board_uid != local_uid || row->tdma_slot != local_slot) {
        return TRIGGER_DEPLOYMENT_NOT_LOCAL;
    }
    *out = *row;
    return TRIGGER_DEPLOYMENT_OK;
}

#endif
