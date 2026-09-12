#include "tdma_origin_exchange.h"

#include <string.h>

static uint32_t load(const uint32_t *address)
{
    return __atomic_load_n(address, __ATOMIC_ACQUIRE);
}

bool tdma_origin_exchange_bind(tdma_origin_exchange_t *e,
                              tdma_origin_plan_state_t *state,
                              const uint8_t *capture_a, const uint8_t *capture_b,
                              uint32_t *shadow_a, uint32_t *shadow_b,
                              uint32_t entry_a, uint32_t entry_b)
{
    if (e == NULL) return false;
    memset(e, 0, sizeof(*e));
    if (state == NULL || capture_a == NULL || capture_b == NULL ||
        shadow_a == NULL || shadow_b == NULL || capture_a == capture_b ||
        shadow_a == shadow_b || entry_a == 0u || entry_b == 0u || entry_a == entry_b ||
        (entry_a | entry_b) % 16u != 0u ||
        ((uintptr_t)state | (uintptr_t)shadow_a | (uintptr_t)shadow_b) %
            _Alignof(uint32_t) != 0u ||
        state->fault != 0u || state->good_bank != 0u || state->capture_bank != 1u ||
        state->bank_version[0] != 2u || state->bank_version[1] != 2u ||
        state->observation_version != 0u || state->local_next_address != entry_a ||
        state->local_selected_generation == 0u ||
        state->local_selected_generation != shadow_a[TDMA_FLIGHT_SHORT_SLOT_SIZE / sizeof(uint32_t)]) {
        return false;
    }
    e->state = state;
    e->capture[0] = capture_a;
    e->capture[1] = capture_b;
    e->shadow[0] = shadow_a;
    e->shadow[1] = shadow_b;
    e->entry[0] = entry_a;
    e->entry[1] = entry_b;
    e->generation = state->local_selected_generation;
    e->rx_version[0] = e->rx_version[1] = 2u;
    return true;
}

bool tdma_origin_exchange_ready(tdma_origin_exchange_t *e)
{
    if (e == NULL || e->state == NULL || load(&e->state->fault) != 0u) return false;
    if (e->pending && load(&e->state->local_selected_generation) == e->generation) {
        /* Hardware writes selection after copying the complete local shadow.
         * Only this replacement selection retires the old bank. */
        e->selected_bank ^= 1u;
        e->pending = false;
    }
    return !e->pending;
}

bool tdma_origin_exchange_publish(tdma_origin_exchange_t *e,
                                 const uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE],
                                 uint32_t *generation)
{
    if (generation != NULL) *generation = 0u;
    if (mailbox == NULL || !tdma_origin_exchange_ready(e)) return false;
    const uint32_t bank = e->selected_bank ^ 1u;
    uint32_t next = e->generation + 1u;
    if (next == 0u) next = 1u;
    memcpy(e->shadow[bank], mailbox, TDMA_FLIGHT_SHORT_SLOT_SIZE);
    e->shadow[bank][TDMA_FLIGHT_SHORT_SLOT_SIZE / sizeof(uint32_t)] = next;
    /* On RP2350 the release store emits a barrier before the aligned pointer
     * word. The executor may already have selected the previous entry; in
     * that case it keeps the old complete mailbox until a later boundary. */
    __atomic_store_n(&e->state->local_next_address, e->entry[bank], __ATOMIC_RELEASE);
    e->generation = next;
    e->pending = true;
    if (generation != NULL) *generation = next;
    return true;
}

bool tdma_origin_exchange_copy_rx(tdma_origin_exchange_t *e,
                                 uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX])
{
    return tdma_origin_exchange_copy_rx_observation(e, packet, NULL);
}

bool tdma_origin_exchange_copy_rx_observation(tdma_origin_exchange_t *e,
                                            uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX],
                                            tdma_origin_observation_t *observation)
{
    if (e == NULL || e->state == NULL || packet == NULL || load(&e->state->fault) != 0u) return false;
    const uint32_t bank = load(&e->state->good_bank);
    if (bank >= TDMA_ORIGIN_PLAN_BANK_COUNT) return false;
    const uint32_t version = load(&e->state->bank_version[bank]);
    if ((version & 1u) != 0u || version == e->rx_version[bank]) return false;
    memcpy(packet, e->capture[bank], TDMA_TRANSPORT_SHORT_PACKET_MAX);
    if (observation != NULL) {
        memcpy(observation, &e->state->bank_observation[bank], sizeof(*observation));
    }
    /* Prevent payload reads from moving after the final version check. DMA
     * begins reuse by making the bank odd, before triggering capture. */
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (load(&e->state->bank_version[bank]) != version || load(&e->state->fault) != 0u) return false;
    e->rx_version[bank] = version;
    return true;
}

bool tdma_origin_exchange_observe(tdma_origin_exchange_t *e,
                                 tdma_origin_observation_t *observation)
{
    if (e == NULL || e->state == NULL || observation == NULL) return false;
    const tdma_origin_plan_state_t *s = e->state;
    const uint32_t version = load(&s->observation_version);
    if ((version & 1u) != 0u || version == e->observation_version) return false;
    observation->sequence = load(&s->observation_sequence);
    observation->identity = load(&s->observation_identity);
    observation->local_generation = load(&s->observation_local_generation);
    observation->output_remaining = load(&s->output_remaining_snapshot);
    observation->rtt_remaining = load(&s->rtt_remaining);
    observation->rtt_present = load(&s->rtt_present);
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (load(&s->observation_version) != version) return false;
    e->observation_version = version;
    return true;
}
