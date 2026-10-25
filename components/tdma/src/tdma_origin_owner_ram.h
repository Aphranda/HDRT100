#ifndef TDMA_ORIGIN_OWNER_RAM_H
#define TDMA_ORIGIN_OWNER_RAM_H

/* RAM placement for the origin hand-off path (Core1). The owner unit and
 * its origin translation unit both place functions, so the macro moved out
 * of tdma_runtime_origin.inc into this header. */
#if defined(PICO_ON_DEVICE) && PICO_ON_DEVICE
#include "pico.h"
#define TDMA_ORIGIN_OWNER_RAM __not_in_flash("tdma_origin_owner")
#else
#define TDMA_ORIGIN_OWNER_RAM
#endif

#endif /* TDMA_ORIGIN_OWNER_RAM_H */
