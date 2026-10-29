#include "distributed_refmem.h"
#include "distributed_refmem_internal.h"

#include <string.h>

#include "drv_flash.h"
#include "osal.h"
#include "board_config.h"
#include "board_identity.h"
#include "project_config.h"

#include "distributed_refmem_vdc_bridge.h"
#include "diagnostics.h"
#include "ota_ao.h"
#include "refmem_application_model.h"
#include "refmem_command.h"
#include "refmem_cmd_load.h"
#include "refmem_lease_table.h"
#include "refmem_control_route.h"
#include "refmem_node_control.h"
#include "refmem_param_pool.h"
#include "refmem_cmd_frame.h"
#include "refmem_cmd_reply.h"
#include "tdma_maintenance_runtime.h"
#include "refmem_node_load_sync.h"
#include "refmem_quality.h"
#include "refmem_realtime_tdma.h"
#include "tdma_runtime_owner.h"
#include "refmem_slot_claim.h"
#include "refmem_spi_physical_adapter.h"
#include "refmem_sync.h"
#include "refmem_sync_vdc_feedback.h"
#include "refmem_table_registry.h"
#include "refmem_vector_table.h"
#include "tdma_flight_engine.h"
#include "tdma_process_image_layout.h"
#include "tdma_process_image_map.h"
#include "tdma_profile.h"
#include "trigger_position_mailbox.h"
#include "vdc_dpll_manager.h"

_Static_assert(TRIGGER_POSITION_SLICE_WIRE_SIZE ==
                   TDMA_PROCESS_IMAGE_REFMEM_SIZE + TDMA_PROCESS_IMAGE_VDC_SIZE,
               "A T2 position record spans the freed VDC observation area plus the RefMem region");

/* RAM placement macros live in a header: the split command units need them. */
#include "distributed_refmem_time_critical.h"

#include "distributed_refmem_split_internal.h"
#include "distributed_refmem.h"
#include "distributed_refmem_internal.h"

#include <string.h>

#include "drv_flash.h"
#include "osal.h"
#include "board_config.h"
#include "board_identity.h"
#include "project_config.h"

#include "distributed_refmem_vdc_bridge.h"
#include "diagnostics.h"
#include "ota_ao.h"
#include "refmem_application_model.h"
#include "refmem_command.h"
#include "refmem_cmd_load.h"
#include "refmem_lease_table.h"
#include "refmem_control_route.h"
#include "refmem_node_control.h"
#include "refmem_param_pool.h"
#include "refmem_cmd_frame.h"
#include "refmem_cmd_reply.h"
#include "tdma_maintenance_runtime.h"
#include "refmem_node_load_sync.h"
#include "refmem_quality.h"
#include "refmem_realtime_tdma.h"
#include "tdma_runtime_owner.h"
#include "refmem_slot_claim.h"
#include "refmem_spi_physical_adapter.h"
#include "refmem_sync.h"
#include "refmem_sync_vdc_feedback.h"
#include "refmem_table_registry.h"
#include "refmem_vector_table.h"
#include "tdma_flight_engine.h"
#include "tdma_process_image_layout.h"
#include "tdma_process_image_map.h"
#include "tdma_profile.h"
#include "trigger_position_mailbox.h"
#include "vdc_dpll_manager.h"

_Static_assert(TRIGGER_POSITION_SLICE_WIRE_SIZE ==
                   TDMA_PROCESS_IMAGE_REFMEM_SIZE + TDMA_PROCESS_IMAGE_VDC_SIZE,
               "A T2 position record spans the freed VDC observation area plus the RefMem region");

/* RAM placement macros live in a header: the split command units need them. */
#include "distributed_refmem_time_critical.h"

#define DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE \
    TDMA_FLIGHT_SHORT_SLOT_SIZE





/* A master snapshot is fanned out over multiple CONFIG_CONTROL windows.  The
 * batch freezes all command fields so backpressure cannot mix generations or
 * effective times within one publication. */

refmem_vector_table_t s_distributed_refmem_table __attribute__((aligned(4)));

refmem_vector_table_t *distributed_refmem_vector_table(void)
{
    return &s_distributed_refmem_table;
}

refmem_command_slot_t s_refmem_command_slot;
bool distributed_refmem_sync_command_handler(
    void *handler_context,
    const refmem_sync_command_payload_t *payload,
    uint8_t source_slot,
    uint32_t epoch_id,
    uint32_t run_id);
refmem_realtime_tdma_service_t s_refmem_realtime_tdma;
refmem_spi_physical_adapter_t s_refmem_realtime_spi;
distributed_refmem_node_load_owner_entry_t
    s_node_load_owners[DISTRIBUTED_REFMEM_NODE_LOAD_OWNER_COUNT];
distributed_refmem_status_t s_status;
refmem_sync_context_t s_refmem_sync_context;
/* VDC commands have a sequence domain separate from node-load deltas.
 * Keep their retained receiver state in ordinary RAM.  The substantially
 * larger cross-core VDC snapshot occupies SCRATCH_Y, while SCRATCH_X stays
 * exclusively available to the Core1 stack. */
refmem_sync_vdc_context_t s_vdc_command_context;
distributed_refmem_node_load_auto_sync_t s_node_load_auto_sync;
distributed_refmem_vdc_follower_rx_t s_vdc_follower_rx;
uint32_t s_service_count;
bool s_refmem_core_initialized;
uint32_t s_tdma_ring_log_last_ms;
bool s_tdma_ring_log_enabled;
uint32_t s_vdc_vector_publish_sequence;
uint32_t s_dpll_vector_publish_sequence;
uint32_t s_vdc_vector_source_update_seq = UINT32_MAX;
uint32_t s_dpll_vector_source_update_seq = UINT32_MAX;
uint32_t s_next_runtime_vector;
distributed_refmem_vdc_command_batch_t s_vdc_command_batch;
uint32_t s_vdc_command_sequence;
uint32_t s_vdc_command_frame_sequence;
uint32_t s_vdc_command_published_update_seq = UINT32_MAX;
uint32_t s_vdc_command_published_generation = UINT32_MAX;
uint32_t s_vdc_command_published_source_slot = UINT32_MAX;
uint32_t s_vdc_command_published_schedule_crc32 = UINT32_MAX;
uint8_t s_vdc_command_context_local_slot;
uint32_t s_vdc_command_context_epoch_id;
uint32_t s_vdc_command_context_run_id;
uint32_t s_vdc_command_context_schedule_epoch;
bool s_vdc_command_context_initialized;
/* Core0-only admission state; transient snapshot failure must not retire a
 * valid command or stop ordinary resident mailbox service. */
bool s_vdc_command_context_ready;

void distributed_refmem_vdc_follower_rx_service(void);

bool distributed_refmem_refresh_vdc_command_context(void);
bool distributed_refmem_refresh_vdc_command_context_from_snapshot(
    const vdc_dpll_manager_refmem_snapshot_t *snapshot);
void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_reset_vdc_command_batch)(void);
bool DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_capture_vdc_command_batch)(
    const vdc_dpll_manager_refmem_snapshot_t *snapshot);
bool distributed_refmem_publish_vdc_command_once(
    const vdc_dpll_manager_refmem_snapshot_t *snapshot);
void DISTRIBUTED_REFMEM_TIME_CRITICAL(
    distributed_refmem_publish_runtime_vector_if_pending)(
    uint32_t source_update_seq);

uint32_t distributed_refmem_next_nonzero_sequence(uint32_t *sequence)
{
    if (sequence == NULL) {
        return 1u;
    }
    uint32_t value = *sequence;
    if (value == 0u) {
        value = 1u;
    }
    *sequence = value == UINT32_MAX ? 1u : value + 1u;
    return value;
}


distributed_refmem_tdma_flight_sync_t s_tdma_flight_sync;

distributed_refmem_tdma_flight_sync_t *distributed_refmem_tdma_flight_sync(void)
{
    return &s_tdma_flight_sync;
}

/* Per-target REFMEM_CMD reply state.
 *
 * The target repeats its terminal result on the per-node process-image ACK
 * seat for a bounded number of publications so a lost frame cannot erase the
 * only control-plane feedback.  The origin stores the newest reply of every
 * source slot and folds them into its own node control record (acked/nacked
 * masks + reason), which `SYSTem:CTRL:STATus?` and
 * `SYSTem:REFMEM:NODE:CONTrol?` already expose. */
/* T2 position TX as a latest-value table drained on a fixed period (see the header): the
 * Core0 producer always succeeds and overwrites the pending value for its position, while
 * the transport takes at most one record per T_drain ms and only while the single-depth
 * first-class seat is free. A newer PREVIEW replaces an older one; a FREEZE already waiting
 * is never replaced by a PREVIEW (FREEZE outranks it). */

/* One wait fact per blocked drain, not one per service pass: the edge detector keeps
 * tx_wait_count readable as "how often a ready value had to wait". */
/* Sole writer: Core0 RefMem service. SCPI only takes a bounded guarded copy.
 * Keep history separate from the current binding so STOP cannot erase the
 * evidence and a later ARM cannot revive it as current reception. */
volatile uint32_t s_vdc_flight_rx_guard;
distributed_refmem_vdc_flight_rx_binding_t s_vdc_flight_rx_binding;
distributed_refmem_vdc_flight_rx_snapshot_t s_vdc_flight_rx;
bool s_vdc_flight_rx_admission_ready;

/* Read-only views for the split command units. The storage stays private so the
 * Core0 RefMem service remains the only writer. */
const distributed_refmem_vdc_flight_rx_binding_t *
distributed_refmem_vdc_flight_rx_binding(void)
{
    return &s_vdc_flight_rx_binding;
}

bool distributed_refmem_vdc_flight_rx_admission_ready(void)
{
    return s_vdc_flight_rx_admission_ready;
}

bool distributed_refmem_is_initialized(void)
{
    return s_refmem_core_initialized;
}

_Static_assert(sizeof(distributed_refmem_vdc_flight_rx_snapshot_t) == 64u,
               "ordinary mailbox receive snapshot must remain compact");
volatile uint32_t s_tdma_ring_arm_last_result =
    DISTRIBUTED_REFMEM_TDMA_ARM_NOT_ATTEMPTED;

uint32_t distributed_refmem_flight_input_offset_for_slot(uint32_t slot)
{
    return slot * DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE;
}

uint32_t distributed_refmem_flight_publish_mask_for_slot(uint32_t slot)
{
    return slot < TDMA_PROCESS_IMAGE_SEGMENT_COUNT ? (1u << slot) : 0u;
}

void distributed_refmem_tdma_reset_resident_command_state(void)
{
    s_tdma_flight_sync.vdc_command_record_active = 0u;
    s_tdma_flight_sync.vdc_command_record_fragment_index = 0u;
#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED
    refmem_sync_vdc_fragment_reset(&s_tdma_flight_sync.vdc_command_fragments);
#endif
}

#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED
void distributed_refmem_tdma_sync_resident_command_identity(
    const vdc_dpll_manager_refmem_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    const uint32_t source_slot =
        snapshot->control_profile.mode == VDC_DPLL_CONTROL_MODE_FOLLOWER
            ? snapshot->control_profile.follow_master_slot_id
            : snapshot->schedule.local_slot_id;
    if (s_tdma_flight_sync.vdc_command_transport_valid !=
            snapshot->control_profile.valid ||
        s_tdma_flight_sync.vdc_command_transport_mode !=
            snapshot->control_profile.mode ||
        s_tdma_flight_sync.vdc_command_transport_source_slot != source_slot ||
        s_tdma_flight_sync.vdc_command_transport_generation !=
            snapshot->control_profile.generation ||
        s_tdma_flight_sync.vdc_command_transport_schedule_crc32 !=
            snapshot->schedule.schedule_crc32 ||
        s_tdma_flight_sync.vdc_command_transport_epoch_id !=
            snapshot->clock_epoch_id ||
        s_tdma_flight_sync.vdc_command_transport_run_id !=
            snapshot->clock_run_id) {
        distributed_refmem_tdma_reset_resident_command_state();
        s_tdma_flight_sync.vdc_command_transport_valid =
            snapshot->control_profile.valid;
        s_tdma_flight_sync.vdc_command_transport_mode =
            snapshot->control_profile.mode;
        s_tdma_flight_sync.vdc_command_transport_source_slot = source_slot;
        s_tdma_flight_sync.vdc_command_transport_generation =
            snapshot->control_profile.generation;
        s_tdma_flight_sync.vdc_command_transport_schedule_crc32 =
            snapshot->schedule.schedule_crc32;
        s_tdma_flight_sync.vdc_command_transport_epoch_id =
            snapshot->clock_epoch_id;
        s_tdma_flight_sync.vdc_command_transport_run_id =
            snapshot->clock_run_id;
    }
}
#endif

tdma_process_image_map_t distributed_refmem_default_flight_map(uint32_t node_count)
{
    tdma_process_image_map_t map;
    memset(&map, 0, sizeof(map));
    map.version = TDMA_PROCESS_IMAGE_MAP_VERSION;
    map.payload_size = tdma_flight_payload_size(node_count);
    if (map.payload_size == 0u) return map;
    map.segment_count = node_count;
    for (uint32_t slot = 0u;
         slot < node_count;
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

void distributed_refmem_log_tdma_ring_service(void);

void distributed_refmem_tdma_flight_sync_init(void)
{
    memset(&s_tdma_flight_sync, 0, sizeof(s_tdma_flight_sync));
    memset(s_position_pending, 0, sizeof(s_position_pending));
    s_position_drain_cursor = 0u;
    s_position_last_drain_ms = 0u;
    s_position_coalesced_count = 0u;
    s_position_wait_counted = 0u;
    memset(s_position_rx_mailboxes, 0, sizeof(s_position_rx_mailboxes));
    memset(s_position_freeze_latches, 0, sizeof(s_position_freeze_latches));
    __atomic_store_n(&s_position_cancel_requested, 0u, __ATOMIC_RELEASE);
    s_tdma_flight_sync.enabled = 1u;
    s_tdma_flight_sync.local_slot = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    s_tdma_flight_sync.reference_slot = 0u;
    s_tdma_flight_sync.remote_slot = 1u;
    s_tdma_flight_sync.publish_interval_ms =
        DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_INTERVAL_MS;
    s_tdma_flight_sync.next_seq32 = 1u;
#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED
    s_tdma_flight_sync.next_command_seq = 1u;
    s_tdma_flight_sync.vdc_command_frame_seq = 1u;
#endif
    (void)refmem_sync_delta_init(&s_tdma_flight_sync.context,
                           (uint8_t)s_tdma_flight_sync.local_slot,
                           DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_EPOCH,
                           DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_RUN);
}

void distributed_refmem_tdma_flight_sync_update_ring(
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
    memset(s_tdma_flight_sync.tx_image, 0, sizeof(s_tdma_flight_sync.tx_image));
    s_tdma_flight_sync.active_mask =
        ring->node_count >= 32u ? UINT32_MAX : ((1u << ring->node_count) - 1u);
    s_tdma_flight_sync.remote_slot =
        (ring->local_slot_id + 1u) % ring->node_count;
    s_tdma_flight_sync.rx_seen_mask = 0u;
    memset(s_tdma_flight_sync.rx_last_seq_by_source,
           0,
           sizeof(s_tdma_flight_sync.rx_last_seq_by_source));
    __atomic_store_n(&s_tdma_flight_sync.position_tx_active, 0u,
                     __ATOMIC_RELEASE);
    s_tdma_flight_sync.position_tx_fragment_index = 0u;
    s_tdma_flight_sync.position_tx_phase =
        DISTRIBUTED_REFMEM_POSITION_TRANSPORT_IDLE;
    s_tdma_flight_sync.position_tx_wait_reason =
        DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_NONE;
    /* A new ring identity invalidates every pending value: they belong to the old grid. */
    memset(s_position_pending, 0, sizeof(s_position_pending));
    s_position_wait_counted = 0u;
    s_position_last_drain_ms = 0u;
    for (uint32_t slot = 0u; slot < DISTRIBUTED_REFMEM_NODE_COUNT; ++slot) {
        __atomic_store_n(&s_position_rx_mailboxes[slot].state,
                         TRIGGER_POSITION_MAILBOX_EMPTY, __ATOMIC_RELEASE);
        __atomic_store_n(&s_position_freeze_latches[slot].mask, 0u, __ATOMIC_RELEASE);
    }
    s_tdma_flight_sync.position_rx_valid_mask = 0u;
    memset(s_tdma_flight_sync.position_rx_fragments, 0,
           sizeof(s_tdma_flight_sync.position_rx_fragments));
    distributed_refmem_tdma_reset_resident_command_state();
    (void)refmem_sync_delta_init(&s_tdma_flight_sync.context,
                           (uint8_t)s_tdma_flight_sync.local_slot,
                           DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_EPOCH,
                           DISTRIBUTED_REFMEM_NODE_LOAD_AUTO_DEFAULT_RUN);
}

/* Little-endian wire accessors shared with the split command units. */
#include "refmem_le.h"


bool distributed_refmem_vdc_flight_rx_read_binding(
    distributed_refmem_vdc_flight_rx_binding_t *out)
{
    tdma_ring_clock_snapshot_t ring;
    vdc_dpll_manager_refmem_snapshot_t vdc;
    memset(out, 0, sizeof(*out));
    if (!tdma_runtime_owner_get_ring_clock_snapshot(&ring)) return false;
    if (ring.enabled == 0u || ring.adapter_started == 0u ||
        ring.config_seq == 0u || ring.config_seq != ring.applied_config_seq ||
        ring.node_count < 2u || ring.node_count > REFMEM_SYNC_NODE_COUNT ||
        ring.local_slot_id >= ring.node_count) return true;
    /* Known STOP/inactive admission must not wait for an unrelated VDC read. */
    if (!vdc_dpll_manager_get_refmem_snapshot(&vdc)) return false;
    if (vdc.control_profile.valid != 1u ||
        vdc.control_profile.mode != VDC_DPLL_CONTROL_MODE_FOLLOWER ||
        vdc.control_profile.generation == 0u ||
        vdc.control_profile.follow_master_slot_id >= ring.node_count ||
        vdc.control_profile.follow_master_slot_id == ring.local_slot_id ||
        vdc.schedule.local_slot_id != ring.local_slot_id ||
        vdc.schedule.schedule_crc32 != ring.schedule_crc32) return true;
    out->active = 1u;
    out->source_slot = vdc.control_profile.follow_master_slot_id;
    out->local_slot = ring.local_slot_id;
    out->control_generation = vdc.control_profile.generation;
    out->ring_config_seq = ring.config_seq;
    out->schedule_crc32 = ring.schedule_crc32;
    out->clock_epoch_id = vdc.clock_epoch_id;
    out->clock_run_id = vdc.clock_run_id;
    return true;
}

bool distributed_refmem_vdc_flight_rx_same_binding(
    const distributed_refmem_vdc_flight_rx_binding_t *a,
    const distributed_refmem_vdc_flight_rx_binding_t *b)
{
    return a->active == b->active && a->source_slot == b->source_slot &&
        a->local_slot == b->local_slot &&
        a->control_generation == b->control_generation &&
        a->ring_config_seq == b->ring_config_seq &&
        a->schedule_crc32 == b->schedule_crc32 &&
        a->clock_epoch_id == b->clock_epoch_id && a->clock_run_id == b->clock_run_id;
}

/* True means known, including inactive; false defers FIFO acquisition. */
bool distributed_refmem_vdc_flight_rx_refresh(tdma_service_service_t *owner)
{
    distributed_refmem_vdc_flight_rx_binding_t current;
    s_vdc_flight_rx_admission_ready = false;
    /* Snapshot contention skips this receive beat without deleting history. */
    if (!distributed_refmem_vdc_flight_rx_read_binding(&current)) return false;
    if (!distributed_refmem_vdc_flight_rx_same_binding(&current, &s_vdc_flight_rx_binding)) {
        if (current.active != 0u) {
            current.rx_admission_epoch =
                tdma_service_core0_advance_flight_rx_admission_epoch(owner);
            if (current.rx_admission_epoch == 0u) return false;
        }
        (void)__atomic_add_fetch(&s_vdc_flight_rx_guard, 1u, __ATOMIC_ACQ_REL);
        s_vdc_flight_rx_binding = current;
        (void)__atomic_add_fetch(&s_vdc_flight_rx_guard, 1u, __ATOMIC_RELEASE);
    }
    s_vdc_flight_rx_admission_ready = current.active != 0u;
    return true;
}

void distributed_refmem_vdc_flight_rx_accept(uint32_t slot,
    const uint8_t *mailbox, const tdma_flight_rx_view_t *view)
{
    const distributed_refmem_vdc_flight_rx_binding_t *binding = &s_vdc_flight_rx_binding;
    if (!s_vdc_flight_rx_admission_ready || binding->active == 0u ||
        view == NULL || mailbox == NULL ||
        binding->rx_admission_epoch == 0u || view->admission_epoch != binding->rx_admission_epoch ||
        slot != binding->source_slot || mailbox[4] != slot ||
        (mailbox[5] & (1u << binding->local_slot)) == 0u ||
        distributed_refmem_get_le16(mailbox) != TDMA_FLIGHT_MAILBOX_MAGIC ||
        mailbox[2] != TDMA_FLIGHT_MAILBOX_VERSION ||
        mailbox[3] != TDMA_PROCESS_IMAGE_MESSAGE_CLASS ||
        (mailbox[TDMA_PROCESS_IMAGE_VDC_QUALITY_OFFSET] & TDMA_PROCESS_IMAGE_VDC_QUALITY_VALID) == 0u ||
        distributed_refmem_get_le16(mailbox + TDMA_PROCESS_IMAGE_CRC_OFFSET) !=
            tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET)) return;
    const uint32_t seq = distributed_refmem_get_le32(mailbox + TDMA_PROCESS_IMAGE_REFMEM_GENERATION_OFFSET);
    if (seq == 0u || (uint16_t)seq != distributed_refmem_get_le16(mailbox + 6u)) return;
    if (s_vdc_flight_rx.retained != 0u &&
        s_vdc_flight_rx.rx_admission_epoch == binding->rx_admission_epoch &&
        (seq - s_vdc_flight_rx.mailbox_seq == 0u ||
         seq - s_vdc_flight_rx.mailbox_seq > INT32_MAX)) return;
    distributed_refmem_vdc_flight_rx_binding_t current;
    if (!distributed_refmem_vdc_flight_rx_read_binding(&current) ||
        !distributed_refmem_vdc_flight_rx_same_binding(binding, &current)) return;
    const distributed_refmem_vdc_flight_rx_snapshot_t next = {
        .retained = 1u, .active = 1u, .source_slot = slot,
        .local_slot = binding->local_slot, .control_generation = binding->control_generation,
        .ring_config_seq = binding->ring_config_seq, .schedule_crc32 = binding->schedule_crc32,
        .mailbox_seq = seq,
        .receive_count = s_vdc_flight_rx.receive_count == UINT32_MAX ? UINT32_MAX :
            s_vdc_flight_rx.receive_count + 1u,
        .phase_offset_ns = tdma_process_image_expand_i16(
            distributed_refmem_get_i16(mailbox + TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET),
            TDMA_PROCESS_IMAGE_VDC_PHASE_QUANTUM_NS),
        .period_adjust_ppb = tdma_process_image_expand_i16(
            distributed_refmem_get_i16(mailbox + TDMA_PROCESS_IMAGE_VDC_RATE_OFFSET),
            TDMA_PROCESS_IMAGE_VDC_RATE_QUANTUM_PPB),
        .lock_state = mailbox[TDMA_PROCESS_IMAGE_VDC_LOCK_OFFSET],
        .quality = mailbox[TDMA_PROCESS_IMAGE_VDC_QUALITY_OFFSET],
        .mailbox_crc16 = distributed_refmem_get_le16(mailbox + TDMA_PROCESS_IMAGE_CRC_OFFSET),
        .rx_admission_epoch = binding->rx_admission_epoch,
        .transport_sequence = view->sequence,
    };
    (void)__atomic_add_fetch(&s_vdc_flight_rx_guard, 1u, __ATOMIC_ACQ_REL);
    s_vdc_flight_rx = next;
    (void)__atomic_add_fetch(&s_vdc_flight_rx_guard, 1u, __ATOMIC_RELEASE);
}

bool distributed_refmem_get_vdc_flight_rx(
    distributed_refmem_vdc_flight_rx_snapshot_t *snapshot)
{
    if (snapshot == NULL) return false;
    const uint32_t guard = __atomic_load_n(&s_vdc_flight_rx_guard, __ATOMIC_ACQUIRE);
    if ((guard & 1u) != 0u) return false;
    memcpy(snapshot, &s_vdc_flight_rx, sizeof(*snapshot));
    const distributed_refmem_vdc_flight_rx_binding_t binding = s_vdc_flight_rx_binding;
    distributed_refmem_vdc_flight_rx_binding_t current;
    snapshot->active = snapshot->retained != 0u &&
        distributed_refmem_vdc_flight_rx_read_binding(&current) && current.active != 0u &&
        distributed_refmem_vdc_flight_rx_same_binding(&binding, &current) &&
        snapshot->rx_admission_epoch == binding.rx_admission_epoch &&
        snapshot->ring_config_seq == current.ring_config_seq &&
        snapshot->control_generation == current.control_generation &&
        snapshot->source_slot == current.source_slot && snapshot->local_slot == current.local_slot;
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
    return guard == __atomic_load_n(&s_vdc_flight_rx_guard, __ATOMIC_ACQUIRE);
}

/* Both are on the origin's per-slice FREEZE path in SYNC_TRIGGER. */

#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED
bool distributed_refmem_tdma_prepare_vdc_command_record(
    const tdma_ring_runtime_snapshot_t *ring)
{
    s_tdma_flight_sync.vdc_command_prepare_attempt_count++;
    if (ring == NULL || ring->local_slot_id >= REFMEM_SYNC_NODE_COUNT ||
        !s_vdc_command_context_ready || ring->config_seq == 0u ||
        ring->config_seq != ring->applied_config_seq ||
        ring->config_seq != s_vdc_command_context.consumer_ring_config_seq) {
        s_tdma_flight_sync.vdc_command_prepare_reject_count++;
        s_tdma_flight_sync.vdc_command_prepare_last_reason =
            DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_BAD_ARGUMENT;
        return false;
    }

    vdc_dpll_manager_refmem_snapshot_t snapshot;
    const bool snapshot_ok = vdc_dpll_manager_get_refmem_snapshot(&snapshot);
    if (!snapshot_ok ||
        snapshot.control_profile.valid != 1u ||
        snapshot.control_profile.mode != VDC_DPLL_CONTROL_MODE_MASTER ||
        snapshot.schedule.local_slot_id != ring->local_slot_id ||
        snapshot.schedule.schedule_crc32 == 0u ||
        snapshot.clock_epoch_id == 0u || snapshot.clock_run_id == 0u) {
        s_tdma_flight_sync.vdc_command_prepare_reject_count++;
        s_tdma_flight_sync.vdc_command_prepare_last_reason =
            snapshot_ok ? DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_IDENTITY
                         : DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_SNAPSHOT;
        return false;
    }

    /* A resident record is immutable while its fragments are in flight.  A
     * newer DPLL update waits until the current record restarts. */
    if (s_tdma_flight_sync.vdc_command_record_active != 0u &&
        s_tdma_flight_sync.vdc_command_record_source_update_seq ==
            snapshot.dpll_update_seq) {
        return true;
    }
    if (s_tdma_flight_sync.vdc_command_record_active != 0u &&
        s_tdma_flight_sync.vdc_command_record_fragment_index != 0u) {
        return true;
    }

    const uint64_t local_now_ns = vdc_dpll_manager_local_time_ns();
    vdc_tdma_window_plan_t plan;
    vdc_gate_result_t gate;
    if (!vdc_dpll_manager_plan_published_tdma_window(
            &snapshot,
            VDC_DOMAIN_WINDOW_REFMEM_DATA,
            local_now_ns,
            &plan,
            &gate)) {
        s_tdma_flight_sync.vdc_command_prepare_reject_count++;
        s_tdma_flight_sync.vdc_command_prepare_last_reason =
            DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_WINDOW;
        s_tdma_flight_sync.vdc_command_prepare_last_gate_code = gate.reject_code;
        s_tdma_flight_sync.vdc_command_prepare_last_gate_slot = gate.reject_slot;
        s_tdma_flight_sync.vdc_command_prepare_last_gate_evidence =
            gate.reject_evidence;
        return false;
    }
    if (plan.valid == 0u || plan.window_start_ns == 0u) {
        s_tdma_flight_sync.vdc_command_prepare_reject_count++;
        s_tdma_flight_sync.vdc_command_prepare_last_reason =
            DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_WINDOW;
        return false;
    }
    if (plan.schedule_crc32 != snapshot.schedule.schedule_crc32) {
        s_tdma_flight_sync.vdc_command_prepare_reject_count++;
        s_tdma_flight_sync.vdc_command_prepare_last_reason =
            DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_SCHEDULE;
        return false;
    }

    uint64_t local_window_start_ns = plan.window_start_ns;
    if (local_window_start_ns < local_now_ns) {
        if (UINT64_MAX - local_window_start_ns <
            (uint64_t)snapshot.schedule.period_ns) {
            s_tdma_flight_sync.vdc_command_prepare_reject_count++;
            s_tdma_flight_sync.vdc_command_prepare_last_reason =
                DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_OVERFLOW;
            return false;
        }
        local_window_start_ns += snapshot.schedule.period_ns;
    }

    uint64_t common_now_ns = 0u;
    if (!vdc_dpll_manager_map_local_to_common_time(
        local_now_ns, &common_now_ns)) {
        /* A local window is not a distributable command deadline until the
         * TDMA owner has published a fresh hardware-latched common-time
         * anchor. Keep the resident record absent and retry next cycle. */
        s_tdma_flight_sync.vdc_command_prepare_reject_count++;
        tdma_ring_clock_snapshot_t clock;
        uint32_t reason =
            DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_COMMON_TIME;
        if (!tdma_runtime_owner_get_ring_clock_snapshot(&clock)) {
            reason = DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_SNAPSHOT;
        } else if (clock.enabled == 0u || clock.adapter_started == 0u ||
                   clock.cycle_period_ns == 0u ||
                   clock.feedback_timeout_ns == 0u) {
            reason = DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_INACTIVE;
        } else if (clock.schedule_crc32 != snapshot.schedule.schedule_crc32) {
            reason = DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_SCHEDULE;
        } else if (clock.clock_observation.valid == 0u) {
            reason = DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_INVALID;
        } else if (clock.clock_observation.correlated_frame_evidence == 0u) {
            reason = DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_UNCORRELATED;
        } else if ((clock.clock_observation.correlation_flags &
                    (TDMA_RING_CLOCK_OBSERVATION_FLAG_CYCLE_PHASE |
                     TDMA_RING_CLOCK_OBSERVATION_FLAG_COMMON_TIME)) !=
                   (TDMA_RING_CLOCK_OBSERVATION_FLAG_CYCLE_PHASE |
                    TDMA_RING_CLOCK_OBSERVATION_FLAG_COMMON_TIME)) {
            reason = DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_FLAGS;
        } else if ((clock.clock_observation.timestamp_flags &
                    TDMA_RING_TIMESTAMP_FLAG_HARDWARE_LATCHED) == 0u) {
            reason = DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_TIMESTAMP;
        } else if (clock.clock_observation.local_rx_timestamp_ns == 0u) {
            reason = DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_TIMESTAMP;
        } else if (clock.clock_observation.common_effective_time_ns == 0u) {
            reason = DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_EFFECTIVE;
        } else if (local_now_ns <
                   clock.clock_observation.local_rx_timestamp_ns) {
            reason = DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_ORDER;
        } else if (local_now_ns -
                       clock.clock_observation.local_rx_timestamp_ns >
                   (uint64_t)clock.feedback_timeout_ns) {
            reason = DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_CLOCK_STALE;
        }
        s_tdma_flight_sync.vdc_command_prepare_last_reason = reason;
        return false;
    }

    const uint64_t local_window_delta_ns =
        local_window_start_ns - local_now_ns;
    if (UINT64_MAX - common_now_ns < local_window_delta_ns) {
        s_tdma_flight_sync.vdc_command_prepare_reject_count++;
        s_tdma_flight_sync.vdc_command_prepare_last_reason =
            DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_OVERFLOW;
        return false;
    }
    const uint64_t common_window_start_ns =
        common_now_ns + local_window_delta_ns;

    refmem_sync_vdc_command_payload_t command;
    memset(&command, 0, sizeof(command));
    command.version = REFMEM_SYNC_VDC_COMMAND_VERSION;
    command.source_slot = snapshot.schedule.local_slot_id;
    command.target_slot = REFMEM_SYNC_VDC_TARGET_BROADCAST;
    command.control_generation = snapshot.control_profile.generation;
    command.command_seq = distributed_refmem_next_nonzero_sequence(
        &s_tdma_flight_sync.next_command_seq);
    command.schedule_crc32 = snapshot.schedule.schedule_crc32;
    command.epoch_id = snapshot.clock_epoch_id;
    command.run_id = snapshot.clock_run_id;
    const uint64_t lead_ns =
        (uint64_t)(REFMEM_SYNC_VDC_FRAGMENT_COUNT + 2u) *
        (uint64_t)snapshot.schedule.period_ns;
    if (UINT64_MAX - common_window_start_ns < lead_ns) {
        s_tdma_flight_sync.vdc_command_prepare_reject_count++;
        s_tdma_flight_sync.vdc_command_prepare_last_reason =
            DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_OVERFLOW;
        return false;
    }
    command.effective_vdc_time_ns = common_window_start_ns + lead_ns;
    s_tdma_flight_sync.vdc_command_last_common_time_ns =
        command.effective_vdc_time_ns;
    command.period_adjust_ppb = snapshot.dco_period_adjust_ppb;
    command.phase_offset_ns = snapshot.dco_phase_offset_ns;
    command.lock_state = snapshot.dpll_state;
    command.quality = snapshot.quality_health_state;
    command.payload_crc32 = refmem_sync_vdc_command_payload_crc32(&command);

    memcpy(s_tdma_flight_sync.vdc_command_record,
           &command,
           sizeof(command));
    s_tdma_flight_sync.vdc_command_record_active = 1u;
    s_tdma_flight_sync.vdc_command_record_fragment_index = 0u;
    s_tdma_flight_sync.vdc_command_record_source_update_seq =
        snapshot.dpll_update_seq;
    s_tdma_flight_sync.vdc_command_prepare_last_reason =
        DISTRIBUTED_REFMEM_VDC_COMMAND_PREPARE_NONE;
    s_tdma_flight_sync.vdc_command_prepare_last_gate_code =
        VDC_DOMAIN_GATE_PASS;
    s_tdma_flight_sync.vdc_command_prepare_last_gate_slot =
        snapshot.schedule.local_slot_id;
    s_tdma_flight_sync.vdc_command_prepare_last_gate_evidence =
        snapshot.schedule.schedule_epoch;
    return true;
}
#endif

bool distributed_refmem_tdma_flight_build_compact_mailbox(
    uint8_t source_slot,
    uint8_t target_mask,
    uint32_t seq32,
    uint32_t value,
    uint8_t message_class,
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
    mailbox[3] = message_class;
    mailbox[4] = source_slot;
    mailbox[5] = target_mask;
    distributed_refmem_put_le16(&mailbox[6], (uint16_t)(seq32 & 0xFFFFu));

    if (message_class == TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS ||
        message_class == TDMA_PROCESS_IMAGE_VDC_BOUNDARY_COMMAND_MESSAGE_CLASS) {
        distributed_refmem_feedback_fragment(mailbox);
    } else
#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED
    if (message_class == TDMA_PROCESS_IMAGE_VDC_COMMAND_MESSAGE_CLASS) {
        const uint32_t offset =
            (uint32_t)s_tdma_flight_sync.vdc_command_record_fragment_index *
            REFMEM_SYNC_VDC_FRAGMENT_DATA_SIZE;
        mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_INDEX_OFFSET] =
            s_tdma_flight_sync.vdc_command_record_fragment_index;
        mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_COUNT_OFFSET] =
            (uint8_t)REFMEM_SYNC_VDC_FRAGMENT_COUNT;
        memcpy(&mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_DATA_OFFSET],
               &s_tdma_flight_sync.vdc_command_record[offset],
               REFMEM_SYNC_VDC_FRAGMENT_DATA_SIZE);
    } else
#endif
    if (message_class != TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS) {
        /* VDC observation is DEAD PAYLOAD on a T2-class record: the follower's position
         * lane requires mailbox[3] == 0x15 and only reads the 10 B slice
         * (distributed_refmem_position_ack.inc:210), and the classic mailbox parse --
         * the one that fills last_vdc_* -- is skipped by design while the position lane
         * owns the transport (distributed_refmem.c:1893-1896). Omitting it frees ~6-7 B
         * of the 32 B record for the two-position (current + provisional) payload without
         * growing the RefMem region, so the ordinary image's generation/field_id/value
         * metadata is not squeezed. VDC diagnostics now come from the VDC classes
         * (feedback / boundary / command) only, which is the more correct source. */
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
    }

    distributed_refmem_put_le32(
        &mailbox[TDMA_PROCESS_IMAGE_REFMEM_GENERATION_OFFSET], seq32);
    distributed_refmem_put_le16(
        &mailbox[TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET],
        TDMA_PROCESS_IMAGE_REFMEM_BASELINE_FIELD_ID);
    distributed_refmem_put_le32(
        &mailbox[TDMA_PROCESS_IMAGE_REFMEM_VALUE_OFFSET], value);

    if (message_class == TDMA_PROCESS_IMAGE_T2_POSITION_MESSAGE_CLASS) {
        /* One compact record per frame: PREVIEW and FREEZE are never split. The slice now
         * starts where the VDC observation used to be and spans the RefMem region too
         * (16 B = VDC area 6 B + RefMem 10 B, asserted at the top of this file), so it
         * carries the current position AND the optional next-position cache. */
        if (!s_tdma_flight_sync.position_tx_active) return false;
        memcpy(&mailbox[TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET],
               s_tdma_flight_sync.position_tx_wire, TRIGGER_POSITION_SLICE_WIRE_SIZE);
    }

    uint8_t ack_flags = 0u;
    uint16_t ack_seq16 = 0u;
    uint8_t ack_reason =
        (uint8_t)(s_tdma_flight_sync.last_error & 0xFFu);
    uint8_t maint_ack_stream_id = 0u;
    uint8_t maint_ack_generation = 0u;
    bool maint_ack_ok = false;
    refmem_cmd_reply_t reply;
    if (distributed_refmem_cmd_reply_tx_get(&reply)) {
        /* The command terminal reply owns the ACK seat; the transport-level
         * maintenance ACK yields to it so a command reply is never delayed by
         * maintenance flow control. */
        refmem_cmd_reply_wire_t wire;
        if (refmem_cmd_reply_encode(&reply, &wire)) {
            ack_seq16 = wire.seq16;
            ack_flags = wire.flags;
            ack_reason = wire.reason;
        }
    } else if (refmem_cmd_load_peek_maintenance_ack(&maint_ack_stream_id,
                                                    &maint_ack_generation,
                                                    &maint_ack_ok)) {
        /* N3.2.4: the ACK seat carries a maintenance-frame ACK, tagged with a
         * diagnostic magic so the origin can distinguish it from a command
         * reply.  seq16 = stream_id:generation, bit0 of the magic = result. */
        ack_seq16 = (uint16_t)(((uint16_t)maint_ack_stream_id << 8u) |
                               maint_ack_generation);
        ack_flags = TDMA_PROCESS_IMAGE_ACK_FLAG_VALID;
        ack_reason = (uint8_t)(0xA0u | (maint_ack_ok ? 0u : 1u));
    } else if (s_tdma_flight_sync.remote_slot < REFMEM_SYNC_NODE_COUNT) {
        const uint32_t remote_bit = 1u << s_tdma_flight_sync.remote_slot;
        ack_seq16 = (uint16_t)(s_tdma_flight_sync.rx_last_seq_by_source[
            s_tdma_flight_sync.remote_slot] & 0xFFFFu);
        if ((s_tdma_flight_sync.rx_seen_mask & remote_bit) != 0u) {
            ack_flags |= TDMA_PROCESS_IMAGE_ACK_FLAG_VALID;
        }
        if (s_tdma_flight_sync.last_error != 0u) {
            ack_flags |= TDMA_PROCESS_IMAGE_ACK_FLAG_NACK;
        }
        ack_flags |= (uint8_t)((s_tdma_flight_sync.last_rx_result <<
                                TDMA_PROCESS_IMAGE_ACK_QUALITY_SHIFT) &
                               TDMA_PROCESS_IMAGE_ACK_QUALITY_MASK);
    } else if (s_tdma_flight_sync.last_error != 0u) {
        ack_flags |= TDMA_PROCESS_IMAGE_ACK_FLAG_NACK;
    }
    distributed_refmem_put_le16(
        &mailbox[TDMA_PROCESS_IMAGE_ACK_SEQ16_OFFSET], ack_seq16);
    mailbox[TDMA_PROCESS_IMAGE_ACK_FLAGS_OFFSET] = ack_flags;
    mailbox[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET] =
        TDMA_PROCESS_IMAGE_CONTROL_OPCODE_NONE;
    mailbox[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET] = 0u;
    mailbox[TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_OFFSET] = ack_reason;
    /* Arbitration: while a first-class freeze-completion fact must ride this
     * image, the shared control bytes are reserved for it and the diagnostic is
     * refused up front (counted); otherwise the diagnostic claims them first and
     * the delivery-critical first-class fact is still written last, counting any
     * override. Either way a diagnostic yield is visible rather than silently
     * losing the delivery fact. */
    action_patch_mailbox(mailbox,
        distributed_refmem_position_ack_control_available(mailbox[3]));
    distributed_refmem_position_ack_patch(mailbox);
    distributed_refmem_put_le16(
        &mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET],
        tdma_process_image_crc16_ccitt(mailbox,
                                       TDMA_PROCESS_IMAGE_CRC_OFFSET));
    return true;
}

void distributed_refmem_tdma_flight_sync_store_mailbox(
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

bool distributed_refmem_tdma_flight_expand_compact_delta(
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
        !tdma_process_image_transport_class_valid(mailbox[3]) ||
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

#if DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED
void distributed_refmem_tdma_flight_parse_vdc_fragment(
    const uint8_t *mailbox,
    size_t mailbox_size)
{
    if (!s_vdc_command_context_ready ||
        s_vdc_command_context.consumer_ring_config_seq == 0u ||
        s_tdma_flight_sync.vdc_command_transport_generation !=
            s_vdc_command_context.consumer_generation || mailbox == NULL ||
        mailbox_size < DISTRIBUTED_REFMEM_TDMA_FLIGHT_SYNC_MAILBOX_SIZE ||
        distributed_refmem_get_le16(&mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET]) !=
            tdma_process_image_crc16_ccitt(
                mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET) ||
        mailbox[4] >= REFMEM_SYNC_NODE_COUNT || mailbox[5] == 0u) {
        s_tdma_flight_sync.vdc_command_fragment_reject_count++;
        refmem_sync_vdc_fragment_reset(&s_tdma_flight_sync.vdc_command_fragments);
        return;
    }
    if (s_tdma_flight_sync.vdc_command_transport_valid != 1u ||
        s_tdma_flight_sync.vdc_command_transport_mode !=
            VDC_DPLL_CONTROL_MODE_FOLLOWER ||
        mailbox[4] != s_tdma_flight_sync.vdc_command_transport_source_slot) {
        s_tdma_flight_sync.vdc_command_fragment_reject_count++;
        return;
    }

    refmem_sync_vdc_command_payload_t command;
    const refmem_sync_vdc_fragment_result_t fragment_result =
        refmem_sync_vdc_fragment_push(
            &s_tdma_flight_sync.vdc_command_fragments,
            mailbox[4],
            mailbox[5],
            distributed_refmem_get_le16(&mailbox[TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET]),
            mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_INDEX_OFFSET],
            mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_COUNT_OFFSET],
            &mailbox[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_DATA_OFFSET],
            &command);
    if (fragment_result == REFMEM_SYNC_VDC_FRAGMENT_REJECTED) {
        s_tdma_flight_sync.vdc_command_fragment_reject_count++;
        s_tdma_flight_sync.last_error = 10u;
        return;
    }
    s_tdma_flight_sync.vdc_command_fragment_rx_count++;
    if (fragment_result != REFMEM_SYNC_VDC_FRAGMENT_COMPLETE) {
        return;
    }
    s_tdma_flight_sync.vdc_command_fragment_complete_count++;

    if (!s_vdc_command_context_initialized ||
        !refmem_sync_vdc_command_payload_validate(&command, sizeof(command)) ||
        command.source_slot != mailbox[4] ||
        command.epoch_id != s_vdc_command_context_epoch_id ||
        command.run_id != s_vdc_command_context_run_id ||
        (command.target_slot < REFMEM_SYNC_NODE_COUNT
            ? mailbox[5] != (uint8_t)(1u << command.target_slot)
            : (command.target_slot != REFMEM_SYNC_VDC_TARGET_BROADCAST ||
               (mailbox[5] & (uint8_t)(1u << s_tdma_flight_sync.local_slot)) == 0u))) {
        s_tdma_flight_sync.vdc_command_fragment_reject_count++;
        s_tdma_flight_sync.last_error = 11u;
        return;
    }

    uint8_t frame[REFMEM_SYNC_FRAME_HEADER_SIZE +
                  sizeof(refmem_sync_vdc_command_payload_t)];
    size_t frame_size = 0u;
    const uint32_t frame_seq = distributed_refmem_next_nonzero_sequence(
        &s_tdma_flight_sync.vdc_command_frame_seq);
    refmem_sync_frame_header_t header;
    if (!refmem_sync_frame_header_init(
            &header,
            REFMEM_SYNC_FRAME_COMMAND,
            0u,
            mailbox[4],
            mailbox[5],
            s_vdc_command_context_epoch_id,
            s_vdc_command_context_run_id,
            frame_seq,
            0u,
            0u,
            &command,
            (uint16_t)sizeof(command)) ||
        !refmem_sync_frame_encode(&header,
                                  &command,
                                  (uint16_t)sizeof(command),
                                  frame,
                                  sizeof(frame),
                                  &frame_size)) {
        s_tdma_flight_sync.vdc_command_fragment_reject_count++;
        s_tdma_flight_sync.last_error = 12u;
        return;
    }

    refmem_sync_rx_snapshot_t rx;
    const refmem_sync_rx_result_t result =
        refmem_sync_vdc_receive_admitted_frame(&s_vdc_command_context,
                                       frame,
                                       frame_size,
                                       &rx);
    s_tdma_flight_sync.last_rx_result = result;
    s_tdma_flight_sync.last_frame_type = rx.header.frame_type;
    s_tdma_flight_sync.last_source_slot = rx.source_slot;
    s_tdma_flight_sync.last_seq32 = rx.header.seq32;
    if (result == REFMEM_SYNC_RX_ACCEPTED) {
        s_tdma_flight_sync.vdc_command_accept_count++;
        s_tdma_flight_sync.vdc_command_last_seq = command.command_seq;
        s_tdma_flight_sync.last_vdc_phase_offset_ns = command.phase_offset_ns;
        s_tdma_flight_sync.last_vdc_rate_adjust_ppb = command.period_adjust_ppb;
        s_tdma_flight_sync.last_vdc_lock_state = command.lock_state;
        s_tdma_flight_sync.last_vdc_quality = command.quality;
        s_tdma_flight_sync.last_error = 0u;
    } else if (result != REFMEM_SYNC_RX_DUPLICATE_SEQ) {
        s_tdma_flight_sync.vdc_command_fragment_reject_count++;
        s_tdma_flight_sync.last_error = 13u;
    }
}
#endif

void distributed_refmem_tdma_parse_position_fragment(
    uint32_t source_slot, const uint8_t *mailbox)
{
    if (mailbox == NULL || source_slot >= DISTRIBUTED_REFMEM_NODE_COUNT) {
        s_tdma_flight_sync.position_rx_reject_count++;
        s_tdma_flight_sync.position_rx_phase =
            DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILED;
        s_tdma_flight_sync.position_rx_last_failure =
            DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_FRAGMENT;
        return;
    }
    trigger_position_descriptor_t descriptor;
    const uint32_t target_mask =
        mailbox[TDMA_FLIGHT_MAILBOX_TARGET_MASK_OFFSET];
    if (s_tdma_flight_sync.local_slot >= 8u ||
        (target_mask & (1u << s_tdma_flight_sync.local_slot)) == 0u) return;
    if (distributed_refmem_get_le16(&mailbox[TDMA_PROCESS_IMAGE_CRC_OFFSET]) !=
        tdma_process_image_crc16_ccitt(mailbox, TDMA_PROCESS_IMAGE_CRC_OFFSET)) {
        s_tdma_flight_sync.position_rx_reject_count++;
        s_tdma_flight_sync.position_rx_phase =
            DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILED;
        s_tdma_flight_sync.position_rx_last_failure =
            DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_CRC;
        return;
    }
    /* The bytes that used to carry the VDC observation now carry the first 6 bytes of the
     * T2 slice (VDC area 8..13 + RefMem area 14..23 = one 16 B slice), so reading VDC
     * fields here would decode slice bytes as diagnostics. VDC diagnostics come from the
     * VDC message classes instead (feedback / boundary / command), which still populate
     * last_vdc_*. */
    trigger_position_slice_t slice;
    if (trigger_position_slice_decode(&mailbox[TDMA_PROCESS_IMAGE_VDC_PHASE_OFFSET], &slice)) {
        /* Publish the NEXT-position cache the record carried (T2-PROVISIONAL-001). The
         * runtime reads it only when a position's FREEZE is missing, so storing it here
         * cannot change any execution on its own. */
        *trigger_position_next_cache() = (trigger_position_next_cache_t){
            .position_id = slice.next_position_id,
            .target_ms_index = slice.next_target_ms_index,
        };
        const uint32_t position_id = slice.position_id;
        /* Run identity and grid bindings are completed by the T2 owner from
         * its own start-up configuration; the wire carries only the slice. */
        const trigger_position_descriptor_t received = {
            .schema = TRIGGER_POSITION_DESCRIPTOR_SCHEMA,
            .stage = slice.stage == TRIGGER_POSITION_SLICE_FREEZE
                ? TRIGGER_POSITION_STAGE_FREEZE : TRIGGER_POSITION_STAGE_PREVIEW,
            .position_id = position_id,
            .prediction_version = slice.prediction_version,
            .target_ms_index = slice.target_ms_index,
            .target_fine_slice_index = slice.target_fine_slice_index,
        };
        const bool staged = received.stage == TRIGGER_POSITION_STAGE_FREEZE
            ? trigger_position_freeze_latch_put(&s_position_freeze_latches[source_slot],
                                                position_id, received.prediction_version,
                                                (uint32_t)received.target_ms_index)
            : trigger_position_mailbox_stage(
                &s_position_rx_mailboxes[source_slot], &received, target_mask);
        if (staged) {
            s_tdma_flight_sync.position_rx_complete_count++;
            s_tdma_flight_sync.position_rx_phase =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_COMPLETE;
            s_tdma_flight_sync.position_rx_last_failure =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_NONE;
            s_tdma_flight_sync.position_rx_source_slot = source_slot;
            s_tdma_flight_sync.position_rx_position_id = position_id;
        } else {
            s_tdma_flight_sync.position_rx_reject_count++;
            s_tdma_flight_sync.position_rx_phase =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILED;
            s_tdma_flight_sync.position_rx_last_failure =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_FRAGMENT;
        }
        return;
    }
    const trigger_position_fragment_result_t result =
        trigger_position_fragment_push(
            &s_tdma_flight_sync.position_rx_fragments[source_slot],
            (uint8_t)source_slot, (uint8_t)target_mask,
            distributed_refmem_get_le16(&mailbox[TDMA_FLIGHT_MAILBOX_SEQ16_OFFSET]),
            mailbox[TDMA_PROCESS_IMAGE_T2_FRAGMENT_INDEX_OFFSET],
            mailbox[TDMA_PROCESS_IMAGE_T2_FRAGMENT_COUNT_OFFSET],
            &mailbox[TDMA_PROCESS_IMAGE_T2_FRAGMENT_DATA_OFFSET],
            &descriptor);
    if (result == TRIGGER_POSITION_FRAGMENT_COMPLETE) {
        if (trigger_position_mailbox_stage(
                &s_position_rx_mailboxes[source_slot], &descriptor,
                target_mask)) {
            s_tdma_flight_sync.position_rx_valid_mask |= 1u << source_slot;
            s_tdma_flight_sync.position_rx_complete_count++;
            s_tdma_flight_sync.position_rx_phase =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_COMPLETE;
            s_tdma_flight_sync.position_rx_last_failure =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_NONE;
            s_tdma_flight_sync.position_rx_source_slot = source_slot;
            s_tdma_flight_sync.position_rx_position_id = descriptor.position_id;
        } else {
            s_tdma_flight_sync.position_rx_reject_count++;
            s_tdma_flight_sync.position_rx_phase =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILED;
            s_tdma_flight_sync.position_rx_last_failure =
                DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_FRAGMENT;
        }
    } else if (result == TRIGGER_POSITION_FRAGMENT_REJECTED) {
        s_tdma_flight_sync.position_rx_reject_count++;
        s_tdma_flight_sync.position_rx_phase =
            DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILED;
        s_tdma_flight_sync.position_rx_last_failure =
            DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_FRAGMENT;
    } else {
        s_tdma_flight_sync.position_rx_phase =
            DISTRIBUTED_REFMEM_POSITION_TRANSPORT_RECEIVED;
    }
}

/* The transmit side TAKES: nothing is ever offered to it. A record is taken out of the
 * latest-value table only when the single-depth seat can board it right now, so there is
 * no "offer refused" state at all - the seat's own occupancy decides when the next value
 * goes out, and the value left behind is the newest one for its position. Returns true
 * when a value was taken into position_tx_wire (the caller then boards it in the same
 * pass); false means "nothing to take right now", never "rejected". */
bool distributed_refmem_position_tx_prepare(tdma_service_service_t *owner)
{
    if (__atomic_load_n(&s_tdma_flight_sync.position_tx_active, __ATOMIC_ACQUIRE))
        return false;
    /* Seat still holding the previous record: take nothing, leave the table untouched. */
    if (owner == NULL || !tdma_service_first_class_free(owner)) {
        if (!s_position_wait_counted) {
            s_position_wait_counted = 1u;
            s_tdma_flight_sync.position_tx_wait_count++;
        }
        s_tdma_flight_sync.position_tx_phase =
            DISTRIBUTED_REFMEM_POSITION_TRANSPORT_WAIT_RESOURCE;
        s_tdma_flight_sync.position_tx_wait_reason =
            DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_SEAT_BUSY;
        return false;
    }
    /* Pick the record to transmit: a waiting FREEZE outranks every PREVIEW, and otherwise
     * the cursor rotates so no position can be starved by a busier neighbour. */
    uint32_t chosen = DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS;
    for (uint32_t i = 0u; i < DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS; ++i) {
        const uint32_t slot =
            (s_position_drain_cursor + i) % DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS;
        if (s_position_pending[slot].pending &&
            s_position_pending[slot].stage == TRIGGER_POSITION_SLICE_FREEZE) {
            chosen = slot;
            break;
        }
    }
    if (chosen == DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS) {
        for (uint32_t i = 0u; i < DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS; ++i) {
            const uint32_t slot =
                (s_position_drain_cursor + i) % DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS;
            if (s_position_pending[slot].pending) { chosen = slot; break; }
        }
    }
    if (chosen == DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS) {
        /* Nothing pending: the seat stays idle and no fact is recorded - an empty table is
         * not a wait. */
        s_position_wait_counted = 0u;
        return false;
    }
    const bool is_freeze =
        s_position_pending[chosen].stage == TRIGGER_POSITION_SLICE_FREEZE;
    const uint32_t now_ms = osal_tick_ms();
    /* Fixed draining period: at most one PREVIEW per T_drain ms, so the transmit side sets
     * the traffic instead of the producer's solve rate. A FREEZE is not paced - a freeze
     * that arrives late is worthless, and there is at most one per position. */
    if (!is_freeze && s_position_drain_ms != 0u && s_position_last_drain_ms != 0u &&
        (uint32_t)(now_ms - s_position_last_drain_ms) < s_position_drain_ms) {
        if (!s_position_wait_counted) {
            s_position_wait_counted = 1u;
            s_tdma_flight_sync.position_tx_wait_count++;
        }
        s_tdma_flight_sync.position_tx_phase =
            DISTRIBUTED_REFMEM_POSITION_TRANSPORT_WAIT_RESOURCE;
        s_tdma_flight_sync.position_tx_wait_reason =
            DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_DRAIN_PACE;
        return false;
    }
    memcpy(s_tdma_flight_sync.position_tx_wire, s_position_pending[chosen].wire,
           TRIGGER_POSITION_SLICE_WIRE_SIZE);
    /* ONE record carries TWO predicted positions: the pending entry of the NEXT position
     * (in table order) rides in bytes 10..15 of the same slice, so the follower caches the
     * next target without a second boarding. Unknown stays 0. */
    {
        const uint32_t next_slot =
            (chosen + 1u) % DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS;
        if (s_position_pending[next_slot].pending) {
            trigger_position_slice_t tail;
            if (trigger_position_slice_decode(s_position_pending[next_slot].wire, &tail)) {
                trigger_position_slice_t head;
                if (trigger_position_slice_decode(s_tdma_flight_sync.position_tx_wire,
                                                  &head)) {
                    head.next_position_id = tail.position_id;
                    head.next_target_ms_index = tail.target_ms_index;
                    (void)trigger_position_slice_encode(&head,
                        s_tdma_flight_sync.position_tx_wire);
                }
            }
        }
    }
    const uint8_t target_mask = s_position_pending[chosen].target_mask;
    const uint8_t position_id = s_position_pending[chosen].position_id;
    s_position_pending[chosen].pending = 0u;
    s_position_drain_cursor =
        (uint8_t)((chosen + 1u) % DISTRIBUTED_REFMEM_POSITION_PENDING_SLOTS);
    s_position_wait_counted = 0u;
    s_position_last_drain_ms = now_ms;
    s_tdma_flight_sync.position_tx_fragment_index = UINT32_MAX;
    s_tdma_flight_sync.position_tx_target_mask = target_mask;
    s_tdma_flight_sync.position_tx_position_id = position_id;
    s_tdma_flight_sync.position_tx_sequence =
        s_tdma_flight_sync.next_seq32 ? s_tdma_flight_sync.next_seq32 : 1u;
    __atomic_store_n(&s_tdma_flight_sync.position_tx_active, 1u,
                     __ATOMIC_RELEASE);
    s_tdma_flight_sync.position_tx_phase =
        DISTRIBUTED_REFMEM_POSITION_TRANSPORT_STAGED;
    s_tdma_flight_sync.position_tx_wait_reason =
        DISTRIBUTED_REFMEM_POSITION_TRANSPORT_FAILURE_NONE;
    return true;
}

