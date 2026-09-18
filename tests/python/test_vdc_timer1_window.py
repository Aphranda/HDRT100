"""Clock failure must produce a defined gate for RefMem's window consumer."""
import subprocess

from test_vdc_command_owner import MANAGER, compile_executable, function_body


def test_failed_clock_initializes_window_result(tmp_path):
    source = MANAGER.read_text(encoding="utf-8")
    harness = r'''
#include <assert.h>
#include <string.h>
#include "vdc_dpll_manager.h"
static vdc_domain_context_t s_vdc_domain;
static uint64_t clock_now;
static uint64_t vdc_dpll_manager_now_ns(void) { return clock_now; }
static void osal_critical_enter(void) {}
static void osal_critical_exit(void) {}
bool vdc_domain_plan_tdma_window(const vdc_tdma_schedule_profile_t *profile,
    uint32_t window_class, uint64_t now, vdc_tdma_window_plan_t *plan,
    vdc_gate_result_t *gate) {
    (void)window_class; (void)now;
    if (plan) memset(plan,0,sizeof(*plan));
    if (gate) { memset(gate,0,sizeof(*gate));
        gate->reject_code=profile ? 0u : VDC_DOMAIN_GATE_BAD_ARGUMENT; }
    return profile && plan;
}
'''
    for name, args in (
        ("vdc_dpll_manager_plan_published_tdma_window",
         "const vdc_dpll_manager_refmem_snapshot_t *snapshot, uint32_t window_class, "
         "uint64_t now_ns, vdc_tdma_window_plan_t *plan, vdc_gate_result_t *gate"),
        ("vdc_dpll_manager_plan_tdma_window",
         "uint32_t window_class, uint64_t now_ns, vdc_tdma_window_plan_t *plan, vdc_gate_result_t *gate"),
    ):
        harness += f"\nbool {name}({args}) {{\n{function_body(source, name)}\n}}\n"
    harness += r'''
int main(void) {
    vdc_dpll_manager_refmem_snapshot_t snapshot={0};
    vdc_tdma_window_plan_t plan, zero={0};
    vdc_gate_result_t gate;
    clock_now=UINT64_MAX;
    for (unsigned published=0;published<2;++published) {
        memset(&plan,0xa5,sizeof(plan)); memset(&gate,0xa5,sizeof(gate));
        bool ok=published ? vdc_dpll_manager_plan_published_tdma_window(
            &snapshot,0,VDC_DPLL_MANAGER_PLAN_NOW_NS,&plan,&gate) :
            vdc_dpll_manager_plan_tdma_window(0,VDC_DPLL_MANAGER_PLAN_NOW_NS,&plan,&gate);
        assert(!ok && memcmp(&plan,&zero,sizeof(plan))==0);
        assert(gate.reject_code==VDC_DOMAIN_GATE_BAD_ARGUMENT);
        assert(gate.reject_slot==0 && gate.reject_evidence==0);
    }
    clock_now=4004;
    assert(vdc_dpll_manager_plan_tdma_window(0,VDC_DPLL_MANAGER_PLAN_NOW_NS,&plan,&gate));
    return 0;
}
'''
    exe = compile_executable(tmp_path, "timer1_window", harness)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr
