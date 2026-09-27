#include "sync_io_turntable_source.h"

#include <string.h>
#include "board_config.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "hardware/structs/io_bank0.h"
#include "hardware/structs/pads_bank0.h"
#include "hardware/regs/pads_bank0.h"
#include "hardware/regs/io_bank0.h"
#include "pico/platform.h"
#include "sync_io_core_internal.h"
#include "tdma_runtime_owner.h"

#define SOURCE_PIN (BOARD_SYNC_OUTPUT_BASE_PIN + 3u)
#define SOURCE_MIN_HZ 100u
#define SOURCE_MAX_HZ 100000u

static sync_io_turntable_source_status_t s_source;
static uint32_t s_busy;
static struct {
    uint32_t ctrl, pad, csr, div, ctr, cc, top, pending_irq;
} s_saved;

static bool enter(void)
{
    uint32_t idle = 0u;
    return get_core_num() == 0u && __atomic_compare_exchange_n(&s_busy,
        &idle, 1u, false, __ATOMIC_ACQUIRE, __ATOMIC_RELAXED);
}
static void leave(void) { __atomic_store_n(&s_busy, 0u, __ATOMIC_RELEASE); }

/* Exact rational error comparison, no floating point. Only STOP calls this
 * bounded search; an exact 1 kHz solution exists at the board clock. */
static bool choose_pwm(uint32_t hz, uint32_t clock_hz, uint32_t *top,
    uint32_t *div16, uint32_t *actual)
{
    if (hz < SOURCE_MIN_HZ || hz > SOURCE_MAX_HZ || !clock_hz ||
        clock_hz > 500000000u) return false;
    const uint64_t numerator = (uint64_t)clock_hz * 16u;
    uint64_t best_error = UINT64_MAX, best_denominator = 1u;
    uint32_t best_period = 0u, best_div = 0u;
    for (uint32_t div = 16u; div <= 4095u; ++div) {
        const uint64_t target = (uint64_t)hz * div;
        const uint64_t period = (numerator + target / 2u) / target;
        if (period < 2u || period > 65536u) continue;
        const uint64_t denominator = period * div;
        const uint64_t represented = denominator * hz;
        const uint64_t error = numerator > represented ?
            numerator - represented : represented - numerator;
        /* Nearest-period rounding bounds error <= hz*div/2, so both
         * cross-products fit uint64_t over the admitted frequency range. */
        if (!best_period || error * best_denominator < best_error * denominator) {
            best_error = error; best_denominator = denominator;
            best_period = (uint32_t)period; best_div = div;
            if (!error) break;
        }
    }
    if (!best_period) return false;
    *top = best_period - 1u; *div16 = best_div;
    *actual = (uint32_t)((numerator + best_denominator / 2u) / best_denominator);
    return true;
}

static bool prepare_stopped(void *context)
{
    const uint32_t hz = *(const uint32_t *)context;
    if (s_source.prepared) return false;
    const uint32_t sys_hz = clock_get_hz(clk_sys);
    uint32_t top, div16, actual;
    if (!choose_pwm(hz, sys_hz, &top, &div16, &actual) ||
        !sync_io_core_turntable_source_reserve(&s_source)) return false;
    const uint slice = pwm_gpio_to_slice_num(SOURCE_PIN);
    const uint32_t bit = 1u << slice;
    /* PWM shares a counter/divider between both channels and GPIO aliases.
     * Reject any existing PWM mux on this slice, even if currently disabled. */
    bool occupied = (pwm_hw->slice[slice].csr & PWM_CH0_CSR_EN_BITS) != 0u ||
        ((pwm_hw->inte | pwm_hw->inte1 | pwm_hw->intf | pwm_hw->intf1) & bit) != 0u;
    for (uint pin = 0u; pin < NUM_BANK0_GPIOS && !occupied; ++pin)
        occupied = pwm_gpio_to_slice_num(pin) == slice &&
            gpio_get_function(pin) == GPIO_FUNC_PWM;
    if (occupied) {
        (void)sync_io_core_turntable_source_release(&s_source);
        return false;
    }
    s_saved.ctrl = io_bank0_hw->io[SOURCE_PIN].ctrl;
    s_saved.pad = pads_bank0_hw->io[SOURCE_PIN];
    s_saved.csr = pwm_hw->slice[slice].csr;
    s_saved.div = pwm_hw->slice[slice].div;
    s_saved.ctr = pwm_hw->slice[slice].ctr;
    s_saved.cc = pwm_hw->slice[slice].cc;
    s_saved.top = pwm_hw->slice[slice].top;
    s_saved.pending_irq = pwm_hw->intr & bit;
    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, (uint16_t)top);
    pwm_config_set_clkdiv_int_frac4(&config, div16 >> 4u, (uint8_t)(div16 & 15u));
    pwm_init(slice, &config, false);
    pwm_set_chan_level(slice, pwm_gpio_to_channel(SOURCE_PIN), (uint16_t)((top + 1u) / 2u));
    /* A disabled PWM may retain an old HIGH result despite a counter write.
     * Keep a physical LOW override throughout preparation and mux handoff.
     * gpio_set_function() cannot be used here: it clears OUTOVER itself. */
    pwm_set_counter(slice, (uint16_t)top);
    gpio_set_outover(SOURCE_PIN, GPIO_OVERRIDE_LOW);
    hw_write_masked(&io_bank0_hw->io[SOURCE_PIN].ctrl,
        ((uint32_t)GPIO_FUNC_PWM << IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB) |
        ((uint32_t)GPIO_OVERRIDE_LOW << IO_BANK0_GPIO0_CTRL_OUTOVER_LSB), UINT32_MAX);
    hw_write_masked(&pads_bank0_hw->io[SOURCE_PIN], PADS_BANK0_GPIO0_IE_BITS,
        PADS_BANK0_GPIO0_IE_BITS | PADS_BANK0_GPIO0_OD_BITS | PADS_BANK0_GPIO0_ISO_BITS);
    s_source = (sync_io_turntable_source_status_t){.prepared = 1u,
        .output_port = 4u, .output_pin = SOURCE_PIN, .requested_hz = hz,
        .actual_hz = actual, .system_clock_hz = sys_hz,
        .pwm_slice = slice, .pwm_top = top, .pwm_div16 = div16};
    return true;
}

bool sync_io_turntable_source_prepare(uint32_t frequency_hz)
{
    if (!enter()) return false;
    const bool ok = tdma_service_run_stopped_maintenance(tdma_runtime_owner_get(),
        prepare_stopped, &frequency_hz);
    leave(); return ok;
}

bool sync_io_turntable_source_start(void)
{
    if (!enter()) return false;
    const bool ok = s_source.prepared &&
        clock_get_hz(clk_sys) == s_source.system_clock_hz;
    if (ok) {
        pwm_set_enabled(s_source.pwm_slice, true);
        gpio_set_outover(SOURCE_PIN, GPIO_OVERRIDE_NORMAL);
        s_source.running = 1u;
    }
    leave(); return ok;
}

bool sync_io_turntable_source_stop(void)
{
    if (!enter()) return false;
    const bool ok = s_source.prepared != 0u;
    if (ok) {
        gpio_set_outover(SOURCE_PIN, GPIO_OVERRIDE_LOW);
        pwm_set_enabled(s_source.pwm_slice, false);
        s_source.running = 0u;
    }
    leave(); return ok;
}

static bool release_stopped(void *context)
{
    (void)context;
    if (!s_source.prepared) return true;
    const uint slice = s_source.pwm_slice;
    pwm_set_enabled(slice, false);
    /* Isolate just this pad while restoring the mux and its PWM slice.
     * SIO and PIO latches/OE were never modified and require no rewriting. */
    pads_bank0_hw->io[SOURCE_PIN] |= PADS_BANK0_GPIO0_OD_BITS;
    io_bank0_hw->io[SOURCE_PIN].ctrl = s_saved.ctrl;
    pwm_hw->slice[slice].div = s_saved.div;
    pwm_hw->slice[slice].ctr = s_saved.ctr;
    pwm_hw->slice[slice].cc = s_saved.cc;
    pwm_hw->slice[slice].top = s_saved.top;
    pwm_hw->slice[slice].csr = s_saved.csr;
    if (!s_saved.pending_irq) pwm_clear_irq(slice);
    pads_bank0_hw->io[SOURCE_PIN] = s_saved.pad;
    const bool released = sync_io_core_turntable_source_release(&s_source);
    if (released) memset(&s_source, 0, sizeof(s_source));
    return released;
}

bool sync_io_turntable_source_release(void)
{
    if (!enter()) return false;
    const bool ok = tdma_service_run_stopped_maintenance(tdma_runtime_owner_get(),
        release_stopped, NULL);
    leave(); return ok;
}

bool sync_io_turntable_source_get_status(sync_io_turntable_source_status_t *out)
{
    if (!out || !enter()) return false;
    *out = s_source;
    leave(); return true;
}
