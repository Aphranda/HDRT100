#ifndef HOST_SHIM_HARDWARE_I2C_H
#define HOST_SHIM_HARDWARE_I2C_H

/* Host stand-in for the Pico SDK I2C header. */

#include <stddef.h>
#include <stdint.h>

typedef struct i2c_inst { uint32_t reserved; } i2c_inst_t;

extern i2c_inst_t *i2c0;
extern i2c_inst_t *i2c1;

void i2c_init(i2c_inst_t *i2c, uint32_t baudrate);
void i2c_deinit(i2c_inst_t *i2c);
int i2c_write_blocking(i2c_inst_t *i2c, uint8_t addr, const uint8_t *src, size_t len, bool nostop);
int i2c_read_blocking(i2c_inst_t *i2c, uint8_t addr, uint8_t *dst, size_t len, bool nostop);

#endif /* HOST_SHIM_HARDWARE_I2C_H */
