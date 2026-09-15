#include "vdc_timestamp_clock.h"
#include <stddef.h>

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#include "hardware/clocks.h"
#include "hardware/timer.h"
#if defined(PICO_RP2350) && PICO_RP2350
#include "hardware/structs/pll.h"
#include "hardware/structs/syscfg.h"
#include "hardware/structs/ticks.h"
#include "hardware/structs/xosc.h"
#include <string.h>
#endif
#define VDC_TIMESTAMP_TIME_CRITICAL(name) __not_in_flash_func(name)
#else
#define VDC_TIMESTAMP_TIME_CRITICAL(name) name
#endif

#define VDC_TIMESTAMP_CLOCK_DEFAULT_HZ 1000000u

static bool s_vdc_timestamp_clock_initialized;
static bool s_vdc_timestamp_clock_ready;
static uint32_t s_vdc_timestamp_clock_tick_hz;
static uint32_t s_vdc_timestamp_clock_resolution_ns;

static uint32_t vdc_timestamp_clock_resolution_from_hz(uint32_t tick_hz)
{
    if (tick_hz == 0u) {
        return 0u;
    }
    const uint64_t resolution =
        (1000000000ull + (uint64_t)tick_hz - 1ull) / (uint64_t)tick_hz;
    return resolution > UINT32_MAX ? UINT32_MAX : (uint32_t)resolution;
}

bool vdc_timestamp_clock_init(void)
{
    if (__atomic_load_n(&s_vdc_timestamp_clock_initialized, __ATOMIC_ACQUIRE)) {
        return true;
    }

    __atomic_store_n(&s_vdc_timestamp_clock_ready, false, __ATOMIC_RELEASE);

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    timer1_hw->pause = 1u;
    timer1_hw->source = TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS;
    timer1_hw->timelw = 0u;
    timer1_hw->timehw = 0u;
    timer1_hw->pause = 0u;
    s_vdc_timestamp_clock_tick_hz = clock_get_hz(clk_sys);
#else
    s_vdc_timestamp_clock_tick_hz = VDC_TIMESTAMP_CLOCK_DEFAULT_HZ;
#endif

    s_vdc_timestamp_clock_resolution_ns =
        vdc_timestamp_clock_resolution_from_hz(s_vdc_timestamp_clock_tick_hz);
    const bool ready = s_vdc_timestamp_clock_tick_hz != 0u &&
                       s_vdc_timestamp_clock_resolution_ns != 0u;
    __atomic_store_n(&s_vdc_timestamp_clock_initialized, true, __ATOMIC_RELEASE);
    __atomic_store_n(&s_vdc_timestamp_clock_ready, ready, __ATOMIC_RELEASE);
    return ready;
}

uint32_t vdc_timestamp_clock_tick_hz(void)
{
    if (!__atomic_load_n(&s_vdc_timestamp_clock_initialized, __ATOMIC_ACQUIRE)) {
        (void)vdc_timestamp_clock_init();
    }
    return s_vdc_timestamp_clock_tick_hz;
}

uint32_t vdc_timestamp_clock_resolution_ns(void)
{
    if (!__atomic_load_n(&s_vdc_timestamp_clock_initialized, __ATOMIC_ACQUIRE)) {
        (void)vdc_timestamp_clock_init();
    }
    return s_vdc_timestamp_clock_resolution_ns;
}

uint64_t VDC_TIMESTAMP_TIME_CRITICAL(vdc_timestamp_clock_read_ticks64)(void)
{
    if (!__atomic_load_n(&s_vdc_timestamp_clock_initialized, __ATOMIC_ACQUIRE)) {
        (void)vdc_timestamp_clock_init();
    }

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    uint32_t hi = timer1_hw->timerawh;
    uint32_t lo;
    do {
        lo = timer1_hw->timerawl;
        const uint32_t next_hi = timer1_hw->timerawh;
        if (hi == next_hi) {
            break;
        }
        hi = next_hi;
    } while (true);
    return ((uint64_t)hi << 32u) | lo;
#else
    static uint64_t host_ticks;
    host_ticks += 1u;
    return host_ticks;
#endif
}

uint64_t vdc_timestamp_clock_ticks_to_ns(uint64_t ticks)
{
    const uint32_t hz = vdc_timestamp_clock_tick_hz();
    if (hz == 0u) {
        return 0u;
    }
    /* Resolution is rounded up for general clocks. Use it as a scale only
     * when one second is exactly hz periods. Unsigned multiplication keeps
     * the same modulo-uint64 result as the quotient/remainder expression. */
    const uint32_t period_ns = s_vdc_timestamp_clock_resolution_ns;
    if ((uint64_t)hz * period_ns == 1000000000ull) {
        return ticks * period_ns;
    }
    const uint64_t seconds = ticks / (uint64_t)hz;
    const uint64_t remainder = ticks % (uint64_t)hz;
    return seconds * 1000000000ull +
           (remainder * 1000000000ull) / (uint64_t)hz;
}

uint64_t vdc_timestamp_clock_now_ns(void)
{
    return vdc_timestamp_clock_ticks_to_ns(vdc_timestamp_clock_read_ticks64());
}

/* Configuration checks follow the ordinary owner service's XIP placement.
 * Keep the counter sampling helper below in RAM; placing this standalone
 * check there too can push the aligned TDMA workspace into another page. */
bool vdc_timestamp_clock_is_current(uint32_t expected_hz)
{
    if (!__atomic_load_n(&s_vdc_timestamp_clock_ready, __ATOMIC_ACQUIRE) ||
        expected_hz == 0u || s_vdc_timestamp_clock_tick_hz != expected_hz) return false;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    return timer1_hw->source == TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS &&
           timer1_hw->pause == 0u && clock_get_hz(clk_sys) == expected_hz;
#else
    return false; /* There is no raw TIMER1 clock in the host fallback. */
#endif
}

bool VDC_TIMESTAMP_TIME_CRITICAL(vdc_timestamp_clock_try_read_ticks64)(
    uint32_t expected_hz, uint64_t *ticks)
{
    if (ticks == NULL || !vdc_timestamp_clock_is_current(expected_hz)) return false;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    const uint32_t hi = timer1_hw->timerawh;
    const uint32_t lo = timer1_hw->timerawl;
    const uint32_t after_hi = timer1_hw->timerawh;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (hi != after_hi || !vdc_timestamp_clock_is_current(expected_hz)) return false;
    *ticks = ((uint64_t)hi << 32u) | lo;
    return true;
#else
    return false;
#endif
}

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE && defined(PICO_RP2350) && PICO_RP2350 && \
    PICO_DEFAULT_TIMER == 0 && XOSC_HZ == 12000000u
/* Keep the bridge and its configuration work in XIP. No persistent bridge
 * state is needed; even a small new RAM function can move the aligned BSS. */
typedef struct {
    uint32_t ref_ctrl, ref_div, ref_selected;
    uint32_t sys_ctrl, sys_div, sys_selected, resus_ctrl, resus_status;
    uint32_t pll_cs, pll_pwr, pll_fbdiv, pll_prim;
    uint32_t tick_ctrl, tick_cycles;
    uint32_t xosc_ctrl, xosc_status, xosc_dormant;
    uint32_t timer0_dbgpause, timer1_dbgpause;
} vdc_timestamp_bridge_config_t;

static bool vdc_timestamp_bridge_config(vdc_timestamp_bridge_config_t *out,
    uint32_t expected_hz)
{
    if (PICO_DEFAULT_TIMER_INSTANCE() != timer0_hw ||
        !vdc_timestamp_clock_is_current(expected_hz) ||
        timer0_hw->source != TIMER_SOURCE_CLK_SYS_VALUE_TICK ||
        timer0_hw->pause != 0u ||
        (syscfg_hw->proc_config & SYSCFG_PROC_CONFIG_BITS) != 0u) return false;
    const vdc_timestamp_bridge_config_t c = {
        .ref_ctrl = clocks_hw->clk[clk_ref].ctrl,
        .ref_div = clocks_hw->clk[clk_ref].div,
        .ref_selected = clocks_hw->clk[clk_ref].selected,
        .sys_ctrl = clocks_hw->clk[clk_sys].ctrl,
        .sys_div = clocks_hw->clk[clk_sys].div,
        .sys_selected = clocks_hw->clk[clk_sys].selected,
        .resus_ctrl = clocks_hw->resus.ctrl,
        .resus_status = clocks_hw->resus.status,
        .pll_cs = pll_sys_hw->cs,
        .pll_pwr = pll_sys_hw->pwr,
        .pll_fbdiv = pll_sys_hw->fbdiv_int,
        .pll_prim = pll_sys_hw->prim,
        .tick_ctrl = ticks_hw->ticks[TICK_TIMER0].ctrl,
        .tick_cycles = ticks_hw->ticks[TICK_TIMER0].cycles,
        .xosc_ctrl = xosc_hw->ctrl,
        .xosc_status = xosc_hw->status,
        .xosc_dormant = xosc_hw->dormant,
        .timer0_dbgpause = timer0_hw->dbgpause,
        .timer1_dbgpause = timer1_hw->dbgpause,
    };
    const uint32_t ref_div = (c.ref_div & CLOCKS_CLK_REF_DIV_INT_BITS) >> CLOCKS_CLK_REF_DIV_INT_LSB;
    const uint32_t sys_div = (c.sys_div & CLOCKS_CLK_SYS_DIV_INT_BITS) >> CLOCKS_CLK_SYS_DIV_INT_LSB;
    const uint32_t pll_ref = c.pll_cs & PLL_CS_REFDIV_BITS;
    const uint32_t post1 = (c.pll_prim & PLL_PRIM_POSTDIV1_BITS) >> PLL_PRIM_POSTDIV1_LSB;
    const uint32_t post2 = (c.pll_prim & PLL_PRIM_POSTDIV2_BITS) >> PLL_PRIM_POSTDIV2_LSB;
    if ((c.ref_ctrl & CLOCKS_CLK_REF_CTRL_SRC_BITS) != CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC ||
        c.ref_selected != (1u << CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC) ||
        ref_div == 0u ||
        (c.sys_ctrl & CLOCKS_CLK_SYS_CTRL_SRC_BITS) != CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX ||
        (c.sys_ctrl & CLOCKS_CLK_SYS_CTRL_AUXSRC_BITS) !=
            (CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS << CLOCKS_CLK_SYS_CTRL_AUXSRC_LSB) ||
        c.sys_selected != (1u << CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX) ||
        sys_div == 0u || (c.sys_div & CLOCKS_CLK_SYS_DIV_FRAC_BITS) != 0u ||
        (c.resus_ctrl & CLOCKS_CLK_SYS_RESUS_CTRL_FRCE_BITS) != 0u || c.resus_status != 0u ||
        (c.pll_cs & (PLL_CS_LOCK_BITS | PLL_CS_BYPASS_BITS)) != PLL_CS_LOCK_BITS ||
        c.pll_pwr != PLL_PWR_DSMPD_BITS || pll_ref == 0u || post1 == 0u || post2 == 0u ||
        c.pll_fbdiv < 16u || c.pll_fbdiv > 320u ||
        c.tick_ctrl != (TICKS_TIMER0_CTRL_ENABLE_BITS | TICKS_TIMER0_CTRL_RUNNING_BITS) ||
        c.tick_cycles == 0u || c.tick_cycles > TICKS_TIMER0_CYCLES_BITS ||
        c.xosc_ctrl != ((XOSC_CTRL_ENABLE_VALUE_ENABLE << XOSC_CTRL_ENABLE_LSB) |
            XOSC_CTRL_FREQ_RANGE_VALUE_1_15MHZ) ||
        /* BADWRITE is WC history of an invalid control write, independent of
         * current RO STABLE/ENABLED/FREQ_RANGE. Do not clear it or treat an
         * existing bit as a rate fault. Retain it in c: the full before/after
         * comparison below still rejects either transition during sampling. */
        (c.xosc_status & ~XOSC_STATUS_BADWRITE_BITS) !=
            (XOSC_STATUS_STABLE_BITS | XOSC_STATUS_ENABLED_BITS) ||
        c.xosc_dormant != XOSC_DORMANT_VALUE_WAKE ||
        c.timer0_dbgpause != c.timer1_dbgpause) return false;
    /* Validate actual dividers, not just the SDK's reported-frequency cache.
     * Both rates derive from the same XOSC, so its constant error cancels.
     * All factors are register-bounded; these cross-products fit uint64_t. */
    if ((uint64_t)ref_div * c.tick_cycles * 1000000u != XOSC_HZ ||
        (uint64_t)expected_hz * pll_ref * post1 * post2 * sys_div !=
            (uint64_t)XOSC_HZ * c.pll_fbdiv) return false;
    *out = c;
    return true;
}
#endif

bool vdc_timestamp_clock_try_read_bridge(uint32_t expected_hz,
    vdc_timestamp_clock_bridge_t *out)
{
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE && defined(PICO_RP2350) && PICO_RP2350 && \
    PICO_DEFAULT_TIMER == 0 && XOSC_HZ == 12000000u
    vdc_timestamp_bridge_config_t before, after;
    vdc_timestamp_clock_bridge_t candidate = {0};
    if (out == NULL || !vdc_timestamp_bridge_config(&before, expected_hz) ||
        !vdc_timestamp_clock_try_read_ticks64(expected_hz, &candidate.raw_before)) return false;
    const uint32_t hi = timer0_hw->timerawh;
    const uint32_t lo = timer0_hw->timerawl;
    const uint32_t after_hi = timer0_hw->timerawh;
    if (hi != after_hi) return false;
    const uint64_t local_us = ((uint64_t)hi << 32u) | lo;
    __atomic_thread_fence(__ATOMIC_SEQ_CST);
    if (local_us > (UINT64_MAX - 999u) / 1000u ||
        !vdc_timestamp_clock_try_read_ticks64(expected_hz, &candidate.raw_after) ||
        candidate.raw_after <= candidate.raw_before ||
        (candidate.raw_after >> 32u) != (candidate.raw_before >> 32u) ||
        !vdc_timestamp_bridge_config(&after, expected_hz) ||
        memcmp(&before, &after, sizeof(before)) != 0) return false;
    candidate.local_ns = local_us * 1000u;
    candidate.tick_hz = expected_hz;
    *out = candidate;
    return true;
#else
    (void)expected_hz;
    (void)out;
    return false;
#endif
}

bool vdc_timestamp_clock_read_bridge_diagnostic(uint32_t expected_hz,
    vdc_timestamp_clock_bridge_diagnostic_t *out)
{
    if (out == NULL) return false;
    vdc_timestamp_clock_bridge_diagnostic_t d = {0};
    d.schema = 1u;
    d.expected_hz = expected_hz;
    d.clock_ready = __atomic_load_n(&s_vdc_timestamp_clock_ready, __ATOMIC_ACQUIRE) ? 1u : 0u;
    d.cached_hz = s_vdc_timestamp_clock_tick_hz;
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE && defined(PICO_RP2350) && PICO_RP2350 && \
    PICO_DEFAULT_TIMER == 0 && XOSC_HZ == 12000000u
    d.platform_supported = 1u;
    d.default_timer = PICO_DEFAULT_TIMER;
    d.sdk_sys_hz = clock_get_hz(clk_sys);
    /* Read all diagnostic fields even when the earliest production gate
     * fails. These reads neither modify nor replace production admission. */
    d.proc_config = syscfg_hw->proc_config;
    d.timer0_source = timer0_hw->source;
    d.timer0_pause = timer0_hw->pause;
    d.timer0_dbgpause = timer0_hw->dbgpause;
    d.timer1_source = timer1_hw->source;
    d.timer1_pause = timer1_hw->pause;
    d.timer1_dbgpause = timer1_hw->dbgpause;
    d.ref_ctrl = clocks_hw->clk[clk_ref].ctrl;
    d.ref_div = clocks_hw->clk[clk_ref].div;
    d.ref_selected = clocks_hw->clk[clk_ref].selected;
    d.sys_ctrl = clocks_hw->clk[clk_sys].ctrl;
    d.sys_div = clocks_hw->clk[clk_sys].div;
    d.sys_selected = clocks_hw->clk[clk_sys].selected;
    d.resus_ctrl = clocks_hw->resus.ctrl;
    d.resus_status = clocks_hw->resus.status;
    d.pll_cs = pll_sys_hw->cs;
    d.pll_pwr = pll_sys_hw->pwr;
    d.pll_fbdiv = pll_sys_hw->fbdiv_int;
    d.pll_prim = pll_sys_hw->prim;
    d.tick_ctrl = ticks_hw->ticks[TICK_TIMER0].ctrl;
    d.tick_cycles = ticks_hw->ticks[TICK_TIMER0].cycles;
    d.xosc_ctrl = xosc_hw->ctrl;
    d.xosc_status = xosc_hw->status;
    d.xosc_dormant = xosc_hw->dormant;
    const uint32_t t0_hi = timer0_hw->timerawh;
    const uint32_t t0_lo = timer0_hw->timerawl;
    const uint32_t t0_after = timer0_hw->timerawh;
    d.timer0_sample_us = ((uint64_t)t0_hi << 32u) | t0_lo;
    d.timer0_sample_valid = t0_hi == t0_after ? 1u : 0u;
    const uint32_t t1_hi = timer1_hw->timerawh;
    const uint32_t t1_lo = timer1_hw->timerawl;
    const uint32_t t1_after = timer1_hw->timerawh;
    d.timer1_sample_ticks = ((uint64_t)t1_hi << 32u) | t1_lo;
    d.timer1_sample_valid = t1_hi == t1_after ? 1u : 0u;
    vdc_timestamp_bridge_config_t config;
    d.configuration_supported = vdc_timestamp_bridge_config(&config, expected_hz) ? 1u : 0u;
    d.bridge_valid = vdc_timestamp_clock_try_read_bridge(expected_hz, &d.bridge) ? 1u : 0u;
#endif
    *out = d;
    return true;
}
