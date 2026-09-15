#ifndef TDMA_ORIGIN_EXCHANGE_H
#define TDMA_ORIGIN_EXCHANGE_H

#include "tdma_origin_plan.h"

/* Core1 TDMA owner only. Graph publication and pool reads use DMA-written
 * versions; polling does not rearm a channel or participate in cadence. */
typedef struct {
    tdma_origin_plan_state_t *state;
    const uint8_t *capture[TDMA_ORIGIN_PLAN_BANK_COUNT];
    uint32_t *shadow[TDMA_ORIGIN_PLAN_BANK_COUNT];
    uint32_t entry[TDMA_ORIGIN_PLAN_BANK_COUNT];
    uint32_t generation;
    uint32_t selected_bank;
    uint32_t rx_version[TDMA_ORIGIN_PLAN_BANK_COUNT];
    uint32_t observation_version;
    uint32_t packet_size;
    bool pending;
} tdma_origin_exchange_t;

/* Bind only while the complete graph is stopped and its seed state is
 * initialized. The physical owner proves resource/workspace ownership.
 * This routine neither grants resources nor modifies DMA-owned storage. */
bool tdma_origin_exchange_bind(tdma_origin_exchange_t *exchange,
                              tdma_origin_plan_state_t *state,
                              const uint8_t *capture_a, const uint8_t *capture_b,
                              uint32_t *shadow_a, uint32_t *shadow_b,
                              uint32_t entry_a, uint32_t entry_b, uint32_t packet_size);

/* False leaves all publication and shadow bytes unchanged. One pending
 * replacement at a time. The caller supplies an owner-validated mailbox. */
bool tdma_origin_exchange_ready(tdma_origin_exchange_t *exchange);
bool tdma_origin_exchange_publish(tdma_origin_exchange_t *exchange,
                                 const uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE],
                                 uint32_t *generation);

/* One bounded attempt. A false result leaves the destination unspecified;
 * consumers must discard it. Initial seed images are never received data. */
bool tdma_origin_exchange_copy_rx(tdma_origin_exchange_t *exchange,
                                 uint8_t *packet, size_t capacity);
bool tdma_origin_exchange_copy_rx_observation(tdma_origin_exchange_t *exchange,
                                            uint8_t *packet, size_t capacity,
                                            tdma_origin_observation_t *observation);
bool tdma_origin_exchange_observe(tdma_origin_exchange_t *exchange,
                                 tdma_origin_observation_t *observation);

/* Core1 owner: copy one latest COMPLETE cyclic raw record, without consuming
 * exchange observation/RX versions or touching the running graph. The owner
 * must keep the exchange/record pool bound for the entire call and enforce a
 * copy-time limit excluding a full publication-version ABA. This helper has
 * no clock and cannot establish that lifetime bound itself.
 * A producer advance of COUNT-1 records already permits reuse of the selected
 * slot before the next publication; reject that boundary and version wrap.
 * False leaves out unchanged. Success retains raw diagnostic provenance only:
 * it does not grant a timestamp, physical event identity or DPLL eligibility. */
bool tdma_origin_exchange_copy_live_record(
    const tdma_origin_exchange_t *exchange,
    const tdma_origin_record_t records[TDMA_ORIGIN_RECORD_COUNT],
    uint32_t expected_epoch,
    tdma_origin_record_frozen_t *out);

#endif
