/* Host test: REFMEM observation page encoder (REFMEM-READ-01 / M4). */
#include "refmem_obs.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static refmem_obs_result_t ring_page(void)
{
    refmem_obs_result_t page;
    memset(&page, 0, sizeof(page));
    page.key = 6u;      /* ring_status: six schema words */
    page.offset = 0u;
    page.count = 2u;
    page.rows[0].slot = 0u;
    page.rows[0].valid = 1u;
    page.rows[0].epoch = 1u;
    page.rows[0].seq = 7u;
    page.rows[0].age_ms = 17u;
    page.rows[0].stale = 0u;
    page.rows[0].row_words = 6u;
    page.rows[0].row[0] = 1u;
    page.rows[0].row[1] = 1u;
    page.rows[0].row[2] = 1u;
    page.rows[0].row[3] = 1u;
    page.rows[0].row[4] = 4u;
    page.rows[0].row[5] = 0u;
    page.rows[1].slot = 1u;  /* peer slot: no publisher on this board */
    page.rows[1].valid = 0u;
    return page;
}

int main(void)
{
    char out[512];

    /* Two rows: local valid + peer stale; invalid row forces age/stale. */
    refmem_obs_result_t page = ring_page();
    page.rows[1].age_ms = 123u;
    page.rows[1].stale = 0u;
    assert(refmem_obs_encode(&page, out, sizeof(out)) == REFMEM_OBS_OK);
    assert(strcmp(out,
                  "6,0,2,0,1,1,7,17,0,1,1,1,1,4,0,"
                  "1,0,0,0,4294967295,1,0,0,0,0,0,0") == 0);

    /* Short row is zero padded to the schema width. */
    refmem_obs_result_t short_row = ring_page();
    short_row.count = 1u;
    short_row.rows[0].row_words = 2u;
    short_row.rows[0].row[0] = 9u;
    short_row.rows[0].row[1] = 8u;
    assert(refmem_obs_encode(&short_row, out, sizeof(out)) == REFMEM_OBS_OK);
    assert(strcmp(out, "6,0,1,0,1,1,7,17,0,9,8,0,0,0,0") == 0);

    /* Unknown key, bad count, slot mismatch and short buffers fail closed. */
    refmem_obs_result_t bad_key = ring_page();
    bad_key.key = 99u;
    assert(refmem_obs_encode(&bad_key, out, sizeof(out)) == REFMEM_OBS_ERR_KEY);
    refmem_obs_result_t bad_count = ring_page();
    bad_count.count = 0u;
    assert(refmem_obs_encode(&bad_count, out, sizeof(out)) ==
           REFMEM_OBS_ERR_OFFSET);
    refmem_obs_result_t bad_slot = ring_page();
    bad_slot.rows[1].slot = 5u;
    assert(refmem_obs_encode(&bad_slot, out, sizeof(out)) ==
           REFMEM_OBS_ERR_OFFSET);
    assert(refmem_obs_encode(&page, NULL, sizeof(out)) == REFMEM_OBS_ERR_ARG);
    assert(refmem_obs_encode(&page, out, 4u) == REFMEM_OBS_ERR_ARG);

    /* Schema widths come from the generated catalog. */
    assert(refmem_obs_schema_words(1u) == 2u);
    assert(refmem_obs_schema_words(4u) == 7u);
    assert(refmem_obs_schema_words(8u) == 4u);
    assert(refmem_obs_schema_words(0u) == 0u);

    printf("refmem obs host tests passed\n");
    return 0;
}
