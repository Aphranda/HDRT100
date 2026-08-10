#include "distributed_refmem.h"

#include <stddef.h>
#include <string.h>

#include "osal.h"

#define DISTRIBUTED_REFMEM_MAGIC          0x44565431u
#define DISTRIBUTED_REFMEM_END_MAGIC      0x454E4400u
#define DISTRIBUTED_REFMEM_SLOT_COUNT     16u

typedef enum {
    DISTRIBUTED_REFMEM_SLOT_HEADER = 0,
    DISTRIBUTED_REFMEM_SLOT_SYSTEM = 1,
    DISTRIBUTED_REFMEM_SLOT_ROLE = 2,
    DISTRIBUTED_REFMEM_SLOT_VDC = 3,
    DISTRIBUTED_REFMEM_SLOT_LOOP = 4,
    DISTRIBUTED_REFMEM_SLOT_DPLL = 5,
    DISTRIBUTED_REFMEM_SLOT_NODE = 6,
    DISTRIBUTED_REFMEM_SLOT_TRIGGER = 7,
    DISTRIBUTED_REFMEM_SLOT_IO = 8,
    DISTRIBUTED_REFMEM_SLOT_CAL = 9,
    DISTRIBUTED_REFMEM_SLOT_STATS = 10,
    DISTRIBUTED_REFMEM_SLOT_ACK_CMD = 11,
    DISTRIBUTED_REFMEM_SLOT_FAULT = 12,
    DISTRIBUTED_REFMEM_SLOT_GATEWAY = 13,
    DISTRIBUTED_REFMEM_SLOT_SERVICE = 14,
    DISTRIBUTED_REFMEM_SLOT_TLV = 15,
} distributed_refmem_slot_id_t;

typedef struct {
    uint32_t offset;
    uint32_t size;
} distributed_refmem_slot_dir_t;

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
    distributed_refmem_slot_dir_t slots[DISTRIBUTED_REFMEM_SLOT_COUNT];
    uint8_t reserved[DISTRIBUTED_REFMEM_HEADER_SIZE -
                     (10u * sizeof(uint32_t)) -
                     (DISTRIBUTED_REFMEM_SLOT_COUNT * sizeof(distributed_refmem_slot_dir_t))];
} distributed_refmem_header_slot_t;

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
} distributed_refmem_node_slot_t;

typedef struct {
    distributed_refmem_header_slot_t header;
    uint8_t system[DISTRIBUTED_REFMEM_SYSTEM_SIZE];
    uint8_t role[DISTRIBUTED_REFMEM_ROLE_SIZE];
    uint8_t vdc[DISTRIBUTED_REFMEM_VDC_SIZE];
    uint8_t loop[DISTRIBUTED_REFMEM_LOOP_SIZE];
    uint8_t dpll[DISTRIBUTED_REFMEM_DPLL_SIZE];
    distributed_refmem_node_slot_t node[DISTRIBUTED_REFMEM_NODE_COUNT];
    uint8_t trigger[DISTRIBUTED_REFMEM_TRIGGER_SIZE];
    uint8_t io[DISTRIBUTED_REFMEM_IO_SIZE];
    uint8_t calibration[DISTRIBUTED_REFMEM_CAL_SIZE];
    uint8_t statistics[DISTRIBUTED_REFMEM_STATS_SIZE];
    uint8_t ack_command[DISTRIBUTED_REFMEM_ACK_CMD_SIZE];
    uint8_t fault_evidence[DISTRIBUTED_REFMEM_FAULT_SIZE];
    uint8_t gateway[DISTRIBUTED_REFMEM_GATEWAY_SIZE];
    uint8_t service[DISTRIBUTED_REFMEM_SERVICE_SIZE];
    uint8_t tlv[DISTRIBUTED_REFMEM_TLV_SIZE];
} distributed_vector_table_t;

_Static_assert(sizeof(distributed_refmem_header_slot_t) == DISTRIBUTED_REFMEM_HEADER_SIZE,
               "refmem header slot must be 1 KB");
_Static_assert(sizeof(distributed_refmem_node_slot_t) == DISTRIBUTED_REFMEM_NODE_SLOT_SIZE,
               "refmem node slot must be 512 bytes");
_Static_assert(sizeof(distributed_vector_table_t) == DISTRIBUTED_REFMEM_TABLE_SIZE,
               "DistributedVectorTable must be exactly 64 KB");

static distributed_vector_table_t s_distributed_refmem_table __attribute__((aligned(4)));
static distributed_refmem_status_t s_status;
static uint32_t s_service_count;
static bool s_initialized;

static distributed_refmem_header_slot_t *distributed_refmem_header(void)
{
    return &s_distributed_refmem_table.header;
}

static distributed_refmem_node_slot_t *distributed_refmem_node_slot(uint32_t node_id)
{
    return &s_distributed_refmem_table.node[node_id];
}

static void distributed_refmem_set_slot(distributed_refmem_slot_id_t id,
                                        size_t offset,
                                        size_t size)
{
    distributed_refmem_header_slot_t *header = distributed_refmem_header();
    header->slots[(uint32_t)id].offset = (uint32_t)offset;
    header->slots[(uint32_t)id].size = (uint32_t)size;
}

static void distributed_refmem_init_directory(void)
{
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_HEADER,
                                offsetof(distributed_vector_table_t, header),
                                sizeof(s_distributed_refmem_table.header));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_SYSTEM,
                                offsetof(distributed_vector_table_t, system),
                                sizeof(s_distributed_refmem_table.system));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_ROLE,
                                offsetof(distributed_vector_table_t, role),
                                sizeof(s_distributed_refmem_table.role));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_VDC,
                                offsetof(distributed_vector_table_t, vdc),
                                sizeof(s_distributed_refmem_table.vdc));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_LOOP,
                                offsetof(distributed_vector_table_t, loop),
                                sizeof(s_distributed_refmem_table.loop));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_DPLL,
                                offsetof(distributed_vector_table_t, dpll),
                                sizeof(s_distributed_refmem_table.dpll));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_NODE,
                                offsetof(distributed_vector_table_t, node),
                                sizeof(s_distributed_refmem_table.node));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_TRIGGER,
                                offsetof(distributed_vector_table_t, trigger),
                                sizeof(s_distributed_refmem_table.trigger));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_IO,
                                offsetof(distributed_vector_table_t, io),
                                sizeof(s_distributed_refmem_table.io));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_CAL,
                                offsetof(distributed_vector_table_t, calibration),
                                sizeof(s_distributed_refmem_table.calibration));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_STATS,
                                offsetof(distributed_vector_table_t, statistics),
                                sizeof(s_distributed_refmem_table.statistics));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_ACK_CMD,
                                offsetof(distributed_vector_table_t, ack_command),
                                sizeof(s_distributed_refmem_table.ack_command));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_FAULT,
                                offsetof(distributed_vector_table_t, fault_evidence),
                                sizeof(s_distributed_refmem_table.fault_evidence));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_GATEWAY,
                                offsetof(distributed_vector_table_t, gateway),
                                sizeof(s_distributed_refmem_table.gateway));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_SERVICE,
                                offsetof(distributed_vector_table_t, service),
                                sizeof(s_distributed_refmem_table.service));
    distributed_refmem_set_slot(DISTRIBUTED_REFMEM_SLOT_TLV,
                                offsetof(distributed_vector_table_t, tlv),
                                sizeof(s_distributed_refmem_table.tlv));
}

static void distributed_refmem_publish_status_locked(void)
{
    const distributed_refmem_header_slot_t *header = distributed_refmem_header();
    const distributed_refmem_node_slot_t *local_node =
        distributed_refmem_node_slot(DISTRIBUTED_REFMEM_LOCAL_NODE_ID);

    s_status.table_size = header->table_size;
    s_status.layout_version = header->layout_version;
    s_status.table_seq = header->table_seq;
    s_status.local_node_id = header->local_node_id;
    s_status.node_count = header->node_count;
    s_status.local_heartbeat = local_node->heartbeat;
    s_status.service_count = s_service_count;
    s_status.flags = header->flags;
}

bool distributed_refmem_init(void)
{
    osal_critical_enter();

    memset(&s_distributed_refmem_table, 0, sizeof(s_distributed_refmem_table));

    distributed_refmem_header_slot_t *header = distributed_refmem_header();
    header->magic = DISTRIBUTED_REFMEM_MAGIC;
    header->end_magic = DISTRIBUTED_REFMEM_END_MAGIC;
    header->layout_version = DISTRIBUTED_REFMEM_LAYOUT_VERSION;
    header->table_size = DISTRIBUTED_REFMEM_TABLE_SIZE;
    header->table_seq = 1u;
    header->local_node_id = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    header->node_count = DISTRIBUTED_REFMEM_NODE_COUNT;
    header->header_size = DISTRIBUTED_REFMEM_HEADER_SIZE;
    header->slot_count = DISTRIBUTED_REFMEM_SLOT_COUNT;
    header->flags = 0u;
    distributed_refmem_init_directory();

    for (uint32_t i = 0u; i < DISTRIBUTED_REFMEM_NODE_COUNT; i++) {
        distributed_refmem_node_slot_t *node = distributed_refmem_node_slot(i);
        node->node_id = i;
        node->state = DISTRIBUTED_REFMEM_NODE_MISSING;
        node->node_type = DISTRIBUTED_REFMEM_NODE_TYPE_BOARD;
    }

    distributed_refmem_node_slot_t *local_node =
        distributed_refmem_node_slot(DISTRIBUTED_REFMEM_LOCAL_NODE_ID);
    local_node->state = DISTRIBUTED_REFMEM_NODE_OK;
    local_node->node_type = DISTRIBUTED_REFMEM_NODE_TYPE_BOARD;
    local_node->slot_version = 1u;
    local_node->last_update_ms = osal_tick_ms();

    s_service_count = 0u;
    s_initialized = true;
    distributed_refmem_publish_status_locked();

    osal_critical_exit();
    return true;
}

void distributed_refmem_service(void)
{
    if (!s_initialized) {
        return;
    }

    osal_critical_enter();

    distributed_refmem_header_slot_t *header = distributed_refmem_header();
    distributed_refmem_node_slot_t *local_node =
        distributed_refmem_node_slot(DISTRIBUTED_REFMEM_LOCAL_NODE_ID);

    s_service_count++;
    header->table_seq++;
    local_node->heartbeat++;
    local_node->slot_version++;
    local_node->last_update_ms = osal_tick_ms();
    local_node->state = DISTRIBUTED_REFMEM_NODE_OK;

    distributed_refmem_publish_status_locked();

    osal_critical_exit();
}

void distributed_refmem_get_status(distributed_refmem_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_status;
    osal_critical_exit();
}

bool distributed_refmem_get_node(uint32_t node_id, distributed_refmem_node_snapshot_t *snapshot)
{
    if (snapshot == NULL || node_id >= DISTRIBUTED_REFMEM_NODE_COUNT) {
        return false;
    }

    osal_critical_enter();

    const distributed_refmem_node_slot_t *node = distributed_refmem_node_slot(node_id);
    snapshot->node_id = node->node_id;
    snapshot->state = node->state;
    snapshot->heartbeat = node->heartbeat;
    snapshot->slot_version = node->slot_version;
    snapshot->last_update_ms = node->last_update_ms;
    snapshot->stale_count = node->stale_count;
    snapshot->fault_code = node->fault_code;
    snapshot->flags = node->flags;
    snapshot->node_type = node->node_type;

    osal_critical_exit();
    return true;
}
