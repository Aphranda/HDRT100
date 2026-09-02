#include "tdma_adapter_comm_fsm.h"

#include <string.h>

static void tdma_adapter_comm_fsm_transition(
    tdma_adapter_comm_fsm_t *fsm,
    tdma_adapter_comm_state_t state,
    tdma_adapter_comm_error_t error)
{
    fsm->state = (uint32_t)state;
    fsm->last_error = (uint32_t)error;
    fsm->transition_sequence++;
}

static bool tdma_adapter_comm_fsm_fail(
    tdma_adapter_comm_fsm_t *fsm,
    tdma_adapter_comm_error_t error)
{
    fsm->clock_tx_active = false;
    fsm->data_rx_active = false;
    tdma_adapter_comm_fsm_transition(
        fsm, TDMA_ADAPTER_COMM_STATE_FAULT, error);
    return false;
}

static void tdma_adapter_comm_fsm_stop(tdma_adapter_comm_fsm_t *fsm)
{
    fsm->clock_tx_active = false;
    fsm->data_rx_active = false;
    fsm->clock_tx_complete = false;
    fsm->data_rx_complete = false;
    tdma_adapter_comm_fsm_transition(
        fsm, TDMA_ADAPTER_COMM_STATE_STOPPED,
        TDMA_ADAPTER_COMM_ERROR_NONE);
}

static bool tdma_adapter_comm_fsm_error(
    tdma_adapter_comm_fsm_t *fsm,
    uint32_t event_value)
{
    fsm->clock_tx_active = false;
    fsm->data_rx_active = false;
    fsm->state = TDMA_ADAPTER_COMM_STATE_FAULT;
    fsm->last_error = event_value == 0u
        ? TDMA_ADAPTER_COMM_ERROR_RUNTIME : event_value;
    fsm->transition_sequence++;
    return true;
}

static bool tdma_adapter_comm_fsm_complete_if_ready(
    tdma_adapter_comm_fsm_t *fsm)
{
    if (!fsm->clock_tx_complete || !fsm->data_rx_complete) {
        tdma_adapter_comm_fsm_transition(
            fsm, TDMA_ADAPTER_COMM_STATE_RUNNING,
            TDMA_ADAPTER_COMM_ERROR_NONE);
        return true;
    }
    fsm->completed_window_count++;
    tdma_adapter_comm_fsm_transition(
        fsm, TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY,
        TDMA_ADAPTER_COMM_ERROR_NONE);
    return true;
}

void tdma_adapter_comm_fsm_init(tdma_adapter_comm_fsm_t *fsm)
{
    if (fsm == NULL) {
        return;
    }
    memset(fsm, 0, sizeof(*fsm));
    fsm->state = TDMA_ADAPTER_COMM_STATE_STOPPED;
}

bool tdma_adapter_comm_fsm_dispatch(tdma_adapter_comm_fsm_t *fsm,
                                    tdma_adapter_comm_event_t event,
                                    uint32_t event_value)
{
    if (fsm == NULL) {
        return false;
    }

    /* A hardware/adapter error is observable from every lifecycle state and
     * always stops both activities before entering FAULT. */
    if (event == TDMA_ADAPTER_COMM_EVENT_ERROR) {
        return tdma_adapter_comm_fsm_error(fsm, event_value);
    }

    switch ((tdma_adapter_comm_state_t)fsm->state) {
    case TDMA_ADAPTER_COMM_STATE_STOPPED:
        if (event == TDMA_ADAPTER_COMM_EVENT_ARM) {
            fsm->window_sequence++;
            fsm->clock_tx_active = false;
            fsm->data_rx_active = false;
            fsm->clock_tx_complete = false;
            fsm->data_rx_complete = false;
            tdma_adapter_comm_fsm_transition(
                fsm, TDMA_ADAPTER_COMM_STATE_ARMED,
                TDMA_ADAPTER_COMM_ERROR_NONE);
            return true;
        }
        if (event == TDMA_ADAPTER_COMM_EVENT_RESET) {
            tdma_adapter_comm_fsm_init(fsm);
            return true;
        }
        break;

    case TDMA_ADAPTER_COMM_STATE_ARMED:
    case TDMA_ADAPTER_COMM_STATE_RUNNING:
        if (event == TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_STARTED) {
            if (fsm->clock_tx_active || fsm->clock_tx_complete) {
                return tdma_adapter_comm_fsm_fail(
                    fsm, TDMA_ADAPTER_COMM_ERROR_CLOCK_TX_ALREADY_STARTED);
            }
            fsm->clock_tx_active = true;
            tdma_adapter_comm_fsm_transition(
                fsm, TDMA_ADAPTER_COMM_STATE_RUNNING,
                TDMA_ADAPTER_COMM_ERROR_NONE);
            return true;
        }
        if (event == TDMA_ADAPTER_COMM_EVENT_DATA_RX_STARTED) {
            if (fsm->data_rx_active || fsm->data_rx_complete) {
                return tdma_adapter_comm_fsm_fail(
                    fsm, TDMA_ADAPTER_COMM_ERROR_DATA_RX_ALREADY_STARTED);
            }
            fsm->data_rx_active = true;
            tdma_adapter_comm_fsm_transition(
                fsm, TDMA_ADAPTER_COMM_STATE_RUNNING,
                TDMA_ADAPTER_COMM_ERROR_NONE);
            return true;
        }
        if (event == TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_COMPLETED) {
            if (!fsm->clock_tx_active ||
                (!fsm->data_rx_active && !fsm->data_rx_complete)) {
                return tdma_adapter_comm_fsm_fail(
                    fsm, TDMA_ADAPTER_COMM_ERROR_CLOCK_TX_NOT_STARTED);
            }
            fsm->clock_tx_active = false;
            fsm->clock_tx_complete = true;
            return tdma_adapter_comm_fsm_complete_if_ready(fsm);
        }
        if (event == TDMA_ADAPTER_COMM_EVENT_DATA_RX_COMPLETED) {
            if (!fsm->data_rx_active ||
                (!fsm->clock_tx_active && !fsm->clock_tx_complete)) {
                return tdma_adapter_comm_fsm_fail(
                    fsm, TDMA_ADAPTER_COMM_ERROR_DATA_RX_NOT_STARTED);
            }
            fsm->data_rx_active = false;
            fsm->data_rx_complete = true;
            return tdma_adapter_comm_fsm_complete_if_ready(fsm);
        }
        if (event == TDMA_ADAPTER_COMM_EVENT_STOP) {
            tdma_adapter_comm_fsm_stop(fsm);
            return true;
        }
        if (event == TDMA_ADAPTER_COMM_EVENT_RESET) {
            tdma_adapter_comm_fsm_init(fsm);
            return true;
        }
        break;

    case TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY:
        if (event == TDMA_ADAPTER_COMM_EVENT_BEGIN_NEXT_CYCLE) {
            fsm->window_sequence++;
            fsm->clock_tx_active = false;
            fsm->data_rx_active = false;
            fsm->clock_tx_complete = false;
            fsm->data_rx_complete = false;
            tdma_adapter_comm_fsm_transition(
                fsm, TDMA_ADAPTER_COMM_STATE_RUNNING,
                TDMA_ADAPTER_COMM_ERROR_NONE);
            return true;
        }
        if (event == TDMA_ADAPTER_COMM_EVENT_STOP ||
            event == TDMA_ADAPTER_COMM_EVENT_RESET) {
            if (event == TDMA_ADAPTER_COMM_EVENT_STOP) {
                tdma_adapter_comm_fsm_stop(fsm);
            } else {
                tdma_adapter_comm_fsm_init(fsm);
            }
            return true;
        }
        break;

    case TDMA_ADAPTER_COMM_STATE_FAULT:
        if (event == TDMA_ADAPTER_COMM_EVENT_RESET) {
            tdma_adapter_comm_fsm_init(fsm);
            return true;
        }
        break;

    default:
        break;
    }

    return tdma_adapter_comm_fsm_fail(
        fsm, TDMA_ADAPTER_COMM_ERROR_INVALID_TRANSITION);
}
