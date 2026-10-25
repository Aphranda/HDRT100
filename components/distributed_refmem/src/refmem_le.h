#ifndef REFMEM_LE_H
#define REFMEM_LE_H

#include <stdint.h>

/* Little-endian wire accessors shared by the RefMem translation units.
 *
 * These were file-static helpers of distributed_refmem.c. They sit on the
 * TDMA/FREEZE hot path, so they are `static inline` in a header instead of
 * becoming externally linked functions: each user keeps the same inline code
 * generation it had inside the single translation unit, and no extra call is
 * introduced on a Core1 path. */

static inline void distributed_refmem_put_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)(value >> 8u);
}

static inline void distributed_refmem_put_i16(uint8_t *dst, int16_t value)
{
    distributed_refmem_put_le16(dst, (uint16_t)value);
}

static inline void distributed_refmem_put_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8u) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16u) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static inline uint16_t distributed_refmem_get_le16(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0]) | ((uint16_t)src[1] << 8u));
}

static inline int16_t distributed_refmem_get_i16(const uint8_t *src)
{
    return (int16_t)distributed_refmem_get_le16(src);
}

static inline uint32_t distributed_refmem_get_le32(const uint8_t *src)
{
    return ((uint32_t)src[0]) |
           ((uint32_t)src[1] << 8u) |
           ((uint32_t)src[2] << 16u) |
           ((uint32_t)src[3] << 24u);
}

#endif /* REFMEM_LE_H */
