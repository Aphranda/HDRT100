"""Run production summary callbacks and registrations through actual libscpi."""
import os
import re
import subprocess

import pytest

from test_vdc_output_timing_config import ROOT, compile_host


@pytest.fixture(scope="module")
def summary_parser(tmp_path_factory):
    directory = tmp_path_factory.mktemp("summary-scpi-parser")
    source = (ROOT / "middleware/scpi_port/src/scpi_sync_commands.c").read_text(encoding="utf-8")
    callbacks = source[source.index("static bool scpi_priority_summary_u32("):
                       source.index("static scpi_result_t scpi_priority_guard_arm(")]
    assert "static scpi_result_t scpi_priority_summary_arm(" in callbacks
    table = (ROOT / "middleware/scpi_port/inc/scpi_system_snapshot_commands.h").read_text(encoding="utf-8")
    registrations = re.findall(r'\{\.pattern = "SYSTem:VDC:PRIORity:TRACe:SUMMary:[^\n]+?\},', table)
    assert len(registrations) == 2
    for pattern, mode in (("PHASe", "phase"), ("ORIGin", "origin")):
        entry = f'.pattern = "SYSTem:VDC:PRIORity:TRACe:SUMMary:{pattern}", .callback = scpi_cmd_vdc_priority_trace_summary_{mode}_arm'
        assert sum(entry in registration for registration in registrations) == 1
    commands = "\nstatic const scpi_command_t commands[]={\n" + "\n".join(registrations) + "\nSCPI_CMD_LIST_END};\n"
    unit = directory / "summary_scpi.c"
    unit.write_text(HARNESS + callbacks + commands + MAIN, encoding="utf-8")
    library = ROOT / "third_party/scpi-parser/libscpi"
    flags = ["-DSCPI_USER_CONFIG=1", "-I" + str(library / "inc"),
             "-I" + str(ROOT / "middleware/scpi_port/inc")]
    flags += ["-Wno-error=attributes"] if os.name == "nt" else ["-lm"]
    return compile_host(directory, "summary_scpi", [unit, *sorted((library / "src").glob("*.c"))], flags)


def run_parser(executable, command, expected, capture_id=0, interval_ms=0,
               origin=False, owner_reject=False):
    result = subprocess.run([str(executable), command, expected, str(capture_id),
                             str(interval_ms), str(int(origin)), str(int(owner_reject))],
                            capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("origin", [False, True])
@pytest.mark.parametrize("arguments,capture_id,interval_ms", [
    ("1", 1, 0), ("4294967295", 0xffffffff, 0), ("+42", 42, 0),
    ("00042 ", 42, 0), ("42,1000", 42, 1000), ("42,2000", 42, 2000),
    ("42,10000", 42, 10000), ("+42,+10000", 42, 10000),
    ("4294967295,1000", 0xffffffff, 1000), (" 42, 2000 ", 42, 2000),
])
def test_registered_legacy_and_window_commands(summary_parser, origin, arguments, capture_id, interval_ms):
    suffix = "ORIG" if origin else "PHAS"
    run_parser(summary_parser, "SYST:VDC:PRIOR:TRAC:SUMM:" + suffix + " " + arguments,
               str(capture_id), capture_id, interval_ms, origin)


@pytest.mark.parametrize("origin", [False, True])
@pytest.mark.parametrize("arguments", [
    "", " ", "0", "-1", "-0", "+", "4294967296", "18446744073709551616",
    "42.0", "4.2e1", '"42"', "#H2A", "42foo", "42 1", "42,",
    ",1000", "42,,1000", "42,-1", "42,-0", "42,0", "42,+",
    "42,999", "42,1001", "42,1500", "42,11000", "42,4294967296",
    "42,18446744073709551616", "42,1000.0", "42,1e3", '42,"1000"',
    "42,#H3E8", "42,1000ms", "42,1000,1", "42,1000,", "42,1000 1",
    "42,1000,garbage", "42 1000", "42 ,1000", "42,1000 ,1",
])
def test_invalid_parameters_never_reach_owner(summary_parser, origin, arguments):
    suffix = "ORIGin" if origin else "PHASe"
    run_parser(summary_parser, "SYSTem:VDC:PRIORity:TRACe:SUMMary:" + suffix + " " + arguments,
               "ERROR", origin=origin)


@pytest.mark.parametrize("origin", [False, True])
@pytest.mark.parametrize("interval_ms", [0, 1000, 2000, 10000])
def test_owner_rejection_emits_no_capture_id(summary_parser, origin, interval_ms):
    suffix = "ORIG" if origin else "PHAS"
    arguments = "42" + (f",{interval_ms}" if interval_ms else "")
    run_parser(summary_parser, "SYST:VDC:PRIOR:TRAC:SUMM:" + suffix + " " + arguments,
               "ERROR", 42, interval_ms, origin, owner_reject=True)


HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scpi/scpi.h"
#include "vdc_priority_trace.h"
#undef assert
#define assert(x) do {if(!(x)){fprintf(stderr,"CHECK line %d: %s\n",__LINE__,#x);exit(1);}}while(0)
static unsigned legacy_calls, window_calls;
static uint32_t received_id, received_interval;
static bool received_origin, reject_owner;
static char response[1024];
static size_t response_size;
bool vdc_dpll_manager_priority_trace_summary_arm(uint32_t id,bool origin){
    ++legacy_calls;received_id=id;received_origin=origin;return !reject_owner;
}
bool vdc_dpll_manager_priority_trace_summary_window_arm(uint32_t id,bool origin,uint32_t interval){
    ++window_calls;received_id=id;received_origin=origin;received_interval=interval;return !reject_owner;
}
static size_t write_response(scpi_t *ctx,const char *data,size_t length){
    (void)ctx;assert(response_size+length<sizeof(response));
    memcpy(response+response_size,data,length);response_size+=length;response[response_size]=0;return length;
}
static scpi_result_t flush_response(scpi_t *ctx){(void)ctx;return SCPI_RES_OK;}
static int record_error(scpi_t *ctx,int_fast16_t error){(void)ctx;(void)error;return 0;}
static void scpi_port_push_exec_error(scpi_t *ctx,const char *text){
    assert(text);SCPI_ErrorPush(ctx,SCPI_ERROR_EXECUTION_ERROR);
}
'''


MAIN = r'''
int main(int argc,char **argv){
    assert(argc==7);
    const uint32_t expected_id=(uint32_t)strtoull(argv[3],NULL,10);
    const uint32_t expected_interval=(uint32_t)strtoul(argv[4],NULL,10);
    const bool expected_origin=atoi(argv[5])!=0;
    reject_owner=atoi(argv[6])!=0;
    char input[1024],command[1024];scpi_t ctx;scpi_error_t errors[16];
    scpi_interface_t interface={.write=write_response,.flush=flush_response,.error=record_error};
    SCPI_Init(&ctx,commands,&interface,scpi_units_def,"vendor","model","serial","version",
        input,sizeof(input),errors,16);
    const int length=snprintf(command,sizeof(command),"%s\n",argv[1]);
    assert(length>0&&(size_t)length<sizeof(command));
    const scpi_bool_t parsed=SCPI_Input(&ctx,command,length);
    const int error_count=SCPI_ErrorCount(&ctx);
    fprintf(stderr,"parsed=%u errors=%d legacy=%u window=%u response=%s\n",
        (unsigned)parsed,error_count,legacy_calls,window_calls,response);
    if(!strcmp(argv[2],"ERROR")){
        assert(error_count>0&&response_size==0u);
        if(!reject_owner)assert(legacy_calls==0u&&window_calls==0u);
    }else{
        assert(parsed&&error_count==0);
        response[strcspn(response,"\r\n")]=0;
        assert(!strcmp(response,argv[2]));
    }
    if(reject_owner||strcmp(argv[2],"ERROR")){
        assert(legacy_calls==(expected_interval==0u)&&window_calls==(expected_interval!=0u));
        assert(received_id==expected_id&&received_origin==expected_origin);
        assert(received_interval==expected_interval);
    }
    return 0;
}
'''
