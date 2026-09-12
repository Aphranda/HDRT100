#ifndef TDMA_OVERLAY_PREPARE_H
#define TDMA_OVERLAY_PREPARE_H

#include "tdma_flight_overlay.h"

/* One Core1 producer, one Core0 worker. The job owns copies of its inputs;
 * only its output pointer leases the existing inactive physical plan pool.
 * Core1 must cancel/drain before STOP retires that pool or reuses its union. */
typedef enum {
    TDMA_OVERLAY_PREPARE_IDLE = 0u,
    TDMA_OVERLAY_PREPARE_REQUESTED,
    TDMA_OVERLAY_PREPARE_BUILDING,
    TDMA_OVERLAY_PREPARE_READY,
    TDMA_OVERLAY_PREPARE_FAILED,
    TDMA_OVERLAY_PREPARE_CANCELLED,
} tdma_overlay_prepare_state_t;

typedef struct {
    volatile uint32_t state;
    uint32_t epoch;
    uint32_t request_epoch;
    uint32_t buffer_index;
    uint32_t ingress_hop;
    uint32_t egress_hop;
    uint32_t target_sequence;
    tdma_flight_tx_layout_t layout;
    tdma_flight_overlay_config_t config;
    tdma_flight_overlay_plan_t *plan;
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    uint8_t tx_data[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    tdma_flight_tx_view_t tx;
    tdma_flight_engine_apply_t applied;
} tdma_overlay_prepare_t;

uint32_t tdma_overlay_prepare_state(const tdma_overlay_prepare_t *job);
/* Input fields are filled by Core1 only while IDLE. Publication freezes them
 * until Core1 consumes READY/FAILED or cancellation returns true. */
bool tdma_overlay_prepare_request(tdma_overlay_prepare_t *job);
bool tdma_overlay_prepare_core0_claim(tdma_overlay_prepare_t *job);
void tdma_overlay_prepare_core0_build_claimed(tdma_overlay_prepare_t *job);
void tdma_overlay_prepare_core0_service(tdma_overlay_prepare_t *job);
/* No waiting. False means the cancelled worker still owns the output pool. */
bool tdma_overlay_prepare_cancel(tdma_overlay_prepare_t *job);
void tdma_overlay_prepare_release(tdma_overlay_prepare_t *job);

#endif
