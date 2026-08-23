#include "ota_journal.h"

#include <string.h>

#include "drv_flash.h"
#include "flash_deployment_map.h"
#include "flash_transaction.h"
#include "flash_transaction_journal.h"
#include "pota_types.h"

#if defined(PROJECT_FLASH_DEPLOYMENT_V2) && PROJECT_FLASH_DEPLOYMENT_V2

#define OTA_JOURNAL_SLOT_SIZE FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE
#define OTA_JOURNAL_CHECKPOINT_INTERVAL \
    (2u * FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE)
#define OTA_JOURNAL_COMPLETION_REGION_SIZE \
    (FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_SIZE / 2u)
#define OTA_JOURNAL_CHECKPOINT_REGION_OFFSET \
    OTA_JOURNAL_COMPLETION_REGION_SIZE
#define OTA_JOURNAL_CHECKPOINT_REGION_SIZE \
    (FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_SIZE - \
     OTA_JOURNAL_CHECKPOINT_REGION_OFFSET)
#define OTA_JOURNAL_COMPLETION_SLOT_COUNT \
    (OTA_JOURNAL_COMPLETION_REGION_SIZE / \
     FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE)
#define OTA_JOURNAL_CHECKPOINT_SLOT_COUNT \
    (OTA_JOURNAL_CHECKPOINT_REGION_SIZE / \
     FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE)

_Static_assert(FLASH_DEPLOYMENT_MAP_HAS_OTA_JOURNAL == 1u,
               "v2 journal requires an OTA_JOURNAL partition");
_Static_assert((FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_SIZE %
                FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE) == 0u,
               "OTA journal must contain complete program pages");
_Static_assert(POTA_STREAM_CHECKPOINT_RECORD_SIZE <= OTA_JOURNAL_SLOT_SIZE,
               "checkpoint record must fit one journal slot");
_Static_assert((OTA_JOURNAL_COMPLETION_REGION_SIZE %
                FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE) == 0u,
               "completion journal region must be erase aligned");
_Static_assert((OTA_JOURNAL_CHECKPOINT_REGION_SIZE %
                FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE) == 0u,
               "checkpoint journal region must be erase aligned");
_Static_assert(OTA_JOURNAL_COMPLETION_SLOT_COUNT != 0u &&
                   OTA_JOURNAL_CHECKPOINT_SLOT_COUNT != 0u,
               "OTA journal regions must contain complete program pages");

typedef struct {
    const ota_journal_platform_t *platform;
    uint32_t base_offset;
    uint32_t size;
} ota_journal_region_t;

static pota_stream_checkpoint_store_t s_checkpoint_store;
static flash_transaction_journal_store_t s_completion_store;
static flash_transaction_completion_lease_t s_completion_lease;
static ota_journal_platform_t s_platform;
static ota_journal_region_t s_checkpoint_region;
static ota_journal_region_t s_completion_region;
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

static bool ota_journal_region_range_valid(
    const ota_journal_region_t *region, uint32_t offset, uint32_t length)
{
    return region != NULL && length != 0u && offset < region->size &&
           length <= region->size - offset;
}

static bool ota_journal_region_read(void *context, uint32_t offset,
                                    void *data, uint32_t length)
{
    const ota_journal_region_t *region = context;
    return data != NULL && ota_journal_region_range_valid(region, offset, length) &&
           region->platform != NULL && region->platform->read != NULL &&
           region->platform->read(region->platform->context,
                                  region->base_offset + offset,
                                  data, length);
}

static bool ota_journal_region_program(void *context, uint32_t offset,
                                       const uint8_t *data, uint32_t length)
{
    const ota_journal_region_t *region = context;
    if (data == NULL || !ota_journal_region_range_valid(region, offset, length) ||
        region->platform == NULL || region->platform->program_page == NULL) {
        return false;
    }

    const uint32_t page_size = FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE;
    const uint32_t page_offset = offset - (offset % page_size);
    const uint32_t within_page = offset - page_offset;
    if (length > page_size - within_page) {
        return false;
    }

    uint8_t page[FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE];
    if (!ota_journal_region_read(context, page_offset, page, sizeof(page))) {
        return false;
    }
    for (uint32_t index = 0u; index < length; index++) {
        if ((page[within_page + index] & data[index]) != data[index]) {
            return false;
        }
        page[within_page + index] = data[index];
    }
    return region->platform->program_page(
        region->platform->context, region->base_offset + page_offset,
        page, sizeof(page));
}

static bool ota_journal_region_program_adapter(void *context, uint32_t offset,
                                               const void *data,
                                               uint32_t length)
{
    return ota_journal_region_program(context, offset, data, length);
}

static bool ota_journal_region_erase(void *context, uint32_t offset,
                                     uint32_t length)
{
    const ota_journal_region_t *region = context;
    return ota_journal_region_range_valid(region, offset, length) &&
           (offset % FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE) == 0u &&
           length == FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE &&
           region->platform != NULL && region->platform->erase_sector != NULL &&
           region->platform->erase_sector(
               region->platform->context, region->base_offset + offset, length);
}

static bool ota_journal_completion_program(
    void *context, uint32_t offset, const void *data, uint32_t length)
{
    return ota_journal_region_program(context, offset, data, length);
}

static uint32_t ota_journal_completion_crc32(const uint8_t *data,
                                             uint32_t length)
{
    return pota_crc32_compute(data, length);
}

bool ota_journal_init_with_platform(const ota_journal_platform_t *platform)
{
    s_initialized = false;
    if (platform == NULL || platform->read == NULL ||
        platform->program_page == NULL || platform->erase_sector == NULL) {
        return false;
    }
    s_platform = *platform;
    s_completion_region = (ota_journal_region_t){
        .platform = &s_platform,
        .base_offset = 0u,
        .size = OTA_JOURNAL_COMPLETION_REGION_SIZE,
    };
    s_checkpoint_region = (ota_journal_region_t){
        .platform = &s_platform,
        .base_offset = OTA_JOURNAL_CHECKPOINT_REGION_OFFSET,
        .size = OTA_JOURNAL_CHECKPOINT_REGION_SIZE,
    };
    const pota_stream_checkpoint_config_t config = {
        .context = &s_checkpoint_region,
        .read = ota_journal_region_read,
        .program = ota_journal_region_program_adapter,
        .erase = ota_journal_region_erase,
        .base_offset = 0u,
        .slot_count = OTA_JOURNAL_CHECKPOINT_SLOT_COUNT,
        .slot_size = OTA_JOURNAL_SLOT_SIZE,
        .erase_size = FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE,
    };
    const flash_transaction_journal_config_t completion_config = {
        .context = &s_completion_region,
        .read = ota_journal_region_read,
        .program = ota_journal_completion_program,
        .crc32 = ota_journal_completion_crc32,
        .base_offset = 0u,
        .slot_count = OTA_JOURNAL_COMPLETION_SLOT_COUNT,
        .slot_size = OTA_JOURNAL_SLOT_SIZE,
    };
    if (pota_stream_checkpoint_init(&s_checkpoint_store, &config) !=
            POTA_STREAM_CHECKPOINT_OK ||
        !flash_transaction_journal_init(&s_completion_store,
                                        &completion_config) ||
        !flash_transaction_journal_make_completion_lease(
            &s_completion_store, &s_completion_lease) ||
        !flash_transaction_ao_set_completion_lease(&s_completion_lease)) {
        return false;
    }
    s_initialized = true;
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
