#include "refmem_vector_table.h"

#include <string.h>

_Static_assert(sizeof(refmem_vector_header_slot_t) == DISTRIBUTED_REFMEM_HEADER_SIZE,
               "refmem header slot must be 1 KB");
_Static_assert(sizeof(refmem_vector_node_slot_t) == DISTRIBUTED_REFMEM_NODE_SLOT_SIZE,
               "refmem node slot must be 512 bytes");
_Static_assert(sizeof(refmem_vector_table_t) == DISTRIBUTED_REFMEM_TABLE_SIZE,
               "DistributedVectorTable must be exactly 64 KB");

void refmem_vector_table_clear(refmem_vector_table_t *table)
{
    if (table == NULL) {
        return;
    }
    memset(table, 0, sizeof(*table));
}

refmem_vector_header_slot_t *refmem_vector_table_header(refmem_vector_table_t *table)
{
    return table != NULL ? &table->header : NULL;
}

const refmem_vector_header_slot_t *refmem_vector_table_header_const(const refmem_vector_table_t *table)
{
    return table != NULL ? &table->header : NULL;
}

refmem_vector_node_slot_t *refmem_vector_table_node(refmem_vector_table_t *table, uint32_t node_id)
{
    if (table == NULL || node_id >= DISTRIBUTED_REFMEM_NODE_COUNT) {
        return NULL;
    }
    return &table->node[node_id];
}

const refmem_vector_node_slot_t *refmem_vector_table_node_const(const refmem_vector_table_t *table,
                                                               uint32_t node_id)
{
    if (table == NULL || node_id >= DISTRIBUTED_REFMEM_NODE_COUNT) {
        return NULL;
    }
    return &table->node[node_id];
}

static void refmem_vector_set_slot(refmem_vector_table_t *table,
                                   refmem_vector_slot_id_t id,
                                   size_t offset,
                                   size_t size)
{
    refmem_vector_header_slot_t *header = refmem_vector_table_header(table);
    if (header == NULL) {
        return;
    }
    header->slots[(uint32_t)id].offset = (uint32_t)offset;
    header->slots[(uint32_t)id].size = (uint32_t)size;
}

void refmem_vector_table_init_directory(refmem_vector_table_t *table)
{
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_HEADER,
                           offsetof(refmem_vector_table_t, header),
                           sizeof(table->header));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_SYSTEM,
                           offsetof(refmem_vector_table_t, system),
                           sizeof(table->system));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_ROLE,
                           offsetof(refmem_vector_table_t, role),
                           sizeof(table->role));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_VDC,
                           offsetof(refmem_vector_table_t, vdc),
                           sizeof(table->vdc));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_LOOP,
                           offsetof(refmem_vector_table_t, loop),
                           sizeof(table->loop));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_DPLL,
                           offsetof(refmem_vector_table_t, dpll),
                           sizeof(table->dpll));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_NODE,
                           offsetof(refmem_vector_table_t, node),
                           sizeof(table->node));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_TRIGGER,
                           offsetof(refmem_vector_table_t, trigger),
                           sizeof(table->trigger));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_IO,
                           offsetof(refmem_vector_table_t, io),
                           sizeof(table->io));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_CAL,
                           offsetof(refmem_vector_table_t, calibration),
                           sizeof(table->calibration));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_STATS,
                           offsetof(refmem_vector_table_t, statistics),
                           sizeof(table->statistics));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_ACK_CMD,
                           offsetof(refmem_vector_table_t, ack_command),
                           sizeof(table->ack_command));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_FAULT,
                           offsetof(refmem_vector_table_t, fault_evidence),
                           sizeof(table->fault_evidence));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_GATEWAY,
                           offsetof(refmem_vector_table_t, gateway),
                           sizeof(table->gateway));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_SERVICE,
                           offsetof(refmem_vector_table_t, service),
                           sizeof(table->service));
    refmem_vector_set_slot(table,
                           REFMEM_VECTOR_SLOT_TLV,
                           offsetof(refmem_vector_table_t, tlv),
                           sizeof(table->tlv));
}

static uint32_t refmem_vector_crc32_update(uint32_t crc, const void *data, size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0u; i < size; i++) {
        crc ^= bytes[i];
        crc *= 16777619u;
    }
    return crc;
}

uint32_t refmem_vector_fast_crc32(const void *data, size_t size)
{
    return refmem_vector_crc32_update(2166136261u, data, size);
}

uint32_t refmem_vector_directory_crc(const refmem_vector_table_t *table)
{
    const refmem_vector_header_slot_t *header = refmem_vector_table_header_const(table);
    if (header == NULL) {
        return 0u;
    }
    return refmem_vector_fast_crc32(header->slots, sizeof(header->slots));
}

bool refmem_vector_table_validate_directory(const refmem_vector_table_t *table)
{
    const refmem_vector_header_slot_t *header = refmem_vector_table_header_const(table);
    if (header == NULL || header->slot_count != REFMEM_VECTOR_SLOT_COUNT) {
        return false;
    }

    uint32_t next_offset = 0u;
    for (uint32_t i = 0u; i < REFMEM_VECTOR_SLOT_COUNT; i++) {
        const refmem_vector_slot_dir_t *slot = &header->slots[i];
        if (slot->offset != next_offset || slot->size == 0u) {
            return false;
        }
        if (slot->offset > DISTRIBUTED_REFMEM_TABLE_SIZE ||
            slot->size > (DISTRIBUTED_REFMEM_TABLE_SIZE - slot->offset)) {
            return false;
        }
        next_offset = slot->offset + slot->size;
    }

    return next_offset == DISTRIBUTED_REFMEM_TABLE_SIZE;
}

uint32_t refmem_vector_header_crc(const refmem_vector_table_t *table)
{
    const refmem_vector_header_slot_t *header = refmem_vector_table_header_const(table);
    if (header == NULL) {
        return 0u;
    }

    const size_t crc_offset = offsetof(refmem_vector_header_slot_t, header_crc32);
    const size_t crc_end = crc_offset + sizeof(header->header_crc32);
    uint32_t crc = refmem_vector_crc32_update(2166136261u, header, crc_offset);
    return refmem_vector_crc32_update(crc,
                                      (const uint8_t *)header + crc_end,
                                      sizeof(*header) - crc_end);
}
