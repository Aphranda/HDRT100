#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vdc_dpll_manager.h"
#include "refmem_vdc_vector.h"

static vdc_domain_snapshot_t s_published_snapshot;
static uint32_t s_published_snapshot_guard;
static bool s_published_snapshot_valid;
static unsigned loads, interfere;

static uint32_t snapshot_load(const uint32_t *value, int order)
{
    (void)order;
    ++loads;
    if ((loads & 1u) == 0u && (interfere == 2u || (interfere == 1u && loads == 2u))) {
        s_published_snapshot_guard += 2u;
        ++s_published_snapshot.service_count;
    }
    return *value;
}

#define __atomic_load_n snapshot_load
#define VDC_DPLL_MANAGER_TIME_CRITICAL(name) name
#define DISTRIBUTED_REFMEM_TIME_CRITICAL(name) name
#include "projection.inc"

static void fixture(unsigned test)
{
    unsigned char *bytes = (unsigned char *)&s_published_snapshot;
    uint32_t seed = 0x7312905u + test;
    for (size_t i=0; i<sizeof(s_published_snapshot); ++i) {
        seed = seed*1664525u + 1013904223u;
        bytes[i] = (unsigned char)(seed >> 24u);
    }
    s_published_snapshot.schedule.enabled = (test >> 0u) & 1u;
    s_published_snapshot.path_delay.valid = (test >> 1u) & 1u;
    s_published_snapshot.path_delay.flags = test;
    s_published_snapshot.dpll.debug_continue_enabled = (test >> 2u) & 1u;
    s_published_snapshot.dpll.state = VDC_DOMAIN_LOCK_LOCKED;
    s_published_snapshot.quality.last_timestamp_source = VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK;
    s_published_snapshot.quality.last_timestamp_resolution_ns = test ? test : 0u;
    s_published_snapshot.quality.last_timestamp_flags = (test >> 3u) & 3u;
    s_published_snapshot_guard = 2u;
    s_published_snapshot_valid = true;
    loads = interfere = 0u;
}

int main(void)
{
    vdc_dpll_manager_vector_snapshot_t projected;
    printf("{\"vectors\":[");
    for (unsigned test=0; test<32; ++test) {
        fixture(test);
        assert(vdc_dpll_manager_get_vector_snapshot(&projected));
        refmem_vdc_vector_payload_t vdc;
        refmem_dpll_vector_payload_t dpll;
        distributed_refmem_fill_vdc_vector_payload(&vdc, &projected, UINT32_MAX-test, true);
        distributed_refmem_fill_dpll_vector_payload(&dpll, &projected, UINT32_MAX-test, true);
        vdc.stable_sequence = dpll.stable_sequence = 2u+test*2u;
        const uint32_t a = refmem_vdc_vector_payload_crc(&vdc);
        const uint32_t b = refmem_dpll_vector_payload_crc(&dpll);
        vdc.payload_crc32=a;
        dpll.payload_crc32=b;
        assert(refmem_vdc_vector_payload_validate(&vdc));
        assert(refmem_dpll_vector_payload_validate(&dpll));
        /* The randomized quality bytes are not a health-qualified lock.
         * Check the actual payload first, then restore ONLY the historical
         * LOCKED bit for the unchanged legacy-byte golden. Independent
         * publication tests cover healthy/aged/recovered qualification. */
        assert(!(vdc.flags & REFMEM_VECTOR_FLAG_LOCKED));
        assert(!(dpll.flags & REFMEM_VECTOR_FLAG_LOCKED));
        if (!(projected.path_delay.flags & VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY) &&
            projected.dpll.debug_continue_enabled == 0u) {
            vdc.flags |= REFMEM_VECTOR_FLAG_LOCKED;
            dpll.flags |= REFMEM_VECTOR_FLAG_LOCKED;
        }
        const uint32_t legacy_a = refmem_vdc_vector_payload_crc(&vdc);
        const uint32_t legacy_b = refmem_dpll_vector_payload_crc(&dpll);
        printf("%s[%u,%u]", test ? "," : "", legacy_a, legacy_b);
    }
    /* Bounded read rejection, including a writer at the guard recheck and
     * generation wrap. Retrying must recopy the new generation's payload. */
    fixture(0);
    assert(!vdc_dpll_manager_get_vector_snapshot(NULL));
    assert(loads==0u);
    s_published_snapshot_valid=false;
    assert(!vdc_dpll_manager_get_vector_snapshot(&projected));
    assert(loads==2u);
    fixture(0);
    s_published_snapshot_guard=3u;
    memset(&projected,0xa5,sizeof(projected));
    assert(!vdc_dpll_manager_get_vector_snapshot(&projected));
    assert(loads==8u && projected.service_count==0xa5a5a5a5u);
    fixture(0);
    s_published_snapshot_guard=UINT32_MAX-1u;
    interfere=1u;
    assert(vdc_dpll_manager_get_vector_snapshot(&projected));
    assert(loads==4u && s_published_snapshot_guard==0u);
    assert(projected.service_count==s_published_snapshot.service_count);
    fixture(0);
    interfere=2u;
    assert(!vdc_dpll_manager_get_vector_snapshot(&projected));
    assert(loads==16u);
    printf("],\"rejection_checks\":5,\"projected_bytes\":%zu,\"domain_bytes\":%zu}\n",
           sizeof(projected),sizeof(s_published_snapshot));
    return 0;
}
