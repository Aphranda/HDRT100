#include "vdc_dpll_manager.h"

#include "vdc_priority_shared.h"
/* Host-side writers of the priority lanes (the SCPI entry points behind the
 * TDMA stopped-metadata boundary) raise this latch. The VDC owner reads it to
 * learn that a session is driven by the host, and then leaves every lane of
 * that session alone: it never overwrites an explicit generation, never
 * revives a lane the host zeroed or switched back to PROBE, and never resets a
 * matcher the host is measuring (VDC-LOCKPUB-001①). A session nobody arms
 * stays the owner's to arm, so a load no longer has to configure the base.
 *
 * Only an arming write (a non-zero generation, TYPED_FOLLOW, or an enabled
 * phase) counts as control: a zero write is a disarm, which the owner honours
 * by simply not re-arming (it is a no-op on the latch). */
bool s_priority_host_control_written;

void priority_note_host_control(void)
{
    s_priority_host_control_written = true;
}
