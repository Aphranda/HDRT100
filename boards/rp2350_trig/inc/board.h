#ifndef BOARD_H
#define BOARD_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    BOARD_KEY_LEFT = 0,
    BOARD_KEY_CENTER,
    BOARD_KEY_RIGHT,
    BOARD_KEY_COUNT,
} board_key_t;

typedef enum {
    BOARD_LED_SYSTEM = 0,
    BOARD_LED_ARM_TRIGGER,
    BOARD_LED_FAULT,
    BOARD_LED_COUNT,
} board_led_t;

bool board_init(void);
void board_service(void);
void board_led_set(board_led_t led, bool on);
void board_led_toggle(board_led_t led);
void board_status_led_set(bool on);
void board_status_led_toggle(void);
bool board_key_is_pressed(board_key_t key);
bool board_key2_is_pressed(void);
uint32_t board_uptime_ms(void);
void board_prepare_lcd_spi(void);
void board_prepare_sd_spi(void);

#endif
