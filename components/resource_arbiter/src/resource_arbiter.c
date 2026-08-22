#include "resource_arbiter.h"

#include <string.h>

#include "osal.h"

typedef struct {
    bool initialized;
    resource_arbiter_snapshot_t snapshot;
} resource_arbiter_context_t;

static resource_arbiter_context_t s_resource_arbiter;

static bool resource_arbiter_owner_matches(const char *expected,
                                           const char *actual)
{
    if (expected == NULL) {
        return true;
    }
    return expected == actual ||
           (actual != NULL && strcmp(expected, actual) == 0);
}

static const char *resource_arbiter_first_owner_locked(uint32_t resources)
{
    for (uint32_t bit = 0u; bit < 32u; ++bit) {
        const uint32_t mask = 1u << bit;
        if ((resources & mask) != 0u) {
            const char *owner = s_resource_arbiter.snapshot.resource_owners[bit];
            if (owner != NULL) {
                return owner;
            }
        }
    }

    return NULL;
}

static void resource_arbiter_reset_locked(void)
{
    memset(&s_resource_arbiter, 0, sizeof(s_resource_arbiter));
    s_resource_arbiter.initialized = true;
    s_resource_arbiter.snapshot.mode = RESOURCE_ARBITER_MODE_RUN;
}

bool resource_arbiter_init(void)
{
    osal_critical_enter();
    resource_arbiter_reset_locked();
    osal_critical_exit();
    return true;
}

void resource_arbiter_publish_trigger_activity(bool capture_running, bool clock_running)
{
    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }

    s_resource_arbiter.snapshot.trigger_capture_running = capture_running;
    s_resource_arbiter.snapshot.trigger_clock_running = clock_running;
    osal_critical_exit();
}

void resource_arbiter_publish_training_activity(bool calibration_active,
                                                 bool tdma_clock_training_active)
{
    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }

    s_resource_arbiter.snapshot.calibration_training_active = calibration_active;
    s_resource_arbiter.snapshot.tdma_clock_training_active =
        tdma_clock_training_active;
    osal_critical_exit();
}

bool resource_arbiter_can_begin_ota(void)
{
    bool allowed;

    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }

    allowed = !s_resource_arbiter.snapshot.trigger_capture_running &&
              !s_resource_arbiter.snapshot.trigger_clock_running &&
              !s_resource_arbiter.snapshot.calibration_training_active &&
              !s_resource_arbiter.snapshot.tdma_clock_training_active &&
              ((s_resource_arbiter.snapshot.active_resources &
                RESOURCE_ARBITER_RESOURCE_FLASH) == 0u) &&
              s_resource_arbiter.snapshot.mode != RESOURCE_ARBITER_MODE_FAULT;
    osal_critical_exit();

    return allowed;
}

bool resource_arbiter_acquire(uint32_t resources)
{
    return resource_arbiter_acquire_owned(resources, NULL);
}

bool resource_arbiter_acquire_owned(uint32_t resources, const char *owner)
{
    bool acquired = false;

    if (resources == 0u) {
        return false;
    }

    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }

    const uint32_t conflicts =
        s_resource_arbiter.snapshot.active_resources & resources;
    if (conflicts == 0u) {
        s_resource_arbiter.snapshot.active_resources |= resources;
        for (uint32_t bit = 0u; bit < 32u; ++bit) {
            const uint32_t mask = 1u << bit;
            if ((resources & mask) != 0u) {
                s_resource_arbiter.snapshot.resource_owners[bit] = owner;
            }
        }
        if ((resources & RESOURCE_ARBITER_RESOURCE_FLASH) != 0u) {
            s_resource_arbiter.snapshot.mode = RESOURCE_ARBITER_MODE_OTA;
        }
        acquired = true;
    } else {
        s_resource_arbiter.snapshot.last_conflict_resources = conflicts;
        s_resource_arbiter.snapshot.last_conflict_owner = owner;
        s_resource_arbiter.snapshot.last_conflict_holder =
            resource_arbiter_first_owner_locked(conflicts);
    }
    osal_critical_exit();

    return acquired;
}

void resource_arbiter_release(uint32_t resources)
{
    resource_arbiter_release_owned(resources, NULL);
}

void resource_arbiter_release_owned(uint32_t resources, const char *owner)
{
    if (resources == 0u) {
        return;
    }

    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }

    for (uint32_t bit = 0u; bit < 32u; ++bit) {
        const uint32_t mask = 1u << bit;
        if ((resources & mask) != 0u &&
            resource_arbiter_owner_matches(
                owner,
                s_resource_arbiter.snapshot.resource_owners[bit])) {
            s_resource_arbiter.snapshot.active_resources &= ~mask;
            s_resource_arbiter.snapshot.resource_owners[bit] = NULL;
        }
    }
    if ((s_resource_arbiter.snapshot.active_resources &
         RESOURCE_ARBITER_RESOURCE_FLASH) == 0u &&
        s_resource_arbiter.snapshot.mode == RESOURCE_ARBITER_MODE_OTA) {
        s_resource_arbiter.snapshot.mode = RESOURCE_ARBITER_MODE_RUN;
    }
    osal_critical_exit();
}

void resource_arbiter_get_snapshot(resource_arbiter_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }

    *snapshot = s_resource_arbiter.snapshot;
    osal_critical_exit();
}
