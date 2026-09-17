#include "refmem_vector_table.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static uint32_t directory_crc_reference(const refmem_vector_header_region_t *header)
{
    /* Independent byte serialization of the directory's little-endian ABI. */
    uint32_t crc = 2166136261u;
    for (uint32_t id = 0u; id < 16u; ++id) {
        const uint32_t words[2] = {header->regions[id].offset, header->regions[id].size};
        for (uint32_t word = 0u; word < 2u; ++word)
            for (uint32_t byte = 0u; byte < 4u; ++byte) {
                crc ^= (words[word] >> (8u * byte)) & 0xffu;
                crc *= 16777619u;
            }
    }
    return crc;
}

int main(void)
{
    struct { uint64_t before; refmem_vector_table_t table; uint64_t after; } guarded;
    memset(&guarded, 0xa5, sizeof(guarded));
    refmem_vector_table_t *table = &guarded.table;
    refmem_vector_table_clear(table);
    const uint64_t canary = UINT64_C(0xa5a5a5a5a5a5a5a5);
    assert(guarded.before == canary && guarded.after == canary);
    const uint8_t *bytes = (const uint8_t *)table;
    for (size_t i = 0u; i < sizeof(*table); ++i) assert(bytes[i] == 0u);
    assert(sizeof(*table) == 18432u && DISTRIBUTED_REFMEM_LAYOUT_VERSION == 2u);
    const refmem_vector_region_id_t ids[] = {
        REFMEM_VECTOR_REGION_HEADER, REFMEM_VECTOR_REGION_SYSTEM, REFMEM_VECTOR_REGION_ROLE,
        REFMEM_VECTOR_REGION_VDC, REFMEM_VECTOR_REGION_LOOP, REFMEM_VECTOR_REGION_DPLL,
        REFMEM_VECTOR_REGION_NODE, REFMEM_VECTOR_REGION_TRIGGER, REFMEM_VECTOR_REGION_IO,
        REFMEM_VECTOR_REGION_CAL, REFMEM_VECTOR_REGION_STATS, REFMEM_VECTOR_REGION_ACK_CMD,
        REFMEM_VECTOR_REGION_FAULT, REFMEM_VECTOR_REGION_GATEWAY, REFMEM_VECTOR_REGION_SERVICE,
        REFMEM_VECTOR_REGION_TLV};
    table->header.region_count = 16u;
    table->header.layout_version = 2u;
    table->header.table_size = sizeof(*table);
    refmem_vector_table_init_directory(table);
    assert(refmem_vector_table_validate_directory(table));
    for (uint32_t id = 0u; id < 16u; ++id) {
        assert((uint32_t)ids[id] == id);
        assert(table->header.regions[id].offset == id * 1024u);
        assert(table->header.regions[id].size == (id == 15u ? 3072u : 1024u));
    }
    const uint32_t crc = refmem_vector_directory_crc(table);
    assert(crc == directory_crc_reference(&table->header));
    const uint32_t header_crc = refmem_vector_header_crc(table);
    table->header.header_crc32 = header_crc;
    assert(refmem_vector_header_crc(table) == header_crc);
    table->header.regions[6].offset++;
    assert(!refmem_vector_table_validate_directory(table));
    assert(refmem_vector_directory_crc(table) != crc);
    assert(refmem_vector_header_crc(table) != header_crc);
    refmem_vector_table_init_directory(table);
    table->header.regions[15].size = UINT32_MAX;
    assert(!refmem_vector_table_validate_directory(table));
    refmem_vector_table_init_directory(table);
    table->header.region_count = 15u;
    assert(!refmem_vector_table_validate_directory(table));
    assert(!refmem_vector_table_validate_directory(NULL));

    /* All eight fixed RefMem nodes remain addressable even in a six-node
     * TDMA build. Check actual writes against every byte of their neighbors. */
    memset(table, 0xa5, sizeof(*table));
    assert(DISTRIBUTED_REFMEM_NODE_COUNT == 8u && sizeof(table->node[0]) == 128u);
    for (uint32_t node = 0u; node < 8u; ++node) {
        refmem_vector_node_region_t *slot = refmem_vector_table_node(table, node);
        assert(slot == &table->node[node]);
        memset(slot, (int)(0x30u + node), sizeof(*slot));
        assert(refmem_vector_table_node_const(table, node) == slot);
        for (size_t i = 0u; i < sizeof(*table); ++i) {
            const size_t first = offsetof(refmem_vector_table_t, node);
            const bool written = i >= first && i < first + (node + 1u) * 128u;
            const uint8_t expected = written ? (uint8_t)(0x30u + (i - first) / 128u) : 0xa5u;
            assert(bytes[i] == expected);
        }
    }
    assert(!refmem_vector_table_node(table, 8u));
    assert(!refmem_vector_table_node(table, UINT32_MAX));
    assert(!refmem_vector_table_node(NULL, 0u));
    assert(!refmem_vector_table_node_const(table, 8u));
    assert(guarded.before == canary && guarded.after == canary);

    /* Exercise real payload storage, natural alignment and CRC validators;
     * touching the reserved tails must not touch adjacent facts or CRC input. */
    memset(table, 0xa5, sizeof(*table));
    memset(&table->vdc.payload, 0, sizeof(table->vdc.payload));
    memset(&table->dpll.payload, 0, sizeof(table->dpll.payload));
    table->vdc.payload.layout_version = REFMEM_VDC_VECTOR_LAYOUT_VERSION;
    table->dpll.payload.layout_version = REFMEM_DPLL_VECTOR_LAYOUT_VERSION;
    table->vdc.payload.writer = table->dpll.payload.writer = REFMEM_VECTOR_WRITER_CORE1;
    table->vdc.payload.stable_sequence = table->dpll.payload.stable_sequence = 2u;
    table->vdc.payload.flags = table->dpll.payload.flags = REFMEM_VECTOR_FLAG_VALID;
    table->vdc.payload.payload_crc32 = refmem_vdc_vector_payload_crc(&table->vdc.payload);
    table->dpll.payload.payload_crc32 = refmem_dpll_vector_payload_crc(&table->dpll.payload);
    assert(refmem_vdc_vector_payload_validate(&table->vdc.payload));
    assert(refmem_dpll_vector_payload_validate(&table->dpll.payload));
    assert((uintptr_t)&table->vdc.payload % _Alignof(refmem_vdc_vector_payload_t) == 0u);
    assert((uintptr_t)&table->dpll.payload % _Alignof(refmem_dpll_vector_payload_t) == 0u);
    assert(sizeof(table->header) == 1024u && sizeof(table->vdc) == 1024u && sizeof(table->dpll) == 1024u);
    assert(sizeof(table->header.reserved) > 0u && sizeof(table->vdc.reserved) > 0u && sizeof(table->dpll.reserved) > 0u);
    memset(table->vdc.reserved, 0x6a, sizeof(table->vdc.reserved));
    memset(table->dpll.reserved, 0x6b, sizeof(table->dpll.reserved));
    assert(refmem_vdc_vector_payload_validate(&table->vdc.payload));
    assert(refmem_dpll_vector_payload_validate(&table->dpll.payload));
    for (size_t i = 0u; i < sizeof(table->loop); ++i) assert(table->loop[i] == 0xa5u);
    for (size_t i = 0u; i < sizeof(table->role); ++i) assert(table->role[i] == 0xa5u);
    for (size_t i = 0u; i < sizeof(table->node); ++i) assert(((uint8_t *)table->node)[i] == 0xa5u);
    assert(guarded.before == canary && guarded.after == canary);
    printf("{\"table_bytes\":%zu,\"node_bytes\":%zu,\"header_reserved\":%zu,"
           "\"vdc_payload\":%zu,\"vdc_reserved\":%zu,\"dpll_payload\":%zu,\"dpll_reserved\":%zu}\n",
           sizeof(*table), sizeof(table->node[0]), sizeof(table->header.reserved),
           sizeof(table->vdc.payload), sizeof(table->vdc.reserved),
           sizeof(table->dpll.payload), sizeof(table->dpll.reserved));
    return 0;
}
