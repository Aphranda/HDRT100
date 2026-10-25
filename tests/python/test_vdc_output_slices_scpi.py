"""Real libscpi parsing for continuous RUN slice configuration."""
from pathlib import Path
import subprocess
import pytest
from test_vdc_run_output import compile_host, PARSER_PREFIX

ROOT = Path(__file__).resolve().parents[2]

@pytest.fixture(scope='module')
def parser(tmp_path_factory):
    source = (ROOT/'middleware/scpi_port/src/scpi_vdc_snapshot_commands.c').read_text(encoding='utf-8')
    helpers = source[source.index('bool scpi_vdc_run_u32('):source.index('scpi_result_t scpi_cmd_vdc_run_output(')]
    callbacks = (ROOT/'middleware/scpi_port/src/scpi_vdc_output_slices.c').read_text(encoding='utf-8')
    # The command unit now has its own include prologue; the harness below supplies
    # that context itself, so keep only the handler body (same text the old .inc had).
    body = callbacks.splitlines(keepends=True)
    while body and (not body[0].strip() or body[0].lstrip().startswith('#include')):
        body.pop(0)
    callbacks = ''.join(body)
    code = PARSER_PREFIX.replace('static scpi_result_t scpi_port_result_ok(',
        'scpi_result_t scpi_port_result_ok(') + r'''
static unsigned changes;
static vdc_run_output_slices_t profile={0,500,100,200};
bool vdc_run_output_get_slices(vdc_run_output_slices_t *out) { *out=profile;return true; }
bool vdc_run_output_set_slices_enabled(bool enabled) { ++changes;profile.enabled=enabled;return true; }
bool vdc_run_output_configure_slices(uint32_t interval,uint32_t high,uint32_t marker)
{ ++changes;profile.interval_ns=interval;profile.high_ns=high;profile.marker_ns=marker;return true; }
''' + helpers + callbacks + r'''
int main(int argc,char **argv) {
    assert(argc==3);
    scpi_command_t commands[]={
        {.pattern="SYSTem:VDC:OUTPut:SUBDivision:AFTER:ENABle",.callback=scpi_cmd_vdc_subdivision_after_sync},
        {.pattern="SYSTem:VDC:OUTPut:SUBDivision:AFTER:STATus?",.callback=scpi_cmd_vdc_subdivision_after_sync_q},
        {.pattern="SYSTem:VDC:OUTPut:SUBDivision:AFTER:CONFigure",.callback=scpi_cmd_vdc_subdivision_after_sync_configure},
        SCPI_CMD_LIST_END};
    char input[1024],line[1024];scpi_t context;scpi_error_t errors[16];
    scpi_interface_t interface={.write=output,.flush=flush,.error=error};
    SCPI_Init(&context,commands,&interface,scpi_units_def,"a","b","c","d",input,sizeof(input),errors,16);
    int length=snprintf(line,sizeof(line),"%s\n",argv[1]);
    SCPI_Input(&context,line,length);
    if(!strcmp(argv[2],"reject"))assert(SCPI_ErrorCount(&context) && !changes);
    else { assert(!SCPI_ErrorCount(&context)); printf("%s",response); }
    return 0;
}
'''
    return compile_host(tmp_path_factory.mktemp('slice-parser'), 'parser', code,
        sorted((ROOT/'third_party/scpi-parser/libscpi/src').glob('*.c')), parser=True)

@pytest.mark.parametrize('command,reply', [
    ('ENAB 0','0'),('ENAB 1','1'),('STAT?','0,500,100,200'),
    ('CONF 500,100,200','500,100,200'),
])
def test_read_write(parser,command,reply):
    r=subprocess.run([str(parser),'SYST:VDC:OUTP:SUBD:AFTER:'+command,'accept'],capture_output=True,text=True)
    assert r.returncode==0,r.stderr
    assert r.stdout.strip()==reply

@pytest.mark.parametrize('command', ['ENAB','ENAB 2','ENAB -1','ENAB 1,0',
    'ENAB 1.0','ENAB 1e0','CONF 500,100','CONF 500,100,200,4','STAT? 1'])
def test_no_partial_mutation(parser,command):
    r=subprocess.run([str(parser),'SYST:VDC:OUTP:SUBD:AFTER:'+command,'reject'],capture_output=True,text=True)
    assert r.returncode==0,r.stderr
