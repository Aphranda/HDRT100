#ifndef SYNC_IO_MODE_SEQ_STEP_H
#define SYNC_IO_MODE_SEQ_STEP_H

#include <stdbool.h>
#include <stdint.h>

#include "sync_io.h"
#include "sync_io_mode.h"

typedef struct {
    const uint32_t *seq_table;
    uint32_t seq_length;
    uint32_t seq_width;
    uint32_t trigger_pin;
    sync_io_edge_t edge;
    bool gate_enabled;
} sync_io_seq_step_mode_config_t;

bool sync_io_seq_step_mode_validate(const sync_io_seq_step_mode_config_t *config);
bool sync_io_seq_step_mode_arm(const sync_io_seq_step_mode_config_t *config);
const sync_io_mode_ops_t *sync_io_seq_step_mode_ops(void);

#endif

