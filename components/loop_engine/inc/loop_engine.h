#ifndef LOOP_ENGINE_H
#define LOOP_ENGINE_H

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool ready;
    uint32_t service_count;
    uint32_t first_service_ms;
    uint32_t last_service_ms;
} loop_engine_status_t;

bool loop_engine_init(void);
void loop_engine_set_ready(bool ready);
void loop_engine_service(void);
void loop_engine_get_status(loop_engine_status_t *status);

#endif
