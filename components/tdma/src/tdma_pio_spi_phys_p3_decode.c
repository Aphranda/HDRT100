#include "tdma_pio_spi_phys_internal.h"

/* P3 capture is a diagnostic decoder. It consumes the immutable DMA sample
 * ring after capture has stopped; no PIO, DMA, or GPIO operation belongs here. */
extern uint32_t s_tdma_pio_spi_cal_ring[TDMA_PIO_SPI_CAL_LOOPBACK_MAX_WORDS];

uint32_t tdma_pio_spi_cal_sample_byte(uint32_t word, uint32_t index)
{
    return (word >> (index * 8u)) & 0xFFu;
}

void tdma_pio_spi_phys_p3_decode(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL) {
        return;
    }
    uint32_t previous = 0u;
    bool have_previous = false;
    uint32_t found = 0u;
    uint64_t times[4] = {0u, 0u, 0u, 0u};
    uint64_t clock_rise = 0u;
    uint64_t clock_fall = 0u;
    uint64_t clock_high_sum = 0u;
    uint64_t clock_low_sum = 0u;
    uint32_t clock_high_count = 0u;
    uint32_t clock_low_count = 0u;
    uint64_t data_rise = 0u;
    uint64_t data_high_sum = 0u;
    uint32_t data_high_count = 0u;
    bool have_clock_rise = false;
    bool have_clock_fall = false;
    bool have_data_rise = false;
    /* signal_group selects the physical line used for the forward leg. */
    const bool forward_is_cs = phys->p3.signal_group ==
        TDMA_PIO_SPI_P3_GROUP_CS_DATA;
    const uint32_t forward_mask = phys->p3.role ==
        TDMA_PIO_SPI_P3_ROLE_INITIATOR
            ? (1u << (forward_is_cs ? 2u : 1u))
            : (1u << (forward_is_cs ? 3u : 4u));
    const uint32_t data_mask = phys->p3.role ==
        TDMA_PIO_SPI_P3_ROLE_INITIATOR ? (1u << 0u) : (1u << 5u);
    const uint32_t period = phys->p3.sample_period_ns;
    for (uint32_t w = 0u; w < phys->p3.requested_words; w++) {
        for (uint32_t i = 0u; i < 4u; i++) {
            const uint32_t sample = tdma_pio_spi_cal_sample_byte(
                s_tdma_pio_spi_cal_ring[w], i);
            if (!have_previous) {
                previous = sample;
                have_previous = true;
                continue;
            }
            const uint32_t rising = sample & ~previous;
            const uint32_t falling = previous & ~sample;
            const uint64_t timestamp =
                ((uint64_t)w * 4ull + i) * period;
            if ((rising & forward_mask) != 0u) {
                if (!have_clock_rise) {
                    clock_rise = timestamp;
                    have_clock_rise = true;
                }
                if (have_clock_fall) {
                    clock_low_sum += timestamp - clock_fall;
                    clock_low_count++;
                    have_clock_fall = false;
                }
                clock_rise = timestamp;
            }
            if ((falling & forward_mask) != 0u && have_clock_rise) {
                clock_fall = timestamp;
                have_clock_fall = true;
                clock_high_sum += timestamp - clock_rise;
                clock_high_count++;
            }
            if ((rising & data_mask) != 0u) {
                data_rise = timestamp;
                have_data_rise = true;
            }
            if ((falling & data_mask) != 0u && have_data_rise) {
                data_high_sum += timestamp - data_rise;
                data_high_count++;
                have_data_rise = false;
            }
            if (phys->p3.role == TDMA_PIO_SPI_P3_ROLE_INITIATOR) {
                if ((rising & forward_mask) != 0u && (found & 1u) == 0u) {
                    times[0] = timestamp;
                    found |= 1u;
                }
                if ((rising & (1u << 0u)) != 0u && (found & 8u) == 0u) {
                    times[3] = timestamp;
                    found |= 8u;
                }
            } else {
                if ((rising & forward_mask) != 0u && (found & 2u) == 0u) {
                    times[1] = timestamp;
                    found |= 2u;
                }
                if ((rising & (1u << 5u)) != 0u && (found & 4u) == 0u) {
                    times[2] = timestamp;
                    found |= 4u;
                }
            }
            previous = sample;
        }
    }
    phys->p3.edge_mask = found;
    phys->p3.t1_clk_tx = times[0];
    phys->p3.t2_clk_rx = times[1];
    phys->p3.t3_data_tx = times[2];
    phys->p3.t4_data_rx = times[3];
    if (clock_high_count != 0u) {
        phys->p3.clock_high_ns = (uint32_t)(
            (clock_high_sum + clock_high_count / 2u) / clock_high_count);
    }
    if (clock_low_count != 0u) {
        phys->p3.clock_low_ns = (uint32_t)(
            (clock_low_sum + clock_low_count / 2u) / clock_low_count);
    }
    if (data_high_count != 0u) {
        phys->p3.data_high_ns = (uint32_t)(
            (data_high_sum + data_high_count / 2u) / data_high_count);
    }
    phys->p3.data_pulse_count = data_high_count;
}
