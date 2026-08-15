#ifndef REFMEM_REALTIME_TDMA_H
#define REFMEM_REALTIME_TDMA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "refmem_spi_physical_adapter.h"

#define REFMEM_REALTIME_TDMA_FRAME_MAX 320u

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
} refmem_realtime_tdma_result_t;

typedef enum {
    REFMEM_REALTIME_TDMA_EXEC_NONE = 0u,
    REFMEM_REALTIME_TDMA_EXEC_TX_OK = 1u,
    REFMEM_REALTIME_TDMA_EXEC_RX_OK = 2u,
    REFMEM_REALTIME_TDMA_EXEC_TIMEOUT = 3u,
    REFMEM_REALTIME_TDMA_EXEC_ERROR = 4u,
} refmem_realtime_tdma_exec_result_t;

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
                     uint32_t deadline_us,
                     refmem_realtime_tdma_exec_status_t *status);
    bool (*receive)(void *context,
                    uint8_t *frame,
                    size_t frame_capacity,
                    refmem_spi_physical_role_t role,
                    uint32_t baud_hz,
                    uint32_t deadline_us,
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
    uint32_t deadline_us;
    uint32_t frame_size;
    uint32_t ready_count;
    uint32_t timeout_count;
    uint32_t overrun_count;
    uint32_t reject_count;
    uint32_t last_result;
    uint32_t last_error;
} refmem_realtime_tdma_snapshot_t;

typedef struct {
    uint32_t window_epoch;
    uint32_t window_index;
    uint32_t deadline_us;
    refmem_spi_physical_role_t role;
    uint32_t baud_hz;
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
    volatile uint32_t deadline_us;
    volatile uint32_t frame_size;
    volatile uint32_t reject_count;
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
