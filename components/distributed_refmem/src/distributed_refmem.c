#include "distributed_refmem.h"

#include "drv_flash.h"
#include "osal.h"
#include "project_config.h"

#include "refmem_application_model.h"
#include "refmem_realtime_tdma.h"
#include "refmem_spi_physical_adapter.h"
#include "refmem_vector_table.h"

static refmem_vector_table_t s_distributed_refmem_table __attribute__((aligned(4)));
static refmem_realtime_tdma_service_t s_refmem_realtime_tdma;
static refmem_spi_physical_adapter_t s_refmem_realtime_spi;
static distributed_refmem_status_t s_status;
static uint32_t s_service_count;
static bool s_initialized;

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
    if (adapter == NULL || status == NULL ||
        role != REFMEM_SPI_PHYSICAL_ROLE_SLAVE ||
        !refmem_spi_physical_adapter_arm(adapter, role, baud_hz, pins)) {
        if (status != NULL) {
            status->result = REFMEM_REALTIME_TDMA_EXEC_ERROR;
            status->error = REFMEM_REALTIME_TDMA_RESULT_BAD_ARGUMENT;
        }
        return false;
    }

    size_t frame_size = 0u;
    const bool ok = refmem_spi_physical_adapter_receive(
        adapter,
        frame,
        frame_capacity,
        &frame_size,
        distributed_refmem_deadline_us_to_ms(deadline_us));
    refmem_spi_physical_snapshot_t snapshot;
    (void)refmem_spi_physical_adapter_get_snapshot(adapter, &snapshot);
    status->frame_size = frame_size;
    status->error = snapshot.last_error;
    status->result = ok ? REFMEM_REALTIME_TDMA_EXEC_RX_OK
                        : REFMEM_REALTIME_TDMA_EXEC_TIMEOUT;
    return ok;
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
