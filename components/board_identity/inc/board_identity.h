#ifndef BOARD_IDENTITY_H
#define BOARD_IDENTITY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../../config/project_node_capacity.h"

#define BOARD_IDENTITY_MAX_NODES PROJECT_NODE_CAPACITY
#define BOARD_IDENTITY_SERIAL_MAX 17u

bool board_identity_init(void);
const char *board_identity_serial(void);
uint8_t board_identity_get_no(void);
bool board_identity_set_no(uint32_t logical_no);

#endif
