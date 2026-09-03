#include "sync_io_mode_enc_count.h"

#include <stddef.h>

#include "board_config.h"
#include "diagnostics.h"
#include "enc_count.pio.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/irq.h"
#include "hardware/pio.h"
#include "sync_io.h"
#include "sync_io_core_internal.h"
#include "sync_io_hw_profile.h"

typedef struct {
    bool running;
    uint sm;
    uint offset;
    uint target;
    uint in_pin_base;
    uint dma_ch;
    volatile uint32_t fire_count;
    volatile uint32_t dma_restart_count;
    volatile uint32_t last_count;
} sync_io_enc_count_t;

static sync_io_enc_count_t s_enc;
static uint32_t s_enc_last_runtime_flags;
static uint32_t s_enc_last_transfer_count;
static uint32_t s_enc_last_dma_restart_count;
static bool s_enc_dma_overflow_latched;
static bool s_enc_trace_sample_valid;

sync_io_enc_count_mode_config_t sync_io_enc_count_mode_default_config(uint32_t target)
{
    sync_io_enc_count_mode_config_t config = {
        .target = target,
        .in_pin_base = SYNC_IO_HW_ENC_A_PIN,
        .output_pin = SYNC_IO_HW_TRIG_OUT_PIN,
    };

    return config;
}

bool sync_io_enc_count_mode_validate(const sync_io_enc_count_mode_config_t *config)
{
    if (config == NULL || config->target == 0u) {
        return false;
    }

    return config->in_pin_base == SYNC_IO_HW_ENC_A_PIN &&
           sync_io_hw_enc_pins_valid(config->in_pin_base,
                                     config->in_pin_base + 1u,
                                     config->in_pin_base + 2u) &&
           config->output_pin == SYNC_IO_HW_TRIG_OUT_PIN;
}

bool sync_io_enc_count_mode_arm(const sync_io_enc_count_mode_config_t *config)
{
    if (!sync_io_enc_count_mode_validate(config)) {
        return false;
    }

    return sync_io_enc_count_arm(config->target,
                                 config->in_pin_base,
                                 config->output_pin);
}

bool sync_io_enc_count_dma_irq_service(uint32_t ints)
{
    if ((ints & (1u << s_enc.dma_ch)) == 0u || !s_enc.running) {
        return false;
    }

    s_enc.fire_count++;
    s_enc.dma_restart_count++;
    dma_hw->ch[s_enc.dma_ch].al1_transfer_count_trig = 1u;
    return true;
}

bool sync_io_enc_count_arm(uint32_t target,
                           uint32_t in_pin_base,
                           uint32_t output_pin)
{
    if (!sync_io_core_initialized() ||
        sync_io_core_tdma_flight_suspended() ||
        target == 0u) {
        sync_io_core_trace(SYNC_IO_TRACE_ENC_ARM_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           target,
                           sync_io_core_initialized() ? 0u : 1u);
        return false;
    }

    if (in_pin_base != SYNC_IO_HW_ENC_A_PIN ||
        output_pin != SYNC_IO_HW_TRIG_OUT_PIN) {
        sync_io_core_trace(SYNC_IO_TRACE_ENC_ARM_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           in_pin_base,
                           output_pin);
        return false;
    }

    if (s_enc.running) {
        sync_io_enc_count_disarm();
    }

    if (!pio_can_add_program(BOARD_SYNC_PIO_WAVE, &enc_count_program)) {
        LOG_ERROR("sync_io", "enc_count: not enough PIO instruction space");
        sync_io_core_trace(SYNC_IO_TRACE_ENC_PIO_NO_SPACE,
                           SYNC_IO_TRACE_ERROR,
                           target,
                           in_pin_base);
        return false;
    }

    s_enc.offset = (uint)pio_add_program(BOARD_SYNC_PIO_WAVE,
                                         &enc_count_program);
    s_enc.sm = BOARD_SYNC_OUTPUT_SM;
    s_enc.target = target;
    s_enc.in_pin_base = in_pin_base;

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_enc.sm, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_WAVE, s_enc.sm);
    pio_sm_restart(BOARD_SYNC_PIO_WAVE, s_enc.sm);

    enc_count_program_init(BOARD_SYNC_PIO_WAVE,
                           s_enc.sm,
                           s_enc.offset,
                           in_pin_base,
                           output_pin,
                           1.0f);

    pio_sm_put(BOARD_SYNC_PIO_WAVE, s_enc.sm, target);

    s_enc.dma_ch = SYNC_IO_ENC_COUNT_DMA_CH;
    dma_channel_abort(s_enc.dma_ch);

    dma_channel_config dma_cfg =
        dma_channel_get_default_config(s_enc.dma_ch);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, false);
    channel_config_set_write_increment(&dma_cfg, false);
    channel_config_set_dreq(&dma_cfg, DREQ_PIO1_TX0 + s_enc.sm);
    dma_channel_configure(s_enc.dma_ch,
                          &dma_cfg,
                          &BOARD_SYNC_PIO_WAVE->txf[s_enc.sm],
                          &s_enc.target,
                          0xFFFFFFFFu,
                          true);

    dma_channel_set_irq0_enabled(s_enc.dma_ch, true);

    if (!irq_is_enabled(SYNC_IO_SHARED_DMA_IRQ)) {
        irq_set_exclusive_handler(SYNC_IO_SHARED_DMA_IRQ,
                                  sync_io_core_dma_irq_handler);
        irq_set_enabled(SYNC_IO_SHARED_DMA_IRQ, true);
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_enc.sm, true);

    s_enc.fire_count = 0u;
    s_enc.dma_restart_count = 0u;
    s_enc.last_count = 0u;
    s_enc_dma_overflow_latched = false;
    s_enc_trace_sample_valid = false;
    s_enc.running = true;

    LOG_INFO("sync_io", "enc_count armed: target=%lu pins=A%lu/B%lu/Z%lu",
             (unsigned long)target,
             (unsigned long)(in_pin_base + 0u),
             (unsigned long)(in_pin_base + 1u),
             (unsigned long)(in_pin_base + 2u));
    sync_io_core_trace(SYNC_IO_TRACE_ENC_ARMED,
                       SYNC_IO_TRACE_INFO,
                       target,
                       ((in_pin_base & 0xFFu) << 8) |
                           (output_pin & 0xFFu));
    sync_io_enc_count_runtime_t runtime;
    sync_io_enc_count_get_runtime(&runtime);
    sync_io_core_trace(
        SYNC_IO_TRACE_ENC_RUNTIME,
        SYNC_IO_TRACE_INFO,
        sync_io_core_pack_runtime_flags(runtime.running,
                                        runtime.pio_enabled,
                                        runtime.dma_busy,
                                        runtime.dma_irq_enabled,
                                        runtime.tx_fifo_empty,
                                        runtime.tx_fifo_full),
        runtime.transfer_count & 0xFFFFu);
    sync_io_enc_count_trace_runtime_sample(true);

    return true;
}

void sync_io_enc_count_disarm(void)
{
    if (!s_enc.running) {
        return;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_WAVE, s_enc.sm, false);

    dma_channel_set_irq0_enabled(s_enc.dma_ch, false);
    dma_channel_abort(s_enc.dma_ch);

    pio_sm_clear_fifos(BOARD_SYNC_PIO_WAVE, s_enc.sm);
    pio_sm_set_pins(BOARD_SYNC_PIO_WAVE, s_enc.sm, 0);

    for (uint i = 0u; i < 3u; i++) {
        gpio_set_function(s_enc.in_pin_base + i, GPIO_FUNC_SIO);
        gpio_set_dir(s_enc.in_pin_base + i, GPIO_IN);
        gpio_pull_down(s_enc.in_pin_base + i);
    }

    pio_remove_program(BOARD_SYNC_PIO_WAVE,
                       &enc_count_program,
                       s_enc.offset);

    s_enc.running = false;
    LOG_INFO("sync_io", "enc_count disarmed: fire_count=%lu dma_restarts=%lu",
             (unsigned long)s_enc.fire_count,
             (unsigned long)s_enc.dma_restart_count);
    sync_io_core_trace(SYNC_IO_TRACE_ENC_DISARM,
                       SYNC_IO_TRACE_INFO,
                       s_enc.fire_count,
                       s_enc.dma_restart_count);
}

uint32_t sync_io_enc_count_get_count(void)
{
    if (!s_enc.running) {
        return 0u;
    }

    while (!pio_sm_is_rx_fifo_empty(BOARD_SYNC_PIO_WAVE, s_enc.sm)) {
        const uint32_t remaining = pio_sm_get(BOARD_SYNC_PIO_WAVE, s_enc.sm);
        if (remaining <= s_enc.target) {
            s_enc.last_count = s_enc.target - remaining;
        }
    }
    return s_enc.last_count;
}

bool sync_io_enc_count_is_running(void)
{
    return s_enc.running;
}

void sync_io_enc_count_get_runtime(sync_io_enc_count_runtime_t *runtime)
{
    if (runtime == NULL) {
        return;
    }

    runtime->running = s_enc.running;
    runtime->pio_enabled = false;
    runtime->dma_busy = false;
    runtime->dma_irq_enabled = false;
    runtime->tx_fifo_empty = true;
    runtime->tx_fifo_full = false;
    runtime->transfer_count = 0u;
    runtime->dma_restart_count = s_enc.dma_restart_count;

    if (!s_enc.running) {
        return;
    }

    runtime->pio_enabled =
        sync_io_core_sm_is_enabled(BOARD_SYNC_PIO_WAVE, s_enc.sm);
    runtime->dma_busy = dma_channel_is_busy(s_enc.dma_ch);
    runtime->dma_irq_enabled =
        (dma_hw->inte0 & (1u << s_enc.dma_ch)) != 0u;
    runtime->tx_fifo_empty =
        pio_sm_is_tx_fifo_empty(BOARD_SYNC_PIO_WAVE, s_enc.sm);
    runtime->tx_fifo_full =
        pio_sm_is_tx_fifo_full(BOARD_SYNC_PIO_WAVE, s_enc.sm);
    runtime->transfer_count = dma_hw->ch[s_enc.dma_ch].transfer_count;
}

void sync_io_enc_count_trace_runtime_sample(bool force)
{
    sync_io_enc_count_runtime_t runtime;
    sync_io_enc_count_get_runtime(&runtime);

    const uint32_t flags =
        sync_io_core_pack_runtime_flags(runtime.running,
                                        runtime.pio_enabled,
                                        runtime.dma_busy,
                                        runtime.dma_irq_enabled,
                                        runtime.tx_fifo_empty,
                                        runtime.tx_fifo_full);
    const uint32_t transfer_count = runtime.transfer_count & 0xFFFFu;
    const uint32_t restart_count = runtime.dma_restart_count;
    const bool runtime_changed = !s_enc_trace_sample_valid ||
                                 flags != s_enc_last_runtime_flags ||
                                 transfer_count != s_enc_last_transfer_count;
    const bool restart_changed = !s_enc_trace_sample_valid ||
                                 restart_count != s_enc_last_dma_restart_count;
    const uint32_t restart_delta = s_enc_trace_sample_valid
        ? (uint32_t)(restart_count - s_enc_last_dma_restart_count)
        : 0u;
    const bool overflow_detected =
        restart_delta > SYNC_IO_DMA_OVERFLOW_DELTA_THRESHOLD;

    if (force || runtime_changed) {
        sync_io_core_trace(
            SYNC_IO_TRACE_ENC_PIO_STATE,
            runtime.running ? SYNC_IO_TRACE_INFO : SYNC_IO_TRACE_WARN,
            sync_io_core_pack_pio_state(s_enc.sm,
                                        s_enc.offset,
                                        runtime.pio_enabled,
                                        runtime.tx_fifo_empty,
                                        runtime.tx_fifo_full),
            transfer_count);
    }

    if (force || restart_changed) {
        sync_io_core_trace(SYNC_IO_TRACE_ENC_DMA_RESTART,
                           SYNC_IO_TRACE_INFO,
                           restart_count,
                           transfer_count);
    }

    if (force || overflow_detected || s_enc_dma_overflow_latched) {
        if (overflow_detected) {
            s_enc_dma_overflow_latched = true;
        }
        sync_io_core_trace(
            SYNC_IO_TRACE_ENC_DMA_OVERFLOW,
            overflow_detected ? SYNC_IO_TRACE_WARN : SYNC_IO_TRACE_INFO,
            restart_count,
            ((restart_delta & 0xFFFFu) << 16) |
                (SYNC_IO_DMA_OVERFLOW_DELTA_THRESHOLD & 0xFFFFu));
    }

    s_enc_last_runtime_flags = flags;
    s_enc_last_transfer_count = transfer_count;
    s_enc_last_dma_restart_count = restart_count;
    s_enc_trace_sample_valid = true;
}

SYNC_IO_MODE_VOID_DISPATCH(sync_io_enc_count_mode, sync_io_enc_count_mode_config_t)

const sync_io_mode_ops_t *sync_io_enc_count_mode_ops(void)
{
    static const sync_io_mode_ops_t ops = {
        .id = SYNC_IO_MODE_ID_ENC_COUNT,
        .name = "enc_count",
        .resources = SYNC_IO_MODE_RESOURCE_MAIN_INPUT |
                     SYNC_IO_MODE_RESOURCE_MAIN_OUTPUT |
                     SYNC_IO_MODE_RESOURCE_PIO_WAVE |
                     SYNC_IO_MODE_RESOURCE_DMA |
                     SYNC_IO_MODE_RESOURCE_IRQ,
        .hw = {
            .pio_mask = SYNC_IO_MODE_HW_PIO1,
            .pio1_sm_mask = 1u << BOARD_SYNC_OUTPUT_SM,
            .dma_channel_mask = 1u << SYNC_IO_ENC_COUNT_DMA_CH,
            .irq_mask = SYNC_IO_MODE_HW_IRQ_DMA0,
        },
        .validate = sync_io_enc_count_mode_validate_void,
        .arm = sync_io_enc_count_mode_arm_void,
        .disarm = sync_io_enc_count_disarm,
        .is_running = sync_io_enc_count_is_running,
    };

    return &ops;
}
