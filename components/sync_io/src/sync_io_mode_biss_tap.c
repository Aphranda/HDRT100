#include "sync_io_mode_biss_tap.h"

#include <stddef.h>

#include "sync_io_hw_profile.h"

#define SYNC_IO_BISS_TAP_MODE_MAX_FRAME_BITS 64u
#define SYNC_IO_BISS_TAP_MODE_MAX_SAMPLE_EDGE 1u

bool sync_io_biss_tap_mode_validate(const sync_io_biss_tap_mode_config_t *config)
{
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
           sync_io_hw_aux_supports_input(1u);
}

bool sync_io_biss_tap_mode_arm(const sync_io_biss_tap_mode_config_t *config)
{
    if (!sync_io_biss_tap_mode_validate(config)) {
        return false;
    }

    return sync_io_biss_tap_arm(config);
}

static bool sync_io_biss_tap_mode_validate_void(const void *config)
{
    return sync_io_biss_tap_mode_validate((const sync_io_biss_tap_mode_config_t *)config);
}

static bool sync_io_biss_tap_mode_arm_void(const void *config)
{
    return sync_io_biss_tap_mode_arm((const sync_io_biss_tap_mode_config_t *)config);
}

const sync_io_mode_ops_t *sync_io_biss_tap_mode_ops(void)
{
    static const sync_io_mode_ops_t ops = {
        .id = SYNC_IO_MODE_ID_BISS_TAP,
        .name = "biss_tap",
        .resources = SYNC_IO_MODE_RESOURCE_AUX_RX |
                     SYNC_IO_MODE_RESOURCE_PIO_AUX,
        .validate = sync_io_biss_tap_mode_validate_void,
        .arm = sync_io_biss_tap_mode_arm_void,
        .disarm = sync_io_biss_tap_disarm,
        .is_running = sync_io_biss_tap_is_running,
    };

    return &ops;
}

