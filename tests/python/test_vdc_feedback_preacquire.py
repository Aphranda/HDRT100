"""Real RefMem service must hold FIFO ownership until admission is known.

Production refresh/readers, service, codec and FIFO are compiled unchanged.
Only external snapshots and board identity are controlled by the host fixture.
"""
import subprocess

import pytest

from test_vdc_boundary_command_transport import FIFO_SEAM, MANAGER, TESTS
from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import ROOT, compile_executable
from test_vdc_feedback_transport import FEEDBACK_SOURCES, make_feedback_harness


@pytest.fixture(scope="module")
def preacquire_executable(tmp_path_factory):
    harness = make_feedback_harness(MANAGER).replace(
        "int main(int argc,char **argv)", "int legacy_main(int argc,char **argv)")
    harness = "#include <stdlib.h>\n#include <signal.h>\n" + harness
    harness = harness.replace("ses==session && role==9 && epoch==3",
        "ses==session && role==profile.control_profile.generation && epoch==3")
    harness = harness.replace("static tdma_service_service_t owner;",
        FIFO_SEAM + "\nstatic tdma_service_service_t owner;")
    harness = harness.replace("tdma_flight_fifo_core0_publish_tx(&service->flight_fifo,",
        "boundary_test_fifo_publish(&service->flight_fifo,")
    harness = harness.replace("static bool mode_available=true;",
        "static bool mode_available=true;\nstatic unsigned injected_refmem_call;")
    snapshot = "{ *out=profile; if(race_guard) *race_guard+=2; return binding_available; }"
    assert harness.count(snapshot) == 1
    harness = harness.replace(snapshot,
        "{ if(injected_refmem_call && --injected_refmem_call==0u)return false; "
        "*out=profile; if(race_guard) *race_guard+=2; return binding_available; }")
    harness += TESTS.replace("int main(int argc,char **argv)",
        "int boundary_main(int argc,char **argv)")
    source = (ROOT / "components/distributed_refmem/src/distributed_refmem.c").read_text(encoding="utf-8")
    definitions = "\n".join(ingress_definition(source, name) for name in (
        "distributed_refmem_tdma_flight_sync_update_ring",
        "distributed_refmem_tdma_flight_sync_service"))
    return compile_executable(tmp_path_factory.mktemp("preacquire"), "preacquire",
        harness + PREACQUIRE_CASES.replace("SERVICE_DEFINITIONS", definitions), FEEDBACK_SOURCES)


def run_case(executable, *args):
    result = subprocess.run([str(executable), *map(str, args)], capture_output=True,
                            text=True, timeout=10)
    (executable.parent / ("-".join(map(str, args)) + ".log")).write_text(
        result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


@pytest.mark.parametrize("master", [False, True], ids=["follower", "master"])
@pytest.mark.parametrize("getter", [0, 1, 2], ids=["auto", "ordinary", "feedback"])
@pytest.mark.parametrize("fragment", range(1, 15))
def test_busy_middle_fragment_resumes_same_group(preacquire_executable, master, getter, fragment):
    run_case(preacquire_executable, "recover", int(master), getter, fragment)


@pytest.mark.parametrize("case", [
    "inactive", "stop", "role_aba", "overflow", "expiry", "idle_ordinary_tx",
    "ordinary_busy_expiry", "follower_epoch_exhausted", "master_epoch_exhausted",
])
def test_service_lifecycle_and_backpressure(preacquire_executable, case):
    run_case(preacquire_executable, case)


PREACQUIRE_CASES = r'''

static unsigned board_identity_get_no(void) { return 3u; }
static void distributed_refmem_tdma_reset_resident_command_state(void)
{ assert(!"stable identity fixture must not reset command state"); }
SERVICE_DEFINITIONS
static void beat(void)
{
    ++now_ms;distributed_refmem_tdma_flight_sync_service();
    tdma_flight_tx_view_t tx;
    if(tdma_flight_fifo_core1_acquire_tx(&owner.flight_fifo,&tx))
        tdma_flight_fifo_core1_release_tx(&owner.flight_fifo);
}
static tdma_flight_fifo_snapshot_t fifo(void)
{ tdma_flight_fifo_snapshot_t out;assert(tdma_service_get_flight_fifo_snapshot(&owner,&out));return out; }
static void inject(unsigned which)
{ if(which==0)mode_available=false;else injected_refmem_call=which; }
static void restore(void) { mode_available=true;injected_refmem_call=0; }
static void configure(bool master,uint8_t wire[64])
{
    boundary_setup(master?0:2);auto_mode=true;refresh();offer_result=0;
    s_tdma_flight_sync.enabled=1;owner.ring_runtime.enabled=1;
    if(master) {
        refmem_sync_vdc_feedback_record_t r=record(1,10);r.schema_version=4;r.domain_flags=31;
        memset(&r.rate,0,sizeof(r.rate));r.rate.absolute_output_ns_lo=900000000;
        r.rate.coordinate_ns=10000;r.rate.model_token=7;r.rate.control_session=session;
        assert(refmem_sync_vdc_feedback_encode(&r,6,wire));
    } else {
        refmem_sync_vdc_boundary_command_t c=command();c.flags=3;
        assert(refmem_sync_vdc_boundary_command_encode(&c,6,wire));
    }
}
static void send_fragment(bool master,const uint8_t wire[64],uint32_t sequence,unsigned fragment)
{ enqueue_boundary(wire,master?1:0,sequence,fragment,master?0x12:0x13); }
static void clean_group(bool master,const uint8_t wire[64],uint32_t base)
{ for(unsigned i=0;i<16;i++){send_fragment(master,wire,base+i,i);beat();} }
static void recovery_case(bool master,unsigned which,unsigned pause)
{
    uint8_t wire[64];configure(master,wire);const unsigned source=master?1:0;
    for(unsigned i=0;i<16;i++) {
        send_fragment(master,wire,100+i,i);
        if(i==pause) {
            tdma_flight_fifo_snapshot_t before=fifo();inject(which);beat();
            tdma_flight_fifo_snapshot_t held=fifo();
            assert(held.rx_queued_count==1 && held.rx_acquire_count==before.rx_acquire_count &&
                held.rx_release_count==before.rx_release_count && owner.flight_fifo.core0_guard==0);
            assert(s_feedback_rx[source].assembly.next_fragment==i);
            restore();beat();assert(fifo().rx_queued_count==0);
        } else beat();
    }
    distributed_refmem_vdc_feedback_rx_snapshot_t rx=rxread(source);
    assert(rx.retained && rx.active && rx.receive_count==1 && !rx.reject_count && !rx.timeout_count);
    assert(!memcmp(rx.record,wire,64));
}
static void inactive_case(void)
{
    uint8_t wire[64];configure(false,wire);
    profile.control_profile.valid=0;mode_available=false;
    const uint32_t tx_before=fifo().tx_publish_count,rx_before=fifo().rx_acquire_count;
    enqueue_boundary(wire,0,100,0,0x10);beat();
    assert(!s_vdc_flight_rx_admission_ready && !s_feedback_ready);
    assert(fifo().rx_acquire_count==rx_before+1 && !fifo().rx_queued_count);
    assert(fifo().tx_publish_count>tx_before); /* ordinary publication still progresses */
    restore();
}
static void stop_or_aba_case(bool stop)
{
    uint8_t wire[64];configure(false,wire);
    for(unsigned i=0;i<7;i++){send_fragment(false,wire,100+i,i);beat();}
    const uint32_t admission=s_vdc_flight_rx_binding.rx_admission_epoch;
    send_fragment(false,wire,107,7);inject(0);beat();assert(fifo().rx_queued_count==1);
    restore();
    if(stop) {
        ring.enabled=0;owner.ring_runtime.enabled=0;
        injected_refmem_call=1;mode_available=false;beat();
        assert(!(s_feedback_rx[0].assembly.state&REFMEM_VDC_FEEDBACK_ASSEMBLY_ACTIVE));
        assert(!s_feedback_ready && !s_vdc_flight_rx_admission_ready);
        assert(injected_refmem_call==1); /* known STOP did not need the busy VDC getter */
        restore();
        ring.enabled=1;owner.ring_runtime.enabled=1;
    } else {
        profile.control_profile.valid=0;beat();
        assert(!s_feedback_ready && !s_vdc_flight_rx_admission_ready);
        profile.control_profile.valid=1;profile.control_profile.generation++;
    }
    beat();assert(s_vdc_flight_rx_binding.rx_admission_epoch!=admission);
    assert(!rxread(0).retained && !fifo().rx_queued_count);
    clean_group(false,wire,200);assert(rxread(0).receive_count==1);
}
static void overflow_case(void)
{
    uint8_t wire[64];configure(false,wire);
    for(unsigned i=0;i<7;i++){send_fragment(false,wire,100+i,i);beat();}
    const uint32_t rejects=fifo().rx_publish_drop_count;
    inject(0);
    for(unsigned i=7;i<11;i++){send_fragment(false,wire,100+i,i);beat();}
    assert(fifo().rx_queued_count==TDMA_FLIGHT_RX_FRAME_SLOT_COUNT && owner.flight_fifo.core0_guard==0);
    uint8_t data[TDMA_FLIGHT_SHORT_PAYLOAD_SIZE]={0};
    assert(!tdma_flight_fifo_core1_publish_rx(&owner.flight_fifo,data,tdma_flight_payload_size(ring.node_count),111,111,1,0,0));
    assert(fifo().rx_publish_drop_count==rejects+1);
    /* Existing TX policy pauses an active typed group rather than splice an
     * ordinary mailbox. The service still calls publish each invocation. */
    assert(s_feedback_tx_active && !s_feedback_ready);
    restore();beat();assert(!fifo().rx_queued_count && s_feedback_rx[0].assembly.next_fragment==11);
    for(unsigned i=12;i<16;i++){send_fragment(false,wire,100+i,i);beat();}
    assert(!rxread(0).retained);
    clean_group(false,wire,200);assert(rxread(0).receive_count==1);
}
static void expiry_case(void)
{
    uint8_t wire[64];configure(false,wire);
    for(unsigned i=0;i<7;i++){send_fragment(false,wire,100+i,i);beat();}
    send_fragment(false,wire,107,7);inject(0);beat();
    now_ms+=REFMEM_VDC_FEEDBACK_ASSEMBLY_TIMEOUT_MS;beat();
    restore();beat();assert(!rxread(0).retained && rxread(0).timeout_count==1);
    clean_group(false,wire,200);assert(rxread(0).receive_count==1);
}
static void idle_ordinary_tx_case(void)
{
    uint8_t wire[64];configure(false,wire);assert(!s_feedback_tx_active);
    const uint32_t tx_before=fifo().tx_publish_count;
    enqueue_boundary(wire,0,100,0,0x10);inject(0);beat();
    assert(fifo().rx_queued_count==1 && fifo().tx_publish_count>tx_before);
    restore();beat();assert(!fifo().rx_queued_count);
}
static void ordinary_busy_expiry_case(void)
{
    uint8_t wire[64];configure(false,wire);
    for(unsigned i=0;i<7;i++){send_fragment(false,wire,100+i,i);beat();}
    send_fragment(false,wire,107,7);
    now_ms+=REFMEM_VDC_FEEDBACK_ASSEMBLY_TIMEOUT_MS;
    inject(1);beat(); /* Ordinary refresh fails, feedback expiry must still run. */
    assert(fifo().rx_queued_count==1 && owner.flight_fifo.core0_guard==0);
    assert(rxread(0).timeout_count==1 && !rxread(0).retained);
    restore();beat();assert(!rxread(0).retained);
    clean_group(false,wire,200);assert(rxread(0).receive_count==1);
}
static void epoch_exhausted_case(bool master)
{
    uint8_t wire[64];configure(master,wire);
    send_fragment(master,wire,100,0);
    const tdma_flight_fifo_snapshot_t before=fifo();
    owner.flight_fifo.rx_admission_epoch=UINT32_MAX;
    profile.control_profile.generation++;
    for(unsigned i=0;i<3;i++) {
        beat();const tdma_flight_fifo_snapshot_t after=fifo();
        assert(after.rx_queued_count==1 && after.rx_acquire_count==before.rx_acquire_count &&
            after.rx_release_count==before.rx_release_count && owner.flight_fifo.core0_guard==0);
        assert(owner.flight_fifo.rx_admission_epoch==UINT32_MAX);
        assert(!rxread(master?1:0).retained);
    }
}
/* An assertion must be a bounded test failure, not a Windows crash dialog. */
static void assertion_abort(int signal_number)
{ (void)signal_number;_Exit(3); }
int main(int argc,char **argv)
{
    signal(SIGABRT,assertion_abort);
    assert(argc>=2);
    if(!strcmp(argv[1],"recover")){assert(argc==5);recovery_case(atoi(argv[2])!=0,(unsigned)atoi(argv[3]),(unsigned)atoi(argv[4]));}
    else if(!strcmp(argv[1],"inactive"))inactive_case();
    else if(!strcmp(argv[1],"stop"))stop_or_aba_case(true);
    else if(!strcmp(argv[1],"role_aba"))stop_or_aba_case(false);
    else if(!strcmp(argv[1],"overflow"))overflow_case();
    else if(!strcmp(argv[1],"expiry"))expiry_case();
    else if(!strcmp(argv[1],"idle_ordinary_tx"))idle_ordinary_tx_case();
    else if(!strcmp(argv[1],"ordinary_busy_expiry"))ordinary_busy_expiry_case();
    else if(!strcmp(argv[1],"follower_epoch_exhausted"))epoch_exhausted_case(false);
    else if(!strcmp(argv[1],"master_epoch_exhausted"))epoch_exhausted_case(true);
    else assert(!"unknown pre-acquire case");
    puts("pre-acquire behavior passed");return 0;
}

'''
