"""Run production START admission with deterministic atomic interleavings."""

import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "tdma_service.h"

static tdma_service_service_t owner;
static tdma_ring_runtime_config_t replacement;
static unsigned start_calls, stop_calls, runtime_loads, data_stores;
static unsigned mutations, final_store_hooks, store_mutations;
static bool tracking, intercept_final_store;
static const char *read_mutation;
static const char *store_mutation;
static void mutate_during_read(void);

static uint32_t observed_load(const volatile uint32_t *address, int order)
{
    const uint32_t value = __atomic_load_n(address, order);
    if (tracking) assert(++runtime_loads < 2048u);
    /* This is the last admission field, before the production guard recheck.
     * Return its actual old value after injecting one complete publication. */
    if (read_mutation != NULL &&
        address == &owner.ring_runtime.train_accepted_seq) {
        mutate_during_read();
    }
    return value;
}

static void observed_store(volatile uint32_t *address, uint32_t value, int order)
{
    const char *mutation = NULL;
    if (address == &owner.ring_runtime.data_enabled) {
        if (tracking) ++data_stores;
        if (value == 1u && store_mutation != NULL) {
            mutation = store_mutation;
            store_mutation = NULL;
            ++store_mutations;
            if (!strcmp(mutation, "core1_cancel_before_store")) {
                /* Core1 lifetime cancellation intentionally bypasses the
                 * Core0 service lock. Exercise the real runtime STOP writer. */
                assert(tdma_ring_runtime_configure(&owner.ring_runtime, NULL));
            }
        }
        if (intercept_final_store && value == 1u) {
            intercept_final_store = false;
            ++final_store_hooks;
            const uint32_t sequence = owner.ring_runtime.config_seq;
            const uint32_t pending = owner.ring_control_pending;
            const uint32_t original_data = owner.ring_runtime.data_enabled;
            /* Interleave actual public control calls immediately BEFORE the
             * final DATA store. They must not enter their locked writers. */
            assert(owner.ring_control_guard == 1u);
            assert(!tdma_service_ring_stop(&owner));
            assert(!tdma_service_configure_ring_runtime(&owner, &replacement));
            assert(!tdma_service_ring_arm(&owner));
            assert(owner.ring_control_guard == 1u);
            assert(owner.ring_runtime.config_seq == sequence);
            assert(owner.ring_runtime.enabled == 1u);
            assert(owner.ring_runtime.data_enabled == original_data);
            assert(owner.ring_control_pending == pending);
        }
    }
    __atomic_store_n(address, value, order);
    if (mutation != NULL) {
        if (!strcmp(mutation, "core1_cancel_after_store")) {
            assert(tdma_ring_runtime_configure(&owner.ring_runtime, NULL));
        } else if (!strcmp(mutation, "result_after_store")) {
            /* A routine Core1 result publication is not a new configuration
             * or cancellation, and must not retract a valid DATA request. */
            tdma_ring_runtime_service(&owner.ring_runtime);
        } else assert(!strcmp(mutation, "core1_cancel_before_store"));
    }
}

/* The complete production implementations and their real dependencies are
 * compiled below. Only atomic loads/stores in runtime are instrumented; the
 * service control lock and every admission/cleanup branch remain untouched. */
#include "components/tdma/src/tdma_service.c"
#define __atomic_load_n observed_load
#define __atomic_store_n observed_store
#include "components/tdma/src/tdma_ring_runtime.c"
#undef __atomic_store_n
#undef __atomic_load_n

static bool adapter_start(void *context, const tdma_ring_runtime_config_t *config)
{
    assert(context == &owner && config != NULL && config->enabled != 0u);
    ++start_calls;
    return true;
}

static bool adapter_stop(void *context)
{
    assert(context == &owner);
    ++stop_calls;
    return true;
}

static bool adapter_service(void *context, uint64_t now, tdma_ring_adapter_status_t *status)
{
    assert(context == &owner);
    (void)now; (void)status;
    return true;
}

static void setup(void)
{
    static const tdma_ring_adapter_ops_t ops = {
        .start = adapter_start, .stop = adapter_stop, .service = adapter_service};
    assert(tdma_service_init(&owner));
    tdma_foundation_profile_t profile;
    assert(tdma_foundation_profile_default(&profile, 1u, 0u, 0u, TDMA_ADAPTER_PIO_SPI));
    assert(tdma_service_configure_foundation_profile(&owner, &profile, 7u));
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(tdma_service_bind_ring_adapter(&owner, &ops, &owner));
    assert(tdma_service_ring_arm(&owner));
    tdma_ring_runtime_service(&owner.ring_runtime);
    tdma_service_core0_lifecycle_service(&owner);
    assert(start_calls == 1u);
    assert(owner.ring_runtime.config_seq == owner.ring_runtime.applied_config_seq);
    assert(owner.ring_runtime.config_seq == owner.ring_runtime.adapter_config_seq);
    assert(owner.ring_runtime.adapter_started == 1u);
    assert(!owner.ring_runtime.adapter_stop_pending && !owner.ring_runtime.data_enabled);
    assert(!owner.ring_control_guard && owner.ring_control_pending == TDMA_RING_CONTROL_NONE);
    replacement = owner.ring_staged_config;
    replacement.schedule_crc32 ^= 1u;
    runtime_loads = data_stores = 0u;
    tracking = true;
}

static bool start(bool through_service)
{
    return through_service ? tdma_service_ring_start(&owner) :
        tdma_ring_runtime_set_data_enabled(&owner.ring_runtime, true);
}

static void mutate_during_read(void)
{
    const char *mutation = read_mutation;
    read_mutation = NULL; /* Nested production calls cannot reinject. */
    ++mutations;
    if (!strcmp(mutation, "config_publish")) {
        /* Direct-runtime adversarial observation: its documented caller
         * serialization is tested separately at the public service boundary. */
        assert(tdma_ring_runtime_configure(&owner.ring_runtime, &replacement));
    } else if (!strcmp(mutation, "result_publish")) {
        /* A real Core1 service publishes a newer result, even if its ACK
         * values happen to stay equal. Core0 START must reject the torn view. */
        tdma_ring_runtime_service(&owner.ring_runtime);
    } else if (!strcmp(mutation, "config_writer")) {
        tdma_ring_runtime_write_guard(&owner.ring_runtime.config_guard);
    } else if (!strcmp(mutation, "result_writer")) {
        tdma_ring_runtime_write_guard(&owner.ring_runtime.result_guard);
    } else assert(!"unknown mutation");
}

static void case_admission(const char *fault, bool through_service)
{
    setup();
    tdma_ring_runtime_t *runtime = &owner.ring_runtime;
    if (!strcmp(fault, "config_odd")) ++runtime->config_guard;
    else if (!strcmp(fault, "result_odd")) ++runtime->result_guard;
    else if (!strcmp(fault, "applied_old")) --runtime->applied_config_seq;
    else if (!strcmp(fault, "adapter_old")) --runtime->adapter_config_seq;
    else if (!strcmp(fault, "adapter_unbound")) runtime->adapter_config_seq = 0u;
    else if (!strcmp(fault, "cleanup_pending")) runtime->adapter_stop_pending = 1u;
    else if (!strcmp(fault, "train_pending")) ++runtime->train_command_seq;
    else if (!strcmp(fault, "unarmed")) runtime->adapter_started = 0u;
    else if (!strcmp(fault, "disabled")) runtime->enabled = 0u;
    else assert(!"unknown admission fault");
    const tdma_ring_runtime_t before = *runtime;
    assert(!start(through_service));
    assert(!memcmp(runtime, &before, sizeof(before)));
    assert(!runtime->data_enabled && !data_stores);
    assert(!owner.ring_control_guard);
    assert(start_calls == 1u && !stop_calls);
    /* A busy/odd publication is rejected without a snapshot retry loop. */
    assert(runtime_loads < 32u);
}

static void case_read_mutation(const char *mutation, bool through_service)
{
    setup();
    const uint32_t old_config = owner.ring_runtime.config_seq;
    const uint32_t old_result_guard = owner.ring_runtime.result_guard;
    const uint32_t old_config_guard = owner.ring_runtime.config_guard;
    read_mutation = mutation;
    assert(!start(through_service));
    assert(mutations == 1u && read_mutation == NULL);
    assert(!owner.ring_runtime.data_enabled && !data_stores);
    assert(!owner.ring_control_guard);
    if (!strcmp(mutation, "config_publish")) {
        assert(owner.ring_runtime.config_seq != old_config);
        assert(owner.ring_runtime.config_guard != old_config_guard);
        assert(owner.ring_runtime.applied_config_seq == old_config);
        tdma_ring_runtime_service(&owner.ring_runtime);
    } else if (!strcmp(mutation, "result_publish")) {
        assert(owner.ring_runtime.result_guard != old_result_guard);
        assert(owner.ring_runtime.config_seq == old_config);
        assert(owner.ring_runtime.applied_config_seq == old_config);
    } else if (!strcmp(mutation, "config_writer")) {
        assert(owner.ring_runtime.config_guard & 1u);
        tdma_ring_runtime_write_guard(&owner.ring_runtime.config_guard);
    } else {
        assert(owner.ring_runtime.result_guard & 1u);
        tdma_ring_runtime_write_guard(&owner.ring_runtime.result_guard);
    }
    /* Rejection leaves no queued DATA request. Only a new caller can START
     * after the real replacement ARM or publication completes. */
    assert(!owner.ring_runtime.data_enabled);
    assert(start(through_service));
    assert(owner.ring_runtime.data_enabled == 1u && data_stores == 1u);
}

static void case_control_busy(void)
{
    setup();
    owner.ring_control_guard = 1u;
    const tdma_service_service_t before = owner;
    for (unsigned attempt = 0; attempt < 3u; ++attempt) {
        assert(!tdma_service_ring_start(&owner));
        assert(!memcmp(&owner, &before, sizeof(owner)));
    }
    assert(!runtime_loads && !data_stores);
    assert(start_calls == 1u && !stop_calls);
    owner.ring_control_guard = 0u;
    assert(tdma_service_ring_start(&owner));
    assert(data_stores == 1u && !owner.ring_control_guard);
}

static void case_final_store_control(void)
{
    setup();
    const uint32_t original_config = owner.ring_runtime.config_seq;
    intercept_final_store = true;
    assert(tdma_service_ring_start(&owner));
    assert(final_store_hooks == 1u && !intercept_final_store);
    assert(!owner.ring_control_guard);
    assert(owner.ring_runtime.config_seq == original_config);
    assert(owner.ring_runtime.data_enabled == 1u && data_stores == 1u);
    /* Once START releases the lock, STOP is admitted and clears DATA. The
     * intercepted/rejected calls were not queued to replay or resurrect it. */
    assert(tdma_service_ring_stop(&owner));
    const uint32_t stop_config = owner.ring_runtime.config_seq;
    assert(stop_config != original_config);
    assert(!owner.ring_runtime.enabled && !owner.ring_runtime.data_enabled);
    assert(!tdma_service_ring_start(&owner));
    for (unsigned phase = 0; phase < 3u; ++phase) {
        tdma_ring_runtime_service(&owner.ring_runtime);
        tdma_service_core0_lifecycle_service(&owner);
        assert(!owner.ring_runtime.data_enabled);
        assert(!tdma_service_ring_start(&owner));
    }
    assert(stop_calls == 1u && start_calls == 1u && data_stores == 1u);
    assert(owner.ring_runtime.applied_config_seq == stop_config);
    assert(!owner.ring_runtime.adapter_started && !owner.ring_runtime.adapter_stop_pending);
    assert(owner.ring_control_pending == TDMA_RING_CONTROL_NONE);
    assert(tdma_service_ring_arm(&owner));
    assert(!tdma_service_ring_start(&owner));
    tdma_ring_runtime_service(&owner.ring_runtime);
    assert(!owner.ring_runtime.data_enabled);
    assert(tdma_service_ring_start(&owner));
    assert(owner.ring_runtime.data_enabled == 1u && data_stores == 2u);
}

static void case_success(void)
{
    setup();
    assert(!tdma_service_ring_start(NULL));
    assert(!tdma_ring_runtime_set_data_enabled(NULL, true));
    assert(tdma_service_ring_start(&owner));
    assert(tdma_service_ring_start(&owner));
    assert(owner.ring_runtime.data_enabled == 1u && start_calls == 1u);
    assert(tdma_ring_runtime_set_data_enabled(&owner.ring_runtime, false));
    assert(!owner.ring_runtime.data_enabled);
    assert(tdma_ring_runtime_set_data_enabled(&owner.ring_runtime, true));
    assert(owner.ring_runtime.data_enabled == 1u && !owner.ring_control_guard);
}

static void case_store_mutation(const char *mutation, bool through_service)
{
    setup();
    const uint32_t config = owner.ring_runtime.config_seq;
    const uint32_t result_guard = owner.ring_runtime.result_guard;
    store_mutation = mutation;
    const bool accepted = start(through_service);
    assert(store_mutations == 1u && store_mutation == NULL);
    assert(!owner.ring_control_guard);
    if (!strcmp(mutation, "result_after_store")) {
        assert(accepted && owner.ring_runtime.data_enabled == 1u);
        assert(owner.ring_runtime.result_guard != result_guard);
        assert(owner.ring_runtime.config_seq == config);
        assert(owner.ring_runtime.enabled == 1u && data_stores == 1u);
    } else {
        assert(!accepted && !owner.ring_runtime.data_enabled);
        assert(owner.ring_runtime.config_seq != config);
        assert(!owner.ring_runtime.enabled);
        tdma_ring_runtime_service(&owner.ring_runtime);
        assert(owner.ring_runtime.config_seq == owner.ring_runtime.applied_config_seq);
        assert(!owner.ring_runtime.adapter_started);
        assert(!start(through_service) && !owner.ring_runtime.data_enabled);
    }
}

int main(int argc, char **argv)
{
    assert(argc == 3);
    const bool through_service = !strcmp(argv[2], "service");
    assert(through_service || !strcmp(argv[2], "runtime"));
    if (!strcmp(argv[1], "control_busy")) case_control_busy();
    else if (!strcmp(argv[1], "final_store_control")) case_final_store_control();
    else if (!strcmp(argv[1], "success")) case_success();
    else if (!strcmp(argv[1], "core1_cancel_before_store") ||
             !strcmp(argv[1], "core1_cancel_after_store") ||
             !strcmp(argv[1], "result_after_store"))
        case_store_mutation(argv[1], through_service);
    else if (!strcmp(argv[1], "config_publish") || !strcmp(argv[1], "result_publish") ||
             !strcmp(argv[1], "config_writer") || !strcmp(argv[1], "result_writer"))
        case_read_mutation(argv[1], through_service);
    else case_admission(argv[1], through_service);
    printf("PASS %s %s loads=%u data_stores=%u mutations=%u final_store_hooks=%u store_mutations=%u\n",
        argv[1], argv[2], runtime_loads, data_stores, mutations, final_store_hooks, store_mutations);
    return 0;
}
'''


@pytest.fixture(scope="module")
def start_admission_exe(tmp_path_factory):
    build = tmp_path_factory.mktemp("tdma-start-admission")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler and Path("D:/Microsoft/mingw64/bin/gcc.exe").is_file():
        compiler = "D:/Microsoft/mingw64/bin/gcc.exe"
    assert compiler, "A host C compiler is required"
    harness = build / "start_admission.c"
    harness.write_text(HARNESS, encoding="utf-8")
    sources = [ROOT / f"components/tdma/src/{name}.c" for name in (
        "tdma_profile", "tdma_operating_profile", "tdma_payload_registry",
        "tdma_flight_fifo", "tdma_flight_engine", "tdma_process_image_map",
        "tdma_traffic_scheduler")]
    exe = build / ("start_admission.exe" if os.name == "nt" else "start_admission")
    result = subprocess.run(
        [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
         "-I" + str(ROOT), "-I" + str(ROOT / "components/tdma/inc"),
         "-I" + str(ROOT / "components/vdc_domain/inc"),
         str(harness), *map(str, sources), "-o", str(exe)],
        capture_output=True, text=True, timeout=60)
    (build / "compile.stdout.txt").write_text(result.stdout, encoding="utf-8")
    (build / "compile.stderr.txt").write_text(result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


CASES = [
    (case, entry)
    for case in ("config_odd", "result_odd", "applied_old", "adapter_old",
                 "adapter_unbound", "cleanup_pending", "train_pending",
                 "unarmed", "disabled", "result_publish", "result_writer",
                 "core1_cancel_before_store", "core1_cancel_after_store",
                 "result_after_store")
    for entry in ("runtime", "service")
] + [
    # Unsynchronized configuration publication is an adversarial direct
    # runtime observation; the real service lock must forbid it below.
    ("config_publish", "runtime"), ("config_writer", "runtime"),
    ("control_busy", "service"), ("final_store_control", "service"),
    ("success", "service"),
]


@pytest.mark.parametrize("case,entry", CASES, ids=[f"{c}-{e}" for c, e in CASES])
def test_start_admission_and_control_interleavings(start_admission_exe, case, entry):
    result = subprocess.run([str(start_admission_exe), case, entry],
                            capture_output=True, text=True, timeout=3)
    (start_admission_exe.parent / f"{case}-{entry}.stdout.txt").write_text(
        result.stdout, encoding="utf-8")
    (start_admission_exe.parent / f"{case}-{entry}.stderr.txt").write_text(
        result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
