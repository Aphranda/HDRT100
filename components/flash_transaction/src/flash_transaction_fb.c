#include "flash_transaction_fb.h"

#include <stddef.h>
#include <string.h>

#define FLASH_TRANSACTION_INVALID_PARTITION UINT32_MAX

typedef struct {
    uint32_t id;
    uint32_t offset;
    uint32_t size;
    uint32_t app_permissions;
} flash_transaction_partition_t;

#define FLASH_TRANSACTION_PARTITION(name, id, offset, size, alignment, boot, app, factory, executable) \
    {id, offset, size, app},

static const flash_transaction_partition_t s_partitions[] = {
    FLASH_COMPAT_MAP_PARTITION_TABLE(FLASH_TRANSACTION_PARTITION)
};

#undef FLASH_TRANSACTION_PARTITION

_Static_assert((sizeof(s_partitions) / sizeof(s_partitions[0])) ==
                   FLASH_COMPAT_MAP_PARTITION_COUNT,
               "compatibility partition table mismatch");

static void flash_transaction_write_begin(flash_transaction_fb_t *context)
{
    (void)__atomic_add_fetch(&context->vector.guard, 1u, __ATOMIC_ACQ_REL);
}

static void flash_transaction_write_end(flash_transaction_fb_t *context)
{
    (void)__atomic_add_fetch(&context->vector.guard, 1u, __ATOMIC_RELEASE);
}

static void flash_transaction_set_state(flash_transaction_fb_t *context,
                                        uint32_t state)
{
    flash_transaction_write_begin(context);
    context->vector.state = state;
    flash_transaction_write_end(context);
}

static const flash_transaction_partition_t *flash_transaction_partition(
    uint32_t partition_id)
{
    for (uint32_t index = 0u; index < FLASH_COMPAT_MAP_PARTITION_COUNT;
         index++) {
        if (s_partitions[index].id == partition_id) {
            return &s_partitions[index];
        }
    }
    return NULL;
}

bool flash_transaction_fb_resolve_range(uint32_t absolute_offset,
                                        uint32_t length,
                                        uint32_t *partition_id,
                                        uint32_t *relative_offset)
{
    if (length == 0u || partition_id == NULL || relative_offset == NULL) {
        return false;
    }
    for (uint32_t index = 0u; index < FLASH_COMPAT_MAP_PARTITION_COUNT;
         index++) {
        const flash_transaction_partition_t *partition = &s_partitions[index];
        if (absolute_offset < partition->offset) {
            continue;
        }
        const uint32_t relative = absolute_offset - partition->offset;
        if (relative >= partition->size) {
            continue;
        }
        if (length > partition->size - relative) {
            return false;
        }
        *partition_id = partition->id;
        *relative_offset = relative;
        return true;
    }
    return false;
}

static uint32_t flash_transaction_validate(
    flash_transaction_fb_t *context)
{
    const flash_transaction_request_t *request = &context->request;
    const flash_transaction_partition_t *partition =
        flash_transaction_partition(request->partition_id);
    if (partition == NULL || request->requester == FLASH_TRANSACTION_REQUESTER_NONE ||
        (request->operation != FLASH_TRANSACTION_OPERATION_ERASE &&
         request->operation != FLASH_TRANSACTION_OPERATION_PROGRAM) ||
        request->length == 0u) {
        return FLASH_TRANSACTION_ERROR_BAD_ARGUMENT;
    }
    if ((partition->app_permissions & FLASH_COMPAT_MAP_PERMISSION_WRITE) == 0u) {
        return FLASH_TRANSACTION_ERROR_PERMISSION;
    }
    if (request->relative_offset >= partition->size ||
        request->length > partition->size - request->relative_offset) {
        return FLASH_TRANSACTION_ERROR_RANGE;
    }
    const uint32_t alignment =
        request->operation == FLASH_TRANSACTION_OPERATION_ERASE
            ? FLASH_COMPAT_GEOMETRY_ERASE_SIZE_BYTES
            : FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES;
    if ((request->relative_offset % alignment) != 0u ||
        (request->length % alignment) != 0u) {
        return FLASH_TRANSACTION_ERROR_ALIGNMENT;
    }
    if (request->operation == FLASH_TRANSACTION_OPERATION_PROGRAM &&
        (request->data == NULL || request->provider_generation == 0u)) {
        return FLASH_TRANSACTION_ERROR_PROVIDER;
    }
    if (request->requester == FLASH_TRANSACTION_REQUESTER_OTA_IMAGE) {
        if (request->partition_id != FLASH_COMPAT_MAP_APP_A_ID &&
            request->partition_id != FLASH_COMPAT_MAP_APP_B_ID) {
            return FLASH_TRANSACTION_ERROR_PERMISSION;
        }
        if (context->active_app_partition_id != FLASH_COMPAT_MAP_APP_A_ID &&
            context->active_app_partition_id != FLASH_COMPAT_MAP_APP_B_ID) {
            return FLASH_TRANSACTION_ERROR_ACTIVE_UNKNOWN;
        }
        if (request->partition_id == context->active_app_partition_id) {
            return FLASH_TRANSACTION_ERROR_ACTIVE_PARTITION;
        }
    } else if (request->requester ==
               FLASH_TRANSACTION_REQUESTER_PRODUCT_CONFIG) {
        if (request->partition_id != FLASH_COMPAT_MAP_PRODUCT_NVS_ID ||
            request->relative_offset != 0u ||
            (request->operation == FLASH_TRANSACTION_OPERATION_ERASE
                 ? request->length != FLASH_COMPAT_GEOMETRY_ERASE_SIZE_BYTES
                 : request->length != FLASH_COMPAT_GEOMETRY_PROGRAM_SIZE_BYTES)) {
            return FLASH_TRANSACTION_ERROR_PERMISSION;
        }
    } else if (request->requester ==
               FLASH_TRANSACTION_REQUESTER_OTA_METADATA) {
        if (request->partition_id != FLASH_COMPAT_MAP_BOOT_CONTROL_ID) {
            return FLASH_TRANSACTION_ERROR_PERMISSION;
        }
    } else {
        return FLASH_TRANSACTION_ERROR_PERMISSION;
    }
    /* Until the immutable multi-page provider lands, never pass an aliased
       producer buffer to the raw writer. */
    if (request->operation == FLASH_TRANSACTION_OPERATION_PROGRAM &&
        request->length > sizeof(context->owned_payload)) {
        return FLASH_TRANSACTION_ERROR_PROVIDER;
    }
    context->absolute_offset = partition->offset + request->relative_offset;
    return FLASH_TRANSACTION_ERROR_NONE;
}

static void flash_transaction_fail(flash_transaction_fb_t *context,
                                   uint32_t error)
{
    flash_transaction_write_begin(context);
    context->vector.last_result = FLASH_TRANSACTION_RESULT_FAILED;
    context->vector.last_error = error;
    context->vector.completed_timestamp_ms = context->platform.now_ms();
    context->vector.state = context->resource_acquired
                                ? FLASH_TRANSACTION_STATE_RELEASE
                                : FLASH_TRANSACTION_STATE_FAILED;
    context->terminal_state = FLASH_TRANSACTION_STATE_FAILED;
    flash_transaction_write_end(context);
}

static void flash_transaction_abort(flash_transaction_fb_t *context)
{
    flash_transaction_write_begin(context);
    context->vector.last_result = FLASH_TRANSACTION_RESULT_ABORTED;
    context->vector.last_error = FLASH_TRANSACTION_ERROR_ABORTED;
    context->vector.completed_timestamp_ms = context->platform.now_ms();
    context->vector.state = context->resource_acquired
                                ? FLASH_TRANSACTION_STATE_RELEASE
                                : FLASH_TRANSACTION_STATE_ABORTED;
    context->terminal_state = FLASH_TRANSACTION_STATE_ABORTED;
    flash_transaction_write_end(context);
}

static bool flash_transaction_platform_valid(
    const flash_transaction_platform_t *platform)
{
    return platform != NULL && platform->policy_allows != NULL &&
           platform->acquire_flash != NULL &&
           platform->release_flash != NULL && platform->erase != NULL &&
           platform->program != NULL && platform->verify_erased != NULL &&
           platform->verify_programmed != NULL &&
           platform->get_lockout != NULL && platform->now_ms != NULL;
}

static uint32_t flash_transaction_policy_check(
    flash_transaction_fb_t *context, uint32_t *temperature_flags)
{
    if (context->platform.policy_check != NULL) {
        return context->platform.policy_check(context->request.requester,
                                              temperature_flags);
    }
    if (temperature_flags != NULL) {
        *temperature_flags = 0u;
    }
    return context->platform.policy_allows(context->request.requester)
               ? FLASH_TRANSACTION_ERROR_NONE
               : FLASH_TRANSACTION_ERROR_POLICY;
}

void flash_transaction_fb_init(flash_transaction_fb_t *context,
                               const flash_transaction_platform_t *platform)
{
    if (context == NULL) {
        return;
    }
    memset(context, 0, sizeof(*context));
    if (platform != NULL) {
        context->platform = *platform;
    }
    context->active_app_partition_id = FLASH_TRANSACTION_INVALID_PARTITION;
    context->next_job_id = 1u;
    context->vector.state = FLASH_TRANSACTION_STATE_IDLE;
    context->vector.map_version = FLASH_COMPAT_MAP_VERSION;
}

bool flash_transaction_fb_set_active_app_partition(
    flash_transaction_fb_t *context, uint32_t partition_id)
{
    if (context == NULL ||
        (partition_id != FLASH_COMPAT_MAP_APP_A_ID &&
         partition_id != FLASH_COMPAT_MAP_APP_B_ID)) {
        return false;
    }
    context->active_app_partition_id = partition_id;
    return true;
}

bool flash_transaction_fb_submit(flash_transaction_fb_t *context,
                                 const flash_transaction_request_t *request)
{
    if (context == NULL || request == NULL || context->occupied ||
        !flash_transaction_platform_valid(&context->platform)) {
        return false;
    }
    context->request = *request;
    context->payload_owned = false;
    if (context->request.operation == FLASH_TRANSACTION_OPERATION_PROGRAM &&
        context->request.data != NULL &&
        context->request.length <= sizeof(context->owned_payload)) {
        memcpy(context->owned_payload, context->request.data,
               context->request.length);
        context->request.data = context->owned_payload;
        context->payload_owned = true;
    }
    if (context->request.job_id == 0u) {
        context->request.job_id = context->next_job_id++;
        if (context->next_job_id == 0u) {
            context->next_job_id = 1u;
        }
    }
    context->occupied = true;
    context->resource_acquired = false;
    context->terminal_state = FLASH_TRANSACTION_STATE_COMPLETE;
    uint32_t transaction_generation =
        context->vector.transaction_generation + 1u;
    if (transaction_generation == 0u) {
        transaction_generation = 1u;
    }
    flash_transaction_write_begin(context);
    const uint32_t guard = context->vector.guard;
    memset(&context->vector, 0, sizeof(context->vector));
    context->vector.guard = guard;
    context->vector.state = FLASH_TRANSACTION_STATE_VALIDATE;
    context->vector.job_id = context->request.job_id;
    context->vector.requester = context->request.requester;
    context->vector.partition_id = context->request.partition_id;
    context->vector.operation = context->request.operation;
    context->vector.requested_bytes = context->request.length;
    context->vector.map_version = FLASH_COMPAT_MAP_VERSION;
    context->vector.provider_generation =
        context->request.provider_generation;
    context->vector.store_generation = context->request.store_generation;
    context->vector.transaction_generation = transaction_generation;
    context->vector.started_timestamp_ms = context->platform.now_ms();
    flash_transaction_write_end(context);
    return true;
}

void flash_transaction_fb_service(flash_transaction_fb_t *context)
{
    if (context == NULL || !context->occupied ||
        !flash_transaction_platform_valid(&context->platform)) {
        return;
    }
    switch ((flash_transaction_state_t)context->vector.state) {
    case FLASH_TRANSACTION_STATE_VALIDATE: {
        const uint32_t error = flash_transaction_validate(context);
        if (error != FLASH_TRANSACTION_ERROR_NONE) {
            flash_transaction_fail(context, error);
            break;
        }
        flash_transaction_write_begin(context);
        context->vector.completion_level = FLASH_TRANSACTION_COMPLETION_ACCEPTED;
        context->vector.state = FLASH_TRANSACTION_STATE_QUIESCE;
        flash_transaction_write_end(context);
        break;
    }
    case FLASH_TRANSACTION_STATE_QUIESCE:
        if (context->vector.abort_pending != 0u) {
            flash_transaction_abort(context);
        } else {
            uint32_t temperature_flags = 0u;
            const uint32_t policy_error =
                flash_transaction_policy_check(context, &temperature_flags);
            if (policy_error == FLASH_TRANSACTION_ERROR_NONE) {
                flash_transaction_set_state(context,
                                            FLASH_TRANSACTION_STATE_ACQUIRE);
                break;
            }
            flash_transaction_write_begin(context);
            context->vector.temperature_flags = temperature_flags;
            context->vector.policy_gate_reason =
                policy_error;
            flash_transaction_write_end(context);
            flash_transaction_fail(context, policy_error);
        }
        break;
    case FLASH_TRANSACTION_STATE_ACQUIRE:
        if (context->vector.abort_pending != 0u) {
            flash_transaction_abort(context);
        } else if (!context->platform.acquire_flash()) {
            flash_transaction_fail(context, FLASH_TRANSACTION_ERROR_RESOURCE);
        } else {
            context->resource_acquired = true;
            flash_transaction_set_state(context,
                                        FLASH_TRANSACTION_STATE_PARK_CORE1);
        }
        break;
    case FLASH_TRANSACTION_STATE_PARK_CORE1:
        if (context->vector.abort_pending != 0u) {
            flash_transaction_abort(context);
        } else {
            flash_transaction_set_state(
                context, FLASH_TRANSACTION_STATE_ERASE_PROGRAM);
        }
        break;
    case FLASH_TRANSACTION_STATE_ERASE_PROGRAM: {
        const bool ok = context->request.operation == FLASH_TRANSACTION_OPERATION_ERASE
                            ? context->platform.erase(context->absolute_offset,
                                                      context->request.length)
                            : context->platform.program(context->absolute_offset,
                                                        context->request.data,
                                                        context->request.length);
        uint32_t request_seq = 0u;
        uint32_t ack_seq = 0u;
        uint32_t timeout_count = 0u;
        context->platform.get_lockout(&request_seq, &ack_seq, &timeout_count);
        flash_transaction_write_begin(context);
        context->vector.lockout_request_seq = request_seq;
        context->vector.lockout_ack_seq = ack_seq;
        context->vector.lockout_timeout_count = timeout_count;
        if (ok) {
            context->vector.processed_bytes = context->request.length;
            context->vector.completion_level = FLASH_TRANSACTION_COMPLETION_PROGRAMMED;
            context->vector.erase_count_delta =
                context->request.operation == FLASH_TRANSACTION_OPERATION_ERASE ? 1u : 0u;
            context->vector.program_count_delta =
                context->request.operation == FLASH_TRANSACTION_OPERATION_PROGRAM ? 1u : 0u;
            context->vector.state = FLASH_TRANSACTION_STATE_VERIFY;
        }
        flash_transaction_write_end(context);
        if (!ok) {
            flash_transaction_fail(context, FLASH_TRANSACTION_ERROR_RAW_OPERATION);
        }
        break;
    }
    case FLASH_TRANSACTION_STATE_VERIFY: {
        const bool ok = context->request.operation == FLASH_TRANSACTION_OPERATION_ERASE
                            ? context->platform.verify_erased(
                                  context->absolute_offset, context->request.length)
                            : context->platform.verify_programmed(
                                  context->absolute_offset, context->request.data,
                                  context->request.length);
        if (!ok) {
            flash_transaction_write_begin(context);
            context->vector.verify_failure_count++;
            flash_transaction_write_end(context);
            flash_transaction_fail(context, FLASH_TRANSACTION_ERROR_VERIFY);
            break;
        }
        flash_transaction_write_begin(context);
        context->vector.verified_bytes = context->request.length;
        context->vector.completion_level = FLASH_TRANSACTION_COMPLETION_VERIFIED;
        context->vector.state = context->vector.abort_pending != 0u
                                    ? FLASH_TRANSACTION_STATE_RELEASE
                                    : FLASH_TRANSACTION_STATE_COMMIT;
        if (context->vector.abort_pending != 0u) {
            context->terminal_state = FLASH_TRANSACTION_STATE_ABORTED;
            context->vector.last_result = FLASH_TRANSACTION_RESULT_ABORTED;
            context->vector.last_error = FLASH_TRANSACTION_ERROR_ABORTED;
        }
        flash_transaction_write_end(context);
        break;
    }
    case FLASH_TRANSACTION_STATE_COMMIT:
        flash_transaction_write_begin(context);
        context->vector.completion_level = FLASH_TRANSACTION_COMPLETION_COMMITTED;
        context->vector.last_result = FLASH_TRANSACTION_RESULT_COMMITTED;
        context->vector.state = FLASH_TRANSACTION_STATE_RELEASE;
        context->terminal_state = FLASH_TRANSACTION_STATE_COMPLETE;
        flash_transaction_write_end(context);
        break;
    case FLASH_TRANSACTION_STATE_RELEASE:
        if (context->resource_acquired) {
            context->platform.release_flash();
            context->resource_acquired = false;
        }
        flash_transaction_write_begin(context);
        context->vector.state = context->terminal_state;
        context->vector.completed_timestamp_ms = context->platform.now_ms();
        flash_transaction_write_end(context);
        context->occupied = false;
        break;
    case FLASH_TRANSACTION_STATE_FAILED:
    case FLASH_TRANSACTION_STATE_ABORTED:
    case FLASH_TRANSACTION_STATE_COMPLETE:
        context->occupied = false;
        break;
    case FLASH_TRANSACTION_STATE_IDLE:
    default:
        break;
    }
}

bool flash_transaction_fb_request_abort(flash_transaction_fb_t *context,
                                        uint32_t job_id)
{
    if (context == NULL || !context->occupied ||
        context->vector.job_id != job_id) {
        return false;
    }
    flash_transaction_write_begin(context);
    context->vector.abort_pending = 1u;
    flash_transaction_write_end(context);
    return true;
}

bool flash_transaction_fb_get_vector(const flash_transaction_fb_t *context,
                                     flash_transaction_vector_t *vector)
{
    if (context == NULL || vector == NULL) {
        return false;
    }
    for (uint32_t attempt = 0u; attempt < 8u; attempt++) {
        const uint32_t begin = __atomic_load_n(&context->vector.guard,
                                               __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) {
            continue;
        }
        *vector = context->vector;
        const uint32_t end = __atomic_load_n(&context->vector.guard,
                                             __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) {
            return true;
        }
    }
    return false;
}
