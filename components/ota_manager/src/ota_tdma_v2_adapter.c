#include "ota_tdma_v2_adapter.h"

#include <string.h>

#include "pota_stream_wire.h"

static bool ota_tdma_v2_send_ack(ota_tdma_v2_adapter_t *adapter,
                                 uint32_t durable_offset, uint8_t result,
                                 uint8_t reason)
{
    if (adapter->send == NULL) {
        return false;
    }
    uint8_t frame[OTA_TDMA_FRAME_MAX];
    const size_t length = ota_tdma_encode_ack(frame, sizeof(frame),
                                              durable_offset, result, reason);
    return length != 0u && adapter->send(adapter->send_context, frame, length);
}

bool ota_tdma_v2_adapter_init(ota_tdma_v2_adapter_t *adapter,
                              ota_tdma_bridge_sink_fn sink, void *sink_context,
                              ota_tdma_v2_send_fn send, void *send_context,
                              ota_tdma_v2_ready_fn ready,
                              void *ready_context)
{
    if (adapter == NULL || sink == NULL || send == NULL) {
        return false;
    }
    memset(adapter, 0, sizeof(*adapter));
    adapter->send = send;
    adapter->send_context = send_context;
    adapter->ready = ready;
    adapter->ready_context = ready_context;
    ota_tdma_open_reassembly_reset(&adapter->reassembly);
    return ota_tdma_bridge_init(&adapter->bridge, sink, sink_context);
}

static bool ota_tdma_v2_adapter_open_done(ota_tdma_v2_adapter_t *adapter)
{
    pota_stream_open_t open;
    if (!pota_stream_open_decode_le(adapter->reassembly.buffer,
                                    adapter->reassembly.length, &open)) {
        return false;
    }
    if (!ota_tdma_bridge_begin(&adapter->bridge, open.total_size,
                               open.package_crc32, open.package_mode)) {
        return false;
    }
    /* Opening handshake: the existing AB pipeline has accepted the BEGIN, so
     * acknowledge offset 0 and let the source pump start sending blocks. */
    return ota_tdma_v2_send_ack(adapter, 0u, 0u, 0u);
}

bool ota_tdma_v2_adapter_frame(ota_tdma_v2_adapter_t *adapter,
                               const uint8_t *frame, size_t length)
{
    if (adapter == NULL || frame == NULL || length == 0u) {
        return false;
    }
    ota_tdma_frame_t decoded;
    if (!ota_tdma_decode(&decoded, frame, length)) {
        return false;
    }
    adapter->last_type = (uint32_t)decoded.type;
    adapter->last_offset = decoded.offset;
    adapter->last_length = (uint32_t)decoded.payload_len;
    switch (decoded.type) {
    case (uint8_t)OTA_TDMA_FRAME_OPEN:
        if (ota_tdma_bridge_state(&adapter->bridge) ==
            OTA_TDMA_BRIDGE_RECEIVING) {
            return true; /* retransmitted OPEN: keep the running transfer */
        }
        if (ota_tdma_open_reassembly_step(&adapter->reassembly, &decoded)) {
            return ota_tdma_v2_adapter_open_done(adapter);
        }
        return true;
    case (uint8_t)OTA_TDMA_FRAME_DATA: {
        if (ota_tdma_bridge_state(&adapter->bridge) !=
            OTA_TDMA_BRIDGE_RECEIVING) {
            return false;
        }
        /* Stage the frame even while the pipeline is still checking
         * permissions or erasing: the bridge holds one block and the AO only
         * consumes the block event once it reaches RECEIVING.  Dropping the
         * frame here made the first block impossible to complete whenever the
         * maintenance window delivered frames before the erase finished. */
        if (adapter->ready != NULL &&
            !adapter->ready(adapter->ready_context)) {
            adapter->deferred_data++;
        }
        if (!ota_tdma_bridge_data(&adapter->bridge, decoded.offset,
                                  decoded.payload, decoded.payload_len)) {
            /* Backpressure (the AO still owns the previous block) or a
             * retransmission of an already staged offset: ignore the frame and
             * let the source pump retransmit.  Only an explicit sink failure is
             * fatal. */
            return ota_tdma_bridge_state(&adapter->bridge) !=
                   OTA_TDMA_BRIDGE_FAILED;
        }
        return true;
    }
    case (uint8_t)OTA_TDMA_FRAME_CLOSE:
        if (ota_tdma_bridge_state(&adapter->bridge) !=
            OTA_TDMA_BRIDGE_RECEIVING) {
            return false;
        }
        if (!ota_tdma_bridge_end(&adapter->bridge)) {
            return false;
        }
        return ota_tdma_v2_send_ack(adapter, adapter->bridge.received_size, 0u,
                                    0u);
    case (uint8_t)OTA_TDMA_FRAME_ABORT:
        return ota_tdma_v2_adapter_abort(adapter);
    case (uint8_t)OTA_TDMA_FRAME_ACK:
        return true; /* the target does not consume ACKs */
    default:
        return false;
    }
}

bool ota_tdma_v2_adapter_release(ota_tdma_v2_adapter_t *adapter)
{
    if (adapter == NULL) {
        return false;
    }
    const uint32_t block_end = ota_tdma_bridge_release(&adapter->bridge);
    if (block_end == 0u) {
        return false;
    }
    return ota_tdma_v2_send_ack(adapter, block_end, 0u, 0u);
}

bool ota_tdma_v2_adapter_abort(ota_tdma_v2_adapter_t *adapter)
{
    if (adapter == NULL) {
        return false;
    }
    const bool aborted = ota_tdma_bridge_abort(&adapter->bridge);
    ota_tdma_open_reassembly_reset(&adapter->reassembly);
    return aborted;
}
