#ifndef SYNC_IO_MODE_ENC_COUNT_H
#define SYNC_IO_MODE_ENC_COUNT_H

#include <stdbool.h>
#include <stdint.h>

#include "sync_io_mode.h"

typedef struct {
    uint32_t target;
    uint32_t in_pin_base;
    uint32_t output_pin;
} sync_io_enc_count_mode_config_t;

sync_io_enc_count_mode_config_t sync_io_enc_count_mode_default_config(uint32_t target);
bool sync_io_enc_count_mode_validate(const sync_io_enc_count_mode_config_t *config);
bool sync_io_enc_count_mode_arm(const sync_io_enc_count_mode_config_t *config);
const sync_io_mode_ops_t *sync_io_enc_count_mode_ops(void);

#endif

