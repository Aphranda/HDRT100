#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "resource_arbiter.h"

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

static void test_directional_tdma_resources(void)
{
    const uint32_t resources =
        RESOURCE_ARBITER_RESOURCE_PIO1 |
        RESOURCE_ARBITER_RESOURCE_PIO2 |
        RESOURCE_ARBITER_RESOURCE_TDMA_DMA_CAPTURE |
        RESOURCE_ARBITER_RESOURCE_TDMA_DMA_OUTPUT |
        RESOURCE_ARBITER_RESOURCE_TDMA_DMA_FORWARD |
        RESOURCE_ARBITER_RESOURCE_TDMA_DMA_SYNC_EDGE |
        RESOURCE_ARBITER_RESOURCE_TDMA_GPIO |
        RESOURCE_ARBITER_RESOURCE_TDMA_IRQ |
        RESOURCE_ARBITER_RESOURCE_TDMA_DREQ;
    const char *const flight_owner = "TDMA_FLIGHT_PIO";

    assert(resource_arbiter_init());
    assert(resource_arbiter_acquire_owned(resources, flight_owner));

    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);
    assert((snapshot.active_resources & resources) == resources);

    assert(!resource_arbiter_acquire_owned(
        RESOURCE_ARBITER_RESOURCE_TDMA_DREQ, "conflicting-persona"));
    resource_arbiter_get_snapshot(&snapshot);
    assert(snapshot.last_conflict_resources ==
           RESOURCE_ARBITER_RESOURCE_TDMA_DREQ);
    assert(snapshot.last_conflict_owner != NULL);
    assert(snapshot.last_conflict_holder != NULL);

    resource_arbiter_release_owned(resources, "wrong-owner");
    resource_arbiter_get_snapshot(&snapshot);
    assert((snapshot.active_resources & resources) == resources);

    resource_arbiter_release_owned(resources, flight_owner);
    resource_arbiter_get_snapshot(&snapshot);
    assert((snapshot.active_resources & resources) == 0u);
}

int main(void)
{
    test_directional_tdma_resources();

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
