#ifndef TRIGGER_MEASURE_H
#define TRIGGER_MEASURE_H

#include <stdbool.h>
#include <stdint.h>

/* ── 触发测量模块 (同步自检) ──
 *
 * MCU 内部高精度测量, SCPI 只查询结果.
 * 使用 time_us_64() 做精确门控, 从 PIO/DMA 硬件寄存器直接读取触发计数.
 *
 * 典型用法:
 *   trigger_measure_start(1000);       // 启动 1 秒测量
 *   while (!trigger_measure_done()) {  // 等待完成 (非阻塞)
 *       trigger_measure_service();     // 主循环中调用
 *   }
 *   uint32_t freq = trigger_measure_freq_hz();   // 读取频率
 *   trigger_measure_report_t report;
 *   trigger_measure_get_report(&report);          // 读取完整报告
 */

/* ── 测量报告 ── */

typedef struct {
    /* 输入条件 */
    uint32_t gate_ms;           /* 门控时长 */
    uint32_t seq_len;           /* 序列长度 (表项/步进数) */
    uint32_t seq_width;         /* 输出位宽 */

    /* 测量结果 */
    uint32_t trigger_count;     /* 门控内的触发次数 */
    uint32_t rollover_count;    /* DMA 回绕次数 */
    uint32_t freq_hz;           /* 触发频率 (Hz) */
    uint32_t period_ns;         /* 平均周期 (ns) */
    uint32_t elapsed_us;        /* 实际经过时间 (us) */

    /* 稳定性 */
    uint32_t jitter_est_ppm;    /* 估算抖动 (ppm) — 基于门控边界误差 */
} trigger_measure_report_t;

/* ── API ── */

/* 启动一次门控测量. gate_ms: 1..60000 (1ms..60s) */
bool trigger_measure_start(uint32_t gate_ms);

/* 检测测量是否完成 (非阻塞) */
bool trigger_measure_done(void);

/* 主循环服务 (非阻塞, 检查门控是否到期) */
void trigger_measure_service(void);

/* 读取测量频率 (Hz). 测量完成后有效. */
uint32_t trigger_measure_freq_hz(void);

/* 读取完整测量报告 */
bool trigger_measure_get_report(trigger_measure_report_t *report);

/* 快速自检: 1 秒门控, 返回频率 (Hz). 阻塞 ~1 秒. */
uint32_t trigger_measure_quick_freq_hz(uint32_t gate_ms);

#endif
