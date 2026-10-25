#ifndef HOST_SHIM_TIME_COMPAT_H
#define HOST_SHIM_TIME_COMPAT_H

/* The firmware build gets `time_us_32` from the SDK umbrella headers that the
 * domain sources rely on. The host build has no such umbrella, so declare it
 * here and force-include this file for the unit compile checks. The symbol is
 * provided by the SDK host platform (or by a scenario double). */

#include <stdint.h>

uint32_t time_us_32(void);

#endif /* HOST_SHIM_TIME_COMPAT_H */
