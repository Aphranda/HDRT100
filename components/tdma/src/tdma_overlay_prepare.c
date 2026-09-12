#include "tdma_overlay_prepare.h"
#include "tdma_profile.h"

#include <string.h>

uint32_t tdma_overlay_prepare_state(const tdma_overlay_prepare_t *job)
{
    return job == NULL ? TDMA_OVERLAY_PREPARE_IDLE :
        __atomic_load_n(&job->state, __ATOMIC_ACQUIRE);
}

bool tdma_overlay_prepare_request(tdma_overlay_prepare_t *job)
{
    if (job == NULL || job->plan == NULL ||
        tdma_overlay_prepare_state(job) != TDMA_OVERLAY_PREPARE_IDLE) return false;
    job->request_epoch = job->epoch;
    /* Never retain a FIFO source pointer beyond this Core1 publication. */
    job->tx.data = job->tx.data_size != 0u ? job->tx_data : NULL;
    __atomic_store_n(&job->state, TDMA_OVERLAY_PREPARE_REQUESTED, __ATOMIC_RELEASE);
    return true;
}

bool tdma_overlay_prepare_core0_claim(tdma_overlay_prepare_t *job)
{
    if (job == NULL) return false;
    uint32_t expected = TDMA_OVERLAY_PREPARE_REQUESTED;
    return __atomic_compare_exchange_n(&job->state, &expected,
        TDMA_OVERLAY_PREPARE_BUILDING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static bool tdma_overlay_prepare_build(tdma_overlay_prepare_t *job)
{
    tdma_transport_frame_view_t view;
    tdma_transport_result_t result;
    uint8_t incoming[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    uint8_t processed[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    memcpy(incoming, job->packet, sizeof(incoming));
    memcpy(processed, job->packet, sizeof(processed));
    if (!tdma_transport_frame_decode(job->packet, sizeof(job->packet), &view, &result) ||
        view.payload_class != TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE ||
        (view.flags & TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE) == 0u ||
        !tdma_transport_frame_prepare_resident_position(incoming, sizeof(incoming),
            job->target_sequence, job->ingress_hop, &result) ||
        !tdma_flight_engine_build_tx(&job->layout, view.payload, view.payload_size,
            &job->tx, processed + TDMA_TRANSPORT_FRAME_HEADER_SIZE,
            sizeof(processed) - TDMA_TRANSPORT_FRAME_HEADER_SIZE, &job->applied) ||
        !tdma_transport_frame_prepare_resident_position(processed, sizeof(processed),
            job->target_sequence, job->egress_hop, &result)) return false;
    return tdma_flight_overlay_build_plan(incoming, processed, sizeof(incoming),
        job->applied.output_byte_bitmap, TDMA_FLIGHT_OUTPUT_BITMAP_WORDS,
        &job->config, job->plan);
}

void tdma_overlay_prepare_core0_build_claimed(tdma_overlay_prepare_t *job)
{
    if (job == NULL) return;
    /* CANCELLED may race every instruction of the pure builder. It cannot
     * retire the lease: this worker is the only writer allowed to ACK it. */
    const uint32_t state = tdma_overlay_prepare_state(job);
    if (state != TDMA_OVERLAY_PREPARE_BUILDING &&
        state != TDMA_OVERLAY_PREPARE_CANCELLED) return;
    const bool built = state == TDMA_OVERLAY_PREPARE_BUILDING &&
        tdma_overlay_prepare_build(job);
    uint32_t expected = TDMA_OVERLAY_PREPARE_BUILDING;
    if (!__atomic_compare_exchange_n(&job->state, &expected,
            built ? TDMA_OVERLAY_PREPARE_READY : TDMA_OVERLAY_PREPARE_FAILED,
            false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
        /* No input/output access after this release. A later ARM may reuse
         * both the job and the physical union immediately. */
        __atomic_store_n(&job->state, TDMA_OVERLAY_PREPARE_IDLE, __ATOMIC_RELEASE);
    }
}

void tdma_overlay_prepare_core0_service(tdma_overlay_prepare_t *job)
{
    if (tdma_overlay_prepare_core0_claim(job))
        tdma_overlay_prepare_core0_build_claimed(job);
}

bool tdma_overlay_prepare_cancel(tdma_overlay_prepare_t *job)
{
    if (job == NULL) return true;
    ++job->epoch; /* Core1 only; the worker never reads this mutable epoch. */
    uint32_t state = __atomic_exchange_n(&job->state,
        TDMA_OVERLAY_PREPARE_CANCELLED, __ATOMIC_ACQ_REL);
    if (state == TDMA_OVERLAY_PREPARE_BUILDING ||
        state == TDMA_OVERLAY_PREPARE_CANCELLED) return false;
    __atomic_store_n(&job->state, TDMA_OVERLAY_PREPARE_IDLE, __ATOMIC_RELEASE);
    return true;
}

void tdma_overlay_prepare_release(tdma_overlay_prepare_t *job)
{
    if (job == NULL) return;
    const uint32_t state = tdma_overlay_prepare_state(job);
    if (state == TDMA_OVERLAY_PREPARE_READY || state == TDMA_OVERLAY_PREPARE_FAILED)
        __atomic_store_n(&job->state, TDMA_OVERLAY_PREPARE_IDLE, __ATOMIC_RELEASE);
}
