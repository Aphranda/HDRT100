/* Real parser + whole production handler bodies; model/storage are observable
 * boundary fakes. No hardware access, and no duplicated parser implementation. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "scpi/scpi.h"
#include "distributed_config.h"
#include "refmem_application_model.h"
#include "refmem_table_registry.h"
#include "refmem_sync.h"
typedef struct spi_inst spi_inst_t;
#include "storage_manager.h"
#include "sync_trigger.h"
#include "trigger_sequence_service.h"

#define SCPI_REFMEM_PACKAGE_PATH "/refmem/app_model.rmtp"
static unsigned sequence_state, legacy_state, mutations, errors;
static uint32_t last_enabled, last_required, last_order;

void trigger_sequence_service_get_status(trigger_sequence_service_status_t *status)
{ memset(status, 0, sizeof(*status)); status->state = (trigger_sequence_service_state_t)sequence_state; }
void sync_trigger_get_vector(trigger_vector_t *vector)
{ memset(vector, 0, sizeof(*vector)); vector->state = legacy_state ? TRIG_STATE_FAULT : TRIG_STATE_IDLE; }
bool sync_trigger_sequence_can_start(void) { return legacy_state == 0; }
void scpi_port_push_exec_error(scpi_t *context, const char *message)
{ (void)message; SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR); }
bool scpi_port_reject_if_run_forbidden(scpi_t *context, uint32_t class_id)
{ (void)context; (void)class_id; return false; }
bool scpi_port_read_u32(scpi_t *context, uint32_t *value)
{ return SCPI_ParamUInt32(context, value, TRUE) == TRUE; }
scpi_result_t scpi_port_result_ok(scpi_t *context)
{ SCPI_ResultBool(context, TRUE); return SCPI_RES_OK; }
bool distributed_refmem_stage_model_turntable_load(uint32_t slot, uint32_t output_index)
{ (void)slot; (void)output_index; ++mutations; return true; }
void refmem_application_model_get_load_snapshot(refmem_application_model_load_snapshot_t *snapshot)
{ memset(snapshot, 0, sizeof(*snapshot)); snapshot->mode = REFMEM_APP_MODEL_MODE_IDLE; }
void refmem_application_model_get_board_load_snapshot(refmem_board_capability_load_snapshot_t *snapshot)
{ memset(snapshot, 0, sizeof(*snapshot)); }
bool distributed_refmem_can_accept_node_load_intent(uint32_t idle) { return idle != 0; }
bool distributed_refmem_stage_node_load(uint32_t node, uint32_t instance, uint32_t role,
    uint32_t persona, uint32_t enabled, uint32_t required, uint32_t order)
{
    (void)node; (void)instance; (void)role; (void)persona;
    ++mutations; last_enabled = enabled; last_required = required; last_order = order;
    return true;
}
bool distributed_refmem_stage_board_capability(uint32_t a, uint32_t b, uint32_t c,
    uint32_t d, uint32_t e, uint32_t f, uint32_t g, uint32_t h, uint32_t i)
{ (void)a; (void)b; (void)c; (void)d; (void)e; (void)f; (void)g; (void)h; (void)i; ++mutations; return true; }
bool distributed_refmem_activate_staging(uint32_t idle)
{ if (!idle) return false; ++mutations; return true; }
bool distributed_refmem_activate_staging_checked(uint32_t idle,
    distributed_refmem_activation_diagnostic_t *diagnostic)
{ memset(diagnostic, 0, sizeof(*diagnostic)); return distributed_refmem_activate_staging(idle); }
void distributed_refmem_get_activation_diagnostic(distributed_refmem_activation_diagnostic_t *diagnostic)
{ memset(diagnostic, 0, sizeof(*diagnostic)); }
bool distributed_refmem_stage_sd_system_pack(const char *path, uint32_t path_hash,
    uint32_t manifest_status, uint32_t manifest_schema, uint32_t required, uint32_t missing,
    const char *build, uint32_t crc, uint32_t valid, uint32_t error, const uint8_t *data,
    size_t size, const uint32_t *table_crc, uint32_t count, uint32_t mask, uint32_t bad)
{
    (void)path; (void)path_hash; (void)manifest_status; (void)manifest_schema; (void)required;
    (void)missing; (void)build; (void)crc; (void)valid; (void)error; (void)data; (void)size;
    (void)table_crc; (void)count; (void)mask; (void)bad; ++mutations; return true;
}
bool distributed_refmem_apply_node_load_sync_payload(const uint8_t *payload, uint16_t size)
{ (void)payload; (void)size; ++mutations; return true; }
bool storage_manager_post_manifest_scan_job(uint32_t *id) { (void)id; return false; }
void storage_manager_get_job_result(storage_manager_job_result_t *job) { memset(job, 0, sizeof(*job)); }
void storage_manager_get_vector(storage_manager_vector_t *vector) { memset(vector, 0, sizeof(*vector)); }
bool refmem_table_registry_begin_staging_write(refmem_table_owner_t owner, uint8_t **buffer, size_t *size)
{ (void)owner; (void)buffer; (void)size; return false; }
bool refmem_table_registry_end_staging_write(refmem_table_owner_t owner, bool committed)
{ (void)owner; (void)committed; return true; }
bool refmem_table_registry_validate_package(const uint8_t *data, size_t size, refmem_table_package_validation_t *validation)
{ (void)data; (void)size; (void)validation; return false; }
void refmem_table_registry_get_snapshot(refmem_table_registry_snapshot_t *snapshot)
{ memset(snapshot, 0, sizeof(*snapshot)); }
bool refmem_table_registry_get_image_descriptor(refmem_table_image_role_t role, refmem_table_image_descriptor_t *descriptor)
{ (void)role; memset(descriptor, 0, sizeof(*descriptor)); return true; }
static bool scpi_refmem_wait_storage_job(uint32_t job) { (void)job; return false; }
static bool scpi_refmem_read_package(const char *path, uint8_t *data, size_t capacity, size_t *size)
{ (void)path; (void)data; (void)capacity; (void)size; return false; }

#include "refmem_scpi_handlers.inc"

static const scpi_command_t commands[] = {
    { .pattern = "SYSTem:REFMEM:LOAD:NODE", .callback = scpi_cmd_refmem_load_node },
    { .pattern = "SYSTem:REFMEM:LOAD:BOARD", .callback = scpi_cmd_refmem_load_board },
    { .pattern = "SYSTem:REFMEM:LOAD:SD", .callback = scpi_cmd_refmem_load_sd },
    { .pattern = "SYSTem:REFMEM:LOAD:ACTivate", .callback = scpi_cmd_refmem_load_activate },
    { .pattern = "CONFigure:SEQuence:NODE:LOAD", .callback = scpi_sequence_node_load },
    { .pattern = "CONFigure:SEQuence:NODE:ACTivate", .callback = scpi_sequence_node_activate },
    { .pattern = "CONFigure:MODEl:TURNtable:LOAD", .callback = scpi_cmd_model_turntable_load },
    SCPI_CMD_LIST_END
};
static size_t output(scpi_t *context, const char *data, size_t size)
{ (void)context; (void)data; return size; }
static int error(scpi_t *context, int_fast16_t code)
{ (void)context; if (code) ++errors; return 0; }
static scpi_result_t flush(scpi_t *context) { (void)context; return SCPI_RES_OK; }

int main(void)
{
    scpi_t context;
    char input[4096], line[4096];
    scpi_error_t queue[32];
    scpi_interface_t interface = { .write = output, .error = error, .flush = flush };
    SCPI_Init(&context, commands, &interface, scpi_units_def, "TEST", "REFMEM", "0", "0",
              input, sizeof(input), queue, 32);
    while (fgets(line, sizeof(line), stdin)) {
        if (sscanf(line, "@state %u", &sequence_state) == 1) continue;
        if (sscanf(line, "@legacy %u", &legacy_state) == 1) continue;
        errors = 0;
        SCPI_ErrorClear(&context);
        if (strncmp(line, "@syncguard", 10) == 0) {
            (void)scpi_refmem_sequence_config_allowed(&context);
        } else if (strncmp(line, "@delta", 6) == 0) {
            refmem_sync_rx_snapshot_t rx = {0};
            rx.accepted = 1;
            rx.header.frame_type = REFMEM_SYNC_FRAME_DELTA;
            scpi_refmem_sync_apply_node_load_delta(&rx);
        } else {
            SCPI_Input(&context, line, strlen(line));
        }
        printf("%u,%u,%u,%u,%u\n", errors, mutations, last_enabled, last_required, last_order);
    }
    return 0;
}
