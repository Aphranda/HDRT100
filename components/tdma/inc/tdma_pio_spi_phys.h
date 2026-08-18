#ifndef TDMA_PIO_SPI_PHYS_H
#define TDMA_PIO_SPI_PHYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_ring_runtime.h"
#include "tdma_transport_frame.h"

/* TDMA PIO SPI resident physical layer (bring-up, half-duplex ring).
 *
 * P0.5-3 topology (ring + half duplex, one RX leg + one TX leg per board):
 * every board carries two independent PIO SMs:
 *   - TX leg (SPI master, downlink): drives frame-sync/CS + SCK + data out
 *     toward the next board in the ring (C_n -> C_{n+1}). One TX direction
 *     only.
 *   - RX leg (SPI slave, uplink): follows the frame-sync/CS and SCK driven by
 *     the previous board and samples the data coming from it (C_{n-1} -> C_n).
 *     One RX direction only.
 * The reference board (slot == reference_slot) originates one IDLE_BEACON per
 * TDMA cycle on its TX leg; every follower receives on its RX leg, advances
 * the hop and re-emits on its TX leg, so the frame travels once around the
 * ring (C1 -> C2 -> C3 -> ... -> C1) and the reference receives its own
 * frame back on the RX leg.
 *
 * Measured min-system wiring (tools/tdma_ring_monitor/line_map_check.py):
 *   Cn downlink: CS=21, TX=23, SCK=24 -> Cn+1 uplink: CS=16, RX=18, SCK=19.
 * The TX-side CS is a point-to-point frame-sync signal, not a bus chip select.
 *
 * This layer is byte/transport level only: it carries the 4-byte packet
 * header (magic + length) plus the TdmaTransportFrame body and never parses
 * VDC/RefMem inner frames.
 */

#define TDMA_PIO_SPI_PACKET_MAGIC0 0x54u
#define TDMA_PIO_SPI_PACKET_MAGIC1 0x44u
#define TDMA_PIO_SPI_PACKET_HEADER_SIZE 4u
#define TDMA_PIO_SPI_RX_DMA_WORD_MAX \
    (TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_SHORT_PACKET_MAX)
#define TDMA_PIO_SPI_RX_STABLE_US 1000u

/* Continuous RX capture ring (EtherCAT-style): the DMA stays armed for the
 * entire session and wraps its write address in SRAM. The CPU only scans
 * completed words for the outer magic/length header, so there is no
 * per-frame abort, FIFO clear, or DMA reconfiguration gap. */
#define TDMA_PIO_SPI_RX_RING_WORDS 512u
#define TDMA_PIO_SPI_RX_RING_LOG2 11u
#define TDMA_PIO_SPI_RX_DMA_CHANNEL 4u
#define TDMA_PIO_SPI_FRAME_WORDS \
    (TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_HEADER_SIZE)

typedef enum {
    TDMA_PIO_SPI_PHYS_ERROR_NONE = 0u,
    TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT = 1u,
    TDMA_PIO_SPI_PHYS_ERROR_BAD_ROLE = 2u,
    TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET = 3u,
    TDMA_PIO_SPI_PHYS_ERROR_PAYLOAD_TOO_LARGE = 4u,
    TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY = 5u,
} tdma_pio_spi_phys_error_t;

typedef enum {
    TDMA_PIO_SPI_ROLE_MASTER = 0u,
    TDMA_PIO_SPI_ROLE_SLAVE = 1u,
} tdma_pio_spi_role_t;

typedef struct {
    uint32_t armed;
    uint32_t role;
    uint32_t baud_hz;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t rx_bad_count;
    uint32_t tx_busy_count;
    uint32_t last_error;
    uint32_t last_tx_size;
    uint32_t last_rx_size;
    uint64_t last_rx_timestamp_ns;
    uint32_t tx_sck_pin;
    uint32_t tx_csn_pin;
    uint32_t tx_pin;
    uint32_t rx_sck_pin;
    uint32_t rx_csn_pin;
    uint32_t rx_pin;
    /* RX capture diagnostics (bring-up): how often the DMA capture produced
     * a partial frame, how often the rx_byte SM stalled on a full RX FIFO,
     * and how often the bounded TX put timed out. */
    uint32_t rx_partial_count;
    uint32_t rx_stall_count;
    uint32_t tx_timeout_count;
    /* Last rejected frame header words and received word count, to identify
     * byte-boundary drift (a shifted magic instead of a corrupted payload). */
    uint32_t last_bad_header0;
    uint32_t last_bad_header1;
    uint32_t last_bad_header2;
    uint32_t last_bad_header3;
    uint32_t last_bad_words;
    /* Capture path diagnostics: how often the service saw the DMA still
     * busy (frame incomplete) vs completed with a shifted magic. */
    uint32_t rx_busy_count;
    uint32_t rx_magic_fail_count;
    /* Partial words visible in the DMA buffer while busy (to identify the
     * byte-boundary drift pattern). */
    uint32_t rx_busy_word0;
    uint32_t rx_busy_word1;
    uint32_t rx_busy_word2;
    uint32_t rx_busy_word3;
    uint32_t rx_busy_moved; /* words moved so far while busy. */
    /* Magic alignment distribution of completed captures: 0 = frame header
     * at buffer start (aligned), 1..35 = header shifted (capture started
     * mid-frame), plus magic_fail_count for header-not-found. */
    uint32_t rx_magic_at_zero;
    uint32_t rx_magic_at_shift;
    uint32_t rx_ring_overrun_count;
    uint32_t rx_dma_produced_words;
    uint32_t rx_scan_produced_words;
    uint32_t rx_dma_write_index;
    uint32_t rx_dma_channel;
} tdma_pio_spi_phys_snapshot_t;

typedef struct {
    bool armed;
    uint32_t role;
    uint32_t baud_hz;
    /* Downlink TX leg (SPI master, drives CS + SCK + data toward next board). */
    uint32_t tx_sm;
    uint32_t tx_sck_pin;
    uint32_t tx_csn_pin;
    uint32_t tx_pin;
    /* Uplink RX leg (SPI slave, follows previous board's CS + SCK). */
    uint32_t rx_sm;
    uint32_t rx_sck_pin;
    uint32_t rx_csn_pin;
    uint32_t rx_pin;
    tdma_pio_spi_phys_snapshot_t snapshot;
    bool rx_capture_active;
    size_t rx_capture_max_words;
    uint32_t rx_capture_last_remaining;
    uint64_t rx_capture_last_change_us;
} tdma_pio_spi_phys_t;

/* Called by the ring adapter start() once the active ring config is known.
 * Both legs (downlink TX master + uplink RX slave) are armed together. */
bool tdma_pio_spi_phys_arm(void *context,
                           const tdma_ring_runtime_config_t *config);
void tdma_pio_spi_phys_disarm(void *context);
bool tdma_pio_spi_phys_get_snapshot(const tdma_pio_spi_phys_t *phys,
                                    tdma_pio_spi_phys_snapshot_t *snapshot);

/* phys_tx: push one complete packet (header + TdmaTransportFrame) into the TX
 * leg FIFO. The downlink SM shifts it out on the TX pin toward the next board
 * while driving SCK. */
bool tdma_pio_spi_phys_tx(void *context,
                          const uint8_t *packet,
                          size_t packet_size,
                          uint64_t *tx_timestamp_ns);
/* phys_rx: pull one complete packet received on the uplink RX leg from the
 * previous board. Non-blocking: it arms the RX DMA on the first call and
 * returns the frame only once the fixed-length capture has arrived. */
bool tdma_pio_spi_phys_rx(void *context,
                          uint8_t *packet,
                          size_t packet_capacity,
                          size_t *packet_size,
                          uint64_t *rx_timestamp_ns);

#endif
