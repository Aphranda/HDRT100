#ifndef TRIGGER_VECTOR_H
#define TRIGGER_VECTOR_H

#include <stdbool.h>
#include <stdint.h>

/* ── 触发模式 ── */

typedef enum {
    TRIG_MODE_IDLE      = 0,
    TRIG_MODE_SEQ_STEP  = 1,
    TRIG_MODE_ENC_COUNT = 2,   /* 编码器计数触发 */
    TRIG_MODE_PROTOCOL_TRIGGER = 3, /* 协议触发模式: AUX 作为协议节点 */
    TRIG_MODE_BISS_BRIDGE = TRIG_MODE_PROTOCOL_TRIGGER, /* 兼容别名 */
    /* 预留 */
    TRIG_MODE_GATE_LEVEL = 4,
    TRIG_MODE_ARM_SINGLE = 5,
    TRIG_MODE_FREE_BURST = 6,
    TRIG_MODE_COUNT,
} trig_mode_t;

typedef enum {
    TRIG_PROTOCOL_BISS_C = 0,
} trig_protocol_t;

/* ── BiSS-C 收发一体三通桥角色 ── */

typedef enum {
    TRIG_BISS_ROLE_TAP_MONITOR  = 0,  /* 只监听 CLK/DATA, 不驱动 DATA */
    TRIG_BISS_ROLE_SLAVE_TX     = 1,  /* RX_PULSE -> 等主站 clock polling 时 TX_BISS */
    TRIG_BISS_ROLE_MASTER_RX    = 2,  /* 主动 clock, RX_BISS -> TX_PULSE */
    TRIG_BISS_ROLE_BRIDGE_PROXY = 3,  /* 显式代理桥, 后续允许转发/改写 */
} trig_biss_role_t;

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
    TRIG_STATE_BISS_CONFIGURED,
    TRIG_STATE_BISS_ARMED,
} trig_state_t;

typedef enum {
    TRIG_ERROR_NONE = 0,
    TRIG_ERROR_INVALID_SEQ_CONFIG = 1,
    TRIG_ERROR_RESOURCE_CONFLICT = 2,
    TRIG_ERROR_IO_ARM_FAILED = 3,
    TRIG_ERROR_IO_LOST = 4,
    TRIG_ERROR_INVALID_ENC_TARGET = 10,
    TRIG_ERROR_INVALID_ENC_PINS = 11,
    TRIG_ERROR_INVALID_BISS_CONFIG = 20,
    TRIG_ERROR_FORCED_FAULT = 100,
} trig_error_code_t;

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
    /* BiSS-C 收发一体三通桥事件 */
    TRIG_EVENT_CONFIGURE_BISS,          /* 使用当前 BiSS 配置进入 BISS_CONFIGURED */
    TRIG_EVENT_SET_BISS_ROLE,           /* TAP/SLAVE/MASTER/BRIDGE */
    TRIG_EVENT_SET_BISS_DEVICE,         /* 产品/底板设备 id */
    TRIG_EVENT_SET_BISS_CLOCK,          /* BiSS clock Hz */
    TRIG_EVENT_SET_BISS_FRAME_BITS,     /* 固定帧位宽 */
    TRIG_EVENT_SET_BISS_POSITION_OFFSET,/* position 起始 bit offset, MSB-first */
    TRIG_EVENT_SET_BISS_POSITION_BITS,  /* position/event_count 位宽 */
    TRIG_EVENT_SET_BISS_POSITION_MODULO,/* position crossing modulo */
    TRIG_EVENT_SET_BISS_SAMPLE_EDGE,     /* 0=rising, 1=falling */
    TRIG_EVENT_SET_BISS_SAMPLE_DELAY,    /* PIO sample delay cycles */
    TRIG_EVENT_SET_BISS_SAMPLE_SCAN,     /* 0=disabled, 1=enabled */
    TRIG_EVENT_SET_BISS_SAMPLE_SCAN_START, /* scan 起始 delay cycles */
    TRIG_EVENT_SET_BISS_SAMPLE_SCAN_END, /* scan 结束 delay cycles */
    TRIG_EVENT_SET_BISS_SAMPLE_SCAN_STEP, /* scan 步进 cycles */
    TRIG_EVENT_SET_BISS_TIMEOUT,         /* frame timeout us */
    TRIG_EVENT_SET_BISS_ANCHOR_OFFSET,   /* anchor 起始 bit offset */
    TRIG_EVENT_SET_BISS_ANCHOR_BITS,     /* anchor bit width */
    TRIG_EVENT_SET_BISS_ANCHOR_MASK,     /* anchor compare mask, low 32 bits */
    TRIG_EVENT_SET_BISS_ANCHOR_VALUE,    /* anchor expected value, low 32 bits */
    TRIG_EVENT_SET_BISS_ERROR_BIT,       /* ERR bit offset, UINT32_MAX=disabled */
    TRIG_EVENT_SET_BISS_WARNING_BIT,     /* WRN bit offset, UINT32_MAX=disabled */
    TRIG_EVENT_SET_BISS_STATUS_GATE,     /* status gate policy */
    TRIG_EVENT_SET_BISS_CRC_OFFSET,      /* CRC field offset */
    TRIG_EVENT_SET_BISS_CRC_BITS,        /* CRC field width */
    TRIG_EVENT_SET_BISS_CRC_COVER_OFFSET,/* CRC coverage start */
    TRIG_EVENT_SET_BISS_CRC_COVER_BITS,  /* CRC coverage width */
    TRIG_EVENT_SET_BISS_CRC_POLYNOMIAL,  /* CRC polynomial */
    TRIG_EVENT_SET_BISS_CRC_INIT,        /* CRC initial value */
    TRIG_EVENT_SET_BISS_CRC_XOR,         /* CRC final xor */
    TRIG_EVENT_SET_BISS_CRC_INVERT,      /* CRC field inverted on wire */
    TRIG_EVENT_SET_BISS_CRC_GATE,        /* CRC gate policy */
    TRIG_EVENT_SET_BISS_LATENCY_OFFSET,  /* measured fixed latency ns */
    TRIG_EVENT_SET_BISS_TARGET,         /* 位置/事件触发阈值 */
    TRIG_EVENT_BISS_PULSE_IN,           /* 软件注入: 本地脉冲输入已锁存 */
    TRIG_EVENT_BISS_FRAME_RX,           /* 软件注入: 收到远端帧 position/event_count */
    TRIG_EVENT_BISS_CRC_ERROR,          /* CRC 错误计数 */
    TRIG_EVENT_BISS_TIMEOUT,            /* 帧/轮询超时计数 */
    TRIG_EVENT_RUNTIME_SAMPLE,         /* AO 管理面运行态采样, 不来自 PIO/DMA IRQ */
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

    /* 协议触发 / BiSS-C 节点配置与计数器
     * P0 固定复用 AUX0..AUX3 作为 BiSS-C 逻辑线，不改底层硬件定义：
     * AUX0=CLK_IN, AUX1=DATA_IN, AUX2=CLK_OUT, AUX3=DATA_OUT。
     * 本地脉冲输入/输出继续使用主触发口 TRIG_IN/TRIG_OUT。 */
    trig_protocol_t  protocol;
    trig_biss_role_t biss_role;
    uint32_t         biss_device_id;          /* 产品/底板设备 profile id */
    uint32_t         biss_phase;              /* P0: 0=FAST_RT_TEST, P1+: SLOW_CTRL_SYNC */
    uint32_t         biss_clock_hz;
    uint32_t         biss_frame_bits;
    uint32_t         biss_position_offset;
    uint32_t         biss_position_bits;
    uint32_t         biss_position_modulo;
    uint32_t         biss_anchor_offset;
    uint32_t         biss_anchor_bits;
    uint64_t         biss_anchor_mask;
    uint64_t         biss_anchor_value;
    uint32_t         biss_sample_edge;
    uint32_t         biss_sample_delay_cycles;
    uint32_t         biss_sample_scan_enabled;
    uint32_t         biss_sample_scan_start_cycles;
    uint32_t         biss_sample_scan_end_cycles;
    uint32_t         biss_sample_scan_step_cycles;
    uint32_t         biss_active_sample_edge;
    uint32_t         biss_active_sample_delay_cycles;
    uint32_t         biss_sample_scan_index;
    uint32_t         biss_sample_scan_wrap_count;
    uint32_t         biss_timeout_us;
    uint32_t         biss_error_bit_offset;
    uint32_t         biss_warning_bit_offset;
    uint32_t         biss_status_gate_policy;
    uint32_t         biss_crc_offset;
    uint32_t         biss_crc_bits;
    uint32_t         biss_crc_cover_offset;
    uint32_t         biss_crc_cover_bits;
    uint32_t         biss_crc_polynomial;
    uint32_t         biss_crc_init;
    uint32_t         biss_crc_xor;
    uint32_t         biss_crc_invert;
    uint32_t         biss_crc_gate_policy;
    uint32_t         biss_target;
    uint32_t         biss_latency_offset_ns;
    uint32_t         biss_last_position;
    uint32_t         biss_last_seq;
    uint32_t         biss_pulse_in_count;
    uint32_t         biss_pulse_out_count;
    uint32_t         biss_tx_frame_count;
    uint32_t         biss_rx_frame_count;
    uint32_t         biss_frame_error_count;
    uint32_t         biss_crc_error_count;
    uint32_t         biss_status_block_count;
    uint32_t         biss_fifo_overflow_count;
    uint32_t         biss_timeout_count;
    uint32_t         biss_trigger_count;
    uint32_t         biss_cal_round_trip_ns;
    uint32_t         biss_cal_jitter_p99_ns;
    uint32_t         biss_cal_valid;
    uint32_t         biss_clk_in_pin;
    uint32_t         biss_data_in_pin;
    uint32_t         biss_clk_out_pin;
    uint32_t         biss_data_out_pin;
    uint32_t         biss_pulse_in_pin;
    uint32_t         biss_pulse_out_pin;

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
