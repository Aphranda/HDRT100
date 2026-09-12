#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tdma_service.h"
#include "tdma_pio_spi_phys.h"
#include "resource_arbiter.h"
/* Include the actual arbiter for guard fault injection; no production test
 * reset API or alternate implementation is linked. Each case is a process. */
#include "../../components/resource_arbiter/src/resource_arbiter.c"

static tdma_service_service_t s_tdma_runtime_owner;
static tdma_pio_spi_phys_t s_tdma_pio_spi_phys;
static bool s_tdma_runtime_owner_initialized;
static tdma_pio_spi_program_persona_t s_tdma_pio_spi_program_persona;
static struct { uint32_t ctrl; } fake_pio;
#undef BOARD_TDMA_SPI_PIO
#define BOARD_TDMA_SPI_PIO (&fake_pio)
#define TRAIN_SMS ((1u << BOARD_TDMA_SPI_MASTER_SM) | (1u << BOARD_TDMA_SPI_SLAVE_SM))

static unsigned lock_entries;
static unsigned lock_depth;
static unsigned action;
static bool fail_stop;
static bool fail_train;
static bool complete_train;

void osal_critical_enter(void) { ++lock_entries; ++lock_depth; }
void osal_critical_exit(void) { assert(lock_depth); --lock_depth; }

static void intercepted_completion(uint32_t sequence, bool terminal);
#include "training_physical.inc"
#define tdma_service_ring_train_clock production_tdma_service_ring_train_clock
#include "training_service.inc"
#undef tdma_service_ring_train_clock
#define resource_arbiter_complete_tdma_clock_training_core1 intercepted_completion
#include "training_owner.inc"
#undef resource_arbiter_complete_tdma_clock_training_core1

static bool gate(void)
{
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);
    return snapshot.tdma_clock_training_active;
}

static void owner_phase(void)
{
    const unsigned before = lock_entries;
    tdma_ring_runtime_service(&s_tdma_runtime_owner.ring_runtime);
    tdma_runtime_owner_update_training_gate();
    assert(lock_entries == before);
}

bool tdma_service_ring_train_clock(tdma_service_service_t *service, uint32_t cycles)
{
    assert(lock_depth != 0); /* Reservation precedes command visibility. */
    assert(gate() && !resource_arbiter_can_begin_ota());
    const bool submitted = production_tdma_service_ring_train_clock(service, cycles);
    if (action == 2 || action == 3) {
        const unsigned injected = action;
        action = 0;
        assert(submitted);
        assert(gate() && !resource_arbiter_request_ota_admission());
        assert(!resource_arbiter_acquire_owned(RESOURCE_ARBITER_RESOURCE_FLASH, "during-submit"));
        if (injected == 3) {
            /* The other core may consume and even finish while the submit
             * callback has not returned its command sequence to the arbiter. */
            complete_train = true;
            owner_phase();
            assert(gate() && !resource_arbiter_request_ota_admission());
        }
    }
    return submitted;
}

static void intercepted_completion(uint32_t sequence, bool terminal)
{
    if (action == 1) {
        action = 0;
        assert(terminal);
        assert(tdma_runtime_owner_train_clock(64));
    }
    resource_arbiter_complete_tdma_clock_training_core1(sequence, terminal);
}

static bool fake_start(void *context, const tdma_ring_runtime_config_t *config)
{ (void)context; return config->enabled != 0; }
static bool fake_stop(void *context)
{
    (void)context;
    if (fail_stop) return false;
    s_tdma_pio_spi_phys.clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_IDLE;
    fake_pio.ctrl = 0;
    return true;
}
static bool fake_train(void *context, uint32_t cycles)
{
    (void)context;
    assert(cycles);
    if (fail_train) {
        /* Model the real bad-argument path: diagnostic ERROR does not
         * disable previously running coarse-training state machines. */
        s_tdma_pio_spi_phys.clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_ERROR;
        return false;
    }
    s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_COARSE;
    s_tdma_pio_spi_phys.clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_MASTER_RUNNING;
    fake_pio.ctrl = TRAIN_SMS;
    return true;
}
static void fake_train_service(void *context, uint64_t now_ns)
{
    (void)context; (void)now_ns;
    if (complete_train) {
        s_tdma_pio_spi_phys.clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_MASTER_COMPLETE;
        fake_pio.ctrl = 0;
    }
}
static bool fake_service(void *context, uint64_t now_ns, tdma_ring_adapter_status_t *status)
{ (void)context; (void)now_ns; (void)status; return true; }
static const tdma_ring_adapter_ops_t ops = {
    .start = fake_start, .stop = fake_stop, .train_clock = fake_train,
    .train_clock_service = fake_train_service, .service = fake_service,
};

static void setup(void)
{
    assert(resource_arbiter_init());
    assert(tdma_ring_runtime_init(&s_tdma_runtime_owner.ring_runtime));
    const tdma_ring_runtime_config_t config = {
        .enabled = 1, .node_count = 4, .local_slot_id = 0, .reference_slot_id = 0,
        .up_group_id = 1, .down_group_id = 2,
        .flags = TDMA_RING_FLAG_SIMULTANEOUS_UP_DOWN,
        .ring_profile_crc32 = 1, .schedule_crc32 = 2, .operating_profile_crc32 = 3,
        .baud_hz = 10000000, .cycle_period_ns = 1000000, .feedback_timeout_ns = 1000000,
        .tx_dma_channel_id = TDMA_PROFILE_DEFAULT_TX_DMA_CHANNEL_ID,
        .rx_dma_channel_id = TDMA_PROFILE_DEFAULT_RX_DMA_CHANNEL_ID,
    };
    assert(tdma_ring_runtime_configure(&s_tdma_runtime_owner.ring_runtime, &config));
    assert(tdma_ring_runtime_bind_adapter(&s_tdma_runtime_owner.ring_runtime, &ops, NULL));
    s_tdma_runtime_owner_initialized = true;
    owner_phase();
    assert(s_tdma_runtime_owner.ring_runtime.adapter_started);
    assert(!gate() && resource_arbiter_can_begin_ota());
    lock_entries = 0;
}

static void request_active(void)
{
    assert(tdma_runtime_owner_train_clock(64));
    assert(gate() && !resource_arbiter_can_begin_ota());
    owner_phase();
    assert(gate() && !resource_arbiter_can_begin_ota());
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    setup();
    tdma_ring_runtime_t *runtime = &s_tdma_runtime_owner.ring_runtime;
    const char *name = argv[1];
    if (strcmp(name, "idle") == 0) {
        for (unsigned i = 0; i != 100; ++i) tdma_runtime_owner_update_training_gate();
        assert(lock_entries == 0);
    } else if (strcmp(name, "stale") == 0) {
        action = 1;
        tdma_runtime_owner_update_training_gate();
        assert(runtime->train_command_seq != runtime->train_accepted_seq);
        assert(gate() && !resource_arbiter_request_ota_admission());
        owner_phase();
        assert(gate());
        complete_train = true;
        owner_phase();
        assert(!gate() && resource_arbiter_request_ota_admission());
    } else if (strcmp(name, "publication") == 0 || strcmp(name, "completion_during_submit") == 0) {
        action = strcmp(name, "publication") == 0 ? 2 : 3;
        assert(tdma_runtime_owner_train_clock(64));
        assert(action == 0);
        assert(gate() == !complete_train);
    } else if (strcmp(name, "reject_previous") == 0) {
        request_active();
        assert(tdma_ring_runtime_set_data_enabled(runtime, true));
        assert(!tdma_runtime_owner_train_clock(64));
        assert(runtime->train_command_seq == 1 && gate());
        owner_phase();
        assert(!gate() && !runtime->adapter_started);
    } else if (strcmp(name, "stop_pending") == 0) {
        assert(tdma_runtime_owner_train_clock(64));
        assert(tdma_ring_runtime_configure(runtime, NULL));
        assert(runtime->train_command_seq == runtime->train_accepted_seq);
        assert(runtime->train_owner_sequence == 0);
        tdma_runtime_owner_update_training_gate();
        assert(gate());
        fail_stop = true;
        owner_phase();
        assert(runtime->adapter_stop_pending && runtime->train_owner_sequence == 0 && gate());
        fail_stop = false;
        owner_phase();
        assert(!runtime->adapter_started && runtime->train_owner_sequence == 1);
        assert(!gate() && resource_arbiter_can_begin_ota());
    } else if (strcmp(name, "reset_pending") == 0) {
        assert(tdma_runtime_owner_train_clock(64));
        assert(resource_arbiter_init());
        assert(gate() && !resource_arbiter_request_ota_admission());
        resource_arbiter_complete_tdma_clock_training_core1(0, true);
        assert(gate());
        complete_train = true;
        owner_phase();
        assert(!gate());
    } else if (strcmp(name, "wrap") == 0) {
        runtime->train_command_seq = runtime->train_accepted_seq = UINT32_MAX;
        runtime->train_owner_sequence = UINT32_MAX;
        tdma_runtime_owner_update_training_gate();
        request_active();
        assert(runtime->train_command_seq == 0 && runtime->train_owner_sequence == 0);
        complete_train = true;
        owner_phase();
        assert(!gate());
    } else if (strcmp(name, "guard_wrap") == 0) {
        request_active();
        __atomic_store_n(&s_tdma_training_completion.guard, UINT32_MAX - 1, __ATOMIC_RELAXED);
        complete_train = true;
        owner_phase();
        assert(gate()); /* Unpublished/zero guard conservatively retains. */
        owner_phase();
        assert(!gate());
    } else if (strcmp(name, "compatibility") == 0) {
        request_active();
        resource_arbiter_publish_training_activity(true, false);
        assert(gate() && !resource_arbiter_can_begin_ota());
        resource_arbiter_publish_calibration_training(false);
        resource_arbiter_publish_tdma_clock_training(false);
        assert(gate());
        resource_arbiter_publish_training_activity(false, true);
        complete_train = true;
        owner_phase();
        assert(gate());
        resource_arbiter_publish_tdma_clock_training(false);
        assert(!gate());
    } else if (strcmp(name, "ota_flash") == 0) {
        assert(resource_arbiter_request_ota_admission());
        assert(!tdma_runtime_owner_train_clock(64) && runtime->train_command_seq == 0);
        resource_arbiter_release_ota_admission();
        assert(resource_arbiter_acquire_owned(RESOURCE_ARBITER_RESOURCE_FLASH, "test"));
        assert(!tdma_runtime_owner_train_clock(64) && runtime->train_command_seq == 0);
        resource_arbiter_release_owned(RESOURCE_ARBITER_RESOURCE_FLASH, "test");
        request_active();
    } else if (strcmp(name, "repeat_active") == 0) {
        request_active();
        assert(tdma_runtime_owner_train_clock(64));
        resource_arbiter_complete_tdma_clock_training_core1(1, true);
        assert(gate() && !resource_arbiter_request_ota_admission());
        owner_phase();
        assert(runtime->train_owner_sequence == 2 && gate());
        complete_train = true;
        owner_phase();
        assert(!gate());
    } else if (strcmp(name, "flash_after_policy") == 0) {
        assert(resource_arbiter_can_begin_ota()); /* Prior policy check passes. */
        request_active(); /* Another Core0 task submits before Flash ACQUIRE. */
        assert(!resource_arbiter_acquire_owned(RESOURCE_ARBITER_RESOURCE_FLASH, "flash-transaction"));
        resource_arbiter_snapshot_t snapshot;
        resource_arbiter_get_snapshot(&snapshot);
        assert(!(snapshot.active_resources & RESOURCE_ARBITER_RESOURCE_FLASH));
        assert(snapshot.last_conflict_resources == RESOURCE_ARBITER_RESOURCE_FLASH);
        assert(strcmp(snapshot.last_conflict_holder, "TDMA_CLOCK_TRAINING") == 0);
        complete_train = true;
        owner_phase();
        assert(resource_arbiter_acquire_owned(RESOURCE_ARBITER_RESOURCE_FLASH, "flash-transaction"));
    } else if (strcmp(name, "unreadable") == 0) {
        request_active();
        __atomic_store_n(&s_tdma_training_completion.guard, 3, __ATOMIC_RELAXED);
        assert(gate());
        __atomic_store_n(&s_tdma_training_completion.guard, 4, __ATOMIC_RELAXED);
        s_tdma_pio_spi_phys.clk_train_guard = 1;
        tdma_runtime_owner_update_training_gate();
        assert(gate());
    } else if (strcmp(name, "physical_reject") == 0) {
        request_active();
        fail_train = true;
        assert(tdma_runtime_owner_train_clock(64));
        owner_phase();
        assert(runtime->train_reject_count == 1);
        assert(s_tdma_pio_spi_phys.clk_train.state == TDMA_PIO_SPI_CLK_TRAIN_ERROR);
        assert(fake_pio.ctrl && gate());
        assert(tdma_ring_runtime_configure(runtime, NULL));
        owner_phase();
        assert(!gate());
    } else if (strcmp(name, "physical_terminal") == 0) {
        assert(!tdma_pio_spi_phys_clk_train_terminal_core1(NULL));
        s_tdma_pio_spi_phys.clk_train.state = 99;
        assert(!tdma_pio_spi_phys_clk_train_terminal_core1(&s_tdma_pio_spi_phys));
        s_tdma_pio_spi_phys.clk_train.state = TDMA_PIO_SPI_CLK_TRAIN_ERROR;
        s_tdma_pio_spi_program_persona = TDMA_PIO_SPI_PROGRAM_PERSONA_CLOCK_COARSE;
        fake_pio.ctrl = 1u << BOARD_TDMA_SPI_MASTER_SM;
        assert(!tdma_pio_spi_phys_clk_train_terminal_core1(&s_tdma_pio_spi_phys));
        fake_pio.ctrl = 1u << BOARD_TDMA_SPI_SLAVE_SM;
        assert(!tdma_pio_spi_phys_clk_train_terminal_core1(&s_tdma_pio_spi_phys));
        fake_pio.ctrl = 0;
        assert(tdma_pio_spi_phys_clk_train_terminal_core1(&s_tdma_pio_spi_phys));
    } else if (strcmp(name, "config_busy") == 0) {
        assert(tdma_runtime_owner_train_clock(64));
        runtime->config_guard = 1;
        owner_phase();
        assert(runtime->train_owner_sequence == 0 && gate());
        runtime->config_guard = 2;
        fail_train = true;
        owner_phase();
        assert(runtime->train_reject_count == 1 && !gate());
    } else {
        assert(!"unknown case");
    }
    assert(lock_depth == 0);
    printf("PASS %s\n", name);
    return 0;
}
