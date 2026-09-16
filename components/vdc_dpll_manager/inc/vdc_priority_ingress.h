#ifndef VDC_PRIORITY_INGRESS_H
#define VDC_PRIORITY_INGRESS_H

#include "tdma_priority_rx.h"
#include "vdc_priority_codec.h"

/* Raw transport ingress only. Carrier identity is not a local event identity;
 * IRQ entry time is not a wire edge or a VDC timestamp. No control grant. */
typedef struct {
    uint32_t schema, active, epoch, have_record;
    uint32_t service_count, copied_count, skipped_carrier_count, duplicate_polls;
    uint32_t copy_retries, empty_polls, order_rejects, clock_failures;
    uint32_t last_class, body_last_cycles, body_max_cycles, timing_samples;
    uint32_t typed_decode_count, typed_reject_count;
    vdc_priority_codec_record_t typed_record;
    /* Last samples use UINT32_MAX/UINT64_MAX when that measurement failed;
     * maxima only include valid observations and are not WCET guarantees. */
    uint64_t arrival_last_cycles, arrival_max_cycles;
    tdma_priority_rx_record_t record;
} vdc_priority_ingress_snapshot_t;

/* Core1 alone writes; readers make one guarded atomic-word copy. The last
 * raw record remains available after STOP, and is retired on the next active
 * RX epoch. Only stopped maintenance exposes this through SCPI. */
bool vdc_dpll_manager_get_priority_ingress(vdc_priority_ingress_snapshot_t *out);

#endif
