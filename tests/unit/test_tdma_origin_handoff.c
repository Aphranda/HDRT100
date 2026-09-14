#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tdma_origin_handoff.h"

int main(void)
{
    tdma_origin_handoff_t record = {0};
    tdma_origin_handoff_snapshot_t out;
    assert(!tdma_origin_handoff_get(NULL, &out));
    assert(!tdma_origin_handoff_get(&record, NULL));
    assert(!tdma_origin_handoff_get(&record, &out));
    tdma_origin_handoff_begin(NULL, 2, 4, 250000000, 0);
    tdma_origin_handoff_record(NULL, 1, 0, 1, TDMA_ORIGIN_BUILD_DONE);
    tdma_origin_handoff_begin(&record, 0, 4, 250000000, 0);
    assert(!tdma_origin_handoff_get(&record, &out));
    tdma_origin_handoff_begin(&record, 2, 4, 0, 0);
    assert(!tdma_origin_handoff_get(&record, &out));

    /* Cross the low timer word without losing the wall interval. Calls in
     * one stage accumulate CPU work but preserve the first entry time. */
    const uint64_t base = UINT32_MAX - 50ull;
    tdma_origin_handoff_begin(&record, 2, 4, 250000000, base);
    tdma_origin_handoff_record(&record, TDMA_ORIGIN_PREPARE_MAILBOX, base+100, base+120, TDMA_ORIGIN_BUILD_BUSY);
    tdma_origin_handoff_record(&record, TDMA_ORIGIN_PREPARE_MAILBOX, base+1000, base+1010, TDMA_ORIGIN_BUILD_BUSY);
    tdma_origin_handoff_record(&record, TDMA_ORIGIN_PREPARE_INSTALL, base+9000, base+9050, TDMA_ORIGIN_BUILD_DONE);
    assert(tdma_origin_handoff_get(&record, &out));
    assert(out.trial_epoch == 2 && out.config_seq == 4 && out.clock_hz == 250000000);
    assert(out.result == TDMA_ORIGIN_BUILD_DONE && !out.invalid_count && out.elapsed_ticks == 9050);
    assert(out.first_ticks[TDMA_ORIGIN_PREPARE_MAILBOX] == 100 && out.work_ticks[TDMA_ORIGIN_PREPARE_MAILBOX] == 30);
    assert(out.calls[TDMA_ORIGIN_PREPARE_MAILBOX] == 2 && out.calls[TDMA_ORIGIN_PREPARE_SEED] == 0);
    const tdma_origin_handoff_snapshot_t frozen = out;
    tdma_origin_handoff_record(&record, TDMA_ORIGIN_PREPARE_FAILED, base+9999, base+10000, TDMA_ORIGIN_BUILD_FAILED);
    assert(tdma_origin_handoff_get(&record, &out) && !memcmp(&frozen, &out, sizeof(out)));
    record.guard++;
    assert(!tdma_origin_handoff_get(&record, &out));
    record.guard++;

    tdma_origin_handoff_begin(&record, 6, 8, 150000000, 1000);
    tdma_origin_handoff_record(&record, TDMA_ORIGIN_PREPARE_BUILD_STEP, 1200, 1201, TDMA_ORIGIN_BUILD_FAILED);
    assert(tdma_origin_handoff_get(&record, &out) && out.result == TDMA_ORIGIN_BUILD_FAILED);
    assert(out.trial_epoch == 6 && out.config_seq == 8 && out.calls[TDMA_ORIGIN_PREPARE_MAILBOX] == 0);
    assert(out.elapsed_ticks == 201 && out.work_ticks[TDMA_ORIGIN_PREPARE_BUILD_STEP] == 1);
    for (unsigned bad = 0; bad < 5; ++bad) {
        tdma_origin_handoff_begin(&record, 6, 8, 150000000, 1000);
        tdma_origin_handoff_record(&record, bad == 0 ? TDMA_ORIGIN_HANDOFF_STAGES : 1u,
            bad == 1 ? 999 : 1100, bad == 2 ? 1099 : bad == 3 ? UINT64_MAX : 1200,
            bad == 4 ? (tdma_origin_build_result_t)-1 : TDMA_ORIGIN_BUILD_BUSY);
        assert(tdma_origin_handoff_get(&record, &out) && out.invalid_count != 0);
    }
    tdma_origin_handoff_begin(&record, 10, 12, 150000000, 0);
    tdma_origin_handoff_record(&record, 1, 10, 20, TDMA_ORIGIN_BUILD_BUSY);
    tdma_origin_handoff_record(&record, 2, 19, 25, TDMA_ORIGIN_BUILD_DONE);
    assert(tdma_origin_handoff_get(&record, &out) && out.invalid_count == 1);
    tdma_origin_handoff_begin(&record, 10, 12, 150000000, 0);
    record.snapshot.calls[1] = UINT16_MAX;
    record.snapshot.work_ticks[1] = UINT32_MAX - 1;
    tdma_origin_handoff_record(&record, 1, 1, 5, TDMA_ORIGIN_BUILD_DONE);
    assert(tdma_origin_handoff_get(&record, &out) && out.invalid_count == 2);
    assert(out.calls[1] == UINT16_MAX && out.work_ticks[1] == UINT32_MAX);
    puts("origin handoff: stage work/wait, identities, wrap, guard, freeze and invalid intervals passed");
    return 0;
}
