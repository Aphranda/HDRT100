#include "portable_ota_port.h"

#include <stddef.h>

#include "pota.h"
#include "project_config.h"

#define PORTABLE_OTA_STATIC_ASSERT(name, condition) \
    typedef char portable_ota_static_assert_##name[(condition) ? 1 : -1]

PORTABLE_OTA_STATIC_ASSERT(metadata_size, sizeof(ota_metadata_t) == sizeof(pota_metadata_t));
PORTABLE_OTA_STATIC_ASSERT(metadata_magic, OTA_METADATA_MAGIC == POTA_METADATA_MAGIC);
PORTABLE_OTA_STATIC_ASSERT(metadata_version, OTA_METADATA_VERSION == POTA_METADATA_VERSION);
PORTABLE_OTA_STATIC_ASSERT(metadata_magic_offset,
                           offsetof(ota_metadata_t, magic) == offsetof(pota_metadata_t, magic));
PORTABLE_OTA_STATIC_ASSERT(metadata_slot_a_sha256_offset,
                           offsetof(ota_metadata_t, slot_a_sha256) ==
                               offsetof(pota_metadata_t, slot_a_sha256));
PORTABLE_OTA_STATIC_ASSERT(metadata_metadata_crc32_offset,
                           offsetof(ota_metadata_t, metadata_crc32) ==
                               offsetof(pota_metadata_t, metadata_crc32));
PORTABLE_OTA_STATIC_ASSERT(metadata_copy_txn_state_offset,
                           offsetof(ota_metadata_t, copy_txn_state) ==
                               offsetof(pota_metadata_t, copy_txn_state));
PORTABLE_OTA_STATIC_ASSERT(metadata_boot_mode_offset,
                           offsetof(ota_metadata_t, boot_mode) == offsetof(pota_metadata_t, boot_mode));
PORTABLE_OTA_STATIC_ASSERT(metadata_ab_crc32_offset,
                           offsetof(ota_metadata_t, metadata_ab_crc32) ==
                               offsetof(pota_metadata_t, metadata_ab_crc32));

static const pota_metadata_t *portable_metadata_const(const ota_metadata_t *metadata)
{
    return (const pota_metadata_t *)metadata;
}

static pota_metadata_t *portable_metadata_mutable(ota_metadata_t *metadata)
{
    return (pota_metadata_t *)metadata;
}

uint32_t portable_ota_port_metadata_crc32(const ota_metadata_t *metadata)
{
    return pota_metadata_crc32(portable_metadata_const(metadata));
}

uint32_t portable_ota_port_metadata_ext_crc32(const ota_metadata_t *metadata)
{
    return pota_metadata_ext_crc32(portable_metadata_const(metadata));
}

uint32_t portable_ota_port_metadata_ab_crc32(const ota_metadata_t *metadata)
{
    return pota_metadata_ab_crc32(portable_metadata_const(metadata));
}

void portable_ota_port_metadata_update_crc(ota_metadata_t *metadata)
{
    pota_metadata_update_crc(portable_metadata_mutable(metadata));
}

bool portable_ota_port_metadata_is_valid(const ota_metadata_t *metadata)
{
    return pota_metadata_is_valid(portable_metadata_const(metadata));
}

bool portable_ota_port_metadata_copy_txn_state_is_valid(uint32_t state)
{
    return pota_metadata_copy_txn_state_is_valid(state);
}

bool portable_ota_port_metadata_boot_mode_is_valid(uint32_t mode)
{
    return pota_metadata_boot_mode_is_valid(mode);
}

bool portable_ota_port_metadata_slot_or_none_is_valid(uint32_t slot)
{
    return pota_metadata_slot_or_none_is_valid(slot);
}

void portable_ota_port_metadata_clear_copy_transaction_fields(ota_metadata_t *metadata)
{
    pota_metadata_clear_copy_transaction_fields(portable_metadata_mutable(metadata));
}

void portable_ota_port_metadata_init_extension_defaults(ota_metadata_t *metadata)
{
    pota_metadata_init_extension_defaults(portable_metadata_mutable(metadata));
}

void portable_ota_port_metadata_set_default(ota_metadata_t *metadata)
{
    pota_metadata_set_default(portable_metadata_mutable(metadata));
#if PROJECT_OTA_DEFAULT_BOOT_MODE_DIRECT_AB
    (void)pota_metadata_set_boot_mode(portable_metadata_mutable(metadata),
                                      POTA_BOOT_MODE_DIRECT_AB);
#endif
}

void portable_ota_port_metadata_upgrade_if_needed(ota_metadata_t *metadata)
{
    pota_metadata_upgrade_if_needed(portable_metadata_mutable(metadata));
}

bool portable_ota_port_metadata_mark_pending(ota_metadata_t *metadata,
                                             ota_slot_t slot,
                                             uint32_t image_size,
                                             uint32_t image_crc32)
{
    return pota_metadata_mark_pending(portable_metadata_mutable(metadata),
                                      (pota_slot_t)slot,
                                      image_size,
                                      image_crc32);
}

bool portable_ota_port_metadata_can_confirm_active(const ota_metadata_t *metadata)
{
    return pota_metadata_can_confirm_active(portable_metadata_const(metadata));
}

bool portable_ota_port_metadata_confirm_active(ota_metadata_t *metadata)
{
    return pota_metadata_confirm_active(portable_metadata_mutable(metadata));
}

bool portable_ota_port_metadata_set_boot_mode(ota_metadata_t *metadata, ota_boot_mode_t mode)
{
    return pota_metadata_set_boot_mode(portable_metadata_mutable(metadata),
                                       (pota_boot_mode_t)mode);
}

bool portable_ota_port_metadata_set_fault_injection(ota_metadata_t *metadata, uint32_t flags)
{
    return pota_metadata_set_fault_injection(portable_metadata_mutable(metadata), flags);
}

bool portable_ota_port_metadata_begin_copy_transaction(ota_metadata_t *metadata,
                                                       ota_slot_t source,
                                                       ota_slot_t destination,
                                                       uint32_t image_size,
                                                       uint32_t image_crc32)
{
    return pota_metadata_begin_copy_transaction(portable_metadata_mutable(metadata),
                                                (pota_slot_t)source,
                                                (pota_slot_t)destination,
                                                image_size,
                                                image_crc32);
}

bool portable_ota_port_metadata_update_copy_transaction(ota_metadata_t *metadata,
                                                        uint32_t state,
                                                        uint32_t written,
                                                        uint32_t last_error)
{
    return pota_metadata_update_copy_transaction(portable_metadata_mutable(metadata),
                                                 state,
                                                 written,
                                                 last_error);
}

bool portable_ota_port_metadata_finish_copy_transaction(ota_metadata_t *metadata)
{
    return pota_metadata_finish_copy_transaction(portable_metadata_mutable(metadata));
}

bool portable_ota_port_metadata_fail_copy_transaction(ota_metadata_t *metadata,
                                                      uint32_t last_error)
{
    return pota_metadata_fail_copy_transaction(portable_metadata_mutable(metadata), last_error);
}

bool portable_ota_port_metadata_clear_copy_transaction(ota_metadata_t *metadata)
{
    return pota_metadata_clear_copy_transaction(portable_metadata_mutable(metadata));
}

bool portable_ota_port_metadata_record_boot_result(ota_metadata_t *metadata,
                                                   ota_boot_result_t result,
                                                   ota_slot_t source_slot,
                                                   bool clear_pending)
{
    return pota_metadata_record_boot_result(portable_metadata_mutable(metadata),
                                            (pota_boot_result_t)result,
                                            (pota_slot_t)source_slot,
                                            clear_pending);
}

bool portable_ota_port_metadata_apply_copy_to_active_done(ota_metadata_t *metadata,
                                                          ota_slot_t staging_slot,
                                                          ota_slot_t active_slot)
{
    return pota_metadata_apply_copy_to_active_done(portable_metadata_mutable(metadata),
                                                   (pota_slot_t)staging_slot,
                                                   (pota_slot_t)active_slot);
}

bool portable_ota_port_metadata_apply_direct_ab_pending(ota_metadata_t *metadata,
                                                        ota_slot_t pending_slot)
{
    return pota_metadata_apply_direct_ab_pending(portable_metadata_mutable(metadata),
                                                 (pota_slot_t)pending_slot);
}

bool portable_ota_port_metadata_rollback_direct_ab(ota_metadata_t *metadata,
                                                   ota_boot_result_t reason,
                                                   ota_slot_t failed_slot,
                                                   ota_slot_t rollback_slot)
{
    return pota_metadata_rollback_direct_ab(portable_metadata_mutable(metadata),
                                            (pota_boot_result_t)reason,
                                            (pota_slot_t)failed_slot,
                                            (pota_slot_t)rollback_slot);
}

bool portable_ota_port_metadata_increment_boot_attempts(ota_metadata_t *metadata)
{
    return pota_metadata_increment_boot_attempts(portable_metadata_mutable(metadata));
}

bool portable_ota_port_metadata_direct_ab_decide(
    const ota_metadata_t *metadata,
    uint32_t max_boot_attempts,
    pota_direct_ab_decision_t *decision)
{
    return pota_direct_ab_decide(portable_metadata_const(metadata),
                                 max_boot_attempts, decision);
}

const ota_metadata_t *portable_ota_port_metadata_select_newest(const ota_metadata_t *copies,
                                                               size_t copy_count)
{
    const pota_metadata_t *selected =
        pota_metadata_select_newest(portable_metadata_const(copies), copy_count);
    return (const ota_metadata_t *)selected;
}
