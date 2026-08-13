#include "loop_engine.h"

#include <string.h>

#include "board.h"
#include "osal.h"

static loop_engine_status_t s_status;
static bool s_ready;

bool loop_engine_init(void)
{
    const uint32_t now_ms = board_uptime_ms();

    memset(&s_status, 0, sizeof(s_status));
    s_status.last_service_ms = now_ms;
    s_ready = false;
    return true;
}

void loop_engine_set_ready(bool ready)
{
    osal_critical_enter();
    s_ready = ready;
    s_status.ready = ready;
    osal_critical_exit();
}

void loop_engine_service(void)
{
    const uint32_t now_ms = board_uptime_ms();

    osal_critical_enter();
    if (s_status.service_count == 0u) {
        s_status.first_service_ms = now_ms;
    }
    s_status.service_count++;
    s_status.last_service_ms = now_ms;
    s_status.ready = s_ready;
    osal_critical_exit();
}

void loop_engine_get_status(loop_engine_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_status;
    status->ready = s_ready;
    osal_critical_exit();
}
