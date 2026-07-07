#include "sync_io_mode_seq_step.h"

#include <stddef.h>

#include "sync_io_hw_profile.h"

#define SYNC_IO_SEQ_STEP_MODE_MAX_LENGTH 256u
#define SYNC_IO_SEQ_STEP_MODE_MAX_WIDTH  8u

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

static bool sync_io_seq_step_mode_validate_void(const void *config)
{
    return sync_io_seq_step_mode_validate((const sync_io_seq_step_mode_config_t *)config);
}

static bool sync_io_seq_step_mode_arm_void(const void *config)
{
    return sync_io_seq_step_mode_arm((const sync_io_seq_step_mode_config_t *)config);
}

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
        .validate = sync_io_seq_step_mode_validate_void,
        .arm = sync_io_seq_step_mode_arm_void,
        .disarm = sync_io_seq_step_disarm,
        .is_running = sync_io_seq_step_is_running,
    };

    return &ops;
}

