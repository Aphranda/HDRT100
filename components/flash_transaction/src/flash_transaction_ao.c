#include "flash_transaction.h"

#include <string.h>

#include "board.h"
#include "diagnostics.h"
#include "drv_flash_write.h"
#include "flash_transaction_fb.h"
#include "resource_arbiter.h"

#define FLASH_TRANSACTION_OWNER "FlashTransactionAO"
#define FLASH_TRANSACTION_EXECUTE_STEPS 16u

static flash_transaction_fb_t s_flash_transaction;

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
        requester != FLASH_TRANSACTION_REQUESTER_PRODUCT_CONFIG) {
        return FLASH_TRANSACTION_ERROR_POLICY;
    }
    resource_arbiter_snapshot_t arbiter;
    resource_arbiter_get_snapshot(&arbiter);
    if (arbiter.mode == RESOURCE_ARBITER_MODE_FAULT) {
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

static bool flash_transaction_erase(uint32_t offset, uint32_t length)
{
    return drv_flash_erase_parked(offset, length);
}

static bool flash_transaction_program(uint32_t offset, const uint8_t *data,
                                      uint32_t length)
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
    };
    flash_transaction_fb_init(&s_flash_transaction, &platform);
    return true;
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
    return flash_transaction_fb_submit(&s_flash_transaction, request);
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

bool flash_transaction_ao_execute(const flash_transaction_request_t *request,
                                  flash_transaction_completion_t *completion)
{
    if (request == NULL || completion == NULL ||
        !flash_transaction_ao_submit(request)) {
        return false;
    }
    flash_transaction_vector_t vector;
    for (uint32_t step = 0u; step < FLASH_TRANSACTION_EXECUTE_STEPS; step++) {
        flash_transaction_ao_service();
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
