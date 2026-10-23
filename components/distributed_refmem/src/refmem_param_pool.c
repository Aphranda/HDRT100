#include "refmem_param_pool.h"

#include <string.h>

typedef struct {
    uint32_t magic;
    uint32_t version;
    uint32_t state;
    uint32_t slot_seq;
    uint32_t command_seq;
    uint32_t epoch;
    uint32_t payload_size;
    uint32_t payload_crc32;
} refmem_param_header_t;

_Static_assert(sizeof(refmem_param_header_t) == REFMEM_PARAM_POOL_HEADER_SIZE,
               "param header must match the reserved header size");

static uint8_t *slot_base(uint8_t *region, uint32_t slot)
{
    return region + (size_t)slot * REFMEM_PARAM_POOL_SLOT_SIZE;
}

static const uint8_t *slot_base_const(const uint8_t *region, uint32_t slot)
{
    return region + (size_t)slot * REFMEM_PARAM_POOL_SLOT_SIZE;
}

uint32_t refmem_param_pool_crc(const void *payload, uint32_t payload_size)
{
    uint32_t crc = 2166136261u;
    const uint8_t *bytes = (const uint8_t *)payload;
    for (uint32_t i = 0u; i < payload_size; i++) {
        crc ^= bytes[i];
        crc *= 16777619u;
    }
    return crc;
}

void refmem_param_pool_init(uint8_t *region, size_t region_size)
{
    if (region == NULL ||
        region_size < (size_t)REFMEM_PARAM_POOL_SLOT_COUNT *
                          REFMEM_PARAM_POOL_SLOT_SIZE) {
        return;
    }
    memset(region, 0, (size_t)REFMEM_PARAM_POOL_SLOT_COUNT *
                          REFMEM_PARAM_POOL_SLOT_SIZE);
    for (uint32_t slot = 0u; slot < REFMEM_PARAM_POOL_SLOT_COUNT; slot++) {
        refmem_param_header_t *header =
            (refmem_param_header_t *)slot_base(region, slot);
        header->magic = REFMEM_PARAM_POOL_MAGIC;
        header->version = REFMEM_PARAM_POOL_VERSION;
        header->state = REFMEM_PARAM_SLOT_FREE;
    }
}

int32_t refmem_param_pool_alloc(uint8_t *region, size_t region_size)
{
    if (region == NULL ||
        region_size < (size_t)REFMEM_PARAM_POOL_SLOT_COUNT *
                          REFMEM_PARAM_POOL_SLOT_SIZE) {
        return -1;
    }
    for (uint32_t slot = 0u; slot < REFMEM_PARAM_POOL_SLOT_COUNT; slot++) {
        refmem_param_header_t *header =
            (refmem_param_header_t *)slot_base(region, slot);
        if (header->magic != REFMEM_PARAM_POOL_MAGIC ||
            header->version != REFMEM_PARAM_POOL_VERSION) {
            header->magic = REFMEM_PARAM_POOL_MAGIC;
            header->version = REFMEM_PARAM_POOL_VERSION;
            header->state = REFMEM_PARAM_SLOT_FREE;
        }
        if (header->state == REFMEM_PARAM_SLOT_FREE ||
            header->state == REFMEM_PARAM_SLOT_STALE ||
            header->state == REFMEM_PARAM_SLOT_DONE) {
            header->state = REFMEM_PARAM_SLOT_ALLOCATED;
            return (int32_t)slot;
        }
    }
    return -1;
}

bool refmem_param_pool_write(uint8_t *region,
                             size_t region_size,
                             uint32_t slot,
                             uint32_t command_seq,
                             uint32_t epoch,
                             const void *payload,
                             uint32_t payload_size)
{
    if (region == NULL || payload == NULL || payload_size == 0u ||
        payload_size > REFMEM_PARAM_POOL_PAYLOAD_MAX ||
        slot >= REFMEM_PARAM_POOL_SLOT_COUNT ||
        region_size < (size_t)REFMEM_PARAM_POOL_SLOT_COUNT *
                          REFMEM_PARAM_POOL_SLOT_SIZE) {
        return false;
    }
    refmem_param_header_t *header =
        (refmem_param_header_t *)slot_base(region, slot);
    header->magic = REFMEM_PARAM_POOL_MAGIC;
    header->version = REFMEM_PARAM_POOL_VERSION;
    header->state = REFMEM_PARAM_SLOT_VALID;
    header->slot_seq++;
    header->command_seq = command_seq;
    header->epoch = epoch;
    header->payload_size = payload_size;
    header->payload_crc32 = refmem_param_pool_crc(payload, payload_size);
    memcpy(slot_base(region, slot) + REFMEM_PARAM_POOL_HEADER_SIZE, payload,
           payload_size);
    return true;
}

bool refmem_param_pool_read(const uint8_t *region,
                            size_t region_size,
                            uint32_t slot,
                            uint32_t command_seq,
                            void *out,
                            uint32_t capacity,
                            uint32_t *out_size,
                            uint32_t *out_crc)
{
    if (region == NULL || out == NULL || slot >= REFMEM_PARAM_POOL_SLOT_COUNT ||
        region_size < (size_t)REFMEM_PARAM_POOL_SLOT_COUNT *
                          REFMEM_PARAM_POOL_SLOT_SIZE) {
        return false;
    }
    const refmem_param_header_t *header =
        (const refmem_param_header_t *)slot_base_const(region, slot);
    if (header->magic != REFMEM_PARAM_POOL_MAGIC ||
        header->version != REFMEM_PARAM_POOL_VERSION ||
        (header->state != REFMEM_PARAM_SLOT_VALID &&
         header->state != REFMEM_PARAM_SLOT_IN_FLIGHT &&
         header->state != REFMEM_PARAM_SLOT_DONE) ||
        header->command_seq != command_seq ||
        header->payload_size == 0u ||
        header->payload_size > REFMEM_PARAM_POOL_PAYLOAD_MAX ||
        header->payload_size > capacity) {
        return false;
    }
    const uint8_t *payload =
        slot_base_const(region, slot) + REFMEM_PARAM_POOL_HEADER_SIZE;
    if (refmem_param_pool_crc(payload, header->payload_size) !=
        header->payload_crc32) {
        return false;
    }
    memcpy(out, payload, header->payload_size);
    if (out_size != NULL) {
        *out_size = header->payload_size;
    }
    if (out_crc != NULL) {
        *out_crc = header->payload_crc32;
    }
    return true;
}

bool refmem_param_pool_release(uint8_t *region, size_t region_size,
                               uint32_t slot)
{
    if (region == NULL || slot >= REFMEM_PARAM_POOL_SLOT_COUNT ||
        region_size < (size_t)REFMEM_PARAM_POOL_SLOT_COUNT *
                          REFMEM_PARAM_POOL_SLOT_SIZE) {
        return false;
    }
    refmem_param_header_t *header =
        (refmem_param_header_t *)slot_base(region, slot);
    header->state = REFMEM_PARAM_SLOT_FREE;
    return true;
}
