#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "scpi/scpi.h"
#include "scpi_sequence_node_commands.h"
#include "refmem_application_model.h"
#include "refmem_table_registry.h"
#include "refmem_vector_table.h"
#include "sync_trigger.h"
#include "pota_types.h"
#include "trigger_sequence_link.h"
#include "tdma_service.h"
#include "tdma_pio_spi_ring_adapter.h"
#include "board_config.h"

static bool frozen, legacy_busy, history_busy;
static bool configuration_gate;
static bool link_rejected;
static trigger_sequence_link_status_t link_status;
static unsigned errors;
static char response[1024];
static size_t response_length;
static refmem_command_slot_t s_refmem_command_slot;
static bool critical;
#define DISTRIBUTED_REFMEM_SOURCE_INSTANCE_REFMEM_AO 0u
void osal_critical_enter(void) { assert(!critical); critical = true; }
void osal_critical_exit(void) { assert(critical); critical = false; }
uint32_t osal_tick_ms(void) { return 1; }
bool distributed_refmem_can_accept_node_load_intent(uint32_t idle) { return idle != 0; }
bool distributed_refmem_command_ack(uint32_t node, uint32_t evidence)
{ return refmem_command_ack(&s_refmem_command_slot, node, evidence); }
bool distributed_refmem_command_nack(uint32_t node, refmem_command_reason_t reason, uint32_t evidence)
{ return refmem_command_nack(&s_refmem_command_slot, node, reason, evidence); }
uint32_t ota_crc32_update(uint32_t crc, const uint8_t *data, size_t length)
{ return pota_crc32_update(crc, data, length); }
bool trigger_sequence_service_is_active(void) { return frozen; }
bool trigger_sequence_service_configuration_begin(void)
{
    if (frozen || configuration_gate) return false;
    configuration_gate = true;
    return true;
}
void trigger_sequence_service_configuration_end(void)
{ assert(configuration_gate); configuration_gate = false; }
bool sync_trigger_sequence_can_start(void) { return !legacy_busy; }
void sync_trigger_get_vector(trigger_vector_t *vector)
{ memset(vector, 0, sizeof(*vector)); vector->state = legacy_busy ? TRIG_STATE_FAULT : TRIG_STATE_IDLE; }
void scpi_port_push_exec_error(scpi_t *context, const char *message)
{ (void)message; SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR); }
scpi_result_t scpi_port_result_accepted(scpi_t *context)
{ SCPI_ResultUInt32(context, 1u); return SCPI_RES_OK; }
trigger_sequence_link_config_diagnostic_t trigger_sequence_link_configure_checked(
    const trigger_sequence_link_config_t *config)
{
    trigger_sequence_link_config_diagnostic_t diagnostic = {0};
    if (frozen) diagnostic.result = TRIGGER_SEQUENCE_LINK_CONFIG_ACTIVE;
    else if (configuration_gate) diagnostic.result = TRIGGER_SEQUENCE_LINK_CONFIG_SERVICE_BUSY;
    else if (link_rejected) {
        diagnostic.result = TRIGGER_SEQUENCE_LINK_CONFIG_TDMA_REJECTED;
        diagnostic.tdma_result = TDMA_STOPPED_CONFIG_GENERATION_PENDING;
    }
    if (diagnostic.result != TRIGGER_SEQUENCE_LINK_CONFIG_OK) return diagnostic;
    link_status.config = *config;
    return diagnostic;
}
void trigger_sequence_link_get_status(trigger_sequence_link_status_t *status)
{ *status = link_status; }
bool trigger_sequence_link_get_history(uint32_t ordinal, trigger_sequence_link_history_t *record)
{
    if (ordinal != 1u) return false;
    *record = (trigger_sequence_link_history_t){
        .ordinal = 1u, .run_id = 4u, .generation = 5u, .binding_epoch = 6u,
        .exchange_id = 7u, .position = 1u, .sequence_index = 2u,
        .sequence_state = 3u, .output_code = 4u,
        .threshold_pulses = 1000u, .observed_pulses = 1032u,
        .trigger_ordinal = 8u, .ready_ordinal = 8u,
        .position_admitted_tick_ms = 100u, .sample_done_tick_ms = 180u,
        .cycle_elapsed_ms = 80u,
        .outcome_flags = 7u, .timing_flags = 63u,
        .timing_ticks = {0u, 1u, 4294967296ull, 4294967297ull, 4294967298ull, 4294967299ull}};
    return true;
}
bool trigger_sequence_link_get_history_status(trigger_sequence_link_history_status_t *status)
{
    if (history_busy) return false;
    *status = (trigger_sequence_link_history_status_t){
        TRIGGER_SEQUENCE_LINK_HISTORY_VECTOR_VERSION, BOARD_SYS_CLOCK_HZ,
        TRIGGER_SEQUENCE_LINK_HISTORY_CAPACITY, 4u, 5u, 6u, 1000u, 240u, 240u, 0u};
    return true;
}
tdma_pio_spi_ring_adapter_t *tdma_runtime_owner_get_ring_adapter(void) { return NULL; }
tdma_local_return_snapshot_quality_t tdma_pio_spi_ring_adapter_get_local_return_snapshot(
    const tdma_pio_spi_ring_adapter_t *adapter, uint32_t values[6])
{
    assert(adapter == NULL);
    memset(values, 0, 6u * sizeof(*values));
    return TDMA_LOCAL_RETURN_SNAPSHOT_UNAVAILABLE;
}
#include "sequence_role_scpi_handlers.inc"

static const scpi_command_t commands[] = {
    {.pattern="CONFigure:SEQuence:NODE:ROLE", .callback=scpi_sequence_node_role},
    {.pattern="READ:SEQuence:NODE:ROLE?", .callback=scpi_sequence_node_role_q},
    {.pattern="CONFigure:SEQuence:LINK", .callback=scpi_sequence_link_config},
    {.pattern="READ:SEQuence:LINK?", .callback=scpi_sequence_link_q},
    {.pattern="READ:SEQuence:LINK:TRANsport?", .callback=scpi_sequence_link_transport_q},
    {.pattern="READ:SEQuence:COUNter?", .callback=scpi_sequence_counter_q},
    {.pattern="READ:SEQuence:COUNter:HISTory?", .callback=scpi_sequence_counter_history_q},
    {.pattern="READ:SEQuence:HISTory?", .callback=scpi_sequence_history_q},
    {.pattern="READ:SEQuence:HISTory:TIMing?", .callback=scpi_sequence_history_timing_q},
    {.pattern="READ:SEQuence:HISTory:STATus?", .callback=scpi_sequence_history_status_q},
    SCPI_CMD_LIST_END
};
static size_t output(scpi_t *context, const char *data, size_t size)
{
    (void)context;
    assert(response_length+size < sizeof(response));
    memcpy(response+response_length, data, size);
    response_length += size;
    return size;
}
static int on_error(scpi_t *context, int_fast16_t code)
{ (void)context; if (code) ++errors; return 0; }
static scpi_result_t flush(scpi_t *context) { (void)context; return SCPI_RES_OK; }

int main(void)
{
    assert(refmem_application_model_init());
    assert(refmem_command_init(&s_refmem_command_slot, 0));
    scpi_t context;
    char buffer[2048], line[1024];
    scpi_error_t error_queue[32];
    scpi_interface_t interface = {.write=output, .error=on_error, .flush=flush};
    SCPI_Init(&context, commands, &interface, scpi_units_def, "TEST", "SEQ", "0", "0",
              buffer, sizeof(buffer), error_queue, 32);
    while (fgets(line, sizeof(line), stdin)) {
        if (line[0]=='@') {
            if (strncmp(line,"@active",7)==0) frozen=true;
            else if (strncmp(line,"@legacy",7)==0) legacy_busy=true;
            else if (strncmp(line,"@idle",5)==0) { frozen=false; legacy_busy=false; }
            else if (strncmp(line,"@config_busy",12)==0) configuration_gate=true;
            else if (strncmp(line,"@config_clear",13)==0) configuration_gate=false;
            else if (strncmp(line,"@link_reject",12)==0) link_rejected=true;
            else if (strncmp(line,"@link_accept",12)==0) link_rejected=false;
            else if (strncmp(line,"@history_busy",13)==0) history_busy=true;
            else if (strncmp(line,"@history_ready",14)==0) history_busy=false;
            else if (strncmp(line,"@link_status",12)==0) {
                const trigger_sequence_link_config_t config = link_status.config;
                link_status = (trigger_sequence_link_status_t){
                    .config=config, .phase=8u, .error=0u, .binding_epoch=101u, .model_epoch=102u,
                    .run_id=103u, .generation=104u, .step=7u, .tx_fragments=48u, .rx_messages=16u,
                    .rejected=2u, .triggers=8u, .ready=8u, .completed=7u, .repeat_count=1u,
                    .exchange_id=105u, .offer_delay_ms=3u, .return_delay_ms=4u,
                    .inbox_delay_ms=5u, .message_total_ms=12u};
            }
            else if (strncmp(line,"@command_busy",13)==0) {
                const refmem_command_request_t request = {
                    .command_seq=1, .source_node=0, .source_instance=0,
                    .target_mask=1, .required_mask=1,
                    .command_type=REFMEM_COMMAND_TYPE_TABLE_PACKAGE_STAGE,
                    .command_class=REFMEM_COMMAND_CLASS_CONFIG,
                    .payload_kind=REFMEM_COMMAND_PAYLOAD_INLINE_SMALL,
                    .payload_ref=0, .payload_size=4, .timeout_us=50000};
                assert(refmem_command_try_post(&s_refmem_command_slot, &request, 0));
            }
            else if (strncmp(line,"@command_clear",14)==0) {
                assert(refmem_command_init(&s_refmem_command_slot, 0));
            }
            else if (strncmp(line,"@activate",9)==0) {
                const refmem_table_activation_gate_t gate = {
                    .refmem_idle=1, .realtime_idle=1, .flash_safe=1, .crc_ok=1,
                    .owner_ok=1, .slot_claim_ok=1, .deployment_gate_ok=1, .command_ack_ok=1};
                assert(refmem_application_model_prepare_staging_table_views());
                assert(refmem_table_registry_activate_staging(&gate));
                assert(refmem_application_model_commit_prepared_table_views());
            }
            continue;
        }
        errors=0; response_length=0; memset(response,0,sizeof(response));
        SCPI_Input(&context,line,strlen(line));
        response[strcspn(response,"\r\n")]=0;
        printf("%u|%s\n",errors,response);
    }
    return 0;
}
