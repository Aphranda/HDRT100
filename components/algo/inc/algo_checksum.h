#ifndef ALGO_CHECKSUM_H
#define ALGO_CHECKSUM_H

#include <stddef.h>
#include <stdint.h>

/* Kernels called from SRAM-resident (`__not_in_flash_func`) wrappers must inline
 * even in -Og/-O0 builds, otherwise the wrapper would call into flash. */
#if defined(__GNUC__)
#define ALGO_ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#define ALGO_ALWAYS_INLINE inline
#endif

/*
 * Shared checksum primitives: one source definition per algorithm.
 *
 * Header-only on purpose. Firmware passes one include list to every unit, but
 * the host harnesses each assemble their own `-I` set; an out-of-line module
 * would force every harness that compiles a dependent unit to be edited and
 * linked. Keeping the kernels inline makes the shared source reachable without
 * touching those harnesses.
 *
 * Values produced here are persisted in Flash records, carried on the wire and
 * used as session/identity fingerprints. A migration must keep the exact byte
 * ingestion of each call site; changing a seed, a terminator or the input
 * granularity silently changes those identities.
 *
 * `data == NULL` is only defined for `size == 0`; callers that accept a null
 * buffer keep their own guard so their historical return value is preserved.
 */

/*
 * CRC-32/ISO-HDLC: reflected polynomial 0xEDB88320, init 0xFFFFFFFF,
 * final XOR 0xFFFFFFFF. `crc` uses the zlib convention: start a new stream with
 * 0 and feed the previous return value to continue. Bit-identical to zlib.crc32.
 *
 * The bit-serial kernel is the cold-path reference. The TDMA transport keeps
 * its own nibble-table kernel in SRAM because it sits in the DMA window; that
 * copy is a documented performance specialisation, not a second definition.
 */
static inline uint32_t algo_crc32_raw_update(uint32_t reg, const void *data,
                                             size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0u; i < size; i++) {
        reg ^= bytes[i];
        for (uint32_t bit = 0u; bit < 8u; bit++) {
            const uint32_t mask = 0u - (reg & 1u);
            reg = (reg >> 1u) ^ (UINT32_C(0xEDB88320) & mask);
        }
    }
    return reg;
}

/*
 * Raw reflected register step: no pre/post complement. The caller owns the
 * register seed and any final terminator.
 *
 *   algo_crc32_iso_hdlc_compute(d) == ~algo_crc32_raw_update(0xFFFFFFFFu, d)
 *
 * Some persisted frames store the raw register instead of the zlib value (they
 * currently start it at 0); keep that seed explicit at the call site.
 */
static inline uint32_t algo_crc32_iso_hdlc_update(uint32_t crc,
                                                  const void *data,
                                                  size_t size)
{
    return ~algo_crc32_raw_update(~crc, data, size);
}

static inline uint32_t algo_crc32_iso_hdlc_compute(const void *data,
                                                   size_t size)
{
    return algo_crc32_iso_hdlc_update(0u, data, size);
}

/*
 * CRC-16/CCITT-FALSE: polynomial 0x1021, init 0xFFFF, no reflection, no xorout.
 * The one-byte step is eight MSB-first shifts folded into one update; core1
 * hot paths (VDC priority codec, TDMA process image) call it directly.
 */
static ALGO_ALWAYS_INLINE uint16_t algo_crc16_ccitt_false_update_byte(
    uint16_t crc, uint8_t byte)
{
    uint32_t x = ((uint32_t)crc >> 8u) ^ byte;
    x ^= x >> 4u;
    return (uint16_t)(((uint32_t)crc << 8u) ^ (x << 12u) ^ (x << 5u) ^ x);
}

static inline uint16_t algo_crc16_ccitt_false_update(uint16_t crc,
                                                     const void *data,
                                                     size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0u; i < size; i++) {
        crc = algo_crc16_ccitt_false_update_byte(crc, bytes[i]);
    }
    return crc;
}

static inline uint16_t algo_crc16_ccitt_false_compute(const void *data,
                                                      size_t size)
{
    return algo_crc16_ccitt_false_update(0xFFFFu, data, size);
}

/*
 * FNV-1a 32: offset basis 2166136261, prime 16777619, one byte at a time.
 * The entry points differ only in input encoding:
 *   _update   continue a caller-owned hash over raw bytes;
 *   _bytes    fresh hash over a byte range;
 *   _str      NUL-terminated text, without the terminator;
 *   _u32_le   one 32-bit value as four little-endian bytes.
 */
#define ALGO_FNV1A32_OFFSET 2166136261u
#define ALGO_FNV1A32_PRIME 16777619u

static ALGO_ALWAYS_INLINE uint32_t algo_fnv1a32_update(uint32_t hash,
                                                       const void *data,
                                                       size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;
    for (size_t i = 0u; i < size; i++) {
        hash ^= bytes[i];
        hash *= ALGO_FNV1A32_PRIME;
    }
    return hash;
}

static inline uint32_t algo_fnv1a32_bytes(const void *data, size_t size)
{
    return algo_fnv1a32_update(ALGO_FNV1A32_OFFSET, data, size);
}

static inline uint32_t algo_fnv1a32_str(const char *text)
{
    uint32_t hash = ALGO_FNV1A32_OFFSET;
    if (text == NULL) {
        return hash;
    }
    while (*text != '\0') {
        hash ^= (uint8_t)(*text);
        hash *= ALGO_FNV1A32_PRIME;
        text++;
    }
    return hash;
}

static inline uint32_t algo_fnv1a32_u32_le(uint32_t hash, uint32_t value)
{
    for (uint32_t shift = 0u; shift < 32u; shift += 8u) {
        hash ^= (value >> shift) & 0xFFu;
        hash *= ALGO_FNV1A32_PRIME;
    }
    return hash;
}

/*
 * Word-at-a-time fold of the same family: one 32-bit symbol per step instead
 * of four bytes. NOT interchangeable with `_u32_le`; registries that hash word
 * streams use it so their published identity stays unchanged.
 */
static inline uint32_t algo_fnv1a32_word(uint32_t hash, uint32_t value)
{
    hash ^= value;
    hash *= ALGO_FNV1A32_PRIME;
    return hash;
}

#endif /* ALGO_CHECKSUM_H */
