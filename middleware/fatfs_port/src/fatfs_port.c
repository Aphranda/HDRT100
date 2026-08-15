#include "fatfs_port.h"

#include <stdio.h>
#include <string.h>

#include "ff.h"

static FATFS s_fatfs;
static bool s_mounted;
static FRESULT s_last_mount_result = FR_OK;
static FRESULT s_last_mkfs_result = FR_OK;

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
    s_last_mount_result = result;
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
    s_last_mount_result = result;
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

fatfs_port_status_t fatfs_port_catalog_page(const char *path,
                                            uint32_t offset,
                                            uint32_t limit,
                                            char *buffer,
                                            size_t buffer_size,
                                            fatfs_port_catalog_page_t *page)
{
    if (buffer == NULL || buffer_size == 0u || page == NULL || limit == 0u) {
        return FATFS_PORT_STATUS_MOUNT_FAILED;
    }

    buffer[0] = '\0';
    memset(page, 0, sizeof(*page));
    page->complete = true;

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
    uint32_t index = 0u;
    FILINFO file_info;
    while (f_readdir(&dir, &file_info) == FR_OK && file_info.fname[0] != '\0') {
        if (index++ < offset) {
            continue;
        }

        if (page->returned_count >= limit) {
            page->complete = false;
            page->next_offset = offset + page->returned_count;
            break;
        }

        const char *kind = (file_info.fattrib & AM_DIR) ? "DIR" : "FILE";
        const int written = snprintf(buffer + used,
                                     buffer_size - used,
                                     "%s,%lu,%s;",
                                     file_info.fname,
                                     (unsigned long)file_info.fsize,
                                     kind);
        if (written <= 0) {
            page->truncated = true;
            page->complete = false;
            page->next_offset = offset + page->returned_count;
            break;
        }
        if ((size_t)written >= buffer_size - used) {
            page->truncated = true;
            page->complete = false;
            page->next_offset = offset + page->returned_count;
            if (buffer_size > 0u) {
                buffer[buffer_size - 1u] = '\0';
            }
            break;
        }
        used += (size_t)written;
        page->returned_count++;
    }
    (void)f_closedir(&dir);

    if (buffer[0] == '\0') {
        (void)snprintf(buffer, buffer_size, "EMPTY");
    }
    if (page->complete) {
        page->next_offset = 0u;
    }
    return FATFS_PORT_STATUS_OK;
}

fatfs_port_status_t fatfs_port_file_info(const char *path, fatfs_port_file_info_t *info)
{
    if (path == NULL || info == NULL) {
        return FATFS_PORT_STATUS_PATH_NOT_FOUND;
    }

    memset(info, 0, sizeof(*info));
    const fatfs_port_status_t mount_status = fatfs_port_mount();
    if (mount_status != FATFS_PORT_STATUS_OK) {
        return mount_status;
    }

    char fat_path[96];
    fatfs_port_make_path(path, fat_path, sizeof(fat_path));

    FILINFO file_info;
    const FRESULT result = f_stat(fat_path, &file_info);
    if (result != FR_OK) {
        return FATFS_PORT_STATUS_PATH_NOT_FOUND;
    }

    info->size = (uint32_t)file_info.fsize;
    info->is_dir = (file_info.fattrib & AM_DIR) != 0u;
    return FATFS_PORT_STATUS_OK;
}

fatfs_port_status_t fatfs_port_read_text_file(const char *path,
                                              char *buffer,
                                              size_t buffer_size,
                                              size_t *bytes_read)
{
    if (path == NULL || buffer == NULL || buffer_size == 0u) {
        return FATFS_PORT_STATUS_READ_FAILED;
    }

    buffer[0] = '\0';
    if (bytes_read != NULL) {
        *bytes_read = 0u;
    }

    const fatfs_port_status_t mount_status = fatfs_port_mount();
    if (mount_status != FATFS_PORT_STATUS_OK) {
        return mount_status;
    }

    char fat_path[96];
    fatfs_port_make_path(path, fat_path, sizeof(fat_path));

    FIL file;
    FRESULT result = f_open(&file, fat_path, FA_READ);
    if (result != FR_OK) {
        return FATFS_PORT_STATUS_PATH_NOT_FOUND;
    }

    UINT read_count = 0u;
    result = f_read(&file, buffer, (UINT)(buffer_size - 1u), &read_count);
    (void)f_close(&file);
    if (result != FR_OK) {
        buffer[0] = '\0';
        return FATFS_PORT_STATUS_READ_FAILED;
    }

    buffer[read_count] = '\0';
    if (bytes_read != NULL) {
        *bytes_read = (size_t)read_count;
    }
    return FATFS_PORT_STATUS_OK;
}

fatfs_port_status_t fatfs_port_read_binary_range(const char *path,
                                                 uint32_t offset,
                                                 uint8_t *buffer,
                                                 size_t buffer_size,
                                                 size_t *bytes_read,
                                                 uint32_t *file_size)
{
    if (path == NULL || buffer == NULL || buffer_size == 0u) {
        return FATFS_PORT_STATUS_READ_FAILED;
    }

    if (bytes_read != NULL) {
        *bytes_read = 0u;
    }
    if (file_size != NULL) {
        *file_size = 0u;
    }

    const fatfs_port_status_t mount_status = fatfs_port_mount();
    if (mount_status != FATFS_PORT_STATUS_OK) {
        return mount_status;
    }

    char fat_path[96];
    fatfs_port_make_path(path, fat_path, sizeof(fat_path));

    FIL file;
    FRESULT result = f_open(&file, fat_path, FA_READ);
    if (result != FR_OK) {
        return FATFS_PORT_STATUS_PATH_NOT_FOUND;
    }

    const FSIZE_t total_size = f_size(&file);
    if (file_size != NULL) {
        *file_size = (uint32_t)total_size;
    }

    if ((FSIZE_t)offset >= total_size) {
        (void)f_close(&file);
        return FATFS_PORT_STATUS_OK;
    }

    result = f_lseek(&file, (FSIZE_t)offset);
    if (result != FR_OK) {
        (void)f_close(&file);
        return FATFS_PORT_STATUS_READ_FAILED;
    }

    UINT read_count = 0u;
    result = f_read(&file, buffer, (UINT)buffer_size, &read_count);
    (void)f_close(&file);
    if (result != FR_OK) {
        return FATFS_PORT_STATUS_READ_FAILED;
    }

    if (bytes_read != NULL) {
        *bytes_read = (size_t)read_count;
    }
    return FATFS_PORT_STATUS_OK;
}

fatfs_port_status_t fatfs_port_make_directory(const char *path)
{
    if (path == NULL) {
        return FATFS_PORT_STATUS_PATH_NOT_FOUND;
    }

    const fatfs_port_status_t mount_status = fatfs_port_mount();
    if (mount_status != FATFS_PORT_STATUS_OK) {
        return mount_status;
    }

    char fat_path[96];
    fatfs_port_make_path(path, fat_path, sizeof(fat_path));

    const FRESULT result = f_mkdir(fat_path);
    if (result == FR_OK) {
        return FATFS_PORT_STATUS_OK;
    }
    if (result == FR_EXIST) {
        FILINFO file_info;
        if (f_stat(fat_path, &file_info) == FR_OK &&
            (file_info.fattrib & AM_DIR) != 0u) {
            return FATFS_PORT_STATUS_OK;
        }
        return FATFS_PORT_STATUS_WRITE_FAILED;
    }
    return FATFS_PORT_STATUS_WRITE_FAILED;
}

fatfs_port_status_t fatfs_port_format_volume(void)
{
    uint8_t work_buffer[4096];
    const MKFS_PARM options = {
        .fmt = FM_FAT | FM_FAT32 | FM_SFD,
        .n_fat = 0,
        .align = 0,
        .n_root = 0,
        .au_size = 0,
    };

    (void)fatfs_port_unmount();

    const FRESULT result = f_mkfs("0:", &options, work_buffer, sizeof(work_buffer));
    s_last_mkfs_result = result;
    if (result != FR_OK) {
        return FATFS_PORT_STATUS_FORMAT_FAILED;
    }

    return fatfs_port_mount();
}

uint32_t fatfs_port_last_mkfs_result(void)
{
    return (uint32_t)s_last_mkfs_result;
}

uint32_t fatfs_port_last_mount_result(void)
{
    return (uint32_t)s_last_mount_result;
}

static fatfs_port_status_t fatfs_port_write_bytes_to_fat_path(const char *fat_path,
                                                              const uint8_t *data,
                                                              size_t data_size)
{
    FIL file;
    FRESULT result = f_open(&file, fat_path, FA_CREATE_ALWAYS | FA_WRITE);
    if (result != FR_OK) {
        return FATFS_PORT_STATUS_WRITE_FAILED;
    }

    UINT written = 0u;
    if (data_size > 0u) {
        result = f_write(&file, data, (UINT)data_size, &written);
    }
    if (result == FR_OK && written == (UINT)data_size) {
        result = f_sync(&file);
    }
    const FRESULT close_result = f_close(&file);
    if (result != FR_OK || close_result != FR_OK || written != (UINT)data_size) {
        return FATFS_PORT_STATUS_WRITE_FAILED;
    }

    return FATFS_PORT_STATUS_OK;
}

static fatfs_port_status_t fatfs_port_replace_tmp_file(const char *fat_tmp_path,
                                                       const char *fat_final_path)
{
    FRESULT result = f_rename(fat_tmp_path, fat_final_path);
    if (result != FR_OK) {
        (void)f_unlink(fat_final_path);
        result = f_rename(fat_tmp_path, fat_final_path);
    }
    return result == FR_OK ? FATFS_PORT_STATUS_OK : FATFS_PORT_STATUS_RENAME_FAILED;
}

static fatfs_port_status_t fatfs_port_write_bytes_file_atomic(const char *final_path,
                                                              const char *tmp_path,
                                                              const uint8_t *data,
                                                              size_t data_size)
{
    if (final_path == NULL || tmp_path == NULL || data == NULL) {
        return FATFS_PORT_STATUS_WRITE_FAILED;
    }

    const fatfs_port_status_t mount_status = fatfs_port_mount();
    if (mount_status != FATFS_PORT_STATUS_OK) {
        return mount_status;
    }

    char fat_final_path[96];
    char fat_tmp_path[96];
    fatfs_port_make_path(final_path, fat_final_path, sizeof(fat_final_path));
    fatfs_port_make_path(tmp_path, fat_tmp_path, sizeof(fat_tmp_path));

    (void)f_unlink(fat_tmp_path);

    fatfs_port_status_t status =
        fatfs_port_write_bytes_to_fat_path(fat_tmp_path, data, data_size);
    if (status != FATFS_PORT_STATUS_OK) {
        (void)f_unlink(fat_tmp_path);
        return status;
    }

    status = fatfs_port_replace_tmp_file(fat_tmp_path, fat_final_path);
    if (status == FATFS_PORT_STATUS_OK) {
        return FATFS_PORT_STATUS_OK;
    }

    status = fatfs_port_write_bytes_to_fat_path(fat_final_path, data, data_size);
    if (status != FATFS_PORT_STATUS_OK) {
        (void)f_unlink(fat_tmp_path);
        return status;
    }

    (void)f_unlink(fat_tmp_path);
    return FATFS_PORT_STATUS_OK;
}

fatfs_port_status_t fatfs_port_write_text_file_atomic(const char *final_path,
                                                      const char *tmp_path,
                                                      const char *text)
{
    if (text == NULL) {
        return FATFS_PORT_STATUS_WRITE_FAILED;
    }
    return fatfs_port_write_bytes_file_atomic(final_path,
                                             tmp_path,
                                             (const uint8_t *)text,
                                             strlen(text));
}

fatfs_port_status_t fatfs_port_write_binary_file_atomic(const char *final_path,
                                                        const char *tmp_path,
                                                        const uint8_t *data,
                                                        size_t data_size)
{
    if (final_path == NULL || tmp_path == NULL || data == NULL || data_size == 0u) {
        return FATFS_PORT_STATUS_WRITE_FAILED;
    }

    return fatfs_port_write_bytes_file_atomic(final_path, tmp_path, data, data_size);
}

fatfs_port_status_t fatfs_port_delete(const char *path)
{
    if (path == NULL) {
        return FATFS_PORT_STATUS_PATH_NOT_FOUND;
    }

    const fatfs_port_status_t mount_status = fatfs_port_mount();
    if (mount_status != FATFS_PORT_STATUS_OK) {
        return mount_status;
    }

    char fat_path[96];
    fatfs_port_make_path(path, fat_path, sizeof(fat_path));
    const FRESULT result = f_unlink(fat_path);
    if (result == FR_OK) {
        return FATFS_PORT_STATUS_OK;
    }
    if (result == FR_NO_FILE || result == FR_NO_PATH) {
        return FATFS_PORT_STATUS_PATH_NOT_FOUND;
    }
    return FATFS_PORT_STATUS_WRITE_FAILED;
}

fatfs_port_status_t fatfs_port_rename(const char *old_path, const char *new_path)
{
    if (old_path == NULL || new_path == NULL) {
        return FATFS_PORT_STATUS_PATH_NOT_FOUND;
    }

    const fatfs_port_status_t mount_status = fatfs_port_mount();
    if (mount_status != FATFS_PORT_STATUS_OK) {
        return mount_status;
    }

    char fat_old_path[96];
    char fat_new_path[96];
    fatfs_port_make_path(old_path, fat_old_path, sizeof(fat_old_path));
    fatfs_port_make_path(new_path, fat_new_path, sizeof(fat_new_path));
    const FRESULT result = f_rename(fat_old_path, fat_new_path);
    if (result == FR_OK) {
        return FATFS_PORT_STATUS_OK;
    }
    if (result == FR_NO_FILE || result == FR_NO_PATH) {
        return FATFS_PORT_STATUS_PATH_NOT_FOUND;
    }
    return FATFS_PORT_STATUS_RENAME_FAILED;
}

static bool fatfs_port_parse_sequence_name(const char *name,
                                           const char *prefix,
                                           const char *suffix,
                                           uint32_t *sequence)
{
    if (name == NULL || prefix == NULL || suffix == NULL || sequence == NULL) {
        return false;
    }

    const size_t prefix_len = strlen(prefix);
    const size_t suffix_len = strlen(suffix);
    const size_t name_len = strlen(name);
    if (name_len <= prefix_len + suffix_len ||
        strncmp(name, prefix, prefix_len) != 0 ||
        strcmp(name + name_len - suffix_len, suffix) != 0) {
        return false;
    }

    uint32_t parsed = 0u;
    const char *digits = name + prefix_len;
    const size_t digit_count = name_len - prefix_len - suffix_len;
    for (size_t i = 0u; i < digit_count; i++) {
        const char c = digits[i];
        if (c < '0' || c > '9') {
            return false;
        }
        parsed = (parsed * 10u) + (uint32_t)(c - '0');
    }
    *sequence = parsed;
    return true;
}

fatfs_port_status_t fatfs_port_find_max_sequence(const char *directory,
                                                 const char *prefix,
                                                 const char *suffix,
                                                 uint32_t *max_sequence)
{
    if (directory == NULL || prefix == NULL || suffix == NULL || max_sequence == NULL) {
        return FATFS_PORT_STATUS_PATH_NOT_FOUND;
    }

    *max_sequence = 0u;
    const fatfs_port_status_t mount_status = fatfs_port_mount();
    if (mount_status != FATFS_PORT_STATUS_OK) {
        return mount_status;
    }

    char fat_path[96];
    fatfs_port_make_path(directory, fat_path, sizeof(fat_path));

    DIR dir;
    FRESULT result = f_opendir(&dir, fat_path);
    if (result != FR_OK) {
        return FATFS_PORT_STATUS_PATH_NOT_FOUND;
    }

    FILINFO file_info;
    while (f_readdir(&dir, &file_info) == FR_OK && file_info.fname[0] != '\0') {
        if ((file_info.fattrib & AM_DIR) != 0u) {
            continue;
        }
        uint32_t sequence = 0u;
        if (fatfs_port_parse_sequence_name(file_info.fname, prefix, suffix, &sequence) &&
            sequence > *max_sequence) {
            *max_sequence = sequence;
        }
    }
    (void)f_closedir(&dir);
    return FATFS_PORT_STATUS_OK;
}

const char *fatfs_port_status_string(fatfs_port_status_t status)
{
    switch (status) {
    case FATFS_PORT_STATUS_OK: return "OK";
    case FATFS_PORT_STATUS_NOT_AVAILABLE: return "NOT_AVAILABLE";
    case FATFS_PORT_STATUS_MOUNT_FAILED: return "MOUNT_FAILED";
    case FATFS_PORT_STATUS_PATH_NOT_FOUND: return "PATH_NOT_FOUND";
    case FATFS_PORT_STATUS_READ_FAILED: return "READ_FAILED";
    case FATFS_PORT_STATUS_WRITE_FAILED: return "WRITE_FAILED";
    case FATFS_PORT_STATUS_RENAME_FAILED: return "RENAME_FAILED";
    case FATFS_PORT_STATUS_FORMAT_FAILED: return "FORMAT_FAILED";
    default: return "UNKNOWN";
    }
}
