/* Host test: generated REFMEM read catalog (contract REFMEM-READ-01). */
#include "refmem_read_catalog.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    assert(refmem_read_catalog_count == 8u);
    assert(refmem_read_catalog != NULL);

    const char *const names[] = {
        "switch1", "switch2", "trigger_state", "guide_plan",
        "boot_plan", "ring_status", "cal_status", "node_health",
    };
    /* Exact row schema per key: the generator can mis-spell a word without
     * changing the count or the id, so the names are pinned here instead of
     * only checking that they are non-empty. */
    const char *const schemas[][7] = {
        {"value", "desired"},
        {"value", "desired"},
        {"state", "run_state", "epoch"},
        {"action", "gen", "status", "trials_done", "passed", "failed", "abort_site"},
        {"action", "fail_step", "boot_done", "half_ring", "profile_source"},
        {"enabled", "up", "down", "adapter", "nodes", "local_slot"},
        {"persisted_valid", "generation", "reject_reason"},
        {"state", "heartbeat", "stale_count", "fault_code"},
    };
    const uint32_t expected_words[] = {2u, 2u, 3u, 7u, 5u, 6u, 3u, 4u};

    for (uint32_t i = 0u; i < refmem_read_catalog_count; i++) {
        const refmem_read_key_t *key = &refmem_read_catalog[i];
        assert(key->id == (uint8_t)(i + 1u));
        assert(key->name != NULL && strcmp(key->name, names[i]) == 0);
        assert(key->writer <= 6u);
        assert(key->schema_words == expected_words[i]);
        assert(key->schema_words <= REFMEM_READ_CATALOG_MAX_SCHEMA_WORDS);
        assert(key->schema != NULL);
        for (uint32_t w = 0u; w < key->schema_words; w++) {
            assert(key->schema[w] != NULL);
            assert(strcmp(key->schema[w], schemas[i][w]) == 0);
        }
        assert(refmem_read_catalog_find(key->id) == key);
    }
    assert(refmem_read_catalog_find(0u) == NULL);
    assert(refmem_read_catalog_find(9u) == NULL);

    printf("refmem read catalog host tests passed (%u keys)\n",
           refmem_read_catalog_count);
    return 0;
}
