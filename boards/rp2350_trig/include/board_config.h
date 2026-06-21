#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "hardware/i2c.h"
#include "hardware/pio.h"
#include "hardware/spi.h"
#include "hardware/uart.h"

#define BOARD_STATUS_LED_PIN 3u

#define BOARD_SPI_PORT spi1
#define BOARD_SPI_CLK_PIN 10u
#define BOARD_SPI_MOSI_PIN 11u
#define BOARD_SPI_MISO_PIN 12u
#define BOARD_SPI_CS_PIN 9u

#define BOARD_LCD_SPI_PORT BOARD_SPI_PORT
#define BOARD_LCD_DC_PIN 8u
#define BOARD_LCD_CS_PIN 9u
#define BOARD_LCD_BL_PIN 25u
#define BOARD_LCD_WIDTH 240u
#define BOARD_LCD_HEIGHT 135u
#define BOARD_LCD_X_OFFSET 40u
#define BOARD_LCD_Y_OFFSET 52u
#define BOARD_LCD_BACKLIGHT_ACTIVE_HIGH 0

#define BOARD_I2C_PORT i2c0
#define BOARD_I2C_SDA_PIN 8u
#define BOARD_I2C_SCL_PIN 9u
#define BOARD_I2C_BAUD_HZ 400000u

#define BOARD_UART_PORT uart1
#define BOARD_UART_TX_PIN 4u
#define BOARD_UART_RX_PIN 5u

#define BOARD_PIO pio0
#define BOARD_PIO_SM 0u

#endif
