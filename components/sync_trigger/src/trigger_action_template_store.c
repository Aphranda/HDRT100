#include "trigger_action_template_store.h"

#include <string.h>

#include "board_config.h"
#include "pico.h"
#include "tdma_runtime_owner.h"
#include "trigger_deployment_service.h"
#include "trigger_counter_service.h"
#include "trigger_gateway_service.h"
#include "trigger_switch_service.h"

static volatile uint32_t s_busy;
static trigger_action_template_store_snapshot_t s_store;

static bool enter(void)
{
    uint32_t idle=0u;
    return get_core_num()==0u && __atomic_compare_exchange_n(&s_busy,&idle,1u,
        false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
}
static void leave(void){__atomic_store_n(&s_busy,0u,__ATOMIC_RELEASE);}

typedef struct {const trigger_action_template_table_t *table;uint32_t crc,result;} request_t;

static bool current_deployment(trigger_deployment_t *out)
{
    trigger_deployment_snapshot_t snapshot;
    if (!out || trigger_deployment_snapshot(&snapshot) ||
        !snapshot.active.valid) return false;
    *out=snapshot.active.table;
    return true;
}

static bool install_stopped(void *context)
{
    request_t *r=context; trigger_deployment_t deployment;
    r->result=TRIGGER_ACTION_TEMPLATE_STORE_NOT_STOPPED;
    if(!current_deployment(&deployment)) return false;
    r->result=trigger_action_template_table_validate(r->table,&deployment,r->crc);
    if(r->result) return false;
    if(r->table->generation<=s_store.last_generation){
        const bool same=s_store.installed && s_store.generation==r->table->generation &&
            s_store.crc==r->crc && memcmp(&s_store.table,r->table,sizeof(*r->table))==0;
        r->result=same?TRIGGER_ACTION_TEMPLATE_STORE_OK:
            TRIGGER_ACTION_TEMPLATE_STORE_GENERATION;
        return same;
    }
    s_store.table=*r->table;s_store.generation=r->table->generation;
    s_store.crc=r->crc;s_store.last_generation=r->table->generation;
    s_store.installed=1u;s_store.binding_current=1u;
    r->result=TRIGGER_ACTION_TEMPLATE_STORE_OK;return true;
}

static bool clear_stopped(void *context)
{
    uint32_t *result=context; const uint32_t high=s_store.last_generation;
    memset(&s_store,0,sizeof(s_store));s_store.last_generation=high;
    *result=TRIGGER_ACTION_TEMPLATE_STORE_OK;return true;
}

uint32_t trigger_action_template_install(
    const trigger_action_template_table_t *table,uint32_t expected_crc)
{
    if(!table || !table->generation || !table->count)
        return TRIGGER_ACTION_TEMPLATE_STORE_CHANGED;
    if(!enter()) return TRIGGER_ACTION_TEMPLATE_STORE_BUSY;
    request_t r={.table=table,.crc=expected_crc,.result=TRIGGER_ACTION_TEMPLATE_STORE_NOT_STOPPED};
    (void)tdma_service_update_stopped_metadata(tdma_runtime_owner_get(),install_stopped,&r);
    leave();return r.result;
}

uint32_t trigger_action_template_clear(void)
{
    if(!enter()) return TRIGGER_ACTION_TEMPLATE_STORE_BUSY;
    uint32_t result=TRIGGER_ACTION_TEMPLATE_STORE_NOT_STOPPED;
    (void)tdma_service_update_stopped_metadata(tdma_runtime_owner_get(),clear_stopped,&result);
    leave();return result;
}

uint32_t trigger_action_template_snapshot(
    trigger_action_template_store_snapshot_t *out)
{
    if(!out) return TRIGGER_ACTION_TEMPLATE_STORE_CHANGED;
    if(!enter()) return TRIGGER_ACTION_TEMPLATE_STORE_BUSY;
    *out=s_store;leave();return TRIGGER_ACTION_TEMPLATE_STORE_OK;
}

uint32_t trigger_action_template_activate_resources(void)
{
    trigger_deployment_snapshot_t deployment;
    if (!s_store.installed || trigger_deployment_snapshot(&deployment) ||
        !deployment.active.valid) return TRIGGER_ACTION_TEMPLATE_STORE_DEPLOYMENT;
    if (s_store.resources_loaded) return TRIGGER_ACTION_TEMPLATE_STORE_OK;
    uint32_t result=TRIGGER_ACTION_TEMPLATE_STORE_CHANGED;
    switch (deployment.active.local_role) {
    case TRIGGER_DEPLOYMENT_SLOT_COUNTER:
        result=trigger_counter_load(deployment.active.table.generation,
            deployment.active.crc); break;
    case TRIGGER_DEPLOYMENT_SLOT_SWITCH_A:
    case TRIGGER_DEPLOYMENT_SLOT_SWITCH_B:
        result=trigger_switch_load(deployment.active.table.generation,
            deployment.active.crc); break;
    case TRIGGER_DEPLOYMENT_SLOT_GATEWAY: {
        uint32_t pulse_ns=0u;
        for(uint32_t i=0u;i<s_store.table.count;++i)
            if(s_store.table.row[i].role==TRIGGER_DEPLOYMENT_SLOT_GATEWAY &&
               s_store.table.row[i].kind==TRIGGER_ACTION_TEMPLATE_GATEWAY)
                pulse_ns=s_store.table.row[i].argument;
        if (!pulse_ns) return TRIGGER_ACTION_TEMPLATE_STORE_CHANGED;
        result=trigger_gateway_load(deployment.active.table.generation,
            deployment.active.crc,pulse_ns); break;
    }
    default: break;
    }
    if (!result) s_store.resources_loaded=1u;
    return result;
}

uint32_t trigger_action_template_release_resources(void)
{
    trigger_deployment_snapshot_t deployment;
    if (!s_store.resources_loaded) return TRIGGER_ACTION_TEMPLATE_STORE_OK;
    if (trigger_deployment_snapshot(&deployment) || !deployment.active.valid)
        return TRIGGER_ACTION_TEMPLATE_STORE_DEPLOYMENT;
    uint32_t result=TRIGGER_ACTION_TEMPLATE_STORE_CHANGED;
    switch (deployment.active.local_role) {
    case TRIGGER_DEPLOYMENT_SLOT_COUNTER:
        result=trigger_counter_stop();
        if (!result) result=trigger_counter_unload();
        break;
    case TRIGGER_DEPLOYMENT_SLOT_SWITCH_A:
    case TRIGGER_DEPLOYMENT_SLOT_SWITCH_B:
        result=trigger_switch_unload(); break;
    case TRIGGER_DEPLOYMENT_SLOT_GATEWAY:
        result=trigger_gateway_unload(); break;
    default: break;
    }
    if (!result) s_store.resources_loaded=0u;
    return result;
}
