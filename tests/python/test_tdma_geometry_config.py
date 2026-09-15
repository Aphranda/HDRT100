"""Exercise the production service/runtime frozen-geometry handoff."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "tdma_service.h"

static tdma_service_service_t owner;
static tdma_ring_runtime_config_t received, replacement;
static unsigned start_calls, stop_calls, replacements;
static bool reject_start, reject_stop, replace_on_geometry_load;

static uint32_t observed_load(const volatile uint32_t *value, int order)
{
    const uint32_t result = __atomic_load_n(value, order);
    if (replace_on_geometry_load && value == &owner.ring_runtime.geometry_generation) {
        replace_on_geometry_load = false;
        ++replacements;
        /* A Core0 publication replaces the tuple between its field reads.
         * The production read_config must retry the entire guarded tuple. */
        assert(tdma_ring_runtime_configure(&owner.ring_runtime, &replacement));
    }
    return result;
}

/* Include complete production code; intercept only runtime atomic loads to
 * reproduce one cross-core publication at a deterministic read boundary. */
#include "components/tdma/src/tdma_service.c"
#define __atomic_load_n observed_load
#include "components/tdma/src/tdma_ring_runtime.c"
#undef __atomic_load_n

static bool adapter_start(void *context, const tdma_ring_runtime_config_t *config)
{
    assert(context == &owner);
    ++start_calls;
    received = *config;
    return !reject_start;
}

static bool adapter_stop(void *context)
{
    assert(context == &owner);
    ++stop_calls;
    return !reject_stop;
}

static bool adapter_service(void *context, uint64_t now, tdma_ring_adapter_status_t *status)
{
    (void)context; (void)now; (void)status;
    return true;
}

static void setup(void)
{
    static const tdma_ring_adapter_ops_t ops = {
        .start = adapter_start, .stop = adapter_stop, .service = adapter_service};
    assert(tdma_service_init(&owner));
    assert(!tdma_service_set_ring_geometry_generation(&owner, 7u));
    tdma_foundation_profile_t profile;
    assert(tdma_foundation_profile_default(&profile, 1u, 0u, 0u, TDMA_ADAPTER_PIO_SPI));
    assert(tdma_service_configure_foundation_profile(&owner, &profile, 7u));
    assert(owner.ring_staged_config.enabled);
    assert(!tdma_service_set_ring_geometry_generation(&owner, 7u));
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(owner.ring_runtime.config_seq == owner.ring_runtime.applied_config_seq);
    assert(tdma_service_bind_ring_adapter(&owner, &ops, &owner));
}

static void finish_stop(void)
{
    assert(tdma_service_ring_stop(&owner));
    tdma_ring_runtime_service(&owner.ring_runtime);
    tdma_service_core0_lifecycle_service(&owner);
    assert(!owner.ring_runtime.adapter_started);
    assert(owner.ring_runtime.config_seq == owner.ring_runtime.applied_config_seq);
}

static void case_one_shot(void)
{
    setup();
    assert(tdma_service_set_ring_geometry_generation(&owner, 42u));
    assert(owner.ring_staged_config.geometry_generation == 42u);
    assert(owner.ring_runtime.geometry_generation == 0u);
    owner.ring_staged_config.owner_config_seq = 0xfedcba98u;
    assert(tdma_service_ring_arm(&owner));
    const uint32_t published = owner.ring_runtime.config_seq;
    assert(owner.ring_runtime.geometry_generation == 42u);
    assert(owner.ring_staged_config.geometry_generation == 0u);
    assert(owner.ring_control_guard == 0u);
    assert(!tdma_service_set_ring_geometry_generation(&owner, 0u));
    assert(!tdma_service_ring_arm(&owner));
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(start_calls == 1u && received.geometry_generation == 42u);
    assert(received.owner_config_seq == published);
    assert(received.owner_config_seq != owner.ring_staged_config.owner_config_seq);
    assert(owner.ring_runtime.applied_config_seq == published);
    assert(owner.ring_runtime.geometry_generation == 42u);
    finish_stop();
    assert(owner.ring_runtime.geometry_generation == 0u);
    assert(tdma_service_ring_arm(&owner));
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(start_calls == 2u && received.geometry_generation == 0u);
    assert(received.owner_config_seq == owner.ring_runtime.config_seq);
    assert(received.owner_config_seq != published);
}

static void case_stop_pending(void)
{
    setup();
    assert(tdma_service_set_ring_geometry_generation(&owner, 11u));
    assert(tdma_service_ring_arm(&owner));
    tdma_ring_runtime_service(&owner.ring_runtime);
    reject_stop = true;
    assert(tdma_service_ring_stop(&owner));
    const uint32_t stop_seq = owner.ring_runtime.config_seq;
    assert(!tdma_service_set_ring_geometry_generation(&owner, 12u));
    for (unsigned retry = 0; retry < 3u; ++retry) {
        tdma_ring_runtime_service(&owner.ring_runtime);
        assert(owner.ring_runtime.adapter_started && owner.ring_runtime.adapter_stop_pending);
        assert(owner.ring_runtime.applied_config_seq != stop_seq);
        assert(!tdma_service_set_ring_geometry_generation(&owner, 12u));
        assert(!tdma_service_set_ring_geometry_generation(&owner, 0u));
        assert(!tdma_service_ring_arm(&owner));
        assert(owner.ring_staged_config.geometry_generation == 0u);
    }
    assert(stop_calls == 3u);
    reject_stop = false;
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(owner.ring_runtime.applied_config_seq == stop_seq);
    assert(tdma_service_set_ring_geometry_generation(&owner, 12u));
    assert(owner.ring_control_pending == TDMA_RING_CONTROL_NONE);
    assert(tdma_service_ring_arm(&owner));
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(received.geometry_generation == 12u);
}

static void case_control_guards(void)
{
    setup();
    assert(!tdma_service_set_ring_geometry_generation(NULL, 0u));
    assert(tdma_service_set_ring_geometry_generation(&owner, UINT32_MAX));
    owner.ring_control_guard = 1u;
    assert(!tdma_service_set_ring_geometry_generation(&owner, 8u));
    assert(!tdma_service_ring_arm(&owner));
    assert(owner.ring_control_guard == 1u);
    owner.ring_control_guard = 0u;
    const uint32_t blocked_updates[] = {
        TDMA_STOPPED_UPDATE_REQUESTED | 1u, TDMA_STOPPED_UPDATE_APPLYING | 1u};
    for (unsigned i = 0; i < 2u; ++i) {
        owner.stopped_update = blocked_updates[i];
        assert(!tdma_service_set_ring_geometry_generation(&owner, 0u));
        assert(!tdma_service_ring_arm(&owner));
        assert(owner.ring_staged_config.geometry_generation == UINT32_MAX);
        assert(owner.ring_control_guard == 0u);
    }
    owner.stopped_update = 0u;
    owner.ring_runtime.result_guard = 1u;
    assert(!tdma_service_set_ring_geometry_generation(&owner, 8u));
    owner.ring_runtime.result_guard = 0u;
    owner.ring_runtime.config_guard = 1u;
    assert(!tdma_service_set_ring_geometry_generation(&owner, 8u));
    owner.ring_runtime.config_guard = 0u;
    owner.ring_staged_config.enabled = 0u;
    assert(!tdma_service_set_ring_geometry_generation(&owner, 8u));
    owner.ring_staged_config.enabled = 1u;
    assert(owner.ring_staged_config.geometry_generation == UINT32_MAX);
    assert(tdma_service_set_ring_geometry_generation(&owner, 0u));
    assert(owner.ring_staged_config.geometry_generation == 0u);
}

static void case_guarded_replace(void)
{
    setup();
    assert(tdma_service_set_ring_geometry_generation(&owner, 41u));
    replacement = owner.ring_staged_config;
    replacement.geometry_generation = 42u;
    replacement.schedule_crc32 ^= 1u;
    replacement.owner_config_seq = UINT32_MAX;
    assert(tdma_service_ring_arm(&owner));
    const uint32_t first_seq = owner.ring_runtime.config_seq;
    replace_on_geometry_load = true;
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(replacements == 1u && !replace_on_geometry_load);
    assert(start_calls == 1u);
    assert(received.geometry_generation == 42u);
    assert(received.schedule_crc32 == replacement.schedule_crc32);
    assert(received.owner_config_seq == first_seq + 1u);
    assert(received.owner_config_seq == owner.ring_runtime.applied_config_seq);
}

static void case_writer_pending(void)
{
    setup();
    assert(tdma_service_set_ring_geometry_generation(&owner, 9u));
    assert(tdma_service_ring_arm(&owner));
    ++owner.ring_runtime.config_guard;
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(start_calls == 0u && !owner.ring_runtime.adapter_started);
    assert(owner.ring_runtime.config_seq != owner.ring_runtime.applied_config_seq);
    ++owner.ring_runtime.config_guard;
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(start_calls == 1u && received.geometry_generation == 9u);
    assert(received.owner_config_seq == owner.ring_runtime.config_seq);
}

static void case_rejected_arm(void)
{
    setup();
    assert(tdma_service_set_ring_geometry_generation(&owner, 37u));
    const uint32_t valid_nodes = owner.ring_staged_config.node_count;
    const uint32_t before = owner.ring_runtime.config_seq;
    owner.ring_staged_config.node_count = 1u;
    assert(!tdma_service_ring_arm(&owner));
    assert(owner.ring_staged_config.geometry_generation == 37u);
    assert(owner.ring_runtime.config_seq == before);
    owner.ring_staged_config.node_count = valid_nodes;
    reject_start = true;
    assert(tdma_service_ring_arm(&owner));
    assert(owner.ring_staged_config.geometry_generation == 0u);
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(start_calls == 1u && received.geometry_generation == 37u);
    assert(owner.ring_runtime.config_seq != owner.ring_runtime.applied_config_seq);
    assert(!owner.ring_runtime.adapter_started);
    finish_stop();
    reject_start = false;
    assert(tdma_service_ring_arm(&owner));
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(start_calls == 2u && received.geometry_generation == 0u);
}

static void case_disabled_clear(void)
{
    setup();
    tdma_ring_runtime_config_t config = owner.ring_staged_config;
    config.geometry_generation = 123u;
    config.owner_config_seq = 99u;
    assert(tdma_service_configure_ring_runtime(&owner, &config));
    assert(owner.ring_runtime.geometry_generation == 123u);
    config.enabled = 0u;
    assert(tdma_service_configure_ring_runtime(&owner, &config));
    assert(owner.ring_runtime.geometry_generation == 0u);
    assert(tdma_ring_runtime_configure(&owner.ring_runtime, NULL));
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(!start_calls);
    assert(owner.ring_runtime.config_seq == owner.ring_runtime.applied_config_seq);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "one_shot")) case_one_shot();
    else if (!strcmp(argv[1], "stop_pending")) case_stop_pending();
    else if (!strcmp(argv[1], "control_guards")) case_control_guards();
    else if (!strcmp(argv[1], "guarded_replace")) case_guarded_replace();
    else if (!strcmp(argv[1], "writer_pending")) case_writer_pending();
    else if (!strcmp(argv[1], "rejected_arm")) case_rejected_arm();
    else if (!strcmp(argv[1], "disabled_clear")) case_disabled_clear();
    else assert(!"unknown case");
    return 0;
}
'''


@pytest.fixture(scope="module")
def geometry_config_exe(tmp_path_factory):
    build = tmp_path_factory.mktemp("tdma-geometry-config")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler and Path("D:/Microsoft/mingw64/bin/gcc.exe").is_file():
        compiler = "D:/Microsoft/mingw64/bin/gcc.exe"
    assert compiler, "A host C compiler is required"
    harness = build / "geometry_config.c"
    harness.write_text(HARNESS, encoding="utf-8")
    sources = [ROOT / f"components/tdma/src/{name}.c" for name in (
        "tdma_profile", "tdma_operating_profile", "tdma_payload_registry",
        "tdma_flight_fifo", "tdma_flight_engine", "tdma_process_image_map",
        "tdma_traffic_scheduler")]
    exe = build / ("geometry_config.exe" if os.name == "nt" else "geometry_config")
    result = subprocess.run(
        [compiler, "-std=c11", "-Wall", "-Wextra", "-Werror",
         "-I" + str(ROOT), "-I" + str(ROOT / "components/tdma/inc"),
         "-I" + str(ROOT / "components/vdc_domain/inc"),
         str(harness), *map(str, sources), "-o", str(exe)],
        capture_output=True, text=True, timeout=60)
    (build / "compile.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (build / "compile.stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


@pytest.mark.parametrize("case", [
    "one_shot", "stop_pending", "control_guards", "guarded_replace",
    "writer_pending", "rejected_arm", "disabled_clear",
])
def test_geometry_selection_uses_guarded_one_shot_config(geometry_config_exe, case):
    result = subprocess.run([str(geometry_config_exe), case], capture_output=True,
                            text=True, timeout=3)
    (geometry_config_exe.parent / f"{case}.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (geometry_config_exe.parent / f"{case}.stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
