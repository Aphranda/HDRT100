#ifndef DISTRIBUTED_REFMEM_TIME_CRITICAL_H
#define DISTRIBUTED_REFMEM_TIME_CRITICAL_H

/* RAM-resident placement for the pieces TDMA Core1 runs with the ring clock
 * live. These were file-local macros of distributed_refmem.c; the first-class
 * position acknowledgement unit carries its own time-critical entry points, so
 * the macros live in a header both units can include. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define DISTRIBUTED_REFMEM_TIME_CRITICAL(name) __not_in_flash_func(name)
#define DISTRIBUTED_REFMEM_RAM_TEXT __attribute__((section(".time_critical.refmem_position")))
#else
#define DISTRIBUTED_REFMEM_TIME_CRITICAL(name) name
#define DISTRIBUTED_REFMEM_RAM_TEXT
#endif

#endif /* DISTRIBUTED_REFMEM_TIME_CRITICAL_H */
