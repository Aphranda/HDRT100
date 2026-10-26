#include "refmem_domain_config.h"

#include <stddef.h>

/* Single-writer table: application init registers once before SCPI/commands
 * are reachable.  Dispatch only reads, from task_refmem_sync (Core0). */
typedef struct {
    uint32_t command_type;
    uint32_t expected_words;
    refmem_domain_config_handler_t handler;
} refmem_domain_config_entry_t;

static refmem_domain_config_entry_t s_handlers[REFMEM_DOMAIN_CONFIG_MAX_HANDLERS];
static uint32_t s_handler_count;

void refmem_domain_config_reset(void)
{
    for (uint32_t i = 0u; i < REFMEM_DOMAIN_CONFIG_MAX_HANDLERS; ++i) {
        s_handlers[i].command_type = 0u;
        s_handlers[i].expected_words = 0u;
        s_handlers[i].handler = NULL;
    }
    s_handler_count = 0u;
}

bool refmem_domain_config_register(uint32_t command_type,
                                   uint32_t expected_words,
                                   refmem_domain_config_handler_t handler)
{
    if (command_type == 0u || handler == NULL ||
        expected_words > REFMEM_DOMAIN_CONFIG_MAX_PARAMS) {
        return false;
    }
    for (uint32_t i = 0u; i < s_handler_count; ++i) {
        if (s_handlers[i].command_type == command_type) {
            return false; /* duplicate registration is a config error */
        }
    }
    if (s_handler_count >= REFMEM_DOMAIN_CONFIG_MAX_HANDLERS) {
        return false;
    }
    s_handlers[s_handler_count].command_type = command_type;
    s_handlers[s_handler_count].expected_words = expected_words;
    s_handlers[s_handler_count].handler = handler;
    ++s_handler_count;
    return true;
}

bool refmem_domain_config_find(uint32_t command_type,
                               refmem_domain_config_handler_t *handler,
                               uint32_t *expected_words)
{
    for (uint32_t i = 0u; i < s_handler_count; ++i) {
        if (s_handlers[i].command_type == command_type) {
            if (handler != NULL) {
                *handler = s_handlers[i].handler;
            }
            if (expected_words != NULL) {
                *expected_words = s_handlers[i].expected_words;
            }
            return true;
        }
    }
    return false;
}

bool refmem_domain_config_dispatch(uint32_t command_type,
                                   const uint32_t *params,
                                   uint32_t param_words,
                                   uint32_t *reason)
{
    refmem_domain_config_handler_t handler = NULL;
    uint32_t expected_words = 0u;
    if (reason == NULL) {
        return false;
    }
    if (params == NULL ||
        !refmem_domain_config_find(command_type, &handler, &expected_words) ||
        handler == NULL || param_words != expected_words) {
        return false;
    }
    return handler(params, param_words, reason);
}
