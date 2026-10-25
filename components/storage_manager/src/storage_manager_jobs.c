#include "storage_manager_internal.h"

bool storage_manager_raw_clear_prefix(uint32_t sector_count,
                                      uint32_t *cleared_count,
                                      sd_card_status_t *raw_status)
{
    if (cleared_count != NULL) {
        *cleared_count = 0u;
    }
    if (raw_status != NULL) {
        *raw_status = SD_CARD_STATUS_BAD_RESPONSE;
    }
    if (sector_count == 0u || sector_count > STORAGE_MANAGER_RAW_CLEAR_MAX_SECTORS) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        return false;
    }

    if (!storage_acquire_sd_resource()) {
        return false;
    }

    const sd_card_config_t config = {
        .spi = BOARD_SD_SPI_PORT,
        .sck_pin = BOARD_SD_SPI_CLK_PIN,
        .mosi_pin = BOARD_SD_SPI_MOSI_PIN,
        .miso_pin = BOARD_SD_SPI_MISO_PIN,
        .cs_pin = BOARD_SD_SPI_CS_PIN,
        .init_baud_hz = STORAGE_MANAGER_SD_INIT_BAUD_HZ,
        .run_baud_hz = STORAGE_MANAGER_SD_RUN_BAUD_HZ,
    };

    sd_card_info_t info;
    sd_card_status_t status = SD_CARD_STATUS_NOT_INITIALIZED;
    if (sd_card_init(&config)) {
        status = sd_card_probe(&info);
    } else {
        memset(&info, 0, sizeof(info));
        status = SD_CARD_STATUS_NOT_INITIALIZED;
    }

    s_storage_vector.card_status = status;
    s_storage_vector.card_present = info.present;
    s_storage_vector.card_type = info.type;
    s_storage_vector.high_capacity = info.high_capacity;
    s_storage_vector.block_count = info.block_count;
    s_storage_vector.capacity_kib = info.capacity_kib;
    s_storage_vector.probe_count++;
    s_storage_vector.last_probe_ms = storage_now_ms();

    if (status == SD_CARD_STATUS_OK && info.present) {
        uint8_t zero_sector[512];
        memset(zero_sector, 0, sizeof(zero_sector));
        for (uint32_t sector = 0u; sector < sector_count; sector++) {
            status = sd_card_write_blocks(sector, 1u, zero_sector);
            if (status != SD_CARD_STATUS_OK) {
                break;
            }
            if (cleared_count != NULL) {
                *cleared_count = sector + 1u;
            }
        }
    }

    storage_release_sd_resource();

    if (raw_status != NULL) {
        *raw_status = status;
    }
    s_storage_vector.fs_mounted = false;
    s_storage_vector.state = status == SD_CARD_STATUS_OK ?
                                 STORAGE_MANAGER_STATE_NO_FILESYSTEM :
                                 STORAGE_MANAGER_STATE_FAILED;
    s_storage_vector.storage_error = status == SD_CARD_STATUS_OK ?
                                         STORAGE_MANAGER_ERROR_NONE :
                                         STORAGE_MANAGER_ERROR_CARD;
    storage_reset_manifest_summary(STORAGE_MANAGER_MANIFEST_UNKNOWN);
    return status == SD_CARD_STATUS_OK;
}

bool storage_manager_raw_read_sector(uint32_t sector,
                                     uint8_t *buffer,
                                     size_t buffer_size,
                                     sd_card_status_t *raw_status)
{
    if (raw_status != NULL) {
        *raw_status = SD_CARD_STATUS_BAD_RESPONSE;
    }
    if (buffer == NULL || buffer_size < 512u) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        return false;
    }

    if (!storage_acquire_sd_resource()) {
        return false;
    }

    const sd_card_config_t config = {
        .spi = BOARD_SD_SPI_PORT,
        .sck_pin = BOARD_SD_SPI_CLK_PIN,
        .mosi_pin = BOARD_SD_SPI_MOSI_PIN,
        .miso_pin = BOARD_SD_SPI_MISO_PIN,
        .cs_pin = BOARD_SD_SPI_CS_PIN,
        .init_baud_hz = STORAGE_MANAGER_SD_INIT_BAUD_HZ,
        .run_baud_hz = STORAGE_MANAGER_SD_RUN_BAUD_HZ,
    };

    sd_card_info_t info;
    sd_card_status_t status = SD_CARD_STATUS_NOT_INITIALIZED;
    if (sd_card_init(&config)) {
        status = sd_card_probe(&info);
    } else {
        memset(&info, 0, sizeof(info));
    }

    s_storage_vector.card_status = status;
    s_storage_vector.card_present = info.present;
    s_storage_vector.card_type = info.type;
    s_storage_vector.high_capacity = info.high_capacity;
    s_storage_vector.block_count = info.block_count;
    s_storage_vector.capacity_kib = info.capacity_kib;
    s_storage_vector.probe_count++;
    s_storage_vector.last_probe_ms = storage_now_ms();

    if (status == SD_CARD_STATUS_OK && info.present) {
        status = sd_card_read_blocks(sector, 1u, buffer);
    }

    storage_release_sd_resource();

    if (raw_status != NULL) {
        *raw_status = status;
    }
    s_storage_vector.storage_error = status == SD_CARD_STATUS_OK ?
                                         STORAGE_MANAGER_ERROR_NONE :
                                         STORAGE_MANAGER_ERROR_CARD;
    return status == SD_CARD_STATUS_OK;
}

bool storage_manager_format_volume(fatfs_port_status_t *format_status)
{
    if (format_status != NULL) {
        *format_status = FATFS_PORT_STATUS_FORMAT_FAILED;
    }

    if (!storage_acquire_sd_resource()) {
        return false;
    }

    const sd_card_config_t config = {
        .spi = BOARD_SD_SPI_PORT,
        .sck_pin = BOARD_SD_SPI_CLK_PIN,
        .mosi_pin = BOARD_SD_SPI_MOSI_PIN,
        .miso_pin = BOARD_SD_SPI_MISO_PIN,
        .cs_pin = BOARD_SD_SPI_CS_PIN,
        .init_baud_hz = STORAGE_MANAGER_SD_INIT_BAUD_HZ,
        .run_baud_hz = STORAGE_MANAGER_SD_RUN_BAUD_HZ,
    };

    sd_card_info_t info;
    sd_card_status_t card_status = SD_CARD_STATUS_NOT_INITIALIZED;
    if (sd_card_init(&config)) {
        card_status = sd_card_probe(&info);
    } else {
        memset(&info, 0, sizeof(info));
    }

    s_storage_vector.card_status = card_status;
    s_storage_vector.card_present = info.present;
    s_storage_vector.card_type = info.type;
    s_storage_vector.high_capacity = info.high_capacity;
    s_storage_vector.block_count = info.block_count;
    s_storage_vector.capacity_kib = info.capacity_kib;
    s_storage_vector.fatfs_available = fatfs_port_is_available();
    s_storage_vector.probe_count++;
    s_storage_vector.last_probe_ms = storage_now_ms();

    fatfs_port_status_t status = FATFS_PORT_STATUS_FORMAT_FAILED;
    if (card_status == SD_CARD_STATUS_OK && info.present && s_storage_vector.fatfs_available) {
        status = fatfs_port_format_volume();
    }

    storage_release_sd_resource();

    if (format_status != NULL) {
        *format_status = status;
    }

    s_storage_vector.fs_mounted = status == FATFS_PORT_STATUS_OK;
    if (card_status != SD_CARD_STATUS_OK || !info.present) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_FAILED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_CARD;
    } else if (status == FATFS_PORT_STATUS_OK) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_CARD_READY;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_NONE;
    } else {
        s_storage_vector.state = STORAGE_MANAGER_STATE_NO_FILESYSTEM;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
    }
    storage_reset_manifest_summary(STORAGE_MANAGER_MANIFEST_UNKNOWN);
    return status == FATFS_PORT_STATUS_OK;
}

static bool storage_post_object_path_job(storage_manager_object_t object,
                                         storage_manager_job_type_t type,
                                         uint32_t offset,
                                         uint32_t length,
                                         uint32_t *job_id)
{
    if (storage_job_is_active() ||
        (type == STORAGE_MANAGER_JOB_TYPE_FILE_READ &&
         storage_write_buffer_is_active())) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    const storage_manager_write_contract_t *contract = storage_find_object_contract(object);
    if (contract == NULL) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = type;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = false;
    s_storage_job.offset = offset;
    s_storage_job.length = length > STORAGE_MANAGER_FILE_READ_MAX_BYTES ?
                               STORAGE_MANAGER_FILE_READ_MAX_BYTES :
                               length;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       contract->final_path);
    s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_post_file_delete_job(const char *path, uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path)) ||
        strcmp(normalized_path, "/") == 0) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_FILE_DELETE;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = false;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       normalized_path);
    s_storage_job.result.path_hash = storage_hash_path(normalized_path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

static bool storage_manager_post_rename_job(const char *old_path,
                                            const char *new_path,
                                            storage_manager_job_type_t type,
                                            bool is_dir,
                                            uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    char normalized_old_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    char normalized_new_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(old_path, normalized_old_path, sizeof(normalized_old_path)) ||
        !storage_normalize_path(new_path, normalized_new_path, sizeof(normalized_new_path)) ||
        strcmp(normalized_old_path, "/") == 0 ||
        strcmp(normalized_new_path, "/") == 0) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = type;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = is_dir;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       normalized_old_path);
    storage_copy_field(s_storage_job.target_path,
                       sizeof(s_storage_job.target_path),
                       normalized_new_path);
    s_storage_job.result.path_hash = storage_hash_path(normalized_old_path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_post_file_rename_job(const char *old_path,
                                          const char *new_path,
                                          uint32_t *job_id)
{
    return storage_manager_post_rename_job(old_path,
                                           new_path,
                                           STORAGE_MANAGER_JOB_TYPE_FILE_RENAME,
                                           false,
                                           job_id);
}

static bool storage_manager_post_directory_job(const char *path,
                                               storage_manager_job_type_t type,
                                               uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path)) ||
        strcmp(normalized_path, "/") == 0) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = type;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = true;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       normalized_path);
    s_storage_job.result.path_hash = storage_hash_path(normalized_path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_post_directory_create_job(const char *path, uint32_t *job_id)
{
    return storage_manager_post_directory_job(path,
                                              STORAGE_MANAGER_JOB_TYPE_DIRECTORY_CREATE,
                                              job_id);
}

bool storage_manager_post_directory_delete_job(const char *path, uint32_t *job_id)
{
    return storage_manager_post_directory_job(path,
                                              STORAGE_MANAGER_JOB_TYPE_DIRECTORY_DELETE,
                                              job_id);
}

bool storage_manager_post_directory_rename_job(const char *old_path,
                                               const char *new_path,
                                               uint32_t *job_id)
{
    return storage_manager_post_rename_job(old_path,
                                           new_path,
                                           STORAGE_MANAGER_JOB_TYPE_DIRECTORY_RENAME,
                                           true,
                                           job_id);
}

bool storage_manager_post_object_info_job(storage_manager_object_t object, uint32_t *job_id)
{
    return storage_post_object_path_job(object,
                                        STORAGE_MANAGER_JOB_TYPE_FILE_INFO,
                                        0u,
                                        0u,
                                        job_id);
}

bool storage_manager_post_object_read_job(storage_manager_object_t object,
                                          uint32_t offset,
                                          uint32_t length,
                                          uint32_t *job_id)
{
    if (length == 0u) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        return false;
    }
    return storage_post_object_path_job(object,
                                        STORAGE_MANAGER_JOB_TYPE_FILE_READ,
                                        offset,
                                        length,
                                        job_id);
}

bool storage_manager_post_object_delete_job(storage_manager_object_t object, uint32_t *job_id)
{
    return storage_post_object_path_job(object,
                                        STORAGE_MANAGER_JOB_TYPE_FILE_DELETE,
                                        0u,
                                        0u,
                                        job_id);
}

bool storage_manager_begin_file_write(const char *path,
                                      uint32_t expected_size,
                                      uint32_t expected_crc32,
                                      uint32_t *txn_id)
{
    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (storage_job_is_active() ||
        !storage_normalize_path(path, normalized_path, sizeof(normalized_path)) ||
        strcmp(normalized_path, "/") == 0 ||
        expected_size == 0u ||
        expected_size > STORAGE_MANAGER_WRITE_BUFFER_MAX_BYTES) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_PATH;
        return false;
    }
    if (s_write_snapshot.state == STORAGE_MANAGER_WRITE_STATE_RECEIVING ||
        s_write_snapshot.state == STORAGE_MANAGER_WRITE_STATE_READY ||
        s_write_snapshot.state == STORAGE_MANAGER_WRITE_STATE_QUEUED ||
        s_write_snapshot.state == STORAGE_MANAGER_WRITE_STATE_WRITING) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    char tmp_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_make_write_tmp_path(normalized_path, tmp_path, sizeof(tmp_path))) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_PATH;
        return false;
    }

    memset(&s_write_snapshot, 0, sizeof(s_write_snapshot));
    memset(s_write_buffer, 0, sizeof(s_write_buffer));
    s_write_snapshot.txn_id = s_next_write_txn_id++;
    if (s_next_write_txn_id == 0u) {
        s_next_write_txn_id = 1u;
    }
    s_write_snapshot.object = STORAGE_MANAGER_OBJECT_NONE;
    s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_RECEIVING;
    s_write_snapshot.expected_size = expected_size;
    s_write_snapshot.expected_crc32 = expected_crc32;
    s_write_snapshot.path_hash = storage_hash_path(normalized_path);
    storage_copy_field(s_write_snapshot.path,
                       sizeof(s_write_snapshot.path),
                       normalized_path);
    storage_copy_field(s_write_snapshot.tmp_path,
                       sizeof(s_write_snapshot.tmp_path),
                       tmp_path);
    if (txn_id != NULL) {
        *txn_id = s_write_snapshot.txn_id;
    }
    return true;
}

bool storage_manager_begin_evidence_write(const char *path,
                                          uint32_t expected_size,
                                          uint32_t expected_crc32,
                                          uint32_t *txn_id)
{
    if (!storage_manager_begin_file_write(
            path, expected_size, expected_crc32, txn_id)) {
        return false;
    }
    s_write_snapshot.direct_write = true;
    return true;
}

bool storage_manager_write_file_chunk(uint32_t txn_id,
                                      uint32_t offset,
                                      const uint8_t *data,
                                      size_t data_size)
{
    if (data == NULL ||
        data_size == 0u ||
        s_write_snapshot.txn_id != txn_id ||
        s_write_snapshot.state != STORAGE_MANAGER_WRITE_STATE_RECEIVING ||
        offset != s_write_snapshot.received_size ||
        offset + data_size > s_write_snapshot.expected_size ||
        offset + data_size > sizeof(s_write_buffer)) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_SEQUENCE;
        return false;
    }

    memcpy(s_write_buffer + offset, data, data_size);
    s_write_snapshot.received_size = offset + (uint32_t)data_size;
    if (s_write_snapshot.received_size == s_write_snapshot.expected_size) {
        s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_READY;
    }
    return true;
}

bool storage_manager_commit_file_write(uint32_t txn_id, uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }
    if (s_write_snapshot.txn_id != txn_id ||
        s_write_snapshot.state != STORAGE_MANAGER_WRITE_STATE_READY ||
        s_write_snapshot.received_size != s_write_snapshot.expected_size) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_SEQUENCE;
        return false;
    }

    s_write_snapshot.actual_crc32 =
        ota_crc32_compute(s_write_buffer, s_write_snapshot.received_size);
    if (s_write_snapshot.actual_crc32 != s_write_snapshot.expected_crc32) {
        s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_FAILED;
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_WRITE_FAILED;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_FILE_WRITE;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.size = s_write_snapshot.expected_size;
    s_storage_job.result.is_dir = false;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       s_write_snapshot.path);
    s_storage_job.result.path_hash = s_write_snapshot.path_hash;
    /* Publish the write transaction before making its job runnable.  The
     * storage service may execute on another task/core as soon as it observes
     * QUEUED; exposing the job first lets it see the preceding READY state and
     * reject an otherwise valid transaction as a sequence error. */
    s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_QUEUED;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_finish_evidence_write(uint32_t txn_id,
                                            uint32_t expected_crc32,
                                            uint32_t *job_id)
{
    if (s_write_snapshot.txn_id != txn_id || !s_write_snapshot.direct_write ||
        s_write_snapshot.received_size == 0u ||
        (s_write_snapshot.state != STORAGE_MANAGER_WRITE_STATE_RECEIVING &&
         s_write_snapshot.state != STORAGE_MANAGER_WRITE_STATE_READY)) {
        return false;
    }
    s_write_snapshot.expected_size = s_write_snapshot.received_size;
    s_write_snapshot.expected_crc32 = expected_crc32;
    s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_READY;
    return storage_manager_commit_file_write(txn_id, job_id);
}

bool storage_manager_copy_evidence_write(uint32_t txn_id, uint32_t offset,
                                          uint8_t *data, size_t size)
{
    if (data == NULL || size == 0u || s_write_snapshot.txn_id != txn_id ||
        !s_write_snapshot.direct_write ||
        (s_write_snapshot.state != STORAGE_MANAGER_WRITE_STATE_RECEIVING &&
         s_write_snapshot.state != STORAGE_MANAGER_WRITE_STATE_READY) ||
        offset > s_write_snapshot.received_size ||
        size > s_write_snapshot.received_size - offset) {
        return false;
    }
    memcpy(data, s_write_buffer + offset, size);
    return true;
}

bool storage_manager_abort_file_write(uint32_t txn_id)
{
    if (storage_job_is_active()) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }
    if (s_write_snapshot.state != STORAGE_MANAGER_WRITE_STATE_IDLE &&
        s_write_snapshot.txn_id != txn_id) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_SEQUENCE;
        return false;
    }
    memset(s_write_buffer, 0, sizeof(s_write_buffer));
    s_write_snapshot.state = STORAGE_MANAGER_WRITE_STATE_ABORTED;
    s_write_snapshot.error = STORAGE_MANAGER_ERROR_NONE;
    return true;
}

bool storage_manager_begin_object_write(storage_manager_object_t object,
                                        uint32_t expected_size,
                                        uint32_t expected_crc32,
                                        uint32_t *txn_id)
{
    const storage_manager_write_contract_t *contract = storage_find_object_contract(object);
    if (contract == NULL) {
        s_write_snapshot.error = STORAGE_MANAGER_ERROR_PATH;
        return false;
    }
    const bool ok = storage_manager_begin_file_write(contract->final_path,
                                                    expected_size,
                                                    expected_crc32,
                                                    txn_id);
    if (ok) {
        s_write_snapshot.object = object;
        storage_copy_field(s_write_snapshot.tmp_path,
                           sizeof(s_write_snapshot.tmp_path),
                           contract->tmp_path);
    }
    return ok;
}

bool storage_manager_write_object_chunk(uint32_t txn_id,
                                        uint32_t offset,
                                        const uint8_t *data,
                                        size_t data_size)
{
    return storage_manager_write_file_chunk(txn_id, offset, data, data_size);
}

bool storage_manager_commit_object_write(uint32_t txn_id, uint32_t *job_id)
{
    return storage_manager_commit_file_write(txn_id, job_id);
}

bool storage_manager_abort_object_write(uint32_t txn_id)
{
    return storage_manager_abort_file_write(txn_id);
}

void storage_manager_get_write_snapshot(storage_manager_write_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }
    *snapshot = s_write_snapshot;
}

bool storage_manager_post_file_info_job(const char *path, uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_FILE_INFO;

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path))) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
        s_storage_job.result.error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           path != NULL ? path : "");
        s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        storage_publish_job_result();
        if (job_id != NULL) {
            *job_id = s_storage_job.result.id;
        }
        return true;
    }

    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       normalized_path);
    s_storage_job.result.path_hash = storage_hash_path(normalized_path);
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_post_file_read_job(const char *path,
                                        uint32_t offset,
                                        uint32_t length,
                                        uint32_t *job_id)
{
    if (storage_job_is_active() || storage_write_buffer_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_FILE_READ;
    s_storage_job.offset = offset;
    s_storage_job.length = length > STORAGE_MANAGER_FILE_READ_MAX_BYTES ?
                               STORAGE_MANAGER_FILE_READ_MAX_BYTES :
                               length;

    if (s_storage_job.length == 0u) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
        s_storage_job.result.error = STORAGE_MANAGER_ERROR_PATH;
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           path != NULL ? path : "");
        s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        storage_publish_job_result();
        if (job_id != NULL) {
            *job_id = s_storage_job.result.id;
        }
        return true;
    }

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path))) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
        s_storage_job.result.error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           path != NULL ? path : "");
        s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        storage_publish_job_result();
        if (job_id != NULL) {
            *job_id = s_storage_job.result.id;
        }
        return true;
    }

    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       normalized_path);
    s_storage_job.result.path_hash = storage_hash_path(normalized_path);
    /* As with FILE_WRITE, QUEUED is the cross-task publication point.  Keep it
     * last so the service cannot observe a runnable read with an empty path. */
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_get_file_read_job_result(uint32_t job_id,
                                              storage_manager_file_read_t *read_info,
                                              uint8_t *buffer,
                                              size_t buffer_size)
{
    if (!storage_manager_acquire_file_read_job_result(job_id, read_info)) {
        return false;
    }

    bool ok = true;
    if (buffer != NULL && buffer_size > 0u && s_storage_job.read_info.returned > 0u) {
        size_t copied_size = 0u;
        ok = storage_manager_copy_file_read_job_result(
            job_id, 0u, buffer, buffer_size, &copied_size);
    }
    storage_manager_release_file_read_job_result(job_id);
    return ok;
}

bool storage_manager_acquire_file_read_job_result(
    uint32_t job_id,
    storage_manager_file_read_t *read_info)
{
    if (read_info == NULL || s_file_read_result_leased ||
        s_storage_job.result.id != job_id ||
        s_storage_job.result.type != STORAGE_MANAGER_JOB_TYPE_FILE_READ ||
        s_storage_job.result.state != STORAGE_MANAGER_JOB_STATE_DONE) {
        return false;
    }

    s_file_read_result_leased = true;
    *read_info = s_storage_job.read_info;
    return true;
}

bool storage_manager_copy_file_read_job_result(
    uint32_t job_id,
    uint32_t result_offset,
    uint8_t *buffer,
    size_t buffer_size,
    size_t *copied_size)
{
    if (copied_size != NULL) {
        *copied_size = 0u;
    }
    if (!s_file_read_result_leased || buffer == NULL || copied_size == NULL ||
        s_storage_job.result.id != job_id ||
        s_storage_job.result.type != STORAGE_MANAGER_JOB_TYPE_FILE_READ ||
        s_storage_job.result.state != STORAGE_MANAGER_JOB_STATE_DONE ||
        result_offset > s_storage_job.read_info.returned) {
        return false;
    }

    const size_t remaining =
        (size_t)s_storage_job.read_info.returned - (size_t)result_offset;
    const size_t copy_size = remaining < buffer_size ? remaining : buffer_size;
    if (copy_size > 0u) {
        memcpy(buffer, &s_write_buffer[result_offset], copy_size);
    }
    *copied_size = copy_size;
    return true;
}

void storage_manager_release_file_read_job_result(uint32_t job_id)
{
    if (s_file_read_result_leased && s_storage_job.result.id == job_id &&
        s_storage_job.result.type == STORAGE_MANAGER_JOB_TYPE_FILE_READ) {
        s_file_read_result_leased = false;
    }
}

bool storage_manager_post_catalog_page_job(const char *path,
                                           uint32_t offset,
                                           uint32_t limit,
                                           uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_CATALOG_PAGE;
    s_storage_job.offset = offset;
    s_storage_job.limit = limit;

    if (limit == 0u) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
        s_storage_job.result.error = STORAGE_MANAGER_ERROR_PATH;
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           path != NULL ? path : "");
        s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        storage_publish_job_result();
        if (job_id != NULL) {
            *job_id = s_storage_job.result.id;
        }
        return true;
    }

    char normalized_path[STORAGE_MANAGER_MAX_PATH_LEN + 1u];
    if (!storage_normalize_path(path, normalized_path, sizeof(normalized_path))) {
        s_storage_vector.state = STORAGE_MANAGER_STATE_PATH_DENIED;
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
        s_storage_job.result.error = STORAGE_MANAGER_ERROR_PATH_DENIED;
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           path != NULL ? path : "");
        s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        storage_copy_field(s_storage_job.catalog_buffer,
                           sizeof(s_storage_job.catalog_buffer),
                           "PATH_DENIED");
        storage_publish_job_result();
        if (job_id != NULL) {
            *job_id = s_storage_job.result.id;
        }
        return true;
    }

    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = true;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       normalized_path);
    s_storage_job.result.path_hash = storage_hash_path(normalized_path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_get_catalog_page_job_result(uint32_t job_id,
                                                 storage_manager_catalog_page_t *page,
                                                 char *buffer,
                                                 size_t buffer_size)
{
    if (page == NULL ||
        s_storage_job.result.id != job_id ||
        s_storage_job.result.type != STORAGE_MANAGER_JOB_TYPE_CATALOG_PAGE ||
        s_storage_job.result.state != STORAGE_MANAGER_JOB_STATE_DONE) {
        return false;
    }

    *page = s_storage_job.catalog_page;
    storage_copy_field(buffer, buffer_size, s_storage_job.catalog_buffer);
    return true;
}

bool storage_manager_post_snapshot_job(const char *kind, uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    const char *snapshot_kind = (kind == NULL || kind[0] == '\0') ? "boot" : kind;
    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_SNAPSHOT_WRITE;

    if (!storage_snapshot_kind_is_valid(snapshot_kind)) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_PATH;
        s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_FAILED;
        s_storage_job.result.error = STORAGE_MANAGER_ERROR_PATH;
        storage_copy_field(s_storage_job.argument,
                           sizeof(s_storage_job.argument),
                           snapshot_kind);
        storage_copy_field(s_storage_job.result.path,
                           sizeof(s_storage_job.result.path),
                           snapshot_kind);
        s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
        storage_publish_job_result();
        if (job_id != NULL) {
            *job_id = s_storage_job.result.id;
        }
        return true;
    }

    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    storage_copy_field(s_storage_job.argument,
                       sizeof(s_storage_job.argument),
                       snapshot_kind);
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       snapshot_kind);
    s_storage_job.result.path_hash = storage_hash_path(snapshot_kind);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_post_manifest_scan_job(uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_MANIFEST_SCAN;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = false;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       "/manifest.idx");
    s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_post_system_init_job(uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_SYSTEM_INIT;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = false;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       "/manifest.idx");
    s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

bool storage_manager_post_fault_evidence_job(uint32_t *job_id)
{
    if (storage_job_is_active()) {
        s_storage_vector.storage_error = STORAGE_MANAGER_ERROR_RESOURCE_BUSY;
        return false;
    }

    memset(&s_storage_job, 0, sizeof(s_storage_job));
    s_storage_job.result.id = s_next_job_id++;
    if (s_next_job_id == 0u) {
        s_next_job_id = 1u;
    }
    s_storage_job.result.type = STORAGE_MANAGER_JOB_TYPE_FAULT_EVIDENCE;
    s_storage_job.result.state = STORAGE_MANAGER_JOB_STATE_QUEUED;
    s_storage_job.result.error = STORAGE_MANAGER_ERROR_NONE;
    s_storage_job.result.is_dir = false;
    storage_copy_field(s_storage_job.result.path,
                       sizeof(s_storage_job.result.path),
                       "/reports/fault");
    s_storage_job.result.path_hash = storage_hash_path(s_storage_job.result.path);
    storage_publish_job_result();
    if (job_id != NULL) {
        *job_id = s_storage_job.result.id;
    }
    return true;
}

void storage_manager_get_job_result(storage_manager_job_result_t *result)
{
    if (result == NULL) {
        return;
    }
    *result = s_storage_job.result;
}
