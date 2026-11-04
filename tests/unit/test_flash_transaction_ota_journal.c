#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "flash_transaction_fb.h"

static uint32_t s_last_offset;
static uint32_t s_last_length;

static bool yes(void)
{
    return true;
}

static void done(void)
{
}

static bool policy(uint32_t requester)
{
    return requester == FLASH_TRANSACTION_REQUESTER_OTA_JOURNAL;
}

static bool raw_erase(uint32_t offset, uint32_t length)
{
    s_last_offset = offset;
    s_last_length = length;
    return true;
}

static bool raw_program(uint32_t offset, const uint8_t *data, uint32_t length)
{
    assert(data != NULL);
    s_last_offset = offset;
    s_last_length = length;
    return true;
}

static bool verify_erased(uint32_t offset, uint32_t length)
{
    return offset == s_last_offset && length == s_last_length;
}

static bool verify_programmed(uint32_t offset, const uint8_t *data,
                              uint32_t length)
{
    return data != NULL && offset == s_last_offset && length == s_last_length;
}

static void lockout(uint32_t *request, uint32_t *ack, uint32_t *timeout)
{
    *request = 1u;
    *ack = 1u;
    *timeout = 0u;
}

static uint32_t now_ms(void)
{
    return 1u;
}

/* Regression: an OTA_IMAGE transaction with a completion lease must append the
 * ACCEPTED journal record only AFTER core1 is parked -- the journal backend is
 * owner-scoped and refuses pre-park programs (field failure: every image
 * transaction failed with COMPLETION(22), so the erase never ran). */
static bool s_core1_parked;
static unsigned s_lease_appends;
static uint32_t s_lease_last_event;

static bool image_policy(uint32_t requester)
{
    return requester == FLASH_TRANSACTION_REQUESTER_OTA_IMAGE ||
           requester == FLASH_TRANSACTION_REQUESTER_OTA_JOURNAL;
}

static bool parked_park_core1(void)
{
    s_core1_parked = true;
    return true;
}

static bool parked_release_core1(void)
{
    s_core1_parked = false;
    return true;
}

static bool lease_retain(void *context)
{
    (void)context;
    return true;
}

static void lease_release(void *context)
{
    (void)context;
}

static bool lease_append(void *context,
                         const flash_transaction_journal_record_t *record)
{
    (void)context;
    assert(s_core1_parked);
    s_lease_appends++;
    s_lease_last_event = record->event;
    return true;
}

static bool lease_find(void *context,
                       const flash_transaction_journal_record_t *identity,
                       flash_transaction_journal_record_t *record)
{
    (void)context;
    (void)identity;
    (void)record;
    return false; /* no durable history for this identity */
}

static void test_image_transaction_with_lease(void)
{
    const flash_transaction_platform_t platform = {
        .policy_allows = image_policy,
        .acquire_flash = yes,
        .release_flash = done,
        .park_core1 = parked_park_core1,
        .release_core1 = parked_release_core1,
        .erase = raw_erase,
        .program = raw_program,
        .verify_erased = verify_erased,
        .verify_programmed = verify_programmed,
        .get_lockout = lockout,
        .now_ms = now_ms,
    };
    flash_transaction_completion_lease_t lease = {
        .context = NULL,
        .retain = lease_retain,
        .release = lease_release,
        .append = lease_append,
        .find = lease_find,
    };
    flash_transaction_fb_t context;
    flash_transaction_fb_init(&context, &platform);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_DEPLOYMENT_MAP_APP_A_ID));
    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_OTA_IMAGE,
        .partition_id = FLASH_DEPLOYMENT_MAP_APP_B_ID,
        .operation = FLASH_TRANSACTION_OPERATION_ERASE,
        .relative_offset = 0u,
        .length = FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE,
        .completion_lease = &lease,
    };
    s_core1_parked = false;
    s_lease_appends = 0u;
    s_lease_last_event = 0u;
    assert(flash_transaction_fb_submit(&context, &request));
    for (uint32_t step = 0u; step < 16u && context.occupied; step++) {
        flash_transaction_fb_service(&context);
    }
    flash_transaction_vector_t vector;
    assert(flash_transaction_fb_get_vector(&context, &vector));
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(vector.last_error == FLASH_TRANSACTION_ERROR_NONE);
    /* OTA_IMAGE keeps only the terminal record; the 64 KiB stream checkpoint
     * is the recovery boundary for earlier bytes. */
    assert(s_lease_appends == 1u);
    assert(s_lease_last_event == FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED);
}

static flash_transaction_vector_t run(flash_transaction_request_t *request)
{
    const flash_transaction_platform_t platform = {
        .policy_allows = policy,
        .acquire_flash = yes,
        .release_flash = done,
        .park_core1 = yes,
        .release_core1 = yes,
        .erase = raw_erase,
        .program = raw_program,
        .verify_erased = verify_erased,
        .verify_programmed = verify_programmed,
        .get_lockout = lockout,
        .now_ms = now_ms,
    };
    flash_transaction_fb_t context;
    flash_transaction_fb_init(&context, &platform);
    assert(flash_transaction_fb_submit(&context, request));
    for (uint32_t step = 0u; step < 16u && context.occupied; step++) {
        flash_transaction_fb_service(&context);
    }
    flash_transaction_vector_t vector;
    assert(flash_transaction_fb_get_vector(&context, &vector));
    return vector;
}

int main(void)
{
    uint8_t page[FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE] = {0u};
    flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_OTA_JOURNAL,
        .partition_id = FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_ID,
        .operation = FLASH_TRANSACTION_OPERATION_PROGRAM,
        .relative_offset = FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE,
        .length = sizeof(page),
        .data = page,
        .provider_generation = 1u,
        .store_generation = 1u,
    };
    flash_transaction_vector_t vector = run(&request);
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(s_last_offset == FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_OFFSET +
                                FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE);

    request.partition_id = FLASH_DEPLOYMENT_MAP_PRODUCT_NVS_ID;
    vector = run(&request);
    assert(vector.state == FLASH_TRANSACTION_STATE_FAILED);
    assert(vector.last_error == FLASH_TRANSACTION_ERROR_PERMISSION);

    request.partition_id = FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_ID;
    request.length = 2u * FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE;
    vector = run(&request);
    assert(vector.state == FLASH_TRANSACTION_STATE_FAILED);
    assert(vector.last_error == FLASH_TRANSACTION_ERROR_PERMISSION);

    request.operation = FLASH_TRANSACTION_OPERATION_ERASE;
    request.relative_offset = 0u;
    request.length = FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE;
    request.data = NULL;
    request.provider_generation = 0u;
    vector = run(&request);
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(s_last_offset == FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_OFFSET);

    request.length = 2u * FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE;
    vector = run(&request);
    assert(vector.state == FLASH_TRANSACTION_STATE_FAILED);
    assert(vector.last_error == FLASH_TRANSACTION_ERROR_PERMISSION);

    test_image_transaction_with_lease();

    puts("flash transaction OTA journal owner tests passed");
    return 0;
}
