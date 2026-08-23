#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "flash_deployment_map.h"
#include "pico/bootrom.h"
#include "pico/stdlib.h"
#include "pota_boot_control_store.h"
#include "project_build_info.h"

#define RECOVERY_COMMAND_CAPACITY 96u
#define RECOVERY_BCB_LANE_SIZE \
    (FLASH_DEPLOYMENT_MAP_BOOT_CONTROL_SIZE / POTA_BCB_LANE_COUNT)
#define RECOVERY_BCB_LANE_PAGE_COUNT \
    (RECOVERY_BCB_LANE_SIZE / POTA_BCB_PAGE_SIZE)

_Static_assert(FLASH_DEPLOYMENT_MAP_HAS_RECOVERY == 1u,
               "Recovery target requires a deployment map with Recovery");
_Static_assert((FLASH_DEPLOYMENT_MAP_BOOT_CONTROL_SIZE %
                POTA_BCB_LANE_COUNT) == 0u,
               "Boot Control must split evenly into BCB lanes");

typedef struct {
    const char *name;
    uint32_t id;
    uint32_t offset;
    uint32_t size;
} recovery_partition_t;

#define RECOVERY_PARTITION(name, id, offset, size, alignment, boot, app, factory, executable) \
    {#name, id, offset, size},

static const recovery_partition_t s_recovery_partitions[] = {
    FLASH_DEPLOYMENT_MAP_PARTITION_TABLE(RECOVERY_PARTITION)
};

#undef RECOVERY_PARTITION

static bool recovery_bcb_read_page(void *context, uint32_t lane,
                                   uint32_t page, uint8_t *data,
                                   uint32_t length)
{
    (void)context;
    if (data == NULL || lane >= POTA_BCB_LANE_COUNT ||
        page >= RECOVERY_BCB_LANE_PAGE_COUNT ||
        length != POTA_BCB_PAGE_SIZE) {
        return false;
    }
    const uint32_t offset = FLASH_DEPLOYMENT_MAP_BOOT_CONTROL_OFFSET +
                            lane * RECOVERY_BCB_LANE_SIZE +
                            page * POTA_BCB_PAGE_SIZE;
    if (offset >= FLASH_DEPLOYMENT_GEOMETRY_TOTAL_SIZE ||
        length > FLASH_DEPLOYMENT_GEOMETRY_TOTAL_SIZE - offset) {
        return false;
    }
    memcpy(data, (const void *)(uintptr_t)(
                     FLASH_DEPLOYMENT_GEOMETRY_XIP_BASE + offset),
           length);
    return true;
}

__attribute__((noinline)) bool recovery_get_bcb_health(
    pota_bcb_health_snapshot_t *health)
{
    const pota_bcb_platform_t platform = {
        .read_page = recovery_bcb_read_page,
    };
    pota_bcb_store_t store;
    return health != NULL &&
           pota_bcb_store_init_read_only(
               &store, &platform,
               FLASH_DEPLOYMENT_MAP_SCHEMA_VERSION,
               FLASH_DEPLOYMENT_MAP_VERSION,
               RECOVERY_BCB_LANE_PAGE_COUNT) == POTA_BCB_RESULT_OK &&
           pota_bcb_store_get_health_snapshot(&store, health);
}

__attribute__((noinline)) bool recovery_get_bcb_wear(
    pota_bcb_wear_snapshot_t *wear)
{
    const pota_bcb_platform_t platform = {
        .read_page = recovery_bcb_read_page,
    };
    pota_bcb_store_t store;
    return wear != NULL &&
           pota_bcb_store_init_read_only(
               &store, &platform,
               FLASH_DEPLOYMENT_MAP_SCHEMA_VERSION,
               FLASH_DEPLOYMENT_MAP_VERSION,
               RECOVERY_BCB_LANE_PAGE_COUNT) == POTA_BCB_RESULT_OK &&
           pota_bcb_store_get_wear_snapshot(&store, wear);
}

static void recovery_print_map(void)
{
    for (uint32_t index = 0u;
         index < sizeof(s_recovery_partitions) /
                     sizeof(s_recovery_partitions[0]);
         index++) {
        const recovery_partition_t *partition = &s_recovery_partitions[index];
        printf("%s,%lu,%lu,%lu%s", partition->name,
               (unsigned long)partition->id,
               (unsigned long)partition->offset,
               (unsigned long)partition->size,
               index + 1u == sizeof(s_recovery_partitions) /
                                  sizeof(s_recovery_partitions[0])
                   ? "\r\n"
                   : ";");
    }
}

static void recovery_print_bcb_health(void)
{
    pota_bcb_health_snapshot_t health;
    if (!recovery_get_bcb_health(&health)) {
        puts("ERR,BCB_READ\r");
        return;
    }
    printf("%lu,%lu,%lu,%lu,%lu,%lu,%lu\r\n",
           (unsigned long)health.valid_lane_count,
           (unsigned long)health.valid_record_count,
           (unsigned long)health.newest_lane_generation,
           (unsigned long)health.newest_sequence,
           (unsigned long)health.newest_security_counter,
           (unsigned long)health.newest_lane,
           (unsigned long)health.newest_record_page);
}

static void recovery_print_bcb_wear(void)
{
    pota_bcb_wear_snapshot_t wear;
    if (!recovery_get_bcb_wear(&wear)) {
        puts("ERR,BCB_READ\r");
        return;
    }
    printf("%lu,%lu\r\n",
           (unsigned long)wear.program_page_count,
           (unsigned long)wear.erase_lane_count);
}

static void recovery_process_command(const char *command)
{
    if (strcmp(command, "*IDN?") == 0) {
        printf("Aphranda,DHRT100,RECOVERY,%s\r\n", g_project_build_id);
    } else if (strcmp(command, "SYST:RECOVERY:STATUS?") == 0) {
        printf("RECOVERY,%lu,%s,%s\r\n",
               (unsigned long)FLASH_DEPLOYMENT_MAP_VERSION,
               FLASH_DEPLOYMENT_MAP_STATE, g_project_build_id);
    } else if (strcmp(command, "SYST:RECOVERY:MAP?") == 0) {
        recovery_print_map();
    } else if (strcmp(command, "SYST:RECOVERY:BCB:HEALTH?") == 0) {
        recovery_print_bcb_health();
    } else if (strcmp(command, "SYST:RECOVERY:BCB:WEAR?") == 0) {
        recovery_print_bcb_wear();
    } else if (strcmp(command, "SYST:RECOVERY:BOOTSEL DHRT100") == 0) {
        puts("OK,BOOTSEL\r");
        stdio_flush();
        sleep_ms(20u);
        reset_usb_boot(0u, 0u);
    } else {
        puts("ERR,UNKNOWN_COMMAND\r");
    }
}

int main(void)
{
    stdio_init_all();
    char command[RECOVERY_COMMAND_CAPACITY];
    uint32_t length = 0u;

    while (true) {
        const int character = getchar_timeout_us(1000u);
        if (character == PICO_ERROR_TIMEOUT) {
            tight_loop_contents();
            continue;
        }
        if (character == '\r' || character == '\n') {
            if (length != 0u) {
                command[length] = '\0';
                recovery_process_command(command);
                length = 0u;
            }
            continue;
        }
        if (length + 1u < sizeof(command)) {
            command[length++] = (char)character;
        } else {
            length = 0u;
            puts("ERR,COMMAND_TOO_LONG\r");
        }
    }
}
