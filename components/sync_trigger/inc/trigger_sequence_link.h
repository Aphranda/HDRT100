#ifndef TRIGGER_SEQUENCE_LINK_H
#define TRIGGER_SEQUENCE_LINK_H
#include <stdbool.h>
#include <stdint.h>
#include "trigger_sequence_service.h"
#include "trigger_sequence_link_protocol.h"

typedef struct {
    bool enabled;
    uint32_t dut_slot, vna_slot;
    uint32_t ready_input, trigger_output_mask, pulse_us, timeout_ms;
    bool falling;
} trigger_sequence_link_config_t;
typedef struct {
    trigger_sequence_link_config_t config;
    uint32_t phase, error, binding_epoch, model_epoch;
    uint32_t run_id, generation, step, tx_fragments, rx_messages, rejected;
    uint32_t triggers, ready, completed;
    uint32_t repeat_count;
    uint32_t exchange_id;
} trigger_sequence_link_status_t;
/* Core0-only; called by RefMem owner and SCPI on the serialized data plane.
 * This first implementation binds two local roles; physical topology addresses
 * remain transport-owned and are not confused with RefMem logical role slots. */
bool trigger_sequence_link_configure(const trigger_sequence_link_config_t *config);
trigger_sequence_service_result_t trigger_sequence_link_next(void);
void trigger_sequence_link_get_status(trigger_sequence_link_status_t *status);
void trigger_sequence_link_service(void);
bool trigger_sequence_link_tx_fragment(uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE]);
void trigger_sequence_link_rx_fragment(uint32_t physical_source,
    const uint8_t fragment[TRIGGER_SEQUENCE_LINK_FRAGMENT_SIZE]);
#endif
