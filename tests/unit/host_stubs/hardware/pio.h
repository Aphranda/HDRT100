#ifndef HOST_STUB_HARDWARE_PIO_H
#define HOST_STUB_HARDWARE_PIO_H

#include <stdint.h>

typedef struct host_pio_hw *PIO;

#define pio0 ((PIO)(uintptr_t)0x1000u)
#define pio1 ((PIO)(uintptr_t)0x2000u)
#define pio2 ((PIO)(uintptr_t)0x3000u)

#endif
