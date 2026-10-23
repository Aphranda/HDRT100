#include "refmem_cmd_load.h"

#include <string.h>

#include "board_identity.h"
#include "distributed_refmem.h"
#include "osal.h"
#include "refmem_cmd_codec.h"
#include "tdma_long_frame.h"
#include "tdma_maintenance_runtime.h"
#include "tdma_second_class_parser.h"

static tdma_pio_spi_ring_adapter_t *s_adapter;
static volatile uint32_t s_initialized;
static volatile uint32_t s_submitted;
static volatile uint32_t s_tx_ok;
static volatile uint32_t s_tx_fail;
static volatile uint32_t s_rejected;
static volatile uint32_t s_rx_frames;
static volatile uint32_t s_decoded;
static volatile uint32_t s_posted;
static volatile uint32_t s_acked;
static uint8_t s_generation;
static uint8_t s_tx_frame[TDMA_MAINTENANCE_RUNTIME_MAX];

static bool refmem_cmd_parser(const uint8_t *payload,
                              size_t payload_size,
                              void *context)
{
    (void)context;
    if (payload == NULL || payload_size != REFMEM_CMD_WIRE_SIZE) {
        return false;
    }
    refmem_command_request_t request;
    if (refmem_cmd_codec_decode(payload, &request) != REFMEM_CMD_CODEC_OK) {
        return false;
    }
    s_decoded++;
    const uint32_t board_no = board_identity_get_no();
    const uint32_t local_slot = board_no > 0u ? board_no - 1u : 0u;
    if ((request.target_mask & (1u << local_slot)) == 0u) {
        return true;
    }
    if (!distributed_refmem_command_try_post(&request, osal_tick_ms())) {
        return true;
    }
    s_posted++;
    const refmem_command_take_result_t take =
        distributed_refmem_command_try_take(
            local_slot, request.issue_epoch, request.run_id,
            request.payload_crc32, 0u);
    if (take == REFMEM_COMMAND_TAKE_TAKEN) {
        (void)distributed_refmem_command_ack(local_slot, 0u);
        s_acked++;
    }
    return true;
}

bool refmem_cmd_load_init(tdma_pio_spi_ring_adapter_t *adapter)
{
    s_adapter = adapter;
    s_initialized = tdma_maintenance_runtime_ready() ? 1u : 0u;
    if (s_initialized == 0u) {
        return false;
    }
    tdma_second_class_parser_reset();
    if (!tdma_second_class_parser_register(
            TDMA_SECOND_CLASS_TYPE_REFMEM_CMD, refmem_cmd_parser, NULL) ||
        !tdma_second_class_parser_register(
            TDMA_SECOND_CLASS_TYPE_T2_STATIC, tdma_t2_static_parser, NULL) ||
        !tdma_second_class_parser_register(
            TDMA_SECOND_CLASS_TYPE_T2_DYNAMIC, tdma_t2_dynamic_parser, NULL)) {
        s_initialized = 0u;
        return false;
    }
    return true;
}

bool refmem_cmd_load_submit(const uint8_t *wire, uint32_t wire_size)
{
    return refmem_cmd_load_submit_type(TDMA_SECOND_CLASS_TYPE_REFMEM_CMD,
                                       wire, wire_size);
}

bool refmem_cmd_load_submit_type(uint8_t type,
                                 const uint8_t *wire,
                                 uint32_t wire_size)
{
    if (wire == NULL || wire_size == 0u ||
        wire_size > TDMA_MAINTENANCE_RUNTIME_MAX || s_initialized == 0u) {
        s_rejected++;
        return false;
    }
    const uint8_t generation = s_generation++;
    memcpy(s_tx_frame, wire, wire_size);
    if (!tdma_maintenance_runtime_submit(s_tx_frame, wire_size,
                                         type, generation)) {
        s_rejected++;
        s_tx_fail++;
        return false;
    }
    s_submitted++;
    s_tx_ok++;
    return true;
}

void refmem_cmd_load_service(void)
{
    if (s_initialized == 0u) {
        return;
    }
    uint8_t frame[TDMA_MAINTENANCE_RUNTIME_MAX];
    size_t frame_size = 0u;
    uint8_t stream_id = 0u;
    uint8_t generation = 0u;
    while (tdma_maintenance_runtime_take(frame, sizeof(frame), &frame_size,
                                         &stream_id, &generation)) {
        s_rx_frames++;
        (void)tdma_second_class_parser_dispatch(stream_id, frame, frame_size);
    }
}

bool refmem_cmd_load_get_status(refmem_cmd_load_status_t *status)
{
    if (status == NULL) {
        return false;
    }
    memset(status, 0, sizeof(*status));
    status->load_initialized = s_initialized;
    status->submitted = s_submitted;
    status->served = s_tx_ok;
    status->rejected = s_rejected;
    status->rx_events = s_rx_frames;
    status->decoded = s_decoded;
    status->posted = s_posted;
    status->acked = s_acked;
    status->tx_ok = s_tx_ok;
    status->tx_fail = s_tx_fail;
    /* No standalone load frame is built on the maintenance-stream path. */
    status->tx_stage = 0u;
    status->build_class = 0u;
    status->build_size = 0u;
    status->build_load = 0u;
    uint32_t mailbox_calls = 0u;
    uint32_t chunks = 0u;
    uint32_t frames = 0u;
    uint32_t rejects = 0u;
    tdma_maintenance_runtime_get_counters(&mailbox_calls, &chunks, &frames,
                                          &rejects);
    status->rx_seen = chunks;
    status->rx_delivered = frames;
    status->parser_registered = tdma_second_class_parser_registered_count();
    status->parser_unknown = tdma_second_class_parser_unknown_count();
    tdma_second_class_parser_count_t count;
    if (tdma_second_class_parser_get_count(
            TDMA_SECOND_CLASS_TYPE_REFMEM_CMD, &count)) {
        status->refmem_parser_ok = count.accepted;
        status->refmem_parser_reject = count.rejected;
    }
    if (tdma_second_class_parser_get_count(
            TDMA_SECOND_CLASS_TYPE_T2_STATIC, &count)) {
        status->t2_static_ok = count.accepted;
        status->t2_static_reject = count.rejected;
    }
    status->t2_static_applied = tdma_t2_static_applied_count();
    uint32_t t2_target = 0u;
    uint32_t t2_config = 0u;
    uint32_t t2_value = 0u;
    (void)tdma_t2_static_get_last(&t2_target, &t2_config, &t2_value);
    status->t2_static_last_config = t2_config;
    status->t2_static_last_value = t2_value;
    if (tdma_second_class_parser_get_count(
            TDMA_SECOND_CLASS_TYPE_T2_DYNAMIC, &count)) {
        status->t2_dynamic_ok = count.accepted;
        status->t2_dynamic_reject = count.rejected;
    }
    if (s_adapter != NULL) {
        tdma_pio_spi_ring_adapter_get_rx_debug(
            s_adapter, &status->rx_drop_count,
            &status->last_rx_packet_size, &status->last_rx_payload_class,
            &status->rx_config_control, &status->rx_process_image);
        tdma_pio_spi_ring_adapter_get_maintenance_inject(
            s_adapter, &status->inject_attempts, &status->inject_ok);
    }
    return true;
}
