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
    for (uint32_t i = 0u; i < refmem_read_catalog_count; i++) {
        const refmem_read_key_t *key = &refmem_read_catalog[i];
        assert(key->id == (uint8_t)(i + 1u));
        assert(key->name != NULL && strcmp(key->name, names[i]) == 0);
        assert(key->writer <= 6u);
        assert(key->schema_words >= 1u);
        assert(key->schema_words <= REFMEM_READ_CATALOG_MAX_SCHEMA_WORDS);
        assert(key->schema != NULL);
        for (uint32_t w = 0u; w < key->schema_words; w++) {
            assert(key->schema[w] != NULL && key->schema[w][0] != '\0');
        }
        assert(refmem_read_catalog_find(key->id) == key);
    }
    assert(refmem_read_catalog_find(0u) == NULL);
    assert(refmem_read_catalog_find(9u) == NULL);

    printf("refmem read catalog host tests passed (%u keys)\n",
           refmem_read_catalog_count);
    return 0;
}
