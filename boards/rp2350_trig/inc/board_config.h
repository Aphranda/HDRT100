#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "hardware/i2c.h"
#include "hardware/pio.h"

/* Overclock: RP2350 default 150 MHz. 250 MHz very safe (67% boost).
 * 300 MHz community-verified. >300 needs voltage bump (silicon lottery). */
#define BOARD_SYS_CLOCK_HZ  250000000u
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
#define BOARD_LCD_SCK_PIN BOARD_SPI_CLK_PIN
#define BOARD_LCD_MOSI_PIN BOARD_SPI_MOSI_PIN
#define BOARD_LCD_WIDTH 240u
#define BOARD_LCD_HEIGHT 135u
#define BOARD_LCD_X_OFFSET 40u
#define BOARD_LCD_Y_OFFSET 52u
#define BOARD_LCD_BACKLIGHT_ACTIVE_HIGH 0

#define BOARD_SD_SPI_PORT BOARD_SPI_PORT
#define BOARD_SD_SPI_CLK_PIN BOARD_SPI_CLK_PIN
#define BOARD_SD_SPI_MOSI_PIN BOARD_SPI_MOSI_PIN
#define BOARD_SD_SPI_MISO_PIN BOARD_SPI_MISO_PIN
#define BOARD_SD_SPI_CS_PIN 15u

#define BOARD_I2C_ENABLED 0
#define BOARD_I2C_PORT i2c0
#define BOARD_I2C_SDA_PIN 8u
#define BOARD_I2C_SCL_PIN 9u
#define BOARD_I2C_BAUD_HZ 400000u

#define BOARD_UART_PORT uart1
#define BOARD_UART_TX_PIN 4u
#define BOARD_UART_RX_PIN 5u

#define BOARD_SYNC_PIO_FAST pio0
#define BOARD_SYNC_PIO_WAVE pio1
#define BOARD_SYNC_PIO_AUX pio2

#define BOARD_SYNC_CAPTURE_SM 0u
#define BOARD_SYNC_TIMESTAMP_SM 1u
#define BOARD_SYNC_QUALIFIER_SM 2u
#define BOARD_SYNC_ARM_SM 3u

#define BOARD_SYNC_OUTPUT_SM 0u
#define BOARD_SYNC_CLOCK_SM 1u
#define BOARD_SYNC_GATE_SM 2u
#define BOARD_SYNC_MARKER_SM 3u

#define BOARD_SYNC_AUX0_SM 0u
#define BOARD_SYNC_AUX1_SM 1u
#define BOARD_SYNC_AUX2_SM 2u
#define BOARD_SYNC_AUX3_SM 3u

#define BOARD_SYNC_INPUT_BASE_PIN 16u
#define BOARD_SYNC_INPUT_PIN_COUNT 4u
#define BOARD_SYNC_TRIG_IN_PIN 16u
#define BOARD_SYNC_ARM_IN_PIN 17u
#define BOARD_SYNC_EXT_CLK_IN_PIN 18u
#define BOARD_SYNC_GATE_IN_PIN 19u

#define BOARD_SYNC_OUTPUT_BASE_PIN 20u
#define BOARD_SYNC_OUTPUT_PIN_COUNT 4u
#define BOARD_SYNC_TRIG_OUT_PIN 20u
#define BOARD_SYNC_PULSE_OUT_PIN 21u
#define BOARD_SYNC_SYNC_CLK_OUT_PIN 22u
#define BOARD_SYNC_MARKER_OUT_PIN 23u

#define BOARD_SYNC_AUX0_PIN 26u
#define BOARD_SYNC_AUX1_PIN 27u
#define BOARD_SYNC_AUX2_PIN 28u
#define BOARD_SYNC_AUX3_PIN 29u
#define BOARD_SYNC_AUX_PIN_COUNT 4u

/* Product AUX semantic aliases.
 * Current low-level trigger paths still use the legacy BOARD_SYNC_* pins above.
 * Product migration should move framework-level ARM/EXT_CLK/SYNC/MARKER
 * functions to these AUX aliases so the main trigger IO remains mode-pure. */
#define BOARD_SYNC_AUX_ARM_IN_PIN      BOARD_SYNC_AUX0_PIN
#define BOARD_SYNC_AUX_EXT_CLK_IN_PIN  BOARD_SYNC_AUX1_PIN
#define BOARD_SYNC_AUX_SYNC_CLK_OUT_PIN BOARD_SYNC_AUX2_PIN
#define BOARD_SYNC_AUX_MARKER_OUT_PIN  BOARD_SYNC_AUX3_PIN

#endif
