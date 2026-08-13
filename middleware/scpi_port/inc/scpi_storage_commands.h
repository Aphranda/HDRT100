#ifndef SCPI_STORAGE_COMMANDS_H
#define SCPI_STORAGE_COMMANDS_H

#include "scpi/scpi.h"

scpi_result_t scpi_cmd_storage_status_q(scpi_t *context);
scpi_result_t scpi_cmd_storage_info_q(scpi_t *context);
scpi_result_t scpi_cmd_storage_raw_clear(scpi_t *context);
scpi_result_t scpi_cmd_storage_raw_read_q(scpi_t *context);
scpi_result_t scpi_cmd_storage_mkfs(scpi_t *context);
scpi_result_t scpi_cmd_storage_init(scpi_t *context);
scpi_result_t scpi_cmd_storage_manifest_q(scpi_t *context);
scpi_result_t scpi_cmd_storage_job_info(scpi_t *context);
scpi_result_t scpi_cmd_storage_job_q(scpi_t *context);
scpi_result_t scpi_cmd_snapshot_write(scpi_t *context);
scpi_result_t scpi_cmd_snapshot_last_q(scpi_t *context);
scpi_result_t scpi_cmd_trace_last_q(scpi_t *context);
scpi_result_t scpi_cmd_fault_last_q(scpi_t *context);
scpi_result_t scpi_cmd_mmem_catalog_q(scpi_t *context);
scpi_result_t scpi_cmd_mmem_info_q(scpi_t *context);
scpi_result_t scpi_cmd_mmem_catalog_page_q(scpi_t *context);
scpi_result_t scpi_cmd_mmem_read_q(scpi_t *context);
scpi_result_t scpi_cmd_storage_file_info_q(scpi_t *context);
scpi_result_t scpi_cmd_storage_file_read_q(scpi_t *context);
scpi_result_t scpi_cmd_storage_file_delete(scpi_t *context);
scpi_result_t scpi_cmd_storage_file_rename(scpi_t *context);
scpi_result_t scpi_cmd_storage_file_write_begin(scpi_t *context);
scpi_result_t scpi_cmd_storage_file_write_data(scpi_t *context);
scpi_result_t scpi_cmd_storage_file_write_end(scpi_t *context);
scpi_result_t scpi_cmd_storage_file_write_abort(scpi_t *context);
scpi_result_t scpi_cmd_storage_file_write_status_q(scpi_t *context);
scpi_result_t scpi_cmd_storage_directory_create(scpi_t *context);
scpi_result_t scpi_cmd_storage_directory_delete(scpi_t *context);
scpi_result_t scpi_cmd_storage_directory_rename(scpi_t *context);
scpi_result_t scpi_cmd_storage_directory_catalog_q(scpi_t *context);

#define SCPI_STORAGE_COMMANDS \
    {.pattern = "SYSTem:SD:STATus?", .callback = scpi_cmd_storage_status_q}, \
    {.pattern = "SYSTem:SD:INFO?", .callback = scpi_cmd_storage_info_q}, \
    {.pattern = "SYSTem:SD:RAW:CLEar", .callback = scpi_cmd_storage_raw_clear}, \
    {.pattern = "SYSTem:SD:RAW:READ?", .callback = scpi_cmd_storage_raw_read_q}, \
    {.pattern = "SYSTem:SD:MKFS", .callback = scpi_cmd_storage_mkfs}, \
    {.pattern = "SYSTem:SD:INITialize", .callback = scpi_cmd_storage_init}, \
    {.pattern = "SYSTem:SD:MANifest?", .callback = scpi_cmd_storage_manifest_q}, \
    {.pattern = "SYSTem:STORage:JOB:INFO", .callback = scpi_cmd_storage_job_info}, \
    {.pattern = "SYSTem:STORage:JOB?", .callback = scpi_cmd_storage_job_q}, \
    {.pattern = "SYSTem:STORage:FILE:INFO?", .callback = scpi_cmd_storage_file_info_q}, \
    {.pattern = "SYSTem:STORage:FILE:READ?", .callback = scpi_cmd_storage_file_read_q}, \
    {.pattern = "SYSTem:STORage:FILE:DELete", .callback = scpi_cmd_storage_file_delete}, \
    {.pattern = "SYSTem:STORage:FILE:REName", .callback = scpi_cmd_storage_file_rename}, \
    {.pattern = "SYSTem:STORage:FILE:WRITe:BEGIN", .callback = scpi_cmd_storage_file_write_begin}, \
    {.pattern = "SYSTem:STORage:FILE:WRITe:DATA", .callback = scpi_cmd_storage_file_write_data}, \
    {.pattern = "SYSTem:STORage:FILE:WRITe:END", .callback = scpi_cmd_storage_file_write_end}, \
    {.pattern = "SYSTem:STORage:FILE:WRITe:ABORt", .callback = scpi_cmd_storage_file_write_abort}, \
    {.pattern = "SYSTem:STORage:FILE:WRITe:STATus?", .callback = scpi_cmd_storage_file_write_status_q}, \
    {.pattern = "SYSTem:STORage:DIRectory:CREate", .callback = scpi_cmd_storage_directory_create}, \
    {.pattern = "SYSTem:STORage:DIRectory:DELete", .callback = scpi_cmd_storage_directory_delete}, \
    {.pattern = "SYSTem:STORage:DIRectory:REName", .callback = scpi_cmd_storage_directory_rename}, \
    {.pattern = "SYSTem:STORage:DIRectory:CATalog?", .callback = scpi_cmd_storage_directory_catalog_q}, \
    {.pattern = "SYSTem:SNAPshot:WRITe", .callback = scpi_cmd_snapshot_write}, \
    {.pattern = "SYSTem:SNAPshot:LAST?", .callback = scpi_cmd_snapshot_last_q}, \
    {.pattern = "SYSTem:TRACe:LAST?", .callback = scpi_cmd_trace_last_q}, \
    {.pattern = "SYSTem:FAULT:LAST?", .callback = scpi_cmd_fault_last_q}, \
    {.pattern = "SYSTem:STORage:STATus?", .callback = scpi_cmd_storage_status_q}, \
    {.pattern = "MMEMory:CATalog:PAGE?", .callback = scpi_cmd_mmem_catalog_page_q}, \
    {.pattern = "MMEMory:CATalog?", .callback = scpi_cmd_mmem_catalog_q}, \
    {.pattern = "MMEMory:INFO?", .callback = scpi_cmd_mmem_info_q}, \
    {.pattern = "MMEMory:READ?", .callback = scpi_cmd_mmem_read_q}

#endif
