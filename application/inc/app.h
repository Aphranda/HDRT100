#ifndef APP_H
#define APP_H

#include <stdbool.h>

bool app_init(void);
bool app_is_ready(void);
bool app_is_control_plane_ready(void);
void app_realtime_run_once(void);
void app_usb_device_service(void);
void app_scpi_service(void);
void app_refmem_service(void);
void app_config_gate_service(void);
void app_ota_service(void);
void app_storage_service(void);
void app_diag_service(void);

#endif
