#include "trigger_sequence_link_protocol.h"

#include <string.h>

_Static_assert(TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT == 1u &&
                   TRIGGER_SEQUENCE_LINK_FRAGMENT_DATA_SIZE ==
                       TRIGGER_SEQUENCE_LINK_WIRE_SIZE &&
                   TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE ==
                       TRIGGER_SEQUENCE_LINK_WIRE_SIZE,
               "sequence link message must fit one process-image field");

static uint32_t read_u32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8u) |
           ((uint32_t)p[2] << 16u) | ((uint32_t)p[3] << 24u);
}

static void write_u32(uint8_t *p, uint32_t value)
{
    for (uint32_t i = 0u; i < 4u; ++i) p[i] = (uint8_t)(value >> (8u * i));
}

static bool valid_message(const trigger_sequence_link_message_t *message)
{
    return message != NULL &&
           (message->kind == TRIGGER_SEQUENCE_LINK_LINK_APPLIED ||
            message->kind == TRIGGER_SEQUENCE_LINK_READY_NEXT ||
            message->kind == TRIGGER_SEQUENCE_LINK_COUNTER_NEXT) &&
           message->run_id != 0u && message->generation != 0u &&
           message->binding_epoch != 0u && message->exchange_id != 0u &&
           message->source_slot != UINT32_MAX && message->target_slot != UINT32_MAX &&
           message->source_slot != message->target_slot;
}

bool trigger_sequence_link_tx_begin(trigger_sequence_link_tx_t *tx,
                                    const trigger_sequence_link_message_t *message,
                                    uint16_t token)
{
    if (tx == NULL || token == 0u || !valid_message(message)) return false;
    trigger_sequence_link_tx_t next = {0};
    next.wire[0] = (uint8_t)message->kind;
    write_u32(next.wire + 1u, message->run_id);
    write_u32(next.wire + 5u, message->step_ordinal);
    next.wire[9] = (uint8_t)message->exchange_id;
    next.token = token;
    next.active = true;
    *tx = next;
    return true;
}

bool trigger_sequence_link_tx_next(trigger_sequence_link_tx_t *tx,
                                   uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE])
{
    if (tx == NULL || fragment == NULL || !tx->active || tx->token == 0u)
        return false;
    memcpy(fragment, tx->wire, TRIGGER_SEQUENCE_LINK_WIRE_SIZE);
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
    const trigger_sequence_link_message_t parsed = {
        .kind = fragment[0], .run_id = read_u32(fragment + 1u),
        .step_ordinal = read_u32(fragment + 5u), .exchange_id = fragment[9],
        .generation = 1u, .binding_epoch = 1u, .source_slot = 0u, .target_slot = 1u,
    };
    if ((parsed.kind != TRIGGER_SEQUENCE_LINK_LINK_APPLIED &&
         parsed.kind != TRIGGER_SEQUENCE_LINK_READY_NEXT &&
         parsed.kind != TRIGGER_SEQUENCE_LINK_COUNTER_NEXT) || parsed.run_id == 0u)
        return TRIGGER_SEQUENCE_LINK_RX_REJECTED;
    if (rx->last_message_valid &&
        memcmp(rx->last_message, fragment, TRIGGER_SEQUENCE_LINK_WIRE_SIZE) == 0)
        return TRIGGER_SEQUENCE_LINK_RX_DUPLICATE;
    memcpy(rx->last_message, fragment, TRIGGER_SEQUENCE_LINK_WIRE_SIZE);
    rx->last_message_valid = true;
    *message = parsed;
    return TRIGGER_SEQUENCE_LINK_RX_MESSAGE;
}
