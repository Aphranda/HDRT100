#include "tdma_seat_admission.h"
#include "tdma_seat_mount_store.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "tdma_pio_spi_ring_adapter_internal.h"
#include "tdma_long_frame.h"

#include <string.h>

#include "tdma_process_image_layout.h"
#include "tdma_service_timing.h"

#if TDMA_SERVICE_TIMING_ENABLED
/* Only the device build owns a real timebase (see tdma_service_timing.h). */
#include "vdc_timestamp_clock.h"
#endif

#define TDMA_PIO_SPI_RING_RESIDENT_BOOTSTRAP_RETRY_MAX 1u

#include "tdma_pio_spi_ring_origin.h"
#include "tdma_pio_spi_ring_rx_prepare.h"
#include "tdma_pio_spi_ring_adapter_api.h"
/* Legacy injected/synchronous RX owns the full packet only while needed.
 * Inlining this branch would charge its array to every async acceptance. */
__attribute__((noinline)) bool tdma_pio_spi_ring_adapter_rx_legacy(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    return tdma_pio_spi_ring_adapter_rx_capture(adapter, packet, sizeof(packet));
}

static bool tdma_pio_spi_ring_adapter_rx_once_impl(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    tdma_rx_prepare_t *job = adapter->rx_preparation;
    if (job != NULL) {
        const uint32_t state = tdma_rx_prepare_state(job);
        tdma_service_timing_rx_station(state, adapter->last_service_ns, job->capture_service_ns);
        if (state != TDMA_RX_PREPARE_IDLE)
            return tdma_pio_spi_ring_rx_accept(adapter, job);
        return tdma_pio_spi_ring_adapter_rx_capture(adapter, job->packet, sizeof(job->packet));
    }
    return tdma_pio_spi_ring_adapter_rx_legacy(adapter);
}

bool tdma_pio_spi_ring_adapter_rx_once(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    const uint64_t started = tdma_service_timing_now();
    const bool result = tdma_pio_spi_ring_adapter_rx_once_impl(adapter);
    tdma_service_timing_record(TDMA_TIMING_RX_HANDOFF, started);
    return result;
}

/* Software forwarding has its own bounded queue drain. Hardware forwarding
 * never waits for this drain; its observation path samples once per phase. */
#define TDMA_PIO_SPI_RING_ADAPTER_FORWARD_POLLS 4u

static bool tdma_pio_spi_ring_adapter_rx_poll(
    tdma_pio_spi_ring_adapter_t *adapter,
    uint64_t now_ns)
{
    if (adapter == NULL) {
        return false;
    }
    /* The DMA/PIO capture remains armed continuously.  Only reference-node
     * feedback is held until the calibrated loop-delay lower bound; forward
     * nodes must consume RX immediately so cut-through forwarding is not
     * delayed by the full-ring measurement. */
    if (adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE &&
        adapter->rx_ready_timestamp_ns != 0ull &&
        now_ns < adapter->rx_ready_timestamp_ns) {
        return false;
    }
    /* PIO/DMA stay armed independently. A continuously producing ring must
     * not multiply capture/scan/parse work inside one Core1 phase. Retain the
     * scanner cursor and consume another observation on the next phase. */
    const bool rx_ok = tdma_pio_spi_ring_adapter_rx_once(adapter);
    if (rx_ok && adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE) {
        adapter->rx_ready_timestamp_ns = 0ull;
    }
    return rx_ok;
}

static bool tdma_pio_spi_ring_adapter_train_clock(void *context,
                                                  uint32_t cycles)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL || adapter->started == 0u ||
        adapter->phys_train == NULL || cycles == 0u ||
        tdma_ring_diagnostic_burst_limit(adapter->config.flags) != 0u) {
        return false;
    }
    return adapter->phys_train(adapter->phys_ctrl_context, cycles);
}

static void tdma_pio_spi_ring_adapter_train_clock_service(void *context,
                                                          uint64_t now_ns)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL || adapter->started == 0u ||
        adapter->phys_train_service == NULL) {
        return;
    }
    adapter->phys_train_service(adapter->phys_ctrl_context, now_ns);
}

static bool tdma_pio_spi_ring_adapter_forward_poll(
    tdma_pio_spi_ring_adapter_t *adapter,
    bool *rx_ok)
{
    bool any_rx = false;
    for (uint32_t i = 0u; i < TDMA_PIO_SPI_RING_ADAPTER_FORWARD_POLLS; i++) {
        if (!tdma_pio_spi_ring_adapter_rx_once(adapter)) {
            continue;
        }
        any_rx = true;
        if (!tdma_pio_spi_ring_adapter_tx_forward(adapter)) {
            if (rx_ok != NULL) {
                *rx_ok = any_rx;
            }
            return false;
        }
    }
    if (rx_ok != NULL) {
        *rx_ok = any_rx;
    }
    return true;
}

static bool tdma_pio_spi_ring_adapter_reuse_overlay_tx(
    tdma_pio_spi_ring_adapter_t *adapter, const tdma_overlay_prepare_t *job)
{
    if (!adapter->resident_overlay_bootstrap_prepared ||
        job->request_epoch != job->epoch ||
        !tdma_flight_engine_is_active(adapter->flight_engine)) return false;
    const tdma_flight_engine_t *engine = adapter->flight_engine;
    const uint32_t begin = __atomic_load_n(&engine->map_sequence, __ATOMIC_ACQUIRE);
    if ((begin & 1u) != 0u || engine->local_slot_id != job->layout.local_slot_id ||
        __atomic_load_n(&engine->map_generation, __ATOMIC_RELAXED) != job->layout.map_generation)
        return false;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    if (begin != __atomic_load_n(&engine->map_sequence, __ATOMIC_ACQUIRE) ||
        !tdma_flight_engine_is_active(engine)) return false;
    /* Successful preparation already validated layout and hop for this epoch.
     * Active maps and topology cannot change before STOP cancels this job.
     * A queued descriptor always takes the full acquisition/validation path. */
    return tdma_flight_fifo_core1_reuse_current_tx(adapter->flight_fifo,
        adapter->resident_overlay_tx_generation, adapter->resident_overlay_tx_sequence);
}

static bool tdma_pio_spi_ring_adapter_prepare_overlay_async(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    tdma_overlay_prepare_t *job = adapter->overlay_preparation;
    const uint32_t state = tdma_overlay_prepare_state(job);
    if (state == TDMA_OVERLAY_PREPARE_READY) {
        /* Core0 is finished; it cannot touch the plan during Core1 binding.
         * The epoch/map check precedes physical publication, not vice versa. */
        const bool current = job->request_epoch == job->epoch &&
            tdma_flight_engine_is_active(adapter->flight_engine) &&
            adapter->flight_engine->local_slot_id == job->layout.local_slot_id &&
            __atomic_load_n(&adapter->flight_engine->map_generation, __ATOMIC_ACQUIRE) ==
                job->layout.map_generation;
        if (current && adapter->phys_commit_overlay(adapter->phys_ctrl_context, job)) {
            (void)tdma_flight_engine_accept_tx(adapter->flight_engine, &job->layout,
                                              &job->applied, job->tx.reused_previous);
            adapter->resident_overlay_bootstrap_prepared = true;
            adapter->resident_overlay_target_sequence = job->target_sequence;
            adapter->resident_overlay_tx_generation = job->tx.generation;
            adapter->resident_overlay_tx_sequence = job->tx.sequence;
        }
        /* A changed alignment/config invalidates only this ordinary update.
         * Keep the live generation, and prepare from new facts next phase. */
        tdma_overlay_prepare_release(job);
        return true;
    }
    if (state == TDMA_OVERLAY_PREPARE_FAILED) {
        tdma_overlay_prepare_release(job);
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT);
        return false;
    }
    if (state != TDMA_OVERLAY_PREPARE_IDLE ||
        adapter->last_rx_packet_size > sizeof(job->packet) ||
        !tdma_pio_spi_ring_product_payload(adapter,
            adapter->last_rx_packet_size - TDMA_TRANSPORT_FRAME_HEADER_SIZE)) return true;
    if (adapter->phys_overlay_ready != NULL) {
        /* Retire observed DMA selection and check the live physical gate
         * before reuse accounting. An unchanged TX needs no inactive plan
         * grant/configuration; a queued descriptor still takes that path. */
        if (!adapter->phys_overlay_ready(adapter->phys_ctrl_context)) return true;
        if (tdma_pio_spi_ring_adapter_reuse_overlay_tx(adapter, job)) return true;
    }
    if (!adapter->phys_grant_overlay(adapter->phys_ctrl_context, job)) return true;
    /* Backends without a separate readiness callback retain grant-before-reuse. */
    if (adapter->phys_overlay_ready == NULL &&
        tdma_pio_spi_ring_adapter_reuse_overlay_tx(adapter, job)) return true;
    if (!tdma_flight_engine_copy_tx_layout(adapter->flight_engine, &job->layout) ||
        !tdma_pio_spi_ring_adapter_resident_hop_position(
            adapter, &job->ingress_hop, &job->egress_hop)) return false;
    tdma_flight_tx_view_t tx = {0};
    const bool has_tx = adapter->flight_fifo != NULL &&
        tdma_flight_fifo_core1_acquire_tx(adapter->flight_fifo, &tx);
    if (adapter->resident_overlay_bootstrap_prepared &&
        (!has_tx || (tx.reused_previous &&
            tx.generation == adapter->resident_overlay_tx_generation &&
            tx.sequence == adapter->resident_overlay_tx_sequence))) return true;
    const uint32_t local_offset = job->layout.local_slot_id * TDMA_FLIGHT_SHORT_SLOT_SIZE;
    if (has_tx && (tx.data == NULL || (tx.data_size != sizeof(job->tx_data) &&
        tx.data_size < local_offset + sizeof(job->tx_data)))) return false;
    job->tx = tx;
    if (has_tx) {
        memcpy(job->tx_data, tx.data + (tx.data_size == sizeof(job->tx_data) ? 0u : local_offset),
               sizeof(job->tx_data));
        job->tx.data_size = sizeof(job->tx_data);
    }
    if (job->config.packet_size != adapter->last_rx_packet_size) return false;
    memcpy(job->packet, adapter->last_rx_packet, adapter->last_rx_packet_size);
    job->target_sequence = adapter->down_rx_sequence +
        (adapter->resident_overlay_bootstrap_prepared ? 1u : 0u);
    return tdma_overlay_prepare_request(job);
}

/* The compatibility path owns two full packet models. Keep it out of the
 * async dispatch frame even when the compiler optimizes the hot branch. */
__attribute__((noinline)) bool tdma_pio_spi_ring_adapter_prepare_overlay_legacy(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    if (adapter == NULL || adapter->last_rx_packet_size == 0u ||
        adapter->phys_prepare_overlay == NULL) {
        return false;
    }
    const bool recurring = adapter->phys_overlay_ready != NULL;
    if (recurring && !adapter->phys_overlay_ready(adapter->phys_ctrl_context)) {
        /* No TX acquire, copy, or acceptance while the inactive pool is
         * pending. The old plan keeps circulating without owner service. */
        return true;
    }
    tdma_transport_frame_view_t view;
    tdma_transport_result_t result = TDMA_TRANSPORT_OK;
    if (!tdma_transport_frame_decode(adapter->last_rx_packet,
                                     adapter->last_rx_packet_size,
                                     &view,
                                     &result)) {
        return false;
    }

    uint32_t ingress_hop = 0u;
    uint32_t egress_hop = 0u;
    if (!tdma_pio_spi_ring_adapter_resident_hop_position(
            adapter, &ingress_hop, &egress_hop)) {
        return false;
    }
    const uint32_t target_sequence =
        adapter->resident_overlay_bootstrap_prepared
            ? view.transport_sequence + 1u
            : view.transport_sequence;

    tdma_flight_tx_view_t tx_view;
    const bool has_tx = adapter->flight_fifo != NULL &&
        tdma_flight_fifo_core1_acquire_tx(adapter->flight_fifo, &tx_view);
    if (recurring && adapter->resident_overlay_bootstrap_prepared &&
        (!has_tx || (tx_view.reused_previous &&
            tx_view.generation == adapter->resident_overlay_tx_generation &&
            tx_view.sequence == adapter->resident_overlay_tx_sequence))) {
        return true;
    }

    uint8_t incoming_model[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    uint8_t processed_model[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    memcpy(incoming_model,
           adapter->last_rx_packet,
           adapter->last_rx_packet_size);
    memcpy(processed_model,
           adapter->last_rx_packet,
           adapter->last_rx_packet_size);
    if (!tdma_transport_frame_prepare_resident_position(
            incoming_model,
            adapter->last_rx_packet_size,
            target_sequence,
            ingress_hop,
            &result)) {
        return false;
    }

    bool applied_ok = false;
    tdma_flight_engine_apply_t applied;
    memset(&applied, 0, sizeof(applied));
    if (adapter->flight_engine != NULL &&
        tdma_flight_engine_is_active(adapter->flight_engine) &&
        view.payload_class == TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE &&
        (view.flags & TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE) != 0u) {
        tdma_flight_engine_result_t engine_result =
            TDMA_FLIGHT_ENGINE_BAD_ARGUMENT;
        applied_ok = tdma_flight_engine_tx_load(
            adapter->flight_engine,
            view.payload,
            view.payload_size,
            has_tx ? &tx_view : NULL,
            processed_model + TDMA_TRANSPORT_FRAME_HEADER_SIZE,
            sizeof(processed_model) - TDMA_TRANSPORT_FRAME_HEADER_SIZE,
            &applied,
            &engine_result);
    }
    if (applied_ok &&
        !tdma_transport_frame_prepare_resident_position(
            processed_model,
            adapter->last_rx_packet_size,
            target_sequence,
            egress_hop,
            &result)) {
        applied_ok = false;
    }
    const bool prepared = applied_ok && adapter->phys_prepare_overlay(
        adapter->phys_ctrl_context,
        incoming_model,
        processed_model,
        adapter->last_rx_packet_size,
        applied.output_byte_bitmap,
        TDMA_FLIGHT_OUTPUT_BITMAP_WORDS);
    if (!prepared || !applied_ok) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_FLIGHT_MAP_REJECT);
    }
    if (prepared && applied_ok) {
        adapter->resident_overlay_bootstrap_prepared = true;
        adapter->resident_overlay_target_sequence = target_sequence;
        adapter->resident_overlay_tx_generation = has_tx ? tx_view.generation : 0u;
        adapter->resident_overlay_tx_sequence = has_tx ? tx_view.sequence : 0u;
    }
    return prepared && applied_ok;
}

static bool tdma_pio_spi_ring_adapter_prepare_process_overlay_impl(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    if (adapter != NULL && adapter->overlay_preparation != NULL &&
        adapter->phys_grant_overlay != NULL && adapter->phys_commit_overlay != NULL)
        return tdma_pio_spi_ring_adapter_prepare_overlay_async(adapter);
    return tdma_pio_spi_ring_adapter_prepare_overlay_legacy(adapter);
}

static bool tdma_pio_spi_ring_adapter_prepare_process_overlay(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    const uint64_t started = tdma_service_timing_now();
    const bool result = tdma_pio_spi_ring_adapter_prepare_process_overlay_impl(adapter);
    tdma_service_timing_record(TDMA_TIMING_OVERLAY_PREPARE, started);
    return result;
}

static bool tdma_pio_spi_ring_adapter_service_overlay_boundary(
    tdma_pio_spi_ring_adapter_t *adapter)
{
    const uint64_t started = tdma_service_timing_now();
    const bool result = adapter->phys_service_overlay_boundary != NULL &&
        adapter->phys_service_overlay_boundary(adapter->phys_ctrl_context);
    tdma_service_timing_record(TDMA_TIMING_OVERLAY_BOUNDARY, started);
    return result;
}

/* Cumulative facts survive a parked preparation. Publishing them does not
 * assert new RX progress or restore the timestamp eligibility cleared by
 * the handoff. Both paths run under the existing service writer guard. */
static void tdma_pio_spi_ring_adapter_publish_status(
    const tdma_pio_spi_ring_adapter_t *adapter, tdma_ring_adapter_status_t *status)
{
    const uint64_t started = tdma_service_timing_now();
    status->up_tx_sequence = adapter->up_sequence;
    status->down_rx_sequence = adapter->down_rx_sequence;
    status->up_tx_frame_crc32 = adapter->up_tx_frame_crc32;
    status->down_rx_frame_crc32 = adapter->down_rx_frame_crc32;
    status->timestamp_resolution_ns =
        adapter->feedback_reference_sequence != 0u
            ? adapter->feedback_timestamp_resolution_ns
            : adapter->timestamp_resolution_ns;
    status->timestamp_flags =
        adapter->feedback_reference_sequence != 0u
            ? adapter->feedback_timestamp_flags
            : adapter->timestamp_flags;
    status->idle_beacon_tx_count = adapter->idle_beacon_tx_count;
    status->idle_beacon_rx_count = adapter->idle_beacon_rx_count;
    status->last_error = adapter->last_error;
    status->tx_count = adapter->tx_count;
    status->rx_count = adapter->rx_count;
    status->rx_bad_count = adapter->rx_bad_count;
    status->rx_transport_bad_count = adapter->rx_transport_bad_count;
    status->rx_schedule_bad_count = adapter->rx_schedule_bad_count;
    status->rx_profile_bad_count = adapter->rx_profile_bad_count;
    status->last_bad_transport_result = adapter->last_bad_transport_result;
    status->last_bad_sequence = adapter->last_bad_sequence;
    status->last_bad_schedule_crc32 = adapter->last_bad_schedule_crc32;
    status->last_bad_profile_crc32 = adapter->last_bad_profile_crc32;
    status->last_bad_header_diff_count =
        adapter->last_bad_header_diff_count;
    status->last_bad_header_first_diff_offset =
        adapter->last_bad_header_first_diff_offset;
    status->last_bad_header_expected_byte =
        adapter->last_bad_header_expected_byte;
    status->last_bad_header_observed_byte =
        adapter->last_bad_header_observed_byte;
    status->feedback_reference_sequence =
        adapter->feedback_reference_sequence;
    status->feedback_reference_frame_crc32 =
        adapter->feedback_reference_frame_crc32;
    status->reference_tx_timestamp_ns = adapter->reference_tx_timestamp_ns;
    status->feedback_rx_timestamp_ns = adapter->feedback_rx_timestamp_ns;
    status->loop_time = adapter->loop_time;
    status->clock_observation = adapter->clock_observation;
    tdma_service_timing_record(TDMA_TIMING_ADAPTER_STATUS, started);
}

static bool tdma_pio_spi_ring_adapter_service_impl(
    void *context,
    uint64_t now_ns,
    tdma_ring_adapter_status_t *status)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL || status == NULL) {
        return false;
    }
    const uint64_t prologue_start = tdma_service_timing_now();
    memset(status, 0, sizeof(*status));
    adapter->service_count++;
    adapter->last_service_ns = now_ns;

    /* A resident flight-origin submit returns at the PIO launch edge.  The
     * hardware latch is harvested on a later core1 pass and attached to the
     * exact sequence/identity entry that was published at launch. */
    bool comm_tx_completion_failed = false;
    if (adapter->origin.active == 0u && adapter->phys_tx_complete != NULL) {
        uint64_t completed_timestamp_ns = 0ull;
        const bool completion_pending =
            adapter->reference_tx_completion_pending;
        const bool bootstrap_completion =
            adapter->resident_bootstrap_tx_pending;
        if (adapter->phys_tx_complete(adapter->phys_context,
                                      &completed_timestamp_ns)) {
            adapter->reference_tx_completion_pending = false;
            adapter->resident_bootstrap_tx_pending = false;
            if (adapter->pending_tx_evidence) {
                const uint32_t evidence_index =
                    adapter->pending_tx_evidence_sequence %
                    TDMA_PIO_SPI_RING_ADAPTER_TX_EVIDENCE_DEPTH;
                if (adapter->reference_tx_evidence[evidence_index].sequence ==
                        adapter->pending_tx_evidence_sequence &&
                    adapter->reference_tx_evidence[evidence_index].identity_crc32 ==
                        adapter->pending_tx_evidence_identity_crc32) {
                    adapter->reference_tx_evidence[evidence_index].timestamp_ns =
                        completed_timestamp_ns;
                    adapter->reference_tx_evidence[evidence_index].valid =
                        completed_timestamp_ns != 0ull;
                    if (completed_timestamp_ns != 0ull) {
                        adapter->reference_tx_timestamp_ns =
                            completed_timestamp_ns;
                    }
                    tdma_pio_spi_ring_adapter_bind_reference_local_tx_edge(
                        adapter,
                        adapter->pending_tx_evidence_sequence,
                        adapter->pending_tx_evidence_identity_crc32,
                        completed_timestamp_ns);
                }
                adapter->pending_tx_evidence = false;
            }
            if (completion_pending &&
                tdma_pio_spi_ring_adapter_resident_process_image(adapter)) {
                if (!tdma_adapter_comm_fsm_dispatch(
                        &adapter->comm_fsm,
                        bootstrap_completion
                            ? TDMA_ADAPTER_COMM_EVENT_BOOTSTRAP_TX_COMPLETED
                            : TDMA_ADAPTER_COMM_EVENT_CLOCK_TX_COMPLETED,
                        0u)) {
                    tdma_pio_spi_ring_adapter_set_error(
                        adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_COMM_FSM);
                    comm_tx_completion_failed = true;
                } else {
                    tdma_pio_spi_ring_adapter_commit_resident_completion(
                        adapter);
                }
            }
        }
    }

    tdma_service_timing_record(TDMA_TIMING_ADAPTER_PROLOGUE, prologue_start);
    if (adapter->started == 0u || !adapter->configured) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_BAD_ARGUMENT);
        return false;
    }
    if (comm_tx_completion_failed) {
        return false;
    }

    status->up_configured = 1u;
    status->down_configured = 1u;
    status->schedule_crc32 = adapter->config.schedule_crc32;

    /* Bring-up honesty gate: without a physical TX path the adapter has no
     * UP leg. Returning false makes the ring runtime report EVIDENCE_MISSING
     * instead of a fabricated running state. RX may come from phys_rx or from
     * the injected test queue; with neither, down_running stays 0. */
    if (adapter->phys_tx == NULL) {
        tdma_pio_spi_ring_adapter_set_error(
            adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_PHYS_MISSING);
        return false;
    }

    bool tx_ok = false;
    bool rx_ok = false;
    if (adapter->origin.active != 0u) {
        if (adapter->comm_fsm.state == TDMA_ADAPTER_COMM_STATE_RESIDENT_PREPARING) {
            const bool progressing = tdma_pio_spi_ring_origin_prepare_service(adapter);
            /* No fake running/timeout evidence during a parked startup. Even
             * DONE returns here; RX/publication wait until the next phase. */
            status->up_running = status->down_running = 0u;
            status->timestamp_flags = TDMA_RING_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY;
            tdma_pio_spi_ring_adapter_publish_status(adapter, status);
            return progressing;
        }
        tx_ok = tdma_pio_spi_ring_origin_service(adapter, &rx_ok);
    } else if (adapter->role == TDMA_PIO_SPI_RING_ROLE_REFERENCE) {
        const bool resident =
            tdma_pio_spi_ring_adapter_resident_process_image(adapter);
        /* Reference node is the ring origin: it emits one IDLE_BEACON per
         * TDMA cycle on the downlink TX leg and receives the same frame back
         * (hop advanced, identity preserved) on the uplink RX leg after it
         * has travelled once around the ring. Only this node may produce
         * simultaneous_feedback_loop_evidence. */
        /* A reference frame occupies one slot period per active node before
         * its feedback can return. The runtime freezes feedback_timeout_ns as
         * cycle_period_ns * node_count, so using it as the emission period
         * preserves the proven two-board 1 ms -> 500 Hz cadence and scales
         * deterministically for 3..8 nodes without a hard-coded divider. */
        const uint64_t emission_period_ns =
            adapter->config.feedback_timeout_ns != 0u
                ? adapter->config.feedback_timeout_ns
                : adapter->config.cycle_period_ns;
        bool emit_now = false;
        if ((!resident || !adapter->resident_seeded) &&
            (adapter->next_tx_deadline_ns == 0ull ||
             now_ns + emission_period_ns < now_ns)) {
            /* Preserve the bring-up behavior where the first service only
             * establishes phase and the next service may emit. The backoff
             * models at most one current core1 tick; steady state below is
             * driven solely by absolute deadlines. */
            const uint64_t phase_backoff_ns =
                emission_period_ns < 1000000ull
                    ? emission_period_ns
                    : 1000000ull;
            adapter->next_tx_deadline_ns =
                now_ns + emission_period_ns - phase_backoff_ns;
        } else if ((!resident || !adapter->resident_seeded) &&
                   now_ns + emission_period_ns <
                   adapter->next_tx_deadline_ns) {
            /* Monotonic clock restarted or wrapped relative to the saved
             * deadline. Re-anchor without emitting a burst. */
            adapter->next_tx_deadline_ns = now_ns + emission_period_ns;
        } else if (!resident || !adapter->resident_seeded) {
            emit_now = now_ns >= adapter->next_tx_deadline_ns;
        }
        if (tdma_pio_spi_ring_adapter_burst_exhausted(adapter)) {
            tx_ok = true; /* Keep completion/RX service alive without TX preparation. */
        } else if (emit_now && adapter->reference_tx_completion_pending) {
            /* The flight-origin callback accepts a launch before DMA/PIO has
             * drained.  Core1 may service the adapter several times during
             * that wire interval; keep the UP leg alive without treating
             * normal physical backpressure as TX failure. */
            tx_ok = true;
        } else if (emit_now) {
            const bool first_emission = adapter->idle_beacon_tx_count == 0u;
            bool launched = false;
            const uint64_t tx_start = tdma_service_timing_now();
            tx_ok = tdma_pio_spi_ring_adapter_tx_beacon(adapter, &launched);
            tdma_service_timing_record(TDMA_TIMING_REFERENCE_TX, tx_start);
            if (tx_ok && launched) {
                /* Advance the absolute phase instead of restarting the
                 * period at the actual (jittered) service time. This avoids
                 * a nominal 2 ms cadence slipping to the third 1 ms RTOS
                 * tick after a slightly late service. Missed deadlines are
                 * coalesced: emit at most one frame per core1 service. */
                if (first_emission) {
                    adapter->next_tx_deadline_ns =
                        now_ns + emission_period_ns;
                } else {
                    do {
                        adapter->next_tx_deadline_ns += emission_period_ns;
                    } while (adapter->next_tx_deadline_ns <= now_ns &&
                             adapter->next_tx_deadline_ns >=
                                 emission_period_ns);
                }
            }
        } else {
            tx_ok = true; /* throttled round: keep the UP leg running. */
            /* The reference hot path does not publish maintenance: the ring
             * admits one TX frame per service and the maintenance transport is
             * not admitted into that slot. */
        }
        rx_ok = tdma_pio_spi_ring_adapter_rx_poll(adapter, now_ns);
        /* The owner hands over a completed bootstrap at this boundary,
         * before any legacy next-cycle or stale-cycle TX can launch. */
        if (tdma_ring_diagnostic_burst_limit(adapter->config.flags) == 0u &&
            resident && adapter->resident_seeded && adapter->resident_return_ready &&
            adapter->comm_fsm.state == TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY &&
            !adapter->reference_tx_completion_pending && !adapter->pending_tx_evidence &&
            adapter->phys_origin.admit != NULL &&
            tdma_pio_spi_ring_origin_seed_valid(adapter)) {
            uint32_t rearm_ticks = 0u, abort_polls = 0u;
            const tdma_origin_admission_result_t admission = adapter->phys_origin.admit(
                adapter->phys_ctrl_context, &adapter->config, &rearm_ticks, &abort_polls);
            if (admission != TDMA_ORIGIN_ADMISSION_NONE) {
                const bool preparing = admission == TDMA_ORIGIN_ADMISSION_READY &&
                    tdma_pio_spi_ring_adapter_start_origin_impl(adapter, rearm_ticks, abort_polls);
                if (!preparing) {
                    adapter->origin.active = 1u; /* STOP owns retirement, including rejected handoff */
                    tdma_pio_spi_ring_origin_invalidate_time(adapter);
                    (void)tdma_adapter_comm_fsm_dispatch(&adapter->comm_fsm,
                        TDMA_ADAPTER_COMM_EVENT_ERROR, TDMA_ADAPTER_COMM_ERROR_RUNTIME);
                    tdma_pio_spi_ring_adapter_set_error(adapter, TDMA_PIO_SPI_RING_ADAPTER_ERROR_COMM_FSM);
                }
                status->up_running = status->down_running = 0u;
                tdma_pio_spi_ring_adapter_publish_status(adapter, status);
                return preparing; /* Poll starts on a later service, under one writer guard. */
            }
        }
        if (!tdma_pio_spi_ring_adapter_burst_exhausted(adapter) &&
            resident && adapter->resident_seeded &&
            adapter->comm_fsm.completed_window_count == 0u &&
            !adapter->resident_return_ready &&
            !adapter->reference_tx_completion_pending &&
            adapter->resident_bootstrap_retry_count <
                TDMA_PIO_SPI_RING_RESIDENT_BOOTSTRAP_RETRY_MAX &&
            now_ns >= adapter->next_tx_deadline_ns) {
            bool launched = false;
            const uint64_t tx_start = tdma_service_timing_now();
            tx_ok = tdma_pio_spi_ring_adapter_tx_bootstrap_retry(
                adapter, &launched);
            tdma_service_timing_record(TDMA_TIMING_REFERENCE_TX, tx_start);
            if (tx_ok && launched) {
                adapter->next_tx_deadline_ns = now_ns + emission_period_ns;
            }
        }
        if (!tdma_pio_spi_ring_adapter_burst_exhausted(adapter) &&
            resident && adapter->resident_seeded &&
            adapter->resident_return_ready &&
            adapter->comm_fsm.state ==
                TDMA_ADAPTER_COMM_STATE_CYCLE_BOUNDARY &&
            now_ns >= adapter->next_tx_deadline_ns) {
            bool launched = false;
            const uint64_t tx_start = tdma_service_timing_now();
            tx_ok = tdma_pio_spi_ring_adapter_tx_next_resident_cycle(
                adapter, &launched);
            tdma_service_timing_record(TDMA_TIMING_REFERENCE_TX, tx_start);
            if (tx_ok && launched) {
                adapter->next_tx_deadline_ns = now_ns + emission_period_ns;
            }
        }
        if (!tdma_pio_spi_ring_adapter_burst_exhausted(adapter) &&
            resident && adapter->resident_seeded &&
            !adapter->resident_return_ready &&
            !adapter->reference_tx_completion_pending &&
            (adapter->comm_fsm.completed_window_count != 0u ||
             adapter->resident_bootstrap_retry_count >=
                  TDMA_PIO_SPI_RING_RESIDENT_BOOTSTRAP_RETRY_MAX) &&
            now_ns >= adapter->next_tx_deadline_ns) {
            bool launched = false;
            const uint64_t tx_start = tdma_service_timing_now();
            tx_ok = tdma_pio_spi_ring_adapter_tx_stale_resident_cycle(
                adapter, &launched);
            tdma_service_timing_record(TDMA_TIMING_REFERENCE_TX, tx_start);
            if (tx_ok && launched) {
                adapter->next_tx_deadline_ns = now_ns + emission_period_ns;
            }
        }
    } else {
        /* Follower node (half-duplex ring): receives the frame from the
         * previous board on the uplink RX leg and re-emits it toward the next
         * board on the downlink TX leg, keeping origin/sequence/identity CRC
         * unchanged and advancing hop/transport CRC. When nothing arrived
         * this round there is nothing to forward and nothing is emitted: a
         * foreign placeholder beacon would race the reference frame around
         * the ring and corrupt the reference's feedback correlation. The TX
         * leg stays ready (up_running=1). */
        if (adapter->forwarding_mode ==
                TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_FLIGHT ||
            adapter->forwarding_mode ==
                TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE) {
            /* CS/SCK/DATA forwarding has already happened in PIO by the time
             * the complete captured frame reaches this parser. Re-emitting
             * here would create a second frame and destroy cut-through. */
            rx_ok = tdma_pio_spi_ring_adapter_rx_poll(adapter, now_ns);
            tx_ok = true;
            bool process_overlay_ok = true;
            if (adapter->forwarding_mode ==
                        TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE &&
                    (rx_ok || adapter->overlay_preparation != NULL) &&
                    !tdma_pio_spi_ring_adapter_prepare_process_overlay(
                        adapter)) {
                    process_overlay_ok = false;
            }
            if (rx_ok) {
                adapter->up_sequence = adapter->down_rx_sequence;
                adapter->up_tx_frame_crc32 = adapter->down_rx_frame_crc32;
                adapter->forward_count++;
                adapter->tx_count++;
            }
            if (adapter->forwarding_mode ==
                    TDMA_PIO_SPI_RING_FORWARDING_PHYSICAL_PROCESS_IMAGE &&
                !tdma_pio_spi_ring_adapter_service_overlay_boundary(adapter)) {
                process_overlay_ok = false;
            }
            if (!process_overlay_ok) {
                return false;
            }
        } else {
            tx_ok = tdma_pio_spi_ring_adapter_forward_poll(adapter, &rx_ok);
        }
    }
    if (!tx_ok) {
        return false;
    }
    if (rx_ok) {
        /* process_rx owns the capture service time. A delayed worker result
         * must not extend receive freshness to its later acceptance phase. */
    } else if (adapter->receive_health.configured != 0u) {
        tdma_receive_health_observe_missing(&adapter->receive_health, now_ns);
    }

    status->up_running = 1u;
    if (rx_ok) {
        status->down_running = 1u;
    } else if (adapter->last_rx_service_ns != 0ull &&
               adapter->last_error !=
                   TDMA_PIO_SPI_RING_ADAPTER_ERROR_RX_BAD_FRAME) {
        /* Same rule as the receive-health window: "four opportunities" is
         * measured in the cadence the link actually runs at, and clock-evidence
         * mode halves it (measured 200 frames/s without it, ~150 with it). */
        const uint64_t freshness_ns =
            adapter->config.feedback_timeout_ns != 0u
                ? (uint64_t)adapter->config.feedback_timeout_ns *
                      (adapter->clock_evidence_enabled != 0u ? 8ull : 4ull)
                : 4000000ull;
        status->down_running =
            (now_ns >= adapter->last_rx_service_ns &&
             now_ns - adapter->last_rx_service_ns <= freshness_ns)
                ? 1u
                : 0u;
    } else {
        status->down_running = 0u;
    }
    tdma_pio_spi_ring_adapter_publish_status(adapter, status);
    return true;
}

static bool tdma_pio_spi_ring_adapter_service(
    void *context,
    uint64_t now_ns,
    tdma_ring_adapter_status_t *status)
{
    tdma_pio_spi_ring_adapter_t *adapter =
        (tdma_pio_spi_ring_adapter_t *)context;
    if (adapter == NULL) {
        return false;
    }
    const uint64_t started = tdma_service_timing_now();
    tdma_pio_spi_ring_adapter_snapshot_write_begin(adapter);
    const bool result = tdma_pio_spi_ring_adapter_service_impl(context,
                                                               now_ns,
                                                               status);
    if (result && adapter->maintenance_service != NULL) {
        (void)adapter->maintenance_service(
            adapter->maintenance_service_context, now_ns);
    }
    tdma_pio_spi_ring_adapter_snapshot_write_end(adapter);
    tdma_service_timing_record(TDMA_TIMING_ADAPTER, started);
    return result;
}

const tdma_ring_adapter_ops_t s_tdma_pio_spi_ring_adapter_ops = {
    .start = tdma_pio_spi_ring_adapter_start,
    .last_error = tdma_pio_spi_ring_adapter_last_error,
    .stop = tdma_pio_spi_ring_adapter_stop,
    .train_clock = tdma_pio_spi_ring_adapter_train_clock,
    .train_clock_service = tdma_pio_spi_ring_adapter_train_clock_service,
    .service = tdma_pio_spi_ring_adapter_service,
};

const tdma_ring_adapter_ops_t *tdma_pio_spi_ring_adapter_ops(void)
{
    return &s_tdma_pio_spi_ring_adapter_ops;
}

bool tdma_pio_spi_ring_adapter_try_get_snapshot(
    const tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_adapter_snapshot_t *snapshot)
{
    if (adapter == NULL || snapshot == NULL) {
        return false;
    }
    do {
        const uint32_t sequence_begin =
            __atomic_load_n(&adapter->snapshot_guard, __ATOMIC_ACQUIRE);
        if ((sequence_begin & 1u) != 0u) {
            return false;
        }
        memset(snapshot, 0, sizeof(*snapshot));
        snapshot->version = TDMA_PIO_SPI_RING_ADAPTER_VERSION;
        snapshot->started = adapter->started;
        snapshot->service_count = adapter->service_count;
        snapshot->role = (uint32_t)adapter->role;
        snapshot->forwarding_mode = (uint32_t)adapter->forwarding_mode;
        const tdma_seat_rotation_shares_t shares = tdma_seat_rotation_unpack(
            __atomic_load_n(&adapter->seat_rotation_packed, __ATOMIC_ACQUIRE));
        snapshot->seat_rotation_priority_share =
            tdma_seat_rotation_priority(shares.priority);
        snapshot->seat_rotation_first_class_share =
            tdma_seat_rotation_first_class(shares.first_class);
        /* Reserved slots for the path that does not use the fast channel: reported as
         * configured (0 = the whole cycle belongs to the two fast seats, i.e. the
         * pre-three-share behaviour). */
        snapshot->seat_rotation_second_class_share = shares.second_class;
        snapshot->first_class_stage_refused_count = adapter->first_class != NULL
            ? __atomic_load_n(&adapter->first_class->stage_refused_count,
                              __ATOMIC_RELAXED) : 0u;
        snapshot->first_class_retired_waiting_count =
            adapter->first_class != NULL
            ? __atomic_load_n(&adapter->first_class->retired_waiting_count,
                              __ATOMIC_RELAXED) : 0u;
        snapshot->forward_count = adapter->forward_count;
        snapshot->up_sequence = adapter->up_sequence;
        snapshot->origin = adapter->origin;
        snapshot->down_rx_sequence = adapter->down_rx_sequence;
        snapshot->up_tx_frame_crc32 = adapter->up_tx_frame_crc32;
        snapshot->down_rx_frame_crc32 = adapter->down_rx_frame_crc32;
        snapshot->idle_beacon_tx_count = adapter->idle_beacon_tx_count;
        snapshot->idle_beacon_rx_count = adapter->idle_beacon_rx_count;
        snapshot->tx_count = adapter->tx_count;
        snapshot->diagnostic_burst_limit =
            tdma_ring_diagnostic_burst_limit(adapter->config.flags);
        snapshot->diagnostic_burst_launched = adapter->diagnostic_burst_launched;
        snapshot->diagnostic_burst_valid = adapter->diagnostic_burst_valid;
        snapshot->rx_count = adapter->rx_count;
        snapshot->rx_bad_count = adapter->rx_bad_count;
        snapshot->rx_transport_bad_count = adapter->rx_transport_bad_count;
        snapshot->rx_schedule_bad_count = adapter->rx_schedule_bad_count;
        snapshot->rx_profile_bad_count = adapter->rx_profile_bad_count;
        snapshot->last_bad_transport_result =
            adapter->last_bad_transport_result;
        snapshot->last_bad_sequence = adapter->last_bad_sequence;
        snapshot->last_bad_schedule_crc32 =
            adapter->last_bad_schedule_crc32;
        snapshot->last_bad_profile_crc32 = adapter->last_bad_profile_crc32;
        snapshot->last_bad_header_diff_count =
            adapter->last_bad_header_diff_count;
        snapshot->last_bad_header_first_diff_offset =
            adapter->last_bad_header_first_diff_offset;
        snapshot->last_bad_header_expected_byte =
            adapter->last_bad_header_expected_byte;
        snapshot->last_bad_header_observed_byte =
            adapter->last_bad_header_observed_byte;
        snapshot->last_bad_packet_diff_count =
            adapter->last_bad_packet_diff_count;
        snapshot->last_bad_packet_first_diff_offset =
            adapter->last_bad_packet_first_diff_offset;
        snapshot->last_bad_packet_expected_byte =
            adapter->last_bad_packet_expected_byte;
        snapshot->last_bad_packet_observed_byte =
            adapter->last_bad_packet_observed_byte;
        snapshot->last_bad_clock_evidence = adapter->last_bad_clock_evidence;
        snapshot->last_bad_expected_transport_crc32 =
            adapter->last_bad_expected_transport_crc32;
        snapshot->last_bad_observed_transport_crc32 =
            adapter->last_bad_observed_transport_crc32;
        snapshot->last_bad_recomputed_transport_crc32 =
            adapter->last_bad_recomputed_transport_crc32;
        snapshot->last_bad_expected_payload_crc32 =
            adapter->last_bad_expected_payload_crc32;
        snapshot->last_bad_observed_payload_crc32 =
            adapter->last_bad_observed_payload_crc32;
        snapshot->clock_evidence_enabled = adapter->clock_evidence_enabled;
        snapshot->rx_drop_count = adapter->rx_drop_count;
        snapshot->timestamp_resolution_ns =
            adapter->feedback_reference_sequence != 0u
                ? adapter->feedback_timestamp_resolution_ns
                : adapter->timestamp_resolution_ns;
        snapshot->timestamp_flags =
            adapter->feedback_reference_sequence != 0u
                ? adapter->feedback_timestamp_flags
                : adapter->timestamp_flags;
        snapshot->feedback_reference_sequence =
            adapter->feedback_reference_sequence;
        snapshot->feedback_reference_frame_crc32 =
            adapter->feedback_reference_frame_crc32;
        snapshot->reference_tx_timestamp_ns = adapter->reference_tx_timestamp_ns;
        snapshot->feedback_rx_timestamp_ns = adapter->feedback_rx_timestamp_ns;
        snapshot->last_rx_service_ns = adapter->last_rx_service_ns;
        snapshot->last_service_ns = adapter->last_service_ns;
        snapshot->clock_observation = adapter->clock_observation;
        snapshot->clock_observation_count = adapter->clock_observation_count;
        snapshot->clock_observation_reject_count =
            adapter->clock_observation_reject_count;
        snapshot->clock_observation_last_reject_reason =
            adapter->clock_observation_last_reject_reason;
        snapshot->clock_evidence_build_count =
            adapter->clock_evidence_build_count;
        snapshot->clock_evidence_build_reject_count =
            adapter->clock_evidence_build_reject_count;
        snapshot->clock_evidence_build_last_reason =
            adapter->clock_evidence_build_last_reason;
        snapshot->clock_evidence_last_tx_encoded =
            adapter->clock_evidence_last_tx_encoded;
        snapshot->clock_evidence_last_rx_encoded =
            adapter->clock_evidence_last_rx_encoded;
        snapshot->pending_tx_evidence = adapter->pending_tx_evidence ? 1u : 0u;
        snapshot->pending_tx_evidence_sequence =
            adapter->pending_tx_evidence_sequence;
        snapshot->last_error = adapter->last_error;
        snapshot->local_slot_id = adapter->config.local_slot_id;
        snapshot->schedule_crc32 = adapter->config.schedule_crc32;
        snapshot->ring_profile_crc32 = adapter->config.ring_profile_crc32;
        snapshot->feedback_timeout_ns = adapter->config.feedback_timeout_ns;
        snapshot->loop_delay_ns = adapter->config.loop_delay_ns;
        snapshot->loop_delay_tolerance_ns =
            adapter->config.loop_delay_tolerance_ns;
        snapshot->rx_ready_timestamp_ns = adapter->rx_ready_timestamp_ns;
        snapshot->last_rx_origin_slot_id = adapter->last_rx_origin_slot_id;
        snapshot->last_rx_hop_count = adapter->last_rx_hop_count;
        snapshot->last_rx_hop_limit = adapter->last_rx_hop_limit;
        snapshot->last_rx_flags = adapter->last_rx_flags;
        snapshot->last_rx_sequence = adapter->last_rx_sequence;
        snapshot->last_rx_identity_crc32 = adapter->last_rx_identity_crc32;
        snapshot->resident_feedback_condition_mask =
            adapter->resident_feedback_condition_mask;
        snapshot->resident_feedback_all_mask =
            TDMA_PIO_SPI_RESIDENT_FEEDBACK_ALL;
        snapshot->comm_fsm_state = (uint32_t)adapter->comm_fsm.state;
        snapshot->resident_seeded = adapter->resident_seeded ? 1u : 0u;
        snapshot->resident_return_ready =
            adapter->resident_return_ready ? 1u : 0u;
        snapshot->resident_bootstrap_retry_count =
            adapter->resident_bootstrap_retry_count;
        snapshot->resident_overlay_bootstrap_prepared =
            adapter->resident_overlay_bootstrap_prepared ? 1u : 0u;
        snapshot->resident_overlay_target_sequence =
            adapter->resident_overlay_target_sequence;
        snapshot->comm_fsm_timed_out_window_count =
            adapter->comm_fsm.timed_out_window_count;
        snapshot->resident_stale_cycle_count =
            adapter->resident_stale_cycle_count;
        snapshot->comm_fsm_window_sequence =
            adapter->comm_fsm.window_sequence;
        snapshot->comm_fsm_completed_window_count =
            adapter->comm_fsm.completed_window_count;
        snapshot->resident_last_completed_cycle =
            adapter->resident_last_completed_cycle;
        snapshot->resident_last_completed_segment_mask =
            adapter->resident_last_completed_segment_mask;
        snapshot->comm_fsm_last_error = adapter->comm_fsm.last_error;
        snapshot->resident_reseed_count = adapter->resident_reseed_count;
        snapshot->resident_last_reseed_reason =
            adapter->resident_last_reseed_reason;
        snapshot->local_tx_edge_timestamp_ns =
            adapter->local_tx_edge_timestamp_ns;
        snapshot->local_tx_edge_count = adapter->local_tx_edge_count;
        snapshot->local_tx_edge_miss_count =
            adapter->local_tx_edge_miss_count;
        snapshot->local_tx_edge_unpaired_count =
            adapter->local_tx_edge_unpaired_count;
        snapshot->local_tx_edge_sequence = adapter->local_tx_edge_sequence;
        snapshot->local_tx_edge_identity_crc32 =
            adapter->local_tx_edge_identity_crc32;
        snapshot->local_tx_edge_capture_generation =
            adapter->local_tx_edge_capture_generation;
        snapshot->local_tx_edge_flags = adapter->local_tx_edge_flags;
        __atomic_thread_fence(__ATOMIC_ACQUIRE);
        const uint32_t sequence_end =
            __atomic_load_n(&adapter->snapshot_guard, __ATOMIC_ACQUIRE);
        if (sequence_begin == sequence_end && (sequence_end & 1u) == 0u) {
            break;
        }
        return false;
    } while (false);
    if (adapter->flight_engine != NULL) {
        tdma_flight_engine_snapshot_t engine_snapshot;
        if (tdma_flight_engine_get_snapshot(adapter->flight_engine,
                                             &engine_snapshot)) {
            snapshot->flight_map_configured = engine_snapshot.configured;
            snapshot->flight_map_active = engine_snapshot.active;
            snapshot->flight_map_crc32 = engine_snapshot.map_crc32;
            snapshot->flight_map_generation = engine_snapshot.map_generation;
            snapshot->flight_map_apply_count = engine_snapshot.map_apply_count;
            snapshot->flight_input_bytes = engine_snapshot.input_bytes;
            snapshot->flight_output_bytes = engine_snapshot.output_bytes;
            snapshot->flight_tx_stale_reuse_count =
                engine_snapshot.tx_stale_reuse_count;
            snapshot->flight_map_reject_count = engine_snapshot.map_reject_count;
            snapshot->flight_length_reject_count =
                engine_snapshot.length_reject_count;
            snapshot->flight_tx_unavailable_count =
                engine_snapshot.tx_unavailable_count;
        }
    }
    (void)tdma_receive_health_get_snapshot(&adapter->receive_health,
                                           adapter->last_service_ns,
                                           &snapshot->receive_health);
    return true;
}

bool tdma_pio_spi_ring_adapter_get_snapshot(
    const tdma_pio_spi_ring_adapter_t *adapter,
    tdma_pio_spi_ring_adapter_snapshot_t *snapshot)
{
    if (adapter == NULL || snapshot == NULL) return false;
    /* Existing Core0 SCPI callers require a complete response. Preserve
     * their retry semantics; realtime/callback consumers must use try_get. */
    while (!tdma_pio_spi_ring_adapter_try_get_snapshot(adapter, snapshot)) {}
    return true;
}

bool tdma_pio_spi_ring_adapter_read_accepted_image(
    const tdma_pio_spi_ring_adapter_t *adapter,
    uint64_t now_ns,
    tdma_receive_image_t *image)
{
    if (adapter == NULL || image == NULL) {
        return false;
    }
    return tdma_receive_health_read_image(&adapter->receive_health,
                                          now_ns,
                                          image);
}
