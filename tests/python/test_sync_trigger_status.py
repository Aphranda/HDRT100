"""Compare the UI's status fields to the owner vector under the existing lock."""
from pathlib import Path
import re
import shutil
import subprocess

from tools.state_machine_resource_check.state_machine_resource_check import c_definition_body

ROOT = Path(__file__).resolve().parents[2]


def test_status_matches_ui_fields_inside_owner_lock(tmp_path):
    source = (ROOT/'components/sync_trigger/src/sync_trigger.c').read_text(encoding='utf-8')
    ui = (ROOT/'components/ui_manager/src/status_ui.c').read_text(encoding='utf-8')
    fields = sorted(set(re.findall(r'snapshot->trigger\.([a-zA-Z0-9_]+)', ui)))
    assert fields
    functions = '\n'.join(signature+'{'+c_definition_body(source, name)+'}' for name, signature in [
        ('sync_trigger_get_summary', 'void sync_trigger_get_summary(sync_trigger_summary_t *summary)'),
        ('sync_trigger_get_status', 'void sync_trigger_get_status(sync_trigger_status_t *status)'),
    ])
    # Expected values come from the UI's consumed fields and the unchanged
    # complete-vector getter, independently of the new projection's assignments.
    assignments = '\n'.join(f's_ao.vector.{name} = (__typeof__(s_ao.vector.{name}))(seed + {index}u);'
                            for index, name in enumerate(fields))
    checks = '\n'.join(f'assert(destination->{name} == expected.{name});' for name in fields)
    fixture = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "sync_trigger.h"
static struct { trigger_vector_t vector; } s_ao;
static trigger_vector_t before, expected;
static sync_trigger_status_t status, *destination;
static unsigned depth, entries, exits, mutation;
static uint32_t seed;
static void populate(void) { ASSIGNMENTS }
static void osal_critical_enter(void)
{
    assert(depth++ == 0); ++entries;
    if (mutation) {
        populate();
        expected = s_ao.vector;
    }
}
static void osal_critical_exit(void)
{
    assert(depth == 1); ++exits;
    if (destination) { CHECKS }
    if (mutation) {
        /* An update after unlock cannot alter the completed consumer copy. */
        seed ^= UINT32_MAX;
        populate();
    }
    --depth;
}
FUNCTIONS
int main(void)
{
    sync_trigger_get_status(NULL);
    assert(!entries && !exits);
    /* Zeroed/uninitialized status must retain the old getter's behavior. */
    sync_trigger_get_summary(&expected);
    destination = &status;
    sync_trigger_get_status(&status);
    for (unsigned trial=0;trial<8;++trial) {
        destination = NULL;
        seed = UINT32_MAX - trial;
        populate();
        before = s_ao.vector;
        sync_trigger_get_summary(&expected);
        destination = &status;
        memset(&status, 0xa5, sizeof(status));
        sync_trigger_get_status(&status);
        assert(memcmp(&before, &s_ao.vector, sizeof(before)) == 0);
        /* The lock-entry hook changes all source fields before the read. */
        mutation = 1;
        seed = trial * 17u;
        sync_trigger_get_status(&status);
        CHECKS
        mutation = 0;
    }
    assert(entries == exits && depth == 0);
    printf("status bytes=%zu, legacy bytes=%zu; UI projection and protected copy passed\n",
           sizeof(status), sizeof(trigger_vector_t));
    return 0;
}
'''.replace('ASSIGNMENTS', assignments).replace('CHECKS', checks).replace('FUNCTIONS', functions)
    path = tmp_path/'status.c'
    path.write_text(fixture, encoding='utf-8')
    compiler = shutil.which('gcc') or shutil.which('clang')
    assert compiler
    exe = tmp_path/'status.exe'
    subprocess.run([compiler, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-I'+str(ROOT/'components/sync_trigger/inc'), str(path), '-o', str(exe)],
                   check=True, capture_output=True, text=True, timeout=60)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout+result.stderr
