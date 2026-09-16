"""Execute production STOP-only boundary SCPI with the real schema3 codec."""
import struct
import subprocess
import zlib

import pytest

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import ROOT, compile_executable


@pytest.fixture(scope="module")
def executable(tmp_path_factory):
    source = (ROOT / "middleware/scpi_port/src/scpi_system_snapshot_commands.c").read_text(encoding="utf-8")
    definitions = "\n".join(ingress_definition(source, name) for name in (
        "scpi_feedback_record", "scpi_cmd_vdc_feedback_probe", "scpi_cmd_vdc_feedback_auto",
        "scpi_cmd_vdc_feedback_auto_q", "scpi_cmd_vdc_feedback_reference",
        "scpi_cmd_vdc_feedback_reference_q", "scpi_cmd_vdc_feedback_boundary_q",
        "scpi_cmd_vdc_feedback_local_follow", "scpi_cmd_vdc_feedback_local_follow_q"))
    return compile_executable(tmp_path_factory.mktemp("boundary-scpi"), "boundary_scpi",
        PREAMBLE + definitions + CASES,
        [ROOT / "components/distributed_refmem/src/refmem_sync_vdc_feedback.c"])


def run(executable, case):
    result = subprocess.run([str(executable), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
    return result.stdout.strip()


def test_probe_signed_parameter_range_and_owner_rejection(executable):
    run(executable, "probe")


def test_auto_requires_explicit_boolean_and_owner_acceptance(executable):
    run(executable, "auto")


def test_reference_requires_explicit_owner_ack_and_busy_query_fails(executable):
    run(executable, "reference")


def test_local_follow_boolean_owner_refusal_and_busy_readback(executable):
    run(executable, "local")


@pytest.mark.parametrize("case", ["missing", "enabled", "adapter", "unapplied", "busy",
    "slot", "unavailable", "after_busy", "after_enabled", "after_adapter", "after_config",
    "after_applied", "bad_record", "capacity"])
def test_boundary_query_fails_closed_without_partial_response(executable, case):
    run(executable, case)


def test_stopped_record_matches_independent_schema3_wire(executable):
    body = struct.pack("<BBBBIIIIIQIIIIiQ", 3, 0, 3, 1, 0xFEDCBA98, 13,
        0xABCD0123, 11, 12, 0x0123456789ABCDEF, 14, 15, 16, 12, -100,
        0xFEDCBA9876543210)
    assert len(body) == 60
    expected = body + struct.pack("<I", zlib.crc32(body))
    assert bytes.fromhex(run(executable, "record")) == expected


def test_empty_retained_command_is_explicit_zero_record(executable):
    assert run(executable, "empty") == "00" * 64


def test_stopped_zero_runtime_nodes_preserves_retained_command(executable):
    assert run(executable, "stopped_zero_nodes") == run(executable, "record")


def test_commands_registered_once():
    header = (ROOT / "middleware/scpi_port/inc/scpi_system_snapshot_commands.h").read_text(encoding="utf-8")
    for pattern, callback in (
        ("SYSTem:VDC:FEEDback:PROBe", "scpi_cmd_vdc_feedback_probe"),
        ("SYSTem:VDC:FEEDback:AUTO", "scpi_cmd_vdc_feedback_auto"),
        ("SYSTem:VDC:FEEDback:AUTO?", "scpi_cmd_vdc_feedback_auto_q"),
        ("SYSTem:VDC:FEEDback:REFerence", "scpi_cmd_vdc_feedback_reference"),
        ("SYSTem:VDC:FEEDback:REFerence?", "scpi_cmd_vdc_feedback_reference_q"),
        ("SYSTem:VDC:FEEDback:BOUNDary?", "scpi_cmd_vdc_feedback_boundary_q"),
        ("SYSTem:VDC:FEEDback:LOCALfollow", "scpi_cmd_vdc_feedback_local_follow"),
        ("SYSTem:VDC:FEEDback:LOCALfollow?", "scpi_cmd_vdc_feedback_local_follow_q"),
    ):
        assert header.count(f'.pattern = "{pattern}", .callback = {callback}') == 1
        assert f"scpi_result_t {callback}(scpi_t *context);" in header


PREAMBLE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vdc_dpll_manager.h"
typedef int scpi_t;
typedef int scpi_result_t;
#define TRUE 1
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
static tdma_ring_runtime_snapshot_t ring={.config_seq=7,.applied_config_seq=7,.node_count=4};
static bool parameter_ok=true,setter_ok=true,available=true,auto_enabled;
static bool reference_enabled;
static uint32_t slot=3,reads,getters,setters,errors,count,texts;
static int32_t delta,current_delta;
static uint64_t values[17];
static char kinds[17],last_text[129];
static const char *scenario;
static vdc_dpll_boundary_status_t status;
static int SCPI_ParamInt32(scpi_t *c,int32_t *out,int required)
{ (void)c;assert(required==TRUE);*out=delta;return parameter_ok?TRUE:0; }
static bool scpi_port_read_u32(scpi_t *c,uint32_t *out)
{ (void)c;*out=slot;return parameter_ok; }
bool vdc_dpll_manager_set_boundary_probe(int32_t value)
{ ++setters;if(!setter_ok)return false;current_delta=value;return true; }
bool vdc_dpll_manager_set_boundary_auto(bool enabled)
{ ++setters;if(!setter_ok)return false;auto_enabled=enabled;return true; }
bool vdc_dpll_manager_boundary_auto_enabled(void)
{ return auto_enabled; }
bool vdc_dpll_manager_set_reference_publish(bool enabled)
{ ++setters;if(!setter_ok)return false;reference_enabled=enabled;return true; }
bool vdc_dpll_manager_try_reference_publish_enabled(bool *enabled)
{ if(!available)return false;*enabled=reference_enabled;return true; }
bool vdc_dpll_manager_set_local_follow(bool enabled)
{ ++setters;if(!setter_ok)return false;reference_enabled=enabled;return true; }
bool vdc_dpll_manager_try_local_follow_enabled(bool *enabled)
{ if(!available)return false;*enabled=reference_enabled;return true; }
bool vdc_dpll_manager_get_boundary_status(uint32_t value,vdc_dpll_boundary_status_t *out)
{ ++getters;if(!available||value>=4)return false;*out=status;return true; }
static bool tdma_runtime_owner_get_ring_snapshot(tdma_ring_runtime_snapshot_t *out)
{
    ++reads;*out=ring;
    if(!strcmp(scenario,"busy"))return false;
    if(reads==2) {
        if(!strcmp(scenario,"after_busy"))return false;
        if(!strcmp(scenario,"after_enabled"))out->enabled=1;
        if(!strcmp(scenario,"after_adapter"))out->adapter_started=1;
        if(!strcmp(scenario,"after_config"))out->config_seq++;
        if(!strcmp(scenario,"after_applied"))out->applied_config_seq++;
    }
    return true;
}
static void scpi_port_push_exec_error(scpi_t *c,const char *message)
{ (void)c;assert(!strncmp(message,"VDC_FEEDBACK_",13));++errors; }
static void SCPI_ResultText(scpi_t *c,const char *text)
{ (void)c;assert(strlen(text)<sizeof(last_text));strcpy(last_text,text);++texts; }
static void SCPI_ResultUInt32(scpi_t *c,uint32_t value)
{ (void)c;assert(count<17);kinds[count]='u';values[count++]=value; }
static void SCPI_ResultInt32(scpi_t *c,int32_t value)
{ (void)c;assert(count<17);kinds[count]='i';values[count++]=(uint64_t)(int64_t)value; }
'''

CASES = r'''
int main(int argc,char **argv)
{
    assert(argc==2);scenario=argv[1];scpi_t ctx=0;
    if(!strcmp(scenario,"local")) {
        parameter_ok=false;slot=1;
        assert(scpi_cmd_vdc_feedback_local_follow(&ctx)==SCPI_RES_ERR && !setters);
        parameter_ok=true;slot=2;
        assert(scpi_cmd_vdc_feedback_local_follow(&ctx)==SCPI_RES_ERR && !setters);
        slot=1;setter_ok=false;
        assert(scpi_cmd_vdc_feedback_local_follow(&ctx)==SCPI_RES_ERR && !reference_enabled);
        setter_ok=true;
        assert(scpi_cmd_vdc_feedback_local_follow(&ctx)==SCPI_RES_OK && reference_enabled);
        count=texts=0;available=false;
        assert(scpi_cmd_vdc_feedback_local_follow_q(&ctx)==SCPI_RES_ERR && !count);
        available=true;
        assert(scpi_cmd_vdc_feedback_local_follow_q(&ctx)==SCPI_RES_OK && values[0]==1);
        slot=0;
        assert(scpi_cmd_vdc_feedback_local_follow(&ctx)==SCPI_RES_OK && !reference_enabled);
        assert(!reads && !getters);return 0;
    }
    if(!strcmp(scenario,"reference")) {
        parameter_ok=false;slot=1;
        assert(scpi_cmd_vdc_feedback_reference(&ctx)==SCPI_RES_ERR && !setters && !count && !texts);
        parameter_ok=true;slot=2;
        assert(scpi_cmd_vdc_feedback_reference(&ctx)==SCPI_RES_ERR && !setters && !count && !texts);
        slot=1;setter_ok=false;
        assert(scpi_cmd_vdc_feedback_reference(&ctx)==SCPI_RES_ERR && setters==1 && !reference_enabled);
        setter_ok=true;
        assert(scpi_cmd_vdc_feedback_reference(&ctx)==SCPI_RES_OK && reference_enabled);
        assert(count==1 && values[0]==1 && texts==1 && !strcmp(last_text,"OK"));
        count=texts=0;available=false;
        assert(scpi_cmd_vdc_feedback_reference_q(&ctx)==SCPI_RES_ERR && !count && !texts);
        available=true;
        assert(scpi_cmd_vdc_feedback_reference_q(&ctx)==SCPI_RES_OK && count==1 && values[0]==1);
        count=texts=0;slot=0;
        assert(scpi_cmd_vdc_feedback_reference(&ctx)==SCPI_RES_OK && !reference_enabled);
        assert(count==1 && values[0]==0 && texts==1 && !strcmp(last_text,"OK"));
        assert(setters==3 && !reads && !getters);return 0;
    }
    if(!strcmp(scenario,"auto")) {
        parameter_ok=false;slot=1;
        assert(scpi_cmd_vdc_feedback_auto(&ctx)==SCPI_RES_ERR && !setters && !count && !texts);
        parameter_ok=true;slot=2;
        assert(scpi_cmd_vdc_feedback_auto(&ctx)==SCPI_RES_ERR && !setters && !count && !texts);
        slot=1;setter_ok=false;
        assert(scpi_cmd_vdc_feedback_auto(&ctx)==SCPI_RES_ERR && setters==1 && !auto_enabled);
        assert(!count && !texts);
        setter_ok=true;
        assert(scpi_cmd_vdc_feedback_auto(&ctx)==SCPI_RES_OK && auto_enabled);
        assert(count==1 && values[0]==1 && texts==1 && !strcmp(last_text,"OK"));
        count=texts=0;
        assert(scpi_cmd_vdc_feedback_auto_q(&ctx)==SCPI_RES_OK && count==1 && values[0]==1 && !texts);
        count=0;slot=0;
        assert(scpi_cmd_vdc_feedback_auto(&ctx)==SCPI_RES_OK && !auto_enabled);
        assert(count==1 && values[0]==0 && texts==1);
        assert(setters==3 && !reads && !getters);return 0;
    }
    if(!strcmp(scenario,"probe")) {
        parameter_ok=false;delta=7;current_delta=99;
        assert(scpi_cmd_vdc_feedback_probe(&ctx)==SCPI_RES_ERR && !setters && !count && !texts);
        parameter_ok=true;
        const int32_t invalid[]={INT32_MIN,-1001,1001,INT32_MAX};
        for(unsigned i=0;i<4;i++) {
            delta=invalid[i];assert(scpi_cmd_vdc_feedback_probe(&ctx)==SCPI_RES_ERR);
            assert(!setters && !count && !texts && current_delta==99);
        }
        setter_ok=false;delta=-100;
        assert(scpi_cmd_vdc_feedback_probe(&ctx)==SCPI_RES_ERR && setters==1);
        assert(!count && !texts && current_delta==99);
        setter_ok=true;
        const int32_t valid[]={-1000,-1,0,1,1000};
        for(unsigned i=0;i<5;i++) {
            count=texts=0;delta=valid[i];
            assert(scpi_cmd_vdc_feedback_probe(&ctx)==SCPI_RES_OK);
            assert(count==1 && texts==1 && !strcmp(last_text,"OK") && kinds[0]=='i');
            assert(values[0]==(uint64_t)(int64_t)delta && current_delta==delta);
        }
        assert(setters==6 && !reads && !getters);return 0;
    }
    status=(vdc_dpll_boundary_status_t){.schema=2,.active=0,.requested_probe_generation=3,
        .requested_delta_ppb=-100,.control_session=0xFEDCBA98u,.probe_generation=6,
        .consumed_mask=14,.offer_id=8,.offer_serial=9,.tx_count=10,.apply_count=11,
        .reject_count=12,.last_reject=13,.peer_state=2,.offered_ms=UINT32_MAX,
        .first_reject=4,.reject_mask=0x12u,
        .command={.schema_version=3,.source_slot=0,.target_slot=3,.flags=1,
            .control_session=0xFEDCBA98u,.command_seq=13,.schedule_crc32=0xABCD0123,
            .target_clock_epoch_id=11,.target_clock_run_id=12,
            .target_arm_epoch=UINT64_C(0x0123456789ABCDEF),.target_observer_epoch=14,
            .basis_measurement_sequence=15,.expected_target_model_token=16,
            .expected_applied_command_seq=12,.signed_delta_rate_ppb=-100,
            .basis_source_output_ns_lo=UINT64_C(0xFEDCBA9876543210)}};
    if(!strcmp(scenario,"missing"))parameter_ok=false;
    if(!strcmp(scenario,"enabled"))ring.enabled=1;
    if(!strcmp(scenario,"adapter"))ring.adapter_started=1;
    if(!strcmp(scenario,"unapplied"))ring.applied_config_seq=6;
    if(!strcmp(scenario,"slot"))slot=4;
    if(!strcmp(scenario,"unavailable"))available=false;
    if(!strcmp(scenario,"bad_record"))status.command.expected_target_model_token=0;
    if(!strcmp(scenario,"capacity"))status.command.target_slot=PROJECT_NODE_CAPACITY;
    if(!strcmp(scenario,"stopped_zero_nodes"))ring.node_count=0;
    if(!strcmp(scenario,"empty"))memset(&status.command,0,sizeof(status.command));
    const int result=scpi_cmd_vdc_feedback_boundary_q(&ctx);
    if(!strcmp(scenario,"record")||!strcmp(scenario,"empty")||!strcmp(scenario,"stopped_zero_nodes")) {
        const uint64_t expected[]={2,0,3,(uint64_t)(int64_t)-100,0xFEDCBA98u,6,14,8,9,10,11,12,13,2,UINT32_MAX,4,0x12u};
        assert(result==SCPI_RES_OK && count==17 && texts==1 && !errors && getters==1 && reads==2);
        assert(!memcmp(expected,values,sizeof(expected)) && !memcmp(kinds,"uuuiuuuuuuuuuuuuu",17));
        assert(strlen(last_text)==128);puts(last_text);
    } else {
        assert(result==SCPI_RES_ERR && !count && !texts);
        if(!parameter_ok)assert(!reads && !getters && !errors);
        else assert(errors==1);
        if(ring.enabled||ring.adapter_started||ring.config_seq!=ring.applied_config_seq||!strcmp(scenario,"busy"))
            assert(!getters && reads==1);
    }
    assert(!setters);return 0;
}
'''
