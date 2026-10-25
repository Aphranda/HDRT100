#ifndef DISTRIBUTED_REFMEM_PRIORITY_HEALTH_H
#define DISTRIBUTED_REFMEM_PRIORITY_HEALTH_H

#include <stdbool.h>
#include <stdint.h>

#include "distributed_refmem.h"

bool distributed_refmem_get_vdc_priority_snapshot(refmem_vdc_priority_payload_t *out);

#endif /* DISTRIBUTED_REFMEM_PRIORITY_HEALTH_H */
