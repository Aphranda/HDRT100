#ifndef REFMEM_CMD_LOAD_H
#define REFMEM_CMD_LOAD_H

#include <stdbool.h>
#include <stdint.h>

#include "tdma_pio_spi_ring_adapter.h"
#include "refmem_command.h"

#ifdef __cplusplus
extern "C" {
#endif

/* REFMEM_CMD load registration and AO-side event intake.
 *
 * Transport only produces raw CONFIG_CONTROL payloads; this module is the
 * RefMem AO edge that decodes them and owns the single command slot/ACK. */
bool refmem_cmd_load_init(tdma_pio_spi_ring_adapter_t *adapter);
bool refmem_cmd_load_submit(const uint8_t *wire, uint32_t wire_size);
bool refmem_cmd_load_submit_type(uint8_t type,
                                 const uint8_t *wire,
                                 uint32_t wire_size);
void refmem_cmd_load_service(void);
bool refmem_cmd_load_submit_fragments(
    const refmem_command_snapshot_t *snapshot,
    const uint32_t *params);

typedef struct {
    uint32_t load_initialized;
    uint32_t submitted;
    uint32_t served;
    uint32_t rejected;
    uint32_t window_closed;
    uint32_t rx_events;
    uint32_t decoded;
    uint32_t posted;
    uint32_t acked;
    uint32_t tx_ok;
    uint32_t tx_fail;
    uint32_t tx_stage;
    uint32_t rx_seen;
    uint32_t rx_delivered;
    uint32_t rx_drop_count;
    uint32_t last_rx_packet_size;
    uint32_t last_rx_payload_class;
    uint32_t rx_config_control;
    uint32_t rx_process_image;
    uint32_t build_class;
    uint32_t build_size;
    uint32_t build_load;
    uint32_t inject_attempts;
    uint32_t inject_ok;
    uint32_t parser_registered;
    uint32_t parser_unknown;
    uint32_t refmem_parser_ok;
    uint32_t refmem_parser_reject;
    uint32_t t2_static_ok;
    uint32_t t2_static_reject;
    uint32_t t2_static_applied;
    uint32_t t2_static_last_config;
    uint32_t t2_static_last_value;
    uint32_t t2_dynamic_ok;
    uint32_t t2_dynamic_reject;
    uint32_t t2_dynamic_applied;
    uint32_t t2_dynamic_last_target;
    uint32_t t2_dynamic_last_run;
    uint32_t t2_dynamic_last_config;
    uint32_t t2_dynamic_last_value;
    uint32_t frag_ok;
    uint32_t frag_reject;
    uint32_t frag_complete;
    uint32_t frag_submit_fail;
    uint32_t frag_pending;
    uint32_t frag_pending_index;
    uint32_t frag_pending_count;
    /* N3.2.1 transport diagnostics (read-only). */
    uint32_t queue_depth;
    uint32_t queue_enqueued;
    uint32_t queue_dequeued;
    uint32_t queue_full;
    uint32_t rx_chunk_rejects;
    uint32_t inject_launched;
    uint32_t inject_dropped_unlaunched;
    uint32_t inject_new_sequence;
    uint32_t inject_repeated_sequence;
    uint32_t inject_last_sequence;
    uint32_t beacon_frames;
    uint32_t rx_polls;
    uint32_t rx_seen_chunks;
    uint32_t rx_accepted_chunks;
    uint32_t rx_dedup_sequence;
    uint32_t rx_bad_mailbox;
    uint32_t rx_last_sequence;
    uint32_t rx_sequence_cursor;
    uint32_t rx_restarts;
    uint32_t rx_duplicate_chunks;
} refmem_cmd_load_status_t;

bool refmem_cmd_load_get_status(refmem_cmd_load_status_t *status);

#ifdef __cplusplus
}
#endif

#endif /* REFMEM_CMD_LOAD_H */
