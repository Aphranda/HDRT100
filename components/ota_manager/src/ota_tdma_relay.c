#include "ota_tdma_relay.h"
#include "ota_tdma_ingress.h"

#include <string.h>

/* Window reader: serves pump chunks from the resident block. */
static bool ota_tdma_relay_read(void *context, uint32_t offset,
                                uint8_t *destination, uint32_t length)
{
    ota_tdma_relay_t *relay = context;
    if (relay == NULL || offset < relay->window_offset) {
        return false;
    }
    const uint32_t relative = offset - relay->window_offset;
    if (relative + length > relay->window_size) {
        return false; /* not fed yet: keep waiting */
    }
    memcpy(destination, &relay->window[relative], length);
    return true;
}

bool ota_tdma_relay_open(ota_tdma_relay_t *relay,
                         const ota_tdma_transport_t *transport,
                         const uint8_t *open_wire, uint32_t image_size,
                         uint32_t image_crc32, uint32_t chunk_size,
                         uint8_t max_attempts, uint32_t timeout_ms,
                         ota_tdma_pump_crc_fn crc32)
{
    if (relay == NULL || transport == NULL || transport->send == NULL ||
        open_wire == NULL || chunk_size == 0u ||
        (OTA_TDMA_RELAY_WINDOW % chunk_size) != 0u) {
        /* v1 keeps one resident block and pumps it in whole chunks, so the
         * window must be a multiple of the chunk size (4 KiB / 64 B). */
        return false;
    }
    memset(relay, 0, sizeof(*relay));
    relay->transport = *transport;
    /* Copy the descriptor: the session keeps the pointer and retransmits OPEN
     * from relay_tick(), long after the caller's frame (SCPI handler stack)
     * has been reused. */
    memcpy(relay->open_wire, open_wire, OTA_TDMA_OPEN_WIRE_SIZE);
    /* The session initialises its pump from a resident image; the relay then
     * replaces that pump with the streaming one below.  The window pointer is
     * a non-NULL placeholder and is not read before the replacement. */
    if (!ota_tdma_session_init_source(&relay->session, &relay->transport,
                                      relay->open_wire, relay->window, image_size,
                                      image_crc32, chunk_size, max_attempts,
                                      timeout_ms, crc32)) {
        return false;
    }
    if (!ota_tdma_pump_init_streaming(&relay->session.pump, image_size,
                                      image_crc32, chunk_size,
                                      OTA_TDMA_INGRESS_STAGE, max_attempts,
                                      timeout_ms, crc32, ota_tdma_relay_read,
                                      relay)) {
        return false;
    }
    relay->open = true;
    return true;
}

bool ota_tdma_relay_feed(ota_tdma_relay_t *relay, uint32_t offset,
                         const uint8_t *data, uint32_t size)
{
    if (relay == NULL || !relay->open || data == NULL || size == 0u ||
        offset != relay->fed_offset || relay->window_size != 0u ||
        size > OTA_TDMA_RELAY_WINDOW) {
        return false;
    }
    memcpy(relay->window, data, size);
    relay->window_offset = offset;
    relay->window_size = size;
    relay->fed_offset = offset + size;
    /* The transfer starts from relay_tick(now_ms): the pump arms its ACK
     * deadline from that timestamp, so starting here with 0 would make every
     * deadline look already expired. */
    return true;
}

bool ota_tdma_relay_on_frame(ota_tdma_relay_t *relay, uint32_t now_ms,
                             const uint8_t *wire, size_t length)
{
    if (relay == NULL || !relay->open || wire == NULL) {
        return false;
    }
    const bool ok =
        ota_tdma_session_on_frame(&relay->session, now_ms, wire, length);
    relay->durable_offset = ota_tdma_pump_durable(&relay->session.pump);
    const ota_tdma_session_state_t state =
        ota_tdma_session_state(&relay->session);
    if (state == OTA_TDMA_SESSION_DONE || state == OTA_TDMA_SESSION_FAILED) {
        relay->open = false;
        return ok;
    }
    if (relay->window_size != 0u &&
        relay->durable_offset >= relay->window_offset + relay->window_size) {
        relay->window_size = 0u;
        relay->window_offset = relay->durable_offset;
    }
    return ok;
}

void ota_tdma_relay_update(ota_tdma_relay_t *relay)
{
    if (relay == NULL) {
        return;
    }
    relay->durable_offset = ota_tdma_pump_durable(&relay->session.pump);
    const ota_tdma_session_state_t state =
        ota_tdma_session_state(&relay->session);
    if (state == OTA_TDMA_SESSION_DONE || state == OTA_TDMA_SESSION_FAILED) {
        relay->open = false;
        return;
    }
    if (relay->window_size != 0u &&
        relay->durable_offset >= relay->window_offset + relay->window_size) {
        relay->window_size = 0u;
        relay->window_offset = relay->durable_offset;
    }
}

bool ota_tdma_relay_tick(ota_tdma_relay_t *relay, uint32_t now_ms)
{
    if (relay == NULL || !relay->open) {
        return false;
    }
    if (relay->session.state == OTA_TDMA_SESSION_IDLE) {
        if (relay->window_size == 0u) {
            return false;
        }
        relay->open_sent_ms = now_ms;
        return ota_tdma_session_start(&relay->session, now_ms);
    }
    /* OPEN-only grace period: keep retransmitting the descriptor and hold the
     * DATA pump back so the window carries OPEN first. */
    if ((uint32_t)(now_ms - relay->open_sent_ms) < 600u) {
        if ((uint32_t)(now_ms - relay->open_resend_ms) >= 100u) {
            relay->open_resend_ms = now_ms;
            (void)ota_tdma_session_resend_open(&relay->session);
        }
        return true;
    }
    if (!ota_tdma_session_tick(&relay->session, now_ms)) {
        return ota_tdma_session_resume(&relay->session, now_ms);
    }
    /* No ACK yet: the target may have missed the one-shot OPEN fragments.
     * Re-send them every 200 ms until the pump records durable progress. */
    if (ota_tdma_pump_durable(&relay->session.pump) == 0u &&
        (uint32_t)(now_ms - relay->open_resend_ms) >= 200u) {
        relay->open_resend_ms = now_ms;
        (void)ota_tdma_session_resend_open(&relay->session);
    }
    return true;
}

void ota_tdma_relay_abort(ota_tdma_relay_t *relay)
{
    if (relay != NULL) {
        relay->open = false;
        relay->window_size = 0u;
        relay->session.state = OTA_TDMA_SESSION_FAILED;
        relay->session.failure = OTA_TDMA_SESSION_FAIL_REJECTED;
    }
}

bool ota_tdma_relay_active(const ota_tdma_relay_t *relay)
{
    return relay != NULL && relay->open;
}

uint32_t ota_tdma_relay_durable(const ota_tdma_relay_t *relay)
{
    return relay == NULL ? 0u : relay->durable_offset;
}

bool ota_tdma_relay_done(const ota_tdma_relay_t *relay)
{
    return relay != NULL &&
           ota_tdma_session_state(&relay->session) == OTA_TDMA_SESSION_DONE;
}
