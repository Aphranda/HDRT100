/* Host test: REFMEM observation encoder (REFTMEM-READ-01 / R1-4). */
#include "refmem_obs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    char out[128];

    /* Valid row: ring_status (key 6) has six schema words. */
    refmem_obs_result_t ring = {
        .key = 6u,
        .offset = 1u,
        .count = 2u,
        .valid = 1u,
        .age_ms = 17u,
        .row_words = 6u,
        .row = {1u, 1u, 1u, 1u, 4u, 0u},
    };
    assert(refmem_obs_encode(&ring, out, sizeof(out)) == REFMEM_OBS_OK);
    assert(strcmp(out, "6,1,2,1,17,1,1,1,1,4,0") == 0);

    /* Invalid row: no local snapshot keeps the fixed width but reports age
     * 0xFFFFFFFF and zeroes the row. */
    refmem_obs_result_t stale = ring;
    stale.valid = 0u;
    stale.age_ms = 999u;
    assert(refmem_obs_encode(&stale, out, sizeof(out)) == REFMEM_OBS_OK);
    assert(strcmp(out, "6,1,2,0,4294967295,0,0,0,0,0,0") == 0);

    /* A short row is zero padded to the schema width. */
    refmem_obs_result_t short_row = ring;
    short_row.row_words = 2u;
    short_row.row[0] = 7u;
    short_row.row[1] = 8u;
    assert(refmem_obs_encode(&short_row, out, sizeof(out)) == REFMEM_OBS_OK);
    assert(strcmp(out, "6,1,2,1,17,7,8,0,0,0,0") == 0);

    /* Unknown key and bad paging fail closed. */
    refmem_obs_result_t bad_key = ring;
    bad_key.key = 99u;
    assert(refmem_obs_encode(&bad_key, out, sizeof(out)) == REFMEM_OBS_ERR_KEY);
    refmem_obs_result_t bad_count = ring;
    bad_count.count = 0u;
    assert(refmem_obs_encode(&bad_count, out, sizeof(out)) ==
           REFMEM_OBS_ERR_OFFSET);
    assert(refmem_obs_encode(&ring, NULL, sizeof(out)) == REFMEM_OBS_ERR_ARG);
    assert(refmem_obs_encode(&ring, out, 4u) == REFMEM_OBS_ERR_ARG);

    /* Schema widths come from the generated catalog. */
    assert(refmem_obs_schema_words(1u) == 2u);
    assert(refmem_obs_schema_words(4u) == 7u);
    assert(refmem_obs_schema_words(8u) == 4u);
    assert(refmem_obs_schema_words(0u) == 0u);

    printf("refmem obs host tests passed\n");
    return 0;
}
