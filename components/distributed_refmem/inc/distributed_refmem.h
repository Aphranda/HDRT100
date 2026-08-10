#ifndef DISTRIBUTED_REFMEM_H
#define DISTRIBUTED_REFMEM_H

#include <stdbool.h>
#include <stdint.h>

#define DISTRIBUTED_REFMEM_TABLE_SIZE       65536u
#define DISTRIBUTED_REFMEM_LAYOUT_VERSION   1u
#define DISTRIBUTED_REFMEM_NODE_COUNT       8u
#define DISTRIBUTED_REFMEM_LOCAL_NODE_ID    0u

#define DISTRIBUTED_REFMEM_NODE_FLAG_VIRTUAL 0x00000001u

#define DISTRIBUTED_REFMEM_HEADER_SIZE      1024u
#define DISTRIBUTED_REFMEM_SYSTEM_SIZE      1024u
#define DISTRIBUTED_REFMEM_ROLE_SIZE        2048u
#define DISTRIBUTED_REFMEM_VDC_SIZE         2048u
#define DISTRIBUTED_REFMEM_LOOP_SIZE        4096u
#define DISTRIBUTED_REFMEM_DPLL_SIZE        2048u
#define DISTRIBUTED_REFMEM_NODE_SLOT_SIZE   512u
#define DISTRIBUTED_REFMEM_TRIGGER_SIZE     8192u
#define DISTRIBUTED_REFMEM_IO_SIZE          8192u
#define DISTRIBUTED_REFMEM_CAL_SIZE         8192u
#define DISTRIBUTED_REFMEM_STATS_SIZE       8192u
#define DISTRIBUTED_REFMEM_ACK_CMD_SIZE     4096u
#define DISTRIBUTED_REFMEM_FAULT_SIZE       6144u
#define DISTRIBUTED_REFMEM_GATEWAY_SIZE     2048u
#define DISTRIBUTED_REFMEM_SERVICE_SIZE     2048u
#define DISTRIBUTED_REFMEM_TLV_SIZE         2048u

typedef enum {
    DISTRIBUTED_REFMEM_NODE_MISSING = 0,
    DISTRIBUTED_REFMEM_NODE_OK = 1,
    DISTRIBUTED_REFMEM_NODE_STALE = 2,
    DISTRIBUTED_REFMEM_NODE_INVALID = 3,
    DISTRIBUTED_REFMEM_NODE_FAULT = 4,
} distributed_refmem_node_state_t;

typedef enum {
    DISTRIBUTED_REFMEM_NODE_TYPE_BOARD = 0,
    DISTRIBUTED_REFMEM_NODE_TYPE_MODEL_VNA = 1,
    DISTRIBUTED_REFMEM_NODE_TYPE_MODEL_TURNTABLE = 2,
    DISTRIBUTED_REFMEM_NODE_TYPE_MODEL_DUT = 3,
    DISTRIBUTED_REFMEM_NODE_TYPE_TEST_AGENT = 4,
} distributed_refmem_node_type_t;

typedef struct {
    uint32_t table_size;
    uint32_t layout_version;
    uint32_t table_seq;
    uint32_t local_node_id;
    uint32_t node_count;
    uint32_t local_heartbeat;
    uint32_t service_count;
    uint32_t flags;
} distributed_refmem_status_t;

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
} distributed_refmem_node_snapshot_t;

bool distributed_refmem_init(void);
void distributed_refmem_service(void);
void distributed_refmem_get_status(distributed_refmem_status_t *status);
bool distributed_refmem_get_node(uint32_t node_id, distributed_refmem_node_snapshot_t *snapshot);

#endif
