"""Exercise the actual Core0 diagnostic owner across four isolated C states."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]

from refmem_units import body as refmem_unit_body


@pytest.fixture(scope="module")
def owner(tmp_path_factory):
    directory = tmp_path_factory.mktemp("distributed-action-owner")
    # Keep the real deployment snapshot layout, while removing unrelated clock
    # dependencies from this transport-only host harness.
    header = (ROOT / "components/sync_trigger/inc/trigger_deployment_service.h").read_text(encoding="utf-8")
    (directory / "trigger_deployment_service.h").write_text(
        header.replace('#include "trigger_grid_preview.h"',
                       'typedef unsigned trigger_grid_preview_plan_t;'), encoding="utf-8")
    source = directory / "owner.c"
    source.write_text(HARNESS.replace(
        '#include "distributed_action_probe.inc"',
        refmem_unit_body("distributed_action_probe")), encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    assert compiler
    executable = directory / ("owner.exe" if os.name == "nt" else "owner")
    includes = [directory, *(ROOT / f"components/{domain}/inc" for domain in
        ("distributed_refmem", "sync_trigger", "tdma")), ROOT / "components/distributed_refmem/src"]
    result = subprocess.run([compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        *[f"-I{path}" for path in includes], str(source), "-o", str(executable)],
        capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize("scenario", [
    "three_targets", "dropped_fragment", "stop_restart", "bad_crc", "wrong_source",
    "wrong_target", "wrong_generation", "wrong_receipt", "core_gate", "stopped_gate",
    "reconfigure", "timeout", "missing_fragment", "source_spoof", "nonordinary",
    "stop_before_run", "stop_between_observations", "broadcast_target_only",
    "publication_failure", "publication_stop",
    "feedback_carrier", "boundary_carrier", "mixed_carriers", "disabled_class",
])
def test_owner_transport(owner, scenario):
    result = subprocess.run([str(owner), scenario], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "tdma_process_image_layout.h"
#include "trigger_deployment_service.h"
#include "refmem_action_probe.h"
typedef struct {
    uint32_t enabled,data_enabled,local_slot_id,schedule_crc32,config_seq;
    uint32_t adapter_stop_count;
} tdma_ring_runtime_snapshot_t;
typedef struct { unsigned dummy; } tdma_service_service_t;
static unsigned core, selected;
static uint32_t now_ms;
static bool stopped=true, metadata_busy;
static uint8_t carrier=TDMA_PROCESS_IMAGE_MESSAGE_CLASS;
static bool mixed_carriers;
static trigger_deployment_snapshot_t deployments[4];
static tdma_service_service_t service;
static unsigned get_core_num(void) { return core; }
static uint32_t osal_tick_ms(void) { return now_ms; }
static tdma_service_service_t *tdma_runtime_owner_get(void) { return &service; }
static bool tdma_service_update_stopped_metadata(tdma_service_service_t *owner,
    bool (*fn)(void*),void *context) {
    assert(owner==&service);
    if(!stopped || metadata_busy) return false;
    metadata_busy=true; bool result=fn(context); metadata_busy=false; return result;
}
uint32_t trigger_deployment_snapshot(trigger_deployment_snapshot_t *out) {
    if(!stopped) return TRIGGER_DEPLOYMENT_SERVICE_NOT_IDLE;
    *out=deployments[selected]; return 0;
}
static void distributed_refmem_put_le16(uint8_t *p,uint16_t v) {p[0]=(uint8_t)v;p[1]=(uint8_t)(v>>8);}
static uint16_t distributed_refmem_get_le16(const uint8_t *p) {return (uint16_t)(p[0]|(uint16_t)p[1]<<8);}
static void distributed_refmem_put_le32(uint8_t *p,uint32_t v) {refmem_action_probe_put32(p,v);}
static uint32_t distributed_refmem_get_le32(const uint8_t *p) {return refmem_action_probe_get32(p);}
#include "distributed_action_probe.inc"
static __typeof__(s_action) states[4];
static uint32_t sequences[4];
static void select_node(unsigned n) { states[selected]=s_action;selected=n;s_action=states[n]; }
static void observe(bool running) {
    tdma_ring_runtime_snapshot_t ring={.enabled=running,.data_enabled=running,
        .local_slot_id=selected,.schedule_crc32=44,.config_seq=99};
    action_observe(&ring);
}
static void init(void) {
    trigger_deployment_t table={.generation=23};
    for(unsigned n=0;n<4;++n) table.rows[n]=(trigger_deployment_row_t){
        .board_uid=0x111100000000ull+n+1u,.tdma_slot=n};
    for(unsigned n=0;n<4;++n) {
        deployments[n].active=(trigger_deployment_binding_t){.table=table,.valid=1,
            .crc=0xabcdef12,.local_uid=table.rows[n].board_uid,.local_role=n,
            .local_slot=n,.schedule_crc=44,.ring_config=90};
        select_node(n);assert(distributed_action_probe_configure(71));
        assert(s_action.status.local_slot==n && s_action.status.role==n);
    }
    select_node(0);
}
static void start(void) {
    stopped=false;
    for(unsigned n=0;n<4;++n) {select_node(n);observe(true);}
    select_node(0);
}
static void finish_crc(uint8_t *m) {
    distributed_refmem_put_le16(m+TDMA_PROCESS_IMAGE_CRC_OFFSET,
        tdma_process_image_crc16_ccitt(m,TDMA_PROCESS_IMAGE_CRC_OFFSET));
}
static void mailbox_prepare(uint8_t *m,bool published) {
    memset(m,0,32);m[3]=mixed_carriers ?
        ((const uint8_t[]){TDMA_PROCESS_IMAGE_MESSAGE_CLASS,
            TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS,
            TDMA_PROCESS_IMAGE_VDC_BOUNDARY_COMMAND_MESSAGE_CLASS})[sequences[selected]%3u] : carrier;
    m[4]=(uint8_t)selected;m[5]=15;
    distributed_refmem_put_le16(m,TDMA_FLIGHT_MAILBOX_MAGIC);
    m[2]=TDMA_FLIGHT_MAILBOX_VERSION;
    for(unsigned i=TDMA_PROCESS_IMAGE_VDC_OFFSET;i<TDMA_PROCESS_IMAGE_VDC_OFFSET+TDMA_PROCESS_IMAGE_VDC_SIZE;++i)
        m[i]=(uint8_t)(0xA0u+i+selected);
    if(m[3]==TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS ||
        m[3]==TDMA_PROCESS_IMAGE_VDC_BOUNDARY_COMMAND_MESSAGE_CLASS) {
        m[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_INDEX_OFFSET]=(uint8_t)(sequences[selected]%16u);
        m[TDMA_PROCESS_IMAGE_VDC_FRAGMENT_COUNT_OFFSET]=16;
    }
    m[TDMA_PROCESS_IMAGE_ACK_SEQ16_OFFSET]=0x73;
    m[TDMA_PROCESS_IMAGE_ACK_SEQ16_OFFSET+1u]=0x26;
    m[TDMA_PROCESS_IMAGE_ACK_FLAGS_OFFSET]=0x15;
    m[TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_OFFSET]=0x69;
    distributed_refmem_put_le32(m+TDMA_PROCESS_IMAGE_REFMEM_GENERATION_OFFSET,++sequences[selected]);
    distributed_refmem_put_le16(m+6u,(uint16_t)sequences[selected]);
    uint8_t before[32];memcpy(before,m,sizeof(before));
    action_patch_mailbox(m,true);
    /* Every request and receipt preserves the shared header/broadcast mask,
     * VDC observation, transport sequence, ACK, and optional diagnostic. */
    assert(memcmp(m,before,TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET)==0);
    assert(memcmp(m+TDMA_PROCESS_IMAGE_ACK_QUALITY_OFFSET,
        before+TDMA_PROCESS_IMAGE_ACK_QUALITY_OFFSET,TDMA_PROCESS_IMAGE_ACK_QUALITY_SIZE)==0);
    assert(m[TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_OFFSET]==before[TDMA_PROCESS_IMAGE_OPTIONAL_DIAGNOSTIC_OFFSET]);
    assert(m[5]==15);
    finish_crc(m);
    if(published) action_mailbox_published(m);
}
static void mailbox(uint8_t *m) {mailbox_prepare(m,true);}
static void record_mailbox(const refmem_action_probe_record_t *r,uint8_t kind,uint8_t index,
    unsigned source,unsigned destination,uint8_t *m) {
    uint16_t f;uint32_t v;assert(refmem_action_probe_fragment_encode(r,index,&f,&v));
    memset(m,0,32);m[3]=TDMA_PROCESS_IMAGE_MESSAGE_CLASS;m[4]=(uint8_t)source;
    m[5]=(uint8_t)(1u<<destination);
    distributed_refmem_put_le32(m+TDMA_PROCESS_IMAGE_REFMEM_GENERATION_OFFSET,++sequences[source]);
    distributed_refmem_put_le16(m+TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET,f);
    distributed_refmem_put_le32(m+TDMA_PROCESS_IMAGE_REFMEM_VALUE_OFFSET,v);
    m[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET]=kind;m[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET]=index;
    finish_crc(m);
}
static void run_cycles(unsigned count,bool drop) {
    bool dropped=false;
    for(unsigned cycle=0;cycle<count;++cycle) for(unsigned source=0;source<4;++source) {
        select_node(source);uint8_t m[32];mailbox(m);
        if(drop&&!dropped&&source==0&&m[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET]==4) {dropped=true;continue;}
        for(unsigned dest=0;dest<4;++dest) if(dest!=source&&(m[5]&(1u<<dest))) {
            select_node(dest);action_receive_mailbox(source,m);
        }
    }
    select_node(0);
}
int main(int argc,char **argv) {
    assert(argc==2);const char *s=argv[1];init();
    if(!strcmp(s,"core_gate")) {
        core=1;distributed_action_probe_status_t out;
        assert(!distributed_action_probe_get(&out));assert(!distributed_action_probe_send(1,1,1));
        assert(!distributed_action_probe_configure(72));assert(!distributed_action_probe_clear());return 0;
    }
    if(!strcmp(s,"stopped_gate")) {
        stopped=false;assert(!distributed_action_probe_configure(72));
        assert(!distributed_action_probe_send(1,1,1));assert(!distributed_action_probe_clear());return 0;
    }
    assert(distributed_action_probe_send(1,10,123));
    if(!strcmp(s,"stop_before_run")) {
        assert(s_action.status.ring_config==0);
        action_stop_requested();
        /* No stopped sample is observed between the STOP request and RUN. */
        stopped=false;observe(true);
        assert(!s_action.engine.active && s_action.status.cancelled);
        uint8_t m[32];mailbox(m);
        assert(m[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET]==0);
        stopped=true;
        assert(!distributed_action_probe_configure(71));
        assert(distributed_action_probe_configure(72));
        assert(s_action.count==0 && s_action.engine.active);return 0;
    }
    if(!strcmp(s,"three_targets")||!strcmp(s,"dropped_fragment")||!strcmp(s,"publication_failure")||
        !strcmp(s,"feedback_carrier")||!strcmp(s,"boundary_carrier")||!strcmp(s,"mixed_carriers")) {
        if(!strcmp(s,"feedback_carrier")) carrier=TDMA_PROCESS_IMAGE_VDC_FEEDBACK_MESSAGE_CLASS;
        if(!strcmp(s,"boundary_carrier")) carrier=TDMA_PROCESS_IMAGE_VDC_BOUNDARY_COMMAND_MESSAGE_CLASS;
        mixed_carriers=!strcmp(s,"mixed_carriers");
        assert(distributed_action_probe_send(2,11,456));assert(distributed_action_probe_send(3,12,789));
        assert(!distributed_action_probe_send(1,12,999));
        start();
        if(!strcmp(s,"publication_failure")) {
            uint8_t first[32],retry[32];mailbox_prepare(first,false);
            for(unsigned attempt=0;attempt<32;++attempt) {
                mailbox_prepare(retry,false);
                assert(s_action.fragment==0 && s_action.status.sent==0);
                assert(retry[TDMA_PROCESS_IMAGE_CONTROL_SEQ8_OFFSET]==0);
                assert(memcmp(first+TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET,
                    retry+TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET,6)==0);
            }
            /* Successful publication alone advances the prepared fragment. */
            action_mailbox_published(retry);
            assert(s_action.fragment==1 && s_action.status.sent==1);
        }
        run_cycles(160,!strcmp(s,"dropped_fragment"));
        assert(s_action.status.completed==3&&s_action.status.completed_mask==14);
        for(unsigned n=1;n<4;++n) {select_node(n);assert(s_action.status.applied==1);
            assert(s_action.status.value==(n==1?123u:n==2?456u:789u));}
        return 0;
    }
    if(!strcmp(s,"reconfigure")) {
        assert(distributed_action_probe_configure(71));assert(s_action.count==1);
        assert(distributed_action_probe_clear());assert(!distributed_action_probe_configure(71));
        assert(distributed_action_probe_configure(72));assert(s_action.count==0);return 0;
    }
    refmem_action_probe_record_t r=s_action.queue[0];start();
    if(!strcmp(s,"nonordinary") || !strcmp(s,"disabled_class")) {
        uint8_t blocked[32];carrier=!strcmp(s,"nonordinary")?
            TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS:TDMA_PROCESS_IMAGE_VDC_COMMAND_MESSAGE_CLASS;
        mailbox(blocked);
        assert(s_action.status.sent==0 && s_action.fragment==0);
        assert(distributed_refmem_get_le16(blocked+TDMA_PROCESS_IMAGE_REFMEM_FIELD_ID_OFFSET)==0);
        assert(distributed_refmem_get_le32(blocked+TDMA_PROCESS_IMAGE_REFMEM_VALUE_OFFSET)==0);
        assert(blocked[TDMA_PROCESS_IMAGE_CONTROL_OPCODE_OFFSET]==0);
    }
    if(!strcmp(s,"publication_stop")) {
        uint8_t prepared[32];mailbox_prepare(prepared,false);action_stop_requested();
        action_mailbox_published(prepared);
        assert(!s_action.engine.active && s_action.status.sent==0);
        assert(s_action.status.cancel_reason==1);return 0;
    }
    if(!strcmp(s,"broadcast_target_only")) {
        run_cycles(80,false);
        assert(s_action.status.completed==1 && s_action.status.completed_mask==2);
        assert(s_action.status.applied==0);
        select_node(1);assert(s_action.status.applied==1 && s_action.status.value==123);
        for(unsigned n=2;n<4;++n) {
            select_node(n);assert(s_action.status.received>0);
            assert(s_action.status.applied==0 && !s_action.receipt_valid);
            assert(s_action.status.completed==0 && s_action.status.rejected==0);
        }
        return 0;
    }
    if(!strcmp(s,"stop_between_observations")) {
        run_cycles(3,false);
        /* The STOP request must survive owner contention. No STOP snapshot
         * appears: the next observable ring is already RUN again. */
        s_action_busy=1;action_stop_requested();observe(true);
        s_action_busy=0;observe(true);
        assert(!s_action.engine.active && s_action.status.cancelled);
        run_cycles(80,false);assert(s_action.status.completed==0);
        select_node(1);assert(s_action.status.applied==0);return 0;
    }
    if(!strcmp(s,"stop_restart")) {
        run_cycles(3,false);
        for(unsigned n=0;n<4;++n) {select_node(n);observe(false);assert(!s_action.engine.active);observe(true);}
        run_cycles(80,false);assert(s_action.status.completed==0);
        for(unsigned n=1;n<4;++n) {select_node(n);assert(s_action.status.applied==0);}
        return 0;
    }
    if(!strcmp(s,"timeout")) {
        now_ms=DISTRIBUTED_ACTION_PROBE_TIMEOUT_MS+1u;observe(true);
        assert(s_action.status.cancelled && s_action.status.cancel_reason==4);
        assert(s_action.status.elapsed_ms==now_ms);assert(!s_action.engine.active);return 0;
    }
    if(!strcmp(s,"wrong_generation")) ++r.deployment_generation;
    if(!strcmp(s,"source_spoof")) r.source_uid+=100u;
    if(!strcmp(s,"wrong_receipt")) ++r.argument;
    unsigned destination=!strcmp(s,"wrong_receipt")?0:1;
    unsigned source=!strcmp(s,"wrong_receipt")?1:0;
    select_node(destination);
    for(uint8_t i=0;i<10;++i) {
        uint8_t m[32];record_mailbox(&r,!strcmp(s,"wrong_receipt")?REFMEM_ACTION_PROBE_RECEIPT:REFMEM_ACTION_PROBE_REQUEST,i,source,destination,m);
        if(!strcmp(s,"missing_fragment")&&i==4) continue;
        if(!strcmp(s,"bad_crc")) m[20]^=1;
        if(!strcmp(s,"wrong_source")) {m[4]=3;finish_crc(m);}
        if(!strcmp(s,"wrong_target")) {m[5]=8;finish_crc(m);}
        if(!strcmp(s,"nonordinary")) {m[3]=TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS;finish_crc(m);}
        if(!strcmp(s,"disabled_class")) {m[3]=TDMA_PROCESS_IMAGE_VDC_COMMAND_MESSAGE_CLASS;finish_crc(m);}
        const bool consumed=action_receive_mailbox(source,m);
        assert(consumed==(strcmp(s,"nonordinary")!=0 && strcmp(s,"disabled_class")!=0));
    }
    assert(s_action.status.applied==0&&s_action.status.completed==0);
    return 0;
}
'''
