#ifndef REFMEM_BOOTSTRAP_RELAY_H
#define REFMEM_BOOTSTRAP_RELAY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Pre-ring REFMEM control relay over the half-duplex P4.5 SPI link.
 *
 * The physical adapter's role is fixed at arm time (MASTER transmits, SLAVE
 * receives), so the relay alternates: TX probe as MASTER, then RX as SLAVE.
 * A peer becomes ready only after its own valid probe/ack frame is received;
 * an ARM success is never treated as readiness.  The probe frame format is
 * injected by the binding (HELLO in firmware, a fake in host tests), so this
 * state machine stays transport- and wire-agnostic. */

#define REFMEM_BOOTSTRAP_RELAY_FRAME_MAX 256u

typedef bool (*refmem_bootstrap_build_probe_fn)(
    uint8_t source_slot, uint8_t target_slot, uint32_t seq, uint8_t *frame,
    size_t frame_capacity, size_t *frame_size);
typedef bool (*refmem_bootstrap_parse_probe_fn)(
    const uint8_t *frame, size_t frame_size, uint8_t *source_slot,
    bool *is_ack);
/* Receives an application frame carried by the relay (command payload or
 * command reply envelope).  Implemented by the REFMEM binding; the relay
 * stays wire-agnostic. */
typedef bool (*refmem_bootstrap_frame_handler_fn)(const uint8_t *frame,
                                                  size_t frame_size);

typedef struct {
    uint8_t local_slot;
    uint8_t peer_slot;
    uint32_t hello_interval_ms;
    uint32_t ack_timeout_ms;
    uint32_t peer_timeout_ms;
    uint32_t reply_guard_ms;
    uint32_t reply_timeout_ms;
    uint8_t max_retries;
    bool responder;
    refmem_bootstrap_build_probe_fn build_probe;
    refmem_bootstrap_parse_probe_fn parse_probe;
    refmem_bootstrap_frame_handler_fn frame_handler;
} refmem_bootstrap_relay_config_t;

typedef enum {
    REFMEM_BOOTSTRAP_REASON_NONE = 0u,
    REFMEM_BOOTSTRAP_REASON_BUILD_FAIL = 1u,
    REFMEM_BOOTSTRAP_REASON_TX_FAIL = 2u,
    REFMEM_BOOTSTRAP_REASON_ACK_TIMEOUT = 3u,
    REFMEM_BOOTSTRAP_REASON_INVALID_FRAME = 4u,
    REFMEM_BOOTSTRAP_REASON_PEER_TIMEOUT = 5u,
    REFMEM_BOOTSTRAP_REASON_RING_HANDOVER = 6u,
} refmem_bootstrap_reason_t;

typedef enum {
    REFMEM_BOOTSTRAP_RELAY_IDLE = 0u,
    REFMEM_BOOTSTRAP_RELAY_WAIT_ACK = 1u,
    REFMEM_BOOTSTRAP_RELAY_LISTEN = 2u,
    REFMEM_BOOTSTRAP_RELAY_REPLY_WAIT = 3u,
    REFMEM_BOOTSTRAP_RELAY_WAIT_REPLY = 4u,
} refmem_bootstrap_relay_state_t;

void refmem_bootstrap_relay_reset(void);
bool refmem_bootstrap_relay_init(const refmem_bootstrap_relay_config_t *config);
/* One non-blocking step; call from the sole Core0 RefMem task. */
void refmem_bootstrap_relay_service(uint32_t now_ms);
/* Carries one application frame (synchronous TX).  Requires the peer to have
 * been proven ready; the initiator then waits for a reply frame, the responder
 * returns to listening.  Returns false when the link is not available. */
bool refmem_bootstrap_relay_submit(const uint8_t *frame, size_t frame_size,
                                   uint32_t now_ms);
bool refmem_bootstrap_relay_ready(void);
refmem_bootstrap_relay_state_t refmem_bootstrap_relay_state(void);
uint8_t refmem_bootstrap_relay_peer_slot(void);
uint32_t refmem_bootstrap_relay_last_reason(void);

/* Ring became ready: retire the bootstrap carrier without clearing counters.
 * Readiness is dropped immediately and the reason is latched for evidence. */
void refmem_bootstrap_relay_handover(void);

uint32_t refmem_bootstrap_relay_tx_count(void);
uint32_t refmem_bootstrap_relay_rx_count(void);
uint32_t refmem_bootstrap_relay_timeout_count(void);
uint32_t refmem_bootstrap_relay_reply_timeout_count(void);
uint32_t refmem_bootstrap_relay_retry_count(void);
uint32_t refmem_bootstrap_relay_invalid_count(void);
uint32_t refmem_bootstrap_relay_build_fail_count(void);
uint32_t refmem_bootstrap_relay_tx_fail_count(void);

#endif /* REFMEM_BOOTSTRAP_RELAY_H */
