"""Run production app profile transactions against controlled owner/Flash edges."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


def _function(source, signature):
    start = source.index(signature)
    body = source.index("{", start)
    depth, end = 1, body + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


@pytest.fixture(scope="module")
def profile_transaction_executable(tmp_path_factory):
    tmp_path = tmp_path_factory.mktemp("profile-transaction")
    app = (ROOT / "application/src/app.c").read_text(encoding="utf-8")
    transactions = app.split(
        "/* ---- run-time configurable core1 phase table", 1
    )[1].split("static void app_realtime_record_skip", 1)[0]
    transactions = transactions[transactions.index("_Static_assert"):]
    core1 = _function(app, "bool app_realtime_apply_pending_profile_core1(void)")
    harness = r'''
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "app_realtime_profile.h"
#include "product_config.h"

/* Avoid the Windows CRT abort dialog on a failing behavioral assertion. */
#undef assert
#define assert(test) do { if (!(test)) { \
    fprintf(stderr, "assertion failed at line %d: %s\n", __LINE__, #test); \
    exit(1); \
} } while (0)

typedef struct {
    uint32_t stopped_update, generation;
    bool running, applying, guard;
} tdma_service_service_t;
static tdma_service_service_t owner;
static bool ready = true, flash_fail, stored_valid, offline, booting;
static bool settle_during_snapshot;
static unsigned flash_writes, schedule_writes;
static product_config_realtime_profile_t durable;
static app_realtime_schedule_snapshot_t s_realtime_schedule;
static bool s_realtime_profile_persisted;
static app_realtime_profile_rejection_t s_realtime_profile_reject =
    APP_REALTIME_PROFILE_REJECTION_INIT;

bool app_is_ready(void) { return ready; }
static tdma_service_service_t *tdma_runtime_owner_get(void) { return &owner; }
bool app_realtime_get_schedule_snapshot(app_realtime_schedule_snapshot_t *out)
{
    *out = s_realtime_schedule;
    if (settle_during_snapshot) {
        settle_during_snapshot = false;
        assert(app_realtime_apply_pending_profile_core1());
    }
    return true;
}
static void app_realtime_schedule_write_begin(void)
{ assert(booting || owner.applying); ++schedule_writes; }
static void app_realtime_schedule_write_end(void) {}
static bool calibration_manager_p3_offline_active_core1(void) { return offline; }
static bool calibration_manager_training_offline_active_core1(void) { return false; }
static bool calibration_manager_ring_capture_offline_active_core1(void) { return false; }

/* The service's concurrency protocol is tested in its own unit suite. These
 * edges enforce admission, temporary publication, and explicit Flash saving. */
static bool tdma_service_apply_stopped_configuration(tdma_service_service_t *service,
    bool (*apply)(void *), void *context)
{
    assert(service == &owner && !owner.guard);
    if (owner.running || owner.stopped_update || owner.applying) return false;
    owner.guard = true;
    bool result = apply(context);
    owner.guard = false;
    return result;
}
static bool tdma_service_prepare_stopped_update(tdma_service_service_t *service,
    uint32_t token, bool (*prepare)(void *), void *context, uint32_t *generation)
{
    assert(service == &owner && !owner.guard);
    if (owner.running || owner.stopped_update || owner.applying) return false;
    owner.guard = true;
    bool result = prepare(context);
    if (result) {
        owner.stopped_update = token;
        *generation = ++owner.generation;
    }
    owner.guard = false;
    return result;
}
static bool tdma_service_claim_stopped_update_core1(tdma_service_service_t *service,
    uint32_t *token, uint32_t *generation)
{
    assert(service == &owner && !owner.guard);
    if (!owner.stopped_update || owner.applying) return false;
    owner.applying = true;
    *token = owner.stopped_update;
    *generation = owner.generation;
    return true;
}
static bool tdma_service_finish_stopped_update_core1(tdma_service_service_t *service,
    uint32_t token, uint32_t generation)
{
    assert(service == &owner && owner.applying);
    assert(token == owner.stopped_update && generation == owner.generation);
    owner.applying = false;
    owner.stopped_update = 0;
    return true;
}
bool product_config_set_realtime_profile(const product_config_realtime_profile_t *in)
{
    assert(owner.guard && !owner.running && !owner.stopped_update && !owner.applying);
    ++flash_writes;
    if (flash_fail) return false;
    durable = *in;
    stored_valid = true;
    return true;
}
bool product_config_clear_realtime_profile(void)
{
    assert(owner.guard && !owner.running && !owner.stopped_update && !owner.applying);
    ++flash_writes;
    if (flash_fail) return false;
    stored_valid = false;
    return true;
}
bool product_config_get_realtime_profile(product_config_realtime_profile_t *out)
{ if (!stored_valid) return false; *out = durable; return true; }
'''
    harness += core1 + "\n" + transactions
    harness += r'''
static app_realtime_profile_status_t status(void)
{
    app_realtime_profile_status_t out;
    assert(app_realtime_profile_status_get(&out));
    return out;
}
static void draft(uint32_t extra)
{
    assert(app_realtime_profile_stage_begin());
    assert(app_realtime_profile_stage_cycle(PROJECT_CORE1_CYCLE_CYCLES + extra));
    assert(app_realtime_profile_stage_validate(NULL));
}
static void apply_and_service(void)
{
    assert(app_realtime_profile_apply(NULL, NULL));
    assert(app_realtime_apply_pending_profile_core1());
}
static void assert_default(void)
{
    assert(!app_realtime_profile_override_active());
    assert(s_realtime_schedule.cycle_cycles == PROJECT_CORE1_CYCLE_CYCLES);
    assert(s_realtime_schedule.profile_generation == 0);
}
static void boot_reset(void)
{
    assert(app_realtime_profile_override_clear());
    assert(app_realtime_profile_stage_discard());
    memset(&owner, 0, sizeof(owner));
    memset(&s_realtime_schedule, 0, sizeof(s_realtime_schedule));
    assert(app_realtime_profile_install(&s_realtime_schedule,
        PROJECT_CORE1_CYCLE_CYCLES, 0));
    s_realtime_schedule.phase_count = APP_REALTIME_PHASE_COUNT;
    s_realtime_profile_persisted = false;
    s_realtime_profile_reject = (app_realtime_profile_rejection_t)
        APP_REALTIME_PROFILE_REJECTION_INIT;
    schedule_writes = flash_writes = 0;
    booting = true;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    unsigned which = (unsigned)strtoul(argv[1], NULL, 10);
    uint32_t code = 0, phase = 0;
    boot_reset();
    booting = false;
    if (which == 0) {
        assert(!app_realtime_profile_apply(&code, &phase));
        assert(code == APP_REALTIME_PROFILE_REJECT_NULL);
        assert(phase == APP_REALTIME_PHASE_COUNT);
        assert(!flash_writes && !owner.stopped_update && !status().staged);
        assert_default();
    } else if (which == 1) {
        draft(1000);
        for (unsigned mode = 0; mode < 4; ++mode) {
            ready = mode != 0;
            owner.running = mode == 1;
            owner.stopped_update = mode == 2 ? 1234 : 0;
            owner.applying = mode == 3;
            uint32_t token = owner.stopped_update;
            assert(!app_realtime_profile_apply(&code, &phase));
            assert(code == APP_REALTIME_PROFILE_REJECT_PUBLISH);
            assert(!app_realtime_profile_reset(&code, &phase));
            assert(code == APP_REALTIME_PROFILE_REJECT_PUBLISH);
            assert(!app_realtime_profile_save(&code, &phase));
            assert(code == APP_REALTIME_PROFILE_REJECT_PUBLISH);
            assert(!flash_writes && owner.stopped_update == token);
            assert(status().staged_dirty && status().staged_valid);
            assert_default();
        }
    } else if (which == 2) {
        draft(1000);
        apply_and_service();
        assert(!flash_writes && !status().persisted);
        assert(app_realtime_profile_save(&code, &phase));
        assert(flash_writes == 1 && status().persisted);
        app_realtime_schedule_snapshot_t previous = s_realtime_schedule;
        product_config_realtime_profile_t previous_durable = durable;
        draft(2000);
        flash_fail = true;
        assert(!app_realtime_profile_save(&code, &phase));
        assert(code == APP_REALTIME_PROFILE_REJECT_PERSIST);
        assert(status().staged_dirty && status().staged_valid);
        assert(status().configured && !status().persisted);
        assert(!memcmp(&previous, &s_realtime_schedule, sizeof(previous)));
        assert(!memcmp(&previous_durable, &durable, sizeof(durable)));
        app_realtime_profile_table_t active;
        assert(app_realtime_profile_table_effective(&active));
        assert(active.cycle_cycles == previous.cycle_cycles);
        assert(!owner.stopped_update && flash_writes == 2);
        /* Flash failure does not block subsequent temporary APPLY. */
        apply_and_service();
        assert(s_realtime_schedule.cycle_cycles == PROJECT_CORE1_CYCLE_CYCLES + 2000);
        assert(flash_writes == 2 && !status().persisted);
    } else if (which == 3) {
        draft(1000);
        app_realtime_schedule_snapshot_t previous = s_realtime_schedule;
        assert(app_realtime_profile_apply(&code, &phase));
        assert(code == APP_REALTIME_PROFILE_REJECT_OK && !status().staged);
        assert(status().configured && !status().persisted);
        assert(owner.stopped_update == PROJECT_CORE1_CYCLE_CYCLES + 1000);
        assert(!stored_valid);
        assert(!memcmp(&previous, &s_realtime_schedule, sizeof(previous)));
        assert(!schedule_writes && !flash_writes);
        offline = true;
        assert(!app_realtime_apply_pending_profile_core1());
        assert(owner.stopped_update && !schedule_writes);
        offline = false;
        assert(app_realtime_apply_pending_profile_core1());
        assert(s_realtime_schedule.cycle_cycles == PROJECT_CORE1_CYCLE_CYCLES + 1000);
        assert(s_realtime_schedule.profile_generation == owner.generation);
        assert(!owner.stopped_update && schedule_writes == 1);
        assert(!app_realtime_apply_pending_profile_core1());
    } else if (which == 4) {
        draft(1000);
        apply_and_service();
        assert(app_realtime_profile_save(NULL, NULL));
        draft(2000);
        app_realtime_schedule_snapshot_t previous = s_realtime_schedule;
        flash_fail = true;
        assert(app_realtime_profile_reset(&code, &phase));
        assert(code == APP_REALTIME_PROFILE_REJECT_OK);
        assert(!status().configured && !status().persisted && !status().staged);
        assert(stored_valid && owner.stopped_update == PROJECT_CORE1_CYCLE_CYCLES);
        assert(!memcmp(&previous, &s_realtime_schedule, sizeof(previous)));
        assert(app_realtime_apply_pending_profile_core1());
        assert(s_realtime_schedule.cycle_cycles == PROJECT_CORE1_CYCLE_CYCLES);
        assert(s_realtime_schedule.profile_generation == 2);
        assert(flash_writes == 1 && !status().persisted);
        boot_reset();
        assert(app_realtime_profile_restore());
        assert(s_realtime_schedule.cycle_cycles == PROJECT_CORE1_CYCLE_CYCLES + 1000);
    } else if (which >= 5 && which <= 8) {
        if (which != 8) {
            draft(1000);
            apply_and_service();
            assert(app_realtime_profile_save(NULL, NULL));
        }
        boot_reset();
        if (which == 6) durable.wcet_cycles[APP_REALTIME_PHASE_GUARD] = 1;
        if (which == 7) --durable.phase_count;
        bool restored = app_realtime_profile_restore();
        assert(!flash_writes && !owner.stopped_update);
        if (which == 5) {
            assert(restored && status().configured && status().persisted);
            assert(s_realtime_schedule.cycle_cycles == durable.cycle_cycles);
            assert(s_realtime_schedule.profile_generation == 0);
            assert(schedule_writes == 1);
        } else {
            assert(!restored && !schedule_writes);
            assert_default();
            if (which == 8) {
                assert(!status().persisted);
                assert(status().reject_code == APP_REALTIME_PROFILE_REJECT_OK);
            } else {
                assert(!status().persisted);
                assert(status().reject_code != APP_REALTIME_PROFILE_REJECT_OK);
            }
        }
    } else if (which == 9) {
        draft(1000);
        assert(app_realtime_profile_stage_phase(APP_REALTIME_PHASE_GUARD,
            PROJECT_CORE1_PHASE_GUARD_START_CYCLE,
            PROJECT_CORE1_CYCLE_CYCLES + 1000, 1));
        assert(!app_realtime_profile_apply(&code, &phase));
        assert(code == APP_REALTIME_PROFILE_REJECT_GUARD_WCET);
        assert(phase == APP_REALTIME_PHASE_GUARD);
        assert(status().staged_dirty && !status().staged_valid);
        assert(!flash_writes && !owner.stopped_update);
        assert_default();
    } else if (which == 10) {
        /* A budget-only edit keeps the 1.5 ms cycle and still needs a new
         * owner generation; equality of cycle length must not drop it. */
        assert(app_realtime_profile_stage_begin());
        assert(app_realtime_profile_stage_phase(APP_REALTIME_PHASE_VDC,
            PROJECT_CORE1_PHASE_VDC_START_CYCLE,
            PROJECT_CORE1_PHASE_VDC_END_CYCLE,
            PROJECT_CORE1_PHASE_VDC_WCET_CYCLES - 1));
        assert(app_realtime_profile_apply(&code, &phase));
        assert(owner.stopped_update == PROJECT_CORE1_CYCLE_CYCLES);
        assert(!flash_writes && !stored_valid && !status().persisted);
        assert(s_realtime_schedule.phase_wcet_cycles[APP_REALTIME_PHASE_VDC] ==
            PROJECT_CORE1_PHASE_VDC_WCET_CYCLES);
        assert(app_realtime_apply_pending_profile_core1());
        assert(s_realtime_schedule.cycle_cycles == PROJECT_CORE1_CYCLE_CYCLES);
        assert(s_realtime_schedule.profile_generation == 1);
        assert(s_realtime_schedule.phase_wcet_cycles[APP_REALTIME_PHASE_VDC] ==
            PROJECT_CORE1_PHASE_VDC_WCET_CYCLES - 1);
    } else if (which == 11) {
        /* An unsaved change disappears on reboot, even with no older record. */
        draft(1000);
        apply_and_service();
        assert(!flash_writes && !status().persisted);
        boot_reset();
        assert(!app_realtime_profile_restore());
        assert_default();
    } else if (which == 12) {
        draft(1000);
        apply_and_service();
        assert(app_realtime_profile_save(NULL, NULL));
        draft(2000);
        apply_and_service();
        assert(!status().persisted && flash_writes == 1);
        boot_reset();
        assert(app_realtime_profile_restore());
        assert(s_realtime_schedule.cycle_cycles == PROJECT_CORE1_CYCLE_CYCLES + 1000);
        assert(status().persisted && !flash_writes);
    } else if (which == 13) {
        /* SAVE uses the applied table even when another valid draft exists. */
        draft(1000);
        apply_and_service();
        draft(2000);
        app_realtime_schedule_snapshot_t before = s_realtime_schedule;
        assert(app_realtime_profile_save(&code, &phase));
        assert(code == APP_REALTIME_PROFILE_REJECT_OK);
        assert(durable.cycle_cycles == PROJECT_CORE1_CYCLE_CYCLES + 1000);
        assert(status().staged_dirty && status().persisted);
        assert(!memcmp(&before, &s_realtime_schedule, sizeof(before)));
        assert(!owner.stopped_update && flash_writes == 1);
    } else if (which == 14) {
        /* Equal cycle length does not hide changed budgets from persisted. */
        assert(app_realtime_profile_save(NULL, NULL));
        assert(status().persisted);
        assert(app_realtime_profile_stage_begin());
        assert(app_realtime_profile_stage_phase(APP_REALTIME_PHASE_VDC,
            PROJECT_CORE1_PHASE_VDC_START_CYCLE,
            PROJECT_CORE1_PHASE_VDC_END_CYCLE,
            PROJECT_CORE1_PHASE_VDC_WCET_CYCLES - 1));
        assert(app_realtime_profile_apply(NULL, NULL));
        assert(!status().persisted);
        assert(!app_realtime_profile_save(&code, &phase));
        assert(code == APP_REALTIME_PROFILE_REJECT_PUBLISH && flash_writes == 1);
        assert(app_realtime_apply_pending_profile_core1());
        assert(!status().persisted);
        assert(app_realtime_profile_save(NULL, NULL));
        assert(status().persisted && flash_writes == 2);
        assert(durable.wcet_cycles[APP_REALTIME_PHASE_VDC] ==
            PROJECT_CORE1_PHASE_VDC_WCET_CYCLES - 1);
    } else if (which == 15) {
        /* SAVE respects a legacy selected runtime cycle, not effective default. */
        assert(app_realtime_profile_install(&s_realtime_schedule,
            PROJECT_CORE1_PROFILE_5MS_CYCLES, 7));
        assert(app_realtime_profile_save(NULL, NULL));
        assert(durable.cycle_cycles == PROJECT_CORE1_PROFILE_5MS_CYCLES);
        assert(s_realtime_schedule.profile_generation == 7 && status().persisted);
        boot_reset();
        assert(app_realtime_profile_restore());
        assert(s_realtime_schedule.cycle_cycles == PROJECT_CORE1_PROFILE_5MS_CYCLES);
    } else if (which == 16) {
        /* Completion after snapshot read cannot mark the old table saved. */
        assert(app_realtime_profile_save(NULL, NULL));
        draft(1000);
        assert(app_realtime_profile_apply(NULL, NULL));
        settle_during_snapshot = true;
        assert(!status().persisted);
        assert(!owner.stopped_update);
        assert(!status().persisted && flash_writes == 1);
    } else if (which == 17) {
        /* A cancelled legacy request advances requested generation only. */
        owner.generation = 5;
        assert(s_realtime_schedule.profile_generation == 0);
        assert(app_realtime_profile_save(NULL, NULL));
        assert(status().persisted && flash_writes == 1);
    } else {
        assert(!"unknown case");
    }
    puts("profile transaction passed");
    return 0;
}
'''
    source = tmp_path / "profile_transaction.c"
    source.write_text(harness, encoding="utf-8")
    executable = tmp_path / "profile_transaction.exe"
    compiler = (os.environ.get("HOST_CC") or shutil.which("gcc") or
                shutil.which("clang") or "D:/Microsoft/mingw64/bin/gcc.exe")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I" + str(ROOT / "application/inc"),
               "-I" + str(ROOT / "components/product_config/inc"),
               "-I" + str(ROOT / "config"), str(source),
               str(ROOT / "application/src/app_realtime_profile.c"),
               "-o", str(executable)]
    result = subprocess.run(command, text=True, capture_output=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize("case", range(18), ids=[
    "no-draft", "owner-admission", "flash-failure", "core1-publication",
    "reset-transaction", "boot-valid", "boot-invalid-table",
    "boot-invalid-record", "boot-unconfigured", "invalid-draft", "same-cycle-budget",
    "unsaved-first-boot", "unsaved-retains-previous", "save-ignores-draft",
    "saved-budget-comparison", "save-legacy-active-cycle",
    "status-completion-race", "save-after-cancelled-request",
])
def test_profile_transaction(profile_transaction_executable, case):
    result = subprocess.run([str(profile_transaction_executable), str(case)],
                            text=True, capture_output=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "profile transaction passed" in result.stdout
