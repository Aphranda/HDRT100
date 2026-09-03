#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "resource_arbiter.h"
#include "sync_io_persona_manager.h"
#include "sync_io_persona_resources.h"
#include "tdma_state_machine_resources.h"

/* Host-only OSAL stubs.  The arbiter contract is independent of the target
 * spinlock implementation; production builds link the real OSAL port. */
void osal_critical_enter(void) {}
void osal_critical_exit(void) {}

static void expect_snapshot(bool calibration, bool tdma)
{
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);
    assert(snapshot.calibration_training_active == calibration);
    assert(snapshot.tdma_clock_training_active == tdma);
}

static void expect_resources_owned(uint32_t resources, const char *owner)
{
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);
    assert((snapshot.active_resources & resources) == resources);
    for (uint32_t bit = 0u; bit < 32u; ++bit) {
        const uint32_t mask = 1u << bit;
        if ((resources & mask) != 0u) {
            assert(snapshot.resource_owners[bit] != NULL);
            assert(strcmp(snapshot.resource_owners[bit], owner) == 0);
        }
    }
}

static void expect_resources_unowned(uint32_t resources)
{
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);
    assert((snapshot.active_resources & resources) == 0u);
    for (uint32_t bit = 0u; bit < 32u; ++bit) {
        if ((resources & (1u << bit)) != 0u) {
            assert(snapshot.resource_owners[bit] == NULL);
        }
    }
}

static void test_directional_tdma_resources(void)
{
    const uint32_t resources = TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK;
    const char *const flight_owner = "TDMA_FLIGHT_PIO";

    assert(resource_arbiter_init());
    assert(resource_arbiter_acquire_owned(resources, flight_owner));
    expect_resources_owned(resources, flight_owner);

    assert(!resource_arbiter_acquire_owned(
        RESOURCE_ARBITER_RESOURCE_TDMA_DREQ, "conflicting-persona"));
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);
    assert(snapshot.last_conflict_resources ==
           RESOURCE_ARBITER_RESOURCE_TDMA_DREQ);
    assert(strcmp(snapshot.last_conflict_owner, "conflicting-persona") == 0);
    assert(strcmp(snapshot.last_conflict_holder, flight_owner) == 0);

    resource_arbiter_release_owned(resources, "wrong-owner");
    expect_resources_owned(resources, flight_owner);

    resource_arbiter_release_owned(resources, flight_owner);
    expect_resources_unowned(resources);
}

static void test_tdma_rx_endpoint_contract(void)
{
    const tdma_state_machine_rx_endpoint_contract_t contract =
        tdma_state_machine_rx_endpoint_contract();
    assert(tdma_state_machine_rx_endpoint_contract_valid(&contract));
    assert(contract.data_output.pio == BOARD_TDMA_RX_PIO);
    assert(contract.data_output.sm == contract.data_unload.sm);
    assert(contract.data_output.fifo_direction == TDMA_STATE_MACHINE_FIFO_TX);
    assert(contract.data_unload.fifo_direction == TDMA_STATE_MACHINE_FIFO_RX);
    assert(contract.data_output.dreq_direction == TDMA_STATE_MACHINE_DREQ_TX);
    assert(contract.data_unload.dreq_direction == TDMA_STATE_MACHINE_DREQ_RX);
    assert(contract.data_output.dma_channel ==
           BOARD_TDMA_RX_DATA_OUT_DMA_CHANNEL);
    assert(contract.data_unload.dma_channel ==
           BOARD_TDMA_TX_DATA_IN_CAPTURE_DMA_CHANNEL);
    assert(contract.clock_evidence.pio == BOARD_TDMA_RX_PIO);
    assert(contract.clock_evidence.sm != contract.data_output.sm);
    assert(contract.clock_evidence.owner ==
           TDMA_STATE_MACHINE_ENDPOINT_OWNER_CORE1);
    assert(contract.clock_evidence.dma_channel ==
           TDMA_STATE_MACHINE_DMA_CHANNEL_NONE);
    assert(contract.business_rx_consumer_count == 1u);

    tdma_state_machine_rx_endpoint_contract_t invalid = contract;
    invalid.business_rx_consumer_count = 2u;
    assert(!tdma_state_machine_rx_endpoint_contract_valid(&invalid));
    invalid = contract;
    invalid.data_unload.sm = BOARD_TDMA_RX_RESERVED_CONTROL_SM;
    assert(!tdma_state_machine_rx_endpoint_contract_valid(&invalid));
    invalid = contract;
    invalid.clock_evidence.sm = contract.data_output.sm;
    assert(!tdma_state_machine_rx_endpoint_contract_valid(&invalid));
}

static void test_tdma_persona_owner_transfer(void)
{
    const char *const maintenance_owner = "TDMA_MAINTENANCE_PIO";
    const char *const flight_owner = "TDMA_FLIGHT_PIO";

    assert(resource_arbiter_init());
    assert(resource_arbiter_acquire_owned(
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK, maintenance_owner));
    expect_resources_owned(
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK, maintenance_owner);

    assert(!resource_arbiter_acquire_owned(
        TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK, flight_owner));
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);
    const uint32_t expected_overlap =
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK &
        TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK;
    assert(snapshot.last_conflict_resources == expected_overlap);
    assert(strcmp(snapshot.last_conflict_owner, flight_owner) == 0);
    assert(strcmp(snapshot.last_conflict_holder, maintenance_owner) == 0);
    expect_resources_owned(
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK, maintenance_owner);

    resource_arbiter_release_owned(
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK, maintenance_owner);
    assert(resource_arbiter_acquire_owned(
        TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK, flight_owner));
    expect_resources_owned(
        TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK, flight_owner);

    resource_arbiter_release_owned(
        TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK, flight_owner);
    assert(resource_arbiter_acquire_owned(
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK, maintenance_owner));
    expect_resources_owned(
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK, maintenance_owner);
    resource_arbiter_release_owned(
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK, maintenance_owner);
    expect_resources_unowned(
        TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK |
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK);
}

static void test_tdma_conflict_recovery_has_no_partial_lease(void)
{
    const char *const maintenance_owner = "TDMA_MAINTENANCE_PIO";
    const char *const flight_owner = "TDMA_FLIGHT_PIO";
    const char *const foreign_owner = "FOREIGN_GPIO_DREQ";
    const uint32_t foreign_resources =
        RESOURCE_ARBITER_RESOURCE_TDMA_GPIO |
        RESOURCE_ARBITER_RESOURCE_TDMA_DREQ;
    const uint32_t flight_only_resources =
        TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK &
        ~TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK;

    assert(resource_arbiter_init());
    assert(resource_arbiter_acquire_owned(
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK, maintenance_owner));
    resource_arbiter_release_owned(
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK, maintenance_owner);
    assert(resource_arbiter_acquire_owned(foreign_resources, foreign_owner));

    assert(!resource_arbiter_acquire_owned(
        TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK, flight_owner));
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);
    assert(snapshot.active_resources == foreign_resources);
    assert(snapshot.last_conflict_resources == foreign_resources);
    assert(strcmp(snapshot.last_conflict_owner, flight_owner) == 0);
    assert(strcmp(snapshot.last_conflict_holder, foreign_owner) == 0);
    expect_resources_unowned(flight_only_resources);

    resource_arbiter_release_owned(
        TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK, flight_owner);
    expect_resources_owned(foreign_resources, foreign_owner);
    resource_arbiter_release_owned(foreign_resources, foreign_owner);

    assert(resource_arbiter_acquire_owned(
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK, maintenance_owner));
    expect_resources_owned(
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK, maintenance_owner);
    expect_resources_unowned(flight_only_resources);
    resource_arbiter_release_owned(
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK, maintenance_owner);
    expect_resources_unowned(
        TDMA_STATE_MACHINE_FLIGHT_RESOURCE_MASK |
        TDMA_STATE_MACHINE_MAINTENANCE_RESOURCE_MASK);
}

static const sync_io_persona_descriptor_t *sync_persona(
    sync_io_persona_id_t id)
{
    const sync_io_persona_descriptor_t *descriptor =
        sync_io_persona_descriptor(id);
    assert(descriptor != NULL);
    assert(sync_io_persona_descriptor_valid(descriptor));
    return descriptor;
}

static void test_sync_io_persona_catalog(void)
{
    assert(sync_io_persona_catalog_valid());
    assert(sync_io_persona_descriptor_count() ==
           (size_t)SYNC_IO_PERSONA_ID_COUNT - 1u);
    assert(sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_NONE) == NULL);
    assert(sync_io_persona_descriptor(SYNC_IO_PERSONA_ID_COUNT) == NULL);
    assert(sync_io_persona_descriptor_by_index(
               sync_io_persona_descriptor_count()) == NULL);

    const sync_io_persona_descriptor_t *capture =
        sync_persona(SYNC_IO_PERSONA_ID_INPUT_CAPTURE);
    assert(capture->implementation ==
           SYNC_IO_PERSONA_IMPLEMENTATION_CURRENT);
    assert(capture->pio_block_id == BOARD_TDMA_SMA_PIO_BLOCK_ID);
    assert(capture->sm_mask ==
           (1u << BOARD_SYNC_PIO0_INPUT_CAPTURE_SM));
    assert(capture->rx_fifo_sm_mask == capture->sm_mask);
    assert(capture->tx_fifo_sm_mask == 0u);
    assert(capture->dma_channel_mask ==
           (1u << SYNC_IO_CAPTURE_DMA_CH));
    assert(capture->rx_dreq_sm_mask == capture->sm_mask);
    assert(capture->workspace_mask ==
           SYNC_IO_PERSONA_WORKSPACE_CAPTURE_SCHEDULE);

    const sync_io_persona_descriptor_t *scheduled =
        sync_persona(SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER);
    assert(scheduled->implementation ==
           SYNC_IO_PERSONA_IMPLEMENTATION_MIGRATION_TARGET);
    assert(scheduled->sm_mask ==
           (1u << BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM));
    assert(scheduled->tx_fifo_sm_mask == scheduled->sm_mask);
    assert(scheduled->dma_channel_mask ==
           (1u << SYNC_IO_MODEL_PULSE_DMA_CH));
    assert(scheduled->tx_dreq_sm_mask == scheduled->sm_mask);
    assert(scheduled->safe_low_gpio_mask != 0u);

    const sync_io_persona_descriptor_t *wave =
        sync_persona(SYNC_IO_PERSONA_ID_WAVE_OUTPUT);
    assert(wave->implementation ==
           SYNC_IO_PERSONA_IMPLEMENTATION_MIGRATION_TARGET);
    assert(wave->sm_mask == (1u << BOARD_SYNC_PIO0_WAVE_OUTPUT_SM));
    assert(wave->gpio_write_mask != 0u);
    assert(wave->safe_low_gpio_mask == wave->gpio_write_mask);
    assert(wave->dma_channel_count == 0u);

    const sync_io_persona_descriptor_t *analyzer =
        sync_persona(SYNC_IO_PERSONA_ID_LOGIC_ANALYZER);
    assert(analyzer->implementation ==
           SYNC_IO_PERSONA_IMPLEMENTATION_MIGRATION_TARGET);
    assert((analyzer->flags & SYNC_IO_PERSONA_FLAG_READ_ONLY_PAD) != 0u);
    assert(analyzer->gpio_read_mask != 0u);
    assert(analyzer->gpio_write_mask == 0u);
    assert(analyzer->tx_fifo_sm_mask == 0u);
    assert(analyzer->safe_low_gpio_mask == 0u);

    const sync_io_persona_descriptor_t *calibration =
        sync_persona(SYNC_IO_PERSONA_ID_SMA_CALIBRATION);
    assert(calibration->implementation ==
           SYNC_IO_PERSONA_IMPLEMENTATION_COMPATIBILITY);
    assert((calibration->flags & SYNC_IO_PERSONA_FLAG_EXCLUSIVE_PIO) != 0u);
    assert(calibration->sm_mask == SYNC_IO_PERSONA_PIO_SM_MASK);
    assert(calibration->instruction_words ==
           SYNC_IO_SMA_PERSONA_MAX_INSTRUCTION_WORDS);
}

static void test_sync_io_persona_compatibility_matrix(void)
{
    const sync_io_persona_descriptor_t *capture =
        sync_persona(SYNC_IO_PERSONA_ID_INPUT_CAPTURE);
    const sync_io_persona_descriptor_t *scheduled =
        sync_persona(SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER);
    const sync_io_persona_descriptor_t *wave =
        sync_persona(SYNC_IO_PERSONA_ID_WAVE_OUTPUT);
    const sync_io_persona_descriptor_t *analyzer =
        sync_persona(SYNC_IO_PERSONA_ID_LOGIC_ANALYZER);
    const sync_io_persona_descriptor_t *maintenance =
        sync_persona(SYNC_IO_PERSONA_ID_SMA_MAINTENANCE);
    sync_io_persona_compatibility_t result;

    assert(sync_io_persona_compatible(capture, wave, &result));
    assert(result.compatible);
    assert(result.conflict_mask == SYNC_IO_PERSONA_CONFLICT_NONE);

    assert(sync_io_persona_compatible(analyzer, wave, &result));
    assert(result.gpio_conflict_mask == 0u);

    assert(!sync_io_persona_compatible(capture, analyzer, &result));
    assert((result.conflict_mask & SYNC_IO_PERSONA_CONFLICT_SM) != 0u);
    assert((result.conflict_mask & SYNC_IO_PERSONA_CONFLICT_FIFO) != 0u);
    assert((result.conflict_mask & SYNC_IO_PERSONA_CONFLICT_DMA) != 0u);
    assert((result.conflict_mask & SYNC_IO_PERSONA_CONFLICT_DREQ) != 0u);
    assert((result.conflict_mask & SYNC_IO_PERSONA_CONFLICT_WORKSPACE) != 0u);

    assert(!sync_io_persona_compatible(capture, scheduled, &result));
    assert((result.conflict_mask & SYNC_IO_PERSONA_CONFLICT_WORKSPACE) != 0u);
    assert((result.conflict_mask & SYNC_IO_PERSONA_CONFLICT_SM) == 0u);

    assert(!sync_io_persona_compatible(scheduled, wave, &result));
    assert((result.conflict_mask & SYNC_IO_PERSONA_CONFLICT_GPIO) != 0u);

    assert(!sync_io_persona_compatible(maintenance, wave, &result));
    assert((result.conflict_mask &
            SYNC_IO_PERSONA_CONFLICT_EXCLUSIVE_PIO) != 0u);
}

static void test_sync_io_persona_descriptor_negative_cases(void)
{
    sync_io_persona_descriptor_t invalid =
        *sync_persona(SYNC_IO_PERSONA_ID_INPUT_CAPTURE);

    invalid.sm_mask = 0u;
    assert(!sync_io_persona_descriptor_valid(&invalid));

    invalid = *sync_persona(SYNC_IO_PERSONA_ID_INPUT_CAPTURE);
    invalid.instruction_words =
        SYNC_IO_PERSONA_PIO_INSTRUCTION_CAPACITY + 1u;
    assert(!sync_io_persona_descriptor_valid(&invalid));

    invalid = *sync_persona(SYNC_IO_PERSONA_ID_LOGIC_ANALYZER);
    invalid.gpio_write_mask = 1u;
    assert(!sync_io_persona_descriptor_valid(&invalid));

    invalid = *sync_persona(SYNC_IO_PERSONA_ID_INPUT_CAPTURE);
    invalid.rx_fifo_sm_mask = 0u;
    assert(!sync_io_persona_descriptor_valid(&invalid));

    invalid = *sync_persona(SYNC_IO_PERSONA_ID_WAVE_OUTPUT);
    invalid.safe_low_gpio_mask |= 1u << BOARD_SYNC_INPUT_BASE_PIN;
    assert(!sync_io_persona_descriptor_valid(&invalid));

    invalid = *sync_persona(SYNC_IO_PERSONA_ID_INPUT_CAPTURE);
    invalid.dma_channel_count = 2u;
    assert(!sync_io_persona_descriptor_valid(&invalid));

    sync_io_persona_descriptor_t first =
        *sync_persona(SYNC_IO_PERSONA_ID_INPUT_CAPTURE);
    sync_io_persona_descriptor_t second =
        *sync_persona(SYNC_IO_PERSONA_ID_WAVE_OUTPUT);
    first.instruction_words = 20u;
    second.instruction_words = 20u;
    sync_io_persona_compatibility_t result;
    assert(!sync_io_persona_compatible(&first, &second, &result));
    assert((result.conflict_mask &
            SYNC_IO_PERSONA_CONFLICT_INSTRUCTION_SPACE) != 0u);
}

typedef struct {
    uint32_t load_count;
    uint32_t arm_count;
    uint32_t start_count;
    uint32_t stop_count;
    uint32_t cleanup_count;
    bool fail_load;
    bool fail_arm;
    bool fail_start;
} sync_persona_hook_context_t;

static bool sync_persona_hook_load(
    void *context,
    const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    sync_persona_hook_context_t *hooks = context;
    assert(descriptor != NULL);
    (void)dma_channel_mask;
    hooks->load_count++;
    return !hooks->fail_load;
}

static bool sync_persona_hook_arm(
    void *context,
    const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    sync_persona_hook_context_t *hooks = context;
    assert(descriptor != NULL);
    (void)dma_channel_mask;
    hooks->arm_count++;
    return !hooks->fail_arm;
}

static bool sync_persona_hook_start(
    void *context,
    const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    sync_persona_hook_context_t *hooks = context;
    assert(descriptor != NULL);
    (void)dma_channel_mask;
    hooks->start_count++;
    return !hooks->fail_start;
}

static void sync_persona_hook_stop(
    void *context,
    const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    sync_persona_hook_context_t *hooks = context;
    assert(descriptor != NULL);
    (void)dma_channel_mask;
    hooks->stop_count++;
}

static void sync_persona_hook_cleanup(
    void *context,
    const sync_io_persona_descriptor_t *descriptor,
    uint32_t dma_channel_mask)
{
    sync_persona_hook_context_t *hooks = context;
    assert(descriptor != NULL);
    (void)dma_channel_mask;
    hooks->cleanup_count++;
}

static sync_io_persona_manager_hooks_t sync_persona_hooks(void)
{
    return (sync_io_persona_manager_hooks_t){
        .load = sync_persona_hook_load,
        .arm = sync_persona_hook_arm,
        .start = sync_persona_hook_start,
        .stop = sync_persona_hook_stop,
        .cleanup = sync_persona_hook_cleanup,
    };
}

static void test_sync_io_persona_manager_lifecycle(void)
{
    sync_persona_hook_context_t hook_context = {0};
    sync_io_persona_manager_t manager;
    sync_io_persona_manager_handle_t handle;
    sync_io_persona_manager_snapshot_t snapshot;
    resource_arbiter_snapshot_t arbiter_snapshot;
    const sync_io_persona_manager_hooks_t hooks = sync_persona_hooks();

    assert(resource_arbiter_init());
    sync_io_persona_manager_init(&manager, &hooks, &hook_context);
    assert(sync_io_persona_manager_claim(
        &manager, SYNC_IO_PERSONA_ID_INPUT_CAPTURE, &handle, NULL));
    assert(sync_io_persona_manager_handle_valid(&manager, &handle));
    sync_io_persona_manager_get_snapshot(&manager, &snapshot);
    assert(snapshot.active_count == 1u);
    assert(snapshot.pio_resource == RESOURCE_ARBITER_RESOURCE_PIO0);
    assert(snapshot.used_sm_mask ==
           (1u << BOARD_SYNC_PIO0_INPUT_CAPTURE_SM));
    assert(snapshot.used_dma_channel_mask ==
           (1u << SYNC_IO_CAPTURE_DMA_CH));
    assert(snapshot.leases[handle.slot].state ==
           SYNC_IO_PERSONA_MANAGER_STATE_CLAIMED);

    assert(!sync_io_persona_manager_start(&manager, &handle));
    assert(sync_io_persona_manager_load(&manager, &handle));
    assert(sync_io_persona_manager_arm(&manager, &handle));
    assert(sync_io_persona_manager_start(&manager, &handle));
    assert(sync_io_persona_manager_stop(&manager, &handle));
    assert(sync_io_persona_manager_release(&manager, &handle));
    assert(!sync_io_persona_manager_handle_valid(&manager, &handle));
    sync_io_persona_manager_get_snapshot(&manager, &snapshot);
    assert(snapshot.active_count == 0u);
    assert(snapshot.used_sm_mask == 0u);
    assert(snapshot.used_dma_channel_mask == 0u);
    assert((snapshot.last_error == SYNC_IO_PERSONA_MANAGER_ERROR_NONE));
    resource_arbiter_get_snapshot(&arbiter_snapshot);
    assert((arbiter_snapshot.active_resources &
            RESOURCE_ARBITER_RESOURCE_PIO0) == 0u);
    assert(hook_context.load_count == 1u);
    assert(hook_context.arm_count == 1u);
    assert(hook_context.start_count == 1u);
    assert(hook_context.stop_count == 1u);
    assert(hook_context.cleanup_count == 1u);
    assert(sync_io_persona_manager_deinit(&manager));
}

static void test_sync_io_persona_manager_compatibility(void)
{
    sync_io_persona_manager_t manager;
    sync_io_persona_manager_handle_t capture;
    sync_io_persona_manager_handle_t wave;
    sync_io_persona_manager_handle_t analyzer;
    sync_io_persona_manager_handle_t scheduled;
    sync_io_persona_manager_handle_t maintenance;
    sync_io_persona_compatibility_t compatibility;

    assert(resource_arbiter_init());
    sync_io_persona_manager_init(&manager, NULL, NULL);
    assert(sync_io_persona_manager_claim(
        &manager, SYNC_IO_PERSONA_ID_INPUT_CAPTURE, &capture, NULL));
    assert(sync_io_persona_manager_claim(
        &manager, SYNC_IO_PERSONA_ID_WAVE_OUTPUT, &wave, NULL));
    assert(!sync_io_persona_manager_claim(
        &manager, SYNC_IO_PERSONA_ID_LOGIC_ANALYZER, &analyzer,
        &compatibility));
    assert((compatibility.conflict_mask & SYNC_IO_PERSONA_CONFLICT_SM) != 0u);
    assert((compatibility.conflict_mask & SYNC_IO_PERSONA_CONFLICT_FIFO) != 0u);
    assert((compatibility.conflict_mask & SYNC_IO_PERSONA_CONFLICT_DMA) != 0u);
    assert((compatibility.conflict_mask & SYNC_IO_PERSONA_CONFLICT_WORKSPACE) != 0u);
    assert(sync_io_persona_manager_release(&manager, &wave));
    assert(sync_io_persona_manager_release(&manager, &capture));

    assert(sync_io_persona_manager_claim(
        &manager, SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER, &scheduled, NULL));
    assert(!sync_io_persona_manager_claim(
        &manager, SYNC_IO_PERSONA_ID_INPUT_CAPTURE, &capture,
        &compatibility));
    assert((compatibility.conflict_mask &
            SYNC_IO_PERSONA_CONFLICT_WORKSPACE) != 0u);
    assert(sync_io_persona_manager_release(&manager, &scheduled));

    assert(sync_io_persona_manager_claim(
        &manager, SYNC_IO_PERSONA_ID_SMA_MAINTENANCE, &maintenance, NULL));
    assert(!sync_io_persona_manager_claim(
        &manager, SYNC_IO_PERSONA_ID_WAVE_OUTPUT, &wave,
        &compatibility));
    assert((compatibility.conflict_mask &
            SYNC_IO_PERSONA_CONFLICT_EXCLUSIVE_PIO) != 0u);
    assert(sync_io_persona_manager_release(&manager, &maintenance));
    assert(sync_io_persona_manager_deinit(&manager));
}

static void test_sync_io_persona_manager_claim_requires_handle(void)
{
    sync_io_persona_manager_t manager;
    sync_io_persona_manager_snapshot_t snapshot;

    assert(resource_arbiter_init());
    sync_io_persona_manager_init(&manager, NULL, NULL);
    assert(!sync_io_persona_manager_claim(
        &manager, SYNC_IO_PERSONA_ID_INPUT_CAPTURE, NULL, NULL));
    sync_io_persona_manager_get_snapshot(&manager, &snapshot);
    assert(snapshot.active_count == 0u);
    assert(snapshot.used_sm_mask == 0u);
    assert(snapshot.last_error ==
           SYNC_IO_PERSONA_MANAGER_ERROR_INVALID_HANDLE);
    assert(sync_io_persona_manager_deinit(&manager));
}

static void test_sync_io_persona_manager_preserves_conflict_without_output(void)
{
    sync_io_persona_manager_t manager;
    sync_io_persona_manager_handle_t capture;
    sync_io_persona_manager_handle_t analyzer;
    sync_io_persona_manager_snapshot_t snapshot;

    assert(resource_arbiter_init());
    sync_io_persona_manager_init(&manager, NULL, NULL);
    assert(sync_io_persona_manager_claim(
        &manager, SYNC_IO_PERSONA_ID_INPUT_CAPTURE, &capture, NULL));
    assert(!sync_io_persona_manager_claim(
        &manager, SYNC_IO_PERSONA_ID_LOGIC_ANALYZER, &analyzer, NULL));
    sync_io_persona_manager_get_snapshot(&manager, &snapshot);
    assert(snapshot.last_error ==
           SYNC_IO_PERSONA_MANAGER_ERROR_RESOURCE_CONFLICT);
    assert((snapshot.last_conflict_mask & SYNC_IO_PERSONA_CONFLICT_SM) != 0u);
    assert((snapshot.last_conflict_mask & SYNC_IO_PERSONA_CONFLICT_DMA) != 0u);
    assert(sync_io_persona_manager_release(&manager, &capture));
    assert(sync_io_persona_manager_deinit(&manager));
}

static void test_sync_io_persona_manager_failure_rollback(void)
{
    sync_persona_hook_context_t hook_context = {.fail_load = true};
    sync_io_persona_manager_t manager;
    sync_io_persona_manager_handle_t handle;
    sync_io_persona_manager_snapshot_t snapshot;
    const sync_io_persona_manager_hooks_t hooks = sync_persona_hooks();

    assert(resource_arbiter_init());
    sync_io_persona_manager_init(&manager, &hooks, &hook_context);
    assert(sync_io_persona_manager_claim(
        &manager, SYNC_IO_PERSONA_ID_WAVE_OUTPUT, &handle, NULL));
    assert(!sync_io_persona_manager_load(&manager, &handle));
    assert(!sync_io_persona_manager_handle_valid(&manager, &handle));
    sync_io_persona_manager_get_snapshot(&manager, &snapshot);
    assert(snapshot.active_count == 0u);
    assert(snapshot.last_error == SYNC_IO_PERSONA_MANAGER_ERROR_LOAD);
    assert(snapshot.last_conflict_mask == SYNC_IO_PERSONA_CONFLICT_NONE);
    assert(hook_context.cleanup_count == 1u);
    assert(sync_io_persona_manager_deinit(&manager));

    assert(resource_arbiter_init());
    assert(resource_arbiter_acquire_owned(
        RESOURCE_ARBITER_RESOURCE_PIO0, "foreign-owner"));
    sync_io_persona_manager_init(&manager, NULL, NULL);
    assert(!sync_io_persona_manager_claim(
        &manager, SYNC_IO_PERSONA_ID_WAVE_OUTPUT, &handle, NULL));
    sync_io_persona_manager_get_snapshot(&manager, &snapshot);
    assert(snapshot.active_count == 0u);
    assert(snapshot.last_error == SYNC_IO_PERSONA_MANAGER_ERROR_RESOURCE_ARBITER);
    assert(snapshot.last_conflict_mask == RESOURCE_ARBITER_RESOURCE_PIO0);
    resource_arbiter_release_owned(
        RESOURCE_ARBITER_RESOURCE_PIO0, "foreign-owner");
    assert(sync_io_persona_manager_deinit(&manager));
}

int main(void)
{
    test_directional_tdma_resources();
    test_tdma_rx_endpoint_contract();
    test_tdma_persona_owner_transfer();
    test_tdma_conflict_recovery_has_no_partial_lease();
    test_sync_io_persona_catalog();
    test_sync_io_persona_compatibility_matrix();
    test_sync_io_persona_descriptor_negative_cases();
    test_sync_io_persona_manager_lifecycle();
    test_sync_io_persona_manager_compatibility();
    test_sync_io_persona_manager_claim_requires_handle();
    test_sync_io_persona_manager_preserves_conflict_without_output();
    test_sync_io_persona_manager_failure_rollback();

    assert(resource_arbiter_mode_is_valid(RESOURCE_ARBITER_MODE_BOOT));
    assert(resource_arbiter_mode_is_valid(RESOURCE_ARBITER_MODE_RUN));
    assert(resource_arbiter_mode_is_valid(RESOURCE_ARBITER_MODE_OTA));
    assert(resource_arbiter_mode_is_valid(RESOURCE_ARBITER_MODE_FAULT));
    assert(!resource_arbiter_mode_is_valid((resource_arbiter_mode_t)99));

    assert(resource_arbiter_init());
    expect_snapshot(false, false);
    assert(resource_arbiter_can_begin_ota());

    resource_arbiter_publish_calibration_training(true);
    expect_snapshot(true, false);
    assert(!resource_arbiter_can_begin_ota());

    /* Independent owners must not clear each other's gate. */
    resource_arbiter_publish_tdma_clock_training(true);
    expect_snapshot(true, true);
    resource_arbiter_publish_calibration_training(false);
    expect_snapshot(false, true);
    assert(!resource_arbiter_can_begin_ota());
    resource_arbiter_publish_tdma_clock_training(false);
    expect_snapshot(false, false);
    assert(resource_arbiter_can_begin_ota());

    resource_arbiter_publish_trigger_activity(false, true);
    assert(!resource_arbiter_can_begin_ota());
    resource_arbiter_publish_trigger_activity(false, false);
    assert(resource_arbiter_request_ota_admission());
    assert(resource_arbiter_ota_admission_active());
    assert(resource_arbiter_acquire_owned(RESOURCE_ARBITER_RESOURCE_FLASH,
                                          "host"));
    assert(!resource_arbiter_can_begin_ota());
    resource_arbiter_release_owned(RESOURCE_ARBITER_RESOURCE_FLASH, "host");
    assert(resource_arbiter_can_begin_ota());
    resource_arbiter_release_ota_admission();
    assert(!resource_arbiter_ota_admission_active());

    puts("resource_arbiter host tests passed");
    return 0;
}
