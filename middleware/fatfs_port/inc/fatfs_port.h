#ifndef FATFS_PORT_H
#define FATFS_PORT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum {
    FATFS_PORT_STATUS_OK = 0,
    FATFS_PORT_STATUS_NOT_AVAILABLE,
    FATFS_PORT_STATUS_MOUNT_FAILED,
    FATFS_PORT_STATUS_PATH_NOT_FOUND,
    FATFS_PORT_STATUS_READ_FAILED,
    FATFS_PORT_STATUS_WRITE_FAILED,
    FATFS_PORT_STATUS_RENAME_FAILED,
    FATFS_PORT_STATUS_FORMAT_FAILED,
} fatfs_port_status_t;

typedef struct {
    uint32_t size;
    bool is_dir;
} fatfs_port_file_info_t;

typedef struct {
    uint32_t returned_count;
    uint32_t next_offset;
    bool complete;
    bool truncated;
} fatfs_port_catalog_page_t;

bool fatfs_port_is_available(void);
fatfs_port_status_t fatfs_port_mount(void);
fatfs_port_status_t fatfs_port_unmount(void);
fatfs_port_status_t fatfs_port_catalog(const char *path, char *buffer, size_t buffer_size);
fatfs_port_status_t fatfs_port_catalog_page(const char *path,
                                            uint32_t offset,
                                            uint32_t limit,
                                            char *buffer,
                                            size_t buffer_size,
                                            fatfs_port_catalog_page_t *page);
fatfs_port_status_t fatfs_port_file_info(const char *path, fatfs_port_file_info_t *info);
fatfs_port_status_t fatfs_port_read_text_file(const char *path,
                                              char *buffer,
                                              size_t buffer_size,
                                              size_t *bytes_read);
fatfs_port_status_t fatfs_port_read_binary_range(const char *path,
                                                 uint32_t offset,
                                                 uint8_t *buffer,
                                                 size_t buffer_size,
                                                 size_t *bytes_read,
                                                 uint32_t *file_size);
fatfs_port_status_t fatfs_port_make_directory(const char *path);
fatfs_port_status_t fatfs_port_format_volume(void);
uint32_t fatfs_port_last_mkfs_result(void);
uint32_t fatfs_port_last_mount_result(void);
fatfs_port_status_t fatfs_port_write_text_file_atomic(const char *final_path,
                                                      const char *tmp_path,
                                                      const char *text);
fatfs_port_status_t fatfs_port_write_binary_file_atomic(const char *final_path,
                                                        const char *tmp_path,
                                                        const uint8_t *data,
                                                        size_t data_size);
fatfs_port_status_t fatfs_port_delete(const char *path);
fatfs_port_status_t fatfs_port_rename(const char *old_path, const char *new_path);
fatfs_port_status_t fatfs_port_find_max_sequence(const char *directory,
                                                 const char *prefix,
                                                 const char *suffix,
                                                 uint32_t *max_sequence);
const char *fatfs_port_status_string(fatfs_port_status_t status);

#endif
