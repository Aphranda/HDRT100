#ifndef TRIGGER_VECTOR_H
#define TRIGGER_VECTOR_H

#include <stdbool.h>
#include <stdint.h>

/* ── 触发模式 ── */

typedef enum {
    TRIG_MODE_IDLE      = 0,
    TRIG_MODE_SEQ_STEP  = 1,
    /* 预留 */
    TRIG_MODE_GATE_LEVEL,
    TRIG_MODE_ARM_SINGLE,
    TRIG_MODE_FREE_BURST,
    TRIG_MODE_COUNT,
} trig_mode_t;

/* ── 触发边沿 ── */

typedef enum {
    TRIG_EDGE_RISING  = 0,
    TRIG_EDGE_FALLING = 1,
} trig_edge_t;

/* ── 安全输出态 ── */

typedef enum {
    TRIG_SAFE_ZERO = 0,
    TRIG_SAFE_ONE  = 1,
} trig_safe_state_t;

/* ── 触发 AO 状态 ── */

typedef enum {
    TRIG_STATE_IDLE            = 0,
    TRIG_STATE_SEQ_CONFIGURED,
    TRIG_STATE_SEQ_ARMED,
    TRIG_STATE_FAULT,
} trig_state_t;

/* ── 触发事件 ── */

typedef enum {
    TRIG_EVENT_CONFIGURE_SEQ   = 0,   /* seq_table + length + width 写入 */
    TRIG_EVENT_SET_SOURCE_PIN,        /* 触发源 IO 选择 */
    TRIG_EVENT_SET_EDGE,              /* 边沿选择 */
    TRIG_EVENT_SET_GATE,              /* GATE_IN 使能/禁用 */
    TRIG_EVENT_SET_SAFE_STATE,        /* 安全输出态 */
    TRIG_EVENT_ARM,
    TRIG_EVENT_DISARM,
    TRIG_EVENT_DMA_ROLLOVER,         /* DMA 一圈完成回绕 */
    TRIG_EVENT_FAULT,
    TRIG_EVENT_CLEAR_FAULT,
    /* 即时脉冲命令（兼容旧 SYNC_TRIGGER_EVENT） */
    TRIG_EVENT_SET_TRIGGER_WIDTH,
    TRIG_EVENT_FIRE_TRIGGER,
    TRIG_EVENT_SET_PULSE_WIDTH,
    TRIG_EVENT_FIRE_PULSE,
    TRIG_EVENT_SET_MARKER_WIDTH,
    TRIG_EVENT_FIRE_MARKER,
    TRIG_EVENT_SET_SAMPLE_RATE,
    TRIG_EVENT_SET_SAMPLE_STATE,
    TRIG_EVENT_SET_CLOCK_FREQ,
    TRIG_EVENT_SET_CLOCK_STATE,
    TRIG_EVENT_RESET,
} trig_event_type_t;

/* ── TriggerVector ── */

#define TRIG_SEQ_TABLE_MAX  256u
#define TRIG_SEQ_WIDTH_MAX  8u

typedef struct {
    /* 模式 */
    trig_mode_t   active_mode;
    uint32_t      supported_modes;

    /* 触发源与条件 (HAOFV: TriggerFB 在 IDLE/SEQ_CONFIGURED 状态写入) */
    uint32_t      trigger_source_pin;   /* 触发源 GPIO (16-19, 26-29), 默认 16 */
    trig_edge_t   edge;                 /* 边沿选择, 默认 RISING */
    bool          gate_enabled;         /* GATE_IN 门控使能 */
    trig_safe_state_t safe_state;       /* IDLE/FAULT 时输出安全态 */

    /* SEQ_STEP 配置 (HAOFV: TriggerFB 在 IDLE→SEQ_CONFIGURED 时写入) */
    uint32_t      seq_table[TRIG_SEQ_TABLE_MAX];
    uint32_t      seq_length;
    uint32_t      seq_index;
    uint32_t      seq_output_width;

    /* 运行态 (HAOFV: TriggerFB ECC 写, PIO/DMA 硬件更新 seq_index) */
    trig_state_t  state;
    uint32_t      fault_timestamp_ms;   /* FAULT 发生时刻 */

    /* 统计 (HAOFV: TriggerFB+DMA ISR 更新, SCPI/UI 只读) */
    uint32_t      trigger_count;        /* 接受的有效触发总数 */
    uint32_t      output_count;         /* 总输出步数 */
    uint32_t      missed_count;         /* BUSY 期间丢失的触发 */
    uint32_t      rollover_count;       /* DMA 回绕次数 */
    uint32_t      error_code;

    /* 即时脉冲参数（兼容旧命令） */
    uint32_t      trigger_width_us;
    uint32_t      pulse_width_us;
    uint32_t      marker_width_us;
    uint32_t      capture_sample_hz;
    uint32_t      sync_clock_hz;
    bool          sync_clock_enabled;

    /* sync_io 状态快照（只读） */
    bool          initialized;
    bool          io_initialized;
    bool          capture_running;
    bool          sync_clock_running;
    uint32_t      dropped_capture_words;
} trigger_vector_t;

/* ── 触发事件载荷 ── */

typedef struct {
    const uint32_t *seq_table;
    uint32_t        seq_length;
    uint32_t        seq_width;
} trig_event_seq_config_t;

typedef struct {
    trig_event_type_t type;
    union {
        trig_event_seq_config_t seq_config;
        uint32_t                value;
    } payload;
} trig_event_t;

#endif
