#include "calibration_manager.h"

#include <string.h>

#include "board.h"
#include "osal.h"

#define CALIBRATION_MANAGER_DEFAULT_CRC32 0x10000003u

static calibration_manager_status_t s_status;
static bool s_ready;

bool calibration_manager_init(void)
{
    const uint32_t now_ms = board_uptime_ms();

    memset(&s_status, 0, sizeof(s_status));
    s_status.last_service_ms = now_ms;
    s_status.command_seq = 1u;
    s_status.link_count = 1u;
    s_status.delay_count = 1u;
    s_status.active_crc32 = CALIBRATION_MANAGER_DEFAULT_CRC32;
    s_ready = false;
    return true;
}

void calibration_manager_set_ready(bool ready)
{
    osal_critical_enter();
    s_ready = ready;
    s_status.ready = ready;
    osal_critical_exit();
}

void calibration_manager_service(void)
{
    const uint32_t now_ms = board_uptime_ms();

    osal_critical_enter();
    if (s_status.service_count == 0u) {
        s_status.first_service_ms = now_ms;
    }
    s_status.service_count++;
    s_status.last_service_ms = now_ms;
    s_status.ready = s_ready;
    s_status.state = 0u;
    osal_critical_exit();
}

void calibration_manager_get_status(calibration_manager_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_status;
    status->ready = s_ready;
    osal_critical_exit();
}
