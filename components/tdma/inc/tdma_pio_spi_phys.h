#ifndef TDMA_PIO_SPI_PHYS_H
#define TDMA_PIO_SPI_PHYS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_ring_runtime.h"
#include "tdma_transport_frame.h"

/* TDMA PIO SPI resident physical layer (bring-up, 3-wire no-CS).
 *
 * Architecture contract (docs/refmem/REFMEM_DOMAIN_TODO.md P0 B2/B3 and
 * docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md P6): the minimum-system ring uses a
 * no-CS 3-wire PIO SPI uplink/downlink pair per board, 25 MHz, half duplex
 * per direction. "downlink" sends data to the next board (master SM), and
 * "uplink" receives data from the previous board (slave SM). Both SMs are
 * armed together and stay resident; the core1 TDMA service drives frame-level
 * TX/RX without host maintenance commands.
 *
 * This layer is byte/transport level only: it carries the 4-byte packet
 * header (magic + length) plus the TdmaTransportFrame body and never parses
 * VDC/RefMem inner frames.
 *
 * Verified pin set (COM5=A slot 0, COM6=B slot 1, docs/refmem/
 * REFMEM_TASK_PROGRESS.md REFMEM-TASK-20260816-050):
 *   slot 0: uplink (rx=16, sck=18, tx=23), downlink (rx=16, sck=22, tx=23)
 *   slot 1: uplink (rx=16, sck=18, tx=23), downlink (rx=16, sck=21, tx=23)
 */

#define TDMA_PIO_SPI_PACKET_MAGIC0 0x54u
#define TDMA_PIO_SPI_PACKET_MAGIC1 0x44u
#define TDMA_PIO_SPI_PACKET_HEADER_SIZE 4u
#define TDMA_PIO_SPI_RX_DMA_WORD_MAX \
    (TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_SHORT_PACKET_MAX)
#define TDMA_PIO_SPI_RX_STABLE_1E3NS 1000u
#define TDMA_PIO_SPI_DEFAULT_TIMEOUT_1E6NS 1000u

typedef enum {
    TDMA_PIO_SPI_PHYS_ERROR_NONE = 0u,
    TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT = 1u,
    TDMA_PIO_SPI_PHYS_ERROR_BAD_ROLE = 2u,
    TDMA_PIO_SPI_PHYS_ERROR_TIMEOUT = 3u,
    TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET = 4u,
    TDMA_PIO_SPI_PHYS_ERROR_PAYLOAD_TOO_LARGE = 5u,
    TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY = 6u,
} tdma_pio_spi_phys_error_t;

typedef struct {
    uint32_t armed;
    uint32_t role;               /* 0 = slot 0, 1 = slot 1 (downlink SCK select). */
    uint32_t baud_hz;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t rx_bad_count;
    uint32_t rx_timeout_count;
    uint32_t tx_busy_count;
    uint32_t last_error;
    uint32_t last_tx_size;
    uint32_t last_rx_size;
    uint64_t last_rx_timestamp_ns;
    uint32_t downlink_sck_pin;
    uint32_t downlink_tx_pin;
    uint32_t uplink_rx_pin;
    uint32_t uplink_sck_pin;
} tdma_pio_spi_phys_snapshot_t;

typedef struct {
    bool armed;
    uint32_t role;
    uint32_t baud_hz;
    uint32_t downlink_sck_pin;
    uint32_t downlink_tx_pin;
    uint32_t uplink_rx_pin;
    uint32_t uplink_sck_pin;
    tdma_pio_spi_phys_snapshot_t snapshot;
    bool rx_capture_active;
    size_t rx_capture_max_words;
    uint32_t rx_capture_last_remaining;
    uint64_t rx_capture_deadline_1e3ns;
    uint64_t rx_capture_last_change_1e3ns;
} tdma_pio_spi_phys_t;

/* Called by the ring adapter start() once the active ring config is known
 * (local_slot_id selects the downlink SCK pin). */
bool tdma_pio_spi_phys_arm(void *context,
                           const tdma_ring_runtime_config_t *config);
void tdma_pio_spi_phys_disarm(void *context);
bool tdma_pio_spi_phys_get_snapshot(const tdma_pio_spi_phys_t *phys,
                                    tdma_pio_spi_phys_snapshot_t *snapshot);

/* phys_tx: push one complete packet (header + TdmaTransportFrame) through the
 * resident master SM. On success *tx_timestamp_ns may be filled with a
 * physical timestamp (0 = none available). */
bool tdma_pio_spi_phys_tx(void *context,
                          const uint8_t *packet,
                          size_t packet_size,
                          uint64_t *tx_timestamp_ns);
/* phys_rx: pull one complete packet received by the resident slave SM. */
bool tdma_pio_spi_phys_rx(void *context,
                          uint8_t *packet,
                          size_t packet_capacity,
                          size_t *packet_size,
                          uint64_t *rx_timestamp_ns);

#endif
