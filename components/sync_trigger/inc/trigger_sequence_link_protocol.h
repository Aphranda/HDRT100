#ifndef TRIGGER_SEQUENCE_LINK_PROTOCOL_H
#define TRIGGER_SEQUENCE_LINK_PROTOCOL_H

#include <stdbool.h>
#include <stdint.h>

/* Versioned application tag; transport owns header/VDC/ACK and verifies the
 * physical source before dispatching these opaque RefMem-region fragments. */
#define TRIGGER_SEQUENCE_LINK_CONTROL_OPCODE 0x53u
#define TRIGGER_SEQUENCE_LINK_WIRE_SIZE 36u
#define TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE 10u
#define TRIGGER_SEQUENCE_LINK_FRAGMENT_DATA_SIZE 6u
#define TRIGGER_SEQUENCE_LINK_FRAGMENT_COUNT 6u

typedef enum {
    TRIGGER_SEQUENCE_LINK_LINK_APPLIED = 1u,
    TRIGGER_SEQUENCE_LINK_READY_NEXT = 2u,
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
    uint8_t next_fragment;
    bool active;
} trigger_sequence_link_tx_t;

typedef struct {
    uint8_t wire[TRIGGER_SEQUENCE_LINK_WIRE_SIZE];
    uint8_t last_message[TRIGGER_SEQUENCE_LINK_WIRE_SIZE];
    uint16_t token;
    uint8_t received_mask;
    bool last_message_valid;
} trigger_sequence_link_rx_t;

typedef enum {
    TRIGGER_SEQUENCE_LINK_RX_INCOMPLETE = 0u,
    TRIGGER_SEQUENCE_LINK_RX_MESSAGE,
    TRIGGER_SEQUENCE_LINK_RX_DUPLICATE,
    TRIGGER_SEQUENCE_LINK_RX_REJECTED,
} trigger_sequence_link_rx_result_t;

/* Failure leaves tx unchanged. Caller supplies a nonzero token and changes it
 * for each new message; a token is only an assembly hint, never authorization.
 * tx_next repeats the immutable message until tx_begin replaces it or the
 * owner clears tx. No retransmission timer or peer state is owned here. */
bool trigger_sequence_link_tx_begin(trigger_sequence_link_tx_t *tx,
                                    const trigger_sequence_link_message_t *message,
                                    uint16_t token);
bool trigger_sequence_link_tx_next(trigger_sequence_link_tx_t *tx,
                                   uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE]);
void trigger_sequence_link_rx_init(trigger_sequence_link_rx_t *rx);
/* One assembler per physical source stream. Fragments may repeat/reorder or
 * disappear. A new token replaces incomplete assembly. A CRC-valid message
 * does not authorize an action: owner checks route/run/generation/epoch/step
 * and preserves its own dedup history across messages and token wrap.
 * message is written only on RX_MESSAGE. All storage is caller-owned. */
trigger_sequence_link_rx_result_t trigger_sequence_link_rx_feed(
    trigger_sequence_link_rx_t *rx,
    const uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE],
    trigger_sequence_link_message_t *message);

#endif
