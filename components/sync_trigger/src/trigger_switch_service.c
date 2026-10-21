#include "trigger_switch_service.h"
#include "trigger_sequence_pilot.h"

#include "board_identity.h"
#include "pico.h"
#include "tdma_runtime_owner.h"
#include "trigger_counter_service.h"
#include "trigger_gateway_service.h"

static uint32_t s_switch_busy, s_switch_loaded, s_switch_generation;
static trigger_deployment_binding_t s_switch_binding;

static bool switch_enter(void)
{
    uint32_t idle=0u;
    return get_core_num()==0u && __atomic_compare_exchange_n(&s_switch_busy,
        &idle,1u,false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED);
}
static void switch_leave(void)
{ __atomic_store_n(&s_switch_busy,0u,__ATOMIC_RELEASE); }
bool trigger_switch_is_loaded(void)
{ return __atomic_load_n(&s_switch_loaded,__ATOMIC_ACQUIRE)!=0u; }

static bool switch_uid(uint64_t *out)
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

static uint32_t switch_binding(trigger_deployment_binding_t *binding,
    uint32_t generation,uint32_t crc,uint32_t *output_mask)
{
    trigger_deployment_snapshot_t snapshot;
    tdma_ring_clock_snapshot_t ring;
    tdma_service_ring_runtime_config_t staged;
    uint64_t uid;
    if (trigger_deployment_snapshot(&snapshot) || !snapshot.active.valid)
        return SWITCH_DEPLOYMENT;
    const trigger_deployment_binding_t *b=&snapshot.active;
    if (b->local_role!=TRIGGER_DEPLOYMENT_SLOT_SWITCH_A &&
        b->local_role!=TRIGGER_DEPLOYMENT_SLOT_SWITCH_B) return SWITCH_WRONG_ROLE;
    if (b->table.generation!=generation || b->crc!=crc ||
        trigger_deployment_crc(&b->table)!=crc ||
        !switch_uid(&uid) || uid!=b->local_uid ||
        !tdma_runtime_owner_get_ring_clock_snapshot(&ring) ||
        !tdma_runtime_owner_get_staged_ring_config(&staged) ||
        ring.enabled || ring.adapter_started || ring.config_seq!=ring.applied_config_seq ||
        ring.config_seq!=b->ring_config || !staged.enabled ||
        staged.local_slot_id!=b->local_slot || staged.schedule_crc32!=b->schedule_crc ||
        (ring.schedule_crc32 && (ring.schedule_crc32!=staged.schedule_crc32 ||
            ring.local_slot_id!=staged.local_slot_id))) return SWITCH_CHANGED;
    trigger_deployment_row_t row;
    if (trigger_deployment_resolve(&b->table,b->local_role,
            uid,staged.local_slot_id,PROJECT_NODE_CAPACITY,&row)) return SWITCH_CHANGED;
    /* The two SWITCH slots share this owner; their installed masks select
     * arbitrary nonempty subsets of the local public OUT1..OUT4 namespace. */
    if (!row.output_mask || (row.output_mask&~TRIGGER_DEPLOYMENT_IO_MASK))
        return SWITCH_ARGUMENT;
    *output_mask=row.output_mask;
    *binding=*b;return SWITCH_OK;
}

typedef struct {uint32_t operation,generation,crc,code,result;} switch_request_t;
enum {SWITCH_LOAD,SWITCH_STAGE,SWITCH_APPLY,SWITCH_STOP,SWITCH_UNLOAD};

/* Give the SMA pins back when the unified model takes over the switch positions. Core1-only:
 * the licence is owned there, and the caller (the pilot service) runs in the same context as the
 * maintain loop. */
bool trigger_switch_force_unload(void)
{
    if (!trigger_switch_is_loaded()) return true;
    /* A persona left STAGED/PENDING refuses a bare release, so stop it first (the state machine
     * requires STOPPED before the licence can go back). */
    (void)sync_io_switch_stop(s_switch_generation);
    bool ok = sync_io_switch_release(s_switch_generation);
    if (!ok) {
        (void)sync_io_switch_stop(s_switch_generation);
        ok = sync_io_switch_release(s_switch_generation);
    }
    if (ok) {
        s_switch_generation = 0u;
        __atomic_store_n(&s_switch_loaded, 0u, __ATOMIC_RELEASE);
    }
    return ok;
}

static bool switch_maintain(void *context)
{
    switch_request_t *r=context;
    /* Mode 2: the unified model owns the switch's outputs and claims the persona itself. Loading
     * the legacy persona as well makes that claim fail (fault detail 9), and releasing a loaded
     * persona is refused, so the clean path is to never load it on a model-driven board. The
     * loaded flag stays 0, so a rollback to mode 1 reloads normally on the next configure. */
    if (r->operation==SWITCH_LOAD && trigger_sequence_pilot_switch_enabled()) {
        r->result=SWITCH_OK;
        return true;
    }
    if (r->operation==SWITCH_LOAD) {
        if (trigger_switch_is_loaded() || trigger_counter_is_loaded() || trigger_gateway_is_loaded()) {
            r->result=SWITCH_BUSY;return false;
        }
        trigger_deployment_binding_t binding;
        uint32_t generation,output_mask;
        r->result=switch_binding(&binding,r->generation,r->crc,&output_mask);
        if (r->result) return false;
        if (!sync_io_switch_load(output_mask,&generation)) {
            r->result=SWITCH_HARDWARE;return false;
        }
        s_switch_binding=binding;s_switch_generation=generation;
        __atomic_store_n(&s_switch_loaded,1u,__ATOMIC_RELEASE);
        return true;
    }
    if (!trigger_switch_is_loaded()) {r->result=SWITCH_NOT_LOADED;return false;}
    bool ok=false;
    if (r->operation==SWITCH_STAGE || r->operation==SWITCH_APPLY) {
        trigger_deployment_binding_t current;
        uint32_t output_mask;
        r->result=switch_binding(&current,s_switch_binding.table.generation,
            s_switch_binding.crc,&output_mask);
        if (r->result) return false;
        if (current.local_uid!=s_switch_binding.local_uid ||
            current.local_role!=s_switch_binding.local_role ||
            current.local_slot!=s_switch_binding.local_slot ||
            current.ring_config!=s_switch_binding.ring_config ||
            current.schedule_crc!=s_switch_binding.schedule_crc ||
            output_mask!=s_switch_binding.table.rows[s_switch_binding.local_role].output_mask) {
            r->result=SWITCH_CHANGED;return false;
        }
        if (r->operation==SWITCH_STAGE) {
            if (r->code&~output_mask) {r->result=SWITCH_ARGUMENT;return false;}
            ok=sync_io_switch_stage(s_switch_generation,r->code);
        } else ok=sync_io_switch_apply(s_switch_generation);
    } else if (r->operation==SWITCH_STOP) ok=sync_io_switch_stop(s_switch_generation);
    else if (r->operation==SWITCH_UNLOAD) ok=sync_io_switch_release(s_switch_generation);
    if (ok && r->operation==SWITCH_UNLOAD)
        __atomic_store_n(&s_switch_loaded,0u,__ATOMIC_RELEASE);
    r->result=ok?SWITCH_OK:SWITCH_HARDWARE;
    return ok;
}

static uint32_t switch_operation(uint32_t operation,uint32_t generation,uint32_t crc,
    uint32_t code)
{
    if (!switch_enter()) return SWITCH_BUSY;
    switch_request_t request={operation,generation,crc,code,SWITCH_NOT_IDLE};
    (void)tdma_service_run_stopped_maintenance(tdma_runtime_owner_get(),switch_maintain,&request);
    switch_leave();return request.result;
}
uint32_t trigger_switch_load(uint32_t generation,uint32_t crc)
{ return generation ? switch_operation(SWITCH_LOAD,generation,crc,0u) : SWITCH_ARGUMENT; }
uint32_t trigger_switch_stage(uint32_t code) {return switch_operation(SWITCH_STAGE,0u,0u,code);}
uint32_t trigger_switch_apply(void) {return switch_operation(SWITCH_APPLY,0u,0u,0u);}
uint32_t trigger_switch_stop(void) {return switch_operation(SWITCH_STOP,0u,0u,0u);}
uint32_t trigger_switch_unload(void) {return switch_operation(SWITCH_UNLOAD,0u,0u,0u);}
uint32_t trigger_switch_snapshot(trigger_switch_snapshot_t *out)
{
    if (!out) return SWITCH_ARGUMENT;
    if (!switch_enter()) return SWITCH_BUSY;
    trigger_switch_snapshot_t next={.binding=s_switch_binding,
        .loaded=trigger_switch_is_loaded()?1u:0u};
    const bool ok=sync_io_switch_snapshot(&next.io);
    if (ok) *out=next;
    switch_leave();return ok?SWITCH_OK:SWITCH_BUSY;
}
