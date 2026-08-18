#ifndef U8G2_PORT_H
#define U8G2_PORT_H

#include <stdint.h>

#include "u8g2.h"

void u8g2_port_setup_160x80(u8g2_t *u8g2, uint8_t *buffer);
uint8_t u8g2_port_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr);

#endif
