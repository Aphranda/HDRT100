#ifndef REFMEM_VECTOR_TABLE_H
#define REFMEM_VECTOR_TABLE_H

#include <stddef.h>
#include <stdint.h>

#include "distributed_refmem.h"

#define REFMEM_VECTOR_MAGIC          0x44565431u
#define REFMEM_VECTOR_END_MAGIC      0x454E4400u
#define REFMEM_VECTOR_SLOT_COUNT     16u
#define REFMEM_VECTOR_TABLE_OWNER    DISTRIBUTED_REFMEM_OWNER_SHARED
#define REFMEM_VECTOR_HEADER_STALE   0u

typedef enum {
    REFMEM_VECTOR_SLOT_HEADER = 0,
    REFMEM_VECTOR_SLOT_SYSTEM = 1,
    REFMEM_VECTOR_SLOT_ROLE = 2,
    REFMEM_VECTOR_SLOT_VDC = 3,
    REFMEM_VECTOR_SLOT_LOOP = 4,
    REFMEM_VECTOR_SLOT_DPLL = 5,
    REFMEM_VECTOR_SLOT_NODE = 6,
    REFMEM_VECTOR_SLOT_TRIGGER = 7,
    REFMEM_VECTOR_SLOT_IO = 8,
    REFMEM_VECTOR_SLOT_CAL = 9,
    REFMEM_VECTOR_SLOT_STATS = 10,
    REFMEM_VECTOR_SLOT_ACK_CMD = 11,
    REFMEM_VECTOR_SLOT_FAULT = 12,
    REFMEM_VECTOR_SLOT_GATEWAY = 13,
    REFMEM_VECTOR_SLOT_SERVICE = 14,
    REFMEM_VECTOR_SLOT_TLV = 15,
} refmem_vector_slot_id_t;

typedef struct {
    uint32_t offset;
    uint32_t size;
} refmem_vector_slot_dir_t;

typedef struct {
    uint32_t magic;
    uint32_t end_magic;
    uint32_t layout_version;
    uint32_t table_size;
    uint32_t table_seq;
    uint32_t local_node_id;
    uint32_t node_count;
    uint32_t header_size;
    uint32_t slot_count;
    uint32_t flags;
    uint32_t table_owner;
    uint32_t header_crc32;
    uint32_t header_stale;
    uint32_t core_count;
    uint32_t core0_vtor_owner;
    uint32_t core1_vtor_owner;
    uint32_t core0_irq_owner_mask;
    uint32_t core1_irq_owner_mask;
    uint32_t entry_table_owner;
    uint32_t runtime_protection_flags;
    uint32_t ram_resident_required;
    uint32_t flash_lockout_supported;
    uint32_t flash_lockout_online;
    uint32_t flash_lockout_requested;
    uint32_t flash_lockout_acknowledged;
    uint32_t core1_park_state;
    refmem_vector_slot_dir_t slots[REFMEM_VECTOR_SLOT_COUNT];
    uint8_t reserved[DISTRIBUTED_REFMEM_HEADER_SIZE -
                     (26u * sizeof(uint32_t)) -
                     (REFMEM_VECTOR_SLOT_COUNT * sizeof(refmem_vector_slot_dir_t))];
} refmem_vector_header_slot_t;

typedef struct {
    uint32_t node_id;
    uint32_t state;
    uint32_t heartbeat;
    uint32_t slot_version;
    uint32_t last_update_ms;
    uint32_t stale_count;
    uint32_t fault_code;
    uint32_t flags;
    uint32_t node_type;
    uint8_t reserved[DISTRIBUTED_REFMEM_NODE_SLOT_SIZE - 9u * sizeof(uint32_t)];
} refmem_vector_node_slot_t;

typedef struct {
    refmem_vector_header_slot_t header;
    uint8_t system[DISTRIBUTED_REFMEM_SYSTEM_SIZE];
    uint8_t role[DISTRIBUTED_REFMEM_ROLE_SIZE];
    uint8_t vdc[DISTRIBUTED_REFMEM_VDC_SIZE];
    uint8_t loop[DISTRIBUTED_REFMEM_LOOP_SIZE];
    uint8_t dpll[DISTRIBUTED_REFMEM_DPLL_SIZE];
    refmem_vector_node_slot_t node[DISTRIBUTED_REFMEM_NODE_COUNT];
    uint8_t trigger[DISTRIBUTED_REFMEM_TRIGGER_SIZE];
    uint8_t io[DISTRIBUTED_REFMEM_IO_SIZE];
    uint8_t calibration[DISTRIBUTED_REFMEM_CAL_SIZE];
    uint8_t statistics[DISTRIBUTED_REFMEM_STATS_SIZE];
    uint8_t ack_command[DISTRIBUTED_REFMEM_ACK_CMD_SIZE];
    uint8_t fault_evidence[DISTRIBUTED_REFMEM_FAULT_SIZE];
    uint8_t gateway[DISTRIBUTED_REFMEM_GATEWAY_SIZE];
    uint8_t service[DISTRIBUTED_REFMEM_SERVICE_SIZE];
    uint8_t tlv[DISTRIBUTED_REFMEM_TLV_SIZE];
} refmem_vector_table_t;

void refmem_vector_table_clear(refmem_vector_table_t *table);
refmem_vector_header_slot_t *refmem_vector_table_header(refmem_vector_table_t *table);
const refmem_vector_header_slot_t *refmem_vector_table_header_const(const refmem_vector_table_t *table);
refmem_vector_node_slot_t *refmem_vector_table_node(refmem_vector_table_t *table, uint32_t node_id);
const refmem_vector_node_slot_t *refmem_vector_table_node_const(const refmem_vector_table_t *table,
                                                               uint32_t node_id);
void refmem_vector_table_init_directory(refmem_vector_table_t *table);
uint32_t refmem_vector_fast_crc32(const void *data, size_t size);
uint32_t refmem_vector_header_crc(const refmem_vector_table_t *table);

#endif
