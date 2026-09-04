#ifndef EVENT_BUS_H
#define EVENT_BUS_H

#include <stdbool.h>

#include "ota_event.h"

#define EVENT_BUS_OTA_QUEUE_LENGTH 8u
#define EVENT_BUS_OTA_FAST_BLOCK_DEPTH 4u

bool event_bus_init(void);
bool event_bus_post_ota_event(const ota_event_t *event);
bool event_bus_try_recv_ota_event(ota_event_t *event);
void event_bus_complete_ota_event(const ota_event_t *event);

#endif
