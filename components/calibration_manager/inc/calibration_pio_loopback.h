#ifndef CALIBRATION_PIO_LOOPBACK_H
#define CALIBRATION_PIO_LOOPBACK_H

#include <stdbool.h>
#include <stdint.h>

/* PIO/DMA monitor for the product-board TDMA three-line loopback.  The
 * monitor is deliberately read-only: TDMA remains the owner of SM0/SM1 and
 * the calibration monitor only claims its declared capture SM while STOPPED. */
typedef struct {
    uint32_t sample_hz;
    uint32_t sample_words;
    uint32_t epoch;
} calibration_pio_loopback_config_t;

typedef struct {
    uint32_t armed;
    uint32_t complete;
    uint32_t pio_block;
    uint32_t state_machine;
    uint32_t dma_channel;
    uint32_t sample_hz;
    uint32_t sample_period_ns;
    uint32_t produced_words;
    uint32_t edge_mask;
    uint32_t flags;
    uint32_t reject_reason;
    uint32_t epoch;
    uint64_t t1_clk_tx;
    uint64_t t2_clk_rx;
    uint64_t t3_data_tx;
    uint64_t t4_data_rx;
} calibration_pio_loopback_snapshot_t;

bool calibration_pio_loopback_init(void);
bool calibration_pio_loopback_request_start(
    const calibration_pio_loopback_config_t *config);
void calibration_pio_loopback_request_stop(void);
void calibration_pio_loopback_service_core1(bool tdma_stopped);
bool calibration_pio_loopback_get_snapshot(
    calibration_pio_loopback_snapshot_t *snapshot);

#endif
