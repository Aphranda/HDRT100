#ifndef TRIGGER_RESOURCE_MAP_H
#define TRIGGER_RESOURCE_MAP_H

#include <stdint.h>

#include "sync_io_mode.h"
#include "trigger_vector.h"

uint32_t trigger_resource_map_from_mode_resources(uint32_t mode_resources);
uint32_t trigger_resource_map_from_mode_hw(const sync_io_mode_hw_resources_t *hw);
uint32_t trigger_resource_map_for_mode(sync_io_mode_id_t mode_id);
uint32_t trigger_resource_map_for_state(trig_state_t state);

#endif
