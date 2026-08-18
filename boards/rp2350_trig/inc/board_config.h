#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

#include "hardware/i2c.h"
#include "hardware/pio.h"

/* Overclock: RP2350 default 150 MHz. 250 MHz very safe (67% boost).
 * 300 MHz community-verified. >300 needs voltage bump (silicon lottery). */
#define BOARD_SYS_CLOCK_HZ  250000000u
#include "hardware/spi.h"
#include "hardware/uart.h"

#define BOARD_LED_SYSTEM_PIN 3u
#define BOARD_LED_ARM_TRIGGER_PIN 8u
#define BOARD_LED_FAULT_PIN 9u
#define BOARD_LED_SYSTEM_ACTIVE_HIGH 0
#define BOARD_LED_ARM_TRIGGER_ACTIVE_HIGH 0
#define BOARD_LED_FAULT_ACTIVE_HIGH 0

/* Compatibility name used by the existing heartbeat path. */
#define BOARD_STATUS_LED_PIN BOARD_LED_SYSTEM_PIN

#define BOARD_KEY1_PIN 2u
#define BOARD_KEY2_PIN 6u
#define BOARD_KEY3_PIN 7u
#define BOARD_KEY1_ACTIVE_LOW 1
#define BOARD_KEY2_ACTIVE_LOW 1
#define BOARD_KEY3_ACTIVE_LOW 1

/* TF card: dedicated SPI1 bus. */
#define BOARD_SPI_PORT spi1
#define BOARD_SPI_CLK_PIN 10u
#define BOARD_SPI_MOSI_PIN 11u
#define BOARD_SPI_MISO_PIN 12u
#define BOARD_SPI_CS_PIN 15u

/* LCD: dedicated write-only SPI0 bus (no MISO). */
#define BOARD_LCD_SPI_PORT spi0
#define BOARD_LCD_RST_PIN 34u
#define BOARD_LCD_BL_PIN 35u
#define BOARD_LCD_DC_PIN 36u
#define BOARD_LCD_CS_PIN 37u
#define BOARD_LCD_SCK_PIN 38u
#define BOARD_LCD_MOSI_PIN 39u
/* 0.96-inch ST7735S panel, native portrait RAM window 80(H) x 160(V).
 * The UI is rendered as 160x80 landscape and rotated in the flush path. */
#define BOARD_LCD_WIDTH 80u
#define BOARD_LCD_HEIGHT 160u
#define BOARD_LCD_X_OFFSET 24u
#define BOARD_LCD_Y_OFFSET 1u
#define BOARD_LCD_BACKLIGHT_ACTIVE_HIGH 0

#define BOARD_SD_SPI_PORT BOARD_SPI_PORT
#define BOARD_SD_SPI_CLK_PIN BOARD_SPI_CLK_PIN
#define BOARD_SD_SPI_MOSI_PIN BOARD_SPI_MOSI_PIN
#define BOARD_SD_SPI_MISO_PIN BOARD_SPI_MISO_PIN
#define BOARD_SD_SPI_CS_PIN 15u
#define BOARD_SD_CARD_DETECT_PIN 14u

/* Debug minimum two-board RefMem PIO-SPI transport.
 * This is a P4.5 bring-up profile. It uses PIO for bit timing and only lets
 * CPU/SCPI trigger frame-level TX/RX transactions. It overlaps the debug
 * SYNC_IO groups and must only be armed while the RefMem SPI HIL owns them. */
#define BOARD_REFMEM_SPI_PIO pio0
#define BOARD_REFMEM_SPI_TX_SM 2u
#define BOARD_REFMEM_SPI_RX_SM 3u
#define BOARD_REFMEM_SPI_RX_PIN 16u
#define BOARD_REFMEM_SPI_CSN_PIN 17u
#define BOARD_REFMEM_SPI_SCK_PIN 18u
#define BOARD_REFMEM_SPI_TX_PIN 19u
#define BOARD_REFMEM_SPI_BAUD_HZ 25000000u

/* Product-board TDMA SPI persona over BiSS + RJ45 Trigger.
 * RJ45 Trigger is the frame-sync CS signal; BiSS supplies one clock and one
 * data direction. Keep the proven TDMA PIO protocol unchanged and migrate
 * only its logical signal roles:
 *   TX: CS=GPIO26 (TRIG_OUT), SCK=GPIO25 (CLK1_OUT), TX=GPIO29 (DATA0_OUT)
 *   RX: CS=GPIO27 (TRIG_IN),  SCK=GPIO28 (CLK0_IN),  RX=GPIO24 (DATA1_IN)
 * Product communication owns PIO2 SM0/SM1 while this persona is active. */
#define BOARD_TDMA_SPI_PIO pio2
#define BOARD_TDMA_SPI_MASTER_SM 0u
#define BOARD_TDMA_SPI_SLAVE_SM 1u
#define BOARD_TDMA_SPI_UPLINK_CSN_PIN 27u
#define BOARD_TDMA_SPI_UPLINK_RX_PIN 24u
#define BOARD_TDMA_SPI_UPLINK_SCK_PIN 28u
#define BOARD_TDMA_SPI_DOWNLINK_CSN_PIN 26u
#define BOARD_TDMA_SPI_DOWNLINK_SCK_PIN 25u
#define BOARD_TDMA_SPI_DOWNLINK_TX_PIN 29u
/* Optimization target rate after CS/frame-sync bring-up. */
#define BOARD_TDMA_SPI_BAUD_HZ 10000000u

/* ISO1452 controls. /RE is active low and DE is active high. Board startup
 * enables receivers but leaves every differential driver disabled. */
#define BOARD_UP_BISS_DE_PIN 30u
#define BOARD_DN_BISS_DE_PIN 31u
#define BOARD_TRIG_DE_PIN 32u
#define BOARD_DN_BISS_RE_PIN 40u
#define BOARD_TRIG_RE_PIN 41u
#define BOARD_UP_BISS_RE_PIN 42u

#define BOARD_I2C_ENABLED 0
#define BOARD_I2C_PORT i2c0
#define BOARD_I2C_SDA_PIN 8u
#define BOARD_I2C_SCL_PIN 9u
#define BOARD_I2C_BAUD_HZ 400000u

/* On-board CH343 debug UART. */
#define BOARD_UART_PORT uart0
#define BOARD_UART_TX_PIN 0u
#define BOARD_UART_RX_PIN 1u

/* External RS485 UART remains receive-only until its own driver is enabled. */
#define BOARD_RS485_UART_PORT uart1
#define BOARD_RS485_UART_TX_PIN 4u
#define BOARD_RS485_UART_RX_PIN 5u
#define BOARD_UART_DE_PIN 13u

/* The legacy GPIO4..7 simulation overlay conflicts with UART1 and product
 * KEY2/KEY3. Keep its API present but reject activation on this board. */
#define BOARD_DEBUG_MODEL_GPIO_ENABLED 0
#define BOARD_DEBUG_MODEL_GPIO_BASE_PIN 4u
#define BOARD_DEBUG_MODEL_GPIO_PIN_COUNT 4u
#define BOARD_DEBUG_MODEL_UART_CONFLICT_MASK ((1u << 0) | (1u << 1))

#define BOARD_SYNC_PIO_FAST pio0
#define BOARD_SYNC_PIO_WAVE pio1
#define BOARD_SYNC_PIO_AUX pio2

/* GPIO24..29 and PIO2 are owned by the product TDMA SPI persona. */
#define BOARD_SYNC_AUX_ENABLED 0
#define BOARD_SYNC_RJ45_TRIGGER_ENABLED 0

#define BOARD_SYNC_CAPTURE_SM 0u
#define BOARD_SYNC_TIMESTAMP_SM 1u
#define BOARD_SYNC_RJ45_TRIG_IN_SM 2u
/* Mode-level alias: pio0/sm2 owns the RJ45_TRIG_IN hardware channel;
 * qualifier/gate/inhibit are software interpretations of that input. */
#define BOARD_SYNC_QUALIFIER_SM BOARD_SYNC_RJ45_TRIG_IN_SM
#define BOARD_SYNC_ARM_SM 3u

#define BOARD_SYNC_AUX0_SM 0u
#define BOARD_SYNC_AUX1_SM 1u
#define BOARD_SYNC_AUX2_SM 2u
#define BOARD_SYNC_AUX3_SM 3u

#define BOARD_SYNC_OUTPUT_SM 0u
/* Dedicated model/simulation scheduled pulse channel.  It is intentionally
 * separate from the product trigger output mode on pio1/sm0. */
#define BOARD_SYNC_MODEL_SCHED_SM 1u
/* SYNC_CLK_OUT is a framework/AUX signal, not main OUT2. */
#define BOARD_SYNC_CLOCK_SM BOARD_SYNC_AUX2_SM
#define BOARD_SYNC_GATE_SM 2u
#define BOARD_SYNC_MARKER_SM 3u

#ifndef PROJECT_SYNC_IO_INPUT_BASE_PIN
#define PROJECT_SYNC_IO_INPUT_BASE_PIN 20u
#endif

#ifndef PROJECT_SYNC_IO_OUTPUT_BASE_PIN
#define PROJECT_SYNC_IO_OUTPUT_BASE_PIN 16u
#endif

#define BOARD_SYNC_INPUT_BASE_PIN PROJECT_SYNC_IO_INPUT_BASE_PIN
#define BOARD_SYNC_INPUT_PIN_COUNT 4u
#define BOARD_SYNC_TRIG_IN_PIN (BOARD_SYNC_INPUT_BASE_PIN + 3u)
#define BOARD_SYNC_ARM_IN_PIN (BOARD_SYNC_INPUT_BASE_PIN + 2u)
#define BOARD_SYNC_EXT_CLK_IN_PIN (BOARD_SYNC_INPUT_BASE_PIN + 1u)
#define BOARD_SYNC_GATE_IN_PIN (BOARD_SYNC_INPUT_BASE_PIN + 0u)
#define BOARD_SYNC_RJ45_TRIG_IN_PIN BOARD_TDMA_SPI_UPLINK_CSN_PIN
#define BOARD_SYNC_INPUT_BITS_REVERSED 1

/* Configurable debug/product board profile:
 * SYNC_IO uses contiguous 4-pin RX/TX groups. Defaults are selected by CMake
 * for the current minimum-system two-board wiring, but a build may override
 * PROJECT_SYNC_IO_INPUT_BASE_PIN / PROJECT_SYNC_IO_OUTPUT_BASE_PIN. */
#define BOARD_SYNC_OUTPUT_BASE_PIN PROJECT_SYNC_IO_OUTPUT_BASE_PIN
#define BOARD_SYNC_OUTPUT_PIN_COUNT 4u
#define BOARD_SYNC_TRIG_OUT_PIN (BOARD_SYNC_OUTPUT_BASE_PIN + 0u)
#define BOARD_SYNC_PULSE_OUT_PIN (BOARD_SYNC_OUTPUT_BASE_PIN + 1u)
#define BOARD_SYNC_MODE_OUT2_PIN (BOARD_SYNC_OUTPUT_BASE_PIN + 2u)
#define BOARD_SYNC_RJ45_TRIG_OUT_PIN BOARD_TDMA_SPI_DOWNLINK_CSN_PIN
#define BOARD_SYNC_RJ45_TRIGGER_SM BOARD_SYNC_MARKER_SM
/* Deprecated compatibility alias: marker output is no longer a separate
 * physical product signal; legacy MARK:* commands pulse RJ45_TRIG_OUT. */
#define BOARD_SYNC_MARKER_OUT_PIN BOARD_SYNC_RJ45_TRIG_OUT_PIN

#define BOARD_SYNC_AUX0_PIN 26u
#define BOARD_SYNC_AUX1_PIN 27u
#define BOARD_SYNC_AUX2_PIN 28u
#define BOARD_SYNC_AUX3_PIN 29u
#define BOARD_SYNC_AUX_PIN_COUNT 4u

/* Product AUX semantic aliases.
 * Current low-level trigger paths still use the legacy BOARD_SYNC_* pins above.
 * Product migration should move framework-level ARM/EXT_CLK/SYNC functions
 * to these AUX aliases so the main trigger IO remains mode-pure. */
#define BOARD_SYNC_AUX_ARM_IN_PIN      BOARD_SYNC_AUX0_PIN
#define BOARD_SYNC_AUX_EXT_CLK_IN_PIN  BOARD_SYNC_AUX1_PIN
#define BOARD_SYNC_AUX_SYNC_CLK_OUT_PIN BOARD_SYNC_AUX2_PIN
#define BOARD_SYNC_AUX3_OUT_PIN        BOARD_SYNC_AUX3_PIN

/* Migrated semantic alias: SYNC_CLK_OUT lives on AUX2/GPIO28. */
#define BOARD_SYNC_SYNC_CLK_OUT_PIN BOARD_SYNC_AUX_SYNC_CLK_OUT_PIN

#endif
