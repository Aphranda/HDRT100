/* Host cases for the P0T pair-scan model (tdma_guide_scan.c).
 *
 * These mirror tests/python/test_cal_guide_topology_table.py plus the local
 * role/record/complete contract the board-side executor relies on. They do not
 * touch hardware and do not replace the four-board probe. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "tdma_guide_scan.h"

static void arm_pair(tdma_guide_scan_table_t *table,
                     uint32_t driver,
                     uint32_t receiver)
{
    const uint32_t index = tdma_guide_scan_pair_index(table, driver, receiver);
    assert(index != UINT32_MAX);
    assert(tdma_guide_scan_record(table, index, true));
}

static tdma_guide_scan_verdict_t verdict_of(
    const tdma_guide_scan_table_t *table, uint32_t anchor, uint8_t *order)
{
    uint32_t length = 0u;
    return tdma_guide_scan_aggregate(table, anchor, order,
                                     TDMA_GUIDE_SCAN_NODE_MAX, &length);
}

static void test_pair_table(void)
{
    tdma_guide_scan_table_t table;
    assert(tdma_guide_scan_init(&table, 4u));
    assert(table.pair_count == 12u);

    uint32_t driver = 0u;
    uint32_t receiver = 0u;
    assert(tdma_guide_scan_pair_at(&table, 0u, &driver, &receiver));
    assert(driver == 0u && receiver == 1u);
    assert(tdma_guide_scan_pair_at(&table, 3u, &driver, &receiver));
    assert(driver == 1u && receiver == 0u);
    assert(!tdma_guide_scan_pair_at(&table, 12u, &driver, &receiver));
    assert(tdma_guide_scan_pair_index(&table, 3u, 2u) == 11u);
    assert(tdma_guide_scan_pair_index(&table, 2u, 2u) == UINT32_MAX);
    assert(tdma_guide_scan_pair_index(&table, 0u, 4u) == UINT32_MAX);

    tdma_guide_scan_table_t eight;
    assert(tdma_guide_scan_init(&eight, 8u));
    assert(eight.pair_count == 56u);
    assert(tdma_guide_scan_pair_at(&eight, 55u, &driver, &receiver));
    assert(driver == 7u && receiver == 6u);

    assert(!tdma_guide_scan_init(&table, 1u));
    assert(!tdma_guide_scan_init(&table, 9u));
    assert(!tdma_guide_scan_init(NULL, 4u));
    assert(table.pair_count == 12u); /* rejected init leaves the table alone */
}

static void test_roles(void)
{
    tdma_guide_scan_table_t table;
    assert(tdma_guide_scan_init(&table, 4u));
    const uint32_t pair_0_1 = tdma_guide_scan_pair_index(&table, 0u, 1u);
    assert(tdma_guide_scan_role(&table, pair_0_1, 0u) ==
           TDMA_GUIDE_SCAN_ROLE_DRIVER);
    assert(tdma_guide_scan_role(&table, pair_0_1, 1u) ==
           TDMA_GUIDE_SCAN_ROLE_RECEIVER);
    assert(tdma_guide_scan_role(&table, pair_0_1, 2u) ==
           TDMA_GUIDE_SCAN_ROLE_IDLE);
    assert(tdma_guide_scan_role(&table, 12u, 0u) ==
           TDMA_GUIDE_SCAN_ROLE_IDLE);

    uint32_t slot = 0xffu;
    assert(tdma_guide_scan_pair_slot(TDMA_GUIDE_SCAN_ROLE_DRIVER, &slot) &&
           slot == 0u);
    assert(tdma_guide_scan_pair_slot(TDMA_GUIDE_SCAN_ROLE_RECEIVER, &slot) &&
           slot == 1u);
    assert(!tdma_guide_scan_pair_slot(TDMA_GUIDE_SCAN_ROLE_IDLE, &slot));
    assert(!tdma_guide_scan_pair_slot(TDMA_GUIDE_SCAN_ROLE_DRIVER, NULL));
}

static void test_accepted_ring(void)
{
    tdma_guide_scan_table_t table;
    assert(tdma_guide_scan_init(&table, 4u));
    arm_pair(&table, 0u, 1u);
    arm_pair(&table, 1u, 2u);
    arm_pair(&table, 2u, 3u);
    arm_pair(&table, 3u, 0u);
    assert(tdma_guide_scan_observed_count(&table) == 4u);
    assert(!tdma_guide_scan_complete(&table));

    uint8_t order[TDMA_GUIDE_SCAN_NODE_MAX] = {0u};
    uint32_t length = 0u;
    assert(tdma_guide_scan_aggregate(&table, 0u, order,
                                     TDMA_GUIDE_SCAN_NODE_MAX, &length) ==
           TDMA_GUIDE_SCAN_VERDICT_ACCEPTED);
    assert(length == 4u);
    assert(memcmp(order, (const uint8_t[]){0u, 1u, 2u, 3u}, 4u) == 0);

    /* Anchor rotates the walk without changing the wiring. */
    assert(tdma_guide_scan_aggregate(&table, 2u, order,
                                     TDMA_GUIDE_SCAN_NODE_MAX, &length) ==
           TDMA_GUIDE_SCAN_VERDICT_ACCEPTED);
    assert(memcmp(order, (const uint8_t[]){2u, 3u, 0u, 1u}, 4u) == 0);
}

static void test_reverse_ring(void)
{
    tdma_guide_scan_table_t table;
    assert(tdma_guide_scan_init(&table, 4u));
    arm_pair(&table, 0u, 3u);
    arm_pair(&table, 3u, 2u);
    arm_pair(&table, 2u, 1u);
    arm_pair(&table, 1u, 0u);

    uint8_t order[TDMA_GUIDE_SCAN_NODE_MAX] = {0u};
    uint32_t length = 0u;
    assert(tdma_guide_scan_aggregate(&table, 0u, order,
                                     TDMA_GUIDE_SCAN_NODE_MAX, &length) ==
           TDMA_GUIDE_SCAN_VERDICT_ACCEPTED);
    assert(memcmp(order, (const uint8_t[]){0u, 3u, 2u, 1u}, 4u) == 0);
}

static void test_rejections(void)
{
    tdma_guide_scan_table_t table;
    uint8_t order[TDMA_GUIDE_SCAN_NODE_MAX] = {0u};

    assert(tdma_guide_scan_init(&table, 4u));
    assert(verdict_of(&table, 0u, order) ==
           TDMA_GUIDE_SCAN_VERDICT_NO_ACTIVITY);

    arm_pair(&table, 0u, 1u);
    arm_pair(&table, 1u, 2u);
    arm_pair(&table, 2u, 3u);
    assert(verdict_of(&table, 0u, order) ==
           TDMA_GUIDE_SCAN_VERDICT_OPEN_CHAIN);

    assert(tdma_guide_scan_init(&table, 4u));
    arm_pair(&table, 0u, 1u);
    arm_pair(&table, 0u, 2u);
    arm_pair(&table, 1u, 3u);
    arm_pair(&table, 2u, 3u);
    arm_pair(&table, 3u, 0u);
    assert(verdict_of(&table, 0u, order) ==
           TDMA_GUIDE_SCAN_VERDICT_BRANCH);

    assert(tdma_guide_scan_init(&table, 4u));
    arm_pair(&table, 0u, 1u);
    arm_pair(&table, 1u, 0u);
    arm_pair(&table, 2u, 3u);
    arm_pair(&table, 3u, 2u);
    const tdma_guide_scan_verdict_t split = verdict_of(&table, 0u, order);
    assert(split == TDMA_GUIDE_SCAN_VERDICT_DUPLICATE_EDGE ||
           split == TDMA_GUIDE_SCAN_VERDICT_MULTI_LOOP);

    /* Three-node cycle with a stranded fourth node. */
    assert(tdma_guide_scan_init(&table, 4u));
    arm_pair(&table, 0u, 1u);
    arm_pair(&table, 1u, 2u);
    arm_pair(&table, 2u, 0u);
    arm_pair(&table, 3u, 1u);
    arm_pair(&table, 0u, 3u);
    const tdma_guide_scan_verdict_t stranded = verdict_of(&table, 0u, order);
    assert(stranded == TDMA_GUIDE_SCAN_VERDICT_BRANCH ||
           stranded == TDMA_GUIDE_SCAN_VERDICT_DUPLICATE_EDGE ||
           stranded == TDMA_GUIDE_SCAN_VERDICT_MULTI_LOOP);
}

static void test_record_semantics(void)
{
    tdma_guide_scan_table_t table;
    assert(tdma_guide_scan_init(&table, 2u));
    assert(table.pair_count == 2u);
    assert(tdma_guide_scan_record(&table, 0u, true));
    assert(!tdma_guide_scan_record(&table, 0u, true)); /* idempotent */
    assert(!tdma_guide_scan_record(&table, 0u, false)); /* no erase */
    assert(tdma_guide_scan_observed_count(&table) == 1u);
    assert(!tdma_guide_scan_complete(&table));
    assert(tdma_guide_scan_record(&table, 1u, true));
    assert(tdma_guide_scan_complete(&table));
    assert(!tdma_guide_scan_record(&table, 2u, true)); /* out of range */
}

static void test_arguments(void)
{
    tdma_guide_scan_table_t table;
    assert(tdma_guide_scan_init(&table, 4u));
    uint8_t order[TDMA_GUIDE_SCAN_NODE_MAX] = {0u};
    uint32_t length = 99u;
    assert(tdma_guide_scan_aggregate(&table, 4u, order,
                                     TDMA_GUIDE_SCAN_NODE_MAX, &length) ==
           TDMA_GUIDE_SCAN_VERDICT_ARGUMENT);
    assert(length == 0u);
    assert(tdma_guide_scan_aggregate(&table, 0u, order, 3u, &length) ==
           TDMA_GUIDE_SCAN_VERDICT_ARGUMENT);
    assert(tdma_guide_scan_aggregate(&table, 0u, NULL,
                                     TDMA_GUIDE_SCAN_NODE_MAX, &length) ==
           TDMA_GUIDE_SCAN_VERDICT_ARGUMENT);
    assert(tdma_guide_scan_aggregate(&table, 0u, order,
                                     TDMA_GUIDE_SCAN_NODE_MAX, NULL) ==
           TDMA_GUIDE_SCAN_VERDICT_ARGUMENT);
    assert(strcmp(tdma_guide_scan_verdict_name(
                      TDMA_GUIDE_SCAN_VERDICT_ACCEPTED), "ACCEPTED") == 0);
}

int main(void)
{
    test_pair_table();
    test_roles();
    test_accepted_ring();
    test_reverse_ring();
    test_rejections();
    test_record_semantics();
    test_arguments();
    printf("tdma guide scan: 7 case groups passed\n");
    return 0;
}
