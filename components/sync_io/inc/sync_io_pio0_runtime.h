#ifndef SYNC_IO_PIO0_RUNTIME_H
#define SYNC_IO_PIO0_RUNTIME_H
#include "sync_io_persona_manager.h"
/* Core0-only stopped preparation/retired release. One global PIO0 lease and
 * compatibility ledger for continuous output and its reference monitor. */
bool sync_io_pio0_runtime_prepare(sync_io_persona_id_t id,
    const sync_io_persona_manager_hooks_t *hooks, void *context,
    sync_io_persona_manager_handle_t *handle);
bool sync_io_pio0_runtime_release(sync_io_persona_manager_handle_t *handle);
bool sync_io_pio0_runtime_start_core1(sync_io_persona_manager_handle_t *handle);
#endif
