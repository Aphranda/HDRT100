#ifndef TRIGGER_TIME_PROJECTION_H
#define TRIGGER_TIME_PROJECTION_H

#include "vdc_domain.h"

/* T2-only equivalent of the Domain's local-to-output projection. Ordinary
 * representable intermediates take a short path; wide/cancelling cases use
 * the existing Domain implementation. Does not alter the model or grant
 * clock precision. False preserves *output_ns, matching the Domain API. */
bool trigger_time_projection_fast(const vdc_dco_control_t *model,
    uint64_t local_ns, uint64_t *output_ns);

#endif
