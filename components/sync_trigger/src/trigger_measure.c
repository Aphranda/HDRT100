#include "trigger_measure.h"

#include <string.h>

#include "hardware/timer.h"
#include "pico/time.h"
#include "sync_io.h"
#include "sync_trigger.h"
#include "trigger_vector.h"

/* ── 内部状态 ── */

typedef enum {
    TRIG_MEAS_IDLE = 0,
    TRIG_MEAS_RUNNING,
    TRIG_MEAS_DONE,
} trig_meas_state_t;

typedef struct {
    trig_meas_state_t state;
    uint64_t t_start_us;
    uint64_t t_end_us;
    uint32_t gate_ms;
    uint32_t count_start;
    uint32_t count_end;
    uint32_t rollover_start;
    uint32_t rollover_end;
    uint32_t seq_len;
    uint32_t seq_width;
} trig_meas_ctx_t;

static trig_meas_ctx_t s_meas;

/* ── 内部: 从硬件读取原始触发计数 ── */

static uint32_t read_raw_trigger_count(uint32_t *rollover_out)
{
    /* 直接从 sync_io 读取, 不经过 vector 快照 */
    const uint32_t rollover = sync_io_seq_step_get_rollover_count();
    const uint32_t seq_idx  = sync_io_seq_step_get_index();

    if (rollover_out != NULL) {
        *rollover_out = rollover;
    }

    /* 从 TriggerVector 获取 seq_len (ARM 后不变) */
    trigger_vector_t v;
    sync_trigger_get_vector(&v);

    const uint32_t seq_len = v.seq_length;

    /* 返回 DMA 字数 (非边沿数). 1 word = 32/seq_width 次触发. */
    return rollover * seq_len + seq_idx;
}

/* trigger_count 和 read_raw_trigger_count 已为边沿计数值,
 * 无需额外转换. */
static uint32_t words_to_edges(uint32_t raw_count)
{
    (void)raw_count;
    return raw_count;
}

/* ── 公共接口 ── */

bool trigger_measure_start(uint32_t gate_ms)
{
    if (gate_ms < 1u || gate_ms > 60000u) {
        return false;
    }

    if (!sync_io_seq_step_is_running()) {
        return false;   /* 需要先 ARM */
    }

    memset(&s_meas, 0, sizeof(s_meas));

    s_meas.count_start = read_raw_trigger_count(&s_meas.rollover_start);
    s_meas.t_start_us = time_us_64();
    s_meas.gate_ms = gate_ms;
    s_meas.state = TRIG_MEAS_RUNNING;

    return true;
}

bool trigger_measure_done(void)
{
    return s_meas.state == TRIG_MEAS_DONE;
}

void trigger_measure_service(void)
{
    if (s_meas.state != TRIG_MEAS_RUNNING) {
        return;
    }

    const uint64_t now_us = time_us_64();
    const uint64_t elapsed_ms = (now_us - s_meas.t_start_us) / 1000u;

    if (elapsed_ms >= s_meas.gate_ms) {
        s_meas.t_end_us = now_us;
        s_meas.count_end = read_raw_trigger_count(&s_meas.rollover_end);
        s_meas.state = TRIG_MEAS_DONE;
    }
}

uint32_t trigger_measure_freq_hz(void)
{
    if (s_meas.state != TRIG_MEAS_DONE) {
        return 0u;
    }

    const uint64_t dt_us = s_meas.t_end_us - s_meas.t_start_us;
    const uint32_t dcount = s_meas.count_end - s_meas.count_start;

    if (dt_us == 0u || dcount == 0u) {
        return 0u;
    }

    /* dcount 是 DMA 字数, 转为边沿数再算频率 */
    const uint64_t edges = words_to_edges(dcount);
    return (uint32_t)(edges * 1000000ull / dt_us);
}

bool trigger_measure_get_report(trigger_measure_report_t *report)
{
    if (report == NULL || s_meas.state != TRIG_MEAS_DONE) {
        return false;
    }

    memset(report, 0, sizeof(*report));

    const uint64_t dt_us = s_meas.t_end_us - s_meas.t_start_us;
    const uint32_t dcount = s_meas.count_end - s_meas.count_start;
    const uint32_t drollover = s_meas.rollover_end - s_meas.rollover_start;

    /* 从 TriggerVector 获取 seq 配置 */
    trigger_vector_t v;
    sync_trigger_get_vector(&v);

    report->gate_ms        = s_meas.gate_ms;
    report->seq_len        = v.seq_length;
    report->seq_width      = v.seq_output_width;
    report->trigger_count  = dcount;
    report->rollover_count = drollover;
    report->elapsed_us     = (uint32_t)dt_us;

    if (dt_us > 0u && dcount > 0u) {
        const uint64_t edges = words_to_edges(dcount);
        report->freq_hz  = (uint32_t)(edges * 1000000ull / dt_us);
        report->period_ns = (uint32_t)(dt_us * 1000ull / edges);
        report->jitter_est_ppm = (uint32_t)(1000000ull / dcount);
    }

    return true;
}

uint32_t trigger_measure_quick_freq_hz(uint32_t gate_ms)
{
    if (gate_ms < 1u || gate_ms > 60000u) {
        gate_ms = 1000u;
    }

    if (!sync_io_seq_step_is_running()) {
        return 0u;
    }

    const uint64_t t0 = time_us_64();
    const uint32_t c0 = read_raw_trigger_count(NULL);

    sleep_ms(gate_ms);

    const uint64_t t1 = time_us_64();
    const uint32_t c1 = read_raw_trigger_count(NULL);

    const uint64_t dt_us = t1 - t0;
    const uint32_t dcount = c1 - c0;

    if (dt_us == 0u || dcount == 0u) {
        return 0u;
    }

    const uint64_t edges = words_to_edges(dcount);
    return (uint32_t)(edges * 1000000ull / dt_us);
}
