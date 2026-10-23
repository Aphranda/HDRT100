#include "refmem_cmd_load.h"

#include <string.h>

#include "board_identity.h"
#include "distributed_refmem.h"
#include "osal.h"
#include "refmem_cmd_codec.h"

#define REFMEM_CMD_LOAD_ID 0x52434D44u
#define REFMEM_CMD_EVENT_COUNT 4u

typedef struct {
    uint8_t wire[REFMEM_CMD_WIRE_SIZE];
    uint32_t size;
} refmem_cmd_event_t;

static tdma_transport_load_t s_load;
static tdma_pio_spi_ring_adapter_t *s_adapter;
static volatile uint32_t s_load_initialized;
static refmem_cmd_event_t s_events[REFMEM_CMD_EVENT_COUNT];
static volatile uint32_t s_event_head;
static volatile uint32_t s_event_tail;
static volatile uint32_t s_rx_events;
static volatile uint32_t s_decoded;
static volatile uint32_t s_posted;
static volatile uint32_t s_acked;

static void refmem_cmd_load_rx(void *context,
                               const uint8_t *payload,
                               size_t payload_size)
{
    (void)context;
    if (payload == NULL || payload_size < REFMEM_CMD_WIRE_SIZE) {
        return;
    }
    s_rx_events++;
    const uint32_t head = s_event_head;
    const uint32_t next = (head + 1u) % REFMEM_CMD_EVENT_COUNT;
    if (next == s_event_tail) {
        return;
    }
    memcpy(s_events[head].wire, payload, REFMEM_CMD_WIRE_SIZE);
    s_events[head].size = REFMEM_CMD_WIRE_SIZE;
    s_event_head = next;
}

bool refmem_cmd_load_init(tdma_pio_spi_ring_adapter_t *adapter)
{
    if (adapter == NULL) {
        return false;
    }
    const tdma_transport_load_descriptor_t descriptor = {
        .load_id = REFMEM_CMD_LOAD_ID,
        .node_id = 0u,
        .payload_class = TDMA_PAYLOAD_CLASS_CONFIG_CONTROL,
        .seat_class = 3u, /* TDMA_LOOP_SLOT_LOAD (second class) */
        .window_period_cycles = 8u,
        .window_bytes = REFMEM_CMD_WIRE_SIZE,
        .max_frame_size = REFMEM_CMD_WIRE_SIZE,
        .required_mask = 0x0Fu,
    };
    if (!tdma_transport_load_init(&s_load, &descriptor)) {
        return false;
    }
    if (!tdma_pio_spi_ring_adapter_set_transport_load(adapter, &s_load)) {
        return false;
    }
    s_adapter = adapter;
    tdma_pio_spi_ring_adapter_set_transport_load_rx(
        adapter, refmem_cmd_load_rx, NULL);
    s_load_initialized = 1u;
    return true;
}

bool refmem_cmd_load_submit(const uint8_t *wire, uint32_t wire_size)
{
    return tdma_transport_load_submit(&s_load, wire, wire_size);
}

void refmem_cmd_load_service(void)
{
    while (s_event_tail != s_event_head) {
        const uint32_t tail = s_event_tail;
        refmem_command_request_t request;
        if (refmem_cmd_codec_decode(s_events[tail].wire, &request) ==
            REFMEM_CMD_CODEC_OK) {
            s_decoded++;
            const uint32_t board_no = board_identity_get_no();
            const uint32_t local_slot = board_no > 0u ? board_no - 1u : 0u;
            if ((request.target_mask & (1u << local_slot)) != 0u &&
                distributed_refmem_command_try_post(&request, osal_tick_ms())) {
                s_posted++;
                const refmem_command_take_result_t take =
                    distributed_refmem_command_try_take(
                        local_slot, request.issue_epoch, request.run_id,
                        request.payload_crc32, 0u);
                if (take == REFMEM_COMMAND_TAKE_TAKEN) {
                    (void)distributed_refmem_command_ack(local_slot, 0u);
                    s_acked++;
                }
            }
        }
        s_event_tail = (tail + 1u) % REFMEM_CMD_EVENT_COUNT;
    }
}

bool refmem_cmd_load_get_status(refmem_cmd_load_status_t *status)
{
    if (status == NULL) {
        return false;
    }
    memset(status, 0, sizeof(*status));
    status->load_initialized = s_load_initialized;
    tdma_transport_load_get_stats(&s_load,
                                  &status->submitted,
                                  &status->served,
                                  &status->rejected,
                                  &status->window_closed);
    status->rx_events = s_rx_events;
    status->decoded = s_decoded;
    status->posted = s_posted;
    status->acked = s_acked;
    tdma_pio_spi_ring_adapter_get_transport_load_tx(
        s_adapter, &status->tx_ok, &status->tx_fail, &status->tx_stage);
    tdma_pio_spi_ring_adapter_get_transport_load_rx(
        s_adapter, &status->rx_seen, &status->rx_delivered);
    tdma_pio_spi_ring_adapter_get_rx_debug(
        s_adapter, &status->rx_drop_count,
        &status->last_rx_packet_size, &status->last_rx_payload_class,
        &status->rx_config_control, &status->rx_process_image);
    tdma_pio_spi_ring_adapter_get_transport_load_build(
        s_adapter, &status->build_class,
        &status->build_size, &status->build_load);
    return true;
}
