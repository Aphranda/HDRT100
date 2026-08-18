#include "tdma_pio_spi_phys.h"

#include <string.h>

#include "board_config.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "tdma_pio_spi.pio.h"
#include "vdc_timestamp_clock.h"

#define TDMA_PIO_SPI_DEFAULT_BAUD_HZ 1000000u

/* Fixed frame window: 4-byte packet header + 32-byte TdmaTransportFrame idle
 * beacon. Every ring frame is exactly this long. */
#define TDMA_PIO_SPI_FIXED_RX_WORDS \
    (TDMA_PIO_SPI_PACKET_HEADER_SIZE + TDMA_TRANSPORT_FRAME_HEADER_SIZE)

/* Bounded TX FIFO wait: core1 must stay predictable (HAOFV). If the downlink
 * SM stops consuming (e.g. the config plane disarms it while core1 is mid
 * frame), put_blocking would hang core1 forever and break the flash lockout
 * protocol. Instead we wait at most this long per word and fail the frame. */
#define TDMA_PIO_SPI_TX_PUT_TIMEOUT_US 500u

/* CS is now the point-to-point frame-sync. After the fixed frame DMA completes,
 * the sender can still hold CS low while the final shifted byte drains. Wait
 * for CS idle before clearing FIFO/re-arming DMA, otherwise the RX SM can treat
 * the low tail as the beginning of the next capture window. */
#define TDMA_PIO_SPI_RX_CSN_IDLE_WAIT_US 500u

static bool s_tdma_pio_spi_programs_loaded;
static uint s_tdma_pio_spi_tx_offset;
static uint s_tdma_pio_spi_rx_offset;
static int s_tdma_pio_spi_rx_dma_channel = -1;
static uint32_t s_tdma_pio_spi_rx_ring[TDMA_PIO_SPI_RX_RING_WORDS]
    __attribute__((aligned(TDMA_PIO_SPI_RX_RING_WORDS * sizeof(uint32_t))));
static uint32_t s_tdma_pio_spi_rx_scan_produced;
static uint32_t s_tdma_pio_spi_rx_produced_seq;
static uint32_t s_tdma_pio_spi_rx_last_write_index;
static bool s_tdma_pio_spi_rx_write_index_valid;
/* Assembled frame (magic-aligned) copied out of the continuous DMA ring. */
static uint32_t s_tdma_pio_spi_rx_frame[TDMA_PIO_SPI_RX_DMA_WORD_MAX];

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
    phys->snapshot.tx_csn_pin = phys->tx_csn_pin;
    phys->snapshot.tx_pin = phys->tx_pin;
    phys->snapshot.rx_sck_pin = phys->rx_sck_pin;
    phys->snapshot.rx_csn_pin = phys->rx_csn_pin;
    phys->snapshot.rx_pin = phys->rx_pin;
}

static uint64_t tdma_pio_spi_phys_now_us(void)
{
    return to_us_since_boot(get_absolute_time());
}

static uint32_t tdma_pio_spi_phys_frame_tail_us(const tdma_pio_spi_phys_t *phys,
                                                size_t packet_size)
{
    const uint32_t baud_hz =
        (phys != NULL && phys->baud_hz != 0u) ? phys->baud_hz : 1000000u;
    const size_t packet_words = packet_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE;
    /* After the last pio_sm_put(), up to the joined TX FIFO depth plus the
     * current OSR can still be on the wire. Keep CS active for that tail. */
    const size_t tail_words = packet_words < 10u ? packet_words : 10u;
    const uint64_t bit_us =
        ((uint64_t)tail_words * 8ull * 1000000ull + baud_hz - 1ull) /
        baud_hz;
    return (uint32_t)bit_us + 10u;
}

static uint64_t tdma_pio_spi_phys_wire_time_ns(const tdma_pio_spi_phys_t *phys,
                                               size_t packet_size)
{
    const uint32_t baud_hz =
        (phys != NULL && phys->baud_hz != 0u) ? phys->baud_hz : 1000000u;
    const uint64_t bits =
        (uint64_t)(packet_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE) * 8ull;
    return (bits * 1000000000ull + baud_hz - 1ull) / baud_hz;
}

static bool tdma_pio_spi_phys_ensure_rx_dma(void)
{
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        return true;
    }
    if (dma_channel_is_claimed(TDMA_PIO_SPI_RX_DMA_CHANNEL)) {
        return false;
    }
    dma_channel_claim(TDMA_PIO_SPI_RX_DMA_CHANNEL);
    s_tdma_pio_spi_rx_dma_channel = (int)TDMA_PIO_SPI_RX_DMA_CHANNEL;
    return true;
}

static void tdma_pio_spi_phys_rx_prepare(tdma_pio_spi_phys_t *phys)
{
    /* The SM must keep running across frame boundaries. Resetting it here
     * would make the next capture depend on the CPU/service phase. */
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
}

/* Half-duplex ring: the pin set is symmetric across boards (measured wiring,
 * see board_config.h), so every ring node uses the same downlink TX leg
 * (CS=21, TX=23, SCK=24) and uplink RX leg (CS=16, RX=18, SCK=19). */
static void tdma_pio_spi_phys_configure(tdma_pio_spi_phys_t *phys)
{
    phys->tx_sm = BOARD_TDMA_SPI_MASTER_SM;
    phys->tx_pin = BOARD_TDMA_SPI_DOWNLINK_TX_PIN;
    phys->tx_sck_pin = BOARD_TDMA_SPI_DOWNLINK_SCK_PIN;
    phys->tx_csn_pin = BOARD_TDMA_SPI_DOWNLINK_CSN_PIN;
    phys->rx_sm = BOARD_TDMA_SPI_SLAVE_SM;
    phys->rx_pin = BOARD_TDMA_SPI_UPLINK_RX_PIN;
    phys->rx_sck_pin = BOARD_TDMA_SPI_UPLINK_SCK_PIN;
    phys->rx_csn_pin = BOARD_TDMA_SPI_UPLINK_CSN_PIN;

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
                                      phys->rx_csn_pin,
                                      phys->rx_sck_pin);
    gpio_init(phys->tx_csn_pin);
    gpio_set_dir(phys->tx_csn_pin, GPIO_OUT);
    gpio_put(phys->tx_csn_pin, true);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, true);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_restart(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, true);
}

static bool tdma_pio_spi_phys_rx_arm(tdma_pio_spi_phys_t *phys)
{
    if (phys == NULL || !tdma_pio_spi_phys_ensure_rx_dma()) {
        return false;
    }
    dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    tdma_pio_spi_phys_rx_prepare(phys);
    memset(s_tdma_pio_spi_rx_ring, 0, sizeof(s_tdma_pio_spi_rx_ring));
    s_tdma_pio_spi_rx_scan_produced = 0u;
    s_tdma_pio_spi_rx_produced_seq = 0u;
    s_tdma_pio_spi_rx_last_write_index = 0u;
    s_tdma_pio_spi_rx_write_index_valid = false;
    dma_channel_config dma_cfg =
        dma_channel_get_default_config((uint)s_tdma_pio_spi_rx_dma_channel);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, false);
    channel_config_set_write_increment(&dma_cfg, true);
    channel_config_set_ring(&dma_cfg, true, TDMA_PIO_SPI_RX_RING_LOG2);
    channel_config_set_dreq(&dma_cfg,
                            pio_get_dreq(BOARD_TDMA_SPI_PIO,
                                         phys->rx_sm,
                                         false));
    dma_channel_configure(
        (uint)s_tdma_pio_spi_rx_dma_channel,
        &dma_cfg,
        s_tdma_pio_spi_rx_ring,
        &BOARD_TDMA_SPI_PIO->rxf[phys->rx_sm],
        UINT32_MAX,
        false);
    dma_start_channel_mask(1u << (uint)s_tdma_pio_spi_rx_dma_channel);
    phys->rx_capture_active = true;
    return true;
}

static uint32_t tdma_pio_spi_phys_rx_write_index(void)
{
    const uintptr_t ring_base = (uintptr_t)s_tdma_pio_spi_rx_ring;
    const uintptr_t write_addr =
        (uintptr_t)dma_hw->ch[(uint)s_tdma_pio_spi_rx_dma_channel].write_addr;
    return (uint32_t)(((write_addr - ring_base) &
                       ((TDMA_PIO_SPI_RX_RING_WORDS * sizeof(uint32_t)) -
                        1u)) /
                      sizeof(uint32_t));
}

static uint32_t tdma_pio_spi_phys_rx_produced_words(void)
{
    const uint32_t write_index = tdma_pio_spi_phys_rx_write_index();
    if (!s_tdma_pio_spi_rx_write_index_valid) {
        s_tdma_pio_spi_rx_last_write_index = write_index;
        s_tdma_pio_spi_rx_write_index_valid = true;
        return s_tdma_pio_spi_rx_produced_seq;
    }
    if (write_index != s_tdma_pio_spi_rx_last_write_index) {
        const uint32_t last = s_tdma_pio_spi_rx_last_write_index;
        const uint32_t delta =
            write_index >= last
                ? write_index - last
                : (TDMA_PIO_SPI_RX_RING_WORDS - last) + write_index;
        s_tdma_pio_spi_rx_produced_seq += delta;
        s_tdma_pio_spi_rx_last_write_index = write_index;
    }
    return s_tdma_pio_spi_rx_produced_seq;
}

static uint32_t tdma_pio_spi_phys_rx_ring_word(uint32_t produced)
{
    return s_tdma_pio_spi_rx_ring[produced &
                                  (TDMA_PIO_SPI_RX_RING_WORDS - 1u)];
}

static uint8_t tdma_pio_spi_phys_rx_ring_byte(uint32_t produced)
{
    return (uint8_t)(tdma_pio_spi_phys_rx_ring_word(produced) & 0xFFu);
}

static bool tdma_pio_spi_phys_transport_header_matches(uint32_t packet_start,
                                                       uint16_t frame_size)
{
    if (frame_size < TDMA_TRANSPORT_FRAME_HEADER_SIZE) {
        return false;
    }
    const uint16_t transport_magic =
        (uint16_t)tdma_pio_spi_phys_rx_ring_byte(packet_start) |
        ((uint16_t)tdma_pio_spi_phys_rx_ring_byte(packet_start + 1u) << 8u);
    const uint16_t transport_size =
        (uint16_t)tdma_pio_spi_phys_rx_ring_byte(packet_start + 4u) |
        ((uint16_t)tdma_pio_spi_phys_rx_ring_byte(packet_start + 5u) << 8u);
    const uint8_t frame_class =
        tdma_pio_spi_phys_rx_ring_byte(packet_start + 3u);

    return transport_magic == TDMA_TRANSPORT_FRAME_MAGIC &&
           tdma_pio_spi_phys_rx_ring_byte(packet_start + 2u) ==
               TDMA_TRANSPORT_FRAME_VERSION &&
           (frame_class == TDMA_TRANSPORT_FRAME_CLASS_SHORT ||
            frame_class == TDMA_TRANSPORT_FRAME_CLASS_LONG) &&
           transport_size == frame_size &&
           tdma_pio_spi_phys_rx_ring_byte(packet_start + 6u) ==
               TDMA_TRANSPORT_FRAME_HEADER_SIZE;
}

static bool tdma_pio_spi_phys_capture_words(tdma_pio_spi_phys_t *phys,
                                            size_t max_words,
                                            size_t *received_words)
{
    if (received_words != NULL) {
        *received_words = 0u;
    }
    if (phys == NULL || received_words == NULL ||
        max_words == 0u || max_words > TDMA_PIO_SPI_RX_DMA_WORD_MAX ||
        !phys->rx_capture_active) {
        return false;
    }
    const uint32_t produced = tdma_pio_spi_phys_rx_produced_words();
    phys->snapshot.rx_dma_produced_words = produced;
    phys->snapshot.rx_scan_produced_words = s_tdma_pio_spi_rx_scan_produced;
    phys->snapshot.rx_dma_write_index = tdma_pio_spi_phys_rx_write_index();
    phys->snapshot.rx_dma_channel = (uint32_t)s_tdma_pio_spi_rx_dma_channel;
    if (produced <= s_tdma_pio_spi_rx_scan_produced) {
        return false;
    }

    if (produced - s_tdma_pio_spi_rx_scan_produced >
        TDMA_PIO_SPI_RX_RING_WORDS) {
        phys->snapshot.rx_ring_overrun_count++;
        s_tdma_pio_spi_rx_scan_produced =
            produced - TDMA_PIO_SPI_RX_RING_WORDS;
    }

    uint32_t candidate = s_tdma_pio_spi_rx_scan_produced;
    while (candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE <= produced) {
        if ((tdma_pio_spi_phys_rx_ring_word(candidate) &
             0xFFu) == TDMA_PIO_SPI_PACKET_MAGIC0 &&
            (tdma_pio_spi_phys_rx_ring_word(candidate + 1u) &
             0xFFu) == TDMA_PIO_SPI_PACKET_MAGIC1) {
            const uint16_t frame_size =
                (uint16_t)(tdma_pio_spi_phys_rx_ring_word(candidate + 2u) &
                           0xFFu) |
                (uint16_t)((tdma_pio_spi_phys_rx_ring_word(candidate + 3u) &
                            0xFFu)
                           << 8u);
            const uint32_t total_words =
                TDMA_PIO_SPI_PACKET_HEADER_SIZE + frame_size;
            if (frame_size == 0u || total_words > max_words) {
                candidate++;
                continue;
            }
            if (candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE +
                    TDMA_TRANSPORT_FRAME_HEADER_SIZE >
                produced) {
                s_tdma_pio_spi_rx_scan_produced = candidate;
                return false;
            }
            if (!tdma_pio_spi_phys_transport_header_matches(
                    candidate + TDMA_PIO_SPI_PACKET_HEADER_SIZE,
                    frame_size)) {
                candidate++;
                continue;
            }
            if (candidate + total_words > produced) {
                /* Header is valid, but the last bytes are still on the wire. */
                s_tdma_pio_spi_rx_scan_produced = candidate;
                return false;
            }
            for (uint32_t i = 0u; i < total_words; i++) {
                s_tdma_pio_spi_rx_frame[i] =
                    tdma_pio_spi_phys_rx_ring_word(candidate + i);
            }
            s_tdma_pio_spi_rx_scan_produced = candidate + total_words;
            phys->snapshot.rx_magic_at_zero++;
            *received_words = total_words;
            return true;
        }
        candidate++;
    }
    /* Keep one trailing word so a split magic header can be completed by the
     * next DMA sample. */
    phys->snapshot.rx_magic_fail_count++;
    s_tdma_pio_spi_rx_scan_produced = produced > 0u ? produced - 1u : 0u;
    return false;
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
    phys->baud_hz = BOARD_TDMA_SPI_BAUD_HZ != 0u
                        ? BOARD_TDMA_SPI_BAUD_HZ
                        : TDMA_PIO_SPI_DEFAULT_BAUD_HZ;
    tdma_pio_spi_phys_configure(phys);
    if (!tdma_pio_spi_phys_rx_arm(phys)) {
        return false;
    }

    phys->armed = true;
    phys->snapshot.tx_count = 0u;
    phys->snapshot.rx_count = 0u;
    phys->snapshot.rx_bad_count = 0u;
    phys->snapshot.tx_busy_count = 0u;
    phys->snapshot.rx_partial_count = 0u;
    phys->snapshot.rx_stall_count = 0u;
    phys->snapshot.tx_timeout_count = 0u;
    phys->snapshot.rx_busy_count = 0u;
    phys->snapshot.rx_magic_fail_count = 0u;
    phys->snapshot.rx_magic_at_zero = 0u;
    phys->snapshot.rx_magic_at_shift = 0u;
    phys->snapshot.rx_ring_overrun_count = 0u;
    phys->snapshot.rx_dma_produced_words = 0u;
    phys->snapshot.rx_scan_produced_words = 0u;
    phys->snapshot.rx_dma_write_index = 0u;
    phys->snapshot.rx_dma_channel = TDMA_PIO_SPI_RX_DMA_CHANNEL;
    phys->snapshot.tx_edge_count = 0u;
    phys->snapshot.rx_edge_count = 0u;
    phys->snapshot.last_tx_edge_timestamp_ns = 0ull;
    phys->snapshot.last_tx_done_timestamp_ns = 0ull;
    phys->snapshot.last_rx_edge_timestamp_ns = 0ull;
    phys->snapshot.last_rx_extract_timestamp_ns = 0ull;
    phys->snapshot.rx_busy_word0 = 0u;
    phys->snapshot.rx_busy_word1 = 0u;
    phys->snapshot.rx_busy_word2 = 0u;
    phys->snapshot.rx_busy_word3 = 0u;
    phys->snapshot.rx_busy_moved = 0u;
    phys->snapshot.last_bad_header0 = 0u;
    phys->snapshot.last_bad_header1 = 0u;
    phys->snapshot.last_bad_header2 = 0u;
    phys->snapshot.last_bad_header3 = 0u;
    phys->snapshot.last_bad_words = 0u;
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
    if (s_tdma_pio_spi_rx_dma_channel >= 0) {
        dma_channel_abort((uint)s_tdma_pio_spi_rx_dma_channel);
    }
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->tx_sm, false);
    pio_sm_set_enabled(BOARD_TDMA_SPI_PIO, phys->rx_sm, false);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->tx_sm);
    pio_sm_clear_fifos(BOARD_TDMA_SPI_PIO, phys->rx_sm);
    gpio_set_function(phys->tx_sck_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->tx_csn_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->tx_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->rx_sck_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->rx_csn_pin, GPIO_FUNC_SIO);
    gpio_set_function(phys->rx_pin, GPIO_FUNC_SIO);
    gpio_set_dir(phys->tx_sck_pin, GPIO_IN);
    gpio_set_dir(phys->tx_csn_pin, GPIO_IN);
    gpio_set_dir(phys->tx_pin, GPIO_IN);
    gpio_set_dir(phys->rx_sck_pin, GPIO_IN);
    gpio_set_dir(phys->rx_csn_pin, GPIO_IN);
    gpio_set_dir(phys->rx_pin, GPIO_IN);
    phys->armed = false;
    phys->rx_capture_active = false;
    tdma_pio_spi_phys_fill_static_snapshot(phys);
}

static bool tdma_pio_spi_phys_tx_put(tdma_pio_spi_phys_t *phys,
                                     uint32_t word)
{
    const uint64_t deadline_us =
        tdma_pio_spi_phys_now_us() + TDMA_PIO_SPI_TX_PUT_TIMEOUT_US;
    while (pio_sm_is_tx_fifo_full(BOARD_TDMA_SPI_PIO, phys->tx_sm)) {
        if (tdma_pio_spi_phys_now_us() >= deadline_us) {
            phys->snapshot.tx_timeout_count++;
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
    const uint64_t tx_edge_timestamp_ns = vdc_timestamp_clock_now_ns();
    gpio_put(phys->tx_csn_pin, false);
    for (uint32_t i = 0u; i < TDMA_PIO_SPI_PACKET_HEADER_SIZE; i++) {
        if (!tdma_pio_spi_phys_tx_put(phys, ((uint32_t)header[i]) << 24u)) {
            gpio_put(phys->tx_csn_pin, true);
            tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
            phys->snapshot.tx_busy_count++;
            return false;
        }
    }
    for (size_t i = 0u; i < packet_size; i++) {
        if (!tdma_pio_spi_phys_tx_put(phys, ((uint32_t)packet[i]) << 24u)) {
            gpio_put(phys->tx_csn_pin, true);
            tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_TX_BUSY);
            phys->snapshot.tx_busy_count++;
            return false;
        }
    }
    busy_wait_us_32(tdma_pio_spi_phys_frame_tail_us(phys, packet_size));
    gpio_put(phys->tx_csn_pin, true);
    const uint64_t tx_done_timestamp_ns = vdc_timestamp_clock_now_ns();

    phys->snapshot.tx_count++;
    phys->snapshot.tx_edge_count++;
    phys->snapshot.last_tx_size = (uint32_t)packet_size;
    phys->snapshot.last_tx_edge_timestamp_ns = tx_edge_timestamp_ns;
    phys->snapshot.last_tx_done_timestamp_ns = tx_done_timestamp_ns;
    phys->snapshot.last_error = TDMA_PIO_SPI_PHYS_ERROR_NONE;
    if (tx_timestamp_ns != NULL) {
        *tx_timestamp_ns = tx_edge_timestamp_ns;
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
                                         TDMA_PIO_SPI_RX_DMA_WORD_MAX,
                                         &received_words)) {
        return false;
    }
    if (received_words < TDMA_PIO_SPI_PACKET_HEADER_SIZE) {
        phys->snapshot.last_bad_words = (uint32_t)received_words;
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET);
        return false;
    }

    uint8_t header[TDMA_PIO_SPI_PACKET_HEADER_SIZE];
    for (uint32_t i = 0u; i < TDMA_PIO_SPI_PACKET_HEADER_SIZE; i++) {
        header[i] = (uint8_t)(s_tdma_pio_spi_rx_frame[i] & 0xFFu);
    }
    const uint16_t frame_size =
        (uint16_t)header[2] | ((uint16_t)header[3] << 8u);
    if (header[0] != TDMA_PIO_SPI_PACKET_MAGIC0 ||
        header[1] != TDMA_PIO_SPI_PACKET_MAGIC1 ||
        frame_size == 0u) {
        phys->snapshot.last_bad_header0 = s_tdma_pio_spi_rx_frame[0];
        phys->snapshot.last_bad_header1 = s_tdma_pio_spi_rx_frame[1];
        phys->snapshot.last_bad_header2 = s_tdma_pio_spi_rx_frame[2];
        phys->snapshot.last_bad_header3 = s_tdma_pio_spi_rx_frame[3];
        phys->snapshot.last_bad_words = (uint32_t)received_words;
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET);
        return false;
    }
    if ((size_t)frame_size > packet_capacity) {
        phys->snapshot.last_bad_header0 =
            header[2] | ((uint32_t)header[3] << 8u);
        phys->snapshot.last_bad_words = (uint32_t)received_words;
        tdma_pio_spi_phys_set_error(phys,
                                    TDMA_PIO_SPI_PHYS_ERROR_PAYLOAD_TOO_LARGE);
        return false;
    }
    if (received_words <
        (size_t)frame_size + TDMA_PIO_SPI_PACKET_HEADER_SIZE) {
        phys->snapshot.last_bad_words = (uint32_t)received_words;
        tdma_pio_spi_phys_set_error(phys, TDMA_PIO_SPI_PHYS_ERROR_BAD_PACKET);
        return false;
    }

    for (uint16_t i = 0u; i < frame_size; i++) {
        packet[i] = (uint8_t)(
            s_tdma_pio_spi_rx_frame[TDMA_PIO_SPI_PACKET_HEADER_SIZE + i] &
            0xFFu);
    }
    *packet_size = frame_size;
    const uint64_t rx_extract_timestamp_ns = vdc_timestamp_clock_now_ns();
    const uint64_t wire_time_ns =
        tdma_pio_spi_phys_wire_time_ns(phys, frame_size);
    const uint64_t rx_edge_timestamp_ns =
        rx_extract_timestamp_ns > wire_time_ns
            ? rx_extract_timestamp_ns - wire_time_ns
            : rx_extract_timestamp_ns;
    *rx_timestamp_ns = rx_edge_timestamp_ns;

    phys->snapshot.rx_count++;
    phys->snapshot.last_rx_size = frame_size;
    phys->snapshot.last_rx_timestamp_ns = *rx_timestamp_ns;
    phys->snapshot.rx_edge_count++;
    phys->snapshot.last_rx_edge_timestamp_ns = rx_edge_timestamp_ns;
    phys->snapshot.last_rx_extract_timestamp_ns = rx_extract_timestamp_ns;
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
