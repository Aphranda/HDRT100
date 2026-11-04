#include "refmem_bootstrap_relay.h"

#include <stdio.h>
#include <string.h>

/* Fake transport layer: the relay only uses these four owner entry points. */
static bool s_armed;
static int s_tx_calls;
static bool s_tx_ok;
static int s_rx_begin_calls;
static bool s_rx_begin_ok;
static int s_poll_result;
static uint8_t s_rx_frame[8];
static size_t s_rx_size;

bool refmem_bootstrap_transport_armed(void)
{
    return s_armed;
}

bool refmem_bootstrap_transport_transmit(const uint8_t *frame,
                                         size_t frame_size)
{
    (void)frame;
    (void)frame_size;
    s_tx_calls++;
    return s_tx_ok;
}

bool refmem_bootstrap_transport_receive_begin(size_t frame_capacity,
                                              uint32_t timeout_ms)
{
    (void)frame_capacity;
    (void)timeout_ms;
    s_rx_begin_calls++;
    return s_rx_begin_ok;
}

int refmem_bootstrap_transport_receive_poll(uint8_t *frame,
                                           size_t frame_capacity,
                                           size_t *frame_size)
{
    if (s_poll_result > 0) {
        if (s_rx_size > frame_capacity) {
            return -1;
        }
        memcpy(frame, s_rx_frame, s_rx_size);
        *frame_size = s_rx_size;
    }
    return s_poll_result;
}

/* Fake probe codec: frame[0] = ack flag, frame[1] = source slot. */
static bool fake_build(uint8_t source_slot, uint8_t target_slot, uint32_t seq,
                       uint8_t *frame, size_t frame_capacity, size_t *frame_size)
{
    (void)source_slot;
    (void)seq;
    if (frame_capacity < 2u) {
        return false;
    }
    frame[0] = 1u;
    frame[1] = target_slot;
    *frame_size = 2u;
    return true;
}

static bool fake_parse(const uint8_t *frame, size_t frame_size,
                       uint8_t *source_slot, bool *is_ack)
{
    if (frame_size < 2u || frame[0] == 0xEEu) {
        return false;
    }
    *source_slot = frame[1];
    *is_ack = frame[0] != 0u;
    return true;
}

static int s_handler_calls;
static uint8_t s_handler_first;

static bool fake_frame_handler(const uint8_t *frame, size_t frame_size)
{
    s_handler_calls++;
    if (frame != NULL && frame_size != 0u) {
        s_handler_first = frame[0];
    }
    return true;
}

static int expect(const char *name, int actual, int expected)
{
    if ((actual != 0) != (expected != 0)) {
        (void)printf("%s: expected %d got %d\n", name, expected, actual);
        return 1;
    }
    return 0;
}

static int expect_u32(const char *name, uint32_t actual, uint32_t expected)
{
    if (actual != expected) {
        (void)printf("%s: expected %u got %u\n", name, expected, actual);
        return 1;
    }
    return 0;
}

static bool init_relay(void)
{
    refmem_bootstrap_relay_config_t config;
    memset(&config, 0, sizeof(config));
    config.local_slot = 0u;
    config.peer_slot = 1u;
    config.hello_interval_ms = 1000u;
    config.ack_timeout_ms = 50u;
    config.peer_timeout_ms = 5000u;
    config.reply_guard_ms = 20u;
    config.reply_timeout_ms = 40u;
    config.max_retries = 2u;
    config.build_probe = fake_build;
    config.parse_probe = fake_parse;
    config.frame_handler = fake_frame_handler;
    return refmem_bootstrap_relay_init(&config);
}

static bool init_relay_responder(void)
{
    refmem_bootstrap_relay_config_t config;
    memset(&config, 0, sizeof(config));
    config.local_slot = 1u;
    config.peer_slot = 0u;
    config.hello_interval_ms = 1000u;
    config.ack_timeout_ms = 50u;
    config.peer_timeout_ms = 5000u;
    config.reply_guard_ms = 20u;
    config.reply_timeout_ms = 40u;
    config.max_retries = 2u;
    config.responder = true;
    config.build_probe = fake_build;
    config.parse_probe = fake_parse;
    config.frame_handler = fake_frame_handler;
    return refmem_bootstrap_relay_init(&config);
}

int main(void)
{
    int failed = 0;

    /* Invalid configurations are rejected. */
    failed += expect("init null", refmem_bootstrap_relay_init(NULL), 0);
    {
        refmem_bootstrap_relay_config_t bad;
        memset(&bad, 0, sizeof(bad));
        bad.local_slot = 1u;
        bad.peer_slot = 1u;
        bad.ack_timeout_ms = 10u;
        bad.build_probe = fake_build;
        bad.parse_probe = fake_parse;
        failed += expect("init same slot",
                         refmem_bootstrap_relay_init(&bad), 0);
    }

    /* Unarmed transport never transmits. */
    refmem_bootstrap_relay_reset();
    failed += expect("init ok", init_relay(), 1);
    s_armed = false;
    refmem_bootstrap_relay_service(0u);
    failed += expect("unarmed no tx", s_tx_calls, 0);
    failed += expect("unarmed not ready", refmem_bootstrap_relay_ready(), 0);

    /* Handshake: TX probe, then RX ack marks the peer ready. */
    s_armed = true;
    s_tx_ok = true;
    s_rx_begin_ok = true;
    s_poll_result = 0;
    refmem_bootstrap_relay_service(0u);
    failed += expect("probe tx", s_tx_calls, 1);
    failed += expect("tx count", refmem_bootstrap_relay_tx_count(), 1u);
    failed += expect("state wait",
                     refmem_bootstrap_relay_state() ==
                         REFMEM_BOOTSTRAP_RELAY_WAIT_ACK, 1);
    refmem_bootstrap_relay_service(1u);
    failed += expect("rx begin", s_rx_begin_calls, 1);
    refmem_bootstrap_relay_service(2u);
    failed += expect("pending not ready", refmem_bootstrap_relay_ready(), 0);

    s_poll_result = 1;
    s_rx_frame[0] = 1u;
    s_rx_frame[1] = 1u;
    s_rx_size = 2u;
    refmem_bootstrap_relay_service(3u);
    failed += expect("ack ready", refmem_bootstrap_relay_ready(), 1);
    failed += expect("rx count", refmem_bootstrap_relay_rx_count(), 1u);
    failed += expect("state idle",
                     refmem_bootstrap_relay_state() ==
                         REFMEM_BOOTSTRAP_RELAY_IDLE, 1);

    /* Peer heartbeat expiry drops readiness and latches the reason. */
    refmem_bootstrap_relay_service(5004u);
    failed += expect("peer timeout drops ready",
                     refmem_bootstrap_relay_ready(), 0);
    failed += expect_u32("peer timeout reason",
                         refmem_bootstrap_relay_last_reason(),
                         (uint32_t)REFMEM_BOOTSTRAP_REASON_PEER_TIMEOUT);

    /* Timeout budget: two retries, then the backoff interval. */
    refmem_bootstrap_relay_reset();
    failed += expect("init ok 2", init_relay(), 1);
    s_armed = true;
    s_tx_ok = true;
    s_rx_begin_ok = true;
    s_poll_result = 0;
    s_tx_calls = 0;
    s_rx_begin_calls = 0;

    refmem_bootstrap_relay_service(0u);   /* attempt 1 */
    refmem_bootstrap_relay_service(1u);   /* rx begin */
    refmem_bootstrap_relay_service(51u);  /* timeout -> retry 1 */
    refmem_bootstrap_relay_service(51u);  /* attempt 2 */
    refmem_bootstrap_relay_service(52u);  /* rx begin */
    refmem_bootstrap_relay_service(102u); /* timeout -> retry 2 */
    refmem_bootstrap_relay_service(102u); /* attempt 3 */
    refmem_bootstrap_relay_service(103u); /* rx begin */
    refmem_bootstrap_relay_service(153u); /* timeout -> backoff */
    failed += expect_u32("timeouts", refmem_bootstrap_relay_timeout_count(),
                         3u);
    failed += expect_u32("retries", refmem_bootstrap_relay_retry_count(), 2u);
    failed += expect_u32("attempts", refmem_bootstrap_relay_tx_count(), 3u);
    refmem_bootstrap_relay_service(154u);
    failed += expect("backoff blocks attempt",
                     refmem_bootstrap_relay_tx_count() == 3u, 1);

    /* A non-ack frame is counted invalid and does not set readiness. */
    refmem_bootstrap_relay_reset();
    failed += expect("init ok 3", init_relay(), 1);
    s_armed = true;
    s_tx_ok = true;
    s_rx_begin_ok = true;
    s_poll_result = 1;
    s_rx_frame[0] = 0u;
    s_rx_frame[1] = 1u;
    s_rx_size = 2u;
    refmem_bootstrap_relay_service(0u);
    refmem_bootstrap_relay_service(1u);
    refmem_bootstrap_relay_service(2u);
    failed += expect("invalid not ready", refmem_bootstrap_relay_ready(), 0);
    failed += expect_u32("invalid count",
                         refmem_bootstrap_relay_invalid_count(), 1u);

    /* TX failure exhausts the retry budget without spinning forever. */
    refmem_bootstrap_relay_reset();
    failed += expect("init ok 4", init_relay(), 1);
    s_armed = true;
    s_tx_ok = false;
    s_tx_calls = 0;
    refmem_bootstrap_relay_service(0u);
    refmem_bootstrap_relay_service(0u);
    refmem_bootstrap_relay_service(0u);
    failed += expect_u32("tx failures", refmem_bootstrap_relay_tx_fail_count(),
                         3u);
    failed += expect_u32("tx failure retries",
                         refmem_bootstrap_relay_retry_count(), 2u);
    refmem_bootstrap_relay_service(1u);
    failed += expect("tx failure backoff", s_tx_calls == 3, 1);

    /* Responder mode: listen, hear the peer, guard, then send one ack. */
    refmem_bootstrap_relay_reset();
    failed += expect("init responder", init_relay_responder(), 1);
    s_armed = true;
    s_tx_ok = true;
    s_rx_begin_ok = true;
    s_poll_result = 1;
    s_rx_frame[0] = 1u;
    s_rx_frame[1] = 0u; /* peer slot 0 */
    s_rx_size = 2u;
    s_tx_calls = 0;
    s_rx_begin_calls = 0;
    refmem_bootstrap_relay_service(0u);
    failed += expect("responder rx begin", s_rx_begin_calls, 1);
    refmem_bootstrap_relay_service(1u);
    failed += expect("responder ready", refmem_bootstrap_relay_ready(), 1);
    failed += expect("responder reply wait",
                     refmem_bootstrap_relay_state() ==
                         REFMEM_BOOTSTRAP_RELAY_REPLY_WAIT, 1);
    refmem_bootstrap_relay_service(5u);
    failed += expect("responder guard blocks tx", s_tx_calls, 0);
    refmem_bootstrap_relay_service(25u);
    failed += expect("responder ack", s_tx_calls, 1);
    failed += expect("responder listen again",
                     refmem_bootstrap_relay_state() ==
                         REFMEM_BOOTSTRAP_RELAY_LISTEN, 1);

    /* Ring handover retires readiness and latches RING_HANDOVER. */
    refmem_bootstrap_relay_reset();
    failed += expect("init ok 5", init_relay(), 1);
    s_armed = true;
    s_tx_ok = true;
    s_rx_begin_ok = true;
    s_poll_result = 1;
    s_rx_frame[0] = 1u;
    s_rx_frame[1] = 1u;
    s_rx_size = 2u;
    refmem_bootstrap_relay_service(0u);
    refmem_bootstrap_relay_service(1u);
    refmem_bootstrap_relay_service(2u);
    failed += expect("ready before handover",
                     refmem_bootstrap_relay_ready(), 1);
    refmem_bootstrap_relay_handover();
    failed += expect("handover not ready",
                     refmem_bootstrap_relay_ready(), 0);
    failed += expect_u32("handover reason",
                         refmem_bootstrap_relay_last_reason(),
                         (uint32_t)REFMEM_BOOTSTRAP_REASON_RING_HANDOVER);

    /* TX failure latches TX_FAIL. */
    refmem_bootstrap_relay_reset();
    failed += expect("init ok 6", init_relay(), 1);
    s_armed = true;
    s_tx_ok = false;
    refmem_bootstrap_relay_service(0u);
    failed += expect_u32("tx fail reason",
                         refmem_bootstrap_relay_last_reason(),
                         (uint32_t)REFMEM_BOOTSTRAP_REASON_TX_FAIL);

    /* Application carrying: submit, reply delivery, reply timeout. */
    static const uint8_t app_frame[3] = {0xEEu, 0x10u, 0x20u};
    refmem_bootstrap_relay_reset();
    failed += expect("init ok 7", init_relay(), 1);
    s_armed = true;
    s_tx_ok = true;
    s_rx_begin_ok = true;
    s_poll_result = 0;
    failed += expect("submit before ready",
                     refmem_bootstrap_relay_submit(app_frame,
                                                   sizeof(app_frame), 0u), 0);
    s_poll_result = 1;
    s_rx_frame[0] = 1u;
    s_rx_frame[1] = 1u;
    s_rx_size = 2u;
    refmem_bootstrap_relay_service(0u);
    refmem_bootstrap_relay_service(1u);
    refmem_bootstrap_relay_service(2u);
    failed += expect("ready for carrying",
                     refmem_bootstrap_relay_ready(), 1);
    s_tx_calls = 0;
    s_handler_calls = 0;
    failed += expect("submit ready",
                     refmem_bootstrap_relay_submit(app_frame,
                                                   sizeof(app_frame), 3u), 1);
    failed += expect("submit tx", s_tx_calls, 1);
    failed += expect("submit state",
                     refmem_bootstrap_relay_state() ==
                         REFMEM_BOOTSTRAP_RELAY_WAIT_REPLY, 1);
    s_poll_result = 0;
    refmem_bootstrap_relay_service(4u);
    s_rx_frame[0] = 0xEEu;
    s_rx_frame[1] = 0x01u;
    s_rx_size = 2u;
    s_poll_result = 1;
    refmem_bootstrap_relay_service(5u);
    failed += expect("reply handler", s_handler_calls, 1);
    failed += expect("reply frame", s_handler_first == 0xEEu, 1);
    failed += expect("reply state",
                     refmem_bootstrap_relay_state() ==
                         REFMEM_BOOTSTRAP_RELAY_IDLE, 1);

    /* Reply timeout: no frame before reply_timeout drops to IDLE with reason. */
    {
        const uint32_t timeouts_before = refmem_bootstrap_relay_timeout_count();
        failed += expect("submit for timeout",
                         refmem_bootstrap_relay_submit(app_frame,
                                                       sizeof(app_frame), 10u), 1);
        s_poll_result = 0;
        refmem_bootstrap_relay_service(11u);
        refmem_bootstrap_relay_service(10u + 40u + 1u);
        failed += expect_u32("reply timeout count",
                             refmem_bootstrap_relay_timeout_count(),
                             timeouts_before + 1u);
        failed += expect_u32("reply timeout reason",
                             refmem_bootstrap_relay_last_reason(),
                             (uint32_t)REFMEM_BOOTSTRAP_REASON_ACK_TIMEOUT);
        failed += expect("reply timeout idle",
                         refmem_bootstrap_relay_state() ==
                             REFMEM_BOOTSTRAP_RELAY_IDLE, 1);
    }

    /* Responder submit: only from LISTEN and after the peer was heard. */
    refmem_bootstrap_relay_reset();
    failed += expect("init ok 8", init_relay_responder(), 1);
    s_armed = true;
    s_tx_ok = true;
    s_rx_begin_ok = true;
    s_poll_result = 1;
    s_rx_frame[0] = 1u;
    s_rx_frame[1] = 0u;
    s_rx_size = 2u;
    refmem_bootstrap_relay_service(0u);
    refmem_bootstrap_relay_service(1u);
    refmem_bootstrap_relay_service(25u);
    failed += expect("responder ready", refmem_bootstrap_relay_ready(), 1);
    s_tx_calls = 0;
    failed += expect("responder submit",
                     refmem_bootstrap_relay_submit(app_frame,
                                                   sizeof(app_frame), 30u), 1);
    failed += expect("responder submit tx", s_tx_calls, 1);
    failed += expect("responder listen",
                     refmem_bootstrap_relay_state() ==
                         REFMEM_BOOTSTRAP_RELAY_LISTEN, 1);

    if (failed != 0) {
        return 1;
    }
    (void)printf("refmem_bootstrap_relay tests passed\n");
    return 0;
}
