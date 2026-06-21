#include "board.h"

#include "board_config.h"
#include "diagnostics.h"
#include "drv_i2c.h"
#include "drv_spi.h"
#include "drv_uart.h"
#include "drv_watchdog.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "lcd_st7789.h"
#include "project_config.h"

static bool s_led_state;

static bool board_init_status_led(void)
{
    gpio_init(BOARD_STATUS_LED_PIN);
    gpio_set_dir(BOARD_STATUS_LED_PIN, GPIO_OUT);
    board_status_led_set(false);
    return true;
}

static bool board_init_spi(void)
{
    const drv_spi_config_t config = {
        .instance = BOARD_SPI_PORT,
        .sck_pin = BOARD_SPI_CLK_PIN,
        .mosi_pin = BOARD_SPI_MOSI_PIN,
        .miso_pin = BOARD_SPI_MISO_PIN,
        .cs_pin = BOARD_SPI_CS_PIN,
        .baud_hz = BOARD_SPI_BAUD_HZ,
    };

    return drv_spi_init(&config);
}

static bool board_init_i2c(void)
{
    const drv_i2c_config_t config = {
        .instance = BOARD_I2C_PORT,
        .sda_pin = BOARD_I2C_SDA_PIN,
        .scl_pin = BOARD_I2C_SCL_PIN,
        .baud_hz = BOARD_I2C_BAUD_HZ,
    };

    return drv_i2c_init(&config);
}

static bool board_init_uart(void)
{
    const drv_uart_config_t config = {
        .instance = BOARD_UART_PORT,
        .tx_pin = BOARD_UART_TX_PIN,
        .rx_pin = BOARD_UART_RX_PIN,
        .baud_hz = BOARD_UART_BAUD_HZ,
    };

    return drv_uart_init(&config);
}

static bool board_init_lcd(void)
{
    const lcd_st7789_config_t config = {
        .spi = BOARD_LCD_SPI_PORT,
        .dc_pin = BOARD_LCD_DC_PIN,
        .cs_pin = BOARD_LCD_CS_PIN,
        .bl_pin = BOARD_LCD_BL_PIN,
        .width = BOARD_LCD_WIDTH,
        .height = BOARD_LCD_HEIGHT,
        .x_offset = BOARD_LCD_X_OFFSET,
        .y_offset = BOARD_LCD_Y_OFFSET,
        .rotation = LCD_ST7789_ROTATION_90,
        .backlight_active_high = BOARD_LCD_BACKLIGHT_ACTIVE_HIGH != 0,
    };

    return lcd_st7789_init(&config);
}

bool board_init(void)
{
    diagnostics_init();

    const bool ok = board_init_status_led() && board_init_spi() && board_init_i2c() && board_init_lcd();
    board_init_uart();

    if (!ok) {
        diagnostics_mark_fault("board", "board initialization failed");
        return false;
    }

    drv_watchdog_enable(PROJECT_WATCHDOG_TIMEOUT_MS);

    LOG_INFO("board", "%s v%u.%u.%u boot",
             PROJECT_NAME,
             PROJECT_VERSION_MAJOR,
             PROJECT_VERSION_MINOR,
             PROJECT_VERSION_PATCH);
    LOG_INFO("clock", "sys=%lu usb=%lu",
             (unsigned long)clock_get_hz(clk_sys),
             (unsigned long)clock_get_hz(clk_usb));

    return true;
}

void board_service(void)
{
    drv_watchdog_feed();
}

void board_status_led_set(bool on)
{
    s_led_state = on;
    gpio_put(BOARD_STATUS_LED_PIN, on ? 1 : 0);
}

void board_status_led_toggle(void)
{
    board_status_led_set(!s_led_state);
}

uint32_t board_uptime_ms(void)
{
    return to_ms_since_boot(get_absolute_time());
}
