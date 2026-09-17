#ifndef VDC_OUTPUT_DELAY_H
#define VDC_OUTPUT_DELAY_H

#include <stdbool.h>
#include <stdint.h>

/* Requested output-only compensation in ns. Positive delays the physical
 * edge, negative advances it. Independent of MATCH link delay. Core0 changes
 * only under STOP; the future output owner latches once per output start. */
bool vdc_dpll_manager_get_output_delay_ns(int32_t *value);
bool vdc_dpll_manager_set_output_delay_ns(int32_t value);
bool vdc_dpll_manager_default_output_delay(void);
bool vdc_dpll_manager_recall_output_delay(void);
bool vdc_dpll_manager_store_output_delay(void);

#endif
