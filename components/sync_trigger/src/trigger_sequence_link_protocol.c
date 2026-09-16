#include "trigger_sequence_link_protocol.h"

#include <stddef.h>
#include <string.h>

_Static_assert(TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT *
                   TRIGGER_SEQUENCE_LINK_FRAGMENT_DATA_SIZE >=
                   TRIGGER_SEQUENCE_LINK_WIRE_SIZE,
               "sequence link fragments must cover the wire message");

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static void write_u32(uint8_t *p, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static uint32_t crc32(const uint8_t *bytes, uint32_t size)
{
    uint32_t crc = UINT32_MAX;
    for (uint32_t i = 0u; i < size; ++i) {
        crc ^= bytes[i];
        for (uint32_t bit = 0u; bit < 8u; ++bit)
            crc = (crc >> 1u) ^ ((crc & 1u) != 0u ? 0xEDB88320u : 0u);
    }
    return ~crc;
}

static bool valid_message(const trigger_sequence_link_message_t *message)
{
    return message != NULL &&
           (message->kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED ||
            message->kind == TRIGGER_SEQUENCE_LINK_READY_NEXT) &&
           message->run_id != 0u && message->generation != 0u &&
           message->binding_epoch != 0u &&
           message->source_slot != UINT32_MAX && message->target_slot != UINT32_MAX &&
           message->source_slot != message->target_slot;
}

bool trigger_sequence_link_tx_begin(trigger_sequence_link_tx_t *tx,
                                    const trigger_sequence_link_message_t *message,
                                    uint16_t token)
{
    if (tx == NULL || token == 0u || !valid_message(message)) return false;
    trigger_sequence_link_tx_t next = {0};
    write_u32(next.wire + 0u, message->kind);
    write_u32(next.wire + 4u, message->run_id);
    write_u32(next.wire + 8u, message->generation);
    write_u32(next.wire + 12u, message->binding_epoch);
    write_u32(next.wire + 16u, message->step_ordinal);
    write_u32(next.wire + 20u, message->source_slot);
    write_u32(next.wire + 24u, message->target_slot);
    write_u32(next.wire + 28u, crc32(next.wire, 28u));
    next.token = token;
    next.active = true;
    *tx = next;
    return true;
}

bool trigger_sequence_link_tx_next(trigger_sequence_link_tx_t *tx,
                                   uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE])
{
    if (tx == NULL || fragment == NULL || !tx->active || tx->token == 0u ||
        tx->next_fragment >= TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT) return false;
    memset(fragment, 0, TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE);
    fragment[0] = (uint8_t)tx->token;
    fragment[1] = (uint8_t)(tx->token >> 8u);
    fragment[2] = tx->next_fragment;
    fragment[3] = TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT;
    const uint32_t offset = tx->next_fragment * TRIGGER_SEQUENCE_LINK_FRAGMENT_DATA_SIZE;
    for (uint32_t i = 0u; i < TRIGGER_SEQUENCE_LINK_FRAGMENT_DATA_SIZE &&
         offset + i < TRIGGER_SEQUENCE_LINK_WIRE_SIZE; ++i)
        fragment[4u + i] = tx->wire[offset + i];
    tx->next_fragment = (uint8_t)((tx->next_fragment + 1u) %
                                TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT);
    return true;
}

void trigger_sequence_link_rx_init(trigger_sequence_link_rx_t *rx)
{
    if (rx != NULL) memset(rx, 0, sizeof(*rx));
}

trigger_sequence_link_rx_result_t trigger_sequence_link_rx_feed(
    trigger_sequence_link_rx_t *rx,
    const uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE],
    trigger_sequence_link_message_t *message)
{
    if (rx == NULL || fragment == NULL || message == NULL)
        return TRIGGER_SEQUENCE_LINK_RX_REJECTED;
    const uint16_t token = (uint16_t)((uint16_t)fragment[0] | ((uint16_t)fragment[1] << 8u));
    const uint32_t index = fragment[2];
    if (token == 0u || index >= TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT ||
        fragment[3] != TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT) {
        rx->received_mask = 0u;
        return TRIGGER_SEQUENCE_LINK_RX_REJECTED;
    }
    const uint32_t offset = index * TRIGGER_SEQUENCE_LINK_FRAGMENT_DATA_SIZE;
    const uint32_t remaining = TRIGGER_SEQUENCE_LINK_WIRE_SIZE - offset;
    const uint32_t count = remaining < TRIGGER_SEQUENCE_LINK_FRAGMENT_DATA_SIZE ?
        remaining : TRIGGER_SEQUENCE_LINK_FRAGMENT_DATA_SIZE;
    for (uint32_t i = count; i < TRIGGER_SEQUENCE_LINK_FRAGMENT_DATA_SIZE; ++i) {
        if (fragment[4u + i] != 0u) {
            rx->received_mask = 0u;
            return TRIGGER_SEQUENCE_LINK_RX_REJECTED;
        }
    }
    if (rx->token != token) {
        rx->token = token;
        rx->received_mask = 0u;
    }
    const uint8_t bit = (uint8_t)(1u << index);
    if ((rx->received_mask & bit) != 0u &&
        memcmp(rx->wire + offset, fragment + 4u, count) != 0) {
        rx->received_mask = 0u;
        return TRIGGER_SEQUENCE_LINK_RX_REJECTED;
    }
    memcpy(rx->wire + offset, fragment + 4u, count);
    rx->received_mask |= bit;
    if (rx->received_mask != (1u << TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT) - 1u)
        return TRIGGER_SEQUENCE_LINK_RX_INCOMPLETE;
    rx->received_mask = 0u;
    if (read_u32(rx->wire + 28u) != crc32(rx->wire, 28u))
        return TRIGGER_SEQUENCE_LINK_RX_REJECTED;
    const trigger_sequence_link_message_t parsed = {
        .kind = read_u32(rx->wire + 0u), .run_id = read_u32(rx->wire + 4u),
        .generation = read_u32(rx->wire + 8u), .binding_epoch = read_u32(rx->wire + 12u),
        .step_ordinal = read_u32(rx->wire + 16u), .source_slot = read_u32(rx->wire + 20u),
        .target_slot = read_u32(rx->wire + 24u),
    };
    if (!valid_message(&parsed)) return TRIGGER_SEQUENCE_LINK_RX_REJECTED;
    if (rx->last_message_valid &&
        memcmp(rx->last_message, rx->wire, TRIGGER_SEQUENCE_LINK_WIRE_SIZE) == 0)
        return TRIGGER_SEQUENCE_LINK_RX_DUPLICATE;
    memcpy(rx->last_message, rx->wire, TRIGGER_SEQUENCE_LINK_WIRE_SIZE);
    rx->last_message_valid = true;
    *message = parsed;
    return TRIGGER_SEQUENCE_LINK_RX_MESSAGE;
}
