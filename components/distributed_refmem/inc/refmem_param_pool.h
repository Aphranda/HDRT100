#ifndef REFMEM_PARAM_POOL_H
#define REFMEM_PARAM_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* REFMEM parameter pool: fixed-size slots inside the REFMEM TLV region for
 * command payloads that do not fit the 64 B inline block.  A command carries
 * payload_kind=REF and payload_ref=slot index; the target validates
 * command_seq/epoch/size/CRC before executing.  Storage-only and host
 * testable; the caller owns the backing region. */
#define REFMEM_PARAM_POOL_MAGIC 0x52415052u /* "RPAR" */
#define REFMEM_PARAM_POOL_VERSION 1u
#define REFMEM_PARAM_POOL_SLOT_COUNT 3u
#define REFMEM_PARAM_POOL_SLOT_SIZE 1024u
#define REFMEM_PARAM_POOL_HEADER_SIZE 32u
#define REFMEM_PARAM_POOL_PAYLOAD_MAX \
    (REFMEM_PARAM_POOL_SLOT_SIZE - REFMEM_PARAM_POOL_HEADER_SIZE)

typedef enum {
    REFMEM_PARAM_SLOT_FREE = 0u,
    REFMEM_PARAM_SLOT_VALID = 1u,
    REFMEM_PARAM_SLOT_IN_FLIGHT = 2u,
    REFMEM_PARAM_SLOT_DONE = 3u,
    REFMEM_PARAM_SLOT_STALE = 4u,
    REFMEM_PARAM_SLOT_ALLOCATED = 5u,
} refmem_param_slot_state_t;

void refmem_param_pool_init(uint8_t *region, size_t region_size);
int32_t refmem_param_pool_alloc(uint8_t *region, size_t region_size);
bool refmem_param_pool_write(uint8_t *region,
                             size_t region_size,
                             uint32_t slot,
                             uint32_t command_seq,
                             uint32_t epoch,
                             const void *payload,
                             uint32_t payload_size);
bool refmem_param_pool_read(const uint8_t *region,
                            size_t region_size,
                            uint32_t slot,
                            uint32_t command_seq,
                            void *out,
                            uint32_t capacity,
                            uint32_t *out_size,
                            uint32_t *out_crc);
bool refmem_param_pool_release(uint8_t *region, size_t region_size,
                               uint32_t slot);
uint32_t refmem_param_pool_crc(const void *payload, uint32_t payload_size);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_PARAM_POOL_H */
