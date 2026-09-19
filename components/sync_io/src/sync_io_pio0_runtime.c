#include "sync_io_pio0_runtime.h"
#include <string.h>
#include "pico.h"
static sync_io_persona_manager_t s_runtime;
static uint32_t s_busy;
static struct {
    sync_io_persona_manager_hooks_t hooks;
    void *context;
    bool occupied;
} s_clients[SYNC_IO_PERSONA_ID_COUNT];
static bool enter(void)
{
    uint32_t expected=0u;
    return get_core_num()==0u && __atomic_compare_exchange_n(&s_busy,&expected,1u,
        false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE);
}
static void leave(void) { __atomic_store_n(&s_busy,0u,__ATOMIC_RELEASE); }
bool sync_io_pio0_runtime_start_core1(sync_io_persona_manager_handle_t *handle)
{
    uint32_t expected=0u;
    if(get_core_num()!=1u || !handle || !__atomic_compare_exchange_n(&s_busy,
        &expected,1u,false,__ATOMIC_ACQ_REL,__ATOMIC_ACQUIRE)) return false;
    const bool ok=sync_io_persona_manager_start(&s_runtime,handle);
    leave();return ok;
}
static bool load(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)ctx;
    return !s_clients[d->id].hooks.load ||
        s_clients[d->id].hooks.load(s_clients[d->id].context,d,dma);
}
static bool arm(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)ctx;
    return !s_clients[d->id].hooks.arm ||
        s_clients[d->id].hooks.arm(s_clients[d->id].context,d,dma);
}
static void cleanup(void *ctx,const sync_io_persona_descriptor_t *d,uint32_t dma)
{
    (void)ctx;
    if(s_clients[d->id].hooks.cleanup)
        s_clients[d->id].hooks.cleanup(s_clients[d->id].context,d,dma);
}
bool sync_io_pio0_runtime_prepare(sync_io_persona_id_t id,
    const sync_io_persona_manager_hooks_t *hooks,void *context,
    sync_io_persona_manager_handle_t *handle)
{
    if(!hooks || !handle || (id!=SYNC_IO_PERSONA_ID_SCHEDULED_TRIGGER &&
        id!=SYNC_IO_PERSONA_ID_REFERENCE_MONITOR) || !enter()) return false;
    if(s_clients[id].occupied) { leave(); return false; }
    if(!s_runtime.initialized) {
        const sync_io_persona_manager_hooks_t dispatch={.load=load,.arm=arm,.cleanup=cleanup};
        sync_io_persona_manager_init(&s_runtime,&dispatch,NULL);
    }
    s_clients[id].hooks=*hooks; s_clients[id].context=context;
    bool ok=sync_io_persona_manager_claim(&s_runtime,id,handle,NULL) &&
        sync_io_persona_manager_load(&s_runtime,handle) &&
        sync_io_persona_manager_arm(&s_runtime,handle);
    if(!ok && sync_io_persona_manager_handle_valid(&s_runtime,handle))
        (void)sync_io_persona_manager_release(&s_runtime,handle);
    s_clients[id].occupied=ok;
    leave(); return ok;
}
bool sync_io_pio0_runtime_release(sync_io_persona_manager_handle_t *handle)
{
    if(!handle || !enter()) return false;
    if(!sync_io_persona_manager_handle_valid(&s_runtime,handle)) { leave();return false; }
    const sync_io_persona_id_t id=s_runtime.leases[handle->slot].descriptor->id;
    const bool ok=sync_io_persona_manager_release(&s_runtime,handle);
    if(ok) memset(&s_clients[id],0,sizeof(s_clients[id]));
    leave();return ok;
}
