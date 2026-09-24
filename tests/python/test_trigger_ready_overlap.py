"""READY evidence is checked against the preceding action window and next action."""
import subprocess

import pytest

from test_trigger_grid_preview import compile_grid


HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "trigger_ready_overlap.h"

static void check(const char *name, trigger_ready_overlap_result_t expected,
                  uint64_t observed, int ready_observed,
                  uint64_t next_action_q, uint32_t next_span)
{
    (void)name;
    trigger_ready_overlap_report_t report;
    const trigger_ready_overlap_result_t result = trigger_ready_overlap_check(
        42u, 100u, 10u, 5, 10u, ready_observed != 0, observed,
        next_action_q, next_span, &report);
    assert(result == expected);
    assert(report.position_id == 42u);
    assert(report.expected_start_q == 105u);
    assert(report.expected_end_q == 115u);
    assert(report.observed_q == observed);
    assert(report.result == expected);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "ok")) {
        check(argv[1], TRIGGER_READY_OVERLAP_OK, 110u, 1, 200u, 4u);
    } else if (!strcmp(argv[1], "missing")) {
        check(argv[1], TRIGGER_READY_OVERLAP_MISSING, 0u, 0, 200u, 4u);
    } else if (!strcmp(argv[1], "early")) {
        check(argv[1], TRIGGER_READY_OVERLAP_EARLY, 104u, 1, 200u, 4u);
    } else if (!strcmp(argv[1], "late")) {
        check(argv[1], TRIGGER_READY_OVERLAP_LATE, 115u, 1, 200u, 4u);
    } else if (!strcmp(argv[1], "conflict")) {
        check(argv[1], TRIGGER_READY_OVERLAP_ACTION_CONFLICT, 111u, 1, 110u, 4u);
    } else if (!strcmp(argv[1], "negative")) {
        trigger_ready_overlap_report_t report;
        assert(trigger_ready_overlap_check(7u, 100u, 10u, -5, 10u, true, 96u,
            200u, 4u, &report) == TRIGGER_READY_OVERLAP_OK);
        assert(report.expected_start_q == 95u && report.expected_end_q == 105u);
    } else if (!strcmp(argv[1], "underflow")) {
        trigger_ready_overlap_report_t report;
        assert(trigger_ready_overlap_check(7u, 3u, 1u, -4, 1u, true, 0u,
            20u, 1u, &report) == TRIGGER_READY_OVERLAP_OVERFLOW);
    } else if (!strcmp(argv[1], "next-overflow")) {
        trigger_ready_overlap_report_t report;
        assert(trigger_ready_overlap_check(7u, 100u, 1u, 0, 1u, true, 100u,
            UINT64_MAX, 2u, &report) == TRIGGER_READY_OVERLAP_OVERFLOW);
    } else if (!strcmp(argv[1], "invalid")) {
        assert(trigger_ready_overlap_check(7u, 100u, 1u, 0, 0u, true, 100u,
            20u, 1u, NULL) == TRIGGER_READY_OVERLAP_OVERFLOW);
    } else {
        return 2;
    }
    return 0;
}
'''


@pytest.fixture(scope="module")
def ready(tmp_path_factory):
    return compile_grid(tmp_path_factory.mktemp("ready-overlap"), "ready", HARNESS)


@pytest.mark.parametrize(
    "case", ["ok", "missing", "early", "late", "conflict", "negative",
              "underflow", "next-overflow", "invalid"],
)
def test_ready_overlap(ready, case):
    result = subprocess.run([str(ready), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
