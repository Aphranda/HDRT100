#ifndef HOST_SHIM_HARDWARE_PIO_H
#define HOST_SHIM_HARDWARE_PIO_H

/* Host stand-in for the Pico SDK PIO header: just enough types for the board
 * and domain headers to compile. PIO instructions never execute on host. */

#include <stdbool.h>
#include <stdint.h>

typedef struct pio_hw { uint32_t reserved; } pio_hw_t;
typedef pio_hw_t *PIO;

extern PIO pio0;
extern PIO pio1;
extern PIO pio2;

typedef struct {
    const uint16_t *instructions;
    uint8_t length;
    int8_t origin;
} pio_program_t;

enum pio_interrupt_source {
    pis_interrupt0 = 0,
    pis_interrupt1,
    pis_interrupt2,
    pis_interrupt3,
};

#endif /* HOST_SHIM_HARDWARE_PIO_H */
