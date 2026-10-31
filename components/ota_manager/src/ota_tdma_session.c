#include "ota_tdma_session.h"
#include "ota_tdma_ingress.h"
#include "portable_ota_port.h"

extern void ota_tdma_trace(uint32_t code) __attribute__((weak));

static void session_trace(uint32_t code)
{
    if (ota_tdma_trace != NULL) {
        ota_tdma_trace(code);
    }
}

#include <string.h>

#include "pota_stream_wire.h"

static void ota_tdma_session_fail(ota_tdma_session_t *session,
                                  ota_tdma_session_failure_t failure)
{
    session->state = OTA_TDMA_SESSION_FAILED;
    session->failure = failure;
}

static bool ota_tdma_session_send(void *context, const uint8_t *frame,
                                  size_t length)
{
    ota_tdma_session_t *session = context;
    if (session->transport.send == NULL ||
        !session->transport.send(session->transport.context, frame, length)) {
        return false;
    }
    return true;
}

bool ota_tdma_session_init_source(ota_tdma_session_t *session,
                                  const ota_tdma_transport_t *transport,
                                  const uint8_t *open_wire,
                                  const uint8_t *image, uint32_t image_size,
                                  uint32_t image_crc32, uint32_t chunk_size,
                                  uint8_t max_attempts, uint32_t timeout_ms,
                                  ota_tdma_pump_crc_fn crc32)
{
    if (session == NULL || transport == NULL || transport->send == NULL ||
        open_wire == NULL) {
        return false;
    }
    memset(session, 0, sizeof(*session));
    session->role = OTA_TDMA_ROLE_SOURCE;
    session->transport = *transport;
    session->open_wire = open_wire;
    session->state = OTA_TDMA_SESSION_IDLE;
    if (!ota_tdma_pump_init(&session->pump, image, image_size, image_crc32,
                            chunk_size, OTA_TDMA_INGRESS_STAGE, max_attempts,
                            timeout_ms, crc32)) {
        return false;
    }
    return true;
}

bool ota_tdma_session_init_target(ota_tdma_session_t *session,
                                  const ota_tdma_transport_t *transport)
{
    if (session == NULL || transport == NULL || transport->send == NULL) {
        return false;
    }
    memset(session, 0, sizeof(*session));
    session->role = OTA_TDMA_ROLE_TARGET;
    session->transport = *transport;
    session->state = OTA_TDMA_SESSION_IDLE;
    ota_tdma_ingress_init(&session->ingress);
    ota_tdma_open_reassembly_reset(&session->reassembly);
    return true;
}

bool ota_tdma_session_init_target_for_node(ota_tdma_session_t *session,
                                           const ota_tdma_transport_t *transport,
                                           uint8_t local_node)
{
    if (!ota_tdma_session_init_target(session, transport)) {
        return false;
    }
    session->local_node = local_node;
    session->node_checked = true;
    return true;
}

bool ota_tdma_session_resend_open(ota_tdma_session_t *session)
{
    if (session == NULL || session->role != OTA_TDMA_ROLE_SOURCE ||
        session->open_wire == NULL) {
        return false;
    }
    uint8_t frame[OTA_TDMA_FRAME_MAX];
    const uint8_t total =
        (uint8_t)((OTA_TDMA_OPEN_WIRE_SIZE + OTA_TDMA_OPEN_FRAGMENT_MAX - 1u) /
                  OTA_TDMA_OPEN_FRAGMENT_MAX);
    for (uint8_t repeat = 0u; repeat < 3u; ++repeat) {
        for (uint8_t seq = 0u; seq < total; ++seq) {
            const size_t len = ota_tdma_encode_open(frame, sizeof(frame),
                                                    session->open_wire,
                                                    OTA_TDMA_OPEN_WIRE_SIZE,
                                                    seq, total);
            if (len == 0u || !ota_tdma_session_send(session, frame, len)) {
                return false;
            }
        }
    }
    return true;
}

bool ota_tdma_session_start(ota_tdma_session_t *session, uint32_t now_ms)
{
    if (session == NULL || session->role != OTA_TDMA_ROLE_SOURCE ||
        session->state != OTA_TDMA_SESSION_IDLE || session->open_wire == NULL) {
        return false;
    }
    uint8_t frame[OTA_TDMA_FRAME_MAX];
    const uint8_t total =
        (uint8_t)((OTA_TDMA_OPEN_WIRE_SIZE + OTA_TDMA_OPEN_FRAGMENT_MAX - 1u) /
                  OTA_TDMA_OPEN_FRAGMENT_MAX);
    for (uint8_t seq = 0u; seq < total; ++seq) {
        const size_t len = ota_tdma_encode_open(frame, sizeof(frame),
                                                session->open_wire,
                                                OTA_TDMA_OPEN_WIRE_SIZE, seq,
                                                total);
        if (len == 0u || !ota_tdma_session_send(session, frame, len)) {
            ota_tdma_session_fail(session, OTA_TDMA_SESSION_FAIL_TRANSPORT);
            return false;
        }
    }
    session->state = OTA_TDMA_SESSION_RUNNING;
    /* Do not start the DATA pump here: DATA would flood the maintenance window
     * and starve the one-shot OPEN fragments.  relay_tick() starts the pump
     * after an OPEN-only grace period. */
    (void)now_ms;
    return true;
}

static uint32_t s_step_diag[4];
static uint32_t s_open_diag[4];

void ota_tdma_session_open_diag(uint32_t out[4])
{
    if (out != NULL) {
        for (uint32_t i = 0; i < 4u; ++i) {
            out[i] = s_open_diag[i];
        }
    }
}

void ota_tdma_session_step_diag(uint32_t out[4])
{
    if (out != NULL) {
        for (uint32_t i = 0; i < 4u; ++i) {
            out[i] = s_step_diag[i];
        }
    }
}

bool ota_tdma_session_target_frame(ota_tdma_session_t *session,
                                          uint32_t now_ms,
                                          const ota_tdma_frame_t *frame)
{
    (void)now_ms;
    switch (frame->type) {
    case (uint8_t)OTA_TDMA_FRAME_OPEN:
        if (session->target_open) {
            return true;
        }
        s_step_diag[0] = (uint32_t)frame->seq;
        s_step_diag[1] = (uint32_t)frame->payload_len;
        s_step_diag[2] = (uint32_t)frame->seq *
                         (uint32_t)OTA_TDMA_OPEN_FRAGMENT_MAX;
        s_step_diag[3] = (uint32_t)frame->type;
        if (ota_tdma_open_reassembly_step(&session->reassembly, frame)) {
            pota_stream_open_t open;
            session_trace(300u); /* decode start */
            {
                /* Retained CRC of the reassembled 88-byte buffer: the host test
                 * prints the same value for the expected wire, so a mismatch
                 * proves the target buffer is corrupted (vs a decode bug). */
                const uint32_t buffer_crc = portable_ota_port_crc32_compute(
                    session->reassembly.buffer, OTA_TDMA_OPEN_WIRE_SIZE);
                session_trace(830000u + buffer_crc % 100000u);
            }
            if (!pota_stream_open_decode_le(session->reassembly.buffer,
                                            session->reassembly.length,
                                            &open)) {
                ota_tdma_session_fail(session, OTA_TDMA_SESSION_FAIL_REJECTED);
                return false;
            }
            if (session->node_checked &&
                (open.capability_mask & POTA_STREAM_CAP_RELAY) != 0u &&
                open.relay_node != session->local_node) {
                ota_tdma_open_reassembly_reset(&session->reassembly);
                session->ignored_opens++;
                return true; /* addressed to another node */
            }
            s_open_diag[0] = open.map_version;
            s_open_diag[1] = open.destination_slot;
            s_open_diag[2] = open.total_size;
            s_open_diag[3] = open.capability_mask;
            session_trace(302u); /* decode ok, ingress open start */
            if (ota_tdma_ingress_open(&session->ingress, &open) !=
                OTA_TDMA_INGRESS_OK) {
                session_trace(303u); /* ingress open failed */
                ota_tdma_session_fail(session, OTA_TDMA_SESSION_FAIL_REJECTED);
                return false;
            }
            session_trace(304u); /* target stream opened */
            session->target_open = true;
            session->state = OTA_TDMA_SESSION_RUNNING;
        }
        return true;
    case (uint8_t)OTA_TDMA_FRAME_DATA: {
        if (!session->target_open) {
            /* Lossy maintenance: DATA can overtake OPEN.  Ignore it and keep
             * waiting for the OPEN fragments instead of killing the session;
             * the source retransmits OPEN and the pump retransmits DATA. */
            session->ignored_opens++;
            return true;
        }
        const ota_tdma_ingress_result_t result =
            ota_tdma_ingress_data(&session->ingress, frame);
        ota_tdma_ingress_sync_status(&session->ingress);
        if (result != OTA_TDMA_INGRESS_OK) {
            uint8_t ack[OTA_TDMA_FRAME_MAX];
            const size_t ack_len = ota_tdma_ingress_encode_ack(
                &session->ingress, 1u, (uint8_t)result, ack, sizeof(ack));
            if (ack_len != 0u) {
                (void)ota_tdma_session_send(session, ack, ack_len);
            }
            ota_tdma_session_fail(session, OTA_TDMA_SESSION_FAIL_REJECTED);
            return false;
        }
        if (ota_tdma_ingress_ack_ready(&session->ingress)) {
            uint8_t ack[OTA_TDMA_FRAME_MAX];
            const size_t ack_len = ota_tdma_ingress_encode_ack(
                &session->ingress, 0u, 0u, ack, sizeof(ack));
            if (ack_len == 0u || !ota_tdma_session_send(session, ack, ack_len)) {
                ota_tdma_session_fail(session, OTA_TDMA_SESSION_FAIL_TRANSPORT);
                return false;
            }
        }
        return true;
    }
    case (uint8_t)OTA_TDMA_FRAME_CLOSE: {
        if (!session->target_open ||
            ota_tdma_ingress_close(&session->ingress) != OTA_TDMA_INGRESS_OK) {
            ota_tdma_session_fail(session, OTA_TDMA_SESSION_FAIL_REJECTED);
            return false;
        }
        uint8_t ack[OTA_TDMA_FRAME_MAX];
        const size_t ack_len = ota_tdma_ingress_encode_ack(
            &session->ingress, 0u, 0u, ack, sizeof(ack));
        if (ack_len == 0u || !ota_tdma_session_send(session, ack, ack_len)) {
            ota_tdma_session_fail(session, OTA_TDMA_SESSION_FAIL_TRANSPORT);
            return false;
        }
        session->state = OTA_TDMA_SESSION_DONE;
        return true;
    }
    case (uint8_t)OTA_TDMA_FRAME_ABORT:
        ota_tdma_session_fail(session, OTA_TDMA_SESSION_FAIL_REJECTED);
        return false;
    default:
        ota_tdma_session_fail(session, OTA_TDMA_SESSION_FAIL_PROTOCOL);
        return false;
    }
}

static bool ota_tdma_session_source_frame(ota_tdma_session_t *session,
                                          uint32_t now_ms,
                                          const ota_tdma_frame_t *frame)
{
    if (frame->type == (uint8_t)OTA_TDMA_FRAME_ABORT) {
        ota_tdma_session_fail(session, OTA_TDMA_SESSION_FAIL_REJECTED);
        return false;
    }
    if (frame->type != (uint8_t)OTA_TDMA_FRAME_ACK) {
        return true; /* ignore non-ACK traffic on the source role */
    }
    if (!ota_tdma_pump_on_ack(&session->pump, now_ms, frame,
                              ota_tdma_session_send, session)) {
        if (session->pump.state == OTA_TDMA_PUMP_FAILED) {
            ota_tdma_session_fail(session,
                                  session->pump.failure ==
                                          OTA_TDMA_PUMP_FAIL_REJECTED
                                      ? OTA_TDMA_SESSION_FAIL_REJECTED
                                      : OTA_TDMA_SESSION_FAIL_PROTOCOL);
        }
        return false;
    }
    if (session->pump.state == OTA_TDMA_PUMP_DONE) {
        session->state = OTA_TDMA_SESSION_DONE;
    }
    return true;
}

bool ota_tdma_session_on_frame(ota_tdma_session_t *session, uint32_t now_ms,
                               const uint8_t *wire, size_t length)
{
    if (session == NULL || session->state == OTA_TDMA_SESSION_FAILED ||
        session->state == OTA_TDMA_SESSION_DONE) {
        return false;
    }
    ota_tdma_frame_t frame;
    if (!ota_tdma_decode(&frame, wire, length)) {
        ota_tdma_session_fail(session, OTA_TDMA_SESSION_FAIL_PROTOCOL);
        return false;
    }
    return session->role == OTA_TDMA_ROLE_SOURCE
               ? ota_tdma_session_source_frame(session, now_ms, &frame)
               : ota_tdma_session_target_frame(session, now_ms, &frame);
}

bool ota_tdma_session_resume(ota_tdma_session_t *session, uint32_t now_ms)
{
    if (session == NULL || session->role != OTA_TDMA_ROLE_SOURCE ||
        session->state != OTA_TDMA_SESSION_RUNNING) {
        return false;
    }
    if (session->pump.state == OTA_TDMA_PUMP_IDLE) {
        return ota_tdma_pump_start(&session->pump, now_ms,
                                   ota_tdma_session_send, session);
    }
    return ota_tdma_pump_resume(&session->pump, now_ms, ota_tdma_session_send,
                                session);
}

bool ota_tdma_session_tick(ota_tdma_session_t *session, uint32_t now_ms)
{
    if (session == NULL || session->role != OTA_TDMA_ROLE_SOURCE ||
        session->state != OTA_TDMA_SESSION_RUNNING) {
        return false;
    }
    if (session->pump.state != OTA_TDMA_PUMP_WAIT_ACK) {
        return false;
    }
    if (!ota_tdma_pump_tick(&session->pump, now_ms, ota_tdma_session_send,
                            session)) {
        if (session->pump.state == OTA_TDMA_PUMP_FAILED &&
            session->failure == OTA_TDMA_SESSION_FAIL_NONE) {
            ota_tdma_session_fail(session, OTA_TDMA_SESSION_FAIL_PROTOCOL);
        }
        return false;
    }
    return true;
}

ota_tdma_session_state_t ota_tdma_session_state(
    const ota_tdma_session_t *session)
{
    return session == NULL ? OTA_TDMA_SESSION_FAILED : session->state;
}

ota_tdma_session_failure_t ota_tdma_session_failure(
    const ota_tdma_session_t *session)
{
    return session == NULL ? OTA_TDMA_SESSION_FAIL_PROTOCOL : session->failure;
}

uint32_t ota_tdma_session_ignored_opens(const ota_tdma_session_t *session)
{
    return session == NULL ? 0u : session->ignored_opens;
}
