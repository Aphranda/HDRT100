#ifndef TDMA_BOOT_PROFILE_H
#define TDMA_BOOT_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Durable boot profile for the single-port guide ring.
 *
 * The profile says how a board rebuilds the 10 kHz guide ring after power-up
 * without any cross-board command: profile level, node count, local slot,
 * the bring-up-no-calibration flag and the topology identity. It is stored in
 * the v2 CALIBRATION_NVS sub-region; the v1 compatibility map has no such
 * sub-region and must report "no profile".
 *
 * Wire/record layout (little endian, fixed size):
 *   [0]  version
 *   [4]  profile_level
 *   [8]  node_count
 *   [12] local_slot
 *   [16] flags
 *   [20] topology_crc32
 *   [24] generation
 *   [28] crc32 over bytes [0..27]
 */
#define TDMA_BOOT_PROFILE_VERSION 1u
#define TDMA_BOOT_PROFILE_PAYLOAD_SIZE 32u
#define TDMA_BOOT_PROFILE_CRC_OFFSET 28u
#define TDMA_BOOT_PROFILE_NODE_MIN 2u
#define TDMA_BOOT_PROFILE_NODE_MAX 8u

#define TDMA_BOOT_PROFILE_FLAG_BRINGUP_NO_CAL 0x00000001u
#define TDMA_BOOT_PROFILE_FLAG_MASK \
    (TDMA_BOOT_PROFILE_FLAG_BRINGUP_NO_CAL)

typedef struct {
    uint32_t version;
    uint32_t profile_level;
    uint32_t node_count;
    uint32_t local_slot;
    uint32_t flags;
    uint32_t topology_crc32;
    uint32_t generation;
} tdma_boot_profile_t;

/* True when the fields are internally consistent (version, level, node/slot
 * range, flag subset, nonzero generation and topology identity). */
bool tdma_boot_profile_valid(const tdma_boot_profile_t *profile);

/* Encode to exactly TDMA_BOOT_PROFILE_PAYLOAD_SIZE bytes, appending the CRC.
 * Returns false on invalid arguments or an inconsistent profile. */
bool tdma_boot_profile_encode(const tdma_boot_profile_t *profile,
                              uint8_t *payload,
                              size_t capacity);

/* Decode and validate a stored record; rejects a corrupt CRC. */
bool tdma_boot_profile_decode(const uint8_t *payload,
                              size_t size,
                              tdma_boot_profile_t *out);

/* CRC32 (IEEE 802.3) used for the record; exposed for store verification. */
uint32_t tdma_boot_profile_crc32(const uint8_t *data, size_t size);

#ifdef __cplusplus
}
#endif

#endif /* TDMA_BOOT_PROFILE_H */
