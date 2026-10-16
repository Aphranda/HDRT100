"""T2-FREEZE-002 (review A-13): a pending position without a saved PREVIEW must be
recorded and must not stop the other positions from being staged.

Before this change `stage_pending_freeze()` returned on the first pending position
whose `preview_copy()` failed, so every later pending position was never examined
and the loss left no trace at all (no counter, no mask). The branch is small and
self-contained, so - like the seat's other facts - it is extracted from the real
source and driven directly instead of through the whole runtime.

Cases:
  1. position 1 has no PREVIEW, position 2 does: position 2 is still staged, the
     missing one is named in `freeze_no_preview_mask` and counted once.
  2. all three positions lack a PREVIEW: nothing is staged, the mask names all
     three, and repeated passes do not double-count any of them.
  3. the healthy baseline is unchanged: a position with a PREVIEW stages as before
     and reports no no-preview facts.
"""
import subprocess

from test_trigger_grid_preview import ROOT, compile_grid

SOURCE = ROOT / "components/sync_trigger/src/trigger_t2_runtime.c"

HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "trigger_t2_runtime.h"
#include "trigger_position_descriptor.h"
#include "trigger_position_window.h"

/* The runtime's own storage, reproduced with the same names so the extracted
 * body compiles unchanged. */
static trigger_t2_runtime_snapshot_t s_state;
static trigger_t2_runtime_config_t s_config;
static uint32_t s_pending_mask;
static uint64_t s_freeze_last_slice;
/* Window bookkeeping and the scan-slot guard the extracted body reads. */
static trigger_position_window_t s_position_window;
static int32_t s_scan_step_mdeg;
static uint64_t s_slot_target[TRIGGER_T2_RUNTIME_POSITION_COUNT];

static uint32_t s_preview_mask = 0u;
static uint64_t s_preview_target;
static uint32_t s_preview_calls;
static bool s_stage_ok = true;
static uint32_t s_stage_calls;
static trigger_position_descriptor_t s_staged[8];

/* PREVIEW availability is the fact under test: a position is "saved" only when its
 * bit is in s_preview_mask. */
static bool preview_copy(uint32_t index, uint32_t age,
                         trigger_position_descriptor_t *out)
{
    (void)age;
    ++s_preview_calls;
    if ((s_preview_mask & (1u << index)) == 0u) return false;
    memset(out, 0, sizeof(*out));
    out->schema = TRIGGER_POSITION_DESCRIPTOR_SCHEMA;
    out->position_id = index + 1u;
    out->prediction_version = 100u + index;
    out->target_count = s_preview_target;
    return true;
}

bool distributed_refmem_stage_position_descriptor(
    const trigger_position_descriptor_t *descriptor, uint32_t mask)
{
    (void)mask;
    if (s_stage_calls < 8u) s_staged[s_stage_calls] = *descriptor;
    ++s_stage_calls;
    return s_stage_ok;
}

'''

MAIN = r'''
static void reset(void)
{
    memset(&s_state, 0, sizeof(s_state));
    memset(&s_config, 0, sizeof(s_config));
    s_config.target_mask = 0xEu;
    s_preview_mask = 0u;
    s_preview_target = 0u;
    s_preview_calls = 0u;
    s_stage_ok = true;
    s_stage_calls = 0u;
    s_freeze_last_slice = 0u;
    s_pending_mask = 0u;
    s_scan_step_mdeg = 0;
    memset(s_slot_target, 0, sizeof(s_slot_target));
    trigger_position_window_reset(&s_position_window);
}

int main(void)
{
    assert(TRIGGER_T2_RUNTIME_POSITION_COUNT >= 3u);

    /* 1. The A-13 shape: the first pending position has no PREVIEW, the next one
     *    does. The next one must still be staged - that is the whole point - and
     *    the missing one must be recorded exactly once. */
    reset();
    s_pending_mask = 0x3u;                        /* positions 1 and 2 pending */
    s_preview_mask = 0x2u;                        /* only position 2 has a preview */
    assert(stage_pending_freeze());
    assert(s_stage_calls == 1u);
    assert(s_staged[0].stage == TRIGGER_POSITION_STAGE_FREEZE);
    assert(s_staged[0].position_id == 2u);
    assert(s_staged[0].prediction_version == 101u);
    assert(s_state.freeze_no_preview_mask == 0x1u);
    assert(s_state.freeze_no_preview_count == 1u);
    /* The staged position is cleared, the starved one stays pending so a preview
     * that still arrives can be staged, and it is not frozen/published. */
    assert(s_pending_mask == 0x1u);
    assert(!(s_state.frozen_mask & 0x1u) && (s_state.frozen_mask & 0x2u));
    assert(!(s_state.published_mask & 0x1u) && (s_state.published_mask & 0x2u));

    /* 2. Every pending position lacks a PREVIEW: nothing is staged, all three are
     *    named, and each is counted once no matter how often the pass runs. */
    reset();
    s_pending_mask = 0x7u;
    s_preview_mask = 0u;
    assert(stage_pending_freeze());
    assert(s_stage_calls == 0u);
    assert(s_state.freeze_no_preview_mask == 0x7u);
    assert(s_state.freeze_no_preview_count == 3u);
    assert(s_pending_mask == 0x7u);               /* nothing silently dropped */
    assert(stage_pending_freeze());
    assert(stage_pending_freeze());
    assert(s_state.freeze_no_preview_count == 3u);/* idempotent per position */

    /* 3. Healthy baseline unchanged: a position with a PREVIEW stages as before
     *    and leaves no no-preview facts behind. */
    reset();
    s_pending_mask = 0x4u;                        /* position 3 pending */
    s_preview_mask = 0x4u;
    assert(stage_pending_freeze());
    assert(s_stage_calls == 1u);
    assert(s_staged[0].position_id == 3u);
    assert(s_state.freeze_no_preview_mask == 0u);
    assert(s_state.freeze_no_preview_count == 0u);
    assert(s_pending_mask == 0u);
    assert(s_state.frozen_mask == 0x4u);

    /* 4. A staging refusal stays a transport fact and still surfaces the first
     *    stageable position rather than being swallowed by the no-preview path. */
    reset();
    s_pending_mask = 0x6u;
    s_preview_mask = 0x6u;
    s_stage_ok = false;
    assert(!stage_pending_freeze());
    assert(s_state.transport_reject_count == 1u);
    assert(s_state.last_error == TRIGGER_T2_RUNTIME_TRANSPORT);
    assert(s_state.freeze_no_preview_count == 0u);

    /* 5. Scan slots are reused: with a scan step a saved PREVIEW only counts when its
     *    target is the one awaiting the FREEZE. An earlier occupant's PREVIEW (a
     *    different target_count) is "no PREVIEW", the pending bit stays, and the
     *    matching one stages normally. */
    reset();
    s_scan_step_mdeg = 1000;
    s_pending_mask = 0x1u;
    s_preview_mask = 0x1u;
    s_slot_target[0] = 777u;                      /* preview_copy() reports target_count 0 */
    assert(stage_pending_freeze());
    assert(s_stage_calls == 0u);
    assert(s_state.freeze_no_preview_mask == 0x1u && s_pending_mask == 0x1u);
    s_preview_target = 777u;
    assert(stage_pending_freeze());
    assert(s_stage_calls == 1u && s_pending_mask == 0u);
    return 0;
}
'''


def _macro_definition_body(source: str, name: str) -> str:
    """Body of a runtime function written as `T2_HOT(name)(void) { ... }`.

    The shared extractor looks for `name(` and therefore cannot see these: the
    runtime marks its hot functions through the T2_HOT macro (they must run from
    SRAM), so the name sits inside the macro's parentheses.
    """
    marker = f"T2_HOT({name})(void)"
    start = source.index(marker)
    open_brace = source.index("{", start)
    depth = 0
    for index in range(open_brace, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[open_brace + 1:index]
    raise AssertionError(f"unterminated body for {name}")


def _build(tmp_path):
    source = SOURCE.read_text(encoding="utf-8")
    body = _macro_definition_body(source, "stage_pending_freeze")
    definition = "static bool stage_pending_freeze(void) {" + body + "}\n"
    return compile_grid(tmp_path, "t2-freeze-no-preview", HARNESS + definition + MAIN)


def test_no_preview_positions_are_counted_and_do_not_stop_the_pass(tmp_path):
    executable = _build(tmp_path)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
