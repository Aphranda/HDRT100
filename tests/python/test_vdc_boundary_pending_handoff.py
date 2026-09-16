"""Real FIFO/RefMem COMPLETE handoff under controlled snapshot contention."""
import subprocess

import pytest

from test_vdc_boundary_command_transport import FIFO_SEAM, MANAGER, TESTS
from test_vdc_command_owner import compile_executable
from test_vdc_feedback_transport import FEEDBACK_SOURCES, make_feedback_harness


@pytest.fixture(scope="module")
def pending_executable(tmp_path_factory):
    harness = make_feedback_harness(MANAGER).replace(
        "int main(int argc,char **argv)", "int legacy_main(int argc,char **argv)")
    harness = harness.replace("static tdma_service_service_t owner;", FIFO_SEAM +
                              "\nstatic tdma_service_service_t owner;")
    harness = harness.replace("tdma_flight_fifo_core0_publish_tx(&service->flight_fifo,",
                              "boundary_test_fifo_publish(&service->flight_fifo,")
    harness = harness.replace("static bool binding_available=true,",
                              "static bool ordinary_available=true;\nstatic bool binding_available=true,")
    needle = "return binding_available;\n}\nbool vdc_dpll_manager_get_snapshot"
    assert needle in harness
    harness = harness.replace(needle,
        "return binding_available && ordinary_available;\n}\nbool vdc_dpll_manager_get_snapshot")
    harness = harness.replace("static bool mode_available=true;",
        "static bool mode_available=true;\nstatic unsigned mode_reads,mode_busy_at;")
    harness = harness.replace("{ if(!mode_available)return false;*out=auto_mode;return true; }",
        "{ ++mode_reads;if(!mode_available || mode_reads==mode_busy_at)return false;"
        "*out=auto_mode;return true; }")
    harness += TESTS.replace("int main(int argc,char **argv)",
                            "int boundary_existing_main(int argc,char **argv)")
    return compile_executable(tmp_path_factory.mktemp("pending-handoff"), "pending_handoff",
                              harness + PENDING_TESTS, FEEDBACK_SOURCES)


@pytest.mark.parametrize("case", [
    "feedback_busy", "ordinary_busy", "busy_empty_fifo", "new_mail", "stop_aba",
    "admission_aba", "role_aba", "session_aba", "clock_change", "schedule_change",
    "mode_change", "expiry", "expiry_wrap", "duplicate", "master_unchanged", "deferred_once",
])
def test_boundary_pending_handoff(pending_executable, case):
    result = subprocess.run([str(pending_executable), case], capture_output=True,
                            text=True, timeout=10)
    (pending_executable.parent / (case + ".log")).write_text(
        result.stdout + result.stderr, encoding="utf-8")
    assert result.returncode == 0, result.stdout + result.stderr


PENDING_TESTS = r'''
_Static_assert(sizeof(s_boundary_pending_admission) + sizeof(s_boundary_pending_completed_ms)==8u,
               "pending metadata is eight global bytes, independent of node count");
static void frozen_complete(uint8_t wire[64],bool ordinary)
{
    assert(refmem_sync_vdc_boundary_command_encode(&offer,6,wire));
    for(unsigned i=0;i<15;i++) {
        enqueue_boundary(wire,0,100+i,i,0x13);receive();
    }
    if(ordinary)ordinary_available=false;else mode_available=false;
    enqueue_boundary(wire,0,115,15,0x13);receive();
    assert(!rxread(0).retained && rxread(0).receive_count==0);
    assert(!memcmp(s_feedback_rx[0].assembly.payload,wire,64));
}
int main(int argc,char **argv)
{
    assert(argc==2);const char *test=argv[1];
    if(!strcmp(test,"master_unchanged")) {
        char *args[]={argv[0],"rx_interleaved"};return legacy_main(2,args);
    }
    boundary_setup(2);uint8_t wire[64];
    if(!strcmp(test,"expiry_wrap"))now_ms=UINT32_MAX-10u;
    frozen_complete(wire,!strcmp(test,"ordinary_busy"));
    const uint32_t completed_ms=now_ms,first_ms=s_feedback_rx[0].assembly.first_ms;
    if(!strcmp(test,"busy_empty_fifo")) {
        for(unsigned i=0;i<20;i++){now_ms++;refresh();receive();assert(!rxread(0).retained);}
        assert(!memcmp(s_feedback_rx[0].assembly.payload,wire,64));
    } else if(!strcmp(test,"new_mail")) {
        /* Ordinary data still consumes its FIFO lease; neither it nor a fresh
         * command may overwrite the complete command awaiting final checks. */
        enqueue_boundary(wire,0,116,0,0x10);receive();
        refmem_sync_vdc_boundary_command_t newer=offer;newer.command_seq++;
        uint8_t other[64];assert(refmem_sync_vdc_boundary_command_encode(&newer,6,other));
        command_group(other,0,200);
        assert(!memcmp(s_feedback_rx[0].assembly.payload,wire,64));
        assert(!rxread(0).retained);
    } else if(!strcmp(test,"deferred_once")) {
        mode_available=true;
        mode_busy_at=mode_reads+2; /* Refresh succeeds; deferred getter is busy. */
        refresh();
        const unsigned reads=mode_reads;
        assert(reads==mode_busy_at);
        command_group(wire,0,200);
        assert(mode_reads==reads); /* No retry for each newly arrived mail. */
        assert(!rxread(0).retained);
        assert(!memcmp(s_feedback_rx[0].assembly.payload,wire,64));
    }
    mode_available=ordinary_available=true;
    bool cancelled=false;
    if(!strcmp(test,"stop_aba")) {
        ring.enabled=0;refresh();ring.enabled=1;refresh();cancelled=true;
    } else if(!strcmp(test,"admission_aba")) {
        /* Ordinary owner observes STOP/ARM while feedback snapshot is busy. */
        mode_available=false;ring.enabled=0;refresh();ring.enabled=1;refresh();
        mode_available=true;cancelled=true;
    } else if(!strcmp(test,"role_aba")) {
        profile.control_profile.mode=VDC_DPLL_CONTROL_MODE_MASTER;
        profile.control_profile.generation++;refresh();
        profile.control_profile.mode=VDC_DPLL_CONTROL_MODE_FOLLOWER;
        profile.control_profile.generation++;refresh();cancelled=true;
    } else if(!strcmp(test,"session_aba")) {
        session++;refresh();session--;refresh();cancelled=true;
    } else if(!strcmp(test,"clock_change")) {
        profile.clock_run_id++;refresh();cancelled=true;
    } else if(!strcmp(test,"schedule_change")) {
        ring.schedule_crc32++;profile.schedule.schedule_crc32++;refresh();cancelled=true;
    } else if(!strcmp(test,"mode_change")) {
        auto_mode=true;refresh();cancelled=true;
    } else if(!strncmp(test,"expiry",6)) {
        now_ms=first_ms+REFMEM_VDC_FEEDBACK_ASSEMBLY_TIMEOUT_MS;cancelled=true;
    }
    now_ms+=3;refresh(); /* Empty FIFO must still finish/retire the handoff. */
    distributed_refmem_vdc_feedback_rx_snapshot_t rx=rxread(0);
    if(cancelled) {
        assert(!rx.retained && rx.receive_count==0);
        if(!strncmp(test,"expiry",6))assert(rx.timeout_count==1);
        refresh();assert(rxread(0).receive_count==0);
        if(!strncmp(test,"expiry",6))assert(rxread(0).timeout_count==1);
        enqueue_boundary(wire,0,115,15,0x13);receive();
        assert(!rxread(0).retained && rxread(0).receive_count==0);
    } else {
        assert(rx.retained && rx.active && rx.receive_count==1);
        assert(!memcmp(rx.record,wire,64));
        assert(rx.first_rx_ms==first_ms && rx.last_rx_ms==completed_ms);
        assert(rx.last_transport_seq==115 && rx.reject_count==0);
        refresh();assert(rxread(0).receive_count==1);
        if(!strcmp(test,"duplicate")) {
            enqueue_boundary(wire,0,115,15,0x13);receive();
            assert(rxread(0).duplicate_count==1 && rxread(0).receive_count==1);
            command_group(wire,0,300);
            assert(rxread(0).duplicate_count==2 && rxread(0).receive_count==1);
        }
    }
    puts("pending handoff scenario passed");return 0;
}
'''
