#include "tdma_pio_spi_phys.h"

#include <string.h>

#include "board_config.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "tdma_pio_spi.pio.h"

#define TDMA_PIO_SPI_DEFAULT_BAUD_HZ 1000000u

/* Fixed frame window: 4-byte packet header + 32-byte TdmaTransportFrame idle
 * beacon. Every ring frame is exactly this long, so the RX DMA completes at a
 * known frame boundary. */
#define TDMA_PIO_SPI_FIXED_RX_WORDS \
    (TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_HEADER_SIZE)

/* Bounded TX FIFO wait: core1 must stay predictable (HAOFV). If the downlink
 * SM stops consuming (e.g. the config plane disarms it while core1 is mid
 * frame), put_blocking would hang core1 forever and break the flash lockout
 * protocol. Instead we wait at most this long per word and fail the frame. */
#define TDMA_PIO_SPI_TX_PUT_TIMEOUT_1E3NS 500u

static bool s_tdma_pio_spi_programs_loaded;
static uint s_tdma_pio_spi_tx_offset;
static uint s_tdma_pio_spi_rx_offset;
static int s_tdma_pio_spi_rx_dma_channel = -1;
static uint32_t s_tdma_pio_spi_rx_dma_words[TDMA_PIO_SPI_RX_DMA_WORD_MAX];

static void tdma_pio_spi_phys_set_error(tdma_pio_spi_phys_t *phys,
                                        uint32_t error)
{
    if (phys == NULL) {
        return;
    }
    phys->snapshot.last_error = error;
    if (error == TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET ||
        error == TDMA_PIO_SPI_PHYS_ERROR_PAYLOAD_TOO_LARGE) {
        phys->snapshot.rx_bad_count++;
    }
}

static bool tdma_pio_spi_phys_ensure_programs(void)
{
    if (s_tdma_pio_spi_programs_loaded) {
        return true;
    }
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_tx_byte_program) ||
        !pio_can_add_program(BOARD_TDMA_SPI_PIO,
                             &tdma_pio_spi_rx_byte_program)) {
        return false;
    }
    s_tdma_pio_spi_tx_offset =
        (uint)pio_add_program(BOARD_TDMA_SPI_PIO,
                              &tdma_pio_spi_tx_byte_program);
    s_tdma_pio_spi_rx_offset =
        (uint)pio_add_program(BOARD_TDMA_SPI_PIO,
                              &tdma_pio_spi_rx_byte_program);
    s_tdma_pio_spi_programs_loaded = true;
    return true;
}

static void tdma_pio_spi_phys_fill_static_snapshot(tdma_pio_spi_phys_t *phys)
{
    phys->snapshot.armed = phys->armed ? 1u : 0u;
    phys->snapshot.role = phys->role;
    phys->snapshot.baud_hz = phys->baud_hz;
    phys->snapshot.tx_sck_pin = phys->tx_sck_pin;
    phys->snapshot.tx_pin = phys->tx_pin;
    phys->snapshot.rx_sck_pin = phys->rx_sck_pin;
    phys->snapshot.rx_pin = phys->rx_pin;
}

static uint64_t tdma_pio_spi_phys_now_1e3ns(void)
{
    return to_us_since_boot(get_absolute_time());
}

static uint32_t tdma_pio_spi_phys_dma_remaining(void)
{
    return dma_channel_hw_addr((uint)s_tdma_pio_spi_rx_dma_channel)->transfer_count;
}

static bool tdma_pio_spi_phys_ensure_rx_dma(void)
{
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        return true;
    }
    s_tdma_pio_spi_rx_dma_channel = dma_claim_unused_channel(false);
    return s_tdma_pio_spi_rx_dma_channel >= 0;
}

static void tdma_pio_spi_phys_rx_prepare(tdma_pio_spi_phys_t *phys)
{
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, true);
}

/* Half-duplex ring: the pin set is symmetric across boards (measured wiring,
 * see board_config.h), so every ring node uses the same downlink TX leg
 * (SCK=24, TX=23) and uplink RX leg (SCK=19, RX=18). */
static void tdma_pio_spi_phys_configure(tdma_pio_spi_phys_t *phys)
{
    phys->tx_sm = BOARD_TDMA_SPI_MASTER_SM;
    phys->tx_pin = BOARD_TDMA_SPI_DOWNLINK_TX_PIN;
    phys->tx_sck_pin = BOARD_TDMA_SPI_DOWNLINK_SCK_PIN;
    phys->rx_sm = BOARD_TDMA_SPI_SLAVE_SM;
    phys->rx_pin = BOARD_TDMA_SPI_UPLINK_RX_PIN;
    phys->rx_sck_pin = BOARD_TDMA_SPI_UPLINK_SCK_PIN;

    tdma_pio_spi_tx_byte_program_init(BOARD_TDMA_SPI_PIO,
                                      phys->tx_sm,
                                      s_tdma_pio_spi_tx_offset,
                                      phys->tx_pin,
                                      phys->tx_sck_pin,
                                      phys->baud_hz);
    tdma_pio_spi_rx_byte_program_init(BOARD_TDMA_SPI_PIO,
                                      phys->rx_sm,
                                      s_tdma_pio_spi_rx_offset,
                                      phys->rx_pin,
                                      UINT32_MAX, /* no CS pin on the ring */
                                      phys->rx_sck_pin);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, true);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, true);
}

/* Non-blocking RX: arm the DMA once, then pick up a complete fixed-size frame
 * when it has arrived. The core1 TDMA service is never stalled. */
static bool tdma_pio_spi_phys_rx_arm(tdma_pio_spi_phys_t *phys,
                                     size_t max_words)
{
    if (phys == NULL || max_words == 0u ||
        max_words > TDMA_PIO_SPI_RX_DMA_WORD_MAX ||
        !tdma_pio_spi_phys_ensure_rx_dma()) {
        return false;
    }
    if (phys->rx_capture_active) {
        return true; /* already armed for this capture window. */
    }

    dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    tdma_pio_spi_phys_rx_prepare(phys);

    dma_channel_config dma_cfg =
        dma_channel_get_default_config((uint)s_tdma_pio_spi_rx_dma_channel);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, false);
    channel_config_set_write_increment(&dma_cfg, true);
    channel_config_set_dreq(&dma_cfg, DREQ_PIO0_RX0 + phys->rx_sm);
    dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel,
                          &dma_cfg,
                          s_tdma_pio_spi_rx_dma_words,
                          &BOARD_TDMA_SPI_PIO->rxf[phys->rx_sm],
                          max_words,
                          true);

    phys->rx_capture_active = true;
    phys->rx_capture_max_words = max_words;
    phys->rx_capture_last_remaining = (uint32_t)max_words;
    phys->rx_capture_last_change_1e3ns = tdma_pio_spi_phys_now_1e3ns();
    return true;
}

static bool tdma_pio_spi_phys_capture_words(tdma_pio_spi_phys_t *phys,
                                            size_t max_words,
                                            size_t *received_words)
{
    if (received_words != NULL) {
        *received_words = 0u;
    }
    if (phys == NULL || received_words == NULL ||
        max_words == 0u ||
        max_words > TDMA_PIO_SPI_RX_DMA_WORD_MAX) {
        return false;
    }
    if (!phys->rx_capture_active) {
        /* No capture window open yet: arm it. The frame is not ready until a
         * later service round. */
        tdma_pio_spi_phys_rx_arm(phys, max_words);
        return false;
    }

    const bool dma_done =
        !dma_channel_is_busy((uint)s_tdma_pio_spi_rx_dma_channel);
    const uint32_t remaining = tdma_pio_spi_phys_dma_remaining();
    const size_t moved = max_words - (size_t)remaining;

    if (moved == 0u) {
        if (dma_done) {
            phys->rx_capture_active = false;
        }
        return false;
    }

    if (moved != (size_t)phys->rx_capture_last_remaining) {
        phys->rx_capture_last_remaining = (uint32_t)remaining;
        phys->rx_capture_last_change_1e3ns = tdma_pio_spi_phys_now_1e3ns();
    }

    const bool frame_ready = dma_done ||
        (tdma_pio_spi_phys_now_1e3ns() - phys->rx_capture_last_change_1e3ns >=
         TDMA_PIO_SPI_RX_STABLE_1E3NS);
    if (!frame_ready) {
        return false;
    }

    if (!dma_done) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    phys->rx_capture_active = false;
    /* Re-arm immediately so the next frame is captured with no dead window:
     * the rx_byte SM pushes one word per byte into the RX FIFO; without a
     * running DMA the FIFO fills (8 deep) and the SM stalls, which corrupts
     * or drops the next frame. The next frame cannot arrive before this
     * re-arm completes (frame interval >> service period), so the FIFO reset
     * in rx_prepare cannot lose an in-flight frame. */
    tdma_pio_spi_phys_rx_arm(phys, max_words);
    *received_words = moved;
    return true;
}

bool tdma_pio_spi_phys_arm(void *context,
                           const tdma_ring_runtime_config_t *config)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || config == NULL || config->enabled == 0u ||
        config->node_count < 2u ||
        config->local_slot_id >= config->node_count) {
        return false;
    }
    if (!tdma_pio_spi_phys_ensure_programs()) {
        return false;
    }

    phys->role = (config->local_slot_id == config->reference_slot_id)
                     ? TDMA_PIO_SPI_ROLE_MASTER
                     : TDMA_PIO_SPI_ROLE_SLAVE;
    phys->baud_hz = TDMA_PIO_SPI_DEFAULT_BAUD_HZ;
    tdma_pio_spi_phys_configure(phys);

    phys->armed = true;
    phys->rx_capture_active = false;
    phys->snapshot.tx_count = 0u;
    phys->snapshot.rx_count = 0u;
    phys->snapshot.rx_bad_count = 0u;
    phys->snapshot.tx_busy_count = 0u;
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    tdma_pio_spi_phys_fill_static_snapshot(phys);
    return true;
}

void tdma_pio_spi_phys_disarm(void *context)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || !phys->armed) {
        return;
    }
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    gpio_set_function(phys->tx_sck_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->tx_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->rx_sck_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->rx_pin, GPIO_FUNC_SIO);
    gpio_set_dir(phys->tx_sck_pin, GPIO_IN);
    gpio_set_dir(phys->tx_pin, GPIO_IN);
    gpio_set_dir(phys->rx_sck_pin, GPIO_IN);
    gpio_set_dir(phys->rx_pin, GPIO_IN);
    phys->armed = false;
    phys->rx_capture_active = false;
    tdma_pio_spi_phys_fill_static_snapshot(phys);
}

static bool tdma_pio_spi_phys_tx_put(tdma_pio_spi_phys_t *phys,
                                     uint32_t word)
{
    const uint64_t deadline_1e3ns =
        tdma_pio_spi_phys_now_1e3ns() + TDMA_PIO_SPI_TX_PUT_TIMEOUT_1E3NS;
    while (pio_sm_is_tx_fifo_full(BOARD_TDMA_SPI_PIO, phys->tx_sm)) {
        if (tdma_pio_spi_phys_now_1e3ns() >= deadline_1e3ns) {
            return false; /* SM stopped: do not hang core1. */
        }
    }
    pio_sm_put(BOARD_TDMA_SPI_PIO, phys->tx_sm, word);
    return true;
}

bool tdma_pio_spi_phys_tx(void *context,
                          const uint8_t *packet,
                          size_t packet_size,
                          uint64_t *tx_timestamp_ns)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (phys == NULL || packet == NULL || packet_size == 0u ||
        packet_size > TDMA_TRANSPORT_SHORT_PACKET_MAX || !phys->armed) {
        if (phys != NULL) {
            tdma_pio_spi_phys_set_error(phys,
                                        TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT);
        }
        return false;
    }
    if (!pio_sm_is_tx_fifo_empty(BOARD_TDMA_SPI_PIO, phys->tx_sm)) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
        phys->snapshot.tx_busy_count++;
        return false;
    }

    uint8_t header[TDMA_PIO_SPI_PACKET_HEADER_SIZE] = {
        TDMA_PIO_SPI_PACKET_MAGIC0,
        TDMA_PIO_SPI_PACKET_MAGIC1,
        (uint8_t)(packet_size & 0xFFu),
        (uint8_t)(packet_size >> 8u),
    };
    for (uint32_t i = 0u; i < TDMA_PIO_SPI_PACKET_HEADER_SIZE; i++) {
        if (!tdma_pio_spi_phys_tx_put(phys, ((uint32_t)header[i]) << 24u)) {
            tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
            phys->snapshot.tx_busy_count++;
            return false;
        }
    }
    for (size_t i = 0u; i < packet_size; i++) {
        if (!tdma_pio_spi_phys_tx_put(phys, ((uint32_t)packet[i]) << 24u)) {
            tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
            phys->snapshot.tx_busy_count++;
            return false;
        }
    }

    phys->snapshot.tx_count++;
    phys->snapshot.last_tx_size = (uint32_t)packet_size;
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    if (tx_timestamp_ns != NULL) {
        *tx_timestamp_ns = 0ull;
    }
    tdma_pio_spi_phys_fill_static_snapshot(phys);
    return true;
}

bool tdma_pio_spi_phys_rx(void *context,
                          uint8_t *packet,
                          size_t packet_capacity,
                          size_t *packet_size,
                          uint64_t *rx_timestamp_ns)
{
    tdma_pio_spi_phys_t *phys = (tdma_pio_spi_phys_t *)context;
    if (packet_size != NULL) {
        *packet_size = 0u;
    }
    if (phys == NULL || packet == NULL || packet_capacity == 0u ||
        packet_size == NULL || rx_timestamp_ns == NULL || !phys->armed) {
        if (phys != NULL) {
            tdma_pio_spi_phys_set_error(phys,
                                        TDMA_PIO_SPI_PHYS_ERROR_BAD_ARGUMENT);
        }
        return false;
    }

    size_t received_words = 0u;
    if (!tdma_pio_spi_phys_capture_words(phys,
                                         TDMA_PIO_SPI_FIXED_RX_WORDS,
                                         &received_words)) {
        return false;
    }
    if (received_words < TDMA_PIO_SPI_PACKET_HEADER_SIZE) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET);
        return false;
    }

    uint8_t header[TDMA_PIO_SPI_PACKET_HEADER_SIZE];
    for (uint32_t i = 0u; i < TDMA_PIO_SPI_PACKET_HEADER_SIZE; i++) {
        header[i] = (uint8_t)(s_tdma_pio_spi_rx_dma_words[i] & 0xFFu);
    }
    const uint16_t frame_size =
        (uint16_t)header[2] | ((uint16_t)header[3] << 8u);
    if (header[0] != TDMA_PIO_SPI_PACKET_MAGIC0 ||
        header[1] != TDMA_PIO_SPI_PACKET_MAGIC1 ||
        frame_size == 0u) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET);
        return false;
    }
    if ((size_t)frame_size > packet_capacity) {
        tdma_pio_spi_phys_set_error(phys,
                                    TDMA_PIO_SPI_PHYS_ERROR_PAYLOAD_TOO_LARGE);
        return false;
    }
    if (received_words <
        (size_t)frame_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE) {
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET);
        return false;
    }

    for (uint16_t i = 0u; i < frame_size; i++) {
        packet[i] =
            (uint8_t)(s_tdma_pio_spi_rx_dma_words[TDMA_PIO_SPI_PACKET_HEADER_SIZE +
                                                  i] &
                      0xFFu);
    }
    *packet_size = frame_size;
    /* Software timestamp first (bring-up diagnostic): PIO/DMA hardware latch
     * is a later P0.5-5 step. */
    *rx_timestamp_ns = (uint64_t)to_us_since_boot(get_absolute_time()) * 1000ull;

    phys->snapshot.rx_count++;
    phys->snapshot.last_rx_size = frame_size;
    phys->snapshot.last_rx_timestamp_ns = *rx_timestamp_ns;
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    tdma_pio_spi_phys_fill_static_snapshot(phys);
    return true;
}

bool tdma_pio_spi_phys_get_snapshot(const tdma_pio_spi_phys_t *phys,
                                    tdma_pio_spi_phys_snapshot_t *snapshot)
{
    if (phys == NULL || snapshot == NULL) {
        return false;
    }
    *snapshot = phys->snapshot;
    return true;
}
