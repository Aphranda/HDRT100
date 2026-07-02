#ifndef FATFS_PORT_H
#define FATFS_PORT_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    FATFS_PORT_STATUS_OK = 0,
    FATFS_PORT_STATUS_NOT_AVAILABLE,
    FATFS_PORT_STATUS_MOUNT_FAILED,
    FATFS_PORT_STATUS_PATH_NOT_FOUND,
} fatfs_port_status_t;

bool fatfs_port_is_available(void);
fatfs_port_status_t fatfs_port_mount(void);
fatfs_port_status_t fatfs_port_unmount(void);
fatfs_port_status_t fatfs_port_catalog(const char *path, char *buffer, size_t buffer_size);
const char *fatfs_port_status_string(fatfs_port_status_t status);

#endif
