#ifndef OTA_TDMA_CARRIER_H
#define OTA_TDMA_CARRIER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota_tdma_session.h"

/* Maintenance-stream carrier for the TDMA OTA session (OTA-TDMA-006a).
 *
 * The carrier binds the session to the TDMA maintenance transport without
 * pulling TDMA headers into the OTA domain: the firmware passes the real
 * tdma_maintenance_runtime functions, host tests pass stubs.  Every submit
 * carries a fresh stream generation so the runtime's frame ACK matches the
 * frame that actually completed. */

typedef bool (*ota_tdma_carrier_submit_fn)(const uint8_t *frame,
                                           size_t frame_size,
                                           uint8_t stream_id,
                                           uint8_t generation,
                                           uint8_t target_mask);
typedef bool (*ota_tdma_carrier_take_fn)(uint8_t *frame, size_t frame_capacity,
                                         size_t *frame_size, uint8_t *stream_id,
                                         uint8_t *generation);
typedef bool (*ota_tdma_carrier_ack_fn)(uint8_t stream_id, uint8_t generation,
                                        uint8_t source_slot, bool ok);

typedef struct {
    ota_tdma_carrier_submit_fn submit;
    ota_tdma_carrier_take_fn take;
    ota_tdma_carrier_ack_fn ack;
    uint8_t stream_id;
    uint8_t target_mask;
    /* v1 simplification: the maintenance runtime does not expose the source
     * slot of a completed frame, so the two-board carrier records the single
     * peer slot.  Multi-peer fan-out needs the runtime API to return it. */
    uint8_t peer_slot;
    uint8_t generation;
} ota_tdma_carrier_t;

bool ota_tdma_carrier_init(ota_tdma_carrier_t *carrier,
                           ota_tdma_carrier_submit_fn submit,
                           ota_tdma_carrier_take_fn take,
                           ota_tdma_carrier_ack_fn ack,
                           uint8_t stream_id, uint8_t target_mask,
                           uint8_t peer_slot);

/* Transport adapter handed to the session. */
bool ota_tdma_carrier_send(void *context, const uint8_t *frame, size_t length);

/* Drains one received maintenance frame (if any), dispatches it to the session
 * and answers the frame-level ACK that released it. */
bool ota_tdma_carrier_poll(ota_tdma_carrier_t *carrier,
                           ota_tdma_session_t *session, uint32_t now_ms);

/* AO-task entry: drains up to `max_frames` received frames and advances the
 * source retry timer.  Never blocks and never touches flash. */
bool ota_tdma_carrier_service(ota_tdma_carrier_t *carrier,
                              ota_tdma_session_t *session, uint32_t now_ms,
                              uint8_t max_frames);

#endif
