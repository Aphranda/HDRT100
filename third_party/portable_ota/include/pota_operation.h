#ifndef POTA_OPERATION_H
#define POTA_OPERATION_H

#include "pota_core.h"

typedef enum {
    POTA_OPERATION_BEGIN = 0,
    POTA_OPERATION_SERVICE,
    POTA_OPERATION_WRITE,
    POTA_OPERATION_END,
    POTA_OPERATION_ABORT,
    POTA_OPERATION_COMMIT,
} pota_operation_t;

typedef pota_error_t (*pota_operation_action_t)(pota_context_t *context, const void *argument);

typedef struct {
    pota_operation_t operation;
    uint32_t allowed_state_mask;
    pota_operation_action_t action;
} pota_operation_entry_t;

uint32_t pota_operation_state_bit(pota_state_t state);
bool pota_operation_state_allowed(const pota_context_t *context, const pota_operation_entry_t *entry);
const pota_operation_entry_t *pota_operation_find(pota_operation_t operation);
pota_error_t pota_operation_execute(pota_context_t *context,
                                    pota_operation_t operation,
                                    const void *argument);

#endif
