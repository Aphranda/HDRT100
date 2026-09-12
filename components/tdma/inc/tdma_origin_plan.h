#ifndef TDMA_ORIGIN_PLAN_H
#define TDMA_ORIGIN_PLAN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "tdma_flight_overlay.h"

/* Internal owner-built RP2350 program. No wire word is an address or PIO PC.
 * These construction bounds are not a resource grant or a cadence contract. */
#define TDMA_ORIGIN_PLAN_RUN_MAX 384u
#define TDMA_ORIGIN_PLAN_LITERAL_MAX 160u
#define TDMA_ORIGIN_PLAN_BANK_COUNT 2u
#define TDMA_ORIGIN_PLAN_SHADOW_BYTES (TDMA_FLIGHT_SHORT_SLOT_SIZE + sizeof(uint32_t))
#define TDMA_ORIGIN_CONTROL_PC 0u
#define TDMA_ORIGIN_CAPTURE_PC 11u
#define TDMA_ORIGIN_RTT_PC 21u
#define TDMA_ORIGIN_LATCH_PC 27u
#define TDMA_ORIGIN_FAULT_PC 31u
#define TDMA_ORIGIN_HELPER_PC 0u
#define TDMA_ORIGIN_TEST_BIT_PC 2u
#define TDMA_ORIGIN_COMPARE_PC 9u
#define TDMA_ORIGIN_DATA_PC 20u

/* Instruction timing of tdma_origin_control. Divider units are 1/256 of
 * clk_sys. This calculation describes the physical cadence only; it does
 * not grant a DMA arbitration budget or change the Core1/VDC periods. */
#define TDMA_ORIGIN_CONTROL_CYCLES_PER_BIT 6u
#define TDMA_ORIGIN_CONTROL_GUARD_CYCLES 32u
#define TDMA_ORIGIN_CONTROL_CYCLE_OVERHEAD 8u
typedef struct {
    uint32_t divider256;
    uint32_t guard_count;
    uint64_t period_floor_ticks;
    uint64_t period_ceiling_ticks;
    uint64_t guard_floor_ticks;
} tdma_origin_cadence_t;

bool tdma_origin_cadence_calculate(uint32_t clk_sys_hz, uint32_t baud_hz,
                                 uint32_t physical_bytes, uint32_t period_ns,
                                 tdma_origin_cadence_t *cadence);

typedef struct {
    uint32_t sequence;
    uint32_t identity;
    uint32_t local_generation;
    uint32_t output_remaining;
    uint32_t rtt_remaining;
    uint32_t rtt_present;
} tdma_origin_observation_t;

typedef struct {
    uint32_t remaining_snapshot;
    uint32_t polls_left;
    uint32_t capture_bank;
    uint32_t good_bank;
    uint32_t local_next_address;
    uint32_t local_selected_generation;
    uint32_t fault;
    uint32_t last_local_sequence;
    uint32_t last_return_sequence;
    uint32_t boundary_token;
    uint32_t bank_version[TDMA_ORIGIN_PLAN_BANK_COUNT];
    /* One local boundary observation, guarded independently of RX banks.
     * RTT is a raw countdown, never an absolute edge time or valid return. */
    uint32_t observation_version;
    uint32_t observation_sequence;
    uint32_t observation_identity;
    uint32_t observation_local_generation;
    uint32_t output_remaining_snapshot;
    uint32_t rtt_remaining;
    uint32_t rtt_present;
    /* Written before publishing good_bank. Each RX bank carries the local
     * observation for its own returned identity, even if Core1 skips cycles. */
    tdma_origin_observation_t bank_observation[TDMA_ORIGIN_PLAN_BANK_COUNT];
} tdma_origin_plan_state_t;

typedef struct {
    uint32_t capture_bank[TDMA_ORIGIN_PLAN_BANK_COUNT];
    uint32_t stage;
    uint32_t tx_header;
    uint32_t rx_packet;
    uint32_t state;
    uint32_t local_shadow[TDMA_ORIGIN_PLAN_BANK_COUNT];
    uint32_t scratch;
    uint32_t runs;
    uint32_t literals;
} tdma_origin_plan_addresses_t;

/* Addresses must be derived from owner-owned SRAM. Binding to actual CPU
 * pointers, SDK channel claims, arbiter leases, catalog placements and
 * Calibration timing admission belong to the physical owner before ARM. */
typedef struct {
    tdma_origin_plan_addresses_t address;
    uint32_t physical_bytes;
    uint32_t outer_header_bytes;
    uint32_t capture_prefix_bits;
    uint32_t guard_count;
    uint32_t abort_poll_count;
    uint32_t local_slot;
    uint32_t active_slot_mask;
    uint32_t returned_route_word;
    uint8_t capture_dma;
    uint8_t output_dma;
    uint8_t loader_dma;
    uint8_t executor_dma;
    uint8_t control_pio;
    uint8_t data_pio;
    uint8_t control_sm;
    uint8_t capture_sm;
    uint8_t data_sm;
    uint8_t helper_sm;
    uint8_t control_pc;
    uint8_t capture_pc;
    uint8_t data_pc;
    uint8_t helper_pc;
    uint8_t compare_pc;
    uint8_t rtt_sm;
    uint8_t rtt_pc;
} tdma_origin_plan_config_t;

typedef struct {
    tdma_flight_overlay_dma_run_t *runs;
    uint32_t *literals;
    uint32_t run_capacity;
    uint32_t literal_capacity;
    uint32_t run_count;
    uint32_t literal_count;
    uint32_t seed_entry;
    uint32_t boundary_entry;
    uint32_t local_entry[TDMA_ORIGIN_PLAN_BANK_COUNT];
    uint32_t fault_entry;
} tdma_origin_plan_t;

/* Owner-private construction state, reusable only after completion/cancel.
 * Each step emits one fixed graph block (bounded by STEP_RUN_MAX), including
 * either pass. These work bounds are not a clk_sys WCET grant. */
#define TDMA_ORIGIN_BUILD_LABEL_CAPACITY 48u
#define TDMA_ORIGIN_BUILD_STEP_RUN_MAX 24u
typedef enum {
    TDMA_ORIGIN_BUILD_FAILED = 0u,
    TDMA_ORIGIN_BUILD_BUSY = 1u,
    TDMA_ORIGIN_BUILD_DONE = 2u,
} tdma_origin_build_result_t;

typedef struct {
    tdma_origin_plan_config_t config;
    const tdma_origin_plan_config_t *c;
    tdma_origin_plan_t *p;
    uint32_t label[TDMA_ORIGIN_BUILD_LABEL_CAPACITY];
    uint8_t immutable[TDMA_ORIGIN_PLAN_LITERAL_MAX];
    uint32_t tx_pio, rx_pio, cap_rx, cap_tx, data_tx, ctrl_rx, ctrl_tx, help_rx, help_tx;
    uint32_t cap_q, cap_tx_q, data_q, ctrl_q, ctrl_tx_q, help_q, help_tx_q;
    uint32_t cap_ctrl, out_ctrl, sum, crc, crc16;
    uint32_t step;
    bool failed, emitting, active, complete;
} tdma_origin_plan_builder_t;

/* Zero-initialize builder before first begin. Config is frozen by value;
 * plan/storage remain private and unchanged by the caller until cancel/DONE.
 * No entry address is published while BUSY or after failure/cancellation.
 * A step never loops into another block or the second pass. */
bool tdma_origin_plan_begin(tdma_origin_plan_builder_t *builder,
                           const tdma_origin_plan_config_t *config,
                           tdma_origin_plan_t *plan);
tdma_origin_build_result_t tdma_origin_plan_step(tdma_origin_plan_builder_t *builder);
void tdma_origin_plan_cancel(tdma_origin_plan_builder_t *builder);

/* Build into unpublished storage only. A failure leaves all entry addresses
 * zero; partially written storage cannot be installed or published. The
 * Synchronous convenience for host/offline callers; runtime uses begin/step.
 * The resulting graph stays immutable until the complete DMA dependency tree
 * has stopped. Does not touch MMIO, claim resources, or allocate memory. */
bool tdma_origin_plan_build(const tdma_origin_plan_config_t *config,
                            tdma_origin_plan_t *plan);

#endif
