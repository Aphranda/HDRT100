#include "lcd_st7789.h"

#include <string.h>

#include "hardware/gpio.h"
#include "osal.h"

#define LCD_CMD_SWRESET 0x01u
#define LCD_CMD_SLPOUT  0x11u
#define LCD_CMD_INVOFF  0x20u
#define LCD_CMD_INVON   0x21u
#define LCD_CMD_CASET   0x2Au
#define LCD_CMD_RASET   0x2Bu
#define LCD_CMD_RAMWR   0x2Cu
#define LCD_CMD_MADCTL  0x36u
#define LCD_CMD_COLMOD  0x3Au
#define LCD_CMD_NORON   0x13u
#define LCD_CMD_DISPON  0x29u

#define LCD_TRANSFER_CHUNK_PIXELS 128u

typedef struct {
    uint8_t cmd;
    uint8_t data[16];
    uint8_t len;
    uint16_t delay_ms;
} lcd_init_cmd_t;

static lcd_st7789_config_t s_lcd;
static bool s_initialized;

static void lcd_select(bool selected)
{
    gpio_put(s_lcd.cs_pin, selected ? 0 : 1);
}

static void lcd_dc_data(bool is_data)
{
    gpio_put(s_lcd.dc_pin, is_data ? 1 : 0);
}

static void lcd_write_cmd(uint8_t cmd)
{
    lcd_dc_data(false);
    lcd_select(true);
    (void)spi_write_blocking(s_lcd.spi, &cmd, 1);
    lcd_select(false);
}

static void lcd_write_data_bytes(const uint8_t *data, size_t len)
{
    if (len == 0u) {
        return;
    }

    lcd_dc_data(true);
    lcd_select(true);
    (void)spi_write_blocking(s_lcd.spi, data, len);
    lcd_select(false);
}

static void lcd_write_cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    lcd_write_cmd(cmd);
    lcd_write_data_bytes(data, len);
}

static uint8_t lcd_madctl_for_rotation(lcd_st7789_rotation_t rotation)
{
    switch (rotation) {
    case LCD_ST7789_ROTATION_0:
        return 0x00u;
    case LCD_ST7789_ROTATION_90:
        return 0x70u;
    case LCD_ST7789_ROTATION_180:
        return 0xC0u;
    case LCD_ST7789_ROTATION_270:
        return 0xA0u;
    default:
        return 0x70u;
    }
}

static void lcd_apply_init_sequence(void)
{
    static const lcd_init_cmd_t init_cmds[] = {
        {LCD_CMD_SWRESET, {0}, 0, 150},
        {LCD_CMD_SLPOUT, {0}, 0, 120},
        {LCD_CMD_COLMOD, {0x05}, 1, 0},
        {0xB2, {0x0C, 0x0C, 0x00, 0x33, 0x33}, 5, 0},
        {0xB7, {0x35}, 1, 0},
        {0xBB, {0x19}, 1, 0},
        {0xC0, {0x2C}, 1, 0},
        {0xC2, {0x01}, 1, 0},
        {0xC3, {0x12}, 1, 0},
        {0xC4, {0x20}, 1, 0},
        {0xC6, {0x01}, 1, 0},
        {0xD0, {0xA4, 0xA1}, 2, 0},
        {0xE0, {0xD0, 0x04, 0x0D, 0x11, 0x13, 0x2B, 0x3F, 0x54,
                0x4C, 0x18, 0x0D, 0x0B, 0x1F, 0x23}, 14, 0},
        {0xE1, {0xD0, 0x04, 0x0C, 0x11, 0x13, 0x2C, 0x3F, 0x44,
                0x51, 0x2F, 0x1F, 0x1F, 0x20, 0x23}, 14, 0},
        {LCD_CMD_INVON, {0}, 0, 0},
        {LCD_CMD_NORON, {0}, 0, 10},
        {LCD_CMD_DISPON, {0}, 0, 120},
    };

    for (size_t i = 0; i < (sizeof(init_cmds) / sizeof(init_cmds[0])); i++) {
        lcd_write_cmd_data(init_cmds[i].cmd, init_cmds[i].data, init_cmds[i].len);
        if (init_cmds[i].delay_ms > 0u) {
            osal_delay_ms(init_cmds[i].delay_ms);
        }
    }
}

bool lcd_st7789_init(const lcd_st7789_config_t *config)
{
    if (config == NULL || config->spi == NULL || config->width == 0u || config->height == 0u) {
        return false;
    }

    s_lcd = *config;

    gpio_init(s_lcd.dc_pin);
    gpio_set_dir(s_lcd.dc_pin, GPIO_OUT);
    gpio_put(s_lcd.dc_pin, 1);

    gpio_init(s_lcd.cs_pin);
    gpio_set_dir(s_lcd.cs_pin, GPIO_OUT);
    gpio_put(s_lcd.cs_pin, 1);

    gpio_init(s_lcd.bl_pin);
    gpio_set_dir(s_lcd.bl_pin, GPIO_OUT);
    lcd_st7789_set_backlight(false);

    osal_delay_ms(50u);
    lcd_apply_init_sequence();

    const uint8_t madctl = lcd_madctl_for_rotation(s_lcd.rotation);
    lcd_write_cmd_data(LCD_CMD_MADCTL, &madctl, 1);

    s_initialized = true;
    lcd_st7789_clear(LCD_ST7789_RGB565_BLACK);
    lcd_st7789_set_backlight(true);
    return true;
}

void lcd_st7789_set_backlight(bool on)
{
    const bool level = s_lcd.backlight_active_high ? on : !on;
    gpio_put(s_lcd.bl_pin, level ? 1 : 0);
}

void lcd_st7789_set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    const uint16_t xs = (uint16_t)(x0 + s_lcd.x_offset);
    const uint16_t xe = (uint16_t)(x1 + s_lcd.x_offset);
    const uint16_t ys = (uint16_t)(y0 + s_lcd.y_offset);
    const uint16_t ye = (uint16_t)(y1 + s_lcd.y_offset);
    const uint8_t x_data[4] = {(uint8_t)(xs >> 8), (uint8_t)xs, (uint8_t)(xe >> 8), (uint8_t)xe};
    const uint8_t y_data[4] = {(uint8_t)(ys >> 8), (uint8_t)ys, (uint8_t)(ye >> 8), (uint8_t)ye};

    lcd_write_cmd_data(LCD_CMD_CASET, x_data, sizeof(x_data));
    lcd_write_cmd_data(LCD_CMD_RASET, y_data, sizeof(y_data));
    lcd_write_cmd(LCD_CMD_RAMWR);
}

void lcd_st7789_clear(uint16_t color)
{
    lcd_st7789_fill_rect(0u, 0u, s_lcd.width, s_lcd.height, color);
}

void lcd_st7789_fill_rect(uint16_t x, uint16_t y, uint16_t width, uint16_t height, uint16_t color)
{
    if (!s_initialized && (s_lcd.spi == NULL)) {
        return;
    }

    if (x >= s_lcd.width || y >= s_lcd.height || width == 0u || height == 0u) {
        return;
    }

    if ((uint32_t)x + width > s_lcd.width) {
        width = (uint16_t)(s_lcd.width - x);
    }
    if ((uint32_t)y + height > s_lcd.height) {
        height = (uint16_t)(s_lcd.height - y);
    }

    lcd_st7789_set_window(x, y, (uint16_t)(x + width - 1u), (uint16_t)(y + height - 1u));

    uint8_t chunk[LCD_TRANSFER_CHUNK_PIXELS * 2u];
    for (size_t i = 0; i < LCD_TRANSFER_CHUNK_PIXELS; i++) {
        chunk[(i * 2u)] = (uint8_t)(color >> 8);
        chunk[(i * 2u) + 1u] = (uint8_t)color;
    }

    uint32_t remaining = (uint32_t)width * height;
    while (remaining > 0u) {
        const uint32_t pixels = remaining > LCD_TRANSFER_CHUNK_PIXELS ? LCD_TRANSFER_CHUNK_PIXELS : remaining;
        lcd_write_data_bytes(chunk, pixels * 2u);
        remaining -= pixels;
    }
}

void lcd_st7789_draw_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x >= s_lcd.width || y >= s_lcd.height) {
        return;
    }

    lcd_st7789_set_window(x, y, x, y);
    lcd_st7789_write_rgb565(&color, 1u);
}

void lcd_st7789_write_rgb565(const uint16_t *pixels, size_t pixel_count)
{
    if (pixels == NULL || pixel_count == 0u) {
        return;
    }

    uint8_t chunk[LCD_TRANSFER_CHUNK_PIXELS * 2u];

    while (pixel_count > 0u) {
        const size_t count = pixel_count > LCD_TRANSFER_CHUNK_PIXELS ? LCD_TRANSFER_CHUNK_PIXELS : pixel_count;
        for (size_t i = 0; i < count; i++) {
            chunk[(i * 2u)] = (uint8_t)(pixels[i] >> 8);
            chunk[(i * 2u) + 1u] = (uint8_t)pixels[i];
        }
        lcd_write_data_bytes(chunk, count * 2u);
        pixels += count;
        pixel_count -= count;
    }
}

uint16_t lcd_st7789_width(void)
{
    return s_lcd.width;
}

uint16_t lcd_st7789_height(void)
{
    return s_lcd.height;
}
