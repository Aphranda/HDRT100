#ifndef APP_RUNTIME_H
#define APP_RUNTIME_H

#include <stdbool.h>

bool app_runtime_init(void);
void app_runtime_run(void);
void app_runtime_fault_forever(void);
bool app_runtime_bringup(void);
void app_runtime_start_realtime_core(void);

#endif
