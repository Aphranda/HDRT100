#ifndef POTA_STREAM_INGRESS_H
#define POTA_STREAM_INGRESS_H

#include <stdbool.h>
#include <stdint.h>

#include "pota_stream_session.h"

/* All local transports use the same stream session.  The source is metadata
 * for admission/diagnostics only; it never changes ordering or durability
 * semantics in pota_stream_session. */
typedef enum {
    POTA_STREAM_INGRESS_USB_CDC = 0,
    POTA_STREAM_INGRESS_USBTMC,
    POTA_STREAM_INGRESS_SD,
    POTA_STREAM_INGRESS_UART,
    POTA_STREAM_INGRESS_RS485,
    POTA_STREAM_INGRESS_SOURCE_COUNT,
} pota_stream_ingress_source_t;

typedef enum {
    POTA_STREAM_INGRESS_OK = 0,
    POTA_STREAM_INGRESS_BAD_ARGUMENT,
    POTA_STREAM_INGRESS_SOURCE_REJECTED,
    POTA_STREAM_INGRESS_FRAME_TOO_LARGE,
    POTA_STREAM_INGRESS_CRC_MISMATCH,
    POTA_STREAM_INGRESS_SESSION,
} pota_stream_ingress_result_t;

typedef struct {
    pota_stream_session_t *session;
    uint32_t source_mask;
    uint32_t max_frame_size;
    pota_stream_ingress_source_t active_source;
    pota_stream_ingress_result_t last_result;
    bool open;
} pota_stream_ingress_t;

typedef struct {
    pota_stream_ingress_source_t source;
    pota_stream_state_t state;
    uint32_t durable_offset;
    uint32_t stream_token;
    pota_stream_ingress_result_t last_result;
} pota_stream_ingress_status_t;

bool pota_stream_ingress_init(pota_stream_ingress_t *ingress,
                              pota_stream_session_t *session,
                              uint32_t source_mask,
                              uint32_t max_frame_size);
pota_stream_ingress_result_t pota_stream_ingress_open(
    pota_stream_ingress_t *ingress,
    pota_stream_ingress_source_t source,
    const pota_stream_open_t *open);
pota_stream_ingress_result_t pota_stream_ingress_write(
    pota_stream_ingress_t *ingress,
    pota_stream_ingress_source_t source,
    uint32_t offset,
    const uint8_t *data,
    uint32_t size,
    bool has_crc32,
    uint32_t crc32);
pota_stream_ingress_result_t pota_stream_ingress_close(
    pota_stream_ingress_t *ingress,
    pota_stream_ingress_source_t source);
pota_stream_ingress_result_t pota_stream_ingress_abort(
    pota_stream_ingress_t *ingress,
    pota_stream_ingress_source_t source);
bool pota_stream_ingress_get_status(const pota_stream_ingress_t *ingress,
                                    pota_stream_ingress_status_t *status);

#define POTA_STREAM_INGRESS_SOURCE_BIT(source) (1u << (uint32_t)(source))

#endif
