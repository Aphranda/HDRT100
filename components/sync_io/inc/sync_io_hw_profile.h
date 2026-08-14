#ifndef SYNC_IO_HW_PROFILE_H
#define SYNC_IO_HW_PROFILE_H

#include <stdbool.h>
#include <stdint.h>

#include "board_config.h"

/* Active board hardware profile:
 * - PROJECT_SYNC_IO_INPUT_BASE_PIN..+3: contiguous SYNC_IO input group.
 * - input +2: ENC_COUNT Z software input.
 * - input +3: RJ45_TRIG_IN compatibility input; gate is mode semantics.
 * - PROJECT_SYNC_IO_OUTPUT_BASE_PIN..+3: contiguous SYNC_IO output group.
 * - output +3: RJ45_TRIG_OUT compatibility output; MARK:* is compatibility.
 * - GPIO26..29: AUX is fixed as two RX + two TX, reused by firmware persona.
 *
 * The active profile is selected by CMake/board profile. Product-board pinout
 * remains documented in docs/hardware/ and should use its own values.
 */

#define SYNC_IO_HW_MAIN_INPUT_BASE_PIN   BOARD_SYNC_INPUT_BASE_PIN
#define SYNC_IO_HW_MAIN_INPUT_PIN_COUNT  BOARD_SYNC_INPUT_PIN_COUNT
#define SYNC_IO_HW_MAIN_OUTPUT_BASE_PIN  BOARD_SYNC_OUTPUT_BASE_PIN
#define SYNC_IO_HW_MAIN_OUTPUT_PIN_COUNT BOARD_SYNC_OUTPUT_PIN_COUNT

#define SYNC_IO_HW_TRIG_IN_PIN       BOARD_SYNC_TRIG_IN_PIN
#define SYNC_IO_HW_GATE_IN_PIN       BOARD_SYNC_GATE_IN_PIN
#define SYNC_IO_HW_TRIG_OUT_PIN      BOARD_SYNC_TRIG_OUT_PIN
#define SYNC_IO_HW_RJ45_TRIG_IN_PIN  BOARD_SYNC_RJ45_TRIG_IN_PIN
#define SYNC_IO_HW_RJ45_TRIG_OUT_PIN BOARD_SYNC_RJ45_TRIG_OUT_PIN
#define SYNC_IO_HW_RJ45_TRIG_IN_SM   BOARD_SYNC_RJ45_TRIG_IN_SM

#define SYNC_IO_HW_ENC_A_PIN  BOARD_SYNC_INPUT_BASE_PIN
#define SYNC_IO_HW_ENC_B_PIN  (BOARD_SYNC_INPUT_BASE_PIN + 1u)
#define SYNC_IO_HW_ENC_Z_PIN  (BOARD_SYNC_INPUT_BASE_PIN + 2u)

#define SYNC_IO_HW_AUX0_PIN BOARD_SYNC_AUX0_PIN
#define SYNC_IO_HW_AUX1_PIN BOARD_SYNC_AUX1_PIN
#define SYNC_IO_HW_AUX2_PIN BOARD_SYNC_AUX2_PIN
#define SYNC_IO_HW_AUX3_PIN BOARD_SYNC_AUX3_PIN

#define SYNC_IO_HW_ARM_IN_PIN       BOARD_SYNC_AUX_ARM_IN_PIN
#define SYNC_IO_HW_EXT_CLK_IN_PIN   BOARD_SYNC_AUX_EXT_CLK_IN_PIN
#define SYNC_IO_HW_SYNC_CLK_OUT_PIN BOARD_SYNC_AUX_SYNC_CLK_OUT_PIN

#define SYNC_IO_HW_BISS_CLK_IN_PIN   BOARD_SYNC_AUX0_PIN
#define SYNC_IO_HW_BISS_DATA_IN_PIN  BOARD_SYNC_AUX1_PIN
#define SYNC_IO_HW_BISS_CLK_OUT_PIN  BOARD_SYNC_AUX2_PIN
#define SYNC_IO_HW_BISS_DATA_OUT_PIN BOARD_SYNC_AUX3_PIN

#define SYNC_IO_HW_AUX_RX_MASK ((1u << 0) | (1u << 1))
#define SYNC_IO_HW_AUX_TX_MASK ((1u << 2) | (1u << 3))

_Static_assert(SYNC_IO_HW_MAIN_INPUT_BASE_PIN <= 26u, "SYNC_IO input group must fit GPIO0..29");
_Static_assert(SYNC_IO_HW_MAIN_INPUT_PIN_COUNT == 4u, "SYNC_IO input count must be 4");
_Static_assert(SYNC_IO_HW_MAIN_OUTPUT_BASE_PIN <= 26u, "SYNC_IO output group must fit GPIO0..29");
_Static_assert(SYNC_IO_HW_MAIN_OUTPUT_PIN_COUNT == 4u, "SYNC_IO output count must be 4");
_Static_assert((SYNC_IO_HW_MAIN_OUTPUT_BASE_PIN > (SYNC_IO_HW_MAIN_INPUT_BASE_PIN + 3u)) ||
               ((SYNC_IO_HW_MAIN_OUTPUT_BASE_PIN + 3u) < SYNC_IO_HW_MAIN_INPUT_BASE_PIN),
               "SYNC_IO input and output groups must not overlap");
_Static_assert(SYNC_IO_HW_RJ45_TRIG_IN_PIN == (SYNC_IO_HW_MAIN_INPUT_BASE_PIN + 3u),
               "RJ45_TRIG_IN must be input group IN3");
_Static_assert(SYNC_IO_HW_RJ45_TRIG_OUT_PIN == (SYNC_IO_HW_MAIN_OUTPUT_BASE_PIN + 3u),
               "RJ45_TRIG_OUT must be output group OUT3");
_Static_assert(SYNC_IO_HW_ARM_IN_PIN == 26u, "ARM_IN must be AUX0/GPIO26");
_Static_assert(SYNC_IO_HW_EXT_CLK_IN_PIN == 27u, "EXT_CLK_IN must be AUX1/GPIO27");
_Static_assert(SYNC_IO_HW_SYNC_CLK_OUT_PIN == 28u, "SYNC_CLK_OUT must be AUX2/GPIO28");
_Static_assert(SYNC_IO_HW_AUX3_PIN == 29u, "AUX3 must be GPIO29");
_Static_assert(BOARD_SYNC_MARKER_OUT_PIN == SYNC_IO_HW_RJ45_TRIG_OUT_PIN,
               "MARKER_OUT alias must resolve to RJ45_TRIG_OUT");
_Static_assert(BOARD_SYNC_SYNC_CLK_OUT_PIN == SYNC_IO_HW_SYNC_CLK_OUT_PIN,
               "SYNC_CLK_OUT must resolve to AUX2/GPIO28");

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
