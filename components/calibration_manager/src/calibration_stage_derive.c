#include "calibration_stage_derive.h"

#include <stddef.h>

static uint64_t ceil_div_u64(uint64_t numerator, uint64_t denominator)
{
    return (numerator + denominator - 1u) / denominator;
}

bool calibration_stage_derive_pio(uint32_t baud_hz,
                                  uint32_t clk_sys_hz,
                                  uint32_t bit_cycles,
                                  calibration_stage_pio_t *out)
{
    if (out == NULL || baud_hz == 0u || clk_sys_hz == 0u || bit_cycles == 0u) {
        return false;
    }
    const uint64_t instruction_hz = (uint64_t)baud_hz * (uint64_t)bit_cycles;
    if (instruction_hz == 0u) {
        return false;
    }
    /* RP2350 PIO programs a 16.8 divider; keep the programmed 8.8 value in the
     * field's 16.16 representation, matching the host tool. */
    const uint64_t clkdiv_q8 =
        ((uint64_t)clk_sys_hz * 256u + instruction_hz / 2u) / instruction_hz;
    if (clkdiv_q8 == 0u || clkdiv_q8 > 0xFFFFu) {
        return false;
    }
    const uint64_t clkdiv_q16 = clkdiv_q8 << 8;
    out->pio_persona = CALIBRATION_STAGE_PIO_PERSONA_NORMAL;
    out->clkdiv_q16 = (uint32_t)clkdiv_q16;
    out->clk_sys_hz = clk_sys_hz;
    out->bit_cycles = bit_cycles;
    out->instruction_period_ns = (uint32_t)ceil_div_u64(
        clkdiv_q16 * 1000000000ull, 65536ull * (uint64_t)clk_sys_hz);
    return true;
}

bool calibration_stage_link_base_delay_ns(uint32_t link_delay_ns,
                                          uint32_t divisor,
                                          uint32_t *out)
{
    if (out == NULL || link_delay_ns == 0u || (link_delay_ns % 2u) != 0u ||
        divisor == 0u) {
        return false;
    }
    const uint32_t base = (link_delay_ns + divisor / 2u) / divisor;
    if (base == 0u) {
        return false;
    }
    *out = base;
    return true;
}

bool calibration_stage_phase_delay_samples(uint32_t link_base_ns,
                                           uint32_t sample_period_ns,
                                           int32_t node_offset_samples,
                                           uint32_t max_delay_samples,
                                           uint32_t *out)
{
    if (out == NULL || link_base_ns == 0u || sample_period_ns == 0u) {
        return false;
    }
    const uint32_t base_samples =
        (link_base_ns + sample_period_ns / 2u) / sample_period_ns;
    const int64_t delay = (int64_t)base_samples + (int64_t)node_offset_samples;
    if (delay < 0 || delay > (int64_t)max_delay_samples) {
        return false;
    }
    *out = (uint32_t)delay;
    return true;
}
