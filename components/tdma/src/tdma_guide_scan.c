#include "tdma_guide_scan.h"

static bool table_valid(const tdma_guide_scan_table_t *table)
{
    return table != NULL &&
           table->node_count >= TDMA_GUIDE_SCAN_NODE_MIN &&
           table->node_count <= TDMA_GUIDE_SCAN_NODE_MAX &&
           table->pair_count ==
               table->node_count * (table->node_count - 1u);
}

bool tdma_guide_scan_init(tdma_guide_scan_table_t *table, uint32_t node_count)
{
    if (table == NULL || node_count < TDMA_GUIDE_SCAN_NODE_MIN ||
        node_count > TDMA_GUIDE_SCAN_NODE_MAX) {
        return false;
    }
    table->node_count = node_count;
    table->pair_count = node_count * (node_count - 1u);
    table->activity_mask = 0u;
    return true;
}

bool tdma_guide_scan_pair_at(const tdma_guide_scan_table_t *table,
                             uint32_t index,
                             uint32_t *driver,
                             uint32_t *receiver)
{
    if (!table_valid(table) || driver == NULL || receiver == NULL ||
        index >= table->pair_count) {
        return false;
    }
    uint32_t position = 0u;
    for (uint32_t candidate_driver = 0u;
         candidate_driver < table->node_count;
         candidate_driver++) {
        for (uint32_t candidate_receiver = 0u;
             candidate_receiver < table->node_count;
             candidate_receiver++) {
            if (candidate_receiver == candidate_driver) {
                continue;
            }
            if (position == index) {
                *driver = candidate_driver;
                *receiver = candidate_receiver;
                return true;
            }
            position++;
        }
    }
    return false;
}

uint32_t tdma_guide_scan_pair_index(const tdma_guide_scan_table_t *table,
                                    uint32_t driver,
                                    uint32_t receiver)
{
    if (!table_valid(table) || driver == receiver ||
        driver >= table->node_count || receiver >= table->node_count) {
        return UINT32_MAX;
    }
    /* Pairs are ordered driver-major, so each driver owns a contiguous run of
     * node_count-1 entries. */
    const uint32_t per_driver = table->node_count - 1u;
    uint32_t index = driver * per_driver;
    for (uint32_t slot = 0u; slot < table->node_count; slot++) {
        if (slot == driver) {
            continue;
        }
        if (slot == receiver) {
            return index;
        }
        index++;
    }
    return UINT32_MAX;
}

tdma_guide_scan_role_t tdma_guide_scan_role(
    const tdma_guide_scan_table_t *table, uint32_t index, uint32_t local_slot)
{
    uint32_t driver = 0u;
    uint32_t receiver = 0u;
    if (!tdma_guide_scan_pair_at(table, index, &driver, &receiver)) {
        return TDMA_GUIDE_SCAN_ROLE_IDLE;
    }
    if (local_slot == driver) {
        return TDMA_GUIDE_SCAN_ROLE_DRIVER;
    }
    if (local_slot == receiver) {
        return TDMA_GUIDE_SCAN_ROLE_RECEIVER;
    }
    return TDMA_GUIDE_SCAN_ROLE_IDLE;
}

bool tdma_guide_scan_pair_slot(tdma_guide_scan_role_t role, uint32_t *slot)
{
    if (slot == NULL) {
        return false;
    }
    if (role == TDMA_GUIDE_SCAN_ROLE_DRIVER) {
        *slot = 0u;
        return true;
    }
    if (role == TDMA_GUIDE_SCAN_ROLE_RECEIVER) {
        *slot = 1u;
        return true;
    }
    return false;
}

bool tdma_guide_scan_record(tdma_guide_scan_table_t *table,
                            uint32_t index,
                            bool observed)
{
    if (!table_valid(table) || index >= table->pair_count || index >= 56u) {
        return false;
    }
    const uint64_t bit = (uint64_t)1u << index;
    if (!observed) {
        /* A negative result is not written: it would erase a positive
         * observation of the same pair, and the first detection is the
         * adjacency fact. */
        return false;
    }
    if ((table->activity_mask & bit) != 0u) {
        return false;
    }
    table->activity_mask |= bit;
    return true;
}

uint32_t tdma_guide_scan_observed_count(const tdma_guide_scan_table_t *table)
{
    if (!table_valid(table)) {
        return 0u;
    }
    uint64_t mask = table->activity_mask;
    uint32_t count = 0u;
    while (mask != 0u) {
        count += (uint32_t)(mask & 1u);
        mask >>= 1u;
    }
    return count;
}

bool tdma_guide_scan_complete(const tdma_guide_scan_table_t *table)
{
    if (!table_valid(table) || table->pair_count >= 64u) {
        return false;
    }
    const uint64_t expected =
        ((uint64_t)1u << table->pair_count) - (uint64_t)1u;
    return (table->activity_mask & expected) == expected;
}

tdma_guide_scan_verdict_t tdma_guide_scan_aggregate(
    const tdma_guide_scan_table_t *table,
    uint32_t anchor,
    uint8_t *ring_order,
    size_t capacity,
    uint32_t *out_len)
{
    if (out_len != NULL) {
        *out_len = 0u;
    }
    if (!table_valid(table) || ring_order == NULL || out_len == NULL ||
        capacity < table->node_count || anchor >= table->node_count) {
        return TDMA_GUIDE_SCAN_VERDICT_ARGUMENT;
    }
    if (table->activity_mask == 0u) {
        return TDMA_GUIDE_SCAN_VERDICT_NO_ACTIVITY;
    }

    uint32_t outgoing[TDMA_GUIDE_SCAN_NODE_MAX] = {0u};
    uint32_t incoming[TDMA_GUIDE_SCAN_NODE_MAX] = {0u};
    for (uint32_t index = 0u; index < table->pair_count; index++) {
        if ((table->activity_mask & ((uint64_t)1u << index)) == 0u) {
            continue;
        }
        uint32_t driver = 0u;
        uint32_t receiver = 0u;
        if (!tdma_guide_scan_pair_at(table, index, &driver, &receiver)) {
            return TDMA_GUIDE_SCAN_VERDICT_ARGUMENT;
        }
        outgoing[driver] |= (uint32_t)1u << receiver;
        incoming[receiver] |= (uint32_t)1u << driver;
    }

    for (uint32_t slot = 0u; slot < table->node_count; slot++) {
        if (outgoing[slot] == 0u) {
            return TDMA_GUIDE_SCAN_VERDICT_OPEN_CHAIN;
        }
        if ((outgoing[slot] & (outgoing[slot] - 1u)) != 0u) {
            return TDMA_GUIDE_SCAN_VERDICT_BRANCH;
        }
    }
    for (uint32_t slot = 0u; slot < table->node_count; slot++) {
        if (incoming[slot] == 0u) {
            return TDMA_GUIDE_SCAN_VERDICT_OPEN_CHAIN;
        }
        if ((incoming[slot] & (incoming[slot] - 1u)) != 0u) {
            return TDMA_GUIDE_SCAN_VERDICT_DUPLICATE_EDGE;
        }
    }

    uint32_t current = anchor;
    for (uint32_t step = 0u; step < table->node_count; step++) {
        for (uint32_t previous = 0u; previous < step; previous++) {
            if (ring_order[previous] == (uint8_t)current) {
                return TDMA_GUIDE_SCAN_VERDICT_MULTI_LOOP;
            }
        }
        ring_order[step] = (uint8_t)current;
        uint32_t next = 0u;
        while ((outgoing[current] & ((uint32_t)1u << next)) == 0u) {
            next++;
            if (next >= table->node_count) {
                return TDMA_GUIDE_SCAN_VERDICT_MULTI_LOOP;
            }
        }
        current = next;
    }
    if (current != anchor) {
        return TDMA_GUIDE_SCAN_VERDICT_MULTI_LOOP;
    }
    *out_len = table->node_count;
    return TDMA_GUIDE_SCAN_VERDICT_ACCEPTED;
}

const char *tdma_guide_scan_verdict_name(tdma_guide_scan_verdict_t verdict)
{
    switch (verdict) {
    case TDMA_GUIDE_SCAN_VERDICT_ACCEPTED:
        return "ACCEPTED";
    case TDMA_GUIDE_SCAN_VERDICT_NO_ACTIVITY:
        return "NO_ACTIVITY";
    case TDMA_GUIDE_SCAN_VERDICT_OPEN_CHAIN:
        return "OPEN_CHAIN";
    case TDMA_GUIDE_SCAN_VERDICT_BRANCH:
        return "BRANCH";
    case TDMA_GUIDE_SCAN_VERDICT_DUPLICATE_EDGE:
        return "DUPLICATE_EDGE";
    case TDMA_GUIDE_SCAN_VERDICT_MULTI_LOOP:
        return "MULTI_LOOP";
    case TDMA_GUIDE_SCAN_VERDICT_INCOMPLETE:
        return "INCOMPLETE";
    case TDMA_GUIDE_SCAN_VERDICT_ARGUMENT:
        return "ARGUMENT";
    case TDMA_GUIDE_SCAN_VERDICT_NONE:
    default:
        return "NONE";
    }
}
