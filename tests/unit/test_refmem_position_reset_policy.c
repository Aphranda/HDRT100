#include "distributed_refmem.h"

#include <stdio.h>

static int expect(const char *name, int actual, int expected)
{
    if ((actual != 0) != (expected != 0)) {
        (void)printf("%s: expected %d got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

int main(void)
{
    int failed = 0;

    /* stopped ring: no running reset. */
    failed += expect("stopped", refmem_position_reset_admits(0u, 0u, 7u, 7u), 0);
    /* enabled but adapter not started. */
    failed += expect("adapter down", refmem_position_reset_admits(1u, 0u, 7u, 7u), 0);
    /* configuration not applied yet. */
    failed += expect("config pending", refmem_position_reset_admits(1u, 1u, 8u, 7u), 0);
    /* running and applied. */
    failed += expect("running", refmem_position_reset_admits(1u, 1u, 7u, 7u), 1);

    if (failed != 0) {
        return 1;
    }
    (void)printf("refmem_position_reset_policy tests passed\n");
    return 0;
}
