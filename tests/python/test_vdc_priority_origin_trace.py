"""Native TIMER1 schema 5 from the real TX/Domain/shared SRAM recorder.

The Python checker reads public bytes independently and computes rational
TIMER1 projection. Product decoder replay and corrupted evidence are
tested separately. Host observations imply no physical lock qualification.
"""
from fractions import Fraction
import math
import struct
import zlib

import pytest

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import ROOT, compile_executable
from test_vdc_priority_follow import domain_sources
from test_vdc_priority_trace import trace_executable, execute, STATUS_FIELDS  # noqa: F401
from tools.vdc_priority_trace import vdc_priority_trace as decoder


@pytest.fixture(scope="module")
def origin_executable(trace_executable, tmp_path_factory):
    source = trace_executable.with_suffix(".c").read_text(encoding="utf-8")
    for name in ("vdc_dpll_manager_priority_sync_generation", "vdc_priority_tx_origin_trace_eligible_core1"):
        source = source.replace(ingress_definition(source, name), "", 1)
    source = source.replace(ingress_definition(source, "tdma_runtime_owner_get_origin_reference_epoch"),
        "bool tdma_runtime_owner_get_origin_reference_epoch(uint32_t *out);", 1)
    source = source.replace("int main(int argc,char **argv)", "int previous_trace_main(int argc,char **argv)", 1)
    source = source.replace("const uint32_t expected=168u+s.record_count*100u;",
        "const uint32_t expected=(s.schema==5u?264u:168u)+s.record_count*100u;")
    source += INPUTS + (ROOT / "components/vdc_dpll_manager/src/vdc_priority_tx.inc").read_text(encoding="utf-8")
    source += CASES
    return compile_executable(tmp_path_factory.mktemp("origin-trace"), "origin_trace", source,
        domain_sources() + [ROOT / "components/vdc_dpll_manager/src/vdc_feedback_match.c",
                           ROOT / "components/distributed_refmem/src/refmem_sync_vdc_feedback.c",
                           ROOT / "components/vdc_dpll_manager/src/vdc_priority_codec.c"])


def native_origin(raw):
    magic, schema, header_size, record_size, crc = struct.unpack_from("<5I", raw)
    assert (magic, schema, header_size, record_size) == (0x52545056, 5, 264, 100)
    status = dict(zip(STATUS_FIELDS, struct.unpack_from("<37I", raw, 20), strict=True))
    assert status["schema"] == 5 and status["request_seq"] == status["ack_seq"]
    assert status["state"] == 3 and status["sample_interval_ms"] == 0
    assert status["match_count"] == status["decision_count"] == 0
    assert len(raw) == 264 + status["record_count"]*100 and zlib.crc32(raw[264:]) == crc
    ext = struct.unpack_from("<24I", raw, 168)
    assert not any(ext[7:])
    fields = "index kind event token hz raw_lo raw_hi now local_lo local_hi base output rate phase seq lower width".split()
    records = [dict(zip(fields, struct.unpack_from("<5I7QiiIQI", raw, offset), strict=True))
               for offset in range(264, len(raw), 100)]
    for index,row in enumerate(records):
        assert (row["index"],row["kind"]) == (index,3)
        assert row["raw_lo"] <= row["raw_hi"] <= row["now"]
        lo=math.floor(Fraction(row["raw_lo"]*10**9,row["hz"]))
        hi=math.ceil(Fraction(row["raw_hi"]*10**9,row["hz"]))
        assert (lo,hi)==(row["local_lo"],row["local_hi"])
        def output(local):
            elapsed=local-row["base"]
            assert elapsed>=0
            return row["output"]+elapsed+int(Fraction(elapsed*row["rate"],10**9))+row["phase"]
        assert (row["lower"],row["lower"]+row["width"])==(output(lo),output(hi))
    if records:
        assert ext[6]==records[-1]["event"] and ext[0]>0 and ext[1]>0
    else:
        assert not any(ext)
    return status,ext,records


@pytest.mark.parametrize("case,count", [("flow",12), ("model",12), ("horizon",12), ("empty",0),
                                       ("capacity",76), ("failed_offer",2), ("repeat",1), ("wide",1)])
def test_native_origin_and_independent_half_open_replay(origin_executable, case, count):
    raw=execute(origin_executable,case)
    status,ext,records=native_origin(raw)
    assert len(records)==count
    product=decoder.decode(raw,1)
    assert product["replay_matches_encoded"] and not product["physical_lock_qualified"]
    assert len(product["records"])==count
    if case=="flow":
        assert records[1]["width"]==records[0]["width"]
        assert not any(ext[7:])
    if case in ("model","horizon"):
        assert not any(ext[7:])
    if case=="capacity":
        assert status["reason"]==4  # Public FULL reason.
    if case=="wide":
        assert records[0]["width"]>65535


@pytest.mark.parametrize("case", ["warm_arm", "pending_arm_race", "missing_generation", "binding_freeze", "release"])
def test_origin_storage_lifecycle_requires_complete_start(origin_executable, case):
    execute(origin_executable,case)


@pytest.fixture(scope="module")
def origin_bytes(origin_executable):
    return execute(origin_executable,"flow")


def test_timer1_capture_cannot_be_relabelled_as_bridge_schema(origin_bytes):
    raw = bytearray(origin_bytes)
    struct.pack_into('<I', raw, 4, 2)
    struct.pack_into('<I', raw, 20, 2)
    struct.pack_into('<I', raw, 16, zlib.crc32(raw[264:]))
    with pytest.raises(ValueError):
        decoder.decode(bytes(raw), 1)


@pytest.mark.parametrize("offset,value,size", [
    (4,1,4),(8,168,4),(12,96,4),(20+12*4,1,4),(168+8*4,7,4),
    (168+9*4,9,4),(168+80,1,8),(264+4,2,4),(264+16,0,4),
    (264+20,2**64-1,8),(264+36,2**64-1,8),(264+52,2**64-1,8),
    (264+52,10000000001,8),(264+88,1,8),(264+96,0,4),
    (364+8,0,4),(364+76,1,4),
])
def test_decoder_rejects_corrupt_replay_even_with_recomputed_payload_crc(origin_bytes,offset,value,size):
    raw=bytearray(origin_bytes)
    struct.pack_into("<I" if size==4 else "<Q",raw,offset,value)
    struct.pack_into("<I",raw,16,zlib.crc32(raw[264:]))
    with pytest.raises(ValueError):
        decoder.decode(bytes(raw),1)


def test_decoder_rejects_local_overflow_despite_negative_rate_cancellation(origin_bytes):
    raw=bytearray(origin_bytes)
    struct.pack_into("<Q",raw,264+52,(2**64-1)//1000*1000)
    struct.pack_into("<i",raw,264+76,-999999999)
    struct.pack_into("<I",raw,16,zlib.crc32(raw[264:]))
    with pytest.raises(ValueError,match="local interval"):
        decoder.decode(bytes(raw),1)


def test_decoder_rejects_negative_latent_local_before_domain(origin_bytes):
    raw=bytearray(origin_bytes)
    struct.pack_into("<Q",raw,264+52,0)
    struct.pack_into("<I",raw,16,zlib.crc32(raw[264:]))
    with pytest.raises(ValueError,match="local interval"):
        decoder.decode(bytes(raw),1)


@pytest.mark.parametrize("page_size", [1,7,64,128])
def test_stop_read_crosses_extended_header_and_whole_file_crc(origin_bytes,page_size):
    def query(command):
        capture,offset,size=map(int,command.split(" ",1)[1].split(","))
        assert capture==1
        return f'{offset},{size},{len(origin_bytes)},{zlib.crc32(origin_bytes)},"{origin_bytes[offset:offset+size].hex()}"'
    actual,pages=decoder.download_capture(query,1,page_size)
    assert actual==origin_bytes and len(pages)>1


INPUTS = r'''
#include "tdma_origin_plan.h"
static tdma_origin_raw_reference_t origin_raw;
static tdma_ring_runtime_config_t origin_config;
static bool origin_epoch_ok=true;
bool tdma_runtime_owner_get_origin_raw_reference(tdma_origin_raw_reference_t *out)
{ *out=origin_raw;return true; }
bool tdma_runtime_owner_get_origin_reference_epoch(uint32_t *out)
{ if(!origin_epoch_ok)return false;*out=origin_raw.epoch;return true; }
#define VDC_PRIORITY_TRACE_ORIGIN_HOOK(evidence) priority_trace_origin_core1(evidence)
'''


CASES = r'''
static void origin_setup(void)
{
    (void)origin_epoch;(void)match_copy_race;
    setup(777);stopped_ring();
    s_vdc_domain.schedule.local_slot_id=0u;ring.local_slot_id=0u;
    s_vdc_domain.dco.dco_update_seq=5u;
    origin_config=(tdma_ring_runtime_config_t){.enabled=1u,.node_count=4u,
        .local_slot_id=0u,.reference_slot_id=0u,.up_group_id=1u,.down_group_id=2u,
        .ring_profile_crc32=0x456u,.schedule_crc32=s_vdc_domain.dco.tdma_schedule_crc32,
        .operating_profile_crc32=0x789u,.cycle_period_ns=1500000u,
        .geometry_generation=12u,.owner_config_seq=23u};
    assert(vdc_dpll_manager_set_priority_sync(101u));
    raw_now=2499999999u;publish();
    raw_now=2500000100u;now_ns=10000000000ull;now_ms=10000u;
    origin_raw=(tdma_origin_raw_reference_t){.epoch=29u,.sequence=47u,.identity=0x2468u,
        .published_version=90u,.tick_hz=BOARD_SYS_CLOCK_HZ,
        .timer_lower=2500000000u,.timer_upper=2500000004u};
}
static void origin_arm(void)
{
    stopped_ring();assert(vdc_dpll_manager_priority_trace_origin_arm(1u));trace_service();
    assert(trace_status().state==VDC_PRIORITY_TRACE_ARMED && trace_status().schema==5u);
    running_ring();trace_service();
}
static void origin_offer(void)
{
    uint8_t mailbox[32];assert(vdc_priority_tx_core1(&origin_config,mailbox)==TDMA_PRIORITY_TX_READY);
}
static void origin_fresh(uint64_t ticks)
{
    ++origin_raw.sequence;origin_raw.identity+=9u;origin_raw.published_version+=2u;
    origin_raw.timer_lower+=ticks;origin_raw.timer_upper+=ticks;raw_now+=ticks;
    now_ns=raw_now*4u/1000u*1000u;now_ms=(uint32_t)(now_ns/1000000u);
}
int main(int argc,char **argv)
{
#ifdef _WIN32
    _setmode(_fileno(stdout),_O_BINARY);
#endif
    assert(argc==2);const char *name=argv[1];origin_setup();
    if(!strcmp(name,"missing_generation")) {
        assert(vdc_dpll_manager_set_priority_sync(0u));
        assert(!vdc_dpll_manager_priority_trace_origin_arm(1u));return 0;
    }
    if(!strcmp(name,"warm_arm")) {
        running_ring();origin_offer();stopped_ring();
        assert(vdc_dpll_manager_priority_trace_origin_arm(1u));trace_service();
        assert(trace_status().state==VDC_PRIORITY_TRACE_REJECTED);return 0;
    }
    if(!strcmp(name,"pending_arm_race")) {
        assert(vdc_dpll_manager_priority_trace_origin_arm(1u));
        core=1u;origin_offer();trace_service();
        assert(trace_status().state==VDC_PRIORITY_TRACE_REJECTED && !trace_status().record_count);return 0;
    }
    origin_arm();
    if(!strcmp(name,"empty")){frozen_trace();export_trace();return 0;}
    if(!strcmp(name,"wide")) {
        origin_raw.timer_upper+=25000u;raw_now+=25000u;now_ns=raw_now*4u/1000u*1000u;
        origin_offer();frozen_trace();export_trace();return 0;
    }
    origin_offer();
    if(!strcmp(name,"repeat")) {
        for(unsigned i=0;i<10u;++i)origin_offer();
        assert(trace_status().record_count==1u);frozen_trace();export_trace();return 0;
    }
    if(!strcmp(name,"failed_offer")) {
        origin_fresh(125u);origin_epoch_ok=false;uint8_t mailbox[32];
        assert(vdc_priority_tx_core1(&origin_config,mailbox)==TDMA_PRIORITY_TX_EMPTY);
        assert(trace_status().record_count==1u);
        origin_epoch_ok=true;origin_offer();frozen_trace();export_trace();return 0;
    }
    if(!strcmp(name,"binding_freeze")) {
        vdc_priority_tx_origin_evidence_t other=s_priority_tx_work.evidence;++other.source_epoch;
        priority_trace_origin_core1(&other);
        assert(trace_status().state==VDC_PRIORITY_TRACE_FROZEN && trace_status().record_count==1u);return 0;
    }
    if(!strcmp(name,"release")) {
        frozen_trace();assert(vdc_dpll_manager_priority_trace_release());trace_service();
        reject_read(1u,0u,1u);assert(s_dpll_capture_pool_owner==DPLL_CAPTURE_POOL_LEGACY);return 0;
    }
    const unsigned limit=!strcmp(name,"capacity")?76u:12u;
    for(unsigned i=1u;i<limit;++i) {
        origin_fresh(!strcmp(name,"horizon") && i==6u?500000000u:125u);
        if(!strcmp(name,"model") && i==6u) {
            const uint64_t saved=raw_now;raw_now=origin_raw.timer_lower;
            ++s_vdc_domain.dco.period_adjust_ppb;++s_vdc_domain.dco.dco_update_seq;publish();raw_now=saved;
        }
        origin_offer();
    }
    frozen_trace();export_trace();return 0;
}
'''
