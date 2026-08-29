#include "tdma_pio_spi_phys.h"

#include <string.h>

#include "tdma_pio_spi_phys_internal.h"

bool tdma_pio_spi_phys_get_marker_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_marker_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&phys->marker_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = phys->marker;
        const uint32_t end =
            __atomic_load_n(&phys->marker_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

bool tdma_pio_spi_phys_copy_marker_capture(
    const tdma_pio_spi_phys_t *phys,
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    tdma_pio_spi_marker_snapshot_t snapshot;
    if (capture_word_count != NULL) *capture_word_count = 0u;
    if (phys == NULL || capture_words == NULL || capture_word_count == NULL ||
        !tdma_pio_spi_phys_get_marker_snapshot(phys, &snapshot) ||
        (snapshot.state != TDMA_PIO_SPI_MARKER_COMPLETE &&
         snapshot.state != TDMA_PIO_SPI_MARKER_ERROR) ||
        (snapshot.flags & TDMA_PIO_SPI_MARKER_FLAG_RX_DMA_COMPLETE) == 0u ||
        snapshot.capture_word_count > capture_word_capacity) {
        return false;
    }
    memcpy(capture_words, tdma_pio_spi_phys_marker_rx_buffer(),
           snapshot.capture_word_count * sizeof(capture_words[0]));
    *capture_word_count = snapshot.capture_word_count;
    return true;
}

bool tdma_pio_spi_phys_get_data_train_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_data_train_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&phys->data_train_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = phys->data_train;
        const uint32_t end =
            __atomic_load_n(&phys->data_train_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

bool tdma_pio_spi_phys_copy_data_train_capture(
    const tdma_pio_spi_phys_t *phys,
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    tdma_pio_spi_data_train_snapshot_t snapshot;
    if (capture_word_count != NULL) *capture_word_count = 0u;
    if (phys == NULL || capture_words == NULL || capture_word_count == NULL ||
        !tdma_pio_spi_phys_get_data_train_snapshot(phys, &snapshot) ||
        (snapshot.role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_INITIATOR &&
         snapshot.role != TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION) ||
        (snapshot.state != TDMA_PIO_SPI_DATA_TRAIN_COMPLETE &&
         !(snapshot.state == TDMA_PIO_SPI_DATA_TRAIN_ERROR &&
           snapshot.reject_reason == TDMA_PIO_SPI_DATA_TRAIN_REJECT_DMA &&
           (snapshot.diagnostic_fault_flags &
            TDMA_PIO_SPI_DATA_TRAIN_FAULT_RX_DMA_SHORT) != 0u &&
           snapshot.dma_overrun_count != 0u))) {
        return false;
    }
    const size_t available_words =
        snapshot.state == TDMA_PIO_SPI_DATA_TRAIN_COMPLETE
            ? snapshot.capture_word_count
            : snapshot.capture_word_count - 1u;
    if (available_words == 0u || available_words > capture_word_capacity) {
        return false;
    }
    memcpy(capture_words, tdma_pio_spi_phys_data_train_rx_buffer(),
           available_words * sizeof(capture_words[0]));
    *capture_word_count = available_words;
    return true;
}

bool tdma_pio_spi_phys_get_sck_train_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_data_train_snapshot_t *snapshot)
{
    return tdma_pio_spi_phys_get_data_train_snapshot(phys, snapshot) &&
           (snapshot->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_SOURCE ||
            snapshot->role == TDMA_PIO_SPI_DATA_TRAIN_ROLE_SCK_DESTINATION);
}

bool tdma_pio_spi_phys_copy_sck_train_capture(
    const tdma_pio_spi_phys_t *phys,
    uint32_t *capture_words,
    size_t capture_word_capacity,
    size_t *capture_word_count)
{
    return tdma_pio_spi_phys_copy_data_train_capture(
        phys, capture_words, capture_word_capacity, capture_word_count);
}

bool tdma_pio_spi_phys_get_clk_train_snapshot(
    const tdma_pio_spi_phys_t *phys,
    tdma_pio_spi_clk_train_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u; attempt < 64u; attempt++) {
        const uint32_t begin =
            __atomic_load_n(&phys->clk_train_guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = phys->clk_train;
        const uint32_t end =
            __atomic_load_n(&phys->clk_train_guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}
