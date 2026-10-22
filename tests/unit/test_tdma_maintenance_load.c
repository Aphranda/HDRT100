#include "tdma_maintenance_load.h"

#include <assert.h>
#include <stdio.h>

int main(void)
{
    const tdma_maintenance_load_t load = {
        .load_id = 1u,
        .node_id = 2u,
        .instance_id = 1u,
        .payload_class = 6u,
        .window_period_cycles = 64u,
        .window_bytes = 96u,
        .max_frame_size = 80u,
        .required_mask = 0x01u,
    };
    assert(tdma_maintenance_load_valid(&load));

    tdma_origin_tx_facts_t facts = {
        .t2_pending = true,
        .maintenance_pending = true,
        .window_open = true,
        .window_credit_bytes = 96u,
    };
    assert(tdma_maintenance_select_origin_tx(&load, &facts) ==
           TDMA_ORIGIN_TX_T2);

    facts.t2_pending = false;
    assert(tdma_maintenance_select_origin_tx(&load, &facts) ==
           TDMA_ORIGIN_TX_MAINTENANCE);

    facts.window_open = false;
    assert(tdma_maintenance_select_origin_tx(&load, &facts) ==
           TDMA_ORIGIN_TX_NONE);

    facts.window_open = true;
    facts.window_credit_bytes = 40u;
    assert(tdma_maintenance_select_origin_tx(&load, &facts) ==
           TDMA_ORIGIN_TX_NONE);

    facts.window_credit_bytes = 96u;
    facts.maintenance_pending = false;
    assert(tdma_maintenance_select_origin_tx(&load, &facts) ==
           TDMA_ORIGIN_TX_NONE);

    printf("tdma_maintenance_load: table-driven origin selector OK\n");
    return 0;
}
