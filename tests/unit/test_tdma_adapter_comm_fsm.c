#include <assert.h>
#include <stdio.h>

#include "tdma_adapter_comm_fsm.h"

static void dispatch(tdma_adapter_comm_fsm_t *fsm,
                     tdma_adapter_comm_event_t event)
{
    assert(tdma_adapter_comm_fsm_dispatch(fsm, event, 0u));
}

static void complete_one_window(tdma_adapter_comm_fsm_t *fsm)
{
    dispatch(fsm, TDMA_ADAPTER_COMM_EVENT_ARM);
    dispatch(fsm, TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_STARTED);
    dispatch(fsm, TDMA_ADAPTER_COMM_EVENT_DATA_RX_STARTED);
    dispatch(fsm, TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_COMPLETED);
    dispatch(fsm, TDMA_ADAPTER_COMM_EVENT_DATA_RX_COMPLETED);
    assert(fsm->state == TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY);
}

int main(void)
{
    tdma_adapter_comm_fsm_t fsm;
    tdma_adapter_comm_fsm_init(&fsm);
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_STOPPED);

    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_ARM);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_STARTED);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_DATA_RX_STARTED);
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_RUNNING);
    assert(fsm.clock_tx_active && fsm.data_rx_active);

    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_COMPLETED);
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_RUNNING);
    assert(!fsm.clock_tx_active && fsm.data_rx_active);
    assert(fsm.clock_tx_complete && !fsm.data_rx_complete);

    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_BOOTSTRAP_TX_STARTED);
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_RUNNING);
    assert(fsm.bootstrap_tx_active);
    assert(fsm.bootstrap_tx_count == 1u);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_BOOTSTRAP_TX_COMPLETED);
    assert(!fsm.bootstrap_tx_active);
    assert(fsm.bootstrap_tx_complete_count == 1u);
    assert(fsm.completed_window_count == 0u);

    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_DATA_RX_COMPLETED);
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY);
    assert(fsm.completed_window_count == 1u);
    assert(fsm.window_sequence == 1u);
    assert(fsm.clock_tx_complete && fsm.data_rx_complete);

    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_BEGIN_NEXT_CYCLE);
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_RUNNING);
    assert(fsm.window_sequence == 2u);
    assert(!fsm.clock_tx_active && !fsm.data_rx_active);
    assert(!fsm.clock_tx_complete && !fsm.data_rx_complete);
    assert(!fsm.bootstrap_tx_active);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_DATA_RX_STARTED);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_STARTED);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_DATA_RX_COMPLETED);
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_RUNNING);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_COMPLETED);
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY);
    assert(fsm.completed_window_count == 2u);
    assert(fsm.window_sequence == 2u);

    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_BEGIN_NEXT_CYCLE);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_DATA_RX_STARTED);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_STARTED);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_COMPLETED);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_DATA_RX_TIMED_OUT);
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY);
    assert(fsm.timed_out_window_count == 1u);
    assert(fsm.completed_window_count == 2u);
    assert(!fsm.data_rx_active && !fsm.data_rx_complete);

    tdma_adapter_comm_fsm_init(&fsm);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_ARM);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_STARTED);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_DATA_RX_STARTED);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_COMPLETED);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_BOOTSTRAP_TX_STARTED);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_DATA_RX_COMPLETED);
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_RUNNING);
    assert(fsm.bootstrap_tx_active);
    assert(fsm.completed_window_count == 0u);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_BOOTSTRAP_TX_COMPLETED);
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY);
    assert(!fsm.bootstrap_tx_active);
    assert(fsm.completed_window_count == 1u);

    assert(!tdma_adapter_comm_fsm_dispatch(
        &fsm, TDMA_ADAPTER_COMM_EVENT_ARM, 0u));
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_FAULT);
    assert(fsm.last_error == TDMA_ADAPTER_COMM_ERROR_INVALID_TRANSITION);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_RESET);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_ARM);
    assert(!tdma_adapter_comm_fsm_dispatch(
        &fsm, TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_COMPLETED, 0u));
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_FAULT);
    assert(fsm.last_error == TDMA_ADAPTER_COMM_ERROR_CLOCK_TX_NOT_STARTED);
    assert(tdma_adapter_comm_fsm_dispatch(
        &fsm, TDMA_ADAPTER_COMM_EVENT_RESET, 0u));
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_STOPPED);

    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_ARM);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_STARTED);
    assert(!tdma_adapter_comm_fsm_dispatch(
        &fsm, TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_STARTED, 0u));
    assert(fsm.last_error == TDMA_ADAPTER_COMM_ERROR_CLOCK_TX_ALREADY_STARTED);
    assert(tdma_adapter_comm_fsm_dispatch(
        &fsm, TDMA_ADAPTER_COMM_EVENT_RESET, 0u));
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_ARM);
    assert(tdma_adapter_comm_fsm_dispatch(
        &fsm, TDMA_ADAPTER_COMM_EVENT_ERROR, 77u));
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_FAULT);
    assert(fsm.last_error == 77u);
    assert(tdma_adapter_comm_fsm_dispatch(
        &fsm, TDMA_ADAPTER_COMM_EVENT_RESET, 0u));
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_STOPPED);

    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_ARM);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_STARTED);
    assert(tdma_adapter_comm_fsm_dispatch(
        &fsm, TDMA_ADAPTER_COMM_EVENT_STOP, 0u));
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_STOPPED);
    assert(fsm.window_sequence == 1u);
    assert(fsm.completed_window_count == 0u);

    tdma_adapter_comm_fsm_init(&fsm);
    complete_one_window(&fsm);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_STOP);
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_STOPPED);
    assert(fsm.completed_window_count == 1u);

    tdma_adapter_comm_fsm_init(&fsm);
    complete_one_window(&fsm);
    dispatch(&fsm, TDMA_ADAPTER_COMM_EVENT_RESET);
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_STOPPED);
    assert(fsm.window_sequence == 0u);
    assert(fsm.completed_window_count == 0u);

    tdma_adapter_comm_fsm_init(&fsm);
    complete_one_window(&fsm);
    assert(tdma_adapter_comm_fsm_dispatch(
        &fsm, TDMA_ADAPTER_COMM_EVENT_ERROR, 91u));
    assert(fsm.state == TDMA_ADAPTER_COMM_STATE_FAULT);
    assert(fsm.last_error == 91u);
    puts("TDMA adapter communication FSM host tests passed");
    return 0;
}
