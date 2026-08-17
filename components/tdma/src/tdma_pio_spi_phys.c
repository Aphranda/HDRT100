#include "tdma_pio_spi_phys.h"

#include <string.h>

#include "board_config.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "tdma_pio_spi.pio.h"

#define TDMA_PIO_SPI_DEFAULT_BAUD_HZ 25000000u

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
    if (error == TDMA_PIO_SPI_PHYS_ERROR_TIMEOUT) {
        phys->snapshot.rx_timeout_count++;
    } else if (error == TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET ||
               error == TDMA_PIO_SPI_PHYS_ERROR_PAYLOAD_TOO_LARGE) {
        phys->snapshot.rx_bad_count++;
    }
}

static bool tdma_pio_spi_phys_ensure_programs(void)
{
    if (s_tdma_pio_spi_programs_loaded) {
        return true;
    }
    if (!pio_can_add_program(BOARD_TDMA_SPI_PIO, &tdma_pio_spi_tx_byte_program) ||
        !pio_can_add_program(BOARD_TDMA_SPI_PIO, &tdma_pio_spi_rx_byte_program)) {
        return false;
    }
    s_tdma_pio_spi_tx_offset =
        (uint)pio_add_program(BOARD_TDMA_SPI_PIO, &tdma_pio_spi_tx_byte_program);
    s_tdma_pio_spi_rx_offset =
        (uint)pio_add_program(BOARD_TDMA_SPI_PIO, &tdma_pio_spi_rx_byte_program);
    s_tdma_pio_spi_programs_loaded = true;
    return true;
}

/* role 0 -> slot 0 downlink SCK, role 1 -> slot 1 downlink SCK. */
static uint32_t tdma_pio_spi_phys_downlink_sck(uint32_t role)
{
    return role == 0u ? BOARD_TDMA_SPI_DOWNLINK_SCK_PIN_SLOT0
                      : BOARD_TDMA_SPI_DOWNLINK_SCK_PIN_SLOT1;
}

static void tdma_pio_spi_phys_fill_static_snapshot(tdma_pio_spi_phys_t *phys)
{
    phys->snapshot.armed = phys->armed ? 1u : 0u;
    phys->snapshot.role = phys->role;
    phys->snapshot.baud_hz = phys->baud_hz;
    phys->snapshot.downlink_sck_pin = phys->downlink_sck_pin;
    phys->snapshot.downlink_tx_pin = phys->downlink_tx_pin;
    phys->snapshot.uplink_rx_pin = phys->uplink_rx_pin;
    phys->snapshot.uplink_sck_pin = phys->uplink_sck_pin;
}

static void tdma_pio_spi_phys_master_init(tdma_pio_spi_phys_t *phys)
{
    /* Downlink master SM: drive SCK + TX(MOSI), no CS. */
    tdma_pio_spi_tx_byte_program_init(BOARD_TDMA_SPI_PIO,
                                      BOARD_TDMA_SPI_MASTER_SM,
                                      s_tdma_pio_spi_tx_offset,
                                      phys->downlink_tx_pin,
                                      phys->downlink_sck_pin,
                                      phys->baud_hz);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM, true);
}

static void tdma_pio_spi_phys_slave_init(tdma_pio_spi_phys_t *phys)
{
    /* Uplink slave SM: sample RX + SCK from the previous board, no CS.
     * The uplink TX pin is owned by the downlink master direction and is not
     * reconfigured here. */
    tdma_pio_spi_rx_byte_program_init(BOARD_TDMA_SPI_PIO,
                                      BOARD_TDMA_SPI_SLAVE_SM,
                                      s_tdma_pio_spi_rx_offset,
                                      phys->uplink_rx_pin,
                                      UINT32_MAX,
                                      phys->uplink_sck_pin);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM, true);
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
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM, true);
    (void)phys;
}

/* Resident RX uses a fixed frame window (4-byte packet header + 32-byte
 * TdmaTransportFrame idle beacon) so the DMA completes at a known frame
 * boundary. Non-blocking: the core1 TDMA service is never stalled waiting
 * for RX; each service round checks DMA progress and picks up a frame only
 * when it is complete. */
#define TDMA_PIO_SPI_FIXED_RX_WORDS \
    (TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_HEADER_SIZE)

static bool tdma_pio_spi_phys_capture_words(tdma_pio_spi_phys_t *phys,
                                            size_t max_words,
                                            size_t *received_words)
{
    if (received_words != NULL) {
        *received_words = 0u;
    }
    if (phys == NULL || received_words == NULL ||
        max_words == 0u ||
        max_words > TDMA_PIO_SPI_RX_DMA_WORD_MAX ||
        !tdma_pio_spi_phys_ensure_rx_dma()) {
        return false;
    }

    if (!phys->rx_capture_active) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
        tdma_pio_spi_phys_rx_prepare(phys);

        dma_channel_config dma_cfg =
            dma_channel_get_default_config((uint)s_tdma_pio_spi_rx_dma_channel);
        channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
        channel_config_set_read_increment(&dma_cfg, false);
        channel_config_set_write_increment(&dma_cfg, true);
        channel_config_set_dreq(&dma_cfg, DREQ_PIO0_RX0 + BOARD_TDMA_SPI_SLAVE_SM);
        dma_channel_configure((uint)s_tdma_pio_spi_rx_dma_channel,
                              &dma_cfg,
                              s_tdma_pio_spi_rx_dma_words,
                              &BOARD_TDMA_SPI_PIO->rxf[BOARD_TDMA_SPI_SLAVE_SM],
                              max_words,
                              true);

        phys->rx_capture_active = true;
        phys->rx_capture_max_words = max_words;
        phys->rx_capture_last_remaining = (uint32_t)max_words;
        phys->rx_capture_last_change_1e3ns = tdma_pio_spi_phys_now_1e3ns();
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

    phys->role = config->local_slot_id == 0u ? 0u : 1u;
    phys->baud_hz = TDMA_PIO_SPI_DEFAULT_BAUD_HZ;
    phys->downlink_sck_pin = tdma_pio_spi_phys_downlink_sck(phys->role);
    phys->downlink_tx_pin = BOARD_TDMA_SPI_DOWNLINK_TX_PIN;
    phys->uplink_rx_pin = BOARD_TDMA_SPI_UPLINK_RX_PIN;
    phys->uplink_sck_pin = BOARD_TDMA_SPI_UPLINK_SCK_PIN;

    tdma_pio_spi_phys_master_init(phys);
    tdma_pio_spi_phys_slave_init(phys);

    phys->armed = true;
    phys->rx_capture_active = false;
    phys->snapshot.tx_count = 0u;
    phys->snapshot.rx_count = 0u;
    phys->snapshot.rx_bad_count = 0u;
    phys->snapshot.rx_timeout_count = 0u;
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
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_SLAVE_SM);
    gpio_set_function(phys->downlink_sck_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->downlink_tx_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->uplink_rx_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->uplink_sck_pin, GPIO_FUNC_SIO);
    gpio_set_dir(phys->downlink_sck_pin, GPIO_IN);
    gpio_set_dir(phys->downlink_tx_pin, GPIO_IN);
    gpio_set_dir(phys->uplink_rx_pin, GPIO_IN);
    gpio_set_dir(phys->uplink_sck_pin, GPIO_IN);
    phys->armed = false;
    phys->rx_capture_active = false;
    tdma_pio_spi_phys_fill_static_snapshot(phys);
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
    /* The master SM drains the TX FIFO autonomously; a non-empty FIFO means
     * the previous frame is still shifting out, so a new TX must wait for the
     * next service round (TX_BUSY) instead of being silently dropped. */
    if (!pio_sm_is_tx_fifo_empty(BOARD_TDMA_SPI_PIO, BOARD_TDMA_SPI_MASTER_SM)) {
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
        pio_sm_put_blocking(BOARD_TDMA_SPI_PIO,
                            BOARD_TDMA_SPI_MASTER_SM,
                            ((uint32_t)header[i]) << 24u);
    }
    for (size_t i = 0u; i < packet_size; i++) {
        pio_sm_put_blocking(BOARD_TDMA_SPI_PIO,
                            BOARD_TDMA_SPI_MASTER_SM,
                            ((uint32_t)packet[i]) << 24u);
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
        /* Non-blocking: no complete frame yet. Not an error; the next service
         * round picks it up. */
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
