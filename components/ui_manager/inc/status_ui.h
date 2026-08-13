#ifndef STATUS_UI_H
#define STATUS_UI_H

#include <stdbool.h>

bool status_ui_init(void);
void status_ui_key_next(void);
bool status_ui_render(void);
bool status_ui_needs_render(void);

#endif
