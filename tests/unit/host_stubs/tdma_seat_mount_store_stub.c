/* Host stub for the flash-backed seat mount store.
 *
 * The TDMA service, the trigger services and the VDC run-output path call the
 * store's diagnostic entry points unconditionally (TDMA-SEAT-01/`8bd547b2`), so
 * every host harness that compiles those callers needs these symbols. The store
 * itself is flash-backed and covered by its own suite; this stub only records
 * what a caller can observe through the public status API.
 *
 * Link it into harnesses that compile seat-instrumented sources; do not add it
 * where the real store is compiled.
 */
#include <stdint.h>

#include "tdma_seat_mount_store.h"

static uint32_t s_stub_last_reject;
static uint32_t s_stub_record_generation;

void tdma_seat_mount_store_note_reject(uint32_t code)
{
    s_stub_last_reject = code;
}

void tdma_seat_mount_store_status(uint32_t *last_reject,
                                  uint32_t *record_generation)
{
    if (last_reject != NULL) *last_reject = s_stub_last_reject;
    if (record_generation != NULL) *record_generation = s_stub_record_generation;
}
