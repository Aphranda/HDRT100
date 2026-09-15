"""Run production session/committed-model and schema-2 match SCPI facades."""
import re
import subprocess

import pytest

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import ROOT, compile_executable

SOURCE = ROOT / "middleware/scpi_port/src/scpi_system_snapshot_commands.c"
HEADER = ROOT / "middleware/scpi_port/inc/scpi_system_snapshot_commands.h"


def test_feedback_commands_are_registered():
    header = HEADER.read_text(encoding="utf-8")
    for pattern, callback in (
        ("SYSTem:VDC:FEEDback:SESSion", "scpi_cmd_vdc_feedback_session"),
        ("SYSTem:VDC:FEEDback:SESSion?", "scpi_cmd_vdc_feedback_session_q"),
        ("SYSTem:VDC:FEEDback:MODel?", "scpi_cmd_vdc_feedback_model_q"),
    ):
        assert len(re.findall(r'\.pattern\s*=\s*"' + re.escape(pattern) +
                              r'"\s*,\s*\.callback\s*=\s*' + callback + r'\b', header)) == 1
        assert f"scpi_result_t {callback}(scpi_t *context);" in header


@pytest.fixture(scope="module")
def executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("feedback-session-scpi")
    source = SOURCE.read_text(encoding="utf-8")
    common = (ROOT / "middleware/scpi_port/src/scpi_port.c").read_text(encoding="utf-8")
    definitions = ingress_definition(common, "scpi_port_read_u32") + "\n" + "\n".join(
        ingress_definition(source, name) for name in (
            "scpi_cmd_vdc_feedback_session", "scpi_cmd_vdc_feedback_session_q",
            "scpi_cmd_vdc_feedback_model_q", "scpi_cmd_vdc_feedback_match_q"))
    return compile_executable(directory, "feedback_session_scpi", PREAMBLE + definitions + CASES)


@pytest.mark.parametrize("case", ["session", "committed", "empty", "schema2"])
def test_actual_feedback_scpi(executable, case):
    result = subprocess.run([str(executable), case], capture_output=True, text=True, timeout=5)
    (executable.parent / (case + ".log")).write_text(result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


PREAMBLE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vdc_dpll_manager.h"
typedef int scpi_t;
typedef int scpi_result_t;
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
#define TRUE 1
static bool parameter_ok=true, setter_ok=true, available=true;
static uint32_t parameter, current_session, setter_calls, getter_calls, errors;
static uint32_t count, text_count;
static uint64_t values[47];
static char kinds[47];
static vdc_dpll_manager_committed_model_t committed;
static vdc_dpll_manager_feedback_match_status_t matched;
static int SCPI_ParamUInt32(scpi_t *ctx,uint32_t *out,int required)
{ (void)ctx;assert(required==TRUE);if(!parameter_ok)return 0;*out=parameter;return TRUE; }
bool vdc_dpll_manager_set_feedback_session(uint32_t session)
{ ++setter_calls;if(!setter_ok)return false;current_session=session;return true; }
uint32_t vdc_dpll_manager_feedback_session(void) { return current_session; }
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t *out)
{ ++getter_calls;*out=committed;return available; }
bool vdc_dpll_manager_get_feedback_match(uint32_t source,vdc_dpll_manager_feedback_match_status_t *out)
{ ++getter_calls;assert(source==3);*out=matched;return available; }
static void scpi_port_push_exec_error(scpi_t *ctx,const char *message)
{ (void)ctx;assert(!strcmp(message,"VDC_FEEDBACK_SESSION_STOP_REQUIRED_OR_BUSY"));++errors; }
static void SCPI_ResultText(scpi_t *ctx,const char *text)
{ (void)ctx;assert(!strcmp(text,"OK"));++text_count; }
static void SCPI_ResultUInt32(scpi_t *ctx,uint32_t value)
{ (void)ctx;assert(count<47);kinds[count]='u';values[count++]=value; }
static void SCPI_ResultUInt64(scpi_t *ctx,uint64_t value)
{ (void)ctx;assert(count<47);kinds[count]='U';values[count++]=value; }
static void SCPI_ResultInt32(scpi_t *ctx,int32_t value)
{ (void)ctx;assert(count<47);kinds[count]='i';values[count++]=(uint64_t)(int64_t)value; }
static void SCPI_ResultInt64(scpi_t *ctx,int64_t value)
{ (void)ctx;assert(count<47);kinds[count]='I';values[count++]=(uint64_t)value; }
'''

CASES = r'''
int main(int argc,char **argv)
{
    assert(argc==2);scpi_t ctx=0;
    if(!strcmp(argv[1],"session")) {
        parameter_ok=false;current_session=7;
        assert(scpi_cmd_vdc_feedback_session(&ctx)==SCPI_RES_ERR);
        assert(!setter_calls && errors==1 && !count && !text_count && current_session==7);
        parameter_ok=true;setter_ok=false;parameter=42;
        assert(scpi_cmd_vdc_feedback_session(&ctx)==SCPI_RES_ERR);
        assert(setter_calls==1 && errors==2 && !count && !text_count && current_session==7);
        setter_ok=true;
        const uint32_t sessions[]={0,42,UINT32_MAX};
        for(unsigned i=0;i<3;i++) {
            count=text_count=0;parameter=sessions[i];
            assert(scpi_cmd_vdc_feedback_session(&ctx)==SCPI_RES_OK);
            assert(count==1 && text_count==1 && values[0]==parameter && current_session==parameter);
            count=text_count=0;
            assert(scpi_cmd_vdc_feedback_session_q(&ctx)==SCPI_RES_OK);
            assert(count==1 && !text_count && values[0]==parameter && kinds[0]=='u');
        }
        assert(setter_calls==4 && !getter_calls);
    } else if(!strcmp(argv[1],"committed")) {
        committed=(vdc_dpll_manager_committed_model_t){
            .token=2,.session=3,.role_generation=4,.applied_command_seq=5,
            .clock_epoch_id=6,.clock_run_id=7,.local_slot=8,
            .valid_from_raw=UINT64_C(0x12345678abcdef90),
            .dco={.valid=10,.dco_update_seq=11,.source_model_seq=12,.epoch_id=13,.run_id=14,
                .base_local_tick64=UINT64_MAX,.base_vdc_time64_ns=UINT64_C(0xfedcba9876543210),
                .nominal_period_ns=17,.period_adjust_ppb=INT32_MIN,.phase_offset_ns=-19,
                .slew_limit_ppb=20,.lock_state=21,.tdma_schedule_crc32=22,.servo_profile_crc32=23}};
        available=false;
        assert(scpi_cmd_vdc_feedback_model_q(&ctx)==SCPI_RES_ERR && getter_calls==1 && !count);
        available=true;
        assert(scpi_cmd_vdc_feedback_model_q(&ctx)==SCPI_RES_OK && getter_calls==2 && count==23);
        const uint64_t expected[]={1,2,3,4,5,6,7,8,UINT64_C(0x12345678abcdef90),10,11,12,13,14,
            UINT64_MAX,UINT64_C(0xfedcba9876543210),17,(uint64_t)(int64_t)INT32_MIN,(uint64_t)(int64_t)-19,20,21,22,23};
        assert(!memcmp(values,expected,sizeof(expected)));
        assert(!memcmp(kinds,"uuuuuuuuUuuuuuUUuiiuuuu",23));
        assert(!setter_calls && !text_count);
    } else if(!strcmp(argv[1],"empty")) {
        assert(scpi_cmd_vdc_feedback_model_q(&ctx)==SCPI_RES_OK && count==23 && values[0]==1);
        for(unsigned i=1;i<23;i++)assert(values[i]==0);
    } else if(!strcmp(argv[1],"schema2")) {
        parameter=3;
        matched.schema=2;matched.last_age_ticks=81;matched.max_age_ticks=120;
        matched.control_session=0xabcde123u;matched.result.reserved=VDC_FEEDBACK_MODEL_DOMAIN;
        matched.result.raw_ppb_lo=-1234567890123ll;matched.result.raw_ppb_hi=1234567890123ll;
        matched.result.pairs[0]=(vdc_feedback_match_pair_t){.measurement_sequence=41,.reference_identity_crc32=42,
            .rx_elapsed_cycles=UINT64_C(0x1111222233334444),.reference_tx_lo=UINT64_C(0x2222333344445555),
            .reference_tx_hi=UINT64_C(0x2222333344446666),.rx_width_ns=1234,.source_model_token=51};
        matched.result.pairs[1]=(vdc_feedback_match_pair_t){.measurement_sequence=43,.reference_identity_crc32=44,
            .rx_elapsed_cycles=UINT64_C(0x3333444455556666),.reference_tx_lo=UINT64_C(0x4444555566667777),
            .reference_tx_hi=UINT64_C(0x4444555566668888),.rx_width_ns=5678,.source_model_token=52};
        available=false;
        assert(scpi_cmd_vdc_feedback_match_q(&ctx)==SCPI_RES_ERR && !count);
        available=true;
        assert(scpi_cmd_vdc_feedback_match_q(&ctx)==SCPI_RES_OK && count==47);
        assert(values[0]==2 && values[19]==81 && values[20]==120);
        assert(kinds[29]=='I' && values[29]==(uint64_t)-1234567890123ll);
        assert(kinds[30]=='I' && values[30]==1234567890123ll);
        const uint64_t tail[]={41,42,UINT64_C(0x1111222233334444),UINT64_C(0x2222333344445555),
            UINT64_C(0x2222333344446666),43,44,UINT64_C(0x3333444455556666),UINT64_C(0x4444555566667777),
            UINT64_C(0x4444555566668888),0xabcde123u,VDC_FEEDBACK_MODEL_DOMAIN,1234,51,5678,52};
        assert(!memcmp(values+31,tail,sizeof(tail)));
        assert(!memcmp(kinds+31,"uuUUUuuUUUuuuuuu",16));
        uint64_t prefix[41];memcpy(prefix,values,sizeof(prefix));
        count=0;matched.schema=1;
        assert(scpi_cmd_vdc_feedback_match_q(&ctx)==SCPI_RES_OK && count==41 && values[0]==1);
        assert(!memcmp(values+1,prefix+1,40*sizeof(values[0])));
    } else return 2;
    puts("feedback SCPI passed");return 0;
}
'''
