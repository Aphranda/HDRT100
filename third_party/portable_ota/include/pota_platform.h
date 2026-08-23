#ifndef POTA_PLATFORM_H
#define POTA_PLATFORM_H

#include "pota_types.h"
#include "pota_package.h"

typedef struct {
    uint32_t offset;
    uint32_t size;
    uint32_t run_offset;
} pota_partition_t;

typedef struct {
    const char *product_id;
    const char *hardware_id;
    uint32_t bootloader_version;
    uint32_t map_version;
    uint32_t slot_a_partition_id;
    uint32_t slot_b_partition_id;
    pota_boot_mode_t boot_mode;
    pota_slot_t active_slot;
    pota_partition_t slot_a;
    pota_partition_t slot_b;
    uint32_t flash_page_size;
    uint32_t flash_sector_size;
    uint32_t security_counter;
    bool require_signature;
    pota_package_signature_verify_fn verify_manifest_signature;
    void *verify_manifest_context;
} pota_platform_info_t;

typedef struct {
    bool (*flash_read)(uint32_t offset, void *buffer, uint32_t size);
    bool (*flash_erase)(uint32_t offset, uint32_t size);
    bool (*flash_program)(uint32_t offset, const void *data, uint32_t size);
    bool (*mark_pending)(pota_slot_t slot, uint32_t image_size,
                         uint32_t image_crc32, uint32_t security_counter);
    bool (*confirm_active)(void);
    bool (*validate_vector)(uint32_t slot_offset, uint32_t image_size, uint32_t run_offset);
    void (*ota_lock)(void);
    void (*ota_unlock)(void);
    void (*ota_yield_or_delay)(void);
    void (*feed_watchdog)(void);
    void (*invalidate_cache)(void);
    void (*reboot)(void);
    uint32_t (*time_ms)(void);
    void (*log)(int level, const char *message);
} pota_platform_ops_t;

typedef struct {
    pota_platform_info_t info;
    pota_platform_ops_t ops;
} pota_platform_t;

#endif
