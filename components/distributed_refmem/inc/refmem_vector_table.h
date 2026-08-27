#ifndef REFMEM_VECTOR_TABLE_H
#define REFMEM_VECTOR_TABLE_H

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "distributed_refmem.h"
#include "refmem_vdc_vector.h"

#define REFMEM_VECTOR_MAGIC          0x44565431u
#define REFMEM_VECTOR_END_MAGIC      0x454E4400u
#define REFMEM_VECTOR_REGION_COUNT  16u
#define REFMEM_VECTOR_TABLE_OWNER    DISTRIBUTED_REFMEM_OWNER_SHARED
#define REFMEM_VECTOR_HEADER_STALE   0u

typedef enum {
    REFMEM_VECTOR_REGION_HEADER = 0,
    REFMEM_VECTOR_REGION_SYSTEM = 1,
    REFMEM_VECTOR_REGION_ROLE = 2,
    REFMEM_VECTOR_REGION_VDC = 3,
    REFMEM_VECTOR_REGION_LOOP = 4,
    REFMEM_VECTOR_REGION_DPLL = 5,
    REFMEM_VECTOR_REGION_NODE = 6,
    REFMEM_VECTOR_REGION_TRIGGER = 7,
    REFMEM_VECTOR_REGION_IO = 8,
    REFMEM_VECTOR_REGION_CAL = 9,
    REFMEM_VECTOR_REGION_STATS = 10,
    REFMEM_VECTOR_REGION_ACK_CMD = 11,
    REFMEM_VECTOR_REGION_FAULT = 12,
    REFMEM_VECTOR_REGION_GATEWAY = 13,
    REFMEM_VECTOR_REGION_SERVICE = 14,
    REFMEM_VECTOR_REGION_TLV = 15,
} refmem_vector_region_id_t;

typedef struct {
    uint32_t offset;
    uint32_t size;
} refmem_vector_region_dir_t;

typedef struct {
    uint32_t magic;
    uint32_t end_magic;
    uint32_t layout_version;
    uint32_t table_size;
    uint32_t table_seq;
    uint32_t local_node_id;
    uint32_t node_count;
    uint32_t header_size;
    uint32_t region_count;
    uint32_t flags;
    uint32_t table_owner;
    uint32_t header_crc32;
    uint32_t directory_crc32;
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
    uint32_t flash_lockout_last_result;
    uint32_t flash_lockout_last_elapsed_us;
    uint32_t flash_lockout_request_seq;
    uint32_t flash_lockout_ack_seq;
    uint32_t flash_lockout_release_seq;
    uint32_t flash_lockout_timeout_count;
    uint32_t flash_lockout_release_timeout_count;
    refmem_vector_region_dir_t regions[REFMEM_VECTOR_REGION_COUNT];
    uint8_t reserved[DISTRIBUTED_REFMEM_HEADER_SIZE -
                     (34u * sizeof(uint32_t)) -
                     (REFMEM_VECTOR_REGION_COUNT * sizeof(refmem_vector_region_dir_t))];
} refmem_vector_header_region_t;

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
} refmem_vector_node_region_t;

/* The payloads are written by core1 and read by core0/diagnostic clients.
 * Keep the seqlock outside the CRC-covered payload so an in-progress write is
 * unambiguously rejected by readers.  The reserved tail keeps the fixed
 * DistributedVectorTable directory ABI unchanged. */
typedef struct {
    volatile uint32_t seqlock;
    /* Keep the payload naturally aligned without changing the fixed 2 KiB
     * region ABI.  Both payloads contain uint64_t fields and therefore need
     * an explicit four-byte pad after the seqlock word on RP2350. */
    uint8_t payload_alignment_pad[
        _Alignof(refmem_vdc_vector_payload_t) > sizeof(uint32_t)
            ? _Alignof(refmem_vdc_vector_payload_t) - sizeof(uint32_t)
            : 0u];
    refmem_vdc_vector_payload_t payload;
    uint8_t reserved[DISTRIBUTED_REFMEM_VDC_SIZE -
                     sizeof(uint32_t) -
                     (_Alignof(refmem_vdc_vector_payload_t) > sizeof(uint32_t)
                          ? _Alignof(refmem_vdc_vector_payload_t) - sizeof(uint32_t)
                          : 0u) -
                     sizeof(refmem_vdc_vector_payload_t)];
} refmem_vdc_vector_region_t;

typedef struct {
    volatile uint32_t seqlock;
    uint8_t payload_alignment_pad[
        _Alignof(refmem_dpll_vector_payload_t) > sizeof(uint32_t)
            ? _Alignof(refmem_dpll_vector_payload_t) - sizeof(uint32_t)
            : 0u];
    refmem_dpll_vector_payload_t payload;
    uint8_t reserved[DISTRIBUTED_REFMEM_DPLL_SIZE -
                     sizeof(uint32_t) -
                     (_Alignof(refmem_dpll_vector_payload_t) > sizeof(uint32_t)
                          ? _Alignof(refmem_dpll_vector_payload_t) - sizeof(uint32_t)
                          : 0u) -
                     sizeof(refmem_dpll_vector_payload_t)];
} refmem_dpll_vector_region_t;

typedef struct {
    refmem_vector_header_region_t header;
    uint8_t system[DISTRIBUTED_REFMEM_SYSTEM_SIZE];
    uint8_t role[DISTRIBUTED_REFMEM_ROLE_SIZE];
    refmem_vdc_vector_region_t vdc;
    uint8_t loop[DISTRIBUTED_REFMEM_LOOP_SIZE];
    refmem_dpll_vector_region_t dpll;
    refmem_vector_node_region_t node[DISTRIBUTED_REFMEM_NODE_COUNT];
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
refmem_vector_header_region_t *refmem_vector_table_header(refmem_vector_table_t *table);
const refmem_vector_header_region_t *refmem_vector_table_header_const(const refmem_vector_table_t *table);
refmem_vector_node_region_t *refmem_vector_table_node(refmem_vector_table_t *table, uint32_t node_id);
const refmem_vector_node_region_t *refmem_vector_table_node_const(const refmem_vector_table_t *table,
                                                                 uint32_t node_id);
void refmem_vector_table_init_directory(refmem_vector_table_t *table);
uint32_t refmem_vector_fast_crc32(const void *data, size_t size);
uint32_t refmem_vector_directory_crc(const refmem_vector_table_t *table);
bool refmem_vector_table_validate_directory(const refmem_vector_table_t *table);
uint32_t refmem_vector_header_crc(const refmem_vector_table_t *table);

#endif
