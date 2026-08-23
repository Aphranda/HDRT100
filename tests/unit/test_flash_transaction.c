#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "diagnostics.h"
#include "flash_transaction_fb.h"

static bool s_policy_ok;
static uint32_t s_policy_error;
static uint32_t s_policy_temperature_flags;
static bool s_acquire_ok;
static bool s_park_ok;
static bool s_unpark_ok;
static bool s_raw_ok;
static bool s_verify_ok;
static uint32_t s_release_count;
static uint32_t s_park_count;
static uint32_t s_unpark_count;
static uint32_t s_erase_count;
static uint32_t s_program_count;
static uint32_t s_verify_erased_count;
static uint32_t s_verify_programmed_count;
static uint32_t s_last_offset;
static uint32_t s_last_length;
static uint8_t s_last_program_first_byte;
static uint32_t s_now_ms;
static flash_transaction_fb_t *s_abort_context;
static bool s_abort_during_raw;
static bool s_abort_request_accepted;
static flash_transaction_fb_t *s_provider_reset_context;
static bool s_provider_reset_during_raw;
static bool s_provider_reset_request_accepted;
static bool s_lease_retain_ok;
static uint32_t s_lease_retain_count;
static uint32_t s_lease_release_count;
static bool s_completion_retain_ok;
static uint32_t s_completion_retain_count;
static uint32_t s_completion_release_count;
static uint32_t s_completion_append_count;
static uint32_t s_completion_fail_event;
static flash_transaction_journal_record_t s_completion_records[8];
static bool s_completion_find_enabled;
static flash_transaction_journal_record_t s_completion_find_record;
static flash_transaction_fb_t *s_step_hook_context;
static uint32_t s_step_hook_state;
static bool s_step_hook_provider_reset;
static uint32_t s_step_hook_calls;

static bool fake_lease_retain(void *context)
{
    assert(context != NULL);
    s_lease_retain_count++;
    return s_lease_retain_ok;
}

static void fake_lease_release(void *context)
{
    assert(context != NULL);
    s_lease_release_count++;
}

static bool fake_completion_retain(void *context)
{
    assert(context != NULL);
    s_completion_retain_count++;
    return s_completion_retain_ok;
}

static void fake_completion_release(void *context)
{
    assert(context != NULL);
    s_completion_release_count++;
}

static bool fake_completion_append(
    void *context, const flash_transaction_journal_record_t *record)
{
    assert(context != NULL);
    assert(record != NULL);
    if (s_completion_append_count <
        (sizeof(s_completion_records) / sizeof(s_completion_records[0]))) {
        s_completion_records[s_completion_append_count] = *record;
    }
    s_completion_append_count++;
    return record->event != s_completion_fail_event;
}

static bool fake_completion_find(
    void *context, const flash_transaction_journal_record_t *identity,
    flash_transaction_journal_record_t *record)
{
    assert(context != NULL);
    assert(identity != NULL);
    assert(record != NULL);
    if (!s_completion_find_enabled ||
        identity->job_id != s_completion_find_record.job_id ||
        identity->transaction_generation !=
            s_completion_find_record.transaction_generation ||
        identity->provider_generation !=
            s_completion_find_record.provider_generation ||
        identity->store_generation != s_completion_find_record.store_generation ||
        (identity->request_fingerprint != 0u &&
         identity->request_fingerprint !=
             s_completion_find_record.request_fingerprint)) {
        return false;
    }
    *record = s_completion_find_record;
    return true;
}

static bool fake_policy(uint32_t requester)
{
    return s_policy_ok &&
           (requester == FLASH_TRANSACTION_REQUESTER_OTA_IMAGE ||
            requester == FLASH_TRANSACTION_REQUESTER_OTA_METADATA ||
            requester == FLASH_TRANSACTION_REQUESTER_PRODUCT_CONFIG
#if PROJECT_ENABLE_FLASH_VALIDATION
            || requester == FLASH_TRANSACTION_REQUESTER_VALIDATION
#endif
           );
}

static uint32_t fake_policy_check(uint32_t requester,
                                  uint32_t *temperature_flags)
{
    if (temperature_flags != NULL) {
        *temperature_flags = s_policy_temperature_flags;
    }
    if (s_policy_error != FLASH_TRANSACTION_ERROR_NONE) {
        return s_policy_error;
    }
    return fake_policy(requester) ? FLASH_TRANSACTION_ERROR_NONE
                                  : FLASH_TRANSACTION_ERROR_POLICY;
}

static bool fake_acquire(void)
{
    return s_acquire_ok;
}

static void fake_release(void)
{
    s_release_count++;
}

static bool fake_park_core1(void)
{
    s_park_count++;
    return s_park_ok;
}

static bool fake_release_core1(void)
{
    s_unpark_count++;
    return s_unpark_ok;
}

static bool fake_erase(uint32_t offset, uint32_t length)
{
    s_erase_count++;
    s_last_offset = offset;
    s_last_length = length;
    if (s_abort_during_raw) {
        assert(s_abort_context != NULL);
        s_abort_request_accepted = flash_transaction_fb_request_abort(
            s_abort_context, s_abort_context->vector.job_id);
    }
    if (s_provider_reset_during_raw) {
        assert(s_provider_reset_context != NULL);
        s_provider_reset_request_accepted =
            flash_transaction_fb_notify_provider_reset(
                s_provider_reset_context,
                s_provider_reset_context->request.provider_generation);
    }
    return s_raw_ok;
}

static bool fake_program(uint32_t offset, const uint8_t *data,
                         uint32_t length)
{
    assert(data != NULL);
    s_program_count++;
    s_last_offset = offset;
    s_last_length = length;
    s_last_program_first_byte = data[0];
    if (s_abort_during_raw) {
        assert(s_abort_context != NULL);
        s_abort_request_accepted = flash_transaction_fb_request_abort(
            s_abort_context, s_abort_context->vector.job_id);
    }
    if (s_provider_reset_during_raw) {
        assert(s_provider_reset_context != NULL);
        s_provider_reset_request_accepted =
            flash_transaction_fb_notify_provider_reset(
                s_provider_reset_context,
                s_provider_reset_context->request.provider_generation);
    }
    return s_raw_ok;
}

static bool fake_verify_erased(uint32_t offset, uint32_t length)
{
    s_verify_erased_count++;
    assert(offset == s_last_offset);
    assert(length == s_last_length);
    return s_verify_ok;
}

static bool fake_verify_programmed(uint32_t offset, const uint8_t *data,
                                   uint32_t length)
{
    s_verify_programmed_count++;
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

static void fake_step_hook(void *context, uint32_t state)
{
    flash_transaction_fb_t *transaction = context;
    assert(transaction != NULL);
    if (s_step_hook_context != NULL) {
        assert(transaction == s_step_hook_context);
    }
    s_step_hook_calls++;
    if (s_step_hook_provider_reset && state == s_step_hook_state) {
        s_step_hook_provider_reset = false;
        assert(flash_transaction_fb_notify_provider_reset(
            transaction, transaction->request.provider_generation));
    }
}

static flash_transaction_platform_t make_platform(void)
{
    const flash_transaction_platform_t platform = {
        .policy_allows = fake_policy,
        .policy_check = fake_policy_check,
        .acquire_flash = fake_acquire,
        .release_flash = fake_release,
        .park_core1 = fake_park_core1,
        .release_core1 = fake_release_core1,
        .erase = fake_erase,
        .program = fake_program,
        .verify_erased = fake_verify_erased,
        .verify_programmed = fake_verify_programmed,
        .get_lockout = fake_get_lockout,
        .now_ms = fake_now_ms,
        .step_hook = fake_step_hook,
        .step_hook_context = s_step_hook_context,
    };
    return platform;
}

static void reset_fakes(void)
{
    s_policy_ok = true;
    s_policy_error = FLASH_TRANSACTION_ERROR_NONE;
    s_policy_temperature_flags = 0u;
    s_acquire_ok = true;
    s_park_ok = true;
    s_unpark_ok = true;
    s_raw_ok = true;
    s_verify_ok = true;
    s_release_count = 0u;
    s_park_count = 0u;
    s_unpark_count = 0u;
    s_erase_count = 0u;
    s_program_count = 0u;
    s_verify_erased_count = 0u;
    s_verify_programmed_count = 0u;
    s_last_offset = 0u;
    s_last_length = 0u;
    s_last_program_first_byte = 0u;
    s_now_ms = 100u;
    s_abort_context = NULL;
    s_abort_during_raw = false;
    s_abort_request_accepted = false;
    s_provider_reset_context = NULL;
    s_provider_reset_during_raw = false;
    s_provider_reset_request_accepted = false;
    s_lease_retain_ok = true;
    s_lease_retain_count = 0u;
    s_lease_release_count = 0u;
    s_completion_retain_ok = true;
    s_completion_retain_count = 0u;
    s_completion_release_count = 0u;
    s_completion_append_count = 0u;
    s_completion_fail_event = 0u;
    memset(s_completion_records, 0, sizeof(s_completion_records));
    s_completion_find_enabled = false;
    memset(&s_completion_find_record, 0, sizeof(s_completion_find_record));
    s_step_hook_context = NULL;
    s_step_hook_state = FLASH_TRANSACTION_STATE_IDLE;
    s_step_hook_provider_reset = false;
    s_step_hook_calls = 0u;
}

static void init_context(flash_transaction_fb_t *context)
{
    reset_fakes();
    const flash_transaction_platform_t platform = make_platform();
    flash_transaction_fb_init(context, &platform);
    s_step_hook_context = context;
    context->platform.step_hook_context = context;
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
    assert(s_park_count == 2u);
    assert(s_unpark_count == 2u);
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

    s_park_ok = false;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_PARK);
    assert(s_erase_count == 0u);
    assert(s_unpark_count == 0u);
    assert(s_release_count == 1u);
    s_park_ok = true;

    s_raw_ok = false;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_RAW_OPERATION);
    assert(s_release_count == 2u);
    assert(s_unpark_count == 1u);
    s_raw_ok = true;

    s_verify_ok = false;
    flash_transaction_vector_t vector = run_request(&context, &request);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_VERIFY);
    assert(vector.verify_failure_count == 1u);
    assert(vector.processed_bytes == request.length);
    assert(vector.verified_bytes == 0u);
    assert(s_release_count == 3u);
    assert(s_unpark_count == 2u);
}

static void test_release_failure_overrides_success(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    const flash_transaction_request_t request = erase_request();
    s_unpark_ok = false;
    const flash_transaction_vector_t vector = run_request(&context, &request);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_RELEASE);
    assert(vector.processed_bytes == request.length);
    assert(vector.verified_bytes == request.length);
    assert(s_erase_count == 1u);
    assert(s_unpark_count == 2u);
    assert(s_release_count == 1u);
}

static void test_thermal_and_diagnostics_gates_are_fail_closed(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    const flash_transaction_request_t request = erase_request();

    s_policy_temperature_flags =
        DIAGNOSTICS_SENSOR_FLAG_BOARD_TEMP_CRITICAL;
    s_policy_error = FLASH_TRANSACTION_ERROR_THERMAL;
    flash_transaction_vector_t vector = run_request(&context, &request);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_THERMAL);
    assert(vector.policy_gate_reason == FLASH_TRANSACTION_ERROR_THERMAL);
    assert(vector.temperature_flags == s_policy_temperature_flags);
    assert(s_erase_count == 0u);
    assert(s_program_count == 0u);
    assert(s_release_count == 0u);

    s_policy_temperature_flags = 0u;
    s_policy_error = FLASH_TRANSACTION_ERROR_DIAGNOSTICS_FAULT;
    vector = run_request(&context, &request);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_DIAGNOSTICS_FAULT);
    assert(vector.policy_gate_reason == FLASH_TRANSACTION_ERROR_DIAGNOSTICS_FAULT);
    assert(vector.temperature_flags == 0u);
    assert(s_erase_count == 0u);
    assert(s_program_count == 0u);
    assert(s_release_count == 0u);
}

static void test_policy_reason_hook_preserves_resource_gates(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    const flash_transaction_request_t request = erase_request();

    s_policy_error = FLASH_TRANSACTION_ERROR_TRIGGER_ACTIVE;
    flash_transaction_vector_t vector = run_request(&context, &request);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_TRIGGER_ACTIVE);
    assert(vector.policy_gate_reason == FLASH_TRANSACTION_ERROR_TRIGGER_ACTIVE);
    assert(s_erase_count == 0u);

    s_policy_error = FLASH_TRANSACTION_ERROR_MODE;
    vector = run_request(&context, &request);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_MODE);
    assert(vector.policy_gate_reason == FLASH_TRANSACTION_ERROR_MODE);
    assert(s_erase_count == 0u);

    s_policy_error = FLASH_TRANSACTION_ERROR_CALIBRATION_ACTIVE;
    vector = run_request(&context, &request);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_CALIBRATION_ACTIVE);
    assert(vector.policy_gate_reason ==
           FLASH_TRANSACTION_ERROR_CALIBRATION_ACTIVE);
    assert(s_erase_count == 0u);

    s_policy_error = FLASH_TRANSACTION_ERROR_TDMA_TRAINING_ACTIVE;
    vector = run_request(&context, &request);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_TDMA_TRAINING_ACTIVE);
    assert(vector.policy_gate_reason ==
           FLASH_TRANSACTION_ERROR_TDMA_TRAINING_ACTIVE);
    assert(s_erase_count == 0u);
}

static void test_large_payload_is_fail_closed_until_immutable_provider(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    uint8_t payload[FLASH_TRANSACTION_OWNED_PAYLOAD_SIZE +
                    FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES] = {0};
    flash_transaction_request_t request = program_request(payload);
    request.length = sizeof(payload);
    flash_transaction_vector_t vector = run_request(&context, &request);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_PROVIDER);
    assert(s_program_count == 0u);
    assert(s_erase_count == 0u);
}

static void test_large_payload_immutable_lease_lifecycle(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    uint8_t payload[FLASH_TRANSACTION_OWNED_PAYLOAD_SIZE +
                    FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES];
    memset(payload, 0x6Bu, sizeof(payload));
    flash_transaction_buffer_lease_t lease = {
        .data = payload,
        .length = sizeof(payload),
        .generation = 4u,
        .context = payload,
        .retain = fake_lease_retain,
        .release = fake_lease_release,
    };
    flash_transaction_request_t request = program_request(NULL);
    request.length = sizeof(payload);
    request.buffer_lease = &lease;

    flash_transaction_vector_t vector = run_request(&context, &request);
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(vector.completion_level == FLASH_TRANSACTION_COMPLETION_COMMITTED);
    assert(vector.processed_bytes == sizeof(payload));
    assert(vector.verified_bytes == sizeof(payload));
    assert(s_lease_retain_count == 1u);
    assert(s_lease_release_count == 1u);
    assert(s_program_count == 1u);
    assert(s_last_program_first_byte == 0x6Bu);
    assert(!context.provider_retained);

    reset_fakes();
    lease.generation++;
    vector = run_request(&context, &request);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_PROVIDER);
    assert(s_lease_retain_count == 0u);
    assert(s_lease_release_count == 0u);
    assert(s_program_count == 0u);

    reset_fakes();
    lease.generation = request.provider_generation;
    s_lease_retain_ok = false;
    vector = run_request(&context, &request);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_PROVIDER);
    assert(s_lease_retain_count == 1u);
    assert(s_lease_release_count == 0u);
    assert(s_program_count == 0u);
}

static void test_completion_lease_publishes_each_boundary_once(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    uint8_t data[FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES] = {0x4Du};
    flash_transaction_completion_lease_t completion_lease = {
        .context = &s_completion_append_count,
        .retain = fake_completion_retain,
        .release = fake_completion_release,
        .append = fake_completion_append,
    };
    flash_transaction_request_t request = program_request(data);
    request.completion_lease = &completion_lease;

    const flash_transaction_vector_t vector = run_request(&context, &request);
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(vector.completion_level == FLASH_TRANSACTION_COMPLETION_COMMITTED);
    assert(s_completion_retain_count == 1u);
    assert(s_completion_release_count == 1u);
    /* ACCEPTED is published in the RAM Vector before the owner/core1 park
     * session exists, so the durable journal begins at PROGRAMMED. */
    assert(s_completion_append_count == 3u);
    assert(s_completion_records[0].event ==
           FLASH_TRANSACTION_JOURNAL_EVENT_PROGRAMMED);
    assert(s_completion_records[1].event ==
           FLASH_TRANSACTION_JOURNAL_EVENT_VERIFIED);
    assert(s_completion_records[2].event ==
           FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED);
    assert(s_completion_records[2].result == FLASH_TRANSACTION_RESULT_COMMITTED);
    assert(s_completion_records[2].error == FLASH_TRANSACTION_ERROR_NONE);
    assert(s_completion_records[2].transaction_generation ==
           vector.transaction_generation);

    flash_transaction_fb_service(&context);
    assert(s_completion_append_count == 3u);
}

static void test_completion_journal_failure_is_fail_closed(void)
{
    const uint32_t failure_events[] = {
        FLASH_TRANSACTION_JOURNAL_EVENT_PROGRAMMED,
        FLASH_TRANSACTION_JOURNAL_EVENT_VERIFIED,
        FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED,
    };
    for (uint32_t index = 0u;
         index < (sizeof(failure_events) / sizeof(failure_events[0]));
         index++) {
        flash_transaction_fb_t context;
        init_context(&context);
        assert(flash_transaction_fb_set_active_app_partition(
            &context, FLASH_COMPAT_MAP_APP_A_ID));
        uint8_t data[FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES] = {0x91u};
        flash_transaction_completion_lease_t completion_lease = {
            .context = &s_completion_append_count,
            .retain = fake_completion_retain,
            .release = fake_completion_release,
            .append = fake_completion_append,
        };
        flash_transaction_request_t request = program_request(data);
        request.completion_lease = &completion_lease;
        s_completion_fail_event = failure_events[index];

        const flash_transaction_vector_t vector =
            run_request(&context, &request);
        assert_failed(vector, FLASH_TRANSACTION_ERROR_COMPLETION);
        assert(s_completion_retain_count == 1u);
        assert(s_completion_release_count == 1u);
        assert(s_completion_append_count == index + 1u);
        assert(s_program_count == 1u);
        assert(s_verify_programmed_count == (index >= 1u ? 1u : 0u));
        assert(s_completion_append_count <=
               (sizeof(s_completion_records) /
                sizeof(s_completion_records[0])));
    }

    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    uint8_t data[FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES] = {0xA2u};
    flash_transaction_completion_lease_t completion_lease = {
        .context = &s_completion_append_count,
        .retain = fake_completion_retain,
        .release = fake_completion_release,
        .append = fake_completion_append,
    };
    flash_transaction_request_t request = program_request(data);
    request.completion_lease = &completion_lease;
    s_unpark_ok = false;
    const flash_transaction_vector_t release_failure =
        run_request(&context, &request);
    assert_failed(release_failure, FLASH_TRANSACTION_ERROR_RELEASE);
    assert(s_completion_append_count == 4u);
    assert(s_completion_records[2].event ==
           FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED);
    assert(s_completion_records[2].error == FLASH_TRANSACTION_ERROR_NONE);
    assert(s_completion_records[3].event ==
           FLASH_TRANSACTION_JOURNAL_EVENT_FAILED);
    assert(s_completion_records[3].error == FLASH_TRANSACTION_ERROR_RELEASE);
}

static void test_two_page_ota_payload_is_owned(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    uint8_t payload[FLASH_TRANSACTION_OWNED_PAYLOAD_SIZE];
    memset(payload, 0x5Au, sizeof(payload));
    flash_transaction_request_t request = program_request(payload);
    request.length = sizeof(payload);
    assert(flash_transaction_fb_submit(&context, &request));
    payload[0] = 0xE7u;
    const flash_transaction_vector_t vector = run_to_terminal(&context);
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(context.payload_owned);
    assert(s_last_program_first_byte == 0x5Au);
}

static void test_terminal_completion_is_stable_and_duplicate_abort_is_rejected(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    const flash_transaction_request_t request = erase_request();
    const flash_transaction_vector_t first = run_request(&context, &request);
    assert(first.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(!flash_transaction_fb_request_abort(&context, first.job_id));
    flash_transaction_vector_t second;
    assert(flash_transaction_fb_get_vector(&context, &second));
    assert(second.state == first.state);
    assert(second.last_result == first.last_result);
    assert(second.transaction_generation == first.transaction_generation);
}

static void test_durable_terminal_replay_skips_raw_io(void)
{
    flash_transaction_fb_t first_context;
    init_context(&first_context);
    assert(flash_transaction_fb_set_active_app_partition(
        &first_context, FLASH_COMPAT_MAP_APP_A_ID));
    uint8_t data[FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES] = {0x6Cu};
    flash_transaction_completion_lease_t first_lease = {
        .context = &s_completion_append_count,
        .retain = fake_completion_retain,
        .release = fake_completion_release,
        .append = fake_completion_append,
    };
    flash_transaction_request_t first_request = program_request(data);
    first_request.completion_lease = &first_lease;
    const flash_transaction_vector_t first =
        run_request(&first_context, &first_request);
    assert(first.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(s_completion_append_count >= 3u);
    const flash_transaction_journal_record_t committed =
        s_completion_records[s_completion_append_count - 1u];
    assert(committed.event == FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED);

    flash_transaction_fb_t reset_context;
    init_context(&reset_context);
    assert(flash_transaction_fb_set_active_app_partition(
        &reset_context, FLASH_COMPAT_MAP_APP_A_ID));
    s_completion_find_enabled = true;
    s_completion_find_record = committed;
    flash_transaction_completion_lease_t reset_lease = {
        .context = &s_completion_append_count,
        .retain = fake_completion_retain,
        .release = fake_completion_release,
        .append = fake_completion_append,
        .find = fake_completion_find,
    };
    flash_transaction_request_t reset_request = program_request(data);
    reset_request.job_id = first.job_id;
    reset_request.completion_lease = &reset_lease;
    const flash_transaction_vector_t replay =
        run_request(&reset_context, &reset_request);
    assert(replay.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(replay.completion_level == FLASH_TRANSACTION_COMPLETION_COMMITTED);
    assert(replay.last_error == FLASH_TRANSACTION_ERROR_NONE);
    assert(s_program_count == 0u);
    assert(s_verify_programmed_count == 0u);
    assert(s_park_count == 0u);
    assert(s_completion_append_count == 3u);
}

static void test_terminal_job_id_replay_is_rejected(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    flash_transaction_request_t request = erase_request();
    request.job_id = 77u;
    const flash_transaction_vector_t first = run_request(&context, &request);
    assert(first.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(context.last_terminal_valid);
    assert(context.last_terminal_job_id == request.job_id);
    const uint32_t erase_count = s_erase_count;

    assert(!flash_transaction_fb_submit(&context, &request));
    assert(s_erase_count == erase_count);
    flash_transaction_vector_t snapshot;
    assert(flash_transaction_fb_get_vector(&context, &snapshot));
    assert(snapshot.transaction_generation == first.transaction_generation);
}

static flash_transaction_request_t product_config_request(
    uint32_t operation, const uint8_t *data)
{
    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_PRODUCT_CONFIG,
        .partition_id = FLASH_COMPAT_MAP_PRODUCT_NVS_ID,
        .operation = operation,
        .relative_offset = 0u,
        .length = operation == FLASH_TRANSACTION_OPERATION_ERASE
                      ? FLASH_COMPAT_GEOMETRY_ERASE_SIZE_BYTES
                      : FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES,
        .data = data,
        .provider_generation =
            operation == FLASH_TRANSACTION_OPERATION_PROGRAM ? 5u : 0u,
        .store_generation = 12u,
    };
    return request;
}

static void test_product_config_policy_and_owned_payload(void)
{
    flash_transaction_fb_t context;
    init_context(&context);

    flash_transaction_request_t request =
        product_config_request(FLASH_TRANSACTION_OPERATION_ERASE, NULL);
    flash_transaction_vector_t vector = run_request(&context, &request);
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(s_last_offset == FLASH_COMPAT_MAP_PRODUCT_NVS_OFFSET);

    uint8_t page[FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES];
    memset(page, 0x3Cu, sizeof(page));
    request = product_config_request(FLASH_TRANSACTION_OPERATION_PROGRAM,
                                     page);
    assert(flash_transaction_fb_submit(&context, &request));
    page[0] = 0xE7u;
    vector = run_to_terminal(&context);
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(context.payload_owned);
    assert(s_last_program_first_byte == 0x3Cu);

    /* Product Config records append at page boundaries; the transaction
     * owner must not force every update back to the first page. */
    request = product_config_request(FLASH_TRANSACTION_OPERATION_PROGRAM,
                                     page);
    request.relative_offset = FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES;
    assert(flash_transaction_fb_submit(&context, &request));
    vector = run_to_terminal(&context);
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);

    request = product_config_request(FLASH_TRANSACTION_OPERATION_ERASE, NULL);
    request.relative_offset = FLASH_COMPAT_GEOMETRY_ERASE_SIZE_BYTES;
    assert(flash_transaction_fb_submit(&context, &request));
    vector = run_to_terminal(&context);
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);

    request = product_config_request(FLASH_TRANSACTION_OPERATION_ERASE, NULL);
    request.partition_id = FLASH_COMPAT_MAP_APP_B_ID;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_PERMISSION);

    request = product_config_request(FLASH_TRANSACTION_OPERATION_PROGRAM,
                                     page);
    request.length = FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES * 2u;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_PERMISSION);
}

#if PROJECT_ENABLE_FLASH_VALIDATION
static flash_transaction_request_t validation_request(uint32_t operation,
                                                       const uint8_t *data)
{
    const flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_VALIDATION,
        .partition_id = FLASH_COMPAT_MAP_SCRATCH_ID,
        .operation = operation,
        .relative_offset = 0u,
        .length = operation == FLASH_TRANSACTION_OPERATION_ERASE
                      ? FLASH_COMPAT_GEOMETRY_ERASE_SIZE_BYTES
                      : FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES,
        .data = data,
        .provider_generation =
            operation == FLASH_TRANSACTION_OPERATION_PROGRAM ? 1u : 0u,
        .store_generation = 1u,
    };
    return request;
}

static void test_validation_is_scratch_only(void)
{
    flash_transaction_fb_t context;
    init_context(&context);

    uint8_t page[FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES];
    memset(page, 0xA5u, sizeof(page));
    flash_transaction_request_t request =
        validation_request(FLASH_TRANSACTION_OPERATION_ERASE, NULL);
    flash_transaction_vector_t vector = run_request(&context, &request);
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(s_last_offset == FLASH_COMPAT_MAP_SCRATCH_OFFSET);
    assert(s_last_length == FLASH_COMPAT_GEOMETRY_ERASE_SIZE_BYTES);

    request = validation_request(FLASH_TRANSACTION_OPERATION_PROGRAM, page);
    vector = run_request(&context, &request);
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(s_last_offset == FLASH_COMPAT_MAP_SCRATCH_OFFSET);
    assert(s_last_length == FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES);
    const uint32_t program_count = s_program_count;
    const uint32_t erase_count = s_erase_count;

    request.partition_id = FLASH_COMPAT_MAP_APP_A_ID;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_PERMISSION);
    assert(s_program_count == program_count);

    request = validation_request(FLASH_TRANSACTION_OPERATION_PROGRAM, page);
    request.relative_offset = FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_PERMISSION);
    assert(s_program_count == program_count);

    request = validation_request(FLASH_TRANSACTION_OPERATION_ERASE, NULL);
    request.length = FLASH_COMPAT_GEOMETRY_ERASE_SIZE_BYTES * 2u;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_PERMISSION);
    assert(s_erase_count == erase_count);
}
#endif

static void test_metadata_policy(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    flash_transaction_request_t request = {
        .requester = FLASH_TRANSACTION_REQUESTER_OTA_METADATA,
        .partition_id = FLASH_COMPAT_MAP_BOOT_CONTROL_ID,
        .operation = FLASH_TRANSACTION_OPERATION_ERASE,
        .relative_offset = 0u,
        .length = FLASH_COMPAT_GEOMETRY_ERASE_SIZE_BYTES * 16u,
        .store_generation = 3u,
    };
    flash_transaction_vector_t vector = run_request(&context, &request);
    assert(vector.state == FLASH_TRANSACTION_STATE_COMPLETE);
    assert(vector.completion_level == FLASH_TRANSACTION_COMPLETION_COMMITTED);
    assert(s_last_offset == FLASH_COMPAT_MAP_BOOT_CONTROL_OFFSET);

    request.partition_id = FLASH_COMPAT_MAP_PRODUCT_NVS_ID;
    assert_failed(run_request(&context, &request),
                  FLASH_TRANSACTION_ERROR_PERMISSION);
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

static void test_abort_during_raw_operation_skips_verify_and_commit(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    s_abort_context = &context;
    s_abort_during_raw = true;

    const flash_transaction_request_t erase = erase_request();
    flash_transaction_vector_t vector = run_request(&context, &erase);
    assert(s_abort_request_accepted);
    assert(vector.state == FLASH_TRANSACTION_STATE_ABORTED);
    assert(vector.last_result == FLASH_TRANSACTION_RESULT_ABORTED);
    assert(vector.last_error == FLASH_TRANSACTION_ERROR_ABORTED);
    assert(vector.completion_level == FLASH_TRANSACTION_COMPLETION_PROGRAMMED);
    assert(vector.processed_bytes == erase.length);
    assert(vector.verified_bytes == 0u);
    assert(s_verify_erased_count == 0u);
    assert(s_release_count == 1u);
    assert(s_unpark_count == 1u);

    reset_fakes();
    s_abort_context = &context;
    s_abort_during_raw = true;
    uint8_t data[FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES] = {0xA5u};
    const flash_transaction_request_t program = program_request(data);
    vector = run_request(&context, &program);
    assert(s_abort_request_accepted);
    assert(vector.state == FLASH_TRANSACTION_STATE_ABORTED);
    assert(vector.last_result == FLASH_TRANSACTION_RESULT_ABORTED);
    assert(vector.last_error == FLASH_TRANSACTION_ERROR_ABORTED);
    assert(vector.completion_level == FLASH_TRANSACTION_COMPLETION_PROGRAMMED);
    assert(vector.processed_bytes == program.length);
    assert(vector.verified_bytes == 0u);
    assert(s_verify_programmed_count == 0u);
    assert(s_release_count == 1u);
    assert(s_unpark_count == 1u);
}

static void test_provider_reset_fails_closed_before_and_during_raw(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    uint8_t data[FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES] = {0x3Cu};
    flash_transaction_request_t request = program_request(data);

    assert(flash_transaction_fb_submit(&context, &request));
    assert(!flash_transaction_fb_notify_provider_reset(
        &context, request.provider_generation + 1u));
    assert(flash_transaction_fb_notify_provider_reset(
        &context, request.provider_generation));
    flash_transaction_vector_t vector = run_to_terminal(&context);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_PROVIDER);
    assert(vector.processed_bytes == 0u);
    assert(vector.verified_bytes == 0u);
    assert(s_program_count == 0u);
    assert(s_release_count == 0u);

    reset_fakes();
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    s_provider_reset_context = &context;
    s_provider_reset_during_raw = true;
    request = program_request(data);
    vector = run_request(&context, &request);
    assert(s_provider_reset_request_accepted);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_PROVIDER);
    assert(vector.completion_level == FLASH_TRANSACTION_COMPLETION_PROGRAMMED);
    assert(vector.processed_bytes == request.length);
    assert(vector.verified_bytes == 0u);
    assert(s_program_count == 1u);
    assert(s_verify_programmed_count == 0u);
    assert(s_release_count == 1u);
    assert(s_unpark_count == 1u);
    assert(!flash_transaction_fb_notify_provider_reset(
        &context, request.provider_generation));
}

static void test_step_hook_provider_reset_is_async_and_journaled(void)
{
    flash_transaction_fb_t context;
    init_context(&context);
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    uint8_t data[FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES] = {0x71u};
    flash_transaction_request_t request = program_request(data);

    s_step_hook_state = FLASH_TRANSACTION_STATE_PARK_CORE1;
    s_step_hook_provider_reset = true;
    flash_transaction_vector_t vector = run_request(&context, &request);
    assert(s_step_hook_calls != 0u);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_PROVIDER);
    assert(vector.processed_bytes == 0u);
    assert(vector.verified_bytes == 0u);
    assert(s_program_count == 0u);
    assert(s_verify_programmed_count == 0u);
    assert(s_park_count == 0u);
    assert(s_unpark_count == 0u);

    reset_fakes();
    s_step_hook_context = &context;
    context.platform.step_hook_context = &context;
    assert(flash_transaction_fb_set_active_app_partition(
        &context, FLASH_COMPAT_MAP_APP_A_ID));
    flash_transaction_completion_lease_t completion_lease = {
        .context = &s_completion_append_count,
        .retain = fake_completion_retain,
        .release = fake_completion_release,
        .append = fake_completion_append,
    };
    request = program_request(data);
    request.completion_lease = &completion_lease;
    s_step_hook_state = FLASH_TRANSACTION_STATE_VERIFY;
    s_step_hook_provider_reset = true;
    vector = run_request(&context, &request);
    assert(s_step_hook_calls != 0u);
    assert_failed(vector, FLASH_TRANSACTION_ERROR_PROVIDER);
    assert(vector.completion_level == FLASH_TRANSACTION_COMPLETION_PROGRAMMED);
    assert(vector.processed_bytes == request.length);
    assert(vector.verified_bytes == 0u);
    assert(s_program_count == 1u);
    assert(s_verify_programmed_count == 0u);
    assert(s_completion_append_count == 2u);
    assert(s_completion_records[0].event ==
           FLASH_TRANSACTION_JOURNAL_EVENT_PROGRAMMED);
    assert(s_completion_records[1].event ==
           FLASH_TRANSACTION_JOURNAL_EVENT_FAILED);
    assert(s_completion_records[1].error == FLASH_TRANSACTION_ERROR_PROVIDER);
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
    test_release_failure_overrides_success();
    test_thermal_and_diagnostics_gates_are_fail_closed();
    test_policy_reason_hook_preserves_resource_gates();
    test_large_payload_is_fail_closed_until_immutable_provider();
    test_large_payload_immutable_lease_lifecycle();
    test_completion_lease_publishes_each_boundary_once();
    test_completion_journal_failure_is_fail_closed();
    test_durable_terminal_replay_skips_raw_io();
    test_two_page_ota_payload_is_owned();
    test_terminal_completion_is_stable_and_duplicate_abort_is_rejected();
    test_terminal_job_id_replay_is_rejected();
    test_product_config_policy_and_owned_payload();
#if PROJECT_ENABLE_FLASH_VALIDATION
    test_validation_is_scratch_only();
#endif
    test_metadata_policy();
    test_busy_abort_and_snapshot();
    test_abort_during_raw_operation_skips_verify_and_commit();
    test_provider_reset_fails_closed_before_and_during_raw();
    test_step_hook_provider_reset_is_async_and_journaled();
    test_platform_and_range_resolution();
    puts("flash transaction tests passed");
    return 0;
}
