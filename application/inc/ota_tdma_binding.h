#ifndef OTA_TDMA_BINDING_H
#define OTA_TDMA_BINDING_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_tdma_session.h"
#include "pota_stream_session.h"

/* Firmware glue between the TDMA maintenance runtime and the OTA TDMA carrier.
 *
 * Kept in the application layer so the OTA domain never includes TDMA headers:
 * this unit owns the runtime binding, the v1 two-node peer derivation and the
 * per-transfer target-session lifecycle. */

/* Maintenance stream id carrying TDMA OTA frames (registered as a
 * second-class parser type, not a new command slot). */
#define OTA_TDMA_MAINTENANCE_STREAM_ID 0x4Fu

/* Registers the OTA frame parser with the TDMA second-class dispatcher, so the
 * single RX owner (refmem command service) forwards OTA frames here instead of
 * a second consumer racing on take(). */
bool ota_tdma_binding_register_parser(void);

/* Entry used by the registered parser: dispatches one OTA frame to the target
 * session or the active relay.  Returns true when the frame was accepted. */
bool ota_tdma_binding_handle_maintenance_frame(const uint8_t *payload,
                                               size_t size);

bool ota_tdma_binding_init(void);
void ota_tdma_binding_service(uint32_t now_ms, uint8_t max_frames);
bool ota_tdma_binding_active(void);

/* Relay (source) role: the host streams the package over the existing USB
 * stream and the coordinator forwards it to the ring without writing its own
 * flash. */
bool ota_tdma_binding_relay_start(const pota_stream_open_t *open);
bool ota_tdma_binding_relay_feed(uint32_t offset, const uint8_t *data,
                                 uint32_t size);
void ota_tdma_binding_relay_abort(void);
/* Drops a finished relay record so its status no longer shadows local
 * sessions; ignored when a relay is still in flight. */
void ota_tdma_binding_relay_clear(void);
bool ota_tdma_binding_relay_active(void);
/* Sticky: the last relay ended in failure (target ABORT / retry exhaustion),
 * cleared by the next relay_start or relay_clear. */
bool ota_tdma_binding_relay_failed(void);
uint32_t ota_tdma_binding_relay_durable(void);
bool ota_tdma_binding_relay_done(void);

/* Packed wire[36..39] of the relay OPEN actually sent (for diagnosis). */
uint32_t ota_tdma_binding_relay_wire_code(void);

/* trace, wire[36..39], buffer crc%1e6, received, total, failure. */
void ota_tdma_binding_diag(uint32_t out[26]);

typedef struct {
    uint32_t relay_frames_rx;
    uint32_t relay_frames_tx;
    uint32_t relay_aborts;
    uint32_t ignored_opens;
} ota_tdma_binding_counters_t;

void ota_tdma_binding_get_counters(ota_tdma_binding_counters_t *counters);

/* Last relay/session failure reason (0 = OK), so host diagnostics do not have
 * to guess from a bare OK. */
uint32_t ota_tdma_binding_last_result(void);

#endif
