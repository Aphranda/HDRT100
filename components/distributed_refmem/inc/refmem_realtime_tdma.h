#ifndef REFMEM_REALTIME_TDMA_H
#define REFMEM_REALTIME_TDMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "refmem_sync_frame.h"
#include "refmem_spi_physical_adapter.h"

#define REFMEM_REALTIME_TDMA_FRAME_MAX \
    (REFMEM_SYNC_FRAME_HEADER_SIZE + REFMEM_SYNC_FRAME_PAYLOAD_MAX)

typedef enum {
    REFMEM_REALTIME_TDMA_STATE_UNINIT = 0u,
    REFMEM_REALTIME_TDMA_STATE_IDLE = 1u,
    REFMEM_REALTIME_TDMA_STATE_ARMED = 2u,
    REFMEM_REALTIME_TDMA_STATE_PENDING = 3u,
    REFMEM_REALTIME_TDMA_STATE_DONE = 4u,
    REFMEM_REALTIME_TDMA_STATE_ERROR = 5u,
} refmem_realtime_tdma_state_t;

typedef enum {
    REFMEM_REALTIME_TDMA_INTENT_NONE = 0u,
    REFMEM_REALTIME_TDMA_INTENT_TX_FRAME = 1u,
    REFMEM_REALTIME_TDMA_INTENT_RX_WINDOW = 2u,
} refmem_realtime_tdma_intent_t;

typedef enum {
    REFMEM_REALTIME_TDMA_RESULT_NONE = 0u,
    REFMEM_REALTIME_TDMA_RESULT_ACCEPTED = 1u,
    REFMEM_REALTIME_TDMA_RESULT_FRAME_READY = 2u,
    REFMEM_REALTIME_TDMA_RESULT_TIMEOUT = 3u,
    REFMEM_REALTIME_TDMA_RESULT_OVERRUN = 4u,
    REFMEM_REALTIME_TDMA_RESULT_BAD_ARGUMENT = 5u,
    REFMEM_REALTIME_TDMA_RESULT_BUSY = 6u,
    REFMEM_REALTIME_TDMA_RESULT_WAITING_FOR_WINDOW = 7u,
    REFMEM_REALTIME_TDMA_RESULT_WINDOW_MISSED = 8u,
} refmem_realtime_tdma_result_t;

typedef enum {
    REFMEM_REALTIME_TDMA_EXEC_NONE = 0u,
    REFMEM_REALTIME_TDMA_EXEC_TX_OK = 1u,
    REFMEM_REALTIME_TDMA_EXEC_RX_OK = 2u,
    REFMEM_REALTIME_TDMA_EXEC_TIMEOUT = 3u,
    REFMEM_REALTIME_TDMA_EXEC_ERROR = 4u,
    REFMEM_REALTIME_TDMA_EXEC_PENDING = 5u,
} refmem_realtime_tdma_exec_result_t;

typedef enum {
    REFMEM_REALTIME_TDMA_TIMESTAMP_SOURCE_NONE = 0u,
    REFMEM_REALTIME_TDMA_TIMESTAMP_SOURCE_SOFTWARE_US = 1u,
    REFMEM_REALTIME_TDMA_TIMESTAMP_SOURCE_HARDWARE_TICK = 2u,
} refmem_realtime_tdma_timestamp_source_t;

#define REFMEM_REALTIME_TDMA_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY 0x00000001u
#define REFMEM_REALTIME_TDMA_TIMESTAMP_FLAG_DPLL_ELIGIBLE   0x00000002u

typedef struct {
    refmem_realtime_tdma_exec_result_t result;
    uint32_t error;
    size_t frame_size;
} refmem_realtime_tdma_exec_status_t;

typedef struct {
    bool (*transmit)(void *context,
                     const uint8_t *frame,
                     size_t frame_size,
                     refmem_spi_physical_role_t role,
                     uint32_t baud_hz,
                     const refmem_spi_physical_pin_config_t *pins,
                     uint32_t deadline_1e3ns,
                     refmem_realtime_tdma_exec_status_t *status);
    bool (*receive)(void *context,
                    uint8_t *frame,
                    size_t frame_capacity,
                    refmem_spi_physical_role_t role,
                    uint32_t baud_hz,
                    const refmem_spi_physical_pin_config_t *pins,
                    uint32_t deadline_1e3ns,
                    refmem_realtime_tdma_exec_status_t *status);
} refmem_realtime_tdma_ops_t;

typedef struct {
    uint32_t state;
    uint32_t owner_core;
    uint32_t armed;
    uint32_t service_count;
    uint32_t intent_seq;
    uint32_t completed_seq;
    uint32_t dropped_seq;
    uint32_t window_epoch;
    uint32_t window_index;
    uint32_t intent_type;
    uint32_t role;
    uint32_t baud_hz;
    uint32_t rx_pin;
    uint32_t csn_pin;
    uint32_t sck_pin;
    uint32_t tx_pin;
    uint32_t deadline_1e3ns;
    uint32_t frame_size;
    uint32_t ready_count;
    uint32_t timeout_count;
    uint32_t overrun_count;
    uint32_t reject_count;
    uint32_t last_result;
    uint32_t last_error;
    uint32_t timestamp_source;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t vdc_window_plan_valid;
    uint32_t vdc_window_class;
    uint32_t vdc_schedule_crc32;
    uint32_t vdc_window_miss_count;
    uint32_t vdc_window_wait_ns;
    uint32_t vdc_window_late_ns;
    uint32_t vdc_window_start_ns_lo;
    uint32_t vdc_window_start_ns_hi;
    uint32_t vdc_window_end_ns_lo;
    uint32_t vdc_window_end_ns_hi;
    uint32_t vdc_guard_start_ns_lo;
    uint32_t vdc_guard_start_ns_hi;
    uint32_t vdc_guard_end_ns_lo;
    uint32_t vdc_guard_end_ns_hi;
    uint32_t submit_time_ns_lo;
    uint32_t submit_time_ns_hi;
    uint32_t core1_arm_time_ns_lo;
    uint32_t core1_arm_time_ns_hi;
    uint32_t core1_start_time_ns_lo;
    uint32_t core1_start_time_ns_hi;
    uint32_t core1_done_time_ns_lo;
    uint32_t core1_done_time_ns_hi;
    uint32_t core1_elapsed_ns;
} refmem_realtime_tdma_snapshot_t;

typedef struct {
    uint32_t window_epoch;
    uint32_t window_index;
    uint32_t deadline_1e3ns;
    refmem_spi_physical_role_t role;
    uint32_t baud_hz;
    refmem_spi_physical_pin_config_t pins;
    uint32_t vdc_window_plan_valid;
    uint32_t vdc_window_class;
    uint32_t vdc_schedule_crc32;
    uint64_t vdc_window_start_ns;
    uint64_t vdc_window_end_ns;
    uint64_t vdc_guard_start_ns;
    uint64_t vdc_guard_end_ns;
    const uint8_t *frame;
    size_t frame_size;
} refmem_realtime_tdma_intent_config_t;

typedef struct {
    volatile uint32_t intent_guard;
    volatile uint32_t result_guard;

    /* Core0 writer: intent mailbox. */
    volatile uint32_t intent_seq;
    volatile uint32_t abort_seq;
    volatile uint32_t window_epoch;
    volatile uint32_t window_index;
    volatile uint32_t intent_type;
    volatile uint32_t role;
    volatile uint32_t baud_hz;
    volatile uint32_t rx_pin;
    volatile uint32_t csn_pin;
    volatile uint32_t sck_pin;
    volatile uint32_t tx_pin;
    volatile uint32_t deadline_1e3ns;
    volatile uint32_t vdc_window_plan_valid;
    volatile uint32_t vdc_window_class;
    volatile uint32_t vdc_schedule_crc32;
    volatile uint64_t vdc_window_start_ns;
    volatile uint64_t vdc_window_end_ns;
    volatile uint64_t vdc_guard_start_ns;
    volatile uint64_t vdc_guard_end_ns;
    volatile uint32_t frame_size;
    volatile uint32_t reject_count;
    volatile uint64_t submit_time_ns;
    uint8_t frame[REFMEM_REALTIME_TDMA_FRAME_MAX];

    /* Core1 writer: realtime execution snapshot. */
    volatile uint32_t state;
    volatile uint32_t owner_core;
    volatile uint32_t armed;
    volatile uint32_t service_count;
    volatile uint32_t completed_seq;
    volatile uint32_t dropped_seq;
    volatile uint32_t ready_count;
    volatile uint32_t timeout_count;
    volatile uint32_t overrun_count;
    volatile uint32_t last_result;
    volatile uint32_t last_error;
    volatile uint32_t result_frame_size;
    volatile uint32_t timing_intent_seq;
    volatile uint32_t vdc_window_miss_count;
    volatile uint32_t vdc_window_wait_ns;
    volatile uint32_t vdc_window_late_ns;
    volatile uint64_t core1_arm_time_ns;
    volatile uint64_t core1_start_time_ns;
    volatile uint64_t core1_done_time_ns;
    volatile uint32_t core1_elapsed_ns;
    uint8_t result_frame[REFMEM_REALTIME_TDMA_FRAME_MAX];

    const refmem_realtime_tdma_ops_t *ops;
    void *ops_context;
} refmem_realtime_tdma_service_t;

bool refmem_realtime_tdma_init(refmem_realtime_tdma_service_t *service);
bool refmem_realtime_tdma_bind_ops(refmem_realtime_tdma_service_t *service,
                                   const refmem_realtime_tdma_ops_t *ops,
                                   void *ops_context);
bool refmem_realtime_tdma_submit_tx(refmem_realtime_tdma_service_t *service,
                                    const refmem_realtime_tdma_intent_config_t *config);
bool refmem_realtime_tdma_submit_rx(refmem_realtime_tdma_service_t *service,
                                    const refmem_realtime_tdma_intent_config_t *config);
void refmem_realtime_tdma_abort(refmem_realtime_tdma_service_t *service);
void refmem_realtime_tdma_core1_service(refmem_realtime_tdma_service_t *service);
bool refmem_realtime_tdma_get_snapshot(const refmem_realtime_tdma_service_t *service,
                                       refmem_realtime_tdma_snapshot_t *snapshot);
bool refmem_realtime_tdma_get_result_frame(const refmem_realtime_tdma_service_t *service,
                                           uint8_t *frame,
                                           size_t frame_capacity,
                                           size_t *frame_size);

#endif
