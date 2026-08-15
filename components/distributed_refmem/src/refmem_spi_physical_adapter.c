#include "refmem_spi_physical_adapter.h"

#include <string.h>

#include "board_config.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/time.h"
#include "refmem_spi_physical.pio.h"
#include "refmem_sync_frame.h"

#define REFMEM_SPI_PACKET_MAGIC0 0x52u
#define REFMEM_SPI_PACKET_MAGIC1 0x4Du
#define REFMEM_SPI_PACKET_HEADER_SIZE 4u
#define REFMEM_SPI_DEFAULT_TIMEOUT_MS 1000u
#define REFMEM_SPI_RX_DMA_WORD_MAX \
    (REFMEM_SPI_PACKET_HEADER_SIZE + REFMEM_SYNC_FRAME_HEADER_SIZE + REFMEM_SYNC_FRAME_PAYLOAD_MAX)
#define REFMEM_SPI_RX_STABLE_US 1000

typedef enum {
    REFMEM_SPI_PHYSICAL_ERROR_NONE = 0u,
    REFMEM_SPI_PHYSICAL_ERROR_BAD_ARGUMENT = 1u,
    REFMEM_SPI_PHYSICAL_ERROR_BAD_ROLE = 2u,
    REFMEM_SPI_PHYSICAL_ERROR_TIMEOUT = 3u,
    REFMEM_SPI_PHYSICAL_ERROR_BAD_PACKET = 4u,
    REFMEM_SPI_PHYSICAL_ERROR_BAD_FRAME = 5u,
    REFMEM_SPI_PHYSICAL_ERROR_PAYLOAD_TOO_LARGE = 6u,
} refmem_spi_physical_error_t;

static bool s_refmem_spi_pio_programs_loaded;
static uint s_refmem_spi_tx_offset;
static uint s_refmem_spi_rx_offset;
static int s_refmem_spi_rx_dma_channel = -1;
static uint32_t s_refmem_spi_rx_dma_words[REFMEM_SPI_RX_DMA_WORD_MAX];

static void refmem_spi_physical_set_error(refmem_spi_physical_adapter_t *adapter,
                                          uint32_t error)
{
    if (adapter == NULL) {
        return;
    }
    adapter->snapshot.last_error = error;
    if (error == REFMEM_SPI_PHYSICAL_ERROR_TIMEOUT) {
        adapter->snapshot.timeout_count++;
    } else if (error == REFMEM_SPI_PHYSICAL_ERROR_BAD_PACKET ||
               error == REFMEM_SPI_PHYSICAL_ERROR_BAD_FRAME) {
        adapter->snapshot.bad_packet_count++;
    } else if (error == REFMEM_SPI_PHYSICAL_ERROR_PAYLOAD_TOO_LARGE) {
        adapter->snapshot.drop_count++;
    }
}

static uint32_t refmem_spi_physical_baud(uint32_t baud_hz)
{
    return baud_hz == 0u ? BOARD_REFMEM_SPI_BAUD_HZ : baud_hz;
}

static refmem_spi_physical_pin_config_t refmem_spi_physical_default_pins(void)
{
    refmem_spi_physical_pin_config_t pins = {
        .rx_pin = BOARD_REFMEM_SPI_RX_PIN,
        .csn_pin = BOARD_REFMEM_SPI_CSN_PIN,
        .sck_pin = BOARD_REFMEM_SPI_SCK_PIN,
        .tx_pin = BOARD_REFMEM_SPI_TX_PIN,
    };
    return pins;
}

static bool refmem_spi_physical_pins_valid(const refmem_spi_physical_pin_config_t *pins)
{
    if (pins == NULL) {
        return false;
    }
    const bool csn_used = pins->csn_pin != REFMEM_SPI_PHYSICAL_PIN_UNUSED;
    return pins->rx_pin <= 29u &&
           (csn_used ? pins->csn_pin <= 29u : true) &&
           pins->sck_pin <= 29u &&
           pins->tx_pin <= 29u &&
           (!csn_used || pins->rx_pin != pins->csn_pin) &&
           pins->rx_pin != pins->sck_pin &&
           pins->rx_pin != pins->tx_pin &&
           (!csn_used || pins->csn_pin != pins->sck_pin) &&
           (!csn_used || pins->csn_pin != pins->tx_pin) &&
           pins->sck_pin != pins->tx_pin;
}

static void refmem_spi_physical_fill_static_snapshot(refmem_spi_physical_adapter_t *adapter)
{
    adapter->snapshot.armed = adapter->armed ? 1u : 0u;
    adapter->snapshot.role = (uint32_t)adapter->role;
    adapter->snapshot.baud_hz = adapter->baud_hz;
    adapter->snapshot.rx_pin = adapter->pins.rx_pin;
    adapter->snapshot.csn_pin = adapter->pins.csn_pin;
    adapter->snapshot.sck_pin = adapter->pins.sck_pin;
    adapter->snapshot.tx_pin = adapter->pins.tx_pin;
}

static void refmem_spi_physical_configure_pins(refmem_spi_physical_adapter_t *adapter)
{
    pio_sm_set_enabled(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_TX_SM, false);
    pio_sm_set_enabled(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_RX_SM, false);
    pio_sm_clear_fifos(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_TX_SM);
    pio_sm_clear_fifos(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_RX_SM);
    pio_sm_restart(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_TX_SM);
    pio_sm_restart(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_RX_SM);

    if (adapter->role == REFMEM_SPI_PHYSICAL_ROLE_MASTER) {
        gpio_set_function(adapter->pins.rx_pin, GPIO_FUNC_SIO);
        gpio_set_dir(adapter->pins.rx_pin, GPIO_IN);
        gpio_pull_up(adapter->pins.rx_pin);

        if (adapter->pins.csn_pin != REFMEM_SPI_PHYSICAL_PIN_UNUSED) {
            gpio_set_function(adapter->pins.csn_pin, GPIO_FUNC_SIO);
            gpio_set_dir(adapter->pins.csn_pin, GPIO_OUT);
            gpio_put(adapter->pins.csn_pin, true);
        }

        refmem_pio_spi_tx_byte_program_init(BOARD_REFMEM_SPI_PIO,
                                            BOARD_REFMEM_SPI_TX_SM,
                                            s_refmem_spi_tx_offset,
                                            adapter->pins.tx_pin,
                                            adapter->pins.sck_pin,
                                            adapter->baud_hz);
        pio_sm_set_enabled(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_TX_SM, true);
    } else {
        refmem_pio_spi_rx_byte_program_init(BOARD_REFMEM_SPI_PIO,
                                            BOARD_REFMEM_SPI_RX_SM,
                                            s_refmem_spi_rx_offset,
                                            adapter->pins.rx_pin,
                                            adapter->pins.csn_pin,
                                            adapter->pins.sck_pin);

        gpio_set_function(adapter->pins.tx_pin, GPIO_FUNC_SIO);
        gpio_set_dir(adapter->pins.tx_pin, GPIO_IN);
        gpio_pull_down(adapter->pins.tx_pin);
        pio_sm_set_enabled(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_RX_SM, true);
    }
}

static uint32_t refmem_spi_physical_line_pin(uint32_t line_index)
{
    switch (line_index) {
    case 0u: return BOARD_REFMEM_SPI_RX_PIN;
    case 1u: return BOARD_REFMEM_SPI_CSN_PIN;
    case 2u: return BOARD_REFMEM_SPI_SCK_PIN;
    case 3u: return BOARD_REFMEM_SPI_TX_PIN;
    default: return UINT32_MAX;
    }
}

static bool refmem_spi_physical_ensure_pio_programs(void)
{
    if (s_refmem_spi_pio_programs_loaded) {
        return true;
    }

    if (!pio_can_add_program(BOARD_REFMEM_SPI_PIO, &refmem_pio_spi_tx_byte_program) ||
        !pio_can_add_program(BOARD_REFMEM_SPI_PIO, &refmem_pio_spi_rx_byte_program)) {
        return false;
    }
    s_refmem_spi_tx_offset =
        (uint)pio_add_program(BOARD_REFMEM_SPI_PIO, &refmem_pio_spi_tx_byte_program);
    s_refmem_spi_rx_offset =
        (uint)pio_add_program(BOARD_REFMEM_SPI_PIO, &refmem_pio_spi_rx_byte_program);
    s_refmem_spi_pio_programs_loaded = true;
    return true;
}

static bool refmem_spi_physical_deadline_expired(absolute_time_t deadline)
{
    return absolute_time_diff_us(get_absolute_time(), deadline) <= 0;
}

static void refmem_spi_physical_rx_prepare(void)
{
    pio_sm_set_enabled(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_RX_SM, false);
    pio_sm_clear_fifos(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_RX_SM);
    pio_sm_restart(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_RX_SM);
    pio_sm_set_enabled(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_RX_SM, true);
}

static bool refmem_spi_physical_ensure_rx_dma(void)
{
    if (s_refmem_spi_rx_dma_channel >= 0) {
        return true;
    }
    s_refmem_spi_rx_dma_channel = dma_claim_unused_channel(false);
    return s_refmem_spi_rx_dma_channel >= 0;
}

static absolute_time_t refmem_spi_physical_deadline(uint32_t timeout_ms)
{
    const uint32_t effective_timeout = timeout_ms == 0u
        ? REFMEM_SPI_DEFAULT_TIMEOUT_MS
        : timeout_ms;
    return make_timeout_time_ms(effective_timeout);
}

static void refmem_spi_physical_tx_prepare(void)
{
    pio_sm_set_enabled(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_TX_SM, false);
    pio_sm_clear_fifos(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_TX_SM);
    pio_sm_restart(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_TX_SM);
    pio_sm_set_enabled(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_TX_SM, true);
}

static void refmem_spi_physical_write_byte(uint8_t value)
{
    pio_sm_put_blocking(BOARD_REFMEM_SPI_PIO,
                        BOARD_REFMEM_SPI_TX_SM,
                        ((uint32_t)value) << 24u);
}

static void refmem_spi_physical_wait_tx_done(size_t byte_count, uint32_t baud_hz)
{
    while (!pio_sm_is_tx_fifo_empty(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_TX_SM)) {
        tight_loop_contents();
    }
    const uint32_t effective_baud = baud_hz == 0u ? BOARD_REFMEM_SPI_BAUD_HZ : baud_hz;
    const uint32_t drain_us =
        (uint32_t)(((uint64_t)(byte_count + 1u) * 8u * 1000000u) / effective_baud) + 2u;
    sleep_us(drain_us);
}

static uint32_t refmem_spi_physical_dma_remaining(void)
{
    return dma_channel_hw_addr((uint)s_refmem_spi_rx_dma_channel)->transfer_count;
}

static uint64_t refmem_spi_physical_now_us(void)
{
    return to_us_since_boot(get_absolute_time());
}

static bool refmem_spi_physical_time_reached(uint64_t deadline_us)
{
    return (int64_t)(refmem_spi_physical_now_us() - deadline_us) >= 0;
}

static bool refmem_spi_physical_parse_received_frame(refmem_spi_physical_adapter_t *adapter,
                                                     size_t received_words,
                                                     uint8_t *frame,
                                                     size_t frame_capacity,
                                                     size_t *frame_size)
{
    if (frame_size != NULL) {
        *frame_size = 0u;
    }
    if (adapter == NULL || frame == NULL || frame_size == NULL ||
        received_words < REFMEM_SPI_PACKET_HEADER_SIZE) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_TIMEOUT);
        return false;
    }

    uint8_t packet_header[REFMEM_SPI_PACKET_HEADER_SIZE];
    for (uint32_t i = 0u; i < REFMEM_SPI_PACKET_HEADER_SIZE; i++) {
        packet_header[i] = (uint8_t)(s_refmem_spi_rx_dma_words[i] & 0xFFu);
    }

    const uint16_t packet_size =
        (uint16_t)packet_header[2] | ((uint16_t)packet_header[3] << 8u);
    if (packet_header[0] != REFMEM_SPI_PACKET_MAGIC0 ||
        packet_header[1] != REFMEM_SPI_PACKET_MAGIC1 ||
        packet_size == 0u) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_BAD_PACKET);
        return false;
    }
    if (packet_size > frame_capacity) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_PAYLOAD_TOO_LARGE);
        return false;
    }
    if (received_words < (size_t)packet_size + REFMEM_SPI_PACKET_HEADER_SIZE) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_TIMEOUT);
        return false;
    }

    for (uint16_t i = 0u; i < packet_size; i++) {
        frame[i] = (uint8_t)(s_refmem_spi_rx_dma_words[REFMEM_SPI_PACKET_HEADER_SIZE + i] &
                             0xFFu);
    }

    refmem_sync_frame_header_t header;
    const uint8_t *payload = NULL;
    uint16_t payload_size = 0u;
    if (refmem_sync_frame_validate(frame,
                                   packet_size,
                                   &header,
                                   &payload,
                                   &payload_size) != REFMEM_SYNC_FRAME_OK) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_BAD_FRAME);
        return false;
    }

    *frame_size = packet_size;
    adapter->snapshot.rx_count++;
    adapter->snapshot.last_rx_size = packet_size;
    adapter->snapshot.last_error = REFMEM_SPI_PHYSICAL_ERROR_NONE;
    refmem_spi_physical_fill_static_snapshot(adapter);
    return true;
}

static bool refmem_spi_physical_capture_words(size_t max_words,
                                              bool wait_full,
                                              uint32_t timeout_ms,
                                              size_t *received_words)
{
    if (received_words != NULL) {
        *received_words = 0u;
    }
    if (received_words == NULL ||
        max_words == 0u ||
        max_words > REFMEM_SPI_RX_DMA_WORD_MAX ||
        !refmem_spi_physical_ensure_rx_dma()) {
        return false;
    }

    refmem_spi_physical_rx_prepare();
    dma_channel_abort((uint)s_refmem_spi_rx_dma_channel);

    dma_channel_config dma_cfg =
        dma_channel_get_default_config((uint)s_refmem_spi_rx_dma_channel);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, false);
    channel_config_set_write_increment(&dma_cfg, true);
    channel_config_set_dreq(&dma_cfg, DREQ_PIO0_RX0 + BOARD_REFMEM_SPI_RX_SM);
    dma_channel_configure((uint)s_refmem_spi_rx_dma_channel,
                          &dma_cfg,
                          s_refmem_spi_rx_dma_words,
                          &BOARD_REFMEM_SPI_PIO->rxf[BOARD_REFMEM_SPI_RX_SM],
                          max_words,
                          true);

    const absolute_time_t deadline = refmem_spi_physical_deadline(timeout_ms);
    uint32_t last_remaining = (uint32_t)max_words;
    absolute_time_t last_change = get_absolute_time();
    while (dma_channel_is_busy((uint)s_refmem_spi_rx_dma_channel)) {
        const uint32_t remaining = refmem_spi_physical_dma_remaining();
        if (remaining != last_remaining) {
            last_remaining = remaining;
            last_change = get_absolute_time();
        }
        const size_t moved = max_words - (size_t)remaining;
        if (!wait_full &&
            moved != 0u &&
            absolute_time_diff_us(last_change, get_absolute_time()) >=
                REFMEM_SPI_RX_STABLE_US) {
            break;
        }
        if (refmem_spi_physical_deadline_expired(deadline)) {
            break;
        }
        tight_loop_contents();
    }

    const bool completed = !dma_channel_is_busy((uint)s_refmem_spi_rx_dma_channel);
    const uint32_t remaining_before_abort = refmem_spi_physical_dma_remaining();
    if (!completed) {
        dma_channel_abort((uint)s_refmem_spi_rx_dma_channel);
    }

    const size_t moved = max_words - (size_t)remaining_before_abort;
    *received_words = moved;
    if (wait_full) {
        return completed && moved == max_words;
    }
    return moved != 0u;
}

bool refmem_spi_physical_adapter_arm(refmem_spi_physical_adapter_t *adapter,
                                     refmem_spi_physical_role_t role,
                                     uint32_t baud_hz,
                                     const refmem_spi_physical_pin_config_t *pins)
{
    if (adapter == NULL) {
        return false;
    }
    if (role != REFMEM_SPI_PHYSICAL_ROLE_MASTER &&
        role != REFMEM_SPI_PHYSICAL_ROLE_SLAVE) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_BAD_ROLE);
        return false;
    }

    const uint32_t old_tx = adapter->snapshot.tx_count;
    const uint32_t old_rx = adapter->snapshot.rx_count;
    const uint32_t old_timeout = adapter->snapshot.timeout_count;
    const uint32_t old_bad = adapter->snapshot.bad_packet_count;
    const uint32_t old_drop = adapter->snapshot.drop_count;
    const refmem_spi_physical_pin_config_t active_pins =
        pins == NULL ? refmem_spi_physical_default_pins() : *pins;
    if (!refmem_spi_physical_pins_valid(&active_pins)) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_BAD_ARGUMENT);
        return false;
    }
    if (!refmem_spi_physical_ensure_pio_programs()) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_BAD_ARGUMENT);
        return false;
    }

    memset(adapter, 0, sizeof(*adapter));
    adapter->role = role;
    adapter->baud_hz = refmem_spi_physical_baud(baud_hz);
    adapter->pins = active_pins;

    refmem_spi_physical_configure_pins(adapter);

    adapter->armed = true;
    adapter->snapshot.tx_count = old_tx;
    adapter->snapshot.rx_count = old_rx;
    adapter->snapshot.timeout_count = old_timeout;
    adapter->snapshot.bad_packet_count = old_bad;
    adapter->snapshot.drop_count = old_drop;
    adapter->snapshot.last_error = REFMEM_SPI_PHYSICAL_ERROR_NONE;
    refmem_spi_physical_fill_static_snapshot(adapter);
    return true;
}

void refmem_spi_physical_adapter_disarm(refmem_spi_physical_adapter_t *adapter)
{
    if (adapter == NULL || !adapter->armed) {
        return;
    }
    pio_sm_set_enabled(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_TX_SM, false);
    pio_sm_set_enabled(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_RX_SM, false);
    pio_sm_clear_fifos(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_TX_SM);
    pio_sm_clear_fifos(BOARD_REFMEM_SPI_PIO, BOARD_REFMEM_SPI_RX_SM);
    gpio_set_function(adapter->pins.rx_pin, GPIO_FUNC_SIO);
    if (adapter->pins.csn_pin != REFMEM_SPI_PHYSICAL_PIN_UNUSED) {
        gpio_set_function(adapter->pins.csn_pin, GPIO_FUNC_SIO);
    }
    gpio_set_function(adapter->pins.sck_pin, GPIO_FUNC_SIO);
    gpio_set_function(adapter->pins.tx_pin, GPIO_FUNC_SIO);
    gpio_set_dir(adapter->pins.rx_pin, GPIO_IN);
    if (adapter->pins.csn_pin != REFMEM_SPI_PHYSICAL_PIN_UNUSED) {
        gpio_set_dir(adapter->pins.csn_pin, GPIO_IN);
    }
    gpio_set_dir(adapter->pins.sck_pin, GPIO_IN);
    gpio_set_dir(adapter->pins.tx_pin, GPIO_IN);
    adapter->armed = false;
    refmem_spi_physical_fill_static_snapshot(adapter);
}

bool refmem_spi_physical_adapter_transmit(refmem_spi_physical_adapter_t *adapter,
                                          const uint8_t *frame,
                                          size_t frame_size)
{
    refmem_sync_frame_header_t header;
    const uint8_t *payload = NULL;
    uint16_t payload_size = 0u;

    if (adapter == NULL || frame == NULL || frame_size == 0u ||
        frame_size > UINT16_MAX || !adapter->armed ||
        adapter->role != REFMEM_SPI_PHYSICAL_ROLE_MASTER) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_BAD_ARGUMENT);
        return false;
    }
    if (refmem_sync_frame_validate(frame,
                                   frame_size,
                                   &header,
                                   &payload,
                                   &payload_size) != REFMEM_SYNC_FRAME_OK) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_BAD_FRAME);
        return false;
    }

    uint8_t packet_header[REFMEM_SPI_PACKET_HEADER_SIZE] = {
        REFMEM_SPI_PACKET_MAGIC0,
        REFMEM_SPI_PACKET_MAGIC1,
        (uint8_t)(frame_size & 0xFFu),
        (uint8_t)(frame_size >> 8u),
    };

    if (adapter->pins.csn_pin != REFMEM_SPI_PHYSICAL_PIN_UNUSED) {
        gpio_put(adapter->pins.csn_pin, false);
        sleep_us(10u);
    }
    refmem_spi_physical_tx_prepare();
    for (size_t i = 0u; i < sizeof(packet_header); i++) {
        refmem_spi_physical_write_byte(packet_header[i]);
    }
    for (size_t i = 0u; i < frame_size; i++) {
        refmem_spi_physical_write_byte(frame[i]);
    }
    refmem_spi_physical_wait_tx_done(sizeof(packet_header) + frame_size, adapter->baud_hz);
    sleep_us(10u);
    if (adapter->pins.csn_pin != REFMEM_SPI_PHYSICAL_PIN_UNUSED) {
        gpio_put(adapter->pins.csn_pin, true);
    }

    adapter->snapshot.tx_count++;
    adapter->snapshot.last_tx_size = (uint32_t)frame_size;
    adapter->snapshot.last_error = REFMEM_SPI_PHYSICAL_ERROR_NONE;
    refmem_spi_physical_fill_static_snapshot(adapter);
    return true;
}

bool refmem_spi_physical_adapter_transmit_raw(refmem_spi_physical_adapter_t *adapter,
                                              uint8_t seed,
                                              size_t byte_count)
{
    if (adapter == NULL || byte_count == 0u || byte_count > UINT16_MAX ||
        !adapter->armed || adapter->role != REFMEM_SPI_PHYSICAL_ROLE_MASTER) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_BAD_ARGUMENT);
        return false;
    }

    if (adapter->pins.csn_pin != REFMEM_SPI_PHYSICAL_PIN_UNUSED) {
        gpio_put(adapter->pins.csn_pin, false);
        sleep_us(10u);
    }
    refmem_spi_physical_tx_prepare();
    for (size_t i = 0u; i < byte_count; i++) {
        const uint8_t value = (uint8_t)(seed + i);
        refmem_spi_physical_write_byte(value);
    }
    refmem_spi_physical_wait_tx_done(byte_count, adapter->baud_hz);
    sleep_us(10u);
    if (adapter->pins.csn_pin != REFMEM_SPI_PHYSICAL_PIN_UNUSED) {
        gpio_put(adapter->pins.csn_pin, true);
    }

    adapter->snapshot.tx_count++;
    adapter->snapshot.last_tx_size = (uint32_t)byte_count;
    adapter->snapshot.last_error = REFMEM_SPI_PHYSICAL_ERROR_NONE;
    refmem_spi_physical_fill_static_snapshot(adapter);
    return true;
}

bool refmem_spi_physical_adapter_receive(refmem_spi_physical_adapter_t *adapter,
                                         uint8_t *frame,
                                         size_t frame_capacity,
                                         size_t *frame_size,
                                         uint32_t timeout_ms)
{
    if (frame_size != NULL) {
        *frame_size = 0u;
    }
    if (adapter == NULL || frame == NULL || frame_size == NULL ||
        frame_capacity == 0u || !adapter->armed ||
        adapter->role != REFMEM_SPI_PHYSICAL_ROLE_SLAVE) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_BAD_ARGUMENT);
        return false;
    }

    if (!refmem_spi_physical_adapter_receive_begin(adapter, frame_capacity, timeout_ms)) {
        return false;
    }

    while (true) {
        const refmem_spi_physical_rx_poll_result_t result =
            refmem_spi_physical_adapter_receive_poll(adapter,
                                                     frame,
                                                     frame_capacity,
                                                     frame_size);
        if (result == REFMEM_SPI_PHYSICAL_RX_POLL_DONE) {
            return true;
        }
        if (result == REFMEM_SPI_PHYSICAL_RX_POLL_ERROR) {
            return false;
        }
        tight_loop_contents();
    }
}

bool refmem_spi_physical_adapter_receive_begin(refmem_spi_physical_adapter_t *adapter,
                                               size_t frame_capacity,
                                               uint32_t timeout_ms)
{
    if (adapter == NULL || frame_capacity == 0u || !adapter->armed ||
        adapter->role != REFMEM_SPI_PHYSICAL_ROLE_SLAVE ||
        frame_capacity + REFMEM_SPI_PACKET_HEADER_SIZE > REFMEM_SPI_RX_DMA_WORD_MAX ||
        !refmem_spi_physical_ensure_rx_dma()) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_BAD_ARGUMENT);
        return false;
    }

    refmem_spi_physical_rx_prepare();
    dma_channel_abort((uint)s_refmem_spi_rx_dma_channel);

    const size_t max_words = frame_capacity + REFMEM_SPI_PACKET_HEADER_SIZE;
    dma_channel_config dma_cfg =
        dma_channel_get_default_config((uint)s_refmem_spi_rx_dma_channel);
    channel_config_set_transfer_data_size(&dma_cfg, DMA_SIZE_32);
    channel_config_set_read_increment(&dma_cfg, false);
    channel_config_set_write_increment(&dma_cfg, true);
    channel_config_set_dreq(&dma_cfg, DREQ_PIO0_RX0 + BOARD_REFMEM_SPI_RX_SM);
    dma_channel_configure((uint)s_refmem_spi_rx_dma_channel,
                          &dma_cfg,
                          s_refmem_spi_rx_dma_words,
                          &BOARD_REFMEM_SPI_PIO->rxf[BOARD_REFMEM_SPI_RX_SM],
                          max_words,
                          true);

    adapter->rx_capture_active = true;
    adapter->rx_capture_wait_full = false;
    adapter->rx_capture_max_words = max_words;
    adapter->rx_capture_last_remaining = (uint32_t)max_words;
    adapter->rx_capture_deadline_us =
        refmem_spi_physical_now_us() + (uint64_t)(timeout_ms == 0u ?
                                                  REFMEM_SPI_DEFAULT_TIMEOUT_MS :
                                                  timeout_ms) * 1000u;
    adapter->rx_capture_last_change_us = refmem_spi_physical_now_us();
    return true;
}

refmem_spi_physical_rx_poll_result_t refmem_spi_physical_adapter_receive_poll(
    refmem_spi_physical_adapter_t *adapter,
    uint8_t *frame,
    size_t frame_capacity,
    size_t *frame_size)
{
    if (frame_size != NULL) {
        *frame_size = 0u;
    }
    if (adapter == NULL || frame == NULL || frame_size == NULL ||
        !adapter->rx_capture_active) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_BAD_ARGUMENT);
        return REFMEM_SPI_PHYSICAL_RX_POLL_ERROR;
    }

    bool should_finish = false;
    if (!dma_channel_is_busy((uint)s_refmem_spi_rx_dma_channel)) {
        should_finish = true;
    } else {
        const uint32_t remaining = refmem_spi_physical_dma_remaining();
        if (remaining != adapter->rx_capture_last_remaining) {
            adapter->rx_capture_last_remaining = remaining;
            adapter->rx_capture_last_change_us = refmem_spi_physical_now_us();
        }
        const size_t moved = adapter->rx_capture_max_words - (size_t)remaining;
        if (!adapter->rx_capture_wait_full &&
            moved != 0u &&
            refmem_spi_physical_now_us() - adapter->rx_capture_last_change_us >=
                REFMEM_SPI_RX_STABLE_US) {
            should_finish = true;
        } else if (refmem_spi_physical_time_reached(adapter->rx_capture_deadline_us)) {
            should_finish = true;
        }
    }

    if (!should_finish) {
        return REFMEM_SPI_PHYSICAL_RX_POLL_PENDING;
    }

    const bool completed = !dma_channel_is_busy((uint)s_refmem_spi_rx_dma_channel);
    const uint32_t remaining_before_abort = refmem_spi_physical_dma_remaining();
    if (!completed) {
        dma_channel_abort((uint)s_refmem_spi_rx_dma_channel);
    }

    adapter->rx_capture_active = false;
    const size_t moved = adapter->rx_capture_max_words - (size_t)remaining_before_abort;
    if (moved < REFMEM_SPI_PACKET_HEADER_SIZE) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_TIMEOUT);
        return REFMEM_SPI_PHYSICAL_RX_POLL_ERROR;
    }

    return refmem_spi_physical_parse_received_frame(adapter,
                                                    moved,
                                                    frame,
                                                    frame_capacity,
                                                    frame_size)
               ? REFMEM_SPI_PHYSICAL_RX_POLL_DONE
               : REFMEM_SPI_PHYSICAL_RX_POLL_ERROR;
}

bool refmem_spi_physical_adapter_receive_raw(refmem_spi_physical_adapter_t *adapter,
                                             uint8_t *buffer,
                                             size_t expected_size,
                                             size_t *received_size,
                                             uint32_t timeout_ms)
{
    if (received_size != NULL) {
        *received_size = 0u;
    }
    if (adapter == NULL || buffer == NULL || received_size == NULL ||
        expected_size == 0u || !adapter->armed ||
        adapter->role != REFMEM_SPI_PHYSICAL_ROLE_SLAVE) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_BAD_ARGUMENT);
        return false;
    }

    size_t received_words_count = 0u;
    if (!refmem_spi_physical_capture_words(expected_size,
                                           true,
                                           timeout_ms,
                                           &received_words_count)) {
        refmem_spi_physical_set_error(adapter, REFMEM_SPI_PHYSICAL_ERROR_TIMEOUT);
        *received_size = received_words_count;
        return false;
    }

    for (size_t i = 0u; i < expected_size; i++) {
        buffer[i] = (uint8_t)(s_refmem_spi_rx_dma_words[i] & 0xFFu);
    }

    *received_size = expected_size;
    adapter->snapshot.rx_count++;
    adapter->snapshot.last_rx_size = (uint32_t)expected_size;
    adapter->snapshot.last_error = REFMEM_SPI_PHYSICAL_ERROR_NONE;
    refmem_spi_physical_fill_static_snapshot(adapter);
    return true;
}

void refmem_spi_physical_line_release(void)
{
    for (uint32_t i = 0u; i < 4u; i++) {
        const uint32_t pin = refmem_spi_physical_line_pin(i);
        gpio_set_function(pin, GPIO_FUNC_SIO);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_down(pin);
    }
}

bool refmem_spi_physical_line_drive(uint32_t line_index, bool level)
{
    const uint32_t pin = refmem_spi_physical_line_pin(line_index);
    if (pin == UINT32_MAX) {
        return false;
    }

    refmem_spi_physical_line_release();
    gpio_set_function(pin, GPIO_FUNC_SIO);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, level);
    return true;
}

uint32_t refmem_spi_physical_line_sample(void)
{
    uint32_t mask = 0u;
    for (uint32_t i = 0u; i < 4u; i++) {
        const uint32_t pin = refmem_spi_physical_line_pin(i);
        gpio_set_function(pin, GPIO_FUNC_SIO);
        gpio_set_dir(pin, GPIO_IN);
        gpio_pull_down(pin);
        if (gpio_get(pin)) {
            mask |= (1u << i);
        }
    }
    return mask;
}

bool refmem_spi_physical_adapter_get_snapshot(
    const refmem_spi_physical_adapter_t *adapter,
    refmem_spi_physical_snapshot_t *snapshot)
{
    if (adapter == NULL || snapshot == NULL) {
        return false;
    }
    *snapshot = adapter->snapshot;
    return true;
}
