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
 *   - TX leg (SPI master, downlink): drives SCK + data out toward the next
 *     board in the ring (C_n -> C_{n+1}). One TX direction only.
 *   - RX leg (SPI slave, uplink): follows the SCK driven by the previous
 *     board and samples the data coming from it (C_{n-1} -> C_n). One RX
 *     direction only.
 * The reference board (slot == reference_slot) originates one IDLE_BEACON per
 * TDMA cycle on its TX leg; every follower receives on its RX leg, advances
 * the hop and re-emits on its TX leg, so the frame travels once around the
 * ring (C1 -> C2 -> C3 -> ... -> C1) and the reference receives its own
 * frame back on the RX leg.
 *
 * Measured min-system wiring (tools/tdma_ring_monitor/line_map_check.py):
 *   A(slot0) downlink:  SCK=22 -> B.18, TX=23 -> B.16 (B uplink RX)
 *   B(slot1) downlink:  SCK=19 -> A.18, TX=23 -> A.16 (A uplink RX)
 * Both boards: uplink SCK=18, uplink RX=16, downlink TX=23. Only the
 * downlink SCK pin is slot-selected (slot0=22, slot1=19).
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
#define TDMA_PIO_SPI_RX_STABLE_1E3NS 1000u

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
    uint32_t tx_pin;
    uint32_t rx_sck_pin;
    uint32_t rx_pin;
} tdma_pio_spi_phys_snapshot_t;

typedef struct {
    bool armed;
    uint32_t role;
    uint32_t baud_hz;
    /* Downlink TX leg (SPI master, drives SCK + data toward next board). */
    uint32_t tx_sm;
    uint32_t tx_sck_pin;
    uint32_t tx_pin;
    /* Uplink RX leg (SPI slave, follows previous board's SCK). */
    uint32_t rx_sm;
    uint32_t rx_sck_pin;
    uint32_t rx_pin;
    tdma_pio_spi_phys_snapshot_t snapshot;
    bool rx_capture_active;
    size_t rx_capture_max_words;
    uint32_t rx_capture_last_remaining;
    uint64_t rx_capture_last_change_1e3ns;
} tdma_pio_spi_phys_t;

/* Called by the ring adapter start() once the active ring config is known.
 * Both legs (downlink TX master + uplink RX slave) are armed together; the
 * downlink SCK pin is selected by the local ring slot. */
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
