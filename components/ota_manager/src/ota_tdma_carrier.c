#include "ota_tdma_carrier.h"

#include <string.h>

bool ota_tdma_carrier_init(ota_tdma_carrier_t *carrier,
                           ota_tdma_carrier_submit_fn submit,
                           ota_tdma_carrier_take_fn take,
                           ota_tdma_carrier_ack_fn ack,
                           uint8_t stream_id, uint8_t target_mask,
                           uint8_t peer_slot)
{
    if (carrier == NULL || submit == NULL || take == NULL || ack == NULL ||
        target_mask == 0u) {
        return false;
    }
    memset(carrier, 0, sizeof(*carrier));
    carrier->submit = submit;
    carrier->take = take;
    carrier->ack = ack;
    carrier->stream_id = stream_id;
    carrier->target_mask = target_mask;
    carrier->peer_slot = peer_slot;
    carrier->generation = 0u;
    return true;
}

bool ota_tdma_carrier_send(void *context, const uint8_t *frame, size_t length)
{
    ota_tdma_carrier_t *carrier = context;
    if (carrier == NULL || carrier->submit == NULL || frame == NULL ||
        length == 0u || length > OTA_TDMA_FRAME_MAX) {
        return false;
    }
    /* A fresh generation makes the runtime reject stale frame ACKs. */
    carrier->generation = (uint8_t)(carrier->generation + 1u);
    return carrier->submit(frame, length, carrier->stream_id,
                           carrier->generation, carrier->target_mask);
}

bool ota_tdma_carrier_poll(ota_tdma_carrier_t *carrier,
                           ota_tdma_session_t *session, uint32_t now_ms)
{
    if (carrier == NULL || carrier->take == NULL || session == NULL) {
        return false;
    }
    uint8_t frame[OTA_TDMA_FRAME_MAX];
    size_t frame_size = 0u;
    uint8_t stream_id = 0u;
    uint8_t generation = 0u;
    if (!carrier->take(frame, sizeof(frame), &frame_size, &stream_id,
                       &generation)) {
        return false;
    }
    const bool ok = stream_id == carrier->stream_id &&
                    ota_tdma_session_on_frame(session, now_ms, frame,
                                              frame_size);
    /* Frame-level ACK: release the sender's slot (or force its retry). */
    (void)carrier->ack(stream_id, generation, carrier->peer_slot, ok);
    return ok;
}

bool ota_tdma_carrier_service(ota_tdma_carrier_t *carrier,
                              ota_tdma_session_t *session, uint32_t now_ms,
                              uint8_t max_frames)
{
    if (carrier == NULL || session == NULL) {
        return false;
    }
    bool did_work = false;
    for (uint8_t index = 0u; index < max_frames; ++index) {
        if (!ota_tdma_carrier_poll(carrier, session, now_ms)) {
            break;
        }
        did_work = true;
    }
    if (ota_tdma_session_tick(session, now_ms)) {
        did_work = true;
    }
    return did_work;
}
