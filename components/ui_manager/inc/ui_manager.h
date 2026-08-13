#ifndef UI_MANAGER_H
#define UI_MANAGER_H

#include <stdbool.h>

bool ui_manager_init(void);
void ui_manager_mark_dirty(void);
void ui_manager_service(void);

#endif
