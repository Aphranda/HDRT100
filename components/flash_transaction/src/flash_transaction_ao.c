#include "flash_transaction.h"

#include <string.h>

#include "board.h"
#include "diagnostics.h"
#include "drv_flash_write.h"
#include "flash_map.h"
#include "flash_transaction_fb.h"
#include "project_config.h"
#include "resource_arbiter.h"

#define FLASH_TRANSACTION_OWNER "FlashTransactionAO"
#define FLASH_TRANSACTION_EXECUTE_STEPS 16u

static flash_transaction_fb_t s_flash_transaction;
static const flash_transaction_completion_lease_t *s_completion_lease;
static uint32_t s_journal_provider_generation;

static uint32_t flash_transaction_policy_check(uint32_t requester,
                                               uint32_t *temperature_flags);

static bool flash_transaction_policy_allows(uint32_t requester)
{
    return flash_transaction_policy_check(requester, NULL) ==
           FLASH_TRANSACTION_ERROR_NONE;
}

static uint32_t flash_transaction_policy_check(uint32_t requester,
                                               uint32_t *temperature_flags)
{
    diagnostics_sensor_status_t sensors;
    diagnostics_get_sensor_status(&sensors);
    if (temperature_flags != NULL) {
        *temperature_flags = sensors.flags;
    }
    const uint32_t thermal_critical =
        DIAGNOSTICS_SENSOR_FLAG_BOARD_TEMP_CRITICAL |
        DIAGNOSTICS_SENSOR_FLAG_CHIP_TEMP_CRITICAL;
    if ((sensors.flags & thermal_critical) != 0u) {
        return FLASH_TRANSACTION_ERROR_THERMAL;
    }
    if (diagnostics_has_fault()) {
        return FLASH_TRANSACTION_ERROR_DIAGNOSTICS_FAULT;
    }
    if (requester != FLASH_TRANSACTION_REQUESTER_OTA_IMAGE &&
        requester != FLASH_TRANSACTION_REQUESTER_OTA_METADATA &&
        requester != FLASH_TRANSACTION_REQUESTER_PRODUCT_CONFIG &&
        requester != FLASH_TRANSACTION_REQUESTER_CALIBRATION &&
        requester != FLASH_TRANSACTION_REQUESTER_OTA_JOURNAL &&
        requester != FLASH_TRANSACTION_REQUESTER_OTA_MANIFEST &&
        !(PROJECT_ENABLE_FLASH_VALIDATION &&
          requester == FLASH_TRANSACTION_REQUESTER_VALIDATION)) {
        return FLASH_TRANSACTION_ERROR_POLICY;
    }
    resource_arbiter_snapshot_t arbiter;
    resource_arbiter_get_snapshot(&arbiter);
    if (!resource_arbiter_mode_is_valid(arbiter.mode) ||
        arbiter.mode == RESOURCE_ARBITER_MODE_FAULT) {
        return FLASH_TRANSACTION_ERROR_MODE;
    }
    const bool ota_requester =
        requester == FLASH_TRANSACTION_REQUESTER_OTA_IMAGE ||
        requester == FLASH_TRANSACTION_REQUESTER_OTA_METADATA ||
        requester == FLASH_TRANSACTION_REQUESTER_OTA_MANIFEST ||
        requester == FLASH_TRANSACTION_REQUESTER_OTA_JOURNAL;
    if (ota_requester && !resource_arbiter_ota_admission_active()) {
        return FLASH_TRANSACTION_ERROR_MODE;
    }
    if (arbiter.trigger_capture_running || arbiter.trigger_clock_running) {
        return FLASH_TRANSACTION_ERROR_TRIGGER_ACTIVE;
    }
    if (arbiter.calibration_training_active) {
        return FLASH_TRANSACTION_ERROR_CALIBRATION_ACTIVE;
    }
    if (arbiter.tdma_clock_training_active) {
        return FLASH_TRANSACTION_ERROR_TDMA_TRAINING_ACTIVE;
    }
    if ((arbiter.active_resources & RESOURCE_ARBITER_RESOURCE_FLASH) != 0u) {
        return FLASH_TRANSACTION_ERROR_RESOURCE;
    }
    return resource_arbiter_can_begin_ota()
               ? FLASH_TRANSACTION_ERROR_NONE
               : FLASH_TRANSACTION_ERROR_POLICY;
}

static bool flash_transaction_acquire(void)
{
    return resource_arbiter_acquire_owned(RESOURCE_ARBITER_RESOURCE_FLASH,
                                          FLASH_TRANSACTION_OWNER);
}

static void flash_transaction_release(void)
{
    resource_arbiter_release_owned(RESOURCE_ARBITER_RESOURCE_FLASH,
                                   FLASH_TRANSACTION_OWNER);
}

static bool flash_transaction_park_core1(void)
{
    return drv_flash_write_session_begin();
}

static bool flash_transaction_release_core1(void)
{
    return drv_flash_write_session_end();
}

static __attribute__((noinline)) bool flash_transaction_erase(uint32_t offset,
                                                              uint32_t length)
{
    return drv_flash_erase_parked(offset, length);
}

static __attribute__((noinline)) bool flash_transaction_program(
    uint32_t offset, const uint8_t *data, uint32_t length)
{
    return drv_flash_program_parked(offset, data, length);
}

static bool flash_transaction_verify_erased(uint32_t offset, uint32_t length)
{
    return drv_flash_is_erased(offset, length);
}

static bool flash_transaction_verify_programmed(uint32_t offset,
                                                const uint8_t *data,
                                                uint32_t length)
{
    const uint8_t *flash = drv_flash_xip_ptr(offset);
    return flash != NULL && data != NULL && memcmp(flash, data, length) == 0;
}

static void flash_transaction_get_lockout(uint32_t *request_seq,
                                          uint32_t *ack_seq,
                                          uint32_t *timeout_count)
{
    drv_flash_lockout_status_t status;
    drv_flash_get_lockout_status(&status);
    *request_seq = status.request_seq;
    *ack_seq = status.ack_seq;
    *timeout_count = status.timeout_count;
}

static uint32_t flash_transaction_now_ms(void)
{
    return board_uptime_ms();
}

bool flash_transaction_ao_init(void)
{
    s_completion_lease = NULL;
    s_journal_provider_generation = 0u;
    const flash_transaction_platform_t platform = {
        .policy_allows = flash_transaction_policy_allows,
        .policy_check = flash_transaction_policy_check,
        .acquire_flash = flash_transaction_acquire,
        .release_flash = flash_transaction_release,
        .park_core1 = flash_transaction_park_core1,
        .release_core1 = flash_transaction_release_core1,
        .erase = flash_transaction_erase,
        .program = flash_transaction_program,
        .verify_erased = flash_transaction_verify_erased,
        .verify_programmed = flash_transaction_verify_programmed,
        .get_lockout = flash_transaction_get_lockout,
        .now_ms = flash_transaction_now_ms,
        .step_hook = NULL,
        .step_hook_context = NULL,
    };
    flash_transaction_fb_init(&s_flash_transaction, &platform);
    return true;
}

bool flash_transaction_ao_set_completion_lease(
    const flash_transaction_completion_lease_t *lease)
{
    if (s_flash_transaction.occupied) {
        return false;
    }
    if (lease != NULL &&
        (lease->retain == NULL || lease->release == NULL ||
         lease->append == NULL)) {
        return false;
    }
    s_completion_lease = lease;
    return true;
}

const flash_transaction_completion_lease_t *
flash_transaction_ao_get_completion_lease(void)
{
    return s_completion_lease;
}

bool flash_transaction_ao_set_active_app_partition(uint32_t partition_id)
{
    return flash_transaction_fb_set_active_app_partition(
        &s_flash_transaction, partition_id);
}

bool flash_transaction_ao_resolve_range(uint32_t absolute_offset,
                                        uint32_t length,
                                        uint32_t *partition_id,
                                        uint32_t *relative_offset)
{
    return flash_transaction_fb_resolve_range(absolute_offset, length,
                                              partition_id, relative_offset);
}

bool flash_transaction_ao_submit(const flash_transaction_request_t *request)
{
    if (request == NULL) {
        return false;
    }
    flash_transaction_request_t effective = *request;
    if (effective.completion_lease == NULL &&
        effective.requester != FLASH_TRANSACTION_REQUESTER_OTA_JOURNAL &&
        effective.requester != FLASH_TRANSACTION_REQUESTER_VALIDATION) {
        /* Validation Scratch is deliberately self-restoring and has no
         * durable object identity.  Attaching the production completion
         * journal would make the final erase share the first erase's stable
         * fingerprint and replay COMMITTED without executing physical IO.
         * It still runs through this AO owner, policy, park and verify path. */
        effective.completion_lease = s_completion_lease;
    }
    return flash_transaction_fb_submit(&s_flash_transaction, &effective);
}

void flash_transaction_ao_service(void)
{
    flash_transaction_fb_service(&s_flash_transaction);
}

bool flash_transaction_ao_request_abort(uint32_t job_id)
{
    return flash_transaction_fb_request_abort(&s_flash_transaction, job_id);
}

bool flash_transaction_ao_notify_provider_reset(uint32_t provider_generation)
{
    return flash_transaction_fb_notify_provider_reset(&s_flash_transaction,
                                                      provider_generation);
}

bool flash_transaction_ao_get_vector(flash_transaction_vector_t *vector)
{
    return flash_transaction_fb_get_vector(&s_flash_transaction, vector);
}

/* Completion journaling is owned by FlashTransactionAO as well.  A journal
 * append is requested from inside the active transaction's completion lease;
 * submitting it back through flash_transaction_fb would therefore be a
 * re-entrant submit and fail with COMPLETION while the owner is still busy.
 * Execute only the journal's bounded physical page/sector operation inside
 * the already parked owner session.  No second owner, lockout lease, or
 * completion lease is created here. */
static __attribute__((noinline)) bool flash_transaction_ao_execute_nested_journal(
    const flash_transaction_request_t *request,
    flash_transaction_completion_t *completion)
{
    if (request == NULL || completion == NULL ||
        request->requester != FLASH_TRANSACTION_REQUESTER_OTA_JOURNAL ||
        !s_flash_transaction.occupied || !s_flash_transaction.core1_parked ||
        request->completion_lease != NULL) {
        return false;
    }

    /* Abort is an owner decision, not a backend-local best effort.  Do not
     * touch the journal once a producer has revoked its lease. */
    if (s_flash_transaction.vector.abort_pending != 0u) {
        (void)memset(completion, 0, sizeof(*completion));
        completion->job_id = s_flash_transaction.vector.job_id;
        completion->transaction_generation =
            s_flash_transaction.vector.transaction_generation;
        completion->result = FLASH_TRANSACTION_RESULT_ABORTED;
        completion->error = FLASH_TRANSACTION_ERROR_ABORTED;
        return false;
    }

    uint32_t absolute_offset = 0u;
    const flash_map_access_t access = {
        .context = FLASH_MAP_CONTEXT_APP,
        .active_app_partition_id =
            s_flash_transaction.active_app_partition_id,
        .scratch_lease = false,
    };
    const flash_map_operation_t map_operation = FLASH_MAP_OPERATION_WRITE;
    if ((request->operation != FLASH_TRANSACTION_OPERATION_ERASE &&
         request->operation != FLASH_TRANSACTION_OPERATION_PROGRAM) ||
        !flash_map_operation_allowed(&access, request->partition_id,
                                     map_operation, request->relative_offset,
                                     request->length, &absolute_offset)) {
        return false;
    }

    diagnostics_watchdog_flash_transaction_progress();

    bool ok = false;
    if (s_flash_transaction.vector.abort_pending == 0u) {
        if (request->operation == FLASH_TRANSACTION_OPERATION_ERASE) {
            ok = flash_transaction_erase(absolute_offset, request->length) &&
                 flash_transaction_verify_erased(absolute_offset,
                                                 request->length);
        } else {
            ok = request->data != NULL &&
                 flash_transaction_program(absolute_offset, request->data,
                                            request->length) &&
                 flash_transaction_verify_programmed(absolute_offset,
                                                     request->data,
                                                     request->length);
        }
    }
    diagnostics_watchdog_flash_transaction_progress();

    (void)memset(completion, 0, sizeof(*completion));
    completion->job_id = s_flash_transaction.vector.job_id;
    completion->transaction_generation =
        s_flash_transaction.vector.transaction_generation;
    completion->processed_bytes = ok ? request->length : 0u;
    completion->verified_bytes = ok ? request->length : 0u;
    completion->level = ok ? FLASH_TRANSACTION_COMPLETION_COMMITTED
                           : FLASH_TRANSACTION_COMPLETION_NONE;
    completion->result = ok ? FLASH_TRANSACTION_RESULT_COMMITTED
                            : FLASH_TRANSACTION_RESULT_FAILED;
    completion->error = ok ? FLASH_TRANSACTION_ERROR_NONE
                           : (s_flash_transaction.vector.abort_pending != 0u
                                  ? FLASH_TRANSACTION_ERROR_ABORTED
                                  : FLASH_TRANSACTION_ERROR_RAW_OPERATION);
    return ok;
}

static uint32_t flash_transaction_ao_next_journal_generation(void)
{
    s_journal_provider_generation++;
    if (s_journal_provider_generation == 0u) {
        s_journal_provider_generation = 1u;
    }
    return s_journal_provider_generation;
}

static bool flash_transaction_ao_journal_operation(
    flash_transaction_operation_t operation, uint32_t relative_offset,
    const uint8_t *data, uint32_t length)
{
    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_OTA_JOURNAL,
        .partition_id = FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_ID,
        .operation = operation,
        .relative_offset = relative_offset,
        .length = length,
        .data = data,
        .provider_generation = operation == FLASH_TRANSACTION_OPERATION_PROGRAM
                                   ? flash_transaction_ao_next_journal_generation()
                                   : 0u,
        .store_generation = FLASH_DEPLOYMENT_MAP_VERSION,
    };
    flash_transaction_completion_t completion;
    if (s_flash_transaction.occupied) {
        return flash_transaction_ao_execute_nested_journal(&request,
                                                           &completion);
    }

    /* A checkpoint append is initiated between image transactions.  It still
     * belongs to this AO owner, so submit it as an ordinary journal intent and
     * service only its bounded state machine here.  No generic execute
     * compatibility path or second writer is exposed to the caller. */
    if (!flash_transaction_ao_submit(&request)) {
        return false;
    }
    for (uint32_t step = 0u; step < FLASH_TRANSACTION_EXECUTE_STEPS; ++step) {
        flash_transaction_ao_service();
        flash_transaction_vector_t vector;
        if (!flash_transaction_ao_get_vector(&vector)) {
            continue;
        }
        if (vector.state == FLASH_TRANSACTION_STATE_COMPLETE ||
            vector.state == FLASH_TRANSACTION_STATE_FAILED ||
            vector.state == FLASH_TRANSACTION_STATE_ABORTED) {
            completion.result = vector.last_result;
            completion.error = vector.last_error;
            return vector.state == FLASH_TRANSACTION_STATE_COMPLETE &&
                   vector.completion_level ==
                       FLASH_TRANSACTION_COMPLETION_COMMITTED;
        }
    }
    return false;
}

bool flash_transaction_ao_journal_program(uint32_t relative_offset,
                                          const uint8_t *data,
                                          uint32_t length)
{
    return flash_transaction_ao_journal_operation(
        FLASH_TRANSACTION_OPERATION_PROGRAM, relative_offset, data, length);
}

bool flash_transaction_ao_journal_erase(uint32_t relative_offset,
                                        uint32_t length)
{
    return flash_transaction_ao_journal_operation(
        FLASH_TRANSACTION_OPERATION_ERASE, relative_offset, NULL, length);
}

bool flash_transaction_ao_execute(const flash_transaction_request_t *request,
                                  flash_transaction_completion_t *completion)
{
    if (request == NULL || completion == NULL) {
        return false;
    }
    if (flash_transaction_ao_execute_nested_journal(request, completion)) {
        return true;
    }
    if (!flash_transaction_ao_submit(request)) {
        return false;
    }
    flash_transaction_vector_t vector;
    for (uint32_t step = 0u; step < FLASH_TRANSACTION_EXECUTE_STEPS; step++) {
        /* The transaction owner only publishes bounded progress.  The
         * independent WatchdogSupervisorAO decides whether the hardware
         * watchdog may be fed; direct feeding here would hide a stalled END
         * callback or a deadlocked FlashTransaction. */
        diagnostics_watchdog_flash_transaction_progress();
        flash_transaction_ao_service();
        diagnostics_watchdog_flash_transaction_progress();
        if (!flash_transaction_ao_get_vector(&vector)) {
            continue;
        }
        if (vector.state == FLASH_TRANSACTION_STATE_COMPLETE ||
            vector.state == FLASH_TRANSACTION_STATE_FAILED ||
            vector.state == FLASH_TRANSACTION_STATE_ABORTED) {
            completion->job_id = vector.job_id;
            completion->level = vector.completion_level;
            completion->result = vector.last_result;
            completion->error = vector.last_error;
            completion->processed_bytes = vector.processed_bytes;
            completion->verified_bytes = vector.verified_bytes;
            completion->transaction_generation =
                vector.transaction_generation;
            return vector.state == FLASH_TRANSACTION_STATE_COMPLETE &&
                   vector.completion_level ==
                       FLASH_TRANSACTION_COMPLETION_COMMITTED;
        }
    }
    return false;
}
