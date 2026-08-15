#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

bool board_init(void);
void board_service(void);
void board_status_led_set(bool on);
void board_status_led_toggle(void);
bool board_key2_is_pressed(void);
uint32_t board_uptime_ms(void);
void board_prepare_lcd_spi(void);
void board_prepare_sd_spi(void);

#endif
