#ifndef DISTRIBUTED_REFMEM_POSITION_ACK_H
#define DISTRIBUTED_REFMEM_POSITION_ACK_H

#include <stdbool.h>
#include <stdint.h>

#include "distributed_refmem.h"

void distributed_refmem_position_note_freeze_taken(void);
void distributed_refmem_note_position_frozen( uint32_t run_id, bool active, uint32_t frozen_mask);
uint32_t distributed_refmem_position_freeze_acked( uint32_t run_id, uint32_t target_mask);
void distributed_refmem_position_ack_pending( uint32_t run_id, uint32_t target_mask, uint32_t *stale_mask, uint32_t *incomplete_mask);
bool distributed_refmem_position_priority_owned(void);
void distributed_refmem_position_priority_rx( const tdma_priority_rx_record_t *record);
bool distributed_refmem_position_priority_drain(uint32_t source_slot);

#endif /* DISTRIBUTED_REFMEM_POSITION_ACK_H */
