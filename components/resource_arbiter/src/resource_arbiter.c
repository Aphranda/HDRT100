#include "resource_arbiter.h"

#include <string.h>

#include "osal.h"

typedef struct {
    bool initialized;
    bool ota_admission_active;
    resource_arbiter_snapshot_t snapshot;
} resource_arbiter_context_t;

static resource_arbiter_context_t s_resource_arbiter;

/* The request side is protected by the arbiter lock. Completion has one
 * Core1 writer and atomic fields: no mixed atomic/non-atomic snapshot copy.
 * Keep this lifetime outside reset_locked: repeated arbiter initialization
 * must not discard an outstanding command or race its owner publication. */
static struct {
    uint32_t command_sequence;
    bool reserved;
    bool submitting;
} s_tdma_training_request;
static struct {
    uint32_t guard;
    uint32_t command_sequence;
    uint32_t terminal;
} s_tdma_training_completion;

static bool resource_arbiter_tdma_clock_training_active_locked(void)
{
    if (s_tdma_training_request.reserved &&
        !s_tdma_training_request.submitting) {
        const uint32_t begin = __atomic_load_n(
            &s_tdma_training_completion.guard, __ATOMIC_ACQUIRE);
        if (begin != 0u && (begin & 1u) == 0u) {
            const uint32_t sequence = __atomic_load_n(
                &s_tdma_training_completion.command_sequence, __ATOMIC_RELAXED);
            const uint32_t terminal = __atomic_load_n(
                &s_tdma_training_completion.terminal, __ATOMIC_RELAXED);
            __atomic_thread_fence(__ATOMIC_ACQUIRE);
            const uint32_t end = __atomic_load_n(
                &s_tdma_training_completion.guard, __ATOMIC_ACQUIRE);
            if (begin == end && terminal != 0u &&
                sequence == s_tdma_training_request.command_sequence) {
                s_tdma_training_request.reserved = false;
            }
        }
    }
    return s_resource_arbiter.snapshot.tdma_clock_training_active ||
           s_tdma_training_request.reserved;
}

void resource_arbiter_complete_tdma_clock_training_core1(
    uint32_t command_sequence, bool terminal)
{
    const uint32_t guard = __atomic_load_n(
        &s_tdma_training_completion.guard, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tdma_training_completion.guard, guard + 1u,
                     __ATOMIC_RELAXED);
    __atomic_thread_fence(__ATOMIC_RELEASE);
    __atomic_store_n(&s_tdma_training_completion.command_sequence,
                     command_sequence, __ATOMIC_RELAXED);
    __atomic_store_n(&s_tdma_training_completion.terminal, terminal ? 1u : 0u,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&s_tdma_training_completion.guard, guard + 2u,
                     __ATOMIC_RELEASE);
}

bool resource_arbiter_mode_is_valid(resource_arbiter_mode_t mode)
{
    switch (mode) {
    case RESOURCE_ARBITER_MODE_BOOT:
    case RESOURCE_ARBITER_MODE_RUN:
    case RESOURCE_ARBITER_MODE_OTA:
    case RESOURCE_ARBITER_MODE_FAULT:
        return true;
    default:
        return false;
    }
}

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

void resource_arbiter_publish_calibration_training(bool active)
{
    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }
    s_resource_arbiter.snapshot.calibration_training_active = active;
    osal_critical_exit();
}

void resource_arbiter_publish_tdma_clock_training(bool active)
{
    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }
    s_resource_arbiter.snapshot.tdma_clock_training_active = active;
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

bool resource_arbiter_request_tdma_clock_training(
    resource_arbiter_tdma_training_submit_t submit, void *context)
{
    if (submit == NULL) {
        return false;
    }
    bool submitted = false;
    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }
    if (!s_tdma_training_request.submitting &&
        resource_arbiter_mode_is_valid(s_resource_arbiter.snapshot.mode) &&
        s_resource_arbiter.snapshot.mode != RESOURCE_ARBITER_MODE_FAULT &&
        s_resource_arbiter.snapshot.mode != RESOURCE_ARBITER_MODE_OTA &&
        !s_resource_arbiter.ota_admission_active &&
        (s_resource_arbiter.snapshot.active_resources &
         RESOURCE_ARBITER_RESOURCE_FLASH) == 0u) {
        const bool previous_reserved = s_tdma_training_request.reserved;
        s_tdma_training_request.reserved = true;
        s_tdma_training_request.submitting = true;
        uint32_t sequence = 0u;
        submitted = submit(context, &sequence);
        if (submitted) {
            s_tdma_training_request.command_sequence = sequence;
        } else {
            s_tdma_training_request.reserved = previous_reserved;
        }
        s_tdma_training_request.submitting = false;
    }
    osal_critical_exit();
    return submitted;
}

bool resource_arbiter_can_begin_ota(void)
{
    bool allowed;

    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }

    allowed = resource_arbiter_mode_is_valid(s_resource_arbiter.snapshot.mode) &&
              !s_resource_arbiter.snapshot.trigger_capture_running &&
              !s_resource_arbiter.snapshot.trigger_clock_running &&
              !s_resource_arbiter.snapshot.calibration_training_active &&
              !resource_arbiter_tdma_clock_training_active_locked() &&
              ((s_resource_arbiter.snapshot.active_resources &
                (RESOURCE_ARBITER_RESOURCE_FLASH | RESOURCE_ARBITER_RESOURCE_SMA_GPIO)) == 0u) &&
              s_resource_arbiter.snapshot.mode != RESOURCE_ARBITER_MODE_FAULT;
    osal_critical_exit();

    return allowed;
}

bool resource_arbiter_request_ota_admission(void)
{
    bool allowed = false;

    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }

    const resource_arbiter_snapshot_t *snapshot =
        &s_resource_arbiter.snapshot;
    allowed = resource_arbiter_mode_is_valid(snapshot->mode) &&
              !snapshot->trigger_capture_running &&
              !snapshot->trigger_clock_running &&
              !snapshot->calibration_training_active &&
              !resource_arbiter_tdma_clock_training_active_locked() &&
              snapshot->mode != RESOURCE_ARBITER_MODE_FAULT &&
              (snapshot->active_resources &
               (RESOURCE_ARBITER_RESOURCE_FLASH | RESOURCE_ARBITER_RESOURCE_SMA_GPIO)) == 0u;
    if (allowed) {
        s_resource_arbiter.ota_admission_active = true;
        s_resource_arbiter.snapshot.mode = RESOURCE_ARBITER_MODE_OTA;
    }
    osal_critical_exit();
    return allowed;
}

void resource_arbiter_release_ota_admission(void)
{
    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }
    s_resource_arbiter.ota_admission_active = false;
    if ((s_resource_arbiter.snapshot.active_resources &
         RESOURCE_ARBITER_RESOURCE_FLASH) == 0u &&
        s_resource_arbiter.snapshot.mode == RESOURCE_ARBITER_MODE_OTA &&
        !s_resource_arbiter.ota_admission_active) {
        s_resource_arbiter.snapshot.mode = RESOURCE_ARBITER_MODE_RUN;
    }
    osal_critical_exit();
}

bool resource_arbiter_ota_admission_active(void)
{
    bool active;
    osal_critical_enter();
    if (!s_resource_arbiter.initialized) {
        resource_arbiter_reset_locked();
    }
    active = s_resource_arbiter.ota_admission_active;
    osal_critical_exit();
    return active;
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

    /* GPIO sequences depend on live Core1 timer IRQs. Flash parking and OTA
     * admission must exclude their lease in both acquisition orders. */
    const uint32_t flash = RESOURCE_ARBITER_RESOURCE_FLASH;
    const uint32_t sma = RESOURCE_ARBITER_RESOURCE_SMA_GPIO;
    const uint32_t active = s_resource_arbiter.snapshot.active_resources;
    const bool mixed_request = (resources & (flash | sma)) == (flash | sma);
    const bool flash_vs_sma = (resources & flash) != 0u && (active & sma) != 0u;
    const bool sma_vs_flash = (resources & sma) != 0u && (active & flash) != 0u;
    const bool sma_vs_ota = (resources & sma) != 0u &&
        (s_resource_arbiter.ota_admission_active ||
         s_resource_arbiter.snapshot.mode == RESOURCE_ARBITER_MODE_OTA);
    if (mixed_request || flash_vs_sma || sma_vs_flash || sma_vs_ota) {
        const uint32_t conflict = mixed_request ? flash | sma : flash_vs_sma ? sma : flash;
        s_resource_arbiter.snapshot.last_conflict_resources = conflict;
        s_resource_arbiter.snapshot.last_conflict_owner = owner;
        s_resource_arbiter.snapshot.last_conflict_holder = mixed_request ?
            "FLASH_SMA_EXCLUSION" : sma_vs_ota && !sma_vs_flash ?
            "OTA_ADMISSION" : resource_arbiter_first_owner_locked(conflict);
        osal_critical_exit();
        return false;
    }

    /* FlashTransaction checks policy before its ACQUIRE state. A training
     * request can arrive between those two actions, so the final lease must
     * enforce the same exclusion while holding the request-side lock. */
    if ((resources & RESOURCE_ARBITER_RESOURCE_FLASH) != 0u &&
        resource_arbiter_tdma_clock_training_active_locked()) {
        s_resource_arbiter.snapshot.last_conflict_resources =
            RESOURCE_ARBITER_RESOURCE_FLASH;
        s_resource_arbiter.snapshot.last_conflict_owner = owner;
        s_resource_arbiter.snapshot.last_conflict_holder = "TDMA_CLOCK_TRAINING";
        osal_critical_exit();
        return false;
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
    snapshot->tdma_clock_training_active =
        resource_arbiter_tdma_clock_training_active_locked();
    osal_critical_exit();
}
