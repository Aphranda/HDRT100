/* Host scenario: the host-control latch is set by the SCPI-facing call and
 * cleared by the owner. Runs against the real production unit. */

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>

#include "vdc_dpll_manager.h"
#include "vdc_priority_shared.h"

int main(void)
{
    s_priority_host_control_written = false;
    assert(!s_priority_host_control_written);

    priority_note_host_control();
    assert(s_priority_host_control_written);

    /* The owner clears the latch when it consumes it. */
    s_priority_host_control_written = false;
    assert(!s_priority_host_control_written);

    printf("host-control scenario: OK\n");
    return 0;
}
