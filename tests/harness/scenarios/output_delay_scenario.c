/* Host scenario for the output-delay configuration unit: exercises the real
 * production code with link-time doubles for core id, TDMA metadata and
 * product-config storage. */

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "vdc_dpll_manager.h"

extern int32_t fake_output_compensation_ns;
extern int fake_output_compensation_writes;
extern bool fake_feedback_session;

int main(void)
{
    int32_t value = 0;

    /* Boot default: the unit starts from the product-config default. */
    assert(vdc_dpll_manager_get_output_delay_ns(&value));
    assert(value == PRODUCT_CONFIG_DPLL_OUTPUT_COMPENSATION_DEFAULT_NS);
    assert(!vdc_dpll_manager_get_output_delay_ns(NULL));

    /* Core0-only writer publishes through the stopped-metadata callback. */
    assert(vdc_dpll_manager_set_output_delay_ns(500));
    assert(vdc_dpll_manager_get_output_delay_ns(&value));
    assert(value == 500);

    /* Store writes the current value back to product config. */
    fake_output_compensation_writes = 0;
    assert(vdc_dpll_manager_store_output_delay());
    assert(fake_output_compensation_writes == 1);
    assert(fake_output_compensation_ns == 500);

    /* Recall pulls the stored value. */
    fake_output_compensation_ns = 1234;
    assert(vdc_dpll_manager_recall_output_delay());
    assert(vdc_dpll_manager_get_output_delay_ns(&value));
    assert(value == 1234);

    /* Default restores the compiled-in default. */
    assert(vdc_dpll_manager_default_output_delay());
    assert(vdc_dpll_manager_get_output_delay_ns(&value));
    assert(value == PRODUCT_CONFIG_DPLL_OUTPUT_COMPENSATION_DEFAULT_NS);

    /* Boot restore is refused while a feedback session is active. */
    fake_feedback_session = true;
    assert(!output_delay_init_from_product_config());
    fake_feedback_session = false;

    printf("output-delay scenario: OK\n");
    return 0;
}
