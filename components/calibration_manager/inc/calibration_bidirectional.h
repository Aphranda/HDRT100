#ifndef CALIBRATION_BIDIRECTIONAL_H
#define CALIBRATION_BIDIRECTIONAL_H

#include <stdbool.h>
#include <stdint.h>

/* The four edge timestamps are local to the board that captured each edge.
 * For a single-board loopback all four values share one clock domain; for a
 * board-to-board sample t1/t4 belong to A and t2/t3 belong to B. */
typedef struct {
    uint64_t t1_clk_tx;
    uint64_t t2_clk_rx;
    uint64_t t3_data_tx;
    uint64_t t4_data_rx;
    uint32_t train_epoch;
    uint32_t train_sequence;
    uint32_t persona_generation;
    uint32_t sample_flags;
    uint32_t edge_mask;
    uint32_t dma_status;
    uint32_t bias_generation;
    uint32_t topology_generation;
    int64_t endpoint_bias_ns;
    uint64_t clock_rate_error_bound_ns;
    bool reference_loopback;
} calibration_bidirectional_sample_t;

typedef struct {
    uint32_t required_sample_flags;
    uint32_t required_edge_mask;
    uint32_t expected_persona_generation;
    uint32_t expected_topology_generation;
    uint64_t max_clock_rate_error_bound_ns;
    bool require_bias_generation;
    bool require_fresh_topology;
    bool allow_reference_loopback;
} calibration_bidirectional_gate_t;

typedef enum {
    CALIBRATION_BIDIRECTIONAL_REJECT_NONE = 0u,
    CALIBRATION_BIDIRECTIONAL_REJECT_BAD_ARGUMENT = 1u,
    CALIBRATION_BIDIRECTIONAL_REJECT_EDGE_ORDER = 2u,
    CALIBRATION_BIDIRECTIONAL_REJECT_MISSING_EDGE = 3u,
    CALIBRATION_BIDIRECTIONAL_REJECT_SAMPLE_FLAGS = 4u,
    CALIBRATION_BIDIRECTIONAL_REJECT_PERSONA = 5u,
    CALIBRATION_BIDIRECTIONAL_REJECT_TOPOLOGY = 6u,
    CALIBRATION_BIDIRECTIONAL_REJECT_BIAS = 7u,
    CALIBRATION_BIDIRECTIONAL_REJECT_CLOCK_RATE = 8u,
    CALIBRATION_BIDIRECTIONAL_REJECT_DMA = 9u,
    CALIBRATION_BIDIRECTIONAL_REJECT_NEGATIVE_PATH = 10u,
    CALIBRATION_BIDIRECTIONAL_REJECT_LOOPBACK_POLICY = 11u,
} calibration_bidirectional_reject_reason_t;

typedef struct {
    uint64_t residence_ns;
    uint64_t raw_path_sum_ns;
    int64_t corrected_path_sum_ns;
    int64_t delay_estimate_ns;
    uint64_t clock_rate_error_bound_ns;
    uint32_t reject_reason;
    bool reference_accepted;
    bool active_eligible;
} calibration_bidirectional_result_t;

/* Evidence flags are deliberately local to the calibration domain. The TDMA
 * adapter maps its transport/timestamp flags into these bits at the owner
 * boundary. */
#define CALIBRATION_BIDIRECTIONAL_FLAG_HARDWARE_LATCHED (1u << 0u)
#define CALIBRATION_BIDIRECTIONAL_FLAG_DIAGNOSTIC_ONLY (1u << 1u)
#define CALIBRATION_BIDIRECTIONAL_FLAG_SYNC_MATCH (1u << 2u)
#define CALIBRATION_BIDIRECTIONAL_FLAG_DMA_COMPLETE (1u << 3u)
#define CALIBRATION_BIDIRECTIONAL_FLAG_BIAS_VALID (1u << 4u)
#define CALIBRATION_BIDIRECTIONAL_FLAG_TOPOLOGY_FRESH (1u << 5u)
#define CALIBRATION_BIDIRECTIONAL_FLAG_REFERENCE_LOOPBACK (1u << 6u)

#define CALIBRATION_BIDIRECTIONAL_EDGE_CLK_TX (1u << 0u)
#define CALIBRATION_BIDIRECTIONAL_EDGE_CLK_RX (1u << 1u)
#define CALIBRATION_BIDIRECTIONAL_EDGE_DATA_TX (1u << 2u)
#define CALIBRATION_BIDIRECTIONAL_EDGE_DATA_RX (1u << 3u)
#define CALIBRATION_BIDIRECTIONAL_EDGE_ALL \
    (CALIBRATION_BIDIRECTIONAL_EDGE_CLK_TX | \
     CALIBRATION_BIDIRECTIONAL_EDGE_CLK_RX | \
     CALIBRATION_BIDIRECTIONAL_EDGE_DATA_TX | \
     CALIBRATION_BIDIRECTIONAL_EDGE_DATA_RX)

bool calibration_bidirectional_evaluate(
    const calibration_bidirectional_sample_t *sample,
    const calibration_bidirectional_gate_t *gate,
    calibration_bidirectional_result_t *result);

#endif
