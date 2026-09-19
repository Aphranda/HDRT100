"""Read the real foundation scalar independently of live diagnostic ownership.

Uses the real public service/scheduler/registry/ring structs, extracted unchanged
production service getters, and complete production registry/ring/scheduler
translation units. Only narrow-getter guard/fence operations are instrumented
to inject a writer and count the bound; scheduler locking is its real atomic
try-lock, not a facade that returns a desired status. Unused production sections
are removed by the host linker, so no hardware/service execution is simulated.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def source_text() -> tuple[str, dict[str, str]]:
    path = ROOT / "components/tdma/src/tdma_service.c"
    source = path.read_text(encoding="utf-8")
    limit = re.search(r"(?m)^#define TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT\s+[^\n]+", source)
    assert limit
    routines = []
    hashes = {}
    for result, name, arguments in [
        ("uint32_t", "tdma_service_load", "const volatile uint32_t *value"),
        ("void", "tdma_service_split_u64", "uint64_t value, uint32_t *lo, uint32_t *hi"),
        ("bool", "tdma_service_get_snapshot", "const tdma_service_service_t *service, tdma_service_snapshot_t *snapshot"),
    ]:
        body = c_definition_body(source, name)
        hashes[name] = hashlib.sha256(body.encode()).hexdigest()
        routines.append(f"{result} {name}({arguments}) {{" + body + "}\n")
    narrow = c_definition_body(source, "tdma_service_get_foundation_crc32")
    hashes["tdma_service_get_foundation_crc32"] = hashlib.sha256(narrow.encode()).hexdigest()
    return (PREFIX + limit.group(0) + "\n" + "\n".join(routines) + HOOKS +
            "bool tdma_service_get_foundation_crc32(const tdma_service_service_t *service, uint32_t *crc32) {" +
            narrow + "}\n#undef tdma_service_load\n#undef __atomic_thread_fence\n" + CASES), hashes


PREFIX = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tdma_service.h"
'''

HOOKS = r'''
static tdma_service_service_t service;
static tdma_traffic_scheduler_t scheduler;
static tdma_service_snapshot_t broad;
static unsigned loads, fences, mutation, mutations_left;
static uint32_t next_crc;
static uint32_t tracked_load(const volatile uint32_t *value) {
    assert(value == &service.intent_guard);
    ++loads;
    return tdma_service_load(value); /* The real acquire load. */
}
static void tracked_fence(int order) {
    assert(order == __ATOMIC_ACQUIRE);
    __atomic_thread_fence(order);
    ++fences;
    if (mutation == 0u || mutations_left == 0u) return;
    --mutations_left;
    /* Writer begins only after this iteration has read the old CRC. */
    __atomic_add_fetch(&service.intent_guard, 1u, __ATOMIC_ACQ_REL);
    service.foundation_profile_crc32 = next_crc;
    if (mutation != 2u)
        __atomic_add_fetch(&service.intent_guard, 1u, __ATOMIC_RELEASE);
}
#define tdma_service_load tracked_load
#define __atomic_thread_fence(order) tracked_fence(order)
'''

CASES = r'''
static unsigned checks;
static void reset(void) {
    memset(&service, 0, sizeof(service));
    memset(&scheduler, 0, sizeof(scheduler));
    service.foundation_profile_crc32 = UINT32_C(0x1234abcd);
    service.traffic_scheduler = &scheduler;
    loads = fences = mutation = mutations_left = 0u;
}
static void read_ok(uint32_t expected) {
    uint32_t crc = UINT32_C(0xdeadbeef);
    assert(tdma_service_get_foundation_crc32(&service, &crc));
    assert(crc == expected);
    ++checks;
}
static void test_real_scheduler_lock_independence(void) {
    reset();
    assert(tdma_service_get_snapshot(&service, &broad));
    assert(broad.foundation_profile_crc32 == service.foundation_profile_crc32);
    assert(scheduler.lock == 0u);
    __atomic_store_n(&scheduler.lock, 1u, __ATOMIC_RELEASE);
    assert(!tdma_service_get_snapshot(&service, &broad));
    assert(scheduler.lock == 1u); /* Broad getter cannot release another owner. */
    const uint32_t guard = service.intent_guard;
    read_ok(UINT32_C(0x1234abcd));
    assert(loads == 2u && fences == 1u && scheduler.lock == 1u && service.intent_guard == guard);
    __atomic_store_n(&scheduler.lock, 0u, __ATOMIC_RELEASE);
    assert(tdma_service_get_snapshot(&service, &broad));
    assert(broad.foundation_profile_crc32 == UINT32_C(0x1234abcd));
}
static void test_unrelated_snapshots_are_not_dependencies(void) {
    for (unsigned which = 0u; which < 5u; ++which) {
        reset();
        volatile uint32_t *guard = which == 0u ? &service.result_guard :
            which == 1u ? &service.payload_registry.guard :
            which == 2u ? &service.ring_runtime.config_guard :
            which == 3u ? &service.ring_runtime.result_guard : &scheduler.lock;
        *guard = 1u;
        assert(!tdma_service_get_snapshot(&service, &broad));
        read_ok(UINT32_C(0x1234abcd));
        assert(loads == 2u && fences == 1u && *guard == 1u);
    }
    reset();
    service.traffic_scheduler = NULL;
    assert(tdma_service_get_snapshot(&service, &broad));
    read_ok(UINT32_C(0x1234abcd));
}
static void test_null_zero_and_failure_clearing(void) {
    reset();
    uint32_t crc = UINT32_MAX;
    assert(!tdma_service_get_foundation_crc32(NULL, &crc) && crc == 0u);
    assert(!tdma_service_get_foundation_crc32(&service, NULL));
    assert(!tdma_service_get_foundation_crc32(NULL, NULL));
    assert(loads == 0u && fences == 0u);
    /* Availability is not configuration/model validation: both the existing
     * broad getter and new getter return the actual zero field unchanged. */
    memset(&service, 0, sizeof(service));
    assert(tdma_service_get_snapshot(&service, &broad) && broad.foundation_profile_crc32 == 0u);
    read_ok(0u);
    service.foundation_profile_crc32 = UINT32_MAX;
    read_ok(UINT32_MAX); /* No CRC value is an unavailable sentinel. */
    service.intent_guard = 1u; loads = fences = 0u; crc = UINT32_MAX;
    assert(!tdma_service_get_foundation_crc32(&service, &crc) && crc == 0u);
    assert(loads == TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT && fences == 0u);
    service.intent_guard = 2u; read_ok(UINT32_MAX);
}
static void test_writer_between_copy_and_recheck(void) {
    reset(); mutation = 1u; mutations_left = 1u; next_crc = UINT32_C(0xaabbccdd);
    read_ok(next_crc);
    assert(loads == 4u && fences == 2u && service.intent_guard == 2u);
    /* Changed payload with the old guard is never returned after a complete
     * writer cycle; a subsequent call observes another published generation. */
    mutation = 0u;
    __atomic_add_fetch(&service.intent_guard, 1u, __ATOMIC_ACQ_REL);
    service.foundation_profile_crc32 = 17u;
    __atomic_add_fetch(&service.intent_guard, 1u, __ATOMIC_RELEASE);
    read_ok(17u);
    reset(); mutation = 2u; mutations_left = 1u; next_crc = 99u;
    uint32_t crc = UINT32_MAX;
    assert(!tdma_service_get_foundation_crc32(&service, &crc) && crc == 0u);
    assert(fences == 1u && loads == TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT + 1u);
    assert(service.intent_guard == 1u);
    __atomic_add_fetch(&service.intent_guard, 1u, __ATOMIC_RELEASE);
    mutation = 0u; read_ok(99u);
}
static void test_bound_and_terminal_retry(void) {
    reset(); mutation = 1u; mutations_left = TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT;
    next_crc = 55u;
    uint32_t crc = UINT32_MAX;
    assert(!tdma_service_get_foundation_crc32(&service, &crc) && crc == 0u);
    assert(loads == 2u * TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT);
    assert(fences == TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT && mutations_left == 0u);
    /* Failing the complete call never substitutes the last observed CRC. */
    mutation = 0u; read_ok(55u);
    reset(); mutation = 1u; mutations_left = TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT - 1u;
    next_crc = 77u; read_ok(77u);
    assert(loads == 2u * TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT && fences == TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT);
    /* A single 32-bit guard rollover is a changed generation, not ABA. The
     * full 2^32 writer wrap is outside a bounded scalar copy's lifetime. */
    reset(); service.intent_guard = UINT32_MAX - 1u;
    mutation = 1u; mutations_left = 1u; next_crc = 88u;
    read_ok(88u);
    assert(service.intent_guard == 0u && loads == 4u && fences == 2u);
}
int main(void) {
    test_real_scheduler_lock_independence();
    test_unrelated_snapshots_are_not_dependencies();
    test_null_zero_and_failure_clearing();
    test_writer_between_copy_and_recheck();
    test_bound_and_terminal_retry();
    printf("foundation scalar: 5 production case groups; %u success checks; retry_limit=%u; real scheduler/registry/ring getters\n",
        checks, TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT);
    return 0;
}
'''


def test_real_foundation_read_is_bounded_and_independent(tmp_path: Path) -> None:
    source, hashes = source_text()
    unit = tmp_path / "foundation.c"
    unit.write_text(source, encoding="utf-8")
    (tmp_path / "production-bodies.json").write_text(json.dumps(hashes, indent=2), encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = tmp_path / "foundation.exe"
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror", "-pedantic",
               "-ffunction-sections", "-fdata-sections", "-Wl,--gc-sections",
               "-I" + str(ROOT / "components/tdma/inc"), "-I" + str(ROOT / "config"), str(unit)]
    command += [str(ROOT / "components/tdma/src" / name) for name in
                ("tdma_payload_registry.c", "tdma_ring_runtime.c", "tdma_traffic_scheduler.c", "tdma_profile.c")]
    command += ["-o", str(executable)]
    for stage, call in (("compile", command), ("run", [str(executable)])):
        result = subprocess.run(call, capture_output=True, text=True, timeout=60)
        (tmp_path / f"{stage}.json").write_text(json.dumps({
            "command": call, "returncode": result.returncode}, indent=2), encoding="utf-8")
        (tmp_path / f"{stage}.log").write_text(result.stdout + result.stderr, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
    assert "5 production case groups" in result.stdout


def test_quality_reader_rechecks_both_publications(tmp_path: Path) -> None:
    source = (ROOT / "components/tdma/src/tdma_service.c").read_text(encoding="utf-8")
    limit = re.search(r"(?m)^#define TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT\s+[^\n]+", source).group(0)
    load = c_definition_body(source, "tdma_service_load")
    quality = c_definition_body(source, "tdma_service_get_quality_snapshot")
    text = PREFIX + limit + "\nuint32_t tdma_service_load(const volatile uint32_t *value) {" + load + "}\n"
    text += r'''
static tdma_service_service_t service;
static unsigned fences, remaining, which;
static void writer_at_fence(int order) {
    __atomic_thread_fence(order);
    ++fences;
    if (!remaining) return;
    --remaining;
    volatile uint32_t *guard = which ? &service.result_guard : &service.intent_guard;
    ++*guard;
    if (which) { ++service.overrun_count; ++service.timeout_count; ++service.last_error; }
    else ++service.reject_count;
    ++*guard;
}
#define __atomic_thread_fence writer_at_fence
'''
    text += "bool tdma_service_get_quality_snapshot(const tdma_service_service_t *service, uint32_t traffic_class, tdma_service_quality_snapshot_t *snapshot) {" + quality + "}\n"
    text += r'''
#undef __atomic_thread_fence
int main(void) {
    for (which = 0; which < 2; ++which) {
        memset(&service, 0, sizeof(service));
        service.intent_guard = service.result_guard = UINT32_MAX - 1u;
        tdma_service_quality_snapshot_t out, sentinel;
        memset(&sentinel, 0xa5, sizeof(sentinel));
        remaining = 1; fences = 0;
        assert(tdma_service_get_quality_snapshot(&service, 0, &out));
        assert(fences == 2 && !remaining);
        assert(out.reject_count == (which ? 0u : 1u));
        assert(out.overrun_count == (which ? 1u : 0u));
        assert(out.timeout_count == (which ? 1u : 0u));
        assert(out.last_error == (which ? 1u : 0u));
        out = sentinel; remaining = TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT; fences = 0;
        assert(!tdma_service_get_quality_snapshot(&service, 0, &out));
        assert(fences == TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT && !remaining);
        assert(!memcmp(&out, &sentinel, sizeof(out)));
    }
    return 0;
}
'''
    unit = tmp_path / "quality.c"
    unit.write_text(text, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executable = tmp_path / "quality.exe"
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I" + str(ROOT / "components/tdma/inc"), str(unit), "-o", str(executable)]
    for call in (command, [str(executable)]):
        result = subprocess.run(call, capture_output=True, text=True, timeout=60)
        assert result.returncode == 0, result.stdout + result.stderr
