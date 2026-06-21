#ifndef OTA_FB_H
#define OTA_FB_H

#include "ota_event.h"

typedef struct ota_ao_context ota_ao_context_t;

void ota_fb_execute(ota_ao_context_t *context, const ota_event_t *event);

#endif
