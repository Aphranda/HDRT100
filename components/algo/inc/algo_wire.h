#ifndef ALGO_WIRE_H
#define ALGO_WIRE_H

#include <stddef.h>
#include <stdint.h>

/*
 * Little-endian wire helpers.
 *
 * Every persisted frame and table in this repository is little-endian; the same
 * put/get bodies had been re-written in each codec. These are byte-wise on
 * purpose: they never depend on the host endianness and never read past the
 * requested width.
 */

static inline void algo_le16_store(void *dst, uint16_t value)
{
    uint8_t *bytes = (uint8_t *)dst;
    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)((value >> 8u) & 0xFFu);
}

static inline uint16_t algo_le16_load(const void *src)
{
    const uint8_t *bytes = (const uint8_t *)src;
    return (uint16_t)((uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8u));
}

static inline void algo_le32_store(void *dst, uint32_t value)
{
    uint8_t *bytes = (uint8_t *)dst;
    bytes[0] = (uint8_t)(value & 0xFFu);
    bytes[1] = (uint8_t)((value >> 8u) & 0xFFu);
    bytes[2] = (uint8_t)((value >> 16u) & 0xFFu);
    bytes[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static inline uint32_t algo_le32_load(const void *src)
{
    const uint8_t *bytes = (const uint8_t *)src;
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8u) |
           ((uint32_t)bytes[2] << 16u) | ((uint32_t)bytes[3] << 24u);
}

static inline void algo_le64_store(void *dst, uint64_t value)
{
    uint8_t *bytes = (uint8_t *)dst;
    for (uint32_t index = 0u; index < 8u; index++) {
        bytes[index] = (uint8_t)((value >> (index * 8u)) & 0xFFu);
    }
}

static inline uint64_t algo_le64_load(const void *src)
{
    const uint8_t *bytes = (const uint8_t *)src;
    uint64_t value = 0u;
    for (uint32_t index = 0u; index < 8u; index++) {
        value |= (uint64_t)bytes[index] << (index * 8u);
    }
    return value;
}

#endif /* ALGO_WIRE_H */
