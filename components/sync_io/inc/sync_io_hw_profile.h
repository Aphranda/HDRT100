#ifndef SYNC_IO_HW_PROFILE_H
#define SYNC_IO_HW_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"

/* Frozen product hardware profile:
 * - GPIO16..19: fixed SYNC_IO input group.
 * - GPIO20..23: fixed SYNC_IO output group.
 * - GPIO26..29: AUX is fixed as two RX + two TX, reused by firmware persona.
 */

#define SYNC_IO_HW_MAIN_INPUT_BASE_PIN   BOARD_SYNC_INPUT_BASE_PIN
#define SYNC_IO_HW_MAIN_INPUT_PIN_COUNT  BOARD_SYNC_INPUT_PIN_COUNT
#define SYNC_IO_HW_MAIN_OUTPUT_BASE_PIN  BOARD_SYNC_OUTPUT_BASE_PIN
#define SYNC_IO_HW_MAIN_OUTPUT_PIN_COUNT BOARD_SYNC_OUTPUT_PIN_COUNT

#define SYNC_IO_HW_TRIG_IN_PIN       BOARD_SYNC_TRIG_IN_PIN
#define SYNC_IO_HW_GATE_IN_PIN       BOARD_SYNC_GATE_IN_PIN
#define SYNC_IO_HW_TRIG_OUT_PIN      BOARD_SYNC_TRIG_OUT_PIN
#define SYNC_IO_HW_RJ45_TRIG_IN_PIN  BOARD_SYNC_GATE_IN_PIN
#define SYNC_IO_HW_RJ45_TRIG_OUT_PIN BOARD_SYNC_MARKER_OUT_PIN

#define SYNC_IO_HW_ENC_A_PIN  BOARD_SYNC_INPUT_BASE_PIN
#define SYNC_IO_HW_ENC_B_PIN  (BOARD_SYNC_INPUT_BASE_PIN + 1u)
#define SYNC_IO_HW_ENC_Z_PIN  (BOARD_SYNC_INPUT_BASE_PIN + 3u)

#define SYNC_IO_HW_AUX0_PIN BOARD_SYNC_AUX0_PIN
#define SYNC_IO_HW_AUX1_PIN BOARD_SYNC_AUX1_PIN
#define SYNC_IO_HW_AUX2_PIN BOARD_SYNC_AUX2_PIN
#define SYNC_IO_HW_AUX3_PIN BOARD_SYNC_AUX3_PIN

#define SYNC_IO_HW_BISS_CLK_IN_PIN   BOARD_SYNC_AUX0_PIN
#define SYNC_IO_HW_BISS_DATA_IN_PIN  BOARD_SYNC_AUX1_PIN
#define SYNC_IO_HW_BISS_CLK_OUT_PIN  BOARD_SYNC_AUX2_PIN
#define SYNC_IO_HW_BISS_DATA_OUT_PIN BOARD_SYNC_AUX3_PIN

#define SYNC_IO_HW_AUX_RX_MASK ((1u << 0) | (1u << 1))
#define SYNC_IO_HW_AUX_TX_MASK ((1u << 2) | (1u << 3))

static inline bool sync_io_hw_enc_pins_valid(uint32_t a_pin,
                                             uint32_t b_pin,
                                             uint32_t z_pin)
{
    return a_pin == SYNC_IO_HW_ENC_A_PIN &&
           b_pin == SYNC_IO_HW_ENC_B_PIN &&
           z_pin == SYNC_IO_HW_ENC_Z_PIN;
}

static inline bool sync_io_hw_aux_channel_valid(uint32_t channel)
{
    return channel < BOARD_SYNC_AUX_PIN_COUNT;
}

static inline bool sync_io_hw_aux_supports_input(uint32_t channel)
{
    return sync_io_hw_aux_channel_valid(channel) &&
           ((SYNC_IO_HW_AUX_RX_MASK & (1u << channel)) != 0u);
}

static inline bool sync_io_hw_aux_supports_output(uint32_t channel)
{
    return sync_io_hw_aux_channel_valid(channel) &&
           ((SYNC_IO_HW_AUX_TX_MASK & (1u << channel)) != 0u);
}

#endif
