"""Exercise the production AO consumer with instrumented SDK lock semantics."""
import re
import shutil
import subprocess
from pathlib import Path

import pytest

from test_sync_io_workspace import function

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / "components/sync_trigger/src/sync_trigger.c"


def consumer_body(text):
    return function(text.replace('__attribute__((noinline,optimize("Os"))) ', '')
                    .replace('TRIGGER_DISPATCH_HOT ', ''),
                    "ao_queue_try_remove")


@pytest.fixture(scope="module", params=[0, 1])
def queue_consumer(tmp_path_factory, request):
    d = tmp_path_factory.mktemp("trigger-queue")
    body = consumer_body(SOURCE.read_text(encoding="utf-8"))
    (d / "queue.c").write_text(MOCK + body + HARNESS, encoding="utf-8")
    exe = d / "queue.exe"
    r = subprocess.run([shutil.which("gcc"), "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
        "-DPROJECT_USE_MULTICORE=1", f"-DPICO_QUEUE_MAX_LEVEL={request.param}",
        str(d / "queue.c"), "-o", str(exe)], capture_output=True, text=True)
    assert r.returncode == 0, r.stderr
    return exe


@pytest.mark.parametrize("case", ["busy", "empty", "wrap", "order", "notify", "irq_disabled"])
def test_consumer(queue_consumer, case):
    r = subprocess.run([str(queue_consumer), case], capture_output=True, text=True, timeout=5)
    assert r.returncode == 0, r.stdout + r.stderr


def test_only_service_consumes_ao_queue():
    text = SOURCE.read_text(encoding="utf-8")
    assert text.count("ao_queue_try_remove(&s_ao.queue, &event)") == 1
    assert not re.search(r"\bqueue_(?:try_remove|remove_blocking)\s*\(\s*&s_ao\.queue", text)
    body = consumer_body(text)
    multicore = body.split("#else", 1)[0]
    assert "spin_lock_blocking" not in multicore
    assert "queue_try_remove(" not in multicore.replace("ao_queue_try_remove(", "")
    assert "queue_get_level(" not in multicore
    assert multicore.count("spin_try_lock_unsafe(") == 1
    assert "lock_internal_spin_unlock_with_notify" not in multicore
    assert not re.search(r"queue_(?:add|remove|peek)_blocking\s*\(", text)
    assert "static sync_trigger_ao_t s_ao;" in text
    direct_users = re.findall(r"(\w+)\(\s*&s_ao\.queue\s*[,)]", text)
    assert set(direct_users) == {"queue_try_add", "queue_get_level", "ao_queue_try_remove"}
    assert not re.search(r"(?:return|=)\s*&s_ao\.queue\b", text)


MOCK = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
typedef unsigned uint;
typedef struct {uint32_t type,value,extra,padding;} trig_event_t;
typedef struct {bool taken;} spin_lock_t;
typedef struct {spin_lock_t *spin_lock;} lock_core_t;
typedef struct {lock_core_t core;uint8_t *data;uint16_t wptr,rptr,element_size,element_count;
#if PICO_QUEUE_MAX_LEVEL
uint16_t max_level;
#endif
} queue_t;
static bool interrupts=true,owned;
static uint tries,unlocks,notifies,reads,fences;
static uint32_t save_and_disable_interrupts(void){uint32_t before=interrupts;interrupts=false;return before;}
static void restore_interrupts(uint32_t before){interrupts=before!=0;}
static bool spin_try_lock_unsafe(spin_lock_t *lock){assert(!interrupts&&!owned);++tries;if(lock->taken)return false;lock->taken=true;owned=true;return true;}
static void __mem_fence_acquire(void){assert(owned&&!interrupts);++fences;}
static void spin_unlock(spin_lock_t *lock,uint32_t before){assert(lock->taken&&owned&&!interrupts);lock->taken=false;owned=false;++unlocks;restore_interrupts(before);}
static uint queue_get_level_unsafe(queue_t *q){assert(owned&&!interrupts);++reads;int level=(int)q->wptr-q->rptr;return level<0 ? (uint)(level+q->element_count+1u):(uint)level;}
static void __sev(void){assert(!owned);++notifies;}
'''

HARNESS = r'''
int main(int argc,char **argv){assert(argc==2);const char *c=argv[1];
    spin_lock_t lock={0};trig_event_t events[17]={0};
    queue_t q={.core={.spin_lock=&lock},.data=(uint8_t *)events,.element_count=16,.element_size=sizeof(events[0])};
    trig_event_t output={99,98,97,96},previous=output;
    for(unsigned i=0;i<17;++i)events[i]=(trig_event_t){i,0xabc0+i,0x1234+i,~i};
    if(!strcmp(c,"busy")){
        q.wptr=2;lock.taken=true;queue_t before=q;
        assert(!ao_queue_try_remove(&q,&output));
        assert(!memcmp(&before,&q,sizeof(q))&&!memcmp(&previous,&output,sizeof(output)));
        assert(tries==1&&!unlocks&&!notifies&&!reads&&!fences&&interrupts&&lock.taken);return 0;
    }
    if(!strcmp(c,"empty")){
        assert(!ao_queue_try_remove(&q,&output));
        assert(!memcmp(&previous,&output,sizeof(output))&&tries==1&&unlocks==1&&!notifies&&reads==1&&fences==1&&interrupts&&!lock.taken);return 0;
    }
    if(!strcmp(c,"irq_disabled"))interrupts=false;
    const bool entry_interrupts=interrupts;
    q.rptr=16;q.wptr=2;
    for(unsigned i=0;i<3;++i){
        const unsigned expected=(16+i)%17;
        assert(ao_queue_try_remove(&q,&output));
        assert(!memcmp(&events[expected],&output,sizeof(output)));
        assert(q.rptr==(expected+1)%17&&q.wptr==2&&!lock.taken&&!owned);
        assert(notifies==i+1&&unlocks==i+1&&interrupts==entry_interrupts);
    }
#if PICO_QUEUE_MAX_LEVEL
    assert(q.max_level==3);
#endif
    assert(!ao_queue_try_remove(&q,&output)&&notifies==3&&unlocks==4&&tries==4);
    /* Same fixed capacity, FIFO order through repeated producer wraps. */
    if(!strcmp(c,"order"))for(unsigned cycle=0;cycle<5;++cycle){
        for(unsigned i=0;i<16;++i){events[q.wptr].type=cycle*16+i;if(++q.wptr>q.element_count)q.wptr=0;}
        for(unsigned i=0;i<16;++i){assert(ao_queue_try_remove(&q,&output));assert(output.type==cycle*16+i);}
        assert(q.rptr==q.wptr);
    }
    return 0;
}
'''
