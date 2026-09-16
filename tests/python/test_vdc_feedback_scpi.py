"""Execute production raw feedback SCPI formatting and argument failure paths."""
import subprocess

from test_vdc_command_ingress import ingress_definition
from test_vdc_command_owner import ROOT, compile_executable


def test_feedback_scpi_readback(tmp_path):
    source = (ROOT / "middleware/scpi_port/src/scpi_system_snapshot_commands.c").read_text(encoding="utf-8")
    header = (ROOT / "middleware/scpi_port/inc/scpi_system_snapshot_commands.h").read_text(encoding="utf-8")
    assert 'SYSTem:REFMEM:SYNC:TDMA:VDC:FEEDback:PROof?' in header
    assert '.callback = scpi_cmd_refmem_vdc_reference_proof_q' in header
    harness = r'''
#include <assert.h>
#include <string.h>
#include "distributed_refmem.h"
typedef int scpi_t;
typedef int scpi_result_t;
#define SCPI_RES_OK 1
#define SCPI_RES_ERR -1
#define TRUE 1
static bool parameter_ok=true, available=true;
static uint32_t parameter, calls, numbers[32], count, texts;
static char strings[2][129];
static distributed_refmem_vdc_feedback_rx_snapshot_t rx;
static distributed_refmem_vdc_feedback_tx_snapshot_t tx;
static bool SCPI_ParamUInt32(scpi_t *ctx, uint32_t *value, bool required)
{ (void)ctx; assert(required); if (!parameter_ok) return false; *value=parameter; return true; }
bool distributed_refmem_get_vdc_feedback_rx(uint32_t source, distributed_refmem_vdc_feedback_rx_snapshot_t *out)
{ ++calls; if (!available || source >= REFMEM_SYNC_NODE_COUNT) return false; *out=rx; return true; }
bool distributed_refmem_get_vdc_feedback_tx(distributed_refmem_vdc_feedback_tx_snapshot_t *out)
{ ++calls; if (!available) return false; *out=tx; return true; }
bool distributed_refmem_get_vdc_reference_proof(uint32_t source, distributed_refmem_vdc_feedback_rx_snapshot_t *out)
{ ++calls; if (!available || source >= REFMEM_SYNC_NODE_COUNT) return false; *out=rx; return true; }
static void SCPI_ResultUInt32(scpi_t *ctx, uint32_t value)
{ (void)ctx; assert(count<32); numbers[count++]=value; }
static void SCPI_ResultText(scpi_t *ctx, const char *value)
{ (void)ctx; assert(texts<2 && strlen(value)==128); strcpy(strings[texts++],value); }
''' + "\n".join(ingress_definition(source, name) for name in (
        "scpi_feedback_record", "scpi_cmd_refmem_vdc_feedback_rx_q", "scpi_cmd_refmem_vdc_feedback_tx_q",
        "scpi_cmd_refmem_vdc_reference_proof_q")) + r'''
int main(void)
{
    scpi_t ctx=0;
    parameter_ok=false;
    assert(scpi_cmd_refmem_vdc_feedback_rx_q(&ctx)==SCPI_RES_ERR && calls==0);
    assert(scpi_cmd_refmem_vdc_reference_proof_q(&ctx)==SCPI_RES_ERR && calls==0);
    parameter_ok=true; parameter=REFMEM_SYNC_NODE_COUNT;
    assert(scpi_cmd_refmem_vdc_feedback_rx_q(&ctx)==SCPI_RES_ERR);
    assert(scpi_cmd_refmem_vdc_reference_proof_q(&ctx)==SCPI_RES_ERR);
    parameter=UINT32_MAX;
    assert(scpi_cmd_refmem_vdc_feedback_rx_q(&ctx)==SCPI_RES_ERR);
    parameter=REFMEM_SYNC_NODE_COUNT-1; available=false;
    assert(scpi_cmd_refmem_vdc_feedback_rx_q(&ctx)==SCPI_RES_ERR);
    assert(scpi_cmd_refmem_vdc_feedback_tx_q(&ctx)==SCPI_RES_ERR);
    assert(scpi_cmd_refmem_vdc_reference_proof_q(&ctx)==SCPI_RES_ERR);
    assert(count==0 && texts==0);
    available=true;
    const uint32_t rxvalues[]={1,0,1,5,0xabcdef01,9,0xff112233,0,UINT32_MAX,12,13,14,15,16,17,18};
    memcpy(&rx,rxvalues,sizeof(rxvalues));
    const uint32_t txvalues[]={1,1,15,123,456,7,8,2,0x10203040,10,3,0,13,14,15};
    memcpy(&tx,txvalues,sizeof(txvalues));
    for(unsigned i=0;i<64;i++) { rx.record[i]=(uint8_t)(i*17); tx.history[0][i]=rx.record[i]; tx.history[1][i]=255-rx.record[i]; }
    assert(scpi_cmd_refmem_vdc_feedback_rx_q(&ctx)==SCPI_RES_OK);
    assert(count==16 && texts==1 && !memcmp(numbers,rxvalues,sizeof(rxvalues)));
    const char *hex="0123456789abcdef";
    for(unsigned i=0;i<64;i++) assert(strings[0][2*i]==hex[rx.record[i]>>4] && strings[0][2*i+1]==hex[rx.record[i]&15]);
    count=texts=0;
    assert(scpi_cmd_refmem_vdc_reference_proof_q(&ctx)==SCPI_RES_OK);
    assert(count==16 && texts==1 && !memcmp(numbers,rxvalues,sizeof(rxvalues)));
    for(unsigned i=0;i<64;i++) assert(strings[0][2*i]==hex[rx.record[i]>>4] && strings[0][2*i+1]==hex[rx.record[i]&15]);
    count=texts=0;
    assert(scpi_cmd_refmem_vdc_feedback_tx_q(&ctx)==SCPI_RES_OK);
    assert(count==15 && texts==2 && !memcmp(numbers,txvalues,sizeof(txvalues)));
    for(unsigned j=0;j<2;j++) for(unsigned i=0;i<64;i++)
        assert(strings[j][2*i]==hex[tx.history[j][i]>>4] && strings[j][2*i+1]==hex[tx.history[j][i]&15]);
    return 0;
}
'''
    exe = compile_executable(tmp_path, "feedback_scpi", harness)
    result = subprocess.run([str(exe)], capture_output=True, text=True, timeout=5)
    assert result.returncode == 0, result.stdout + result.stderr
