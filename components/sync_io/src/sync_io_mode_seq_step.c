#include "sync_io_mode_seq_step.h"

#include <stddef.h>
#include <stdint.h>

#include "board_config.h"
#include "diagnostics.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "seq_step.pio.h"
#include "sync_io_core_internal.h"
#include "sync_io_hw_profile.h"

#define SYNC_IO_SEQ_STEP_MODE_MAX_LENGTH 256u
#define SYNC_IO_SEQ_STEP_MODE_MAX_WIDTH  8u

typedef struct {
    bool running;
    bool has_gate;
    uint seq_sm;
    uint seq_offset;
    uint dma_ch;
    uint seq_length;
    uint seq_width;
    uintptr_t seq_table_addr;
    volatile uint64_t rollover_count;
    volatile bool dma_done;
} sync_io_seq_step_t;

static sync_io_seq_step_t s_seq_step;
static uint32_t s_seq_last_runtime_flags;
static uint32_t s_seq_last_transfer_count;
static uint32_t s_seq_last_rollover_low32;
static bool s_seq_dma_overflow_latched;
static bool s_seq_trace_sample_valid;

static bool sync_io_seq_step_trigger_pin_valid(uint32_t trigger_pin)
{
    const uint32_t first = SYNC_IO_HW_MAIN_INPUT_BASE_PIN;
    const uint32_t last = first + SYNC_IO_HW_MAIN_INPUT_PIN_COUNT - 1u;

    return trigger_pin >= first && trigger_pin <= last;
}

bool sync_io_seq_step_mode_validate(const sync_io_seq_step_mode_config_t *config)
{
    if (config == NULL ||
        config->seq_table == NULL ||
        config->seq_length == 0u ||
        config->seq_length > SYNC_IO_SEQ_STEP_MODE_MAX_LENGTH ||
        config->seq_width == 0u ||
        config->seq_width > SYNC_IO_SEQ_STEP_MODE_MAX_WIDTH) {
        return false;
    }

    if (config->edge != SYNC_IO_EDGE_RISING &&
        config->edge != SYNC_IO_EDGE_FALLING) {
        return false;
    }

    if (config->gate_enabled &&
        !sync_io_seq_step_trigger_pin_valid(config->trigger_pin)) {
        return false;
    }

    return true;
}

bool sync_io_seq_step_mode_arm(const sync_io_seq_step_mode_config_t *config)
{
    if (!sync_io_seq_step_mode_validate(config)) {
        return false;
    }

    return sync_io_seq_step_arm(config->seq_table,
                                config->seq_length,
                                config->seq_width,
                                config->trigger_pin,
                                config->edge,
                                config->gate_enabled);
}

bool sync_io_seq_step_dma_irq_service(uint32_t ints)
{
    if ((ints & (1u << s_seq_step.dma_ch)) == 0u || !s_seq_step.running) {
        return false;
    }

    s_seq_step.rollover_count++;
    dma_hw->ch[s_seq_step.dma_ch].read_addr = s_seq_step.seq_table_addr;
    dma_hw->ch[s_seq_step.dma_ch].al1_transfer_count_trig =
        s_seq_step.seq_length;
    return true;
}

bool sync_io_seq_step_arm(const uint32_t *seq_table,
                          uint32_t seq_length,
                          uint32_t seq_width,
                          uint32_t trigger_pin,
                          sync_io_edge_t edge,
                          bool gate_enabled)
{
    if (!sync_io_core_initialized() ||
        seq_table == NULL ||
        seq_length == 0u ||
        seq_length > SYNC_IO_SEQ_STEP_MODE_MAX_LENGTH ||
        seq_width == 0u ||
        seq_width > SYNC_IO_SEQ_STEP_MODE_MAX_WIDTH) {
        sync_io_core_trace(SYNC_IO_TRACE_SEQ_ARM_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           seq_length,
                           seq_width);
        return false;
    }

    if (s_seq_step.running) {
        sync_io_seq_step_disarm();
    } else {
        dma_channel_abort(SYNC_IO_SEQ_STEP_DMA_CH);
        pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_OUTPUT_SM, false);
        pio_sm_clear_fifos(BOARD_SYNC_PIO_WAVE, BOARD_SYNC_OUTPUT_SM);
    }

    dma_hw->ints0 = dma_hw->ints0;
    irq_set_enabled(SYNC_IO_SHARED_DMA_IRQ, false);
    dma_channel_set_irq0_enabled(SYNC_IO_SEQ_STEP_DMA_CH, false);

    if (gate_enabled && !sync_io_seq_step_trigger_pin_valid(trigger_pin)) {
        LOG_ERROR("sync_io",
                  "seq_step: gate mode requires trigger_pin in GPIO16..GPIO19, "
                  "got %lu", (unsigned long)trigger_pin);
        sync_io_core_trace(SYNC_IO_TRACE_SEQ_GATE_INVALID,
                           SYNC_IO_TRACE_ERROR,
                           trigger_pin,
                           0u);
        return false;
    }

    const pio_program_t *prog = gate_enabled
        ? &seq_step_gated_program
        : &seq_step_program;

    if (!pio_can_add_program(BOARD_SYNC_PIO_WAVE, prog)) {
        LOG_ERROR("sync_io", "seq_step: not enough PIO instruction space");
        sync_io_core_trace(SYNC_IO_TRACE_SEQ_PIO_NO_SPACE,
                           SYNC_IO_TRACE_ERROR,
                           gate_enabled ? 1u : 0u,
                           seq_length);
        return false;
    }

    s_seq_step.seq_offset = (uint)pio_add_program(BOARD_SYNC_PIO_WAVE, prog);
    s_seq_step.seq_sm = BOARD_SYNC_OUTPUT_SM;
    s_seq_step.has_gate = gate_enabled;

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);
    pio_sm_restart(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);

    seq_step_program_init_ex(BOARD_SYNC_PIO_WAVE,
                             s_seq_step.seq_sm,
                             s_seq_step.seq_offset,
                             trigger_pin,
                             BOARD_SYNC_OUTPUT_BASE_PIN,
                             seq_width,
                             1.0f,
                             gate_enabled,
                             (int)edge);

    s_seq_step.dma_ch = SYNC_IO_SEQ_STEP_DMA_CH;
    dma_channel_abort(s_seq_step.dma_ch);

    s_seq_step.seq_length = seq_length;
    s_seq_step.seq_width = seq_width;
    s_seq_step.seq_table_addr = (uintptr_t)seq_table;
    s_seq_step.rollover_count = 0u;
    s_seq_step.dma_done = false;
    s_seq_dma_overflow_latched = false;
    s_seq_trace_sample_valid = false;

    irq_set_exclusive_handler(SYNC_IO_SHARED_DMA_IRQ,
                              sync_io_core_dma_irq_handler);
    dma_channel_set_irq0_enabled(s_seq_step.dma_ch, true);
    irq_set_enabled(SYNC_IO_SHARED_DMA_IRQ, true);

    dma_channel_config dma_cfg =
        dma_channel_get_default_config(s_seq_step.dma_ch);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, true);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg, DREQ_PIO1_TX0 + s_seq_step.seq_sm);
    dma_channel_configure(s_seq_step.dma_ch,
                          &dma_cfg,
                          &BOARD_SYNC_PIO_WAVE->txf[s_seq_step.seq_sm],
                          seq_table,
                          seq_length,
                          true);

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm, true);
    s_seq_step.running = true;

    LOG_INFO("sync_io", "seq_step armed: length=%lu width=%lu",
             (unsigned long)seq_length, (unsigned long)seq_width);
    sync_io_core_trace(SYNC_IO_TRACE_SEQ_ARMED,
                       SYNC_IO_TRACE_INFO,
                       seq_length,
                       ((seq_width & 0xFFu) << 24) |
                           ((trigger_pin & 0xFFu) << 8) |
                           ((uint32_t)edge & 0xFFu) |
                           (gate_enabled ? (1u << 16) : 0u));
    sync_io_seq_step_runtime_t runtime;
    sync_io_seq_step_get_runtime(&runtime);
    sync_io_core_trace(
        SYNC_IO_TRACE_SEQ_RUNTIME,
        SYNC_IO_TRACE_INFO,
        sync_io_core_pack_runtime_flags(runtime.running,
                                        runtime.pio_enabled,
                                        runtime.dma_busy,
                                        runtime.dma_irq_enabled,
                                        runtime.tx_fifo_empty,
                                        runtime.tx_fifo_full),
        runtime.transfer_count & 0xFFFFu);
    sync_io_seq_step_trace_runtime_sample(true);

    return true;
}

void sync_io_seq_step_disarm(void)
{
    if (!s_seq_step.running) {
        return;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm, false);
    irq_set_enabled(SYNC_IO_SHARED_DMA_IRQ, false);
    dma_channel_set_irq0_enabled(s_seq_step.dma_ch, false);
    dma_channel_abort(s_seq_step.dma_ch);

    pio_sm_clear_fifos(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);
    pio_sm_set_pins(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm, 0);

    for (uint pin = 0u; pin < s_seq_step.seq_width; pin++) {
        gpio_set_function(BOARD_SYNC_OUTPUT_BASE_PIN + pin, GPIO_FUNC_SIO);
        gpio_set_dir(BOARD_SYNC_OUTPUT_BASE_PIN + pin, GPIO_OUT);
        gpio_put(BOARD_SYNC_OUTPUT_BASE_PIN + pin, false);
    }

    gpio_set_function(BOARD_SYNC_TRIG_IN_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(BOARD_SYNC_TRIG_IN_PIN, GPIO_IN);
    gpio_pull_down(BOARD_SYNC_TRIG_IN_PIN);

    const pio_program_t *prog_to_remove = s_seq_step.has_gate
        ? &seq_step_gated_program
        : &seq_step_program;
    pio_remove_program(BOARD_SYNC_PIO_WAVE,
                       prog_to_remove,
                       s_seq_step.seq_offset);

    s_seq_step.running = false;
    LOG_INFO("sync_io", "seq_step disarmed: rollover_count=%lu",
             (unsigned long)s_seq_step.rollover_count);
    sync_io_core_trace(SYNC_IO_TRACE_SEQ_DISARM,
                       SYNC_IO_TRACE_INFO,
                       (uint32_t)s_seq_step.rollover_count,
                       s_seq_step.seq_length);
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

uint64_t sync_io_seq_step_get_rollover_count(void)
{
    return s_seq_step.rollover_count;
}

void sync_io_seq_step_get_runtime(sync_io_seq_step_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    runtime->running = s_seq_step.running;
    runtime->pio_enabled = false;
    runtime->dma_busy = false;
    runtime->dma_irq_enabled = false;
    runtime->tx_fifo_empty = true;
    runtime->tx_fifo_full = false;
    runtime->transfer_count = 0u;
    runtime->rollover_count_low32 = (uint32_t)s_seq_step.rollover_count;

    if (!s_seq_step.running) {
        return;
    }

    runtime->pio_enabled =
        sync_io_core_sm_is_enabled(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);
    runtime->dma_busy = dma_channel_is_busy(s_seq_step.dma_ch);
    runtime->dma_irq_enabled =
        (dma_hw->inte0 & (1u << s_seq_step.dma_ch)) != 0u;
    runtime->tx_fifo_empty =
        pio_sm_is_tx_fifo_empty(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);
    runtime->tx_fifo_full =
        pio_sm_is_tx_fifo_full(BOARD_SYNC_PIO_WAVE, s_seq_step.seq_sm);
    runtime->transfer_count = dma_hw->ch[s_seq_step.dma_ch].transfer_count;
}

void sync_io_seq_step_trace_runtime_sample(bool force)
{
    sync_io_seq_step_runtime_t runtime;
    sync_io_seq_step_get_runtime(&runtime);

    const uint32_t flags =
        sync_io_core_pack_runtime_flags(runtime.running,
                                        runtime.pio_enabled,
                                        runtime.dma_busy,
                                        runtime.dma_irq_enabled,
                                        runtime.tx_fifo_empty,
                                        runtime.tx_fifo_full);
    const uint32_t transfer_count = runtime.transfer_count & 0xFFFFu;
    const uint32_t rollover_low32 = runtime.rollover_count_low32;
    const bool runtime_changed = !s_seq_trace_sample_valid ||
                                 flags != s_seq_last_runtime_flags ||
                                 transfer_count != s_seq_last_transfer_count;
    const bool rollover_changed = !s_seq_trace_sample_valid ||
                                  rollover_low32 != s_seq_last_rollover_low32;
    const uint32_t rollover_delta = s_seq_trace_sample_valid
        ? (uint32_t)(rollover_low32 - s_seq_last_rollover_low32)
        : 0u;
    const bool overflow_detected =
        rollover_delta > SYNC_IO_DMA_OVERFLOW_DELTA_THRESHOLD;

    if (force || runtime_changed) {
        sync_io_core_trace(
            SYNC_IO_TRACE_SEQ_PIO_STATE,
            runtime.running ? SYNC_IO_TRACE_INFO : SYNC_IO_TRACE_WARN,
            sync_io_core_pack_pio_state(s_seq_step.seq_sm,
                                        s_seq_step.seq_offset,
                                        runtime.pio_enabled,
                                        runtime.tx_fifo_empty,
                                        runtime.tx_fifo_full),
            transfer_count);
    }

    if (force || rollover_changed) {
        sync_io_core_trace(SYNC_IO_TRACE_SEQ_DMA_RESTART,
                           SYNC_IO_TRACE_INFO,
                           rollover_low32,
                           transfer_count);
    }

    if (force || overflow_detected || s_seq_dma_overflow_latched) {
        if (overflow_detected) {
            s_seq_dma_overflow_latched = true;
        }
        sync_io_core_trace(
            SYNC_IO_TRACE_SEQ_DMA_OVERFLOW,
            overflow_detected ? SYNC_IO_TRACE_WARN : SYNC_IO_TRACE_INFO,
            rollover_low32,
            ((rollover_delta & 0xFFFFu) << 16) |
                (SYNC_IO_DMA_OVERFLOW_DELTA_THRESHOLD & 0xFFFFu));
    }

    s_seq_last_runtime_flags = flags;
    s_seq_last_transfer_count = transfer_count;
    s_seq_last_rollover_low32 = rollover_low32;
    s_seq_trace_sample_valid = true;
}

SYNC_IO_MODE_VOID_DISPATCH(sync_io_seq_step_mode, sync_io_seq_step_mode_config_t)

const sync_io_mode_ops_t *sync_io_seq_step_mode_ops(void)
{
    static const sync_io_mode_ops_t ops = {
        .id = SYNC_IO_MODE_ID_SEQ_STEP,
        .name = "seq_step",
        .resources = SYNC_IO_MODE_RESOURCE_MAIN_INPUT |
                     SYNC_IO_MODE_RESOURCE_MAIN_OUTPUT |
                     SYNC_IO_MODE_RESOURCE_PIO_WAVE |
                     SYNC_IO_MODE_RESOURCE_DMA |
                     SYNC_IO_MODE_RESOURCE_IRQ,
        .hw = {
            .pio_mask = SYNC_IO_MODE_HW_PIO1,
            .pio1_sm_mask = 1u << BOARD_SYNC_OUTPUT_SM,
            .dma_channel_mask = 1u << SYNC_IO_SEQ_STEP_DMA_CH,
            .irq_mask = SYNC_IO_MODE_HW_IRQ_DMA0,
        },
        .validate = sync_io_seq_step_mode_validate_void,
        .arm = sync_io_seq_step_mode_arm_void,
        .disarm = sync_io_seq_step_disarm,
        .is_running = sync_io_seq_step_is_running,
    };

    return &ops;
}
