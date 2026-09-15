"""Interrupt the real compact clock getter with real configuration updates.

Only atomic-load observation is intercepted. The actual runtime configure and
service paths publish STOP and its ACK; no adapter, PIO or physical timing is
claimed by these host interleavings.
"""
import subprocess

import pytest

from test_vdc_command_owner import ROOT, compile_executable


def build_clock_snapshot(directory, production_root=ROOT):
    runtime = production_root / "components/tdma/src/tdma_ring_runtime.c"
    header = production_root / "components/tdma/inc/tdma_ring_runtime.h"
    # Include the frozen header first when repeating the pre-fix counterexample.
    harness = '#include "' + header.as_posix() + '"\n' + r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
static tdma_ring_runtime_t runtime;
static unsigned mode, result_reads, mutations;
static bool observing;
static uint32_t observed_load(const volatile uint32_t *value, int order)
{
    if (observing && value == &runtime.result_guard) {
        ++result_reads;
        const bool once = !mutations && result_reads == mode;
        const bool every_attempt = mode == 3 && (result_reads & 1u);
        if (once || every_attempt) {
            observing = false;
            /* A real STOP configuration and Core1 acknowledgement arrive
             * after the getter copied configuration, or after its result. */
            assert(tdma_ring_runtime_configure(&runtime, NULL));
            tdma_ring_runtime_service(&runtime);
            ++mutations;
            observing = true;
        }
    }
    return __atomic_load_n(value, order);
}
#define __atomic_load_n observed_load
''' + '#include "' + runtime.as_posix() + '"\n' + r'''
#undef __atomic_load_n
int main(int argc, char **argv)
{
    assert(argc == 2);
    assert(tdma_ring_runtime_init(&runtime));
    assert(tdma_ring_runtime_configure(&runtime, NULL));
    tdma_ring_runtime_service(&runtime);
    tdma_ring_clock_snapshot_t snapshot;
    assert(tdma_ring_runtime_get_clock_snapshot(&runtime, &snapshot));
    assert(snapshot.config_seq == runtime.applied_config_seq);
    if (!strcmp(argv[1], "before_result")) mode = 1;
    else if (!strcmp(argv[1], "after_result")) mode = 2;
    else if (!strcmp(argv[1], "continuous_config")) mode = 3;
    else if (!strcmp(argv[1], "busy_config")) runtime.config_guard |= 1u;
    else if (!strcmp(argv[1], "busy_result")) runtime.result_guard |= 1u;
    else assert(!"Unknown scenario");
    observing = true;
    const bool copied = tdma_ring_runtime_get_clock_snapshot(&runtime, &snapshot);
    observing = false;
    printf("copied=%u snapshot_config=%u current_config=%u mutations=%u reads=%u\n",
        copied, snapshot.config_seq, runtime.config_seq, mutations, result_reads);
    fflush(stdout);
    if (mode == 1 || mode == 2) {
        assert(mutations == 1 && copied);
        assert(snapshot.config_seq == runtime.config_seq);
    } else {
        assert(!copied);
        assert(result_reads <= 2u * TDMA_RING_RUNTIME_SNAPSHOT_RETRY_LIMIT);
        if (mode == 3) assert(mutations == TDMA_RING_RUNTIME_SNAPSHOT_RETRY_LIMIT);
    }
    /* Odd guards model in-progress writers; complete them and retry. */
    runtime.config_guard += runtime.config_guard & 1u;
    runtime.result_guard += runtime.result_guard & 1u;
    assert(tdma_ring_runtime_get_clock_snapshot(&runtime, &snapshot));
    assert(snapshot.config_seq == runtime.config_seq);
    return 0;
}
'''
    return compile_executable(directory, "clock_snapshot", harness)


@pytest.fixture(scope="module")
def clock_snapshot_executable(tmp_path_factory):
    return build_clock_snapshot(tmp_path_factory.mktemp("clock-snapshot"))


@pytest.mark.parametrize("scenario", [
    "before_result", "after_result", "continuous_config", "busy_config", "busy_result",
])
def test_clock_snapshot_configuration_interleaving(clock_snapshot_executable, scenario):
    result = subprocess.run([str(clock_snapshot_executable), scenario],
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
