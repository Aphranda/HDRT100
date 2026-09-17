#ifndef VDC_PRIORITY_TRACE_H
#define VDC_PRIORITY_TRACE_H

#include <stdbool.h>
#include <stdint.h>

#define VDC_PRIORITY_TRACE_MAGIC UINT32_C(0x52545056) /* VPTR, little endian */
#define VDC_PRIORITY_TRACE_SCHEMA 1u
#define VDC_PRIORITY_TRACE_ORIGIN_SCHEMA 3u
#define VDC_PRIORITY_TRACE_ORIGIN_EXTENSION_BYTES 96u
#define VDC_PRIORITY_TRACE_RECORD_BYTES 100u
#define VDC_PRIORITY_TRACE_MATCH_INTERVAL_MS 200u
#define VDC_PRIORITY_TRACE_READ_MAX_BYTES 128u

enum {
    VDC_PRIORITY_TRACE_IDLE = 0u, VDC_PRIORITY_TRACE_ARMED = 1u,
    VDC_PRIORITY_TRACE_RUNNING = 2u, VDC_PRIORITY_TRACE_FROZEN = 3u,
    VDC_PRIORITY_TRACE_REJECTED = 4u
};
enum {
    VDC_PRIORITY_TRACE_COMMAND_NONE = 0u, VDC_PRIORITY_TRACE_COMMAND_ARM = 1u,
    VDC_PRIORITY_TRACE_COMMAND_STOP = 2u, VDC_PRIORITY_TRACE_COMMAND_RELEASE = 3u
};
enum {
    VDC_PRIORITY_TRACE_OK = 0u, VDC_PRIORITY_TRACE_STOP = 1u,
    VDC_PRIORITY_TRACE_SESSION = 2u, VDC_PRIORITY_TRACE_BINDING = 3u,
    VDC_PRIORITY_TRACE_FULL = 4u, VDC_PRIORITY_TRACE_LEGACY_BUSY = 5u,
    VDC_PRIORITY_TRACE_RELEASED = 6u, VDC_PRIORITY_TRACE_MODE = 7u
};
enum { VDC_PRIORITY_TRACE_MATCH = 1u, VDC_PRIORITY_TRACE_DECISION = 2u,
    VDC_PRIORITY_TRACE_ORIGIN = 3u };

/* Atomic-word diagnostic status. request_seq != ack_seq means a command is
 * pending: neither ARM ownership nor STOP readback is acknowledged yet.
 * Binding fields are frozen by the first admitted match. No remote model
 * token is invented. arm_epoch is represented by low/high little-endian words. */
typedef struct {
    uint32_t schema, request_seq, ack_seq, command, state, reason;
    uint32_t capture_id, session, generation, sample_interval_ms, capacity;
    uint32_t record_count, match_count, decision_count, skipped_count, dropped_count;
    uint32_t first_ms, last_ms, freeze_ms;
    uint32_t ring_config_seq, ring_applied_seq, local_slot, reference_slot, node_count;
    uint32_t schedule_crc32, profile_crc32, path_table_crc32, path_crc32, delay_ns;
    uint32_t clock_epoch, clock_run, arm_epoch_lo, arm_epoch_hi, observer_epoch, rx_epoch, tick_hz, mode;
} vdc_priority_trace_status_t;

/* Native RAM export is this fixed header followed by record_count records.
 * payload_crc32 covers records only; READ's file_crc32 covers header+records.
 * Every integer and signed two's-complement interval is little endian. */
typedef struct {
    uint32_t magic, schema, header_size, record_size, payload_crc32;
    vdc_priority_trace_status_t status;
} vdc_priority_trace_header_t;

/* Origin schemas 2 and 3 append 96 bytes to schema 1 (264 total).
 * Schema 2 retains at most 8 clock constraints; schema 3 retains at most 64.
 * Layout and record size are identical. Replay must use the file schema's
 * retention limit, never the decoder's current firmware configuration.
 * u32: role_generation, source_epoch, first/last_source_identity,
 * first/last_published_version, last_event_sequence, last_reset_reason,
 * cache_count, cache_epoch, cache_tick_hz, cache_model_token.
 * u64: anchor_raw, anchor_local_ns, last_raw_after, last_local_ns;
 * i64: offset_lo, offset_hi_open. This is the cache at the last recorded
 * successful commit, immutable after FULL/STOP. MATCH/DECISION counts and
 * sample_interval_ms are zero. Unused follower binding fields remain zero.
 * Schema 2/3 ORIGIN record: u32 index/kind/event_sequence/model_token/tick_hz;
 * u64 raw_lo/raw_hi/bridge_before/bridge_after/bridge_local_ns/base_local_ns/
 * base_output_ns; i32 rate_ppb/phase_ns; u32 dco_seq; u64 encoded_lo;
 * u32 encoded_width. All 100 bytes describe one successful projection. */

/* Each record is exactly 100 bytes; no cross-record half-group exists.
 * Common u32: zero-based index@0, kind@4, uptime_ms@8, event_sequence@12, carrier_sequence@16.
 * MATCH: u64 raw_lo@20/raw_hi@28; i64 residual_lo@36/residual_hi@44;
 *   u64 local_lo@52/local_hi@60/remote_lo@68/remote_hi@76;
 *   u32 model_token@84/dco_seq@88; i32 actual_ppb@92; u32 match_reason@96.
 * DECISION: u32 baseline_sequence@20/model_token@24/before_seq@28/after_seq@32;
 *   i32 before_ppb@36/after_ppb@40/delta_ppb@44; u32 follow_reason@48;
 *   i64 error_lo_ppb@52/error_hi_ppb@60;
 *   u64 expected_delta_lo@68/hi@76/local_delta_lo@84/hi@92.
 * Decision outcomes are actual facts: after_seq==before_seq is not an apply.
 * MATCH decimation uses actual raw event time; DECISION records are additional.
 * No record or getter authorizes control, phase lock or a precision claim. */
typedef struct { uint8_t bytes[VDC_PRIORITY_TRACE_RECORD_BYTES]; } vdc_priority_trace_record_t;

/* Core0, stopped metadata gate. Nonzero capture_id must increase within boot.
 * True only acknowledges request admission; poll status ACK while STOPPED
 * before ring ARM/read/reuse. RELEASE ACK relinquishes the shared legacy pool. */
bool vdc_dpll_manager_priority_trace_arm(uint32_t capture_id);
bool vdc_dpll_manager_priority_trace_origin_arm(uint32_t capture_id);
bool vdc_dpll_manager_priority_trace_stop(void);
bool vdc_dpll_manager_priority_trace_release(void);
bool vdc_dpll_manager_get_priority_trace(vdc_priority_trace_status_t *out);
bool vdc_dpll_manager_priority_trace_read(uint32_t capture_id, uint32_t offset,
    uint8_t *data, uint32_t size, uint32_t *total_bytes, uint32_t *file_crc32);

#endif
