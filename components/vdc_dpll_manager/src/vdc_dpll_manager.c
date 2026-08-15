#include "vdc_dpll_manager.h"

#include <string.h>

#include "board.h"
#include "osal.h"
#include "vdc_domain.h"

static vdc_dpll_manager_vdc_status_t s_vdc_status;
static vdc_dpll_manager_dpll_status_t s_dpll_status;
static vdc_domain_context_t s_vdc_domain;
static bool s_vdc_ready;
static bool s_dpll_ready;

static uint64_t vdc_dpll_manager_now_ns(void)
{
    return (uint64_t)board_uptime_ms() * 1000000ull;
}

bool vdc_dpll_manager_init(void)
{
    const uint32_t now_ms = board_uptime_ms();

    memset(&s_vdc_status, 0, sizeof(s_vdc_status));
    memset(&s_dpll_status, 0, sizeof(s_dpll_status));
    if (!vdc_domain_init(&s_vdc_domain)) {
        return false;
    }
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
    vdc_domain_set_ready(&s_vdc_domain, ready);
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
    vdc_domain_snapshot_t snapshot;

    osal_critical_enter();
    vdc_domain_service(&s_vdc_domain, vdc_dpll_manager_now_ns());
    (void)vdc_domain_get_snapshot(&s_vdc_domain, &snapshot);
    if (s_vdc_status.service_count == 0u) {
        s_vdc_status.first_service_ms = now_ms;
    }
    s_vdc_status.service_count++;
    s_vdc_status.last_service_ms = now_ms;
    s_vdc_status.ready = s_vdc_ready;
    s_vdc_status.lock_state = snapshot.dpll.state;
    s_vdc_status.sync_seq++;
    osal_critical_exit();
}

void vdc_dpll_manager_dpll_service(void)
{
    const uint32_t now_ms = board_uptime_ms();
    vdc_domain_snapshot_t snapshot;

    osal_critical_enter();
    (void)vdc_domain_get_snapshot(&s_vdc_domain, &snapshot);
    if (s_dpll_status.service_count == 0u) {
        s_dpll_status.first_service_ms = now_ms;
    }
    s_dpll_status.service_count++;
    s_dpll_status.last_service_ms = now_ms;
    s_dpll_status.ready = s_dpll_ready;
    s_dpll_status.state = snapshot.dpll.state;
    s_dpll_status.update_seq = snapshot.dpll.update_seq;
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

bool vdc_dpll_manager_get_snapshot(vdc_domain_snapshot_t *snapshot)
{
    bool result = false;
    if (snapshot == NULL) {
        return false;
    }

    osal_critical_enter();
    result = vdc_domain_get_snapshot(&s_vdc_domain, snapshot);
    osal_critical_exit();
    return result;
}

bool vdc_dpll_manager_plan_tdma_window(uint32_t window_class,
                                       uint64_t now_ns,
                                       vdc_tdma_window_plan_t *plan,
                                       vdc_gate_result_t *gate)
{
    bool result = false;
    if (plan == NULL) {
        return false;
    }

    if (now_ns == VDC_DPLL_MANAGER_PLAN_NOW_NS) {
        now_ns = vdc_dpll_manager_now_ns();
    }

    osal_critical_enter();
    result = vdc_domain_plan_tdma_window(&s_vdc_domain.schedule,
                                         window_class,
                                         now_ns,
                                         plan,
                                         gate);
    osal_critical_exit();
    return result;
}
