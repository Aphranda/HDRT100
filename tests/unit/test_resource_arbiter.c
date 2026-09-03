#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "resource_arbiter.h"
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

int main(void)
{
    test_directional_tdma_resources();
    test_tdma_rx_endpoint_contract();
    test_tdma_persona_owner_transfer();
    test_tdma_conflict_recovery_has_no_partial_lease();

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
