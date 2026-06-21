#include "u8g2_port.h"

#include "osal.h"

static const u8x8_display_info_t s_u8g2_port_display_info = {
    /* chip_enable_level = */ 0,
    /* chip_disable_level = */ 1,
    /* post_chip_enable_wait_ns = */ 0,
    /* pre_chip_disable_wait_ns = */ 0,
    /* reset_pulse_width_ms = */ 0,
    /* post_reset_wait_ms = */ 0,
    /* sda_setup_time_ns = */ 0,
    /* sck_pulse_width_ns = */ 0,
    /* sck_clock_hz = */ 40000000UL,
    /* spi_mode = */ 0,
    /* i2c_bus_clock_100kHz = */ 4,
    /* data_setup_time_ns = */ 0,
    /* write_pulse_width_ns = */ 0,
    /* tile_width = */ 30,
    /* tile_height = */ 17,
    /* default_x_offset = */ 0,
    /* flipmode_x_offset = */ 0,
    /* pixel_width = */ 240,
    /* pixel_height = */ 136,
};

static uint8_t u8g2_port_display_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)arg_int;
    (void)arg_ptr;

    switch (msg) {
    case U8X8_MSG_DISPLAY_SETUP_MEMORY:
        u8x8_d_helper_display_setup_memory(u8x8, &s_u8g2_port_display_info);
        return 1;
    case U8X8_MSG_DISPLAY_INIT:
    case U8X8_MSG_DISPLAY_SET_POWER_SAVE:
    case U8X8_MSG_DISPLAY_SET_FLIP_MODE:
        return 1;
    default:
        return 1;
    }
}

void u8g2_port_setup_240x136(u8g2_t *u8g2, uint8_t *buffer)
{
    u8g2_SetupDisplay(u8g2, u8g2_port_display_cb, u8x8_cad_empty, u8x8_byte_empty, u8g2_port_gpio_and_delay);
    u8g2_SetupBuffer(u8g2, buffer, 17, u8g2_ll_hvline_vertical_top_lsb, U8G2_R0);
}

uint8_t u8g2_port_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    (void)u8x8;
    (void)arg_ptr;

    switch (msg) {
    case U8X8_MSG_DELAY_MILLI:
        osal_delay_ms(arg_int);
        return 1;
    case U8X8_MSG_DELAY_10MICRO:
    case U8X8_MSG_DELAY_100NANO:
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
    case U8X8_MSG_GPIO_RESET:
    case U8X8_MSG_GPIO_CS:
    case U8X8_MSG_GPIO_DC:
        return 1;
    default:
        return 0;
    }
}
