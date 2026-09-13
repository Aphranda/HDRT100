#include "tdma_overlay_prepare.h"
#include "tdma_profile.h"
#include "tdma_process_image_layout.h"

#include <string.h>

uint32_t tdma_overlay_prepare_state(const tdma_overlay_prepare_t *job)
{
    return job == NULL ? TDMA_OVERLAY_PREPARE_IDLE :
        __atomic_load_n(&job->state, __ATOMIC_ACQUIRE);
}

bool tdma_overlay_prepare_request(tdma_overlay_prepare_t *job)
{
    if (job == NULL || job->plan == NULL ||
        tdma_flight_payload_slots(job->layout.payload_size) == 0u ||
        job->config.packet_size != TDMA_TRANSPORT_FRAME_HEADER_SIZE + job->layout.payload_size ||
        tdma_overlay_prepare_state(job) != TDMA_OVERLAY_PREPARE_IDLE) return false;
    job->request_epoch = job->epoch;
    job->kind = TDMA_OVERLAY_PREPARE_FOLLOWER;
    /* Never retain a FIFO source pointer beyond this Core1 publication. */
    job->tx.data = job->tx.data_size != 0u ? job->tx_data : NULL;
    __atomic_store_n(&job->state, TDMA_OVERLAY_PREPARE_REQUESTED, __ATOMIC_RELEASE);
    return true;
}

bool tdma_overlay_prepare_origin_request(tdma_overlay_prepare_t *job)
{
    if (job == NULL || job->plan != NULL ||
        tdma_overlay_prepare_state(job) != TDMA_OVERLAY_PREPARE_IDLE ||
        job->tx.data_size != sizeof(job->tx_data)) return false;
    job->request_epoch = job->epoch;
    job->kind = TDMA_OVERLAY_PREPARE_ORIGIN;
    job->tx.data = job->tx_data;
    __atomic_store_n(&job->state, TDMA_OVERLAY_PREPARE_REQUESTED, __ATOMIC_RELEASE);
    return true;
}

bool tdma_overlay_prepare_origin_current(const tdma_overlay_prepare_t *job,
                                        uint32_t packet_size, uint32_t local_slot_id)
{
    return job != NULL && tdma_overlay_prepare_state(job) == TDMA_OVERLAY_PREPARE_READY &&
        job->kind == TDMA_OVERLAY_PREPARE_ORIGIN && job->request_epoch == job->epoch &&
        job->plan == NULL && job->config.packet_size == packet_size &&
        job->config.local_slot_id == local_slot_id && job->layout.local_slot_id == local_slot_id &&
        packet_size == TDMA_TRANSPORT_FRAME_HEADER_SIZE + job->layout.payload_size &&
        job->tx.data == job->tx_data && job->tx.data_size == sizeof(job->tx_data);
}

static bool tdma_overlay_prepare_origin_build(tdma_overlay_prepare_t *job)
{
    const uint32_t nodes = tdma_flight_payload_slots(job->layout.payload_size);
    const uint32_t slot = job->layout.local_slot_id;
    if (nodes == 0u || slot >= nodes || job->config.local_slot_id != slot ||
        job->config.packet_size != TDMA_TRANSPORT_FRAME_HEADER_SIZE + job->layout.payload_size ||
        job->plan != NULL || job->tx.data != job->tx_data || job->tx.data_size != sizeof(job->tx_data) ||
        job->tx.generation == 0u || job->tx.segment_mask != (1u << slot) ||
        job->layout.output_segment_mask == 0u) return false;
    const uint8_t *mailbox = job->tx_data;
    if (((uint32_t)mailbox[0] | ((uint32_t)mailbox[1] << 8u)) != TDMA_FLIGHT_MAILBOX_MAGIC ||
        mailbox[TDMA_FLIGHT_MAILBOX_VERSION_OFFSET] != TDMA_FLIGHT_MAILBOX_VERSION ||
        mailbox[3] != TDMA_PROCESS_IMAGE_MESSAGE_CLASS ||
        mailbox[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] != slot ||
        (mailbox[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] & ~((1u << nodes) - 1u)) != 0u ||
        tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET) !=
            ((uint32_t)mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET] |
             ((uint32_t)mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] << 8u))) return false;
    job->applied = (tdma_flight_engine_apply_t){
        .output_segment_mask = job->layout.output_segment_mask,
        .output_bytes = TDMA_FLIGHT_SHORT_SLOT_SIZE,
    };
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
    uint8_t incoming[TDMA_FLIGHT_SHORT_PACKET_SIZE];
    uint8_t processed[TDMA_FLIGHT_SHORT_PACKET_SIZE];
    const size_t packet_size = job->config.packet_size;
    if (packet_size > sizeof(incoming) ||
        packet_size != TDMA_TRANSPORT_FRAME_HEADER_SIZE + job->layout.payload_size) return false;
    memcpy(incoming, job->packet, packet_size);
    memcpy(processed, job->packet, packet_size);
    if (!tdma_transport_frame_decode(job->packet, packet_size, &view, &result) ||
        view.payload_class != TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE ||
        (view.flags & TDMA_TRANSPORT_FLAG_FLIGHT_MUTABLE) == 0u ||
        !tdma_transport_frame_prepare_resident_position(incoming, packet_size,
            job->target_sequence, job->ingress_hop, &result) ||
        !tdma_flight_engine_build_tx(&job->layout, view.payload, view.payload_size,
            &job->tx, processed + TDMA_TRANSPORT_FRAME_HEADER_SIZE,
            sizeof(processed) - TDMA_TRANSPORT_FRAME_HEADER_SIZE, &job->applied) ||
        !tdma_transport_frame_prepare_resident_position(processed, packet_size,
            job->target_sequence, job->egress_hop, &result)) return false;
    return tdma_flight_overlay_build_plan(incoming, processed, packet_size,
        job->applied.output_byte_bitmap, TDMA_FLIGHT_OUTPUT_BITMAP_WORDS,
        &job->config, job->plan) &&
        tdma_flight_overlay_bind_plan(job->plan, job->config.final_bit_pc, &job->binding);
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
        (job->kind == TDMA_OVERLAY_PREPARE_ORIGIN ? tdma_overlay_prepare_origin_build(job) :
         job->kind == TDMA_OVERLAY_PREPARE_FOLLOWER && tdma_overlay_prepare_build(job));
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
