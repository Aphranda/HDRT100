#include "ota_metadata_flash.h"

#include "flash_transaction.h"

static uint32_t s_provider_generation;

static uint32_t next_provider_generation(void)
{
    s_provider_generation++;
    if (s_provider_generation == 0u) {
        s_provider_generation = 1u;
    }
    return s_provider_generation;
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

    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_OTA_METADATA,
        .partition_id = partition_id,
        .operation = operation,
        .relative_offset = relative_offset,
        .length = (uint32_t)length,
        .data = data,
        .provider_generation = operation == FLASH_TRANSACTION_OPERATION_PROGRAM
                                   ? next_provider_generation()
                                   : 0u,
        .store_generation = s_provider_generation,
    };
    flash_transaction_completion_t completion;
    return flash_transaction_ao_execute(&request, &completion);
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
