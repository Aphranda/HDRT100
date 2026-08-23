#ifndef POTA_STREAM_SESSION_H
#define POTA_STREAM_SESSION_H

#include <stdbool.h>
#include <stdint.h>

#include "pota_session.h"
#include "pota_stream_checkpoint.h"

#define POTA_STREAM_IDENTITY_SIZE 16u
#define POTA_STREAM_PACKAGE_HASH_SIZE 32u
#define POTA_STREAM_CAP_INACTIVE_WRITE (1u << 0)
#define POTA_STREAM_CAP_DURABLE_ACK    (1u << 1)

typedef enum {
    POTA_STREAM_STATE_IDLE = 0,
    POTA_STREAM_STATE_OPEN,
    POTA_STREAM_STATE_RECEIVING,
    POTA_STREAM_STATE_READY_TO_REBOOT,
    POTA_STREAM_STATE_ABORTED,
    POTA_STREAM_STATE_FAILED,
} pota_stream_state_t;

typedef enum {
    POTA_STREAM_RESULT_OK = 0,
    POTA_STREAM_RESULT_BAD_ARGUMENT,
    POTA_STREAM_RESULT_INVALID_STATE,
    POTA_STREAM_RESULT_CAPABILITY,
    POTA_STREAM_RESULT_IDENTITY,
    POTA_STREAM_RESULT_DESTINATION,
    POTA_STREAM_RESULT_MISMATCH,
    POTA_STREAM_RESULT_OFFSET,
    POTA_STREAM_RESULT_CONFLICT,
    POTA_STREAM_RESULT_CHECKPOINT,
    POTA_STREAM_RESULT_CORE,
} pota_stream_result_t;

typedef struct {
    uint32_t session_id;
    uint32_t generation;
    uint32_t capability_mask;
    uint32_t map_version;
    uint32_t partition_id;
    uint32_t destination_slot;
    uint32_t object_id;
    uint32_t total_size;
    uint32_t package_crc32;
    bool package_mode;
    uint8_t identity[POTA_STREAM_IDENTITY_SIZE];
    uint8_t package_hash[POTA_STREAM_PACKAGE_HASH_SIZE];
} pota_stream_open_t;

typedef bool (*pota_stream_checkpoint_append_fn)(
    void *context, const pota_stream_checkpoint_t *checkpoint);
typedef pota_stream_checkpoint_result_t (*pota_stream_checkpoint_recover_fn)(
    void *context, pota_stream_checkpoint_t *checkpoint, uint32_t *sequence);

typedef struct {
    pota_session_t core;
    pota_stream_open_t open;
    pota_stream_state_t state;
    uint32_t durable_offset;
    uint32_t last_chunk_offset;
    uint32_t last_chunk_size;
    uint32_t last_chunk_crc32;
    bool last_chunk_valid;
    void *checkpoint_context;
    pota_stream_checkpoint_append_fn checkpoint_append;
    pota_stream_checkpoint_recover_fn checkpoint_recover;
    pota_stream_checkpoint_policy_t checkpoint_policy;
    uint32_t last_checkpoint_offset;
    bool resume_pending;
    bool resume_header_pending;
    pota_stream_checkpoint_t resume_checkpoint;
} pota_stream_session_t;

bool pota_stream_session_init(pota_stream_session_t *session,
                              const pota_platform_t *platform);
bool pota_stream_session_set_checkpoint(
    pota_stream_session_t *session,
    void *context,
    pota_stream_checkpoint_append_fn append,
    const pota_stream_checkpoint_policy_t *policy);
bool pota_stream_session_set_checkpoint_store(
    pota_stream_session_t *session,
    pota_stream_checkpoint_store_t *store,
    const pota_stream_checkpoint_policy_t *policy);
pota_stream_result_t pota_stream_session_open(
    pota_stream_session_t *session, const pota_stream_open_t *open);
pota_stream_result_t pota_stream_session_service(
    pota_stream_session_t *session, uint32_t budget_us);
pota_stream_result_t pota_stream_session_write(
    pota_stream_session_t *session, uint32_t offset,
    const uint8_t *data, uint32_t size);
pota_stream_result_t pota_stream_session_close(
    pota_stream_session_t *session);
pota_stream_result_t pota_stream_session_abort(
    pota_stream_session_t *session);
uint32_t pota_stream_session_token(const pota_stream_session_t *session);
uint32_t pota_stream_session_durable_offset(
    const pota_stream_session_t *session);
pota_stream_state_t pota_stream_session_state(
    const pota_stream_session_t *session);

#endif
