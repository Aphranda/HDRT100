#ifndef TRIGGER_SEQUENCE_LINK_PROTOCOL_H
#define TRIGGER_SEQUENCE_LINK_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

/* Versioned application tag; transport owns header/VDC/ACK and verifies the
 * physical source before dispatching these opaque RefMem-region fragments. */
#define TRIGGER_SEQUENCE_LINK_CONTROL_OPCODE 0x53u
#define TRIGGER_SEQUENCE_LINK_WIRE_SIZE 10u
#define TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE 10u
#define TRIGGER_SEQUENCE_LINK_FRAGMENT_DATA_SIZE 10u
#define TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT 1u

typedef enum {
    TRIGGER_SEQUENCE_LINK_LINK_APPLIED = 1u,
    TRIGGER_SEQUENCE_LINK_READY_NEXT = 2u,
    TRIGGER_SEQUENCE_LINK_COUNTER_NEXT = 3u,
} trigger_sequence_link_kind_t;

typedef struct {
    uint32_t kind;
    uint32_t run_id;
    uint32_t generation;
    uint32_t binding_epoch;
    uint32_t step_ordinal;
    uint32_t source_slot;
    uint32_t target_slot;
    uint32_t exchange_id;
} trigger_sequence_link_message_t;

typedef struct {
    uint8_t wire[TRIGGER_SEQUENCE_LINK_WIRE_SIZE];
    uint16_t token;
    bool active;
} trigger_sequence_link_tx_t;

typedef struct {
    uint8_t last_message[TRIGGER_SEQUENCE_LINK_WIRE_SIZE];
    bool last_message_valid;
} trigger_sequence_link_rx_t;

typedef enum {
    TRIGGER_SEQUENCE_LINK_RX_INCOMPLETE = 0u,
    TRIGGER_SEQUENCE_LINK_RX_MESSAGE,
    TRIGGER_SEQUENCE_LINK_RX_DUPLICATE,
    TRIGGER_SEQUENCE_LINK_RX_REJECTED,
} trigger_sequence_link_rx_result_t;

/* Failure leaves tx unchanged. The compact wire carries kind, exact run and
 * step, plus the low exchange byte. Generation, binding, full exchange and
 * route are reconstructed only from the frozen Core1 run after those fields
 * match. The process-image CRC and transport CRC protect the complete frame.
 * Caller token remains a local replacement hint, never authorization. */
bool trigger_sequence_link_tx_begin(trigger_sequence_link_tx_t *tx,
                                    const trigger_sequence_link_message_t *message,
                                    uint16_t token);
bool trigger_sequence_link_tx_next(trigger_sequence_link_tx_t *tx,
                                   uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE]);
void trigger_sequence_link_rx_init(trigger_sequence_link_rx_t *rx);
/* A valid compact frame does not authorize an action: the link owner checks
 * the frozen identity, route and phase before execution. Repeated frames are
 * deduplicated. message is written only on RX_MESSAGE. */
trigger_sequence_link_rx_result_t trigger_sequence_link_rx_feed(
    trigger_sequence_link_rx_t *rx,
    const uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE],
    trigger_sequence_link_message_t *message);

#endif
