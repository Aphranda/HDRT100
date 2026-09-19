"""Verify the STOP-only TX GAP command contract using actual SCPI callbacks."""
import os
import shutil
import subprocess

import pytest

from test_tdma_priority_rx_scpi import ROOT, callback


@pytest.fixture(scope="module")
def gap_commands(tmp_path_factory):
    directory = tmp_path_factory.mktemp("gap-scpi")
    source = (ROOT / "middleware/scpi_port/src/scpi_sync_commands.c").read_text(encoding="utf-8")
    header = (ROOT / "middleware/scpi_port/inc/scpi_system_snapshot_commands.h").read_text(encoding="utf-8")
    names = ("scpi_cmd_vdc_priority_tx_gap", "scpi_cmd_vdc_priority_tx_gap_q",
             "scpi_cmd_vdc_priority_tx_gap_status_q")
    for pattern, name in zip(("GAP", "GAP?", "GAP:STATus?"), names):
        assert header.count(f'.pattern = "SYSTem:VDC:PRIORity:TX:{pattern}", .callback = {name}') == 1
    unit = directory / "commands.c"
    unit.write_text(HARNESS + "\n".join(callback(source, name) for name in names) + MAIN,
                    encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    exe = directory / ("commands.exe" if os.name == "nt" else "commands")
    result = subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        *["-I" + str(ROOT / "components" / name / "inc") for name in
          ("tdma", "vdc_dpll_manager", "vdc_domain", "calibration_manager", "distributed_refmem")],
        str(unit), "-o", str(exe)], capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


@pytest.mark.parametrize("case", ["missing0", "missing1", "missing2", "setter_reject",
    "set", "clear", "max", "config_fields", "status_fields", "ring_fail", "enabled",
    "adapter_started", "config_fail", "status_fail"])
def test_gap_scpi_contract(gap_commands, case):
    result = subprocess.run([str(gap_commands), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <string.h>
#include "vdc_priority_tx.h"
typedef int scpi_t;
typedef int scpi_result_t;
#define TRUE 1
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
static uint32_t parameters[3]={101u,234u,567u}, parsed, available=3u, sets, gets, errors, results;
static uint32_t fields[32];
static bool setter_ok=true, ring_ok=true, config_ok=true, status_ok=true;
static tdma_ring_clock_snapshot_t ring;
static vdc_priority_tx_gap_config_t cfg;
static vdc_priority_tx_gap_snapshot_t status;
static bool SCPI_ParamUInt32(scpi_t *c,uint32_t *out,int required) {
    (void)c; assert(required); if(parsed>=available) return false; *out=parameters[parsed++]; return true;
}
bool vdc_dpll_manager_set_priority_tx_gap(uint32_t generation,uint32_t after,uint32_t duration) {
    ++sets; assert(generation==parameters[0] && after==parameters[1] && duration==parameters[2]);
    return setter_ok;
}
static bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out) {
    *out=ring; return ring_ok;
}
bool vdc_dpll_manager_get_priority_tx_gap_config(vdc_priority_tx_gap_config_t *out) {
    ++gets; *out=cfg; return config_ok;
}
bool vdc_dpll_manager_get_priority_tx_gap(vdc_priority_tx_gap_snapshot_t *out) {
    ++gets; *out=status; return status_ok;
}
static void SCPI_ResultUInt32(scpi_t *c,uint32_t value) {
    (void)c; assert(results<32u); fields[results++]=value;
}
static void scpi_sync_result_u64_parts(scpi_t *c,uint64_t value) {
    SCPI_ResultUInt32(c,(uint32_t)value); SCPI_ResultUInt32(c,(uint32_t)(value>>32u));
}
static void scpi_port_push_exec_error(scpi_t *c,const char *message) {
    (void)c; assert(message && *message); ++errors;
}
'''

MAIN = r'''
int main(int argc,char **argv) {
    assert(argc==2); const char *name=argv[1]; scpi_t c=0;
    if(!strncmp(name,"missing",7u)) {
        available=(uint32_t)(name[7]-'0');
        assert(scpi_cmd_vdc_priority_tx_gap(&c)==SCPI_RES_ERR);
        assert(!sets && !results && errors==1u && parsed==available);
    } else if(!strcmp(name,"setter_reject")) {
        setter_ok=false; assert(scpi_cmd_vdc_priority_tx_gap(&c)==SCPI_RES_ERR);
        assert(sets==1u && errors==1u && !results);
    } else if(!strcmp(name,"set") || !strcmp(name,"clear") || !strcmp(name,"max")) {
        if(!strcmp(name,"clear")) memset(parameters,0,sizeof(parameters));
        if(!strcmp(name,"max")) for(unsigned i=0;i<3u;++i) parameters[i]=UINT32_MAX;
        assert(scpi_cmd_vdc_priority_tx_gap(&c)==SCPI_RES_OK);
        assert(sets==1u && results==3u && !errors && !memcmp(fields,parameters,sizeof(parameters)));
    } else if(!strcmp(name,"config_fields")) {
        cfg=(vdc_priority_tx_gap_config_t){101u,999u,234u,567u};
        assert(scpi_cmd_vdc_priority_tx_gap_q(&c)==SCPI_RES_OK);
        assert(results==3u && gets==1u && !errors && !memcmp(fields,parameters,sizeof(parameters)));
    } else if(!strcmp(name,"status_fields")) {
        status=(vdc_priority_tx_gap_snapshot_t){.schema=11u,.generation=12u,.session=13u,.state=14u,
            .suppressed=15u,.before_sequence=16u,.resume_sequence=17u,.tick_hz=18u,
            .after_ms=19u,.duration_ms=20u,.anchor_raw=UINT64_C(0x1000000002),
            .first_suppressed_raw=UINT64_C(0x3000000004),.last_suppressed_raw=UINT64_C(0x5000000006),
            .resume_raw=UINT64_C(0x7000000008)};
        assert(scpi_cmd_vdc_priority_tx_gap_status_q(&c)==SCPI_RES_OK);
        const uint32_t expected[]={11,12,13,14,15,16,17,18,19,20,2,16,4,48,6,80,8,112};
        assert(results==18u && gets==1u && !errors && !memcmp(fields,expected,sizeof(expected)));
    } else if(!strcmp(name,"config_fail") || !strcmp(name,"status_fail")) {
        config_ok=status_ok=false;
        const int result=!strcmp(name,"config_fail") ? scpi_cmd_vdc_priority_tx_gap_q(&c) :
            scpi_cmd_vdc_priority_tx_gap_status_q(&c);
        assert(result==SCPI_RES_ERR && gets==1u && !results && errors==1u);
    } else {
        if(!strcmp(name,"ring_fail")) ring_ok=false;
        else if(!strcmp(name,"enabled")) ring.enabled=1u;
        else if(!strcmp(name,"adapter_started")) ring.adapter_started=1u;
        else assert(!"unknown SCPI gap scenario");
        assert(scpi_cmd_vdc_priority_tx_gap_q(&c)==SCPI_RES_ERR);
        assert(scpi_cmd_vdc_priority_tx_gap_status_q(&c)==SCPI_RES_ERR);
        assert(!gets && !results && errors==2u);
    }
    return 0;
}
'''
