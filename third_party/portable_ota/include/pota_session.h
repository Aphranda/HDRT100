#ifndef POTA_SESSION_H
#define POTA_SESSION_H

#include "pota_core.h"

typedef struct {
    pota_context_t core;
} pota_session_t;

bool pota_session_init(pota_session_t *session, const pota_platform_t *platform);
pota_error_t pota_session_begin(pota_session_t *session, const pota_begin_t *begin);
pota_error_t pota_session_service(pota_session_t *session, uint32_t budget_us);
pota_error_t pota_session_write(pota_session_t *session, const uint8_t *data, uint32_t size);
pota_error_t pota_session_end(pota_session_t *session);
pota_error_t pota_session_abort(pota_session_t *session);
pota_error_t pota_session_commit(pota_session_t *session);
void pota_session_get_status(const pota_session_t *session, pota_status_t *status);

#endif
