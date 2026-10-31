#ifndef OTA_TDMA_RELAY_H
#define OTA_TDMA_RELAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota_tdma_session.h"

/* Source-side relay (OTA-TDMA-006b).
 *
 * The coordinator receives the package over the existing USB stream and
 * forwards it to the ring without touching its own flash.  v1 keeps one
 * receive block resident: the sender feeds a block, the relay pumps it with
 * stop-and-wait, and only after the whole block is durable may the next block
 * be fed.  That makes back-pressure explicit without a second flow-control
 * mechanism. */

#define OTA_TDMA_RELAY_WINDOW 2048u

typedef struct {
    ota_tdma_session_t session;
    ota_tdma_transport_t transport;
    /* The OPEN descriptor is retransmitted from relay_tick() (a different task
     * and stack than the caller that started the relay), so the relay must own
     * a copy instead of storing the caller's pointer. */
    uint8_t open_wire[OTA_TDMA_OPEN_WIRE_SIZE];
    uint8_t window[OTA_TDMA_RELAY_WINDOW];
    uint32_t window_offset;
    uint32_t window_size;
    uint32_t fed_offset;
    uint32_t durable_offset;
    uint32_t open_resend_ms;
    uint32_t open_sent_ms;
    bool open;
} ota_tdma_relay_t;

/* Starts a source session: sends the OPEN fragments and the first DATA frame
 * as soon as a window is fed. */
bool ota_tdma_relay_open(ota_tdma_relay_t *relay,
                         const ota_tdma_transport_t *transport,
                         const uint8_t *open_wire, uint32_t image_size,
                         uint32_t image_crc32, uint32_t chunk_size,
                         uint8_t max_attempts, uint32_t timeout_ms,
                         ota_tdma_pump_crc_fn crc32);

/* Feeds the next contiguous block; the previous block must be fully durable. */
bool ota_tdma_relay_feed(ota_tdma_relay_t *relay, uint32_t offset,
                         const uint8_t *data, uint32_t size);

/* Dispatches one frame from the ring (ACK/ABORT) and releases a durable
 * window so the sender can feed the next block. */
bool ota_tdma_relay_on_frame(ota_tdma_relay_t *relay, uint32_t now_ms,
                             const uint8_t *wire, size_t length);

/* Mirrors durable / releases a consumed window after the caller dispatched a
 * frame through ota_tdma_session_on_frame (carrier_poll path). */
void ota_tdma_relay_update(ota_tdma_relay_t *relay);

/* Source retry timer. */
bool ota_tdma_relay_tick(ota_tdma_relay_t *relay, uint32_t now_ms);

/* Abandons the transfer; the session is left for the next OPEN. */
void ota_tdma_relay_abort(ota_tdma_relay_t *relay);

bool ota_tdma_relay_active(const ota_tdma_relay_t *relay);
uint32_t ota_tdma_relay_durable(const ota_tdma_relay_t *relay);
bool ota_tdma_relay_done(const ota_tdma_relay_t *relay);

#endif
