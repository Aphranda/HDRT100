#ifndef TDMA_FROZEN_GEOMETRY_H
#define TDMA_FROZEN_GEOMETRY_H
#include <stdbool.h>
#include <stdint.h>

/* Same-boot diagnostic lifecycle only. DMA-relative training is not a proof
 * of the next ARM's CS-relative prefix, physical identity or timestamp. */
enum {
    TDMA_GEOMETRY_EMPTY, TDMA_GEOMETRY_TRAINING, TDMA_GEOMETRY_CANDIDATE,
    TDMA_GEOMETRY_FROZEN, TDMA_GEOMETRY_SELECTING, TDMA_GEOMETRY_SELECTED,
    TDMA_GEOMETRY_RETIRED, TDMA_GEOMETRY_REJECTED
};
enum {
    TDMA_GEOMETRY_OK, TDMA_GEOMETRY_STALE, TDMA_GEOMETRY_CONTENT,
    TDMA_GEOMETRY_INCOMPLETE_STOP, TDMA_GEOMETRY_UNTRAINED,
    TDMA_GEOMETRY_ARM_FAILED, TDMA_GEOMETRY_PERSONA,
    TDMA_GEOMETRY_CLOCK, TDMA_GEOMETRY_EXHAUSTED, TDMA_GEOMETRY_STOPPED
};
typedef struct {
    uint32_t version, state, reason, generation, requested_generation;
    uint32_t source_config_seq, bound_config_seq;
    uint64_t source_arm_epoch, source_observation_epoch;
    uint64_t bound_arm_epoch, bound_observation_epoch;
    uint32_t physical_bytes, dma_byte_shift, dma_bit_shift, training_samples;
    uint32_t clk_sys_hz, persona, node_count, local_slot, reference_slot;
    uint32_t topology_generation, topology_crc32, calibration_generation;
    uint32_t schedule_crc32, operating_profile_crc32;
    uint32_t source_map_generation, bound_map_generation;
} tdma_frozen_geometry_snapshot_t;

bool tdma_pio_spi_phys_get_frozen_geometry(tdma_frozen_geometry_snapshot_t *out);
/* Core1 only, called after the adapter's complete STOP, including station ACK. */
void tdma_pio_spi_phys_geometry_stopped(void *context);
#endif
