#include "distributed_refmem.h"

#include <stdio.h>
#include <string.h>

static refmem_command_snapshot_t s_nodes[DISTRIBUTED_REFMEM_NODE_COUNT];
static int s_present[DISTRIBUTED_REFMEM_NODE_COUNT];

bool distributed_refmem_node_control_read(uint32_t node_id,
                                          refmem_command_snapshot_t *snapshot,
                                          uint32_t *seq,
                                          uint32_t *crc32)
{
    (void)seq;
    (void)crc32;
    if (snapshot == NULL || node_id >= DISTRIBUTED_REFMEM_NODE_COUNT ||
        !s_present[node_id]) {
        return false;
    }
    *snapshot = s_nodes[node_id];
    return true;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n", name,
                     (unsigned long)expected, (unsigned long)actual);
        return 1;
    }
    return 0;
}

static void reset_nodes(void)
{
    memset(s_nodes, 0, sizeof(s_nodes));
    memset(s_present, 0, sizeof(s_present));
}

int main(void)
{
    int failed = 0;
    refmem_command_aggregate_t aggregate;

    /* 1. no records: counts stay zero, node_count is the fixed capacity. */
    reset_nodes();
    failed += expect_u32("empty returns", distributed_refmem_command_aggregate(&aggregate) ? 1u : 0u, 1u);
    failed += expect_u32("empty node_count", aggregate.node_count, DISTRIBUTED_REFMEM_NODE_COUNT);
    failed += expect_u32("empty acked", aggregate.acked, 0u);
    failed += expect_u32("empty last_reason", aggregate.last_reason, 0u);

    /* 2. one ACKED, one NACKED, one POSTED, one absent. */
    reset_nodes();
    s_present[0] = 1;
    s_nodes[0].state = (uint32_t)REFMEM_COMMAND_STATE_ACKED;
    s_nodes[0].command_seq = 10u;
    s_nodes[0].last_completed_seq = 10u;
    s_present[1] = 1;
    s_nodes[1].state = (uint32_t)REFMEM_COMMAND_STATE_NACKED;
    s_nodes[1].command_seq = 20u;
    s_nodes[1].last_reason = 13u;
    s_present[2] = 1;
    s_nodes[2].state = (uint32_t)REFMEM_COMMAND_STATE_POSTED;
    s_nodes[2].command_seq = 30u;
    failed += expect_u32("mixed acked", distributed_refmem_command_aggregate(&aggregate) ? aggregate.acked : 99u, 1u);
    failed += expect_u32("mixed nacked", aggregate.nacked, 1u);
    failed += expect_u32("mixed posted", aggregate.posted, 1u);
    failed += expect_u32("mixed last_state", aggregate.last_state,
                         (uint32_t)REFMEM_COMMAND_STATE_NACKED);
    failed += expect_u32("mixed last_reason", aggregate.last_reason, 13u);
    failed += expect_u32("mixed last_slot", aggregate.last_reason_slot, 1u);
    failed += expect_u32("mixed completed_seq", aggregate.last_completed_seq, 20u);

    /* 3. a newer ACK (seq 40) wins over the older NACK. */
    s_present[2] = 1;
    s_nodes[2].state = (uint32_t)REFMEM_COMMAND_STATE_ACKED;
    s_nodes[2].command_seq = 40u;
    s_nodes[2].last_completed_seq = 40u;
    failed += expect_u32("newer returns", distributed_refmem_command_aggregate(&aggregate) ? 1u : 0u, 1u);
    failed += expect_u32("newer last_state", aggregate.last_state,
                         (uint32_t)REFMEM_COMMAND_STATE_ACKED);
    failed += expect_u32("newer last_slot", aggregate.last_reason_slot, 2u);
    failed += expect_u32("newer completed_seq", aggregate.last_completed_seq, 40u);
    failed += expect_u32("newer acked", aggregate.acked, 2u);

    /* 4. NULL output fails closed. */
    failed += expect_u32("null returns", distributed_refmem_command_aggregate(NULL) ? 1u : 0u, 0u);

    if (failed != 0) {
        return 1;
    }
    (void)printf("refmem_command_aggregate tests passed\n");
    return 0;
}
