#ifndef FLASH_TRANSACTION_JOURNAL_H
#define FLASH_TRANSACTION_JOURNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "flash_transaction.h"

#define FLASH_TRANSACTION_JOURNAL_MAGIC 0x4A4E4C46u
#define FLASH_TRANSACTION_JOURNAL_SCHEMA_VERSION 1u
#define FLASH_TRANSACTION_JOURNAL_COMMIT_MARKER 0xC04D4954u

typedef bool (*flash_transaction_journal_read_fn)(void *context,
                                                   uint32_t offset,
                                                   void *data,
                                                   uint32_t length);
typedef bool (*flash_transaction_journal_program_fn)(void *context,
                                                      uint32_t offset,
                                                      const void *data,
                                                      uint32_t length);
typedef uint32_t (*flash_transaction_journal_crc32_fn)(const uint8_t *data,
                                                       uint32_t length);

typedef struct {
    void *context;
    flash_transaction_journal_read_fn read;
    flash_transaction_journal_program_fn program;
    flash_transaction_journal_crc32_fn crc32;
    uint32_t base_offset;
    uint32_t slot_count;
    uint32_t slot_size;
} flash_transaction_journal_config_t;

typedef struct {
    uint32_t magic;
    uint32_t schema_version;
    uint32_t sequence;
    uint32_t record_length;
    flash_transaction_journal_record_t record;
    uint32_t record_crc32;
    uint32_t commit_marker;
    uint32_t reserved;
} flash_transaction_journal_disk_record_t;

typedef struct {
    flash_transaction_journal_config_t config;
    uint32_t next_sequence;
    uint32_t retained_refs;
    bool initialized;
} flash_transaction_journal_store_t;

bool flash_transaction_journal_init(
    flash_transaction_journal_store_t *store,
    const flash_transaction_journal_config_t *config);
bool flash_transaction_journal_append(
    flash_transaction_journal_store_t *store,
    const flash_transaction_journal_record_t *record);
bool flash_transaction_journal_recover_latest(
    const flash_transaction_journal_store_t *store,
    flash_transaction_journal_record_t *record,
    uint32_t *sequence);

bool flash_transaction_journal_completion_retain(void *context);
void flash_transaction_journal_completion_release(void *context);
bool flash_transaction_journal_completion_append(
    void *context, const flash_transaction_journal_record_t *record);
bool flash_transaction_journal_make_completion_lease(
    flash_transaction_journal_store_t *store,
    flash_transaction_completion_lease_t *lease);

#endif
