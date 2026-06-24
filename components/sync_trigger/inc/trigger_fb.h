#ifndef TRIGGER_FB_H
#define TRIGGER_FB_H

#include "trigger_vector.h"

bool trigger_fb_init(trigger_vector_t *vector);
void trigger_fb_execute(trigger_vector_t *vector, const trig_event_t *event);

#endif
