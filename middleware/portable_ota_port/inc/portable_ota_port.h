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
pota_stream_ingress_result_t portable_ota_port_stream_abort(
    pota_stream_ingress_source_t source);
bool portable_ota_port_stream_is_active(void);
bool portable_ota_port_stream_get_status(pota_stream_ingress_status_t *status);
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
