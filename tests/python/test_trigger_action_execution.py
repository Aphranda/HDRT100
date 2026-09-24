"""Actual execution owner C harness: VDC inverse, identity and backend gates."""
import subprocess
import pytest
from test_trigger_grid_preview import ROOT, compile_grid


@pytest.fixture(scope='module')
def owner(tmp_path_factory):
    d=tmp_path_factory.mktemp('action-exec');(d/'pico.h').write_text('unsigned get_core_num(void);\n',encoding='utf-8')
    (d/'board_config.h').write_text('#define BOARD_SYS_CLOCK_HZ 250000000u\n',encoding='utf-8')
    (d/'board_identity.h').write_text('const char *board_identity_serial(void);\n',encoding='utf-8')
    (d/'tdma_runtime_owner.h').write_text('#include "tdma_service.h"\n'
      'tdma_service_service_t *tdma_runtime_owner_get(void);\n'
      'bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *out);\n'
      'bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *out);\n',encoding='utf-8')
    return compile_grid(d,'owner',HARNESS,[ROOT/'components/sync_trigger/src/trigger_action_service.c'])


@pytest.mark.parametrize('case',['prepare_arm_release','prepare_no_row','late','model_change','identity_change',
    'session_change','deadline_bracket','stop_cancel','release_required','repeat_consumed','clear_newgen'])
def test_actual_execution_owner(owner,case):
    r=subprocess.run([str(owner),case],capture_output=True,text=True,timeout=5)
    assert r.returncode==0,r.stdout+r.stderr


HARNESS=r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "trigger_action_service.h"
#include "tdma_runtime_owner.h"
static unsigned core,depth;static bool model_ok=true,clock_ok=true,metadata=true,io_loaded,arm_ok=true,stop_ok=true,release_ok=true;
static uint32_t session=9,model_token=17,model_seq=3;static uint64_t raw=UINT64_C(1000000000);
static const char *serial="0000000000000002";
static tdma_ring_clock_snapshot_t ring={.config_seq=7,.applied_config_seq=7};
static tdma_service_ring_runtime_config_t staged={.enabled=1,.local_slot_id=1,.schedule_crc32=123};
static trigger_deployment_snapshot_t dep;
static sync_io_action_snapshot_t io;
unsigned get_core_num(void){return core;} const char *board_identity_serial(void){return serial;}
tdma_service_service_t *tdma_runtime_owner_get(void){return NULL;}
bool tdma_runtime_owner_get_ring_clock_snapshot(tdma_ring_clock_snapshot_t *o){*o=ring;return true;}
bool tdma_runtime_owner_get_staged_ring_config(tdma_service_ring_runtime_config_t *o){*o=staged;return true;}
uint32_t trigger_deployment_snapshot(trigger_deployment_snapshot_t *o){*o=dep;return dep.active.valid?0:4;}
bool tdma_service_run_stopped_maintenance(tdma_service_service_t*s,bool(*f)(void*),void*c){(void)s;if(!metadata||ring.enabled)return false;assert(!depth);++depth;bool x=f(c);--depth;return x;}
bool tdma_service_update_stopped_metadata(tdma_service_service_t*s,bool(*f)(void*),void*c){(void)s;if(!metadata||ring.enabled)return false;assert(!depth);++depth;bool x=f(c);--depth;return x;}
uint32_t vdc_dpll_manager_feedback_session(void){return session;}
bool vdc_dpll_manager_get_committed_model(vdc_dpll_manager_committed_model_t*o){if(!model_ok)return false;memset(o,0,sizeof(*o));o->token=model_token;o->session=session;o->role_generation=1;o->clock_epoch_id=2;o->clock_run_id=3;o->local_slot=1;o->valid_from_raw=UINT64_C(999900000);o->dco.valid=1;o->dco.dco_update_seq=model_seq;o->dco.nominal_period_ns=1000000;o->dco.period_adjust_ppb=0;o->dco.lock_state=5;o->dco.tdma_schedule_crc32=123;o->dco.base_local_tick64=0;o->dco.base_vdc_time64_ns=0;return true;}
bool vdc_timestamp_clock_try_read_ticks64(uint32_t hz,uint64_t*o){(void)hz;if(!clock_ok)return false;*o=raw;raw+=20;return true;}
bool vdc_domain_dco_local_to_output_ns(const vdc_dco_control_t*d,uint64_t n,uint64_t*o){(void)d;*o=n*4;return true;}
bool vdc_domain_dco_output_to_local_ns(const vdc_dco_control_t*d,uint64_t n,uint64_t*o){(void)d;*o=n/4;return true;}
bool sync_io_action_load(const sync_io_action_config_t*c,uint32_t*g){assert(depth==1);assert(c->kind==1&&c->output_mask==7&&c->code==3);io=(sync_io_action_snapshot_t){.schema=1,.generation=4,.state=1,.loaded=1,.config=*c,.tick_hz=250000000};*g=4;io_loaded=true;return true;}
bool sync_io_action_arm_checked(uint32_t g,uint64_t d,bool(*f)(void*),void*c){assert(depth==1&&io_loaded&&g==4);if(!f(c))return false;if(!arm_ok)return false;io.state=2;io.deadline_tick=d;io.enable_before_tick=raw;io.enable_after_tick=raw+3;return true;}
bool sync_io_action_stop(uint32_t g){assert(depth==1&&g==4);if(!stop_ok)return false;io.state=4;return true;}
bool sync_io_action_release(uint32_t g){assert(depth==1&&g==4);if(!release_ok)return false;io.loaded=0;io.state=0;io_loaded=false;return true;}
bool sync_io_action_snapshot(sync_io_action_snapshot_t*o){*o=io;return true;}
static void setup(void){dep.active=(trigger_deployment_binding_t){.valid=1,.local_uid=2,.local_role=1,.local_slot=1,.ring_config=7,.schedule_crc=123,.table={.generation=21,.rows={{.board_uid=1,.tdma_slot=0,.input_mask=1},{.board_uid=2,.tdma_slot=1,.output_mask=7},{.board_uid=3,.tdma_slot=2,.input_mask=1,.output_mask=8,.feedback_mode=1},{.board_uid=4,.tdma_slot=3,.output_mask=1}}}};dep.active.crc=trigger_deployment_crc(&dep.active.table);}
static trigger_action_table_t table(uint32_t role,uint32_t argument){trigger_action_table_t t={.generation=21,.dep_generation=21,.dep_crc=0,.grid={.epoch=5,.generation=6,.anchor_vdc_ns=UINT64_C(20000000000),.period_ns=1000000,.subdivisions=2000},.count=1,.rows={{.action_id=1,.role=role,.kind=1,.argument=argument,.ordinal=400010,.span_slices=2}}};t.dep_crc=dep.active.crc;return t;}
int main(int argc,char**argv){assert(argc==2);const char*n=argv[1];setup();const bool no_row=!strcmp(n,"prepare_no_row")||!strcmp(n,"model_change");trigger_action_table_t t=table(no_row?3u:1u,no_row?1u:3u);{uint32_t z=trigger_action_install(&t,trigger_action_table_crc(&t));fprintf(stderr,"z=%u dcrc=%u tdep=%u\n",z,dep.active.crc,t.dep_generation);assert(!z);}
 if(!strcmp(n,"prepare_no_row")){assert(trigger_action_execution_prepare(0)==TRIGGER_ACTION_EXECUTION_NOT_LOCAL);return 0;}
 if(!strcmp(n,"late")){raw=UINT64_C(1000000000000);assert(trigger_action_execution_prepare(0)==TRIGGER_ACTION_EXECUTION_CLOCK||trigger_action_execution_prepare(0)==TRIGGER_TIME_GRID_LATE);return 0;}
 if(!strcmp(n,"model_change")){assert(trigger_action_execution_prepare(0)==TRIGGER_ACTION_EXECUTION_NOT_LOCAL);return 0;}
 /* role1 row is local; all remaining cases exercise real prepare/ARM gates. */
  if(!strcmp(n,"clear_newgen")){t.generation=22;assert(!trigger_action_clear());uint32_t install_result=trigger_action_install(&t,trigger_action_table_crc(&t)); if(install_result) fprintf(stderr,"install=%u crc=%u dep=%u\\n",install_result,trigger_action_table_crc(&t),dep.active.crc); assert(!install_result);}
 if(!strcmp(n,"identity_change")){serial="000000000000000F";assert(trigger_action_execution_prepare(0)!=0);return 0;}
 if(!strcmp(n,"session_change")){uint32_t p=trigger_action_execution_prepare(0);fprintf(stderr,"p=%u\\n",p);assert(!p);session=10;assert(trigger_action_execution_arm()!=0);return 0;}
  if(!strcmp(n,"deadline_bracket")){uint32_t p=trigger_action_execution_prepare(0),a=trigger_action_execution_arm();fprintf(stderr,"p=%u a=%u\\n",p,a);assert(!p&&!a);assert(io.enable_after_tick>=io.enable_before_tick);assert(io.deadline_tick>io.enable_before_tick);return 0;}
  if(!strcmp(n,"stop_cancel")){uint32_t p=trigger_action_execution_prepare(0),s=trigger_action_execution_stop(),r=trigger_action_execution_release();fprintf(stderr,"p=%u s=%u r=%u\\n",p,s,r);assert(!p&&!s&&!r);return 0;}
  if(!strcmp(n,"release_required")){uint32_t p=trigger_action_execution_prepare(0),r=trigger_action_execution_release();fprintf(stderr,"p=%u r=%u\\n",p,r);assert(!p);assert(!r);assert(!trigger_action_execution_is_loaded());return 0;}
  if(!strcmp(n,"repeat_consumed")){uint32_t p=trigger_action_execution_prepare(0),a=trigger_action_execution_arm();fprintf(stderr,"p=%u a=%u\\n",p,a);assert(!p&&!a);assert(trigger_action_execution_prepare(0)!=0);return 0;}
  if(!strcmp(n,"clear_newgen")){uint32_t p=trigger_action_execution_prepare(0),s=trigger_action_execution_stop(),r=trigger_action_execution_release();fprintf(stderr,"p=%u s=%u r=%u\\n",p,s,r);assert(!p&&!s&&!r);return 0;}
  assert(!strcmp(n,"prepare_arm_release"));{uint32_t p=trigger_action_execution_prepare(0),a=trigger_action_execution_arm(),s=trigger_action_execution_stop(),r=trigger_action_execution_release();fprintf(stderr,"p=%u a=%u s=%u r=%u\\n",p,a,s,r);assert(!p&&!a&&!s&&!r);}return 0;}
'''







