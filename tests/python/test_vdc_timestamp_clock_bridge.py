"""Run the production TIMER bridge against MMIO mutations, without SDK/HIL."""
import os
from pathlib import Path
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]

# RP2350 SDK 2.2 register encodings, independently fixed in the device fixture.
REGS = {
    "TIMER_SOURCE_CLK_SYS_VALUE_TICK": 0,
    "TIMER_SOURCE_CLK_SYS_VALUE_CLK_SYS": 1,
    "SYSCFG_PROC_CONFIG_BITS": 3,
    "CLOCKS_CLK_REF_CTRL_SRC_BITS": 3,
    "CLOCKS_CLK_REF_CTRL_SRC_VALUE_XOSC_CLKSRC": 2,
    "CLOCKS_CLK_REF_DIV_INT_BITS": 0xFF0000,
    "CLOCKS_CLK_REF_DIV_INT_LSB": 16,
    "CLOCKS_CLK_SYS_CTRL_SRC_BITS": 1,
    "CLOCKS_CLK_SYS_CTRL_SRC_VALUE_CLKSRC_CLK_SYS_AUX": 1,
    "CLOCKS_CLK_SYS_CTRL_AUXSRC_BITS": 0xE0,
    "CLOCKS_CLK_SYS_CTRL_AUXSRC_LSB": 5,
    "CLOCKS_CLK_SYS_CTRL_AUXSRC_VALUE_CLKSRC_PLL_SYS": 0,
    "CLOCKS_CLK_SYS_DIV_INT_BITS": 0xFFFF0000,
    "CLOCKS_CLK_SYS_DIV_INT_LSB": 16,
    "CLOCKS_CLK_SYS_DIV_FRAC_BITS": 0xFFFF,
    "CLOCKS_CLK_SYS_RESUS_CTRL_FRCE_BITS": 0x1000,
    "PLL_CS_REFDIV_BITS": 0x3F,
    "PLL_CS_LOCK_BITS": 0x80000000,
    "PLL_CS_BYPASS_BITS": 0x100,
    "PLL_PRIM_POSTDIV1_BITS": 0x70000,
    "PLL_PRIM_POSTDIV1_LSB": 16,
    "PLL_PRIM_POSTDIV2_BITS": 0x7000,
    "PLL_PRIM_POSTDIV2_LSB": 12,
    "PLL_PWR_DSMPD_BITS": 4,
    "TICKS_TIMER0_CTRL_ENABLE_BITS": 1,
    "TICKS_TIMER0_CTRL_RUNNING_BITS": 2,
    "TICKS_TIMER0_CYCLES_BITS": 0x1FF,
    "XOSC_CTRL_ENABLE_VALUE_ENABLE": 0xFAB,
    "XOSC_CTRL_ENABLE_LSB": 12,
    "XOSC_CTRL_FREQ_RANGE_VALUE_1_15MHZ": 0xAA0,
    "XOSC_STATUS_STABLE_BITS": 0x80000000,
    "XOSC_STATUS_ENABLED_BITS": 0x1000,
    "XOSC_STATUS_BADWRITE_BITS": 0x01000000,
    "XOSC_DORMANT_VALUE_WAKE": 0x77616B65,
}


@pytest.fixture(scope="module")
def bridge_executable(tmp_path_factory):
    directory = tmp_path_factory.mktemp("bridge")
    (directory / "hardware/structs").mkdir(parents=True)
    (directory / "pico.h").write_text("#define __not_in_flash_func(n) n\n", encoding="utf-8")
    (directory / "fake_device.h").write_text(
        "\n".join(f"#define {key} {value}u" for key, value in REGS.items()) + DEVICE_HEADER,
        encoding="utf-8")
    for name in ("clocks.h", "timer.h", "structs/pll.h", "structs/ticks.h",
                 "structs/xosc.h", "structs/syscfg.h"):
        (directory / "hardware" / name).write_text('#include "fake_device.h"\n', encoding="utf-8")
    source = directory / "bridge.c"
    source.write_text(HARNESS, encoding="utf-8")
    cc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    executables = {}
    for name, device, default_timer, xosc in (
        ("device", 1, 0, 12_000_000), ("host", 0, 0, 12_000_000),
        ("timer1-default", 1, 1, 12_000_000), ("xosc24", 1, 0, 24_000_000),
    ):
        executable = directory / (name + ".exe")
        command = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
                   f"-DPICO_ON_DEVICE={device}", "-DPICO_RP2350=1",
                   f"-DPICO_DEFAULT_TIMER={default_timer}", f"-DXOSC_HZ={xosc}u",
                   "-I" + str(directory), "-I" + str(ROOT / "components/vdc_domain/inc"),
                   "-I" + str(ROOT / "components/vdc_domain/src"), str(source), "-o", str(executable)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=60)
        (directory / (name + "-compile.log")).write_text(result.stdout + result.stderr, encoding="utf-8")
        assert result.returncode == 0, result.stdout + result.stderr
        executables[name] = executable
    return executables


@pytest.mark.parametrize("case", ["success", "unsupported-config", "changed-config",
                                  "bounded-rollover", "unavailable", "limits",
                                  "diagnostic-success", "diagnostic-rejection",
                                  "diagnostic-rollover", "diagnostic-uninitialized",
                                  "sticky-badwrite"])
def test_actual_device_bridge(bridge_executable, case):
    result = subprocess.run([str(bridge_executable["device"]), case], capture_output=True,
                            text=True, timeout=10)
    (bridge_executable["device"].parent / (case + ".log")).write_text(
        result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr
    assert "bridge passed" in result.stdout


@pytest.mark.parametrize("platform", ["host", "timer1-default", "xosc24"])
def test_unsupported_platform_preserves_output(bridge_executable, platform):
    result = subprocess.run([str(bridge_executable[platform]), "unsupported"],
                            capture_output=True, text=True, timeout=10)
    assert result.returncode == 0, result.stdout + result.stderr


DEVICE_HEADER = r'''
#ifndef FAKE_DEVICE_H
#define FAKE_DEVICE_H
#include <stdint.h>
enum { clk_ref=4, clk_sys=5, TICK_TIMER0=2 };
typedef struct { uint32_t pause,source,timelw,timehw,timerawl,timerawh,dbgpause; } timer_t;
typedef struct { uint32_t ctrl,div,selected; } clock_slice_t;
typedef struct { clock_slice_t clk[6]; struct { uint32_t ctrl,status; } resus; } clocks_t;
typedef struct { uint32_t cs,pwr,fbdiv_int,prim; } pll_t;
typedef struct { struct { uint32_t ctrl,cycles; } ticks[6]; } ticks_t;
typedef struct { uint32_t ctrl,status,dormant; } xosc_t;
typedef struct { uint32_t proc_config; } syscfg_t;
extern timer_t banks[2];
extern clocks_t clocks;
extern pll_t pll;
extern ticks_t ticks;
extern xosc_t xosc;
extern syscfg_t syscfg;
timer_t *fake_timer(unsigned);
uint32_t clock_get_hz(int);
#define timer0_hw fake_timer(0)
#define timer1_hw fake_timer(1)
#define PICO_DEFAULT_TIMER_INSTANCE() (&banks[PICO_DEFAULT_TIMER])
#define clocks_hw (&clocks)
#define pll_sys_hw (&pll)
#define ticks_hw (&ticks)
#define xosc_hw (&xosc)
#define syscfg_hw (&syscfg)
#endif
'''

HARNESS = r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "fake_device.h"
#include "vdc_timestamp_clock.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"FAIL line %d: %s\n",__LINE__,#c); return 1; } } while(0)
timer_t banks[2]; clocks_t clocks; pll_t pll; ticks_t ticks; xosc_t xosc; syscfg_t syscfg;
static unsigned timer_reads[2], clock_reads, mutation, trigger_bank, trigger_read;
static uint32_t hz;
static bool stop_raw;
static void mutate(unsigned code) {
    switch(code) {
    case 1: banks[0].pause=1; break;
    case 2: banks[1].pause=1; break;
    case 3: banks[0].source=1; break;
    case 4: banks[1].source=0; break;
    case 5: ticks.ticks[2].ctrl=1; break;
    case 6: ticks.ticks[2].ctrl=2; break;
    case 7: ticks.ticks[2].cycles=13; break;
    case 8: clocks.clk[4].ctrl=0; break;
    case 9: clocks.clk[4].selected=1; break;
    case 10: clocks.clk[4].div=0; break;
    case 11: clocks.clk[5].ctrl=0; break;
    case 12: clocks.clk[5].ctrl=33; break;
    case 13: clocks.clk[5].selected=1; break;
    case 14: clocks.clk[5].div=0; break;
    case 15: clocks.clk[5].div=65537; break;
    case 16: pll.cs &= ~0x80000000u; break;
    case 17: pll.cs |= 0x100; break;
    case 18: pll.cs &= ~63u; break;
    case 19: pll.pwr=0x24; break;
    case 20: pll.prim=0x60000; break;
    case 21: pll.prim=0x2000; break;
    case 22: pll.fbdiv_int=126; break;
    case 23: xosc.ctrl=0; break;
    case 24: xosc.status &= ~0x80000000u; break;
    case 25: xosc.status &= ~0x1000u; break;
    case 26: xosc.dormant=0x636f6d61; break;
    case 27: clocks.resus.ctrl=0x1000; break;
    case 28: clocks.resus.status=1; break;
    case 29: syscfg.proc_config=1; break;
    case 30: syscfg.proc_config=2; break;
    case 31: banks[0].dbgpause=2; break;
    case 32: ++hz; break;
    /* Still 1 MHz, but a changed configuration must not survive the bracket. */
    case 33: clocks.clk[4].div=2u<<16; ticks.ticks[2].cycles=6; break;
    /* Still the same sys rate with a different PLL configuration. */
    case 34: pll.cs=0x80000002u; pll.fbdiv_int=250; break;
    case 35: pll.fbdiv_int=15; break;
    case 36: pll.fbdiv_int=321; break;
    case 37: ticks.ticks[2].cycles=0; break;
    case 38: ticks.ticks[2].cycles=512; break;
    case 39: xosc.status |= 0x01000000; break;
    case 40: pll.pwr=0; break;
    case 41: banks[0].dbgpause=0; banks[1].dbgpause=0; break;
    case 42: xosc.status &= ~0x01000000u; break;
    }
}
timer_t *fake_timer(unsigned bank) {
    ++timer_reads[bank];
    if (bank==1 && !stop_raw) ++banks[1].timerawl;
    if (bank==trigger_bank && timer_reads[bank]==trigger_read) {
        if (mutation==100) { ++banks[bank].timerawh; banks[bank].timerawl=0; }
        else if (mutation==101) banks[bank].timerawl=0;
        else mutate(mutation);
    }
    return &banks[bank];
}
uint32_t clock_get_hz(int which) { (void)which; ++clock_reads; return hz; }
static void clean(void) {
    memset(banks,0,sizeof(banks)); memset(&clocks,0,sizeof(clocks));
    memset(&pll,0,sizeof(pll)); memset(&ticks,0,sizeof(ticks));
    memset(&xosc,0,sizeof(xosc)); memset(&syscfg,0,sizeof(syscfg));
    memset(timer_reads,0,sizeof(timer_reads)); clock_reads=0;
    mutation=trigger_bank=trigger_read=0; stop_raw=false;
    hz=125000000; banks[0].timerawl=1234567; banks[1].timerawl=1000;
    banks[1].source=1; banks[0].dbgpause=banks[1].dbgpause=6;
    clocks.clk[4]=(clock_slice_t){2,65536,4};
    clocks.clk[5]=(clock_slice_t){1,65536,2};
    pll=(pll_t){0x80000001u,4,125,0x62000};
    ticks.ticks[2].ctrl=3; ticks.ticks[2].cycles=12;
    xosc=(xosc_t){0xFABAA0,0x80001000u,0x77616b65};
    s_vdc_timestamp_clock_initialized=s_vdc_timestamp_clock_ready=true;
    s_vdc_timestamp_clock_tick_hz=hz;
}
static int rejects(uint32_t expected) {
    vdc_timestamp_clock_bridge_t out, before;
    memset(&out,0xA5,sizeof(out)); memcpy(&before,&out,sizeof(out));
    CHECK(!vdc_timestamp_clock_try_read_bridge(expected,&out));
    CHECK(memcmp(&before,&out,sizeof(out))==0);
    CHECK(timer_reads[0]<=11 && timer_reads[1]<=20 && clock_reads<=6);
    return 0;
}
int main(int argc,char **argv) {
    CHECK(argc==2); clean();
    if (!strcmp(argv[1],"unsupported")) {
        CHECK(rejects(hz)==0);
        CHECK(timer_reads[0]==0 && timer_reads[1]==0 && clock_reads==0);
        vdc_timestamp_clock_bridge_diagnostic_t d;
        memset(&d,0xA5,sizeof(d));
        CHECK(vdc_timestamp_clock_read_bridge_diagnostic(hz,&d));
        CHECK(d.schema==1 && d.platform_supported==0 && d.configuration_supported==0 && d.bridge_valid==0);
        CHECK(d.expected_hz==hz && d.timer0_sample_us==0 && d.timer1_sample_ticks==0);
        CHECK(d.bridge.raw_before==0 && d.bridge.local_ns==0 && d.bridge.raw_after==0);
        CHECK(timer_reads[0]==0 && timer_reads[1]==0 && clock_reads==0);
    } else if (!strcmp(argv[1],"success")) {
        for (unsigned mode=0; mode<5; ++mode) {
            clean();
            if (mode==1) { hz=250000000; pll.prim=0x32000; }
            if (mode==2) { clocks.clk[4].div=2u<<16; ticks.ticks[2].cycles=6; }
            if (mode==3) { hz=62500000; clocks.clk[5].div=2u<<16; }
            if (mode==4) { pll.cs=0x80000002u; pll.fbdiv_int=250; }
            s_vdc_timestamp_clock_tick_hz=hz;
            vdc_timestamp_clock_bridge_t out;
            CHECK(vdc_timestamp_clock_try_read_bridge(hz,&out));
            CHECK(out.raw_before==1007 && out.raw_after==1014);
            CHECK(out.local_ns==1234567000 && out.tick_hz==hz);
            CHECK(timer_reads[0]==11 && timer_reads[1]==20 && clock_reads==6);
            CHECK(banks[0].pause==0 && banks[1].pause==0 && banks[1].source==1);
        }
    } else if (!strcmp(argv[1],"unsupported-config")) {
        for (unsigned code=1; code<=40; ++code) {
            if (code==33 || code==34 || code==39) continue;
            clean(); mutate(code); CHECK(rejects(125000000)==0);
        }
    } else if (!strcmp(argv[1],"changed-config")) {
        for (unsigned code=1; code<=41; ++code) {
            clean(); trigger_bank=0; trigger_read=6; mutation=code;
            CHECK(rejects(125000000)==0);
        }
    } else if (!strcmp(argv[1],"bounded-rollover")) {
        const unsigned cases[][3]={{0,6,100},{1,7,100},{1,14,100},{1,11,100},{1,11,101}};
        for (unsigned i=0; i<sizeof(cases)/sizeof(cases[0]); ++i) {
            clean(); trigger_bank=cases[i][0]; trigger_read=cases[i][1]; mutation=cases[i][2];
            CHECK(rejects(hz)==0);
        }
        clean(); stop_raw=true; CHECK(rejects(hz)==0);
    } else if (!strcmp(argv[1],"unavailable")) {
        s_vdc_timestamp_clock_initialized=s_vdc_timestamp_clock_ready=false;
        CHECK(rejects(hz)==0);
        CHECK(!s_vdc_timestamp_clock_initialized && !s_vdc_timestamp_clock_ready);
        CHECK(timer_reads[0]<=1 && timer_reads[1]==0 && clock_reads==0);
        clean(); CHECK(!vdc_timestamp_clock_try_read_bridge(hz,NULL));
        CHECK(timer_reads[0]==0 && timer_reads[1]==0 && clock_reads==0);
        clean(); CHECK(rejects(0)==0);
        clean(); CHECK(rejects(hz+1)==0);
    } else if (!strcmp(argv[1],"limits")) {
        uint64_t us=(UINT64_MAX-999)/1000;
        banks[0].timerawh=(uint32_t)(us>>32); banks[0].timerawl=(uint32_t)us;
        vdc_timestamp_clock_bridge_t out;
        CHECK(vdc_timestamp_clock_try_read_bridge(hz,&out));
        CHECK(out.local_ns==us*1000 && out.local_ns<=UINT64_MAX-999);
        clean(); ++us; banks[0].timerawh=(uint32_t)(us>>32); banks[0].timerawl=(uint32_t)us;
        CHECK(rejects(hz)==0);
    } else if (!strcmp(argv[1],"diagnostic-success")) {
        for (unsigned mode=0; mode<3; ++mode) {
            clean();
            if (mode>=1) { hz=250000000; pll.prim=0x32000; s_vdc_timestamp_clock_tick_hz=hz; }
            /* SDK reset includes reserved DBGPAUSE bit 0: equal 7 also admits. */
            if (mode==2) banks[0].dbgpause=banks[1].dbgpause=7;
            vdc_timestamp_clock_bridge_diagnostic_t d;
            memset(&d,0xA5,sizeof(d));
            CHECK(vdc_timestamp_clock_read_bridge_diagnostic(hz,&d));
            CHECK(d.schema==1 && d.platform_supported==1 && d.configuration_supported==1 && d.bridge_valid==1);
            CHECK(d.expected_hz==hz && d.cached_hz==hz && d.sdk_sys_hz==hz && d.clock_ready==1);
            CHECK(d.pll_prim==pll.prim && d.pll_fbdiv==125 && d.pll_pwr==4 && d.pll_cs==0x80000001u);
            CHECK(d.proc_config==0 && d.xosc_ctrl==0xFABAA0 && d.xosc_status==0x80001000u && d.xosc_dormant==0x77616b65);
            CHECK(d.timer0_dbgpause==banks[0].dbgpause && d.timer1_dbgpause==banks[1].dbgpause);
            CHECK(d.timer0_sample_valid==1 && d.timer1_sample_valid==1 && d.timer0_sample_us==1234567);
            CHECK(d.timer1_sample_ticks>1000 && d.bridge.raw_before>d.timer1_sample_ticks);
            CHECK(d.bridge.local_ns==1234567000 && d.bridge.raw_after>d.bridge.raw_before && d.bridge.tick_hz==hz);
            CHECK(timer_reads[0]<40 && timer_reads[1]<50 && clock_reads<12);
        }
    } else if (!strcmp(argv[1],"diagnostic-rejection")) {
        for (unsigned code=1; code<=40; ++code) {
            if (code==33 || code==34 || code==39) continue;
            clean(); mutate(code);
            vdc_timestamp_clock_bridge_diagnostic_t d;
            CHECK(vdc_timestamp_clock_read_bridge_diagnostic(125000000,&d));
            CHECK(d.platform_supported==1 && d.configuration_supported==0 && d.bridge_valid==0);
            CHECK(d.bridge.raw_before==0 && d.bridge.local_ns==0 && d.bridge.raw_after==0 && d.bridge.tick_hz==0);
            CHECK(d.timer0_source==banks[0].source && d.timer1_source==banks[1].source);
            CHECK(d.timer0_pause==banks[0].pause && d.timer1_pause==banks[1].pause);
            CHECK(d.proc_config==syscfg.proc_config && d.sdk_sys_hz==hz);
            CHECK(d.xosc_ctrl==xosc.ctrl && d.xosc_status==xosc.status && d.xosc_dormant==xosc.dormant);
            CHECK(d.pll_cs==pll.cs && d.pll_pwr==pll.pwr && d.pll_fbdiv==pll.fbdiv_int && d.pll_prim==pll.prim);
            CHECK(d.ref_ctrl==clocks.clk[4].ctrl && d.ref_div==clocks.clk[4].div && d.ref_selected==clocks.clk[4].selected);
            CHECK(d.sys_ctrl==clocks.clk[5].ctrl && d.sys_div==clocks.clk[5].div && d.sys_selected==clocks.clk[5].selected);
            CHECK(d.tick_ctrl==ticks.ticks[2].ctrl && d.tick_cycles==ticks.ticks[2].cycles);
            CHECK(d.resus_ctrl==clocks.resus.ctrl && d.resus_status==clocks.resus.status);
            CHECK(d.timer0_sample_us==1234567 && d.timer1_sample_ticks>1000);
        }
        clean(); stop_raw=true;
        vdc_timestamp_clock_bridge_diagnostic_t d;
        CHECK(vdc_timestamp_clock_read_bridge_diagnostic(hz,&d));
        CHECK(d.configuration_supported==1 && d.bridge_valid==0);
        CHECK(d.timer1_sample_valid==1 && d.timer1_sample_ticks==1000);
    } else if (!strcmp(argv[1],"diagnostic-rollover")) {
        for (unsigned bank=0; bank<2; ++bank) {
            clean(); trigger_bank=bank; trigger_read=6; mutation=100;
            vdc_timestamp_clock_bridge_diagnostic_t d;
            CHECK(vdc_timestamp_clock_read_bridge_diagnostic(hz,&d));
            CHECK((bank==0 ? d.timer0_sample_valid : d.timer1_sample_valid)==0);
            CHECK(d.configuration_supported==1 && d.bridge_valid==1);
        }
    } else if (!strcmp(argv[1],"diagnostic-uninitialized")) {
        clean();
        CHECK(!vdc_timestamp_clock_read_bridge_diagnostic(hz,NULL));
        CHECK(timer_reads[0]==0 && timer_reads[1]==0 && clock_reads==0);
        s_vdc_timestamp_clock_initialized=s_vdc_timestamp_clock_ready=false;
        vdc_timestamp_clock_bridge_diagnostic_t d;
        CHECK(vdc_timestamp_clock_read_bridge_diagnostic(hz,&d));
        CHECK(d.platform_supported==1 && d.clock_ready==0 && d.configuration_supported==0 && d.bridge_valid==0);
        CHECK(d.xosc_dormant==0x77616b65 && d.timer0_sample_us==1234567);
        CHECK(!s_vdc_timestamp_clock_initialized && !s_vdc_timestamp_clock_ready);
    } else if (!strcmp(argv[1],"sticky-badwrite")) {
        /* Four-board observed state: 250 MHz, stable/enabled XOSC and an
         * existing WC BADWRITE history bit. Observe it without clearing it. */
        hz=250000000; pll.prim=0x61000; s_vdc_timestamp_clock_tick_hz=hz;
        xosc.status=0x81001000u;
        vdc_timestamp_clock_bridge_t bridge;
        CHECK(vdc_timestamp_clock_try_read_bridge(hz,&bridge));
        CHECK(bridge.tick_hz==250000000 && xosc.status==0x81001000u);
        vdc_timestamp_clock_bridge_diagnostic_t d;
        CHECK(vdc_timestamp_clock_read_bridge_diagnostic(hz,&d));
        CHECK(d.configuration_supported==1 && d.bridge_valid==1);
        CHECK(d.xosc_status==0x81001000u && xosc.status==0x81001000u);
        /* Historical BADWRITE grants no exception to the live state bits. */
        const uint32_t invalid_statuses[]={0x01001000u,0x81000000u,0x81001001u};
        for (unsigned i=0; i<3; ++i) {
            clean(); hz=250000000; pll.prim=0x61000; s_vdc_timestamp_clock_tick_hz=hz;
            xosc.status=invalid_statuses[i]; CHECK(rejects(hz)==0);
        }
        /* Either transition during the bridge invalidates the full config
         * comparison, even though either steady endpoint can be admitted. */
        for (unsigned code=39; code<=42; code+=3) {
            clean(); hz=250000000; pll.prim=0x61000; s_vdc_timestamp_clock_tick_hz=hz;
            if (code==42) xosc.status=0x81001000u;
            trigger_bank=0; trigger_read=6; mutation=code;
            CHECK(rejects(hz)==0);
            CHECK(xosc.status==(code==39 ? 0x81001000u : 0x80001000u));
        }
    } else return 2;
    puts("bridge passed"); return 0;
}
'''
