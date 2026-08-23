#include "ota_journal.h"

#include <string.h>

#include "drv_flash.h"
#include "flash_deployment_map.h"
#include "flash_transaction.h"

#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2

#define OTA_JOURNAL_SLOT_SIZE FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE
#define OTA_JOURNAL_SLOT_COUNT \
    (FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_SIZE / OTA_JOURNAL_SLOT_SIZE)
#define OTA_JOURNAL_CHECKPOINT_INTERVAL \
    (2u * FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE)

_Static_assert(FLASH_DEPLOYMENT_MAP_HAS_OTA_JOURNAL == 1u,
               "v2 journal requires an OTA_JOURNAL partition");
_Static_assert((FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_SIZE %
                FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE) == 0u,
               "OTA journal must contain complete program pages");
_Static_assert(POTA_STREAM_CHECKPOINT_RECORD_SIZE <= OTA_JOURNAL_SLOT_SIZE,
               "checkpoint record must fit one journal slot");

static pota_stream_checkpoint_store_t s_checkpoint_store;
static ota_journal_platform_t s_platform;
static uint32_t s_provider_generation;
static bool s_initialized;

static bool ota_journal_range_valid(uint32_t offset, uint32_t length)
{
    return length != 0u && offset < FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_SIZE &&
           length <= FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_SIZE - offset;
}

static bool ota_journal_default_read(void *context, uint32_t offset,
                                     void *data, uint32_t length)
{
    (void)context;
    return data != NULL && ota_journal_range_valid(offset, length) &&
           drv_flash_read(FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_OFFSET + offset,
                          data, length);
}

static bool ota_journal_default_program_page(void *context, uint32_t offset,
                                             const uint8_t *data,
                                             uint32_t length)
{
    (void)context;
    s_provider_generation++;
    if (s_provider_generation == 0u) {
        s_provider_generation = 1u;
    }
    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_OTA_JOURNAL,
        .partition_id = FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_ID,
        .operation = FLASH_TRANSACTION_OPERATION_PROGRAM,
        .relative_offset = offset,
        .length = length,
        .data = data,
        .provider_generation = s_provider_generation,
        .store_generation = FLASH_DEPLOYMENT_MAP_VERSION,
    };
    flash_transaction_completion_t completion;
    return flash_transaction_ao_execute(&request, &completion);
}

static bool ota_journal_default_erase_sector(void *context, uint32_t offset,
                                             uint32_t length)
{
    (void)context;
    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_OTA_JOURNAL,
        .partition_id = FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_ID,
        .operation = FLASH_TRANSACTION_OPERATION_ERASE,
        .relative_offset = offset,
        .length = length,
        .store_generation = FLASH_DEPLOYMENT_MAP_VERSION,
    };
    flash_transaction_completion_t completion;
    return flash_transaction_ao_execute(&request, &completion);
}

static bool ota_journal_read(void *context, uint32_t offset, void *data,
                             uint32_t length)
{
    const ota_journal_platform_t *platform = context;
    return data != NULL && ota_journal_range_valid(offset, length) &&
           platform != NULL && platform->read != NULL &&
           platform->read(platform->context, offset, data, length);
}

static bool ota_journal_program(void *context, uint32_t offset,
                                const void *data, uint32_t length)
{
    const ota_journal_platform_t *platform = context;
    if (data == NULL || !ota_journal_range_valid(offset, length)) {
        return false;
    }
    const uint32_t page_size = FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE;
    const uint32_t page_offset = offset - (offset % page_size);
    const uint32_t within_page = offset - page_offset;
    if (length > page_size - within_page) {
        return false;
    }

    uint8_t page[FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE];
    if (!ota_journal_read(context, page_offset, page, sizeof(page))) {
        return false;
    }
    const uint8_t *incoming = data;
    for (uint32_t index = 0u; index < length; index++) {
        if ((page[within_page + index] & incoming[index]) != incoming[index]) {
            return false;
        }
        page[within_page + index] = incoming[index];
    }
    return platform != NULL && platform->program_page != NULL &&
           platform->program_page(platform->context, page_offset, page,
                                  sizeof(page));
}

static bool ota_journal_erase(void *context, uint32_t offset,
                              uint32_t length)
{
    const ota_journal_platform_t *platform = context;
    return ota_journal_range_valid(offset, length) &&
           (offset % FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE) == 0u &&
           length == FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE &&
           platform != NULL && platform->erase_sector != NULL &&
           platform->erase_sector(platform->context, offset, length);
}

bool ota_journal_init_with_platform(const ota_journal_platform_t *platform)
{
    s_initialized = false;
    if (platform == NULL || platform->read == NULL ||
        platform->program_page == NULL || platform->erase_sector == NULL) {
        return false;
    }
    s_platform = *platform;
    const pota_stream_checkpoint_config_t config = {
        .context = &s_platform,
        .read = ota_journal_read,
        .program = ota_journal_program,
        .erase = ota_journal_erase,
        .base_offset = 0u,
        .slot_count = OTA_JOURNAL_SLOT_COUNT,
        .slot_size = OTA_JOURNAL_SLOT_SIZE,
        .erase_size = FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE,
    };
    s_initialized =
        pota_stream_checkpoint_init(&s_checkpoint_store, &config) ==
        POTA_STREAM_CHECKPOINT_OK;
    return s_initialized;
}

bool ota_journal_init(void)
{
    const ota_journal_platform_t platform = {
        .read = ota_journal_default_read,
        .program_page = ota_journal_default_program_page,
        .erase_sector = ota_journal_default_erase_sector,
    };
    return ota_journal_init_with_platform(&platform);
}

bool ota_journal_attach(pota_stream_session_t *session)
{
    const pota_stream_checkpoint_policy_t policy = {
        .interval_bytes = OTA_JOURNAL_CHECKPOINT_INTERVAL,
        .checkpoint_on_final = true,
    };
    return s_initialized &&
           pota_stream_session_set_checkpoint_store(
               session, &s_checkpoint_store, &policy);
}

pota_stream_checkpoint_result_t ota_journal_append(
    const pota_stream_checkpoint_t *checkpoint)
{
    if (!s_initialized) {
        return POTA_STREAM_CHECKPOINT_BAD_ARGUMENT;
    }
    return pota_stream_checkpoint_append(&s_checkpoint_store, checkpoint);
}

pota_stream_checkpoint_result_t ota_journal_recover_latest(
    pota_stream_checkpoint_t *checkpoint, uint32_t *sequence)
{
    if (!s_initialized) {
        return POTA_STREAM_CHECKPOINT_BAD_ARGUMENT;
    }
    return pota_stream_checkpoint_recover_latest(&s_checkpoint_store,
                                                  checkpoint, sequence);
}

bool ota_journal_get_snapshot(ota_journal_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->result = ota_journal_recover_latest(&snapshot->checkpoint,
                                                   &snapshot->sequence);
    snapshot->valid = snapshot->result == POTA_STREAM_CHECKPOINT_OK;
    return true;
}

#else

bool ota_journal_init(void)
{
    return false;
}

bool ota_journal_init_with_platform(const ota_journal_platform_t *platform)
{
    (void)platform;
    return false;
}

bool ota_journal_attach(pota_stream_session_t *session)
{
    (void)session;
    return false;
}

pota_stream_checkpoint_result_t ota_journal_append(
    const pota_stream_checkpoint_t *checkpoint)
{
    (void)checkpoint;
    return POTA_STREAM_CHECKPOINT_BAD_ARGUMENT;
}

pota_stream_checkpoint_result_t ota_journal_recover_latest(
    pota_stream_checkpoint_t *checkpoint, uint32_t *sequence)
{
    (void)checkpoint;
    (void)sequence;
    return POTA_STREAM_CHECKPOINT_BAD_ARGUMENT;
}

bool ota_journal_get_snapshot(ota_journal_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->result = POTA_STREAM_CHECKPOINT_BAD_ARGUMENT;
    return true;
}

#endif
