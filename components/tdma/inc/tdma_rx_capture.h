#ifndef TDMA_RX_CAPTURE_H
#define TDMA_RX_CAPTURE_H

#include <stdint.h>

/* Diagnostic provenance of one immutable byte copy, not a physical event or
 * timestamp lease. Coordinates count DMA words (one wire byte per word).
 * IDs are unique only within this boot; they never wrap into a reused ID.
 * candidate/frame_words include the physical packet header stripped by rx;
 * bit_shift != 0 consumes one additional source word for the final byte.
 * PRIVATE_COPY proves copy/header checks only, not transport/mailbox CRC.
 * Only the owner's successful callback/station state gives these bytes a
 * lifetime; cancellation never authorizes reading a stale copied object. */
#define TDMA_RX_CAPTURE_PRIVATE_COPY 1u
typedef struct {
    uint64_t arm_epoch;
    uint64_t capture_id;
    uint64_t observation_epoch;
    uint64_t candidate;
    uint64_t produced_before;
    uint64_t produced_after;
    uint32_t frame_words;
    uint32_t bit_shift;
    uint32_t persona;
    uint32_t flags;
} tdma_rx_capture_t;

_Static_assert(sizeof(tdma_rx_capture_t) == 64u, "capture provenance must stay bounded");

#endif
