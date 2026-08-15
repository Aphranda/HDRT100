#include "distributed_refmem.h"

#include <string.h>

#include "drv_flash.h"
#include "osal.h"
#include "project_config.h"

#include "refmem_application_model.h"
#include "refmem_command.h"
#include "refmem_quality.h"
#include "refmem_realtime_tdma.h"
#include "refmem_slot_claim.h"
#include "refmem_spi_physical_adapter.h"
#include "refmem_table_registry.h"
#include "refmem_vector_table.h"

#define DISTRIBUTED_REFMEM_NODE_LOAD_OWNER_COUNT 16u
#define DISTRIBUTED_REFMEM_SOURCE_INSTANCE_REFMEM_AO 0u

typedef struct {
    uint32_t instance_id;
    distributed_refmem_node_load_owner_t owner;
    void *context;
} distributed_refmem_node_load_owner_entry_t;

static refmem_vector_table_t s_distributed_refmem_table __attribute__((aligned(4)));
static refmem_command_slot_t s_refmem_command_slot;
static refmem_realtime_tdma_service_t s_refmem_realtime_tdma;
static refmem_spi_physical_adapter_t s_refmem_realtime_spi;
static distributed_refmem_node_load_owner_entry_t
    s_node_load_owners[DISTRIBUTED_REFMEM_NODE_LOAD_OWNER_COUNT];
static distributed_refmem_status_t s_status;
static uint32_t s_service_count;
static bool s_initialized;

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
    return refmem_slot_claim_gate_evaluate(&claim_map, &claim_gate);
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

static refmem_command_reason_t distributed_refmem_activation_nack_reason(uint32_t result)
{
    switch ((refmem_table_activation_result_t)result) {
    case REFMEM_TABLE_ACTIVATE_ERR_GATE:
        return REFMEM_COMMAND_REASON_RUN_STATE_DENIED;
    case REFMEM_TABLE_ACTIVATE_ERR_IMAGE_BUSY:
        return REFMEM_COMMAND_REASON_RESOURCE_BUSY;
    case REFMEM_TABLE_ACTIVATE_ERR_IMAGE_TOO_LARGE:
        return REFMEM_COMMAND_REASON_PAYLOAD_CRC_MISMATCH;
    case REFMEM_TABLE_ACTIVATE_ERR_BAD_ARGUMENT:
    case REFMEM_TABLE_ACTIVATE_ERR_NO_VALID_STAGING:
    case REFMEM_TABLE_ACTIVATE_ERR_IMAGE_NOT_LOADED:
    case REFMEM_TABLE_ACTIVATE_OK:
    default:
        return REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH;
    }
}

static refmem_vector_header_slot_t *distributed_refmem_header(void)
{
    return refmem_vector_table_header(&s_distributed_refmem_table);
}

static refmem_vector_node_slot_t *distributed_refmem_node_slot(uint32_t node_id)
{
    return refmem_vector_table_node(&s_distributed_refmem_table, node_id);
}

static void distributed_refmem_publish_status_locked(void)
{
    const refmem_vector_header_slot_t *header = distributed_refmem_header();
    const refmem_vector_node_slot_t *local_node =
        distributed_refmem_node_slot(DISTRIBUTED_REFMEM_LOCAL_NODE_ID);

    s_status.table_size = header->table_size;
    s_status.layout_version = header->layout_version;
    s_status.table_seq = header->table_seq;
    s_status.local_node_id = header->local_node_id;
    s_status.node_count = header->node_count;
    s_status.local_heartbeat = local_node->heartbeat;
    s_status.service_count = s_service_count;
    s_status.flags = header->flags;
}

static uint32_t distributed_refmem_header_crc_locked(void)
{
    return refmem_vector_header_crc(&s_distributed_refmem_table);
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

static void distributed_refmem_refresh_directory_flags_locked(void)
{
    refmem_vector_header_slot_t *header = distributed_refmem_header();
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
    refmem_vector_header_slot_t *header = distributed_refmem_header();
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
    if (!refmem_application_model_init()) {
        return false;
    }
    if (!refmem_realtime_tdma_init(&s_refmem_realtime_tdma)) {
        return false;
    }
    if (!refmem_command_init(&s_refmem_command_slot, 0u)) {
        return false;
    }
    static const refmem_realtime_tdma_ops_t tdma_ops = {
        .transmit = distributed_refmem_tdma_transmit,
        .receive = distributed_refmem_tdma_receive,
    };
    if (!refmem_realtime_tdma_bind_ops(&s_refmem_realtime_tdma,
                                       &tdma_ops,
                                       &s_refmem_realtime_spi)) {
        return false;
    }

    osal_critical_enter();

    refmem_vector_table_clear(&s_distributed_refmem_table);

    refmem_vector_header_slot_t *header = distributed_refmem_header();
    header->magic = REFMEM_VECTOR_MAGIC;
    header->end_magic = REFMEM_VECTOR_END_MAGIC;
    header->layout_version = DISTRIBUTED_REFMEM_LAYOUT_VERSION;
    header->table_size = DISTRIBUTED_REFMEM_TABLE_SIZE;
    header->table_seq = 1u;
    header->local_node_id = DISTRIBUTED_REFMEM_LOCAL_NODE_ID;
    header->node_count = DISTRIBUTED_REFMEM_NODE_COUNT;
    header->header_size = DISTRIBUTED_REFMEM_HEADER_SIZE;
    header->slot_count = REFMEM_VECTOR_SLOT_COUNT;
    header->flags = 0u;
    header->table_owner = REFMEM_VECTOR_TABLE_OWNER;
    header->header_stale = REFMEM_VECTOR_HEADER_STALE;
    refmem_vector_table_init_directory(&s_distributed_refmem_table);
    header->directory_crc32 = refmem_vector_directory_crc(&s_distributed_refmem_table);
    distributed_refmem_publish_runtime_locked();

    for (uint32_t i = 0u; i < DISTRIBUTED_REFMEM_NODE_COUNT; i++) {
        refmem_vector_node_slot_t *node = distributed_refmem_node_slot(i);
        node->node_id = i;
        node->state = DISTRIBUTED_REFMEM_NODE_MISSING;
        node->node_type = DISTRIBUTED_REFMEM_NODE_TYPE_BOARD;
    }

    refmem_vector_node_slot_t *local_node =
        distributed_refmem_node_slot(DISTRIBUTED_REFMEM_LOCAL_NODE_ID);
    local_node->state = DISTRIBUTED_REFMEM_NODE_OK;
    local_node->node_type = DISTRIBUTED_REFMEM_NODE_TYPE_BOARD;
    local_node->slot_version = 1u;
    local_node->last_update_ms = osal_tick_ms();

    s_service_count = 0u;
    s_initialized = true;
    distributed_refmem_publish_status_locked();

    osal_critical_exit();
    return true;
}

void distributed_refmem_realtime_run_once(void)
{
    if (!s_initialized) {
        return;
    }

    refmem_realtime_tdma_core1_service(&s_refmem_realtime_tdma);
}

void distributed_refmem_service(void)
{
    if (!s_initialized) {
        return;
    }

    osal_critical_enter();

    refmem_vector_header_slot_t *header = distributed_refmem_header();
    refmem_vector_node_slot_t *local_node =
        distributed_refmem_node_slot(DISTRIBUTED_REFMEM_LOCAL_NODE_ID);

    s_service_count++;
    header->table_seq++;
    distributed_refmem_publish_runtime_locked();
    local_node->heartbeat++;
    local_node->slot_version++;
    local_node->last_update_ms = osal_tick_ms();
    local_node->state = DISTRIBUTED_REFMEM_NODE_OK;

    distributed_refmem_publish_status_locked();

    osal_critical_exit();
}

bool distributed_refmem_get_realtime_tdma(refmem_realtime_tdma_snapshot_t *snapshot)
{
    return refmem_realtime_tdma_get_snapshot(&s_refmem_realtime_tdma, snapshot);
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

    refmem_deployment_gate_entry_t gate;
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
                                REFMEM_VECTOR_SLOT_ACK_CMD);
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
        (void)distributed_refmem_command_ack(node_id, REFMEM_VECTOR_SLOT_ACK_CMD);
        return true;
    }

    (void)distributed_refmem_command_nack(node_id,
                                          REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH,
                                          REFMEM_VECTOR_SLOT_ACK_CMD);
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
                                REFMEM_VECTOR_SLOT_ACK_CMD);
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
        (void)distributed_refmem_command_ack(local_target, REFMEM_VECTOR_SLOT_ACK_CMD);
        return true;
    }

    (void)distributed_refmem_command_nack(local_target,
                                          REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH,
                                          REFMEM_VECTOR_SLOT_ACK_CMD);
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
                                REFMEM_VECTOR_SLOT_ACK_CMD);
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

    const bool activated = refmem_table_registry_activate_staging(&gate);
    refmem_table_registry_snapshot_t registry;
    refmem_table_registry_get_snapshot(&registry);
    if (activated) {
        (void)distributed_refmem_command_ack(local_target, REFMEM_VECTOR_SLOT_ACK_CMD);
        return true;
    }

    (void)distributed_refmem_command_nack(
        local_target,
        distributed_refmem_activation_nack_reason(registry.last_error),
        REFMEM_VECTOR_SLOT_ACK_CMD);
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
                                REFMEM_VECTOR_SLOT_ACK_CMD);
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
        (void)distributed_refmem_command_ack(local_target, REFMEM_VECTOR_SLOT_ACK_CMD);
        return true;
    }

    (void)distributed_refmem_command_nack(local_target,
                                          REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH,
                                          REFMEM_VECTOR_SLOT_ACK_CMD);
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
                                REFMEM_VECTOR_SLOT_ACK_CMD);
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
        (void)distributed_refmem_command_ack(slot_id, REFMEM_VECTOR_SLOT_ACK_CMD);
        return true;
    }

    (void)distributed_refmem_command_nack(slot_id,
                                          staged
                                              ? REFMEM_COMMAND_REASON_RUN_STATE_DENIED
                                              : REFMEM_COMMAND_REASON_CONFIG_CRC_MISMATCH,
                                          REFMEM_VECTOR_SLOT_ACK_CMD);
    return false;
}

void distributed_refmem_get_core_vector(distributed_refmem_core_vector_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    osal_critical_enter();
    const refmem_vector_header_slot_t *header = distributed_refmem_header();
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
    const refmem_vector_header_slot_t *header = distributed_refmem_header();
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

    const refmem_vector_node_slot_t *node = distributed_refmem_node_slot(node_id);
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
