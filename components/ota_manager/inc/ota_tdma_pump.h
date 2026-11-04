#ifndef OTA_TDMA_PUMP_H
#define OTA_TDMA_PUMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota_tdma_codec.h"

/* Source-side stop-and-wait pump (OTA-TDMA-006a).
 *
 * One DATA frame is in flight at a time.  The next frame is only sent after
 * the target acknowledges the durable offset that this frame covered, so a
 * flash-lockout pause can never be mistaken for a consumed block.  Retries
 * reuse the same offset; exhausting them fails closed. */

typedef enum {
    OTA_TDMA_PUMP_IDLE = 0,
    OTA_TDMA_PUMP_SENDING_BLOCK,
    OTA_TDMA_PUMP_WAIT_ACK,
    OTA_TDMA_PUMP_WAIT_CLOSE_ACK,
    OTA_TDMA_PUMP_DONE,
    OTA_TDMA_PUMP_FAILED,
} ota_tdma_pump_state_t;

typedef enum {
    OTA_TDMA_PUMP_FAIL_NONE = 0,
    OTA_TDMA_PUMP_FAIL_RETRIES,
    OTA_TDMA_PUMP_FAIL_REJECTED,
    OTA_TDMA_PUMP_FAIL_PROTOCOL,
    OTA_TDMA_PUMP_FAIL_SOURCE,
} ota_tdma_pump_failure_t;

typedef bool (*ota_tdma_pump_send_fn)(void *context, const uint8_t *frame,
                                      size_t length);
typedef uint32_t (*ota_tdma_pump_crc_fn)(const uint8_t *data, uint32_t length);
/* Streaming source: copies `length` bytes at `offset` into `destination`.
 * Returning false means "not available yet"; the pump then waits. */
typedef bool (*ota_tdma_pump_read_fn)(void *context, uint32_t offset,
                                      uint8_t *destination, uint32_t length);

typedef struct {
    const uint8_t *image;
    ota_tdma_pump_read_fn read;
    void *read_context;
    uint32_t image_size;
    uint32_t image_crc32;
    uint32_t chunk_size;
    uint32_t block_size;
    uint32_t next_offset;
    uint32_t durable_offset;
    uint32_t sent_offset;
    uint32_t sent_length;
    /* First chunk of the in-flight block that has not been submitted yet.  A
     * maintenance window can admit only part of a block per opportunity, so a
     * retry must continue here instead of replaying the block head forever. */
    uint32_t send_cursor;
    /* Diagnostics: offset of the last frame the transport accepted. */
    uint32_t last_sent_offset;
    uint32_t send_count;
    uint32_t timeout_replays;
    uint32_t timeout_ms;
    uint32_t deadline_ms;
    ota_tdma_pump_crc_fn crc32;
    uint8_t attempts;
    uint8_t max_attempts;
    ota_tdma_pump_state_t state;
    ota_tdma_pump_failure_t failure;
} ota_tdma_pump_t;

/* chunk_size must be 1..OTA_TDMA_DATA_MAX_PAYLOAD; max_attempts >= 1. */
/* Streaming variant: the image bytes come from `read` instead of a resident
 * buffer, so a relay can pump a window while the sender is still feeding it. */
bool ota_tdma_pump_init_streaming(ota_tdma_pump_t *pump, uint32_t image_size,
                                  uint32_t image_crc32, uint32_t chunk_size,
                                  uint32_t block_size, uint8_t max_attempts,
                                  uint32_t timeout_ms,
                                  ota_tdma_pump_crc_fn crc32,
                                  ota_tdma_pump_read_fn read,
                                  void *read_context);

/* block_size is the ACK granularity: 0 means one chunk per block (host tests),
 * while the TDMA carrier uses 512 bytes so the receiver can program a whole
 * flash page/header per durable ACK. */
bool ota_tdma_pump_init(ota_tdma_pump_t *pump, const uint8_t *image,
                        uint32_t image_size, uint32_t image_crc32,
                        uint32_t chunk_size, uint32_t block_size,
                        uint8_t max_attempts, uint32_t timeout_ms,
                        ota_tdma_pump_crc_fn crc32);

/* Sends the first DATA frame. */
bool ota_tdma_pump_start(ota_tdma_pump_t *pump, uint32_t now_ms,
                         ota_tdma_pump_send_fn send, void *context);

/* Sends the next chunk when the source has data again (nothing in flight). */
bool ota_tdma_pump_resume(ota_tdma_pump_t *pump, uint32_t now_ms,
                          ota_tdma_pump_send_fn send, void *context);

/* Resends the in-flight frame when the ACK deadline expired. */
bool ota_tdma_pump_tick(ota_tdma_pump_t *pump, uint32_t now_ms,
                        ota_tdma_pump_send_fn send, void *context);

/* Consumes one ACK frame; stale ACKs are ignored, protocol violations fail. */
bool ota_tdma_pump_on_ack(ota_tdma_pump_t *pump, uint32_t now_ms,
                          const ota_tdma_frame_t *ack,
                          ota_tdma_pump_send_fn send, void *context);

ota_tdma_pump_state_t ota_tdma_pump_state(const ota_tdma_pump_t *pump);
ota_tdma_pump_failure_t ota_tdma_pump_failure(const ota_tdma_pump_t *pump);
uint32_t ota_tdma_pump_durable(const ota_tdma_pump_t *pump);

#endif
