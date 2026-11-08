#ifndef REFMEM_OBS_H
#define REFMEM_OBS_H

#include <stddef.h>
#include <stdint.h>

/* REFMEM-READ-01: local-mirror observation rows (contract
 * docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md, key table
 * config/refmem/read_catalog.yaml).
 *
 * The read plane never performs a synchronous cross-board read: the caller
 * fills one row from its own local snapshot (or marks it invalid), and this
 * module only turns it into the wire text
 *
 *     key,offset,count,valid,age_ms,row...
 *
 * The row is truncated/padded to the key's schema_words so a host paging by
 * offset/count always sees a fixed-width row. */

#define REFMEM_OBS_MAX_ROW_WORDS 8u
#define REFMEM_OBS_AGE_INVALID 0xFFFFFFFFu

typedef enum {
    REFMEM_OBS_OK = 0,
    REFMEM_OBS_ERR_KEY = 1,    /* unknown or out-of-range key id */
    REFMEM_OBS_ERR_OFFSET = 2, /* offset outside the slot space */
    REFMEM_OBS_ERR_ARG = 3,    /* null buffer / zero capacity */
} refmem_obs_status_t;

typedef struct {
    uint8_t key;
    uint16_t offset;
    uint16_t count;
    uint8_t valid; /* 0: no local snapshot; age_ms is then REFMEM_OBS_AGE_INVALID */
    uint32_t age_ms;
    uint32_t row_words; /* number of row words provided by the caller */
    uint32_t row[REFMEM_OBS_MAX_ROW_WORDS];
} refmem_obs_result_t;

/* Encode one observation row. Returns REFMEM_OBS_OK and writes a NUL-terminated
 * string into out (at least cap bytes). The schema width is taken from the
 * generated read catalog; a valid row shorter than the schema is zero padded
 * and a longer row is truncated, so the field count never depends on the value
 * path that produced it. */
refmem_obs_status_t refmem_obs_encode(const refmem_obs_result_t *result,
                                      char *out, size_t cap);

/* Number of schema words for a key id, 0 when the key is unknown. */
uint32_t refmem_obs_schema_words(uint8_t key);

#endif /* REFMEM_OBS_H */
