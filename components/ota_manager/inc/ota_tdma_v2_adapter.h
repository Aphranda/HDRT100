#ifndef OTA_TDMA_V2_ADAPTER_H
#define OTA_TDMA_V2_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota_tdma_bridge.h"
#include "ota_tdma_codec.h"

/* Architecture v2 frame adapter.
 *
 * The TDMA maintenance stream only carries OTA data.  This adapter decodes
 * maintenance frames and drives the OTA-TDMA bridge, which forwards ordered
 * operations to the existing AB pipeline.  OPEN/CLOSE/ABORT remain the wire
 * control frames (REFMEM drives the same begin/end/abort intent eventually);
 * the ACK that stops the source pump is emitted only after the existing
 * pipeline has consumed a DATA block. */

typedef bool (*ota_tdma_v2_send_fn)(void *context, const uint8_t *frame,
                                    size_t length);
/* True when the existing AB pipeline accepts DATA (it is RECEIVING); DATA is
 * deferred while the pipeline still checks permissions or erases the slot. */
typedef bool (*ota_tdma_v2_ready_fn)(void *context);

typedef struct {
    ota_tdma_bridge_t bridge;
    ota_tdma_open_reassembly_t reassembly;
    ota_tdma_v2_send_fn send;
    void *send_context;
    ota_tdma_v2_ready_fn ready;
    void *ready_context;
    /* Diagnostics: the last decoded maintenance frame. */
    uint32_t last_type;
    uint32_t last_offset;
    uint32_t last_length;
    /* DATA frames staged while the pipeline was still checking/erasing. */
    uint32_t deferred_data;
} ota_tdma_v2_adapter_t;

bool ota_tdma_v2_adapter_init(ota_tdma_v2_adapter_t *adapter,
                              ota_tdma_bridge_sink_fn sink, void *sink_context,
                              ota_tdma_v2_send_fn send, void *send_context,
                              ota_tdma_v2_ready_fn ready,
                              void *ready_context);

/* Decodes and dispatches one maintenance frame.  Returns false only for a
 * malformed frame or a rejected control request; DATA backpressure (a block is
 * still in flight) returns true so the source keeps retransmitting. */
bool ota_tdma_v2_adapter_frame(ota_tdma_v2_adapter_t *adapter,
                               const uint8_t *frame, size_t length);

/* Called by the OTA AO once the block handed to it has been consumed: the
 * bridge accepts the next block and the pump finally sees its ACK. */
bool ota_tdma_v2_adapter_release(ota_tdma_v2_adapter_t *adapter);

/* Aborts the bridged transfer (control-plane abort) and releases the block. */
bool ota_tdma_v2_adapter_abort(ota_tdma_v2_adapter_t *adapter);

#endif
