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
    /* Independently computed via Python struct.pack('<8I', ...) + zlib.crc32. */
    const uint8_t expected[36] = {
        1,0,0,0, 0x78,0x56,0x34,0x12, 9,0,0,0, 13,0,0,0,
        0,0,0,0, 2,0,0,0, 3,0,0,0, 17,0,0,0, 0xce,0x5a,0x64,0x4d,
    };
    trigger_sequence_link_tx_t tx = {0};
    assert(trigger_sequence_link_tx_begin(&tx, &baseline, 0xBE01u));
    assert(memcmp(tx.wire, expected, sizeof(expected)) == 0);
    fragments_t f;
    encode(&baseline, 0xBE01u, f);
    for (uint32_t i = 0u; i < 6u; ++i) {
        assert(f[i][0] == 1u && f[i][1] == 0xBEu && f[i][2] == i && f[i][3] == 6u);
    }
    assert(f[5][4] == 0u && f[5][5] == 0u);
    assert(f[5][6] == 0xCEu && f[5][9] == 0x4Du);
}

static void reordered_lost_repeated(void)
{
    fragments_t f;
    encode(&baseline, UINT16_MAX, f);
    trigger_sequence_link_rx_t rx;
    trigger_sequence_link_rx_init(&rx);
    trigger_sequence_link_message_t out = {0};
    const uint8_t order[] = {5u, 1u, 1u, 0u, 4u, 3u}; /* fragment 2 lost */
    for (uint32_t i = 0u; i < sizeof(order); ++i) {
        assert(trigger_sequence_link_rx_feed(&rx, f[order[i]], &out) == TRIGGER_SEQUENCE_LINK_RX_INCOMPLETE);
        assert(out.run_id == 0u);
    }
    assert(trigger_sequence_link_rx_feed(&rx, f[2], &out) == TRIGGER_SEQUENCE_LINK_RX_MESSAGE);
    assert(memcmp(&out, &baseline, sizeof(out)) == 0);
    memset(&out, 0, sizeof(out));
    assert(feed_all(&rx, f, &out) == TRIGGER_SEQUENCE_LINK_RX_DUPLICATE);
    assert(out.run_id == 0u);
    encode(&baseline, 1u, f); /* token wrap/replacement does not reissue message */
    assert(feed_all(&rx, f, &out) == TRIGGER_SEQUENCE_LINK_RX_DUPLICATE);
    trigger_sequence_link_message_t next = baseline;
    next.kind = TRIGGER_SEQUENCE_LINK_READY_NEXT;
    next.source_slot = 3u;
    next.target_slot = 2u;
    next.step_ordinal = UINT32_MAX;
    encode(&next, 2u, f);
    assert(feed_all(&rx, f, &out) == TRIGGER_SEQUENCE_LINK_RX_MESSAGE);
    assert(memcmp(&out, &next, sizeof(out)) == 0);
}

static void torn_and_corrupt(void)
{
    fragments_t a, b;
    encode(&baseline, 5u, a);
    trigger_sequence_link_message_t other = baseline;
    other.run_id++;
    encode(&other, 6u, b);
    trigger_sequence_link_rx_t rx;
    trigger_sequence_link_message_t out = {0};
    trigger_sequence_link_rx_init(&rx);
    for (uint32_t i = 0u; i < 3u; ++i)
        assert(trigger_sequence_link_rx_feed(&rx, a[i], &out) == TRIGGER_SEQUENCE_LINK_RX_INCOMPLETE);
    for (uint32_t i = 3u; i < 6u; ++i)
        assert(trigger_sequence_link_rx_feed(&rx, b[i], &out) == TRIGGER_SEQUENCE_LINK_RX_INCOMPLETE);
    for (uint32_t i = 0u; i < 3u; ++i)
        assert(trigger_sequence_link_rx_feed(&rx, b[i], &out) ==
               (i == 2u ? TRIGGER_SEQUENCE_LINK_RX_MESSAGE : TRIGGER_SEQUENCE_LINK_RX_INCOMPLETE));
    assert(out.run_id == other.run_id);
    /* Reused token cannot silently overwrite a fragment already collected. */
    trigger_sequence_link_rx_init(&rx);
    assert(trigger_sequence_link_rx_feed(&rx, a[0], &out) == TRIGGER_SEQUENCE_LINK_RX_INCOMPLETE);
    uint8_t corrupted[10];
    memcpy(corrupted, a[0], sizeof(corrupted));
    corrupted[9] ^= 1u;
    assert(trigger_sequence_link_rx_feed(&rx, corrupted, &out) == TRIGGER_SEQUENCE_LINK_RX_REJECTED);
    assert(feed_all(&rx, a, &out) == TRIGGER_SEQUENCE_LINK_RX_MESSAGE);
    /* Every payload bit corruption must be detected, including CRC and padding. */
    for (uint32_t fragment = 0u; fragment < 6u; ++fragment) {
        for (uint32_t byte = 4u; byte < 10u; ++byte) {
            for (uint32_t bit = 0u; bit < 8u; ++bit) {
                trigger_sequence_link_rx_init(&rx);
                memcpy(b, a, sizeof(b));
                b[fragment][byte] ^= (uint8_t)(1u << bit);
                uint32_t rejected = 0u;
                for (uint32_t i = 0u; i < 6u; ++i) {
                    const trigger_sequence_link_rx_result_t r = trigger_sequence_link_rx_feed(&rx, b[i], &out);
                    assert(r != TRIGGER_SEQUENCE_LINK_RX_MESSAGE && r != TRIGGER_SEQUENCE_LINK_RX_DUPLICATE);
                    rejected += r == TRIGGER_SEQUENCE_LINK_RX_REJECTED;
                }
                assert(rejected != 0u);
            }
        }
    }
    /* Same-token fragments from two messages fail end-to-end integrity. */
    encode(&other, 5u, b);
    memcpy(b[0], a[0], sizeof(b[0]));
    trigger_sequence_link_rx_init(&rx);
    assert(feed_all(&rx, b, &out) == TRIGGER_SEQUENCE_LINK_RX_REJECTED);
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
    f[0][2] = 6u;
    assert(trigger_sequence_link_rx_feed(&rx, f[0], &out) == TRIGGER_SEQUENCE_LINK_RX_REJECTED);
    f[0][2] = 0u; f[0][3] = 5u;
    assert(trigger_sequence_link_rx_feed(&rx, f[0], &out) == TRIGGER_SEQUENCE_LINK_RX_REJECTED);
    f[0][3] = 6u; f[0][0] = 0u;
    assert(trigger_sequence_link_rx_feed(&rx, f[0], &out) == TRIGGER_SEQUENCE_LINK_RX_REJECTED);
    assert(memcmp(&out, &baseline, sizeof(out)) == 0);
}

int main(void)
{
    golden_wire();
    reordered_lost_repeated();
    torn_and_corrupt();
    invalid_arguments();
    puts("sequence link codec passed");
    return 0;
}
