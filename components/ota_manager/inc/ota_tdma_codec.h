#ifndef OTA_TDMA_CODEC_H
#define OTA_TDMA_CODEC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* TDMA OTA frame codec (OTA-TDMA-004).
 *
 * Frames ride the existing maintenance stream, whose logical message limit is
 * TDMA_MAINTENANCE_STREAM_MAX (80 B).  Every frame therefore stays within
 * OTA_TDMA_FRAME_MAX; only the OPEN descriptor (the fixed 88-byte
 * local-ingress wire format) is fragmented.
 */

#define OTA_TDMA_FRAME_MAX 80u
#define OTA_TDMA_HEADER_SIZE 4u
#define OTA_TDMA_OPEN_WIRE_SIZE 88u
#define OTA_TDMA_DATA_HEADER_SIZE 8u /* offset + crc32 */
#define OTA_TDMA_DATA_MAX_PAYLOAD \
    (OTA_TDMA_FRAME_MAX - OTA_TDMA_HEADER_SIZE - OTA_TDMA_DATA_HEADER_SIZE)
#define OTA_TDMA_OPEN_FRAGMENT_MAX \
    (OTA_TDMA_FRAME_MAX - OTA_TDMA_HEADER_SIZE)
#define OTA_TDMA_OPEN_MAX_FRAGMENTS 3u

typedef enum {
    OTA_TDMA_FRAME_OPEN = 1,
    OTA_TDMA_FRAME_DATA = 2,
    OTA_TDMA_FRAME_ACK = 3,
    OTA_TDMA_FRAME_CLOSE = 4,
    OTA_TDMA_FRAME_ABORT = 5,
} ota_tdma_frame_type_t;

#define OTA_TDMA_FLAG_FINAL 0x01u
#define OTA_TDMA_FLAG_HAS_CRC 0x02u

typedef struct {
    uint8_t type;
    uint8_t flags;
    uint8_t seq;
    uint8_t total;
    uint32_t offset;
    uint32_t crc32;
    uint8_t result;
    uint8_t reason;
    uint16_t payload_len;
    /* Large enough for an OPEN fragment (76 B), not just DATA (68 B): the
     * decoder writes the whole body here, so a DATA-sized array overflows on
     * every full OPEN fragment. */
    uint8_t payload[OTA_TDMA_OPEN_FRAGMENT_MAX];
} ota_tdma_frame_t;

/* Each encoder returns the encoded length, or 0 when the arguments or the
 * destination capacity are invalid (fail closed, never truncate). */
size_t ota_tdma_encode_open(uint8_t *out, size_t cap,
                            const uint8_t *open_wire, size_t open_wire_len,
                            uint8_t seq, uint8_t total);
size_t ota_tdma_encode_data(uint8_t *out, size_t cap, uint32_t offset,
                            uint32_t crc32, const uint8_t *payload,
                            size_t payload_len, bool is_final);
size_t ota_tdma_encode_ack(uint8_t *out, size_t cap, uint32_t durable_offset,
                           uint8_t result, uint8_t reason);
size_t ota_tdma_encode_close(uint8_t *out, size_t cap, uint32_t final_offset,
                             uint32_t crc32);
size_t ota_tdma_encode_abort(uint8_t *out, size_t cap, uint8_t reason);

/* Returns false for malformed input: unknown type, bad lengths, seq >= total,
 * or a body that exceeds the declared frame size. */
bool ota_tdma_decode(ota_tdma_frame_t *frame, const uint8_t *data, size_t len);

/* OPEN reassembly: the 88-byte descriptor arrives as `total` fragments with
 * seq 0..total-1.  Returns true once the descriptor is complete; the caller
 * reads descriptor/descriptor_len. */
typedef struct {
    uint8_t total;
    uint8_t received;
    uint16_t length;
    uint8_t buffer[OTA_TDMA_OPEN_WIRE_SIZE];
} ota_tdma_open_reassembly_t;

void ota_tdma_open_reassembly_reset(ota_tdma_open_reassembly_t *reassembly);
bool ota_tdma_open_reassembly_step(ota_tdma_open_reassembly_t *reassembly,
                                   const ota_tdma_frame_t *frame);

#endif
