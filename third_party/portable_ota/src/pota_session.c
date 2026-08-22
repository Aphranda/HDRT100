#include "pota_session.h"

bool pota_session_init(pota_session_t *session, const pota_platform_t *platform)
{
    if (session == NULL) {
        return false;
    }

    return pota_init(&session->core, platform);
}

pota_error_t pota_session_begin(pota_session_t *session, const pota_begin_t *begin)
{
    if (session == NULL) {
        return POTA_ERR_BAD_ARGUMENT;
    }

    return pota_begin(&session->core, begin);
}

pota_error_t pota_session_resume_raw(pota_session_t *session,
                                     const pota_begin_t *begin,
                                     uint32_t durable_offset,
                                     uint32_t durable_crc32)
{
    if (session == NULL) {
        return POTA_ERR_BAD_ARGUMENT;
    }
    return pota_resume_raw(&session->core, begin, durable_offset,
                           durable_crc32);
}

pota_error_t pota_session_service(pota_session_t *session, uint32_t budget_us)
{
    if (session == NULL) {
        return POTA_ERR_BAD_ARGUMENT;
    }

    return pota_service(&session->core, budget_us);
}

pota_error_t pota_session_write(pota_session_t *session, const uint8_t *data, uint32_t size)
{
    if (session == NULL) {
        return POTA_ERR_BAD_ARGUMENT;
    }

    return pota_write(&session->core, data, size);
}

pota_error_t pota_session_end(pota_session_t *session)
{
    if (session == NULL) {
        return POTA_ERR_BAD_ARGUMENT;
    }

    return pota_end(&session->core);
}

pota_error_t pota_session_abort(pota_session_t *session)
{
    if (session == NULL) {
        return POTA_ERR_BAD_ARGUMENT;
    }

    return pota_abort(&session->core);
}

pota_error_t pota_session_commit(pota_session_t *session)
{
    if (session == NULL) {
        return POTA_ERR_BAD_ARGUMENT;
    }

    return pota_commit(&session->core);
}

void pota_session_get_status(const pota_session_t *session, pota_status_t *status)
{
    if (session == NULL) {
        return;
    }

    pota_get_status(&session->core, status);
}
