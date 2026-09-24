"""Compile the actual installation-only kernel; independent canonical CRC oracle."""
from pathlib import Path
import random
import struct
import subprocess
import zlib
import pytest

ROOT=Path(__file__).resolve().parents[2]


@pytest.fixture(scope='module')
def kernel(tmp_path_factory):
    from test_trigger_grid_preview import compile_grid
    return compile_grid(tmp_path_factory.mktemp('action-table'), 'kernel', HARNESS)


CASES = [
    'valid', 'null', 'zero_gen', 'zero_count', 'count9', 'zero_hz', 'large_hz',
    'dep_gen', 'dep_crc', 'dep_shape', 'dep_duplicate', 'grid_epoch', 'grid_generation',
    'grid_subdivisions', 'grid_resolution', 'id_zero', 'id_duplicate', 'span_zero',
    'same_q', 'reverse_q', 'overlap', 'adjacent', 'reserved_start', 'reserved_end',
    'cross_reserved', 'q_overflow', 'target_overflow', 'end_overflow', 'unknown_role',
    'counter_role', 'switch_gateway_role', 'gateway_switch_role', 'unknown_kind',
    'switch_bad_code', 'switch_zero_code', 'pulse_zero', 'pulse_fractional_tick',
    'pulse_short', 'pulse_too_long', 'pulse_exact_span', 'pulse_nonuniform_span',
    'capacity', 'target_preserved', 'equal_padding', 'equal_fields', 'far_slot',
    'five_hundred_one_us', 'different_role_overlap',
]


@pytest.mark.parametrize('case',CASES)
def test_actual_kernel_validation(kernel,case):
    result=subprocess.run([str(kernel),case],capture_output=True,text=True,timeout=10)
    assert result.returncode==0,result.stdout+result.stderr


def deployment_crc():
    words=[17]
    for role,(input_mask,output_mask,feedback) in enumerate(((1,0,0),(0,7,0),(1,8,1),(0,8,0))):
        uid=0x1234567800000100+role
        words += [uid&0xffffffff,uid>>32,role,input_mask,output_mask,feedback]
    return zlib.crc32(struct.pack('<'+'I'*len(words),*words))


def serialize(table):
    generation,dep_generation,dep_crc,epoch,gridgen,anchor,period,subdivisions,rows=table
    blob=struct.pack('<IIIIIQIII',generation,dep_generation,dep_crc,epoch,gridgen,anchor,period,subdivisions,len(rows))
    for action_id,role,kind,arg,q,span in rows:
        blob += struct.pack('<IIIIQI',action_id,role,kind,arg,q,span)
    return blob


def stdin_table(table):
    generation,dep_generation,dep_crc,epoch,gridgen,anchor,period,subdivisions,rows=table
    words=[generation,dep_generation,dep_crc,epoch,gridgen,anchor,period,subdivisions,len(rows)]
    return ' '.join(map(str,words))+'\n'+''.join(' '.join(map(str,row))+'\n' for row in rows)


def test_canonical_crc_independent_little_endian_oracle(kernel):
    rng=random.Random(2350)
    tables=[(31,17,deployment_crc(),8,9,1000000000,1000000,2000,
             [(101,1,1,5,0,2),(102,3,1,8,2,2),(103,2,2,1000,4,4)])]
    for _ in range(48):
        rows=[(rng.getrandbits(32),rng.getrandbits(32),rng.getrandbits(32),rng.getrandbits(32),
               rng.getrandbits(64),rng.getrandbits(32)) for _ in range(rng.randrange(9))]
        tables.append((rng.getrandbits(32),rng.getrandbits(32),rng.getrandbits(32),
                       rng.getrandbits(32),rng.getrandbits(32),rng.getrandbits(64),
                       rng.getrandbits(32),rng.getrandbits(32),rows))
    for table in tables:
        result=subprocess.run([str(kernel),'fingerprint'],input=stdin_table(table),capture_output=True,text=True,timeout=10)
        assert result.returncode==0,result.stderr
        actual,dep_actual=map(int,result.stdout.split())
        assert actual==zlib.crc32(serialize(table))
        assert dep_actual==deployment_crc()


HARNESS=r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "trigger_action_table.h"
static trigger_deployment_t deployment(void){
    trigger_deployment_t d={.generation=17};
    for(unsigned i=0;i<4;i++){
        d.rows[i].board_uid=UINT64_C(0x1234567800000100)+i;d.rows[i].tdma_slot=i;
    }
    d.rows[0].input_mask=1;d.rows[1].output_mask=7;
    d.rows[2].input_mask=1;d.rows[2].output_mask=8;d.rows[2].feedback_mode=1;
    d.rows[3].output_mask=8;return d;
}
static trigger_action_table_t table(const trigger_deployment_t*d){
    return (trigger_action_table_t){.generation=31,.dep_generation=d->generation,.dep_crc=trigger_deployment_crc(d),
        .grid={.epoch=8,.generation=9,.anchor_vdc_ns=1000000000,.period_ns=1000000,.subdivisions=2000},.count=3,
        .rows={{.action_id=101,.role=1,.kind=1,.argument=5,.ordinal=0,.span_slices=2},
               {.action_id=102,.role=3,.kind=1,.argument=8,.ordinal=2,.span_slices=2},
               {.action_id=103,.role=2,.kind=2,.argument=1000,.ordinal=4,.span_slices=4}}};
}
static void fingerprint(void){
    trigger_action_table_t t={0};
    assert(scanf("%"SCNu32" %"SCNu32" %"SCNu32" %"SCNu32" %"SCNu32" %"SCNu64" %"SCNu32" %"SCNu32" %"SCNu32,
        &t.generation,&t.dep_generation,&t.dep_crc,&t.grid.epoch,&t.grid.generation,&t.grid.anchor_vdc_ns,
        &t.grid.period_ns,&t.grid.subdivisions,&t.count)==9);
    assert(t.count<=8);
    for(uint32_t i=0;i<t.count;i++){
        trigger_action_row_t*r=&t.rows[i];
        assert(scanf("%"SCNu32" %"SCNu32" %"SCNu32" %"SCNu32" %"SCNu64" %"SCNu32,
            &r->action_id,&r->role,&r->kind,&r->argument,&r->ordinal,&r->span_slices)==6);
    }
    trigger_deployment_t d=deployment();printf("%"PRIu32" %"PRIu32"\n",trigger_action_table_crc(&t),trigger_deployment_crc(&d));
}
int main(int argc,char**argv){
    assert(argc==2);const char*c=argv[1];
    if(!strcmp(c,"fingerprint")){fingerprint();return 0;}
    trigger_deployment_t d=deployment();trigger_action_table_t t=table(&d);
    uint32_t hz=250000000;trigger_action_table_result_t expected=TRIGGER_ACTION_TABLE_OK;
    if(!strcmp(c,"null")){
        assert(trigger_action_table_validate(NULL,&d,hz)==TRIGGER_ACTION_TABLE_ARGUMENT);
        assert(trigger_action_table_validate(&t,NULL,hz)==TRIGGER_ACTION_TABLE_ARGUMENT);
        assert(!trigger_action_table_equal(NULL,&t)&&!trigger_action_table_equal(&t,NULL));
        assert(trigger_action_table_crc(NULL)==0);return 0;
    }
    if(!strcmp(c,"zero_gen")){t.generation=0;expected=TRIGGER_ACTION_TABLE_ARGUMENT;}
    else if(!strcmp(c,"zero_count")){t.count=0;expected=TRIGGER_ACTION_TABLE_ARGUMENT;}
    else if(!strcmp(c,"count9")){t.count=9;assert(!trigger_action_table_crc(&t)&&!trigger_action_table_equal(&t,&t));expected=TRIGGER_ACTION_TABLE_ARGUMENT;}
    else if(!strcmp(c,"zero_hz")){hz=0;expected=TRIGGER_ACTION_TABLE_ARGUMENT;}
    else if(!strcmp(c,"large_hz")){hz=500000001;expected=TRIGGER_ACTION_TABLE_ARGUMENT;}
    else if(!strcmp(c,"dep_gen")){t.dep_generation++;expected=TRIGGER_ACTION_TABLE_DEPLOYMENT;}
    else if(!strcmp(c,"dep_crc")){t.dep_crc^=1;expected=TRIGGER_ACTION_TABLE_CRC;}
    else if(!strcmp(c,"dep_shape")){d.rows[0].input_mask=3;t.dep_crc=trigger_deployment_crc(&d);expected=TRIGGER_ACTION_TABLE_DEPLOYMENT;}
    else if(!strcmp(c,"dep_duplicate")){d.rows[1].board_uid=d.rows[0].board_uid;t.dep_crc=trigger_deployment_crc(&d);expected=TRIGGER_ACTION_TABLE_DEPLOYMENT;}
    else if(!strcmp(c,"grid_epoch")){t.grid.epoch=0;expected=TRIGGER_ACTION_TABLE_GRID;}
    else if(!strcmp(c,"grid_generation")){t.grid.generation=0;expected=TRIGGER_ACTION_TABLE_GRID;}
    else if(!strcmp(c,"grid_subdivisions")){t.grid.subdivisions=1;expected=TRIGGER_ACTION_TABLE_GRID;}
    else if(!strcmp(c,"grid_resolution")){t.grid.period_ns=1000;expected=TRIGGER_ACTION_TABLE_GRID;}
    else if(!strcmp(c,"id_zero")){t.rows[0].action_id=0;expected=TRIGGER_ACTION_TABLE_ID;}
    else if(!strcmp(c,"id_duplicate")){t.rows[2].action_id=t.rows[0].action_id;expected=TRIGGER_ACTION_TABLE_ID;}
    else if(!strcmp(c,"span_zero")){t.rows[1].span_slices=0;expected=TRIGGER_ACTION_TABLE_ARGUMENT;}
    else if(!strcmp(c,"same_q")){t.rows[1].ordinal=0;expected=TRIGGER_ACTION_TABLE_ORDER;}
    else if(!strcmp(c,"reverse_q")){t.rows[0].ordinal=5;expected=TRIGGER_ACTION_TABLE_ORDER;}
    else if(!strcmp(c,"overlap")||!strcmp(c,"different_role_overlap")){t.rows[0].span_slices=3;expected=TRIGGER_ACTION_TABLE_CONFLICT;}
    else if(!strcmp(c,"adjacent")){t.rows[2].span_slices=2;}
    else if(!strcmp(c,"reserved_start")){t.count=1;t.rows[0].ordinal=1999;expected=TRIGGER_ACTION_TABLE_RESERVED;}
    else if(!strcmp(c,"reserved_end")){t.count=1;t.rows[0].ordinal=1997;t.rows[0].span_slices=2;}
    else if(!strcmp(c,"cross_reserved")){t.count=1;t.rows[0].ordinal=1998;t.rows[0].span_slices=4;}
    else if(!strcmp(c,"q_overflow")){t.count=1;t.rows[0].ordinal=UINT64_MAX-1;t.rows[0].span_slices=2;expected=TRIGGER_ACTION_TABLE_OVERFLOW;}
    else if(!strcmp(c,"target_overflow")){t.count=1;t.grid.anchor_vdc_ns=UINT64_MAX-10;t.rows[0].ordinal=2;expected=TRIGGER_ACTION_TABLE_OVERFLOW;}
    else if(!strcmp(c,"end_overflow")){t.count=1;t.grid.anchor_vdc_ns=UINT64_MAX-10;t.rows[0].ordinal=0;expected=TRIGGER_ACTION_TABLE_OVERFLOW;}
    else if(!strcmp(c,"unknown_role")){t.rows[0].role=4;expected=TRIGGER_ACTION_TABLE_ROLE;}
    else if(!strcmp(c,"counter_role")){t.rows[0].role=0;expected=TRIGGER_ACTION_TABLE_ROLE;}
    else if(!strcmp(c,"switch_gateway_role")){t.rows[0].role=2;expected=TRIGGER_ACTION_TABLE_ROLE;}
    else if(!strcmp(c,"gateway_switch_role")){t.rows[2].role=1;expected=TRIGGER_ACTION_TABLE_ROLE;}
    else if(!strcmp(c,"unknown_kind")){t.rows[0].kind=3;expected=TRIGGER_ACTION_TABLE_ROLE;}
    else if(!strcmp(c,"switch_bad_code")){t.rows[0].argument=8;expected=TRIGGER_ACTION_TABLE_CODE;}
    else if(!strcmp(c,"switch_zero_code")){t.rows[0].argument=0;}
    else if(!strcmp(c,"pulse_zero")){t.rows[2].argument=0;expected=TRIGGER_ACTION_TABLE_CODE;}
    else if(!strcmp(c,"pulse_fractional_tick")){t.rows[2].argument=1001;expected=TRIGGER_ACTION_TABLE_CODE;}
    else if(!strcmp(c,"pulse_short")){t.rows[2].argument=4;expected=TRIGGER_ACTION_TABLE_CODE;}
    else if(!strcmp(c,"pulse_too_long")){t.rows[2].argument=2004;expected=TRIGGER_ACTION_TABLE_CODE;}
    else if(!strcmp(c,"pulse_exact_span")){t.rows[2].argument=2000;}
    else if(!strcmp(c,"pulse_nonuniform_span")){
        t.count=1;t.grid.period_ns=10;t.grid.subdivisions=3;t.rows[0]=t.rows[2];
        t.rows[0].ordinal=1;t.rows[0].span_slices=3;t.rows[0].argument=8;
    }
    else if(!strcmp(c,"five_hundred_one_us")){
        t.count=1;t.rows[0]=t.rows[2];t.rows[0].ordinal=1998;t.rows[0].span_slices=1002;t.rows[0].argument=501000;
    }
    else if(!strcmp(c,"capacity")){
        for(uint32_t i=0;i<8;i++){t.rows[i]=t.rows[0];t.rows[i].action_id=100+i;t.rows[i].ordinal=i*2;}
        t.count=8;
    }
    else if(!strcmp(c,"far_slot")){
        d.rows[2].tdma_slot=127;t.dep_crc=trigger_deployment_crc(&d); /* owner enforces physical slot capacity */
    }
    else if(!strcmp(c,"target_preserved")){
        uint64_t v=123;assert(trigger_action_table_target(&t,3,&v)==TRIGGER_ACTION_TABLE_ARGUMENT&&v==123);
        t.rows[0].ordinal=1999;assert(trigger_action_table_target(&t,0,&v)==TRIGGER_ACTION_TABLE_RESERVED&&v==123);
        t.rows[0].ordinal=0;t.grid.anchor_vdc_ns=UINT64_MAX;t.rows[0].ordinal=1;
        assert(trigger_action_table_target(&t,0,&v)==TRIGGER_ACTION_TABLE_OVERFLOW&&v==123);
        t=table(&d);assert(trigger_action_table_target(&t,2,&v)==0&&v==1000002000);return 0;
    }
    else if(!strcmp(c,"equal_padding")){
        trigger_action_table_t other;memset(&other,0xa5,sizeof(other));
        other.generation=t.generation;other.dep_generation=t.dep_generation;other.dep_crc=t.dep_crc;
        other.grid=t.grid;other.count=t.count;
        for(uint32_t i=0;i<t.count;i++){
            other.rows[i].action_id=t.rows[i].action_id;other.rows[i].role=t.rows[i].role;
            other.rows[i].kind=t.rows[i].kind;other.rows[i].argument=t.rows[i].argument;
            other.rows[i].ordinal=t.rows[i].ordinal;other.rows[i].span_slices=t.rows[i].span_slices;
        }
        assert(trigger_action_table_equal(&t,&other)&&trigger_action_table_crc(&t)==trigger_action_table_crc(&other));return 0;
    }
    else if(!strcmp(c,"equal_fields")){
        trigger_action_table_t other=t;
#define DIFFERENT(field) other=t;other.field^=1;assert(!trigger_action_table_equal(&t,&other))
        DIFFERENT(generation);DIFFERENT(dep_generation);DIFFERENT(dep_crc);DIFFERENT(count);
        DIFFERENT(grid.epoch);DIFFERENT(grid.generation);DIFFERENT(grid.anchor_vdc_ns);
        DIFFERENT(grid.period_ns);DIFFERENT(grid.subdivisions);DIFFERENT(rows[1].action_id);
        DIFFERENT(rows[1].role);DIFFERENT(rows[1].kind);DIFFERENT(rows[1].argument);
        DIFFERENT(rows[1].ordinal);DIFFERENT(rows[1].span_slices);
#undef DIFFERENT
        return 0;
    }
    const trigger_action_table_t before=t;const trigger_deployment_t dep_before=d;
    trigger_action_table_result_t actual=trigger_action_table_validate(&t,&d,hz);
    if(actual!=expected)fprintf(stderr,"%s actual=%u expected=%u\n",c,actual,expected);
    assert(actual==expected);assert(!memcmp(&t,&before,sizeof(t))&&!memcmp(&d,&dep_before,sizeof(d)));
    return 0;
}
'''
