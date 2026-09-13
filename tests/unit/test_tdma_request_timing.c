#include <assert.h>
#include "tdma_service_timing.h"
#define main adapter_legacy_main
#include "test_tdma_pio_spi_ring_adapter.c"
#undef main

static uint64_t timing_ticks;
uint64_t vdc_timestamp_clock_read_ticks64(void) { return ++timing_ticks; }
uint32_t vdc_timestamp_clock_tick_hz(void) { return 250000000u; }
#include "../../components/tdma/src/tdma_service_timing.c"

int main(void) { return test_rx_prepare_cases(); }
