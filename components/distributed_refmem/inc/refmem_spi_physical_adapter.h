#ifndef REFMEM_SPI_PHYSICAL_ADAPTER_H
#define REFMEM_SPI_PHYSICAL_ADAPTER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    REFMEM_SPI_PHYSICAL_ROLE_DISABLED = 0u,
    REFMEM_SPI_PHYSICAL_ROLE_MASTER = 1u,
    REFMEM_SPI_PHYSICAL_ROLE_SLAVE = 2u,
} refmem_spi_physical_role_t;

typedef struct {
    uint32_t armed;
    uint32_t role;
    uint32_t baud_hz;
    uint32_t tx_count;
    uint32_t rx_count;
    uint32_t timeout_count;
    uint32_t bad_packet_count;
    uint32_t drop_count;
    uint32_t last_error;
    uint32_t last_tx_size;
    uint32_t last_rx_size;
    uint32_t rx_pin;
    uint32_t csn_pin;
    uint32_t sck_pin;
    uint32_t tx_pin;
} refmem_spi_physical_snapshot_t;

typedef struct {
    uint32_t rx_pin;
    uint32_t csn_pin;
    uint32_t sck_pin;
    uint32_t tx_pin;
} refmem_spi_physical_pin_config_t;

typedef enum {
    REFMEM_SPI_PHYSICAL_RX_POLL_PENDING = 0u,
    REFMEM_SPI_PHYSICAL_RX_POLL_DONE = 1u,
    REFMEM_SPI_PHYSICAL_RX_POLL_ERROR = 2u,
} refmem_spi_physical_rx_poll_result_t;

typedef struct {
    bool armed;
    refmem_spi_physical_role_t role;
    uint32_t baud_hz;
    refmem_spi_physical_pin_config_t pins;
    refmem_spi_physical_snapshot_t snapshot;
    bool rx_capture_active;
    bool rx_capture_wait_full;
    size_t rx_capture_max_words;
    uint32_t rx_capture_last_remaining;
    uint64_t rx_capture_deadline_us;
    uint64_t rx_capture_last_change_us;
} refmem_spi_physical_adapter_t;

bool refmem_spi_physical_adapter_arm(refmem_spi_physical_adapter_t *adapter,
                                     refmem_spi_physical_role_t role,
                                     uint32_t baud_hz,
                                     const refmem_spi_physical_pin_config_t *pins);
void refmem_spi_physical_adapter_disarm(refmem_spi_physical_adapter_t *adapter);
bool refmem_spi_physical_adapter_transmit(refmem_spi_physical_adapter_t *adapter,
                                          const uint8_t *frame,
                                          size_t frame_size);
bool refmem_spi_physical_adapter_transmit_raw(refmem_spi_physical_adapter_t *adapter,
                                              uint8_t seed,
                                              size_t byte_count);
bool refmem_spi_physical_adapter_receive(refmem_spi_physical_adapter_t *adapter,
                                         uint8_t *frame,
                                         size_t frame_capacity,
                                         size_t *frame_size,
                                         uint32_t timeout_ms);
bool refmem_spi_physical_adapter_receive_begin(refmem_spi_physical_adapter_t *adapter,
                                               size_t frame_capacity,
                                               uint32_t timeout_ms);
refmem_spi_physical_rx_poll_result_t refmem_spi_physical_adapter_receive_poll(
    refmem_spi_physical_adapter_t *adapter,
    uint8_t *frame,
    size_t frame_capacity,
    size_t *frame_size);
bool refmem_spi_physical_adapter_receive_raw(refmem_spi_physical_adapter_t *adapter,
                                             uint8_t *buffer,
                                             size_t expected_size,
                                             size_t *received_size,
                                             uint32_t timeout_ms);
void refmem_spi_physical_line_release(void);
bool refmem_spi_physical_line_drive(uint32_t line_index, bool level);
uint32_t refmem_spi_physical_line_sample(void);
bool refmem_spi_physical_adapter_get_snapshot(
    const refmem_spi_physical_adapter_t *adapter,
    refmem_spi_physical_snapshot_t *snapshot);

#endif
