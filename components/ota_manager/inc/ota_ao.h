#ifndef OTA_AO_H
#define OTA_AO_H

#include <stdbool.h>
#include <stdint.h>

#include "ota_event.h"
#include "ota_vector.h"

bool ota_ao_init(void);
bool ota_ao_post_event(const ota_event_t *event);
void ota_ao_service(uint32_t budget_us);
void ota_ao_get_vector(ota_vector_t *vector);

#endif
