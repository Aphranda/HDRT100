#include "refmem_obs.h"

#include <stdio.h>
#include <string.h>

#include "refmem_read_catalog.h"

uint32_t refmem_obs_schema_words(uint8_t key)
{
    const refmem_read_key_t *entry = refmem_read_catalog_find(key);
    return entry != NULL ? entry->schema_words : 0u;
}

refmem_obs_status_t refmem_obs_encode(const refmem_obs_result_t *result,
                                      char *out, size_t cap)
{
    if (result == NULL || out == NULL || cap == 0u) {
        return REFMEM_OBS_ERR_ARG;
    }
    const refmem_read_key_t *entry = refmem_read_catalog_find(result->key);
    if (entry == NULL) {
        return REFMEM_OBS_ERR_KEY;
    }
    if (result->count == 0u || result->offset > 0xFFFFu - result->count) {
        return REFMEM_OBS_ERR_OFFSET;
    }
    const uint32_t schema_words = entry->schema_words;
    const uint8_t valid = result->valid != 0u ? 1u : 0u;
    const uint32_t age = valid != 0u ? result->age_ms : REFMEM_OBS_AGE_INVALID;

    int written = snprintf(out, cap, "%u,%u,%u,%u,%lu",
                           (unsigned)result->key, (unsigned)result->offset,
                           (unsigned)result->count, (unsigned)valid,
                           (unsigned long)age);
    if (written < 0 || (size_t)written >= cap) {
        return REFMEM_OBS_ERR_ARG;
    }
    size_t used = (size_t)written;
    for (uint32_t word = 0u; word < schema_words; word++) {
        const uint32_t value = (valid != 0u && word < result->row_words)
                                   ? result->row[word]
                                   : 0u;
        written = snprintf(out + used, cap - used, ",%lu",
                           (unsigned long)value);
        if (written < 0 || (size_t)written >= cap - used) {
            return REFMEM_OBS_ERR_ARG;
        }
        used += (size_t)written;
    }
    return REFMEM_OBS_OK;
}
