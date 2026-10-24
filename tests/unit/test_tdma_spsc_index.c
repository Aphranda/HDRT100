#include "tdma_spsc_index.h"

#include <stdbool.h>
#include <stdio.h>

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n", name,
                     (unsigned long)expected, (unsigned long)actual);
        return 1;
    }
    return 0;
}

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %d got %d\n", name, expected ? 1 : 0,
                     actual ? 1 : 0);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failed = 0;
    const uint32_t cap = 8u;

    failed += expect_u32("next 0", tdma_spsc_next(0u, cap), 1u);
    failed += expect_u32("next 6", tdma_spsc_next(6u, cap), 7u);
    failed += expect_u32("next wraps", tdma_spsc_next(7u, cap), 0u);

    failed += expect_bool("empty at equal", tdma_spsc_empty(3u, 3u), true);
    failed += expect_bool("not empty", tdma_spsc_empty(3u, 4u), false);

    failed += expect_bool("full head=0 tail=7",
                          tdma_spsc_full(0u, 7u, cap), true);
    failed += expect_bool("not full head=0 tail=6",
                          tdma_spsc_full(0u, 6u, cap), false);
    failed += expect_bool("not full head=4 tail=2",
                          tdma_spsc_full(4u, 2u, cap), false);

    failed += expect_u32("depth empty", tdma_spsc_depth(0u, 0u, cap), 0u);
    failed += expect_u32("depth linear", tdma_spsc_depth(0u, 5u, cap), 5u);
    failed += expect_u32("depth wrap", tdma_spsc_depth(5u, 2u, cap), 5u);
    failed += expect_u32("depth full-ish", tdma_spsc_depth(0u, 7u, cap), 7u);
    failed += expect_u32("depth bad capacity", tdma_spsc_depth(0u, 1u, 0u), 0u);
    failed += expect_u32("depth bad index", tdma_spsc_depth(9u, 1u, cap), 0u);

    if (failed != 0) {
        (void)printf("tdma_spsc_index tests failed: %d\n", failed);
        return 1;
    }
    (void)printf("tdma_spsc_index tests passed\n");
    return 0;
}
