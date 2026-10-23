/* Core1 adapter boundary for an owner-admitted autonomous origin. Physical
 * callbacks own the graph; the adapter owns software views and provenance.
 * No polling action here launches, rearms or waits for a physical cycle. */
static bool tdma_pio_spi_ring_adapter_rx_once(tdma_pio_spi_ring_adapter_t *adapter);

static bool tdma_pio_spi_ring_origin_mailbox_valid(const uint8_t *mailbox,
                                                  uint32_t slot, uint32_t mask)
{
    return mailbox != NULL &&
        ((uint32_t)mailbox[0] | ((uint32_t)mailbox[1] << 8u)) == TDMA_FLIGHT_MAILBOX_MAGIC &&
        mailbox[TDMA_FLIGHT_MAILBOX_VERSION_OFFSET] == TDMA_FLIGHT_MAILBOX_VERSION &&
        (tdma_process_image_flight_class_valid(mailbox[3]) ||
         tdma_process_image_typed_sync_class_valid(mailbox[3])) &&
        mailbox[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] == slot &&
        (mailbox[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] & ~mask) == 0u &&
        tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET) ==
            ((uint32_t)mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET] |
             ((uint32_t)mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] << 8u));
}

static bool tdma_pio_spi_ring_origin_header_valid(
    const tdma_pio_spi_ring_adapter_t *adapter, const tdma_transport_frame_view_t *view)
{
    if (view->payload_class != TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE ||
        !tdma_pio_spi_ring_product_payload(adapter, view->payload_size) ||
        view->origin_slot_id != adapter->config.local_slot_id ||
        view->hop_count != adapter->config.node_count - 1u ||
        view->hop_limit != view->hop_count ||
        view->schedule_crc32 != adapter->config.schedule_crc32 ||
        view->ring_profile_crc32 != adapter->config.ring_profile_crc32 ||
        view->flags != (TDMA_TRANSPORT_FLAG_REQUIRE_FEEDBACK | TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE))
        return false;
    return true;
}

static bool tdma_pio_spi_ring_origin_packet_valid(
    const tdma_pio_spi_ring_adapter_t *adapter, const tdma_transport_frame_view_t *view)
{
    if (!tdma_pio_spi_ring_origin_header_valid(adapter, view)) return false;
    const uint32_t mask = (1u << adapter->config.node_count) - 1u;
    for (uint32_t slot = 0u; slot < adapter->config.node_count; ++slot) {
        if (!tdma_pio_spi_ring_origin_mailbox_valid(
                view->payload + slot * TDMA_FLIGHT_SHORT_SLOT_SIZE, slot, mask)) return false;
    }
    return true;
}

/* Bootstrap feedback may have a valid mutable transport header while a
 * follower mailbox is still incomplete. Check the frozen software copy
 * before consuming the one-shot owner grant or touching physical state.
 * A rejected candidate stays on the existing bootstrap path until another
 * completed return arrives; the autonomous running path never calls this. */
static bool tdma_pio_spi_ring_origin_seed_valid(
    const tdma_pio_spi_ring_adapter_t *adapter)
{
    tdma_transport_frame_view_t view;
    tdma_transport_result_t result;
    return tdma_transport_frame_decode(adapter->last_rx_packet, adapter->last_rx_packet_size,
               &view, &result) &&
        tdma_pio_spi_ring_origin_packet_valid(adapter, &view) &&
        view.transport_sequence == adapter->up_sequence &&
        view.identity_crc32 == adapter->up_tx_frame_crc32;
}

bool tdma_pio_spi_ring_adapter_set_phys_origin(tdma_pio_spi_ring_adapter_t *adapter,
                                              const tdma_pio_spi_ring_origin_ops_t *ops)
{
    if (adapter == NULL || adapter->started != 0u || adapter->origin.active != 0u ||
        (ops != NULL && (ops->begin == NULL || ops->poll == NULL || ops->healthy == NULL ||
         ops->ready == NULL || ops->publish == NULL || ops->observe == NULL ||
         ops->take_rx_observation == NULL ||
         ((ops->grant_tx == NULL) != (ops->commit_tx == NULL))))) return false;
    adapter->phys_origin = ops != NULL ? *ops : (tdma_pio_spi_ring_origin_ops_t){0};
    return true;
}

static void tdma_pio_spi_ring_origin_invalidate_time(tdma_pio_spi_ring_adapter_t *adapter)
{
    adapter->timestamp_resolution_ns = 0u;
    adapter->timestamp_flags = TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    adapter->feedback_reference_sequence = 0u;
    adapter->feedback_reference_frame_crc32 = 0u;
    adapter->feedback_timestamp_resolution_ns = 0u;
    adapter->feedback_timestamp_flags = TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
    adapter->reference_tx_timestamp_ns = 0ull;
    adapter->feedback_rx_timestamp_ns = 0ull;
    adapter->rx_ready_timestamp_ns = 0ull;
    adapter->local_tx_edge_timestamp_ns = 0ull;
    adapter->local_tx_edge_flags = 0u;
    /* The handoff clears timestamp eligibility; a loop/beat measurement taken
     * before it cannot describe this origin session either. */
    tdma_pio_spi_ring_adapter_reset_loop_time(adapter);
    memset(&adapter->origin_feedback_tx, 0, sizeof(adapter->origin_feedback_tx));
    memset(adapter->reference_tx_evidence, 0, sizeof(adapter->reference_tx_evidence));
    memset(adapter->local_rx_evidence, 0, sizeof(adapter->local_rx_evidence));
    memset(adapter->local_tx_edge_evidence, 0, sizeof(adapter->local_tx_edge_evidence));
    memset(&adapter->clock_observation, 0, sizeof(adapter->clock_observation));
}

/* The autonomous origin has no per-edge CPU latch: its cadence emits the frame
 * that follows an accepted return from that same return, so one accepted
 * own-frame return is exactly one emission (see tdma_ring_loop_time_t).  The
 * feedback pair is therefore taken from two consecutive accepted returns of
 * the reference's own frame: the previous return's raw TIMER1 sample is the
 * transmission boundary of the frame accepted now and the current sample is its
 * return.  The frame identity is the hardware-checked own-frame return
 * (tdma_pio_spi_ring_origin_pair_valid) whose selected local generation and
 * transport sequence advanced, so the pair brackets one transmitted frame and
 * not two adjacent ones.  Both endpoints are the same observation point, so the
 * systematic core1 acceptance latency cancels in the difference and no absolute
 * per-edge latch is claimed.  Only the shared timestamp clock is used: without
 * it, or without a single-revolution interval, nothing is published. */
static void tdma_pio_spi_ring_origin_correlate_feedback(
    tdma_pio_spi_ring_adapter_t *adapter,
    const tdma_transport_frame_view_t *view,
    uint32_t returned_generation)
{
    if (adapter == NULL || view == NULL) return;
    const uint64_t rx_ticks = adapter->loop_time_last_rx_ticks;
    const uint64_t tx_ticks = adapter->origin_feedback_tx.ticks;
    const uint32_t tick_hz = tdma_pio_spi_ring_adapter_loop_tick_hz();
    const bool one_emission =
        tick_hz != 0u &&
        view->transport_sequence != 0u && view->identity_crc32 != 0u &&
        returned_generation != 0u &&
        adapter->origin_feedback_tx.sequence != 0u &&
        adapter->origin_feedback_tx.generation != 0u &&
        adapter->origin_feedback_tx.sequence != view->transport_sequence &&
        adapter->origin_feedback_tx.generation != returned_generation &&
        tx_ticks != 0ull && rx_ticks > tx_ticks;
    /* The anchor follows the newest accepted return even when this pair is
     * ineligible: a stale anchor would pair two different frames. */
    adapter->origin_feedback_tx.ticks = rx_ticks;
    adapter->origin_feedback_tx.sequence = view->transport_sequence;
    adapter->origin_feedback_tx.generation = returned_generation;
    if (!one_emission) return;
    const uint64_t tx_ns = tdma_pio_spi_ring_adapter_loop_ns(tx_ticks);
    const uint64_t rx_ns = tdma_pio_spi_ring_adapter_loop_ns(rx_ticks);
    if (tx_ns == 0ull || rx_ns <= tx_ns ||
        rx_ns - tx_ns > (uint64_t)adapter->config.feedback_timeout_ns) {
        /* Wider than one nominal revolution: a return was not observed in
         * between, so the previous anchor is not this frame's transmission. */
        return;
    }
    adapter->feedback_reference_sequence = view->transport_sequence;
    adapter->feedback_reference_frame_crc32 = view->identity_crc32;
    /* Same rounding rule as vdc_timestamp_clock_resolution_ns(). */
    adapter->feedback_timestamp_resolution_ns =
        (1000000000u + tick_hz - 1u) / tick_hz;
    adapter->feedback_timestamp_flags = TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED;
    adapter->reference_tx_timestamp_ns = tx_ns;
    adapter->feedback_rx_timestamp_ns = rx_ns;
}

/* Caller already owns the sole snapshot writer guard. */
static bool tdma_pio_spi_ring_adapter_start_origin_impl(tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t rearm_budget_ticks, uint32_t abort_poll_count)
{
    if (adapter == NULL || adapter->started == 0u || !adapter->configured ||
        adapter->origin.active != 0u || adapter->phys_origin.begin == NULL ||
        adapter->phys_disarm == NULL || adapter->phys_rx == NULL ||
        !tdma_pio_spi_ring_adapter_resident_process_image(adapter) ||
        (adapter->config.flags & TDMA_RING_FLAG_DIAGNOSTIC_CONTINUE) == 0u ||
        tdma_ring_diagnostic_burst_limit(adapter->config.flags) != 0u ||
        adapter->comm_fsm.state != TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY ||
        !adapter->resident_seeded || !adapter->resident_return_ready ||
        !adapter->last_rx_gate_accepted || adapter->reference_tx_completion_pending ||
        adapter->pending_tx_evidence || adapter->rx_queue_count != 0u ||
        tdma_rx_prepare_state(adapter->rx_preparation) != TDMA_RX_PREPARE_IDLE ||
        !tdma_pio_spi_ring_product_payload(adapter,
            adapter->last_rx_packet_size - TDMA_TRANSPORT_FRAME_HEADER_SIZE) ||
        adapter->receive_health.configured == 0u || rearm_budget_ticks == 0u ||
        abort_poll_count == 0u || abort_poll_count > UINT16_MAX) return false;
    tdma_transport_frame_view_t view;
    tdma_transport_result_t result;
    if (!tdma_transport_frame_decode(adapter->last_rx_packet, adapter->last_rx_packet_size,
            &view, &result) || !tdma_pio_spi_ring_origin_packet_valid(adapter, &view) ||
        view.transport_sequence != adapter->up_sequence || view.identity_crc32 != adapter->up_tx_frame_crc32)
        return false;

    memset(&adapter->origin, 0, sizeof(adapter->origin));
    memset(adapter->origin_shadow, 0, sizeof(adapter->origin_shadow));
    adapter->origin.active = 1u;
    /* Once a handoff touches hardware, a failure requires common STOP.
     * Keep this latch even if the physical backend is no longer armed. */
    const bool started = adapter->phys_origin.begin(adapter->phys_ctrl_context, &adapter->config,
        adapter->last_rx_packet, adapter->last_rx_packet_size, rearm_budget_ticks, abort_poll_count);
    tdma_pio_spi_ring_origin_invalidate_time(adapter);
    if (!started || !tdma_adapter_comm_fsm_dispatch(&adapter->comm_fsm,
            TDMA_ADAPTER_COMM_EVENT_RESIDENT_PREPARE, 0u)) {
        (void)tdma_adapter_comm_fsm_dispatch(&adapter->comm_fsm,
            TDMA_ADAPTER_COMM_EVENT_ERROR, TDMA_ADAPTER_COMM_ERROR_RUNTIME);
        tdma_pio_spi_ring_adapter_set_error(adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_COMM_FSM);
        return false;
    }
    adapter->origin.returned.sequence = view.transport_sequence;
    adapter->origin.returned.identity = view.identity_crc32;
    adapter->origin_shadow[0].generation = 1u;
    memcpy(adapter->origin_shadow[0].mailbox,
        view.payload + adapter->config.local_slot_id * TDMA_FLIGHT_SHORT_SLOT_SIZE,
        TDMA_FLIGHT_SHORT_SLOT_SIZE);
    /* Seed content has no known full owner generation. Do not recover it
     * from the mailbox's truncated wire sequence. */
    adapter->resident_return_ready = false;
    adapter->resident_completion_pending = false;
    adapter->last_error = TDMA_PIO_SPI_RING_ADAPTER_ERROR_NONE;
    return true;
}

bool tdma_pio_spi_ring_adapter_start_origin(tdma_pio_spi_ring_adapter_t *adapter,
    uint32_t rearm_budget_ticks, uint32_t abort_poll_count)
{
    if (adapter == NULL) return false;
    tdma_pio_spi_ring_adapter_snapshot_write_begin(adapter);
    const bool result = tdma_pio_spi_ring_adapter_start_origin_impl(
        adapter, rearm_budget_ticks, abort_poll_count);
    tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
    return result;
}

static bool tdma_pio_spi_ring_origin_prepare_service(tdma_pio_spi_ring_adapter_t *adapter)
{
    const tdma_origin_build_result_t result = adapter->phys_origin.poll(adapter->phys_ctrl_context);
    if (result == TDMA_ORIGIN_BUILD_BUSY) return true;
    if (result == TDMA_ORIGIN_BUILD_DONE &&
        tdma_adapter_comm_fsm_dispatch(&adapter->comm_fsm,
                                      TDMA_ADAPTER_COMM_EVENT_RESIDENT_INSTALLED, 0u)) {
        adapter->origin.published_generation = 1u;
        return true;
    }
    (void)tdma_adapter_comm_fsm_dispatch(&adapter->comm_fsm,
        TDMA_ADAPTER_COMM_EVENT_ERROR, TDMA_ADAPTER_COMM_ERROR_RUNTIME);
    tdma_pio_spi_ring_adapter_set_error(adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_COMM_FSM);
    return false;
}

static bool tdma_pio_spi_ring_origin_pair_valid(tdma_pio_spi_ring_adapter_t *adapter,
    const tdma_transport_frame_view_t *view, const tdma_origin_observation_t *observation,
    const tdma_rx_prepare_t *prepared)
{
    const bool packet_valid = prepared != NULL
        ? prepared->origin_mailboxes_valid && tdma_pio_spi_ring_origin_header_valid(adapter, view)
        : tdma_pio_spi_ring_origin_packet_valid(adapter, view);
    if (observation == NULL || !packet_valid ||
        observation->sequence != view->transport_sequence || observation->identity != view->identity_crc32 ||
        observation->local_generation == 0u || observation->output_remaining != 0u) return false;
    const uint32_t advance = view->transport_sequence - adapter->origin.returned.sequence;
    if (advance == 0u || advance > INT32_MAX) return false;
    if (prepared != NULL) return prepared->origin_owner_match;
    for (uint32_t i = 0u; i < TDMA_ORIGIN_PLAN_BANK_COUNT; ++i) {
        if (adapter->origin_shadow[i].generation == observation->local_generation &&
            memcmp(adapter->origin_shadow[i].mailbox,
                view->payload + adapter->config.local_slot_id * TDMA_FLIGHT_SHORT_SLOT_SIZE,
                TDMA_FLIGHT_SHORT_SLOT_SIZE) == 0) return true;
    }
    return false;
}

static void tdma_pio_spi_ring_origin_accept(tdma_pio_spi_ring_adapter_t *adapter,
                                           const tdma_origin_observation_t *observation)
{
    const uint32_t gap = observation->sequence - adapter->origin.returned.sequence - 1u;
    adapter->origin.unobserved_cycle_count = gap > UINT32_MAX - adapter->origin.unobserved_cycle_count
        ? UINT32_MAX : adapter->origin.unobserved_cycle_count + gap;
    adapter->origin.observed_return_count++;
    adapter->origin.returned = *observation;
    for (uint32_t i = 0u; i < TDMA_ORIGIN_PLAN_BANK_COUNT; ++i) {
        if (adapter->origin_shadow[i].generation == observation->local_generation) {
            adapter->origin.matched_owner_generation = adapter->origin_shadow[i].owner_generation;
            adapter->origin.matched_owner_sequence = adapter->origin_shadow[i].owner_sequence;
            break;
        }
    }
    /* This pair has a real decoded return and its bank's local identity.
     * It supplies transport progress, not SENT/ACK, a latch or a VDC epoch. */
    adapter->up_sequence = observation->sequence;
    adapter->up_tx_frame_crc32 = observation->identity;
}

static void tdma_pio_spi_ring_origin_commit_owner(tdma_pio_spi_ring_adapter_t *adapter,
    const tdma_flight_tx_view_t *tx, const uint8_t *mailbox, uint32_t generation)
{
    const uint32_t bank = adapter->origin_shadow[0].generation == adapter->origin.published_generation ? 1u : 0u;
    adapter->origin_shadow[bank].generation = generation;
    adapter->origin_shadow[bank].owner_generation = tx->generation;
    adapter->origin_shadow[bank].owner_sequence = tx->sequence;
    memcpy(adapter->origin_shadow[bank].mailbox, mailbox, TDMA_FLIGHT_SHORT_SLOT_SIZE);
    adapter->origin.published_generation = generation;
    adapter->origin.published_owner_generation = tx->generation;
    adapter->origin.published_owner_sequence = tx->sequence;
}

static bool tdma_pio_spi_ring_origin_publish_maintenance(
    tdma_pio_spi_ring_adapter_t *adapter);

static bool tdma_pio_spi_ring_origin_publish_async(tdma_pio_spi_ring_adapter_t *adapter)
{
    tdma_overlay_prepare_t *job = adapter->overlay_preparation;
    const uint32_t state = tdma_overlay_prepare_state(job);
    if (state == TDMA_OVERLAY_PREPARE_READY) {
        const bool current = job->kind == TDMA_OVERLAY_PREPARE_ORIGIN &&
            job->request_epoch == job->epoch &&
            tdma_flight_engine_is_active(adapter->flight_engine) &&
            adapter->config.local_slot_id == job->layout.local_slot_id &&
            adapter->flight_engine->local_slot_id == job->layout.local_slot_id &&
            adapter->flight_engine->map.payload_size == job->layout.payload_size &&
            __atomic_load_n(&adapter->flight_engine->map_generation, __ATOMIC_ACQUIRE) == job->layout.map_generation;
        if (!current) {
            tdma_overlay_prepare_release(job);
            return false;
        }
        uint32_t generation = 0u;
        if (!adapter->phys_origin.commit_tx(adapter->phys_ctrl_context, job, &generation))
            return true; /* Deferred admission retains this complete private version. */
        const bool valid = generation != 0u && generation != adapter->origin.published_generation;
        if (valid) {
            (void)tdma_flight_engine_accept_tx(adapter->flight_engine, &job->layout,
                &job->applied, job->tx.reused_previous);
            tdma_pio_spi_ring_origin_commit_owner(adapter, &job->tx, job->tx_data, generation);
            (void)tdma_pio_spi_ring_origin_publish_maintenance(adapter);
        }
        tdma_overlay_prepare_release(job);
        return valid;
    }
    if (state == TDMA_OVERLAY_PREPARE_FAILED) {
        tdma_overlay_prepare_release(job);
        return false;
    }
    if (state != TDMA_OVERLAY_PREPARE_IDLE ||
        !adapter->phys_origin.ready(adapter->phys_ctrl_context) || adapter->flight_fifo == NULL) return true;
    tdma_flight_tx_view_t tx;
    if (!tdma_flight_fifo_core1_acquire_tx(adapter->flight_fifo, &tx)) {
        if (adapter->maintenance_mailbox != NULL &&
            adapter->phys_origin.ready(adapter->phys_ctrl_context)) {
            uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
            if (adapter->maintenance_mailbox(
                    adapter->maintenance_mailbox_context, mailbox)) {
                uint32_t generation = 0u;
                if (adapter->phys_origin.publish(
                        adapter->phys_ctrl_context, mailbox, &generation) &&
                    generation != 0u &&
                    generation != adapter->origin.published_generation) {
                    adapter->origin.published_generation = generation;
                }
            }
        }
        return true;
    }
    if (adapter->origin.published_owner_generation == tx.generation &&
        adapter->origin.published_owner_sequence == tx.sequence) {
        /* Acquiring made this slot CORE1_ACTIVE; returning without releasing it leaks the
         * only active view, and every later acquire then fails while the FIFO keeps
         * reporting one ready image for ever (measured 2026-10-17: tx_ready_count stayed 1
         * for 568 s, which stopped BOTH the ordinary VDC frame and T2). */
        tdma_flight_fifo_core1_release_tx(adapter->flight_fifo);
        return true;
    }
    if (tx.data == NULL ||
        !tdma_flight_engine_copy_tx_layout(adapter->flight_engine, &job->layout) ||
        job->layout.local_slot_id != adapter->config.local_slot_id) return false;
    const uint32_t offset = job->layout.local_slot_id * TDMA_FLIGHT_SHORT_SLOT_SIZE;
    if (tx.data_size != sizeof(job->tx_data) && tx.data_size < offset + sizeof(job->tx_data)) return false;
    if (!adapter->phys_origin.grant_tx(adapter->phys_ctrl_context, job)) return true;
    job->tx = tx;
    memcpy(job->tx_data, tx.data + (tx.data_size == sizeof(job->tx_data) ? 0u : offset), sizeof(job->tx_data));
    job->tx.data_size = sizeof(job->tx_data);
    return tdma_overlay_prepare_origin_request(job);
}

/* Separate frame keeps the provider/projection stack out of the ordinary
 * FIFO builder's frame. Both branches still have the same sole Core1 owner. */
/* Streaming-schedule evidence lives outside adapter->origin: that struct is
 * memset on every handoff and reset, which would erase latched totals. */
static uint32_t s_origin_schedule_allow_total;
static uint32_t s_origin_schedule_block_total;


#if TDMA_LOOP_SCHEDULE_ENABLED
#include "tdma_seat_mount_store.h"

/* A load rides a seat: the seat is the capacity, the load is what is mounted on
 * it. This map names the mounting, so the publisher compares seat against seat
 * and never equates a load with a seat. An unmounted load takes no seat and is
 * left unconstrained here. */
static tdma_loop_slot_class_t tdma_pio_spi_ring_origin_load_seat(uint8_t message_class)
{
    /* The mounting comes from the persisted seat mount table, never from a
     * hard-coded class list: an unloaded or unmounted load takes no seat. */
    tdma_seat_mount_table_t table;
    if (!tdma_seat_mount_store_get(&table)) return TDMA_LOOP_SLOT_NONE;
    return tdma_seat_mount_lookup(&table, message_class);
}

/* Seat-window gate for one publish attempt. True means "publish now"; false
 * means "defer and stay retryable". The plan is rebuilt per attempt, so the gate
 * is a small unit that a host harness can extract and exercise directly. A seat
 * the plan cannot serve at all (dropped for space, or never requested like the
 * deliberately disabled LOAD seat) is counted as starvation rather than deferred
 * silently forever. */
static bool tdma_pio_spi_ring_origin_seat_gate(tdma_pio_spi_ring_adapter_t *adapter,
    tdma_loop_slot_class_t load_seat, uint64_t boundary_sequence)
{
    if (load_seat == TDMA_LOOP_SLOT_NONE) return true;   /* unmounted load: unconstrained */
    tdma_loop_plan_request_t plan_request;
    tdma_loop_plan_t plan;
    tdma_loop_plan_default_request(&plan_request, true, true, false);
    (void)tdma_loop_plan(&plan_request, &plan);
    const tdma_loop_seat_service_t service = tdma_loop_plan_seat_service(&plan, load_seat,
        boundary_sequence, adapter->config.cycle_period_ns);
    if (service == TDMA_LOOP_SEAT_SERVICE_STARVED) {
        adapter->origin.seat_starved_count++;
        if (load_seat == TDMA_LOOP_SLOT_FIRST_CLASS)
            adapter->origin.seat_first_class_starved_count++;
        return false;
    }
    return service == TDMA_LOOP_SEAT_SERVICE_OK;
}
#endif

/* TDMA-SEAT-01: board one complete local mailbox on the shared fast path.
 * False is a fault; a deferred hardware publish leaves the caller retrying. */
static bool tdma_pio_spi_ring_origin_board(tdma_pio_spi_ring_adapter_t *adapter,
    const uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE], bool *boarded)
{
    *boarded = false;
    tdma_flight_tx_layout_t layout;
    if (!tdma_flight_engine_copy_tx_layout(adapter->flight_engine, &layout) ||
        layout.local_slot_id != adapter->config.local_slot_id ||
        layout.output_segment_mask != (1u << layout.local_slot_id)) return false;
    uint32_t generation = 0u;
    if (!adapter->phys_origin.publish(adapter->phys_ctrl_context, mailbox, &generation)) return true;
    if (!generation || generation == adapter->origin.published_generation) return false;
    const tdma_flight_engine_apply_t applied = {
        .output_segment_mask = layout.output_segment_mask,
        .output_bytes = TDMA_FLIGHT_SHORT_SLOT_SIZE,
    };
    (void)tdma_flight_engine_accept_tx(adapter->flight_engine, &layout, &applied, false);
    /* Not a Core0 FIFO offer: do not credit its generation or SENT/ACK. */
    const tdma_flight_tx_view_t tx = {0};
    tdma_pio_spi_ring_origin_commit_owner(adapter, &tx, mailbox, generation);
    *boarded = true;
    return true;
}

static bool tdma_pio_spi_ring_origin_first_class_valid(
    const tdma_pio_spi_ring_adapter_t *adapter, const uint8_t *mailbox)
{
    return tdma_process_image_flight_class_valid(mailbox[3]) &&
        tdma_pio_spi_ring_origin_mailbox_valid(mailbox, adapter->config.local_slot_id,
            (1u << adapter->config.node_count) - 1u);
}

/* One origin fast-path step. `*handled` reports ROUTE OWNERSHIP, not a spent
 * opportunity: it is true whenever the priority provider is enabled, i.e. this
 * path owns local mailbox publication for the slot, even when nobody has news
 * and the call consumes no publication opportunity at all. Publication
 * opportunities are counted separately in origin.seat_opportunity_count, and each
 * one ends in exactly one seat, deferral or refusal. */
static bool tdma_pio_spi_ring_origin_publish_maintenance(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    ++adapter->maintenance_attempts;
    if (adapter->maintenance_mailbox == NULL ||
        !adapter->phys_origin.ready(adapter->phys_ctrl_context)) {
        return false;
    }
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
    if (!adapter->maintenance_mailbox(
            adapter->maintenance_mailbox_context, mailbox)) {
        return false;
    }
    uint32_t generation = 0u;
    if (!adapter->phys_origin.publish(
            adapter->phys_ctrl_context, mailbox, &generation) ||
        generation == 0u ||
        generation == adapter->origin.published_generation) {
        return false;
    }
    adapter->origin.published_generation = generation;
    ++adapter->maintenance_publish_ok;
    return true;
}

static __attribute__((noinline)) bool tdma_pio_spi_ring_origin_publish_priority(
    tdma_pio_spi_ring_adapter_t *adapter, bool *handled)
{
    ++adapter->maintenance_priority_calls;
    uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];    const tdma_priority_tx_result_t offered =
        adapter->priority_tx_provider(&adapter->config, mailbox);
    *handled = offered != TDMA_PRIORITY_TX_DISABLED;
    if (!*handled) return true;
    /* While the priority provider is enabled this fast path is the only route
     * for local mailboxes; the first-class seat rides it (TDMA-SEAT-01). A
     * record waiting across route loss is retired, not boarded: publish its
     * wait first so the seat backpressure fact is not lost. */
    if (adapter->first_class != NULL && !tdma_first_class_route(adapter->first_class)) {
        if (tdma_first_class_pending(adapter->first_class) &&
            adapter->origin.seat_first_class_wait >
                adapter->origin.seat_first_class_wait_max)
            adapter->origin.seat_first_class_wait_max =
                adapter->origin.seat_first_class_wait;
        tdma_first_class_retire(adapter->first_class, true);
    }
    /* A worker may finish a previously granted ordinary offer. Cancel its
     * lease before direct publication; never select it after priority owns
     * this slot, and never wait for Core0 to acknowledge cancellation. */
    const uint32_t station_state = tdma_overlay_prepare_state(adapter->overlay_preparation);
    if (station_state != TDMA_OVERLAY_PREPARE_IDLE) {
        if (adapter->overlay_preparation->kind != TDMA_OVERLAY_PREPARE_ORIGIN ||
            adapter->overlay_preparation->plan != NULL) return false;
        if (station_state != TDMA_OVERLAY_PREPARE_CANCELLED)
            (void)tdma_overlay_prepare_cancel(adapter->overlay_preparation);
        /* Origin's worker owns only private SRAM, not an exchange/DMA bank.
         * Leave its cancelled station untouched until ACK; direct publication
         * uses separate owner shadows and has no Core0 completion dependency. */
    }
    bool priority_new = false;
    if (offered == TDMA_PRIORITY_TX_READY) {
        if (!tdma_process_image_typed_sync_class_valid(mailbox[3])) return false;
        /* Duplicate provider content is the common case. Compare against the
         * last boarded mailbox first: those bytes already passed the boarding
         * CRC, so recomputing it for an identical payload is wasted TDMA time.
         * New means new against the priority seat's own last boarding, not the
         * seat's last publication: a first-class boarding must not make an old
         * priority event look new. */
        const bool same_as_last = adapter->origin.priority_boarded &&
            memcmp(adapter->origin.last_priority, mailbox, sizeof(mailbox)) == 0;
        if (!same_as_last &&
            !tdma_pio_spi_ring_origin_mailbox_valid(
                mailbox, adapter->config.local_slot_id,
                (1u << adapter->config.node_count) - 1u)) return false;
        priority_new = !same_as_last;
    } else if (offered != TDMA_PRIORITY_TX_EMPTY) {
        return false;
    }
    /* EMPTY reserves nothing: an idle priority seat yields the opportunity. */
    const bool first_pending = tdma_first_class_pending(adapter->first_class);
    if (!adapter->phys_origin.ready(adapter->phys_ctrl_context)) return true;
    if (!priority_new && !first_pending) {
        (void)tdma_pio_spi_ring_origin_publish_maintenance(adapter);
        return true;
    }

    /* One publication opportunity: the exchange is free and someone has news.
     * Rotate the leading class per opportunity (M:N), never wasting it. The
     * ratio is a STOP-time setting with a compiled default, so a zero/unset
     * field still yields the 1:1 default instead of a zero cycle. */
    const uint32_t priority_share = tdma_seat_rotation_priority(
        adapter->seat_rotation_priority_share);
    const uint32_t first_class_share = tdma_seat_rotation_first_class(
        adapter->seat_rotation_first_class_share);
    /* Three-share form (owner decision 2026-10-16): the cycle is p + f + s, and in the
     * last s slots NEITHER fast seat boards - that time is reserved for the path that
     * does not use the fast channel ("second class" = ordinary frames / refmem images).
     * A yielded slot leaves both leases pending, so nothing is lost; it is deliberately
     * NOT counted as first-class wait, because the seat was not passed over in favour of
     * the other fast seat, the cycle simply reserves this opportunity. s == 0 reproduces
     * the previous behaviour exactly.
     * Evidence without a new counter: opportunities - priority - first_class = yields. */
    const uint32_t second_class_share = adapter->seat_rotation_second_class_share;
    const uint32_t cycle = priority_share + first_class_share + second_class_share;
    const uint32_t slot = (cycle != 0u)
        ? adapter->origin.seat_opportunity_count % cycle : 0u;
    adapter->origin.seat_opportunity_count++;
    if (slot >= priority_share + first_class_share) {
        ++adapter->maintenance_yield_slots;
        (void)tdma_pio_spi_ring_origin_publish_maintenance(adapter);
        return true;
    }
    const bool priority_leads = slot < priority_share;
    const bool board_first = first_pending && (!priority_new || !priority_leads);
    bool boarded = false;
    if (board_first) {
        uint8_t first[TDMA_FLIGHT_SHORT_SLOT_SIZE];
        if (!tdma_first_class_take(adapter->first_class, first)) return true;
        if (!tdma_pio_spi_ring_origin_first_class_valid(adapter, first)) {
            /* A malformed record is retired, never retried or allowed to block. */
            tdma_first_class_finish(adapter->first_class, true);
            adapter->origin.seat_first_class_reject_count++;
            return true;
        }
        const bool ok = tdma_pio_spi_ring_origin_board(adapter, first, &boarded);
        tdma_first_class_finish(adapter->first_class, boarded);
        if (!ok) return false;
        if (!boarded) {
            /* The record stayed leased for the next opportunity: a deferred
             * hardware publish is also a waiting opportunity, so it advances the
             * same declared service budget. Otherwise a record blocked by the
             * publish side (rather than by priority contention) could wait
             * without bound and without evidence. */
            adapter->origin.seat_first_class_deferred_count++;
            adapter->origin.seat_first_class_wait++;
            if (adapter->origin.seat_first_class_wait ==
                TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES)
                adapter->origin.seat_first_class_wait_violation_count++;
            return true;
        }
        if (boarded) {
            adapter->origin.seat_first_class_count++;
            if (priority_new) adapter->origin.seat_priority_yield_count++;
            if (adapter->origin.seat_first_class_wait > adapter->origin.seat_first_class_wait_max)
                adapter->origin.seat_first_class_wait_max = adapter->origin.seat_first_class_wait;
            adapter->origin.seat_first_class_wait = 0u;
        }
        return true;
    }
    if (!tdma_pio_spi_ring_origin_board(adapter, mailbox, &boarded)) return false;
    if (boarded) {
        memcpy(adapter->origin.last_priority, mailbox, sizeof(mailbox));
        adapter->origin.priority_boarded = 1u;
        adapter->origin.seat_priority_count++;
        if (first_pending) {
            adapter->origin.seat_first_class_wait++;
            /* Bounded service: count the episode once when it reaches the
             * declared budget instead of only reporting a raw maximum. */
            if (adapter->origin.seat_first_class_wait ==
                TDMA_SEAT_FIRST_CLASS_WAIT_MAX_OPPORTUNITIES)
                adapter->origin.seat_first_class_wait_violation_count++;
        }
    }
    return true;
}

static bool tdma_pio_spi_ring_origin_publish(tdma_pio_spi_ring_adapter_t *adapter)
{
    if (adapter->priority_tx_provider != NULL) {
        bool handled;
        const bool valid = tdma_pio_spi_ring_origin_publish_priority(adapter, &handled);
        if (handled) return valid;
    }
    if (adapter->overlay_preparation != NULL && adapter->phys_origin.grant_tx != NULL)
        return tdma_pio_spi_ring_origin_publish_async(adapter);
    if (!adapter->phys_origin.ready(adapter->phys_ctrl_context) || adapter->flight_fifo == NULL)
        return true;
    tdma_flight_tx_view_t tx;
    if (!tdma_flight_fifo_core1_acquire_tx(adapter->flight_fifo, &tx)) {
        if (adapter->maintenance_mailbox != NULL &&
            adapter->phys_origin.ready(adapter->phys_ctrl_context)) {
            uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE];
            if (adapter->maintenance_mailbox(
                    adapter->maintenance_mailbox_context, mailbox)) {
                uint32_t generation = 0u;
                if (adapter->phys_origin.publish(
                        adapter->phys_ctrl_context, mailbox, &generation) &&
                    generation != 0u &&
                    generation != adapter->origin.published_generation) {
                    adapter->origin.published_generation = generation;
                }
            }
        }
        return true;
    }
    if (adapter->origin.published_owner_generation == tx.generation &&
        adapter->origin.published_owner_sequence == tx.sequence) {
        /* Acquiring made this slot CORE1_ACTIVE; returning without releasing it leaks the
         * only active view, and every later acquire then fails while the FIFO keeps
         * reporting one ready image for ever (measured 2026-10-17: tx_ready_count stayed 1
         * for 568 s, which stopped BOTH the ordinary VDC frame and T2). */
        tdma_flight_fifo_core1_release_tx(adapter->flight_fifo);
        return true;
    }
    if (tx.generation == 0u || tx.segment_mask != (1u << adapter->config.local_slot_id)) {
        tdma_flight_fifo_core1_release_tx(adapter->flight_fifo);
        adapter->origin.flight_publish_reject_count++;
        return false;
    }
    /* Activation already validated the complete map under its writer guard.
     * Only that fixed local-mailbox grant can authorize this hardware graph.
     * The active FIFO owns complete Core0-prepared bytes throughout this call;
     * no remote image, full map copy or byte bitmap construction is needed. */
    tdma_flight_tx_layout_t layout;
    if (!tdma_flight_engine_copy_tx_layout(adapter->flight_engine, &layout) ||
        layout.local_slot_id != adapter->config.local_slot_id || tx.data == NULL) {
        tdma_flight_fifo_core1_release_tx(adapter->flight_fifo);
        adapter->origin.flight_publish_reject_count++;
        return false;
    }
    const uint32_t offset = layout.local_slot_id * TDMA_FLIGHT_SHORT_SLOT_SIZE;
    const uint8_t *mailbox = tx.data;
    if (tx.data_size != TDMA_FLIGHT_SHORT_SLOT_SIZE) {
        /* Preserve full-image producer support, selecting only our grant. */
        if (tx.data_size < offset + TDMA_FLIGHT_SHORT_SLOT_SIZE) {
        tdma_flight_fifo_core1_release_tx(adapter->flight_fifo);
        adapter->origin.flight_publish_reject_count++;
            return false;
        }
        mailbox += offset;
    }
    if (!tdma_process_image_flight_class_valid(mailbox[3]) ||
        !tdma_pio_spi_ring_origin_mailbox_valid(mailbox, adapter->config.local_slot_id,
            (1u << adapter->config.node_count) - 1u)) {
        tdma_flight_fifo_core1_release_tx(adapter->flight_fifo);
        adapter->origin.flight_publish_reject_count++;
        return false;
    }

#if TDMA_LOOP_SCHEDULE_ENABLED
    /* Publish only in the window of the seat this load is mounted on; otherwise
     * return true to defer (the acquired view stays valid for retry). */
    if (!tdma_pio_spi_ring_origin_seat_gate(adapter,
            tdma_pio_spi_ring_origin_load_seat(mailbox[3]),
            adapter->origin.boundary.sequence)) return true;
#endif

    const tdma_flight_engine_apply_t applied = {
        .output_segment_mask = layout.output_segment_mask,
        .output_bytes = TDMA_FLIGHT_SHORT_SLOT_SIZE,
    };
    if (!tdma_flight_engine_accept_tx(adapter->flight_engine, &layout, &applied,
                                    tx.reused_previous)) {
        tdma_flight_fifo_core1_release_tx(adapter->flight_fifo);
        adapter->origin.flight_publish_reject_count++;
        return false;
    }
    uint32_t generation = 0u;
    if (!adapter->phys_origin.publish(adapter->phys_ctrl_context, mailbox, &generation))
        return true; /* deferred: active FIFO view remains available for retry */
    if (generation == 0u || generation == adapter->origin.published_generation) {
        tdma_flight_fifo_core1_release_tx(adapter->flight_fifo);
        adapter->origin.flight_publish_reject_count++;
        return false;
    }
    tdma_pio_spi_ring_origin_commit_owner(adapter, &tx, mailbox, generation);
    return true;
}

static bool tdma_pio_spi_ring_origin_service(tdma_pio_spi_ring_adapter_t *adapter, bool *rx_ok)
{
    if (adapter->comm_fsm.state != TDMA_ADAPTER_COMM_STATE_AUTONOMOUS ||
        !adapter->phys_origin.healthy(adapter->phys_ctrl_context)) {
        (void)tdma_adapter_comm_fsm_dispatch(&adapter->comm_fsm,
            TDMA_ADAPTER_COMM_EVENT_ERROR, TDMA_ADAPTER_COMM_ERROR_RUNTIME);
        tdma_pio_spi_ring_adapter_set_error(adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_COMM_FSM);
        return false;
    }
    *rx_ok = tdma_pio_spi_ring_adapter_rx_once(adapter);
    const uint64_t observe_start = tdma_service_timing_now();
    tdma_origin_observation_t observation;
    if (adapter->phys_origin.observe(adapter->phys_ctrl_context, &observation))
        adapter->origin.boundary = observation;
    tdma_service_timing_record(TDMA_TIMING_ORIGIN_OBSERVE, observe_start);
    const uint64_t publish_start = tdma_service_timing_now();
    const bool published = tdma_pio_spi_ring_origin_publish(adapter);
    tdma_service_timing_record(TDMA_TIMING_ORIGIN_PUBLISH, publish_start);
    if (!published) {
        tdma_pio_spi_ring_adapter_set_error(adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT);
        /* Invalid/late software publication keeps the old hardware shadow. */
    }
    return true;
}

void tdma_pio_spi_ring_adapter_schedule_evidence(tdma_origin_schedule_evidence_t *out)
{
    if (out == NULL) return;
    out->allow_total = s_origin_schedule_allow_total;
    out->block_total = s_origin_schedule_block_total;
}
