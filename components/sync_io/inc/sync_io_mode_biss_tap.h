#ifndef SYNC_IO_MODE_BISS_TAP_H
#define SYNC_IO_MODE_BISS_TAP_H

#include <stdbool.h>

#include "sync_io.h"
#include "sync_io_mode.h"

typedef sync_io_biss_tap_config_t sync_io_biss_tap_mode_config_t;

bool sync_io_biss_tap_mode_validate(const sync_io_biss_tap_mode_config_t *config);
bool sync_io_biss_tap_mode_arm(const sync_io_biss_tap_mode_config_t *config);
const sync_io_mode_ops_t *sync_io_biss_tap_mode_ops(void);

#endif

