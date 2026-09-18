"""Execute actual DMA graphs with synthetic peripherals, never timing qualification.

The helper implements its compare/test-bit FIFO contract. Transfers, sniff
selection, memory extents, raw-register order and graph branches execute from
the real C emitter. Physical bus/PIO delays still require target evidence.
"""
import binascii
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess

import pytest

ROOT=Path(__file__).resolve().parents[2]
U32=(1<<32)-1

class FirstRecordStopped(Exception):
    """The controlled DMA model was interrupted at a first-record word."""


@pytest.fixture(scope='module',params=(2,3,4,5,6,7,8))
def graph_exe(request,tmp_path_factory):
    sdk=Path(os.environ.get('PICO_SDK_PATH',Path.home()/'.pico-sdk/sdk/2.2.0'))
    regs=sdk/'src/rp2350/hardware_regs/include'
    assert (regs/'hardware/regs/dma.h').is_file()
    directory=tmp_path_factory.mktemp(f'origin-raw-{request.param}')
    exe=directory/('graph.exe' if os.name=='nt' else 'graph')
    includes=[ROOT/'tests/unit/host_stubs',ROOT/'components/tdma/inc',ROOT/'components/tdma/src',
              ROOT/'components/resource_arbiter/inc',ROOT/'boards/rp2350_trig/inc',regs]
    command=[shutil.which('gcc') or shutil.which('clang'),'-std=c11','-O2','-Wall','-Wextra','-Werror',
             f'-DPROJECT_NODE_CAPACITY={request.param}',*[f'-I{p}' for p in includes],
             str(ROOT/'tests/unit/tdma_origin_raw_graph.c'),'-o',str(exe)]
    result=subprocess.run(command,capture_output=True,text=True,timeout=60)
    assert result.returncode==0,result.stderr
    return exe,request.param


class Model:
    def __init__(self,graph,*,missing_edge=False,bad_mailbox=False,missing_return=False,torn=False,
                 stop_after_first_words=None,seed_sequence=U32-2):
        self.g=graph
        self.r=graph['registers']
        self.o=graph['offsets']
        self.mem=bytearray(0x10000)
        self.mmio={}
        self.help=[]
        self.help_result=[]
        self.fifo=[0x12345678]  # Must be retired before first capture.
        self.rtt=[]
        self.tick=(7<<32)-30 if torn else 7<<32
        self.timer_reads=[]
        self.timestamp_events=[]
        self.pad_high=True
        self.sniff_control=0
        self.sniff_seed=0
        self.sniff_bytes=bytearray()
        self.launches=0
        self.completed=[]
        self.first_writes=0
        self.first_commits=0
        self.stop_after_first_words=stop_after_first_words
        self.latch_enabled=False
        self.latch_x=None
        self.missing_edge=missing_edge
        self.bad_mailbox=bad_mailbox
        self.missing_return=missing_return
        self.cap_target=0
        self.pc=graph['entries']['seed']
        for i,word in enumerate(graph['literals']):self.store(0x2000a000+i*4,word)
        for i,run in enumerate(graph['runs']):
            for j,word in enumerate(run):self.store(0x20008000+i*16+j*4,word)
        for field,value in [('capture_bank',1),('good_bank',0),('local_next_address',graph['entries']['local']),
                            ('local_selected_generation',1),('record_next_address',graph['entries']['record']),
                            ('record_epoch',7),('record_format',2)]:self.state(field,value)
        self.state('bank_version',2)
        self.store(0x20004000+self.o['bank_version']+4,2)
        self.store(0x20004000+self.o['record_time']+36,250000000)
        # Header bytes are generated from the frozen transport format; both CRCs
        # are checked independently after every trip through the emitted graph.
        n=graph['nodes']
        packet=bytearray(n*32+36)
        struct.pack_into('<HBBHBBIBBBBII',packet,0,0x5444,1,1,len(packet),32,graph['local'],
                         seed_sequence,2,5,0,graph['mask'].bit_count()-1,0x123,0x456)
        self.fix_crc(packet)
        for slot in range(n):
            mailbox=bytes((i+slot)&255 for i in range(30))
            packet[32+slot*32:64+slot*32]=mailbox+binascii.crc_hqx(mailbox,0xffff).to_bytes(2,'little')
        self.write_bytes(0x20000000,packet)
        self.write_bytes(0x20000800,packet)
        self.write_bytes(0x20002000,packet[:32])
        local_start=32+graph['local']*32
        self.write_bytes(0x20005000,packet[local_start:local_start+32])
        self.store(0x20005020,1)

    @staticmethod
    def fix_crc(packet):
        struct.pack_into('<I',packet,24,binascii.crc32(packet[:14]+packet[15:24]))
        struct.pack_into('<I',packet,28,binascii.crc32(packet[:28]+b'\0'*4))

    def write_bytes(self,address,data):
        offset=address-0x20000000
        assert 0<=offset and offset+len(data)<=len(self.mem)
        self.mem[offset:offset+len(data)]=data

    def store(self,address,value,size=4):self.write_bytes(address,int(value&((1<<(8*size))-1)).to_bytes(size,'little'))
    def state(self,name,value=None):
        address=0x20004000+self.o[name]
        if value is None:return self.read(address,4)
        self.store(address,value)

    def read(self,address,size):
        if 0x20000000<=address<0x20010000:
            offset=address-0x20000000
            assert offset+size<=len(self.mem)
            return int.from_bytes(self.mem[offset:offset+size],'little')
        if address in (self.r['timer_hi'],self.r['timer_lo']):
            self.tick+=13
            value=(self.tick>>32) if address==self.r['timer_hi'] else self.tick&U32
            self.timer_reads.append((address,self.tick,value))
            self.timestamp_events.append(('hi' if address==self.r['timer_hi'] else 'lo',self.tick))
            return value
        if address==self.r['padout']:
            self.timestamp_events.append(('pad',self.tick))
            return (1<<26) if self.pad_high else 0
        if address==self.r['fstat']:return ((not self.fifo)<<10)|((not self.rtt)<<9)
        if address==self.r['latch_fifo']:
            assert self.fifo,'DMA tried to wait on a missing latch'
            return self.fifo.pop(0)
        if address==self.r['rtt_fifo']:
            assert self.rtt
            return self.rtt.pop(0)
        if address==self.r['help_rx']:
            assert self.help_result
            return self.help_result.pop(0)
        if address==self.r['ctrl_rx']:return 0
        if address==self.r['sniff_data']:
            if self.sniff_control==self.r['sniff_sum']:return self.sniff_seed
            if self.sniff_control==self.r['sniff_crc']:
                return binascii.crc32(self.sniff_bytes,self.sniff_seed^U32)
            if self.sniff_control==self.r['sniff_crc16']:
                return binascii.crc_hqx(self.sniff_bytes,self.sniff_seed)
            raise AssertionError('unconfigured sniff read')
        return self.mmio.get(address,0)

    def write(self,address,value,size):
        if 0x20000000<=address<0x20010000:
            first=self.g['entries']['first_record']
            commit=self.g['entries']['first_commit']
            is_first=first<=address<=commit
            if is_first and self.stop_after_first_words==0:
                raise FirstRecordStopped
            self.store(address,value,size)
            if is_first:
                assert size==4
                self.first_writes+=1
                if address==commit:
                    assert self.first_writes==23 and value==2
                    self.first_commits+=1
                if self.first_writes==self.stop_after_first_words:
                    raise FirstRecordStopped
            if address==0x20004000+self.o['record_published_version']:
                index=(value//2-1)%8
                start=0xb000+index*self.g['record_size']
                self.completed.append(bytes(self.mem[start:start+self.g['record_size']]))
            return
        if address==self.r['sniff_ctrl']:self.sniff_control=value
        elif address==self.r['sniff_data']:
            self.sniff_seed=value
            self.sniff_bytes.clear()
        elif address==self.r['help_tx']:
            self.help.append(value)
            if self.help[0]==9 and len(self.help)==5:
                _,actual,expected,equal,different=self.help
                self.help_result.append(equal if actual==expected else different)
                self.help.clear()
            elif self.help[0]==2 and len(self.help)==6:
                _,instruction,actual,expected,equal,different=self.help
                shift=0 if instruction==0xa042 else instruction&31
                self.help_result.append(equal if (actual>>shift)&1==expected else different)
                self.help.clear()
        elif address==self.r['latch_shift']:self.fifo.clear()
        elif address==self.r['latch_instr']:
            if value==0xa02b:self.latch_x=U32
            else:assert value==27
        elif address==self.r['tx']+0x2000:
            if value&4:
                assert self.latch_x==U32 and not self.fifo
                self.latch_enabled=True
                self.timestamp_events.append(('enable',self.tick))
        elif address==self.r['tx']+0x3000:
            if value&4:self.latch_enabled=False
        elif address==self.r['rx']+0x2000:
            if value&4:self.timestamp_events.append(('rx_enable',self.tick))
        elif address==self.r['ctrl_tx']:
            assert self.latch_enabled
            self.timestamp_events.append(('launch',self.tick))
            self.launches+=1
            self.fifo=[] if self.missing_edge else [U32-50-self.launches,U32-51-self.launches]
            self.rtt=[U32-30]
            n=self.g['nodes']
            count=n*32+36
            packet=bytearray(self.mem[0x1000+9:0x1000+9+count*2:2])
            assert struct.unpack_from('<I',packet,24)[0]==binascii.crc32(packet[:14]+packet[15:24])
            assert struct.unpack_from('<I',packet,28)[0]==binascii.crc32(packet[:28]+b'\0'*4)
            packet[14]=self.g['mask'].bit_count()-1
            self.fix_crc(packet)
            if self.bad_mailbox:packet[38]^=1
            self.write_bytes(self.cap_target,packet)
            self.mmio[self.r['dma']+4*0x40+8]=1 if self.missing_return else 0
            self.mmio[self.r['dma']+5*0x40+8]=0
        elif address==self.r['abort']:
            for channel,ctrl in [(4,'cap_ctrl'),(5,'out_ctrl')]:self.mmio[self.r['dma']+channel*0x40+0xc]=self.r[ctrl]&~1
            self.mmio[address]=0
        elif address==self.r['dma']+6*0x40+0x3c:self.next_pc=value
        else:
            self.mmio[address]=value
            if address==self.r['dma']+4*0x40+0x34:self.cap_target=value

    def run(self,count=12):
        for _ in range(25000):
            if len(self.completed)==count:return self
            index=(self.pc-0x20008000)//16
            assert self.pc%16==0 and 0<=index<len(self.g['runs'])
            ctrl,dst,n,src=self.g['runs'][index]
            size=1<<((ctrl>>2)&3)
            self.next_pc=self.pc+16
            for i in range(n):
                a=src+i*size if ctrl&(1<<4) else src
                stride=size*(2 if ctrl&(1<<7) and not ctrl&(1<<6) else 1)
                d=dst+i*stride if ctrl&((1<<6)|(1<<7)) else dst
                value=self.read(a,size)
                data=value.to_bytes(size,'little')
                if ctrl&(1<<24):data=data[::-1]
                value=int.from_bytes(data,'little')
                if ctrl&(1<<25):
                    assert self.sniff_control&1
                    assert (self.sniff_control>>1)&15==8
                    if self.sniff_control==self.r['sniff_sum']:self.sniff_seed=(self.sniff_seed+value)&U32
                    else:self.sniff_bytes.extend(data)
                self.write(d,value,size)
            self.pc=self.next_pc
        raise AssertionError('graph failed to retire bounded cycles')

    def first_record(self):
        start=self.g['entries']['first_record']-0x20000000
        return bytes(self.mem[start:start+self.g['record_size']])


def graph(exe,nodes,*config):
    result=subprocess.run([str(exe),str(nodes),*map(str,config)],capture_output=True,text=True,timeout=5)
    assert result.returncode==0,result.stderr
    return json.loads(result.stdout)


def test_raw_configuration_resources(graph_exe):
    exe,capacity=graph_exe
    result=subprocess.run([str(exe),'matrix'],capture_output=True,text=True,timeout=90)
    assert result.returncode==0,result.stderr
    summary=json.loads(result.stdout)
    expected=sum(sum(mask.bit_count() for mask in range(1,1<<n) if mask.bit_count()>=2)
                 for n in range(2,capacity+1))*7*3*2
    assert summary['cases']==expected
    assert summary['max_runs']<=summary['run_capacity']
    assert summary['max_literals']<=summary['literal_capacity']
    assert summary['max_step_runs']<=24
    (exe.parent/'matrix-summary.json').write_text(json.dumps(summary,indent=2),encoding='utf-8')


def test_raw_sparse_and_nonzero_local_transport(graph_exe):
    exe,capacity=graph_exe
    masks={3,(1<<capacity)-1,1|(1<<(capacity-1)),sum(1<<i for i in range(0,capacity,2))}
    for mask in sorted(m for m in masks if m.bit_count()>=2):
        for local in range(capacity):
            if not mask&(1<<local):continue
            for guard in (1,257,65536):
                g=graph(exe,capacity,mask,local,guard)
                m=Model(g).run(3)
                assert m.state('fault')==0
                for raw in m.completed:
                    words=struct.unpack('<22I',raw)
                    assert words[0]==words[-1] and words[8:11]==(7,1,2)
                expected=bytes(m.mem[0x5000:0x5020])
                start=0x1000+9+2*(32+local*32)
                assert bytes(m.mem[start:start+64:2])==expected


def test_raw_records_follow_real_graph(graph_exe):
    exe,capacity=graph_exe
    for nodes in range(2,capacity+1):
        g=graph(exe,nodes)
        assert len(g['runs'])<=g['run_capacity']
        assert len(g['literals'])<=g['literal_capacity']
        m=Model(g).run()
        seq=[]
        for i,raw in enumerate(m.completed):
            words=struct.unpack('<22I',raw)
            seq.append(words[0])
            assert words[0]==words[-1]
            assert words[8:11]==(7,1,2)  # epoch, checked transport, raw format
            assert words[17]==U32-51-i  # first FIFO word, not a stale/duplicate word
            assert not words[18]&(1<<10)
            assert words[19:21]==(1<<26,250000000)
            reads=m.timer_reads[i*6:(i+1)*6]
            # The two high/low/high samples overlap in execution order.
            assert words[11:14]==tuple(reads[j][2] for j in (1,2,4))
            assert words[14:17]==tuple(reads[j][2] for j in (0,3,5))
            events=m.timestamp_events[i*10:(i+1)*10]
            assert [e[0] for e in events]==['pad','hi','hi','lo','enable','lo','hi','hi','rx_enable','launch']
            assert reads[2][1]<=events[4][1]<=reads[3][1]
        assert seq==[(U32-1+i)&U32 for i in range(12)]
        assert m.first_record()==m.completed[0]
        assert m.first_commits==1 and m.first_writes==23
        assert m.state('fault')==0


def test_first_record_is_one_shot_through_sequence_and_version_wrap(graph_exe):
    exe,capacity=graph_exe
    m=Model(graph(exe,capacity),seed_sequence=U32).run(7)
    first=m.first_record()
    assert struct.unpack_from('<I',first)[0]==0
    # The next writer is ring7, matching publication0 across wrap. Keep that
    # mapping while crossing0 and2; neither may reopen the startup entry.
    m.state('observation_version',U32-1)
    m.run(80)
    assert m.first_record()==first
    assert m.first_commits==1 and m.first_writes==23
    assert [struct.unpack('<22I',raw)[0] for raw in m.completed]==list(range(80))
    assert all(struct.unpack('<22I',raw)[0]==struct.unpack('<22I',raw)[-1] for raw in m.completed)
    assert m.state('fault')==0


@pytest.fixture(scope='module')
def raw_reference_reader(tmp_path_factory):
    from test_vdc_command_owner import compile_executable
    source = r'''
#include <assert.h>
#include <stdio.h>
#include "tdma_origin_plan.h"
int main(int argc, char **argv)
{
    assert(argc == 2);
    FILE *input = fopen(argv[1], "rb");
    assert(input);
    tdma_origin_live_snapshot_t live = {.retained=1, .active=1,
        .sample={.epoch=7, .published_version=2}};
    assert(fread(&live.sample.record, sizeof(live.sample.record), 1, input) == 1);
    assert(fgetc(input) == EOF);
    fclose(input);
    tdma_origin_raw_reference_t reference;
    printf("%u\n", tdma_origin_raw_reference(&live, 2, 26, &reference));
    return 0;
}
'''
    return compile_executable(tmp_path_factory.mktemp('raw-reference-reader'),
        'raw_reference_reader', source,
        [ROOT/'components/tdma/src/tdma_origin_reference.c'])


@pytest.mark.parametrize('rollover_read', range(8))
def test_overlapping_high_guards_reject_rollover_and_recover(
        graph_exe, raw_reference_reader, tmp_path, rollover_read):
    exe, capacity = graph_exe
    m = Model(graph(exe, capacity))
    # Rollover at every timer read, plus before/after the entire enclosure.
    m.tick = (7 << 32) - 13 * rollover_read
    m.run(2)
    for index, raw in enumerate(m.completed):
        record = tmp_path/f'record-{index}.bin'
        record.write_bytes(raw)
        result = subprocess.run([str(raw_reference_reader), str(record)],
            capture_output=True, text=True, timeout=5)
        assert result.returncode == 0, result.stderr
        expected = index != 0 or rollover_read not in range(2, 7)
        assert result.stdout.strip() == str(int(expected))
    assert m.state('fault') == 0


def test_low_pad_record_rejected_with_healthy_transport(graph_exe, raw_reference_reader, tmp_path):
    exe, capacity = graph_exe
    m = Model(graph(exe, capacity))
    m.pad_high = False
    m.run(1)
    raw = m.completed[0]
    assert struct.unpack('<22I', raw)[9] == 1
    record = tmp_path/'low-pad.bin'
    record.write_bytes(raw)
    result = subprocess.run([str(raw_reference_reader), str(record)],
        capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stderr
    assert result.stdout.strip() == '0'


@pytest.mark.parametrize('mode',('missing_edge','bad_mailbox','missing_return','torn'))
def test_first_bad_observation_cannot_be_replaced_by_recovery(graph_exe,mode):
    exe,capacity=graph_exe
    m=Model(graph(exe,capacity),**{mode:True}).run(1)
    first=m.first_record()
    assert first==m.completed[0]
    m.missing_edge=m.bad_mailbox=m.missing_return=False
    m.run(12)
    assert m.first_record()==first and m.first_commits==1
    assert all(struct.unpack('<22I',raw)[9]==1 for raw in m.completed[1:])


def test_stop_at_each_first_record_word_keeps_partial_unpublished(graph_exe):
    exe,capacity=graph_exe
    g=graph(exe,capacity)
    complete=Model(g).run(1).first_record()
    for cut in range(24):
        m=Model(g,stop_after_first_words=cut)
        with pytest.raises(FirstRecordStopped):m.run(1)
        assert m.first_writes==cut and not m.completed
        commit=m.read(g['entries']['first_commit'],4)
        if cut<23:
            assert commit==0 and m.first_commits==0
        else:
            assert commit==2 and m.first_record()==complete and m.first_commits==1


@pytest.mark.parametrize('mode',('missing_edge','bad_mailbox','missing_return','torn'))
def test_raw_failures_preserve_transport_scope(graph_exe,mode):
    exe,capacity=graph_exe
    m=Model(graph(exe,capacity),**{mode:True}).run()
    first=struct.unpack('<22I',m.completed[0])
    if mode=='missing_edge':
        assert first[17]==0 and first[18]&(1<<10)
        assert first[9]==1  # A local timing miss does not reject healthy transport.
    if mode in ('bad_mailbox','missing_return'):
        assert all(struct.unpack('<22I',raw)[9]==0 for raw in m.completed)
        assert first[17]!=0  # Capture evidence still exists for a rejected return.
    if mode=='torn':assert first[11]!=first[13]
    assert m.state('fault')==0
