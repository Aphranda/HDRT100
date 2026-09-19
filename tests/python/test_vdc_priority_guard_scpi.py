"""Guard control callbacks through the real libscpi parser."""
import os
import re
import subprocess
import pytest
from test_vdc_output_timing_config import ROOT, compile_host
from test_vdc_priority_summary_scpi import HARNESS


@pytest.fixture(scope='module')
def parser(tmp_path_factory):
    directory=tmp_path_factory.mktemp('guard-parser')
    source=(ROOT/'middleware/scpi_port/src/scpi_sync_commands.c').read_text(encoding='utf-8')
    helper=source[source.index('static bool scpi_priority_summary_u32('):source.index('static scpi_result_t scpi_priority_summary_arm(')]
    callbacks=source[source.index('static scpi_result_t scpi_priority_guard_arm('):source.index('scpi_result_t scpi_cmd_vdc_priority_trace_arm(')]
    table=(ROOT/'middleware/scpi_port/inc/scpi_system_snapshot_commands.h').read_text(encoding='utf-8')
    registrations=re.findall(r'\{\.pattern = "SYSTem:VDC:PRIORity:TRACe:GUARd[^\n]+?\},',table)
    assert len(registrations)==3
    unit=directory/'guard.c'
    unit.write_text(HARNESS+STUBS+helper+callbacks+'\nstatic const scpi_command_t commands[]={\n'+
        '\n'.join(registrations)+'\nSCPI_CMD_LIST_END};\n'+MAIN,encoding='utf-8')
    library=ROOT/'third_party/scpi-parser/libscpi'
    flags=['-DSCPI_USER_CONFIG=1','-I'+str(library/'inc'),'-I'+str(ROOT/'middleware/scpi_port/inc')]
    flags+=['-Wno-error=attributes'] if os.name=='nt' else ['-lm']
    return compile_host(directory,'guard',[unit,*sorted((library/'src').glob('*.c'))],flags)


@pytest.mark.parametrize('command,expected',[
    ('SYST:VDC:PRIOR:TRAC:GUAR:PHAS 42,60','42'),
    ('SYST:VDC:PRIOR:TRAC:GUAR:ORIG 42,600','42'),
    ('SYST:VDC:PRIOR:TRAC:GUAR:PHAS 0,60','ERROR'),
    ('SYST:VDC:PRIOR:TRAC:GUAR:PHAS 42,61','ERROR'),
    ('SYST:VDC:PRIOR:TRAC:GUAR:PHAS 42,60,1','ERROR'),
    ('SYST:VDC:PRIOR:TRAC:GUAR:PHAS -1,60','ERROR'),
    ('SYST:VDC:PRIOR:TRAC:GUAR:PHAS 42,1e2','ERROR'),
    ('SYST:VDC:PRIOR:TRAC:GUAR:PHAS 42','ERROR'),
    ('SYST:VDC:PRIOR:TRAC:GUAR?','2,42,123,102,600,4,2,100,60,0,60000,7,8,1,0,0,9,4,3,70024,1')])
def test_guard_registered_control(parser,command,expected):
    r=subprocess.run([str(parser),command,expected],capture_output=True,text=True,timeout=5)
    assert r.returncode==0,r.stdout+r.stderr


STUBS=r'''
bool vdc_dpll_manager_priority_trace_guard_arm(uint32_t id,bool origin,uint32_t seconds)
{ (void)origin;return id && seconds && seconds<=600 && seconds%60==0; }
bool vdc_dpll_manager_get_priority_guard(vdc_priority_guard_status_t *s)
{ *s=(vdc_priority_guard_status_t){2,42,123,102,600,4,2,100,60,0,60000,7,8,1,0,0,9,4,3,70024,1};return true; }
'''
MAIN=r'''
int main(int argc,char **argv){
 assert(argc==3);char input[1024],command[1024];scpi_t ctx;scpi_error_t errors[16];
 scpi_interface_t interface={.write=write_response,.flush=flush_response,.error=record_error};
 SCPI_Init(&ctx,commands,&interface,scpi_units_def,"v","m","s","v",input,sizeof(input),errors,16);
 const int length=snprintf(command,sizeof(command),"%s\n",argv[1]);SCPI_Input(&ctx,command,length);
 if(!strcmp(argv[2],"ERROR"))assert(SCPI_ErrorCount(&ctx)>0 && response_size==0);
 else {assert(!SCPI_ErrorCount(&ctx));response[strcspn(response,"\r\n")]=0;assert(!strcmp(response,argv[2]));}
 assert(!legacy_calls&&!window_calls);return 0;
}
'''
