#ifndef SYNC_IO_LOGIC_ANALYZER_H
#define SYNC_IO_LOGIC_ANALYZER_H

#include <stdbool.h>
#include <stdint.h>

#include "sync_io_persona_resources.h"

#define SYNC_IO_LOGIC_ANALYZER_CONTRACT_VERSION 1u
#define SYNC_IO_LOGIC_ANALYZER_SNAPSHOT_READ_ATTEMPTS 8u
#define SYNC_IO_LOGIC_ANALYZER_DEBUG_CONTINUE_LIMIT 4u

typedef enum {
    SYNC_IO_LOGIC_ANALYZER_MODE_INVALID = 0,
    SYNC_IO_LOGIC_ANALYZER_MODE_RAW_SAMPLE,
    SYNC_IO_LOGIC_ANALYZER_MODE_EDGE_TIMESTAMP,
    SYNC_IO_LOGIC_ANALYZER_MODE_TRIGGERED_CAPTURE,
    SYNC_IO_LOGIC_ANALYZER_MODE_COUNT,
} sync_io_logic_analyzer_mode_t;

typedef enum {
    SYNC_IO_LOGIC_ANALYZER_TRIGGER_NONE = 0,
    SYNC_IO_LOGIC_ANALYZER_TRIGGER_LEVEL,
    SYNC_IO_LOGIC_ANALYZER_TRIGGER_EDGE,
    SYNC_IO_LOGIC_ANALYZER_TRIGGER_PATTERN,
    SYNC_IO_LOGIC_ANALYZER_TRIGGER_COUNT,
} sync_io_logic_analyzer_trigger_type_t;

typedef enum {
    SYNC_IO_LOGIC_ANALYZER_STATE_STOPPED = 0,
    SYNC_IO_LOGIC_ANALYZER_STATE_ARMED,
    SYNC_IO_LOGIC_ANALYZER_STATE_RUNNING,
    SYNC_IO_LOGIC_ANALYZER_STATE_COMPLETE,
    SYNC_IO_LOGIC_ANALYZER_STATE_FAULT,
} sync_io_logic_analyzer_state_t;

typedef enum {
    SYNC_IO_LOGIC_ANALYZER_END_NONE = 0,
    SYNC_IO_LOGIC_ANALYZER_END_COMPLETE,
    SYNC_IO_LOGIC_ANALYZER_END_CAPACITY,
    SYNC_IO_LOGIC_ANALYZER_END_TIMEOUT,
    SYNC_IO_LOGIC_ANALYZER_END_STOP_REQUEST,
    SYNC_IO_LOGIC_ANALYZER_END_OVERFLOW,
    SYNC_IO_LOGIC_ANALYZER_END_DMA_FAULT,
    SYNC_IO_LOGIC_ANALYZER_END_RESOURCE_CONFLICT,
    SYNC_IO_LOGIC_ANALYZER_END_PROFILE_MISMATCH,
    SYNC_IO_LOGIC_ANALYZER_END_GATE_BUDGET,
    SYNC_IO_LOGIC_ANALYZER_END_RESET,
} sync_io_logic_analyzer_end_reason_t;

/* A gate observation is deliberately separate from capture termination.  A
 * quality/phase/CRC issue is retained as evidence and may consume a bounded
 * debug continuation budget; only the explicit hard-stop classes may halt
 * the surrounding state machine. */
typedef enum {
    SYNC_IO_LOGIC_ANALYZER_GATE_NONE = 0,
    SYNC_IO_LOGIC_ANALYZER_GATE_SIGNAL_QUALITY,
    SYNC_IO_LOGIC_ANALYZER_GATE_CRC,
    SYNC_IO_LOGIC_ANALYZER_GATE_TIMEOUT,
    SYNC_IO_LOGIC_ANALYZER_GATE_RESOURCE_CONFLICT,
    SYNC_IO_LOGIC_ANALYZER_GATE_DMA_BOUNDS,
    SYNC_IO_LOGIC_ANALYZER_GATE_ILLEGAL_MEMORY,
    SYNC_IO_LOGIC_ANALYZER_GATE_ILLEGAL_FLASH,
    SYNC_IO_LOGIC_ANALYZER_GATE_UNCONTROLLED_GPIO,
    SYNC_IO_LOGIC_ANALYZER_GATE_COUNT,
} sync_io_logic_analyzer_gate_reason_t;

typedef enum {
    SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_REJECT = 0,
    SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_CONTINUE,
    SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_ROUND_END,
    SYNC_IO_LOGIC_ANALYZER_GATE_ACTION_HARD_STOP,
} sync_io_logic_analyzer_gate_action_t;

typedef enum {
    SYNC_IO_LOGIC_ANALYZER_RECORD_FLAG_NONE = 0u,
    SYNC_IO_LOGIC_ANALYZER_RECORD_FLAG_DIAGNOSTIC_ONLY = 1u << 0,
    SYNC_IO_LOGIC_ANALYZER_RECORD_FLAG_TRIGGER = 1u << 1,
    SYNC_IO_LOGIC_ANALYZER_RECORD_FLAG_DISCONTINUITY = 1u << 2,
} sync_io_logic_analyzer_record_flag_t;

typedef struct {
    sync_io_logic_analyzer_trigger_type_t type;
    uint32_t source_mask;
    uint32_t level_mask;
    uint32_t edge_mask;
    uint32_t pattern_mask;
    uint32_t pattern_value;
} sync_io_logic_analyzer_trigger_t;

typedef struct {
    uint32_t contract_version;
    sync_io_logic_analyzer_mode_t mode;
    uint32_t source_mask;
    uint32_t sample_period_ns;
    uint32_t max_records;
    uint32_t pre_trigger_records;
    uint32_t post_trigger_records;
    uint32_t timeout_us;
    uint32_t overwrite_oldest;
    uint32_t expected_profile_generation;
    uint32_t expected_persona_generation;
    uint32_t debug_continue_budget;
    sync_io_logic_analyzer_trigger_t trigger;
} sync_io_logic_analyzer_config_t;

typedef struct {
    uint64_t hardware_tick;
    uint32_t capture_sequence;
    uint32_t record_sequence;
    uint32_t level_mask;
    uint32_t edge_mask;
    uint32_t flags;
    uint32_t reserved;
} sync_io_logic_analyzer_record_t;

#define SYNC_IO_LOGIC_ANALYZER_MAX_RECORDS \
    ((SYNC_IO_SHARED_WORKSPACE_WORDS * sizeof(uint32_t)) / \
     sizeof(sync_io_logic_analyzer_record_t))

typedef struct {
    uint32_t contract_version;
    sync_io_logic_analyzer_state_t state;
    sync_io_logic_analyzer_mode_t mode;
    sync_io_logic_analyzer_end_reason_t end_reason;
    uint32_t capture_sequence;
    uint32_t source_mask;
    uint32_t profile_generation;
    uint32_t persona_generation;
    uint64_t base_hardware_tick;
    uint32_t hardware_tick_hz;
    uint32_t timestamp_resolution_ns;
    uint32_t timestamp_flags;
    uint32_t produced_records;
    uint32_t consumed_records;
    uint32_t dropped_records;
    uint32_t overrun_count;
    uint32_t first_fault;
    uint32_t data_crc32;
    uint32_t associated_tdma_cycle;
    uint32_t associated_tdma_persona_generation;
    uint32_t capture_complete;
    sync_io_logic_analyzer_gate_reason_t last_gate_reason;
    sync_io_logic_analyzer_gate_action_t last_gate_action;
    sync_io_logic_analyzer_state_t last_gate_state;
    uint32_t last_gate_raw_value0;
    uint32_t last_gate_raw_value1;
    uint32_t resource_conflict_mask;
    uint32_t gate_observation_count;
    uint32_t debug_continue_count;
    uint32_t product_mode;
    uint32_t hard_stop;
} sync_io_logic_analyzer_snapshot_payload_t;

typedef struct {
    volatile uint32_t sequence_lock;
    sync_io_logic_analyzer_snapshot_payload_t payload;
} sync_io_logic_analyzer_snapshot_t;

/* Core1-owned bounded RAW_SAMPLE ring.  The backing storage is supplied by
 * the caller (normally the persona's reserved SRAM workspace); no dynamic
 * allocation or SD/USB operation is permitted on this path. */
typedef struct {
    sync_io_logic_analyzer_record_t *records;
    sync_io_logic_analyzer_config_t config;
    uint32_t capacity;
    uint32_t write_index;
    uint32_t read_index;
    uint32_t produced_records;
    uint32_t consumed_records;
    uint32_t dropped_records;
    uint32_t overrun_count;
    sync_io_logic_analyzer_end_reason_t end_reason;
    sync_io_logic_analyzer_state_t state;
    bool initialized;
    bool complete;
} sync_io_logic_analyzer_raw_capture_t;

bool sync_io_logic_analyzer_config_valid(
    const sync_io_logic_analyzer_config_t *config);
sync_io_logic_analyzer_gate_action_t sync_io_logic_analyzer_gate_action(
    sync_io_logic_analyzer_gate_reason_t reason,
    bool product_mode,
    uint32_t debug_continue_count,
    uint32_t debug_continue_budget);
bool sync_io_logic_analyzer_raw_capture_init(
    sync_io_logic_analyzer_raw_capture_t *capture,
    sync_io_logic_analyzer_record_t *records,
    uint32_t capacity,
    const sync_io_logic_analyzer_config_t *config);
bool sync_io_logic_analyzer_raw_capture_push(
    sync_io_logic_analyzer_raw_capture_t *capture,
    const sync_io_logic_analyzer_record_t *record);
bool sync_io_logic_analyzer_raw_capture_pop(
    sync_io_logic_analyzer_raw_capture_t *capture,
    sync_io_logic_analyzer_record_t *record);
void sync_io_logic_analyzer_raw_capture_finish(
    sync_io_logic_analyzer_raw_capture_t *capture,
    sync_io_logic_analyzer_end_reason_t reason);
uint32_t sync_io_logic_analyzer_raw_capture_crc32(
    const sync_io_logic_analyzer_raw_capture_t *capture);
bool sync_io_logic_analyzer_raw_capture_snapshot(
    const sync_io_logic_analyzer_raw_capture_t *capture,
    sync_io_logic_analyzer_snapshot_payload_t *snapshot);
bool sync_io_logic_analyzer_snapshot_read(
    const sync_io_logic_analyzer_snapshot_t *source,
    sync_io_logic_analyzer_snapshot_payload_t *snapshot);

/* Hardware-backed RAW_SAMPLE lifecycle.  The manager owns the persona
 * lease; these calls only expose the bounded Core1 capture path.  No call
 * performs SD/USB I/O or changes a target GPIO direction. */
bool sync_io_logic_analyzer_hw_arm(
    sync_io_logic_analyzer_raw_capture_t *capture,
    sync_io_logic_analyzer_record_t *records,
    uint32_t capacity,
    const sync_io_logic_analyzer_config_t *config);
bool sync_io_logic_analyzer_hw_start(void);
void sync_io_logic_analyzer_hw_stop(void);
size_t sync_io_logic_analyzer_hw_service(uint32_t max_records);
bool sync_io_logic_analyzer_hw_active(void);

#endif
