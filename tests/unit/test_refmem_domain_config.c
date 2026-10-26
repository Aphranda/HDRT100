#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include "refmem_domain_config.h"

static uint32_t s_last_reason;
static uint32_t s_seen_params;
static uint32_t s_seen_words;

static bool accept_handler(const uint32_t *params, uint32_t param_words,
                           uint32_t *reason)
{
    s_seen_params = params != NULL ? params[0] : 0u;
    s_seen_words = param_words;
    (void)reason;
    return true;
}

static bool reject_handler(const uint32_t *params, uint32_t param_words,
                           uint32_t *reason)
{
    (void)params;
    (void)param_words;
    *reason = 42u;
    return false;
}

int main(void)
{
    refmem_domain_config_reset();

    refmem_domain_config_handler_t handler = NULL;
    uint32_t words = 0u;
    assert(!refmem_domain_config_find(24u, &handler, &words));
    assert(!refmem_domain_config_dispatch(24u, (const uint32_t[]){0}, 1u,
                                          &s_last_reason));

    assert(refmem_domain_config_register(24u, 2u, accept_handler));
    assert(refmem_domain_config_find(24u, &handler, &words));
    assert(handler == accept_handler);
    assert(words == 2u);
    assert(!refmem_domain_config_register(24u, 2u, accept_handler));

    const uint32_t params[2] = {7u, 9u};
    s_last_reason = 0u;
    assert(refmem_domain_config_dispatch(24u, params, 2u, &s_last_reason));
    assert(s_seen_params == 7u);
    assert(s_seen_words == 2u);

    assert(!refmem_domain_config_dispatch(24u, params, 1u, &s_last_reason));
    assert(!refmem_domain_config_dispatch(24u, NULL, 2u, &s_last_reason));

    assert(refmem_domain_config_register(25u, 1u, reject_handler));
    s_last_reason = 0u;
    assert(!refmem_domain_config_dispatch(25u, (const uint32_t[]){1u}, 1u,
                                          &s_last_reason));
    assert(s_last_reason == 42u);

    assert(!refmem_domain_config_register(0u, 0u, accept_handler));
    assert(!refmem_domain_config_register(26u, 17u, accept_handler));
    assert(!refmem_domain_config_register(26u, 0u, NULL));

    refmem_domain_config_reset();
    assert(!refmem_domain_config_find(24u, &handler, &words));

    /* Bounded table: 16 registrations fit, the 17th is rejected. */
    for (uint32_t i = 0u; i < REFMEM_DOMAIN_CONFIG_MAX_HANDLERS; ++i) {
        assert(refmem_domain_config_register(100u + i, 0u, accept_handler));
    }
    assert(!refmem_domain_config_register(200u, 0u, accept_handler));

    refmem_domain_config_reset();
    printf("refmem_domain_config: registry/dispatch OK\n");
    return 0;
}
