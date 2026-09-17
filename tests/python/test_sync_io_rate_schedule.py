"""Independent rational-edge and ownership checks for fixed-rate PIO output.

The timing oracle uses Fraction and the actual PIO instruction stream, not
the production duration decoder or the generator's remainder algorithm.
"""
import ctypes
from fractions import Fraction
from pathlib import Path
import random
import re
import shutil
import subprocess

import pytest

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "components/sync_io/src"


class Request(ctypes.Structure):
    _fields_ = [("request_id", ctypes.c_uint32), ("rate_ppb", ctypes.c_int32),
                ("period_ns", ctypes.c_uint32), ("high_ns", ctypes.c_uint32),
                ("pulse_count", ctypes.c_uint32), ("tick_period_ns", ctypes.c_uint32)]


class Encoding(ctypes.Structure):
    _fields_ = [("first_edge_ticks", ctypes.c_uint64),
                ("last_edge_ticks", ctypes.c_uint64), ("total_ticks", ctypes.c_uint64),
                ("min_period_ticks", ctypes.c_uint32),
                ("max_period_ticks", ctypes.c_uint32), ("high_ticks", ctypes.c_uint32)]


@pytest.fixture(scope="module")
def generator(tmp_path_factory):
    work = tmp_path_factory.mktemp("rate-generator")
    source = work / "rate.c"
    source.write_text('''#include "sync_io_rate_schedule.h"
bool generate(const sync_io_rate_schedule_request_t *r, uint32_t *w,
              size_t n, sync_io_rate_schedule_encoding_t *e) {
    return sync_io_rate_schedule_generate(r, w, n, e);
}
''', encoding="utf-8")
    cc = shutil.which("gcc")
    assert cc, "host gcc is required for independent rate-schedule validation"
    dll = work / "rate.dll"
    built = subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-shared", "-O2", "-I" + str(ROOT / "components/sync_io/inc"),
                            str(source), "-o", str(dll)], capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    library = ctypes.CDLL(str(dll))
    library.generate.argtypes = [ctypes.POINTER(Request), ctypes.POINTER(ctypes.c_uint32),
                                 ctypes.c_size_t, ctypes.POINTER(Encoding)]
    library.generate.restype = ctypes.c_bool
    return library.generate


def encode(generator, request):
    words = (ctypes.c_uint32 * (request.pulse_count * 2 + 2))()
    words[-2] = 0x12345678
    words[-1] = 0xABCDEF01
    result = Encoding()
    assert generator(ctypes.byref(request), words, len(words) - 2, ctypes.byref(result))
    assert list(words[-2:]) == [0x12345678, 0xABCDEF01]
    return list(words[:-2]), result


def ceil_fraction(value):
    return -(-value.numerator // value.denominator)


def check_edges(generator, request):
    words, encoded = encode(generator, request)
    # pull + out + (word + 1) iterations of jmp x-- + set = word + 4.
    elapsed = 0
    edges = []
    for low, high in zip(words[::2], words[1::2]):
        assert low > 0
        elapsed += low + 4
        edges.append(elapsed)
        elapsed += high + 4
    ideal_step = Fraction(request.period_ns * 10**9,
                          (10**9 + request.rate_ppb) * request.tick_period_ns)
    high_ticks = ceil_fraction(Fraction(request.high_ns, request.tick_period_ns))
    periods = [edges[0]] + [b - a for a, b in zip(edges, edges[1:])]
    for index, edge in enumerate(edges, 1):
        ideal = index * ideal_step
        assert edge == ceil_fraction(ideal)
        assert 0 <= edge - ideal < 1, (request.rate_ppb, index, edge, ideal)
    assert all(word + 4 == high_ticks for word in words[1::2])
    assert encoded.first_edge_ticks == edges[0]
    assert encoded.last_edge_ticks == edges[-1]
    assert encoded.total_ticks == elapsed == edges[-1] + high_ticks
    assert encoded.min_period_ticks == min(periods)
    assert encoded.max_period_ticks == max(periods)
    assert encoded.high_ticks == high_ticks
    return edges


def test_pio_instruction_cost_matches_independent_oracle():
    source = (SRC / "sync_io.pio").read_text(encoding="utf-8")
    body = source.split(".program sync_model_sched_pulse_high\n", 1)[1].split(".program", 1)[0]
    instructions = [line.strip() for line in body.splitlines()
                    if line.strip() and not line.startswith(".") and not line.endswith(":")]
    assert instructions == ["pull block", "out x, 32", "jmp x-- model_high_delay_low",
                            "set pins, 1", "pull block", "out x, 32",
                            "jmp x-- model_high_delay_high", "set pins, 0"]


@pytest.mark.parametrize("tick", [4, 100])
@pytest.mark.parametrize("rate", [-999999999, -100000000, -10000, -1000, -1,
                                  0, 1, 1000, 10000, 100000000, 2147483647])
def test_rational_edges_across_full_schedule(generator, tick, rate):
    period = 36 if rate == -999999999 else 1500001
    high = 16 if rate == -999999999 else 1003
    if rate == -999999999 and tick == 4:
        # 9e9 ticks exceeds the admitted uint32 period; use a valid near-boundary input.
        period = 17
    if rate == -999999999 and tick == 100:
        high = 400
    check_edges(generator, Request(7, rate, period, high, 2048, tick))


@pytest.mark.parametrize("tick", [4, 100])
def test_one_nanosecond_correction_survives_quantization(generator, tick):
    base = check_edges(generator, Request(1, 0, 1000000, 1000, 2048, tick))
    faster = check_edges(generator, Request(2, 1000, 1000000, 1000, 2048, tick))
    slower = check_edges(generator, Request(3, -1000, 1000000, 1000, 2048, tick))
    assert faster[-1] < base[-1] < slower[-1]
    assert base[-1] - faster[-1] >= 2047 // tick
    assert slower[-1] - base[-1] >= 2048 // tick
    assert len(set(b - a for a, b in zip(faster, faster[1:]))) == 2


def test_wide_inputs_do_not_multiply_cumulative_numerator_in_uint64(generator):
    request = Request(0xFFFFFFFF, 1, 0xFFFFFFFF, 400, 2048, 4)
    assert request.period_ns * 10**9 * request.pulse_count > 2**64
    check_edges(generator, request)


def test_reproducible_fractional_period_matrix(generator):
    rng = random.Random(0xDCC001)
    for _ in range(128):
        check_edges(generator, Request(rng.randrange(2**32), rng.randrange(-900000000, 2147483648),
                                        rng.randrange(100000, 2**32), rng.randrange(400, 10000),
                                        rng.choice([1, 2, 3, 17, 127, 2048]), rng.choice([4, 100])))


@pytest.mark.parametrize("field,value", [
    ("rate_ppb", -1000000000), ("rate_ppb", -2147483648),
    ("period_ns", 0), ("high_ns", 0), ("pulse_count", 0),
    ("pulse_count", 2049), ("pulse_count", 0xFFFFFFFF),
    ("tick_period_ns", 0), ("tick_period_ns", 1), ("tick_period_ns", 5),
    ("tick_period_ns", 0xFFFFFFFF), ("high_ns", 1), ("high_ns", 0xFFFFFFFF),
])
def test_rejects_invalid_request_without_mutating_outputs(generator, field, value):
    request = Request(1, 0, 1000000, 1000, 2, 4)
    setattr(request, field, value)
    assert_rejected_unchanged(generator, request)


def assert_rejected_unchanged(generator, request, capacity=4096, null_arg=None):
    words = (ctypes.c_uint32 * 4096)(*([0xDEADC0DE] * 4096))
    encoded = Encoding()
    ctypes.memset(ctypes.byref(encoded), 0xA5, ctypes.sizeof(encoded))
    before = bytes(encoded)
    assert not generator(None if null_arg == "request" else ctypes.byref(request),
                         None if null_arg == "words" else words, capacity,
                         None if null_arg == "encoding" else ctypes.byref(encoded))
    assert bytes(encoded) == before
    assert all(word == 0xDEADC0DE for word in words)


@pytest.mark.parametrize("null_arg", ["request", "words", "encoding"])
def test_null_rejection(generator, null_arg):
    assert_rejected_unchanged(generator, Request(1, 0, 1000000, 1000, 2, 4), null_arg=null_arg)


def test_capacity_and_section_boundary(generator):
    assert_rejected_unchanged(generator, Request(1, 0, 1000000, 1000, 2048, 4), capacity=4095)
    assert_rejected_unchanged(generator, Request(1, 0, 32, 16, 2, 4))
    words, _ = encode(generator, Request(1, 0, 36, 16, 2, 4))
    assert words == [5, 0, 1, 0]
    assert_rejected_unchanged(generator, Request(1, -999999999, 18, 16, 2, 4))


def production_function(source, name):
    match = re.search(rf"(?m)^(?:static )?[\w *]+\b{name}\([^;]*?\n\{{", source)
    assert match, name
    end = source.index("\n}\n", match.start()) + 3
    return source[match.start():end]


def test_real_owner_stop_and_final_falling_edge(tmp_path):
    source = (SRC / "sync_io_model_sched.c").read_text(encoding="utf-8")
    declarations = source[source.index("typedef struct {"):source.index("static float sync_io_model_clkdiv")]
    harness = r'''
#include <assert.h>
#include <string.h>
#include "sync_io.h"
#include "board_config.h"
#include "sync_io_persona_manager.h"
#include "sync_io_persona_resources.h"
#include "resource_arbiter.h"
typedef unsigned uint;
struct host_pio_hw { uint32_t txf[4]; struct { uint32_t clkdiv; } sm[4]; };
static struct host_pio_hw host_pio;
#undef BOARD_SYNC_PIO_FAST
#define BOARD_SYNC_PIO_FAST (&host_pio)
typedef struct { unsigned unused; } pio_program_t;
typedef struct { unsigned unused; } dma_channel_config;
static pio_program_t sync_model_sched_pulse_high_program, sync_model_sched_pulse_low_program;
static struct { struct { uint32_t transfer_count; } ch[16]; } fake_dma;
#define dma_hw (&fake_dma)
static uint32_t sync_io_shared_workspace[SYNC_IO_SHARED_WORKSPACE_WORDS];
static bool initialized=true, capture, sequence, encoder, pwm;
static bool sm_claimed, dma_claimed, program_loaded, can_load=true;
static bool busy, fifo_empty=true, enabled, pins_high;
static uint32_t pc, sys_hz=250000000, dma_words, starts, aborts, hw_reads;
static uint64_t now_us;
static bool validator_ok=true, alter_clock, alter_divider;
static unsigned validations;
static unsigned critical_depth;
static void (*claim_interleave)(void);
static void (*validator_interleave)(void);
static bool instrumented_claim(const void *owner) {
    assert(critical_depth==0);
    const bool claimed=sync_io_workspace_claim(owner);
    if (claimed && claim_interleave!=NULL) {
        void (*hook)(void)=claim_interleave;
        claim_interleave=NULL;
        hook();
    }
    return claimed;
}
#define sync_io_workspace_claim(owner) instrumented_claim(owner)
#define clk_sys 0
#define clock_get_hz(...) sys_hz
#define time_us_64() now_us
#define sync_io_core_initialized() initialized
#define sync_io_core_capture_is_running() capture
#define sync_io_seq_step_is_running() sequence
#define sync_io_enc_count_is_running() encoder
#define DMA_SIZE_32 0
#define GPIO_FUNC_SIO 0
#define GPIO_IN 0
#define SYNC_IO_MODEL_PULSE_US_TICK_HZ 1000000u
#define SYNC_IO_MODEL_PULSE_DEFAULT_TICK_PERIOD_NS 100u
#define SYNC_IO_MODEL_PULSE_SECTION_OVERHEAD_TICKS 3u
#define SYNC_IO_TRACE_MODEL_ARM 0
#define SYNC_IO_TRACE_MODEL_DISARM 0
#define SYNC_IO_TRACE_INFO 0
#define SYNC_IO_TRACE_MODEL_FAIL 0
#define SYNC_IO_TRACE_ERROR 0
#define LOG_INFO(...) ((void)0)
#define sync_io_core_trace(a,b,c,d) ((void)(a),(void)(b),(void)(c),(void)(d))
#define pio_sm_is_claimed(p,s) ((void)(p),(void)(s),sm_claimed)
#define dma_channel_is_claimed(c) ((void)(c),dma_claimed)
#define pio_can_add_program(p,g) ((void)(p),(void)(g),can_load)
#define pio_sm_claim(p,s) ((void)(p),(void)(s),sm_claimed=true)
#define dma_channel_claim(c) ((void)(c),dma_claimed=true)
#define pio_add_program(p,g) ((void)(p),(void)(g),program_loaded=true,8u)
#define pio_remove_program(p,g,o) ((void)(p),(void)(g),(void)(o),program_loaded=false)
#define pio_sm_unclaim(p,s) ((void)(p),(void)(s),sm_claimed=false)
#define dma_channel_unclaim(c) ((void)(c),dma_claimed=false)
#define pio_sm_set_enabled(p,s,e) ((void)(p),(void)(s),enabled=(e))
#define pio_sm_clear_fifos(p,s) ((void)(p),(void)(s),fifo_empty=true)
#define pio_sm_restart(p,s) ((void)(p),(void)(s))
#define pio_sm_set_pins(p,s,v) ((void)(p),(void)(s),pins_high=((v)!=0))
#define dma_channel_abort(c) ((void)(c),assert(critical_depth==0),++aborts,busy=false)
#define dma_channel_set_irq0_enabled(c,e) ((void)(c),(void)(e))
#define dma_channel_get_default_config(c) ((void)(c),(dma_channel_config){0})
#define channel_config_set_transfer_data_size(c,v) ((void)(c),(void)(v))
#define channel_config_set_read_increment(c,v) ((void)(c),(void)(v))
#define channel_config_set_write_increment(c,v) ((void)(c),(void)(v))
#define channel_config_set_dreq(c,v) ((void)(c),(void)(v))
#define pio_get_dreq(p,s,t) ((void)(p),(void)(s),(void)(t),0u)
#define dma_start_channel_mask(m) ((void)(m),assert(critical_depth>0),++starts,busy=true,fifo_empty=false)
#define dma_channel_is_busy(c) ((void)(c),++hw_reads,busy)
#define pio_sm_is_tx_fifo_empty(p,s) ((void)(p),(void)(s),++hw_reads,fifo_empty)
#define pio_sm_is_tx_fifo_full(p,s) ((void)(p),(void)(s),++hw_reads,false)
#define pio_sm_get_pc(p,s) ((void)(p),(void)(s),++hw_reads,pc)
#define sync_io_core_sm_is_enabled(p,s) ((void)(p),(void)(s),++hw_reads,enabled)
#define gpio_set_function(p,f) ((void)(p),(void)(f))
#define gpio_put(p,v) ((void)(p),pins_high=(v))
#define gpio_set_dir(p,v) ((void)(p),(void)(v))
#define gpio_pull_down(p) ((void)(p))
void osal_critical_enter(void) { ++critical_depth; }
void osal_critical_exit(void) { assert(critical_depth>0); --critical_depth; }
void sync_io_sma_frequency_tx_get_status(sync_io_sma_frequency_tx_status_t *s) {
    memset(s,0,sizeof(*s)); s->running=pwm;
}
static void sync_model_sched_pulse_program_init(PIO p, uint sm, uint offset,
                                               uint pin, bool high, float divider) {
    (void)pin; (void)high; pc=offset;
    p->sm[sm].clkdiv=(uint32_t)(divider*65536.0f);
}
static void dma_channel_configure(uint channel, const dma_channel_config *config,
                                  volatile void *dst, const void *src, uint count, bool start) {
    (void)config; (void)dst; (void)src; assert(!start);
    dma_words=count; dma_hw->ch[channel].transfer_count=count;
}
'''
    harness += declarations
    harness += "\nstatic void sync_io_model_pulse_schedule_disarm_owned(void);\n"
    functions = [
        "sync_io_model_clkdiv_for_tick_rate", "sync_io_model_tick_hz_from_period_ns",
        "sync_io_model_ns_to_ticks", "sync_io_model_delay_ticks_for_duration",
        "sync_io_model_high_ticks_for_duration", "sync_io_model_high_word",
        "sync_io_model_delay_word", "sync_io_model_delay_word_to_ticks",
        "sync_io_model_high_word_to_ticks", "sync_io_model_word_ticks_to_ns",
        "sync_io_model_pulse_duration_ns", "sync_io_model_saturate_u64_to_u32",
        "sync_io_model_release_pin", "sync_io_pio0_output_program",
        "sync_io_wave_output_load", "sync_io_wave_output_arm", "sync_io_wave_output_start",
        "sync_io_wave_output_stop", "sync_io_wave_output_cleanup",
        "sync_io_wave_output_manager_init", "sync_io_wave_output_manager_start",
        "sync_io_wave_output_manager_release", "sync_io_core_wave_output_persona_active",
        "sync_io_model_update_completion",
        "sync_io_model_pulse_schedule_get_runtime_owned",
        "sync_io_model_pulse_schedule_get_runtime", "sync_io_model_pulse_schedule_is_running",
        "sync_io_sma_observer_fixed_rate_get_runtime",
        "sync_io_model_pulse_schedule_disarm_owned",
        "sync_io_model_pulse_schedule_disarm", "sync_io_sma_observer_fixed_rate_disarm",
        "sync_io_pulse_schedule_arm_on_pin_common_owned",
        "sync_io_pulse_schedule_arm_on_pin_common",
        "sync_io_sma_observer_fixed_rate_arm_owned",
        "sync_io_sma_observer_fixed_rate_arm",
    ]
    harness += "\n".join(production_function(source, name) for name in functions)
    harness += r'''
static bool validator(void *ctx) {
    assert(ctx == &validations); ++validations;
    assert(critical_depth==0);
    assert(sm_claimed && dma_claimed && program_loaded);
    assert(!enabled && !busy);
    if (validator_interleave!=NULL) validator_interleave();
    if (alter_clock) sys_hz-=1000;
    if (alter_divider) host_pio.sm[s_model_pulse.sm].clkdiv+=256;
    return validator_ok;
}
static void assert_released(void) {
    static unsigned owner;
    assert(!sm_claimed && !dma_claimed && !program_loaded && !enabled && !busy);
    assert(!s_wave_output_manager_active && !pins_high);
    assert(critical_depth==0);
    assert(sync_io_workspace_claim(&owner));
    assert(sync_io_workspace_release(&owner));
}
static bool cancel_preparing;
static unsigned interleavings;
static sync_io_fixed_rate_runtime_t published_before;
static void exercise_preparing_reentry(void) {
    ++interleavings;
    const unsigned starts_before=starts, aborts_before=aborts;
    const sync_io_model_pulse_t model_before=s_model_pulse;
    uint32_t words_before[SYNC_IO_SHARED_WORKSPACE_WORDS];
    memcpy(words_before,sync_io_shared_workspace,sizeof(words_before));
    assert(sync_io_workspace_held_by(&s_model_pulse));
    assert(sync_io_core_wave_output_persona_active());
    const unsigned reads_before=hw_reads;
    sync_io_model_pulse_runtime_t generic_runtime;
    sync_io_model_pulse_schedule_get_runtime(&generic_runtime);
    assert(generic_runtime.running && hw_reads==reads_before);
    /* Legacy disarm must not cancel or release a reserved fixed preparation. */
    sync_io_model_pulse_schedule_disarm();
    sync_io_model_pulse_entry_ns_t entry={1000000,1000};
    assert(!sync_io_pulse_schedule_arm_on_pin_common(
        BOARD_SYNC_PIO_FAST,BOARD_SYNC_PIO0_SCHEDULED_TRIGGER_SM,0,
        BOARD_SYNC_OUTPUT_BASE_PIN,0,NULL,&entry,0,0,0,0,1,true,4));
    sync_io_rate_schedule_request_t nested={999,1000,1000000,1000,32,4};
    sync_io_fixed_rate_runtime_t nested_result;
    memset(&nested_result,0xA5,sizeof(nested_result));
    const sync_io_fixed_rate_runtime_t nested_before=nested_result;
    assert(!sync_io_sma_observer_fixed_rate_arm(
        &nested,&nested_result,validator,&validations));
    assert(memcmp(&nested_result,&nested_before,sizeof(nested_before))==0);
    sync_io_sma_observer_fixed_rate_get_runtime(&nested_result);
    assert(memcmp(&nested_result,&published_before,sizeof(nested_result))==0);
    if (cancel_preparing) sync_io_sma_observer_fixed_rate_disarm();
    assert(sync_io_workspace_held_by(&s_model_pulse));
    assert(memcmp(&s_model_pulse,&model_before,sizeof(model_before))==0);
    assert(memcmp(sync_io_shared_workspace,words_before,sizeof(words_before))==0);
    assert(starts==starts_before && aborts==aborts_before);
}
int main(void) {
    assert(resource_arbiter_init());
    sync_io_rate_schedule_request_t req={17,1000,1000000,1000,2048,4};
    sync_io_fixed_rate_runtime_t rt={0}, saved={0};
    static unsigned foreign;
    memset(sync_io_shared_workspace,0xA5,sizeof(sync_io_shared_workspace));
    uint32_t untouched[SYNC_IO_SHARED_WORKSPACE_WORDS];
    memcpy(untouched,sync_io_shared_workspace,sizeof(untouched));
    bool *conflicts[]={&capture,&sequence,&encoder,&pwm};
    for (unsigned i=0;i<sizeof(conflicts)/sizeof(*conflicts);++i) {
        *conflicts[i]=true;
        assert(!sync_io_sma_observer_fixed_rate_arm(&req,&rt,validator,&validations));
        assert(*conflicts[i] && starts==0 && aborts==0);
        assert(memcmp(untouched,sync_io_shared_workspace,sizeof(untouched))==0);
        *conflicts[i]=false;
    }
    assert(sync_io_workspace_claim(&foreign));
    assert(!sync_io_sma_observer_fixed_rate_arm(&req,&rt,validator,&validations));
    assert(sync_io_workspace_held_by(&foreign));
    assert(memcmp(untouched,sync_io_shared_workspace,sizeof(untouched))==0);
    assert(sync_io_workspace_release(&foreign));
    sys_hz=251000000;
    assert(!sync_io_sma_observer_fixed_rate_arm(&req,&rt,validator,&validations));
    sys_hz=100000000;
    assert(!sync_io_sma_observer_fixed_rate_arm(&req,&rt,validator,&validations));
    sys_hz=250000000;
    assert(!sync_io_sma_observer_fixed_rate_arm(&req,&rt,NULL,NULL));
    assert_released();
    sm_claimed=true;
    assert(!sync_io_sma_observer_fixed_rate_arm(&req,&rt,validator,&validations));
    assert(sm_claimed && !dma_claimed && starts==0 && aborts==0);
    sm_claimed=false; assert_released();
    dma_claimed=true;
    assert(!sync_io_sma_observer_fixed_rate_arm(&req,&rt,validator,&validations));
    assert(dma_claimed && !sm_claimed && starts==0 && aborts==0);
    dma_claimed=false; assert_released();
    can_load=false;
    assert(!sync_io_sma_observer_fixed_rate_arm(&req,&rt,validator,&validations));
    can_load=true; assert_released();
    validator_ok=false;
    assert(!sync_io_sma_observer_fixed_rate_arm(&req,&rt,validator,&validations));
    assert(starts==0); assert_released(); validator_ok=true;
    alter_clock=true;
    assert(!sync_io_sma_observer_fixed_rate_arm(&req,&rt,validator,&validations));
    assert(starts==0); assert_released(); alter_clock=false; sys_hz=250000000;
    alter_divider=true;
    assert(!sync_io_sma_observer_fixed_rate_arm(&req,&rt,validator,&validations));
    assert(starts==0); assert_released(); alter_divider=false;
    assert(sync_io_sma_observer_fixed_rate_arm(&req,&rt,validator,&validations));
    assert(rt.configured && rt.pulse.running && rt.request.request_id==17);
    assert(rt.pio_divider256==256 && rt.system_clock_hz==250000000);
    assert(starts==1 && dma_words==4096 && busy && enabled);
    assert(sync_io_workspace_held_by(&s_model_pulse));
    unsigned generic_reads=hw_reads;
    sync_io_model_pulse_runtime_t generic_active;
    sync_io_model_pulse_schedule_get_runtime(&generic_active);
    assert(generic_active.running && sync_io_model_pulse_schedule_is_running());
    assert(hw_reads==generic_reads && starts==1);
    sync_io_model_pulse_t model_saved=s_model_pulse;
    saved=rt;
    memcpy(untouched,sync_io_shared_workspace,sizeof(untouched));
    unsigned aborts_before=aborts;
    ++req.request_id;
    assert(!sync_io_sma_observer_fixed_rate_arm(&req,&rt,validator,&validations));
    assert(memcmp(&model_saved,&s_model_pulse,sizeof(model_saved))==0);
    assert(memcmp(&rt,&saved,sizeof(rt))==0);
    assert(memcmp(untouched,sync_io_shared_workspace,sizeof(untouched))==0);
    assert(starts==1 && aborts==aborts_before);
    sync_io_sma_observer_fixed_rate_disarm();
    assert_released();
    sync_io_sma_observer_fixed_rate_get_runtime(&rt);
    assert(rt.configured && !rt.pulse.running && rt.request.request_id==17);
    aborts_before=aborts;
    sync_io_sma_observer_fixed_rate_disarm();
    assert(aborts==aborts_before); assert_released();
    /* A new batch never retires while its last high loop still owns X. */
    assert(sync_io_sma_observer_fixed_rate_arm(&req,&rt,validator,&validations));
    now_us=10000000;
    busy=false; fifo_empty=true;
    dma_hw->ch[s_model_pulse.dma_ch].transfer_count=0;
    pc=s_model_pulse.offset+6; pins_high=true;
    sync_io_sma_observer_fixed_rate_get_runtime(&rt);
    assert(rt.pulse.running && rt.pulse.completed_pulses<req.pulse_count);
    assert(sync_io_workspace_held_by(&s_model_pulse));
    pc=s_model_pulse.offset;
    dma_hw->ch[s_model_pulse.dma_ch].transfer_count=1;
    sync_io_sma_observer_fixed_rate_get_runtime(&rt);
    assert(rt.pulse.running);
    dma_hw->ch[s_model_pulse.dma_ch].transfer_count=0;
    fifo_empty=false;
    sync_io_sma_observer_fixed_rate_get_runtime(&rt);
    assert(rt.pulse.running);
    fifo_empty=true; busy=true;
    sync_io_sma_observer_fixed_rate_get_runtime(&rt);
    assert(rt.pulse.running);
    busy=false; pins_high=false;
    /* Core1's generic busy probe must not harvest even a physically complete
     * fixed batch or read the Core0 publication. Only the dedicated API does. */
    generic_reads=hw_reads;
    sync_io_model_pulse_schedule_get_runtime(&generic_active);
    assert(generic_active.running && sync_io_model_pulse_schedule_is_running());
    assert(hw_reads==generic_reads && sync_io_workspace_held_by(&s_model_pulse));
    sync_io_sma_observer_fixed_rate_get_runtime(&rt);
    assert(!rt.pulse.running && rt.pulse.completed_pulses==req.pulse_count);
    assert_released(); saved=rt;
    assert(sync_io_workspace_claim(&foreign));
    unsigned reads_before=hw_reads;
    sync_io_sma_observer_fixed_rate_get_runtime(&rt);
    assert(hw_reads==reads_before && memcmp(&saved,&rt,sizeof(rt))==0);
    aborts_before=aborts;
    sync_io_sma_observer_fixed_rate_disarm();
    assert(aborts==aborts_before && sync_io_workspace_held_by(&foreign));
    assert(sync_io_workspace_release(&foreign));
    assert_released();
    /* Interleave immediately after lease acquisition (before fill), and from
     * the real manager START validator (after DMA configuration, before start).
     * Each phase is exercised with and without an explicit fixed STOP. */
    for (unsigned injection=0;injection<2;++injection) {
        for (unsigned cancel=0;cancel<2;++cancel) {
            sync_io_sma_observer_fixed_rate_get_runtime(&published_before);
            unsigned starts_before=starts, interleavings_before=interleavings;
            cancel_preparing=(cancel!=0);
            if (injection==0) claim_interleave=exercise_preparing_reentry;
            else validator_interleave=exercise_preparing_reentry;
            ++req.request_id;
            const sync_io_fixed_rate_runtime_t output_before=rt;
            bool armed=sync_io_sma_observer_fixed_rate_arm(&req,&rt,validator,&validations);
            claim_interleave=NULL; validator_interleave=NULL;
            assert(interleavings==interleavings_before+1);
            if (cancel_preparing) {
                assert(!armed && starts==starts_before);
                assert(memcmp(&rt,&output_before,sizeof(rt))==0);
                sync_io_sma_observer_fixed_rate_get_runtime(&rt);
                assert(memcmp(&rt,&published_before,sizeof(rt))==0);
            } else {
                assert(armed && starts==starts_before+1);
                assert(rt.pulse.running && rt.request.request_id==req.request_id);
                sync_io_sma_observer_fixed_rate_disarm();
            }
            assert_released();
        }
    }
    return 0;
}
'''
    path = tmp_path / "fixed_owner.c"
    path.write_text(harness, encoding="utf-8")
    includes = ["tests/unit/host_stubs", "boards/rp2350_trig/inc", "components/tdma/inc",
                "components/sync_io/inc", "components/sync_io/src",
                "components/resource_arbiter/inc", "osal/inc"]
    executable = tmp_path / "fixed_owner.exe"
    command = [shutil.which("gcc"), "-std=c11", "-Wall", "-Wextra", "-Werror",
               *("-I" + str(ROOT / item) for item in includes), str(path),
               str(SRC / "sync_io_persona_resources.c"), str(SRC / "sync_io_persona_manager.c"),
               str(ROOT / "components/resource_arbiter/src/resource_arbiter.c"), "-o", str(executable)]
    built = subprocess.run(command, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    ran = subprocess.run([str(executable)], capture_output=True, text=True)
    assert ran.returncode == 0, ran.stdout + ran.stderr


@pytest.mark.parametrize("filename,name", [
    ("sync_io_mode_seq_step.c", "sync_io_seq_step_arm"),
    ("sync_io_mode_enc_count.c", "sync_io_enc_count_arm"),
])
def test_legacy_modes_guard_fixed_owner_before_side_effects(filename, name):
    body = production_function((SRC / filename).read_text(encoding="utf-8"), name)
    first_guard = body.split("{", 1)[1].split("\n    }", 1)[0]
    assert "sync_io_core_wave_output_persona_active()" in first_guard
    assert "return false;" in first_guard
    assert "disarm();" not in first_guard
    assert "pio_sm_set_enabled" not in first_guard
    assert "dma_channel_abort" not in first_guard


def test_legacy_schedule_reserves_idle_before_owned_body():
    body = production_function((SRC / "sync_io_model_sched.c").read_text(encoding="utf-8"),
                               "sync_io_pulse_schedule_arm_on_pin_common")
    guard = body.index("sync_io_schedule_reserve(SYNC_IO_SCHEDULE_IDLE,")
    rejection = body.index("return false;", guard)
    assert rejection < body.index("sync_io_pulse_schedule_arm_on_pin_common_owned(")
