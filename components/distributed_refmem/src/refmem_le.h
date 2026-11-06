#ifndef REFMEM_LE_H
#define REFMEM_LE_H

#include <stdint.h>

#include "algo_wire.h"

/* Little-endian wire accessors shared by the RefMem translation units.
 *
 * These were file-static helpers of distributed_refmem.c. They sit on the
 * TDMA/FREEZE hot path, so they are `static inline` in a header instead of
 * becoming externally linked functions: each user keeps the same inline code
 * generation it had inside the single translation unit, and no extra call is
 * introduced on a Core1 path. The bodies delegate to `algo_wire.h`. */

static inline void distributed_refmem_put_le16(uint8_t *dst, uint16_t value)
{
    algo_le16_store(dst, value);
}

static inline void distributed_refmem_put_i16(uint8_t *dst, int16_t value)
{
    distributed_refmem_put_le16(dst, (uint16_t)value);
}

static inline void distributed_refmem_put_le32(uint8_t *dst, uint32_t value)
{
    algo_le32_store(dst, value);
}

static inline uint16_t distributed_refmem_get_le16(const uint8_t *src)
{
    return algo_le16_load(src);
}

static inline int16_t distributed_refmem_get_i16(const uint8_t *src)
{
    return (int16_t)distributed_refmem_get_le16(src);
}

static inline uint32_t distributed_refmem_get_le32(const uint8_t *src)
{
    return algo_le32_load(src);
}

#endif /* REFMEM_LE_H */
