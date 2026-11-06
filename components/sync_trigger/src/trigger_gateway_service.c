#include "trigger_gateway_service.h"
#include "trigger_sequence_pilot.h"

#include "board_identity.h"
#include "pico.h"
#include "tdma_runtime_owner.h"

#include "trigger_binding_ring.h"
#include "trigger_counter_service.h"

static uint32_t s_gateway_busy, s_gateway_loaded, s_gateway_generation;
static trigger_deployment_binding_t s_gateway_binding;

static bool gateway_enter(void)
{
    uint32_t idle=0u;
    return get_core_num()==0u && __atomic_compare_exchange_n(&s_gateway_busy,
        &idle,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
}
static void gateway_leave(void)
{ __atomic_store_n(&s_gateway_busy,0u,__ATOMIC_RELEASE); }
bool trigger_gateway_is_loaded(void)
{ return __atomic_load_n(&s_gateway_loaded,__ATOMIC_ACQUIRE)!=0u; }

static bool gateway_uid(uint64_t *out)
{
    const char *p=board_identity_serial();
    uint64_t uid=0u;
    if (!p) return false;
    for (uint32_t i=0u;i<16u;++i) {
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

static uint32_t gateway_binding(trigger_deployment_binding_t *binding,
    uint32_t generation,uint32_t crc,uint32_t *input_port,uint32_t *output_port)
{
    trigger_deployment_snapshot_t snapshot;
    tdma_ring_clock_snapshot_t ring;
    tdma_service_ring_runtime_config_t staged;
    uint64_t uid;
    if (trigger_deployment_snapshot(&snapshot) || !snapshot.active.valid)
        return GATEWAY_DEPLOYMENT;
    const trigger_deployment_binding_t *b=&snapshot.active;
    if (b->local_role!=TRIGGER_DEPLOYMENT_SLOT_GATEWAY) return GATEWAY_WRONG_ROLE;
    if (b->table.generation!=generation || b->crc!=crc ||
        trigger_deployment_crc(&b->table)!=crc ||
        !gateway_uid(&uid) ||
        !tdma_runtime_owner_get_ring_clock_snapshot(&ring) ||
        !tdma_runtime_owner_get_staged_ring_config(&staged) ||
        trigger_binding_ring_check(b,uid,&ring,&staged)!=TRIGGER_BINDING_RING_OK ||
        (ring.schedule_crc32 && (ring.schedule_crc32!=staged.schedule_crc32 ||
            ring.local_slot_id!=staged.local_slot_id))) return GATEWAY_CHANGED;
    trigger_deployment_row_t row;
    if (trigger_deployment_resolve(&b->table,TRIGGER_DEPLOYMENT_SLOT_GATEWAY,
            uid,staged.local_slot_id,PROJECT_NODE_CAPACITY,&row)) return GATEWAY_CHANGED;
    /* Deployment validation requires one bit in each independent namespace.
     * No board number or fixed pad pair overrides the installed route. */
    uint32_t input=1u,output=1u;
    while (input<=4u && row.input_mask!=(1u<<(input-1u))) ++input;
    while (output<=4u && row.output_mask!=(1u<<(output-1u))) ++output;
    if (input>4u || output>4u) return GATEWAY_ARGUMENT;
    *input_port=input;*output_port=output;
    *binding=*b;return GATEWAY_OK;
}

typedef struct {uint32_t operation,generation,crc,pulse_ns,result;} gateway_request_t;
enum {GATEWAY_LOAD,GATEWAY_STOP,GATEWAY_UNLOAD};

/* Give the SMA pins back when the unified model takes over. Core1 only (the licence lives there). */
bool trigger_gateway_force_unload(void)
{
    if (!trigger_gateway_is_loaded()) return true;
    const bool ok = sync_io_gateway_release(s_gateway_generation);
    if (ok) {
        s_gateway_generation = 0u;
        __atomic_store_n(&s_gateway_loaded, 0u, __ATOMIC_RELEASE);
    }
    return ok;
}

static bool gateway_maintain(void *context)
{
    gateway_request_t *r=context;
    /* Unified-model position flow owns the SMA pins itself: the legacy gateway persona must not be
     * loaded, or its PIO claims the same outputs and the model's reservation can never be granted. */
    if (trigger_sequence_pilot_position_enabled()) { r->result=GATEWAY_OK; return true; }
    if (r->operation==GATEWAY_LOAD) {
        if (trigger_gateway_is_loaded() || trigger_counter_is_loaded()) {
            r->result=GATEWAY_BUSY;return false;
        }
        trigger_deployment_binding_t binding;
        uint32_t generation,input_port,output_port;
        r->result=gateway_binding(&binding,r->generation,r->crc,&input_port,&output_port);
        if (r->result) return false;
        if (!sync_io_gateway_load(input_port,output_port,r->pulse_ns,&generation)) {
            r->result=GATEWAY_HARDWARE;return false;
        }
        s_gateway_binding=binding;s_gateway_generation=generation;
        __atomic_store_n(&s_gateway_loaded,1u,__ATOMIC_RELEASE);
        return true;
    }
    if (!trigger_gateway_is_loaded()) {r->result=GATEWAY_NOT_LOADED;return false;}
    const bool ok=r->operation==GATEWAY_STOP ? sync_io_gateway_stop(s_gateway_generation) :
        sync_io_gateway_release(s_gateway_generation);
    if (ok && r->operation==GATEWAY_UNLOAD)
        __atomic_store_n(&s_gateway_loaded,0u,__ATOMIC_RELEASE);
    r->result=ok?GATEWAY_OK:GATEWAY_HARDWARE;
    return ok;
}

static uint32_t gateway_operation(uint32_t operation,uint32_t generation,uint32_t crc,
    uint32_t pulse_ns)
{
    if (!gateway_enter()) return GATEWAY_BUSY;
    gateway_request_t request={operation,generation,crc,pulse_ns,GATEWAY_NOT_IDLE};
    (void)tdma_service_run_stopped_maintenance(tdma_runtime_owner_get(),gateway_maintain,&request);
    gateway_leave();return request.result;
}
uint32_t trigger_gateway_load(uint32_t generation,uint32_t crc,uint32_t pulse_ns)
{ return generation && pulse_ns ? gateway_operation(GATEWAY_LOAD,generation,crc,pulse_ns) : GATEWAY_ARGUMENT; }
uint32_t trigger_gateway_stop(void) {return gateway_operation(GATEWAY_STOP,0u,0u,0u);}
uint32_t trigger_gateway_unload(void) {return gateway_operation(GATEWAY_UNLOAD,0u,0u,0u);}
uint32_t trigger_gateway_snapshot(trigger_gateway_snapshot_t *out)
{
    if (!out) return GATEWAY_ARGUMENT;
    if (!gateway_enter()) return GATEWAY_BUSY;
    trigger_gateway_snapshot_t next={.binding=s_gateway_binding,
        .loaded=trigger_gateway_is_loaded()?1u:0u};
    const bool ok=sync_io_gateway_snapshot(&next.io);
    if (ok) *out=next;
    gateway_leave();return ok?GATEWAY_OK:GATEWAY_BUSY;
}
