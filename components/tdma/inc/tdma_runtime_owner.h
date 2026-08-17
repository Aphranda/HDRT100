#ifndef TDMA_RUNTIME_OWNER_H
#define TDMA_RUNTIME_OWNER_H

#include <stdbool.h>

#include "tdma_service.h"

/* Product firmware has one TDMA owner. Domain wrappers register payloads and
 * adapter operations against it; they do not create parallel runtimes. */
bool tdma_runtime_owner_init(void);
tdma_service_service_t *tdma_runtime_owner_get(void);
tdma_traffic_scheduler_t *tdma_runtime_owner_get_scheduler(void);

#endif
