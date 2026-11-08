#include "resource_arbiter.h"

/* Host unit tests exercise the TDMA binding without linking the full OtaAO.
 * Mirror the production deferred-release symbol with the direct arbiter call;
 * the binding tests do not create a FlashTransaction owner. */
void ota_ao_request_admission_release(void)
{
    resource_arbiter_release_ota_admission();
}
