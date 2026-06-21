#include "sync_config_ui.h"

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "lcd_st7789.h"
#include "u8g2.h"
#include "u8g2_port.h"

#define UI_WIDTH 240u
#define UI_HEIGHT 135u
#define UI_U8G2_HEIGHT 136u
#define UI_MONO_BUFFER_SIZE ((UI_WIDTH * UI_U8G2_HEIGHT) / 8u)
#define UI_FLUSH_PIXELS 120u

typedef struct {
    u8g2_t u8g2;
    uint8_t mono_buffer[UI_MONO_BUFFER_SIZE];
    uint16_t line_buffer[UI_FLUSH_PIXELS];
    bool initialized;
} sync_config_ui_t;

static sync_config_ui_t s_ui;

extern const uint8_t u8g2_font_5x8_tr[];
extern const uint8_t u8g2_font_6x10_tf[];
extern const uint8_t u8g2_font_6x13B_tf[];

static uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)((uint16_t)(r & 0xF8u) << 8) |
           (uint16_t)((uint16_t)(g & 0xFCu) << 3) |
           (uint16_t)(b >> 3);
}

static void draw_card(u8g2_t *u8g2, uint8_t x, uint8_t y, uint8_t w, uint8_t h, const char *title)
{
    u8g2_DrawFrame(u8g2, x, y, w, h);
    u8g2_DrawHLine(u8g2, (u8g2_uint_t)(x + 1u), (u8g2_uint_t)(y + 12u), (u8g2_uint_t)(w - 2u));
    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, (u8g2_uint_t)(x + 5u), (u8g2_uint_t)(y + 9u), title);
}

static void draw_metric(u8g2_t *u8g2, uint8_t x, uint8_t y, const char *label, const char *value)
{
    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, x, y, label);
    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    u8g2_DrawStr(u8g2, (u8g2_uint_t)(x + 45u), y, value);
}

static void draw_toggle(u8g2_t *u8g2, uint8_t x, uint8_t y, const char *label, bool enabled)
{
    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, x, y, label);
    u8g2_DrawFrame(u8g2, (u8g2_uint_t)(x + 58u), (u8g2_uint_t)(y - 7u), 18, 8);
    if (enabled) {
        u8g2_DrawBox(u8g2, (u8g2_uint_t)(x + 68u), (u8g2_uint_t)(y - 5u), 6, 4);
    } else {
        u8g2_DrawBox(u8g2, (u8g2_uint_t)(x + 60u), (u8g2_uint_t)(y - 5u), 6, 4);
    }
}

static bool mono_pixel_is_set(uint16_t x, uint16_t y)
{
    const size_t byte_index = ((size_t)(y >> 3u) * UI_WIDTH) + x;
    const uint8_t bit_mask = (uint8_t)(1u << (y & 7u));
    return (s_ui.mono_buffer[byte_index] & bit_mask) != 0u;
}

static uint16_t themed_background(uint16_t x, uint16_t y)
{
    if (y < 20u) {
        return rgb565(13, 36, 47);
    }
    if (y >= 118u) {
        return rgb565(17, 27, 34);
    }
    if (x < 76u) {
        return rgb565(18, 30, 36);
    }
    if (x > 161u) {
        return rgb565(22, 32, 40);
    }
    return rgb565(15, 24, 31);
}

static uint16_t themed_foreground(uint16_t x, uint16_t y)
{
    if (y < 20u) {
        return rgb565(115, 232, 222);
    }
    if (y >= 118u) {
        return rgb565(246, 185, 77);
    }
    if ((x > 169u && x < 232u && y > 21u && y < 113u) ||
        (x > 80u && x < 158u && y > 21u && y < 83u)) {
        return rgb565(85, 211, 150);
    }
    return rgb565(220, 232, 229);
}

static void flush_to_lcd(void)
{
    lcd_st7789_set_window(0u, 0u, (uint16_t)(UI_WIDTH - 1u), (uint16_t)(UI_HEIGHT - 1u));

    size_t count = 0u;
    for (uint16_t y = 0u; y < UI_HEIGHT; y++) {
        for (uint16_t x = 0u; x < UI_WIDTH; x++) {
            s_ui.line_buffer[count++] = mono_pixel_is_set(x, y) ? themed_foreground(x, y) : themed_background(x, y);
            if (count == UI_FLUSH_PIXELS) {
                lcd_st7789_write_rgb565(s_ui.line_buffer, count);
                count = 0u;
            }
        }
    }

    if (count > 0u) {
        lcd_st7789_write_rgb565(s_ui.line_buffer, count);
    }
}

bool sync_config_ui_init(void)
{
    memset(&s_ui, 0, sizeof(s_ui));
    u8g2_port_setup_240x136(&s_ui.u8g2, s_ui.mono_buffer);
    u8g2_SetFontMode(&s_ui.u8g2, 1);
    s_ui.initialized = true;
    return true;
}

void sync_config_ui_render(void)
{
    if (!s_ui.initialized) {
        return;
    }

    u8g2_t *u8g2 = &s_ui.u8g2;

    u8g2_ClearBuffer(u8g2);

    u8g2_SetFont(u8g2, u8g2_font_6x13B_tf);
    u8g2_DrawStr(u8g2, 8, 13, "SYNC TRIGGER CONFIG");
    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, 181, 11, "LOCKED");
    u8g2_DrawDisc(u8g2, 226, 8, 3, U8G2_DRAW_ALL);

    draw_card(u8g2, 6, 22, 68, 90, "STATUS");
    draw_metric(u8g2, 11, 43, "MODE", "SYNC");
    draw_metric(u8g2, 11, 58, "RATE", "1kHz");
    draw_metric(u8g2, 11, 73, "JIT", "42ns");
    draw_metric(u8g2, 11, 88, "ARM", "READY");
    u8g2_DrawFrame(u8g2, 11, 96, 54, 8);
    u8g2_DrawBox(u8g2, 13, 98, 38, 4);

    draw_card(u8g2, 82, 22, 76, 60, "TRIGGER");
    draw_metric(u8g2, 88, 43, "SRC", "EXT0");
    draw_metric(u8g2, 88, 58, "EDGE", "RISING");
    draw_metric(u8g2, 88, 73, "LVL", "3V3");

    draw_card(u8g2, 166, 22, 68, 90, "TIMING");
    draw_metric(u8g2, 172, 43, "DELAY", "12us");
    draw_metric(u8g2, 172, 58, "WIDTH", "50us");
    draw_metric(u8g2, 172, 73, "COUNT", "1024");
    draw_toggle(u8g2, 172, 91, "OUT A", true);
    draw_toggle(u8g2, 172, 104, "OUT B", false);

    u8g2_SetFont(u8g2, u8g2_font_6x10_tf);
    u8g2_DrawButtonUTF8(u8g2, 83, 126, U8G2_BTN_BW1 | U8G2_BTN_SHADOW1, 37, 4, 2, "APPLY");
    u8g2_DrawButtonUTF8(u8g2, 132, 126, U8G2_BTN_BW1, 34, 4, 2, "SAVE");
    u8g2_DrawButtonUTF8(u8g2, 177, 126, U8G2_BTN_BW1 | U8G2_BTN_INV, 42, 4, 2, "ARM");

    u8g2_SetFont(u8g2, u8g2_font_5x8_tr);
    u8g2_DrawStr(u8g2, 7, 128, "CH A:B");
    u8g2_DrawStr(u8g2, 218, 128, "v0.1");

    flush_to_lcd();
}
