#include "trigger_action_service.h"
#include <string.h>
#include "board_config.h"
#include "board_identity.h"
#include "pico.h"
#include "tdma_runtime_owner.h"
#include "vdc_timestamp_clock.h"

static uint32_t s_action_busy;
static trigger_action_snapshot_t s_action;
static trigger_action_execution_snapshot_t s_execution;
static uint32_t s_execution_loaded,s_consumed_table_generation,s_consumed_row_mask;
bool trigger_action_execution_is_loaded(void)
{return __atomic_load_n(&s_execution_loaded,__ATOMIC_ACQUIRE)!=0u;}
static bool enter(void)
{
    uint32_t idle=0u;
    return get_core_num()==0u && __atomic_compare_exchange_n(&s_action_busy,&idle,
        1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
}
static void leave(void){__atomic_store_n(&s_action_busy,0u,__ATOMIC_RELEASE);}
static bool local_uid(uint64_t *out)
{
    const char *p=board_identity_serial();uint64_t uid=0u;
    if(!p)return false;
    for(uint32_t i=0u;i<16u;++i){
        const char c=p[i];uint32_t n;
        if(c>='0'&&c<='9')n=(uint32_t)(c-'0');
        else if(c>='A'&&c<='F')n=(uint32_t)(c-'A'+10);
        else if(c>='a'&&c<='f')n=(uint32_t)(c-'a'+10);
        else return false;
        uid=(uid<<4u)|n;
    }
    if(p[16]||!uid)return false;
    *out=uid;return true;
}
static bool deployment_equal(const trigger_deployment_t *a,const trigger_deployment_t *b)
{
    if(a->generation!=b->generation)return false;
    for(uint32_t i=0u;i<TRIGGER_DEPLOYMENT_ROLE_COUNT;++i){
        const trigger_deployment_row_t *x=&a->rows[i],*y=&b->rows[i];
        if(x->board_uid!=y->board_uid || x->tdma_slot!=y->tdma_slot ||
            x->input_mask!=y->input_mask || x->output_mask!=y->output_mask ||
            x->feedback_mode!=y->feedback_mode)return false;
    }
    return true;
}
static bool binding_equal(const trigger_deployment_binding_t *a,const trigger_deployment_binding_t *b)
{
    return a->valid==b->valid && a->crc==b->crc && a->local_uid==b->local_uid &&
        a->local_role==b->local_role && a->local_slot==b->local_slot &&
        a->ring_config==b->ring_config && a->schedule_crc==b->schedule_crc &&
        deployment_equal(&a->table,&b->table);
}
/* Called under the TDMA STOP/control guard, so no Core0 ARM/configure can
 * interleave the retained staged identity and SRAM publication. */
static bool current_binding(trigger_deployment_binding_t *out)
{
    trigger_deployment_snapshot_t dep;tdma_ring_clock_snapshot_t ring;
    tdma_service_ring_runtime_config_t staged;uint64_t uid;
    if(trigger_deployment_snapshot(&dep) || !dep.active.valid || !local_uid(&uid) ||
        !tdma_runtime_owner_get_ring_clock_snapshot(&ring) ||
        !tdma_runtime_owner_get_staged_ring_config(&staged))return false;
    const trigger_deployment_binding_t *b=&dep.active;
    if(ring.enabled || ring.adapter_started || ring.config_seq!=ring.applied_config_seq ||
        !staged.enabled || ring.config_seq!=b->ring_config || uid!=b->local_uid ||
        staged.local_slot_id!=b->local_slot || staged.schedule_crc32!=b->schedule_crc ||
        trigger_deployment_crc(&b->table)!=b->crc ||
        (ring.schedule_crc32 && (ring.schedule_crc32!=staged.schedule_crc32 ||
            ring.local_slot_id!=staged.local_slot_id)))return false;
    trigger_deployment_row_t row;
    if(trigger_deployment_resolve(&b->table,b->local_role,uid,staged.local_slot_id,
            PROJECT_NODE_CAPACITY,&row))return false;
    *out=*b;return true;
}
typedef struct {
    const trigger_action_table_t *table;
    trigger_action_snapshot_t *snapshot;
    trigger_action_row_t *row;
    uint64_t *target;
    uint32_t crc,index,result;
} action_request_t;
static bool install_stopped(void *context)
{
    action_request_t *r=context;trigger_deployment_binding_t binding;
    if(trigger_action_execution_is_loaded()){r->result=TRIGGER_ACTION_SERVICE_BUSY;return false;}
    r->result=TRIGGER_ACTION_SERVICE_CHANGED;
    if(!current_binding(&binding))return false;
    r->result=trigger_action_table_validate(r->table,&binding.table,BOARD_SYS_CLOCK_HZ);
    if(r->result)return false;
    if(r->table->generation<=s_action.last_generation){
        const bool same=s_action.installed && trigger_action_table_equal(r->table,&s_action.table) &&
            binding_equal(&binding,&s_action.binding);
        r->result=same?TRIGGER_ACTION_TABLE_OK:TRIGGER_ACTION_SERVICE_GENERATION;return same;
    }
    trigger_action_snapshot_t candidate={.binding=binding,.table=*r->table,
        .crc=r->crc,.last_generation=r->table->generation,.installed=1u,.binding_current=1u,
        .state=TRIGGER_ACTION_SERVICE_INSTALLED_CONFIG};
    for(uint32_t i=0u;i<r->table->count;++i)
        if(r->table->rows[i].role==binding.local_role)candidate.local_row_mask|=1u<<i;
    s_action=candidate;r->result=TRIGGER_ACTION_TABLE_OK;return true;
}
static bool clear_stopped(void *context)
{
    action_request_t *r=context;const uint32_t high=s_action.last_generation;
    if(trigger_action_execution_is_loaded()){r->result=TRIGGER_ACTION_SERVICE_BUSY;return false;}
    memset(&s_action,0,sizeof(s_action));s_action.last_generation=high;
    r->result=TRIGGER_ACTION_TABLE_OK;return true;
}
static bool snapshot_stopped(void *context)
{
    action_request_t *r=context;trigger_action_snapshot_t candidate=s_action;
    trigger_deployment_binding_t current;
    candidate.binding_current=candidate.installed && current_binding(&current) &&
        binding_equal(&candidate.binding,&current);
    *r->snapshot=candidate;r->result=TRIGGER_ACTION_TABLE_OK;return true;
}
static bool row_stopped(void *context)
{
    action_request_t *r=context;trigger_deployment_binding_t current;
    r->result=TRIGGER_ACTION_SERVICE_UNCONFIGURED;if(!s_action.installed)return false;
    r->result=TRIGGER_ACTION_SERVICE_CHANGED;
    if(!current_binding(&current) || !binding_equal(&current,&s_action.binding))return false;
    uint64_t target;r->result=trigger_action_table_target(&s_action.table,r->index,&target);
    if(r->result)return false;
    *r->row=s_action.table.rows[r->index];*r->target=target;return true;
}
static uint32_t operate(bool(*callback)(void *),action_request_t *request)
{
    if(!enter())return TRIGGER_ACTION_SERVICE_BUSY;
    request->result=TRIGGER_ACTION_SERVICE_NOT_IDLE;
    (void)tdma_service_update_stopped_metadata(tdma_runtime_owner_get(),callback,request);
    leave();return request->result;
}
uint32_t trigger_action_install(const trigger_action_table_t *table,uint32_t expected_crc)
{
    if(!table || !table->generation || !table->count || table->count>TRIGGER_ACTION_TABLE_CAPACITY)
        return TRIGGER_ACTION_TABLE_ARGUMENT;
    if(trigger_action_table_crc(table)!=expected_crc)return TRIGGER_ACTION_SERVICE_DIGEST;
    action_request_t request={.table=table,.crc=expected_crc};return operate(install_stopped,&request);
}
uint32_t trigger_action_clear(void)
{action_request_t request={0};return operate(clear_stopped,&request);}
uint32_t trigger_action_snapshot(trigger_action_snapshot_t *out)
{
    if(!out)return TRIGGER_ACTION_TABLE_ARGUMENT;
    action_request_t request={.snapshot=out};return operate(snapshot_stopped,&request);
}
uint32_t trigger_action_row(uint32_t index,trigger_action_row_t *row,uint64_t *target_vdc_ns)
{
    if(!row || !target_vdc_ns)return TRIGGER_ACTION_TABLE_ARGUMENT;
    action_request_t request={.row=row,.target=target_vdc_ns,.index=index};return operate(row_stopped,&request);
}

static bool execution_model(vdc_dpll_manager_committed_model_t *out,
    const trigger_deployment_binding_t *binding)
{
    const uint32_t session=vdc_dpll_manager_feedback_session();
    vdc_dpll_manager_committed_model_t model;
    if(!session || !vdc_dpll_manager_get_committed_model(&model) ||
        session!=vdc_dpll_manager_feedback_session() || model.session!=session ||
        !model.token || !model.dco.valid || !model.dco.dco_update_seq ||
        !model.dco.nominal_period_ns || model.dco.period_adjust_ppb<=-1000000000 ||
        model.dco.lock_state>VDC_DOMAIN_LOCK_FAULT ||
        model.local_slot!=binding->local_slot || model.dco.tdma_schedule_crc32!=binding->schedule_crc)
        return false;
    *out=model;return true;
}
static bool model_equal(const vdc_dpll_manager_committed_model_t *a,
    const vdc_dpll_manager_committed_model_t *b)
{
    /* Publisher fills an immutable complete model; publication guard excludes
     * torn copies. Epoch/run, origin, rate, phase and applied sequence all bind. */
    return memcmp(a,b,sizeof(*a))==0;
}
typedef struct {
    uint32_t operation,index,result;
    trigger_action_now_t *now;
    trigger_action_execution_snapshot_t *snapshot;
} execution_request_t;
enum {EXECUTION_PREPARE,EXECUTION_ARM,EXECUTION_STOP,EXECUTION_RELEASE,
    EXECUTION_SNAPSHOT,EXECUTION_NOW};

static bool execution_now_stopped(execution_request_t *r)
{
    trigger_deployment_binding_t binding,after_binding;
    trigger_action_now_t candidate={.tick_hz=BOARD_SYS_CLOCK_HZ,.diagnostic=1u};
    vdc_dpll_manager_committed_model_t after;
    r->result=TRIGGER_ACTION_SERVICE_CHANGED;
    if(!current_binding(&binding))return false;
    r->result=TRIGGER_ACTION_EXECUTION_MODEL;
    if(!execution_model(&candidate.model,&binding))return false;
    r->result=TRIGGER_ACTION_EXECUTION_CLOCK;
    if(!vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ,&candidate.raw_before_tick) ||
        candidate.raw_before_tick<candidate.model.valid_from_raw)return false;
    uint64_t local;
    if(!vdc_timer1_ticks_to_ns(BOARD_SYS_CLOCK_HZ,candidate.raw_before_tick,false,&local) ||
        local<candidate.model.dco.base_local_tick64 ||
        !vdc_domain_dco_local_to_output_ns(&candidate.model.dco,local,&candidate.vdc_before_ns) ||
        !vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ,&candidate.raw_after_tick) ||
        candidate.raw_after_tick<candidate.raw_before_tick ||
        !vdc_timer1_ticks_to_ns(BOARD_SYS_CLOCK_HZ,candidate.raw_after_tick,true,&local) ||
        !vdc_domain_dco_local_to_output_ns(&candidate.model.dco,local,&candidate.vdc_after_ns))return false;
    r->result=TRIGGER_ACTION_SERVICE_CHANGED;
    if(!current_binding(&after_binding) || !binding_equal(&binding,&after_binding) ||
        !execution_model(&after,&binding) || !model_equal(&candidate.model,&after))return false;
    *r->now=candidate;r->result=TRIGGER_ACTION_TABLE_OK;return true;
}

static uint32_t execution_revalidate(uint64_t *checked)
{
    if(!s_action.installed || !trigger_action_table_equal(&s_execution.installation.table,&s_action.table) ||
        s_action.crc!=s_execution.installation.crc)return TRIGGER_ACTION_SERVICE_CHANGED;
    trigger_deployment_binding_t current;
    if(!current_binding(&current) || !binding_equal(&current,&s_execution.installation.binding))
        return TRIGGER_ACTION_SERVICE_CHANGED;
    vdc_dpll_manager_committed_model_t model;
    if(!execution_model(&model,&current) || !model_equal(&model,&s_execution.model))
        return TRIGGER_ACTION_EXECUTION_MODEL;
    uint64_t now;
    if(!vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ,&now) ||
        now<s_execution.prepared_at_tick || now<model.valid_from_raw)
        return TRIGGER_ACTION_EXECUTION_CLOCK;
    if(now>s_execution.expires_at_tick)return TRIGGER_ACTION_EXECUTION_EXPIRED;
    const uint64_t lead=(uint64_t)BOARD_SYS_CLOCK_HZ/10u;
    if(s_execution.plan.deadline_ticks<now || s_execution.plan.deadline_ticks-now<lead)
        return TRIGGER_ACTION_EXECUTION_LATE;
    *checked=now;return TRIGGER_ACTION_TABLE_OK;
}
static bool execution_arm_validate(void *context)
{
    execution_request_t *r=context;
    r->result=execution_revalidate(&s_execution.arm_checked_tick);
    return r->result==TRIGGER_ACTION_TABLE_OK;
}

static bool execution_prepare_stopped(execution_request_t *r)
{
    if(trigger_action_execution_is_loaded()){r->result=TRIGGER_ACTION_SERVICE_BUSY;return false;}
    r->result=TRIGGER_ACTION_SERVICE_UNCONFIGURED;
    if(!s_action.installed)return false;
    r->result=TRIGGER_ACTION_TABLE_ARGUMENT;
    if(r->index>=s_action.table.count)return false;
    r->result=TRIGGER_ACTION_EXECUTION_NOT_LOCAL;
    /* First executor slice owns exactly one row on each participating board. */
    if(s_action.local_row_mask!=(1u<<r->index))return false;
    r->result=TRIGGER_ACTION_EXECUTION_ALREADY_EXECUTED;
    if(s_action.table.generation==s_consumed_table_generation && (s_consumed_row_mask&(1u<<r->index)))
        return false;
    trigger_action_execution_snapshot_t candidate={.installation=s_action,
        .row_index=r->index,.diagnostic=1u};
    trigger_deployment_binding_t current,after_binding;
    r->result=TRIGGER_ACTION_SERVICE_CHANGED;
    if(!current_binding(&current) || !binding_equal(&current,&s_action.binding))return false;
    r->result=trigger_action_table_validate(&s_action.table,&current.table,BOARD_SYS_CLOCK_HZ);
    if(r->result)return false;
    r->result=TRIGGER_ACTION_EXECUTION_MODEL;
    if(!execution_model(&candidate.model,&current))return false;
    r->result=TRIGGER_ACTION_EXECUTION_CLOCK;
    if(!vdc_timestamp_clock_try_read_ticks64(BOARD_SYS_CLOCK_HZ,&candidate.prepared_at_tick) ||
        candidate.prepared_at_tick<candidate.model.valid_from_raw)return false;
    uint64_t local;
    if(!vdc_timer1_ticks_to_ns(BOARD_SYS_CLOCK_HZ,candidate.prepared_at_tick,false,&local) ||
        local<candidate.model.dco.base_local_tick64)return false;
    const uint64_t horizon=(uint64_t)BOARD_SYS_CLOCK_HZ*12u;
    if(candidate.prepared_at_tick>UINT64_MAX-horizon)return false;
    candidate.expires_at_tick=candidate.prepared_at_tick+horizon;
    const trigger_action_row_t *row=&s_action.table.rows[r->index];
    r->result=trigger_time_grid_prepare(&s_action.table.grid,row->ordinal,&candidate.model.dco,
        BOARD_SYS_CLOCK_HZ,candidate.prepared_at_tick,(uint64_t)BOARD_SYS_CLOCK_HZ/10u,
        horizon,&candidate.plan);
    if(r->result)return false;
    const trigger_deployment_row_t *route=&current.table.rows[row->role];
    sync_io_action_config_t config={.kind=SYNC_IO_ACTION_SWITCH,.output_mask=route->output_mask,
        .code=row->argument};
    if(row->kind==TRIGGER_ACTION_GATEWAY_PULSE){
        config.kind=SYNC_IO_ACTION_GATEWAY;config.code=route->output_mask;
        config.pulse_ns=row->argument;config.input_port=1u;
        while(config.input_port<=4u && route->input_mask!=(1u<<(config.input_port-1u)))++config.input_port;
        if(config.input_port>4u){r->result=TRIGGER_ACTION_EXECUTION_HARDWARE;return false;}
    }
    vdc_dpll_manager_committed_model_t after;
    r->result=TRIGGER_ACTION_SERVICE_CHANGED;
    if(!current_binding(&after_binding) || !binding_equal(&current,&after_binding) ||
        !execution_model(&after,&current) || !model_equal(&candidate.model,&after))return false;
    uint32_t generation;
    r->result=TRIGGER_ACTION_EXECUTION_HARDWARE;
    if(!sync_io_action_load(&config,&generation))return false;
    /* LOAD enables nothing. Even if later reads contend, retain ownership so
     * the caller can safely STOP/RELEASE; ARM always does a fresh validation. */
    candidate.io=(sync_io_action_snapshot_t){.generation=generation,.config=config,
        .loaded=1u,.state=SYNC_IO_ACTION_LOADED,.tick_hz=BOARD_SYS_CLOCK_HZ};
    candidate.loaded=1u;candidate.binding_current=1u;candidate.model_current=1u;
    s_execution=candidate;
    __atomic_store_n(&s_execution_loaded,1u,__ATOMIC_RELEASE);
    r->result=TRIGGER_ACTION_TABLE_OK;return true;
}

static bool execution_maintain(void *context)
{
    execution_request_t *r=context;
    if(r->operation==EXECUTION_NOW)return execution_now_stopped(r);
    if(r->operation==EXECUTION_PREPARE)return execution_prepare_stopped(r);
    if(r->operation==EXECUTION_SNAPSHOT){
        trigger_action_execution_snapshot_t candidate=s_execution;
        candidate.loaded=trigger_action_execution_is_loaded()?1u:0u;
        candidate.diagnostic=1u;
        candidate.consumed_table_generation=s_consumed_table_generation;
        candidate.consumed_row_mask=s_consumed_row_mask;
        trigger_deployment_binding_t current;
        candidate.binding_current=candidate.loaded && current_binding(&current) &&
            binding_equal(&current,&candidate.installation.binding) && s_action.installed &&
            trigger_action_table_equal(&s_action.table,&candidate.installation.table);
        vdc_dpll_manager_committed_model_t model;
        candidate.model_current=candidate.binding_current && execution_model(&model,&current) &&
            model_equal(&model,&candidate.model);
        r->result=TRIGGER_ACTION_SERVICE_BUSY;
        if(!sync_io_action_snapshot(&candidate.io))return false;
        *r->snapshot=candidate;r->result=TRIGGER_ACTION_TABLE_OK;return true;
    }
    r->result=TRIGGER_ACTION_EXECUTION_NOT_LOADED;
    if(!trigger_action_execution_is_loaded())return false;
    bool ok=false;
    if(r->operation==EXECUTION_ARM){
        r->result=execution_revalidate(&s_execution.arm_checked_tick);
        if(r->result)return false;
        const bool consumed=s_execution.installation.table.generation==s_consumed_table_generation &&
            (s_consumed_row_mask&(1u<<s_execution.row_index));
        if(consumed){r->result=TRIGGER_ACTION_EXECUTION_ALREADY_EXECUTED;return false;}
        r->result=TRIGGER_ACTION_TABLE_OK;
        ok=sync_io_action_arm_checked(s_execution.io.generation,s_execution.plan.deadline_ticks,
            execution_arm_validate,r);
        if(ok){
            if(s_consumed_table_generation!=s_execution.installation.table.generation){
                s_consumed_table_generation=s_execution.installation.table.generation;s_consumed_row_mask=0u;
            }
            s_consumed_row_mask|=1u<<s_execution.row_index;
        }else if(r->result)return false;
    }else if(r->operation==EXECUTION_STOP)ok=sync_io_action_stop(s_execution.io.generation);
    else if(r->operation==EXECUTION_RELEASE){
        ok=sync_io_action_release(s_execution.io.generation);
        if(ok)__atomic_store_n(&s_execution_loaded,0u,__ATOMIC_RELEASE);
    }
    r->result=ok?TRIGGER_ACTION_TABLE_OK:TRIGGER_ACTION_EXECUTION_HARDWARE;return ok;
}
static uint32_t execution_operation(execution_request_t *request)
{
    if(!enter())return TRIGGER_ACTION_SERVICE_BUSY;
    request->result=TRIGGER_ACTION_SERVICE_NOT_IDLE;
    (void)tdma_service_run_stopped_maintenance(tdma_runtime_owner_get(),execution_maintain,request);
    leave();return request->result;
}
uint32_t trigger_action_execution_now(trigger_action_now_t *out)
{if(!out)return TRIGGER_ACTION_TABLE_ARGUMENT;execution_request_t r={.operation=EXECUTION_NOW,.now=out};return execution_operation(&r);}
uint32_t trigger_action_execution_prepare(uint32_t index)
{execution_request_t r={.operation=EXECUTION_PREPARE,.index=index};return execution_operation(&r);}
uint32_t trigger_action_execution_arm(void)
{execution_request_t r={.operation=EXECUTION_ARM};return execution_operation(&r);}
uint32_t trigger_action_execution_stop(void)
{execution_request_t r={.operation=EXECUTION_STOP};return execution_operation(&r);}
uint32_t trigger_action_execution_release(void)
{execution_request_t r={.operation=EXECUTION_RELEASE};return execution_operation(&r);}
uint32_t trigger_action_execution_snapshot(trigger_action_execution_snapshot_t *out)
{if(!out)return TRIGGER_ACTION_TABLE_ARGUMENT;execution_request_t r={.operation=EXECUTION_SNAPSHOT,.snapshot=out};return execution_operation(&r);}
