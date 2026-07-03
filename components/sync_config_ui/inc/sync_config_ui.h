#ifndef SYNC_CONFIG_UI_H
#define SYNC_CONFIG_UI_H

#include <stdbool.h>

bool sync_config_ui_init(void);
void sync_config_ui_key_next(void);
bool sync_config_ui_render(void);
bool sync_config_ui_needs_render(void);

#endif
