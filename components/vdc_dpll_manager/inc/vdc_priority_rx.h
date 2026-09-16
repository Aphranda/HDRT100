#ifndef VDC_PRIORITY_RX_H
#define VDC_PRIORITY_RX_H

#include "tdma_priority_rx.h"
#include "vdc_priority_codec.h"

#define VDC_PRIORITY_RX_SCHEMA 1u
#define VDC_PRIORITY_RX_EMPTY 0u
#define VDC_PRIORITY_RX_OTHER_CLASS 1u
#define VDC_PRIORITY_RX_UNIQUE 2u
#define VDC_PRIORITY_RX_DUPLICATE 3u
#define VDC_PRIORITY_RX_CODEC_REJECT 4u
#define VDC_PRIORITY_RX_SEQUENCE_REJECT 5u
#define VDC_PRIORITY_RX_CONFLICT 6u
#define VDC_PRIORITY_RX_RETIRED 7u

/* Transport-only evidence: no control grant, event match, or lock claim.
 * epoch is local RX lane lifetime, not cross-board binding_generation.
 * Counts saturate and reset on lane epoch change. carrier_count includes all
 * accepted owner callbacks; typed_accept_count excludes typed rejects.
 * Duplicate/unique compare only the preceding accepted source identity
 * (source slot, binding generation, complete event sequence), not history.
 * Same identity with changed timestamp/uncertainty/flags is a conflict.
 * Retained fields describe the last successful typed carrier; last_status
 * describes the latest callback, which may have left those fields unchanged. */
typedef struct {
    uint32_t schema, active, epoch, have_record;
    uint32_t carrier_count, typed_accept_count, typed_reject_count;
    uint32_t duplicate_count, unique_count, conflict_count, last_status;
    uint32_t source_slot, target_mask, carrier_sequence;
    uint64_t irq_entry_ticks; /* Handler entry clk_sys ticks, not a wire edge. */
    vdc_priority_codec_record_t typed_record;
} vdc_priority_rx_snapshot_t;

/* Sole writer: Core1 capture IRQ, or STOP owner with capture IRQ disabled.
 * TDMA supplies an already CRC/identity-validated, successfully published
 * record with its post-publication lane epoch. Borrowed pointer, no retention.
 * NULL retires; evidence remains readable until a different nonzero epoch.
 * A retired epoch cannot reactivate. No hardware FIFO access or allocation. */
void vdc_priority_rx_core1(const tdma_priority_rx_record_t *record);

/* One atomic-word snapshot attempt; false leaves *out unchanged. Reader must
 * not span a full guard cycle (2^31 publications). For STOP diagnostics only. */
bool vdc_dpll_manager_get_priority_rx(vdc_priority_rx_snapshot_t *out);

/* Core1 foreground handoff: one guarded copy requiring active and a retained
 * successful typed record. No consumption or control grant; caller must
 * bind the local/remote lifetimes, match the source event and check age.
 * Repeated carriers do not create a fresh source event. All failures preserve
 * out. STOP/rebase revoke this read in the same guard window. */
bool vdc_priority_rx_copy_live(vdc_priority_rx_snapshot_t *out);

#endif
