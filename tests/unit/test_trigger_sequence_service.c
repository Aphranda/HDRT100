#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "trigger_sequence_service.h"
#include "sync_io_sequence.h"

/* Seed boundary counters without introducing firmware test hooks. */
#include "../../components/sync_trigger/src/trigger_sequence_service.c"

static sync_io_sequence_snapshot_t hw;
static sync_io_sequence_config_t hw_config;
static uint32_t hw_values[TRIGGER_SEQUENCE_STATE_MAX];
static bool legacy_idle = true, available = true, arm_success = true;
static bool software_success = true, pause_success = true, reserved;
static bool stop_during_arm, stop_during_step;
static bool preserve_failed_arm_receipts;
static bool fault_during_stop;
static bool probe_snapshot_ownership;
static unsigned snapshot_probes;
static uint32_t writes, output;
static int locked;

void osal_critical_enter(void) { assert(!locked); locked = 1; }
void osal_critical_exit(void) { assert(locked); locked = 0; }
bool sync_trigger_sequence_can_start(void) { assert(!locked); return legacy_idle; }
bool sync_io_sequence_reserve(void)
{ assert(!locked); if (!available) return false; reserved = true; return true; }
void sync_io_sequence_release(void) { assert(!locked); reserved = false; }

static void probe_immutable_snapshot(void)
{
    if (!probe_snapshot_ownership) return;
    run_config_t before = s_run;
    assert(trigger_sequence_service_start(NULL) == TRIGGER_SEQUENCE_SERVICE_FROZEN);
    assert(trigger_sequence_service_set_code(0, 3) == TRIGGER_SEQUENCE_SERVICE_FROZEN);
    trigger_sequence_params_t params = {1, 0, 1, 1};
    assert(trigger_sequence_configure(trigger_sequence_service_config(), &params) == TRIGGER_SEQUENCE_FROZEN);
    assert(memcmp(&before, &s_run, sizeof(before)) == 0);
    ++snapshot_probes;
}

bool sync_io_sequence_arm_plan(const sync_io_sequence_config_t *config,
                               const uint32_t *values, uint32_t count)
{
    assert(!locked && reserved && count);
    probe_immutable_snapshot();
    sync_io_sequence_snapshot_t previous = hw;
    memset(&hw, 0, sizeof(hw));
    hw_config = *config;
    memcpy(hw_values, values, count * sizeof(*values));
    hw.plan_count = count;
    hw.tick_ns = 100;
    hw.timing_kind = TRIGGER_SEQUENCE_TIMING_PIO0;
    hw.current_index = hw.completed_index = UINT32_MAX;
    hw.armed = hw.ready = arm_success;
    hw.rejection_counts_pending = config->input_channel != 0;
    if (!arm_success) {
        if (preserve_failed_arm_receipts) hw = previous;
        reserved = false;
        hw.fault = 1;
    }
    if (stop_during_arm) {
        stop_during_arm = false;
        assert(trigger_sequence_service_stop() == TRIGGER_SEQUENCE_SERVICE_OK);
    }
    return arm_success;
}

static void accept(void)
{
    assert(hw.armed && hw.ready && !hw.busy && !hw.pending && !hw.paused);
    if (hw.accepted == UINT32_MAX) { hw.fault = 1; hw.ready = false; return; }
    hw.current_index = hw.accepted % hw.plan_count;
    ++hw.accepted;
    hw.ready = false;
    hw.pending = true;
}

bool sync_io_sequence_software_step(void)
{
    assert(!locked);
    if (!software_success || hw_config.input_channel || !hw.ready || hw.paused) return false;
    accept();
    if (stop_during_step) {
        stop_during_step = false;
        assert(trigger_sequence_service_stop() == TRIGGER_SEQUENCE_SERVICE_OK);
    }
    return hw.fault == 0;
}

static void physical_write(void)
{
    assert(hw.pending);
    hw.pending = false;
    hw.busy = true;
    ++hw.written;
    ++writes;
    output = hw_values[hw.current_index];
}

static void physical_complete(void)
{
    assert(hw.busy);
    hw.busy = false;
    ++hw.completed;
    hw.completed_index = hw.current_index;
    hw.ready = !hw.paused;
    hw.rejection_counts_pending = !hw.paused && hw_config.input_channel != 0;
}

bool sync_io_sequence_pause(bool paused)
{
    assert(!locked);
    if (!pause_success) return false;
    hw.paused = paused;
    hw.rejection_counts_pending = hw_config.input_channel != 0 &&
        (!paused || hw.busy || hw.pending);
    hw.ready = !paused && !hw.busy && !hw.pending && hw.armed;
    return true;
}
void sync_io_sequence_stop(void)
{
    assert(!locked);
    probe_immutable_snapshot();
    hw.cancelled = hw.accepted - hw.completed;
    hw.rejection_counts_pending = false;
    if (fault_during_stop) hw.fault = SYNC_IO_SEQUENCE_FAULT_RECEIPT;
    hw.busy = hw.ready = hw.pending = hw.armed = false;
    reserved = false;
    output = 0;
}
void sync_io_sequence_service(void) { assert(!locked); }
void sync_io_sequence_get_snapshot(sync_io_sequence_snapshot_t *snapshot)
{ assert(!locked); *snapshot = hw; }

static trigger_sequence_service_status_t status(void)
{
    trigger_sequence_service_status_t snapshot;
    trigger_sequence_service_get_status(&snapshot);
    return snapshot;
}

static void complete(void)
{
    if (hw.pending) physical_write();
    physical_complete();
    trigger_sequence_service_service();
}

static void edge(void)
{
    ++hw.input_events;
    if (hw.ready && !hw.paused) accept();
    else if (hw.busy || hw.pending) ++hw.busy_rejected;
    else ++hw.notready_rejected;
}

static void setup(void)
{
    memset(&hw, 0, sizeof(hw));
    writes = output = 0;
    legacy_idle = available = arm_success = software_success = pause_success = true;
    reserved = stop_during_arm = stop_during_step = false;
    preserve_failed_arm_receipts = false;
    fault_during_stop = false;
    probe_snapshot_ownership = false;
    snapshot_probes = 0;
    trigger_sequence_service_init();
    trigger_sequence_store_t *store = trigger_sequence_service_config();
    trigger_sequence_params_t params = {2, 0, 1, 1};
    const uint32_t ids[] = {1, 0};
    assert(trigger_sequence_configure(store, &params) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_write_plan(store, "A", ids, 2) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_activate(store, "A") == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_service_set_io(3, 4, 10, 5) == TRIGGER_SEQUENCE_SERVICE_OK);
    assert(trigger_sequence_service_set_code(0, 1) == TRIGGER_SEQUENCE_SERVICE_OK);
    assert(trigger_sequence_service_set_code(1, 2) == TRIGGER_SEQUENCE_SERVICE_OK);
}

static void start(void)
{
    assert(trigger_sequence_service_start(NULL) == TRIGGER_SEQUENCE_SERVICE_OK);
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_STARTING);
    assert(trigger_sequence_service_config()->frozen && !hw.armed);
    trigger_sequence_service_service();
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_READY && hw.armed);
    assert(status().tick_ns == 100 && status().timing_kind == TRIGGER_SEQUENCE_TIMING_PIO0);
}

static void stop(void)
{
    assert(trigger_sequence_service_stop() == TRIGGER_SEQUENCE_SERVICE_OK);
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_STOPPING);
    assert(trigger_sequence_service_config()->frozen);
    trigger_sequence_service_service();
    assert(!trigger_sequence_service_config()->frozen);
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_IDLE && !reserved);
}

static void test_bus_receipt_lifecycle(void)
{
    setup();
    start();
    assert(writes == 0 && status().current_index == UINT32_MAX);
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_OK);
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_BUSY);
    trigger_sequence_service_service();
    assert(status().accepted == 1 && status().executed_index == UINT32_MAX && writes == 0);
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_BUSY);
    physical_write();
    trigger_sequence_service_service();
    assert(status().executed_state == 1 && output == 2 && status().completed == 0);
    assert(trigger_sequence_service_pause() == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_PAUSING);
    complete();
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_PAUSED && status().completed_state == 1);
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_NOT_READY);
    assert(trigger_sequence_service_continue() == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    assert(writes == 1);
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    complete();
    assert(status().next_index == 0 && status().cycles == 0 && output == 1);
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    assert(status().cycles == 1);
    stop();
    assert(status().accepted == 3 && status().cancelled == 1 && status().completed == 2);
    assert(status().busy_rejected == 2 && status().notready_rejected == 1);
    start();
    assert(status().run_id == 2 && status().accepted == 0 && status().busy_rejected == 0);
    stop();
}

static void test_external_autonomous_batches(void)
{
    for (uint32_t channel = 1; channel <= 4; ++channel) {
        setup();
        assert(trigger_sequence_service_set_source(channel, true) == TRIGGER_SEQUENCE_SERVICE_OK);
        start();
        assert(hw_config.input_channel == channel && hw_config.falling);
        assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_SOURCE_MISMATCH);
        /* Entire runs execute while the CPU owner is absent. */
        for (uint32_t i = 0; i < 7; ++i) {
            edge();
            physical_write();
            physical_complete();
        }
        assert(status().accepted == 0);
        trigger_sequence_service_service();
        assert(status().accepted == 7 && status().completed == 7 && status().cycles == 3);
        assert(status().current_state == 1 && status().executed_state == 1 && status().completed_state == 1);
        assert(status().written_at_us == 0 && status().completed_at_us == 0);
        edge();
        assert(trigger_sequence_service_pause() == TRIGGER_SEQUENCE_SERVICE_OK);
        trigger_sequence_service_service();
        assert(status().state == TRIGGER_SEQUENCE_SERVICE_PAUSING);
        assert(status().rejection_counts_pending);
        edge();
        complete();
        edge();
        trigger_sequence_service_service();
        assert(status().state == TRIGGER_SEQUENCE_SERVICE_PAUSED && status().completed == 8);
        assert(!status().rejection_counts_pending);
        assert(status().busy_rejected == 1 && status().notready_rejected == 2);
        stop();
    }
}

static void test_configuration_faults_stop_priority(void)
{
    setup();
    assert(trigger_sequence_service_set_io(8, 4, 0, 1) == TRIGGER_SEQUENCE_SERVICE_INVALID);
    assert(trigger_sequence_service_set_io(1, 4, 0, SYNC_IO_SEQUENCE_TIME_MAX_US + 1u) == TRIGGER_SEQUENCE_SERVICE_INVALID);
    assert(trigger_sequence_service_set_code(0, 8) == TRIGGER_SEQUENCE_SERVICE_INVALID);
    assert(trigger_sequence_service_set_source(5, false) == TRIGGER_SEQUENCE_SERVICE_INVALID);
    trigger_sequence_store_t *store = trigger_sequence_service_config();
    const uint32_t ids[] = {0};
    assert(trigger_sequence_write_plan(store, "B", ids, 1) == TRIGGER_SEQUENCE_OK);
    available = false;
    assert(trigger_sequence_service_start("B") == TRIGGER_SEQUENCE_SERVICE_RESOURCE);
    assert(strcmp(trigger_sequence_get_plan(store, NULL)->id, "A") == 0 && !store->frozen);
    available = true;
    arm_success = false;
    assert(trigger_sequence_service_start(NULL) == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_FAULT && status().backend_fault == 1);
    stop();
    arm_success = true;
    start();
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    physical_write();
    hw.fault = 2;
    trigger_sequence_service_service();
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_FAULT);
    assert(status().executed_state == 1 && status().completed == 0 && status().cancelled == 1);
    stop();

    setup();
    assert(trigger_sequence_service_start(NULL) == TRIGGER_SEQUENCE_SERVICE_OK);
    assert(trigger_sequence_service_stop() == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_IDLE && !reserved && writes == 0);
    start();
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_OK);
    stop();
    assert(status().accepted == 1 && status().cancelled == 1 && writes == 0);

    setup();
    stop_during_arm = true;
    assert(trigger_sequence_service_start(NULL) == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_IDLE && !reserved);
    setup();
    start();
    stop_during_step = true;
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_IDLE && status().cancelled == 1);
    assert(writes == 0 && output == 0);
}

static void test_counter_and_corrupt_receipts(void)
{
    setup();
    assert(trigger_sequence_service_start(NULL) == TRIGGER_SEQUENCE_SERVICE_OK);
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_BUSY);
    trigger_sequence_service_service();
    assert(status().busy_rejected == 1);
    hw.accepted = hw.written = hw.completed = UINT32_MAX;
    hw.current_index = hw.completed_index = 0;
    trigger_sequence_service_service();
    assert(status().accepted == UINT32_MAX);
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_EXHAUSTED);
    trigger_sequence_service_service();
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_FAULT && writes == 0);
    s_busy_rejected = s_notready_rejected = UINT32_MAX;
    s_published.busy_rejected = s_published.notready_rejected = 1;
    assert(status().busy_rejected == UINT32_MAX && status().notready_rejected == UINT32_MAX);
    stop();

    setup();
    start();
    hw.completed = 1;
    trigger_sequence_service_service();
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_FAULT && status().completed == 0);
    stop();
}

static void test_prefill_and_stop_invalid_receipt(void)
{
    setup();
    start();
    hw.ready = false;
    trigger_sequence_service_service();
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_STARTING);
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_NOT_READY);
    hw.ready = true;
    trigger_sequence_service_service();
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_READY);
    hw.completed = 1;
    stop();
    assert(status().error == TRIGGER_SEQUENCE_SERVICE_BACKEND && status().faults == 1);
}

static void test_receipt_index_and_written_regressions(void)
{
    for (unsigned corruption = 0; corruption < 3; ++corruption) {
        setup();
        start();
        assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_OK);
        trigger_sequence_service_service();
        physical_write();
        trigger_sequence_service_service();
        assert(status().accepted == 1 && status().executed_index == 0);
        if (corruption == 0) hw.current_index = 1;
        if (corruption == 1) { physical_complete(); hw.completed_index = 1; }
        if (corruption == 2) hw.written = 0;
        trigger_sequence_service_service();
        assert(status().state == TRIGGER_SEQUENCE_SERVICE_FAULT);
        assert(status().completed == 0 && status().executed_index == 0);
        stop();
    }
}

static void test_failed_arm_never_relabels_old_receipts(void)
{
    setup();
    start();
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    complete();
    stop();
    assert(status().completed == 1);
    arm_success = false;
    preserve_failed_arm_receipts = true;
    assert(trigger_sequence_service_start(NULL) == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_FAULT && status().run_id == 2);
    assert(status().accepted == 0 && status().completed == 0);
    assert(status().completed_index == UINT32_MAX);
    stop();
    assert(status().completed == 0);
}

static void test_rejection_settlement_and_stop_fault(void)
{
    setup();
    assert(trigger_sequence_service_set_source(1, false) == TRIGGER_SEQUENCE_SERVICE_OK);
    start();
    assert(status().rejection_counts_pending);
    hw.busy_rejected = 4;
    assert(trigger_sequence_service_pause() == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    assert(!status().rejection_counts_pending && status().busy_rejected == 4);
    assert(trigger_sequence_service_continue() == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    assert(status().rejection_counts_pending);
    fault_during_stop = true;
    stop();
    assert(!status().rejection_counts_pending && status().state == TRIGGER_SEQUENCE_SERVICE_IDLE);
    assert(status().error == TRIGGER_SEQUENCE_SERVICE_BACKEND && status().faults == 1);
    assert(status().backend_fault == SYNC_IO_SEQUENCE_FAULT_RECEIPT);
}

static void test_one_snapshot_mailbox_lifetime(void)
{
    setup();
    probe_snapshot_ownership = true;
    assert(trigger_sequence_service_start(NULL) == TRIGGER_SEQUENCE_SERVICE_OK);
    probe_immutable_snapshot();
    trigger_sequence_service_service();
    assert(status().state == TRIGGER_SEQUENCE_SERVICE_READY);
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    complete();
    stop();
    assert(snapshot_probes >= 3);
    /* Once STOP is published, the same memory may hold a different plan. */
    const uint32_t ids[] = {0};
    assert(trigger_sequence_write_plan(trigger_sequence_service_config(), "B", ids, 1) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_service_set_code(0, 3) == TRIGGER_SEQUENCE_SERVICE_OK);
    assert(trigger_sequence_service_start("B") == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    assert(status().count == 1 && status().run_id == 2);
    assert(hw_values[0] == 3);
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    complete();
    assert(status().completed_state == 0 && output == 3);
    stop();
}

static void test_compact_code_bounds_without_truncation(void)
{
    setup();
    trigger_sequence_store_t *store = trigger_sequence_service_config();
    trigger_sequence_params_t params = {1, 0, TRIGGER_SEQUENCE_STATE_MAX, 1};
    uint32_t id = TRIGGER_SEQUENCE_STATE_MAX - 1u;
    assert(trigger_sequence_configure(store, &params) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_write_plan(store, "MAX", &id, 1) == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_activate(store, "MAX") == TRIGGER_SEQUENCE_OK);
    assert(trigger_sequence_service_set_io(7, 4, 10, 5) == TRIGGER_SEQUENCE_SERVICE_OK);
    assert(trigger_sequence_service_set_code(id, 7) == TRIGGER_SEQUENCE_SERVICE_OK);
    assert(trigger_sequence_service_set_code(id, 0x107) == TRIGGER_SEQUENCE_SERVICE_INVALID);
    assert(trigger_sequence_service_set_code(id, UINT32_MAX) == TRIGGER_SEQUENCE_SERVICE_INVALID);
    uint32_t value = UINT32_MAX;
    assert(trigger_sequence_service_get_code(id, &value) && value == 7);
    assert(sizeof(s_codes) == TRIGGER_SEQUENCE_STATE_MAX);
    start();
    assert(hw_values[0] == 7);
    assert(trigger_sequence_service_step() == TRIGGER_SEQUENCE_SERVICE_OK);
    trigger_sequence_service_service();
    complete();
    assert(status().completed_state == id && output == 7);
    stop();
}

int main(void)
{
    test_bus_receipt_lifecycle();
    test_external_autonomous_batches();
    test_configuration_faults_stop_priority();
    test_counter_and_corrupt_receipts();
    test_prefill_and_stop_invalid_receipt();
    test_receipt_index_and_written_regressions();
    test_failed_arm_never_relabels_old_receipts();
    test_rejection_settlement_and_stop_fault();
    test_one_snapshot_mailbox_lifetime();
    test_compact_code_bounds_without_truncation();
    puts("sequence service lifecycle passed");
    return 0;
}
