#ifndef SYNC_IO_TURNTABLE_SOURCE_H
#define SYNC_IO_TURNTABLE_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

/* Local OUT4 -> selected COUNTER input test source. Hardware PWM runs without
 * any PIO, DMA, IRQ or Core1 service. Frequency is a clk_sys-derived nominal
 * value, not an external frequency calibration or VDC synchronization fact. */
typedef struct {
    uint32_t prepared, running, output_port, output_pin;
    uint32_t requested_hz, actual_hz, system_clock_hz;
    uint32_t pwm_slice, pwm_top, pwm_div16;
} sync_io_turntable_source_status_t;

/* Core0 STOP maintenance: claim and configure disabled PWM on OUT4 only.
 * Reconfiguration requires RELEASE. Failure preserves all hardware state. */
bool sync_io_turntable_source_prepare(uint32_t frequency_hz);
/* Core0 only, also allowed in RUN: toggle the already prepared PWM enable
 * and the pin's LOW/NORMAL output override gate. No mux, divider, resource
 * claim or realtime service changes. STOP holds LOW; RELEASE restores the
 * original pin. PREPARE keeps LOW even if disabled PWM retains stale HIGH. */
bool sync_io_turntable_source_start(void);
bool sync_io_turntable_source_stop(void);
/* Core0 STOP maintenance: disable and restore only the claimed pin/slice. */
bool sync_io_turntable_source_release(void);
bool sync_io_turntable_source_get_status(sync_io_turntable_source_status_t *out);

#endif
