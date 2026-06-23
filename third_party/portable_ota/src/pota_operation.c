#include "pota_operation.h"

#define POTA_OPERATION_STATE_BIT(state) (1u << (uint32_t)(state))

#define POTA_OPERATION_STATE_MASK_IDLE \
    (POTA_OPERATION_STATE_BIT(POTA_STATE_IDLE) | \
     POTA_OPERATION_STATE_BIT(POTA_STATE_FAILED) | \
     POTA_OPERATION_STATE_BIT(POTA_STATE_ABORTED) | \
     POTA_OPERATION_STATE_BIT(POTA_STATE_COMMITTED))

#define POTA_OPERATION_STATE_MASK_SERVICE \
    (POTA_OPERATION_STATE_BIT(POTA_STATE_CHECK_PERMISSION) | \
     POTA_OPERATION_STATE_BIT(POTA_STATE_ERASE_SLOT))

static const pota_operation_entry_t s_operation_table[] = {
    {
        .operation = POTA_OPERATION_BEGIN,
        .allowed_state_mask = POTA_OPERATION_STATE_MASK_IDLE,
        .action = pota_core_begin_action,
    },
    {
        .operation = POTA_OPERATION_SERVICE,
        .allowed_state_mask = POTA_OPERATION_STATE_MASK_SERVICE,
        .action = pota_core_service_action,
    },
    {
        .operation = POTA_OPERATION_WRITE,
        .allowed_state_mask = POTA_OPERATION_STATE_BIT(POTA_STATE_RECEIVING),
        .action = pota_core_write_action,
    },
    {
        .operation = POTA_OPERATION_END,
        .allowed_state_mask = POTA_OPERATION_STATE_BIT(POTA_STATE_RECEIVING),
        .action = pota_core_end_action,
    },
    {
        .operation = POTA_OPERATION_ABORT,
        .allowed_state_mask = 0xFFFFFFFFu,
        .action = pota_core_abort_action,
    },
    {
        .operation = POTA_OPERATION_COMMIT,
        .allowed_state_mask = 0xFFFFFFFFu,
        .action = pota_core_commit_action,
    },
};

uint32_t pota_operation_state_bit(pota_state_t state)
{
    const uint32_t index = (uint32_t)state;
    if (index >= 32u) {
        return 0u;
    }
    return 1u << index;
}

bool pota_operation_state_allowed(const pota_context_t *context, const pota_operation_entry_t *entry)
{
    if (context == NULL || entry == NULL) {
        return false;
    }
    return (entry->allowed_state_mask &
            pota_operation_state_bit((pota_state_t)context->status.state)) != 0u;
}

const pota_operation_entry_t *pota_operation_find(pota_operation_t operation)
{
    for (size_t i = 0u; i < (sizeof(s_operation_table) / sizeof(s_operation_table[0])); i++) {
        if (s_operation_table[i].operation == operation) {
            return &s_operation_table[i];
        }
    }
    return NULL;
}

pota_error_t pota_operation_execute(pota_context_t *context,
                                    pota_operation_t operation,
                                    const void *argument)
{
    const pota_operation_entry_t *entry = pota_operation_find(operation);
    if (context == NULL || entry == NULL || entry->action == NULL) {
        return POTA_ERR_BAD_ARGUMENT;
    }

    if (!pota_operation_state_allowed(context, entry)) {
        pota_core_set_failed(context, POTA_ERR_INVALID_STATE);
        return POTA_ERR_INVALID_STATE;
    }

    return entry->action(context, argument);
}
