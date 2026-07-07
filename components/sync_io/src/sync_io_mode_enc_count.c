#include "sync_io_mode_enc_count.h"

#include <stddef.h>

#include "sync_io.h"
#include "sync_io_hw_profile.h"

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
                                     config->in_pin_base + 3u) &&
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

static bool sync_io_enc_count_mode_validate_void(const void *config)
{
    return sync_io_enc_count_mode_validate((const sync_io_enc_count_mode_config_t *)config);
}

static bool sync_io_enc_count_mode_arm_void(const void *config)
{
    return sync_io_enc_count_mode_arm((const sync_io_enc_count_mode_config_t *)config);
}

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
        .validate = sync_io_enc_count_mode_validate_void,
        .arm = sync_io_enc_count_mode_arm_void,
        .disarm = sync_io_enc_count_disarm,
        .is_running = sync_io_enc_count_is_running,
    };

    return &ops;
}

