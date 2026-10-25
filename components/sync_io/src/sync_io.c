#include "sync_io_internal.h"


sync_io_context_t s_sync_io;
sync_io_sma_frequency_tx_status_t s_sma_frequency_tx;
uint32_t sync_io_shared_workspace[SYNC_IO_SHARED_WORKSPACE_WORDS]
    __attribute__((section(".sync_io_dma_ring")))
    __attribute__((aligned(SYNC_IO_CAPTURE_DMA_RING_BYTES)));
#define s_sync_io_capture_dma_ring sync_io_shared_workspace

_Static_assert(sizeof(sync_io_shared_workspace) ==
                   (1u << SYNC_IO_CAPTURE_DMA_RING_BITS),
               "capture DMA wrap must equal the actual arena size");

void sync_io_core_trace(sync_io_trace_event_t event_id,
                        uint8_t severity,
                        uint32_t arg0,
                        uint32_t arg1)
{
    storage_manager_trace_event(SYNC_IO_TRACE_DOMAIN,
                                (uint16_t)event_id,
                                severity,
                                arg0,
                                arg1);
}

void sync_io_trace(sync_io_trace_event_t event_id,
                          uint8_t severity,
                          uint32_t arg0,
                          uint32_t arg1)
{
    sync_io_core_trace(event_id, severity, arg0, arg1);
}

static uint32_t sync_io_pack_runtime_flags(bool running,
                                           bool pio_enabled,
                                           bool dma_busy,
                                           bool dma_irq_enabled,
                                           bool tx_fifo_empty,
                                           bool tx_fifo_full)
{
    return (running ? (1u << 0) : 0u) |
           (pio_enabled ? (1u << 1) : 0u) |
           (dma_busy ? (1u << 2) : 0u) |
           (dma_irq_enabled ? (1u << 3) : 0u) |
           (tx_fifo_empty ? (1u << 4) : 0u) |
           (tx_fifo_full ? (1u << 5) : 0u);
}

uint32_t sync_io_core_pack_runtime_flags(bool running,
                                         bool pio_enabled,
                                         bool dma_busy,
                                         bool dma_irq_enabled,
                                         bool tx_fifo_empty,
                                         bool tx_fifo_full)
{
    return sync_io_pack_runtime_flags(running,
                                      pio_enabled,
                                      dma_busy,
                                      dma_irq_enabled,
                                      tx_fifo_empty,
                                      tx_fifo_full);
}

static bool sync_io_sm_is_enabled(PIO pio, uint sm)
{
    return (pio->ctrl & (1u << sm)) != 0u;
}

bool sync_io_core_sm_is_enabled(PIO pio, uint sm)
{
    return sync_io_sm_is_enabled(pio, sm);
}

static uint32_t sync_io_pack_pio_state(uint sm,
                                       uint32_t offset,
                                       bool pio_enabled,
                                       bool tx_fifo_empty,
                                       bool tx_fifo_full)
{
    return (sm & 0xFFu) |
           ((offset & 0xFFu) << 8) |
           (pio_enabled ? (1u << 16) : 0u) |
           (tx_fifo_empty ? (1u << 17) : 0u) |
           (tx_fifo_full ? (1u << 18) : 0u);
}

uint32_t sync_io_core_pack_pio_state(uint sm,
                                     uint32_t offset,
                                     bool pio_enabled,
                                     bool tx_fifo_empty,
                                     bool tx_fifo_full)
{
    return sync_io_pack_pio_state(sm,
                                  offset,
                                  pio_enabled,
                                  tx_fifo_empty,
                                  tx_fifo_full);
}

const uint s_aux_pins[SYNC_IO_AUX_COUNT] = {
    BOARD_SYNC_AUX0_PIN,
    BOARD_SYNC_AUX1_PIN,
    BOARD_SYNC_AUX2_PIN,
    BOARD_SYNC_AUX3_PIN,
};

const uint s_aux_sms[SYNC_IO_AUX_COUNT] = {
    BOARD_SYNC_AUX0_SM,
    BOARD_SYNC_AUX1_SM,
    BOARD_SYNC_AUX2_SM,
    BOARD_SYNC_AUX3_SM,
};

uint32_t s_aux_last_snapshot;
uint32_t s_ready_last_snapshot;
uint32_t s_aux_wait_start_ms;
uint32_t s_aux_timeout_latched_mask;
uint32_t s_expected_ready_mask;
bool s_aux_trace_sample_valid;

float sync_io_clkdiv_for_instruction_rate(uint32_t instruction_hz)
{
    if (instruction_hz < SYNC_IO_MIN_HZ) {
        instruction_hz = SYNC_IO_MIN_HZ;
    }

    const uint32_t sys_hz = clock_get_hz(clk_sys);
    float clkdiv = (float)sys_hz / (float)instruction_hz;

    if (clkdiv < 1.0f) {
        clkdiv = 1.0f;
    }

    return clkdiv;
}

static uint32_t sync_io_capture_sample_period_ns(uint32_t sample_hz)
{
    if (sample_hz == 0u) {
        return 0u;
    }
    uint64_t period_ns = 1000000000ull / (uint64_t)sample_hz;
    if (period_ns == 0u) {
        period_ns = 1u;
    }
    return period_ns > UINT32_MAX ? UINT32_MAX : (uint32_t)period_ns;
}

uint32_t sync_io_capture_dma_produced_words(void)
{
    if (!s_sync_io.initialized) {
        return 0u;
    }

    if (!s_sync_io.capture_running) {
        return s_sync_io.capture_dma_produced_seq;
    }

    const uintptr_t ring_base = (uintptr_t)s_sync_io_capture_dma_ring;
    const uintptr_t write_addr =
        (uintptr_t)dma_hw->ch[SYNC_IO_CAPTURE_DMA_CH].write_addr;
    const uint32_t write_index =
        (uint32_t)(((write_addr - ring_base) &
                    (SYNC_IO_CAPTURE_DMA_RING_BYTES - 1u)) /
                   sizeof(uint32_t));
    const uint32_t transfer_remaining =
        dma_hw->ch[SYNC_IO_CAPTURE_DMA_CH].transfer_count;
    const uint32_t transfer_produced = UINT32_MAX - transfer_remaining;

    osal_critical_enter();
    if (!s_sync_io.capture_dma_write_index_valid) {
        s_sync_io.capture_dma_last_write_index = write_index;
        s_sync_io.capture_dma_write_index_valid = true;
    }
    if (transfer_produced > s_sync_io.capture_dma_produced_seq) {
        s_sync_io.capture_dma_produced_seq = transfer_produced;
    } else if (write_index != s_sync_io.capture_dma_last_write_index) {
        const uint32_t last = s_sync_io.capture_dma_last_write_index;
        const uint32_t delta =
            write_index >= last
                ? write_index - last
                : (SYNC_IO_CAPTURE_DMA_RING_WORDS - last) + write_index;
        s_sync_io.capture_dma_produced_seq += delta;
    }
    s_sync_io.capture_dma_last_write_index = write_index;
    const uint32_t produced = s_sync_io.capture_dma_produced_seq;
    osal_critical_exit();
    return produced;
}

static bool sync_io_capture_dma_configure(void)
{
    if (!dma_channel_is_claimed(SYNC_IO_CAPTURE_DMA_CH)) {
        return false;
    }

    dma_channel_abort(SYNC_IO_CAPTURE_DMA_CH);
    dma_channel_set_irq0_enabled(SYNC_IO_CAPTURE_DMA_CH, false);

    dma_channel_config dma_cfg =
        dma_channel_get_default_config(SYNC_IO_CAPTURE_DMA_CH);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, false);
    channel_config_set_write_increment(&dma_cfg, true);
    channel_config_set_ring(&dma_cfg, true, SYNC_IO_CAPTURE_DMA_RING_BITS);
    channel_config_set_dreq(
        &dma_cfg,
        pio_get_dreq(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false));

    dma_channel_configure(
        SYNC_IO_CAPTURE_DMA_CH,
        &dma_cfg,
        s_sync_io_capture_dma_ring,
        &BOARD_SYNC_PIO_FAST->rxf[BOARD_SYNC_CAPTURE_SM],
        UINT32_MAX,
        false);
    return true;
}

#if !defined(PICO_ON_DEVICE) || !PICO_ON_DEVICE
static uint64_t sync_io_capture_latch_read_ticks64(void)
{
    return vdc_timestamp_clock_read_ticks64();
}

static uint64_t sync_io_capture_latch_ticks_to_ns(uint64_t ticks)
{
    return vdc_timestamp_clock_ticks_to_ns(ticks);
}
#endif

static uint64_t sync_io_common_time_now_ns(void)
{
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
    uint64_t now;
    return vdc_timestamp_clock_try_read_ns(BOARD_SYS_CLOCK_HZ, &now) ? now : UINT64_MAX;
#else
    return sync_io_capture_latch_ticks_to_ns(sync_io_capture_latch_read_ticks64());
#endif
}

static void sync_io_capture_latch_timer_init(void)
{
    (void)vdc_timestamp_clock_init();
    s_sync_io.capture_latch_tick_hz = vdc_timestamp_clock_tick_hz();
    s_sync_io.capture_latch_resolution_ns =
        vdc_timestamp_clock_resolution_ns();
}

static void sync_io_capture_latch_reset_locked(void)
{
    s_sync_io.capture_latch_write = 0u;
    s_sync_io.capture_latch_read = 0u;
    s_sync_io.capture_latch_seq = 0u;
    s_sync_io.capture_latch_previous_sample_mask =
        s_sync_io.capture_timestamp_window_initial_sample_mask &
        s_sync_io.capture_timestamp_window_observed_mask;
    s_sync_io.capture_latch_edge_state_valid = false;
    s_sync_io.latched_capture_words = 0u;
    s_sync_io.dropped_latched_capture_words = 0u;
}

static uint32_t sync_io_capture_sample_at(uint32_t raw_word,
                                          uint32_t sample_index)
{
    const uint32_t index = 7u - sample_index;
    return (raw_word >> (index * 4u)) & 0x0Fu;
}

static uint32_t sync_io_map_capture_word(uint32_t raw_word)
{
#if BOARD_SYNC_INPUT_BITS_REVERSED
    /* Reverse the four input bits inside each of the eight packed samples:
     * GPIO20..23 arrives as IN4..IN1, public masks are IN1..IN4. */
    return ((raw_word & 0x11111111u) << 3u) |
           ((raw_word & 0x22222222u) << 1u) |
           ((raw_word & 0x44444444u) >> 1u) |
           ((raw_word & 0x88888888u) >> 3u);
#else
    return raw_word;
#endif
}

uint32_t sync_io_map_input_mask(uint32_t physical_mask)
{
#if BOARD_SYNC_INPUT_BITS_REVERSED
    physical_mask &= 0x0Fu;
    return ((physical_mask & 0x01u) << 3u) |
           ((physical_mask & 0x02u) << 1u) |
           ((physical_mask & 0x04u) >> 1u) |
           ((physical_mask & 0x08u) >> 3u);
#else
    return physical_mask;
#endif
}

static bool sync_io_capture_word_has_observed_edge(uint32_t raw_word,
                                                   uint32_t observed_mask,
                                                   uint32_t *previous_sample_mask)
{
    if (observed_mask == 0u) {
        if (previous_sample_mask != NULL) {
            *previous_sample_mask = 0u;
        }
        return true;
    }

    uint32_t previous =
        s_sync_io.capture_latch_previous_sample_mask & observed_mask;
    if (previous_sample_mask != NULL) {
        *previous_sample_mask = previous;
    }
    bool edge_found = false;

    for (uint32_t i = 0u; i < 8u; i++) {
        const uint32_t current =
            sync_io_capture_sample_at(raw_word, i) & observed_mask;
        if (s_sync_io.capture_latch_edge_state_valid &&
            current != previous) {
            edge_found = true;
        }
        previous = current;
        s_sync_io.capture_latch_edge_state_valid = true;
    }

    s_sync_io.capture_latch_previous_sample_mask = previous;
    return edge_found;
}

static bool sync_io_capture_latch_word_in_window(uint64_t word_start_ns,
                                                 uint64_t word_end_ns,
                                                 uint64_t *matched_window_start_ns)
{
    uint64_t window_start_ns = s_sync_io.capture_timestamp_window_start_ns;
    uint64_t window_end_ns = s_sync_io.capture_timestamp_window_end_ns;

    if (s_sync_io.capture_timestamp_window_periodic) {
        const uint32_t period_ns = s_sync_io.capture_timestamp_window_period_ns;
        const uint32_t width_ns =
            (uint32_t)(s_sync_io.capture_timestamp_window_end_ns -
                       s_sync_io.capture_timestamp_window_start_ns);
        if (period_ns == 0u || width_ns == 0u) {
            return false;
        }

        uint64_t cycle = 0u;
        if (word_start_ns > s_sync_io.capture_timestamp_window_start_ns) {
            cycle =
                (word_start_ns - s_sync_io.capture_timestamp_window_start_ns) /
                (uint64_t)period_ns;
        }
        window_start_ns =
            s_sync_io.capture_timestamp_window_start_ns +
            cycle * (uint64_t)period_ns;
        window_end_ns = window_start_ns + (uint64_t)width_ns;
        if (word_start_ns > window_end_ns) {
            window_start_ns += period_ns;
            window_end_ns += period_ns;
        }
    }

    if (word_end_ns < window_start_ns || word_start_ns > window_end_ns) {
        return false;
    }

    if (matched_window_start_ns != NULL) {
        *matched_window_start_ns = window_start_ns;
    }
    return true;
}

static bool sync_io_capture_latch_next_window_start(uint64_t word_start_ns,
                                                    uint64_t word_end_ns,
                                                    uint64_t *window_start_ns,
                                                    uint64_t *window_end_ns)
{
    uint64_t candidate_start = s_sync_io.capture_timestamp_window_start_ns;
    uint64_t candidate_end = s_sync_io.capture_timestamp_window_end_ns;

    if (!s_sync_io.capture_timestamp_window_armed) {
        return false;
    }

    if (s_sync_io.capture_timestamp_window_periodic) {
        const uint32_t period_ns =
            s_sync_io.capture_timestamp_window_period_ns;
        const uint64_t width_ns =
            s_sync_io.capture_timestamp_window_end_ns -
            s_sync_io.capture_timestamp_window_start_ns;
        if (period_ns == 0u || width_ns == 0u) {
            return false;
        }

        if (word_start_ns > candidate_start) {
            candidate_start +=
                ((word_start_ns - candidate_start) / (uint64_t)period_ns) *
                (uint64_t)period_ns;
            candidate_end = candidate_start + width_ns;
            if (word_start_ns > candidate_end ||
                word_end_ns > candidate_end) {
                candidate_start += (uint64_t)period_ns;
                candidate_end += (uint64_t)period_ns;
            }
        }
    }

    if (word_start_ns < candidate_start) {
        /* The current word precedes the first eligible window. */
    } else if (word_start_ns >= candidate_start &&
               word_end_ns <= candidate_end) {
        return false;
    } else if (!s_sync_io.capture_timestamp_window_periodic) {
        return false;
    } else {
        candidate_start +=
            (uint64_t)s_sync_io.capture_timestamp_window_period_ns;
        candidate_end +=
            (uint64_t)s_sync_io.capture_timestamp_window_period_ns;
    }

    if (window_start_ns != NULL) {
        *window_start_ns = candidate_start;
    }
    if (window_end_ns != NULL) {
        *window_end_ns = candidate_end;
    }
    return true;
}

static uint32_t sync_io_capture_latch_flags_for_word(uint64_t word_start_ns,
                                                     uint64_t word_end_ns,
                                                     uint32_t sample_period_ns,
                                                     uint64_t *matched_window_start_ns)
{
    uint32_t flags = SYNC_IO_CAPTURE_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    const uint64_t word_span_ns =
        word_end_ns > word_start_ns ? word_end_ns - word_start_ns : 0ull;

    if (matched_window_start_ns != NULL) {
        *matched_window_start_ns = 0u;
    }

    if (!s_sync_io.capture_timebase_valid ||
        !s_sync_io.capture_timestamp_window_armed ||
        sample_period_ns == 0u ||
        word_span_ns == 0u ||
        !sync_io_capture_latch_word_in_window(word_start_ns,
                                              word_end_ns,
                                              matched_window_start_ns) ||
        sample_period_ns != s_sync_io.capture_timestamp_window_sample_period_ns ||
        s_sync_io.capture_timestamp_window_observed_mask == 0u) {
        return flags;
    }

    flags &= ~SYNC_IO_CAPTURE_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    flags |= SYNC_IO_CAPTURE_TIMESTAMP_FLAG_DPLL_ELIGIBLE;
    if (!s_sync_io.capture_timestamp_window_periodic) {
        s_sync_io.capture_timestamp_window_armed = false;
    }
    return flags;
}

static void sync_io_capture_latch_discard_outside_window(
    uint32_t produced,
    uint64_t word_span_ns)
{
    if (!s_sync_io.capture_timestamp_window_armed ||
        !s_sync_io.capture_timebase_valid ||
        word_span_ns == 0u) {
        return;
    }

    osal_critical_enter();
    uint32_t read_seq = s_sync_io.capture_dma_read_seq;
    uint32_t available = produced - read_seq;
    if (available > SYNC_IO_CAPTURE_DMA_RING_WORDS) {
        s_sync_io.dropped_capture_words +=
            available - SYNC_IO_CAPTURE_DMA_RING_WORDS;
        s_sync_io.capture_latch_edge_state_valid = false;
        read_seq = produced - SYNC_IO_CAPTURE_DMA_RING_WORDS;
        s_sync_io.capture_dma_read_seq = read_seq;
        available = SYNC_IO_CAPTURE_DMA_RING_WORDS;
    }

    if (available != 0u) {
        const uint64_t capture_start_ns = s_sync_io.capture_timebase_start_ns;
        const uint64_t word_start_ns =
            capture_start_ns + (uint64_t)read_seq * word_span_ns;
        const uint64_t word_end_ns = word_start_ns + word_span_ns;
        uint64_t next_window_start_ns = 0u;
        uint64_t next_window_end_ns = 0u;

        if (sync_io_capture_latch_word_in_window(word_start_ns,
                                                 word_end_ns,
                                                 NULL)) {
            osal_critical_exit();
            return;
        }

        if (sync_io_capture_latch_next_window_start(
                word_start_ns,
                word_end_ns,
                &next_window_start_ns,
                &next_window_end_ns)) {
            (void)next_window_end_ns;
            const uint64_t delta_ns =
                next_window_start_ns > capture_start_ns
                    ? next_window_start_ns - capture_start_ns
                    : 0u;
            uint64_t target_seq =
                (delta_ns + word_span_ns - 1u) / word_span_ns;
            if (target_seq < read_seq) {
                target_seq = read_seq;
            }
            if (target_seq > produced) {
                target_seq = produced;
            }
            s_sync_io.capture_dma_read_seq = (uint32_t)target_seq;
        } else {
            /* A one-shot window has expired; no diagnostic backlog is useful. */
            s_sync_io.capture_dma_read_seq = produced;
        }
    }
    osal_critical_exit();
}

static bool sync_io_claim_sm(PIO pio, uint sm, const char *name)
{
    if (pio_sm_is_claimed(pio, sm)) {
        LOG_ERROR("sync_io", "%s state machine already claimed", name);
        return false;
    }

    pio_sm_claim(pio, sm);
    return true;
}

static void sync_io_configure_static_inputs(void)
{
    gpio_pull_down(BOARD_SYNC_TRIG_IN_PIN);
    gpio_pull_down(BOARD_SYNC_ARM_IN_PIN);
    gpio_pull_down(BOARD_SYNC_EXT_CLK_IN_PIN);
    gpio_pull_down(BOARD_SYNC_GATE_IN_PIN);
}

bool sync_io_valid_aux_channel(sync_io_aux_channel_t channel)
{
    return sync_io_hw_aux_channel_valid((uint32_t)channel);
}

bool sync_io_aux_mode_allowed(sync_io_aux_channel_t channel,
                                     sync_io_aux_mode_t mode)
{
    const uint32_t index = (uint32_t)channel;
    if (mode == SYNC_IO_AUX_MODE_INPUT) {
        return sync_io_hw_aux_supports_input(index);
    }
    if (mode == SYNC_IO_AUX_MODE_PIO_OUTPUT) {
        return sync_io_hw_aux_supports_output(index);
    }
    return false;
}

bool sync_io_aux_resource_busy(void)
{
    resource_arbiter_snapshot_t snapshot;
    resource_arbiter_get_snapshot(&snapshot);
    return (snapshot.active_resources & RESOURCE_ARBITER_RESOURCE_AUX) != 0u;
}

bool sync_io_core_initialized(void)
{
    return s_sync_io.initialized;
}

bool sync_io_core_capture_is_running(void)
{
    return s_sync_io.capture_running;
}

uint sync_io_core_biss_tap_offset(void)
{
    return s_sync_io.biss_tap_offset;
}

uint sync_io_core_aux_passthrough_offset(void)
{
    return s_sync_io.aux_passthrough_offset;
}

void sync_io_core_set_aux_mode(sync_io_aux_channel_t channel,
                               sync_io_aux_mode_t mode)
{
    if (sync_io_valid_aux_channel(channel)) {
        s_sync_io.aux_modes[(uint)channel] = mode;
    }
}

void sync_io_core_restore_aux_channel_input(uint pin)
{
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_IN);
    gpio_pull_down(pin);

    for (uint channel = 0u; channel < (uint)SYNC_IO_AUX_COUNT; channel++) {
        if (s_aux_pins[channel] == pin) {
            s_sync_io.aux_modes[channel] = SYNC_IO_AUX_MODE_INPUT;
        }
    }
}

void sync_io_core_mark_aux_channel_input(uint pin)
{
    for (uint channel = 0u; channel < (uint)SYNC_IO_AUX_COUNT; channel++) {
        if (s_aux_pins[channel] == pin) {
            s_sync_io.aux_modes[channel] = SYNC_IO_AUX_MODE_INPUT;
        }
    }
}

uint32_t sync_io_read_aux_level_mask(void)
{
    uint32_t mask = 0u;
    for (uint channel = 0u; channel < (uint)SYNC_IO_AUX_COUNT; channel++) {
        if (gpio_get(s_aux_pins[channel])) {
            mask |= (1u << channel);
        }
    }
    return mask;
}

uint32_t sync_io_read_aux_mode_mask(void)
{
    uint32_t mask = 0u;
    for (uint channel = 0u; channel < (uint)SYNC_IO_AUX_COUNT; channel++) {
        if (s_sync_io.aux_modes[channel] == SYNC_IO_AUX_MODE_PIO_OUTPUT) {
            mask |= (1u << channel);
        }
    }
    return mask;
}

uint32_t sync_io_read_ready_level_mask(void)
{
    return (gpio_get(BOARD_SYNC_TRIG_IN_PIN) ? (1u << 0) : 0u) |
           (gpio_get(BOARD_SYNC_ARM_IN_PIN) ? (1u << 1) : 0u) |
           (gpio_get(BOARD_SYNC_EXT_CLK_IN_PIN) ? (1u << 2) : 0u) |
           (gpio_get(BOARD_SYNC_GATE_IN_PIN) ? (1u << 3) : 0u) |
           (gpio_get(BOARD_SYNC_AUX_ARM_IN_PIN) ? (1u << 4) : 0u) |
           (gpio_get(BOARD_SYNC_AUX_EXT_CLK_IN_PIN) ? (1u << 5) : 0u) |
           (gpio_get(BOARD_SYNC_AUX_SYNC_CLK_OUT_PIN) ? (1u << 6) : 0u) |
           (gpio_get(BOARD_SYNC_AUX3_OUT_PIN) ? (1u << 7) : 0u);
}

bool sync_io_init(const sync_io_config_t *config)
{
    if (s_sync_io.initialized) {
        return true;
    }

    const uint32_t capture_hz = (config != NULL && config->capture_sample_hz != 0u)
                                    ? config->capture_sample_hz
                                    : SYNC_IO_DEFAULT_CAPTURE_HZ;
    const uint32_t clock_hz = (config != NULL && config->sync_clock_hz != 0u)
                                  ? config->sync_clock_hz
                                  : SYNC_IO_DEFAULT_CLOCK_HZ;

    bool pio_programs_fit =
        pio_can_add_program(BOARD_SYNC_PIO_FAST, &sync_capture_4bit_program) &&
        pio_can_add_program(BOARD_SYNC_PIO_FAST,
                            &sync_model_sched_pulse_high_program);
#if BOARD_SYNC_AUX_ENABLED
    pio_programs_fit =
        pio_programs_fit &&
        pio_can_add_program(SYNC_IO_CLOCK_PIO, &sync_clock_program) &&
        pio_can_add_program(BOARD_SYNC_PIO_AUX, &sync_aux_output_program) &&
        pio_can_add_program(BOARD_SYNC_PIO_AUX,
                            &sync_passthrough_1bit_program) &&
        pio_can_add_program(BOARD_SYNC_PIO_AUX, &biss_tap_rx_program);
#endif
    if (!pio_programs_fit) {
        LOG_ERROR("sync_io", "not enough PIO instruction memory");
        sync_io_trace(SYNC_IO_TRACE_INIT_FAIL, SYNC_IO_TRACE_ERROR, 1u, 0u);
        return false;
    }

    bool state_machines_claimed =
        sync_io_claim_sm(BOARD_SYNC_PIO_FAST,
                         BOARD_SYNC_CAPTURE_SM,
                         "capture");
#if BOARD_SYNC_AUX_ENABLED
    state_machines_claimed =
        state_machines_claimed &&
        sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX0_SM, "aux0") &&
        sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX1_SM, "aux1") &&
        sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX2_SM, "aux2") &&
        sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX3_SM, "aux3");
#endif
    if (!state_machines_claimed) {
        sync_io_trace(SYNC_IO_TRACE_INIT_FAIL, SYNC_IO_TRACE_ERROR, 2u, 0u);
        return false;
    }
    if (dma_channel_is_claimed(SYNC_IO_CAPTURE_DMA_CH)) {
        LOG_ERROR("sync_io", "capture DMA channel already claimed");
        sync_io_trace(SYNC_IO_TRACE_INIT_FAIL,
                      SYNC_IO_TRACE_ERROR,
                      3u,
                      SYNC_IO_CAPTURE_DMA_CH);
        return false;
    }
    dma_channel_claim(SYNC_IO_CAPTURE_DMA_CH);

    s_sync_io.capture_offset = (uint)pio_add_program(BOARD_SYNC_PIO_FAST, &sync_capture_4bit_program);
#if BOARD_SYNC_AUX_ENABLED
    s_sync_io.clock_offset = (uint)pio_add_program(SYNC_IO_CLOCK_PIO, &sync_clock_program);
    s_sync_io.aux_offset = (uint)pio_add_program(BOARD_SYNC_PIO_AUX, &sync_aux_output_program);
    s_sync_io.aux_passthrough_offset =
        (uint)pio_add_program(BOARD_SYNC_PIO_AUX, &sync_passthrough_1bit_program);
    s_sync_io.biss_tap_offset = (uint)pio_add_program(BOARD_SYNC_PIO_AUX, &biss_tap_rx_program);
#endif

    sync_io_configure_static_inputs();
    sync_io_capture_latch_timer_init();

    sync_capture_4bit_program_init(BOARD_SYNC_PIO_FAST,
                                   BOARD_SYNC_CAPTURE_SM,
                                   s_sync_io.capture_offset,
                                   BOARD_SYNC_INPUT_BASE_PIN,
                                   BOARD_SYNC_INPUT_PIN_COUNT,
                                   sync_io_clkdiv_for_instruction_rate(capture_hz));

#if BOARD_SYNC_AUX_ENABLED
    for (uint channel = 0u; channel < (uint)SYNC_IO_AUX_COUNT; channel++) {
        sync_aux_output_program_init(BOARD_SYNC_PIO_AUX,
                                     s_aux_sms[channel],
                                     s_sync_io.aux_offset,
                                     s_aux_pins[channel]);
        pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, s_aux_sms[channel], false);
        gpio_set_function(s_aux_pins[channel], GPIO_FUNC_SIO);
        gpio_set_dir(s_aux_pins[channel], GPIO_IN);
        gpio_pull_down(s_aux_pins[channel]);
        s_sync_io.aux_modes[channel] = SYNC_IO_AUX_MODE_INPUT;
    }

    sync_clock_program_init(SYNC_IO_CLOCK_PIO,
                            SYNC_IO_CLOCK_SM,
                            s_sync_io.clock_offset,
                            SYNC_IO_CLOCK_PIN,
                            sync_io_clkdiv_for_instruction_rate(clock_hz * 2u));
    sync_io_core_restore_aux_channel_input(SYNC_IO_CLOCK_PIN);
#endif

    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false);
#if BOARD_SYNC_AUX_ENABLED
    pio_sm_set_enabled(SYNC_IO_CLOCK_PIO, SYNC_IO_CLOCK_SM, false);
#endif

    s_sync_io.capture_sample_hz = capture_hz;
    s_sync_io.sync_clock_hz = clock_hz;
    s_sync_io.initialized = true;

    LOG_INFO("sync_io", "initialized capture=%luHz clock=%luHz",
             (unsigned long)capture_hz,
             (unsigned long)clock_hz);
    sync_io_trace(SYNC_IO_TRACE_INIT_OK,
                  SYNC_IO_TRACE_INFO,
                  capture_hz,
                  clock_hz);

    return true;
}

static bool sync_io_start_capture_owned(uint32_t sample_hz);
static void sync_io_stop_capture_owned(void);
bool sync_io_debug_set_output_mask_owned(uint32_t mask);
void sync_io_debug_release_output_mask_owned(void);
bool sync_io_sma_frequency_tx_start_owned(
    uint32_t output_channel,
    uint32_t frequency_hz,
    sync_io_sma_frequency_tx_status_t *status);
void sync_io_sma_frequency_tx_stop_owned(void);
bool sync_io_sma_frequency_rx_measure_owned(
    uint32_t input_channel,
    uint32_t gate_us,
    sync_io_sma_frequency_rx_result_t *result);

/* Capture-SM lease for the unified sequence base model (node_sequence): the model holds the
 * capture SM and the workspace arena for one armed run and restores the capture program after.
 * Both calls are Core1-only, matching the model's lifecycle contract. */
bool sync_io_core_capture_sm_lease(const void *owner)
{
    if (get_core_num() != 1u || !s_sync_io.initialized || owner == NULL ||
        !sync_io_workspace_claim(owner)) return false;
    /* The model needs the capture SM to be FREE, not pre-claimed: on an idle board nothing has
     * claimed it yet, and requiring a prior claim made every arm fail with RESOURCE. An enabled SM
     * or a busy DMA channel still refuses the lease. */
    if (s_sync_io.capture_running ||
        sync_io_core_sm_is_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM) ||
        dma_channel_is_busy(SYNC_IO_CAPTURE_DMA_CH)) {
        (void)sync_io_workspace_release(owner);
        return false;
    }
    return true;
}

void sync_io_core_capture_sm_restore(const void *owner)
{
    if (get_core_num() != 1u || !sync_io_workspace_held_by(owner)) return;
    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false);
    sync_capture_4bit_program_init(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM,
        s_sync_io.capture_offset, BOARD_SYNC_INPUT_BASE_PIN, BOARD_SYNC_INPUT_PIN_COUNT,
        sync_io_clkdiv_for_instruction_rate(s_sync_io.capture_sample_hz));
    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false);
    (void)sync_io_workspace_release(owner);
}

/* Why the sequence base model cannot take the SMA outputs right now. Mirrors the model's own
 * outputs_available() preconditions so the owner of the resolver can name the blocker instead of
 * only counting failures. Bit 0 uninitialised, 1 seq-step running, 2 enc-count running,
 * 3 model output active, 4 wave output persona active, 5 an SMA pin is not SIO/NULL,
 * 6 an SMA output is already high. */
uint32_t sync_io_sequence_outputs_blockers(void)
{
    uint32_t reason = 0u;
    if (!s_sync_io.initialized) reason |= 1u << 0;
    if (sync_io_seq_step_is_active()) reason |= 1u << 1;
    if (sync_io_enc_count_is_active()) reason |= 1u << 2;
    if (sync_io_core_model_pulse_active()) reason |= 1u << 3;
    if (sync_io_core_wave_output_persona_active()) reason |= 1u << 4;
    for (uint32_t i = 0u; i < BOARD_SYNC_OUTPUT_PIN_COUNT; ++i) {
        const gpio_function_t fn = gpio_get_function(BOARD_SYNC_OUTPUT_BASE_PIN + i);
        if (fn != GPIO_FUNC_SIO && fn != GPIO_FUNC_NULL) { reason |= 1u << 5; break; }
    }
    for (uint32_t i = 0u; i < BOARD_SYNC_OUTPUT_PIN_COUNT; ++i) {
        if (gpio_get(BOARD_SYNC_OUTPUT_BASE_PIN + i)) { reason |= 1u << 6; break; }
    }
    return reason;
}

static bool sync_io_start_capture_owned(uint32_t sample_hz)
{
    if (!s_sync_io.initialized ||
        sync_io_core_model_output_active() ||
        sync_io_core_wave_output_persona_active_owned()) {
        sync_io_trace(SYNC_IO_TRACE_CAPTURE_FAIL, SYNC_IO_TRACE_ERROR, sample_hz, 1u);
        return false;
    }

    if (sample_hz == 0u) {
        sample_hz = s_sync_io.capture_sample_hz;
    }
    if (sync_io_workspace_held_by(&s_sync_io)) {
        sync_io_stop_capture_owned();
    }
    if (!sync_io_workspace_claim(&s_sync_io)) {
        sync_io_trace(SYNC_IO_TRACE_CAPTURE_FAIL, SYNC_IO_TRACE_ERROR,
                      sample_hz, SYNC_IO_PERSONA_CONFLICT_WORKSPACE);
        return false;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM);
    pio_sm_set_clkdiv(BOARD_SYNC_PIO_FAST,
                      BOARD_SYNC_CAPTURE_SM,
                      sync_io_clkdiv_for_instruction_rate(sample_hz));
    pio_sm_restart(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM);
    if (!sync_io_capture_dma_configure()) {
        sync_io_trace(SYNC_IO_TRACE_CAPTURE_FAIL,
                      SYNC_IO_TRACE_ERROR,
                      sample_hz,
                      2u);
        (void)sync_io_workspace_release(&s_sync_io);
        return false;
    }

    const uint64_t capture_start_ns = sync_io_common_time_now_ns();
    if (capture_start_ns == UINT64_MAX) {
        (void)sync_io_workspace_release(&s_sync_io);
        return false;
    }

    osal_critical_enter();
    sync_io_capture_latch_reset_locked();
    s_sync_io.dropped_capture_words = 0u;
    s_sync_io.capture_dma_read_seq = 0u;
    s_sync_io.capture_dma_produced_seq = 0u;
    s_sync_io.capture_dma_last_write_index = 0u;
    s_sync_io.capture_dma_write_index_valid = true;
    s_sync_io.capture_timebase_start_ns = capture_start_ns;
    s_sync_io.capture_timebase_valid = true;
    osal_critical_exit();

    s_sync_io.capture_sample_hz = sample_hz;
    s_sync_io.capture_running = true;
    dma_start_channel_mask(1u << SYNC_IO_CAPTURE_DMA_CH);
    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, true);

    sync_io_trace(SYNC_IO_TRACE_CAPTURE_START, SYNC_IO_TRACE_INFO, sample_hz, 0u);
    return true;
}

bool sync_io_start_capture(uint32_t sample_hz)
{
    if (!sync_io_core_legacy_try_enter()) return false;
    const bool result = sync_io_start_capture_owned(sample_hz);
    sync_io_core_legacy_leave();
    return result;
}

static void sync_io_stop_capture_owned(void)
{
    /* An idle STOP must not abort another owner's DMA3. */
    if (!s_sync_io.initialized || !sync_io_workspace_held_by(&s_sync_io)) {
        return;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false);
    dma_channel_abort(SYNC_IO_CAPTURE_DMA_CH);
    osal_critical_enter();
    s_sync_io.capture_running = false;
    s_sync_io.capture_timebase_valid = false;
    s_sync_io.capture_dma_write_index_valid = false;
    osal_critical_exit();
    (void)sync_io_workspace_release(&s_sync_io);
    sync_io_trace(SYNC_IO_TRACE_CAPTURE_STOP,
                  SYNC_IO_TRACE_INFO,
                  s_sync_io.capture_sample_hz,
                  s_sync_io.dropped_capture_words);
}

void sync_io_stop_capture(void)
{
    if (!sync_io_core_legacy_try_enter()) return;
    sync_io_stop_capture_owned();
    sync_io_core_legacy_leave();
}

static bool sync_io_capture_dma_pop(uint32_t *raw_word,
                                    uint32_t *capture_word_seq)
{
    if (raw_word == NULL || capture_word_seq == NULL ||
        !s_sync_io.capture_running) {
        return false;
    }

    const uint32_t produced = sync_io_capture_dma_produced_words();
    bool overflowed = false;
    uint32_t overflow_count = 0u;

    osal_critical_enter();
    uint32_t read_seq = s_sync_io.capture_dma_read_seq;
    uint32_t available = produced - read_seq;
    if (available > SYNC_IO_CAPTURE_DMA_RING_WORDS) {
        overflow_count = available - SYNC_IO_CAPTURE_DMA_RING_WORDS;
        s_sync_io.dropped_capture_words += overflow_count;
        s_sync_io.capture_latch_edge_state_valid = false;
        read_seq = produced - SYNC_IO_CAPTURE_DMA_RING_WORDS;
        s_sync_io.capture_dma_read_seq = read_seq;
        available = SYNC_IO_CAPTURE_DMA_RING_WORDS;
        overflowed = true;
    }
    if (available == 0u) {
        osal_critical_exit();
        return false;
    }

    *capture_word_seq = read_seq;
    *raw_word = sync_io_map_capture_word(
        s_sync_io_capture_dma_ring[read_seq &
                                   (SYNC_IO_CAPTURE_DMA_RING_WORDS - 1u)]);
    s_sync_io.capture_dma_read_seq = read_seq + 1u;
    osal_critical_exit();

    if (overflowed) {
        sync_io_trace(SYNC_IO_TRACE_CAPTURE_DROP,
                      SYNC_IO_TRACE_WARN,
                      s_sync_io.dropped_capture_words,
                      overflow_count);
    }
    return true;
}

size_t sync_io_read_capture_words(uint32_t *buffer, size_t max_words)
{
    if (!s_sync_io.initialized || buffer == NULL || max_words == 0u) {
        return 0u;
    }

    size_t count = 0u;
    while (count < max_words) {
        uint32_t capture_word_seq = 0u;
        if (!sync_io_capture_dma_pop(&buffer[count], &capture_word_seq)) {
            break;
        }
        (void)capture_word_seq;
        count++;
    }

    return count;
}

void sync_io_capture_latch_service_core1(void)
{
    if (!s_sync_io.initialized || !s_sync_io.capture_running) {
        return;
    }

    const uint32_t sample_period_ns =
        sync_io_capture_sample_period_ns(s_sync_io.capture_sample_hz);
    const uint64_t word_span_ns =
        (uint64_t)sample_period_ns * SYNC_IO_CAPTURE_WORD_SAMPLES;
    const uint32_t produced = sync_io_capture_dma_produced_words();

    sync_io_capture_latch_discard_outside_window(produced, word_span_ns);

    for (uint32_t processed = 0u;
         processed < SYNC_IO_CAPTURE_LATCH_SERVICE_MAX_WORDS;
         processed++) {
        uint32_t raw_word = 0u;
        uint32_t capture_word_seq = 0u;
        if (!sync_io_capture_dma_pop(&raw_word, &capture_word_seq)) {
            break;
        }

        osal_critical_enter();
        const uint64_t capture_start_ns = s_sync_io.capture_timebase_start_ns;
        osal_critical_exit();
        const uint64_t word_start_ns =
            capture_start_ns + (uint64_t)capture_word_seq * word_span_ns;
        const uint64_t word_end_ns = word_start_ns + word_span_ns;
        const uint32_t base_time_l32_ns =
            (uint32_t)word_start_ns;
        uint64_t matched_window_start_ns = 0u;
        uint32_t observed_mask_for_edge = 0u;

        osal_critical_enter();
        if (s_sync_io.capture_timestamp_window_armed) {
            observed_mask_for_edge =
                s_sync_io.capture_timestamp_window_observed_mask;
        }
        osal_critical_exit();
        uint32_t word_previous_sample_mask = 0u;
        if (observed_mask_for_edge != 0u &&
            !sync_io_capture_word_has_observed_edge(raw_word,
                                                    observed_mask_for_edge,
                                                    &word_previous_sample_mask)) {
            continue;
        }

        osal_critical_enter();
        const uint32_t next_write =
            (s_sync_io.capture_latch_write + 1u) % SYNC_IO_CAPTURE_LATCH_RING_SIZE;
        if (next_write == s_sync_io.capture_latch_read) {
            s_sync_io.dropped_latched_capture_words++;
            osal_critical_exit();
            continue;
        }

        sync_io_capture_latched_word_t *slot =
            &s_sync_io.capture_latch_ring[s_sync_io.capture_latch_write];
        slot->raw_word = raw_word;
        slot->sample_seq = ++s_sync_io.capture_latch_seq;
        slot->previous_sample_mask = word_previous_sample_mask;
        slot->base_time_l32_ns = base_time_l32_ns;
        slot->sample_period_ns = sample_period_ns;
        slot->timestamp_source = SYNC_IO_CAPTURE_TIMESTAMP_SOURCE_HARDWARE_TICK;
        slot->timestamp_resolution_ns =
            sample_period_ns > s_sync_io.capture_latch_resolution_ns
                ? sample_period_ns
                : s_sync_io.capture_latch_resolution_ns;
        slot->timestamp_flags =
            sync_io_capture_latch_flags_for_word(word_start_ns,
                                                 word_end_ns,
                                                 sample_period_ns,
                                                 &matched_window_start_ns);
        slot->matched_window_start_ns = matched_window_start_ns;
        slot->dropped_before =
            UINT32_MAX - s_sync_io.dropped_capture_words <
                    s_sync_io.dropped_latched_capture_words
                ? UINT32_MAX
                : s_sync_io.dropped_capture_words +
                      s_sync_io.dropped_latched_capture_words;
        s_sync_io.capture_latch_write = next_write;
        s_sync_io.latched_capture_words++;
        osal_critical_exit();
    }
}

size_t sync_io_read_capture_latched(sync_io_capture_latched_word_t *buffer,
                                    size_t max_words)
{
    if (!s_sync_io.initialized || buffer == NULL || max_words == 0u) {
        return 0u;
    }

    size_t count = 0u;
    osal_critical_enter();
    while (count < max_words &&
           s_sync_io.capture_latch_read != s_sync_io.capture_latch_write) {
        buffer[count] =
            s_sync_io.capture_latch_ring[s_sync_io.capture_latch_read];
        s_sync_io.capture_latch_read =
            (s_sync_io.capture_latch_read + 1u) % SYNC_IO_CAPTURE_LATCH_RING_SIZE;
        count++;
    }
    osal_critical_exit();
    return count;
}

bool sync_io_capture_time_now_ns(uint64_t *now_ns)
{
    if (!s_sync_io.initialized || now_ns == NULL) {
        return false;
    }

    const uint64_t now = sync_io_common_time_now_ns();
    if (now == UINT64_MAX) return false;
    *now_ns = now;
    return true;
}

bool sync_io_capture_arm_timestamp_window(uint64_t window_start_ns,
                                          uint32_t window_width_ns,
                                          uint32_t sample_period_ns,
                                          uint32_t observed_mask,
                                          uint32_t initial_sample_mask)
{
    return sync_io_capture_arm_periodic_timestamp_window(window_start_ns,
                                                        window_width_ns,
                                                        0u,
                                                        sample_period_ns,
                                                        observed_mask,
                                                        initial_sample_mask);
}

bool sync_io_capture_arm_periodic_timestamp_window(uint64_t window_start_ns,
                                                   uint32_t window_width_ns,
                                                   uint32_t period_ns,
                                                   uint32_t sample_period_ns,
                                                   uint32_t observed_mask,
                                                   uint32_t initial_sample_mask)
{
    if (!s_sync_io.initialized ||
        window_width_ns == 0u ||
        sample_period_ns == 0u ||
        observed_mask == 0u ||
        (period_ns != 0u && period_ns < window_width_ns) ||
        (observed_mask & ~0x0Fu) != 0u ||
        (initial_sample_mask & ~0x0Fu) != 0u) {
        return false;
    }

    osal_critical_enter();
    s_sync_io.capture_timestamp_window_armed = true;
    s_sync_io.capture_timestamp_window_periodic = period_ns != 0u;
    s_sync_io.capture_timestamp_window_start_ns = window_start_ns;
    s_sync_io.capture_timestamp_window_end_ns =
        window_start_ns + (uint64_t)window_width_ns;
    s_sync_io.capture_timestamp_window_period_ns = period_ns;
    s_sync_io.capture_timestamp_window_sample_period_ns = sample_period_ns;
    s_sync_io.capture_timestamp_window_observed_mask = observed_mask & 0x0Fu;
    s_sync_io.capture_timestamp_window_initial_sample_mask =
        initial_sample_mask & observed_mask & 0x0Fu;
    osal_critical_exit();
    return true;
}

void sync_io_capture_disarm_timestamp_window(void)
{
    osal_critical_enter();
    s_sync_io.capture_timestamp_window_armed = false;
    s_sync_io.capture_timestamp_window_periodic = false;
    s_sync_io.capture_timestamp_window_start_ns = 0u;
    s_sync_io.capture_timestamp_window_end_ns = 0u;
    s_sync_io.capture_timestamp_window_period_ns = 0u;
    s_sync_io.capture_timestamp_window_sample_period_ns = 0u;
    s_sync_io.capture_timestamp_window_observed_mask = 0u;
    s_sync_io.capture_timestamp_window_initial_sample_mask = 0u;
    osal_critical_exit();
}
