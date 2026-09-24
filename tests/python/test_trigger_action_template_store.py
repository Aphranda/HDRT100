"""STOP-only static action-template owner."""
import subprocess
import pytest
from test_trigger_grid_preview import ROOT, compile_grid

HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "trigger_action_template_store.h"
#include "trigger_deployment_service.h"
#include "tdma_runtime_owner.h"
static trigger_deployment_snapshot_t dep; static bool stopped=true; static unsigned core;
static unsigned switch_loads,switch_unloads;
unsigned get_core_num(void){return core;}
tdma_service_service_t *tdma_runtime_owner_get(void){return NULL;}
uint32_t trigger_deployment_snapshot(trigger_deployment_snapshot_t *o){*o=dep;return 0;}
bool tdma_service_update_stopped_metadata(tdma_service_service_t*s,bool(*f)(void*),void*c){(void)s;return stopped&&f(c);}
uint32_t trigger_counter_load(uint32_t g,uint32_t c){(void)g;(void)c;return 0;}
uint32_t trigger_counter_unload(void){return 0;}
uint32_t trigger_switch_load(uint32_t g,uint32_t c){(void)g;(void)c;switch_loads++;return 0;}
uint32_t trigger_switch_unload(void){switch_unloads++;return 0;}
uint32_t trigger_gateway_load(uint32_t g,uint32_t c,uint32_t p){(void)g;(void)c;(void)p;return 0;}
uint32_t trigger_gateway_unload(void){return 0;}
static void setup(void){memset(&dep,0,sizeof(dep));dep.active.valid=1;dep.active.table.generation=2;dep.active.table.rows[0].board_uid=1;dep.active.table.rows[0].tdma_slot=0;dep.active.table.rows[0].input_mask=1;dep.active.table.rows[1].board_uid=2;dep.active.table.rows[1].tdma_slot=1;dep.active.table.rows[1].output_mask=7;dep.active.table.rows[2].board_uid=3;dep.active.table.rows[2].tdma_slot=2;dep.active.table.rows[2].input_mask=1;dep.active.table.rows[2].output_mask=8;dep.active.table.rows[2].feedback_mode=1;dep.active.table.rows[3].board_uid=4;dep.active.table.rows[3].tdma_slot=3;dep.active.table.rows[3].output_mask=1;}
static trigger_action_template_table_t table(uint32_t g){return (trigger_action_template_table_t){.generation=g,.count=1,.row={{.template_id=4,.generation=g,.role=1,.kind=1,.argument=3,.span_slices=2,.delta_q=-2}}};}
int main(int argc,char**argv){assert(argc==2);setup();trigger_action_template_table_t t=table(3);uint32_t crc=trigger_action_template_table_crc(&t);if(!strcmp(argv[1],"install")){assert(trigger_action_template_install(&t,crc)==0);trigger_action_template_store_snapshot_t s;assert(trigger_action_template_snapshot(&s)==0&&s.installed&&s.generation==3);return 0;}if(!strcmp(argv[1],"busy")){stopped=false;assert(trigger_action_template_install(&t,crc)!=0);return 0;}if(!strcmp(argv[1],"idempotent")){assert(trigger_action_template_install(&t,crc)==0&&trigger_action_template_install(&t,crc)==0);return 0;}if(!strcmp(argv[1],"activate")){dep.active.local_role=1;assert(trigger_action_template_install(&t,crc)==0&&trigger_action_template_activate_resources()==0&&switch_loads==1&&trigger_action_template_release_resources()==0&&switch_unloads==1);return 0;}if(!strcmp(argv[1],"clear")){assert(trigger_action_template_install(&t,crc)==0&&trigger_action_template_clear()==0);t.generation=3;assert(trigger_action_template_install(&t,trigger_action_template_table_crc(&t))!=0);return 0;}return 2;}
'''

@pytest.fixture(scope="module")
def store(tmp_path_factory):
    d=tmp_path_factory.mktemp("template-store")
    (d/"board_config.h").write_text("#define BOARD_SYS_CLOCK_HZ 250000000u\n",encoding="utf-8")
    (d/"pico.h").write_text("unsigned get_core_num(void);\n",encoding="utf-8")
    (d/"tdma_runtime_owner.h").write_text(
        "#include \"tdma_service.h\"\n"
        "tdma_service_service_t *tdma_runtime_owner_get(void);\n",
        encoding="utf-8")
    return compile_grid(d,"store",HARNESS,[ROOT/'components/sync_trigger/src/trigger_action_template_store.c'])

@pytest.mark.parametrize("case",["install","busy","idempotent","activate","clear"])
def test_store(store,case):
    r=subprocess.run([str(store),case],capture_output=True,text=True,timeout=5)
    assert r.returncode==0,r.stdout+r.stderr
