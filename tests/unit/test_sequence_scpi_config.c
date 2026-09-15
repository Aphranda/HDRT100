#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "scpi_config_commands.h"
#include "distributed_config.h"
#include "trigger_sequence_service.h"

static trigger_sequence_store_t store;
static char output[32768];
static size_t output_size;
static unsigned error_count;
static bool legacy_running;

trigger_sequence_store_t *trigger_sequence_service_config(void) { return &store; }

scpi_result_t scpi_port_result_accepted(scpi_t *context)
{
    SCPI_ResultUInt32(context, 1u);
    return SCPI_RES_OK;
}

void scpi_port_push_exec_error(scpi_t *context, const char *info)
{
    (void)info;
    SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
}

bool scpi_port_reject_if_run_forbidden(scpi_t *context, uint32_t class_id)
{
    assert(class_id == DISTRIBUTED_CONFIG_SCPI_CLASS_TRIGGER_CONFIG);
    if (legacy_running) SCPI_ErrorPush(context, SCPI_ERROR_EXECUTION_ERROR);
    return legacy_running;
}

static size_t capture(scpi_t *context, const char *data, size_t length)
{
    (void)context;
    assert(length < sizeof(output) - output_size);
    memcpy(output + output_size, data, length);
    output_size += length;
    return length;
}

static int error(scpi_t *context, int_fast16_t code)
{
    (void)context;
    (void)code;
    ++error_count;
    return 0;
}

static scpi_result_t flush(scpi_t *context) { (void)context; return SCPI_RES_OK; }

static const scpi_command_t commands[] = { SCPI_CONFIG_COMMANDS, SCPI_CMD_LIST_END };

int main(void)
{
    scpi_t context;
    char input[8192], line[8192];
    scpi_error_t errors[32];
    scpi_interface_t interface = {.write=capture, .flush=flush, .error=error};
    trigger_sequence_init(&store);
    SCPI_Init(&context, commands, &interface, scpi_units_def,
              "test", "DHRT100", "test", "test", input, sizeof(input), errors, 32);
    while (fgets(line, sizeof(line), stdin) != NULL) {
        if (strcmp(line, "@freeze\n") == 0) { store.frozen = true; continue; }
        if (strcmp(line, "@thaw\n") == 0) { store.frozen = false; continue; }
        if (strcmp(line, "@legacy\n") == 0) { legacy_running = true; continue; }
        if (strcmp(line, "@idle\n") == 0) { legacy_running = false; continue; }
        const trigger_sequence_store_t before = store;
        SCPI_ErrorClear(&context);
        output_size = 0u;
        error_count = 0u;
        /* Real stream parsing: deliberately fragment every command. */
        for (size_t i = 0u; i < strlen(line); ++i) SCPI_Input(&context, line + i, 1);
        while (output_size && (output[output_size - 1u] == '\n' || output[output_size - 1u] == '\r')) --output_size;
        printf("%u|%u|%u|%u|", error_count,
               memcmp(&before, &store, sizeof(store)) == 0, store.generation, store.active_slot);
        fwrite(output, 1u, output_size, stdout);
        putchar('\n');
    }
    return 0;
}
