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
    const refmem_read_key_t *entry = refmem_read_catalog_find((uint8_t)result->key);
    if (entry == NULL) {
        return REFMEM_OBS_ERR_KEY;
    }
    if (result->count == 0u || result->count > REFMEM_OBS_MAX_ROWS ||
        result->offset > 0xFFFFu - result->count) {
        return REFMEM_OBS_ERR_OFFSET;
    }
    const uint32_t schema_words = entry->schema_words;

    int written = snprintf(out, cap, "%lu,%lu,%lu",
                           (unsigned long)result->key,
                           (unsigned long)result->offset,
                           (unsigned long)result->count);
    if (written < 0 || (size_t)written >= cap) {
        return REFMEM_OBS_ERR_ARG;
    }
    size_t used = (size_t)written;

    for (uint32_t i = 0u; i < result->count; i++) {
        const refmem_obs_row_t *row = &result->rows[i];
        if (row->slot != result->offset + i) {
            return REFMEM_OBS_ERR_OFFSET;
        }
        const uint32_t valid = row->valid != 0u ? 1u : 0u;
        const uint32_t age = valid != 0u ? row->age_ms : REFMEM_OBS_AGE_INVALID;
        const uint32_t stale = valid != 0u ? (row->stale != 0u ? 1u : 0u) : 1u;
        written = snprintf(out + used, cap - used, ",%lu,%lu,%lu,%lu,%lu,%lu",
                           (unsigned long)row->slot, (unsigned long)valid,
                           (unsigned long)row->epoch, (unsigned long)row->seq,
                           (unsigned long)age, (unsigned long)stale);
        if (written < 0 || (size_t)written >= cap - used) {
            return REFMEM_OBS_ERR_ARG;
        }
        used += (size_t)written;
        for (uint32_t word = 0u; word < schema_words; word++) {
            const uint32_t value = (valid != 0u && word < row->row_words)
                                       ? row->row[word]
                                       : 0u;
            written = snprintf(out + used, cap - used, ",%lu",
                               (unsigned long)value);
            if (written < 0 || (size_t)written >= cap - used) {
                return REFMEM_OBS_ERR_ARG;
            }
            used += (size_t)written;
        }
    }
    return REFMEM_OBS_OK;
}
