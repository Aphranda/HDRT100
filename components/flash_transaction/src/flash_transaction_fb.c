#include "flash_transaction_fb.h"

#include <stddef.h>
#include <string.h>

#include "project_config.h"
#include "algo_checksum.h"

/* Which COMPLETION site failed (0 = none).  Diagnostic only. */
static uint32_t s_completion_site;
uint32_t flash_transaction_completion_site(void)
{
    return s_completion_site;
}
#define FLASH_COMPLETION_SITE(n) do { s_completion_site = (n); } while (0)



#define FLASH_TRANSACTION_INVALID_PARTITION UINT32_MAX

static uint32_t flash_transaction_request_fingerprint(
    const flash_transaction_request_t *request)
{
    if (request == NULL) {
        return 0u;
    }
    uint32_t hash = ALGO_FNV1A32_OFFSET;
    /* provider generation is a runtime buffer lease and may legitimately be
     * re-created after reset.  store generation belongs to the durable object
     * epoch and must remain part of its stable identity. */
    const uint32_t fields[] = {
        request->requester, request->partition_id, request->operation,
        request->relative_offset, request->length, request->store_generation,
    };
    const uint8_t *bytes = (const uint8_t *)fields;
    hash = algo_fnv1a32_update(hash, bytes, sizeof(fields));
    if (request->operation == FLASH_TRANSACTION_OPERATION_PROGRAM &&
        request->data != NULL) {
        hash = algo_fnv1a32_update(hash, request->data, request->length);
    }
    return hash == 0u ? 1u : hash;
}

typedef struct {
    uint32_t id;
    uint32_t offset;
    uint32_t size;
    uint32_t app_permissions;
} flash_transaction_partition_t;

#define FLASH_TRANSACTION_PARTITION(name, id, offset, size, alignment, boot, app, factory, executable) \
    {id, offset, size, app},

static const flash_transaction_partition_t s_partitions[] = {
    FLASH_DEPLOYMENT_MAP_PARTITION_TABLE(FLASH_TRANSACTION_PARTITION)
};

#undef FLASH_TRANSACTION_PARTITION

_Static_assert((sizeof(s_partitions) / sizeof(s_partitions[0])) ==
                   FLASH_DEPLOYMENT_MAP_PARTITION_COUNT,
               "deployment partition table mismatch");

static void flash_transaction_write_begin(flash_transaction_fb_t *context)
{
    (void)__atomic_add_fetch(&context->vector.guard, 1u, __ATOMIC_ACQ_REL);
}

static void flash_transaction_write_end(flash_transaction_fb_t *context)
{
    (void)__atomic_add_fetch(&context->vector.guard, 1u, __ATOMIC_RELEASE);
}

static bool flash_transaction_provider_reset_is_pending(
    const flash_transaction_fb_t *context)
{
    return __atomic_load_n(&context->provider_reset_pending, __ATOMIC_ACQUIRE);
}

static void flash_transaction_provider_reset_set(
    flash_transaction_fb_t *context, bool pending)
{
    __atomic_store_n(&context->provider_reset_pending, pending,
                     __ATOMIC_RELEASE);
}

static bool flash_transaction_provider_lease_valid(
    const flash_transaction_fb_t *context)
{
    const flash_transaction_request_t *request = &context->request;
    const flash_transaction_buffer_lease_t *lease = request->buffer_lease;
    return request->operation == FLASH_TRANSACTION_OPERATION_PROGRAM &&
           lease != NULL && lease->data != NULL &&
           lease->length >= request->length &&
           lease->generation == request->provider_generation &&
           lease->retain != NULL && lease->release != NULL;
}

static bool flash_transaction_provider_retain(flash_transaction_fb_t *context)
{
    if (context->request.operation != FLASH_TRANSACTION_OPERATION_PROGRAM) {
        return true;
    }
    if (context->request.buffer_lease == NULL) {
        return context->request.length <= sizeof(context->owned_payload);
    }
    if (!flash_transaction_provider_lease_valid(context) ||
        !context->request.buffer_lease->retain(
            context->request.buffer_lease->context)) {
        return false;
    }
    context->buffer_lease = context->request.buffer_lease;
    context->provider_retained = true;
    context->request.data = context->buffer_lease->data;
    return true;
}

static void flash_transaction_provider_release(flash_transaction_fb_t *context)
{
    if (context->provider_retained && context->buffer_lease != NULL) {
        context->buffer_lease->release(context->buffer_lease->context);
    }
    context->provider_retained = false;
    context->buffer_lease = NULL;
}

static bool flash_transaction_completion_lease_valid(
    const flash_transaction_fb_t *context)
{
    const flash_transaction_completion_lease_t *lease =
        context->request.completion_lease;
    return lease != NULL && lease->retain != NULL && lease->release != NULL &&
           lease->append != NULL;
}

static bool flash_transaction_completion_find(
    flash_transaction_fb_t *context,
    flash_transaction_journal_record_t *record)
{
    const flash_transaction_completion_lease_t *lease =
        context->request.completion_lease;
    if (lease == NULL || lease->find == NULL) {
        return false;
    }
    flash_transaction_journal_record_t identity = {0};
    identity.job_id = context->vector.job_id;
    identity.transaction_generation = context->vector.transaction_generation;
    identity.provider_generation = context->vector.provider_generation;
    identity.store_generation = context->vector.store_generation;
    identity.request_fingerprint = context->request_fingerprint;
    return lease->find(lease->context, &identity, record);
}

static bool flash_transaction_completion_retain(flash_transaction_fb_t *context)
{
    const flash_transaction_completion_lease_t *lease =
        context->request.completion_lease;
    if (lease == NULL) {
        return true;
    }
    if (!flash_transaction_completion_lease_valid(context) ||
        !lease->retain(lease->context)) {
        return false;
    }
    context->completion_lease = lease;
    context->completion_retained = true;
    context->completion_terminal_published = false;
    context->completion_journal_failed = false;
    return true;
}

static bool flash_transaction_completion_append(
    flash_transaction_fb_t *context, uint32_t event)
{
    if (!context->completion_retained ||
        context->completion_lease == NULL ||
        context->completion_journal_failed) {
        return !context->completion_journal_failed;
    }
    /* ACCEPTED is published before the transaction has acquired the Flash
     * owner and parked core1.  Persisting it through OTA_JOURNAL at this
     * point would recurse into the still-occupied owner and falsely fail the
     * real operation with COMPLETION.  The accepted Vector is authoritative;
     * durable journal events begin once the owner session is active. */
    if (event == FLASH_TRANSACTION_JOURNAL_EVENT_ACCEPTED &&
        !context->core1_parked) {
        return true;
    }
    /* The OTA image path has a separate 64 KiB stream checkpoint.  Persisting
     * PROGRAMMED and VERIFIED per 4 KiB transaction tripled journal writes
     * without adding a recovery boundary: the checkpoint never advances until
     * the terminal COMMITTED record is durable.  Keep one terminal record for
     * OTA_IMAGE; BCB/metadata transactions keep the full sequence. */
    if (context->request.requester == FLASH_TRANSACTION_REQUESTER_OTA_IMAGE &&
        (event == FLASH_TRANSACTION_JOURNAL_EVENT_PROGRAMMED ||
         event == FLASH_TRANSACTION_JOURNAL_EVENT_VERIFIED)) {
        return true;
    }
    const flash_transaction_journal_record_t record = {
        .job_id = context->vector.job_id,
        .transaction_generation = context->vector.transaction_generation,
        .provider_generation = context->vector.provider_generation,
        .store_generation = context->vector.store_generation,
        .request_fingerprint = context->request_fingerprint,
        .event = event,
        .result = event == FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED
                      ? FLASH_TRANSACTION_RESULT_COMMITTED
                      : context->vector.last_result,
        .error = event == FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED
                     ? FLASH_TRANSACTION_ERROR_NONE
                     : context->vector.last_error,
        .processed_bytes = context->vector.processed_bytes,
        .verified_bytes = context->vector.verified_bytes,
    };
    if (!context->completion_lease->append(context->completion_lease->context,
                                          &record)) {
        context->completion_journal_failed = true;
        return false;
    }
    if (event == FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED ||
        event == FLASH_TRANSACTION_JOURNAL_EVENT_FAILED ||
        event == FLASH_TRANSACTION_JOURNAL_EVENT_ABORTED) {
        context->completion_terminal_published = true;
    }
    return true;
}

static bool flash_transaction_replay_terminal(
    flash_transaction_fb_t *context)
{
    if (!context->completion_retained ||
        context->request.completion_lease == NULL ||
        context->request.completion_lease->find == NULL) {
        return false;
    }
    flash_transaction_journal_record_t record;
    if (!flash_transaction_completion_find(context, &record)) {
        return false;
    }
    /* A durable identity already has a result.  Do not execute raw IO again;
     * expose the committed terminal as a replayed no-op, and reject any
     * non-terminal/failed history conservatively. */
    if (record.event != FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED) {
        flash_transaction_write_begin(context);
        context->vector.last_result = FLASH_TRANSACTION_RESULT_FAILED;
        FLASH_COMPLETION_SITE(91);
        context->vector.last_error = FLASH_TRANSACTION_ERROR_COMPLETION;
        context->vector.state = FLASH_TRANSACTION_STATE_RELEASE;
        flash_transaction_write_end(context);
        context->terminal_state = FLASH_TRANSACTION_STATE_FAILED;
        return true;
    }
    flash_transaction_write_begin(context);
    context->vector.completion_level = FLASH_TRANSACTION_COMPLETION_COMMITTED;
    context->vector.last_result = FLASH_TRANSACTION_RESULT_COMMITTED;
    context->vector.last_error = FLASH_TRANSACTION_ERROR_NONE;
    context->vector.processed_bytes = record.processed_bytes;
    context->vector.verified_bytes = record.verified_bytes;
    context->vector.state = FLASH_TRANSACTION_STATE_RELEASE;
    flash_transaction_write_end(context);
    context->terminal_state = FLASH_TRANSACTION_STATE_COMPLETE;
    /* The durable record already contains the terminal boundary.  Mark it
     * published so RELEASE does not append a duplicate COMMITTED record after
     * a reset replay. */
    context->completion_terminal_published = true;
    context->durable_replay = true;
    return true;
}

static void flash_transaction_completion_release(flash_transaction_fb_t *context)
{
    if (context->completion_retained && context->completion_lease != NULL) {
        context->completion_lease->release(context->completion_lease->context);
    }
    context->completion_retained = false;
    context->completion_lease = NULL;
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
    for (uint32_t index = 0u; index < FLASH_DEPLOYMENT_MAP_PARTITION_COUNT;
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
    for (uint32_t index = 0u; index < FLASH_DEPLOYMENT_MAP_PARTITION_COUNT;
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
    if ((partition->app_permissions & FLASH_DEPLOYMENT_PERMISSION_WRITE) == 0u) {
        return FLASH_TRANSACTION_ERROR_PERMISSION;
    }
    if (request->relative_offset >= partition->size ||
        request->length > partition->size - request->relative_offset) {
        return FLASH_TRANSACTION_ERROR_RANGE;
    }
    const uint32_t alignment =
        request->operation == FLASH_TRANSACTION_OPERATION_ERASE
            ? FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE
            : FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE;
    if ((request->relative_offset % alignment) != 0u ||
        (request->length % alignment) != 0u) {
        return FLASH_TRANSACTION_ERROR_ALIGNMENT;
    }
    if (request->operation == FLASH_TRANSACTION_OPERATION_PROGRAM &&
        request->provider_generation == 0u) {
        return FLASH_TRANSACTION_ERROR_PROVIDER;
    }
    if (request->requester == FLASH_TRANSACTION_REQUESTER_OTA_IMAGE) {
        if (request->partition_id != FLASH_DEPLOYMENT_MAP_APP_A_ID &&
            request->partition_id != FLASH_DEPLOYMENT_MAP_APP_B_ID) {
            return FLASH_TRANSACTION_ERROR_PERMISSION;
        }
        if (context->active_app_partition_id != FLASH_DEPLOYMENT_MAP_APP_A_ID &&
            context->active_app_partition_id != FLASH_DEPLOYMENT_MAP_APP_B_ID) {
            return FLASH_TRANSACTION_ERROR_ACTIVE_UNKNOWN;
        }
        if (request->partition_id == context->active_app_partition_id) {
            return FLASH_TRANSACTION_ERROR_ACTIVE_PARTITION;
        }
    } else if (request->requester ==
               FLASH_TRANSACTION_REQUESTER_PRODUCT_CONFIG) {
        if (request->partition_id != FLASH_DEPLOYMENT_MAP_PRODUCT_NVS_ID ||
            (request->operation == FLASH_TRANSACTION_OPERATION_ERASE
                 ? (request->length != FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE)
                 : (request->length != FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE))) {
            return FLASH_TRANSACTION_ERROR_PERMISSION;
        }
    } else if (request->requester ==
               FLASH_TRANSACTION_REQUESTER_SEAT_MOUNT) {
        /* The seat mount table owns the reserved sub-region inside its carrier
         * partition; nothing else in that partition may be programmed here. */
        const uint32_t store_begin =
            FLASH_DEPLOYMENT_MAP_SEAT_MOUNT_STORE_RELATIVE_OFFSET;
        const uint32_t store_size =
            FLASH_DEPLOYMENT_MAP_SEAT_MOUNT_STORE_SIZE;
        if (request->partition_id !=
                FLASH_DEPLOYMENT_MAP_SEAT_MOUNT_STORE_PARTITION_ID ||
            request->relative_offset < store_begin ||
            request->relative_offset - store_begin >= store_size ||
            request->length >
                store_size - (request->relative_offset - store_begin) ||
            (request->operation == FLASH_TRANSACTION_OPERATION_ERASE
                 ? request->length != FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE
                 : request->length != FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE)) {
            return FLASH_TRANSACTION_ERROR_PERMISSION;
        }
    } else if (request->requester ==
               FLASH_TRANSACTION_REQUESTER_CALIBRATION) {
        /* The calibration requester owns two durable sub-regions of the same
         * CALIBRATION_NVS partition: the TRN-03 training record and the TDMA
         * boot profile (v2 map only). They are disjoint by construction
         * (flash_deployment_map.h static asserts); a request must fit inside
         * exactly one of them and use whole erase/program units. */
        const uint32_t store_begin =
            FLASH_DEPLOYMENT_MAP_CALIBRATION_STORE_RELATIVE_OFFSET;
        const uint32_t store_size =
            FLASH_DEPLOYMENT_MAP_CALIBRATION_STORE_SIZE;
        const bool calibration_unit =
            request->operation == FLASH_TRANSACTION_OPERATION_ERASE
                ? request->length == FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE
                : request->length == FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE;
        bool in_region =
            request->partition_id ==
                FLASH_DEPLOYMENT_MAP_CALIBRATION_STORE_PARTITION_ID &&
            request->relative_offset >= store_begin &&
            request->relative_offset - store_begin < store_size &&
            request->length <=
                store_size - (request->relative_offset - store_begin) &&
            calibration_unit;
#if FLASH_DEPLOYMENT_MAP_HAS_TDMA_BOOT_PROFILE
        if (!in_region) {
            const uint32_t boot_begin =
                FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_RELATIVE_OFFSET;
            const uint32_t boot_size =
                FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_SIZE;
            in_region =
                request->partition_id ==
                    FLASH_DEPLOYMENT_MAP_TDMA_BOOT_PROFILE_STORE_PARTITION_ID &&
                request->relative_offset >= boot_begin &&
                request->relative_offset - boot_begin < boot_size &&
                request->length <=
                    boot_size - (request->relative_offset - boot_begin) &&
                calibration_unit;
        }
#endif
        if (!in_region) {
            return FLASH_TRANSACTION_ERROR_PERMISSION;
        }
    } else if (request->requester ==
               FLASH_TRANSACTION_REQUESTER_OTA_METADATA) {
        if (request->partition_id != FLASH_DEPLOYMENT_MAP_BOOT_CONTROL_ID) {
            return FLASH_TRANSACTION_ERROR_PERMISSION;
        }
    } else if (request->requester ==
               FLASH_TRANSACTION_REQUESTER_OTA_JOURNAL) {
        if (FLASH_DEPLOYMENT_MAP_HAS_OTA_JOURNAL == 0u ||
            request->partition_id != FLASH_DEPLOYMENT_MAP_OTA_JOURNAL_ID ||
            (request->operation == FLASH_TRANSACTION_OPERATION_ERASE
                 ? request->length != FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE
                 : request->length != FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE)) {
            return FLASH_TRANSACTION_ERROR_PERMISSION;
        }
    } else if (request->requester ==
               FLASH_TRANSACTION_REQUESTER_OTA_MANIFEST) {
        if (FLASH_DEPLOYMENT_MAP_HAS_OTA_STAGE == 0u ||
            request->partition_id != FLASH_DEPLOYMENT_MAP_OTA_STAGE_ID ||
            (request->operation == FLASH_TRANSACTION_OPERATION_ERASE
                 ? request->length != FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE
                 : request->length != FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE)) {
            return FLASH_TRANSACTION_ERROR_PERMISSION;
        }
    } else if (request->requester == FLASH_TRANSACTION_REQUESTER_VALIDATION) {
#if PROJECT_ENABLE_FLASH_VALIDATION
        /* Validation is deliberately constrained to the first erase sector of
         * Scratch.  No arbitrary offset or other partition can enter the
         * transaction owner through this requester. */
        if (request->partition_id != FLASH_DEPLOYMENT_MAP_SCRATCH_ID ||
            request->relative_offset != 0u ||
            (request->operation == FLASH_TRANSACTION_OPERATION_ERASE
                 ? request->length != FLASH_DEPLOYMENT_GEOMETRY_ERASE_SIZE
                 : request->length != FLASH_DEPLOYMENT_GEOMETRY_PROGRAM_SIZE)) {
            return FLASH_TRANSACTION_ERROR_PERMISSION;
        }
#else
        return FLASH_TRANSACTION_ERROR_POLICY;
#endif
    } else {
        return FLASH_TRANSACTION_ERROR_PERMISSION;
    }
    if (request->operation == FLASH_TRANSACTION_OPERATION_PROGRAM &&
        request->buffer_lease == NULL &&
        request->length <= sizeof(context->owned_payload) &&
        request->data == NULL) {
        return FLASH_TRANSACTION_ERROR_PROVIDER;
    }
    if (request->operation == FLASH_TRANSACTION_OPERATION_PROGRAM &&
        request->buffer_lease == NULL &&
        request->length > sizeof(context->owned_payload)) {
        return FLASH_TRANSACTION_ERROR_PROVIDER;
    }
    if (request->operation == FLASH_TRANSACTION_OPERATION_PROGRAM &&
        request->buffer_lease != NULL &&
        !flash_transaction_provider_lease_valid(context)) {
        return FLASH_TRANSACTION_ERROR_PROVIDER;
    }
    if (request->completion_lease != NULL &&
        !flash_transaction_completion_lease_valid(context)) {
        FLASH_COMPLETION_SITE(90);
        return FLASH_TRANSACTION_ERROR_COMPLETION;
    }
    context->absolute_offset = partition->offset + request->relative_offset;
    return FLASH_TRANSACTION_ERROR_NONE;
}

static void flash_transaction_fail(flash_transaction_fb_t *context,
                                   uint32_t error)
{
    /* Remember the FIRST failure of the current transaction.  The terminal
     * journal publish can overwrite last_error with COMPLETION when the
     * follow-up append collides, which masks the real cause. */
    if (context->first_error == FLASH_TRANSACTION_ERROR_NONE) {
        context->first_error = error;
    }
    flash_transaction_write_begin(context);
    context->vector.last_result = FLASH_TRANSACTION_RESULT_FAILED;
    context->vector.last_error = error;
    context->vector.completed_timestamp_ms = context->platform.now_ms();
    context->vector.state = context->resource_acquired ||
                                    context->provider_retained ||
                                    context->completion_retained
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
    context->vector.state = context->resource_acquired ||
                                    context->provider_retained ||
                                    context->completion_retained
                                ? FLASH_TRANSACTION_STATE_RELEASE
                                : FLASH_TRANSACTION_STATE_ABORTED;
    context->terminal_state = FLASH_TRANSACTION_STATE_ABORTED;
    flash_transaction_write_end(context);
}

static void flash_transaction_provider_reset(flash_transaction_fb_t *context)
{
    flash_transaction_write_begin(context);
    context->vector.last_result = FLASH_TRANSACTION_RESULT_FAILED;
    context->vector.last_error = FLASH_TRANSACTION_ERROR_PROVIDER;
    context->vector.completed_timestamp_ms = context->platform.now_ms();
    context->vector.state = context->resource_acquired ||
                                    context->provider_retained ||
                                    context->completion_retained
                                ? FLASH_TRANSACTION_STATE_RELEASE
                                : FLASH_TRANSACTION_STATE_FAILED;
    context->terminal_state = FLASH_TRANSACTION_STATE_FAILED;
    flash_transaction_write_end(context);
}

static bool flash_transaction_platform_valid(
    const flash_transaction_platform_t *platform)
{
    return platform != NULL && platform->policy_allows != NULL &&
           platform->acquire_flash != NULL &&
           platform->release_flash != NULL && platform->erase != NULL &&
           platform->park_core1 != NULL && platform->release_core1 != NULL &&
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
    context->vector.map_version = FLASH_DEPLOYMENT_MAP_VERSION;
}

bool flash_transaction_fb_set_active_app_partition(
    flash_transaction_fb_t *context, uint32_t partition_id)
{
    if (context == NULL ||
        (partition_id != FLASH_DEPLOYMENT_MAP_APP_A_ID &&
         partition_id != FLASH_DEPLOYMENT_MAP_APP_B_ID)) {
        return false;
    }
    context->active_app_partition_id = partition_id;
    return true;
}

/* ({occupied:1}{parked:1}) captured when the terminal journal append
 * failed: 11 = still owning+parked, 10 = owning but not parked,
 * 1 = parked but owner released, 0 = neither. */
static uint32_t s_terminal_owner_state;
uint32_t flash_transaction_fb_terminal_owner_state(void)
{
    return s_terminal_owner_state;
}

uint32_t flash_transaction_fb_first_error(const flash_transaction_fb_t *context)
{
    return context != NULL ? context->first_error : 0u;
}

static uint32_t s_submit_reject;

uint32_t flash_transaction_fb_submit_reject(void)
{
    return s_submit_reject;
}

bool flash_transaction_fb_submit(flash_transaction_fb_t *context,
                                 const flash_transaction_request_t *request)
{
    s_submit_reject = 0u;
    if (context == NULL || request == NULL) {
        s_submit_reject = 1u;
        return false;
    }
    if (context->occupied) {
        /* The owner is still running the previous transaction: the caller's
         * request never even becomes the current one, so the vector keeps the
         * old terminal result (that stale read is what made an admission
         * refusal look like a COMPLETION erase failure). */
        s_submit_reject = 2u;
        return false;
    }
    if (!flash_transaction_platform_valid(&context->platform)) {
        s_submit_reject = 3u;
        return false;
    }
    /* A caller may retry a failed operation, but it must allocate a new job
     * identity. Reusing a terminal explicit job ID would make a replay
     * indistinguishable from a fresh erase/program request. */
    if (request->job_id != 0u && context->last_terminal_valid &&
        request->job_id == context->last_terminal_job_id) {
        s_submit_reject = 4u; /* reused terminal job identity */
        return false;
    }
    context->request = *request;
    context->payload_owned = false;
    if (context->request.operation == FLASH_TRANSACTION_OPERATION_PROGRAM &&
        context->request.buffer_lease == NULL &&
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
    context->first_error = FLASH_TRANSACTION_ERROR_NONE;
    context->request_fingerprint =
        flash_transaction_request_fingerprint(&context->request);
    context->occupied = true;
    context->resource_acquired = false;
    context->core1_parked = false;
    flash_transaction_provider_reset_set(context, false);
    context->buffer_lease = NULL;
    context->provider_retained = false;
    context->completion_lease = NULL;
    context->completion_retained = false;
    context->completion_terminal_published = false;
    context->completion_journal_failed = false;
    context->durable_replay = false;
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
    context->vector.map_version = FLASH_DEPLOYMENT_MAP_VERSION;
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
    if (context->platform.step_hook != NULL) {
        context->platform.step_hook(context->platform.step_hook_context,
                                    context->vector.state);
    }
    if (flash_transaction_provider_reset_is_pending(context) &&
        context->vector.state != FLASH_TRANSACTION_STATE_RELEASE &&
        context->vector.state != FLASH_TRANSACTION_STATE_FAILED &&
        context->vector.state != FLASH_TRANSACTION_STATE_ABORTED &&
        context->vector.state != FLASH_TRANSACTION_STATE_COMPLETE) {
        flash_transaction_provider_reset(context);
        return;
    }
    switch ((flash_transaction_state_t)context->vector.state) {
    case FLASH_TRANSACTION_STATE_VALIDATE: {
        const uint32_t error = flash_transaction_validate(context);
        if (error != FLASH_TRANSACTION_ERROR_NONE) {
            flash_transaction_fail(context, error);
            break;
        }
        if (!flash_transaction_provider_retain(context)) {
            flash_transaction_fail(context, FLASH_TRANSACTION_ERROR_PROVIDER);
            break;
        }
        if (!flash_transaction_completion_retain(context)) {
            FLASH_COMPLETION_SITE(1);
            
            break;
        }
        if (flash_transaction_replay_terminal(context)) {
            break;
        }
        flash_transaction_write_begin(context);
        context->vector.completion_level = FLASH_TRANSACTION_COMPLETION_ACCEPTED;
        context->vector.state = FLASH_TRANSACTION_STATE_QUIESCE;
        flash_transaction_write_end(context);
        if (!flash_transaction_completion_append(
                context, FLASH_TRANSACTION_JOURNAL_EVENT_ACCEPTED)) {
            FLASH_COMPLETION_SITE(2);
            
        }
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
        } else if (!context->platform.park_core1()) {
            flash_transaction_fail(context, FLASH_TRANSACTION_ERROR_PARK);
        } else {
            context->core1_parked = true;
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
            context->vector.state =
                                        flash_transaction_provider_reset_is_pending(context) ||
                                            context->vector.abort_pending != 0u
                                        ? FLASH_TRANSACTION_STATE_RELEASE
                                        : FLASH_TRANSACTION_STATE_VERIFY;
            if (flash_transaction_provider_reset_is_pending(context)) {
                context->terminal_state = FLASH_TRANSACTION_STATE_FAILED;
                context->vector.last_result = FLASH_TRANSACTION_RESULT_FAILED;
                context->vector.last_error = FLASH_TRANSACTION_ERROR_PROVIDER;
                context->vector.completed_timestamp_ms =
                    context->platform.now_ms();
            } else if (context->vector.abort_pending != 0u) {
                context->terminal_state = FLASH_TRANSACTION_STATE_ABORTED;
                context->vector.last_result = FLASH_TRANSACTION_RESULT_ABORTED;
                context->vector.last_error = FLASH_TRANSACTION_ERROR_ABORTED;
                context->vector.completed_timestamp_ms =
                    context->platform.now_ms();
            }
        }
        flash_transaction_write_end(context);
        if (ok && !flash_transaction_completion_append(
                       context, FLASH_TRANSACTION_JOURNAL_EVENT_PROGRAMMED)) {
            FLASH_COMPLETION_SITE(3);
            
        }
        if (!ok) {
            if (flash_transaction_provider_reset_is_pending(context)) {
                flash_transaction_provider_reset(context);
                break;
            }
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
        if (ok && !flash_transaction_completion_append(
                       context, FLASH_TRANSACTION_JOURNAL_EVENT_VERIFIED)) {
            FLASH_COMPLETION_SITE(4);
            
        }
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
        bool release_ok = true;

        /* The completion journal is itself a FlashTransaction operation.  It
         * must be appended while this transaction still owns the Flash and
         * core1 remains parked; releasing the session first makes the nested
         * journal path reject the append as a self-reentrant operation and
         * incorrectly converts an otherwise successful transaction into
         * FLASH_TRANSACTION_ERROR_COMPLETION. */
        if (context->completion_retained &&
            !context->completion_terminal_published &&
            (context->terminal_state == FLASH_TRANSACTION_STATE_COMPLETE ||
             context->terminal_state == FLASH_TRANSACTION_STATE_FAILED ||
             context->terminal_state == FLASH_TRANSACTION_STATE_ABORTED)) {
            uint32_t event = FLASH_TRANSACTION_JOURNAL_EVENT_FAILED;
            if (context->terminal_state == FLASH_TRANSACTION_STATE_COMPLETE) {
                event = FLASH_TRANSACTION_JOURNAL_EVENT_COMMITTED;
            } else if (context->terminal_state ==
                       FLASH_TRANSACTION_STATE_ABORTED) {
                event = FLASH_TRANSACTION_JOURNAL_EVENT_ABORTED;
            }
            /* The append programs a journal slot through the nested journal
             * path, which is only legal while core1 is parked.  A RELEASE pass
             * can be re-entered after core1 was already released (measured:
             * occupied=1, parked=0 when the OTA terminal append failed on the
             * relay path), so re-park here before writing.  Parking is the C1
             * sanctioned gate: without it the write must not happen at all.
             * The session is released again by the code below. */
            if (!context->core1_parked && context->platform.park_core1 != NULL) {
                context->core1_parked = context->platform.park_core1();
            }
            if (!context->core1_parked ||
                !flash_transaction_completion_append(context, event)) {
                /* The nested journal path is only legal while this transaction
                 * still owns flash and core1 is parked.  Record both flags so
                 * a relay-only failure can be told apart from a slot problem. */
                s_terminal_owner_state =
                    (context->occupied ? 10u : 0u) +
                    (context->core1_parked ? 1u : 0u);
                context->terminal_state = FLASH_TRANSACTION_STATE_FAILED;
                flash_transaction_write_begin(context);
                context->vector.last_result =
                    FLASH_TRANSACTION_RESULT_FAILED;
                context->vector.last_error =
                    FLASH_TRANSACTION_ERROR_COMPLETION;
                flash_transaction_write_end(context);
            }
        }

        if (context->core1_parked) {
            release_ok = context->platform.release_core1();
            if (release_ok) {
                context->core1_parked = false;
            }
        }
        if (!release_ok) {
            context->terminal_state = FLASH_TRANSACTION_STATE_FAILED;
            flash_transaction_write_begin(context);
            context->vector.last_result = FLASH_TRANSACTION_RESULT_FAILED;
            context->vector.last_error = FLASH_TRANSACTION_ERROR_RELEASE;
            flash_transaction_write_end(context);

            /* A committed record may already have been written while the
             * owner/core1 session was valid.  Publish a corrective FAILED
             * record before finally dropping the lease so recovery observes
             * the release error rather than a false committed terminal. */
            if (context->completion_retained &&
                context->completion_terminal_published) {
                context->completion_terminal_published = false;
                (void)flash_transaction_completion_append(
                    context, FLASH_TRANSACTION_JOURNAL_EVENT_FAILED);
            }

            /* Retry the release once after journaling.  A failed first
             * attempt may have left core1 parked; do not silently discard the
             * lease state while the corrective record is being written. */
            if (context->core1_parked &&
                context->platform.release_core1()) {
                context->core1_parked = false;
            }
        }
        if (context->resource_acquired) {
            context->platform.release_flash();
            context->resource_acquired = false;
        }
        flash_transaction_provider_release(context);
        flash_transaction_completion_release(context);
        flash_transaction_write_begin(context);
        context->vector.state = context->terminal_state;
        context->vector.completed_timestamp_ms = context->platform.now_ms();
        flash_transaction_write_end(context);
        context->last_terminal_job_id = context->vector.job_id;
        context->last_terminal_valid = true;
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

bool flash_transaction_fb_notify_provider_reset(
    flash_transaction_fb_t *context, uint32_t provider_generation)
{
    if (context == NULL || !context->occupied ||
        context->request.operation != FLASH_TRANSACTION_OPERATION_PROGRAM ||
        context->request.provider_generation != provider_generation) {
        return false;
    }
    flash_transaction_provider_reset_set(context, true);
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
