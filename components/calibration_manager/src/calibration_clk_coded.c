#include "calibration_clk_coded.h"

#include <string.h>

void calibration_clk_coded_store_init(calibration_clk_coded_store_t *store)
{
    if (store == NULL) return;
    memset(store, 0, sizeof(*store));
    store->snapshot.version = CALIBRATION_CLK_CODED_SNAPSHOT_VERSION;
    store->snapshot.state = CALIBRATION_CLK_CODED_IDLE;
}

bool calibration_clk_coded_publish_core1(
    calibration_clk_coded_store_t *store,
    const calibration_clk_coded_snapshot_t *snapshot)
{
    if (store == NULL || snapshot == NULL ||
        snapshot->version != CALIBRATION_CLK_CODED_SNAPSHOT_VERSION ||
        snapshot->state > CALIBRATION_CLK_CODED_REJECTED) {
        return false;
    }
    (void)__atomic_add_fetch(&store->guard, 1u, __ATOMIC_ACQ_REL);
    store->snapshot = *snapshot;
    (void)__atomic_add_fetch(&store->guard, 1u, __ATOMIC_RELEASE);
    return true;
}

bool calibration_clk_coded_get_snapshot(
    const calibration_clk_coded_store_t *store,
    calibration_clk_coded_snapshot_t *snapshot)
{
    if (store == NULL || snapshot == NULL) return false;
    for (uint32_t attempt = 0u;
         attempt < CALIBRATION_CLK_CODED_SNAPSHOT_READ_ATTEMPTS; attempt++) {
        const uint32_t begin = __atomic_load_n(&store->guard, __ATOMIC_ACQUIRE);
        if ((begin & 1u) != 0u) continue;
        *snapshot = store->snapshot;
        const uint32_t end = __atomic_load_n(&store->guard, __ATOMIC_ACQUIRE);
        if (begin == end && (end & 1u) == 0u) return true;
    }
    return false;
}

bool calibration_clk_coded_begin_coarse_core1(
    calibration_clk_coded_store_t *store,
    const calibration_clk_coded_request_t *request)
{
    if (store == NULL || request == NULL ||
        request->logical_slot > 7u || request->train_epoch > 0xFFu ||
        request->sample_period_ns == 0u ||
        request->coarse_min_sample >= request->coarse_max_sample ||
        request->coarse_max_sample - request->coarse_min_sample + 1u >
            CALIBRATION_CLK_CORRELATION_MAX_LAGS) {
        return false;
    }
    const calibration_clk_marker_config_t config = {
        .version = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
        .codebook_id = (uint8_t)request->codebook_id,
        .epoch = (uint8_t)request->train_epoch,
        .master_slot = (uint8_t)request->logical_slot,
        .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
    };
    if (!calibration_clk_marker_config_valid(&config)) return false;

    calibration_clk_coded_snapshot_t next;
    memset(&next, 0, sizeof(next));
    next.version = CALIBRATION_CLK_CODED_SNAPSHOT_VERSION;
    next.state = CALIBRATION_CLK_CODED_CLOCK_COARSE;
    next.flags = CALIBRATION_CLK_CODED_FLAG_DIAGNOSTIC_ONLY |
                 CALIBRATION_CLK_CODED_FLAG_COARSE_BRACKET_VALID;
    next.board_unique_id = request->board_unique_id;
    next.build_id = request->build_id;
    next.logical_slot = request->logical_slot;
    next.train_epoch = request->train_epoch;
    next.train_sequence = request->train_sequence;
    next.calibration_generation = request->calibration_generation;
    next.topology_generation = request->topology_generation;
    next.topology_crc32 = request->topology_crc32;
    next.profile_crc32 = request->profile_crc32;
    next.schedule_crc32 = request->schedule_crc32;
    next.baud_hz = request->baud_hz;
    next.codebook_id = request->codebook_id;
    next.sample_period_ns = request->sample_period_ns;
    next.coarse_min_sample = request->coarse_min_sample;
    next.coarse_max_sample = request->coarse_max_sample;
    return calibration_clk_coded_publish_core1(store, &next);
}

bool calibration_clk_coded_reject_request_core1(
    calibration_clk_coded_store_t *store,
    const calibration_clk_coded_request_t *request,
    uint32_t reject_reason)
{
    if (reject_reason == CALIBRATION_CLK_CODED_REJECT_NONE ||
        !calibration_clk_coded_begin_coarse_core1(store, request)) {
        return false;
    }
    calibration_clk_coded_snapshot_t next;
    if (!calibration_clk_coded_get_snapshot(store, &next)) return false;
    next.state = CALIBRATION_CLK_CODED_REJECTED;
    next.reject_reason = reject_reason;
    return calibration_clk_coded_publish_core1(store, &next);
}

bool calibration_clk_coded_stop_core1(
    calibration_clk_coded_store_t *store)
{
    calibration_clk_coded_snapshot_t next;
    if (store == NULL ||
        !calibration_clk_coded_get_snapshot(store, &next)) {
        return false;
    }
    next.state = CALIBRATION_CLK_CODED_IDLE;
    return calibration_clk_coded_publish_core1(store, &next);
}

static bool calibration_clk_coded_evidence_matches(
    const calibration_clk_coded_snapshot_t *coarse,
    const calibration_clk_coded_evidence_t *evidence)
{
    return evidence->train_epoch == coarse->train_epoch &&
           evidence->train_sequence == coarse->train_sequence &&
           evidence->topology_generation == coarse->topology_generation &&
           evidence->topology_crc32 == coarse->topology_crc32 &&
           evidence->profile_crc32 == coarse->profile_crc32 &&
           evidence->schedule_crc32 == coarse->schedule_crc32;
}

static bool calibration_clk_coded_publish_reject(
    calibration_clk_coded_store_t *store,
    calibration_clk_coded_snapshot_t *next,
    uint32_t reason)
{
    next->state = CALIBRATION_CLK_CODED_REJECTED;
    next->reject_reason = reason;
    return calibration_clk_coded_publish_core1(store, next);
}

bool calibration_clk_coded_process_core1(
    calibration_clk_coded_store_t *store,
    calibration_clk_coded_workspace_t *workspace,
    const calibration_clk_coded_evidence_t *evidence,
    const calibration_clk_correlation_gate_t *correlation_gate)
{
    if (store == NULL || workspace == NULL || evidence == NULL ||
        correlation_gate == NULL || evidence->capture_words == NULL) {
        return false;
    }
    calibration_clk_coded_snapshot_t next;
    if (!calibration_clk_coded_get_snapshot(store, &next)) return false;
    if (next.state != CALIBRATION_CLK_CODED_CLOCK_COARSE) {
        return calibration_clk_coded_publish_reject(
            store, &next, CALIBRATION_CLK_CODED_REJECT_BAD_STATE);
    }
    next.state = CALIBRATION_CLK_CODED_CLOCK_CODED;
    next.capture_origin_tick = evidence->capture_origin_tick;
    next.capture_sample_count = evidence->capture_sample_count;
    next.timing_field_tx_origin_sample =
        evidence->timing_field_tx_origin_sample;
    next.tx_dma_count = evidence->tx_dma_count;
    next.rx_dma_count = evidence->rx_dma_count;
    next.dma_overrun_count = evidence->dma_overrun_count;
    next.pio_stall_count = evidence->pio_stall_count;
    next.flags |= evidence->flags &
                  (CALIBRATION_CLK_CODED_FLAG_TX_DMA_COMPLETE |
                   CALIBRATION_CLK_CODED_FLAG_RX_DMA_COMPLETE);

    if (!calibration_clk_coded_evidence_matches(&next, evidence)) {
        return calibration_clk_coded_publish_reject(
            store, &next, CALIBRATION_CLK_CODED_REJECT_GENERATION);
    }
    if (correlation_gate->min_lag_sample != next.coarse_min_sample ||
        correlation_gate->max_lag_sample != next.coarse_max_sample) {
        return calibration_clk_coded_publish_reject(
            store, &next, CALIBRATION_CLK_CODED_REJECT_COARSE_BRACKET);
    }
    const uint32_t dma_flags =
        CALIBRATION_CLK_CODED_FLAG_TX_DMA_COMPLETE |
        CALIBRATION_CLK_CODED_FLAG_RX_DMA_COMPLETE;
    if ((next.flags & dma_flags) != dma_flags ||
        evidence->dma_overrun_count != 0u) {
        return calibration_clk_coded_publish_reject(
            store, &next, CALIBRATION_CLK_CODED_REJECT_DMA);
    }
    if (evidence->pio_stall_count != 0u) {
        return calibration_clk_coded_publish_reject(
            store, &next, CALIBRATION_CLK_CODED_REJECT_PIO_STALL);
    }

    const calibration_clk_marker_config_t config = {
        .version = CALIBRATION_CLK_MARKER_CANDIDATE_VERSION,
        .codebook_id = (uint8_t)next.codebook_id,
        .epoch = (uint8_t)next.train_epoch,
        .master_slot = (uint8_t)next.logical_slot,
        .polarity = CALIBRATION_CLK_POLARITY_NORMAL,
    };
    if (!calibration_clk_marker_build(
            &config, workspace->expected_words,
            CALIBRATION_CLK_MARKER_MAX_RAW_WORDS, &workspace->marker)) {
        return calibration_clk_coded_publish_reject(
            store, &next, CALIBRATION_CLK_CODED_REJECT_BAD_ARGUMENT);
    }
    calibration_clk_correlation_result_t correlation;
    if (!calibration_clk_marker_correlate(
            &config, workspace->expected_words, workspace->marker.raw_samples,
            evidence->capture_words, evidence->capture_sample_count,
            correlation_gate, &correlation)) {
        return calibration_clk_coded_publish_reject(
            store, &next, CALIBRATION_CLK_CODED_REJECT_BAD_ARGUMENT);
    }
    next.best_lag_sample = correlation.best_lag_sample;
    next.best_distance = correlation.best_distance;
    next.second_lag_sample = correlation.second_lag_sample;
    next.second_distance = correlation.second_distance;
    next.margin = correlation.margin;
    next.detected_polarity = correlation.detected_polarity;
    next.marker_flags = correlation.marker_flags;
    if (correlation.accepted == 0u) {
        return calibration_clk_coded_publish_reject(
            store, &next,
            CALIBRATION_CLK_CODED_REJECT_CORRELATION_BASE +
                correlation.reject_reason);
    }
    next.flags |= CALIBRATION_CLK_CODED_FLAG_CORRELATION_VALID |
                  CALIBRATION_CLK_CODED_FLAG_HEADER_VALID;
    next.state = CALIBRATION_CLK_CODED_ACCEPTED;
    next.reject_reason = CALIBRATION_CLK_CODED_REJECT_NONE;
    /* DIAGNOSTIC_ONLY remains set. Only the later HIL/activation gate may
     * clear it; a successful correlation is not active calibration. */
    return calibration_clk_coded_publish_core1(store, &next);
}
