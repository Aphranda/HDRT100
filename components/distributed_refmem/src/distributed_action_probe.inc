/* Explicit opt-in Core0 diagnostic transport. Physical IO never touched. */
#include "distributed_action_probe.h"
#include "refmem_action_probe.h"
#include "trigger_deployment_service.h"
_Static_assert(REFMEM_ACTION_PROBE_ROLE_COUNT==TRIGGER_DEPLOYMENT_ROLE_COUNT,"business role count");
static uint32_t s_action_busy;
static uint32_t s_action_stop_pending;
static struct {
    refmem_action_probe_engine_t engine;
    trigger_deployment_binding_t binding;
    refmem_action_probe_record_t queue[4], receipt;
    refmem_action_probe_assembly_t assembly[PROJECT_NODE_CAPACITY];
    uint32_t last_transport[PROJECT_NODE_CAPACITY];
    distributed_action_probe_status_t status;
    uint32_t count, current, fragment, receipt_fragment, started_ms;
    bool receipt_valid;
} s_action;

static void action_cancel(uint32_t reason);
static bool action_enter(void)
{
    uint32_t idle=0;
    if (get_core_num()!=0u || !__atomic_compare_exchange_n(&s_action_busy,&idle,1u,
        false,__ATOMIC_ACQUIRE,__ATOMIC_RELAXED)) return false;
    if (__atomic_exchange_n(&s_action_stop_pending,0u,__ATOMIC_ACQ_REL))
        action_cancel(ACTION_PROBE_CANCEL_STOP);
    return true;
}
static void action_leave(void) { __atomic_store_n(&s_action_busy,0u,__ATOMIC_RELEASE); }
/* Latch even when the owner is busy: a STOP/re-START between service samples
 * must never resurrect a queued request or receipt. */
static void action_stop_requested(void)
{
    __atomic_store_n(&s_action_stop_pending,1u,__ATOMIC_RELEASE);
    if (action_enter()) action_leave();
}
static void action_cancel(uint32_t reason)
{
    if (s_action.engine.active) {
        s_action.status.cancel_reason=reason;
        s_action.status.elapsed_ms=s_action.status.ring_config ?
            (uint32_t)(osal_tick_ms()-s_action.started_ms) : 0u;
    }
    refmem_action_probe_engine_cancel(&s_action.engine);
    s_action.status.configured=0u;s_action.status.cancelled=1u;
    s_action.receipt_valid=false;
    memset(s_action.assembly,0,sizeof(s_action.assembly));
}

static bool action_configure_stopped(void *context)
{
    const uint32_t session=*(const uint32_t*)context;
    trigger_deployment_snapshot_t dep;
    if (trigger_deployment_snapshot(&dep) || !dep.active.valid) return false;
    refmem_action_probe_config_t cfg={.session=session,
        .deployment_generation=dep.active.table.generation,.deployment_crc32=dep.active.crc,
        .local_uid=dep.active.local_uid,.local_slot=dep.active.local_slot,
        .business_role=dep.active.local_role};
    if (s_action.engine.active && s_action.engine.config.session==session)
        return refmem_action_probe_engine_configure(&s_action.engine,&cfg);
    if (!refmem_action_probe_engine_configure(&s_action.engine,&cfg)) return false;
    s_action.binding=dep.active;s_action.count=s_action.current=s_action.fragment=0;
    s_action.receipt_fragment=s_action.started_ms=0;s_action.receipt_valid=false;
    s_action.engine.action_count=s_action.engine.value=0;
    memset(s_action.assembly,0,sizeof(s_action.assembly));
    memset(s_action.last_transport,0,sizeof(s_action.last_transport));
    s_action.status=(distributed_action_probe_status_t){.configured=1,.session=session,
        .deployment_generation=cfg.deployment_generation,.deployment_crc=cfg.deployment_crc32,
        .local_slot=cfg.local_slot,.role=cfg.business_role};
    return true;
}
bool distributed_action_probe_configure(uint32_t session)
{
    if (!session || !action_enter()) return false;
    bool ok=tdma_service_update_stopped_metadata(tdma_runtime_owner_get(),action_configure_stopped,&session);
    action_leave();return ok;
}
static bool action_clear_stopped(void *context) { (void)context;action_cancel(ACTION_PROBE_CANCEL_CLEAR);return true; }
bool distributed_action_probe_clear(void)
{
    if (!action_enter()) return false;
    bool ok=tdma_service_update_stopped_metadata(tdma_runtime_owner_get(),action_clear_stopped,NULL);
    action_leave();return ok;
}
static bool action_send_stopped(void *context)
{
    const uint32_t *a=context;
    trigger_deployment_snapshot_t dep;
    if (!s_action.engine.active || s_action.count==4u || a[0]>=4u || !a[1] ||
        trigger_deployment_snapshot(&dep) || !dep.active.valid ||
        dep.active.crc!=s_action.binding.crc || dep.active.table.generation!=s_action.status.deployment_generation ||
        (s_action.count && a[1]<=s_action.queue[s_action.count-1u].request_id)) return false;
    const trigger_deployment_row_t *target=&s_action.binding.table.rows[a[0]];
    refmem_action_probe_record_t r={.session=s_action.status.session,
        .deployment_generation=s_action.status.deployment_generation,.deployment_crc32=s_action.status.deployment_crc,
        .request_id=a[1],.source_uid=dep.active.local_uid,.target_uid=target->board_uid,
        .source_slot=dep.active.local_slot,.target_slot=target->tdma_slot,
        .business_role=a[0],.opcode=REFMEM_ACTION_PROBE_SET_VALUE,.argument=a[2]};
    if (!refmem_action_probe_record_valid(&r)) return false;
    s_action.queue[s_action.count++]=r;s_action.status.queued=s_action.count;return true;
}
bool distributed_action_probe_send(uint32_t role,uint32_t request,uint32_t value)
{
    if (!action_enter()) return false;
    uint32_t args[]={role,request,value};
    bool ok=tdma_service_update_stopped_metadata(tdma_runtime_owner_get(),action_send_stopped,args);
    action_leave();return ok;
}
bool distributed_action_probe_get(distributed_action_probe_status_t *out)
{
    if (!out || !action_enter()) return false;
    *out=s_action.status;
    if (s_action.engine.active && s_action.status.ring_config)
        out->elapsed_ms=(uint32_t)(osal_tick_ms()-s_action.started_ms);
    action_leave();return true;
}
static void action_observe(const tdma_ring_runtime_snapshot_t *ring)
{
    if (!action_enter()) return;
    if (s_action.engine.active) {
        if (!ring->enabled || !ring->data_enabled) {
            if (s_action.status.ring_config) action_cancel(ACTION_PROBE_CANCEL_INACTIVE);
        } else if (ring->local_slot_id!=s_action.status.local_slot ||
            ring->schedule_crc32!=s_action.binding.schedule_crc ||
            (s_action.status.ring_config && ring->config_seq!=s_action.status.ring_config))
            action_cancel(ACTION_PROBE_CANCEL_BINDING);
        else {
            if (!s_action.status.ring_config) {
                s_action.status.ring_config=ring->config_seq;s_action.started_ms=osal_tick_ms();
            }
            if ((uint32_t)(osal_tick_ms()-s_action.started_ms)>DISTRIBUTED_ACTION_PROBE_TIMEOUT_MS)
                action_cancel(ACTION_PROBE_CANCEL_TIMEOUT);
        }
    }
    action_leave();
}
static void action_patch_mailbox(uint8_t *mailbox)
{
    /* All ordinary transport classes retain the same RefMem/control regions.
     * VDC feedback fragments use only their separate six-byte VDC region.
     * The full-body priority SYNC class is deliberately excluded. */
    if (!tdma_process_image_transport_class_valid(mailbox[3]) || !action_enter()) return;
    if (s_action.engine.active && s_action.status.ring_config) {
        const bool request=s_action.current<s_action.count;
        if (request || s_action.receipt_valid) {
            const refmem_action_probe_record_t *r=request?&s_action.queue[s_action.current]:&s_action.receipt;
            uint32_t *index=request?&s_action.fragment:&s_action.receipt_fragment;
            uint16_t field;uint32_t value;
            if (refmem_action_probe_fragment_encode(r,(uint8_t)*index,&field,&value)) {
                /* Preserve the ordinary VDC broadcast audience. The full
                 * diagnostic record, not the shared header, selects its peer. */
                distributed_refmem_put_le16(mailbox+TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET,field);
                distributed_refmem_put_le32(mailbox+TDMA_PROCESS_IMAGE_REFMEM_VALUE_OFFSET,value);
                mailbox[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET]=request?REFMEM_ACTION_PROBE_REQUEST:REFMEM_ACTION_PROBE_RECEIPT;
                mailbox[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET]=(uint8_t)*index;
            }
        }
    }
    action_leave();
}
/* Preparing a latest-value mailbox is not publication. Backpressure or a
 * failed publish must retain this fragment for the next attempt. */
static void action_mailbox_published(const uint8_t *mailbox)
{
    if (!tdma_process_image_transport_class_valid(mailbox[3]) || !action_enter()) return;
    if (s_action.engine.active && s_action.status.ring_config) {
        const bool request=s_action.current<s_action.count;
        if (request || s_action.receipt_valid) {
            const refmem_action_probe_record_t *r=request?&s_action.queue[s_action.current]:&s_action.receipt;
            uint32_t *index=request?&s_action.fragment:&s_action.receipt_fragment;
            uint16_t field;uint32_t value;
            if (mailbox[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET]==
                    (request?REFMEM_ACTION_PROBE_REQUEST:REFMEM_ACTION_PROBE_RECEIPT) &&
                mailbox[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET]==*index &&
                refmem_action_probe_fragment_encode(r,(uint8_t)*index,&field,&value) &&
                distributed_refmem_get_le16(mailbox+TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET)==field &&
                distributed_refmem_get_le32(mailbox+TDMA_PROCESS_IMAGE_REFMEM_VALUE_OFFSET)==value) {
                *index=(*index+1u)%REFMEM_ACTION_PROBE_FRAGMENT_COUNT;
                ++s_action.status.sent;
            }
        }
    }
    action_leave();
}
static bool action_receive_mailbox(uint32_t source,const uint8_t *mailbox)
{
    const uint8_t kind=mailbox[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET];
    if (!tdma_process_image_transport_class_valid(mailbox[3]) ||
        (kind!=REFMEM_ACTION_PROBE_REQUEST && kind!=REFMEM_ACTION_PROBE_RECEIPT)) return false;
    if (!action_enter()) return true;
    if (!s_action.engine.active || !s_action.status.ring_config) goto done;
    if (source>=PROJECT_NODE_CAPACITY || mailbox[4]!=source ||
        (mailbox[5] & (1u<<s_action.status.local_slot))==0u ||
        distributed_refmem_get_le16(mailbox+TDMA_PROCESS_IMAGE_CRC_OFFSET)!=
            tdma_process_image_crc16_ccitt(mailbox,TDMA_PROCESS_IMAGE_CRC_OFFSET)) {
        ++s_action.status.rejected;goto done;
    }
    uint32_t seq=distributed_refmem_get_le32(mailbox+TDMA_PROCESS_IMAGE_REFMEM_GENERATION_OFFSET);
    if (seq<=s_action.last_transport[source]) goto done;
    s_action.last_transport[source]=seq;
    refmem_action_probe_record_t r;
    refmem_action_probe_result_t result=refmem_action_probe_assembly_push(&s_action.assembly[source],kind,
        mailbox[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET],
        distributed_refmem_get_le16(mailbox+TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET),
        distributed_refmem_get_le32(mailbox+TDMA_PROCESS_IMAGE_REFMEM_VALUE_OFFSET),&r);
    s_action.status.last_result=(uint32_t)result;
    if (result==REFMEM_ACTION_PROBE_INCOMPLETE) goto done;
    if (result!=REFMEM_ACTION_PROBE_COMPLETE) {++s_action.status.rejected;goto done;}
    ++s_action.status.received;
    if (kind==REFMEM_ACTION_PROBE_RECEIPT) {
        if (s_action.current<s_action.count && source==s_action.queue[s_action.current].target_slot &&
            refmem_action_probe_equal(&r,&s_action.queue[s_action.current])) {
            ++s_action.current;++s_action.status.completed;s_action.fragment=0;
            s_action.status.last_completed_request=r.request_id;
            s_action.status.completed_mask|=1u<<r.business_role;
            s_action.status.completed_elapsed_ms=(uint32_t)(osal_tick_ms()-s_action.started_ms);
        }
        goto done;
    }
    if (r.target_slot!=s_action.status.local_slot) goto done;
    bool known_source=false;
    for (uint32_t i=0;i<TRIGGER_DEPLOYMENT_ROLE_COUNT;++i) {
        const trigger_deployment_row_t *row=&s_action.binding.table.rows[i];
        if (row->board_uid==r.source_uid && row->tdma_slot==source && r.source_slot==source) known_source=true;
    }
    if (!known_source) {++s_action.status.rejected;goto done;}
    result=refmem_action_probe_engine_execute(&s_action.engine,kind,&r);
    s_action.status.last_result=(uint32_t)result;
    if (result==REFMEM_ACTION_PROBE_APPLIED || result==REFMEM_ACTION_PROBE_DUPLICATE) {
        if (result==REFMEM_ACTION_PROBE_DUPLICATE) ++s_action.status.duplicates;
        s_action.status.applied=s_action.engine.action_count;s_action.status.value=s_action.engine.value;
        s_action.status.last_request=r.request_id;
        if (!s_action.receipt_valid || !refmem_action_probe_equal(&s_action.receipt,&r)) {
            s_action.receipt=r;s_action.receipt_fragment=0;s_action.receipt_valid=true;
        }
    } else ++s_action.status.rejected;
done:action_leave();return true;
}
