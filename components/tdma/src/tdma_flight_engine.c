#include "tdma_flight_engine.h"

#include <string.h>

static void tdma_flight_engine_counter_inc(volatile uint32_t *counter)
{
    (void)__atomic_add_fetch(counter, 1u, __ATOMIC_RELAXED);
}

static void tdma_flight_engine_counter_add(volatile uint32_t *counter,
                                           uint32_t value)
{
    (void)__atomic_add_fetch(counter, value, __ATOMIC_RELAXED);
}

static void tdma_flight_engine_set_result(
    tdma_flight_engine_result_t *result,
    tdma_flight_engine_result_t value)
{
    if (result != NULL) {
        *result = value;
    }
}

static uint16_t tdma_flight_engine_get_le16(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0]) | ((uint16_t)src[1] << 8u));
}

void tdma_flight_engine_fill_alignment_symbols(uint8_t *payload,
                                               size_t payload_size)
{
    if (payload == NULL) {
        return;
    }
    uint8_t state = TDMA_FLIGHT_ALIGNMENT_LFSR_SEED;
    for (size_t byte_index = 0u; byte_index < payload_size; byte_index++) {
        uint8_t value = 0u;
        for (uint32_t bit_index = 0u; bit_index < 8u; bit_index++) {
            const uint8_t symbol = state & 1u;
            value |= (uint8_t)(symbol << (7u - bit_index));
            state >>= 1u;
            if (symbol != 0u) {
                state ^= TDMA_FLIGHT_ALIGNMENT_LFSR_MASK;
            }
        }
        payload[byte_index] = value;
    }
}

static bool tdma_flight_engine_fast_mailbox_header(
    const uint8_t *payload,
    const tdma_process_image_segment_t *segment,
    uint32_t local_slot_id,
    uint16_t *seq16)
{
    if (payload == NULL || segment == NULL || seq16 == NULL ||
        segment->segment_id >= TDMA_PROCESS_IMAGE_SEGMENT_COUNT ||
        segment->byte_length < TDMA_FLIGHT_MAILBOX_FAST_HEADER_SIZE) {
        return false;
    }

    const uint8_t *mailbox = payload + segment->byte_offset;
    if (tdma_flight_engine_get_le16(mailbox) != TDMA_FLIGHT_MAILBOX_MAGIC ||
        mailbox[TDMA_FLIGHT_MAILBOX_VERSION_OFFSET] !=
            TDMA_FLIGHT_MAILBOX_VERSION) {
        return false;
    }
    const uint32_t source_slot =
        mailbox[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET];
    const uint32_t target_mask =
        mailbox[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET];
    if (source_slot != segment->owner_slot_id ||
        source_slot == local_slot_id ||
        (target_mask & (1u << local_slot_id)) == 0u) {
        return false;
    }
    *seq16 = tdma_flight_engine_get_le16(
        &mailbox[TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET]);
    return true;
}

/* Core1 fast RX classifier. It only reads the fixed mailbox header; the
 * payload remains opaque and is parsed by core0. */
static bool tdma_flight_engine_fast_mailbox_match(
    tdma_flight_engine_t *engine,
    const uint8_t *payload,
    const tdma_process_image_segment_t *segment,
    uint32_t local_slot_id)
{
    if (engine == NULL || payload == NULL || segment == NULL ||
        segment->segment_id >= TDMA_PROCESS_IMAGE_SEGMENT_COUNT ||
        segment->byte_length < TDMA_FLIGHT_MAILBOX_FAST_HEADER_SIZE) {
        return false;
    }

    tdma_flight_engine_counter_inc(&engine->rx_bitmap_scan_count);
    uint16_t seq16 = 0u;
    if (!tdma_flight_engine_fast_mailbox_header(payload,
                                                segment,
                                                local_slot_id,
                                                &seq16)) {
        return false;
    }
    const uint32_t segment_mask = 1u << segment->segment_id;
    if ((engine->rx_seen_segment_mask & segment_mask) != 0u &&
        engine->rx_last_seq16_by_segment[segment->segment_id] == seq16) {
        tdma_flight_engine_counter_inc(&engine->rx_bitmap_duplicate_count);
        return false;
    }
    return true;
}

static uint32_t tdma_flight_engine_classify_input_map(
    tdma_flight_engine_t *engine,
    const uint8_t *incoming,
    size_t incoming_size,
    const tdma_process_image_map_t *map,
    uint32_t local_slot_id)
{
    if (engine == NULL || incoming == NULL || map == NULL ||
        incoming_size != map->payload_size) {
        return 0u;
    }
    uint32_t mask = 0u;
    for (uint32_t i = 0u; i < TDMA_PROCESS_IMAGE_SEGMENT_COUNT; i++) {
        const tdma_process_image_segment_t *segment = &map->segment[i];
        if (segment->used == 0u ||
            segment->owner_slot_id == local_slot_id) {
            continue;
        }
        if (tdma_flight_engine_fast_mailbox_match(engine,
                                                  incoming,
                                                  segment,
                                                  local_slot_id)) {
            mask |= 1u << segment->segment_id;
        }
    }
    return mask;
}

static bool tdma_flight_engine_lock_map(tdma_flight_engine_t *engine,
                                        uint32_t *sequence)
{
    if (engine == NULL || sequence == NULL) {
        return false;
    }
    uint32_t expected =
        __atomic_load_n(&engine->map_sequence, __ATOMIC_ACQUIRE);
    if ((expected & 1u) != 0u ||
        !__atomic_compare_exchange_n(&engine->map_sequence,
                                     &expected,
                                     expected + 1u,
                                     false,
                                     __ATOMIC_ACQ_REL,
                                     __ATOMIC_ACQUIRE)) {
        return false;
    }
    *sequence = expected;
    return true;
}

static void tdma_flight_engine_unlock_map(tdma_flight_engine_t *engine,
                                          uint32_t sequence)
{
    __atomic_store_n(&engine->map_sequence,
                     sequence + 2u,
                     __ATOMIC_RELEASE);
}

static bool tdma_flight_engine_read_map(
    const tdma_flight_engine_t *engine,
    tdma_process_image_map_t *map,
    uint32_t *local_slot_id,
    uint32_t *map_generation)
{
    if (engine == NULL || map == NULL) {
        return false;
    }
    for (uint32_t retry = 0u;
         retry < TDMA_FLIGHT_MAP_SNAPSHOT_RETRY_MAX;
         retry++) {
        const uint32_t sequence_begin =
            __atomic_load_n(&engine->map_sequence, __ATOMIC_ACQUIRE);
        if ((sequence_begin & 1u) != 0u) {
            continue;
        }
        *map = engine->map;
        if (local_slot_id != NULL) {
            *local_slot_id = engine->local_slot_id;
        }
        if (map_generation != NULL) {
            *map_generation =
                __atomic_load_n(&engine->map_generation, __ATOMIC_RELAXED);
        }
        const uint32_t sequence_end =
            __atomic_load_n(&engine->map_sequence, __ATOMIC_ACQUIRE);
        if (sequence_begin == sequence_end && (sequence_end & 1u) == 0u) {
            return true;
        }
    }
    return false;
}

bool tdma_flight_engine_init(tdma_flight_engine_t *engine)
{
    if (engine == NULL) {
        return false;
    }
    memset(engine, 0, sizeof(*engine));
    return true;
}

bool tdma_flight_engine_configure(tdma_flight_engine_t *engine,
                                  const tdma_process_image_map_t *map)
{
    tdma_process_image_map_result_t map_result =
        TDMA_PROCESS_IMAGE_MAP_BAD_ARGUMENT;
    if (engine == NULL || map == NULL ||
        !tdma_process_image_map_validate(map, &map_result)) {
        if (engine != NULL) {
            tdma_flight_engine_counter_inc(&engine->map_reject_count);
        }
        return false;
    }
    uint32_t sequence = 0u;
    if (!tdma_flight_engine_lock_map(engine, &sequence)) {
        tdma_flight_engine_counter_inc(&engine->map_reject_count);
        return false;
    }
    if (tdma_flight_engine_is_active(engine)) {
        tdma_flight_engine_unlock_map(engine, sequence);
        tdma_flight_engine_counter_inc(&engine->map_reject_count);
        return false;
    }
    engine->map = *map;
    engine->rx_seen_segment_mask = 0u;
    memset(engine->rx_last_seq16_by_segment,
           0,
           sizeof(engine->rx_last_seq16_by_segment));
    (void)__atomic_add_fetch(&engine->map_generation, 1u, __ATOMIC_RELAXED);
    __atomic_store_n(&engine->configured, 1u, __ATOMIC_RELEASE);
    tdma_flight_engine_unlock_map(engine, sequence);
    return true;
}

bool tdma_flight_engine_activate(tdma_flight_engine_t *engine,
                                 uint32_t local_slot_id)
{
    if (engine == NULL || local_slot_id >= TDMA_TRANSPORT_FRAME_MAX_SLOT_COUNT) {
        return false;
    }
    uint32_t sequence = 0u;
    if (!tdma_flight_engine_lock_map(engine, &sequence)) {
        return false;
    }
    if (!tdma_flight_engine_is_configured(engine) ||
        tdma_flight_engine_is_active(engine)) {
        tdma_flight_engine_unlock_map(engine, sequence);
        return false;
    }
    engine->local_slot_id = local_slot_id;
    engine->rx_seen_segment_mask = 0u;
    memset(engine->rx_last_seq16_by_segment,
           0,
           sizeof(engine->rx_last_seq16_by_segment));
    __atomic_store_n(&engine->active, 1u, __ATOMIC_RELEASE);
    tdma_flight_engine_unlock_map(engine, sequence);
    return true;
}

void tdma_flight_engine_deactivate(tdma_flight_engine_t *engine)
{
    if (engine == NULL) {
        return;
    }
    __atomic_store_n(&engine->active, 0u, __ATOMIC_RELEASE);
}

bool tdma_flight_engine_is_configured(const tdma_flight_engine_t *engine)
{
    return engine != NULL &&
           __atomic_load_n(&engine->configured, __ATOMIC_ACQUIRE) != 0u;
}

bool tdma_flight_engine_is_active(const tdma_flight_engine_t *engine)
{
    return engine != NULL &&
           __atomic_load_n(&engine->active, __ATOMIC_ACQUIRE) != 0u;
}

bool tdma_flight_engine_apply(tdma_flight_engine_t *engine,
                              const uint8_t *incoming,
                              size_t incoming_size,
                              const tdma_flight_tx_view_t *tx_view,
                              uint8_t *output,
                              size_t output_capacity,
                              tdma_flight_engine_apply_t *applied,
                              tdma_flight_engine_result_t *result)
{
    if (applied != NULL) {
        memset(applied, 0, sizeof(*applied));
    }
    tdma_flight_engine_set_result(result, TDMA_FLIGHT_ENGINE_BAD_ARGUMENT);
    if (engine == NULL || incoming == NULL || output == NULL ||
        !tdma_flight_engine_is_active(engine)) {
        return false;
    }
    tdma_process_image_map_t map;
    uint32_t local_slot_id = 0u;
    if (!tdma_flight_engine_read_map(engine,
                                     &map,
                                     &local_slot_id,
                                     NULL)) {
        tdma_flight_engine_set_result(result,
                                      TDMA_FLIGHT_ENGINE_MAP_UNAVAILABLE);
        return false;
    }

    if (incoming_size != map.payload_size ||
        output_capacity < map.payload_size) {
        tdma_flight_engine_counter_inc(&engine->length_reject_count);
        tdma_flight_engine_set_result(result,
                                      TDMA_FLIGHT_ENGINE_LENGTH_REJECTED);
        return false;
    }

    const uint32_t input_segment_mask =
        tdma_flight_engine_classify_input_map(engine,
                                              incoming,
                                              incoming_size,
                                              &map,
                                              local_slot_id);
    memcpy(output, incoming, incoming_size);
    for (uint32_t i = 0u; i < TDMA_PROCESS_IMAGE_SEGMENT_COUNT; i++) {
        const tdma_process_image_segment_t *segment = &map.segment[i];
        if (segment->used == 0u) {
            continue;
        }
        const uint32_t segment_mask = 1u << segment->segment_id;
        if (segment->owner_slot_id != local_slot_id) {
            continue;
        }
        if ((segment->flags & TDMA_PROCESS_SEGMENT_FLAG_FLIGHT_WRITE) != 0u) {
            const uint8_t *segment_data = NULL;
            if (tx_view != NULL && tx_view->data != NULL) {
                if (tx_view->data_size == segment->byte_length) {
                    /* Core0 may publish only its local mailbox. This is the
                     * normal on-device FIFO form; the engine expands it into
                     * the fixed eight-mailbox wire image. */
                    segment_data = tx_view->data;
                } else if (tx_view->data_size >=
                           segment->byte_offset + segment->byte_length) {
                    /* A full process image remains supported for host and
                     * replay producers. */
                    segment_data = tx_view->data + segment->byte_offset;
                }
            }
            if (segment_data == NULL) {
                tdma_flight_engine_counter_inc(
                    &engine->tx_unavailable_count);
                /* No active image is a bounded bring-up condition. Preserve
                 * the received bytes and keep the wire path running; the
                 * next cycle may provide the prepared output image. */
                tdma_flight_engine_set_result(
                    result,
                    TDMA_FLIGHT_ENGINE_TX_UNAVAILABLE);
                continue;
            }
            memcpy(output + segment->byte_offset,
                   segment_data,
                   segment->byte_length);
            if (applied != NULL) {
                applied->output_segment_mask |= segment_mask;
                applied->output_bytes += segment->byte_length;
            }
        }
    }
    if (applied != NULL) {
        applied->input_segment_mask |= input_segment_mask;
        for (uint32_t i = 0u; i < TDMA_PROCESS_IMAGE_SEGMENT_COUNT; i++) {
            const tdma_process_image_segment_t *segment = &map.segment[i];
            if (segment->used != 0u &&
                (input_segment_mask & (1u << segment->segment_id)) != 0u) {
                applied->input_bytes += segment->byte_length;
            }
        }
    }
    tdma_flight_engine_counter_inc(&engine->map_apply_count);
    if (applied != NULL) {
        tdma_flight_engine_counter_add(&engine->input_bytes,
                                       applied->input_bytes);
        tdma_flight_engine_counter_add(&engine->output_bytes,
                                       applied->output_bytes);
    }
    if (tx_view != NULL && tx_view->reused_previous) {
        tdma_flight_engine_counter_inc(&engine->tx_stale_reuse_count);
    }
    tdma_flight_engine_set_result(result, TDMA_FLIGHT_ENGINE_OK);
    return true;
}

bool tdma_flight_engine_classify_input(
    tdma_flight_engine_t *engine,
    const uint8_t *incoming,
    size_t incoming_size,
    uint32_t *input_segment_mask)
{
    if (input_segment_mask != NULL) {
        *input_segment_mask = 0u;
    }
    if (engine == NULL || incoming == NULL || input_segment_mask == NULL ||
        !tdma_flight_engine_is_active(engine)) {
        return false;
    }
    tdma_process_image_map_t map;
    uint32_t local_slot_id = 0u;
    if (!tdma_flight_engine_read_map(engine,
                                     &map,
                                     &local_slot_id,
                                     NULL)) {
        return false;
    }
    if (incoming_size != map.payload_size) {
        return false;
    }
    *input_segment_mask =
        tdma_flight_engine_classify_input_map(engine,
                                              incoming,
                                              incoming_size,
                                              &map,
                                              local_slot_id);
    return true;
}

bool tdma_flight_engine_commit_input(
    tdma_flight_engine_t *engine,
    const uint8_t *incoming,
    size_t incoming_size,
    uint32_t input_segment_mask)
{
    if (engine == NULL || incoming == NULL || input_segment_mask == 0u ||
        !tdma_flight_engine_is_active(engine)) {
        return false;
    }
    tdma_process_image_map_t map;
    uint32_t local_slot_id = 0u;
    if (!tdma_flight_engine_read_map(engine,
                                     &map,
                                     &local_slot_id,
                                     NULL)) {
        return false;
    }
    if (incoming_size != map.payload_size) {
        return false;
    }

    uint32_t committed_mask = 0u;
    for (uint32_t i = 0u; i < TDMA_PROCESS_IMAGE_SEGMENT_COUNT; i++) {
        const tdma_process_image_segment_t *segment = &map.segment[i];
        if (segment->used == 0u || segment->segment_id >= 32u) {
            continue;
        }
        const uint32_t segment_mask = 1u << segment->segment_id;
        if ((input_segment_mask & segment_mask) == 0u) {
            continue;
        }
        uint16_t seq16 = 0u;
        if (!tdma_flight_engine_fast_mailbox_header(incoming,
                                                    segment,
                                                    local_slot_id,
                                                    &seq16)) {
            continue;
        }
        engine->rx_seen_segment_mask |= segment_mask;
        engine->rx_last_seq16_by_segment[segment->segment_id] = seq16;
        committed_mask |= segment_mask;
        tdma_flight_engine_counter_inc(&engine->rx_bitmap_hit_count);
    }
    return committed_mask == input_segment_mask;
}

bool tdma_flight_engine_get_snapshot(
    const tdma_flight_engine_t *engine,
    tdma_flight_engine_snapshot_t *snapshot)
{
    if (engine == NULL || snapshot == NULL) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->version = TDMA_FLIGHT_ENGINE_VERSION;
    snapshot->configured = tdma_flight_engine_is_configured(engine) ? 1u : 0u;
    snapshot->active = tdma_flight_engine_is_active(engine) ? 1u : 0u;
    tdma_process_image_map_t map;
    if (!tdma_flight_engine_read_map(engine,
                                     &map,
                                     &snapshot->local_slot_id,
                                     &snapshot->map_generation)) {
        return false;
    }
    snapshot->map_crc32 = snapshot->configured != 0u
                              ? map.map_crc32
                              : 0u;
    snapshot->payload_size = snapshot->configured != 0u
                                 ? map.payload_size
                                 : 0u;
    for (uint32_t i = 0u; i < TDMA_PROCESS_IMAGE_SEGMENT_COUNT; i++) {
        if (map.segment[i].used != 0u &&
            map.segment[i].owner_slot_id == snapshot->local_slot_id) {
            snapshot->local_segment_count++;
        }
    }
    snapshot->map_apply_count =
        __atomic_load_n(&engine->map_apply_count, __ATOMIC_RELAXED);
    snapshot->input_bytes =
        __atomic_load_n(&engine->input_bytes, __ATOMIC_RELAXED);
    snapshot->output_bytes =
        __atomic_load_n(&engine->output_bytes, __ATOMIC_RELAXED);
    snapshot->tx_stale_reuse_count =
        __atomic_load_n(&engine->tx_stale_reuse_count, __ATOMIC_RELAXED);
    snapshot->map_reject_count =
        __atomic_load_n(&engine->map_reject_count, __ATOMIC_RELAXED);
    snapshot->length_reject_count =
        __atomic_load_n(&engine->length_reject_count, __ATOMIC_RELAXED);
    snapshot->tx_unavailable_count =
        __atomic_load_n(&engine->tx_unavailable_count, __ATOMIC_RELAXED);
    snapshot->rx_bitmap_scan_count =
        __atomic_load_n(&engine->rx_bitmap_scan_count, __ATOMIC_RELAXED);
    snapshot->rx_bitmap_hit_count =
        __atomic_load_n(&engine->rx_bitmap_hit_count, __ATOMIC_RELAXED);
    snapshot->rx_bitmap_duplicate_count =
        __atomic_load_n(&engine->rx_bitmap_duplicate_count, __ATOMIC_RELAXED);
    return true;
}
