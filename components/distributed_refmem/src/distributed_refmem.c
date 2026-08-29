#include "distributed_refmem.h"

#include <string.h>

#include "drv_flash.h"
#include "osal.h"
#include "board_config.h"
#include "project_config.h"

#include "distributed_refmem_vdc_bridge.h"
#include "diagnostics.h"
#include "ota_ao.h"
#include "refmem_application_model.h"
#include "refmem_command.h"
#include "refmem_node_load_sync.h"
#include "refmem_quality.h"
#include "refmem_realtime_tdma.h"
#include "tdma_runtime_owner.h"
#include "refmem_slot_claim.h"
#include "refmem_spi_physical_adapter.h"
#include "refmem_sync.h"
#include "refmem_table_registry.h"
#include "refmem_vector_table.h"
#include "tdma_flight_engine.h"
#include "tdma_process_image_layout.h"
#include "tdma_process_image_map.h"
#include "tdma_profile.h"
#include "vdc_dpll_manager.h"

#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define DISTRIBUTED_REFMEM_TIME_CRITICAL(name) __not_in_flash_func(name)
#else
#define DISTRIBUTED_REFMEM_TIME_CRITICAL(name) name
#endif

#define DISTRIBUTED_REFMEM_NODE_LOAD_OWNER_COUNT 16u
#define DISTRIBUTED_REFMEM_SOURCE_INSTANCE_REFMEM_AO 0u
#define DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_QUEUE_COUNT 8u
#define DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_EPOCH 1u
#define DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_RUN 1u
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE \
    TDMA_FLIGHT_SHORT_SLOT_SIZE
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_PAYLOAD_SIZE \
    TDMA_FLIGHT_SHORT_PAYLOAD_SIZE
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT \
    TDMA_FLIGHT_SHORT_SLOT_COUNT
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_INTERVAL_MS 1u
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_MAGIC \
    TDMA_FLIGHT_MAILBOX_MAGIC
#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_VERSION \
    TDMA_FLIGHT_MAILBOX_VERSION

typedef enum {
    DISTRIBUTED_REFMEM_AUTO_INTENT_NONE = 0u,
    DISTRIBUTED_REFMEM_AUTO_INTENT_TX_NODE_LOAD = 1u,
    DISTRIBUTED_REFMEM_AUTO_INTENT_RX_WINDOW = 2u,
} distributed_refmem_auto_intent_t;

typedef struct {
    uint32_t instance_id;
    distributed_refmem_node_load_owner_t owner;
    void *context;
} distributed_refmem_node_load_owner_entry_t;

typedef struct {
    uint32_t enabled;
    uint8_t local_slot;
    uint8_t target_mask;
    uint32_t epoch_id;
    uint32_t run_id;
    uint32_t baud_hz;
    uint32_t deadline_us;
    uint32_t uplink_duplex_mode;
    uint32_t downlink_duplex_mode;
    refmem_spi_physical_pin_config_t uplink_adapter_pins;
    refmem_spi_physical_pin_config_t downlink_adapter_pins;
    uint32_t pending_instance[DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_QUEUE_COUNT];
    uint32_t pending_count;
    uint32_t active_intent;
    uint32_t active_instance_id;
    uint32_t active_intent_seq;
    uint32_t last_processed_completed_seq;
    uint32_t next_seq32;
    uint32_t submitted_tx_count;
    uint32_t submitted_rx_count;
    uint32_t applied_rx_count;
    uint32_t failed_apply_count;
    uint32_t dropped_pending_count;
    uint32_t last_rx_result;
    uint32_t last_frame_type;
    uint32_t last_source_slot;
    uint32_t last_error;
} distributed_refmem_node_load_auto_sync_t;

static refmem_vector_table_t s_distributed_refmem_table __attribute__((aligned(4)));
static refmem_command_slot_t s_refmem_command_slot;
static refmem_realtime_tdma_service_t s_refmem_realtime_tdma;
static refmem_spi_physical_adapter_t s_refmem_realtime_spi;
static distributed_refmem_node_load_owner_entry_t
    s_node_load_owners[DISTRIBUTED_REFMEM_NODE_LOAD_OWNER_COUNT];
static distributed_refmem_status_t s_status;
static refmem_sync_context_t s_refmem_sync_context;
static distributed_refmem_node_load_auto_sync_t s_node_load_auto_sync;
static uint32_t s_service_count;
static bool s_initialized;
static uint32_t s_tdma_ring_log_last_ms;
static bool s_tdma_ring_log_enabled;
static uint32_t s_vdc_vector_publish_sequence;
static uint32_t s_dpll_vector_publish_sequence;
static uint32_t s_vdc_vector_source_update_seq = UINT32_MAX;
static uint32_t s_dpll_vector_source_update_seq = UINT32_MAX;
static uint32_t s_next_runtime_vector;

typedef struct {
    uint32_t enabled;
    uint32_t local_slot;
    uint32_t node_count;
    uint32_t active_mask;
    uint32_t reference_slot;
    uint32_t remote_slot;
    uint32_t publish_interval_ms;
    uint32_t last_publish_ms;
    uint32_t next_seq32;
    uint32_t tx_publish_count;
    uint32_t tx_reject_count;
    uint32_t rx_acquire_count;
    uint32_t rx_empty_count;
    uint32_t rx_accept_count;
    uint32_t rx_reject_count;
    uint32_t rx_duplicate_skip_count;
    uint32_t rx_bad_mailbox_count;
    uint32_t rx_seen_mask;
    uint32_t rx_last_seq_by_source[REFMEM_SYNC_NODE_COUNT];
    uint32_t last_rx_result;
    uint32_t last_frame_type;
    uint32_t last_source_slot;
    uint32_t last_seq32;
    uint32_t last_value_u32;
    int32_t last_vdc_phase_offset_ns;
    int32_t last_vdc_rate_adjust_ppb;
    uint32_t last_vdc_lock_state;
    uint32_t last_vdc_quality;
    uint32_t last_ack_seq16;
    uint32_t last_ack_flags;
    uint32_t last_control_opcode;
    uint32_t last_control_seq8;
    uint32_t last_optional_diagnostic;
    uint32_t last_mailbox_crc16;
    uint32_t last_error;
    refmem_sync_context_t context;
    uint8_t tx_image[DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_PAYLOAD_SIZE];
} distributed_refmem_tdma_flight_sync_t;

static distributed_refmem_tdma_flight_sync_t s_tdma_flight_sync;
static volatile uint32_t s_tdma_ring_arm_last_result =
    DISTRIBUTED_REFMEM_TDMA_ARM_NOT_ATTEMPTED;

static uint32_t distributed_refmem_flight_input_offset_for_slot(uint32_t slot)
{
    return slot * DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE;
}

static uint32_t distributed_refmem_flight_publish_mask_for_slot(uint32_t slot)
{
    return slot < TDMA_PROCESS_IMAGE_SEGMENT_COUNT ? (1u << slot) : 0u;
}

static tdma_process_image_map_t distributed_refmem_default_flight_map(void)
{
    tdma_process_image_map_t map;
    memset(&map, 0, sizeof(map));
    map.version = TDMA_PROCESS_IMAGE_MAP_VERSION;
    map.payload_size = DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_PAYLOAD_SIZE;
    map.segment_count = DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT;
    for (uint32_t slot = 0u;
         slot < DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT;
         slot++) {
        map.segment[slot].used = 1u;
        map.segment[slot].segment_id = slot;
        map.segment[slot].owner_slot_id = slot;
        map.segment[slot].payload_class =
            TDMA_PAYLOAD_CLASS_CYCLIC_PROCESS_IMAGE;
        map.segment[slot].byte_offset =
            slot * DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE;
        map.segment[slot].byte_length =
            DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE;
        map.segment[slot].flags = TDMA_PROCESS_SEGMENT_FLAG_FLIGHT_WRITE;
    }
    map.map_crc32 = tdma_process_image_map_crc32(&map);
    return map;
}

static void distributed_refmem_log_tdma_ring_service(void);

static void distributed_refmem_tdma_flight_sync_init(void)
{
    memset(&s_tdma_flight_sync, 0, sizeof(s_tdma_flight_sync));
    s_tdma_flight_sync.enabled = 1u;
    s_tdma_flight_sync.local_slot = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    s_tdma_flight_sync.reference_slot = 0u;
    s_tdma_flight_sync.remote_slot = 1u;
    s_tdma_flight_sync.publish_interval_ms =
        DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_INTERVAL_MS;
    s_tdma_flight_sync.next_seq32 = 1u;
    (void)refmem_sync_init(&s_tdma_flight_sync.context,
                           (uint8_t)s_tdma_flight_sync.local_slot,
                           DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_EPOCH,
                           DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_RUN);
}

static void distributed_refmem_tdma_flight_sync_update_ring(
    const tdma_ring_runtime_snapshot_t *ring)
{
    if (ring == NULL ||
        ring->local_slot_id >= DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT ||
        ring->node_count == 0u ||
        ring->node_count > DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT) {
        return;
    }
    if (s_tdma_flight_sync.local_slot == ring->local_slot_id &&
        s_tdma_flight_sync.reference_slot == ring->reference_slot_id &&
        s_tdma_flight_sync.node_count == ring->node_count) {
        return;
    }
    s_tdma_flight_sync.local_slot = ring->local_slot_id;
    s_tdma_flight_sync.reference_slot = ring->reference_slot_id;
    s_tdma_flight_sync.node_count = ring->node_count;
    s_tdma_flight_sync.active_mask =
        ring->node_count >= 32u ? UINT32_MAX : ((1u << ring->node_count) - 1u);
    s_tdma_flight_sync.remote_slot =
        (ring->local_slot_id + 1u) % ring->node_count;
    s_tdma_flight_sync.rx_seen_mask = 0u;
    memset(s_tdma_flight_sync.rx_last_seq_by_source,
           0,
           sizeof(s_tdma_flight_sync.rx_last_seq_by_source));
    (void)refmem_sync_init(&s_tdma_flight_sync.context,
                           (uint8_t)s_tdma_flight_sync.local_slot,
                           DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_EPOCH,
                           DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_RUN);
}

static void distributed_refmem_put_le16(uint8_t *dst, uint16_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)(value >> 8u);
}

static void distributed_refmem_put_i16(uint8_t *dst, int16_t value)
{
    distributed_refmem_put_le16(dst, (uint16_t)value);
}

static void distributed_refmem_put_le32(uint8_t *dst, uint32_t value)
{
    dst[0] = (uint8_t)(value & 0xFFu);
    dst[1] = (uint8_t)((value >> 8u) & 0xFFu);
    dst[2] = (uint8_t)((value >> 16u) & 0xFFu);
    dst[3] = (uint8_t)((value >> 24u) & 0xFFu);
}

static uint16_t distributed_refmem_get_le16(const uint8_t *src)
{
    return (uint16_t)(((uint16_t)src[0]) | ((uint16_t)src[1] << 8u));
}

static int16_t distributed_refmem_get_i16(const uint8_t *src)
{
    return (int16_t)distributed_refmem_get_le16(src);
}

static uint32_t distributed_refmem_get_le32(const uint8_t *src)
{
    return ((uint32_t)src[0]) |
           ((uint32_t)src[1] << 8u) |
           ((uint32_t)src[2] << 16u) |
           ((uint32_t)src[3] << 24u);
}

static bool distributed_refmem_tdma_flight_build_compact_mailbox(
    uint8_t source_slot,
    uint8_t target_mask,
    uint32_t seq32,
    uint32_t value,
    uint8_t *mailbox,
    size_t mailbox_size)
{
    if (mailbox == NULL ||
        mailbox_size < DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE) {
        return false;
    }
    memset(mailbox, 0, mailbox_size);
    distributed_refmem_put_le16(&mailbox[0],
                                DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_MAGIC);
    mailbox[2] = DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_VERSION;
    mailbox[3] = TDMA_PROCESS_IMAGE_MESSAGE_CLASS;
    mailbox[4] = source_slot;
    mailbox[5] = target_mask;
    distributed_refmem_put_le16(&mailbox[6], (uint16_t)(seq32 & 0xFFFFu));

    vdc_domain_snapshot_t vdc;
    memset(&vdc, 0, sizeof(vdc));
    if (vdc_dpll_manager_get_snapshot(&vdc)) {
        distributed_refmem_put_i16(
            &mailbox[TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET],
            tdma_process_image_quantize_i16(
                vdc.dco.phase_offset_ns,
                TDMA_PROCESS_IMAGE_VDC_PHASE_QUANTUM_NS));
        distributed_refmem_put_i16(
            &mailbox[TDMA_PROCESS_IMAGE_VDC_RATE_OFFSET],
            tdma_process_image_quantize_i16(
                vdc.dco.period_adjust_ppb,
                TDMA_PROCESS_IMAGE_VDC_RATE_QUANTUM_PPB));
        mailbox[TDMA_PROCESS_IMAGE_VDC_LOCK_OFFSET] =
            (uint8_t)vdc.dpll.state;
        mailbox[TDMA_PROCESS_IMAGE_VDC_QUALITY_OFFSET] =
            (uint8_t)((vdc.quality.health_state &
                       TDMA_PROCESS_IMAGE_VDC_QUALITY_HEALTH_MASK) |
                      ((vdc.quality.lock_quality_tier <<
                        TDMA_PROCESS_IMAGE_VDC_QUALITY_TIER_SHIFT) &
                       TDMA_PROCESS_IMAGE_VDC_QUALITY_TIER_MASK) |
                      (vdc.ready != 0u
                           ? TDMA_PROCESS_IMAGE_VDC_QUALITY_VALID
                           : 0u));
    }

    distributed_refmem_put_le32(
        &mailbox[TDMA_PROCESS_IMAGE_REFMEM_GENERATION_OFFSET], seq32);
    distributed_refmem_put_le16(
        &mailbox[TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET],
        TDMA_PROCESS_IMAGE_REFMEM_BASELINE_FIELD_ID);
    distributed_refmem_put_le32(
        &mailbox[TDMA_PROCESS_IMAGE_REFMEM_VALUE_OFFSET], value);

    uint8_t ack_flags = 0u;
    uint16_t ack_seq16 = 0u;
    if (s_tdma_flight_sync.remote_slot < REFMEM_SYNC_NODE_COUNT) {
        const uint32_t remote_bit = 1u << s_tdma_flight_sync.remote_slot;
        ack_seq16 = (uint16_t)(s_tdma_flight_sync.rx_last_seq_by_source[
            s_tdma_flight_sync.remote_slot] & 0xFFFFu);
        if ((s_tdma_flight_sync.rx_seen_mask & remote_bit) != 0u) {
            ack_flags |= TDMA_PROCESS_IMAGE_ACK_FLAG_VALID;
        }
    }
    if (s_tdma_flight_sync.last_error != 0u) {
        ack_flags |= TDMA_PROCESS_IMAGE_ACK_FLAG_NACK;
    }
    ack_flags |= (uint8_t)((s_tdma_flight_sync.last_rx_result <<
                            TDMA_PROCESS_IMAGE_ACK_QUALITY_SHIFT) &
                           TDMA_PROCESS_IMAGE_ACK_QUALITY_MASK);
    distributed_refmem_put_le16(
        &mailbox[TDMA_PROCESS_IMAGE_ACK_SEQ16_OFFSET], ack_seq16);
    mailbox[TDMA_PROCESS_IMAGE_ACK_FLAGS_OFFSET] = ack_flags;
    mailbox[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET] =
        TDMA_PROCESS_IMAGE_CONTROL_OPCODE_NONE;
    mailbox[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET] = 0u;
    mailbox[TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_OFFSET] =
        (uint8_t)(s_tdma_flight_sync.last_error & 0xFFu);
    distributed_refmem_put_le16(
        &mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET],
        tdma_process_image_crc16_ccitt(mailbox,
                                       TDMA_PROCESS_IMAGE_CRC_OFFSET));
    return true;
}

static void distributed_refmem_tdma_flight_sync_store_mailbox(
    uint8_t source_slot,
    const uint8_t *mailbox,
    size_t mailbox_size)
{
    if (source_slot >= DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT ||
        mailbox == NULL ||
        mailbox_size < DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE) {
        return;
    }
    memcpy(&s_tdma_flight_sync.tx_image[
               distributed_refmem_flight_input_offset_for_slot(source_slot)],
           mailbox,
           DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE);
}

static bool distributed_refmem_tdma_flight_expand_compact_delta(
    const uint8_t *mailbox,
    size_t mailbox_size,
    uint8_t *frame,
    size_t frame_capacity,
    size_t *frame_size)
{
    if (frame_size != NULL) {
        *frame_size = 0u;
    }
    if (mailbox == NULL || frame == NULL || frame_size == NULL ||
        mailbox_size < DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE ||
        distributed_refmem_get_le16(&mailbox[0]) !=
            DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_MAGIC ||
        mailbox[2] != DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_VERSION ||
        mailbox[3] != TDMA_PROCESS_IMAGE_MESSAGE_CLASS ||
        mailbox[4] >= REFMEM_SYNC_NODE_COUNT ||
        distributed_refmem_get_le16(
            &mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET]) !=
            tdma_process_image_crc16_ccitt(
                mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET)) {
        return false;
    }

    uint8_t payload[sizeof(refmem_sync_delta_header_t) + sizeof(uint32_t)];
    refmem_sync_delta_header_t delta;
    memset(&delta, 0, sizeof(delta));
    const uint32_t seq32 = distributed_refmem_get_le32(
        &mailbox[TDMA_PROCESS_IMAGE_REFMEM_GENERATION_OFFSET]);
    delta.delta_id = distributed_refmem_get_le16(&mailbox[6]);
    delta.slot_id = mailbox[4];
    delta.payload_kind = REFMEM_APP_DATA_U32;
    delta.slot_seq = seq32;
    delta.field_id = distributed_refmem_get_le16(
        &mailbox[TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET]);
    delta.field_offset = 0u;
    delta.field_width = sizeof(uint32_t);
    delta.dirty_mask = 1u;
    memcpy(payload, &delta, sizeof(delta));
    memcpy(&payload[sizeof(delta)],
           &mailbox[TDMA_PROCESS_IMAGE_REFMEM_VALUE_OFFSET],
           sizeof(uint32_t));

    refmem_sync_frame_header_t header;
    if (!refmem_sync_frame_header_init(
            &header,
            REFMEM_SYNC_FRAME_DELTA,
            REFMEM_SYNC_FRAME_FLAG_ACK_REQUEST,
            mailbox[4],
            mailbox[5],
            DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_EPOCH,
            DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_RUN,
            seq32,
            0u,
            0u,
            payload,
            sizeof(payload))) {
        return false;
    }
    return refmem_sync_frame_encode(&header,
                                    payload,
                                    sizeof(payload),
                                    frame,
                                    frame_capacity,
                                    frame_size);
}

static void distributed_refmem_tdma_flight_parse_mailbox(
    const uint8_t *mailbox,
    size_t mailbox_size)
{
    if (mailbox == NULL ||
        mailbox_size < DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE) {
        s_tdma_flight_sync.rx_bad_mailbox_count++;
        return;
    }
    if (distributed_refmem_get_le16(&mailbox[0]) !=
        DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_MAGIC) {
        s_tdma_flight_sync.rx_empty_count++;
        return;
    }

    uint8_t frame[REFMEM_SYNC_FRAME_HEADER_SIZE +
                  sizeof(refmem_sync_delta_header_t) + sizeof(uint32_t)];
    size_t frame_size = 0u;
    if (!distributed_refmem_tdma_flight_expand_compact_delta(mailbox,
                                                             mailbox_size,
                                                             frame,
                                                             sizeof(frame),
                                                             &frame_size)) {
        s_tdma_flight_sync.rx_bad_mailbox_count++;
        s_tdma_flight_sync.last_error = 2u;
        return;
    }

    refmem_sync_frame_header_t header;
    if (refmem_sync_frame_decode_header(frame,
                                        frame_size,
                                        &header) != REFMEM_SYNC_FRAME_OK ||
        header.source_slot >= REFMEM_SYNC_NODE_COUNT) {
        s_tdma_flight_sync.rx_bad_mailbox_count++;
        s_tdma_flight_sync.last_error = 7u;
        return;
    }
    const uint32_t source_bit = 1u << header.source_slot;
    refmem_sync_rx_snapshot_t rx;
    const refmem_sync_rx_result_t result =
        refmem_sync_receive_frame(&s_tdma_flight_sync.context,
                                  frame,
                                  frame_size,
                                  &rx);
    s_tdma_flight_sync.rx_seen_mask |= source_bit;
    s_tdma_flight_sync.rx_last_seq_by_source[header.source_slot] = header.seq32;
    s_tdma_flight_sync.last_rx_result = result;
    s_tdma_flight_sync.last_frame_type = rx.header.frame_type;
    s_tdma_flight_sync.last_source_slot = rx.source_slot;
    s_tdma_flight_sync.last_seq32 = rx.header.seq32;
    s_tdma_flight_sync.last_vdc_phase_offset_ns =
        tdma_process_image_expand_i16(
            distributed_refmem_get_i16(
                &mailbox[TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET]),
            TDMA_PROCESS_IMAGE_VDC_PHASE_QUANTUM_NS);
    s_tdma_flight_sync.last_vdc_rate_adjust_ppb =
        tdma_process_image_expand_i16(
            distributed_refmem_get_i16(
                &mailbox[TDMA_PROCESS_IMAGE_VDC_RATE_OFFSET]),
            TDMA_PROCESS_IMAGE_VDC_RATE_QUANTUM_PPB);
    s_tdma_flight_sync.last_vdc_lock_state =
        mailbox[TDMA_PROCESS_IMAGE_VDC_LOCK_OFFSET];
    s_tdma_flight_sync.last_vdc_quality =
        mailbox[TDMA_PROCESS_IMAGE_VDC_QUALITY_OFFSET];
    s_tdma_flight_sync.last_ack_seq16 = distributed_refmem_get_le16(
        &mailbox[TDMA_PROCESS_IMAGE_ACK_SEQ16_OFFSET]);
    s_tdma_flight_sync.last_ack_flags =
        mailbox[TDMA_PROCESS_IMAGE_ACK_FLAGS_OFFSET];
    s_tdma_flight_sync.last_control_opcode =
        mailbox[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET];
    s_tdma_flight_sync.last_control_seq8 =
        mailbox[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET];
    s_tdma_flight_sync.last_optional_diagnostic =
        mailbox[TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_OFFSET];
    s_tdma_flight_sync.last_mailbox_crc16 = distributed_refmem_get_le16(
        &mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET]);
    if (result == REFMEM_SYNC_RX_ACCEPTED) {
        s_tdma_flight_sync.rx_accept_count++;
        s_tdma_flight_sync.last_error = 0u;
        if (rx.header.frame_type == (uint8_t)REFMEM_SYNC_FRAME_DELTA &&
            rx.payload != NULL &&
            rx.payload_size >=
                (uint16_t)(sizeof(refmem_sync_delta_header_t) + sizeof(uint32_t))) {
            const uint8_t *value =
                &rx.payload[sizeof(refmem_sync_delta_header_t)];
            s_tdma_flight_sync.last_value_u32 =
                ((uint32_t)value[0]) |
                ((uint32_t)value[1] << 8u) |
                ((uint32_t)value[2] << 16u) |
                ((uint32_t)value[3] << 24u);
        }
    } else if (result == REFMEM_SYNC_RX_DUPLICATE_SEQ) {
        s_tdma_flight_sync.rx_duplicate_skip_count++;
        s_tdma_flight_sync.last_error = 0u;
    } else {
        s_tdma_flight_sync.rx_reject_count++;
        s_tdma_flight_sync.last_error = 3u;
    }
}

static void distributed_refmem_tdma_flight_sync_publish(
    tdma_service_service_t *owner,
    const tdma_ring_runtime_snapshot_t *ring)
{
    if (owner == NULL || ring == NULL ||
        ring->enabled == 0u ||
        ring->adapter_started == 0u ||
        ring->data_enabled == 0u ||
        ring->node_count == 0u ||
        ring->node_count > DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT ||
        ring->local_slot_id >= ring->node_count) {
        return;
    }

    const uint32_t now_ms = osal_tick_ms();
    uint32_t publish_interval_ms = s_tdma_flight_sync.publish_interval_ms;
    const uint32_t ring_interval_ms =
        (ring->feedback_timeout_ns + 999999u) / 1000000u;
    if (ring_interval_ms > publish_interval_ms) {
        publish_interval_ms = ring_interval_ms;
    }
    if (now_ms - s_tdma_flight_sync.last_publish_ms < publish_interval_ms) {
        return;
    }

    /* TX is a latest-value process image, not an event log. Keep at most one
     * prepared image behind the core1-active image and coalesce while that
     * descriptor is pending. Calling publish against the occupied two-buffer
     * state turns normal bounded backpressure into a reject counter and can
     * also create artificial sequence gaps. */
    tdma_flight_fifo_snapshot_t fifo;
    if (!tdma_service_get_flight_fifo_snapshot(owner, &fifo) ||
        fifo.tx_ready_count != 0u) {
        return;
    }
    s_tdma_flight_sync.last_publish_ms = now_ms;

    uint8_t frame[DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE];
    size_t frame_size = 0u;
    const uint32_t seq32 = s_tdma_flight_sync.next_seq32++;
    if (s_tdma_flight_sync.next_seq32 == 0u) {
        s_tdma_flight_sync.next_seq32 = 1u;
    }
    if (!distributed_refmem_tdma_flight_build_compact_mailbox(
            (uint8_t)ring->local_slot_id,
            (uint8_t)s_tdma_flight_sync.active_mask,
            seq32,
            s_service_count,
            frame,
            sizeof(frame))) {
        s_tdma_flight_sync.tx_reject_count++;
        s_tdma_flight_sync.last_error = 4u;
        return;
    }
    frame_size = sizeof(frame);

    distributed_refmem_tdma_flight_sync_store_mailbox(ring->local_slot_id,
                                                      frame,
                                                      frame_size);

    if (tdma_service_publish_flight_tx(
            owner,
            s_tdma_flight_sync.tx_image,
            sizeof(s_tdma_flight_sync.tx_image),
            seq32,
            seq32,
            distributed_refmem_flight_publish_mask_for_slot(
                ring->local_slot_id))) {
        s_tdma_flight_sync.tx_publish_count++;
    } else {
        s_tdma_flight_sync.tx_reject_count++;
        s_tdma_flight_sync.last_error = 5u;
    }
}

static void distributed_refmem_tdma_flight_sync_receive(
    tdma_service_service_t *owner,
    const tdma_ring_runtime_snapshot_t *ring)
{
    if (owner == NULL || ring == NULL ||
        ring->local_slot_id >=
            DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT) {
        return;
    }

    for (;;) {
        tdma_flight_rx_view_t view;
        if (!tdma_service_acquire_flight_rx(owner, &view)) {
            break;
        }
        s_tdma_flight_sync.rx_acquire_count++;
        if (view.data != NULL &&
            view.data_size == DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_PAYLOAD_SIZE) {
            uint32_t scan_mask = view.segment_mask;
            scan_mask &= s_tdma_flight_sync.active_mask;
            scan_mask &= ~(1u << ring->local_slot_id);
            for (uint32_t slot = 0u;
                 slot < DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_SLOT_COUNT;
                 slot++) {
                if ((scan_mask & (1u << slot)) == 0u) {
                    continue;
                }
                const uint32_t offset =
                    distributed_refmem_flight_input_offset_for_slot(slot);
                const uint8_t *mailbox = &view.data[offset];
                if (distributed_refmem_get_le16(mailbox) !=
                        DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_MAGIC ||
                    mailbox[2] !=
                        DISTRIBUTED_REFMEM_TDMA_FLIGHT_COMPACT_VERSION) {
                    s_tdma_flight_sync.rx_empty_count++;
                    continue;
                }
                if (mailbox[4] == ring->local_slot_id ||
                    mailbox[4] >= s_tdma_flight_sync.node_count ||
                    (mailbox[5] & (1u << ring->local_slot_id)) == 0u) {
                    continue;
                }
                distributed_refmem_tdma_flight_parse_mailbox(
                    mailbox,
                    DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE);
            }
        } else {
            s_tdma_flight_sync.rx_bad_mailbox_count++;
            s_tdma_flight_sync.last_error = 6u;
        }
        (void)tdma_service_release_flight_rx(owner, view.slot_index);
    }
}

static void distributed_refmem_tdma_flight_sync_service(void)
{
    if (s_tdma_flight_sync.enabled == 0u) {
        return;
    }
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    if (owner == NULL ||
        __atomic_load_n(&owner->ring_runtime.enabled, __ATOMIC_ACQUIRE) == 0u) {
        return;
    }
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_ring_runtime_get_snapshot(&owner->ring_runtime, &ring)) {
        s_tdma_flight_sync.last_error = 1u;
        return;
    }
    distributed_refmem_tdma_flight_sync_update_ring(&ring);
    distributed_refmem_tdma_flight_sync_receive(owner, &ring);
    distributed_refmem_tdma_flight_sync_publish(owner, &ring);
}

static void distributed_refmem_node_load_auto_init(void)
{
    memset(&s_node_load_auto_sync, 0, sizeof(s_node_load_auto_sync));
    s_node_load_auto_sync.enabled = 0u;
    s_node_load_auto_sync.local_slot = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    s_node_load_auto_sync.target_mask = 0xFFu;
    s_node_load_auto_sync.epoch_id = DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_EPOCH;
    s_node_load_auto_sync.run_id = DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_RUN;
    s_node_load_auto_sync.baud_hz = BOARD_REFMEM_SPI_BAUD_HZ;
    s_node_load_auto_sync.deadline_us = 1000000u;
    s_node_load_auto_sync.uplink_duplex_mode =
        DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_HALF;
    s_node_load_auto_sync.downlink_duplex_mode =
        DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_HALF;
    s_node_load_auto_sync.uplink_adapter_pins.rx_pin = BOARD_REFMEM_SPI_RX_PIN;
    s_node_load_auto_sync.uplink_adapter_pins.csn_pin =
        REFMEM_SPI_PHYSICAL_PIN_UNUSED;
    s_node_load_auto_sync.uplink_adapter_pins.sck_pin = BOARD_REFMEM_SPI_SCK_PIN;
    s_node_load_auto_sync.uplink_adapter_pins.tx_pin = BOARD_REFMEM_SPI_TX_PIN;
    s_node_load_auto_sync.downlink_adapter_pins.rx_pin = BOARD_REFMEM_SPI_RX_PIN;
    s_node_load_auto_sync.downlink_adapter_pins.csn_pin =
        REFMEM_SPI_PHYSICAL_PIN_UNUSED;
    s_node_load_auto_sync.downlink_adapter_pins.sck_pin = BOARD_REFMEM_SPI_SCK_PIN;
    s_node_load_auto_sync.downlink_adapter_pins.tx_pin = BOARD_REFMEM_SPI_TX_PIN;
    s_node_load_auto_sync.next_seq32 = 1u;
    (void)refmem_sync_init(&s_refmem_sync_context,
                           s_node_load_auto_sync.local_slot,
                           s_node_load_auto_sync.epoch_id,
                           s_node_load_auto_sync.run_id);
}

static bool distributed_refmem_node_load_auto_enqueue(uint32_t instance_id)
{
    if (instance_id == 0u ||
        instance_id >= REFMEM_APP_MODEL_INSTANCE_COUNT) {
        return false;
    }

    for (uint32_t i = 0u; i < s_node_load_auto_sync.pending_count; i++) {
        if (s_node_load_auto_sync.pending_instance[i] == instance_id) {
            return true;
        }
    }
    if (s_node_load_auto_sync.pending_count >=
        DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_QUEUE_COUNT) {
        s_node_load_auto_sync.dropped_pending_count++;
        s_node_load_auto_sync.last_error = 1u;
        return false;
    }

    s_node_load_auto_sync.pending_instance[s_node_load_auto_sync.pending_count] =
        instance_id;
    s_node_load_auto_sync.pending_count++;
    return true;
}

static bool distributed_refmem_fill_vdc_data_window_plan(
    refmem_realtime_tdma_intent_config_t *config)
{
    vdc_tdma_window_plan_t plan;
    vdc_gate_result_t gate;
    if (config == NULL) {
        return false;
    }
    if (!vdc_dpll_manager_plan_tdma_window(VDC_DOMAIN_WINDOW_REFMEM_DATA,
                                           VDC_DPLL_MANAGER_PLAN_NOW_NS,
                                           &plan,
                                           &gate)) {
        (void)gate;
        return false;
    }
    config->vdc_window_plan_valid = plan.valid;
    config->vdc_window_class = plan.window_class;
    config->vdc_schedule_crc32 = plan.schedule_crc32;
    config->vdc_window_start_ns = plan.window_start_ns;
    config->vdc_window_end_ns = plan.window_end_ns;
    config->vdc_guard_start_ns = plan.guard_start_ns;
    config->vdc_guard_end_ns = plan.guard_end_ns;
    return true;
}

static void distributed_refmem_node_load_auto_pop_front(void)
{
    if (s_node_load_auto_sync.pending_count == 0u) {
        return;
    }
    for (uint32_t i = 1u; i < s_node_load_auto_sync.pending_count; i++) {
        s_node_load_auto_sync.pending_instance[i - 1u] =
            s_node_load_auto_sync.pending_instance[i];
    }
    s_node_load_auto_sync.pending_count--;
    s_node_load_auto_sync.pending_instance[s_node_load_auto_sync.pending_count] = 0u;
}

static bool distributed_refmem_tdma_busy(const refmem_realtime_tdma_snapshot_t *snapshot)
{
    return snapshot != NULL &&
           snapshot->intent_seq > snapshot->completed_seq;
}

static bool distributed_refmem_node_load_auto_rx_preemptible(void)
{
    return s_node_load_auto_sync.enabled != 0u &&
           s_node_load_auto_sync.active_intent ==
               DISTRIBUTED_REFMEM_AUTO_INTENT_RX_WINDOW;
}

static void distributed_refmem_node_load_auto_preempt_rx(void)
{
    if (!distributed_refmem_node_load_auto_rx_preemptible()) {
        return;
    }

    refmem_realtime_tdma_abort(&s_refmem_realtime_tdma);
    s_node_load_auto_sync.active_intent = DISTRIBUTED_REFMEM_AUTO_INTENT_NONE;
    s_node_load_auto_sync.active_instance_id = 0u;
    s_node_load_auto_sync.active_intent_seq = 0u;
    s_node_load_auto_sync.last_error = 0u;
}

static uint32_t distributed_refmem_u32_payload_crc32(const uint32_t *fields,
                                                     uint32_t field_count)
{
    if (fields == NULL || field_count == 0u) {
        return 0u;
    }

    uint32_t crc = 2166136261u;
    const uint8_t *bytes = (const uint8_t *)fields;
    for (uint32_t i = 0u; i < field_count * (uint32_t)sizeof(uint32_t); i++) {
        crc ^= bytes[i];
        crc *= 16777619u;
    }
    return crc;
}

static uint32_t distributed_refmem_model_payload_crc32(uint32_t slot_id,
                                                       uint32_t output_index)
{
    const uint32_t fields[] = {slot_id, output_index};
    return distributed_refmem_u32_payload_crc32(
        fields,
        (uint32_t)(sizeof(fields) / sizeof(fields[0])));
}

static bool distributed_refmem_command_state_is_complete(uint32_t state)
{
    return state == REFMEM_COMMAND_STATE_ACKED ||
           state == REFMEM_COMMAND_STATE_NACKED ||
           state == REFMEM_COMMAND_STATE_TIMED_OUT;
}

static uint32_t distributed_refmem_next_command_seq(
    const refmem_command_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return 1u;
    }

    uint32_t seq = snapshot->last_completed_seq;
    if (snapshot->command_seq > seq) {
        seq = snapshot->command_seq;
    }
    seq++;
    return seq == 0u ? 1u : seq;
}

static bool distributed_refmem_post_command_replacing_complete(
    refmem_command_request_t *request,
    uint32_t issue_tick32)
{
    if (request == NULL) {
        return false;
    }

    osal_critical_enter();
    refmem_command_snapshot_t snapshot;
    if (!refmem_command_get_snapshot(&s_refmem_command_slot, &snapshot)) {
        osal_critical_exit();
        return false;
    }
    if (snapshot.command_seq != 0u) {
        if (!distributed_refmem_command_state_is_complete(snapshot.state) ||
            !refmem_command_clear(&s_refmem_command_slot, snapshot.command_seq)) {
            osal_critical_exit();
            return false;
        }
    }
    request->command_seq = distributed_refmem_next_command_seq(&snapshot);
    const bool ok = refmem_command_try_post(&s_refmem_command_slot,
                                            request,
                                            issue_tick32);
    osal_critical_exit();
    return ok;
}

static distributed_refmem_node_load_owner_entry_t *
distributed_refmem_find_node_load_owner(uint32_t instance_id)
{
    for (uint32_t i = 0u; i < DISTRIBUTED_REFMEM_NODE_LOAD_OWNER_COUNT; i++) {
        if (s_node_load_owners[i].owner != NULL &&
            s_node_load_owners[i].instance_id == instance_id) {
            return &s_node_load_owners[i];
        }
    }
    return NULL;
}

static bool distributed_refmem_execute_node_load_owner(uint32_t instance_id,
                                                       uint32_t slot_id,
                                                       uint32_t payload_ref)
{
    distributed_refmem_node_load_owner_entry_t *entry =
        distributed_refmem_find_node_load_owner(instance_id);
    if (entry == NULL) {
        return false;
    }
    return entry->owner(instance_id, slot_id, payload_ref, entry->context);
}

static bool distributed_refmem_slot_claim_gate_ready(void)
{
    refmem_slot_claim_map_t claim_map;
    refmem_slot_claim_gate_status_t claim_gate;
    if (!refmem_slot_claim_derive_map(refmem_application_model_get_generic_node_table(),
                                      refmem_application_model_get_board_capability_table(),
                                      refmem_application_model_get_node_load_table(),
                                      refmem_application_model_get_fb_instance_table(),
                                      &claim_map)) {
        return false;
    }
    return refmem_slot_claim_gate_evaluate(&claim_map, &claim_gate) &&
           claim_gate.ready != 0u;
}

static bool distributed_refmem_flash_activation_safe(void)
{
    distributed_refmem_runtime_protection_snapshot_t protection;
    distributed_refmem_get_runtime_protection(&protection);
    const bool ram_entry_ok =
        protection.ram_resident_required == 0u ||
        (protection.flags & DISTRIBUTED_REFMEM_PROT_RAM_RESIDENT_REQUIRED) != 0u;
    const bool flash_lockout_ok =
        protection.flash_lockout_supported == 0u ||
        protection.flash_lockout_online != 0u;
    const bool entry_owner_ok =
        protection.entry_table_owner == DISTRIBUTED_REFMEM_OWNER_SHARED;
    return ram_entry_ok && flash_lockout_ok && entry_owner_ok;
}

static bool distributed_refmem_tdma_profile_activation_ready(
    const tdma_foundation_profile_t *profile,
    uint32_t *schedule_crc32)
{
    tdma_profile_result_t profile_result = TDMA_PROFILE_BAD_ARGUMENT;
    vdc_tdma_ring_plan_t plan;
    if (profile == NULL || schedule_crc32 == NULL ||
        !tdma_foundation_profile_validate(profile, &profile_result) ||
        profile->resource.short_frame_capacity > TDMA_SERVICE_SHORT_FRAME_MAX ||
        profile->resource.long_frame_capacity > TDMA_SERVICE_LONG_FRAME_MAX ||
        !vdc_dpll_manager_plan_tdma_ring(&plan) || plan.valid == 0u ||
        plan.schedule_crc32 == 0u ||
        profile->ring.node_count != plan.ring_node_count ||
        profile->ring.local_index != plan.local_slot_id ||
        profile->ring.reference_index != plan.reference_slot_id ||
        profile->ring.upstream_slot_id != plan.upstream_slot_id ||
        profile->ring.downstream_slot_id != plan.downstream_slot_id ||
        profile->ring.feedback_slot_id != plan.feedback_slot_id ||
        profile->ring.flags != plan.ring_flags ||
        profile->ring.profile_crc32 != plan.ring_profile_crc32 ||
        profile->resource.cycle_period_ns != plan.cycle_period_ns) {
        return false;
    }

    *schedule_crc32 = plan.schedule_crc32;
    return true;
}

static bool distributed_refmem_apply_tdma_foundation_profile(
    const tdma_foundation_profile_t *profile,
    uint32_t schedule_crc32)
{
    return refmem_realtime_tdma_configure_foundation_profile(
        &s_refmem_realtime_tdma,
        profile,
        schedule_crc32);
}

static refmem_command_reason_t distributed_refmem_activation_nack_reason(uint32_t result)
{
    switch ((refmem_table_activation_result_t)result) {
    case REFMEM_TABLE_ACTIVATE_ERR_GATE:
        return REFMEM_COMMAND_REASON_RUN_STATE_DENIED;
    case REFMEM_TABLE_ACTIVATE_ERR_IMAGE_BUSY:
        return REFMEM_COMMAND_REASON_RESOURCE_BUSY;
    case REFMEM_TABLE_ACTIVATE_ERR_IMAGE_TOO_LARGE:
        return REFMEM_COMMAND_REASON_PAYLOAD_CRC_MISMATCH;
    case REFMEM_TABLE_ACTIVATE_ERR_STAGING_VIEW_INVALID:
    case REFMEM_TABLE_ACTIVATE_ERR_RUNTIME_PROFILE:
        return REFMEM_COMMAND_REASON_CONFIG_VALIDATION_FAILED;
    case REFMEM_TABLE_ACTIVATE_ERR_BAD_ARGUMENT:
    case REFMEM_TABLE_ACTIVATE_ERR_NO_VALID_STAGING:
    case REFMEM_TABLE_ACTIVATE_ERR_IMAGE_NOT_LOADED:
    case REFMEM_TABLE_ACTIVATE_OK:
    default:
        return REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH;
    }
}

static refmem_vector_header_region_t *distributed_refmem_header(void)
{
    return refmem_vector_table_header(&s_distributed_refmem_table);
}

static refmem_vector_node_region_t *distributed_refmem_node_region(uint32_t node_id)
{
    return refmem_vector_table_node(&s_distributed_refmem_table, node_id);
}

static void distributed_refmem_publish_status_locked(void)
{
    const refmem_vector_header_region_t *header = distributed_refmem_header();
    const refmem_vector_node_region_t *local_node =
        distributed_refmem_node_region(DISTRIBUTED_REFMEM_LOCAL_NODE_ID);

    s_status.table_size = header->table_size;
    s_status.layout_version = header->layout_version;
    s_status.table_seq = header->table_seq;
    s_status.local_node_id = header->local_node_id;
    s_status.node_count = header->node_count;
    s_status.local_heartbeat = local_node->heartbeat;
    s_status.service_count = s_service_count;
    s_status.flags = header->flags;
    s_status.initialized = s_initialized ? 1u : 0u;
}

static uint32_t distributed_refmem_header_crc_locked(void)
{
    return refmem_vector_header_crc(&s_distributed_refmem_table);
}

static refmem_vdc_vector_region_t *DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_vdc_vector_region)(void)
{
    return &s_distributed_refmem_table.vdc;
}

static refmem_dpll_vector_region_t *DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_dpll_vector_region)(void)
{
    return &s_distributed_refmem_table.dpll;
}

static void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_copy_to_volatile)(volatile uint8_t *destination,
                                         const void *source,
                                         size_t size)
{
    const uint8_t *bytes = (const uint8_t *)source;
    for (size_t i = 0u; i < size; i++) {
        destination[i] = bytes[i];
    }
}

static void distributed_refmem_copy_from_volatile(void *destination,
                                                  const volatile uint8_t *source,
                                                  size_t size)
{
    uint8_t *bytes = (uint8_t *)destination;
    for (size_t i = 0u; i < size; i++) {
        bytes[i] = source[i];
    }
}

static uint32_t DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_next_publish_sequence)(uint32_t *sequence)
{
    if (sequence == NULL) {
        return 1u;
    }
    (*sequence)++;
    if (*sequence == 0u) {
        *sequence = 1u;
    }
    return *sequence;
}

static uint32_t DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_begin_vector_write)(volatile uint32_t *seqlock,
                                           uint32_t *stable_sequence)
{
    uint32_t current = __atomic_load_n(seqlock, __ATOMIC_ACQUIRE);
    current &= ~1u;
    if (current > (UINT32_MAX - 2u)) {
        current = 0u;
    }
    const uint32_t odd = current + 1u;
    const uint32_t even = current + 2u;
    __atomic_store_n(seqlock, odd, __ATOMIC_RELEASE);
    if (stable_sequence != NULL) {
        *stable_sequence = even;
    }
    return even;
}

static void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_publish_vdc_vector_payload)(
    refmem_vdc_vector_region_t *region,
    refmem_vdc_vector_payload_t *payload)
{
    if (region == NULL || payload == NULL) {
        return;
    }
    uint32_t stable_sequence = 0u;
    (void)distributed_refmem_begin_vector_write(&region->seqlock,
                                                &stable_sequence);
    payload->stable_sequence = stable_sequence;
    payload->payload_crc32 = 0u;
    payload->payload_crc32 = refmem_vdc_vector_payload_crc(payload);
    distributed_refmem_copy_to_volatile((volatile uint8_t *)&region->payload,
                                        payload,
                                        sizeof(*payload));
    __atomic_store_n(&region->seqlock, stable_sequence, __ATOMIC_RELEASE);
}

static void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_publish_dpll_vector_payload)(
    refmem_dpll_vector_region_t *region,
    refmem_dpll_vector_payload_t *payload)
{
    if (region == NULL || payload == NULL) {
        return;
    }
    uint32_t stable_sequence = 0u;
    (void)distributed_refmem_begin_vector_write(&region->seqlock,
                                                &stable_sequence);
    payload->stable_sequence = stable_sequence;
    payload->payload_crc32 = 0u;
    payload->payload_crc32 = refmem_dpll_vector_payload_crc(payload);
    distributed_refmem_copy_to_volatile((volatile uint8_t *)&region->payload,
                                        payload,
                                        sizeof(*payload));
    __atomic_store_n(&region->seqlock, stable_sequence, __ATOMIC_RELEASE);
}

static bool DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_vector_hardware_evidence_valid)(
    const vdc_domain_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    const uint32_t flags = snapshot->quality.last_timestamp_flags;
    return snapshot->quality.last_timestamp_source ==
               VDC_DOMAIN_TIMESTAMP_SOURCE_HARDWARE_TICK &&
           snapshot->quality.last_timestamp_resolution_ns != 0u &&
           snapshot->quality.last_timestamp_resolution_ns <=
               VDC_DOMAIN_DPLL_ADMISSION_TIMESTAMP_RESOLUTION_LIMIT_NS &&
           (flags & VDC_DOMAIN_TIMESTAMP_FLAG_DPLL_ELIGIBLE) != 0u &&
           (flags & VDC_DOMAIN_TIMESTAMP_FLAG_DIAGNOSTIC_ONLY) == 0u;
}

static void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_fill_vdc_vector_payload)(
    refmem_vdc_vector_payload_t *payload,
    const vdc_domain_snapshot_t *snapshot,
    uint32_t publish_sequence,
    bool snapshot_valid)
{
    if (payload == NULL) {
        return;
    }
    memset(payload, 0, sizeof(*payload));
    payload->layout_version = REFMEM_VDC_VECTOR_LAYOUT_VERSION;
    payload->writer = REFMEM_VECTOR_WRITER_CORE1;
    payload->publish_sequence = publish_sequence;
    if (!snapshot_valid || snapshot == NULL) {
        payload->flags = REFMEM_VECTOR_FLAG_STALE;
        return;
    }

    payload->flags = REFMEM_VECTOR_FLAG_VALID;
    const bool provisional =
        (snapshot->path_delay.flags &
         VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY) != 0u;
    if (provisional) {
        payload->flags |= REFMEM_VECTOR_FLAG_PROVISIONAL;
    }
    if (snapshot->schedule.enabled != 0u &&
        snapshot->schedule.schedule_crc32 != 0u) {
        payload->flags |= REFMEM_VECTOR_FLAG_SCHEDULE_VALID;
    }
    if (snapshot->path_delay.valid != 0u &&
        (snapshot->path_delay.flags &
         (VDC_PATH_DELAY_FLAG_ACCEPTED |
          VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
          VDC_PATH_DELAY_FLAG_BIAS_VALID |
          VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH)) ==
            (VDC_PATH_DELAY_FLAG_ACCEPTED |
             VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
             VDC_PATH_DELAY_FLAG_BIAS_VALID |
             VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH)) {
        payload->flags |= REFMEM_VECTOR_FLAG_CALIBRATION_VALID;
    }
    if (distributed_refmem_vector_hardware_evidence_valid(snapshot)) {
        payload->flags |= REFMEM_VECTOR_FLAG_HARDWARE_EVIDENCE;
    }
    if (!provisional &&
        snapshot->dpll.state == VDC_DOMAIN_LOCK_LOCKED) {
        payload->flags |= REFMEM_VECTOR_FLAG_LOCKED;
    }

    payload->source_update_seq = snapshot->dpll.update_seq;
    payload->source_service_count = (uint32_t)snapshot->service_count;
    payload->schedule_epoch = snapshot->schedule.schedule_epoch;
    payload->local_node_id = snapshot->schedule.local_slot_id;
    payload->reference_node_id = snapshot->schedule.reference_slot_id;
    payload->node_count = snapshot->schedule.ring_binding.node_count;
    payload->schedule_crc32 = snapshot->schedule.schedule_crc32;
    payload->servo_profile_crc32 = snapshot->servo.servo_profile_crc32;
    payload->path_delay_table_crc32 = snapshot->path_delay.table_crc32;
    payload->path_delay_generation = snapshot->path_delay.calibration_generation;
    payload->path_delay_freshness_us = snapshot->path_delay.freshness_us;

    payload->dpll_state = snapshot->dpll.state;
    payload->dpll_update_seq = snapshot->dpll.update_seq;
    payload->dpll_accepted_sample_count = snapshot->dpll.accepted_sample_count;
    payload->dpll_rejected_sample_count = snapshot->dpll.rejected_sample_count;
    payload->dpll_last_sample_seq = snapshot->dpll.last_sample_seq;
    payload->dpll_last_phase_error_ns = snapshot->dpll.last_phase_error_ns;
    payload->dpll_last_frequency_error_ppb = snapshot->dpll.last_frequency_error_ppb;
    payload->dpll_last_offset_ns = snapshot->dpll.last_offset_ns;
    payload->dpll_rms_offset_ns = snapshot->dpll.rms_offset_ns;
    payload->dpll_max_abs_offset_ns = snapshot->dpll.max_abs_offset_ns;
    payload->dpll_jitter_pk_ns = snapshot->dpll.jitter_pk_ns;
    payload->dpll_holdover_age_us = snapshot->dpll.holdover_age_us;

    payload->quality_health_state = snapshot->quality.health_state;
    payload->quality_lock_quality_tier = snapshot->quality.lock_quality_tier;
    payload->quality_flags = snapshot->quality.quality_flags;
    payload->quality_last_reject_code = snapshot->quality.last_reject_code;
    payload->quality_last_timestamp_source = snapshot->quality.last_timestamp_source;
    payload->quality_last_timestamp_resolution_ns =
        snapshot->quality.last_timestamp_resolution_ns;
    payload->quality_last_timestamp_flags = snapshot->quality.last_timestamp_flags;
    payload->quality_last_sample_age_us = snapshot->quality.last_sample_age_us;
    payload->quality_freshness_limit_us = snapshot->quality.freshness_limit_us;

    payload->gate_passed = snapshot->gate.passed;
    payload->gate_reject_code = snapshot->gate.reject_code;
    payload->gate_reject_slot = snapshot->gate.reject_slot;
    payload->gate_reject_evidence = snapshot->gate.reject_evidence;

    payload->clock_base_local_tick64 = snapshot->clock.base_local_tick64;
    payload->clock_base_vdc_time64_ns = snapshot->clock.base_vdc_time64_ns;
    payload->clock_phase_offset_ns = snapshot->clock.phase_offset_ns;
    payload->clock_period_adjust_ppb = snapshot->clock.period_adjust_ppb;
    payload->clock_nominal_period_ns = snapshot->clock.nominal_period_ns;
    payload->clock_model_seq = snapshot->clock.model_seq;
    payload->clock_slew_limit_ppb = snapshot->clock.slew_limit_ppb;
    payload->last_sample_time_ns = snapshot->quality.last_sample_time_ns;
}

static void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_fill_dpll_vector_payload)(
    refmem_dpll_vector_payload_t *payload,
    const vdc_domain_snapshot_t *snapshot,
    uint32_t publish_sequence,
    bool snapshot_valid)
{
    if (payload == NULL) {
        return;
    }
    memset(payload, 0, sizeof(*payload));
    payload->layout_version = REFMEM_DPLL_VECTOR_LAYOUT_VERSION;
    payload->writer = REFMEM_VECTOR_WRITER_CORE1;
    payload->publish_sequence = publish_sequence;
    if (!snapshot_valid || snapshot == NULL) {
        payload->flags = REFMEM_VECTOR_FLAG_STALE;
        return;
    }

    payload->flags = REFMEM_VECTOR_FLAG_VALID;
    const bool provisional =
        (snapshot->path_delay.flags &
         VDC_PATH_DELAY_FLAG_DIAGNOSTIC_ONLY) != 0u;
    if (provisional) {
        payload->flags |= REFMEM_VECTOR_FLAG_PROVISIONAL;
    }
    if (snapshot->schedule.enabled != 0u &&
        snapshot->schedule.schedule_crc32 != 0u) {
        payload->flags |= REFMEM_VECTOR_FLAG_SCHEDULE_VALID;
    }
    if (snapshot->path_delay.valid != 0u &&
        (snapshot->path_delay.flags &
         (VDC_PATH_DELAY_FLAG_ACCEPTED |
          VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
          VDC_PATH_DELAY_FLAG_BIAS_VALID |
          VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH)) ==
            (VDC_PATH_DELAY_FLAG_ACCEPTED |
             VDC_PATH_DELAY_FLAG_HARDWARE_LATCHED |
             VDC_PATH_DELAY_FLAG_BIAS_VALID |
             VDC_PATH_DELAY_FLAG_TOPOLOGY_FRESH)) {
        payload->flags |= REFMEM_VECTOR_FLAG_CALIBRATION_VALID;
    }
    if (distributed_refmem_vector_hardware_evidence_valid(snapshot)) {
        payload->flags |= REFMEM_VECTOR_FLAG_HARDWARE_EVIDENCE;
    }
    if (!provisional &&
        snapshot->dpll.state == VDC_DOMAIN_LOCK_LOCKED) {
        payload->flags |= REFMEM_VECTOR_FLAG_LOCKED;
    }

    payload->source_update_seq = snapshot->dpll.update_seq;
    payload->source_service_count = (uint32_t)snapshot->service_count;
    payload->ready = snapshot->ready;
    payload->schedule_epoch = snapshot->schedule.schedule_epoch;
    payload->local_node_id = snapshot->schedule.local_slot_id;
    payload->reference_node_id = snapshot->schedule.reference_slot_id;
    payload->node_count = snapshot->schedule.ring_binding.node_count;
    payload->schedule_crc32 = snapshot->schedule.schedule_crc32;
    payload->servo_profile_crc32 = snapshot->servo.servo_profile_crc32;

    payload->state = snapshot->dpll.state;
    payload->dpll_update_seq = snapshot->dpll.update_seq;
    payload->dpll_accepted_sample_count = snapshot->dpll.accepted_sample_count;
    payload->dpll_rejected_sample_count = snapshot->dpll.rejected_sample_count;
    payload->last_sample_seq = snapshot->dpll.last_sample_seq;
    payload->last_phase_error_ns = snapshot->dpll.last_phase_error_ns;
    payload->last_frequency_error_ppb = snapshot->dpll.last_frequency_error_ppb;
    payload->last_offset_ns = snapshot->dpll.last_offset_ns;
    payload->rms_offset_ns = snapshot->dpll.rms_offset_ns;
    payload->max_abs_offset_ns = snapshot->dpll.max_abs_offset_ns;
    payload->jitter_pk_ns = snapshot->dpll.jitter_pk_ns;
    payload->holdover_age_us = snapshot->dpll.holdover_age_us;

    payload->dco_valid = snapshot->dco.valid;
    payload->dco_update_seq = snapshot->dco.dco_update_seq;
    payload->dco_source_model_seq = snapshot->dco.source_model_seq;
    payload->dco_lock_state = snapshot->dco.lock_state;
    payload->dco_phase_offset_ns = snapshot->dco.phase_offset_ns;
    payload->dco_period_adjust_ppb = snapshot->dco.period_adjust_ppb;
    payload->dco_base_local_tick64 = snapshot->dco.base_local_tick64;
    payload->dco_base_vdc_time64_ns = snapshot->dco.base_vdc_time64_ns;
    payload->dco_nominal_period_ns = snapshot->dco.nominal_period_ns;
    payload->dco_slew_limit_ppb = snapshot->dco.slew_limit_ppb;

    payload->quality_health_state = snapshot->quality.health_state;
    payload->quality_lock_quality_tier = snapshot->quality.lock_quality_tier;
    payload->quality_last_reject_code = snapshot->quality.last_reject_code;
    payload->quality_last_timestamp_source = snapshot->quality.last_timestamp_source;
    payload->quality_last_timestamp_resolution_ns =
        snapshot->quality.last_timestamp_resolution_ns;
    payload->quality_last_timestamp_flags = snapshot->quality.last_timestamp_flags;
    payload->quality_last_sample_age_us = snapshot->quality.last_sample_age_us;
    payload->quality_freshness_limit_us = snapshot->quality.freshness_limit_us;
    payload->gate_passed = snapshot->gate.passed;
    payload->gate_reject_code = snapshot->gate.reject_code;
    payload->gate_reject_slot = snapshot->gate.reject_slot;
    payload->gate_reject_evidence = snapshot->gate.reject_evidence;
    payload->path_delay_table_crc32 = snapshot->path_delay.table_crc32;
    payload->path_delay_generation = snapshot->path_delay.calibration_generation;
    payload->path_delay_freshness_us = snapshot->path_delay.freshness_us;
}

static bool distributed_refmem_read_vdc_vector_snapshot(
    distributed_refmem_vdc_vector_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    refmem_vdc_vector_region_t *region = distributed_refmem_vdc_vector_region();
    for (uint32_t attempt = 0u; attempt < 8u; attempt++) {
        const uint32_t begin = __atomic_load_n(&region->seqlock,
                                               __ATOMIC_ACQUIRE);
        if (begin == 0u || (begin & 1u) != 0u) {
            continue;
        }
        refmem_vdc_vector_payload_t copy;
        distributed_refmem_copy_from_volatile(
            &copy,
            (const volatile uint8_t *)&region->payload,
            sizeof(copy));
        const uint32_t end = __atomic_load_n(&region->seqlock,
                                             __ATOMIC_ACQUIRE);
        if (begin != end || (end & 1u) != 0u || copy.stable_sequence != end ||
            !refmem_vdc_vector_payload_validate(&copy)) {
            continue;
        }
        *snapshot = copy;
        return true;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    return false;
}

static bool distributed_refmem_read_dpll_vector_snapshot(
    distributed_refmem_dpll_vector_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    refmem_dpll_vector_region_t *region = distributed_refmem_dpll_vector_region();
    for (uint32_t attempt = 0u; attempt < 8u; attempt++) {
        const uint32_t begin = __atomic_load_n(&region->seqlock,
                                               __ATOMIC_ACQUIRE);
        if (begin == 0u || (begin & 1u) != 0u) {
            continue;
        }
        refmem_dpll_vector_payload_t copy;
        distributed_refmem_copy_from_volatile(
            &copy,
            (const volatile uint8_t *)&region->payload,
            sizeof(copy));
        const uint32_t end = __atomic_load_n(&region->seqlock,
                                             __ATOMIC_ACQUIRE);
        if (begin != end || (end & 1u) != 0u || copy.stable_sequence != end ||
            !refmem_dpll_vector_payload_validate(&copy)) {
            continue;
        }
        *snapshot = copy;
        return true;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    return false;
}

static bool distributed_refmem_mark_init_failed(distributed_refmem_init_stage_t stage)
{
    s_initialized = false;
    s_status.initialized = 0u;
    s_status.init_stage = (uint32_t)stage;
    s_status.init_error = (uint32_t)stage;
    return false;
}

static uint32_t distributed_refmem_deadline_us_to_ms(uint32_t deadline_us)
{
    if (deadline_us == 0u) {
        return 1u;
    }
    const uint32_t rounded_ms = (deadline_us + 999u) / 1000u;
    return rounded_ms == 0u ? 1u : rounded_ms;
}

static bool distributed_refmem_tdma_transmit(void *context,
                                             const uint8_t *frame,
                                             size_t frame_size,
                                             refmem_spi_physical_role_t role,
                                             uint32_t baud_hz,
                                             const refmem_spi_physical_pin_config_t *pins,
                                             uint32_t deadline_us,
                                             refmem_realtime_tdma_exec_status_t *status)
{
    (void)deadline_us;
    refmem_spi_physical_adapter_t *adapter = (refmem_spi_physical_adapter_t *)context;
    if (adapter == NULL || status == NULL ||
        role != REFMEM_SPI_PHYSICAL_ROLE_MASTER ||
        !refmem_spi_physical_adapter_arm(adapter, role, baud_hz, pins)) {
        if (status != NULL) {
            status->result = REFMEM_REALTIME_TDMA_EXEC_ERROR;
            status->error = REFMEM_REALTIME_TDMA_RESULT_BAD_ARGUMENT;
        }
        return false;
    }

    const bool ok = refmem_spi_physical_adapter_transmit(adapter, frame, frame_size);
    refmem_spi_physical_snapshot_t snapshot;
    (void)refmem_spi_physical_adapter_get_snapshot(adapter, &snapshot);
    status->frame_size = frame_size;
    status->error = snapshot.last_error;
    status->result = ok ? REFMEM_REALTIME_TDMA_EXEC_TX_OK
                        : REFMEM_REALTIME_TDMA_EXEC_ERROR;
    return ok;
}

static bool distributed_refmem_tdma_receive(void *context,
                                            uint8_t *frame,
                                            size_t frame_capacity,
                                            refmem_spi_physical_role_t role,
                                            uint32_t baud_hz,
                                            const refmem_spi_physical_pin_config_t *pins,
                                            uint32_t deadline_us,
                                            refmem_realtime_tdma_exec_status_t *status)
{
    refmem_spi_physical_adapter_t *adapter = (refmem_spi_physical_adapter_t *)context;
    if (adapter == NULL || status == NULL || role != REFMEM_SPI_PHYSICAL_ROLE_SLAVE) {
        if (status != NULL) {
            status->result = REFMEM_REALTIME_TDMA_EXEC_ERROR;
            status->error = REFMEM_REALTIME_TDMA_RESULT_BAD_ARGUMENT;
        }
        return false;
    }

    size_t frame_size = 0u;
    refmem_spi_physical_snapshot_t snapshot;
    if (!adapter->rx_capture_active) {
        if (!refmem_spi_physical_adapter_arm(adapter, role, baud_hz, pins) ||
            !refmem_spi_physical_adapter_receive_begin(
                adapter,
                frame_capacity,
                distributed_refmem_deadline_us_to_ms(deadline_us))) {
            (void)refmem_spi_physical_adapter_get_snapshot(adapter, &snapshot);
            status->frame_size = 0u;
            status->error = snapshot.last_error;
            status->result = REFMEM_REALTIME_TDMA_EXEC_ERROR;
            return false;
        }

        (void)refmem_spi_physical_adapter_get_snapshot(adapter, &snapshot);
        status->frame_size = 0u;
        status->error = snapshot.last_error;
        status->result = REFMEM_REALTIME_TDMA_EXEC_PENDING;
        return false;
    }

    const refmem_spi_physical_rx_poll_result_t poll_result =
        refmem_spi_physical_adapter_receive_poll(adapter,
                                                 frame,
                                                 frame_capacity,
                                                 &frame_size);
    (void)refmem_spi_physical_adapter_get_snapshot(adapter, &snapshot);
    status->frame_size = frame_size;
    status->error = snapshot.last_error;
    if (poll_result == REFMEM_SPI_PHYSICAL_RX_POLL_PENDING) {
        status->result = REFMEM_REALTIME_TDMA_EXEC_PENDING;
        return false;
    }
    status->result = poll_result == REFMEM_SPI_PHYSICAL_RX_POLL_DONE
                         ? REFMEM_REALTIME_TDMA_EXEC_RX_OK
                         : REFMEM_REALTIME_TDMA_EXEC_TIMEOUT;
    return poll_result == REFMEM_SPI_PHYSICAL_RX_POLL_DONE;
}

static bool distributed_refmem_apply_node_load_sync_payload_internal(
    const uint8_t *payload,
    uint16_t payload_size)
{
    refmem_node_load_entry_t entry;
    if (!refmem_node_load_sync_decode_delta_payload(payload,
                                                   payload_size,
                                                   NULL,
                                                   &entry)) {
        return false;
    }

    return refmem_application_model_stage_scpi_node_config(entry.node_id,
                                                           entry.instance_id,
                                                           entry.role_mask,
                                                           entry.persona_mask,
                                                           entry.enabled,
                                                           entry.required,
                                                           entry.load_order);
}

static void distributed_refmem_node_load_auto_process_completed(
    const refmem_realtime_tdma_snapshot_t *snapshot)
{
    if (snapshot == NULL ||
        s_node_load_auto_sync.active_intent == DISTRIBUTED_REFMEM_AUTO_INTENT_NONE ||
        snapshot->completed_seq == s_node_load_auto_sync.last_processed_completed_seq ||
        snapshot->completed_seq < s_node_load_auto_sync.active_intent_seq) {
        return;
    }

    s_node_load_auto_sync.last_processed_completed_seq = snapshot->completed_seq;
    if (s_node_load_auto_sync.active_intent ==
        DISTRIBUTED_REFMEM_AUTO_INTENT_TX_NODE_LOAD) {
        distributed_refmem_node_load_auto_pop_front();
        s_node_load_auto_sync.active_intent = DISTRIBUTED_REFMEM_AUTO_INTENT_NONE;
        s_node_load_auto_sync.active_instance_id = 0u;
        return;
    }

    if (s_node_load_auto_sync.active_intent ==
        DISTRIBUTED_REFMEM_AUTO_INTENT_RX_WINDOW) {
        uint8_t frame[REFMEM_REALTIME_TDMA_FRAME_MAX];
        size_t frame_size = 0u;
        if (snapshot->last_result == REFMEM_REALTIME_TDMA_RESULT_FRAME_READY &&
            distributed_refmem_get_realtime_tdma_frame(frame,
                                                       sizeof(frame),
                                                       &frame_size)) {
            refmem_sync_rx_snapshot_t rx;
            const refmem_sync_rx_result_t result =
                refmem_sync_receive_frame(&s_refmem_sync_context,
                                          frame,
                                          frame_size,
                                          &rx);
            s_node_load_auto_sync.last_rx_result = result;
            s_node_load_auto_sync.last_frame_type = rx.header.frame_type;
            s_node_load_auto_sync.last_source_slot = rx.source_slot;
            if (result == REFMEM_SYNC_RX_ACCEPTED &&
                rx.header.frame_type == (uint8_t)REFMEM_SYNC_FRAME_DELTA &&
                distributed_refmem_apply_node_load_sync_payload_internal(
                    rx.payload,
                    rx.payload_size)) {
                s_node_load_auto_sync.applied_rx_count++;
                s_node_load_auto_sync.last_error = 0u;
            } else if (result == REFMEM_SYNC_RX_ACCEPTED &&
                       rx.header.frame_type != (uint8_t)REFMEM_SYNC_FRAME_DELTA) {
                s_node_load_auto_sync.last_error = 0u;
            } else {
                s_node_load_auto_sync.failed_apply_count++;
                s_node_load_auto_sync.last_error = 2u;
            }
        }
        s_node_load_auto_sync.active_intent = DISTRIBUTED_REFMEM_AUTO_INTENT_NONE;
        s_node_load_auto_sync.active_instance_id = 0u;
    }
}

static bool distributed_refmem_node_load_auto_submit_tx(void)
{
    if (s_node_load_auto_sync.pending_count == 0u) {
        return false;
    }

    refmem_node_load_entry_t entry;
    const uint32_t instance_id = s_node_load_auto_sync.pending_instance[0];
    if (!refmem_application_model_get_staging_node_load_entry(instance_id, &entry)) {
        distributed_refmem_node_load_auto_pop_front();
        s_node_load_auto_sync.failed_apply_count++;
        s_node_load_auto_sync.last_error = 3u;
        return false;
    }

    uint8_t frame[REFMEM_REALTIME_TDMA_FRAME_MAX];
    size_t frame_size = 0u;
    const uint32_t seq32 = s_node_load_auto_sync.next_seq32++;
    if (s_node_load_auto_sync.next_seq32 == 0u) {
        s_node_load_auto_sync.next_seq32 = 1u;
    }

    refmem_application_model_load_snapshot_t load;
    refmem_application_model_get_load_snapshot(&load);
    if (!refmem_node_load_sync_build_delta_frame(&entry,
                                                 s_node_load_auto_sync.local_slot,
                                                 s_node_load_auto_sync.target_mask,
                                                 s_node_load_auto_sync.epoch_id,
                                                 s_node_load_auto_sync.run_id,
                                                 seq32,
                                                 load.load_seq,
                                                 osal_tick_ms(),
                                                 frame,
                                                 sizeof(frame),
                                                 &frame_size)) {
        s_node_load_auto_sync.last_error = 4u;
        return false;
    }

    refmem_realtime_tdma_intent_config_t config = {
        .window_epoch = s_node_load_auto_sync.epoch_id,
        .window_index = seq32,
        .deadline_us = s_node_load_auto_sync.deadline_us,
        .role = REFMEM_SPI_PHYSICAL_ROLE_MASTER,
        .baud_hz = s_node_load_auto_sync.baud_hz,
        .pins = s_node_load_auto_sync.downlink_adapter_pins,
        .frame = frame,
        .frame_size = frame_size,
    };
    if (!distributed_refmem_fill_vdc_data_window_plan(&config)) {
        s_node_load_auto_sync.last_error = 12u;
        return false;
    }
    if (!refmem_realtime_tdma_submit_tx(&s_refmem_realtime_tdma, &config)) {
        s_node_load_auto_sync.last_error = 5u;
        return false;
    }

    refmem_realtime_tdma_snapshot_t snapshot;
    (void)refmem_realtime_tdma_get_snapshot(&s_refmem_realtime_tdma, &snapshot);
    s_node_load_auto_sync.active_intent = DISTRIBUTED_REFMEM_AUTO_INTENT_TX_NODE_LOAD;
    s_node_load_auto_sync.active_instance_id = instance_id;
    s_node_load_auto_sync.active_intent_seq = snapshot.intent_seq;
    s_node_load_auto_sync.submitted_tx_count++;
    s_node_load_auto_sync.last_error = 0u;
    return true;
}

static bool distributed_refmem_node_load_auto_submit_rx(void)
{
    const uint32_t seq32 = s_node_load_auto_sync.next_seq32++;
    if (s_node_load_auto_sync.next_seq32 == 0u) {
        s_node_load_auto_sync.next_seq32 = 1u;
    }

    refmem_realtime_tdma_intent_config_t config = {
        .window_epoch = s_node_load_auto_sync.epoch_id,
        .window_index = seq32,
        .deadline_us = s_node_load_auto_sync.deadline_us,
        .role = REFMEM_SPI_PHYSICAL_ROLE_SLAVE,
        .baud_hz = s_node_load_auto_sync.baud_hz,
        .pins = s_node_load_auto_sync.uplink_adapter_pins,
        .frame = NULL,
        .frame_size = 0u,
    };
    if (!distributed_refmem_fill_vdc_data_window_plan(&config)) {
        s_node_load_auto_sync.last_error = 13u;
        return false;
    }
    if (!refmem_realtime_tdma_submit_rx(&s_refmem_realtime_tdma, &config)) {
        s_node_load_auto_sync.last_error = 6u;
        return false;
    }

    refmem_realtime_tdma_snapshot_t snapshot;
    (void)refmem_realtime_tdma_get_snapshot(&s_refmem_realtime_tdma, &snapshot);
    s_node_load_auto_sync.active_intent = DISTRIBUTED_REFMEM_AUTO_INTENT_RX_WINDOW;
    s_node_load_auto_sync.active_instance_id = 0u;
    s_node_load_auto_sync.active_intent_seq = snapshot.intent_seq;
    s_node_load_auto_sync.submitted_rx_count++;
    s_node_load_auto_sync.last_error = 0u;
    return true;
}

static void distributed_refmem_node_load_auto_service(void)
{
    if (s_node_load_auto_sync.enabled == 0u) {
        return;
    }

    refmem_realtime_tdma_snapshot_t snapshot;
    if (!refmem_realtime_tdma_get_snapshot(&s_refmem_realtime_tdma, &snapshot)) {
        s_node_load_auto_sync.last_error = 7u;
        return;
    }

    distributed_refmem_node_load_auto_process_completed(&snapshot);

    if (distributed_refmem_tdma_busy(&snapshot)) {
        if (s_node_load_auto_sync.pending_count != 0u &&
            s_node_load_auto_sync.active_intent ==
                DISTRIBUTED_REFMEM_AUTO_INTENT_RX_WINDOW) {
            refmem_realtime_tdma_abort(&s_refmem_realtime_tdma);
        }
        return;
    }

    if (s_node_load_auto_sync.pending_count != 0u) {
        (void)distributed_refmem_node_load_auto_submit_tx();
        return;
    }

    if (s_node_load_auto_sync.active_intent ==
        DISTRIBUTED_REFMEM_AUTO_INTENT_NONE) {
        (void)distributed_refmem_node_load_auto_submit_rx();
    }
}

static void distributed_refmem_refresh_directory_flags_locked(void)
{
    refmem_vector_header_region_t *header = distributed_refmem_header();
    const uint32_t directory_crc32 = refmem_vector_directory_crc(&s_distributed_refmem_table);

    if (refmem_vector_table_validate_directory(&s_distributed_refmem_table)) {
        header->flags |= DISTRIBUTED_REFMEM_FLAG_DIRECTORY_VALID;
    } else {
        header->flags &= ~DISTRIBUTED_REFMEM_FLAG_DIRECTORY_VALID;
    }

    if (header->directory_crc32 == directory_crc32) {
        header->flags |= DISTRIBUTED_REFMEM_FLAG_DIRECTORY_CRC_VALID;
    } else {
        header->flags &= ~DISTRIBUTED_REFMEM_FLAG_DIRECTORY_CRC_VALID;
    }

    if (refmem_application_model_get_snapshot()->valid != 0u) {
        header->flags |= DISTRIBUTED_REFMEM_FLAG_APP_MODEL_VALID;
    } else {
        header->flags &= ~DISTRIBUTED_REFMEM_FLAG_APP_MODEL_VALID;
    }
}

static void distributed_refmem_publish_runtime_locked(void)
{
    refmem_vector_header_region_t *header = distributed_refmem_header();
    drv_flash_lockout_status_t flash_status;
    drv_flash_get_lockout_status(&flash_status);

#if PROJECT_USE_MULTICORE
    header->core_count = 2u;
    header->core1_vtor_owner = DISTRIBUTED_REFMEM_OWNER_CORE1;
    header->core1_irq_owner_mask = DISTRIBUTED_REFMEM_IRQ_PIO_MASK |
                                   DISTRIBUTED_REFMEM_IRQ_DMA_MASK |
                                   DISTRIBUTED_REFMEM_IRQ_CAPTURE_MASK |
                                   DISTRIBUTED_REFMEM_IRQ_TIMER_MASK;
    header->ram_resident_required = 1u;
#else
    header->core_count = 1u;
    header->core1_vtor_owner = DISTRIBUTED_REFMEM_OWNER_CORE0;
    header->core1_irq_owner_mask = 0u;
    header->ram_resident_required = 0u;
#endif

    header->core0_vtor_owner = DISTRIBUTED_REFMEM_OWNER_CORE0;
    header->core0_irq_owner_mask = DISTRIBUTED_REFMEM_IRQ_USB_MASK |
                                   DISTRIBUTED_REFMEM_IRQ_STORAGE_MASK |
                                   DISTRIBUTED_REFMEM_IRQ_OTA_MASK |
                                   DISTRIBUTED_REFMEM_IRQ_UI_MASK;
    header->entry_table_owner = DISTRIBUTED_REFMEM_OWNER_SHARED;
    header->flash_lockout_supported = flash_status.core1_lockout_supported ? 1u : 0u;
    header->flash_lockout_online = flash_status.core1_lockout_online ? 1u : 0u;
    header->flash_lockout_requested = flash_status.core1_lockout_requested ? 1u : 0u;
    header->flash_lockout_acknowledged = flash_status.core1_lockout_acknowledged ? 1u : 0u;
    header->core1_park_state = flash_status.park_state;
    header->flash_lockout_last_result = flash_status.last_result;
    header->flash_lockout_last_elapsed_us = flash_status.last_elapsed_us;
    header->flash_lockout_request_seq = flash_status.request_seq;
    header->flash_lockout_ack_seq = flash_status.ack_seq;
    header->flash_lockout_release_seq = flash_status.release_seq;
    header->flash_lockout_timeout_count = flash_status.timeout_count;
    header->flash_lockout_release_timeout_count = flash_status.release_timeout_count;
    header->runtime_protection_flags = 0u;
    if (header->ram_resident_required != 0u) {
        header->runtime_protection_flags |= DISTRIBUTED_REFMEM_PROT_RAM_RESIDENT_REQUIRED;
    }
    if (header->flash_lockout_supported != 0u && header->flash_lockout_online != 0u) {
        header->runtime_protection_flags |= DISTRIBUTED_REFMEM_PROT_FLASH_LOCKOUT_READY;
    }
    if (header->core1_park_state == DRV_FLASH_LOCKOUT_PARK_PARKED) {
        header->runtime_protection_flags |= DISTRIBUTED_REFMEM_PROT_CORE1_PARKED;
    }
    if (header->entry_table_owner == DISTRIBUTED_REFMEM_OWNER_SHARED) {
        header->runtime_protection_flags |= DISTRIBUTED_REFMEM_PROT_ENTRY_OWNER_VALID;
    }
    header->table_owner = REFMEM_VECTOR_TABLE_OWNER;
    header->header_stale = REFMEM_VECTOR_HEADER_STALE;
    distributed_refmem_refresh_directory_flags_locked();
    header->header_crc32 = distributed_refmem_header_crc_locked();
}

bool distributed_refmem_init(void)
{
    memset(&s_status, 0, sizeof(s_status));
    s_initialized = false;
    s_vdc_vector_publish_sequence = 0u;
    s_dpll_vector_publish_sequence = 0u;
    s_tdma_ring_log_enabled = PROJECT_ENABLE_TDMA_RING_LOG ? true : false;
    s_tdma_ring_log_last_ms = 0u;
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_APP_MODEL;
    if (!refmem_application_model_init()) {
        return distributed_refmem_mark_init_failed(
            DISTRIBUTED_REFMEM_INIT_STAGE_APP_MODEL);
    }
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_OWNER;
    tdma_service_service_t *tdma_owner = tdma_runtime_owner_get();
    if (tdma_owner == NULL ||
        !refmem_realtime_tdma_init_shared(&s_refmem_realtime_tdma,
                                          tdma_owner)) {
        return distributed_refmem_mark_init_failed(
            DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_OWNER);
    }
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_COMMAND_SLOT;
    if (!refmem_command_init(&s_refmem_command_slot, 0u)) {
        return distributed_refmem_mark_init_failed(
            DISTRIBUTED_REFMEM_INIT_STAGE_COMMAND_SLOT);
    }
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_NODE_LOAD_AUTO;
    distributed_refmem_node_load_auto_init();
    distributed_refmem_tdma_flight_sync_init();
    static const refmem_realtime_tdma_ops_t tdma_ops = {
        .transmit = distributed_refmem_tdma_transmit,
        .receive = distributed_refmem_tdma_receive,
    };
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_OPS;
    if (!refmem_realtime_tdma_bind_ops(&s_refmem_realtime_tdma,
                                       &tdma_ops,
                                       &s_refmem_realtime_spi)) {
        return distributed_refmem_mark_init_failed(
            DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_OPS);
    }
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_PROFILE;
    uint32_t tdma_schedule_crc32 = 0u;
    const tdma_foundation_profile_t *tdma_profile =
        refmem_application_model_get_tdma_foundation_profile();
    if (!distributed_refmem_tdma_profile_activation_ready(
            tdma_profile,
            &tdma_schedule_crc32) ||
        !distributed_refmem_apply_tdma_foundation_profile(
            tdma_profile,
            tdma_schedule_crc32)) {
        return distributed_refmem_mark_init_failed(
            DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_PROFILE);
    }
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_VECTOR_TABLE;

    osal_critical_enter();

    refmem_vector_table_clear(&s_distributed_refmem_table);

    refmem_vector_header_region_t *header = distributed_refmem_header();
    header->magic = REFMEM_VECTOR_MAGIC;
    header->end_magic = REFMEM_VECTOR_END_MAGIC;
    header->layout_version = DISTRIBUTED_REFMEM_LAYOUT_VERSION;
    header->table_size = DISTRIBUTED_REFMEM_TABLE_SIZE;
    header->table_seq = 1u;
    header->local_node_id = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    header->node_count = DISTRIBUTED_REFMEM_NODE_COUNT;
    header->header_size = DISTRIBUTED_REFMEM_HEADER_SIZE;
    header->region_count = REFMEM_VECTOR_REGION_COUNT;
    header->flags = 0u;
    header->table_owner = REFMEM_VECTOR_TABLE_OWNER;
    header->header_stale = REFMEM_VECTOR_HEADER_STALE;
    refmem_vector_table_init_directory(&s_distributed_refmem_table);
    header->directory_crc32 = refmem_vector_directory_crc(&s_distributed_refmem_table);
    distributed_refmem_publish_runtime_locked();

    for (uint32_t i = 0u; i < DISTRIBUTED_REFMEM_NODE_COUNT; i++) {
        refmem_vector_node_region_t *node = distributed_refmem_node_region(i);
        node->node_id = i;
        node->state = DISTRIBUTED_REFMEM_NODE_MISSING;
        node->node_type = DISTRIBUTED_REFMEM_NODE_TYPE_BOARD;
    }

    refmem_vector_node_region_t *local_node =
        distributed_refmem_node_region(DISTRIBUTED_REFMEM_LOCAL_NODE_ID);
    local_node->state = DISTRIBUTED_REFMEM_NODE_OK;
    local_node->node_type = DISTRIBUTED_REFMEM_NODE_TYPE_BOARD;
    local_node->slot_version = 1u;
    local_node->last_update_ms = osal_tick_ms();

    s_service_count = 0u;
    s_vdc_vector_source_update_seq = UINT32_MAX;
    s_dpll_vector_source_update_seq = UINT32_MAX;
    s_next_runtime_vector = 0u;
    s_initialized = true;
    s_status.init_stage = DISTRIBUTED_REFMEM_INIT_STAGE_READY;
    s_status.init_error = 0u;
    distributed_refmem_publish_status_locked();

    osal_critical_exit();
    return true;
}

void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_realtime_run_once)(void)
{
    if (!s_initialized) {
        return;
    }

    /* The single TdmaSchedulerAO is advanced by tdma_component_core1_service.
     * RefMem consumes its result here and must not run a second scheduler.
     * Only the already-published, core1-owned VDC snapshot crosses this phase;
     * no SCPI, storage, logging, or scheduler work is allowed here. */
    const uint32_t source_update_seq =
        vdc_dpll_manager_published_update_seq();
    const bool vdc_pending =
        source_update_seq != s_vdc_vector_source_update_seq;
    const bool dpll_pending =
        source_update_seq != s_dpll_vector_source_update_seq;
    if (!vdc_pending && !dpll_pending) {
        return;
    }

    vdc_domain_snapshot_t snapshot;
    const bool snapshot_valid = vdc_dpll_manager_get_snapshot(&snapshot);
    const bool publish_vdc = vdc_pending &&
        (!dpll_pending || s_next_runtime_vector == 0u);
    if (publish_vdc) {
        refmem_vdc_vector_payload_t payload;
        distributed_refmem_fill_vdc_vector_payload(
            &payload,
            snapshot_valid ? &snapshot : NULL,
            distributed_refmem_next_publish_sequence(
                &s_vdc_vector_publish_sequence),
            snapshot_valid);
        distributed_refmem_publish_vdc_vector_payload(
            distributed_refmem_vdc_vector_region(), &payload);
        s_vdc_vector_source_update_seq = source_update_seq;
        s_next_runtime_vector = 1u;
        return;
    }

    refmem_dpll_vector_payload_t payload;
    distributed_refmem_fill_dpll_vector_payload(
        &payload,
        snapshot_valid ? &snapshot : NULL,
        distributed_refmem_next_publish_sequence(
            &s_dpll_vector_publish_sequence),
        snapshot_valid);
    distributed_refmem_publish_dpll_vector_payload(
        distributed_refmem_dpll_vector_region(), &payload);
    s_dpll_vector_source_update_seq = source_update_seq;
    s_next_runtime_vector = 0u;
}

void distributed_refmem_service(void)
{
    if (!s_initialized) {
        return;
    }

    osal_critical_enter();

    refmem_vector_header_region_t *header = distributed_refmem_header();
    refmem_vector_node_region_t *local_node =
        distributed_refmem_node_region(DISTRIBUTED_REFMEM_LOCAL_NODE_ID);

    s_service_count++;
    header->table_seq++;
    distributed_refmem_publish_runtime_locked();
    local_node->heartbeat++;
    local_node->slot_version++;
    local_node->last_update_ms = osal_tick_ms();
    local_node->state = DISTRIBUTED_REFMEM_NODE_OK;

    distributed_refmem_publish_status_locked();

    osal_critical_exit();
    if (ota_ao_is_active()) {
        return;
    }
    distributed_refmem_node_load_auto_service();
    distributed_refmem_tdma_flight_sync_service();
    distributed_refmem_log_tdma_ring_service();
}

/* Low-frequency read-only TDMA ring maintenance log (P0.5-3 monitoring).
 * The resident ring is owned and driven by the core1 TDMA service; this core0
 * path only mirrors ring state into the LOG system every few seconds for
 * post-hoc diagnosis, and never submits TX/RX intents. */
static void distributed_refmem_log_tdma_ring_service(void)
{
    if (ota_ao_is_active() || !s_tdma_ring_log_enabled) {
        return;
    }
    const uint32_t now_ms = osal_tick_ms();
    if (now_ms - s_tdma_ring_log_last_ms < 5000u) {
        return;
    }
    s_tdma_ring_log_last_ms = now_ms;

    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_runtime_owner_get_ring_snapshot(&ring)) {
        return;
    }
    LOG_INFO("tdma_ring",
             "en=%lu up=%lu down=%lu seq=%lu reason=%lu ev=%lu "
             "adapt_start=%lu svc=%lu beacon_tx=%lu beacon_rx=%lu rt_ns=%lu",
             (unsigned long)ring.enabled,
             (unsigned long)ring.up_running,
             (unsigned long)ring.down_running,
             (unsigned long)ring.ring_seq,
             (unsigned long)ring.last_reason,
             (unsigned long)ring.simultaneous_feedback_loop_evidence,
             (unsigned long)ring.adapter_started,
             (unsigned long)ring.adapter_service_count,
             (unsigned long)ring.idle_beacon_tx_count,
             (unsigned long)ring.idle_beacon_rx_count,
             (unsigned long)ring.feedback_round_trip_ns);
}

bool distributed_refmem_get_realtime_tdma(refmem_realtime_tdma_snapshot_t *snapshot)
{
    return refmem_realtime_tdma_get_snapshot(&s_refmem_realtime_tdma, snapshot);
}

bool distributed_refmem_get_vdc_vector_snapshot(
    distributed_refmem_vdc_vector_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (!s_initialized) {
        return false;
    }
    return distributed_refmem_read_vdc_vector_snapshot(snapshot);
}

bool distributed_refmem_get_dpll_vector_snapshot(
    distributed_refmem_dpll_vector_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return false;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    if (!s_initialized) {
        return false;
    }
    return distributed_refmem_read_dpll_vector_snapshot(snapshot);
}

bool distributed_refmem_get_realtime_tdma_frame(uint8_t *frame,
                                               size_t frame_capacity,
                                               size_t *frame_size)
{
    return refmem_realtime_tdma_get_result_frame(&s_refmem_realtime_tdma,
                                                frame,
                                                frame_capacity,
                                                frame_size);
}

bool distributed_refmem_build_realtime_tdma_vdc_envelope(
    const vdc_tdma_schedule_profile_t *schedule,
    vdc_tdma_frame_envelope_t *envelope,
    refmem_vdc_bridge_status_t *status)
{
    uint8_t frame[REFMEM_REALTIME_TDMA_FRAME_MAX];
    size_t frame_size = 0u;
    refmem_realtime_tdma_snapshot_t snapshot;

    if (!refmem_realtime_tdma_get_snapshot(&s_refmem_realtime_tdma, &snapshot) ||
        !refmem_realtime_tdma_get_result_frame(&s_refmem_realtime_tdma,
                                               frame,
                                               sizeof(frame),
                                               &frame_size)) {
        if (status != NULL) {
            memset(status, 0, sizeof(*status));
            status->result = REFMEM_VDC_BRIDGE_BAD_TDMA_SNAPSHOT;
        }
        return false;
    }

    return refmem_vdc_bridge_build_data_envelope(schedule,
                                                 &snapshot,
                                                 frame,
                                                 frame_size,
                                                 envelope,
                                                 status);
}

bool distributed_refmem_submit_realtime_tdma_tx(
    const refmem_realtime_tdma_intent_config_t *config)
{
    return refmem_realtime_tdma_submit_tx(&s_refmem_realtime_tdma, config);
}

bool distributed_refmem_submit_realtime_tdma_rx(
    const refmem_realtime_tdma_intent_config_t *config)
{
    return refmem_realtime_tdma_submit_rx(&s_refmem_realtime_tdma, config);
}

void distributed_refmem_abort_realtime_tdma(void)
{
    refmem_realtime_tdma_abort(&s_refmem_realtime_tdma);
}

bool distributed_refmem_quality_gate_ready(void)
{
    const refmem_quality_gate_threshold_t threshold = {
        .max_crc_error_count = 0u,
        .max_stale_count = 0u,
        .max_late_count = 0u,
        .max_drop_count = 0u,
        .max_timeout_count = 0u,
        .require_no_last_error = 1u,
    };

    refmem_realtime_tdma_snapshot_t tdma;
    if (!refmem_realtime_tdma_get_snapshot(&s_refmem_realtime_tdma, &tdma)) {
        return false;
    }

    refmem_quality_runtime_table_t table;
    memset(&table, 0, sizeof(table));
    table.version = REFMEM_APP_MODEL_VERSION;
    table.entry_count = 1u;
    table.local_slot = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    if (!refmem_quality_map_realtime_tdma_slot(DISTRIBUTED_REFMEM_LOCAL_NODE_ID,
                                               &tdma,
                                               &table.entry[0])) {
        return false;
    }

    refmem_quality_gate_result_t gate;
    if (!refmem_quality_evaluate_deployment_gate(&table, &threshold, &gate)) {
        return false;
    }
    return gate.last_state == REFMEM_APP_GATE_PASS;
}

bool distributed_refmem_command_set_reason_table_crc32(uint32_t reason_table_crc32)
{
    osal_critical_enter();
    const bool ok = refmem_command_set_reason_table_crc32(&s_refmem_command_slot,
                                                          reason_table_crc32);
    osal_critical_exit();
    return ok;
}

bool distributed_refmem_command_try_post(const refmem_command_request_t *request,
                                         uint32_t issue_tick32)
{
    osal_critical_enter();
    const bool ok = refmem_command_try_post(&s_refmem_command_slot,
                                            request,
                                            issue_tick32);
    osal_critical_exit();
    return ok;
}

bool distributed_refmem_command_ack(uint32_t target_node,
                                    uint32_t evidence_index)
{
    osal_critical_enter();
    const bool ok = refmem_command_ack(&s_refmem_command_slot,
                                       target_node,
                                       evidence_index);
    osal_critical_exit();
    return ok;
}

bool distributed_refmem_command_nack(uint32_t target_node,
                                     refmem_command_reason_t reason,
                                     uint32_t evidence_index)
{
    osal_critical_enter();
    const bool ok = refmem_command_nack(&s_refmem_command_slot,
                                        target_node,
                                        reason,
                                        evidence_index);
    osal_critical_exit();
    return ok;
}

bool distributed_refmem_command_mark_timeout(uint32_t now_tick32,
                                             uint32_t evidence_index)
{
    osal_critical_enter();
    const bool ok = refmem_command_mark_timeout(&s_refmem_command_slot,
                                                now_tick32,
                                                evidence_index);
    osal_critical_exit();
    return ok;
}

bool distributed_refmem_command_clear(uint32_t clear_seq)
{
    osal_critical_enter();
    const bool ok = refmem_command_clear(&s_refmem_command_slot, clear_seq);
    osal_critical_exit();
    return ok;
}

bool distributed_refmem_get_command_snapshot(refmem_command_snapshot_t *snapshot)
{
    return refmem_command_get_snapshot(&s_refmem_command_slot, snapshot);
}

bool distributed_refmem_register_node_load_owner(
    uint32_t instance_id,
    distributed_refmem_node_load_owner_t owner,
    void *context)
{
    if (owner == NULL || instance_id == 0u) {
        return false;
    }

    osal_critical_enter();
    distributed_refmem_node_load_owner_entry_t *empty = NULL;
    for (uint32_t i = 0u; i < DISTRIBUTED_REFMEM_NODE_LOAD_OWNER_COUNT; i++) {
        if (s_node_load_owners[i].owner != NULL &&
            s_node_load_owners[i].instance_id == instance_id) {
            s_node_load_owners[i].owner = owner;
            s_node_load_owners[i].context = context;
            osal_critical_exit();
            return true;
        }
        if (empty == NULL && s_node_load_owners[i].owner == NULL) {
            empty = &s_node_load_owners[i];
        }
    }

    if (empty != NULL) {
        empty->instance_id = instance_id;
        empty->owner = owner;
        empty->context = context;
        osal_critical_exit();
        return true;
    }

    osal_critical_exit();
    return false;
}

bool distributed_refmem_can_accept_node_load_intent(uint32_t realtime_idle)
{
    if (!s_initialized) {
        return false;
    }

    if (realtime_idle != 0u) {
        return true;
    }

    return distributed_refmem_node_load_auto_rx_preemptible();
}

bool distributed_refmem_stage_node_load(uint32_t node_id,
                                        uint32_t instance_id,
                                        uint32_t role_mask,
                                        uint32_t persona_mask,
                                        uint32_t enabled,
                                        uint32_t required,
                                        uint32_t load_order)
{
    if (!s_initialized) {
        return false;
    }
    distributed_refmem_node_load_auto_preempt_rx();

    if (node_id >= DISTRIBUTED_REFMEM_NODE_COUNT) {
        (void)refmem_application_model_stage_scpi_node_config(node_id,
                                                              instance_id,
                                                              role_mask,
                                                              persona_mask,
                                                              enabled,
                                                              required,
                                                              load_order);
        return false;
    }

    const uint32_t fields[] = {
        node_id,
        instance_id,
        role_mask,
        persona_mask,
        enabled,
        required,
        load_order,
    };
    const uint32_t payload_crc32 = distributed_refmem_u32_payload_crc32(
        fields,
        (uint32_t)(sizeof(fields) / sizeof(fields[0])));
    const uint32_t target_mask = (uint32_t)(1u << node_id);
    refmem_command_request_t request = {
        .command_seq = 0u,
        .source_node = DISTRIBUTED_REFMEM_LOCAL_NODE_ID,
        .source_instance = instance_id,
        .target_mask = target_mask,
        .required_mask = target_mask,
        .command_type = REFMEM_COMMAND_TYPE_NODE_LOAD_STAGE,
        .command_class = REFMEM_COMMAND_CLASS_CONFIG,
        .payload_kind = REFMEM_COMMAND_PAYLOAD_INLINE_SMALL,
        .payload_ref = instance_id,
        .payload_size = (uint32_t)sizeof(fields),
        .payload_crc32 = payload_crc32,
        .issue_epoch = 0u,
        .run_id = 0u,
        .timeout_us = 50000u,
    };

    if (!distributed_refmem_post_command_replacing_complete(&request, osal_tick_ms())) {
        return false;
    }

    osal_critical_enter();
    const refmem_command_take_result_t take_result =
        refmem_command_try_take(&s_refmem_command_slot,
                                node_id,
                                0u,
                                0u,
                                payload_crc32,
                                REFMEM_VECTOR_REGION_ACK_CMD);
    osal_critical_exit();
    if (take_result != REFMEM_COMMAND_TAKE_TAKEN) {
        return false;
    }

    const bool staged =
        refmem_application_model_stage_scpi_node_config(node_id,
                                                        instance_id,
                                                        role_mask,
                                                        persona_mask,
                                                        enabled,
                                                        required,
                                                        load_order);
    if (staged) {
        (void)distributed_refmem_node_load_auto_enqueue(instance_id);
        (void)distributed_refmem_command_ack(node_id, REFMEM_VECTOR_REGION_ACK_CMD);
        return true;
    }

    (void)distributed_refmem_command_nack(node_id,
                                          REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH,
                                          REFMEM_VECTOR_REGION_ACK_CMD);
    return false;
}

bool distributed_refmem_stage_sd_system_pack(const char *path,
                                             uint32_t path_hash,
                                             uint32_t manifest_status,
                                             uint32_t manifest_schema,
                                             uint32_t manifest_required_count,
                                             uint32_t manifest_missing_count,
                                             const char *manifest_build_id,
                                             uint32_t package_crc32,
                                             uint32_t package_valid,
                                             uint32_t package_error,
                                             const uint8_t *package_data,
                                             size_t package_size,
                                             const uint32_t *table_crc32,
                                             uint32_t table_crc32_count,
                                             uint32_t owner_validated_table_mask,
                                             uint32_t first_bad_table)
{
    if (!s_initialized) {
        return false;
    }

    uint32_t fields[10u + REFMEM_TABLE_REGISTRY_COUNT];
    fields[0] = path_hash;
    fields[1] = manifest_status;
    fields[2] = manifest_schema;
    fields[3] = manifest_required_count;
    fields[4] = manifest_missing_count;
    fields[5] = package_crc32;
    fields[6] = package_valid;
    fields[7] = package_error;
    fields[8] = owner_validated_table_mask;
    fields[9] = first_bad_table;
    for (uint32_t i = 0u; i < REFMEM_TABLE_REGISTRY_COUNT; i++) {
        fields[10u + i] =
            (table_crc32 != NULL && i < table_crc32_count) ? table_crc32[i] : 0u;
    }

    const uint32_t payload_crc32 = distributed_refmem_u32_payload_crc32(
        fields,
        (uint32_t)(sizeof(fields) / sizeof(fields[0])));
    const uint32_t local_target = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    const uint32_t target_mask = (uint32_t)(1u << local_target);
    refmem_command_request_t request = {
        .command_seq = 0u,
        .source_node = DISTRIBUTED_REFMEM_LOCAL_NODE_ID,
        .source_instance = DISTRIBUTED_REFMEM_SOURCE_INSTANCE_REFMEM_AO,
        .target_mask = target_mask,
        .required_mask = target_mask,
        .command_type = REFMEM_COMMAND_TYPE_TABLE_PACKAGE_STAGE,
        .command_class = REFMEM_COMMAND_CLASS_CONFIG,
        .payload_kind = REFMEM_COMMAND_PAYLOAD_STAGING_REF,
        .payload_ref = path_hash,
        .payload_size = (uint32_t)sizeof(fields),
        .payload_crc32 = payload_crc32,
        .issue_epoch = 0u,
        .run_id = 0u,
        .timeout_us = 50000u,
    };

    if (!distributed_refmem_post_command_replacing_complete(&request, osal_tick_ms())) {
        return false;
    }

    osal_critical_enter();
    const refmem_command_take_result_t take_result =
        refmem_command_try_take(&s_refmem_command_slot,
                                local_target,
                                0u,
                                0u,
                                payload_crc32,
                                REFMEM_VECTOR_REGION_ACK_CMD);
    osal_critical_exit();
    if (take_result != REFMEM_COMMAND_TAKE_TAKEN) {
        return false;
    }

    const bool staged =
        refmem_application_model_stage_sd_system_pack(path,
                                                      path_hash,
                                                      manifest_status,
                                                      manifest_schema,
                                                      manifest_required_count,
                                                      manifest_missing_count,
                                                      manifest_build_id,
                                                      package_crc32,
                                                      package_valid,
                                                      package_error);
    if (staged && package_valid != 0u) {
        refmem_application_model_load_snapshot_t snapshot;
        refmem_table_package_validation_t validation = {0};
        refmem_application_model_get_load_snapshot(&snapshot);
        validation.valid = package_valid;
        validation.error = package_error;
        validation.package_crc32 = package_crc32;
        validation.table_count = REFMEM_TABLE_REGISTRY_COUNT;
        validation.table_mask = (1u << REFMEM_TABLE_REGISTRY_COUNT) - 1u;
        validation.owner_validated_table_mask = owner_validated_table_mask;
        validation.first_bad_table = first_bad_table;
        for (uint32_t i = 0u; i < REFMEM_TABLE_REGISTRY_COUNT; i++) {
            validation.table_crc32[i] =
                (table_crc32 != NULL && i < table_crc32_count) ? table_crc32[i] : 0u;
        }
        (void)refmem_table_registry_stage_package_image(&snapshot,
                                                        package_data,
                                                        package_size,
                                                        &validation);
    }

    if (staged) {
        (void)distributed_refmem_command_ack(local_target, REFMEM_VECTOR_REGION_ACK_CMD);
        return true;
    }

    (void)distributed_refmem_command_nack(local_target,
                                          REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH,
                                          REFMEM_VECTOR_REGION_ACK_CMD);
    return false;
}

bool distributed_refmem_activate_staging(uint32_t realtime_idle)
{
    if (!s_initialized) {
        return false;
    }

    refmem_table_image_descriptor_t staging;
    if (!refmem_table_registry_get_image_descriptor(REFMEM_TABLE_IMAGE_STAGING,
                                                    &staging)) {
        return false;
    }

    const uint32_t fields[] = {
        staging.table_mask,
        staging.package_crc32,
        staging.table_seq,
        realtime_idle,
    };
    const uint32_t payload_crc32 = distributed_refmem_u32_payload_crc32(
        fields,
        (uint32_t)(sizeof(fields) / sizeof(fields[0])));
    const uint32_t local_target = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    const uint32_t target_mask = (uint32_t)(1u << local_target);
    refmem_command_request_t request = {
        .command_seq = 0u,
        .source_node = DISTRIBUTED_REFMEM_LOCAL_NODE_ID,
        .source_instance = DISTRIBUTED_REFMEM_SOURCE_INSTANCE_REFMEM_AO,
        .target_mask = target_mask,
        .required_mask = target_mask,
        .command_type = REFMEM_COMMAND_TYPE_TABLE_PACKAGE_ACTIVATE,
        .command_class = REFMEM_COMMAND_CLASS_CONFIG,
        .payload_kind = REFMEM_COMMAND_PAYLOAD_STAGING_REF,
        .payload_ref = staging.package_crc32,
        .payload_size = (uint32_t)sizeof(fields),
        .payload_crc32 = payload_crc32,
        .issue_epoch = 0u,
        .run_id = 0u,
        .timeout_us = 50000u,
    };

    if (!distributed_refmem_post_command_replacing_complete(&request, osal_tick_ms())) {
        return false;
    }

    osal_critical_enter();
    const refmem_command_take_result_t take_result =
        refmem_command_try_take(&s_refmem_command_slot,
                                local_target,
                                0u,
                                0u,
                                payload_crc32,
                                REFMEM_VECTOR_REGION_ACK_CMD);
    osal_critical_exit();
    if (take_result != REFMEM_COMMAND_TAKE_TAKEN) {
        return false;
    }

    refmem_application_model_load_snapshot_t load;
    refmem_application_model_get_load_snapshot(&load);
    const refmem_table_activation_gate_t gate = {
        .refmem_idle = load.mode == REFMEM_APP_MODEL_MODE_IDLE ? 1u : 0u,
        .realtime_idle = realtime_idle != 0u ? 1u : 0u,
        .flash_safe = distributed_refmem_flash_activation_safe() ? 1u : 0u,
        .crc_ok = staging.package_crc32 != 0u &&
                  staging.state >= REFMEM_TABLE_VALIDATION_CRC_OK ? 1u : 0u,
        .owner_ok = staging.state == REFMEM_TABLE_VALIDATION_OWNER_OK ? 1u : 0u,
        .slot_claim_ok = distributed_refmem_slot_claim_gate_ready() ? 1u : 0u,
        .deployment_gate_ok =
            refmem_application_model_get_snapshot()->valid != 0u &&
                    distributed_refmem_quality_gate_ready()
                ? 1u
                : 0u,
        .command_ack_ok = 1u,
    };

    const bool staging_candidate_ready =
        staging.state == REFMEM_TABLE_VALIDATION_OWNER_OK &&
        staging.table_mask == REFMEM_APP_TABLE_MASK_ALL &&
        staging.package_crc32 != 0u;
    const bool gate_ready_for_preparse =
        gate.refmem_idle != 0u &&
        gate.realtime_idle != 0u &&
        gate.flash_safe != 0u &&
        gate.crc_ok != 0u &&
        gate.owner_ok != 0u &&
        gate.slot_claim_ok != 0u &&
        gate.deployment_gate_ok != 0u &&
        gate.command_ack_ok != 0u;

    tdma_foundation_profile_t prepared_tdma_profile;
    uint32_t prepared_tdma_schedule_crc32 = 0u;
    if (staging_candidate_ready && gate_ready_for_preparse) {
        if (!refmem_application_model_prepare_staging_table_views()) {
            (void)refmem_table_registry_note_activation_result(
                REFMEM_TABLE_ACTIVATE_ERR_STAGING_VIEW_INVALID);
            refmem_application_model_discard_prepared_table_views();
            (void)distributed_refmem_command_nack(
                local_target,
                distributed_refmem_activation_nack_reason(
                    REFMEM_TABLE_ACTIVATE_ERR_STAGING_VIEW_INVALID),
                REFMEM_VECTOR_REGION_ACK_CMD);
            return false;
        }
        if (!refmem_application_model_get_prepared_tdma_foundation_profile(
                &prepared_tdma_profile) ||
            !distributed_refmem_tdma_profile_activation_ready(
                &prepared_tdma_profile,
                &prepared_tdma_schedule_crc32)) {
            (void)refmem_table_registry_note_activation_result(
                REFMEM_TABLE_ACTIVATE_ERR_RUNTIME_PROFILE);
            refmem_application_model_discard_prepared_table_views();
            (void)distributed_refmem_command_nack(
                local_target,
                distributed_refmem_activation_nack_reason(
                    REFMEM_TABLE_ACTIVATE_ERR_RUNTIME_PROFILE),
                REFMEM_VECTOR_REGION_ACK_CMD);
            return false;
        }
    }

    const bool activated = refmem_table_registry_activate_staging(&gate);
    refmem_table_registry_snapshot_t registry;
    refmem_table_registry_get_snapshot(&registry);
    if (activated) {
        const bool model_applied =
            refmem_application_model_commit_prepared_table_views() ||
            refmem_application_model_apply_active_table_views();
        if (!model_applied ||
            !distributed_refmem_apply_tdma_foundation_profile(
                &prepared_tdma_profile,
                prepared_tdma_schedule_crc32)) {
            (void)refmem_table_registry_note_activation_result(
                REFMEM_TABLE_ACTIVATE_ERR_RUNTIME_PROFILE);
            (void)distributed_refmem_command_nack(
                local_target,
                distributed_refmem_activation_nack_reason(
                    REFMEM_TABLE_ACTIVATE_ERR_RUNTIME_PROFILE),
                REFMEM_VECTOR_REGION_ACK_CMD);
            return false;
        }
        (void)distributed_refmem_command_ack(local_target, REFMEM_VECTOR_REGION_ACK_CMD);
        return true;
    }

    refmem_application_model_discard_prepared_table_views();
    (void)distributed_refmem_command_nack(
        local_target,
        distributed_refmem_activation_nack_reason(registry.last_error),
        REFMEM_VECTOR_REGION_ACK_CMD);
    return false;
}

bool distributed_refmem_stage_board_capability(uint32_t board_id,
                                               uint32_t board_uuid_crc32,
                                               uint32_t capability_mask,
                                               uint32_t io_constraint_mask,
                                               uint32_t ip_core_mask,
                                               uint32_t default_persona_mask,
                                               uint32_t hw_profile_crc32,
                                               uint32_t active_default_slot,
                                               uint32_t online_required)
{
    if (!s_initialized) {
        return false;
    }

    const uint32_t fields[] = {
        board_id,
        board_uuid_crc32,
        capability_mask,
        io_constraint_mask,
        ip_core_mask,
        default_persona_mask,
        hw_profile_crc32,
        active_default_slot,
        online_required,
    };
    const uint32_t payload_crc32 = distributed_refmem_u32_payload_crc32(
        fields,
        (uint32_t)(sizeof(fields) / sizeof(fields[0])));
    const uint32_t local_target = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    const uint32_t target_mask = (uint32_t)(1u << local_target);
    refmem_command_request_t request = {
        .command_seq = 0u,
        .source_node = DISTRIBUTED_REFMEM_LOCAL_NODE_ID,
        .source_instance = DISTRIBUTED_REFMEM_SOURCE_INSTANCE_REFMEM_AO,
        .target_mask = target_mask,
        .required_mask = target_mask,
        .command_type = REFMEM_COMMAND_TYPE_BOARD_CAPABILITY_STAGE,
        .command_class = REFMEM_COMMAND_CLASS_CONFIG,
        .payload_kind = REFMEM_COMMAND_PAYLOAD_INLINE_SMALL,
        .payload_ref = board_id,
        .payload_size = (uint32_t)sizeof(fields),
        .payload_crc32 = payload_crc32,
        .issue_epoch = 0u,
        .run_id = 0u,
        .timeout_us = 50000u,
    };

    if (!distributed_refmem_post_command_replacing_complete(&request, osal_tick_ms())) {
        return false;
    }

    osal_critical_enter();
    const refmem_command_take_result_t take_result =
        refmem_command_try_take(&s_refmem_command_slot,
                                local_target,
                                0u,
                                0u,
                                payload_crc32,
                                REFMEM_VECTOR_REGION_ACK_CMD);
    osal_critical_exit();
    if (take_result != REFMEM_COMMAND_TAKE_TAKEN) {
        return false;
    }

    const bool staged =
        refmem_application_model_stage_scpi_board_capability(board_id,
                                                             board_uuid_crc32,
                                                             capability_mask,
                                                             io_constraint_mask,
                                                             ip_core_mask,
                                                             default_persona_mask,
                                                             hw_profile_crc32,
                                                             active_default_slot,
                                                             online_required);
    if (staged) {
        (void)distributed_refmem_command_ack(local_target, REFMEM_VECTOR_REGION_ACK_CMD);
        return true;
    }

    (void)distributed_refmem_command_nack(local_target,
                                          REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH,
                                          REFMEM_VECTOR_REGION_ACK_CMD);
    return false;
}

bool distributed_refmem_stage_model_turntable_load(uint32_t slot_id,
                                                   uint32_t output_index)
{
    if (!s_initialized || slot_id >= DISTRIBUTED_REFMEM_NODE_COUNT) {
        return false;
    }

    const uint32_t payload_crc32 =
        distributed_refmem_model_payload_crc32(slot_id, output_index);
    const uint32_t target_mask = (uint32_t)(1u << slot_id);
    refmem_command_request_t request = {
        .command_seq = 0u,
        .source_node = DISTRIBUTED_REFMEM_LOCAL_NODE_ID,
        .source_instance = REFMEM_APP_INSTANCE_TEMPLATE_MODEL_TURNTABLE,
        .target_mask = target_mask,
        .required_mask = target_mask,
        .command_type = REFMEM_COMMAND_TYPE_NODE_LOAD_STAGE,
        .command_class = REFMEM_COMMAND_CLASS_CONFIG,
        .payload_kind = REFMEM_COMMAND_PAYLOAD_INLINE_SMALL,
        .payload_ref = output_index,
        .payload_size = 2u * sizeof(uint32_t),
        .payload_crc32 = payload_crc32,
        .issue_epoch = 0u,
        .run_id = 0u,
        .timeout_us = 50000u,
    };

    if (!distributed_refmem_post_command_replacing_complete(&request, osal_tick_ms())) {
        return false;
    }
    osal_critical_enter();
    const refmem_command_take_result_t take_result =
        refmem_command_try_take(&s_refmem_command_slot,
                                slot_id,
                                0u,
                                0u,
                                payload_crc32,
                                REFMEM_VECTOR_REGION_ACK_CMD);
    osal_critical_exit();
    if (take_result != REFMEM_COMMAND_TAKE_TAKEN) {
        return false;
    }

    const bool staged =
        refmem_application_model_stage_scpi_node_config(
            slot_id,
            REFMEM_APP_INSTANCE_TEMPLATE_MODEL_TURNTABLE,
            REFMEM_APP_ROLE_MODEL_TURNTABLE | REFMEM_APP_ROLE_TEST_AGENT,
            REFMEM_APP_PERSONA_MODEL_INSTRUMENTS,
            1u,
            0u,
            0u);
    const bool loaded =
        staged &&
        distributed_refmem_execute_node_load_owner(
            REFMEM_APP_INSTANCE_TEMPLATE_MODEL_TURNTABLE,
            slot_id,
            output_index);
    if (loaded) {
        (void)distributed_refmem_node_load_auto_enqueue(
            REFMEM_APP_INSTANCE_TEMPLATE_MODEL_TURNTABLE);
        (void)distributed_refmem_command_ack(slot_id, REFMEM_VECTOR_REGION_ACK_CMD);
        return true;
    }

    (void)distributed_refmem_command_nack(slot_id,
                                          staged
                                              ? REFMEM_COMMAND_REASON_RUN_STATE_DENIED
                                              : REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH,
                                          REFMEM_VECTOR_REGION_ACK_CMD);
    return false;
}

bool distributed_refmem_build_node_load_sync_frame(uint32_t instance_id,
                                                   uint8_t source_slot,
                                                   uint8_t target_mask,
                                                   uint32_t epoch_id,
                                                   uint32_t run_id,
                                                   uint32_t seq32,
                                                   uint32_t compact_time,
                                                   uint8_t *frame,
                                                   size_t frame_capacity,
                                                   size_t *frame_size)
{
    refmem_node_load_entry_t entry;
    if (!s_initialized ||
        !refmem_application_model_get_staging_node_load_entry(instance_id, &entry)) {
        if (frame_size != NULL) {
            *frame_size = 0u;
        }
        return false;
    }

    refmem_application_model_load_snapshot_t load;
    refmem_application_model_get_load_snapshot(&load);
    return refmem_node_load_sync_build_delta_frame(&entry,
                                                   source_slot,
                                                   target_mask,
                                                   epoch_id,
                                                   run_id,
                                                   seq32,
                                                   load.load_seq,
                                                   compact_time,
                                                   frame,
                                                   frame_capacity,
                                                   frame_size);
}

bool distributed_refmem_apply_node_load_sync_payload(const uint8_t *payload,
                                                     uint16_t payload_size)
{
    if (!s_initialized) {
        return false;
    }

    return distributed_refmem_apply_node_load_sync_payload_internal(payload,
                                                                    payload_size);
}

bool distributed_refmem_configure_node_load_auto_sync(
    uint32_t enabled,
    uint32_t local_slot,
    uint32_t target_mask,
    uint32_t baud_hz,
    uint32_t deadline_us,
    uint32_t uplink_duplex_mode,
    const refmem_spi_physical_pin_config_t *uplink_adapter_pins,
    uint32_t downlink_duplex_mode,
    const refmem_spi_physical_pin_config_t *downlink_adapter_pins)
{
    if (!s_initialized ||
        enabled > 1u ||
        local_slot >= DISTRIBUTED_REFMEM_NODE_COUNT ||
        target_mask > 0xFFu ||
        deadline_us == 0u ||
        (uplink_duplex_mode != DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_HALF &&
         uplink_duplex_mode != DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_FULL) ||
        (downlink_duplex_mode != DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_HALF &&
         downlink_duplex_mode != DISTRIBUTED_REFMEM_ADAPTER_DUPLEX_FULL) ||
        uplink_adapter_pins == NULL ||
        downlink_adapter_pins == NULL) {
        return false;
    }

    if (s_node_load_auto_sync.active_intent != DISTRIBUTED_REFMEM_AUTO_INTENT_NONE ||
        s_node_load_auto_sync.enabled != enabled) {
        refmem_realtime_tdma_abort(&s_refmem_realtime_tdma);
    }

    s_node_load_auto_sync.enabled = enabled;
    s_node_load_auto_sync.local_slot = (uint8_t)local_slot;
    s_node_load_auto_sync.target_mask = (uint8_t)target_mask;
    s_node_load_auto_sync.baud_hz =
        baud_hz == 0u ? BOARD_REFMEM_SPI_BAUD_HZ : baud_hz;
    s_node_load_auto_sync.deadline_us = deadline_us;
    s_node_load_auto_sync.uplink_duplex_mode = uplink_duplex_mode;
    s_node_load_auto_sync.downlink_duplex_mode = downlink_duplex_mode;
    s_node_load_auto_sync.uplink_adapter_pins = *uplink_adapter_pins;
    s_node_load_auto_sync.downlink_adapter_pins = *downlink_adapter_pins;
    s_node_load_auto_sync.active_intent = DISTRIBUTED_REFMEM_AUTO_INTENT_NONE;
    s_node_load_auto_sync.active_instance_id = 0u;
    s_node_load_auto_sync.active_intent_seq = 0u;
    s_node_load_auto_sync.last_processed_completed_seq = 0u;
    s_node_load_auto_sync.last_error = 0u;
    return refmem_sync_init(&s_refmem_sync_context,
                            s_node_load_auto_sync.local_slot,
                            s_node_load_auto_sync.epoch_id,
                            s_node_load_auto_sync.run_id);
}

void distributed_refmem_get_node_load_auto_sync(
    distributed_refmem_node_load_auto_sync_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->enabled = s_node_load_auto_sync.enabled;
    snapshot->local_slot = s_node_load_auto_sync.local_slot;
    snapshot->target_mask = s_node_load_auto_sync.target_mask;
    snapshot->baud_hz = s_node_load_auto_sync.baud_hz;
    snapshot->deadline_us = s_node_load_auto_sync.deadline_us;
    snapshot->uplink_duplex_mode = s_node_load_auto_sync.uplink_duplex_mode;
    snapshot->uplink_rx_pin = s_node_load_auto_sync.uplink_adapter_pins.rx_pin;
    snapshot->uplink_sck_pin = s_node_load_auto_sync.uplink_adapter_pins.sck_pin;
    snapshot->uplink_tx_pin = s_node_load_auto_sync.uplink_adapter_pins.tx_pin;
    snapshot->downlink_duplex_mode = s_node_load_auto_sync.downlink_duplex_mode;
    snapshot->downlink_rx_pin =
        s_node_load_auto_sync.downlink_adapter_pins.rx_pin;
    snapshot->downlink_sck_pin =
        s_node_load_auto_sync.downlink_adapter_pins.sck_pin;
    snapshot->downlink_tx_pin =
        s_node_load_auto_sync.downlink_adapter_pins.tx_pin;
    snapshot->pending_count = s_node_load_auto_sync.pending_count;
    snapshot->active_intent = s_node_load_auto_sync.active_intent;
    snapshot->active_instance_id = s_node_load_auto_sync.active_instance_id;
    snapshot->next_seq32 = s_node_load_auto_sync.next_seq32;
    snapshot->submitted_tx_count = s_node_load_auto_sync.submitted_tx_count;
    snapshot->submitted_rx_count = s_node_load_auto_sync.submitted_rx_count;
    snapshot->applied_rx_count = s_node_load_auto_sync.applied_rx_count;
    snapshot->failed_apply_count = s_node_load_auto_sync.failed_apply_count;
    snapshot->dropped_pending_count = s_node_load_auto_sync.dropped_pending_count;
    snapshot->last_rx_result = s_node_load_auto_sync.last_rx_result;
    snapshot->last_frame_type = s_node_load_auto_sync.last_frame_type;
    snapshot->last_source_slot = s_node_load_auto_sync.last_source_slot;
    snapshot->last_error = s_node_load_auto_sync.last_error;
}

void distributed_refmem_get_tdma_flight_sync(
    distributed_refmem_tdma_flight_sync_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    memset(snapshot, 0, sizeof(*snapshot));
    snapshot->enabled = s_tdma_flight_sync.enabled;
    snapshot->local_slot = s_tdma_flight_sync.local_slot;
    snapshot->node_count = s_tdma_flight_sync.node_count;
    snapshot->active_mask = s_tdma_flight_sync.active_mask;
    snapshot->reference_slot = s_tdma_flight_sync.reference_slot;
    snapshot->remote_slot = s_tdma_flight_sync.remote_slot;
    snapshot->payload_size = DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_PAYLOAD_SIZE;
    snapshot->mailbox_size = DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE;
    snapshot->publish_interval_ms = s_tdma_flight_sync.publish_interval_ms;
    snapshot->next_seq32 = s_tdma_flight_sync.next_seq32;
    snapshot->tx_publish_count = s_tdma_flight_sync.tx_publish_count;
    snapshot->tx_reject_count = s_tdma_flight_sync.tx_reject_count;
    snapshot->rx_acquire_count = s_tdma_flight_sync.rx_acquire_count;
    snapshot->rx_empty_count = s_tdma_flight_sync.rx_empty_count;
    snapshot->rx_accept_count = s_tdma_flight_sync.rx_accept_count;
    snapshot->rx_reject_count = s_tdma_flight_sync.rx_reject_count;
    snapshot->rx_duplicate_skip_count =
        s_tdma_flight_sync.rx_duplicate_skip_count;
    snapshot->rx_bad_mailbox_count = s_tdma_flight_sync.rx_bad_mailbox_count;
    snapshot->last_rx_result = s_tdma_flight_sync.last_rx_result;
    snapshot->last_frame_type = s_tdma_flight_sync.last_frame_type;
    snapshot->last_source_slot = s_tdma_flight_sync.last_source_slot;
    snapshot->last_seq32 = s_tdma_flight_sync.last_seq32;
    snapshot->last_value_u32 = s_tdma_flight_sync.last_value_u32;
    snapshot->last_error = s_tdma_flight_sync.last_error;
    snapshot->wire_layout_version = TDMA_PROCESS_IMAGE_LAYOUT_VERSION;
    snapshot->last_vdc_phase_offset_ns =
        s_tdma_flight_sync.last_vdc_phase_offset_ns;
    snapshot->last_vdc_rate_adjust_ppb =
        s_tdma_flight_sync.last_vdc_rate_adjust_ppb;
    snapshot->last_vdc_lock_state = s_tdma_flight_sync.last_vdc_lock_state;
    snapshot->last_vdc_quality = s_tdma_flight_sync.last_vdc_quality;
    snapshot->last_ack_seq16 = s_tdma_flight_sync.last_ack_seq16;
    snapshot->last_ack_flags = s_tdma_flight_sync.last_ack_flags;
    snapshot->last_control_opcode = s_tdma_flight_sync.last_control_opcode;
    snapshot->last_control_seq8 = s_tdma_flight_sync.last_control_seq8;
    snapshot->last_optional_diagnostic =
        s_tdma_flight_sync.last_optional_diagnostic;
    snapshot->last_mailbox_crc16 = s_tdma_flight_sync.last_mailbox_crc16;
}

bool distributed_refmem_get_tdma_flight_sync_peer(
    uint32_t source_slot,
    refmem_sync_peer_state_t *snapshot)
{
    if (snapshot == NULL || source_slot >= REFMEM_SYNC_NODE_COUNT) {
        return false;
    }
    const refmem_sync_peer_state_t *peer =
        refmem_sync_get_peer(&s_tdma_flight_sync.context,
                             (uint8_t)source_slot);
    if (peer == NULL) {
        return false;
    }
    *snapshot = *peer;
    return true;
}

bool distributed_refmem_get_tdma_flight_sync_mirror(
    uint32_t source_slot,
    refmem_sync_mirror_snapshot_t *snapshot)
{
    if (snapshot == NULL || source_slot >= REFMEM_SYNC_NODE_COUNT) {
        return false;
    }
    const refmem_sync_mirror_snapshot_t *mirror =
        refmem_sync_get_mirror(&s_tdma_flight_sync.context,
                               (uint8_t)source_slot);
    if (mirror == NULL) {
        return false;
    }
    *snapshot = *mirror;
    return true;
}

void distributed_refmem_get_tdma_flight_sync_quality(
    refmem_sync_quality_counters_t *snapshot)
{
    refmem_sync_get_quality(&s_tdma_flight_sync.context, snapshot);
}

bool distributed_refmem_set_tdma_ring_local_slot(uint32_t local_slot_id)
{
    const tdma_foundation_profile_t *profile =
        refmem_application_model_get_tdma_foundation_profile();
    if (profile == NULL) {
        return false;
    }
    return distributed_refmem_set_tdma_ring_topology(
        local_slot_id, profile->ring.reference_index, profile->ring.node_count);
}

bool distributed_refmem_set_tdma_ring_topology(uint32_t local_slot_id,
                                               uint32_t reference_slot_id,
                                               uint32_t node_count)
{
    if (!s_initialized) {
        return false;
    }
    tdma_service_service_t *tdma_owner = tdma_runtime_owner_get();
    tdma_ring_runtime_snapshot_t ring_snapshot;
    if (tdma_owner == NULL ||
        !tdma_ring_runtime_get_snapshot(&tdma_owner->ring_runtime,
                                        &ring_snapshot) ||
        ring_snapshot.enabled != 0u) {
        return false;
    }
    if (!vdc_dpll_manager_set_tdma_ring_topology(local_slot_id,
                                                 reference_slot_id,
                                                 node_count)) {
        return false;
    }
    if (!refmem_application_model_set_tdma_ring_topology(local_slot_id,
                                                         reference_slot_id,
                                                         node_count)) {
        return false;
    }
    const tdma_foundation_profile_t *profile =
        refmem_application_model_get_tdma_foundation_profile();
    uint32_t schedule_crc32 = 0u;
    if (!distributed_refmem_tdma_profile_activation_ready(profile,
                                                          &schedule_crc32) ||
        !distributed_refmem_apply_tdma_foundation_profile(profile,
                                                          schedule_crc32)) {
        return false;
    }
    return true;
}

bool distributed_refmem_tdma_ring_arm(void)
{
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    if (!s_initialized || owner == NULL) {
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         DISTRIBUTED_REFMEM_TDMA_ARM_OWNER_UNAVAILABLE,
                         __ATOMIC_RELEASE);
        return false;
    }
    tdma_ring_runtime_snapshot_t ring;
    if (!tdma_ring_runtime_get_snapshot(&owner->ring_runtime, &ring)) {
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         DISTRIBUTED_REFMEM_TDMA_ARM_SNAPSHOT_UNAVAILABLE,
                         __ATOMIC_RELEASE);
        return false;
    }
    if (ring.enabled != 0u || ring.adapter_started != 0u) {
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         DISTRIBUTED_REFMEM_TDMA_ARM_RUNTIME_ACTIVE,
                         __ATOMIC_RELEASE);
        return false;
    }
    const tdma_process_image_map_t map =
        distributed_refmem_default_flight_map();
    if (!tdma_service_configure_flight_map(owner, &map)) {
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         DISTRIBUTED_REFMEM_TDMA_ARM_FLIGHT_MAP_REJECTED,
                         __ATOMIC_RELEASE);
        return false;
    }
    if (owner->ring_staged_config.enabled == 0u) {
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         DISTRIBUTED_REFMEM_TDMA_ARM_STAGED_CONFIG_MISSING,
                         __ATOMIC_RELEASE);
        return false;
    }
    if (owner->calibration_gate_required != 0u &&
        (owner->calibration_stage.profile_crc32 !=
             owner->ring_staged_config.operating_profile_crc32 ||
         owner->calibration_stage.schedule_crc32 !=
             owner->ring_staged_config.schedule_crc32 ||
         !tdma_ring_runtime_validate_calibration_stage(
             &owner->calibration_stage,
             owner->ring_staged_config.node_count,
             NULL))) {
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         DISTRIBUTED_REFMEM_TDMA_ARM_CALIBRATION_GATE_REJECTED,
                         __ATOMIC_RELEASE);
        return false;
    }
    if (!tdma_service_ring_arm(owner)) {
        __atomic_store_n(&s_tdma_ring_arm_last_result,
                         DISTRIBUTED_REFMEM_TDMA_ARM_RUNTIME_CONFIG_REJECTED,
                         __ATOMIC_RELEASE);
        return false;
    }
    __atomic_store_n(&s_tdma_ring_arm_last_result,
                     DISTRIBUTED_REFMEM_TDMA_ARM_OK,
                     __ATOMIC_RELEASE);
    return true;
}

distributed_refmem_tdma_arm_result_t
distributed_refmem_tdma_ring_arm_last_result(void)
{
    return (distributed_refmem_tdma_arm_result_t)__atomic_load_n(
        &s_tdma_ring_arm_last_result, __ATOMIC_ACQUIRE);
}

bool distributed_refmem_tdma_ring_train(uint32_t cycles)
{
    return s_initialized && tdma_runtime_owner_train_clock(cycles);
}

bool distributed_refmem_tdma_ring_start(void)
{
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    return s_initialized && owner != NULL && tdma_service_ring_start(owner);
}

bool distributed_refmem_tdma_ring_stop(void)
{
    tdma_service_service_t *owner = tdma_runtime_owner_get();
    return s_initialized && owner != NULL && tdma_service_ring_stop(owner);
}

bool distributed_refmem_set_tdma_ring_log_enabled(bool enabled)
{
    if (!s_initialized) {
        return false;
    }

    s_tdma_ring_log_enabled = enabled;
    if (!enabled) {
        s_tdma_ring_log_last_ms = 0u;
    }
    return true;
}

bool distributed_refmem_get_tdma_ring_log_enabled(void)
{
    return s_tdma_ring_log_enabled;
}

void distributed_refmem_get_core_vector(distributed_refmem_core_vector_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    osal_critical_enter();
    const refmem_vector_header_region_t *header = distributed_refmem_header();
    snapshot->version = header->layout_version;
    snapshot->table_seq = header->table_seq;
    snapshot->core_count = header->core_count;
    snapshot->core0_vtor_owner = header->core0_vtor_owner;
    snapshot->core1_vtor_owner = header->core1_vtor_owner;
    snapshot->core0_irq_owner_mask = header->core0_irq_owner_mask;
    snapshot->core1_irq_owner_mask = header->core1_irq_owner_mask;
    snapshot->entry_table_owner = header->entry_table_owner;
    snapshot->flags = header->flags;
    snapshot->guard.table_seq = header->table_seq;
    snapshot->guard.owner = header->table_owner;
    snapshot->guard.crc32 = header->header_crc32;
    snapshot->guard.stale = header->header_stale;
    snapshot->guard.flags = header->flags;
    osal_critical_exit();
}

void distributed_refmem_get_runtime_protection(distributed_refmem_runtime_protection_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    osal_critical_enter();
    distributed_refmem_publish_runtime_locked();
    const refmem_vector_header_region_t *header = distributed_refmem_header();
    snapshot->version = header->layout_version;
    snapshot->table_seq = header->table_seq;
    snapshot->ram_resident_required = header->ram_resident_required;
    snapshot->flash_lockout_supported = header->flash_lockout_supported;
    snapshot->flash_lockout_online = header->flash_lockout_online;
    snapshot->flash_lockout_requested = header->flash_lockout_requested;
    snapshot->flash_lockout_acknowledged = header->flash_lockout_acknowledged;
    snapshot->park_state = header->core1_park_state;
    snapshot->last_result = header->flash_lockout_last_result;
    snapshot->last_elapsed_us = header->flash_lockout_last_elapsed_us;
    snapshot->request_seq = header->flash_lockout_request_seq;
    snapshot->ack_seq = header->flash_lockout_ack_seq;
    snapshot->release_seq = header->flash_lockout_release_seq;
    snapshot->timeout_count = header->flash_lockout_timeout_count;
    snapshot->release_timeout_count = header->flash_lockout_release_timeout_count;
    snapshot->entry_table_owner = header->entry_table_owner;
    snapshot->flags = header->runtime_protection_flags;
    snapshot->guard.table_seq = header->table_seq;
    snapshot->guard.owner = header->table_owner;
    snapshot->guard.crc32 = header->header_crc32;
    snapshot->guard.stale = header->header_stale;
    snapshot->guard.flags = header->flags;
    osal_critical_exit();
}

void distributed_refmem_get_status(distributed_refmem_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_status;
    osal_critical_exit();
}

bool distributed_refmem_get_node(uint32_t node_id, distributed_refmem_node_snapshot_t *snapshot)
{
    if (snapshot == NULL || node_id >= DISTRIBUTED_REFMEM_NODE_COUNT) {
        return false;
    }

    osal_critical_enter();

    const refmem_vector_node_region_t *node = distributed_refmem_node_region(node_id);
    snapshot->node_id = node->node_id;
    snapshot->state = node->state;
    snapshot->heartbeat = node->heartbeat;
    snapshot->slot_version = node->slot_version;
    snapshot->last_update_ms = node->last_update_ms;
    snapshot->stale_count = node->stale_count;
    snapshot->fault_code = node->fault_code;
    snapshot->flags = node->flags;
    snapshot->node_type = node->node_type;

    osal_critical_exit();
    return true;
}
