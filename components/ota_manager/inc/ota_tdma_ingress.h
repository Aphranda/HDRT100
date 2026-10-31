#ifndef OTA_TDMA_INGRESS_H
#define OTA_TDMA_INGRESS_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_tdma_codec.h"
#include "pota_stream_session.h"

/* Thin TDMA→OtaStreamSession adapter (OTA-TDMA-005).
 *
 * It owns no session state: offsets are forwarded to the port, durability is
 * read back from the port status.  The only local state is the sender-side
 * offset expectation so a lost/reordered DATA frame is rejected before it
 * reaches the session. */

typedef enum {
    OTA_TDMA_INGRESS_OK = 0,
    OTA_TDMA_INGRESS_NOT_OPEN,
    OTA_TDMA_INGRESS_BAD_FRAME,
    OTA_TDMA_INGRESS_OFFSET,
    OTA_TDMA_INGRESS_REJECTED,
} ota_tdma_ingress_result_t;

/* The stream session only accepts page-multiple writes (256 B pages) and a
 * 512-byte package header; a TDMA frame is at most 96 B, so the ingress stages
 * bytes and forwards whole blocks. */
#define OTA_TDMA_INGRESS_STAGE 512u

typedef struct {
    uint32_t expected_offset;
    uint32_t durable_offset;
    uint32_t last_offset;
    uint32_t last_length;
    uint32_t last_crc32;
    bool has_last;
    bool open;
    uint8_t stage[OTA_TDMA_INGRESS_STAGE];
    uint32_t stage_length;
    uint32_t stage_offset;
    uint32_t total_size;
} ota_tdma_ingress_t;

void ota_tdma_ingress_init(ota_tdma_ingress_t *ingress);

uint32_t ota_tdma_ingress_last_session_result(void);

/* Opens the session through the port with the TDMA ingress source. */
ota_tdma_ingress_result_t ota_tdma_ingress_open(ota_tdma_ingress_t *ingress,
                                                const pota_stream_open_t *open);

/* Forwards one decoded DATA frame; offset must match the expected offset. */
ota_tdma_ingress_result_t ota_tdma_ingress_data(ota_tdma_ingress_t *ingress,
                                                const ota_tdma_frame_t *frame);

ota_tdma_ingress_result_t ota_tdma_ingress_close(ota_tdma_ingress_t *ingress);

/* Mirrors the port's durable offset and drops the open flag when the session
 * reached a terminal state. */
void ota_tdma_ingress_sync_status(ota_tdma_ingress_t *ingress);

/* True once every forwarded byte is durable, i.e. an ACK may advance the
 * sender.  Stop-and-wait means this is the only ACK trigger. */
bool ota_tdma_ingress_ack_ready(const ota_tdma_ingress_t *ingress);

/* Encodes an ACK carrying the current durable offset and the caller's result.
 * Returns 0 when the frame cannot be built. */
size_t ota_tdma_ingress_encode_ack(const ota_tdma_ingress_t *ingress,
                                   uint8_t result, uint8_t reason,
                                   uint8_t *out, size_t capacity);

#endif
