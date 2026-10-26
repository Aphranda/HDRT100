#ifndef SCPI_SNAPSHOT_COMMANDS_INTERNAL_H
#define SCPI_SNAPSHOT_COMMANDS_INTERNAL_H

#include "scpi_system_snapshot_commands.h"

/* N5.0.4n: shared helpers/state of the snapshot command split. */
#define SCPI_REFMEM_SYNC_SPI_RAW_MAX 256u
#define SCPI_COMMAND_ACK_SCHEMA_VERSION 1u

typedef struct {
    uint32_t reason_id;
    uint32_t severity;
    uint32_t retryable;
    uint32_t blocking;
    uint32_t detail_code;
    const char *name;
} scpi_command_reason_entry_t;

typedef struct {
    refmem_sync_context_t context;
    refmem_pio_spi_adapter_t adapter;
    refmem_spi_physical_adapter_t spi_adapter;
    uint32_t tx_seq32;
    refmem_sync_rx_snapshot_t last_rx;
    uint32_t initialized;
} scpi_refmem_sync_state_t;

#define SCPI_REFMEM_LOAD_JOB_WAIT_LOOPS 10000u
#define SCPI_TDMA_FIFO_RESET_WAIT_LOOPS 100u
#define SCPI_REFMEM_PACKAGE_PATH "/refmem/app_model.rmtp"
#define SCPI_REFMEM_PACKAGE_READ_CHUNK 512u
#define SCPI_REFMEM_SYNC_FRAME_MAX (REFMEM_SYNC_FRAME_HEADER_SIZE + REFMEM_SYNC_FRAME_PAYLOAD_MAX)
#define SCPI_REFMEM_SYNC_HEX_MAX ((SCPI_REFMEM_SYNC_FRAME_MAX * 2u) + 1u)
#define SCPI_REFMEM_SYNC_DEFAULT_EPOCH 1u
#define SCPI_REFMEM_SYNC_DEFAULT_RUN 1u
#define SCPI_REFMEM_SYNC_DEFAULT_MAX_PAYLOAD REFMEM_SYNC_FRAME_PAYLOAD_MAX
#define SCPI_REFMEM_SYNC_DEFAULT_MTU SCPI_REFMEM_SYNC_FRAME_MAX
#define SCPI_REFMEM_SYNC_DEFAULT_LATENCY_US 50u
#define SCPI_REFMEM_SYNC_SPI_RAW_MAX 256u
#define SCPI_COMMAND_ACK_SCHEMA_VERSION 1u

extern tdma_long_frame_fifo_t s_tdma_long_frame_rx;
extern tdma_maintenance_service_t s_tdma_maintenance;
extern scpi_refmem_sync_state_t s_refmem_sync;
extern uint8_t s_refmem_package_chunk[SCPI_REFMEM_PACKAGE_READ_CHUNK];

bool tdma_ring_maintenance_mailbox(void *context, uint8_t mailbox[TDMA_FLIGHT_SHORT_SLOT_SIZE]);
bool tdma_ring_maintenance_send(void *context, const uint8_t *frame, size_t frame_size, uint32_t generation, uint32_t sequence, uint32_t segment_mask);
bool tdma_ring_maintenance_poll(void *context, uint8_t *frame, size_t frame_capacity, size_t *frame_size, uint32_t *generation, uint32_t *sequence, uint32_t *segment_mask);
bool tdma_ring_maintenance_service(void *context, uint64_t now_ns);
void tdma_long_frame_ensure_ready(void);
scpi_result_t scpi_tdma_profile_result(scpi_t *context, uint32_t selection);
void scpi_refmem_sync_apply_node_load_delta(const refmem_sync_rx_snapshot_t *rx);
uint32_t scpi_refmem_sync_build_id_crc32(void);
bool scpi_refmem_sync_build_hello_frame(uint8_t source_slot, uint8_t target_mask, uint32_t seq32, uint8_t *frame, size_t frame_capacity, size_t *frame_size);
bool scpi_refmem_sync_build_epoch_frame(uint8_t source_slot, uint8_t target_mask, uint32_t seq32, uint8_t *frame, size_t frame_capacity, size_t *frame_size);
bool scpi_refmem_sync_build_delta_frame(uint8_t source_slot, uint8_t target_mask, uint32_t seq32, uint8_t slot_id, uint32_t slot_seq, uint16_t field_id, uint32_t value, uint32_t dirty_mask, uint8_t *frame, size_t frame_capacity, size_t *frame_size);
bool scpi_refmem_sync_build_ack_frame(uint8_t source_slot, uint8_t target_mask, uint32_t seq32, const refmem_sync_rx_snapshot_t *rx, uint8_t *frame, size_t frame_capacity, size_t *frame_size);
bool scpi_refmem_sync_build_fence_frame(uint8_t source_slot, uint8_t target_mask, uint32_t seq32, uint32_t fence_seq, uint32_t fence_scope, uint32_t required_mask, uint32_t min_table_seq, uint32_t deadline_us, uint8_t *frame, size_t frame_capacity, size_t *frame_size);
bool scpi_refmem_sync_build_quality_frame(uint8_t source_slot, uint8_t target_mask, uint32_t seq32, uint32_t quality_id, uint32_t scope, uint32_t target_slot, uint8_t *frame, size_t frame_capacity, size_t *frame_size);
void scpi_refmem_sync_result_rx_snapshot(scpi_t *context, const refmem_sync_rx_snapshot_t *snapshot);
void scpi_refmem_sync_hex_encode(const uint8_t *data, size_t data_size, char *hex, size_t hex_size);
int scpi_refmem_sync_hex_nibble(char ch);
const scpi_command_reason_entry_t *scpi_command_find_reason(uint32_t reason_id);
extern const scpi_command_reason_entry_t s_command_reason_table[15];

#endif /* SCPI_SNAPSHOT_COMMANDS_INTERNAL_H */
