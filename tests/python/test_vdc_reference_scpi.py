"""Exercise the real external-reference SCPI callbacks with libscpi."""
import os
from pathlib import Path
import re
import subprocess

import pytest

from test_vdc_output_timing_config import compile_host

ROOT = Path(__file__).resolve().parents[2]


@pytest.mark.parametrize("header,valid,invalid", [
    ("SYST:VDC:REF:CONF", "4,10000000,0,1000,2500", "4"),
    ("SYSTEM:VDC:REFERENCE:CONFIGURE", "4,10000000,0,1000,2500", "4,10000000"),
    ("SYST:VDC:REF:ENAB", "1", "1,0"),
    ("SYSTEM:VDC:REFERENCE:ENABLE", "0", '"OK"'),
    ("SYST:VDC:REF:DISC", "1", "1,0"),
    ("SYST:VDC:REF:DISC:CONF", "50,4,10000", "50,4"),
    ("SYSTEM:VDC:REFERENCE:DISCIPLINE:CONFIGURE", "0,1,4294967295", "0,1,4294967295,0"),
])
def test_serial_write_result_shape(header, valid, invalid):
    from tools.scpi_common.scpi_serial import scpi_response_matches_command
    assert scpi_response_matches_command(header, valid)
    assert not scpi_response_matches_command(header, invalid)


@pytest.fixture(scope="module")
def reference_parser(tmp_path_factory):
    directory = tmp_path_factory.mktemp("reference-parser")
    callbacks = (ROOT / "middleware/scpi_port/src/scpi_reference_commands.c").read_text(encoding="utf-8")
    callbacks = callbacks.replace('#include "scpi_sync_commands.h"', '#include "scpi/scpi.h"')
    header = (ROOT / "middleware/scpi_port/inc/scpi_system_snapshot_commands.h").read_text(encoding="utf-8")
    entries = re.findall(r'\{\.pattern = "SYSTem:VDC:REFerence:[^\n]+?\},', header)
    assert len(entries) == 16
    unit = directory / "reference.c"
    unit.write_text(HARNESS + callbacks + '\nstatic const scpi_command_t commands[]={\n' +
                    '\n'.join(entries) + '\nSCPI_CMD_LIST_END};\n' + MAIN, encoding="utf-8")
    library = ROOT / "third_party/scpi-parser/libscpi"
    flags = ["-DSCPI_USER_CONFIG=1", "-I" + str(library / "inc"),
             "-I" + str(ROOT / "middleware/scpi_port/inc"),
             "-I" + str(ROOT / "components/sync_io/inc")]
    flags += ["-Wno-error=attributes"] if os.name == "nt" else ["-lm"]
    return compile_host(directory, "reference", [unit, *sorted((library / "src").glob("*.c"))], flags)


@pytest.mark.parametrize("command,expected", [
    ("CONF 4,10000000,0,1000,2500", "4,10000000,0,1000,2500"),
    ("CONF +1,+20000000,+1,+5000,+10000", "1,20000000,1,5000,10000"),
    ("CONF?", "4,10000000,0,1000,2500"),
    ("ENAB 1", "1"), ("ENAB 0", "0"), ("ENAB?", "0"),
    ("DISC 1", "1"), ("DISC 0", "0"),
    ("DISC?", "1,3,0,2,0,9,10,8,0,8,4000,3990,800,7,2,3,4,0,5,90"),
    ("DISC:CONF 50,8,9000", "50,8,9000"),
    ("DISC:CONF +100,+4,+10000", "100,4,10000"),
    ("DISC:CONF 0,0,0", "0,0,0"),
    ("DISC:CONF 0,1,4294967295", "0,1,4294967295"),
    ("DISC:CONF 4294967295,4294967295,4294967295", "4294967295,4294967295,4294967295"),
    ("DISC:CONF?", "100,4,10000"),
    ("DISC:ACT?", "1,3,6,123456,50,8,9000"),
    ("DISC:DEFA", '"OK"'), ("DISC:REC", '"OK"'), ("DISC:STOR", '"OK"'),
    ("DEFA", '"OK"'), ("REC", '"OK"'), ("STOR", '"OK"'),
    ("STAT?", "1,2,0,0,3,1,9,7,1,4,10000000,0,1000,2500,20,250000000,10000000,1,250000002,250000000,1,-123,1,3,17"),
])
def test_commands(reference_parser, command, expected):
    run(reference_parser, command, expected)


@pytest.mark.parametrize("arguments", [
    "", "4,10000000,0,1000", "4,10000000,0,1000,2500,1",
    "0,10000000,0,1000,2500", "5,10000000,0,1000,2500",
    "4,999,0,1000,2500", "4,20000001,0,1000,2500",
    "4,10000000,2,1000,2500", "4,10000000,0,99,2500",
    "4,10000000,0,5001,6000", "4,10000000,0,1000,1000",
    "4,10000000,0,1000,10001", "4,1001,0,101,2500",
    "4,1e7,0,1000,2500", "4,10000000.0,0,1000,2500",
    "-4,10000000,0,1000,2500", "4,4294967296,0,1000,2500",
    "4,10000000 ,0,1000,2500", "4,10000000,0,1000,2500,",
])
def test_bad_config_never_reaches_owner(reference_parser, arguments):
    run(reference_parser, "CONF " + arguments, "ERROR")


@pytest.mark.parametrize("arguments", [
    "", "50", "50,4", "50,4,10000,1", "50,4,10000,",
    "-1,4,10000", "50,-1,10000", "50,4,-1",
    "4294967296,4,10000", "50,4294967296,10000", "50,4,4294967296",
    "50.0,4,10000", "5e1,4,10000", '"50",4,10000',
    "50 ,4,10000", "50,4.0,10000", "50,4,1e4",
])
def test_bad_discipline_config_never_reaches_owner(reference_parser, arguments):
    run(reference_parser, "DISC:CONF " + arguments, "ERROR")


@pytest.mark.parametrize("command", ["ENAB", "ENAB 2", "ENAB -1", "ENAB 0.5", "ENAB 1,0",
    "ENAB? 1", "CONF? 1", "STAT? 1", "DEFA 1", "REC 1", "STOR 1",
    "DISC", "DISC 2", "DISC -1", "DISC 1,0", "DISC? 1",
    "DISC:CONF? 1", "DISC:ACT? 1", "DISC:DEFA 1", "DISC:REC 1", "DISC:STOR 1"])
def test_malformed_commands(reference_parser, command):
    run(reference_parser, command, "ERROR")


@pytest.mark.parametrize("command", ["CONF 4,10000000,0,1000,2500", "CONF?", "ENAB 1", "ENAB 0",
    "ENAB?", "STAT?", "DEFA", "REC", "STOR", "DISC 1", "DISC 0", "DISC?",
    "DISC:CONF 50,8,9000", "DISC:CONF?", "DISC:ACT?", "DISC:DEFA", "DISC:REC", "DISC:STOR"])
def test_owner_rejection(reference_parser, command):
    run(reference_parser, command, "ERROR", "blocked")


def run(executable, command, expected, blocker="none"):
    result = subprocess.run([str(executable), "SYST:VDC:REF:" + command, expected, blocker],
                            capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scpi/scpi.h"
#include "vdc_reference.h"
#include "sync_io_reference_math.h"
static unsigned owner_calls;
static bool blocked, enabled;
static sync_io_reference_config_t config={4,0,10000000,1000,2500};
static vdc_reference_discipline_config_t discipline_config={100,4,10000};
static bool owner(void){++owner_calls;return !blocked;}
bool sync_io_reference_config_valid(const sync_io_reference_config_t *c){uint32_t n;return sync_io_reference_validate(c,&n);}
bool vdc_dpll_manager_set_reference_config(const sync_io_reference_config_t *c){if(!owner())return false;config=*c;return true;}
bool vdc_dpll_manager_get_reference_config(sync_io_reference_config_t *c){if(!owner())return false;*c=config;return true;}
bool vdc_dpll_manager_set_reference_enabled(bool e){if(!owner())return false;enabled=e;return true;}
bool vdc_dpll_manager_set_reference_discipline(bool e){if(!owner())return false;enabled=e;return true;}
bool vdc_dpll_manager_get_reference_discipline(vdc_reference_discipline_status_t *s){
    if(!owner())return false;
    *s=(vdc_reference_discipline_status_t){1,3,enabled,2,0,9,10,8,0,8,4000,3990,800,7,2,3,4,0,5,90,
        6,123456,{50,8,9000}};
    return true;
}
bool vdc_dpll_manager_get_reference_discipline_config(vdc_reference_discipline_config_t *c){
    if(!owner())return false;
    *c=discipline_config;return true;
}
bool vdc_dpll_manager_set_reference_discipline_config(const vdc_reference_discipline_config_t *c){
    if(!owner())return false;
    discipline_config=*c;return true;
}
bool vdc_dpll_manager_default_reference_discipline(void){return owner();}
bool vdc_dpll_manager_recall_reference_discipline(void){return owner();}
bool vdc_dpll_manager_store_reference_discipline(void){return owner();}
bool vdc_dpll_manager_get_reference_status(vdc_reference_status_t *s){
    if(!owner())return false;
    *s=(vdc_reference_status_t){.config_generation=2,.enabled=enabled,.monitor={
        .state=3,.reason=1,.generation=9,.sample_seq=7,.valid=1,.config=config,.input_pin=20,
        .tick_hz=250000000,.reference_cycles=10000000,.start_raw32=1,.end_raw32=250000002,
        .elapsed_ticks=250000000,.pio_bias_ticks=1,.frequency_error_ppb=-123,
        .measurement_flags=1,.completed_raw=(UINT64_C(3)<<32)|17}};return true;
}
bool vdc_dpll_manager_default_reference(void){return owner();}
bool vdc_dpll_manager_recall_reference(void){return owner();}
bool vdc_dpll_manager_store_reference(void){return owner();}
static void scpi_port_push_exec_error(scpi_t *ctx,const char *message){(void)message;SCPI_ErrorPush(ctx,SCPI_ERROR_EXECUTION_ERROR);}
static char response[2048];static size_t used;
static size_t write_response(scpi_t *ctx,const char *p,size_t n){(void)ctx;assert(used+n<sizeof(response));memcpy(response+used,p,n);used+=n;response[used]=0;return n;}
static scpi_result_t flush(scpi_t *ctx){(void)ctx;return SCPI_RES_OK;}
static int error(scpi_t *ctx,int_fast16_t e){(void)ctx;(void)e;return 0;}
'''

MAIN = r'''
int main(int argc,char **argv){
    assert(argc==4);blocked=!strcmp(argv[3],"blocked");
    const sync_io_reference_config_t before=config;
    const vdc_reference_discipline_config_t discipline_before=discipline_config;
    char input[1024],command[1024];scpi_t ctx;scpi_error_t errors[16];
    scpi_interface_t interface={.write=write_response,.flush=flush,.error=error};
    SCPI_Init(&ctx,commands,&interface,scpi_units_def,"v","m","s","r",input,sizeof(input),errors,16);
    int n=snprintf(command,sizeof(command),"%s\n",argv[1]);assert(n>0&&(size_t)n<sizeof(command));
    const scpi_bool_t parsed=SCPI_Input(&ctx,command,n);
    fprintf(stderr,"parsed=%u errors=%d response=%s\n",(unsigned)parsed,SCPI_ErrorCount(&ctx),response);
    if(!strcmp(argv[2],"ERROR")){
        assert(SCPI_ErrorCount(&ctx)>0&&used==0&&!memcmp(&before,&config,sizeof(config))&&!enabled);
        assert(!memcmp(&discipline_before,&discipline_config,sizeof(discipline_config)));
        if(!blocked)assert(owner_calls==0);
    }else{assert(parsed&&SCPI_ErrorCount(&ctx)==0);response[strcspn(response,"\r\n")]=0;assert(!strcmp(response,argv[2]));}
    return 0;
}
'''
