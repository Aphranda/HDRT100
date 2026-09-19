#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "distributed_refmem.h"
#include "refmem_application_model.h"
#include "refmem_table_registry.h"
#include "refmem_quality.h"
#include "refmem_vector_table.h"
#include "scpi/scpi.h"

static bool s_initialized = true;
static distributed_refmem_activation_diagnostic_t s_activation_diagnostic;
static uint32_t s_activation_attempt_seq;
static refmem_realtime_tdma_service_t s_refmem_realtime_tdma;
static refmem_command_slot_t s_refmem_command_slot;
#define DISTRIBUTED_REFMEM_SOURCE_INSTANCE_REFMEM_AO 0u
static bool critical, configuration, descriptor, post, take, flash, claim;
static bool quality_available, prepare, profile, apply;
static bool realtime_idle = true;
static unsigned commits, samples, nacks, posts;
static refmem_application_model_snapshot_t model;
static tdma_service_quality_snapshot_t quality;
static refmem_table_registry_snapshot_t registry;
static refmem_table_image_descriptor_t staging;
static uint32_t load_mode;
void refmem_sync_get_quality(const refmem_sync_context_t *sync, refmem_sync_quality_counters_t *out)
{ (void)sync; memset(out, 0, sizeof(*out)); }

void osal_critical_enter(void) { assert(!critical); critical = true; }
void osal_critical_exit(void) { assert(critical); critical = false; }
uint32_t osal_tick_ms(void) { return 1u; }
bool trigger_sequence_service_configuration_begin(void) { return configuration; }
void trigger_sequence_service_configuration_end(void) { assert(configuration); }
bool refmem_table_registry_get_image_descriptor(refmem_table_image_role_t role,
    refmem_table_image_descriptor_t *image)
{ (void)role; *image = staging; return descriptor; }
static uint32_t distributed_refmem_u32_payload_crc32(const uint32_t *words, uint32_t count)
{ (void)words; (void)count; return 123u; }
static bool distributed_refmem_post_command_replacing_complete(refmem_command_request_t *r, uint32_t tick)
{ (void)r; (void)tick; ++posts; return post; }
refmem_command_take_result_t refmem_command_try_take(refmem_command_slot_t *slot, uint32_t node,
    uint32_t epoch, uint32_t run, uint32_t crc, uint32_t evidence)
{ (void)slot; (void)node; (void)epoch; (void)run; (void)crc; (void)evidence;
  return take ? REFMEM_COMMAND_TAKE_TAKEN : REFMEM_COMMAND_TAKE_NO_COMMAND; }
void refmem_application_model_get_load_snapshot(refmem_application_model_load_snapshot_t *s)
{ memset(s, 0, sizeof(*s)); s->mode = load_mode; }
static bool distributed_refmem_flash_activation_safe(void) { return flash; }
static bool distributed_refmem_slot_claim_gate_ready(void) { return claim; }
const refmem_application_model_snapshot_t *refmem_application_model_get_snapshot(void) { return &model; }
bool refmem_realtime_tdma_get_quality_snapshot(const refmem_realtime_tdma_service_t *s,
    tdma_service_quality_snapshot_t *out)
{ (void)s; ++samples; if (!quality_available) return false; *out = quality; return true; }
bool refmem_application_model_prepare_staging_table_views(void) { return prepare; }
bool refmem_table_registry_note_activation_result(refmem_table_activation_result_t value)
{ registry.last_error = value; return true; }
void refmem_application_model_discard_prepared_table_views(void) {}
bool distributed_refmem_command_nack(uint32_t node, refmem_command_reason_t reason, uint32_t evidence)
{ (void)node; (void)reason; (void)evidence; ++nacks; return true; }
bool distributed_refmem_command_ack(uint32_t node, uint32_t evidence)
{ (void)node; (void)evidence; return true; }
bool refmem_application_model_get_prepared_tdma_foundation_profile(tdma_foundation_profile_t *out)
{ memset(out, 0, sizeof(*out)); return profile; }
static bool distributed_refmem_tdma_profile_activation_ready(const tdma_foundation_profile_t *p, uint32_t *crc)
{ (void)p; *crc = 44; return profile; }
static bool distributed_refmem_apply_tdma_foundation_profile(const tdma_foundation_profile_t *p, uint32_t crc)
{ (void)p; (void)crc; return apply; }
bool refmem_application_model_commit_prepared_table_views(void) { return true; }
bool refmem_application_model_apply_active_table_views(void) { return true; }
void refmem_table_registry_get_snapshot(refmem_table_registry_snapshot_t *out) { *out = registry; }
bool refmem_table_registry_activate_staging(const refmem_table_activation_gate_t *gate)
{
    if (!gate->refmem_idle || !gate->realtime_idle || !gate->flash_safe || !gate->crc_ok ||
        !gate->owner_ok || !gate->slot_claim_ok || !gate->deployment_gate_ok || !gate->command_ack_ok) {
        registry.last_error = REFMEM_TABLE_ACTIVATE_ERR_GATE; return false;
    }
    registry.last_error = REFMEM_TABLE_ACTIVATE_OK; ++commits; return true;
}
#include "activation_owner.inc"

static void reset(void)
{
    configuration = descriptor = post = take = flash = claim = true;
    quality_available = prepare = profile = apply = s_initialized = true;
    model.valid = 1u; commits = samples = nacks = posts = 0;
    load_mode = REFMEM_APP_MODEL_MODE_IDLE;
    memset(&quality, 0, sizeof(quality));
    registry.last_error = REFMEM_TABLE_ACTIVATE_ERR_RUNTIME_PROFILE;
    staging = (refmem_table_image_descriptor_t){.state=REFMEM_TABLE_VALIDATION_OWNER_OK,
        .package_crc32=123, .table_seq=7, .table_mask=REFMEM_APP_TABLE_MASK_ALL};
}

static void rejected(uint32_t expected)
{
    distributed_refmem_activation_diagnostic_t diagnostic, readback;
    const uint32_t previous = s_activation_attempt_seq;
    assert(!distributed_refmem_activate_staging_checked(1u, &diagnostic));
    assert(diagnostic.attempt_seq == previous + 1u && diagnostic.result == expected);
    distributed_refmem_get_activation_diagnostic(&readback);
    assert(memcmp(&diagnostic, &readback, sizeof(diagnostic)) == 0);
    assert(commits == 0 && posts <= 1 && samples <= 1);
}

static bool scpi_refmem_realtime_idle(void) { return realtime_idle; }
void scpi_port_push_exec_error(scpi_t *context, const char *message)
{ (void)message; SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR); }
bool scpi_sequence_params_end(scpi_t *context)
{ scpi_parameter_t parameter; return !SCPI_Parameter(context, &parameter, FALSE); }
static void scpi_refmem_result_table_image_descriptor(scpi_t *, const refmem_table_image_descriptor_t *);
#include "activation_scpi.inc"
static char response[2048];
static size_t length;
static unsigned errors;
static size_t write_response(scpi_t *context, const char *data, size_t size)
{ (void)context; assert(length + size < sizeof(response)); memcpy(response + length, data, size); length += size; return size; }
static int error(scpi_t *context, int_fast16_t code) { (void)context; if (code) ++errors; return 0; }
static scpi_result_t flush(scpi_t *context) { (void)context; return SCPI_RES_OK; }
static const scpi_command_t commands[] = {
    {.pattern="CONF:SEQ:NODE:ACT", .callback=scpi_sequence_node_activate},
    {.pattern="SYST:REFMEM:LOAD:ACT:STAT?", .callback=scpi_cmd_refmem_load_activation_status_q},
    SCPI_CMD_LIST_END
};

int main(void)
{
    reset(); configuration=false; rejected(DISTRIBUTED_REFMEM_ACT_CONFIG_BUSY);
    reset(); s_initialized=false; rejected(DISTRIBUTED_REFMEM_ACT_NOT_INITIALIZED);
    reset(); descriptor=false; rejected(DISTRIBUTED_REFMEM_ACT_STAGING_UNAVAILABLE);
    reset(); post=false; rejected(DISTRIBUTED_REFMEM_ACT_COMMAND_BUSY);
    reset(); take=false; rejected(DISTRIBUTED_REFMEM_ACT_TAKE_REJECTED);
    reset(); quality_available=false; rejected(DISTRIBUTED_REFMEM_ACT_SNAPSHOT_UNAVAILABLE);
    assert(s_activation_diagnostic.failed_mask == 0 && s_activation_diagnostic.unavailable_mask == 0x40u);
    assert(s_activation_diagnostic.evaluated_mask == 0xbfu && s_activation_diagnostic.quality_state == 1u);
    reset(); quality_available=false; flash=false; rejected(DISTRIBUTED_REFMEM_ACT_GATE_REJECTED);
    assert(s_activation_diagnostic.failed_mask == 4u && s_activation_diagnostic.unavailable_mask == 0x40u);
    uint32_t *counters[] = {&quality.reject_count, &quality.overrun_count, &quality.timeout_count, &quality.last_error};
    for (unsigned i = 0; i < 4; ++i) {
        reset(); *counters[i] = 9u; rejected(DISTRIBUTED_REFMEM_ACT_GATE_REJECTED);
        assert(s_activation_diagnostic.failed_mask == 0x40u && s_activation_diagnostic.unavailable_mask == 0);
        assert(s_activation_diagnostic.quality_state == 3 && s_activation_diagnostic.quality_reason != 0);
    }
    reset(); claim=false; rejected(DISTRIBUTED_REFMEM_ACT_GATE_REJECTED);
    assert(s_activation_diagnostic.failed_mask == 0x20u);
    reset(); prepare=false; rejected(DISTRIBUTED_REFMEM_ACT_PREPARE_REJECTED);
    reset(); profile=false; rejected(DISTRIBUTED_REFMEM_ACT_PROFILE_REJECTED);
    reset(); assert(distributed_refmem_activate_staging(1u)); assert(commits == 1 && samples == 1 && posts == 1);
    assert(s_activation_diagnostic.result == DISTRIBUTED_REFMEM_ACT_OK);
    reset(); apply=false; assert(!distributed_refmem_activate_staging(1u));
    assert(s_activation_diagnostic.result == DISTRIBUTED_REFMEM_ACT_APPLY_REJECTED && commits == 1);
    reset(); assert(!distributed_refmem_activate_staging(0u));
    assert(s_activation_diagnostic.result == DISTRIBUTED_REFMEM_ACT_GATE_REJECTED);
    assert(s_activation_diagnostic.failed_mask == 2 && posts == 0 && samples == 0);

    scpi_t context; char input[512]; scpi_error_t queue[8];
    scpi_interface_t interface = {.write=write_response, .error=error, .flush=flush};
    SCPI_Init(&context, commands, &interface, scpi_units_def, "TEST", "ACT", "0", "0", input, sizeof(input), queue, 8);
    reset();
    SCPI_Input(&context, "CONF:SEQ:NODE:ACT\n", strlen("CONF:SEQ:NODE:ACT\n"));
    assert(strncmp(response, "\"ACTIVE\",", 9) == 0 && !errors);
    unsigned commas = 0; for (size_t i = 0; i < length; ++i) commas += response[i] == ',';
    assert(commas == 33u);
    reset(); configuration=false; length=0; memset(response, 0, sizeof(response));
    SCPI_Input(&context, "CONF:SEQ:NODE:ACT\n", strlen("CONF:SEQ:NODE:ACT\n"));
    assert(strncmp(response, "\"REJECTED\",", 11) == 0 && !errors);
    assert(s_activation_diagnostic.registry_error == REFMEM_TABLE_ACTIVATE_ERR_BAD_ARGUMENT);
    assert(s_activation_diagnostic.registry_error != registry.last_error);
    reset(); quality_available=false; length=0; memset(response, 0, sizeof(response));
    SCPI_Input(&context, "CONF:SEQ:NODE:ACT\n", strlen("CONF:SEQ:NODE:ACT\n"));
    assert(strncmp(response, "\"BUSY\",", 7) == 0 && !errors && commits == 0 && posts == 1);
    length=0; memset(response, 0, sizeof(response));
    SCPI_Input(&context, "SYST:REFMEM:LOAD:ACT:STAT?\n", strlen("SYST:REFMEM:LOAD:ACT:STAT?\n"));
    commas=0; for (size_t i=0; i<length; ++i) commas += response[i] == ',';
    assert(commas == 13u && !errors && posts == 1);
    reset(); realtime_idle=false; length=0; memset(response, 0, sizeof(response));
    SCPI_Input(&context, "CONF:SEQ:NODE:ACT\n", strlen("CONF:SEQ:NODE:ACT\n"));
    assert(strncmp(response, "\"REJECTED\",", 11) == 0 && errors == 1 && posts == 0);
    assert(s_activation_diagnostic.failed_mask == 2u);
    puts("activation diagnostics passed");
    return 0;
}
