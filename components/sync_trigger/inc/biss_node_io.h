#ifndef BISS_NODE_IO_H
#define BISS_NODE_IO_H

#include <stdbool.h>
#include <stdint.h>

#include "biss_protocol.h"
#include "sync_io.h"
#include "trigger_vector.h"

typedef enum {
    BISS_NODE_IO_POLL_OK = 0,
    BISS_NODE_IO_POLL_IO_LOST,
    BISS_NODE_IO_POLL_SCAN_STEP,
    BISS_NODE_IO_POLL_REARM_FAILED,
} biss_node_io_poll_result_t;

bool biss_node_io_make_profile(const trigger_vector_t *vector,
                               biss_profile_t *profile);
bool biss_node_io_arm(const trigger_vector_t *vector);
void biss_node_io_disarm(void);
bool biss_node_io_is_running(void);
biss_node_io_poll_result_t biss_node_io_poll_runtime(trigger_vector_t *vector);
bool biss_node_io_get_tap_config(sync_io_biss_tap_config_t *config);
void biss_node_io_sample_scan_rearm_succeeded(void);
bool biss_node_io_poll(trigger_vector_t *vector);
void biss_node_io_rx_irq_callback(void);
bool biss_node_io_process_frame(trigger_vector_t *vector, uint64_t frame);
bool biss_node_io_process_position(trigger_vector_t *vector, uint32_t position);

#endif
