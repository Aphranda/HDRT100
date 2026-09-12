#include "tdma_rx_scan.h"
#include <string.h>

uint32_t tdma_rx_scan_state(const tdma_rx_scan_t *job)
{
    return job == NULL ? TDMA_RX_SCAN_IDLE : __atomic_load_n(&job->state, __ATOMIC_ACQUIRE);
}

bool tdma_rx_scan_request(tdma_rx_scan_t *job)
{
    if (job == NULL || tdma_rx_scan_state(job) != TDMA_RX_SCAN_IDLE ||
        job->byte_count > sizeof(job->bytes) || job->byte_count < TDMA_PIO_SPI_PACKET_HEADER_SIZE ||
        job->max_frame_words < TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_HEADER_SIZE ||
        job->max_frame_words > TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_SHORT_PACKET_MAX ||
        job->tail_words > TDMA_PIO_SPI_FLIGHT_MAX_TAIL_BYTES ||
        job->physical_frame_words <= job->tail_words ||
        job->window_start > UINT64_MAX - job->byte_count) return false;
    job->request_epoch = job->epoch;
    __atomic_store_n(&job->state, TDMA_RX_SCAN_REQUESTED, __ATOMIC_RELEASE);
    return true;
}

bool tdma_rx_scan_core0_claim(tdma_rx_scan_t *job)
{
    if (job == NULL) return false;
    uint32_t expected = TDMA_RX_SCAN_REQUESTED;
    return __atomic_compare_exchange_n(&job->state, &expected, TDMA_RX_SCAN_BUILDING,
        false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

static uint8_t aligned(const tdma_rx_scan_t *job, uint32_t offset, uint32_t shift)
{
    return shift == 0u ? job->bytes[offset] :
        (uint8_t)((job->bytes[offset] << shift) | (job->bytes[offset + 1u] >> (8u - shift)));
}

static uint32_t frame_at(const tdma_rx_scan_t *job, uint32_t offset, uint32_t shift)
{
    const uint32_t extra = shift != 0u;
    if (offset > job->byte_count || job->byte_count - offset < TDMA_PIO_SPI_PACKET_HEADER_SIZE + extra ||
        aligned(job, offset, shift) != TDMA_PIO_SPI_PACKET_MAGIC0 ||
        aligned(job, offset + 1u, shift) != TDMA_PIO_SPI_PACKET_MAGIC1) return 0u;
    const uint32_t size = aligned(job, offset + 2u, shift) | ((uint32_t)aligned(job, offset + 3u, shift) << 8u);
    const uint32_t words = size + TDMA_PIO_SPI_PACKET_HEADER_SIZE;
    if (size < TDMA_TRANSPORT_FRAME_HEADER_SIZE || words > job->max_frame_words ||
        words + extra > job->byte_count - offset) return 0u;
    uint8_t packet[TDMA_TRANSPORT_SHORT_PACKET_MAX];
    for (uint32_t i = 0u; i < size; ++i) packet[i] = aligned(job, offset + TDMA_PIO_SPI_PACKET_HEADER_SIZE + i, shift);
    tdma_transport_frame_view_t view;
    tdma_transport_result_t result;
    return tdma_transport_frame_decode(packet, size, &view, &result) ? words : 0u;
}

void tdma_rx_scan_core0_build_claimed(tdma_rx_scan_t *job)
{
    if (job == NULL) return;
    const uint32_t state = tdma_rx_scan_state(job);
    if (state != TDMA_RX_SCAN_BUILDING && state != TDMA_RX_SCAN_CANCELLED) return;
    if (state == TDMA_RX_SCAN_BUILDING) {
        memset(&job->result, 0, sizeof(job->result));
        for (uint32_t offset = 0u; offset < job->byte_count && !job->result.valid; ++offset) {
            for (uint32_t shift = 0u; shift < 8u; ++shift) {
                const uint32_t words = frame_at(job, offset, shift);
                if (words == 0u) continue;
                /* The configured tail is fixed even for a bootstrap beacon. */
                const uint32_t stride = words + job->tail_words;
                job->result = (tdma_rx_scan_hint_t){.valid = true, .bit_shift = shift,
                    .frame_words = words, .stride_words = stride,
                    .candidate = job->window_start + offset, .observation_epoch = job->observation_epoch,
                    .stable_frames = frame_at(job, offset + stride, shift) == words ? 2u : 1u};
                break;
            }
        }
    }
    uint32_t expected = TDMA_RX_SCAN_BUILDING;
    if (!__atomic_compare_exchange_n(&job->state, &expected, TDMA_RX_SCAN_READY,
            false, __ATOMIC_RELEASE, __ATOMIC_RELAXED))
        __atomic_store_n(&job->state, TDMA_RX_SCAN_IDLE, __ATOMIC_RELEASE);
}

void tdma_rx_scan_core0_service(tdma_rx_scan_t *job)
{
    if (tdma_rx_scan_core0_claim(job)) tdma_rx_scan_core0_build_claimed(job);
}

bool tdma_rx_scan_cancel(tdma_rx_scan_t *job)
{
    if (job == NULL) return true;
    ++job->epoch;
    const uint32_t state = __atomic_exchange_n(&job->state, TDMA_RX_SCAN_CANCELLED, __ATOMIC_ACQ_REL);
    if (state == TDMA_RX_SCAN_BUILDING || state == TDMA_RX_SCAN_CANCELLED) return false;
    __atomic_store_n(&job->state, TDMA_RX_SCAN_IDLE, __ATOMIC_RELEASE);
    return true;
}

void tdma_rx_scan_release(tdma_rx_scan_t *job)
{
    if (tdma_rx_scan_state(job) == TDMA_RX_SCAN_READY)
        __atomic_store_n(&job->state, TDMA_RX_SCAN_IDLE, __ATOMIC_RELEASE);
}

bool tdma_rx_scan_locate(const tdma_rx_scan_hint_t *hint, uint64_t produced,
    uint64_t cursor, uint64_t *candidate)
{
    if (hint == NULL || candidate == NULL || !hint->valid || hint->bit_shift > 7u ||
        hint->frame_words < TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_HEADER_SIZE ||
        hint->frame_words > TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_SHORT_PACKET_MAX ||
        hint->stride_words < hint->frame_words ||
        hint->stride_words - hint->frame_words > TDMA_PIO_SPI_FLIGHT_MAX_TAIL_BYTES ||
        produced < hint->candidate) return false;
    uint64_t start = cursor > hint->candidate ? cursor : hint->candidate;
    const uint32_t phase = (uint32_t)(hint->candidate % hint->stride_words);
    const uint32_t current = (uint32_t)(start % hint->stride_words);
    const uint32_t advance = (phase + hint->stride_words - current) % hint->stride_words;
    if (UINT64_MAX - start < advance) return false;
    start += advance;
    if (start > produced || produced - start < hint->frame_words + (hint->bit_shift != 0u)) return false;
    *candidate = start;
    return true;
}
