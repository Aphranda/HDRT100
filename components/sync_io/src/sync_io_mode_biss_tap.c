#include "sync_io_mode_biss_tap.h"

#include <stddef.h>

#include "biss_tap_rx.pio.h"
#include "board_config.h"
#include "diagnostics.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "osal.h"
#include "sync_io.pio.h"
#include "sync_io_core_internal.h"
#include "sync_io_hw_profile.h"

#define SYNC_IO_BISS_TAP_MODE_MAX_FRAME_BITS 64u
#define SYNC_IO_BISS_TAP_MODE_MAX_SAMPLE_EDGE 1u
#define SYNC_IO_BISS_TAP_SM BOARD_SYNC_AUX0_SM

typedef struct {
    bool running;
    uint32_t frame_bits;
    uint32_t clk_pin;
    uint32_t data_pin;
} sync_io_biss_tap_runtime_t;

static sync_io_biss_tap_runtime_t s_biss_tap;

bool sync_io_biss_tap_mode_validate(const sync_io_biss_tap_mode_config_t *config)
{
#if !BOARD_SYNC_AUX_ENABLED
    (void)config;
    return false;
#else
    if (config == NULL ||
        config->frame_bits == 0u ||
        config->frame_bits > SYNC_IO_BISS_TAP_MODE_MAX_FRAME_BITS ||
        config->sample_edge > SYNC_IO_BISS_TAP_MODE_MAX_SAMPLE_EDGE) {
        return false;
    }

    if (config->clk_pin != SYNC_IO_HW_BISS_CLK_IN_PIN ||
        config->data_pin != SYNC_IO_HW_BISS_DATA_IN_PIN ||
        config->clk_pin == config->data_pin) {
        return false;
    }

    return sync_io_hw_aux_supports_input(0u) &&
           sync_io_hw_aux_supports_input(1u) &&
           sync_io_hw_aux_supports_output(2u) &&
           sync_io_hw_aux_supports_output(3u);
#endif
}

bool sync_io_biss_tap_mode_arm(const sync_io_biss_tap_mode_config_t *config)
{
    if (!sync_io_biss_tap_mode_validate(config)) {
        return false;
    }

    return sync_io_biss_tap_arm(config);
}

bool sync_io_biss_tap_arm(const sync_io_biss_tap_config_t *config)
{
    if (!sync_io_core_initialized() ||
        !sync_io_biss_tap_mode_validate(config)) {
        sync_io_core_trace(SYNC_IO_TRACE_BISS_TAP_FAIL,
                           SYNC_IO_TRACE_ERROR,
                           config != NULL ? config->frame_bits : 0u,
                           config != NULL ? config->sample_edge : 0u);
        return false;
    }

    if (s_biss_tap.running) {
        sync_io_biss_tap_disarm();
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM);
    pio_sm_restart(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM);
    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX2_SM, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX2_SM);
    pio_sm_restart(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX2_SM);
    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX3_SM, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX3_SM);
    pio_sm_restart(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX3_SM);

    biss_tap_rx_program_init(BOARD_SYNC_PIO_AUX,
                             SYNC_IO_BISS_TAP_SM,
                             sync_io_core_biss_tap_offset(),
                             config->clk_pin,
                             config->data_pin,
                             config->frame_bits,
                             config->sample_delay_cycles,
                             (int)config->sample_edge,
                             1.0f);

    sync_passthrough_1bit_program_init(BOARD_SYNC_PIO_AUX,
                                       BOARD_SYNC_AUX2_SM,
                                       sync_io_core_aux_passthrough_offset(),
                                       SYNC_IO_HW_BISS_CLK_IN_PIN,
                                       SYNC_IO_HW_BISS_CLK_OUT_PIN,
                                       1.0f);
    sync_passthrough_1bit_program_init(BOARD_SYNC_PIO_AUX,
                                       BOARD_SYNC_AUX3_SM,
                                       sync_io_core_aux_passthrough_offset(),
                                       SYNC_IO_HW_BISS_DATA_IN_PIN,
                                       SYNC_IO_HW_BISS_DATA_OUT_PIN,
                                       1.0f);

    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM, true);
    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX2_SM, true);
    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX3_SM, true);

    s_biss_tap.running = true;
    s_biss_tap.frame_bits = config->frame_bits;
    s_biss_tap.clk_pin = config->clk_pin;
    s_biss_tap.data_pin = config->data_pin;
    sync_io_core_mark_aux_channel_input(config->clk_pin);
    sync_io_core_mark_aux_channel_input(config->data_pin);
    sync_io_core_set_aux_mode(SYNC_IO_AUX2, SYNC_IO_AUX_MODE_PIO_OUTPUT);
    sync_io_core_set_aux_mode(SYNC_IO_AUX3, SYNC_IO_AUX_MODE_PIO_OUTPUT);

    sync_io_core_trace(SYNC_IO_TRACE_BISS_TAP_ARM,
                       SYNC_IO_TRACE_INFO,
                       config->frame_bits,
                       (config->sample_edge & 0xFFu) |
                           ((config->sample_delay_cycles & 0xFFu) << 8));
    sync_io_core_trace(SYNC_IO_TRACE_BISS_TAP_FORWARD,
                       SYNC_IO_TRACE_INFO,
                       SYNC_IO_HW_BISS_CLK_IN_PIN |
                           (SYNC_IO_HW_BISS_CLK_OUT_PIN << 8),
                       SYNC_IO_HW_BISS_DATA_IN_PIN |
                           (SYNC_IO_HW_BISS_DATA_OUT_PIN << 8));
    LOG_INFO("sync_io",
             "biss_tap armed: bits=%lu edge=%lu delay=%lu pass=%lu>%lu,%lu>%lu",
             (unsigned long)config->frame_bits,
             (unsigned long)config->sample_edge,
             (unsigned long)config->sample_delay_cycles,
             (unsigned long)SYNC_IO_HW_BISS_CLK_IN_PIN,
             (unsigned long)SYNC_IO_HW_BISS_CLK_OUT_PIN,
             (unsigned long)SYNC_IO_HW_BISS_DATA_IN_PIN,
             (unsigned long)SYNC_IO_HW_BISS_DATA_OUT_PIN);
    return true;
}

void sync_io_biss_tap_disarm(void)
{
    if (!sync_io_core_initialized() || !s_biss_tap.running) {
        return;
    }

    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM);
    pio_sm_restart(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM);
    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX2_SM, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX2_SM);
    pio_sm_restart(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX2_SM);
    pio_sm_set_enabled(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX3_SM, false);
    pio_sm_clear_fifos(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX3_SM);
    pio_sm_restart(BOARD_SYNC_PIO_AUX, BOARD_SYNC_AUX3_SM);

    sync_io_core_restore_aux_channel_input(s_biss_tap.clk_pin);
    sync_io_core_restore_aux_channel_input(s_biss_tap.data_pin);
    sync_io_core_restore_aux_channel_input(SYNC_IO_HW_BISS_CLK_OUT_PIN);
    sync_io_core_restore_aux_channel_input(SYNC_IO_HW_BISS_DATA_OUT_PIN);

    sync_io_core_trace(SYNC_IO_TRACE_BISS_TAP_DISARM,
                       SYNC_IO_TRACE_INFO,
                       s_biss_tap.frame_bits,
                       0u);
    LOG_INFO("sync_io",
             "biss_tap disarmed: bits=%lu",
             (unsigned long)s_biss_tap.frame_bits);
    s_biss_tap.running = false;
    s_biss_tap.frame_bits = 0u;
    s_biss_tap.clk_pin = 0u;
    s_biss_tap.data_pin = 0u;
}

bool sync_io_biss_tap_is_running(void)
{
    return sync_io_core_initialized() && s_biss_tap.running;
}

bool sync_io_biss_tap_read_frame_word(uint32_t *word)
{
    if (!sync_io_core_initialized() ||
        !s_biss_tap.running ||
        word == NULL ||
        pio_sm_is_rx_fifo_empty(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM)) {
        return false;
    }

    *word = pio_sm_get(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM);
    return true;
}

void sync_io_biss_tap_mode_disarm(void)
{
    sync_io_biss_tap_disarm();
}

bool sync_io_biss_tap_mode_is_running(void)
{
    return sync_io_biss_tap_is_running();
}

bool sync_io_biss_tap_mode_read_frame_word(uint32_t *word)
{
    return sync_io_biss_tap_read_frame_word(word);
}

bool sync_io_biss_tap_mode_rx_fifo_full(void)
{
    return sync_io_core_initialized() &&
           s_biss_tap.running &&
           pio_sm_is_rx_fifo_full(BOARD_SYNC_PIO_AUX, SYNC_IO_BISS_TAP_SM);
}

SYNC_IO_MODE_VOID_DISPATCH(sync_io_biss_tap_mode, sync_io_biss_tap_mode_config_t)

const sync_io_mode_ops_t *sync_io_biss_tap_mode_ops(void)
{
    static const sync_io_mode_ops_t ops = {
        .id = SYNC_IO_MODE_ID_BISS_TAP,
        .name = "biss_tap",
        .resources = SYNC_IO_MODE_RESOURCE_AUX_RX |
                     SYNC_IO_MODE_RESOURCE_AUX_TX |
                     SYNC_IO_MODE_RESOURCE_PIO_AUX,
        .hw = {
            .pio_mask = SYNC_IO_MODE_HW_PIO2,
            .pio2_sm_mask = (1u << SYNC_IO_BISS_TAP_SM) |
                            (1u << BOARD_SYNC_AUX2_SM) |
                            (1u << BOARD_SYNC_AUX3_SM),
        },
        .validate = sync_io_biss_tap_mode_validate_void,
        .arm = sync_io_biss_tap_mode_arm_void,
        .disarm = sync_io_biss_tap_mode_disarm,
        .is_running = sync_io_biss_tap_mode_is_running,
    };

    return &ops;
}
