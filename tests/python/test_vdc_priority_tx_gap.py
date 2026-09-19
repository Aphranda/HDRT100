"""Exercise finite reference-offer gaps through the real Core1 provider."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

from test_vdc_priority_tx import HARNESS as PROVIDER_HARNESS, ROOT


@pytest.fixture(scope="module", params=[(6, False), (6, True), (8, False), (8, True)])
def gap_executable(request, tmp_path_factory):
    capacity, short_enums = request.param
    directory = tmp_path_factory.mktemp(f"priority-gap-{capacity}-{int(short_enums)}")
    source = directory / "gap.c"
    source.write_text(PROVIDER_HARNESS.replace("int main(", "int provider_original_main(")
                      + HARNESS, encoding="utf-8")
    compiler = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    exe = directory / ("gap.exe" if os.name == "nt" else "gap")
    command = [compiler, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               f"-DPROJECT_NODE_CAPACITY={capacity}",
               *(["-fshort-enums"] if short_enums else []),
               *[f"-I{ROOT / 'components' / name / 'inc'}" for name in (
                   "tdma", "vdc_domain", "vdc_dpll_manager", "distributed_refmem",
                   "calibration_manager", "ota_manager")],
               f"-I{ROOT / 'components/vdc_dpll_manager/src'}", str(source),
               str(ROOT / "components/vdc_dpll_manager/src/vdc_priority_codec.c"),
               "-o", str(exe)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=60)
    assert result.returncode == 0, result.stdout + result.stderr
    return exe


CASES = [
    "default_off", "configure", "wrong_core", "no_owner", "running", "no_session",
    "wrong_generation", "invalid_parameters", "clear", "session_before_bind",
    "anchor_valid_source", "zero_delay", "before_begin", "at_begin", "last_tick",
    "at_end_old", "in_gap_source", "at_end_new", "post_end_new", "projection_retry",
    "stop_waiting", "stop_active", "stop_recovering", "stop_done", "disable",
    "session", "role", "clock_epoch", "source_epoch", "config", "source_sequence",
    "new_generation", "guard", "null_readback", "max_parameters", "max_duration",
    "missed", "clock_backwards", "counter_saturation", "near_u64_limit",
]


@pytest.mark.parametrize("case", CASES)
def test_reference_offer_gap(gap_executable: Path, case):
    result = subprocess.run([str(gap_executable), case], capture_output=True,
                            text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
    assert "priority gap passed" in result.stdout


HARNESS = r'''
static bool configure_gap(uint32_t generation, uint32_t after, uint32_t duration) {
    const unsigned saved=core; core=0u;
    const bool ok=vdc_dpll_manager_set_priority_tx_gap(generation,after,duration);
    core=saved; return ok;
}
static vdc_priority_tx_gap_snapshot_t gap_snapshot(void) {
    vdc_priority_tx_gap_snapshot_t out;
    assert(vdc_dpll_manager_get_priority_tx_gap(&out)); return out;
}
static vdc_priority_tx_gap_config_t gap_config(void) {
    vdc_priority_tx_gap_config_t out;
    assert(vdc_dpll_manager_get_priority_tx_gap_config(&out)); return out;
}
static void gap_empty(void) {
    uint8_t mailbox[32], saved[32]; memset(mailbox,0xa5,32u); memcpy(saved,mailbox,32u);
    const vdc_priority_tx_snapshot_t before=snapshot();
    assert(vdc_priority_tx_core1(&config,mailbox)==TDMA_PRIORITY_TX_EMPTY);
    assert(!memcmp(mailbox,saved,32u));
    const vdc_priority_tx_snapshot_t after=snapshot();
    assert(after.rejected==before.rejected && after.encoded==before.encoded);
    assert(!memcmp(after.mailbox,before.mailbox,32u));
}
static void source_at(uint64_t ticks) {
    ++raw.sequence; raw.identity+=9u; raw.published_version+=2u;
    raw.timer_lower=ticks; raw.timer_upper=ticks; now=ticks;
}
static void arm_normal(uint8_t *mailbox) {
    assert(configure_gap(101u,1u,2u)); offer(mailbox);
    assert(gap_snapshot().state==VDC_PRIORITY_GAP_WAITING);
    assert(gap_snapshot().anchor_raw==1200u);
}
int main(int argc,char **argv) {
    assert(argc==2); const char *c=argv[1]; init(); uint8_t mailbox[32];
    const uint64_t begin=1200u+250000u, end=1200u+750000u;
    if(!strcmp(c,"default_off")) {
        assert(!gap_config().generation); offer(mailbox); now=end; offer(mailbox);
        assert(gap_snapshot().schema==1u && gap_snapshot().state==VDC_PRIORITY_GAP_DISABLED);
        assert(!gap_snapshot().suppressed && !gap_snapshot().anchor_raw);
    } else if(!strcmp(c,"configure")) {
        assert(configure_gap(101u,17u,29u));
        const vdc_priority_tx_gap_config_t g=gap_config();
        assert(g.generation==101u && g.session==session && g.after_ms==17u && g.duration_ms==29u);
        offer(mailbox); assert(gap_snapshot().generation==101u && gap_snapshot().session==session);
    } else if(!strcmp(c,"wrong_core") || !strcmp(c,"no_owner") || !strcmp(c,"running") ||
              !strcmp(c,"no_session") || !strcmp(c,"wrong_generation")) {
        assert(configure_gap(101u,1u,2u));
        const vdc_priority_tx_gap_config_t saved=gap_config();
        if(!strcmp(c,"wrong_core")) assert(!vdc_dpll_manager_set_priority_tx_gap(101u,3u,4u));
        else if(!strcmp(c,"no_owner")) { s_vdc_tdma_service=NULL; assert(!configure_gap(101u,3u,4u)); }
        else if(!strcmp(c,"running")) {
            stopped=false; assert(!configure_gap(101u,3u,4u)); assert(!configure_gap(0u,0u,0u));
        } else if(!strcmp(c,"no_session")) { session=0u; assert(!configure_gap(101u,3u,4u)); }
        else { assert(!configure_gap(100u,3u,4u)); assert(!configure_gap(102u,3u,4u)); }
        const vdc_priority_tx_gap_config_t after=gap_config(); assert(!memcmp(&saved,&after,sizeof(saved)));
    } else if(!strcmp(c,"invalid_parameters")) {
        assert(!configure_gap(101u,0u,0u)); assert(!configure_gap(101u,2u,0u));
        assert(!configure_gap(0u,1u,0u)); assert(!configure_gap(0u,0u,1u));
        assert(!configure_gap(0u,1u,1u)); assert(!gap_config().generation);
    } else if(!strcmp(c,"clear")) {
        assert(configure_gap(101u,1u,2u)); assert(configure_gap(0u,0u,0u));
        const vdc_priority_tx_gap_config_t g=gap_config();
        assert(!g.generation && !g.session && !g.after_ms && !g.duration_ms);
        offer(mailbox); now=begin; offer(mailbox); assert(gap_snapshot().state==VDC_PRIORITY_GAP_DISABLED);
    } else if(!strcmp(c,"session_before_bind")) {
        assert(configure_gap(101u,1u,2u)); ++session; committed.session=session;
        offer(mailbox); now=begin; offer(mailbox);
        assert(gap_snapshot().state==VDC_PRIORITY_GAP_CANCELLED && !gap_snapshot().suppressed);
    } else if(!strcmp(c,"anchor_valid_source")) {
        assert(configure_gap(101u,1u,2u)); source_ok=false;
        expect_empty(VDC_PRIORITY_TX_REJECT_SOURCE,false); assert(!gap_snapshot().anchor_raw);
        source_ok=true; clock_ok=false; expect_empty(VDC_PRIORITY_TX_REJECT_MODEL,false);
        assert(!gap_snapshot().anchor_raw); clock_ok=true; now=9000u; offer(mailbox);
        assert(gap_snapshot().anchor_raw==9000u);
    } else if(!strcmp(c,"zero_delay")) {
        assert(configure_gap(101u,0u,2u)); gap_empty();
        assert(gap_snapshot().state==VDC_PRIORITY_GAP_ACTIVE && !snapshot().have_offer);
        source_at(1200u+500000u); offer(mailbox);
        assert(gap_snapshot().state==VDC_PRIORITY_GAP_DONE && gap_snapshot().resume_sequence==raw.sequence);
    } else if(!strcmp(c,"before_begin") || !strcmp(c,"at_begin") || !strcmp(c,"last_tick")) {
        arm_normal(mailbox); now=begin-1u; offer(mailbox);
        assert(gap_snapshot().state==VDC_PRIORITY_GAP_WAITING && !gap_snapshot().suppressed);
        if(strcmp(c,"before_begin")) {
            now=!strcmp(c,"at_begin") ? begin : end-1u; gap_empty();
            const vdc_priority_tx_gap_snapshot_t g=gap_snapshot();
            assert(g.state==VDC_PRIORITY_GAP_ACTIVE && g.suppressed==1u);
            assert(g.first_suppressed_raw==now && g.before_sequence==47u);
        }
    } else if(!strcmp(c,"at_end_old") || !strcmp(c,"in_gap_source") ||
              !strcmp(c,"at_end_new") || !strcmp(c,"post_end_new") || !strcmp(c,"projection_retry")) {
        arm_normal(mailbox); now=begin; gap_empty();
        if(!strcmp(c,"in_gap_source")) source_at(end-1u);
        now=end; gap_empty();
        assert(s_priority_gap.state==VDC_PRIORITY_GAP_RECOVERING && !s_priority_gap.resume_raw);
        if(strcmp(c,"at_end_old") && strcmp(c,"in_gap_source")) {
            source_at(!strcmp(c,"post_end_new") ? end+100u : end);
            if(!strcmp(c,"projection_retry")) {
                projection_ok=false; expect_empty(VDC_PRIORITY_TX_REJECT_PROJECTION,false);
                assert(s_priority_gap.state==VDC_PRIORITY_GAP_RECOVERING);
                projection_ok=true;
            }
            offer(mailbox); const vdc_priority_tx_gap_snapshot_t g=gap_snapshot();
            assert(g.state==VDC_PRIORITY_GAP_DONE && g.resume_raw==now);
            assert(g.resume_sequence==raw.sequence && g.resume_sequence>g.before_sequence);
            assert(g.suppressed==2u && g.first_suppressed_raw==begin && g.last_suppressed_raw==end);
            source_at(now+250000u); offer(mailbox); assert(gap_snapshot().suppressed==2u);
        }
    } else if(!strncmp(c,"stop_",5u)) {
        arm_normal(mailbox);
        if(strcmp(c,"stop_waiting")) { now=begin; gap_empty(); }
        if(!strcmp(c,"stop_recovering")) { now=end; gap_empty(); }
        if(!strcmp(c,"stop_done")) { source_at(end); offer(mailbox); }
        assert(vdc_priority_tx_core1(NULL,NULL)==TDMA_PRIORITY_TX_EMPTY);
        const vdc_priority_tx_gap_snapshot_t g=gap_snapshot();
        assert(g.state==(!strcmp(c,"stop_done") ? VDC_PRIORITY_GAP_DONE : VDC_PRIORITY_GAP_CANCELLED));
        expect_empty(VDC_PRIORITY_TX_REJECT_RETIRED,true);
        assert(gap_snapshot().state==g.state);
    } else if(!strcmp(c,"disable")) {
        arm_normal(mailbox); now=begin; gap_empty(); assert(request_generation(0u));
        assert(vdc_priority_tx_core1(&config,mailbox)==TDMA_PRIORITY_TX_DISABLED);
        assert(gap_snapshot().state==VDC_PRIORITY_GAP_CANCELLED);
    } else if(!strcmp(c,"session") || !strcmp(c,"role") || !strcmp(c,"clock_epoch") ||
              !strcmp(c,"source_epoch") || !strcmp(c,"config") || !strcmp(c,"source_sequence")) {
        arm_normal(mailbox); now=begin; gap_empty();
        uint32_t reason=VDC_PRIORITY_TX_REJECT_BINDING;
        if(!strcmp(c,"session")) ++session;
        else if(!strcmp(c,"role")) ++committed.role_generation;
        else if(!strcmp(c,"clock_epoch")) ++committed.clock_epoch_id;
        else if(!strcmp(c,"source_epoch")) ++raw.epoch;
        else if(!strcmp(c,"config")) ++config.owner_config_seq;
        else { --raw.sequence; reason=VDC_PRIORITY_TX_REJECT_SEQUENCE; }
        expect_empty(reason,true); assert(gap_snapshot().state==VDC_PRIORITY_GAP_CANCELLED);
    } else if(!strcmp(c,"new_generation")) {
        arm_normal(mailbox); now=begin; gap_empty();
        assert(vdc_priority_tx_core1(NULL,NULL)==TDMA_PRIORITY_TX_EMPTY);
        assert(request_generation(102u)); offer(mailbox); now=end; offer(mailbox);
        assert(gap_snapshot().state==VDC_PRIORITY_GAP_DISABLED && !gap_snapshot().suppressed);
        assert(gap_config().generation==101u);
    } else if(!strcmp(c,"guard")) {
        arm_normal(mailbox);
        vdc_priority_tx_gap_snapshot_t s, saved; memset(&saved,0xa5,sizeof(saved)); s=saved;
        ++s_priority_gap_status_guard; assert(!vdc_dpll_manager_get_priority_tx_gap(&s));
        assert(!memcmp(&s,&saved,sizeof(s))); --s_priority_gap_status_guard;
        vdc_priority_tx_gap_config_t cfg, cfg_saved;
        memset(&cfg_saved,0xa5,sizeof(cfg_saved)); cfg=cfg_saved;
        ++s_priority_gap_config_guard; assert(!vdc_dpll_manager_get_priority_tx_gap_config(&cfg));
        assert(!memcmp(&cfg,&cfg_saved,sizeof(cfg))); --s_priority_gap_config_guard;
        s_priority_gap_status_guard=UINT32_MAX-1u; now=begin; gap_empty();
        assert(!s_priority_gap_status_guard && gap_snapshot().state==VDC_PRIORITY_GAP_ACTIVE);
        s_priority_gap_config_guard=UINT32_MAX-1u; assert(configure_gap(101u,3u,4u));
        assert(!s_priority_gap_config_guard && gap_config().after_ms==3u);
    } else if(!strcmp(c,"null_readback")) {
        assert(!vdc_dpll_manager_get_priority_tx_gap(NULL));
        assert(!vdc_dpll_manager_get_priority_tx_gap_config(NULL));
    } else if(!strcmp(c,"max_parameters") || !strcmp(c,"max_duration")) {
        raw.tick_hz=UINT32_MAX;
        const uint32_t after=!strcmp(c,"max_parameters") ? UINT32_MAX : 0u;
        assert(configure_gap(101u,after,UINT32_MAX));
        if(after) offer(mailbox); else gap_empty();
        const uint64_t offset=(uint64_t)UINT32_MAX*UINT32_MAX/1000u;
        const uint64_t at=1200u+(after ? offset : 0u);
        if(after) { source_at(at-1u); offer(mailbox); source_at(at); gap_empty(); }
        source_at(at+offset-1u); gap_empty(); source_at(at+offset); offer(mailbox);
        const vdc_priority_tx_gap_snapshot_t g=gap_snapshot();
        assert(g.state==VDC_PRIORITY_GAP_DONE && g.first_suppressed_raw==at);
        assert(g.resume_raw==at+offset && g.after_ms==after && g.duration_ms==UINT32_MAX);
    } else if(!strcmp(c,"missed")) {
        arm_normal(mailbox); source_at(end); offer(mailbox);
        const vdc_priority_tx_gap_snapshot_t g=gap_snapshot();
        assert(g.state==VDC_PRIORITY_GAP_MISSED && !g.suppressed && !g.resume_raw);
        source_at(end+100u); offer(mailbox); assert(gap_snapshot().state==VDC_PRIORITY_GAP_MISSED);
    } else if(!strcmp(c,"clock_backwards")) {
        arm_normal(mailbox); now=1100u; offer(mailbox);
        assert(gap_snapshot().state==VDC_PRIORITY_GAP_CANCELLED && !gap_snapshot().suppressed);
    } else if(!strcmp(c,"counter_saturation")) {
        arm_normal(mailbox); now=begin; gap_empty(); s_priority_gap.suppressed=UINT32_MAX;
        now=begin+1u; gap_empty(); assert(s_priority_gap.suppressed==UINT32_MAX);
        assert(vdc_priority_tx_core1(NULL,NULL)==TDMA_PRIORITY_TX_EMPTY);
        assert(gap_snapshot().suppressed==UINT32_MAX);
    } else if(!strcmp(c,"near_u64_limit")) {
        now=UINT64_MAX-800000u; raw.timer_lower=now; raw.timer_upper=now;
        assert(configure_gap(101u,1u,2u)); offer(mailbox);
        const uint64_t anchor=now; source_at(anchor+250000u); gap_empty();
        source_at(anchor+750000u); offer(mailbox);
        assert(gap_snapshot().state==VDC_PRIORITY_GAP_DONE && gap_snapshot().resume_raw==now);
    } else assert(!"unknown gap scenario");
    printf("priority gap passed: %s\n",c); return 0;
}
'''
