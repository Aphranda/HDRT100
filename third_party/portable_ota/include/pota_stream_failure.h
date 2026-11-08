#ifndef POTA_STREAM_FAILURE_H
#define POTA_STREAM_FAILURE_H

#include <stdint.h>

#include "pota_stream_session.h"

/* First-failure token encoding shared by the RP2350 shim and the host tests.
 * Keeping the arithmetic here makes the diagnostic contract testable without
 * pulling in flash/RTOS dependencies. */
static inline uint32_t pota_stream_write_failure_token(
    pota_stream_result_t write_result, uint32_t write_error)
{
    return write_result == POTA_STREAM_RESULT_CORE
               ? 6000u + write_error
               : 5000u + (uint32_t)write_result;
}

static inline uint32_t pota_stream_service_failure_token(
    pota_stream_result_t session_result, uint32_t flash_stage,
    uint32_t session_error)
{
    if (session_result != POTA_STREAM_RESULT_CORE) {
        return 1000u + (uint32_t)session_result;
    }
    return flash_stage != 0u ? 2000u + flash_stage
                             : 3000u + session_error;
}

#endif
