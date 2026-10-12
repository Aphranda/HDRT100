"""G5: the origin's FREEZE-abandonment branch, driven directly.

Three attempts to reach this branch through the whole-runtime driver failed: the
origin keeps re-solving, so a position's window is continuously extended, and
freezing the sample stream makes the runtime report an unrelated service error
before any FREEZE is staged. The branch itself is a small self-contained
decision, so it is extracted and driven here instead - the same technique the
seat's other facts use. Until this file existed, `freeze_abandoned_count` had no
test that produced it.
"""
import re
import subprocess

from test_trigger_grid_preview import ROOT, compile_grid
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

SOURCE = ROOT / "components/sync_trigger/src/trigger_t2_runtime.c"

HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "trigger_t2_runtime.h"
#include "trigger_position_descriptor.h"

/* The runtime's own storage, reproduced with the same names so the extracted
 * body compiles unchanged. */
static trigger_t2_runtime_snapshot_t s_state;
static trigger_t2_runtime_config_t s_config;
static uint64_t s_freeze_last_slice;
static uint32_t s_freeze_resend_next;

static uint32_t s_acked;
static bool s_stage_ok = true;
static uint32_t s_ack_calls, s_stage_calls;
static trigger_position_descriptor_t s_staged[8];

static uint32_t s_ack_mask;
uint32_t distributed_refmem_position_freeze_acked(uint32_t run, uint32_t mask)
{
    (void)run; ++s_ack_calls; s_ack_mask = mask; return s_acked;
}
bool distributed_refmem_stage_position_descriptor(
    const trigger_position_descriptor_t *descriptor, uint32_t mask)
{
    (void)mask;
    if (s_stage_calls < 8u) s_staged[s_stage_calls] = *descriptor;
    ++s_stage_calls;
    return s_stage_ok;
}

/* The pending-ack diagnostic is a different owner's fact; this harness only
 * checks that the runtime refreshes what it reports. */
static uint32_t s_pending_stale = 0x4u, s_pending_incomplete = 0x2u;
void distributed_refmem_position_ack_pending(uint32_t run_id, uint32_t target_mask,
                                             uint32_t *stale_mask,
                                             uint32_t *incomplete_mask)
{
    (void)run_id; (void)target_mask;
    if (stale_mask) *stale_mask = s_pending_stale;
    if (incomplete_mask) *incomplete_mask = s_pending_incomplete;
}

'''

MAIN = r'''
static void reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(&s_config, 0, sizeof(s_config));
    s_config.run_id = 0x1234u;
    s_config.target_mask = 0x5u;
    s_freeze_last_slice = 0u;
    s_freeze_resend_next = 0u;
    s_acked = 0u;
    s_ack_mask = 0u;
    s_stage_ok = true;
    s_ack_calls = 0u;
    s_stage_calls = 0u;
}

int main(void)
{
    assert(TRIGGER_T2_RUNTIME_POSITION_COUNT >= 2u);

    /* Nothing published: no work, no counters. */
    reset();
    assert(resend_one_freeze());
    assert(!s_ack_calls && !s_stage_calls && s_state.freeze_abandoned_count == 0u);
    /* Nothing waits for completion: no pending ack is reported. */
    assert(s_state.freeze_ack_stale_mask == 0u && s_state.freeze_ack_incomplete_mask == 0u);

    /* Nothing acknowledges the FREEZE and the promised window has closed: the
     * position is abandoned - the per-position mask AND the surviving count -
     * and it is not staged again. */
    reset();
    s_state.published_mask = 1u;                 /* position 1 promised */
    s_state.descriptors[0].expiry_sync = 10u;
    s_state.cursor.pulse_index = 20u;            /* > expiry + 1 */
    assert(resend_one_freeze());
    assert(s_state.freeze_abandoned_mask == 1u);
    assert(s_state.freeze_abandoned_count == 1u);
    assert(!s_stage_calls);
    assert(s_state.freeze_resend_count == 0u);
    assert(!(s_state.freeze_complete_mask & 1u));

    /* The count saturates instead of wrapping. */
    reset();
    s_state.published_mask = 1u;
    s_state.descriptors[0].expiry_sync = 10u;
    s_state.cursor.pulse_index = 20u;
    s_state.freeze_abandoned_count = UINT32_MAX;
    assert(resend_one_freeze());
    assert(s_state.freeze_abandoned_count == UINT32_MAX);
    assert(s_state.freeze_abandoned_mask == 1u);

    /* A position still inside its window is RESENT, once, and the cadence
     * advances so the next slice does not stage it again. */
    reset();
    s_state.published_mask = 2u;                 /* position 2 promised */
    s_state.descriptors[1].expiry_sync = 100u;
    s_state.freeze_version[1] = 7u;
    s_state.cursor.pulse_index = 30u;
    assert(resend_one_freeze());
    assert(s_state.freeze_resend_count == 1u);
    assert(s_state.freeze_abandoned_count == 0u);
    /* The laggard facts are refreshed from the owner and land in the snapshot. */
    assert(s_state.freeze_ack_stale_mask == s_pending_stale);
    assert(s_state.freeze_ack_incomplete_mask == s_pending_incomplete);
    assert(s_stage_calls == 1u);
    assert(s_staged[0].stage == TRIGGER_POSITION_STAGE_FREEZE);
    assert(s_staged[0].position_id == 2u);
    assert(s_staged[0].prediction_version == 7u);
    assert(s_freeze_last_slice == 30u);

    /* The cadence gate: too soon after the last resend, nothing is staged. */
    s_state.cursor.pulse_index = 30u + T2_FREEZE_RESEND_SLICES - 1u;
    assert(resend_one_freeze());
    assert(s_stage_calls == 1u);
    assert(s_state.freeze_resend_count == 1u);

    /* Acknowledgement completes the position without abandoning it. */
    reset();
    s_state.published_mask = 1u;
    s_state.descriptors[0].expiry_sync = 1u;      /* long past ... */
    s_state.cursor.pulse_index = 50u;             /* ... but it is acknowledged */
    s_acked = 1u;
    assert(resend_one_freeze());
    assert(s_state.freeze_complete_mask == 1u);
    assert(s_state.freeze_abandoned_count == 0u);
    assert(!s_stage_calls);

    /* A staging refusal is a transport fact, not an abandonment. */
    reset();
    s_state.published_mask = 1u;
    s_state.descriptors[0].expiry_sync = 100u;
    s_state.cursor.pulse_index = 30u;
    s_stage_ok = false;
    assert(!resend_one_freeze());
    assert(s_state.transport_reject_count == 1u);
    assert(s_state.last_error == TRIGGER_T2_RUNTIME_TRANSPORT);
    assert(s_state.freeze_abandoned_count == 0u);
    return wait_on_peers_only();
}

/* A freeze target mask that carries the POSITION SOURCE SLOT can never be
 * acknowledged: the acknowledgement path only ever fills a slot from a received
 * image, so waiting for it means resending every position until its window
 * closes. The origin must wait on its peers only. */
static int wait_on_peers_only(void)
{
    reset();
    s_config.source_slot = 1u;
    s_config.target_mask = 0x6u;                 /* slots 1 and 2: 1 is ourselves */
    s_state.published_mask = 1u;
    s_state.descriptors[0].expiry_sync = 100u;
    s_state.cursor.pulse_index = 30u;
    s_acked = 0x1u;                              /* the peer has frozen it */
    assert(resend_one_freeze());
    assert(s_state.freeze_ack_own_in_mask == 1u);
    assert(s_ack_mask == 0x4u);                  /* waited on the peer, not itself */
    assert(s_state.freeze_complete_mask == 1u);
    assert(s_state.freeze_resend_count == 0u);   /* no blind resend at all */
    assert(s_state.freeze_abandoned_count == 0u);

    /* A mask with no peer is complete by construction: a single-board bench must
     * not turn into a resend storm either. */
    reset();
    s_config.source_slot = 1u;
    s_config.target_mask = 0x2u;                 /* only ourselves */
    s_state.published_mask = 1u;
    s_state.descriptors[0].expiry_sync = 100u;
    s_state.cursor.pulse_index = 30u;
    assert(resend_one_freeze());
    assert(s_state.freeze_ack_own_in_mask == 1u);
    assert(!s_ack_calls);                        /* nothing to ask */
    assert(s_state.freeze_complete_mask == 1u);
    assert(s_state.freeze_resend_count == 0u);
    assert(s_state.freeze_ack_stale_mask == 0u && s_state.freeze_ack_incomplete_mask == 0u);

    /* The ordinary bench layout (mask excludes the source slot) is unchanged. */
    reset();
    s_config.source_slot = 0u;
    s_config.target_mask = 0xEu;
    s_state.published_mask = 1u;
    s_state.descriptors[0].expiry_sync = 100u;
    s_state.cursor.pulse_index = 30u;
    s_acked = 0x1u;
    assert(resend_one_freeze());
    assert(s_state.freeze_ack_own_in_mask == 0u);
    assert(s_ack_mask == 0xEu);
    return 0;
}
'''


def _build(tmp_path):
    source = SOURCE.read_text(encoding="utf-8")
    resend = re.search(r"#define T2_FREEZE_RESEND_SLICES (\d+)u", source)
    assert resend, "T2_FREEZE_RESEND_SLICES not found in the runtime source"
    body = c_definition_body(source, "resend_one_freeze")
    definition = ("#define T2_FREEZE_RESEND_SLICES " + resend.group(1) + "u\n"
                  "static bool resend_one_freeze(void) {" + body + "}\n")
    # `main` calls the peer-only cases that are defined below it in MAIN.
    prototype = "static int wait_on_peers_only(void);\n"
    return compile_grid(tmp_path, "t2-freeze-abandon",
                        HARNESS + definition + prototype + MAIN)


def test_freeze_abandonment_is_driven_and_counted(tmp_path):
    executable = _build(tmp_path)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
