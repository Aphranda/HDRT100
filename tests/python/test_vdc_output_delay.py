"""Real output-delay persistence and service/SCPI callback bodies on the host."""
from pathlib import Path
import json
import os
import shutil
import subprocess

ROOT=next(p for p in Path(__file__).resolve().parents if (p/'AGENTS.md').exists())
# Allows the isolated candidate to run before production integration.
CANDIDATE=Path(os.environ.get('HDRT_OUTPUT_DELAY_CANDIDATE_ROOT',ROOT))

def test_output_delay_c_host(tmp_path):
    cc=shutil.which('gcc') or shutil.which('clang')
    assert cc
    source=(CANDIDATE/'middleware/scpi_port/src/scpi_sync_commands.c').read_text(encoding='utf-8')
    header=(CANDIDATE/'middleware/scpi_port/inc/scpi_system_snapshot_commands.h').read_text(encoding='utf-8')
    bodies=[]
    for suffix,name in zip(('', '?', ':DEFAult', ':RECall', ':STORe'),
        ('scpi_cmd_vdc_output_delay','scpi_cmd_vdc_output_delay_q','scpi_cmd_vdc_output_delay_default',
         'scpi_cmd_vdc_output_delay_recall','scpi_cmd_vdc_output_delay_store')):
        assert header.count(f'.pattern = "SYSTem:VDC:OUTPut:DELay{suffix}", .callback = {name}')==1
        start=source.index('scpi_result_t '+name+'(')
        opening=source.index('{',start);depth=1;end=opening+1
        while depth:
            depth+=(source[end]=='{')-(source[end]=='}');end+=1
        bodies.append(source[start:end])
    (tmp_path/'scpi_callbacks.inc').write_text('\n'.join(bodies),encoding='utf-8')
    includes=[tmp_path,CANDIDATE,CANDIDATE/'components/product_config/inc',
        CANDIDATE/'components/vdc_dpll_manager/inc',ROOT,ROOT/'components/flash_transaction/inc',
        ROOT/'components/ota_manager/inc',ROOT/'drivers/mcu/flash/inc',ROOT/'config']
    log=[]
    for name in ('persistence','service_scpi'):
        unit=CANDIDATE/f'tests/unit/test_output_delay_{name}.c'
        if not unit.exists():unit=CANDIDATE/f'test_output_delay_{name}.c'
        # Copy the harness so its quoted callback include resolves to extracted real bodies.
        copy=tmp_path/unit.name;copy.write_text(unit.read_text(encoding='utf-8'),encoding='utf-8')
        sources=[copy]
        if name=='persistence':sources.append(CANDIDATE/'components/product_config/src/product_config.c')
        exe=tmp_path/(name+'.exe')
        command=[cc,'-std=c11','-O2','-Wall','-Wextra','-Werror',*[f'-I{p}' for p in includes],*map(str,sources),'-o',str(exe)]
        compiled=subprocess.run(command,capture_output=True,text=True,timeout=60)
        entry={'command':command,'compile_exit':compiled.returncode,'compile_stdout':compiled.stdout,'compile_stderr':compiled.stderr}
        log.append(entry)
        (tmp_path/'results.json').write_text(json.dumps(log,indent=2),encoding='utf-8')
        assert compiled.returncode==0,compiled.stdout+compiled.stderr
        result=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
        entry.update(run_exit=result.returncode,stdout=result.stdout,stderr=result.stderr)
        (tmp_path/'results.json').write_text(json.dumps(log,indent=2),encoding='utf-8')
        assert result.returncode==0,result.stdout+result.stderr
