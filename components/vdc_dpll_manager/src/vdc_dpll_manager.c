#include "vdc_dpll_manager.h"

#include <string.h>

#include "board.h"
#include "osal.h"

static vdc_dpll_manager_vdc_status_t s_vdc_status;
static vdc_dpll_manager_dpll_status_t s_dpll_status;
static bool s_vdc_ready;
static bool s_dpll_ready;

bool vdc_dpll_manager_init(void)
{
    const uint32_t now_ms = board_uptime_ms();

    memset(&s_vdc_status, 0, sizeof(s_vdc_status));
    memset(&s_dpll_status, 0, sizeof(s_dpll_status));
    s_vdc_status.last_service_ms = now_ms;
    s_dpll_status.last_service_ms = now_ms;
    s_vdc_ready = false;
    s_dpll_ready = false;
    return true;
}

void vdc_dpll_manager_set_vdc_ready(bool ready)
{
    osal_critical_enter();
    s_vdc_ready = ready;
    s_vdc_status.ready = ready;
    osal_critical_exit();
}

void vdc_dpll_manager_set_dpll_ready(bool ready)
{
    osal_critical_enter();
    s_dpll_ready = ready;
    s_dpll_status.ready = ready;
    osal_critical_exit();
}

void vdc_dpll_manager_vdc_service(void)
{
    const uint32_t now_ms = board_uptime_ms();

    osal_critical_enter();
    if (s_vdc_status.service_count == 0u) {
        s_vdc_status.first_service_ms = now_ms;
    }
    s_vdc_status.service_count++;
    s_vdc_status.last_service_ms = now_ms;
    s_vdc_status.ready = s_vdc_ready;
    s_vdc_status.lock_state = 0u;
    s_vdc_status.sync_seq++;
    osal_critical_exit();
}

void vdc_dpll_manager_dpll_service(void)
{
    const uint32_t now_ms = board_uptime_ms();

    osal_critical_enter();
    if (s_dpll_status.service_count == 0u) {
        s_dpll_status.first_service_ms = now_ms;
    }
    s_dpll_status.service_count++;
    s_dpll_status.last_service_ms = now_ms;
    s_dpll_status.ready = s_dpll_ready;
    s_dpll_status.state = 0u;
    s_dpll_status.update_seq++;
    osal_critical_exit();
}

void vdc_dpll_manager_get_vdc_status(vdc_dpll_manager_vdc_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_vdc_status;
    status->ready = s_vdc_ready;
    osal_critical_exit();
}

void vdc_dpll_manager_get_dpll_status(vdc_dpll_manager_dpll_status_t *status)
{
    if (status == NULL) {
        return;
    }

    osal_critical_enter();
    *status = s_dpll_status;
    status->ready = s_dpll_ready;
    osal_critical_exit();
}
