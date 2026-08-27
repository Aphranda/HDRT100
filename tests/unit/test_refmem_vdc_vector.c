#include "refmem_vector_table.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int expect_true(const char *name, int value)
{
    if (!value) {
        (void)printf("%s: expected true\n", name);
        return 1;
    }
    return 0;
}

static int expect_false(const char *name, int value)
{
    if (value) {
        (void)printf("%s: expected false\n", name);
        return 1;
    }
    return 0;
}

static void make_vdc_payload(refmem_vdc_vector_payload_t *payload)
{
    (void)memset(payload, 0, sizeof(*payload));
    payload->layout_version = REFMEM_VDC_VECTOR_LAYOUT_VERSION;
    payload->writer = REFMEM_VECTOR_WRITER_CORE1;
    payload->flags = REFMEM_VECTOR_FLAG_VALID;
    payload->stable_sequence = 2u;
    payload->publish_sequence = 1u;
    payload->source_update_seq = 7u;
    payload->payload_crc32 = refmem_vdc_vector_payload_crc(payload);
}

static void make_dpll_payload(refmem_dpll_vector_payload_t *payload)
{
    (void)memset(payload, 0, sizeof(*payload));
    payload->layout_version = REFMEM_DPLL_VECTOR_LAYOUT_VERSION;
    payload->writer = REFMEM_VECTOR_WRITER_CORE1;
    payload->flags = REFMEM_VECTOR_FLAG_VALID;
    payload->stable_sequence = 2u;
    payload->publish_sequence = 1u;
    payload->source_update_seq = 7u;
    payload->payload_crc32 = refmem_dpll_vector_payload_crc(payload);
}

static int test_layout_sizes(void)
{
    int failed = 0;
    failed += expect_true("VDC region is fixed 2 KiB",
                          sizeof(refmem_vdc_vector_region_t) ==
                              DISTRIBUTED_REFMEM_VDC_SIZE);
    failed += expect_true("DPLL region is fixed 2 KiB",
                          sizeof(refmem_dpll_vector_region_t) ==
                              DISTRIBUTED_REFMEM_DPLL_SIZE);
    failed += expect_true("VDC payload is naturally aligned",
                          offsetof(refmem_vdc_vector_region_t, payload) %
                                  _Alignof(refmem_vdc_vector_payload_t) ==
                              0u);
    failed += expect_true("DPLL payload is naturally aligned",
                          offsetof(refmem_dpll_vector_region_t, payload) %
                                  _Alignof(refmem_dpll_vector_payload_t) ==
                              0u);
    failed += expect_true("table is fixed 64 KiB",
                          sizeof(refmem_vector_table_t) ==
                              DISTRIBUTED_REFMEM_TABLE_SIZE);
    return failed;
}

static int test_vdc_validation(void)
{
    int failed = 0;
    refmem_vdc_vector_payload_t payload;
    make_vdc_payload(&payload);
    failed += expect_true("VDC payload validates",
                          refmem_vdc_vector_payload_validate(&payload));

    payload.stable_sequence = 3u;
    payload.payload_crc32 = refmem_vdc_vector_payload_crc(&payload);
    failed += expect_false("odd VDC sequence rejected",
                           refmem_vdc_vector_payload_validate(&payload));
    make_vdc_payload(&payload);
    payload.flags |= REFMEM_VECTOR_FLAG_STALE;
    payload.payload_crc32 = refmem_vdc_vector_payload_crc(&payload);
    failed += expect_false("stale VDC payload rejected",
                           refmem_vdc_vector_payload_validate(&payload));
    make_vdc_payload(&payload);
    payload.source_update_seq++;
    failed += expect_false("bad VDC CRC rejected",
                           refmem_vdc_vector_payload_validate(&payload));
    make_vdc_payload(&payload);
    payload.writer = 0u;
    payload.payload_crc32 = refmem_vdc_vector_payload_crc(&payload);
    failed += expect_false("wrong VDC writer rejected",
                           refmem_vdc_vector_payload_validate(&payload));
    return failed;
}

static int test_dpll_validation(void)
{
    int failed = 0;
    refmem_dpll_vector_payload_t payload;
    make_dpll_payload(&payload);
    failed += expect_true("DPLL payload validates",
                          refmem_dpll_vector_payload_validate(&payload));

    payload.stable_sequence = 0u;
    payload.payload_crc32 = refmem_dpll_vector_payload_crc(&payload);
    failed += expect_false("zero DPLL sequence rejected",
                           refmem_dpll_vector_payload_validate(&payload));
    make_dpll_payload(&payload);
    payload.flags &= ~REFMEM_VECTOR_FLAG_VALID;
    payload.payload_crc32 = refmem_dpll_vector_payload_crc(&payload);
    failed += expect_false("invalid DPLL payload rejected",
                           refmem_dpll_vector_payload_validate(&payload));
    make_dpll_payload(&payload);
    payload.path_delay_generation++;
    failed += expect_false("bad DPLL CRC rejected",
                           refmem_dpll_vector_payload_validate(&payload));
    return failed;
}

int main(void)
{
    int failed = 0;
    failed += test_layout_sizes();
    failed += test_vdc_validation();
    failed += test_dpll_validation();
    if (failed != 0) {
        (void)printf("refmem_vdc_vector tests failed: %d\n", failed);
        return 1;
    }
    (void)printf("refmem_vdc_vector tests passed\n");
    return 0;
}
