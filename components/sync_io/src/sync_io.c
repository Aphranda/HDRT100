#include "sync_io.h"

#include "board_config.h"
#include "diagnostics.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "seq_step.pio.h"
#include "sync_io.pio.h"

#define SYNC_IO_DEFAULT_CAPTURE_HZ  1000000u
#define SYNC_IO_DEFAULT_CLOCK_HZ    1000000u
#define SYNC_IO_MIN_HZ              1u
#define SYNC_IO_SEQ_STEP_DMA_CH     0u
#define SYNC_IO_SEQ_STEP_DMA_IRQ    DMA_IRQ_0

typedef struct {
    bool initialized;
    bool capture_running;
    bool clock_running;
    uint capture_offset;
    uint pulse_offset;
    uint clock_offset;
    uint aux_offset;
    uint32_t capture_sample_hz;
    uint32_t sync_clock_hz;
    uint32_t dropped_capture_words;
    sync_io_aux_mode_t aux_modes[SYNC_IO_AUX_COUNT];
} sync_io_context_t;

static sync_io_context_t s_sync_io;

/* ── SEQ_STEP 状态 ── */

typedef struct {
    bool running;
    bool has_gate;
    uint seq_sm;
    uint seq_offset;
    uint dma_ch;
    uint seq_length;
    uint seq_width;
    volatile uint32_t rollover_count;
    volatile bool dma_done;
} sync_io_seq_step_t;

static sync_io_seq_step_t s_seq_step;

static const uint s_aux_pins[SYNC_IO_AUX_COUNT] = {
    BOARD_SYNC_AUX0_PIN,
    BOARD_SYNC_AUX1_PIN,
    BOARD_SYNC_AUX2_PIN,
    BOARD_SYNC_AUX3_PIN,
};

static const uint s_aux_sms[SYNC_IO_AUX_COUNT] = {
    BOARD_SYNC_AUX0_SM,
    BOARD_SYNC_AUX1_SM,
    BOARD_SYNC_AUX2_SM,
    BOARD_SYNC_AUX3_SM,
};

static float sync_io_clkdiv_for_instruction_rate(uint32_t instruction_hz)
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

static bool sync_io_valid_aux_channel(sync_io_aux_channel_t channel)
{
    return (uint)channel < (uint)SYNC_IO_AUX_COUNT;
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

    if (!pio_can_add_program(BOARD_SYNC_PIO_FAST, &sync_capture_4bit_program) ||
        !pio_can_add_program(BOARD_SYNC_PIO_WAVE, &sync_pulse_program) ||
        !pio_can_add_program(BOARD_SYNC_PIO_WAVE, &sync_clock_program) ||
        !pio_can_add_program(BOARD_SYNC_PIO_AUX, &sync_aux_output_program)) {
        LOG_ERROR("sync_io", "not enough PIO instruction memory");
        return false;
    }

    if (!sync_io_claim_sm(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, "capture") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_OUTPUT_SM, "output") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_CLOCK_SM, "clock") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_GATE_SM, "pulse") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_MARKER_SM, "marker") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX0_SM, "aux0") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX1_SM, "aux1") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX2_SM, "aux2") ||
        !sync_io_claim_sm(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX3_SM, "aux3")) {
        return false;
    }

    s_sync_io.capture_offset = (uint)pio_add_program(BOARD_SYNC_PIO_FAST, &sync_capture_4bit_program);
    s_sync_io.pulse_offset = (uint)pio_add_program(BOARD_SYNC_PIO_WAVE, &sync_pulse_program);
    s_sync_io.clock_offset = (uint)pio_add_program(BOARD_SYNC_PIO_WAVE, &sync_clock_program);
    s_sync_io.aux_offset = (uint)pio_add_program(BOARD_SYNC_PIO_AUX, &sync_aux_output_program);

    sync_io_configure_static_inputs();

    sync_capture_4bit_program_init(BOARD_SYNC_PIO_FAST,
                                   BOARD_SYNC_CAPTURE_SM,
                                   s_sync_io.capture_offset,
                                   BOARD_SYNC_INPUT_BASE_PIN,
                                   BOARD_SYNC_INPUT_PIN_COUNT,
                                   sync_io_clkdiv_for_instruction_rate(capture_hz));

    sync_pulse_program_init(BOARD_SYNC_PIO_WAVE,
                            BOARD_SYNC_OUTPUT_SM,
                            s_sync_io.pulse_offset,
                            BOARD_SYNC_TRIG_OUT_PIN);

    sync_pulse_program_init(BOARD_SYNC_PIO_WAVE,
                            BOARD_SYNC_GATE_SM,
                            s_sync_io.pulse_offset,
                            BOARD_SYNC_PULSE_OUT_PIN);

    sync_clock_program_init(BOARD_SYNC_PIO_WAVE,
                            BOARD_SYNC_CLOCK_SM,
                            s_sync_io.clock_offset,
                            BOARD_SYNC_SYNC_CLK_OUT_PIN,
                            sync_io_clkdiv_for_instruction_rate(clock_hz * 2u));

    sync_pulse_program_init(BOARD_SYNC_PIO_WAVE,
                            BOARD_SYNC_MARKER_SM,
                            s_sync_io.pulse_offset,
                            BOARD_SYNC_MARKER_OUT_PIN);

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

    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_OUTPUT_SM, true);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_GATE_SM, true);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_CLOCK_SM, false);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_MARKER_SM, true);

    s_sync_io.capture_sample_hz = capture_hz;
    s_sync_io.sync_clock_hz = clock_hz;
    s_sync_io.initialized = true;

    LOG_INFO("sync_io", "initialized capture=%luHz clock=%luHz",
             (unsigned long)capture_hz,
             (unsigned long)clock_hz);

    return true;
}

bool sync_io_start_capture(uint32_t sample_hz)
{
    if (!s_sync_io.initialized) {
        return false;
    }

    if (sample_hz == 0u) {
        sample_hz = s_sync_io.capture_sample_hz;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM);
    pio_sm_set_clkdiv(BOARD_SYNC_PIO_FAST,
                      BOARD_SYNC_CAPTURE_SM,
                      sync_io_clkdiv_for_instruction_rate(sample_hz));
    pio_sm_restart(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM);
    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, true);

    s_sync_io.capture_sample_hz = sample_hz;
    s_sync_io.capture_running = true;
    return true;
}

void sync_io_stop_capture(void)
{
    if (!s_sync_io.initialized) {
        return;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM, false);
    s_sync_io.capture_running = false;
}

size_t sync_io_read_capture_words(uint32_t *buffer, size_t max_words)
{
    if (!s_sync_io.initialized || buffer == NULL || max_words == 0u) {
        return 0u;
    }

    size_t count = 0u;
    while (count < max_words && !pio_sm_is_rx_fifo_empty(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM)) {
        buffer[count] = pio_sm_get(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM);
        count++;
    }

    if (pio_sm_is_rx_fifo_full(BOARD_SYNC_PIO_FAST, BOARD_SYNC_CAPTURE_SM)) {
        s_sync_io.dropped_capture_words++;
    }

    return count;
}

static bool sync_io_fire_pulse_on_sm(uint sm, uint32_t high_cycles)
{
    if (!s_sync_io.initialized || high_cycles == 0u) {
        return false;
    }

    if (pio_sm_is_tx_fifo_full(BOARD_SYNC_PIO_WAVE, sm)) {
        return false;
    }

    pio_sm_put(BOARD_SYNC_PIO_WAVE, sm, high_cycles - 1u);
    return true;
}

static bool sync_io_fire_pulse_us_on_sm(uint sm, uint32_t high_us)
{
    if (high_us == 0u) {
        return false;
    }

    const uint32_t sys_hz = clock_get_hz(clk_sys);
    uint64_t cycles = ((uint64_t)sys_hz * (uint64_t)high_us) / 1000000ull;
    if (cycles == 0u) {
        cycles = 1u;
    }
    if (cycles > UINT32_MAX) {
        cycles = UINT32_MAX;
    }

    return sync_io_fire_pulse_on_sm(sm, (uint32_t)cycles);
}

bool sync_io_fire_pulse_cycles(uint32_t high_cycles)
{
    return sync_io_fire_pulse_on_sm(BOARD_SYNC_OUTPUT_SM, high_cycles);
}

bool sync_io_fire_pulse_us(uint32_t high_us)
{
    return sync_io_fire_pulse_us_on_sm(BOARD_SYNC_OUTPUT_SM, high_us);
}

bool sync_io_fire_pulse_out_cycles(uint32_t high_cycles)
{
    return sync_io_fire_pulse_on_sm(BOARD_SYNC_GATE_SM, high_cycles);
}

bool sync_io_fire_pulse_out_us(uint32_t high_us)
{
    return sync_io_fire_pulse_us_on_sm(BOARD_SYNC_GATE_SM, high_us);
}

bool sync_io_start_clock(uint32_t frequency_hz)
{
    if (!s_sync_io.initialized || frequency_hz == 0u) {
        return false;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_CLOCK_SM, false);
    pio_sm_set_clkdiv(BOARD_SYNC_PIO_WAVE,
                      BOARD_SYNC_CLOCK_SM,
                      sync_io_clkdiv_for_instruction_rate(frequency_hz * 2u));
    pio_sm_restart(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_CLOCK_SM);
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_CLOCK_SM, true);

    s_sync_io.sync_clock_hz = frequency_hz;
    s_sync_io.clock_running = true;
    return true;
}

void sync_io_stop_clock(void)
{
    if (!s_sync_io.initialized) {
        return;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_CLOCK_SM, false);
    pio_sm_set_pins(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_CLOCK_SM, 0);
    s_sync_io.clock_running = false;
}

bool sync_io_fire_marker_cycles(uint32_t high_cycles)
{
    return sync_io_fire_pulse_on_sm(BOARD_SYNC_MARKER_SM, high_cycles);
}

bool sync_io_fire_marker_us(uint32_t high_us)
{
    return sync_io_fire_pulse_us_on_sm(BOARD_SYNC_MARKER_SM, high_us);
}

bool sync_io_aux_set_mode(sync_io_aux_channel_t channel, sync_io_aux_mode_t mode)
{
    if (!s_sync_io.initialized || !sync_io_valid_aux_channel(channel)) {
        return false;
    }

    const uint index = (uint)channel;
    const uint sm = s_aux_sms[index];
    const uint pin = s_aux_pins[index];

    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, sm, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_AUX, sm);
    pio_sm_restart(BOARD_SYNC_PIO_AUX, sm);

    if (mode == SYNC_IO_AUX_MODE_INPUT) {
        gpio_set_function(pin, GPIO_FUNC_SIO);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_down(pin);
        s_sync_io.aux_modes[index] = mode;
        return true;
    }

    if (mode == SYNC_IO_AUX_MODE_PIO_OUTPUT) {
        pio_gpio_init(BOARD_SYNC_PIO_AUX, pin);
        pio_sm_set_consecutive_pindirs(BOARD_SYNC_PIO_AUX, sm, pin, 1, true);
        pio_sm_put(BOARD_SYNC_PIO_AUX, sm, 0u);
        pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, sm, true);
        s_sync_io.aux_modes[index] = mode;
        return true;
    }

    return false;
}

bool sync_io_aux_write(sync_io_aux_channel_t channel, bool level)
{
    if (!s_sync_io.initialized || !sync_io_valid_aux_channel(channel)) {
        return false;
    }

    const uint index = (uint)channel;
    if (s_sync_io.aux_modes[index] != SYNC_IO_AUX_MODE_PIO_OUTPUT) {
        return false;
    }

    const uint sm = s_aux_sms[index];
    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, sm, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_AUX, sm);
    pio_sm_restart(BOARD_SYNC_PIO_AUX, sm);
    pio_sm_put(BOARD_SYNC_PIO_AUX, sm, level ? 1u : 0u);
    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, sm, true);
    return true;
}

bool sync_io_aux_read(sync_io_aux_channel_t channel, bool *level)
{
    if (!s_sync_io.initialized || !sync_io_valid_aux_channel(channel) || level == NULL) {
        return false;
    }

    *level = gpio_get(s_aux_pins[(uint)channel]) != 0;
    return true;
}

void sync_io_get_status(sync_io_status_t *status)
{
    if (status == NULL) {
        return;
    }

    status->initialized = s_sync_io.initialized;
    status->capture_running = s_sync_io.capture_running;
    status->sync_clock_running = s_sync_io.clock_running;
    status->capture_sample_hz = s_sync_io.capture_sample_hz;
    status->sync_clock_hz = s_sync_io.sync_clock_hz;
    status->dropped_capture_words = s_sync_io.dropped_capture_words;
}

/* ── SEQ_STEP 编码序列步进 ── */

static void sync_io_seq_step_dma_handler(void)
{
    dma_hw->ints0 = 1u << s_seq_step.dma_ch;
    s_seq_step.dma_done = true;
    s_seq_step.rollover_count++;
    /* ring-buffer DMA 自动回绕，无需手动重置读地址 */
}

bool sync_io_seq_step_arm(const uint32_t *seq_table,
                          uint32_t seq_length,
                          uint32_t seq_width,
                          uint32_t trigger_pin,
                          sync_io_edge_t edge,
                          bool gate_enabled)
{
    if (!s_sync_io.initialized ||
        seq_table == NULL ||
        seq_length == 0u ||
        seq_length > 256u ||
        seq_width == 0u ||
        seq_width > 8u) {
        return false;
    }

    if (s_seq_step.running) {
        sync_io_seq_step_disarm();
    }

    /* ── PIO 程序选择 ── */

    const pio_program_t *prog = gate_enabled
        ? &seq_step_gated_program
        : &seq_step_program;

    if (!pio_can_add_program(BOARD_SYNC_PIO_WAVE, prog)) {
        LOG_ERROR("sync_io", "seq_step: not enough PIO instruction space");
        return false;
    }

    s_seq_step.seq_offset = (uint)pio_add_program(BOARD_SYNC_PIO_WAVE, prog);
    s_seq_step.seq_sm = BOARD_SYNC_OUTPUT_SM;
    s_seq_step.has_gate = gate_enabled;

    /* 停旧 SM，清 FIFO */
    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);
    pio_sm_restart(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);

    /* ── PIO 初始化（HAOFV 实时面配置, ARM 时一次性完成）── */

    seq_step_program_init_ex(BOARD_SYNC_PIO_WAVE,
                              s_seq_step.seq_sm,
                              s_seq_step.seq_offset,
                              trigger_pin,
                              BOARD_SYNC_OUTPUT_BASE_PIN,
                              seq_width,
                              1.0f,
                              gate_enabled,
                              (int)edge);

    /* ── DMA 配置: SRAM seq_table → PIO TX FIFO, ring-buffer ── */

    s_seq_step.dma_ch = SYNC_IO_SEQ_STEP_DMA_CH;
    dma_channel_unclaim(s_seq_step.dma_ch);

    s_seq_step.seq_length = seq_length;
    s_seq_step.seq_width = seq_width;

    dma_channel_config dma_cfg = dma_channel_get_default_config(s_seq_step.dma_ch);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg,
                            DREQ_PIO1_TX0 + s_seq_step.seq_sm);
    channel_config_set_ring(&dma_cfg, false,
                            (uint)(sizeof(uint32_t) * seq_length));
    dma_channel_configure(s_seq_step.dma_ch,
                          &dma_cfg,
                          &BOARD_SYNC_PIO_WAVE->txf[s_seq_step.seq_sm],
                          seq_table,
                          seq_length,
                          true);   /* 立即启动, ring-buffer 持续循环 */

    /* ── DMA 完成中断（每次回绕触发一次）── */

    s_seq_step.rollover_count = 0u;
    s_seq_step.dma_done = false;

    irq_set_exclusive_handler(SYNC_IO_SEQ_STEP_DMA_IRQ,
                              sync_io_seq_step_dma_handler);
    dma_channel_set_irq0_enabled(s_seq_step.dma_ch, true);
    irq_set_enabled(SYNC_IO_SEQ_STEP_DMA_IRQ, true);

    /* ── 启动 PIO ── */

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm, true);

    s_seq_step.running = true;

    LOG_INFO("sync_io", "seq_step armed: length=%lu width=%lu",
             (unsigned long)seq_length, (unsigned long)seq_width);

    return true;
}

void sync_io_seq_step_disarm(void)
{
    if (!s_seq_step.running) {
        return;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm, false);
    irq_set_enabled(SYNC_IO_SEQ_STEP_DMA_IRQ, false);
    dma_channel_set_irq0_enabled(s_seq_step.dma_ch, false);
    dma_channel_abort(s_seq_step.dma_ch);

    pio_sm_clear_fifos(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);
    pio_sm_set_pins(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm, 0);

    /* 恢复为默认输出低 */
    for (uint pin = 0u; pin < s_seq_step.seq_width; pin++) {
        gpio_set_function(BOARD_SYNC_OUTPUT_BASE_PIN + pin, GPIO_FUNC_SIO);
        gpio_set_dir(BOARD_SYNC_OUTPUT_BASE_PIN + pin, GPIO_OUT);
        gpio_put(BOARD_SYNC_OUTPUT_BASE_PIN + pin, false);
    }

    const pio_program_t *prog_to_remove = s_seq_step.has_gate
        ? &seq_step_gated_program
        : &seq_step_program;
    pio_remove_program(BOARD_SYNC_PIO_WAVE, prog_to_remove,
                       s_seq_step.seq_offset);

    s_seq_step.running = false;
    LOG_INFO("sync_io", "seq_step disarmed: rollover_count=%lu",
             (unsigned long)s_seq_step.rollover_count);
}

uint32_t sync_io_seq_step_get_index(void)
{
    if (!s_seq_step.running) {
        return 0u;
    }

    const uint32_t remaining = dma_hw->ch[s_seq_step.dma_ch].transfer_count
                               & 0xFFFFu;
    const uint32_t idx = s_seq_step.seq_length - remaining;

    return (idx >= s_seq_step.seq_length) ? 0u : idx;
}

bool sync_io_seq_step_is_running(void)
{
    return s_seq_step.running;
}

uint32_t sync_io_seq_step_get_rollover_count(void)
{
    return s_seq_step.rollover_count;
}
