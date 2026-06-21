#include "drv_i2c.h"

#include "hardware/gpio.h"

bool drv_i2c_init(const drv_i2c_config_t *config)
{
    if (config == NULL || config->instance == NULL) {
        return false;
    }

    i2c_init(config->instance, config->baud_hz);
    gpio_set_function(config->sda_pin, GPIO_FUNC_I2C);
    gpio_set_function(config->scl_pin, GPIO_FUNC_I2C);
    gpio_pull_up(config->sda_pin);
    gpio_pull_up(config->scl_pin);

    return true;
}
