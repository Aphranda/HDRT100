#include "tdma_rx_sequence.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

static int expect_bool(const char *name, bool actual, bool expected)
{
    if (actual == expected) return 0;
    (void)fprintf(stderr, "FAIL %s: got %u expected %u\n",
                  name, actual ? 1u : 0u, expected ? 1u : 0u);
    return 1;
}

static int expect_u64(const char *name, uint64_t actual, uint64_t expected)
{
    if (actual == expected) return 0;
    (void)fprintf(stderr, "FAIL %s: got %llu expected %llu\n",
                  name,
                  (unsigned long long)actual,
                  (unsigned long long)expected);
    return 1;
}

int main(void)
{
    int failed = 0;
    tdma_rx_sequence_tracker_t tracker = {0};
    uint64_t produced = 0u;

    failed += expect_bool(
        "reject invalid reset",
        tdma_rx_sequence_reset(&tracker, 0u, 0u, 0u), false);
    failed += expect_bool(
        "reset",
        tdma_rx_sequence_reset(&tracker, 1024u, 0u, 0u), true);
    failed += expect_bool(
        "first observation retains initial words",
        tdma_rx_sequence_observe(&tracker, 12u, 0u, 297u, &produced), true);
    failed += expect_u64("first produced", produced, 12u);
    failed += expect_bool(
        "no new data",
        tdma_rx_sequence_observe(&tracker, 12u, 0u, 297u, &produced), true);
    failed += expect_u64("no new produced", produced, 12u);

    failed += expect_bool(
        "reset early boundary scenario",
        tdma_rx_sequence_reset(&tracker, 1024u, 0u, 0u), true);
    failed += expect_bool(
        "receive before boundary",
        tdma_rx_sequence_observe(&tracker, 290u, 0u, 297u, &produced), true);
    failed += expect_u64("before boundary produced", produced, 290u);
    failed += expect_bool(
        "boundary leads returned tail",
        tdma_rx_sequence_observe(&tracker, 290u, 1u, 297u, &produced), true);
    failed += expect_u64("early boundary does not add ring", produced, 290u);
    failed += expect_bool(
        "returned tail arrives",
        tdma_rx_sequence_observe(&tracker, 297u, 1u, 297u, &produced), true);
    failed += expect_u64("tail advances exactly", produced, 297u);

    failed += expect_bool(
        "reset modulo wrap scenario",
        tdma_rx_sequence_reset(&tracker, 1024u, 900u, 3u), true);
    failed += expect_bool(
        "single modulo wrap below ring capacity",
        tdma_rx_sequence_observe(&tracker, 100u, 4u, 297u, &produced), true);
    failed += expect_u64("modulo wrap delta", produced, 224u);

    failed += expect_bool(
        "reset missed ring scenario",
        tdma_rx_sequence_reset(&tracker, 1024u, 0u, 10u), true);
    failed += expect_bool(
        "frame evidence restores missed ring",
        tdma_rx_sequence_observe(&tracker, 164u, 14u, 297u, &produced), true);
    failed += expect_u64("missed ring restored", produced, 1188u);
    failed += expect_bool(
        "next frame remains continuous",
        tdma_rx_sequence_observe(&tracker, 461u, 15u, 297u, &produced), true);
    failed += expect_u64("post-wrap continuity", produced, 1485u);

    failed += expect_bool(
        "reset inconsistent evidence scenario",
        tdma_rx_sequence_reset(&tracker, 1024u, 0u, 0u), true);
    failed += expect_bool(
        "two early boundaries observed",
        tdma_rx_sequence_observe(&tracker, 0u, 2u, 297u, &produced), true);
    failed += expect_u64("inconsistent counter cannot invent ring", produced, 0u);

    failed += expect_bool(
        "reset modulo-only scenario",
        tdma_rx_sequence_reset(&tracker, 1024u, 1000u, 0u), true);
    failed += expect_bool(
        "variable frame modulo accumulation",
        tdma_rx_sequence_observe(&tracker, 20u, 999u, 0u, &produced), true);
    failed += expect_u64("variable frame delta", produced, 44u);

    tdma_rx_dma_counter_t dma = {0};
    bool discontinuity = false;
    const uint32_t reload = 301u * TDMA_RX_DMA_RELOAD_FRAMES;
    failed += expect_bool("DMA rejects empty frame", tdma_rx_dma_counter_reset(&dma,0,0),false);
    failed += expect_bool("DMA rejects count overflow", tdma_rx_dma_counter_reset(&dma,4096,0),false);
    failed += expect_bool("DMA reset", tdma_rx_dma_counter_reset(&dma,301,0),true);
    failed += expect_u64("DMA period preserves SRAM phase",dma.reload_words%1024,0);
    failed += expect_u64("DMA period preserves frame phase",dma.reload_words%301,0);
    failed += expect_bool("DMA observes committed words",
        tdma_rx_dma_counter_observe(&dma,reload-12,20,21,&produced,&discontinuity),true);
    failed += expect_u64("DMA first position",produced,12);
    failed += expect_bool("DMA continuity",discontinuity,false);
    failed += expect_bool("DMA multiple SRAM rings",
        tdma_rx_dma_counter_observe(&dma,reload-4108,4200,4201,&produced,&discontinuity),true);
    failed += expect_u64("DMA exact multiple wraps",produced,4108);
    failed += expect_bool("DMA before hardware reload",
        tdma_rx_dma_counter_observe(&dma,2,reload+10ull,reload+11ull,&produced,&discontinuity),true);
    failed += expect_u64("DMA before reload position",produced,reload-2);
    failed += expect_bool("DMA terminal zero",
        tdma_rx_dma_counter_observe(&dma,0,reload+14ull,reload+15ull,&produced,&discontinuity),true);
    failed += expect_u64("DMA terminal advances",produced,reload);
    failed += expect_bool("DMA self reload is not extra data",
        tdma_rx_dma_counter_observe(&dma,reload,reload+16ull,reload+17ull,&produced,&discontinuity),true);
    failed += expect_u64("DMA reload same position",produced,reload);
    failed += expect_bool("DMA next period",
        tdma_rx_dma_counter_observe(&dma,reload-5,reload+25ull,reload+26ull,&produced,&discontinuity),true);
    failed += expect_u64("DMA next data",produced,reload+5ull);
    failed += expect_bool("DMA long sample bracket",
        tdma_rx_dma_counter_observe(&dma,reload-5,reload+27ull,2ull*reload+27,&produced,&discontinuity),true);
    failed += expect_bool("DMA ambiguity drops continuity",discontinuity,true);
    failed += expect_u64("DMA ambiguity invents no bytes",produced,reload+5ull);
    failed += expect_u64("DMA ambiguity changes only observation epoch",dma.observation_epoch,1);
    const uint64_t saved=dma.produced_words;
    failed += expect_bool("DMA invalid count",
        tdma_rx_dma_counter_observe(&dma,reload+1,2ull*reload+28,2ull*reload+29,&produced,&discontinuity),false);
    failed += expect_bool("DMA reversed clock",
        tdma_rx_dma_counter_observe(&dma,reload-6,1,2,&produced,&discontinuity),false);
    failed += expect_u64("DMA rejected observations preserve state",dma.produced_words,saved);

    if (failed != 0) {
        (void)fprintf(stderr, "tdma_rx_sequence tests failed: %d\n", failed);
        return 1;
    }
    (void)puts("tdma_rx_sequence tests passed");
    return 0;
}
