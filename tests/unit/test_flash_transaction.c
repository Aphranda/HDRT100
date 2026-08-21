#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "flash_transaction_fb.h"

static bool s_policy_ok;
static bool s_acquire_ok;
static bool s_raw_ok;
static bool s_verify_ok;
static uint32_t s_release_count;
static uint32_t s_erase_count;
static uint32_t s_program_count;
static uint32_t s_last_offset;
static uint32_t s_last_length;
static uint32_t s_now_ms;

static bool fake_policy(uint32_t requester)
{
    return s_policy_ok && requester == FLASH_TRANSACTION_REQUESTER_OTA_IMAGE;
}

static bool fake_acquire(void)
{
    return s_acquire_ok;
}

static void fake_release(void)
{
    s_release_count++;
}

static bool fake_erase(uint32_t offset, uint32_t length)
{
    s_erase_count++;
    s_last_offset = offset;
    s_last_length = length;
    return s_raw_ok;
}

static bool fake_program(uint32_t offset, const uint8_t *data,
                         uint32_t length)
{
    assert(data != NULL);
    s_program_count++;
    s_last_offset = offset;
    s_last_length = length;
    return s_raw_ok;
}

static bool fake_verify_erased(uint32_t offset, uint32_t length)
{
    assert(offset == s_last_offset);
    assert(length == s_last_length);
    return s_verify_ok;
}

static bool fake_verify_programmed(uint32_t offset, const uint8_t *data,
                                   uint32_t length)
{
    assert(data != NULL);
    assert(offset == s_last_offset);
    assert(length == s_last_length);
    return s_verify_ok;
}

static void fake_get_lockout(uint32_t *request_seq, uint32_t *ack_seq,
                             uint32_t *timeout_count)
{
    *request_seq = 7u;
    *ack_seq = 7u;
    *timeout_count = 0u;
}

static uint32_t fake_now_ms(void)
{
    return ++s_now_ms;
}

static flash_transaction_platform_t make_platform(void)
{
    const flash_transaction_platform_t platform = {
        .policy_allows = fake_policy,
        .acquire_flash = fake_acquire,
        .release_flash = fake_release,
        .erase = fake_erase,
        .program = fake_program,
        .verify_erased = fake_verify_erased,
        .verify_programmed = fake_verify_programmed,
        .get_lockout = fake_get_lockout,
        .now_ms = fake_now_ms,
    };
    return platform;
}

static void reset_fakes(void)
{
    s_policy_ok = true;
    s_acquire_ok = true;
    s_raw_ok = true;
    s_verify_ok = true;
    s_release_count = 0u;
    s_erase_count = 0u;
    s_program_count = 0u;
    s_last_offset = 0u;
    s_last_length = 0u;
    s_now_ms = 100u;
}

static void init_context(flash_transaction_fb_t *context)
{
    reset_fakes();
    const flash_transaction_platform_t platform = make_platform();
    flash_transaction_fb_init(context, &platform);
}

static flash_transaction_request_t erase_request(void)
{
    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_OTA_IMAGE,
        .partition_id = FLASH_COMPAT_MAP_APP_B_ID,
        .operation = FLASH_TRANSACTION_OPERATION_ERASE,
        .relative_offset = 0u,
        .length = FLASH_COMPAT_GEOMETRY_ERASE_SIZE_BYTES,
        .store_generation = 9u,
    };
    return request;
}

static flash_transaction_request_t program_request(const uint8_t *data)
{
    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_OTA_IMAGE,
        .partition_id = FLASH_COMPAT_MAP_APP_B_ID,
        .operation = FLASH_TRANSACTION_OPERATION_PROGRAM,
        .relative_offset = FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES,
        .length = FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES,
        .data = data,
        .provider_generation = 4u,
        .store_generation = 9u,
    };
    return request;
}

static flash_transaction_vector_t run_to_terminal(
    flash_transaction_fb_t *context)
{
    for (uint32_t step = 0u; step < 16u && context->occupied; step++) {
        flash_transaction_fb_service(context);
    }
    assert(!context->occupied);
    flash_transaction_vector_t vector;
    assert(flash_transaction_fb_get_vector(context, &vector));
    assert((vector.guard & 1u) == 0u);
    return vector;
}

static flash_transaction_vector_t run_request(
    flash_transaction_fb_t *context,
    const flash_transaction_request_t *request)
{
    assert(flash_transaction_fb_submit(context, request));
    return run_to_terminal(context);
}

static void assert_failed(flash_transaction_vector_t vector,
                          flash_transaction_error_t error)
{
    assert(vector.state == FLASH_TRANSACTION_STATE_FAILED);
    assert(vector.last_result == FLASH_TRANSACTION_RESULT_FAILED);
    assert(vector.last_error == (uint32_t)error);
}

static void test_positive_erase_and_program(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));

    flash_transaction_request_t request = erase_request();
    flash_transaction_vector_t vector = run_request(&context, &request);
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(vector.completion_level == FLASH_TRANSACTION_COMPLETION_COMMITTED);
    assert(vector.last_result == FLASH_TRANSACTION_RESULT_COMMITTED);
    assert(vector.processed_bytes == request.length);
    assert(vector.verified_bytes == request.length);
    assert(vector.erase_count_delta == 1u);
    assert(vector.program_count_delta == 0u);
    assert(vector.lockout_request_seq == vector.lockout_ack_seq);
    assert(vector.store_generation == request.store_generation);
    assert(vector.transaction_generation == 1u);
    assert(s_last_offset == FLASH_COMPAT_MAP_APP_B_OFFSET);
    assert(s_release_count == 1u);

    uint8_t data[FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES];
    memset(data, 0xA5, sizeof(data));
    request = program_request(data);
    vector = run_request(&context, &request);
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(vector.provider_generation == request.provider_generation);
    assert(vector.transaction_generation == 2u);
    assert(vector.erase_count_delta == 0u);
    assert(vector.program_count_delta == 1u);
    assert(s_last_offset == FLASH_COMPAT_MAP_APP_B_OFFSET +
                                FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES);
    assert(s_erase_count == 1u);
    assert(s_program_count == 1u);
    assert(s_release_count == 2u);
}

static void test_policy_and_partition_rejections(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    flash_transaction_request_t request = erase_request();

    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_ACTIVE_UNKNOWN);

    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_B_ID));
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_ACTIVE_PARTITION);

    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    request.partition_id = FLASH_COMPAT_MAP_FUTURE_POOL_ID;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_PERMISSION);

    request = erase_request();
    request.partition_id = FLASH_COMPAT_MAP_PARTITION_COUNT + 10u;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_BAD_ARGUMENT);
}

static void test_range_alignment_and_provider_rejections(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));

    flash_transaction_request_t request = erase_request();
    request.relative_offset = FLASH_COMPAT_MAP_APP_B_SIZE -
                              FLASH_COMPAT_GEOMETRY_ERASE_SIZE_BYTES;
    request.length = FLASH_COMPAT_GEOMETRY_ERASE_SIZE_BYTES * 2u;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_RANGE);

    request = erase_request();
    request.relative_offset = 1u;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_ALIGNMENT);

    uint8_t data[FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES] = {0};
    request = program_request(data);
    request.provider_generation = 0u;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_PROVIDER);
    request.provider_generation = 1u;
    request.data = NULL;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_PROVIDER);
}

static void test_runtime_failures(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    flash_transaction_request_t request = erase_request();

    s_policy_ok = false;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_POLICY);
    s_policy_ok = true;

    s_acquire_ok = false;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_RESOURCE);
    s_acquire_ok = true;

    s_raw_ok = false;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_RAW_OPERATION);
    assert(s_release_count == 1u);
    s_raw_ok = true;

    s_verify_ok = false;
    flash_transaction_vector_t vector = run_request(&context, &request);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_VERIFY);
    assert(vector.verify_failure_count == 1u);
    assert(vector.processed_bytes == request.length);
    assert(vector.verified_bytes == 0u);
    assert(s_release_count == 2u);
}

static void test_busy_abort_and_snapshot(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    const flash_transaction_request_t request = erase_request();
    assert(flash_transaction_fb_submit(&context, &request));
    assert(!flash_transaction_fb_submit(&context, &request));
    flash_transaction_fb_service(&context);

    flash_transaction_vector_t vector;
    assert(flash_transaction_fb_get_vector(&context, &vector));
    assert(vector.state == FLASH_TRANSACTION_STATE_QUIESCE);
    assert(vector.completion_level == FLASH_TRANSACTION_COMPLETION_ACCEPTED);
    assert(flash_transaction_fb_request_abort(&context, vector.job_id));
    assert(!flash_transaction_fb_request_abort(&context, vector.job_id + 1u));
    vector = run_to_terminal(&context);
    assert(vector.state == FLASH_TRANSACTION_STATE_ABORTED);
    assert(vector.last_result == FLASH_TRANSACTION_RESULT_ABORTED);
    assert(vector.last_error == FLASH_TRANSACTION_ERROR_ABORTED);
    assert(s_erase_count == 0u);
    assert(s_release_count == 0u);
}

static void test_platform_and_range_resolution(void)
{
    flash_transaction_fb_t context;
    flash_transaction_fb_init(&context, NULL);
    const flash_transaction_request_t request = erase_request();
    assert(!flash_transaction_fb_submit(&context, &request));

    uint32_t partition_id = UINT32_MAX;
    uint32_t relative_offset = UINT32_MAX;
    assert(flash_transaction_fb_resolve_range(
        FLASH_COMPAT_MAP_APP_B_OFFSET,
        FLASH_COMPAT_GEOMETRY_ERASE_SIZE_BYTES,
        &partition_id, &relative_offset));
    assert(partition_id == FLASH_COMPAT_MAP_APP_B_ID);
    assert(relative_offset == 0u);
    assert(!flash_transaction_fb_resolve_range(
        FLASH_COMPAT_MAP_APP_B_OFFSET + FLASH_COMPAT_MAP_APP_B_SIZE - 1u,
        2u, &partition_id, &relative_offset));
    assert(!flash_transaction_fb_resolve_range(UINT32_MAX, 1u,
                                                &partition_id,
                                                &relative_offset));
    assert(!flash_transaction_fb_resolve_range(
        FLASH_COMPAT_MAP_APP_B_OFFSET, 0u, &partition_id,
        &relative_offset));
}

int main(void)
{
    test_positive_erase_and_program();
    test_policy_and_partition_rejections();
    test_range_alignment_and_provider_rejections();
    test_runtime_failures();
    test_busy_abort_and_snapshot();
    test_platform_and_range_resolution();
    puts("flash transaction tests passed");
    return 0;
}
