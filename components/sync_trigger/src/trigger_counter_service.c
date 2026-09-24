#include "trigger_counter_service.h"

#include "board_identity.h"
#include "pico.h"
#include "tdma_runtime_owner.h"

static uint32_t s_counter_busy, s_counter_loaded, s_counter_generation;
static trigger_deployment_binding_t s_counter_binding;

static bool counter_enter(void)
{
    uint32_t idle=0u;
    return get_core_num()==0u && __atomic_compare_exchange_n(&s_counter_busy,
        &idle,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
}
static void counter_leave(void)
{ __atomic_store_n(&s_counter_busy,0u,__ATOMIC_RELEASE); }
bool trigger_counter_is_loaded(void)
{ return __atomic_load_n(&s_counter_loaded,__ATOMIC_ACQUIRE)!=0u; }

static bool counter_uid(uint64_t *out)
{
    const char *p=board_identity_serial();
    uint64_t uid=0u;
    if (!p) return false;
    for (uint32_t i=0;i<16u;++i) {
        const char c=p[i];
        uint32_t digit;
        if (c>='0' && c<='9') digit=(uint32_t)(c-'0');
        else if (c>='A' && c<='F') digit=(uint32_t)(c-'A'+10);
        else if (c>='a' && c<='f') digit=(uint32_t)(c-'a'+10);
        else return false;
        uid=(uid<<4u)|digit;
    }
    if (p[16] || !uid) return false;
    *out=uid;return true;
}

static uint32_t counter_binding(trigger_deployment_binding_t *binding,
    uint32_t generation,uint32_t crc,uint32_t *port)
{
    trigger_deployment_snapshot_t snapshot;
    tdma_ring_clock_snapshot_t ring;
    tdma_service_ring_runtime_config_t staged;
    uint64_t uid;
    if (trigger_deployment_snapshot(&snapshot) || !snapshot.active.valid)
        return COUNTER_DEPLOYMENT;
    const trigger_deployment_binding_t *b=&snapshot.active;
    if (b->local_role!=TRIGGER_DEPLOYMENT_SLOT_COUNTER) return COUNTER_WRONG_ROLE;
    if (b->table.generation!=generation || b->crc!=crc ||
        !counter_uid(&uid) || uid!=b->local_uid ||
        !tdma_runtime_owner_get_ring_clock_snapshot(&ring) ||
        !tdma_runtime_owner_get_staged_ring_config(&staged) ||
        ring.enabled || ring.adapter_started || ring.config_seq!=ring.applied_config_seq ||
        ring.config_seq!=b->ring_config || !staged.enabled ||
        staged.local_slot_id!=b->local_slot || staged.schedule_crc32!=b->schedule_crc)
        return COUNTER_CHANGED;
    trigger_deployment_row_t row;
    if (trigger_deployment_resolve(&b->table,TRIGGER_DEPLOYMENT_SLOT_COUNTER,
            uid,staged.local_slot_id,PROJECT_NODE_CAPACITY,&row))
        return COUNTER_CHANGED;
    uint32_t input=1u;
    while (input<=4u && row.input_mask!=(1u<<(input-1u))) ++input;
    if (input>4u) return COUNTER_ARGUMENT;
    *binding=*b;*port=input;return COUNTER_OK;
}

typedef struct {uint32_t operation,generation,crc,result;} counter_request_t;
enum {COUNTER_LOAD,COUNTER_START,COUNTER_STOP,COUNTER_CLEAR,COUNTER_UNLOAD};

static bool counter_maintain(void *context)
{
    counter_request_t *r=context;
    if (r->operation==COUNTER_LOAD) {
        if (trigger_counter_is_loaded()) {r->result=COUNTER_BUSY;return false;}
        trigger_deployment_binding_t binding;
        uint32_t port,generation;
        r->result=counter_binding(&binding,r->generation,r->crc,&port);
        if (r->result) return false;
        if (!sync_io_pulse_counter_load(port,&generation)) {
            r->result=COUNTER_HARDWARE;return false;
        }
        s_counter_binding=binding;s_counter_generation=generation;
        __atomic_store_n(&s_counter_loaded,1u,__ATOMIC_RELEASE);
        return true;
    }
    if (!trigger_counter_is_loaded()) {r->result=COUNTER_NOT_LOADED;return false;}
    bool ok=false;
    if (r->operation==COUNTER_START) {
        trigger_deployment_binding_t current;
        uint32_t port;
        r->result=counter_binding(&current,s_counter_binding.table.generation,
            s_counter_binding.crc,&port);
        if (r->result) return false;
        if (current.local_uid!=s_counter_binding.local_uid ||
            current.local_slot!=s_counter_binding.local_slot ||
            current.ring_config!=s_counter_binding.ring_config ||
            current.schedule_crc!=s_counter_binding.schedule_crc ||
            current.table.rows[TRIGGER_DEPLOYMENT_SLOT_COUNTER].input_mask!=
                s_counter_binding.table.rows[TRIGGER_DEPLOYMENT_SLOT_COUNTER].input_mask) {
            r->result=COUNTER_CHANGED;return false;
        }
        ok=sync_io_pulse_counter_start(s_counter_generation);
    } else if (r->operation==COUNTER_STOP) ok=sync_io_pulse_counter_stop(s_counter_generation);
    else if (r->operation==COUNTER_CLEAR) ok=sync_io_pulse_counter_clear(s_counter_generation);
    else if (r->operation==COUNTER_UNLOAD) {
        ok=sync_io_pulse_counter_release(s_counter_generation);
        if (ok) __atomic_store_n(&s_counter_loaded,0u,__ATOMIC_RELEASE);
    }
    r->result=ok?COUNTER_OK:COUNTER_HARDWARE;return ok;
}
static uint32_t counter_operation(uint32_t operation,uint32_t generation,uint32_t crc)
{
    if (!counter_enter()) return COUNTER_BUSY;
    counter_request_t request={operation,generation,crc,COUNTER_NOT_IDLE};
    (void)tdma_service_run_stopped_maintenance(tdma_runtime_owner_get(),counter_maintain,&request);
    counter_leave();return request.result;
}
uint32_t trigger_counter_load(uint32_t generation,uint32_t crc)
{ return generation?counter_operation(COUNTER_LOAD,generation,crc):COUNTER_ARGUMENT; }
uint32_t trigger_counter_start(void) {return counter_operation(COUNTER_START,0u,0u);}
uint32_t trigger_counter_stop(void) {return counter_operation(COUNTER_STOP,0u,0u);}
uint32_t trigger_counter_clear(void) {return counter_operation(COUNTER_CLEAR,0u,0u);}
uint32_t trigger_counter_unload(void) {return counter_operation(COUNTER_UNLOAD,0u,0u);}
uint32_t trigger_counter_snapshot(trigger_counter_snapshot_t *out)
{
    if (!out) return COUNTER_ARGUMENT;
    if (!counter_enter()) return COUNTER_BUSY;
    trigger_counter_snapshot_t next={.binding=s_counter_binding,
        .loaded=trigger_counter_is_loaded()?1u:0u};
    const bool ok=sync_io_pulse_counter_snapshot(&next.io);
    if (ok) *out=next;
    counter_leave();return ok?COUNTER_OK:COUNTER_BUSY;
}

uint32_t trigger_counter_snapshot_core1(trigger_counter_snapshot_t *out)
{
    if (!out || get_core_num()!=1u || !trigger_counter_is_loaded())
        return COUNTER_ARGUMENT;
    trigger_counter_snapshot_t next={.binding=s_counter_binding,.loaded=1u};
    if (!sync_io_pulse_counter_snapshot_core1(&next.io)) return COUNTER_BUSY;
    *out=next;
    return COUNTER_OK;
}
