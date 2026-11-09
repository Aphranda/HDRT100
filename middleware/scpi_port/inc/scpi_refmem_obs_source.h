#ifndef SCPI_REFMEM_OBS_SOURCE_H
#define SCPI_REFMEM_OBS_SOURCE_H

#include <stdbool.h>
#include <stdint.h>

/* M2b local OBS sample source: fills the contract word array and presence
 * mask for the compact publish path.  Returns false when the contract cannot
 * be sampled; the publisher then keeps the baseline diagnostic field. */
bool scpi_refmem_obs_sample_local(uint32_t *values, uint32_t *mask,
                                  uint32_t count);

#endif
