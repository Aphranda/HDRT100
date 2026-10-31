#ifndef OTA_TDMA_SESSION_H
#define OTA_TDMA_SESSION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota_tdma_codec.h"
#include "ota_tdma_ingress.h"
#include "ota_tdma_pump.h"

/* Role-aware TDMA OTA session (OTA-TDMA-006a integration seam).
 *
 * The session owns the frame routing only: the source emits OPEN + DATA via
 * the pump, the target feeds DATA into the ingress and answers with durable
 * ACKs.  Frames are handed to a caller-provided transport (maintenance stream
 * submit on firmware, a queue in host tests); the session never blocks and
 * never calls the transport recursively. */

typedef bool (*ota_tdma_transport_send_fn)(void *context, const uint8_t *frame,
                                           size_t length);

typedef struct {
    ota_tdma_transport_send_fn send;
    void *context;
} ota_tdma_transport_t;

typedef enum {
    OTA_TDMA_ROLE_SOURCE = 0,
    OTA_TDMA_ROLE_TARGET,
} ota_tdma_role_t;

typedef enum {
    OTA_TDMA_SESSION_IDLE = 0,
    OTA_TDMA_SESSION_RUNNING,
    OTA_TDMA_SESSION_DONE,
    OTA_TDMA_SESSION_FAILED,
} ota_tdma_session_state_t;

typedef enum {
    OTA_TDMA_SESSION_FAIL_NONE = 0,
    OTA_TDMA_SESSION_FAIL_TRANSPORT,
    OTA_TDMA_SESSION_FAIL_PROTOCOL,
    OTA_TDMA_SESSION_FAIL_REJECTED,
} ota_tdma_session_failure_t;

typedef struct {
    ota_tdma_role_t role;
    ota_tdma_transport_t transport;
    ota_tdma_pump_t pump;
    ota_tdma_ingress_t ingress;
    ota_tdma_open_reassembly_t reassembly;
    uint8_t local_node;
    bool node_checked;
    uint32_t ignored_opens;
    const uint8_t *open_wire;
    bool target_open;
    ota_tdma_session_state_t state;
    ota_tdma_session_failure_t failure;
} ota_tdma_session_t;

bool ota_tdma_session_init_source(ota_tdma_session_t *session,
                                  const ota_tdma_transport_t *transport,
                                  const uint8_t *open_wire,
                                  const uint8_t *image, uint32_t image_size,
                                  uint32_t image_crc32, uint32_t chunk_size,
                                  uint8_t max_attempts, uint32_t timeout_ms,
                                  ota_tdma_pump_crc_fn crc32);

bool ota_tdma_session_init_target(ota_tdma_session_t *session,
                                  const ota_tdma_transport_t *transport);

/* Relay-aware target: ignores an OPEN whose relay_node addresses another
 * node, so a broadcast maintenance frame cannot upgrade bystanders. */
bool ota_tdma_session_init_target_for_node(ota_tdma_session_t *session,
                                           const ota_tdma_transport_t *transport,
                                           uint8_t local_node);

/* Source: emits OPEN fragments and the first DATA frame. */
bool ota_tdma_session_start(ota_tdma_session_t *session, uint32_t now_ms);

/* Re-sends only the OPEN fragments (no pump side effects).  A lossy
 * maintenance window can drop OPEN before the first DATA, which makes
 * the target fail the session with PROTOCOL; the relay retries until
 * the first ACK arrives. */
bool ota_tdma_session_resend_open(ota_tdma_session_t *session);

/* Last OPEN reassembly step: seq, payload_len, byte offset, frame type. */
void ota_tdma_session_step_diag(uint32_t out[4]);

/* Decoded OPEN snapshot: map_version, destination_slot, total_size, caps. */
void ota_tdma_session_open_diag(uint32_t out[4]);

/* Dispatches one received wire frame. */
bool ota_tdma_session_on_frame(ota_tdma_session_t *session, uint32_t now_ms,
                               const uint8_t *wire, size_t length);

/* Source: sends the next chunk once the relay window has data again. */
bool ota_tdma_session_resume(ota_tdma_session_t *session, uint32_t now_ms);

/* Source: retransmits on ACK timeout. */
bool ota_tdma_session_tick(ota_tdma_session_t *session, uint32_t now_ms);

ota_tdma_session_state_t ota_tdma_session_state(const ota_tdma_session_t *session);
ota_tdma_session_failure_t ota_tdma_session_failure(
    const ota_tdma_session_t *session);

/* Counts OPEN frames dropped because they addressed another ring node. */
uint32_t ota_tdma_session_ignored_opens(const ota_tdma_session_t *session);

#endif
