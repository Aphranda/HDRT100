#ifndef TDMA_ADAPTER_COMM_FSM_H
#define TDMA_ADAPTER_COMM_FSM_H

#include <stdbool.h>
#include <stdint.h>

/* Adapter resident-cycle lifecycle. CLOCK_TX and DATA_RX are independent
 * activities within one physical window. Completing both reaches a cycle
 * boundary; it does not disarm the resident process image. */
typedef enum {
    TDMA_ADAPTER_COMM_STATE_STOPPED = 0u,
    TDMA_ADAPTER_COMM_STATE_ARMED = 1u,
    TDMA_ADAPTER_COMM_STATE_RUNNING = 2u,
    TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY = 3u,
    TDMA_ADAPTER_COMM_STATE_FAULT = 4u,
} tdma_adapter_comm_state_t;

typedef enum {
    TDMA_ADAPTER_COMM_EVENT_ARM = 0u,
    TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_STARTED = 1u,
    TDMA_ADAPTER_COMM_EVENT_DATA_RX_STARTED = 2u,
    TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_COMPLETED = 3u,
    TDMA_ADAPTER_COMM_EVENT_DATA_RX_COMPLETED = 4u,
    TDMA_ADAPTER_COMM_EVENT_STOP = 5u,
    TDMA_ADAPTER_COMM_EVENT_ERROR = 6u,
    TDMA_ADAPTER_COMM_EVENT_RESET = 7u,
    TDMA_ADAPTER_COMM_EVENT_BEGIN_NEXT_CYCLE = 8u,
    TDMA_ADAPTER_COMM_EVENT_BOOTSTRAP_TX_STARTED = 9u,
    TDMA_ADAPTER_COMM_EVENT_BOOTSTRAP_TX_COMPLETED = 10u,
    TDMA_ADAPTER_COMM_EVENT_DATA_RX_TIMED_OUT = 11u,
} tdma_adapter_comm_event_t;

typedef enum {
    TDMA_ADAPTER_COMM_ERROR_NONE = 0u,
    TDMA_ADAPTER_COMM_ERROR_INVALID_TRANSITION = 1u,
    TDMA_ADAPTER_COMM_ERROR_CLOCK_TX_ALREADY_STARTED = 2u,
    TDMA_ADAPTER_COMM_ERROR_DATA_RX_ALREADY_STARTED = 3u,
    TDMA_ADAPTER_COMM_ERROR_CLOCK_TX_NOT_STARTED = 4u,
    TDMA_ADAPTER_COMM_ERROR_DATA_RX_NOT_STARTED = 5u,
    TDMA_ADAPTER_COMM_ERROR_RUNTIME = 6u,
    TDMA_ADAPTER_COMM_ERROR_BOOTSTRAP_TX_ALREADY_STARTED = 7u,
    TDMA_ADAPTER_COMM_ERROR_BOOTSTRAP_TX_NOT_STARTED = 8u,
} tdma_adapter_comm_error_t;

typedef struct {
    uint32_t state;
    uint32_t window_sequence;
    uint32_t transition_sequence;
    bool clock_tx_active;
    bool data_rx_active;
    bool clock_tx_complete;
    bool data_rx_complete;
    bool bootstrap_tx_active;
    uint32_t bootstrap_tx_count;
    uint32_t bootstrap_tx_complete_count;
    uint32_t completed_window_count;
    uint32_t timed_out_window_count;
    uint32_t last_error;
} tdma_adapter_comm_fsm_t;

void tdma_adapter_comm_fsm_init(tdma_adapter_comm_fsm_t *fsm);
bool tdma_adapter_comm_fsm_dispatch(tdma_adapter_comm_fsm_t *fsm,
                                    tdma_adapter_comm_event_t event,
                                    uint32_t event_value);

#endif
