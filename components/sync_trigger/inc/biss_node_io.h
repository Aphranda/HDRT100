#ifndef BISS_NODE_IO_H
#define BISS_NODE_IO_H

#include <stdbool.h>
#include <stdint.h>

#include "biss_protocol.h"
#include "trigger_vector.h"

bool biss_node_io_make_profile(const trigger_vector_t *vector,
                               biss_profile_t *profile);
bool biss_node_io_arm(const trigger_vector_t *vector);
void biss_node_io_disarm(void);
bool biss_node_io_is_running(void);
bool biss_node_io_poll(trigger_vector_t *vector);
void biss_node_io_rx_irq_callback(void);
bool biss_node_io_process_frame(trigger_vector_t *vector, uint64_t frame);
bool biss_node_io_process_position(trigger_vector_t *vector, uint32_t position);

#endif
