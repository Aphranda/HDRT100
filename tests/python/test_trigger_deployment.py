"""Real C deployment validation/routing with independent canonical CRC bytes."""
from dataclasses import dataclass, replace
import itertools
from pathlib import Path
import random
import struct
import subprocess
import zlib

import pytest

from test_vdc_command_owner import compile_executable

ROOT = Path(__file__).resolve().parents[2]
OK, ARGUMENT, IDENTITY, IO, ROLE, NOT_LOCAL, GENERATION = range(7)


@dataclass(frozen=True)
class Row:
    uid: int
    slot: int
    inputs: int
    outputs: int
    feedback: int = 0


BASE = (Row(0xFEDCBA9876543210, 0, 1, 0), Row(102, 1, 0, 7),
        Row(103, 2, 1, 8, 1), Row(104, 3, 0, 1))


def validation(rows, generation, capacity):
    if capacity < 4:
        return ARGUMENT
    if not generation:
        return GENERATION
    for index, row in enumerate(rows):
        if (not row.uid or row.slot >= capacity or
                any(row.uid == prior.uid or row.slot == prior.slot for prior in rows[:index])):
            return IDENTITY
        if (row.inputs | row.outputs) & ~15:
            return IO
        if index in (1, 3):
            io_valid = row.inputs == 0 and 1 <= row.outputs <= 15
        else:
            io_valid = (row.inputs.bit_count(), row.outputs.bit_count()) == ((1, 0) if index == 0 else (1, 1))
        if not io_valid:
            return IO
        if row.feedback != (1 if index == 2 else 0):
            return ROLE
    return OK


def crc(rows, generation):
    data = struct.pack('<I', generation) + b''.join(
        struct.pack('<QIIII', row.uid, row.slot, row.inputs, row.outputs, row.feedback)
        for row in rows)
    assert len(data) == 100
    return zlib.crc32(data)


HARNESS = r'''
#include <assert.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include "trigger_deployment.h"
int main(void) {
    trigger_deployment_t table;
    trigger_deployment_row_t out;memset(&out,0xa5,sizeof(out));
    const trigger_deployment_row_t saved=out;
    assert(trigger_deployment_validate(NULL,4)==TRIGGER_DEPLOYMENT_ARGUMENT);
    assert(trigger_deployment_resolve(NULL,0,1,0,4,&out)==TRIGGER_DEPLOYMENT_ARGUMENT);
    assert(trigger_deployment_resolve(NULL,0,1,0,4,NULL)==TRIGGER_DEPLOYMENT_ARGUMENT);
    assert(trigger_deployment_crc(NULL)==0);
    assert(trigger_deployment_role_kind(0)==TRIGGER_DEPLOYMENT_KIND_COUNTER);
    assert(trigger_deployment_role_kind(1)==TRIGGER_DEPLOYMENT_KIND_SWITCH);
    assert(trigger_deployment_role_kind(3)==trigger_deployment_role_kind(1));
    assert(trigger_deployment_role_kind(2)==TRIGGER_DEPLOYMENT_KIND_GATEWAY);
    assert(trigger_deployment_role_kind(4)==UINT32_MAX);
    assert(trigger_deployment_role_kind(UINT32_MAX)==UINT32_MAX);
    assert(memcmp(&out,&saved,sizeof(out))==0);
    unsigned padding=0;
    for(;;) {
        memset(&table,(padding++ & 1u) ? 0xa5:0x5a,sizeof(table));
        if(scanf("%"SCNu32,&table.generation)!=1)break;
        for(unsigned i=0;i<4;++i) {
            trigger_deployment_row_t *r=&table.rows[i];
            assert(scanf("%"SCNu64" %"SCNu32" %"SCNu32" %"SCNu32" %"SCNu32,
                &r->board_uid,&r->tdma_slot,&r->input_mask,&r->output_mask,&r->feedback_mode)==5);
        }
        uint32_t capacity,role,slot;uint64_t uid;
        assert(scanf("%"SCNu32" %"SCNu32" %"SCNu64" %"SCNu32,&capacity,&role,&uid,&slot)==4);
        const trigger_deployment_t before=table;
        out=saved;
        const uint32_t valid=trigger_deployment_validate(&table,capacity);
        const uint32_t resolve=trigger_deployment_resolve(&table,role,uid,slot,capacity,&out);
        if(resolve)assert(memcmp(&out,&saved,sizeof(out))==0);
        else {
            const trigger_deployment_row_t *r=&table.rows[role];
            assert(out.board_uid==r->board_uid && out.tdma_slot==r->tdma_slot &&
                out.input_mask==r->input_mask && out.output_mask==r->output_mask &&
                out.feedback_mode==r->feedback_mode);
        }
        const uint32_t fingerprint=trigger_deployment_crc(&table);
        assert(memcmp(&table,&before,sizeof(table))==0);
        printf("%"PRIu32" %"PRIu32" %"PRIu32"\n",valid,resolve,fingerprint);
    }
    return 0;
}
'''


@pytest.fixture(scope='module')
def executable(tmp_path_factory):
    header = ROOT / 'components/sync_trigger/inc/trigger_deployment.h'
    return compile_executable(tmp_path_factory.mktemp('trigger-deployment'), 'deployment',
        HARNESS.replace('"trigger_deployment.h"', f'"{header.as_posix()}"'))


def case(rows=BASE, generation=1, capacity=4, role=0, uid=None, slot=None):
    local = rows[role] if role < 4 else rows[0]
    return rows, generation, capacity, role, local.uid if uid is None else uid, local.slot if slot is None else slot


def check(executable, cases):
    lines=[]
    for rows, gen, cap, role, uid, slot in cases:
        values=[gen]
        for row in rows:
            values.extend(vars(row).values())
        values.extend([cap, role, uid, slot])
        lines.append(' '.join(map(str, values)))
    run=subprocess.run([str(executable)], input='\n'.join(lines)+'\n',
        capture_output=True, text=True, timeout=20)
    assert run.returncode == 0, run.stderr
    results=[tuple(map(int,line.split())) for line in run.stdout.splitlines()]
    assert len(results)==len(cases)
    for c,result in zip(cases,results):
        rows,gen,cap,role,uid,slot=c
        valid=validation(rows,gen,cap)
        resolved=valid
        if valid==OK:
            if role>=4:
                resolved=ROLE
            elif not uid or slot>=cap:
                resolved=IDENTITY
            elif uid!=rows[role].uid or slot!=rows[role].slot:
                resolved=NOT_LOCAL
        assert result==(valid,resolved,crc(rows,gen)), (c,result)
    return results


def changed(index, **fields):
    rows=list(BASE)
    rows[index]=replace(rows[index],**fields)
    return tuple(rows)


def test_validation_and_local_routing(executable):
    cases=[case(role=r) for r in range(4)]
    cases += [case(generation=0),case(capacity=0),case(capacity=3),case(role=4),
              case(role=2**32-1),case(uid=0),case(uid=999),case(slot=4),case(slot=1)]
    for r in range(4):
        cases += [case(changed(r,uid=0)),case(changed(r,slot=4)),
                  case(changed(r,inputs=16)),case(changed(r,outputs=16)),
                  case(changed(r,feedback=7))]
    cases += [case(changed(1,uid=BASE[0].uid)),case(changed(3,slot=2)),
              case(changed(0,inputs=3)),case(changed(0,outputs=1)),
              case(changed(1,outputs=0)),
              case(changed(1,inputs=1)),case(changed(2,inputs=0)),
              case(changed(2,outputs=3)),case(changed(2,feedback=0)),
              case(changed(3,outputs=0)),case(changed(3,inputs=1)),
              case(changed(0,feedback=1))]
    results=check(executable,cases)
    assert {code for row in results for code in row[:2]} == set(range(7))


def test_four_board_role_permutations_and_generation(executable):
    cases=[]
    for permutation in itertools.permutations(range(4)):
        rows=tuple(replace(BASE[role],uid=BASE[board].uid,slot=BASE[board].slot)
                   for role,board in enumerate(permutation))
        for role in range(4):
            cases.append(case(rows,generation=77,role=role))
            if permutation[role]!=role:
                cases.append(case(rows,generation=77,role=role,
                                  uid=BASE[role].uid,slot=BASE[role].slot))
    results=check(executable,cases)
    assert any(row[1]==NOT_LOCAL for row in results)
    assert all(row[0]==OK for row in results)


def test_configurable_masks_separate_input_output_namespaces(executable):
    cases=[]
    for inp in [1,2,4,8]:
        for out in [1,2,4,8]:
            rows=list(BASE)
            rows[0]=replace(rows[0],inputs=inp)
            rows[2]=replace(rows[2],inputs=inp,outputs=out)
            rows[3]=replace(rows[3],outputs=out)
            for mask in range(1,16):
                rows[1]=replace(rows[1],outputs=mask)
                rows[3]=replace(rows[3],outputs=mask)
                cases.append(case(tuple(rows),role=2))
    # Switch slots retain distinct identities while exchanging device widths.
    swapped=list(BASE)
    swapped[1]=replace(swapped[1],outputs=BASE[3].outputs)
    swapped[3]=replace(swapped[3],outputs=BASE[1].outputs)
    cases.extend([case(tuple(swapped),role=1),case(tuple(swapped),role=3)])
    assert all(row[:2]==(OK,OK) for row in check(executable,cases))


def test_canonical_crc_not_padding_and_all_fields(executable):
    # Adjacent identical input rows reach C structs with deliberately different padding.
    cases=[case(),case(),case(generation=2**32-1)]
    rng=random.Random(773)
    for _ in range(200):
        rows=tuple(Row(rng.randrange(1,2**64),rng.randrange(2**32),
                       rng.randrange(2**32),rng.randrange(2**32),rng.randrange(2**32))
                   for _ in range(4))
        cases.append(case(rows,generation=rng.randrange(2**32),capacity=32))
    results=check(executable,cases)
    assert results[0][2]==results[1][2]
    assert results[0][2]!=results[2][2]


def test_capacity_parameter_is_explicit(executable):
    rows=tuple(replace(row,slot=row.slot+8) for row in BASE)
    results=check(executable,[case(rows,capacity=4),case(rows,capacity=16,role=3)])
    assert results[0][0]==IDENTITY and results[1][:2]==(OK,OK)
