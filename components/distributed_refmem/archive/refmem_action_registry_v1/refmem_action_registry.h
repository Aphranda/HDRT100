#ifndef REFMEM_ACTION_REGISTRY_H
#define REFMEM_ACTION_REGISTRY_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define REFMEM_ACTION_REGISTRY_MAX 16u
#define REFMEM_ACTION_QUEUE_DEPTH 8u

typedef enum {
    REFMEM_ACTION_OK = 0u,
    REFMEM_ACTION_BAD_ARGUMENT = 1u,
    REFMEM_ACTION_DUPLICATE = 2u,
    REFMEM_ACTION_TABLE_FULL = 3u,
    REFMEM_ACTION_NOT_FOUND = 4u,
    REFMEM_ACTION_UNHANDLED = 5u,
    REFMEM_ACTION_QUEUE_FULL = 6u,
    REFMEM_ACTION_REQUIRED_HANDLER_MISSING = 7u,
} refmem_action_result_t;

typedef enum {
    REFMEM_ACTION_SOURCE_EVENT = 0u,
    REFMEM_ACTION_SOURCE_COMMAND = 1u,
} refmem_action_source_t;

struct refmem_action_request;

typedef void (*refmem_action_handler_t)(void *context,
                                        const struct refmem_action_request *request);

typedef struct refmem_action_request {
    uint32_t action_id;
    uint32_t command_type;
    uint32_t command_seq;
    uint32_t target_mask;
    uint32_t required_mask;
    uint32_t payload_ref;
    uint32_t payload_size;
    uint32_t payload_crc32;
} refmem_action_request_t;

typedef struct {
    uint32_t action_id;
    uint32_t command_type;
    uint32_t owner_id;
    bool required;
    uint32_t version;
    uint32_t crc;
    refmem_action_handler_t handler;
} refmem_action_entry_t;

typedef struct {
    refmem_action_entry_t entries[REFMEM_ACTION_REGISTRY_MAX];
    uint32_t count;
    uint32_t registry_crc;
} refmem_action_registry_t;

typedef struct {
    refmem_action_request_t queue[REFMEM_ACTION_QUEUE_DEPTH];
    uint32_t head;
    uint32_t tail;
    uint32_t count;
    uint32_t dropped;
} refmem_action_dispatcher_t;

void refmem_action_registry_init(refmem_action_registry_t *registry);
refmem_action_result_t refmem_action_registry_register(
    refmem_action_registry_t *registry,
    const refmem_action_entry_t *entry);
bool refmem_action_registry_required_handlers_ok(
    const refmem_action_registry_t *registry);
uint32_t refmem_action_registry_crc(const refmem_action_registry_t *registry);

void refmem_action_dispatcher_init(refmem_action_dispatcher_t *dispatcher);
refmem_action_result_t refmem_action_dispatcher_post(
    refmem_action_dispatcher_t *dispatcher,
    const refmem_action_registry_t *registry,
    refmem_action_source_t source,
    uint32_t id,
    const refmem_action_request_t *request);
refmem_action_result_t refmem_action_dispatcher_pop(
    refmem_action_dispatcher_t *dispatcher,
    refmem_action_request_t *request);
uint32_t refmem_action_dispatcher_depth(
    const refmem_action_dispatcher_t *dispatcher);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_ACTION_REGISTRY_H */
