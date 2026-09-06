#include "ota_metadata_flash.h"

#include <string.h>

#include "flash_transaction.h"

static uint32_t s_provider_generation;
static uint32_t s_provider_refs;

typedef struct {
    bool active;
    uint32_t operation;
    uint32_t flash_offset;
    uint32_t length;
    const uint8_t *data;
    uint32_t job_id;
    flash_transaction_buffer_lease_t lease;
} ota_metadata_flash_async_t;

static ota_metadata_flash_async_t s_async;

static uint32_t next_provider_generation(void)
{
    s_provider_generation++;
    if (s_provider_generation == 0u) {
        s_provider_generation = 1u;
    }
    return s_provider_generation;
}

static bool provider_retain(void *context)
{
    uint32_t *refs = context;
    if (refs == NULL || *refs == UINT32_MAX) {
        return false;
    }
    (*refs)++;
    return true;
}

static void provider_release(void *context)
{
    uint32_t *refs = context;
    if (refs != NULL && *refs != 0u) {
        (*refs)--;
    }
}

static bool execute(uint32_t operation, uint32_t flash_offset,
                    const uint8_t *data, size_t length)
{
    uint32_t partition_id = 0u;
    uint32_t relative_offset = 0u;
    if (length > UINT32_MAX ||
        !flash_transaction_ao_resolve_range(flash_offset, (uint32_t)length,
                                            &partition_id, &relative_offset)) {
        return false;
    }

    const uint32_t provider_generation =
        operation == FLASH_TRANSACTION_OPERATION_PROGRAM
            ? next_provider_generation()
            : 0u;
    const flash_transaction_buffer_lease_t lease = {
        .data = data,
        .length = (uint32_t)length,
        .generation = provider_generation,
        .context = &s_provider_refs,
        .retain = provider_retain,
        .release = provider_release,
    };
    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_OTA_METADATA,
        .partition_id = partition_id,
        .operation = operation,
        .relative_offset = relative_offset,
        .length = (uint32_t)length,
        .data = data,
        .provider_generation = provider_generation,
        .store_generation = s_provider_generation,
        .buffer_lease = operation == FLASH_TRANSACTION_OPERATION_PROGRAM
                            ? &lease
                            : NULL,
        .completion_lease = flash_transaction_ao_get_completion_lease(),
    };
    flash_transaction_completion_t completion;
    return flash_transaction_ao_execute(&request, &completion);
}

static bool async_request_matches(uint32_t operation, uint32_t flash_offset,
                                  const uint8_t *data, size_t length)
{
    return s_async.active && s_async.operation == operation &&
           s_async.flash_offset == flash_offset &&
           s_async.length == length && s_async.data == data;
}

static pota_bcb_step_result_t execute_step(
    uint32_t operation, uint32_t flash_offset, const uint8_t *data,
    size_t length)
{
    if (length == 0u || length > UINT32_MAX) {
        return POTA_BCB_STEP_FAILED;
    }

    if (!s_async.active) {
        uint32_t partition_id = 0u;
        uint32_t relative_offset = 0u;
        if (!flash_transaction_ao_resolve_range(
                flash_offset, (uint32_t)length, &partition_id,
                &relative_offset)) {
            return POTA_BCB_STEP_FAILED;
        }
        (void)memset(&s_async, 0, sizeof(s_async));
        s_async.operation = operation;
        s_async.flash_offset = flash_offset;
        s_async.length = (uint32_t)length;
        s_async.data = data;
        const uint32_t provider_generation =
            operation == FLASH_TRANSACTION_OPERATION_PROGRAM
                ? next_provider_generation()
                : 0u;
        s_async.lease = (flash_transaction_buffer_lease_t){
            .data = data,
            .length = (uint32_t)length,
            .generation = provider_generation,
            .context = &s_provider_refs,
            .retain = provider_retain,
            .release = provider_release,
        };
        const flash_transaction_request_t request = {
            .requester = FLASH_TRANSACTION_REQUESTER_OTA_METADATA,
            .partition_id = partition_id,
            .operation = operation,
            .relative_offset = relative_offset,
            .length = (uint32_t)length,
            .data = data,
            .provider_generation = provider_generation,
            .store_generation = s_provider_generation,
            .buffer_lease = operation == FLASH_TRANSACTION_OPERATION_PROGRAM
                                ? &s_async.lease
                                : NULL,
            .completion_lease = flash_transaction_ao_get_completion_lease(),
        };
        if (!flash_transaction_ao_submit(&request)) {
            (void)memset(&s_async, 0, sizeof(s_async));
            return POTA_BCB_STEP_FAILED;
        }
        s_async.active = true;
        flash_transaction_vector_t vector;
        if (flash_transaction_ao_get_vector(&vector)) {
            s_async.job_id = vector.job_id;
        }
        return POTA_BCB_STEP_PENDING;
    }

    if (!async_request_matches(operation, flash_offset, data, length)) {
        return POTA_BCB_STEP_FAILED;
    }

    flash_transaction_ao_service();
    flash_transaction_vector_t vector;
    if (!flash_transaction_ao_get_vector(&vector)) {
        return POTA_BCB_STEP_PENDING;
    }
    if (s_async.job_id == 0u) {
        s_async.job_id = vector.job_id;
    }
    if (vector.job_id != s_async.job_id) {
        return POTA_BCB_STEP_FAILED;
    }
    if (vector.state != FLASH_TRANSACTION_STATE_COMPLETE &&
        vector.state != FLASH_TRANSACTION_STATE_FAILED &&
        vector.state != FLASH_TRANSACTION_STATE_ABORTED) {
        return POTA_BCB_STEP_PENDING;
    }

    const bool committed =
        vector.state == FLASH_TRANSACTION_STATE_COMPLETE &&
        vector.last_result == FLASH_TRANSACTION_RESULT_COMMITTED &&
        vector.completion_level == FLASH_TRANSACTION_COMPLETION_COMMITTED;
    (void)memset(&s_async, 0, sizeof(s_async));
    return committed ? POTA_BCB_STEP_DONE : POTA_BCB_STEP_FAILED;
}

bool ota_metadata_flash_erase(uint32_t flash_offset, size_t length)
{
    return execute(FLASH_TRANSACTION_OPERATION_ERASE, flash_offset, NULL,
                   length);
}

bool ota_metadata_flash_program(uint32_t flash_offset, const uint8_t *data,
                                size_t length)
{
    return execute(FLASH_TRANSACTION_OPERATION_PROGRAM, flash_offset, data,
                   length);
}

pota_bcb_step_result_t ota_metadata_flash_program_step(
    uint32_t flash_offset, const uint8_t *data, size_t length)
{
    if (data == NULL) {
        return POTA_BCB_STEP_FAILED;
    }
    return execute_step(FLASH_TRANSACTION_OPERATION_PROGRAM, flash_offset,
                        data, length);
}

pota_bcb_step_result_t ota_metadata_flash_erase_step(
    uint32_t flash_offset, size_t length)
{
    return execute_step(FLASH_TRANSACTION_OPERATION_ERASE, flash_offset, NULL,
                        length);
}
