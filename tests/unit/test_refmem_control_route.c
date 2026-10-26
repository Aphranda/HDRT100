#include "refmem_control_route.h"

#include <stdio.h>

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %lu got %lu\n",
                     name,
                     (unsigned long)expected,
                     (unsigned long)actual);
        return 1;
    }
    return 0;
}

int main(void)
{
    int rc = 0;

    rc |= expect_u32("local only",
                     (uint32_t)refmem_control_route_decide(1u, 0x02u, false),
                     (uint32_t)REFMEM_CONTROL_ROUTE_LOCAL);
    rc |= expect_u32("local only with unified",
                     (uint32_t)refmem_control_route_decide(1u, 0x02u, true),
                     (uint32_t)REFMEM_CONTROL_ROUTE_LOCAL);
    rc |= expect_u32("remote unavailable",
                     (uint32_t)refmem_control_route_decide(1u, 0x04u, false),
                     (uint32_t)REFMEM_CONTROL_ROUTE_REJECT_UNIFIED_UNAVAILABLE);
    rc |= expect_u32("mixed unavailable",
                     (uint32_t)refmem_control_route_decide(1u, 0x06u, false),
                     (uint32_t)REFMEM_CONTROL_ROUTE_REJECT_UNIFIED_UNAVAILABLE);
    rc |= expect_u32("remote available",
                     (uint32_t)refmem_control_route_decide(1u, 0x04u, true),
                     (uint32_t)REFMEM_CONTROL_ROUTE_UNIFIED);
    rc |= expect_u32("mixed available",
                     (uint32_t)refmem_control_route_decide(1u, 0x06u, true),
                     (uint32_t)REFMEM_CONTROL_ROUTE_UNIFIED);
    rc |= expect_u32("no target",
                     (uint32_t)refmem_control_route_decide(1u, 0u, true),
                     (uint32_t)REFMEM_CONTROL_ROUTE_REJECT_NO_TARGET);
    rc |= expect_u32("bad slot",
                     (uint32_t)refmem_control_route_decide(32u, 0x01u, true),
                     (uint32_t)REFMEM_CONTROL_ROUTE_REJECT_BAD_SLOT);

    /* N5.2b: config-stage origin authorization. */
    rc |= expect_u32("local origin accepted",
                     (uint32_t)refmem_control_origin_decide(1u, 1u, 0u, true),
                     (uint32_t)REFMEM_CONTROL_ORIGIN_LOCAL);
    rc |= expect_u32("local origin without owner",
                     (uint32_t)refmem_control_origin_decide(1u, 1u, 0u, false),
                     (uint32_t)REFMEM_CONTROL_ORIGIN_LOCAL);
    rc |= expect_u32("coordinator origin accepted",
                     (uint32_t)refmem_control_origin_decide(0u, 1u, 0u, true),
                     (uint32_t)REFMEM_CONTROL_ORIGIN_COORDINATOR);
    rc |= expect_u32("non-coordinator rejected",
                     (uint32_t)refmem_control_origin_decide(2u, 1u, 0u, true),
                     (uint32_t)REFMEM_CONTROL_ORIGIN_REJECT);
    rc |= expect_u32("missing owner rejects remote",
                     (uint32_t)refmem_control_origin_decide(0u, 1u, 0u, false),
                     (uint32_t)REFMEM_CONTROL_ORIGIN_REJECT);
    rc |= expect_u32("out of range origin rejected",
                     (uint32_t)refmem_control_origin_decide(32u, 1u, 0u, true),
                     (uint32_t)REFMEM_CONTROL_ORIGIN_REJECT);

    /* N5.5 entry convergence: only the owner may originate remote commands. */
    rc |= expect_u32("entry local only",
                     (uint32_t)refmem_control_entry_decide(1u, 0u, true, 0x02u),
                     (uint32_t)REFMEM_CONTROL_ENTRY_LOCAL_ONLY);
    rc |= expect_u32("entry owner remote",
                     (uint32_t)refmem_control_entry_decide(0u, 0u, true, 0x02u),
                     (uint32_t)REFMEM_CONTROL_ENTRY_REMOTE_ALLOWED);
    rc |= expect_u32("entry non-owner remote rejected",
                     (uint32_t)refmem_control_entry_decide(2u, 0u, true, 0x02u),
                     (uint32_t)REFMEM_CONTROL_ENTRY_REJECT_REMOTE);
    rc |= expect_u32("entry mixed remote rejected",
                     (uint32_t)refmem_control_entry_decide(1u, 0u, true, 0x06u),
                     (uint32_t)REFMEM_CONTROL_ENTRY_REJECT_REMOTE);
    rc |= expect_u32("entry missing owner rejected",
                     (uint32_t)refmem_control_entry_decide(0u, 0u, false, 0x02u),
                     (uint32_t)REFMEM_CONTROL_ENTRY_REJECT_REMOTE);
    rc |= expect_u32("entry bad slot rejected",
                     (uint32_t)refmem_control_entry_decide(32u, 0u, true, 0x02u),
                     (uint32_t)REFMEM_CONTROL_ENTRY_REJECT_REMOTE);

    if (rc == 0) {
        (void)printf("refmem_control_route tests passed\n");
    }
    return rc;
}
