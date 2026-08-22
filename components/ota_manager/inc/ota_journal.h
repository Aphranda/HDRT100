#ifndef OTA_JOURNAL_H
#define OTA_JOURNAL_H

#include <stdbool.h>
#include <stdint.h>

#include "pota_stream_checkpoint.h"
#include "pota_stream_session.h"

typedef struct {
    void *context;
    bool (*read)(void *context, uint32_t offset, void *data, uint32_t length);
    bool (*program_page)(void *context, uint32_t offset,
                         const uint8_t *data, uint32_t length);
} ota_journal_platform_t;

typedef struct {
    bool valid;
    pota_stream_checkpoint_result_t result;
    uint32_t sequence;
    pota_stream_checkpoint_t checkpoint;
} ota_journal_snapshot_t;

bool ota_journal_init(void);
bool ota_journal_init_with_platform(const ota_journal_platform_t *platform);
bool ota_journal_attach(pota_stream_session_t *session);
pota_stream_checkpoint_result_t ota_journal_append(
    const pota_stream_checkpoint_t *checkpoint);
pota_stream_checkpoint_result_t ota_journal_recover_latest(
    pota_stream_checkpoint_t *checkpoint, uint32_t *sequence);
bool ota_journal_get_snapshot(ota_journal_snapshot_t *snapshot);

#endif
