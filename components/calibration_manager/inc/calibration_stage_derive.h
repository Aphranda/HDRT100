#ifndef CALIBRATION_STAGE_DERIVE_H
#define CALIBRATION_STAGE_DERIVE_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Board-side derivation of the constant fields of one calibration link.
 *
 * The measured values (offset sample counts, link base delay, forward
 * residence) cross the wire; the persona/profile constants and the PIO
 * instruction period are derived here. The numbers must match the host
 * formula frozen in tests/python/test_cal_guide_derive_golden.py.
 */

typedef struct {
    uint32_t pio_persona;
    uint32_t clkdiv_q16;
    uint32_t clk_sys_hz;
    uint32_t instruction_period_ns;
    uint32_t bit_cycles;
} calibration_stage_pio_t;

/* Normal SPI persona: six PIO instruction cycles per bit. */
#define CALIBRATION_STAGE_PIO_BIT_CYCLES 6u
#define CALIBRATION_STAGE_PIO_PERSONA_NORMAL 1u

/* baud_hz and clk_sys_hz are the live profile/board values; bit_cycles is the
 * persona's per-bit instruction count. Returns false on a zero/overflowing
 * divider instead of guessing. */
bool calibration_stage_derive_pio(uint32_t baud_hz,
                                  uint32_t clk_sys_hz,
                                  uint32_t bit_cycles,
                                  calibration_stage_pio_t *out);

/* half of the measured round-trip link delay, rounded; requires a positive
 * even link_delay_ns and a positive divisor. */
bool calibration_stage_link_base_delay_ns(uint32_t link_delay_ns,
                                          uint32_t divisor,
                                          uint32_t *out);

/* base_samples = round(link_base_ns / sample_period_ns); delay = base + offset,
 * bounded by max_delay_samples (PIO delay range). */
bool calibration_stage_phase_delay_samples(uint32_t link_base_ns,
                                           uint32_t sample_period_ns,
                                           int32_t node_offset_samples,
                                           uint32_t max_delay_samples,
                                           uint32_t *out);

#ifdef __cplusplus
}
#endif

#endif /* CALIBRATION_STAGE_DERIVE_H */
