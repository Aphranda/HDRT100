#ifndef LCD_ST7789_H
#define LCD_ST7789_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "hardware/spi.h"

typedef enum {
    LCD_ST7789_ROTATION_0 = 0,
    LCD_ST7789_ROTATION_90,
    LCD_ST7789_ROTATION_180,
    LCD_ST7789_ROTATION_270,
} lcd_st7789_rotation_t;

typedef struct {
    spi_inst_t *spi;
    uint32_t rst_pin;
    uint32_t dc_pin;
    uint32_t cs_pin;
    uint32_t bl_pin;
    uint16_t width;
    uint16_t height;
    uint16_t x_offset;
    uint16_t y_offset;
    lcd_st7789_rotation_t rotation;
    bool backlight_active_high;
} lcd_st7789_config_t;

#define LCD_ST7789_RGB565_BLACK   0x0000u
#define LCD_ST7789_RGB565_BLUE    0x001Fu
#define LCD_ST7789_RGB565_GREEN   0x07E0u
#define LCD_ST7789_RGB565_CYAN    0x07FFu
#define LCD_ST7789_RGB565_RED     0xF800u
#define LCD_ST7789_RGB565_MAGENTA 0xF81Fu
#define LCD_ST7789_RGB565_YELLOW  0xFFE0u
#define LCD_ST7789_RGB565_WHITE   0xFFFFu

bool lcd_st7789_init(const lcd_st7789_config_t *config);
void lcd_st7789_set_backlight(bool on);
void lcd_st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1);
void lcd_st7789_clear(uint16_t color);
void lcd_st7789_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color);
void lcd_st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color);
void lcd_st7789_write_rgb565(const uint16_t *pixels, size_t pixel_count);
uint16_t lcd_st7789_width(void);
uint16_t lcd_st7789_height(void);

#endif
