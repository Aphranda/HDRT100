#ifndef PORTABLE_OTA_PORT_H
#define PORTABLE_OTA_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ota_package.h"
#include "ota_metadata.h"
#include "ota_vector.h"
#include "pota_direct_ab.h"
#include "pota_stream_ingress.h"
#include "pota_stream_wire.h"

bool portable_ota_port_parse_package_header(const uint8_t *data,
                                            uint32_t length,
                                            ota_package_manifest_t *manifest);
const ota_package_image_t *portable_ota_port_find_package_image(const ota_package_manifest_t *manifest,
                                                                ota_slot_t slot);
uint32_t portable_ota_port_crc32_update(uint32_t crc, const uint8_t *data, size_t length);
uint32_t portable_ota_port_crc32_compute(const uint8_t *data, size_t length);
/* Last flash-execute failure token: 0 = OK, 1 = range/permission rejected,
 * else 200 + 10*error_class + failing_state.  Read-only diagnostic used to tell
 * a park problem (state PARK_CORE1) from a raw erase/verify problem. */
uint32_t portable_ota_port_flash_stage(void);
bool portable_ota_port_validate_app_vector(uint32_t app_flash_offset,
                                           uint32_t app_size,
                                           uint32_t run_flash_offset);
bool portable_ota_port_core_begin(const ota_metadata_t *metadata,
                                  uint32_t size,
                                  uint32_t crc32,
                                  bool package_mode,
                                  ota_vector_t *vector);
bool portable_ota_port_core_service(uint32_t budget_us, ota_vector_t *vector);
bool portable_ota_port_core_write(const uint8_t *data, uint32_t length, ota_vector_t *vector);
bool portable_ota_port_core_end(ota_vector_t *vector);
bool portable_ota_port_core_abort(ota_vector_t *vector);
/* Initialize v2 durable OTA/completion stores independently of the current
 * BCB/active-slot state.  Factory/empty metadata must not leave the real
 * FlashTransaction completion lease unconfigured. */
bool portable_ota_port_durable_init(void);
/* App-side transport-neutral stream boundary.  The port owns the platform
 * callbacks and target-slot context; callers only provide stream metadata and
 * transport frames.  Boot builds return false/zero and never expose this
 * writer boundary. */
bool portable_ota_port_stream_init(const ota_metadata_t *metadata);
pota_stream_ingress_result_t portable_ota_port_stream_open(
    pota_stream_ingress_source_t source,
    const pota_stream_open_t *open);
pota_stream_ingress_result_t portable_ota_port_stream_write(
    pota_stream_ingress_source_t source,
    uint32_t offset,
    const uint8_t *data,
    uint32_t size,
    bool has_crc32,
    uint32_t crc32);
pota_stream_ingress_result_t portable_ota_port_stream_service(uint32_t budget_us);
pota_stream_ingress_result_t portable_ota_port_stream_close(
    pota_stream_ingress_source_t source);
/* Drops any open stream session and rebuilds the ingress surfaces.  Used by
 * the TDMA target when a retried OPEN arrives while an old lease is stuck. */
bool portable_ota_port_stream_force_reset(void);

/* Raw pota_stream_result_t of the last ingestion session open (diagnostics). */
uint32_t portable_ota_port_stream_last_session_result(void);
/* Stage code of the last stream-init attempt: 0 never run, 100..106 init
 * sub-steps, 200/201/202 the platform-recovery path. */
uint32_t portable_ota_port_stream_init_stage(void);
/* Last stream write/service failure code: 5xxx = pota_stream_result_t,
 * 6xxx = 6000 + pota_error_t (diagnostics). */
uint32_t portable_ota_port_stream_last_service_result(void);
uint32_t portable_ota_port_stream_platform_map_version(void);

pota_stream_ingress_result_t portable_ota_port_stream_abort(
    pota_stream_ingress_source_t source);
bool portable_ota_port_stream_is_active(void);
bool portable_ota_port_stream_get_status(pota_stream_ingress_status_t *status);
uint32_t portable_ota_port_stream_core_state(void);
uint32_t portable_ota_port_stream_total_size(void);
/* Inactive-slot erase progress for host polling: core state (pota_state_t),
 * bytes already erased, and total bytes to erase. */
bool portable_ota_port_stream_erase_progress(uint32_t *core_state,
                                             uint32_t *erased_bytes,
                                             uint32_t *erase_bytes);
/* Lightweight OTA performance counters (milliseconds): [0]=flash execute,
 * [1]=stream write, [2]=stream service, [3]=stream close.  Count is reserved
 * (0) so the RAM cost stays 16 bytes.  Diagnostic only. */
#define PORTABLE_OTA_PORT_PERF_COUNT 4u
void portable_ota_port_perf_get(uint32_t *last_us, uint32_t *max_us,
                                uint32_t *count);
const char *portable_ota_port_state_to_string(ota_state_t state);
const char *portable_ota_port_error_to_string(uint32_t error_code);
const char *portable_ota_port_result_to_string(ota_result_t result);
const char *portable_ota_port_boot_result_to_string(uint32_t result);
uint32_t portable_ota_port_metadata_crc32(const ota_metadata_t *metadata);
uint32_t portable_ota_port_metadata_ext_crc32(const ota_metadata_t *metadata);
uint32_t portable_ota_port_metadata_ab_crc32(const ota_metadata_t *metadata);
void portable_ota_port_metadata_update_crc(ota_metadata_t *metadata);
bool portable_ota_port_metadata_is_valid(const ota_metadata_t *metadata);
bool portable_ota_port_metadata_copy_txn_state_is_valid(uint32_t state);
bool portable_ota_port_metadata_boot_mode_is_valid(uint32_t mode);
bool portable_ota_port_metadata_slot_or_none_is_valid(uint32_t slot);
void portable_ota_port_metadata_clear_copy_transaction_fields(ota_metadata_t *metadata);
void portable_ota_port_metadata_init_extension_defaults(ota_metadata_t *metadata);
void portable_ota_port_metadata_set_default(ota_metadata_t *metadata);
void portable_ota_port_metadata_upgrade_if_needed(ota_metadata_t *metadata);
bool portable_ota_port_metadata_mark_pending(ota_metadata_t *metadata,
                                             ota_slot_t slot,
                                             uint32_t image_size,
                                             uint32_t image_crc32);
bool portable_ota_port_metadata_can_confirm_active(const ota_metadata_t *metadata);
bool portable_ota_port_metadata_confirm_active(ota_metadata_t *metadata);
bool portable_ota_port_metadata_set_boot_mode(ota_metadata_t *metadata, ota_boot_mode_t mode);
bool portable_ota_port_metadata_set_fault_injection(ota_metadata_t *metadata, uint32_t flags);
bool portable_ota_port_metadata_begin_copy_transaction(ota_metadata_t *metadata,
                                                       ota_slot_t source,
                                                       ota_slot_t destination,
                                                       uint32_t image_size,
                                                       uint32_t image_crc32);
bool portable_ota_port_metadata_update_copy_transaction(ota_metadata_t *metadata,
                                                        uint32_t state,
                                                        uint32_t written,
                                                        uint32_t last_error);
bool portable_ota_port_metadata_finish_copy_transaction(ota_metadata_t *metadata);
bool portable_ota_port_metadata_fail_copy_transaction(ota_metadata_t *metadata,
                                                      uint32_t last_error);
bool portable_ota_port_metadata_clear_copy_transaction(ota_metadata_t *metadata);
bool portable_ota_port_metadata_record_boot_result(ota_metadata_t *metadata,
                                                   ota_boot_result_t result,
                                                   ota_slot_t source_slot,
                                                   bool clear_pending);
bool portable_ota_port_metadata_apply_copy_to_active_done(ota_metadata_t *metadata,
                                                          ota_slot_t staging_slot,
                                                          ota_slot_t active_slot);
bool portable_ota_port_metadata_apply_direct_ab_pending(ota_metadata_t *metadata,
                                                        ota_slot_t pending_slot);
bool portable_ota_port_metadata_rollback_direct_ab(ota_metadata_t *metadata,
                                                   ota_boot_result_t reason,
                                                   ota_slot_t failed_slot,
                                                   ota_slot_t rollback_slot);
bool portable_ota_port_metadata_increment_boot_attempts(ota_metadata_t *metadata);
bool portable_ota_port_metadata_direct_ab_decide(
    const ota_metadata_t *metadata,
    uint32_t max_boot_attempts,
    pota_direct_ab_decision_t *decision);
const ota_metadata_t *portable_ota_port_metadata_select_newest(const ota_metadata_t *copies,
                                                               size_t copy_count);

#endif
