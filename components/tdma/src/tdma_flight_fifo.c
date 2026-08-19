#include "tdma_flight_fifo.h"

#include <string.h>

#define TDMA_FLIGHT_NO_ACTIVE_SLOT UINT32_MAX

static uint32_t tdma_flight_load_u32(const volatile uint32_t *value)
{
    return __atomic_load_n(value, __ATOMIC_ACQUIRE);
}

static void tdma_flight_store_u32(volatile uint32_t *value, uint32_t next)
{
    __atomic_store_n(value, next, __ATOMIC_RELEASE);
}

static bool tdma_flight_ring_full(uint32_t head,
                                  uint32_t tail,
                                  uint32_t capacity)
{
    return head - tail >= capacity;
}

static bool tdma_flight_ring_empty(uint32_t head, uint32_t tail)
{
    return head == tail;
}

static void tdma_flight_counter_inc(volatile uint32_t *value)
{
    (void)__atomic_add_fetch(value, 1u, __ATOMIC_RELEASE);
}

static bool tdma_flight_valid_payload(const uint8_t *data, size_t data_size)
{
    return data_size <= TDMA_FLIGHT_PAYLOAD_CAPACITY &&
           (data_size == 0u || data != NULL);
}

bool tdma_flight_fifo_init(tdma_flight_fifo_t *fifo)
{
    if (fifo == NULL) {
        return false;
    }
    memset(fifo, 0, sizeof(*fifo));
    fifo->tx_active_slot = TDMA_FLIGHT_NO_ACTIVE_SLOT;
    return true;
}

static int32_t tdma_flight_find_tx_inactive_slot(tdma_flight_fifo_t *fifo)
{
    for (uint32_t i = 0u; i < TDMA_FLIGHT_TX_IMAGE_SLOT_COUNT; i++) {
        if (tdma_flight_load_u32(&fifo->tx_slots[i].owner) ==
            TDMA_FLIGHT_TX_OWNER_CORE0_INACTIVE) {
            return (int32_t)i;
        }
    }
    return -1;
}

bool tdma_flight_fifo_core0_publish_tx(tdma_flight_fifo_t *fifo,
                                       const uint8_t *data,
                                       size_t data_size,
                                       uint32_t generation,
                                       uint32_t sequence,
                                       uint32_t segment_mask)
{
    if (fifo == NULL || !tdma_flight_valid_payload(data, data_size) ||
        generation == 0u) {
        if (fifo != NULL) {
            tdma_flight_counter_inc(&fifo->tx_publish_reject_count);
        }
        return false;
    }

    const uint32_t head = tdma_flight_load_u32(&fifo->tx_head);
    const uint32_t tail = tdma_flight_load_u32(&fifo->tx_tail);
    if (tdma_flight_ring_full(head,
                              tail,
                              TDMA_FLIGHT_TX_IMAGE_SLOT_COUNT)) {
        tdma_flight_counter_inc(&fifo->tx_publish_reject_count);
        return false;
    }

    const int32_t slot_index = tdma_flight_find_tx_inactive_slot(fifo);
    if (slot_index < 0) {
        tdma_flight_counter_inc(&fifo->tx_publish_reject_count);
        return false;
    }

    tdma_flight_tx_slot_t *slot = &fifo->tx_slots[(uint32_t)slot_index];
    tdma_flight_store_u32(&slot->owner, TDMA_FLIGHT_TX_OWNER_CORE0_FILL);
    if (data_size != 0u) {
        memcpy(slot->data, data, data_size);
    }
    slot->data_size = (uint32_t)data_size;
    slot->generation = generation;
    slot->sequence = sequence;
    slot->segment_mask = segment_mask;
    tdma_flight_store_u32(&slot->owner, TDMA_FLIGHT_TX_OWNER_CORE0_READY);

    const uint32_t ring_index = head % TDMA_FLIGHT_TX_IMAGE_SLOT_COUNT;
    fifo->tx_ring[ring_index].slot_index = (uint32_t)slot_index;
    fifo->tx_ring[ring_index].generation = generation;
    tdma_flight_store_u32(&fifo->tx_head, head + 1u);
    tdma_flight_counter_inc(&fifo->tx_publish_count);
    return true;
}

void tdma_flight_fifo_core1_release_tx(tdma_flight_fifo_t *fifo)
{
    if (fifo == NULL) {
        return;
    }
    const uint32_t active = tdma_flight_load_u32(&fifo->tx_active_slot);
    if (active >= TDMA_FLIGHT_TX_IMAGE_SLOT_COUNT) {
        return;
    }
    tdma_flight_store_u32(&fifo->tx_slots[active].owner,
                          TDMA_FLIGHT_TX_OWNER_CORE0_INACTIVE);
    tdma_flight_store_u32(&fifo->tx_active_slot, TDMA_FLIGHT_NO_ACTIVE_SLOT);
    tdma_flight_store_u32(&fifo->tx_active_generation, 0u);
    tdma_flight_counter_inc(&fifo->tx_release_count);
}

bool tdma_flight_fifo_core1_acquire_tx(tdma_flight_fifo_t *fifo,
                                       tdma_flight_tx_view_t *view)
{
    if (view != NULL) {
        memset(view, 0, sizeof(*view));
        view->slot_index = TDMA_FLIGHT_NO_ACTIVE_SLOT;
    }
    if (fifo == NULL || view == NULL) {
        return false;
    }

    const uint32_t head = tdma_flight_load_u32(&fifo->tx_head);
    const uint32_t tail = tdma_flight_load_u32(&fifo->tx_tail);
    if (!tdma_flight_ring_empty(head, tail)) {
        const uint32_t ring_index = tail % TDMA_FLIGHT_TX_IMAGE_SLOT_COUNT;
        const uint32_t slot_index =
            tdma_flight_load_u32(&fifo->tx_ring[ring_index].slot_index);
        if (slot_index >= TDMA_FLIGHT_TX_IMAGE_SLOT_COUNT ||
            tdma_flight_load_u32(&fifo->tx_slots[slot_index].owner) !=
                TDMA_FLIGHT_TX_OWNER_CORE0_READY) {
            tdma_flight_counter_inc(&fifo->tx_publish_reject_count);
            return false;
        }
        tdma_flight_fifo_core1_release_tx(fifo);
        tdma_flight_store_u32(&fifo->tx_slots[slot_index].owner,
                              TDMA_FLIGHT_TX_OWNER_CORE1_ACTIVE);
        tdma_flight_store_u32(&fifo->tx_tail, tail + 1u);
        tdma_flight_store_u32(&fifo->tx_active_slot, slot_index);
        tdma_flight_store_u32(&fifo->tx_active_generation,
                              fifo->tx_slots[slot_index].generation);
        tdma_flight_counter_inc(&fifo->tx_acquire_count);
    } else {
        const uint32_t active = tdma_flight_load_u32(&fifo->tx_active_slot);
        if (active >= TDMA_FLIGHT_TX_IMAGE_SLOT_COUNT) {
            return false;
        }
        tdma_flight_counter_inc(&fifo->tx_image_stale_count);
        tdma_flight_counter_inc(&fifo->tx_reuse_count);
        view->reused_previous = true;
    }

    const uint32_t active = tdma_flight_load_u32(&fifo->tx_active_slot);
    if (active >= TDMA_FLIGHT_TX_IMAGE_SLOT_COUNT) {
        return false;
    }
    const tdma_flight_tx_slot_t *slot = &fifo->tx_slots[active];
    view->generation = tdma_flight_load_u32(&slot->generation);
    view->sequence = tdma_flight_load_u32(&slot->sequence);
    view->segment_mask = tdma_flight_load_u32(&slot->segment_mask);
    view->data_size = tdma_flight_load_u32(&slot->data_size);
    view->data = slot->data;
    view->slot_index = active;
    return true;
}

static int32_t tdma_flight_find_rx_free_slot(tdma_flight_fifo_t *fifo)
{
    for (uint32_t i = 0u; i < TDMA_FLIGHT_RX_FRAME_SLOT_COUNT; i++) {
        if (tdma_flight_load_u32(&fifo->rx_slots[i].owner) ==
            TDMA_FLIGHT_RX_OWNER_FREE) {
            return (int32_t)i;
        }
    }
    return -1;
}

bool tdma_flight_fifo_core1_publish_rx(tdma_flight_fifo_t *fifo,
                                       const uint8_t *data,
                                       size_t data_size,
                                       uint32_t generation,
                                       uint32_t sequence,
                                       uint32_t segment_mask,
                                       uint64_t timestamp_ns,
                                       uint32_t quality_flags)
{
    if (fifo == NULL || !tdma_flight_valid_payload(data, data_size) ||
        generation == 0u) {
        if (fifo != NULL) {
            tdma_flight_counter_inc(&fifo->rx_mirror_drop_count);
            tdma_flight_counter_inc(&fifo->rx_publish_drop_count);
        }
        return false;
    }

    const uint32_t head = tdma_flight_load_u32(&fifo->rx_head);
    const uint32_t tail = tdma_flight_load_u32(&fifo->rx_tail);
    if (tdma_flight_ring_full(head,
                              tail,
                              TDMA_FLIGHT_RX_FRAME_SLOT_COUNT)) {
        tdma_flight_counter_inc(&fifo->rx_mirror_drop_count);
        tdma_flight_counter_inc(&fifo->rx_publish_drop_count);
        return false;
    }

    const int32_t slot_index = tdma_flight_find_rx_free_slot(fifo);
    if (slot_index < 0) {
        tdma_flight_counter_inc(&fifo->rx_mirror_drop_count);
        tdma_flight_counter_inc(&fifo->rx_publish_drop_count);
        return false;
    }

    tdma_flight_rx_slot_t *slot = &fifo->rx_slots[(uint32_t)slot_index];
    tdma_flight_store_u32(&slot->owner, TDMA_FLIGHT_RX_OWNER_CORE1_FILL);
    if (data_size != 0u) {
        memcpy(slot->data, data, data_size);
    }
    slot->data_size = (uint32_t)data_size;
    slot->generation = generation;
    slot->sequence = sequence;
    slot->segment_mask = segment_mask;
    slot->timestamp_ns = timestamp_ns;
    slot->quality_flags = quality_flags;

    const uint32_t ring_index = head % TDMA_FLIGHT_RX_FRAME_SLOT_COUNT;
    fifo->rx_ring[ring_index].slot_index = (uint32_t)slot_index;
    fifo->rx_ring[ring_index].generation = generation;
    tdma_flight_store_u32(&slot->owner, TDMA_FLIGHT_RX_OWNER_CORE0_PARSE);
    tdma_flight_store_u32(&fifo->rx_head, head + 1u);
    tdma_flight_counter_inc(&fifo->rx_publish_count);
    return true;
}

bool tdma_flight_fifo_core0_acquire_rx(tdma_flight_fifo_t *fifo,
                                       tdma_flight_rx_view_t *view)
{
    if (view != NULL) {
        memset(view, 0, sizeof(*view));
        view->slot_index = UINT32_MAX;
    }
    if (fifo == NULL || view == NULL) {
        return false;
    }
    const uint32_t head = tdma_flight_load_u32(&fifo->rx_head);
    const uint32_t tail = tdma_flight_load_u32(&fifo->rx_tail);
    if (tdma_flight_ring_empty(head, tail)) {
        return false;
    }
    const uint32_t ring_index = tail % TDMA_FLIGHT_RX_FRAME_SLOT_COUNT;
    const uint32_t slot_index =
        tdma_flight_load_u32(&fifo->rx_ring[ring_index].slot_index);
    if (slot_index >= TDMA_FLIGHT_RX_FRAME_SLOT_COUNT ||
        tdma_flight_load_u32(&fifo->rx_slots[slot_index].owner) !=
            TDMA_FLIGHT_RX_OWNER_CORE0_PARSE) {
        tdma_flight_counter_inc(&fifo->rx_mirror_drop_count);
        tdma_flight_counter_inc(&fifo->rx_publish_drop_count);
        return false;
    }
    tdma_flight_store_u32(&fifo->rx_tail, tail + 1u);
    const tdma_flight_rx_slot_t *slot = &fifo->rx_slots[slot_index];
    view->generation = tdma_flight_load_u32(&slot->generation);
    view->sequence = tdma_flight_load_u32(&slot->sequence);
    view->segment_mask = tdma_flight_load_u32(&slot->segment_mask);
    view->data_size = tdma_flight_load_u32(&slot->data_size);
    view->timestamp_ns = __atomic_load_n(&slot->timestamp_ns, __ATOMIC_ACQUIRE);
    view->quality_flags = tdma_flight_load_u32(&slot->quality_flags);
    view->data = slot->data;
    view->slot_index = slot_index;
    tdma_flight_counter_inc(&fifo->rx_acquire_count);
    return true;
}

bool tdma_flight_fifo_core0_release_rx(tdma_flight_fifo_t *fifo,
                                       uint32_t slot_index)
{
    if (fifo == NULL || slot_index >= TDMA_FLIGHT_RX_FRAME_SLOT_COUNT ||
        tdma_flight_load_u32(&fifo->rx_slots[slot_index].owner) !=
            TDMA_FLIGHT_RX_OWNER_CORE0_PARSE) {
        return false;
    }
    tdma_flight_store_u32(&fifo->rx_slots[slot_index].owner,
                          TDMA_FLIGHT_RX_OWNER_FREE);
    tdma_flight_counter_inc(&fifo->rx_release_count);
    return true;
}

bool tdma_flight_fifo_get_snapshot(const tdma_flight_fifo_t *fifo,
                                   tdma_flight_fifo_snapshot_t *snapshot)
{
    if (fifo == NULL || snapshot == NULL) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->version = TDMA_FLIGHT_FIFO_VERSION;
    snapshot->tx_publish_count = tdma_flight_load_u32(&fifo->tx_publish_count);
    snapshot->tx_publish_reject_count =
        tdma_flight_load_u32(&fifo->tx_publish_reject_count);
    snapshot->tx_acquire_count = tdma_flight_load_u32(&fifo->tx_acquire_count);
    snapshot->tx_image_stale_count =
        tdma_flight_load_u32(&fifo->tx_image_stale_count);
    snapshot->tx_reuse_count = tdma_flight_load_u32(&fifo->tx_reuse_count);
    snapshot->tx_release_count = tdma_flight_load_u32(&fifo->tx_release_count);
    snapshot->tx_active_slot = tdma_flight_load_u32(&fifo->tx_active_slot);
    snapshot->tx_active_generation =
        tdma_flight_load_u32(&fifo->tx_active_generation);
    snapshot->rx_publish_count = tdma_flight_load_u32(&fifo->rx_publish_count);
    snapshot->rx_mirror_drop_count =
        tdma_flight_load_u32(&fifo->rx_mirror_drop_count);
    snapshot->rx_publish_drop_count = snapshot->rx_mirror_drop_count;
    snapshot->rx_acquire_count = tdma_flight_load_u32(&fifo->rx_acquire_count);
    snapshot->rx_release_count = tdma_flight_load_u32(&fifo->rx_release_count);

    const uint32_t tx_head = tdma_flight_load_u32(&fifo->tx_head);
    const uint32_t tx_tail = tdma_flight_load_u32(&fifo->tx_tail);
    snapshot->tx_ready_count = tx_head - tx_tail;
    const uint32_t rx_head = tdma_flight_load_u32(&fifo->rx_head);
    const uint32_t rx_tail = tdma_flight_load_u32(&fifo->rx_tail);
    snapshot->rx_queued_count = rx_head - rx_tail;
    for (uint32_t i = 0u; i < TDMA_FLIGHT_RX_FRAME_SLOT_COUNT; i++) {
        if (tdma_flight_load_u32(&fifo->rx_slots[i].owner) ==
            TDMA_FLIGHT_RX_OWNER_CORE0_PARSE) {
            snapshot->rx_parse_count++;
        }
    }
    return true;
}
