#ifndef TDMA_RX_SCAN_H
#define TDMA_RX_SCAN_H

#include <stddef.h>
#include "tdma_rx_sequence.h"
#include "tdma_transport_frame.h"

#define TDMA_PIO_SPI_PACKET_MAGIC0 0x54u
#define TDMA_PIO_SPI_PACKET_MAGIC1 0x44u
#define TDMA_PIO_SPI_PACKET_HEADER_SIZE 4u
#define TDMA_PIO_SPI_FLIGHT_MAX_TAIL_BYTES 11u
#define TDMA_RX_SCAN_WINDOW_BYTES (TDMA_RX_OBSERVATION_SCAN_WORDS + \
    TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_SHORT_PACKET_MAX)

typedef enum {
    TDMA_RX_SCAN_IDLE = 0u,
    TDMA_RX_SCAN_REQUESTED,
    TDMA_RX_SCAN_BUILDING,
    TDMA_RX_SCAN_READY,
    TDMA_RX_SCAN_CANCELLED,
} tdma_rx_scan_state_t;

typedef struct {
    bool valid;
    uint32_t bit_shift, frame_words, stride_words, stable_frames;
    uint64_t candidate, observation_epoch;
} tdma_rx_scan_hint_t;

/* Core1 fills a private copy then publishes it. Core0 returns geometry only;
 * the hint cannot publish receive facts, bind a latch, or move wire slots. */
typedef struct {
    volatile uint32_t state;
    uint32_t epoch, request_epoch, persona, physical_frame_words;
    uint32_t byte_count, max_frame_words, tail_words;
    uint64_t window_start, observation_epoch;
    uint8_t bytes[TDMA_RX_SCAN_WINDOW_BYTES];
    tdma_rx_scan_hint_t result;
} tdma_rx_scan_t;

uint32_t tdma_rx_scan_state(const tdma_rx_scan_t *job);
bool tdma_rx_scan_request(tdma_rx_scan_t *job);
bool tdma_rx_scan_core0_claim(tdma_rx_scan_t *job);
void tdma_rx_scan_core0_build_claimed(tdma_rx_scan_t *job);
void tdma_rx_scan_core0_service(tdma_rx_scan_t *job);
bool tdma_rx_scan_cancel(tdma_rx_scan_t *job);
void tdma_rx_scan_release(tdma_rx_scan_t *job);
/* One direct location. No search and no permission to read unretired words. */
bool tdma_rx_scan_locate(const tdma_rx_scan_hint_t *hint, uint64_t produced,
    uint64_t cursor, uint64_t *candidate);

#endif
