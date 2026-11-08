#ifndef OTA_AO_H
#define OTA_AO_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_event.h"
#include "ota_metadata.h"
#include "ota_vector.h"

bool ota_ao_init(void);
bool ota_ao_post_event(const ota_event_t *event);
void ota_ao_service(uint32_t budget_us);
/* Request release of the OTA admission.  The release is deferred while a
 * FlashTransaction still owns the slot; ota_ao_service() completes it once the
 * terminal owner is drained.  SCPI abort/error paths must use this instead of
 * releasing resource_arbiter directly. */
void ota_ao_request_admission_release(void);
/* Called after the AO consumed an OTA_EVENT_DATA_BLOCK (the block's bytes are
 * no longer referenced).  The TDMA v2 bridge uses it to release its staged
 * block and acknowledge the source pump. */
typedef void (*ota_ao_data_done_fn)(void *context);
void ota_ao_set_data_done_hook(ota_ao_data_done_fn hook, void *context);
void ota_ao_get_vector(ota_vector_t *vector);
bool ota_ao_get_metadata_snapshot(ota_metadata_t *metadata);
void ota_ao_publish_metadata_snapshot(const ota_metadata_t *metadata);
/* True while an OTA session is active (check/erase/receive/verify/pending).
 * The realtime core uses this to skip TDMA service during flash maintenance,
 * keeping the lockout poll tight. */
bool ota_ao_is_active(void);
/* True when an active OTA session expects the TDMA maintenance transport to
 * keep running (static maintenance mode).  Legacy raw-data sessions keep the
 * historical behaviour: the ring stays idle while flash is written. */
bool ota_ao_tdma_maintenance_allowed(void);
/* Architecture v2: the TDMA bridge feeds the OTA AO directly and therefore
 * needs the maintenance transport to stay running while the AB session is
 * active (the legacy stream path declares the same need by opening a stream
 * session). */
void ota_ao_set_tdma_maintenance_required(bool required);

#endif
