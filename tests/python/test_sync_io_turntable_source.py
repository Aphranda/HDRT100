"""Production PWM source and real admission gates, with register-only hardware mocks."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

from test_sync_io_workspace import function

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "components/sync_io/src"


@pytest.fixture(scope="module")
def source(tmp_path_factory):
    d = tmp_path_factory.mktemp("turntable-source")
    (d / "source_mock.h").write_text(MOCK, encoding="utf-8")
    for name in ("board_config.h", "hardware/clocks.h", "hardware/gpio.h", "hardware/pwm.h",
                 "hardware/structs/io_bank0.h", "hardware/structs/pads_bank0.h",
                 "hardware/regs/pads_bank0.h", "hardware/regs/io_bank0.h", "pico/platform.h", "tdma_runtime_owner.h"):
        p = d / name
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text('#include "source_mock.h"\n', encoding="utf-8")
    model = (SRC / "sync_io_model_sched.c").read_text(encoding="utf-8")
    enum = re.search(r"typedef enum \{\n    SYNC_IO_SCHEDULE_IDLE.*?} sync_io_schedule_phase_t;", model, re.S)[0]
    gate = enum + "\n"
    for name in ("sync_io_schedule_reserve", "sync_io_schedule_publish_phase",
                 "sync_io_core_turntable_source_reserve", "sync_io_core_turntable_source_release",
                 "sync_io_core_legacy_try_enter", "sync_io_core_legacy_leave",
                 "sync_io_core_run_output_reserve", "sync_io_core_run_output_held", "sync_io_core_run_output_release",
                 "sync_io_core_reference_reserve", "sync_io_core_reference_release",
                 "sync_io_core_pulse_counter_reserve", "sync_io_core_pulse_counter_release",
                 "sync_io_core_gateway_reserve", "sync_io_core_gateway_release",
                 "sync_io_core_switch_reserve", "sync_io_core_switch_release"):
        gate += function(model, name)
    (d / "source.c").write_text('#include "source_mock.h"\n' + gate +
        '#include "' + (SRC / "sync_io_turntable_source.c").as_posix() + '"\n' + HARNESS,
        encoding="utf-8")
    exe = d / "source.exe"
    r = subprocess.run([shutil.which("gcc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-I" + str(d), "-I" + str(ROOT / "components/sync_io/inc"),
        str(d / "source.c"), "-o", str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return exe


@pytest.mark.parametrize("case", ["lifecycle", "counter_first", "source_first", "run_first",
    "source_run", "reference", "switch_first", "gateway_first", "source_excludes",
    "legacy_first", "capture", "pwm", "slice_busy", "slice_irq", "partner_mux", "alias_mux",
    "bad_frequency", "not_stopped", "wrong_core", "clock_changed", "release_running",
    "pending_irq", "frequency_rounding", "failed_prepare_releases", "wrong_token", "stale_pwm_high"])
def test_source(source, case):
    r = subprocess.run([str(source), case], capture_output=True, text=True, timeout=10)
    assert r.returncode == 0, r.stdout + r.stderr


MOCK = r'''
#ifndef SOURCE_MOCK_H
#define SOURCE_MOCK_H
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#define SYNC_IO_CORE_INTERNAL_H
#define BOARD_SYNC_OUTPUT_BASE_PIN 16u
#define NUM_BANK0_GPIOS 48u
#define PADS_BANK0_GPIO0_OD_BITS 128u
#define PADS_BANK0_GPIO0_IE_BITS 64u
#define PADS_BANK0_GPIO0_ISO_BITS 256u
#define IO_BANK0_GPIO0_CTRL_FUNCSEL_LSB 0u
#define IO_BANK0_GPIO0_CTRL_OUTOVER_LSB 8u
#define GPIO_OVERRIDE_LOW 2u
#define GPIO_OVERRIDE_NORMAL 0u
#define PWM_CH0_CSR_EN_BITS 1u
#define GPIO_FUNC_PWM 4u
#define clk_sys 0u
#define __not_in_flash_func(n) n
typedef unsigned uint;
static unsigned core;
static bool stopped=true,initialized=true,capture,sequence,encoder,pwm;
static uint32_t sys_hz=250000000;
static struct {bool running;} s_model_pulse;
static bool s_wave_output_manager_active,s_wave_output_sm_claimed,s_wave_output_dma_claimed,s_wave_output_program_loaded;
static uint32_t s_schedule_phase;
static uintptr_t s_turntable_source_token,s_run_output_token,s_reference_token,s_counter_token,s_gateway_token,s_switch_token;
#define get_core_num() core
#define sync_io_core_initialized() initialized
#define sync_io_core_capture_is_running() capture
#define sync_io_seq_step_is_running() sequence
#define sync_io_enc_count_is_running() encoder
#define sync_io_core_sma_frequency_output_active() pwm
static struct {struct {uint32_t ctrl;} io[48];} io_regs;
static struct {uint32_t io[48];} pad_regs;
static struct {struct {uint32_t csr,div,ctr,cc,top;} slice[12];uint32_t inte,inte1,intf,intf1,intr;} pwm_regs;
#define io_bank0_hw (&io_regs)
#define pads_bank0_hw (&pad_regs)
#define pwm_hw (&pwm_regs)
typedef struct {uint32_t top,div;} pwm_config;
static uint gpio_writes,config_writes,enable_writes;
static uint32_t clock_get_hz(uint c){assert(c==0);return sys_hz;}
static uint pwm_gpio_to_slice_num(uint pin){return pin<32 ? (pin>>1u)&7u : 8u+((pin>>1u)&3u);}
static uint pwm_gpio_to_channel(uint pin){return pin%2;}
static uint gpio_get_function(uint pin){return io_regs.io[pin].ctrl&31;}
/* Disabled PWM retains old HIGH regardless of CTR/CC writes. SIO starts
 * LOW. Observe every API write so an intermediate mux glitch is visible. */
static bool pwm_level=true, pad_level;
static uint pad_rising;
static void observe_pad(void){
    uint32_t override=(io_regs.io[19].ctrl>>8)&3;
    bool value=override==GPIO_OVERRIDE_LOW ? false :
        gpio_get_function(19)==GPIO_FUNC_PWM ? pwm_level : false;
    if(pad_regs.io[19]&(PADS_BANK0_GPIO0_OD_BITS|PADS_BANK0_GPIO0_ISO_BITS))value=false;
    if(value&&!pad_level)++pad_rising;
    pad_level=value;
}
static void gpio_set_outover(uint pin,uint value){assert(pin==19);io_regs.io[pin].ctrl=(io_regs.io[pin].ctrl&~(3u<<8))|(value<<8);observe_pad();}
static void hw_write_masked(volatile uint32_t *ptr,uint32_t value,uint32_t mask){
    assert(ptr==&io_regs.io[19].ctrl||ptr==&pad_regs.io[19]);
    if(ptr==&io_regs.io[19].ctrl)++gpio_writes;
    *ptr=(*ptr&~mask)|(value&mask);observe_pad();
}
static pwm_config pwm_get_default_config(void){return (pwm_config){0};}
static void pwm_config_set_wrap(pwm_config *c,uint16_t top){c->top=top;}
static void pwm_config_set_clkdiv_int_frac4(pwm_config *c,uint div,uint8_t frac){c->div=div*16+frac;}
static void pwm_init(uint slice,const pwm_config *c,bool enabled){assert(slice==1&&!enabled);++config_writes;pwm_regs.slice[slice].csr=0;pwm_regs.slice[slice].div=c->div;pwm_regs.slice[slice].ctr=0;pwm_regs.slice[slice].cc=0;pwm_regs.slice[slice].top=c->top;}
static void pwm_set_chan_level(uint slice,uint chan,uint16_t v){assert(slice==1&&chan==1);pwm_regs.slice[slice].cc=(uint32_t)v<<16;}
static void pwm_set_counter(uint slice,uint16_t v){assert(slice==1);pwm_regs.slice[slice].ctr=v;}
static void pwm_set_enabled(uint slice,bool enabled){assert(slice==1);++enable_writes;pwm_regs.slice[slice].csr=(pwm_regs.slice[slice].csr&~1u)|(enabled?1u:0u);}
static void pwm_clear_irq(uint slice){pwm_regs.intr&=~(1u<<slice);}
typedef void tdma_service_service_t;
static tdma_service_service_t *tdma_runtime_owner_get(void){return (void *)1;}
static bool tdma_service_run_stopped_maintenance(tdma_service_service_t *s,bool (*f)(void *),void *p){assert(s);return stopped&&f(p);}
#endif
'''

HARNESS = r'''
int main(int argc,char **argv){assert(argc==2);const char *c=argv[1];static int owner,wrong;
    for(uint i=0;i<48;i++){io_regs.io[i].ctrl=0xa5;pad_regs.io[i]=0x1234+i;}
    for(uint i=0;i<12;i++){pwm_regs.slice[i].csr=0x10;pwm_regs.slice[i].div=17+i;pwm_regs.slice[i].ctr=2+i;pwm_regs.slice[i].cc=0x10100+i;pwm_regs.slice[i].top=789+i;}
    if(!strcmp(c,"pending_irq"))pwm_regs.intr=1u<<1;
    const uint32_t original_pending=pwm_regs.intr;
    unsigned char oldio[sizeof(io_regs)],oldpad[sizeof(pad_regs)],oldpwm[sizeof(pwm_regs)];
    memcpy(oldio,&io_regs,sizeof(io_regs));memcpy(oldpad,&pad_regs,sizeof(pad_regs));memcpy(oldpwm,&pwm_regs,sizeof(pwm_regs));
    if(!strcmp(c,"counter_first"))assert(sync_io_core_pulse_counter_reserve(&owner));
    if(!strcmp(c,"run_first"))assert(sync_io_core_run_output_reserve(&owner));
    if(!strcmp(c,"reference"))assert(sync_io_core_reference_reserve(&owner));
    if(!strcmp(c,"switch_first"))assert(sync_io_core_switch_reserve(&owner));
    if(!strcmp(c,"gateway_first"))assert(sync_io_core_gateway_reserve(&owner));
    if(!strcmp(c,"legacy_first"))assert(sync_io_core_legacy_try_enter());
    if(!strcmp(c,"capture"))capture=true;
    if(!strcmp(c,"pwm"))pwm=true;
    if(!strcmp(c,"slice_busy"))pwm_regs.slice[1].csr|=1;
    if(!strcmp(c,"slice_irq"))pwm_regs.inte1=1u<<1;
    if(!strcmp(c,"partner_mux"))io_regs.io[18].ctrl=GPIO_FUNC_PWM;
    if(!strcmp(c,"alias_mux"))io_regs.io[3].ctrl=GPIO_FUNC_PWM;
    if(!strcmp(c,"not_stopped"))stopped=false;
    if(!strcmp(c,"wrong_core"))core=1;
    if(!strcmp(c,"bad_frequency")){assert(!sync_io_turntable_source_prepare(0));assert(!sync_io_turntable_source_prepare(99));assert(!sync_io_turntable_source_prepare(100001));return 0;}
    if(!strcmp(c,"failed_prepare_releases")){pwm_regs.slice[1].csr|=1;assert(!sync_io_turntable_source_prepare(1000));assert(!s_turntable_source_token);pwm_regs.slice[1].csr&=~1u;}
    bool expected=!strcmp(c,"switch_first")||!strcmp(c,"gateway_first")||!strcmp(c,"legacy_first")||!strcmp(c,"capture")||!strcmp(c,"pwm")||!strcmp(c,"slice_busy")||!strcmp(c,"slice_irq")||!strcmp(c,"partner_mux")||!strcmp(c,"alias_mux")||!strcmp(c,"not_stopped")||!strcmp(c,"wrong_core");
    bool ok=sync_io_turntable_source_prepare(1000);
    if(expected){assert(!ok&&!gpio_writes&&!config_writes&&!enable_writes&&!s_turntable_source_token);return 0;}
    assert(ok&&s_source.prepared&&!s_source.running&&!pad_level&&!pad_rising);
    assert(pwm_regs.slice[1].ctr==pwm_regs.slice[1].top&&pwm_regs.slice[1].ctr>=(pwm_regs.slice[1].cc>>16));
    assert(s_source.actual_hz==1000&&s_source.output_pin==19&&s_source.output_port==4&&s_source.pwm_slice==1);
    assert((uint64_t)(s_source.pwm_top+1)*s_source.pwm_div16*1000==(uint64_t)sys_hz*16);
    assert(!sync_io_turntable_source_prepare(2000));
    if(!strcmp(c,"wrong_token")){assert(!sync_io_core_turntable_source_release(&wrong));assert(s_turntable_source_token);}
    if(!strcmp(c,"source_first"))assert(sync_io_core_pulse_counter_reserve(&owner));
    if(!strcmp(c,"source_run"))assert(sync_io_core_run_output_reserve(&owner));
    if(!strcmp(c,"source_excludes")){assert(!sync_io_core_gateway_reserve(&owner));assert(!sync_io_core_switch_reserve(&owner));assert(!sync_io_core_legacy_try_enter());assert(!sync_io_schedule_reserve(SYNC_IO_SCHEDULE_IDLE,SYNC_IO_SCHEDULE_FIXED_PREPARING));}
    stopped=false;
    if(!strcmp(c,"clock_changed")){++sys_hz;assert(!sync_io_turntable_source_start());--sys_hz;}
    assert(sync_io_turntable_source_start()&&s_source.running&&pad_level&&pad_rising==1);
    assert(!sync_io_turntable_source_release()&&s_source.prepared&&s_source.running);
    assert(!sync_io_turntable_source_prepare(2000));
    assert(gpio_writes==1&&config_writes==1);
    assert(sync_io_turntable_source_stop()&&!s_source.running&&!pad_level);
    assert(sync_io_turntable_source_start());
    assert(gpio_writes==1&&config_writes==1);
    pwm_regs.intr|=1u<<1;
    stopped=true;assert(sync_io_turntable_source_release());
    assert(!s_source.prepared&&!s_turntable_source_token&&pwm_regs.intr==original_pending);
    assert(!memcmp(oldio,&io_regs,sizeof(io_regs)));assert(!memcmp(oldpad,&pad_regs,sizeof(pad_regs)));assert(!memcmp(oldpwm,&pwm_regs,sizeof(pwm_regs)));
    assert(!sync_io_turntable_source_start()&&!sync_io_turntable_source_stop());
    if(!strcmp(c,"frequency_rounding"))for(uint hz=100;hz<=100000;hz+=791){
        assert(sync_io_turntable_source_prepare(hz));
        uint64_t den=(uint64_t)(s_source.pwm_top+1)*s_source.pwm_div16;
        uint64_t n=(uint64_t)sys_hz*16;
        assert(s_source.actual_hz==(n+den/2)/den);
        assert(sync_io_turntable_source_release());
    }
    return 0;
}
'''
