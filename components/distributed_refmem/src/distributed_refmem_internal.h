#ifndef DISTRIBUTED_REFMEM_INTERNAL_H
#define DISTRIBUTED_REFMEM_INTERNAL_H

#include "distributed_refmem.h"
#include "refmem_cmd_reply.h"
#include "refmem_sync.h"
#include "refmem_vector_table.h"

#include <stdbool.h>
#include <stdint.h>

/* Internal command-reply helpers (N5.0.2b).  The implementation lives in
 * refmem_cmd_reply.c; only this private header is shared inside the domain. */
void distributed_refmem_cmd_reply_arm(uint32_t command_seq,
                                      uint32_t origin_slot,
                                      uint32_t state,
                                      uint32_t reason);
bool distributed_refmem_cmd_reply_pending(void);
void distributed_refmem_cmd_reply_store(uint32_t source_slot,
                                        uint16_t seq16,
                                        uint8_t flags,
                                        uint8_t reason);
void distributed_refmem_cmd_reply_service(void);
bool distributed_refmem_cmd_reply_tx_get(refmem_cmd_reply_t *reply);
uint32_t distributed_refmem_cmd_reply_target_mask(void);
void distributed_refmem_cmd_reply_note_published(void);

/* Shared node region accessor used by the node-control implementation. */
refmem_vector_node_region_t *distributed_refmem_node_region(uint32_t node_id);

#endif
