#ifndef REFMEM_DOMAIN_CONFIG_H
#define REFMEM_DOMAIN_CONFIG_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bounded, statically registered command handlers for the N6 online domain
 * configuration commands.  RefMem carries and dispatches; the application
 * layer registers handlers that call the owning TDMA/VDC/T2 API.  This keeps
 * components/distributed_refmem free of a link dependency on the domain
 * components and preserves the single-writer boundary. */
#define REFMEM_DOMAIN_CONFIG_MAX_HANDLERS 32u
#define REFMEM_DOMAIN_CONFIG_MAX_PARAMS 16u

typedef bool (*refmem_domain_config_handler_t)(
    const uint32_t *params, uint32_t param_words, uint32_t *reason);

/* Registration happens once at application init, before commands can run.
 * Reset is for host tests and re-initialization. */
void refmem_domain_config_reset(void);
bool refmem_domain_config_register(uint32_t command_type,
                                   uint32_t expected_words,
                                   refmem_domain_config_handler_t handler);

/* Look up a handler without invoking it (host/observability). */
bool refmem_domain_config_find(uint32_t command_type,
                               refmem_domain_config_handler_t *handler,
                               uint32_t *expected_words);

/* Dispatch one already-authorized target-local command.  Unknown type,
 * word-count mismatch and handler rejection are fail-closed; the caller
 * turns the returned false into NACK with the supplied reason. */
bool refmem_domain_config_dispatch(uint32_t command_type,
                                   const uint32_t *params,
                                   uint32_t param_words,
                                   uint32_t *reason);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_DOMAIN_CONFIG_H */
