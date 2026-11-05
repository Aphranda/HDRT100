#include <assert.h>
#include <stdio.h>

#include "calibration_stage_derive.h"

static void expect_pio(uint32_t baud_hz,
                       uint32_t clkdiv_q16,
                       uint32_t instruction_period_ns)
{
    calibration_stage_pio_t pio = {0};
    assert(calibration_stage_derive_pio(
        baud_hz, 250000000u, CALIBRATION_STAGE_PIO_BIT_CYCLES, &pio));
    assert(pio.pio_persona == CALIBRATION_STAGE_PIO_PERSONA_NORMAL);
    assert(pio.clkdiv_q16 == clkdiv_q16);
    assert(pio.clk_sys_hz == 250000000u);
    assert(pio.instruction_period_ns == instruction_period_ns);
    assert(pio.bit_cycles == CALIBRATION_STAGE_PIO_BIT_CYCLES);
}

int main(void)
{
    /* Golden vectors frozen in tests/python/test_cal_guide_derive_golden.py. */
    expect_pio(10000000u, 273152u, 17u);
    expect_pio(25000000u, 109312u, 7u);
    expect_pio(30000000u, 91136u, 6u);

    /* Level 20/21/22 share the 10 MHz baud; only the cycle period differs. */
    expect_pio(10000000u, 273152u, 17u);

    calibration_stage_pio_t pio = {0};
    assert(!calibration_stage_derive_pio(0u, 250000000u, 6u, &pio));
    assert(!calibration_stage_derive_pio(10000000u, 0u, 6u, &pio));
    assert(!calibration_stage_derive_pio(10000000u, 250000000u, 0u, &pio));
    assert(!calibration_stage_derive_pio(10000000u, 250000000u, 6u, NULL));
    /* A divider outside the 16.8 range must be rejected, not truncated. */
    assert(!calibration_stage_derive_pio(1000u, 250000000u, 6u, &pio));

    uint32_t base = 0u;
    assert(calibration_stage_link_base_delay_ns(160u, 2u, &base));
    assert(base == 80u);
    assert(calibration_stage_link_base_delay_ns(162u, 2u, &base));
    assert(base == 81u);
    assert(calibration_stage_link_base_delay_ns(164u, 2u, &base));
    assert(base == 82u);
    assert(!calibration_stage_link_base_delay_ns(0u, 2u, &base));
    assert(!calibration_stage_link_base_delay_ns(161u, 2u, &base));
    assert(!calibration_stage_link_base_delay_ns(160u, 0u, &base));
    assert(!calibration_stage_link_base_delay_ns(2u, 8u, &base));

    uint32_t delay = 0u;
    assert(calibration_stage_phase_delay_samples(80u, 4u, 0, 31u, &delay));
    assert(delay == 20u);
    assert(calibration_stage_phase_delay_samples(80u, 4u, 1, 31u, &delay));
    assert(delay == 21u);
    assert(calibration_stage_phase_delay_samples(80u, 4u, -1, 31u, &delay));
    assert(delay == 19u);
    assert(calibration_stage_phase_delay_samples(81u, 4u, 0, 31u, &delay));
    assert(delay == 20u);
    assert(!calibration_stage_phase_delay_samples(80u, 4u, 20, 31u, &delay));
    assert(!calibration_stage_phase_delay_samples(80u, 4u, -30, 31u, &delay));
    assert(!calibration_stage_phase_delay_samples(0u, 4u, 0, 31u, &delay));
    assert(!calibration_stage_phase_delay_samples(80u, 0u, 0, 31u, &delay));

    printf("calibration stage derive host tests passed\n");
    return 0;
}
