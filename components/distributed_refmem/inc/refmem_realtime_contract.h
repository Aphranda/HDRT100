#ifndef REFMEM_REALTIME_CONTRACT_H
#define REFMEM_REALTIME_CONTRACT_H

#include <stdbool.h>
#include <stdint.h>

#include "refmem_application_model.h"
#include "refmem_slot_claim.h"

typedef enum {
    REFMEM_RT_CONTRACT_OK = 0u,
    REFMEM_RT_CONTRACT_BAD_ARGUMENT = 1u,
    REFMEM_RT_CONTRACT_BOARD_NOT_FOUND = 2u,
    REFMEM_RT_CONTRACT_MISSING_BASELINE = 3u,
    REFMEM_RT_CONTRACT_MISSING_CAPABILITY = 4u,
    REFMEM_RT_CONTRACT_MISSING_IO = 5u,
    REFMEM_RT_CONTRACT_MISSING_IP_CORE = 6u,
    REFMEM_RT_CONTRACT_SLOT_CLAIM_INVALID = 7u,
} refmem_realtime_contract_result_t;

typedef struct {
    uint32_t node_id;
    uint32_t board_id;
    uint32_t instance_id;
    uint32_t resource_claim;
    uint32_t io_claim;
    uint32_t ip_core_claim;
    uint32_t target_capability_mask;
    uint32_t target_io_constraint_mask;
    uint32_t target_ip_core_mask;
    uint32_t required_capability_mask;
    uint32_t missing_capability_mask;
    uint32_t missing_io_mask;
    uint32_t missing_ip_core_mask;
    uint32_t time_budget_us;
    uint32_t valid;
    uint32_t result;
} refmem_realtime_contract_t;

/* Optional local projection for the autonomous origin candidate. This is
 * not a System Pack wire table and never upgrades a diagnostic trial to RUN.
 * TDMA supplies board-owned endpoints; Calibration supplies window facts. */
#define REFMEM_RT_ORIGIN_EXECUTOR (1u << 0u)
#define REFMEM_RT_ORIGIN_SNIFFER (1u << 1u)
#define REFMEM_RT_ORIGIN_REQUIRED_FEATURES \
    (REFMEM_RT_ORIGIN_EXECUTOR | REFMEM_RT_ORIGIN_SNIFFER)
#define REFMEM_RT_ORIGIN_PRODUCT_RAM (1u << 0u)
#define REFMEM_RT_ORIGIN_PRODUCT_WCET (1u << 1u)
#define REFMEM_RT_ORIGIN_PRODUCT_TIMING (1u << 2u)

typedef struct {
    uint32_t features;
    uint32_t persona;
    uint32_t clk_sys_hz;
    uint32_t physical_bytes;
    uint32_t resource_mask;
    uint32_t dma_mask;
    uint32_t tx_pio;
    uint32_t rx_pio;
    uint32_t product_reject_mask;
} refmem_realtime_origin_capability_t;

typedef struct {
    uint32_t model_epoch;
    uint32_t foundation_crc32;
    uint32_t deployment_crc32;
    uint32_t owner_instance;
    uint32_t node_id;
    uint32_t board_id;
    uint32_t gate_mask;
    uint32_t diagnostic_valid;
    uint32_t product_valid;
    uint32_t product_reject_mask;
    refmem_realtime_origin_capability_t capability;
} refmem_realtime_origin_admission_t;

/* Core0 projection of active NodeLoad/SlotClaim/DeploymentGate. Core1 only
 * reads the epoch to revoke an accepted immutable projection. */
uint32_t refmem_realtime_contract_origin_model_epoch(void);
bool refmem_realtime_contract_admit_origin_trial(
    const refmem_realtime_origin_capability_t *capability,
    uint32_t foundation_crc32, refmem_realtime_origin_admission_t *admission);

uint32_t refmem_realtime_contract_resource_capability_mask(uint32_t resource_claim);
uint32_t refmem_realtime_contract_io_capability_mask(uint32_t io_claim);
uint32_t refmem_realtime_contract_ip_capability_mask(uint32_t ip_core_claim);
uint32_t refmem_realtime_contract_transport_resource_claim(uint32_t transport);
uint32_t refmem_realtime_contract_transport_io_claim(uint32_t transport);
uint32_t refmem_realtime_contract_transport_ip_core_claim(uint32_t transport);
bool refmem_realtime_contract_derive_from_claim_map(
    const refmem_node_load_entry_t *load,
    const refmem_fb_instance_entry_t *instance,
    const refmem_app_node_entry_t *node,
    const refmem_slot_claim_map_t *claim_map,
    refmem_realtime_contract_t *contract);

#endif
