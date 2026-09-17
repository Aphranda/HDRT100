#include "trigger_sequence_link_protocol.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef uint8_t fragments_t[TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT][TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE];

static const trigger_sequence_link_message_t baseline = {
    TRIGGER_SEQUENCE_LINK_LINK_APPLIED, 0x12345678u, 9u, 13u, 0u, 2u, 3u, 17u,
};

static void encode(const trigger_sequence_link_message_t *message, uint16_t token,
                   fragments_t fragments)
{
    trigger_sequence_link_tx_t tx = {0};
    assert(trigger_sequence_link_tx_begin(&tx, message, token));
    for (uint32_t i = 0u; i < TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT; ++i)
        assert(trigger_sequence_link_tx_next(&tx, fragments[i]));
    uint8_t repeated[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE];
    assert(trigger_sequence_link_tx_next(&tx, repeated));
    assert(memcmp(repeated, fragments[0], sizeof(repeated)) == 0);
}

static trigger_sequence_link_rx_result_t feed_all(trigger_sequence_link_rx_t *rx,
                                                  fragments_t fragments,
                                                  trigger_sequence_link_message_t *out)
{
    trigger_sequence_link_rx_result_t result = TRIGGER_SEQUENCE_LINK_RX_REJECTED;
    for (uint32_t i = 0u; i < TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT; ++i) {
        result = trigger_sequence_link_rx_feed(rx, fragments[i], out);
        if (i + 1u != TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT)
            assert(result == TRIGGER_SEQUENCE_LINK_RX_INCOMPLETE);
    }
    return result;
}

static void golden_wire(void)
{
    const uint8_t expected[10] = {
        1u, 0x78u, 0x56u, 0x34u, 0x12u, 0u, 0u, 0u, 0u, 17u,
    };
    trigger_sequence_link_tx_t tx = {0};
    assert(trigger_sequence_link_tx_begin(&tx, &baseline, 0xBE01u));
    assert(memcmp(tx.wire, expected, sizeof(expected)) == 0);
    fragments_t f;
    encode(&baseline, 0xBE01u, f);
    assert(memcmp(f[0], expected, sizeof(expected)) == 0);
}

static void repeated_and_replaced(void)
{
    fragments_t f;
    encode(&baseline, UINT16_MAX, f);
    trigger_sequence_link_rx_t rx;
    trigger_sequence_link_rx_init(&rx);
    trigger_sequence_link_message_t out = {0};
    assert(feed_all(&rx, f, &out) == TRIGGER_SEQUENCE_LINK_RX_MESSAGE);
    assert(out.kind == baseline.kind && out.run_id == baseline.run_id &&
           out.step_ordinal == baseline.step_ordinal &&
           out.exchange_id == (uint8_t)baseline.exchange_id);
    memset(&out, 0, sizeof(out));
    assert(feed_all(&rx, f, &out) == TRIGGER_SEQUENCE_LINK_RX_DUPLICATE);
    assert(out.run_id == 0u);
    encode(&baseline, 1u, f); /* Local token changes are not wire identity. */
    assert(feed_all(&rx, f, &out) == TRIGGER_SEQUENCE_LINK_RX_DUPLICATE);
    trigger_sequence_link_message_t next = baseline;
    next.kind = TRIGGER_SEQUENCE_LINK_READY_NEXT;
    next.source_slot = 3u;
    next.target_slot = 2u;
    next.step_ordinal = UINT32_MAX;
    encode(&next, 2u, f);
    assert(feed_all(&rx, f, &out) == TRIGGER_SEQUENCE_LINK_RX_MESSAGE);
    assert(out.kind == next.kind && out.run_id == next.run_id &&
           out.step_ordinal == next.step_ordinal &&
           out.exchange_id == (uint8_t)next.exchange_id);
}

static void compact_boundaries(void)
{
    fragments_t f;
    trigger_sequence_link_rx_t rx;
    trigger_sequence_link_message_t out = {0};
    trigger_sequence_link_rx_init(&rx);
    trigger_sequence_link_message_t wrap = baseline;
    wrap.step_ordinal = UINT32_MAX;
    wrap.exchange_id = 256u;
    encode(&wrap, 5u, f);
    assert(feed_all(&rx, f, &out) == TRIGGER_SEQUENCE_LINK_RX_MESSAGE);
    assert(out.step_ordinal == UINT32_MAX && out.exchange_id == 0u);

    f[0][0] = 0u;
    assert(trigger_sequence_link_rx_feed(&rx, f[0], &out) ==
           TRIGGER_SEQUENCE_LINK_RX_REJECTED);
    f[0][0] = TRIGGER_SEQUENCE_LINK_READY_NEXT;
    memset(&f[0][1], 0, sizeof(uint32_t));
    assert(trigger_sequence_link_rx_feed(&rx, f[0], &out) ==
           TRIGGER_SEQUENCE_LINK_RX_REJECTED);
}

static void invalid_arguments(void)
{
    trigger_sequence_link_tx_t tx = {0};
    assert(trigger_sequence_link_tx_begin(&tx, &baseline, 3u));
    const trigger_sequence_link_tx_t before = tx;
    assert(!trigger_sequence_link_tx_begin(&tx, &baseline, 0u));
    assert(!trigger_sequence_link_tx_begin(NULL, &baseline, 1u));
    assert(!trigger_sequence_link_tx_begin(&tx, NULL, 1u));
    for (uint32_t field = 0u; field < 7u; ++field) {
        trigger_sequence_link_message_t bad = baseline;
        if (field == 0u) bad.kind = 4u;
        if (field == 1u) bad.run_id = 0u;
        if (field == 2u) bad.generation = 0u;
        if (field == 3u) bad.binding_epoch = 0u;
        if (field == 4u) bad.source_slot = UINT32_MAX;
        if (field == 5u) bad.target_slot = bad.source_slot;
        if (field == 6u) bad.exchange_id = 0u;
        assert(!trigger_sequence_link_tx_begin(&tx, &bad, 1u));
        assert(memcmp(&tx, &before, sizeof(tx)) == 0);
    }
    fragments_t f;
    encode(&baseline, 1u, f);
    trigger_sequence_link_rx_t rx;
    trigger_sequence_link_rx_init(&rx);
    trigger_sequence_link_message_t out = baseline;
    assert(trigger_sequence_link_rx_feed(NULL, f[0], &out) == TRIGGER_SEQUENCE_LINK_RX_REJECTED);
    assert(trigger_sequence_link_rx_feed(&rx, NULL, &out) == TRIGGER_SEQUENCE_LINK_RX_REJECTED);
    assert(trigger_sequence_link_rx_feed(&rx, f[0], NULL) == TRIGGER_SEQUENCE_LINK_RX_REJECTED);
    f[0][0] = 4u;
    assert(trigger_sequence_link_rx_feed(&rx, f[0], &out) == TRIGGER_SEQUENCE_LINK_RX_REJECTED);
    assert(memcmp(&out, &baseline, sizeof(out)) == 0);
}

int main(void)
{
    golden_wire();
    repeated_and_replaced();
    compact_boundaries();
    invalid_arguments();
    puts("sequence link codec passed");
    return 0;
}
