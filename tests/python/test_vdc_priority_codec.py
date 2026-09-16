"""Host tests for the isolated typed synchronization body codec."""
import os
import shutil
import subprocess
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]


HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "vdc_priority_codec.h"

static void field_crc_damage(const uint8_t *original, size_t offset, size_t size,
                             uint16_t crc) {
    for (size_t i=0; i<size; ++i) {
        uint8_t body[VDC_PRIORITY_CODEC_BODY_SIZE];
        memcpy(body, original, sizeof(body));
        body[offset+i] ^= (uint8_t)(1u << (i & 7u));
        assert(!vdc_priority_codec_crc_valid(body, crc));
    }
}

int main(void) {
    _Static_assert(VDC_PRIORITY_CODEC_BODY_SIZE == 22u, "exact typed body");
    _Static_assert(TDMA_PROCESS_IMAGE_PRIORITY_SYNC_FLAGS_OFFSET == 28u, "flags offset");
    vdc_priority_codec_record_t input = {
        .binding_generation=1u, .event_sequence=UINT32_MAX,
        .event_time_lower=UINT64_MAX-1u, .uncertainty_width=1u, .flags=0u};
    uint8_t body[VDC_PRIORITY_CODEC_BODY_SIZE];
    assert(vdc_priority_codec_layout_admit(0x14u, 22u));
    assert(!vdc_priority_codec_layout_admit(0x13u, 22u));
    assert(!vdc_priority_codec_layout_admit(0x14u, 21u));
    assert(!tdma_process_image_transport_class_valid(0x14u));
    assert(tdma_process_image_typed_sync_class_valid(0x14u));
    assert(vdc_priority_codec_encode(&input, body));
    assert(body[0]==1u && body[1]==0u && body[4]==0xffu && body[5]==0xffu);
    assert(body[8]==0xfeu && body[15]==0xffu && body[16]==1u);
    vdc_priority_codec_record_t output={0};
    assert(vdc_priority_codec_decode(body,&output));
    assert(input.binding_generation==output.binding_generation &&
           input.event_sequence==output.event_sequence &&
           input.event_time_lower==output.event_time_lower &&
           input.uncertainty_width==output.uncertainty_width && input.flags==output.flags);
    const uint16_t crc=vdc_priority_codec_crc16(body,sizeof(body));
    assert(vdc_priority_codec_crc16((const uint8_t *)"123456789",9u)==0x29b1u);
    assert(vdc_priority_codec_crc_valid(body,crc));
    field_crc_damage(body,0u,4u,crc);
    field_crc_damage(body,4u,4u,crc);
    field_crc_damage(body,8u,8u,crc);
    field_crc_damage(body,16u,4u,crc);
    field_crc_damage(body,20u,2u,crc);

    vdc_priority_codec_record_t bounded={.binding_generation=UINT32_MAX,
        .event_sequence=0u,.event_time_lower=0u,.uncertainty_width=UINT32_MAX,.flags=0u};
    assert(vdc_priority_codec_encode(&bounded,body));
    assert(vdc_priority_codec_decode(body,&output));
    assert(output.binding_generation==UINT32_MAX && output.event_sequence==0u &&
           output.uncertainty_width==UINT32_MAX);
    bounded.uncertainty_width=0u;
    assert(!vdc_priority_codec_encode(&bounded,body));
    bounded.uncertainty_width=1u; bounded.binding_generation=0u;
    assert(!vdc_priority_codec_encode(&bounded,body));
    memset(body,0,sizeof(body)); body[16]=1u; body[20]=1u;
    assert(!vdc_priority_codec_decode(body,&output));
    bounded.binding_generation=1u; bounded.uncertainty_width=1u; bounded.flags=1u;
    assert(!vdc_priority_codec_encode(&bounded,body));
    memset(body,0,sizeof(body)); body[0]=1u; body[16]=1u; body[20]=1u;
    assert(!vdc_priority_codec_decode(body,&output));
    bounded.flags=0u; bounded.event_time_lower=UINT64_MAX;
    assert(!vdc_priority_codec_encode(&bounded,body));
    assert(vdc_priority_codec_encode(&input,body));
    body[8]=0xffu;
    const vdc_priority_codec_record_t retained=output;
    assert(!vdc_priority_codec_decode(body,&output));
    assert(output.event_time_lower==retained.event_time_lower &&
           output.event_sequence==retained.event_sequence);
    assert(!vdc_priority_codec_encode(NULL,body));
    assert(!vdc_priority_codec_encode(&bounded,NULL));
    assert(vdc_priority_codec_crc16(NULL,22u)==0u);
    puts("vdc priority codec: 10 case groups passed");
    return 0;
}
'''


def test_vdc_priority_codec(tmp_path: Path) -> None:
    cc = os.environ.get("HOST_CC") or shutil.which("gcc") or "D:/Microsoft/mingw64/bin/gcc.exe"
    harness = tmp_path / "priority_codec.c"
    harness.write_text(HARNESS, encoding="utf-8")
    executable = tmp_path / "priority_codec.exe"
    command = [cc, "-std=c11", "-O2", "-Wall", "-Wextra", "-Werror",
               "-I" + str(ROOT / "components/tdma/inc"),
               "-I" + str(ROOT / "components/vdc_dpll_manager/inc"),
               str(ROOT / "components/vdc_dpll_manager/src/vdc_priority_codec.c"),
               str(harness), "-o", str(executable)]
    built = subprocess.run(command, capture_output=True, text=True)
    assert built.returncode == 0, built.stdout + built.stderr
    ran = subprocess.run([str(executable)], capture_output=True, text=True)
    assert ran.returncode == 0, ran.stdout + ran.stderr
    assert "vdc priority codec: 10 case groups passed" in ran.stdout
