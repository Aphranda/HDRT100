#include "refmem_bootstrap_relay.h"

#include <string.h>

#include "refmem_bootstrap_transport.h"

static refmem_bootstrap_relay_config_t s_config;
static bool s_initialized;
static refmem_bootstrap_relay_state_t s_state;
static bool s_ready;
static bool s_rx_started;
static uint32_t s_seq;
static uint32_t s_retries;
static uint32_t s_next_hello_ms;
static uint32_t s_deadline_ms;
static uint32_t s_last_ready_ms;

static uint32_t s_tx_count;
static uint32_t s_rx_count;
static uint32_t s_timeout_count;
static uint32_t s_reply_timeout_count;
static uint32_t s_retry_count;
static uint32_t s_invalid_count;
static uint32_t s_build_fail_count;
static uint32_t s_tx_fail_count;
static uint32_t s_reason;

static bool time_reached(uint32_t now_ms, uint32_t target_ms)
{
    return (int32_t)(now_ms - target_ms) >= 0;
}

static void relay_enter_idle(void)
{
    s_state = REFMEM_BOOTSTRAP_RELAY_IDLE;
    s_rx_started = false;
}

void refmem_bootstrap_relay_reset(void)
{
    memset(&s_config, 0, sizeof(s_config));
    s_initialized = false;
    s_state = REFMEM_BOOTSTRAP_RELAY_IDLE;
    s_ready = false;
    s_rx_started = false;
    s_seq = 0u;
    s_retries = 0u;
    s_next_hello_ms = 0u;
    s_deadline_ms = 0u;
    s_last_ready_ms = 0u;
    s_tx_count = 0u;
    s_rx_count = 0u;
    s_timeout_count = 0u;
    s_reply_timeout_count = 0u;
    s_retry_count = 0u;
    s_invalid_count = 0u;
    s_build_fail_count = 0u;
    s_tx_fail_count = 0u;
    s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_NONE;
}

bool refmem_bootstrap_relay_init(const refmem_bootstrap_relay_config_t *config)
{
    if (config == NULL || config->build_probe == NULL ||
        config->parse_probe == NULL || config->ack_timeout_ms == 0u ||
        config->local_slot == config->peer_slot) {
        return false;
    }
    s_config = *config;
    s_initialized = true;
    s_state = s_config.responder ? REFMEM_BOOTSTRAP_RELAY_LISTEN
                                 : REFMEM_BOOTSTRAP_RELAY_IDLE;
    s_rx_started = false;
    s_ready = false;
    s_seq = 0u;
    s_retries = 0u;
    s_next_hello_ms = 0u;
    s_deadline_ms = 0u;
    s_last_ready_ms = 0u;
    s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_NONE;
    return true;
}

static void relay_fail_attempt(uint32_t now_ms, uint32_t reason)
{
    s_reason = reason;
    s_ready = false;
    relay_enter_idle();
    if (s_retries < s_config.max_retries) {
        s_retries++;
        s_retry_count++;
        s_next_hello_ms = now_ms;
    } else {
        s_retries = 0u;
        s_next_hello_ms = now_ms + s_config.hello_interval_ms;
    }
}

static void relay_begin_attempt(uint32_t now_ms)
{
    uint8_t frame[REFMEM_BOOTSTRAP_RELAY_FRAME_MAX];
    size_t frame_size = 0u;
    if (!s_config.build_probe(s_config.local_slot, s_config.peer_slot, s_seq,
                              frame, sizeof(frame), &frame_size) ||
        frame_size == 0u) {
        s_build_fail_count++;
        s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_BUILD_FAIL;
        s_next_hello_ms = now_ms + s_config.hello_interval_ms;
        return;
    }
    if (!refmem_bootstrap_transport_transmit(frame, frame_size)) {
        s_tx_fail_count++;
        relay_fail_attempt(now_ms, (uint32_t)REFMEM_BOOTSTRAP_REASON_TX_FAIL);
        return;
    }
    s_tx_count++;
    s_seq++;
    s_rx_started = false;
    s_deadline_ms = now_ms + s_config.ack_timeout_ms;
    s_state = REFMEM_BOOTSTRAP_RELAY_WAIT_ACK;
}

static void relay_service_wait_ack(uint32_t now_ms)
{
    if (!s_rx_started) {
        if (!refmem_bootstrap_transport_receive_begin(
                REFMEM_BOOTSTRAP_RELAY_FRAME_MAX, s_config.ack_timeout_ms)) {
            s_timeout_count++;
            relay_fail_attempt(now_ms,
                               (uint32_t)REFMEM_BOOTSTRAP_REASON_ACK_TIMEOUT);
            return;
        }
        s_rx_started = true;
        return;
    }

    uint8_t frame[REFMEM_BOOTSTRAP_RELAY_FRAME_MAX];
    size_t frame_size = 0u;
    const int poll = refmem_bootstrap_transport_receive_poll(
        frame, sizeof(frame), &frame_size);
    if (poll > 0) {
        s_rx_count++;
        uint8_t source_slot = 0xFFu;
        bool is_ack = false;
        if (s_config.parse_probe(frame, frame_size, &source_slot, &is_ack) &&
            source_slot == s_config.peer_slot && is_ack) {
            s_ready = true;
            s_last_ready_ms = now_ms;
            s_retries = 0u;
            s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_NONE;
            relay_enter_idle();
            s_next_hello_ms = now_ms + s_config.hello_interval_ms;
            return;
        }
        s_invalid_count++;
        s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_INVALID_FRAME;
        if (s_config.frame_handler != NULL) {
            (void)s_config.frame_handler(frame, frame_size);
            s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_NONE;
        }
        /* A malformed frame does not clear readiness early; wait for the
         * deadline or a valid frame from the expected peer. */
    } else if (poll < 0) {
        s_timeout_count++;
        relay_fail_attempt(now_ms,
                           (uint32_t)REFMEM_BOOTSTRAP_REASON_ACK_TIMEOUT);
        return;
    }

    if (time_reached(now_ms, s_deadline_ms)) {
        s_timeout_count++;
        relay_fail_attempt(now_ms,
                           (uint32_t)REFMEM_BOOTSTRAP_REASON_ACK_TIMEOUT);
    }
}

static void relay_service_responder(uint32_t now_ms)
{
    if (s_state == REFMEM_BOOTSTRAP_RELAY_REPLY_WAIT) {
        if (!time_reached(now_ms, s_deadline_ms)) {
            return;
        }
        uint8_t frame[REFMEM_BOOTSTRAP_RELAY_FRAME_MAX];
        size_t frame_size = 0u;
        if (s_config.build_probe(s_config.local_slot, s_config.peer_slot, s_seq,
                                 frame, sizeof(frame), &frame_size) &&
            frame_size != 0u &&
            refmem_bootstrap_transport_transmit(frame, frame_size)) {
            s_tx_count++;
            s_seq++;
        } else {
            s_tx_fail_count++;
            s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_TX_FAIL;
        }
        s_state = REFMEM_BOOTSTRAP_RELAY_LISTEN;
        s_rx_started = false;
        return;
    }

    if (!s_rx_started) {
        if (!refmem_bootstrap_transport_receive_begin(
                REFMEM_BOOTSTRAP_RELAY_FRAME_MAX, s_config.ack_timeout_ms)) {
            s_timeout_count++;
            s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_ACK_TIMEOUT;
            return;
        }
        s_rx_started = true;
        return;
    }

    uint8_t frame[REFMEM_BOOTSTRAP_RELAY_FRAME_MAX];
    size_t frame_size = 0u;
    const int poll = refmem_bootstrap_transport_receive_poll(
        frame, sizeof(frame), &frame_size);
    if (poll > 0) {
        s_rx_count++;
        s_rx_started = false;
        uint8_t source_slot = 0xFFu;
        bool is_ack = false;
        if (s_config.parse_probe(frame, frame_size, &source_slot, &is_ack) &&
            source_slot == s_config.peer_slot && is_ack) {
            s_ready = true;
            s_last_ready_ms = now_ms;
            s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_NONE;
            s_deadline_ms = now_ms + s_config.reply_guard_ms;
            s_state = REFMEM_BOOTSTRAP_RELAY_REPLY_WAIT;
            return;
        }
        s_invalid_count++;
        s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_INVALID_FRAME;
        if (s_config.frame_handler != NULL) {
            (void)s_config.frame_handler(frame, frame_size);
            s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_NONE;
        }
        return;
    }
    if (poll < 0) {
        s_timeout_count++;
        s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_ACK_TIMEOUT;
        s_rx_started = false;
    }
}

static void relay_service_wait_reply(uint32_t now_ms)
{
    if (!s_rx_started) {
        if (!refmem_bootstrap_transport_receive_begin(
                REFMEM_BOOTSTRAP_RELAY_FRAME_MAX, s_config.reply_timeout_ms)) {
            s_timeout_count++;
            s_reply_timeout_count++;
            s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_ACK_TIMEOUT;
            s_state = REFMEM_BOOTSTRAP_RELAY_IDLE;
            return;
        }
        s_rx_started = true;
        return;
    }

    uint8_t frame[REFMEM_BOOTSTRAP_RELAY_FRAME_MAX];
    size_t frame_size = 0u;
    const int poll = refmem_bootstrap_transport_receive_poll(
        frame, sizeof(frame), &frame_size);
    if (poll > 0) {
        s_rx_count++;
        s_rx_started = false;
        if (s_config.frame_handler != NULL) {
            (void)s_config.frame_handler(frame, frame_size);
        }
        s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_NONE;
        s_state = REFMEM_BOOTSTRAP_RELAY_IDLE;
        return;
    }
    if (poll < 0 || time_reached(now_ms, s_deadline_ms)) {
        s_timeout_count++;
        s_reply_timeout_count++;
        s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_ACK_TIMEOUT;
        s_state = REFMEM_BOOTSTRAP_RELAY_IDLE;
        s_rx_started = false;
    }
}

bool refmem_bootstrap_relay_submit(const uint8_t *frame, size_t frame_size,
                                   uint32_t now_ms)
{
    if (!s_initialized || frame == NULL || frame_size == 0u ||
        frame_size > REFMEM_BOOTSTRAP_RELAY_FRAME_MAX ||
        !refmem_bootstrap_transport_armed() || !s_ready) {
        return false;
    }
    if (s_config.responder) {
        if (s_state != REFMEM_BOOTSTRAP_RELAY_LISTEN) {
            return false;
        }
    } else if (s_state != REFMEM_BOOTSTRAP_RELAY_IDLE &&
               s_state != REFMEM_BOOTSTRAP_RELAY_WAIT_ACK) {
        return false;
    }
    if (!refmem_bootstrap_transport_transmit(frame, frame_size)) {
        s_tx_fail_count++;
        s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_TX_FAIL;
        return false;
    }
    s_tx_count++;
    s_rx_started = false;
    if (s_config.responder) {
        s_state = REFMEM_BOOTSTRAP_RELAY_LISTEN;
    } else {
        s_state = REFMEM_BOOTSTRAP_RELAY_WAIT_REPLY;
        s_deadline_ms = now_ms + s_config.reply_timeout_ms;
    }
    return true;
}

void refmem_bootstrap_relay_service(uint32_t now_ms)
{
    if (!s_initialized) {
        return;
    }
    if (!refmem_bootstrap_transport_armed()) {
        s_ready = false;
        s_rx_started = false;
        s_state = s_config.responder ? REFMEM_BOOTSTRAP_RELAY_LISTEN
                                     : REFMEM_BOOTSTRAP_RELAY_IDLE;
        return;
    }
    if (s_ready && s_config.peer_timeout_ms != 0u &&
        (uint32_t)(now_ms - s_last_ready_ms) > s_config.peer_timeout_ms) {
        s_ready = false;
        s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_PEER_TIMEOUT;
    }
    if (s_config.responder) {
        relay_service_responder(now_ms);
        return;
    }
    if (s_state == REFMEM_BOOTSTRAP_RELAY_WAIT_REPLY) {
        relay_service_wait_reply(now_ms);
        return;
    }
    if (s_state == REFMEM_BOOTSTRAP_RELAY_WAIT_ACK) {
        relay_service_wait_ack(now_ms);
        return;
    }
    if (!time_reached(now_ms, s_next_hello_ms)) {
        return;
    }
    relay_begin_attempt(now_ms);
}

bool refmem_bootstrap_relay_ready(void)
{
    return s_ready;
}

refmem_bootstrap_relay_state_t refmem_bootstrap_relay_state(void)
{
    return s_state;
}

uint8_t refmem_bootstrap_relay_peer_slot(void)
{
    return s_config.peer_slot;
}

uint32_t refmem_bootstrap_relay_last_reason(void)
{
    return s_reason;
}

void refmem_bootstrap_relay_handover(void)
{
    s_ready = false;
    s_rx_started = false;
    s_state = s_config.responder ? REFMEM_BOOTSTRAP_RELAY_LISTEN
                                 : REFMEM_BOOTSTRAP_RELAY_IDLE;
    s_reason = (uint32_t)REFMEM_BOOTSTRAP_REASON_RING_HANDOVER;
}

uint32_t refmem_bootstrap_relay_tx_count(void)
{
    return s_tx_count;
}

uint32_t refmem_bootstrap_relay_rx_count(void)
{
    return s_rx_count;
}

uint32_t refmem_bootstrap_relay_timeout_count(void)
{
    return s_timeout_count;
}

uint32_t refmem_bootstrap_relay_reply_timeout_count(void)
{
    return s_reply_timeout_count;
}

uint32_t refmem_bootstrap_relay_retry_count(void)
{
    return s_retry_count;
}

uint32_t refmem_bootstrap_relay_invalid_count(void)
{
    return s_invalid_count;
}

uint32_t refmem_bootstrap_relay_build_fail_count(void)
{
    return s_build_fail_count;
}

uint32_t refmem_bootstrap_relay_tx_fail_count(void)
{
    return s_tx_fail_count;
}
