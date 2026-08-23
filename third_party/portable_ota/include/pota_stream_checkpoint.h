#ifndef POTA_STREAM_CHECKPOINT_H
#define POTA_STREAM_CHECKPOINT_H

#include <stdbool.h>
#include <stdint.h>

#define POTA_STREAM_CHECKPOINT_MAGIC 0x50434B54u
#define POTA_STREAM_CHECKPOINT_SCHEMA_VERSION 4u
#define POTA_STREAM_CHECKPOINT_COMMIT_MARKER 0xC04D434Bu
#define POTA_STREAM_CHECKPOINT_RECORD_SIZE 64u
#define POTA_STREAM_CHECKPOINT_FLAG_ABORTED (1u << 0)

typedef bool (*pota_stream_checkpoint_read_fn)(void *context,
                                                uint32_t offset,
                                                void *data,
                                                uint32_t length);
typedef bool (*pota_stream_checkpoint_program_fn)(void *context,
                                                   uint32_t offset,
                                                   const void *data,
                                                   uint32_t length);
typedef bool (*pota_stream_checkpoint_erase_fn)(void *context,
                                                uint32_t offset,
                                                uint32_t length);

typedef struct {
    void *context;
    pota_stream_checkpoint_read_fn read;
    pota_stream_checkpoint_program_fn program;
    pota_stream_checkpoint_erase_fn erase;
    uint32_t base_offset;
    uint32_t slot_count;
    uint32_t slot_size;
    uint32_t erase_size;
} pota_stream_checkpoint_config_t;

typedef struct {
    uint32_t session_id;
    uint32_t generation;
    uint32_t token;
    uint32_t object_id;
    uint32_t durable_offset;
    uint32_t total_size;
    uint32_t package_crc32;
    uint32_t image_crc32;
    uint32_t durable_crc32;
    uint32_t flags;
} pota_stream_checkpoint_t;

typedef struct {
    uint32_t interval_bytes;
    bool checkpoint_on_final;
} pota_stream_checkpoint_policy_t;

typedef struct {
    pota_stream_checkpoint_config_t config;
    uint32_t next_sequence;
    bool initialized;
} pota_stream_checkpoint_store_t;

typedef enum {
    POTA_STREAM_CHECKPOINT_OK = 0,
    POTA_STREAM_CHECKPOINT_BAD_ARGUMENT,
    POTA_STREAM_CHECKPOINT_NO_VALID,
    POTA_STREAM_CHECKPOINT_CONFLICT,
    POTA_STREAM_CHECKPOINT_FULL,
    POTA_STREAM_CHECKPOINT_IO,
    POTA_STREAM_CHECKPOINT_VERIFY,
} pota_stream_checkpoint_result_t;

pota_stream_checkpoint_result_t pota_stream_checkpoint_init(
    pota_stream_checkpoint_store_t *store,
    const pota_stream_checkpoint_config_t *config);
pota_stream_checkpoint_result_t pota_stream_checkpoint_append(
    pota_stream_checkpoint_store_t *store,
    const pota_stream_checkpoint_t *checkpoint);
pota_stream_checkpoint_result_t pota_stream_checkpoint_recover_latest(
    const pota_stream_checkpoint_store_t *store,
    pota_stream_checkpoint_t *checkpoint,
    uint32_t *sequence);
bool pota_stream_checkpoint_matches(const pota_stream_checkpoint_t *checkpoint,
                                    uint32_t session_id,
                                    uint32_t generation,
                                    uint32_t token,
                                    uint32_t object_id,
                                    uint32_t total_size,
                                    uint32_t package_crc32);
bool pota_stream_checkpoint_policy_valid(
    const pota_stream_checkpoint_policy_t *policy);
bool pota_stream_checkpoint_should_append(
    const pota_stream_checkpoint_policy_t *policy,
    uint32_t last_checkpoint_offset,
    uint32_t durable_offset,
    uint32_t total_size);

#endif
