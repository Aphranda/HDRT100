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
void ota_ao_get_vector(ota_vector_t *vector);
bool ota_ao_get_metadata(ota_metadata_t *metadata);
/* True while an OTA session is active (check/erase/receive/verify/pending).
 * The realtime core uses this to skip TDMA service during flash maintenance,
 * keeping the lockout poll tight. */
bool ota_ao_is_active(void);

#endif
