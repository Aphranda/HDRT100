#include "tdma_rx_prepare.h"
#include "tdma_process_image_layout.h"

#include <string.h>

uint32_t tdma_rx_prepare_state(const tdma_rx_prepare_t *job)
{
    return job == NULL ? TDMA_RX_PREPARE_IDLE :
        __atomic_load_n(&job->state, __ATOMIC_ACQUIRE);
}

bool tdma_rx_prepare_request(tdma_rx_prepare_t *job)
{
    if (job == NULL || tdma_rx_prepare_state(job) != TDMA_RX_PREPARE_IDLE ||
        job->packet_size == 0u || job->packet_size > sizeof(job->packet) ||
        job->expected_size > sizeof(job->expected)) return false;
    job->request_epoch = job->epoch;
    __atomic_store_n(&job->state, TDMA_RX_PREPARE_REQUESTED, __ATOMIC_RELEASE);
    return true;
}

bool tdma_rx_prepare_core0_claim(tdma_rx_prepare_t *job)
{
    if (job == NULL) return false;
    uint32_t expected = TDMA_RX_PREPARE_REQUESTED;
    return __atomic_compare_exchange_n(&job->state, &expected,
        TDMA_RX_PREPARE_BUILDING, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

void tdma_rx_diagnose(const uint8_t *packet, size_t packet_size,
    const tdma_transport_frame_view_t *view, const uint8_t *expected,
    size_t expected_size, bool clock_evidence, tdma_rx_diagnostic_t *d)
{
    memset(d, 0, sizeof(*d));
    d->header_first_diff_offset = d->packet_first_diff_offset = UINT32_MAX;
    if (expected_size == 0u || expected == NULL ||
        packet_size < TDMA_TRANSPORT_FRAME_HEADER_SIZE || view->transport_sequence == 0u)
        return;
    const size_t common_size = expected_size < packet_size ? expected_size : packet_size;
    for (size_t i = 0u; i < common_size; ++i) {
        if (expected[i] == packet[i]) continue;
        if (d->packet_diff_count++ == 0u) {
            d->packet_first_diff_offset = (uint32_t)i;
            d->packet_expected_byte = expected[i];
            d->packet_observed_byte = packet[i];
        }
        if (i < TDMA_TRANSPORT_FRAME_HEADER_SIZE && d->header_diff_count++ == 0u) {
            d->header_first_diff_offset = (uint32_t)i;
            d->header_expected_byte = expected[i];
            d->header_observed_byte = packet[i];
        }
    }
    if (expected_size != packet_size) {
        if (d->packet_diff_count == 0u) d->packet_first_diff_offset = (uint32_t)common_size;
        d->packet_diff_count += (uint32_t)(expected_size > packet_size
            ? expected_size - packet_size : packet_size - expected_size);
    }
    d->clock_evidence = clock_evidence ? 1u : 0u;
    tdma_transport_frame_view_t expected_view;
    tdma_transport_result_t result;
    if (tdma_transport_frame_decode(expected, expected_size, &expected_view, &result)) {
        d->expected_transport_crc32 = expected_view.transport_crc32;
        d->expected_payload_crc32 = tdma_transport_crc32_compute(
            expected_view.payload, expected_view.payload_size);
    }
    d->observed_transport_crc32 = view->transport_crc32;
    d->observed_payload_crc32 = tdma_transport_crc32_compute(view->payload, view->payload_size);
    (void)tdma_transport_frame_calculate_transport_crc32(packet, packet_size,
        &d->recomputed_transport_crc32);
}

static bool tdma_rx_prepare_origin_mailboxes(const tdma_rx_prepare_t *job)
{
    if (!job->decoded || job->view.payload_size != TDMA_FLIGHT_SHORT_PAYLOAD_SIZE ||
        job->node_count < 2u || job->node_count > TDMA_FLIGHT_SHORT_SLOT_COUNT) return false;
    const uint32_t mask = (1u << job->node_count) - 1u;
    for (uint32_t slot = 0u; slot < job->node_count; ++slot) {
        const uint8_t *p = job->view.payload + slot * TDMA_FLIGHT_SHORT_SLOT_SIZE;
        if (((uint32_t)p[0] | ((uint32_t)p[1] << 8u)) != TDMA_FLIGHT_MAILBOX_MAGIC ||
            p[TDMA_FLIGHT_MAILBOX_VERSION_OFFSET] != TDMA_FLIGHT_MAILBOX_VERSION ||
            p[3] != TDMA_PROCESS_IMAGE_MESSAGE_CLASS ||
            p[TDMA_FLIGHT_MAILBOX_SOURCE_SLOT_OFFSET] != slot ||
            (p[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET] & ~mask) != 0u ||
            tdma_process_image_crc16_ccitt(p, TDMA_PROCESS_IMAGE_CRC_OFFSET) !=
                ((uint32_t)p[TDMA_PROCESS_IMAGE_CRC_OFFSET] |
                 ((uint32_t)p[TDMA_PROCESS_IMAGE_CRC_OFFSET + 1u] << 8u))) return false;
    }
    return true;
}

void tdma_rx_prepare_core0_build_claimed(tdma_rx_prepare_t *job)
{
    if (job == NULL) return;
    const uint32_t state = tdma_rx_prepare_state(job);
    if (state != TDMA_RX_PREPARE_BUILDING && state != TDMA_RX_PREPARE_CANCELLED) return;
    if (state == TDMA_RX_PREPARE_BUILDING) {
        job->decoded = tdma_transport_frame_decode(job->packet, job->packet_size,
            &job->view, &job->result);
        job->origin_mailboxes_valid = job->origin_active && tdma_rx_prepare_origin_mailboxes(job);
        if (!job->decoded || job->view.schedule_crc32 != job->schedule_crc32 ||
            job->view.ring_profile_crc32 != job->profile_crc32)
            tdma_rx_diagnose(job->packet, job->packet_size, &job->view,
                job->expected, job->expected_size, job->expected_clock, &job->diagnostic);
    }
    uint32_t expected = TDMA_RX_PREPARE_BUILDING;
    if (!__atomic_compare_exchange_n(&job->state, &expected, TDMA_RX_PREPARE_READY,
            false, __ATOMIC_RELEASE, __ATOMIC_RELAXED)) {
        /* No access after ACK; Core1 may reuse this slot immediately. */
        __atomic_store_n(&job->state, TDMA_RX_PREPARE_IDLE, __ATOMIC_RELEASE);
    }
}

void tdma_rx_prepare_core0_service(tdma_rx_prepare_t *job)
{
    if (tdma_rx_prepare_core0_claim(job)) tdma_rx_prepare_core0_build_claimed(job);
}

bool tdma_rx_prepare_cancel(tdma_rx_prepare_t *job)
{
    if (job == NULL) return true;
    ++job->epoch;
    const uint32_t state = __atomic_exchange_n(&job->state,
        TDMA_RX_PREPARE_CANCELLED, __ATOMIC_ACQ_REL);
    if (state == TDMA_RX_PREPARE_BUILDING || state == TDMA_RX_PREPARE_CANCELLED) return false;
    __atomic_store_n(&job->state, TDMA_RX_PREPARE_IDLE, __ATOMIC_RELEASE);
    return true;
}

void tdma_rx_prepare_release(tdma_rx_prepare_t *job)
{
    if (tdma_rx_prepare_state(job) == TDMA_RX_PREPARE_READY)
        __atomic_store_n(&job->state, TDMA_RX_PREPARE_IDLE, __ATOMIC_RELEASE);
}
