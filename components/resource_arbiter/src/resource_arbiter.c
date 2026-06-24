#include "resource_arbiter.h"

#include <string.h>

#include "osal.h"

typedef struct {
    bool initialized;
    resource_arbiter_snapshot_t snapshot;
} resource_arbiter_context_t;

static resource_arbiter_context_t s_resource_arbiter;

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

bool resource_arbiter_can_begin_ota(void)
{
    bool allowed;

    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }

    allowed = !s_resource_arbiter.snapshot.trigger_capture_running &&
              !s_resource_arbiter.snapshot.trigger_clock_running &&
              ((s_resource_arbiter.snapshot.active_resources &
                RESOURCE_ARBITER_RESOURCE_FLASH) == 0u) &&
              s_resource_arbiter.snapshot.mode != RESOURCE_ARBITER_MODE_FAULT;
    osal_critical_exit();

    return allowed;
}

bool resource_arbiter_acquire(uint32_t resources)
{
    bool acquired = false;

    if (resources == 0u) {
        return false;
    }

    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }

    if ((s_resource_arbiter.snapshot.active_resources & resources) == 0u) {
        s_resource_arbiter.snapshot.active_resources |= resources;
        if ((resources & RESOURCE_ARBITER_RESOURCE_FLASH) != 0u) {
            s_resource_arbiter.snapshot.mode = RESOURCE_ARBITER_MODE_OTA;
        }
        acquired = true;
    }
    osal_critical_exit();

    return acquired;
}

void resource_arbiter_release(uint32_t resources)
{
    if (resources == 0u) {
        return;
    }

    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }

    s_resource_arbiter.snapshot.active_resources &= ~resources;
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
