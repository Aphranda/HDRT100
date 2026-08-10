#ifndef APP_H
#define APP_H

#include <stdbool.h>

bool app_init(void);
bool app_is_ready(void);
void app_run_once(void);
void app_management_run_once(void);
void app_realtime_run_once(void);
void app_comm_service(void);
void app_trigger_service(void);
void app_ota_service(void);
void app_storage_service(void);
void app_ui_service(void);
void app_diag_service(void);

#endif
