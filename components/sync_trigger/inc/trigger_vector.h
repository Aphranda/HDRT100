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
    TRIG_MODE_ENC_COUNT = 2,   /* 编码器计数触发 */
    TRIG_MODE_COUNT,
} trig_mode_t;

/* ── 脉冲计数解码模式 ── */

typedef enum {
    TRIG_PCNT_DECODE_SINGLE   = 0,  /* 单脉冲 (A↑ 计数) */
    TRIG_PCNT_DECODE_QUAD_1X  = 1,  /* 正交 1x (A↑ + B 方向) */
    TRIG_PCNT_DECODE_QUAD_2X  = 2,  /* 正交 2x (A↑↓ + B 方向) */
    TRIG_PCNT_DECODE_UP_DOWN  = 3,  /* 上下计数 (A=UP, B=DOWN) */
} trig_pcnt_decode_t;

/* ── 脉冲计数方向 ── */

typedef enum {
    TRIG_PCNT_DIR_CW   = 0,  /* 仅正向 */
    TRIG_PCNT_DIR_CCW  = 1,  /* 仅反向 */
    TRIG_PCNT_DIR_BOTH = 2,  /* 双向 */
} trig_pcnt_dir_t;

/* ── 旧编码器解码别名 ── */

typedef enum {
    TRIG_ENC_DECODE_1X = TRIG_PCNT_DECODE_QUAD_1X,
    TRIG_ENC_DECODE_2X = TRIG_PCNT_DECODE_QUAD_2X,
    TRIG_ENC_DECODE_4X = 4,  /* 预留 */
} trig_enc_decode_t;

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
    TRIG_STATE_ENC_CONFIGURED,
    TRIG_STATE_ENC_ARMED,
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
    /* ENC_COUNT 事件 */
    TRIG_EVENT_CONFIGURE_ENC,          /* enc_target + pins → 写配置 */
    TRIG_EVENT_SET_ENC_TARGET,         /* 只更新目标值 */
    TRIG_EVENT_SET_ENC_PINS,           /* 更新 A/B/Z 引脚 */
    TRIG_EVENT_ENC_Z_PULSE,            /* PIO IRQ: Z 脉冲到达 (内部事件) */
    /* PCNT 脉冲计数事件 */
    TRIG_EVENT_SET_PCNT_DECODE,        /* 解码模式 */
    TRIG_EVENT_SET_PCNT_DIR,           /* 计数方向 */
    TRIG_EVENT_SET_PCNT_FILTER,        /* 数字滤波窗口 */
    TRIG_EVENT_SET_PCNT_GATE,          /* 门控使能 */
    TRIG_EVENT_SET_PCNT_CMP,           /* 比较器配置 */
    TRIG_EVENT_SET_PCNT_PRESET,        /* 预设计数值 */
    TRIG_EVENT_PCNT_CLEAR,             /* 清零计数 */
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

    /* SEQ_STEP 配置 (HAOFV: TriggerFB 在 IDLE→SEQ_CONFIGURED 时写入)
     * seq_table 保持 1024 字节对齐，便于 DMA/调试工具稳定观察。
     * 当前 DMA 连续循环不使用 ring buffer，而是在 ISR 中手动复位 read_addr。 */
    uint32_t      seq_table[TRIG_SEQ_TABLE_MAX] __attribute__((aligned(1024)));
    uint32_t      seq_length;
    uint32_t      seq_index;
    uint32_t      seq_output_width;

    /* ENC_COUNT / PCNT 配置 */
    uint32_t         enc_target;       /* 目标位置计数值 (比较器0 阈值) */
    uint32_t         enc_count;        /* 当前位置计数 (PIO 维护, AO 快照) */
    uint32_t         enc_rev_count;    /* 圈数计数器 (Z 脉冲累计, CPU 管理面更新) */
    trig_pcnt_decode_t enc_decode;     /* 解码模式: SINGLE/QUAD_1X/QUAD_2X/UP_DOWN */
    trig_pcnt_dir_t  enc_dir;          /* 计数方向: CW/CCW/BOTH */
    bool             enc_z_enabled;    /* Z 相使能 */
    bool             enc_gate_enabled; /* 门控使能 (硬件 GATE_IN) */
    uint32_t         enc_filter_ns;    /* 数字滤波窗口 (ns), 0=禁用 */
    uint32_t         enc_preset;       /* 预设计数值 (CLEAR 后加载) */
    uint32_t         enc_total;        /* 总累计 (永不复位) */
    uint32_t         enc_frequency_hz; /* 当前频率 (管理面计算) */
    uint32_t         enc_cmp_fire_count; /* 比较器触发次数 */
    uint32_t         enc_filter_reject;  /* 滤波器拒绝脉冲数 */
    uint32_t         enc_cmp_pulse_ns;   /* 比较器触发脉冲宽度 (ns) */
    uint32_t         enc_a_pin;        /* A/UP 相 GPIO (默认 16) */
    uint32_t         enc_b_pin;        /* B/DOWN 相 GPIO (默认 17) */
    uint32_t         enc_z_pin;        /* Z 相 GPIO (默认 19, 0=禁用) */

    /* 运行态 (HAOFV: TriggerFB ECC 写, PIO/DMA 硬件更新 seq_index) */
    trig_state_t  state;
    uint32_t      fault_timestamp_ms;   /* FAULT 发生时刻 */

    /* 统计 (HAOFV: TriggerFB+DMA ISR 更新, SCPI/UI 只读) */
    uint32_t      trigger_count;        /* 接受的有效触发总数 */
    uint32_t      output_count;         /* 总输出步数 */
    uint32_t      missed_count;         /* BUSY 期间丢失的触发 */
    uint64_t      rollover_count;       /* DMA 回绕次数 (64-bit) */
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
