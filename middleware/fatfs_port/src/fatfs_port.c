#include "fatfs_port.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"

static FATFS s_fatfs;
static bool s_mounted;

bool fatfs_port_is_available(void)
{
    return true;
}

fatfs_port_status_t fatfs_port_mount(void)
{
    if (s_mounted) {
        return FATFS_PORT_STATUS_OK;
    }

    const FRESULT result = f_mount(&s_fatfs, "0:", 1);
    if (result != FR_OK) {
        s_mounted = false;
        return FATFS_PORT_STATUS_MOUNT_FAILED;
    }

    s_mounted = true;
    return FATFS_PORT_STATUS_OK;
}

fatfs_port_status_t fatfs_port_unmount(void)
{
    const FRESULT result = f_mount(NULL, "0:", 0);
    s_mounted = false;
    return result == FR_OK ? FATFS_PORT_STATUS_OK : FATFS_PORT_STATUS_MOUNT_FAILED;
}

static void fatfs_port_make_path(const char *path, char *buffer, size_t buffer_size)
{
    if (buffer_size == 0u) {
        return;
    }

    const char *relative = path;
    if (relative == NULL || relative[0] == '\0') {
        relative = "/";
    }

    if (relative[0] == '0' && relative[1] == ':') {
        (void)snprintf(buffer, buffer_size, "%s", relative);
        return;
    }

    if (relative[0] == '/') {
        (void)snprintf(buffer, buffer_size, "0:%s", relative);
    } else {
        (void)snprintf(buffer, buffer_size, "0:/%s", relative);
    }
}

fatfs_port_status_t fatfs_port_catalog(const char *path, char *buffer, size_t buffer_size)
{
    if (buffer == NULL || buffer_size == 0u) {
        return FATFS_PORT_STATUS_MOUNT_FAILED;
    }

    buffer[0] = '\0';
    const fatfs_port_status_t mount_status = fatfs_port_mount();
    if (mount_status != FATFS_PORT_STATUS_OK) {
        return mount_status;
    }

    char fat_path[96];
    fatfs_port_make_path(path, fat_path, sizeof(fat_path));

    DIR dir;
    FRESULT result = f_opendir(&dir, fat_path);
    if (result != FR_OK) {
        (void)snprintf(buffer, buffer_size, "OPEN_FAILED:%u", (unsigned)result);
        return FATFS_PORT_STATUS_PATH_NOT_FOUND;
    }

    size_t used = 0u;
    FILINFO file_info;
    while (f_readdir(&dir, &file_info) == FR_OK && file_info.fname[0] != '\0') {
        const char *kind = (file_info.fattrib & AM_DIR) ? "DIR" : "FILE";
        const int written = snprintf(buffer + used,
                                     buffer_size - used,
                                     "%s,%lu,%s;",
                                     file_info.fname,
                                     (unsigned long)file_info.fsize,
                                     kind);
        if (written <= 0) {
            break;
        }
        if ((size_t)written >= buffer_size - used) {
            used = buffer_size - 1u;
            buffer[used] = '\0';
            break;
        }
        used += (size_t)written;
    }
    (void)f_closedir(&dir);

    if (buffer[0] == '\0') {
        (void)snprintf(buffer, buffer_size, "EMPTY");
    }
    return FATFS_PORT_STATUS_OK;
}

const char *fatfs_port_status_string(fatfs_port_status_t status)
{
    switch (status) {
    case FATFS_PORT_STATUS_OK: return "OK";
    case FATFS_PORT_STATUS_NOT_AVAILABLE: return "NOT_AVAILABLE";
    case FATFS_PORT_STATUS_MOUNT_FAILED: return "MOUNT_FAILED";
    case FATFS_PORT_STATUS_PATH_NOT_FOUND: return "PATH_NOT_FOUND";
    default: return "UNKNOWN";
    }
}
