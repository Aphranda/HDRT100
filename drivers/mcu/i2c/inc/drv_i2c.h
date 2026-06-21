#ifndef DRV_I2C_H
#define DRV_I2C_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/i2c.h"

typedef struct {
    i2c_inst_t *instance;
    uint32_t sda_pin;
    uint32_t scl_pin;
    uint32_t baud_hz;
} drv_i2c_config_t;

bool drv_i2c_init(const drv_i2c_config_t *config);

#endif
