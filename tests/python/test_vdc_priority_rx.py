"""Exercise the production direct IRQ sink without RTOS, FIFO, or DCO dependencies."""
import json
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture(scope="module", params=[(6, False), (6, True), (8, False), (8, True)])
def direct_rx_executable(request, tmp_path_factory):
    nodes, short_enums = request.param
    directory = tmp_path_factory.mktemp(f"vdc-direct-rx-{nodes}-{int(short_enums)}")
    source = directory / "direct_rx.c"
    source.write_text(HARNESS, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or shutil.which("clang")
    if not compiler and Path("D:/Microsoft/mingw64/bin/gcc.exe").is_file():
        compiler = "D:/Microsoft/mingw64/bin/gcc.exe"
    assert compiler, "A host C compiler is required"
    executable = directory / ("direct_rx.exe" if os.name == "nt" else "direct_rx")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-fstack-usage", f"-DPROJECT_NODE_CAPACITY={nodes}",
               *["-I" + str(ROOT / path) for path in (
                   "components/tdma/inc", "components/vdc_dpll_manager/inc",
                   "components/vdc_dpll_manager/src")],
               str(source), str(ROOT / "components/vdc_dpll_manager/src/vdc_priority_codec.c"),
               "-o", str(executable)]
    if short_enums:
        command.insert(1, "-fshort-enums")
    result = subprocess.run(command, cwd=directory, capture_output=True, text=True, timeout=60)
    (directory / "build.json").write_text(json.dumps({"command": command,
        "returncode": result.returncode, "stdout": result.stdout, "stderr": result.stderr}, indent=2),
        encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    return executable


@pytest.mark.parametrize("case", [
    "initial_stop", "typed_zero", "ordinary_unknown", "duplicate", "full_event",
    "binding_generation", "source_identity", "conflict_time", "conflict_width",
    "zero_generation", "zero_width", "bad_flags", "interval_overflow", "seq16_mismatch",
    "epoch_reset", "stop_retention", "zero_epoch", "guard_busy", "guard_collision",
    "counter_saturation", "adjacent_identity", "carrier_wrap_epoch", "input_immutable",
    "live_empty", "live_read", "live_wrong_core", "live_stop", "live_collision",
])
def test_direct_core1_typed_rx(direct_rx_executable, case):
    result = subprocess.run([str(direct_rx_executable), case], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr


HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "vdc_priority_rx.h"
static uint32_t *collision_guard;
static unsigned core=1u;
static unsigned get_core_num(void) { return core; }
static const uint32_t *collision_word;
static uint32_t read_atomic(const uint32_t *p, int order)
{
    const uint32_t value = __atomic_load_n(p, order);
    if (p == collision_word) {
        __atomic_add_fetch(collision_guard, 2u, __ATOMIC_RELEASE);
        collision_word = NULL;
    }
    return value;
}
#define __atomic_load_n read_atomic
#include "vdc_priority_rx.inc"
#undef __atomic_load_n

static tdma_priority_rx_record_t record;
static vdc_priority_codec_record_t typed = {
    .binding_generation=42u, .event_sequence=0u,
    .event_time_lower=123456789u, .uncertainty_width=7u, .flags=0u
};
static void encode(void)
{
    assert(vdc_priority_codec_encode(&typed, record.mailbox+8u));
    record.mailbox[6]=(uint8_t)typed.event_sequence;
    record.mailbox[7]=(uint8_t)(typed.event_sequence >> 8u);
}
static vdc_priority_rx_snapshot_t get(void)
{
    vdc_priority_rx_snapshot_t out;
    assert(vdc_dpll_manager_get_priority_rx(&out));
    return out;
}
static vdc_priority_rx_snapshot_t send(void)
{ vdc_priority_rx_core1(&record); return get(); }
static void expect_retained(const vdc_priority_rx_snapshot_t *a,
    const vdc_priority_rx_snapshot_t *b)
{
    assert(a->have_record==b->have_record && a->source_slot==b->source_slot);
    assert(a->target_mask==b->target_mask && a->carrier_sequence==b->carrier_sequence);
    assert(a->irq_entry_ticks==b->irq_entry_ticks);
    assert(a->typed_record.binding_generation==b->typed_record.binding_generation);
    assert(a->typed_record.event_sequence==b->typed_record.event_sequence);
    assert(a->typed_record.event_time_lower==b->typed_record.event_time_lower);
    assert(a->typed_record.uncertainty_width==b->typed_record.uncertainty_width);
    assert(a->typed_record.flags==b->typed_record.flags);
}
int main(int argc, char **argv)
{
    assert(argc==2); const char *mode=argv[1];
    record.epoch=5u; record.sequence=100u; record.irq_entry_ticks=UINT32_MAX+100ull;
    record.mailbox[3]=0x14u; record.mailbox[4]=0u; record.mailbox[5]=15u;
    encode(); vdc_priority_rx_snapshot_t out, saved;
    if (!strcmp(mode,"live_empty")) {
        memset(&saved,0xa5,sizeof(saved));out=saved;
        assert(!vdc_priority_rx_copy_live(&out) && !memcmp(&out,&saved,sizeof(out)));
        record.mailbox[3]=0x10u;send();
        assert(!vdc_priority_rx_copy_live(&out) && !memcmp(&out,&saved,sizeof(out)));
    } else if (!strcmp(mode,"live_read")) {
        saved=send();assert(vdc_priority_rx_copy_live(&out));expect_retained(&out,&saved);
        assert(!vdc_priority_rx_copy_live(NULL));
    } else if (!strcmp(mode,"live_wrong_core") || !strcmp(mode,"live_stop") || !strcmp(mode,"live_collision")) {
        send();memset(&saved,0xa5,sizeof(saved));out=saved;
        if (!strcmp(mode,"live_wrong_core")) core=0u;
        else if (!strcmp(mode,"live_stop")) vdc_priority_rx_core1(NULL);
        else {collision_guard=&s_priority_rx_guard;collision_word=s_priority_rx_words+3u;}
        assert(!vdc_priority_rx_copy_live(&out) && !memcmp(&out,&saved,sizeof(out)));
    } else if (!strcmp(mode,"initial_stop")) {
        out=get(); assert(out.schema==1u && !out.active && !out.have_record);
        vdc_priority_rx_core1(NULL); out=get();
        assert(!out.active && out.last_status==VDC_PRIORITY_RX_RETIRED);
        out=send(); assert(out.active && out.unique_count==1u);
    } else if (!strcmp(mode,"typed_zero")) {
        out=send(); assert(out.active && out.have_record && out.epoch==5u);
        assert(out.carrier_count==1u && out.typed_accept_count==1u && out.unique_count==1u);
        assert(out.last_status==VDC_PRIORITY_RX_UNIQUE && !out.typed_reject_count);
        assert(out.typed_record.event_sequence==0u && out.carrier_sequence==100u);
        assert(out.typed_record.binding_generation==42u && out.irq_entry_ticks==UINT32_MAX+100ull);
    } else if (!strcmp(mode,"ordinary_unknown")) {
        saved=send(); record.mailbox[3]=0x10u; out=send(); expect_retained(&out,&saved);
        record.mailbox[3]=0x7fu; out=send(); expect_retained(&out,&saved);
        assert(out.carrier_count==3u && out.typed_accept_count==1u && !out.typed_reject_count);
        assert(out.last_status==VDC_PRIORITY_RX_OTHER_CLASS);
    } else if (!strcmp(mode,"duplicate")) {
        out=send(); ++record.sequence; ++record.irq_entry_ticks; out=send();
        assert(out.typed_accept_count==2u && out.unique_count==1u && out.duplicate_count==1u);
        assert(out.carrier_sequence==101u && out.last_status==VDC_PRIORITY_RX_DUPLICATE);
    } else if (!strcmp(mode,"full_event")) {
        out=send(); typed.event_sequence=65536u; encode(); out=send();
        assert(out.unique_count==2u && !out.duplicate_count && out.typed_record.event_sequence==65536u);
    } else if (!strcmp(mode,"binding_generation")) {
        out=send(); ++typed.binding_generation; encode(); out=send();
        assert(out.epoch==5u && out.unique_count==2u && !out.duplicate_count);
    } else if (!strcmp(mode,"source_identity")) {
        out=send(); record.mailbox[4]=1u; out=send();
        assert(out.unique_count==2u && out.source_slot==1u && !out.duplicate_count);
    } else if (!strcmp(mode,"conflict_time") || !strcmp(mode,"conflict_width")) {
        saved=send(); ++record.sequence;
        if (!strcmp(mode,"conflict_time")) ++typed.event_time_lower; else ++typed.uncertainty_width;
        encode(); out=send(); expect_retained(&out,&saved);
        assert(out.last_status==VDC_PRIORITY_RX_CONFLICT && out.conflict_count==1u);
        assert(out.typed_reject_count==1u && out.typed_accept_count==1u && !out.duplicate_count);
    } else if (!strcmp(mode,"zero_generation") || !strcmp(mode,"zero_width") ||
               !strcmp(mode,"bad_flags") || !strcmp(mode,"interval_overflow") ||
               !strcmp(mode,"seq16_mismatch")) {
        saved=send(); ++record.sequence;
        if (!strcmp(mode,"zero_generation")) memset(record.mailbox+8u,0,4u);
        if (!strcmp(mode,"zero_width")) memset(record.mailbox+24u,0,4u);
        if (!strcmp(mode,"bad_flags")) record.mailbox[28]=1u;
        if (!strcmp(mode,"interval_overflow")) memset(record.mailbox+16u,255,8u);
        if (!strcmp(mode,"seq16_mismatch")) ++record.mailbox[6];
        out=send(); expect_retained(&out,&saved);
        assert(out.typed_reject_count==1u && out.typed_accept_count==1u);
        assert(out.last_status==(!strcmp(mode,"seq16_mismatch") ?
            VDC_PRIORITY_RX_SEQUENCE_REJECT : VDC_PRIORITY_RX_CODEC_REJECT));
    } else if (!strcmp(mode,"epoch_reset") || !strcmp(mode,"carrier_wrap_epoch")) {
        record.sequence=UINT32_MAX; out=send(); out=send(); ++record.epoch;
        record.sequence=0u; out=send();
        assert(out.epoch==6u && out.carrier_count==1u && out.unique_count==1u);
        assert(!out.duplicate_count && out.typed_record.binding_generation==42u);
        ++record.epoch; record.mailbox[3]=0x10u; out=send();
        assert(!out.have_record && !out.unique_count && !out.typed_record.binding_generation);
    } else if (!strcmp(mode,"stop_retention")) {
        saved=send(); vdc_priority_rx_core1(NULL); out=get(); expect_retained(&out,&saved);
        assert(!out.active && out.last_status==VDC_PRIORITY_RX_RETIRED);
        saved=out; out=send(); assert(!memcmp(&out,&saved,sizeof(out)));
        ++record.epoch; out=send(); assert(out.active && out.carrier_count==1u);
    } else if (!strcmp(mode,"zero_epoch")) {
        saved=send(); record.epoch=0u; out=send(); assert(!memcmp(&out,&saved,sizeof(out)));
    } else if (!strcmp(mode,"guard_busy") || !strcmp(mode,"guard_collision")) {
        out=send(); memset(&saved,0xa5,sizeof(saved)); out=saved;
        assert(!vdc_dpll_manager_get_priority_rx(NULL));
        if (!strcmp(mode,"guard_busy")) ++s_priority_rx_guard;
        else { collision_guard=&s_priority_rx_guard; collision_word=s_priority_rx_words+3u; }
        assert(!vdc_dpll_manager_get_priority_rx(&out) && !memcmp(&out,&saved,sizeof(out)));
    } else if (!strcmp(mode,"counter_saturation")) {
        out=send(); s_priority_rx.carrier_count=UINT32_MAX;
        s_priority_rx.typed_accept_count=UINT32_MAX; s_priority_rx.duplicate_count=UINT32_MAX;
        s_priority_rx.unique_count=UINT32_MAX; s_priority_rx.typed_reject_count=UINT32_MAX;
        s_priority_rx.conflict_count=UINT32_MAX;
        out=send(); assert(out.duplicate_count==UINT32_MAX && out.carrier_count==UINT32_MAX);
        ++typed.event_sequence; encode(); out=send(); assert(out.unique_count==UINT32_MAX);
        ++typed.event_time_lower; encode(); out=send(); assert(out.conflict_count==UINT32_MAX);
        assert(out.typed_accept_count==UINT32_MAX && out.typed_reject_count==UINT32_MAX);
    } else if (!strcmp(mode,"adjacent_identity")) {
        out=send(); ++typed.event_sequence; encode(); out=send();
        --typed.event_sequence; encode(); out=send();
        assert(out.unique_count==3u && !out.duplicate_count); /* No history/control admission. */
    } else if (!strcmp(mode,"input_immutable")) {
        const tdma_priority_rx_record_t original=record;
        out=send(); assert(!memcmp(&record,&original,sizeof(record)));
    } else assert(!"unknown case");
    printf("snapshot=%zu static=%zu decoded=%zu direct RX passed\n",sizeof(out),
        sizeof(s_priority_rx)+sizeof(s_priority_rx_words)+sizeof(s_priority_rx_guard)+
        sizeof(s_priority_rx_decode),sizeof(typed));
    return 0;
}
'''
