#include "calibration_pio_loopback.h"

#include "tdma_runtime_owner.h"

bool calibration_pio_loopback_init(void)
{
    return true;
}

bool calibration_pio_loopback_request_start(
    const calibration_pio_loopback_config_t *config)
{
    if (config == NULL) {
        return false;
    }
    return tdma_runtime_owner_cal_loopback_start(config->sample_hz,
                                                 config->sample_words,
                                                 config->epoch);
}

void calibration_pio_loopback_request_stop(void)
{
    tdma_runtime_owner_cal_loopback_stop();
}

bool calibration_pio_loopback_service_core1(bool tdma_stopped)
{
    (void)tdma_stopped;
    return tdma_runtime_owner_cal_loopback_service();
}

bool calibration_pio_loopback_get_snapshot(
    calibration_pio_loopback_snapshot_t *snapshot)
{
    tdma_pio_spi_cal_loopback_snapshot_t raw;
    if (snapshot == NULL ||
        !tdma_runtime_owner_get_cal_loopback_snapshot(&raw)) {
        return false;
    }
    snapshot->armed = raw.armed;
    snapshot->complete = raw.complete;
    snapshot->pio_block = 2u;
    snapshot->state_machine = 0u;
    snapshot->dma_channel = TDMA_PIO_SPI_RX_DMA_CHANNEL;
    snapshot->sample_hz = raw.sample_hz;
    snapshot->sample_period_ns = raw.sample_period_ns;
    snapshot->produced_words = raw.produced_words;
    snapshot->edge_mask = raw.edge_mask;
    snapshot->flags = raw.flags;
    snapshot->reject_reason = raw.reject_reason;
    snapshot->epoch = raw.epoch;
    snapshot->t1_clk_tx = raw.t1_clk_tx;
    snapshot->t2_clk_rx = raw.t2_clk_rx;
    snapshot->t3_data_tx = raw.t3_data_tx;
    snapshot->t4_data_rx = raw.t4_data_rx;
    return true;
}
