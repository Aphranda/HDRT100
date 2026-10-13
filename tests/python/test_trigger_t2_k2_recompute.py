"""K-2: the two deadline-recompute predicates, driven directly.

`prepare_deadlines_core0()` cannot be driven through the whole-runtime harness
(the DCO model, the deadline mailbox and the frozen-public mask are all in the
way), so the decision it makes about one position was extracted into two small
pure functions and they are driven here - the same extraction technique
test_trigger_t2_freeze_abandon.py uses. Until this file existed, the K-2 rule had
no test that produced it.

The rule these encode: a position whose deadline the hardware owner already
accepted (`action_armed_mask | action_completed_mask`) is never re-planned, while
a position that is merely frozen but not yet accepted must still be recomputed
when the prediction version or the model token moves.
"""
import subprocess

from test_trigger_grid_preview import ROOT, compile_grid
from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

SOURCE = ROOT / "components/sync_trigger/src/trigger_t2_runtime.c"

HEAD = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include "trigger_t2_runtime.h"
'''

MAIN = r'''
int main(void)
{
    const uint32_t last = TRIGGER_T2_RUNTIME_POSITION_COUNT - 1u;
    assert(TRIGGER_T2_RUNTIME_POSITION_COUNT >= 2u);

    /* C1: a position the owner already accepted is skipped, at any index. */
    assert(t2_deadline_position_accepted(0x1u, 0u));
    assert(t2_deadline_position_accepted(0x1u << 3, 3u));
    assert(t2_deadline_position_accepted(1u << last, last));

    /* C2: a position nobody accepted is planned. */
    assert(!t2_deadline_position_accepted(0x0u, 0u));
    assert(!t2_deadline_position_accepted(0x1u, 1u));
    assert(!t2_deadline_position_accepted(0u, last));

    /* C3: the caller combines the owner's two facts (armed | completed) and
     * either one alone is enough to skip. */
    assert(t2_deadline_position_accepted(0x2u | 0x0u, 1u));
    assert(t2_deadline_position_accepted(0x0u | 0x2u, 1u));

    /* C4: a neighbour's bit never suppresses this position. */
    assert(!t2_deadline_position_accepted(0x1u, 2u));

    /* C5: an unchanged prediction version and model token need no recompute. */
    assert(t2_deadline_plan_current(7u, 0xABCDu, 7u, 0xABCDu));

    /* C6: a new prediction version, or a new model token, forces one. */
    assert(!t2_deadline_plan_current(7u, 0xABCDu, 8u, 0xABCDu));
    assert(!t2_deadline_plan_current(7u, 0xABCDu, 7u, 0xABCEu));
    assert(!t2_deadline_plan_current(7u, 0xABCDu, 8u, 0xABCEu));
    return 0;
}
'''


def _build(tmp_path):
    source = SOURCE.read_text(encoding="utf-8")
    accepted = c_definition_body(source, "t2_deadline_position_accepted")
    current = c_definition_body(source, "t2_deadline_plan_current")
    definition = (
        "static bool t2_deadline_position_accepted(uint32_t accepted_mask, uint32_t index) {"
        + accepted + "}\n"
        "static bool t2_deadline_plan_current(uint32_t previous_version, uint32_t previous_token, "
        "uint32_t descriptor_version, uint32_t model_token) {" + current + "}\n")
    return compile_grid(tmp_path, "t2-k2-recompute", HEAD + definition + MAIN)


def test_k2_recompute_predicates(tmp_path):
    executable = _build(tmp_path)
    result = subprocess.run([str(executable)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
