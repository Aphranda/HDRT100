# PinProbeA1 多机协同架构设计方案

Status: Active
Domain: Architecture
Canonical: `docs/legacy/pinprobe/LEGACY_PINPROBEA1_RAM_REFLECTIVE_MEMORY_ARCHITECTURE.md`
Related: `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/legacy/pinprobe/LEGACY_PINPROBEA1_OTA_CAN_DISTRIBUTION.md`
Last updated: 2026-08-10

## 文档信息

| 项目 | 内容 |
|------|------|
| 文档版本 | V1.0 |
| 创建日期 | 2026-05-30 |
| 目标平台 | STM32F103RCT6 (256KB Flash / 48KB RAM) |
| 当前版本 | 单机模式 (PinProbeA1) |
| 升级目标 | 多机协同模式 (1~8 节点) |

---

## 1. 系统概述

### 1.1 升级目标

将 PinProbeA1 从单台独立控制器升级为**多节点协同控制系统**，支持最多 8 个节点通过 CAN 总线互联，统一由 PLC 通过 Modbus RTU 管理。

### 1.2 核心设计原则

- **RAM 反射内存**：所有节点在 SRAM 中维护一份相同的"公共区向量表"，通过 CAN 广播实现毫秒级同步
- **W25Q128 快照持久化**：外部 SPI Flash 存储公共区快照，掉电恢复
- **CAN 总线广播同步**：节点间数据同步通道，500kbps~1Mbps
- **Modbus RTU 从站**：PLC 接入任意节点即可读写整个系统

### 1.3 与单机版的区别

| 维度 | 单机版 (当前) | 多机版 (目标) |
|------|--------------|--------------|
| 节点数 | 1 | 1~8 |
| 上位机接口 | SCPI (USART1) | Modbus RTU 从站 (USART3) |
| 调试接口 | SCPI (USART1) | SCPI (USART1, 保留) |
| 节点间通信 | 无 | CAN 总线 (500kbps~1Mbps) |
| 持久化 | 内部 Flash 2KB | W25Q128 SPI Flash (16MB) |
| 设备寻址 | device_id (已有,未用) | node_id (1~8) |
| 同步机制 | 无 | CAN 广播 + Version 号 |

---

## 2. 硬件变更

### 2.1 新增外设

| 外设 | 引脚 | 功能 | 备注 |
|------|------|------|------|
| **CAN1** | PB8 (CAN_RX), PB9 (CAN_TX) | 节点间同步通信 | 使用 GPIO Remap (默认 CAN1 在 PA11/PA12 被 USB 占用) |
| **SPI2** | PB12 (NSS), PB13 (SCK), PB14 (MISO), PB15 (MOSI) | W25Q128 接口 | 唯一可用的 SPI 外设 (SPI1 被 JTAG 占用部分引脚) |

### 2.2 引脚占用分析

当前已用引脚：

```
USART1:  PA9  (TX), PA10 (RX)          — SCPI 控制台 (保留)
USART3:  PB10 (TX), PB11 (RX)          — RS485 (改为 Modbus 从站)
RF控制:  PA6, PA7, PB0, PB1, PC6-9     — 8路射频开关 (不变)
LED:     PA8                            — 板载状态灯 (不变)
SWD:     PA13, PA14                     — 调试接口 (不变)
```

新增引脚（无冲突）：

```
CAN1:    PB8  (CAN_RX), PB9  (CAN_TX)  — Remap1 模式
SPI2:    PB12 (NSS),  PB13 (SCK)       — W25Q128
         PB14 (MISO), PB15 (MOSI)
```

**引脚冲突检查：通过。** CAN1 Remap 使用 PB8/PB9，SPI2 使用 PB12~PB15，与现有 IO 无冲突。

### 2.3 W25Q128 简介

| 参数 | 规格 |
|------|------|
| 容量 | 128 Mbit (16 MB) |
| 页大小 | 256 字节 |
| 扇区大小 | 4 KB |
| 块大小 | 64 KB |
| 擦写寿命 | 100,000 次 (典型值) |
| 擦除时间 | 扇区 60~300ms, 芯片 80~200s |
| 写时间 | 页编程 0.7~3ms |

### 2.4 CAN 总线拓扑

```
    PLC (Modbus Master)
         │
         │ RS485 (Modbus RTU)
         │
    ┌────┴────┐
    │ Node #1 │──── CAN_H ────┐
    │ (任意)  │──── CAN_L ────┤
    └─────────┘              │
                        ┌────┴────┐
                        │ Node #2 │
                        └────┬────┘
                             │
                        ┌────┴────┐
                        │ Node #3 │
                        └────┬────┘
                          ... (最多8节点)
                             │
                        120Ω 终端电阻
```

- 任一节点通过 Modbus 接收 PLC 指令
- 所有节点通过 CAN 总线同步公共区数据
- 总线两端各接 120Ω 终端电阻

---

## 3. RAM 反射内存设计

### 3.1 内存布局规划

STM32F103RCT6 共 48KB SRAM (0x20000000~0x2000BFFF)：

```
┌──────────────────────────────────────────────────┐ 0x2000C000
│  FreeRTOS 内核 + 堆 (heap_4, 10KB)                │
│  (实际堆使用量约 6~8KB)                            │
├──────────────────────────────────────────────────┤
│  任务栈: SCPITask (4KB) + ModBusTask (4KB)        │
│  + defaultTask (512B) + ISR 栈 (1KB)              │
├──────────────────────────────────────────────────┤
│  系统数据: .bss / .data                           │
│  (USART DMA 缓冲区, SCPI 缓冲区, Flash 缓存等)     │
│  估计: ~4KB                                       │
├──────────────────────────────────────────────────┤
│  ★ RAM 反射内存向量表 (1KB)                        │
│  绝对地址: 0x2000A000~0x2000A3FF                  │
├──────────────────────────────────────────────────┤
│  代码只读数据 (.rodata) + 堆栈余量                  │
│  ~22KB 剩余                                        │
└──────────────────────────────────────────────────┘ 0x20000000
```

**RAM 向量表起始地址**：`0x2000A000` (固定在 FreeRTOS 堆之后的空闲区，通过分散加载文件锁定)

### 3.2 RAM 向量表结构体 (1024 字节)

```c
#define RAM_VECTOR_BASE_ADDR    0x2000A000UL
#define RAM_VECTOR_SIZE         1024U
#define RAM_VECTOR_MAGIC        0x50504156UL  /* "PPAV" = PinProbeA Vector */

typedef struct __attribute__((packed)) {
    /* ===== 帧头 (16B) ===== */
    uint32_t magic;             /* +0x00: 魔数 0x50504156                    */
    uint32_t version;           /* +0x04: 版本号 (单调递增, 每次更新+1)       */
    uint32_t timestamp;         /* +0x08: 时间戳 (ms, 由发起节点填写)         */
    uint32_t crc32;             /* +0x0C: CRC32 校验 (CMD~END, 含结构填充)   */

    /* ===== 命令区 (16B) ===== */
    uint16_t cmd;               /* +0x10: 命令码 (见命令码表)                 */
    uint16_t param_len;         /* +0x12: 参数长度 (字节)                     */
    uint8_t  params[12];        /* +0x14: 参数区 (变长,最大扩展到128B使用填充) */

    /* ===== 目标节点掩码 (8B) ===== */
    uint8_t  node_mask;         /* +0x20: 目标节点位掩码 (bit0=Node1, ...)   */
    uint8_t  source_node;       /* +0x21: 发起节点 ID (1~8)                  */
    uint8_t  priority;          /* +0x22: 优先级 (0=普通, 1=紧急)            */
    uint8_t  ack_flags;         /* +0x23: 应答标志 (bit0~7 = Node1~8 ACK)    */
    uint32_t ack_timestamp;     /* +0x24: 最后 ACK 时间戳                     */

    /* ===== 系统状态区 (64B) ===== */
    uint8_t  sys_state;         /* +0x28: 系统状态 (Lock/Idle/Running/...)   */
    uint8_t  error_code;        /* +0x29: 错误码                              */
    uint16_t reserved_sys;      /* +0x2A: 保留                                */

    /* 各节点状态 (每个节点 7 字节, 8 节点共 56 字节) */
    struct {
        uint8_t  online;        /* +0x2C: 在线标志 (0=离线, 1=在线)         */
        uint8_t  state;         /* +0x2D: 该节点状态                          */
        uint8_t  hw_ver;        /* +0x2E: 硬件版本                            */
        uint16_t fw_ver;        /* +0x2F: 固件版本                            */
        uint16_t heartbeat;     /* +0x31: 心跳计数                            */
    } node_status[8];           /* +0x2C~0x63: 共 56 字节                    */

    /* ===== IO 映射区 (128B) ===== */
    /* 气缸控制 (2 通道) */
    uint8_t  cylinder_cmd[2];   /* +0x64: 气缸命令 (0=CLOSE, 1=OPEN)         */
    uint8_t  cylinder_state[2]; /* +0x66: 气缸状态                            */

    /* 锁定控制 */
    uint8_t  lock_cmd;          /* +0x68: 锁定命令                            */
    uint8_t  lock_state;        /* +0x69: 锁定状态                            */

    /* LED 控制 */
    uint8_t  led_cmd;           /* +0x6A: LED 命令                            */
    uint8_t  led_state;         /* +0x6B: LED 状态                            */

    /* RF 开关矩阵 (16 通道, 每个 1 字节) */
    uint8_t  rf_channel[16];    /* +0x6C: RF 通道映射 (每通道存目标端口 1~16)  */
    uint8_t  rf_state[16];      /* +0x7C: RF 通道状态 (0=断开, 1=闭合)        */

    uint8_t  reserved_io[48];   /* +0x8C: IO 保留扩展区                       */

    /* ===== 扩展参数区 (768B) ===== */
    uint8_t  ext_params[768];   /* +0xBC: 大参数区,供复杂命令使用              */
                                /*         可被 cmd+param_len 指向此区域       */

    /* ===== 帧尾 (4B) ===== */
    uint32_t end_magic;         /* +0x3FC: 帧尾魔数 0x454E4400 ("END\0")     */
} RAM_Vector_t;                 /* 总计: 1024 字节                            */

/* 编译期静态断言: 结构体大小必须等于 1024 */
_Static_assert(sizeof(RAM_Vector_t) == RAM_VECTOR_SIZE,
               "RAM_Vector_t must be exactly 1024 bytes");
```

### 3.3 Version 号同步机制

```
版本号规则:
  - 单调递增 uint32_t, 初始值 1
  - 任何节点修改公共区时: version++
  - 所有节点持续监听 CAN 广播, 比较 version

同步流程:
  1. Node_N 收到 Modbus 写命令 → 修改本地 RAM 向量表 → version++
  2. Node_N 通过 CAN 广播: [version, crc32, cmd, params, node_mask]
  3. 其他节点收到广播 → 比较 version:
     a) 广播 version > 本地 version → 接受更新 → 覆盖 RAM 表
     b) 广播 version == 本地 version → 忽略 (已同步)
     c) 广播 version < 本地 version → 回发当前最新数据 (纠正发送方)
```

### 3.4 访问接口

```c
/* ===== RAM 向量表核心接口 ===== */

/* 获取向量表指针 (直接映射到固定地址) */
static inline RAM_Vector_t* RAM_Vector_Get(void) {
    return (RAM_Vector_t*)RAM_VECTOR_BASE_ADDR;
}

/* 初始化向量表 (上电时调用) */
void RAM_Vector_Init(void);

/* 更新命令 — 修改公共区并触发广播 */
RAM_Vector_Status_t RAM_Vector_Update(uint16_t cmd, const uint8_t* params,
                                       uint16_t param_len, uint8_t node_mask);

/* 接收远程更新 — 收到 CAN 广播后应用 */
RAM_Vector_Status_t RAM_Vector_ApplyRemote(const RAM_Vector_t* remote);

/* 校验向量表完整性 */
uint8_t RAM_Vector_Verify(void);

/* 快照: 将向量表整体写入/读取 W25Q128 */
RAM_Vector_Status_t RAM_Vector_SnapshotSave(void);
RAM_Vector_Status_t RAM_Vector_SnapshotLoad(void);
```

---

## 4. W25Q128 快照存储设计

### 4.1 Flash 分区

W25Q128 共 16MB (65536 页 × 256B)，按功能分区：

```
┌────────────────────────────────────────────────────┐ 0x000000
│ 分区 0: 引导信息区 (4KB = 1 扇区)                    │
│   - Magic, Flash 布局版本, 节点配置表                 │
│   - 每个节点保留 64 字节配置项                         │
├────────────────────────────────────────────────────┤ 0x001000
│ 分区 1: 快照存储区 — Active 区 (8KB = 2 扇区)         │
│   - 32 个 Slot (每 Slot 256B = 1 页)                │
│   - 日志式循环写入, 最新版本追加到下一个 Slot          │
│   - 每 Slot 结构: Header(8B) + Vector(1024B 分4页)  │
├────────────────────────────────────────────────────┤ 0x003000
│ 分区 2: 快照存储区 — Backup 区 (8KB, 结构与 Active 同) │
├────────────────────────────────────────────────────┤ 0x005000
│ 分区 3: 事件日志区 (64KB = 1 块)                      │
│   - 记录关键动作、错误、状态变更                        │
│   - 循环写入                                          │
├────────────────────────────────────────────────────┤ 0x015000
│ 分区 4: 固件升级区 (4MB, 保留)                         │
├────────────────────────────────────────────────────┤
│ 分区 5: 用户数据保留区 (剩余 ~12MB)                     │
└────────────────────────────────────────────────────┘ 0xFFFFFF
```

### 4.2 快照 Slot 结构

```c
#define SNAPSHOT_SLOT_SIZE      256U    /* 每 Slot 1 页 */
#define SNAPSHOT_MAX_SLOTS      32U     /* Active 区 32 个 Slot */
#define SNAPSHOT_ACTIVE_OFFSET  0x1000U /* Active 区起始地址 */
#define SNAPSHOT_BACKUP_OFFSET  0x3000U /* Backup 区起始地址 */

typedef struct __attribute__((packed)) {
    uint32_t slot_magic;        /* 魔数 0x534E4150 ("SNAP") */
    uint32_t vector_version;    /* 对应的 RAM Vector version */
    /* 以下 248 字节存储 RAM_Vector_t 的压缩/分片 */
    /* 实际存储时: 1024 字节向量表分 4 页存于连续 Slot */
} SnapshotSlot_t;
```

### 4.3 写入策略

```
触发条件:
  1. 定时写入: 每 10 分钟检查 version 变化,如有则写入
  2. 急停触发: 立即写入
  3. 状态变更: 门开关完成、锁定状态变更 → 立即写入
  4. 关机前: 掉电检测中断触发 → 紧急写入 (电容保持供电)

写入流程 (日志式):
  1. 读取 Active 区当前写入指针
  2. 擦除目标 Slot 所在扇区 (仅在跨越扇区边界时)
  3. 写入新 SnapshotSlot (256B 页编程, 0.7ms)
  4. 更新写入指针
  5. 校验写入的 CRC

寿命分析:
  - 10 分钟定时写入 + 事件触发写入 ≈ 200 次/天
  - Active 区 32 Slot × 2 (Active+Backup) = 64 Slot
  - 每 Slot 可单独擦除 (扇区擦除 = 32 个 Slot 批量擦除)
  - 每个扇区 128 页 (32 Slot × 4 页/Slot)
  - 最坏情况: 每 32 次写入擦除 1 次扇区
  - 100,000 次擦除 / (200次/天 / 32) ≈ 16,000 天 ≈ 43 年
```

### 4.4 双区冗余

```
写入时:
  1. 先写 Active 区 → CRC 校验通过
  2. 再写 Backup 区 → CRC 校验通过
  3. 更新引导区的"当前有效区"标记

恢复时:
  1. 读取引导区 → 定位 Active 区
  2. 读取最新 Slot → CRC 校验
  3. 若 Active 区 CRC 失败 → 自动切换到 Backup 区恢复
```

---

## 5. CAN 总线协议设计

### 5.1 帧格式 (标准帧 11-bit ID)

```
CAN ID 分配 (11-bit):
  ┌──────────┬──────────┬──────────┐
  │ 功能码    │ 源节点   │ 目标节点  │
  │ 5 bit    │ 3 bit    │ 3 bit    │
  └──────────┴──────────┴──────────┘

功能码定义:
  0x00  DATA_SYNC      数据同步帧 (广播, 目标节点=0)
  0x01  DATA_REQUEST   数据请求帧
  0x02  DATA_RESPONSE  数据响应帧
  0x03  HEARTBEAT      心跳帧
  0x04  ACK            应答帧
  0x05  NODE_JOIN      新节点加入请求
  0x06  NODE_LEAVE     节点离线通知
  0x07  EMERGENCY      紧急广播 (急停等)
  0x08~0x1F 保留
```

### 5.2 各帧数据格式

```c
/* ===== DATA_SYNC (同步帧) ===== */
/* CAN ID: (0x00 << 6) | (src_node << 3) | 0x00 */
/* DLC=8, 传输向量表的关键变更信息 */
typedef struct __attribute__((packed)) {
    uint32_t version;       /* Byte 0-3: 新版本号 */
    uint16_t cmd;           /* Byte 4-5: 命令码 */
    uint8_t  param_len;     /* Byte 6: 参数长度 */
    uint8_t  node_mask;     /* Byte 7: 目标掩码 */
} CAN_SyncFrame_t;

/* ===== DATA_REQUEST / DATA_RESPONSE ===== */
/* 用于大参数传输 (>8 字节时分包) */
/* CAN ID: (0x01/0x02 << 6) | (src_node << 3) | dst_node */

/* ===== HEARTBEAT (心跳帧) ===== */
/* CAN ID: (0x03 << 6) | (src_node << 3) | 0x00 */
/* DLC=4, 每 500ms 发送一次 */
typedef struct __attribute__((packed)) {
    uint16_t heartbeat_cnt; /* Byte 0-1: 心跳计数 */
    uint8_t  state;         /* Byte 2: 节点状态 */
    uint8_t  checksum;      /* Byte 3: XOR 校验 */
} CAN_HeartbeatFrame_t;

/* ===== ACK (应答帧) ===== */
/* CAN ID: (0x04 << 6) | (src_node << 3) | dst_node */
/* DLC=4 */
typedef struct __attribute__((packed)) {
    uint32_t ack_version;   /* 确认的版本号 */
} CAN_AckFrame_t;

/* ===== NODE_JOIN (加入帧) ===== */
/* CAN ID: (0x05 << 6) | (src_node << 3) | 0x00 */
typedef struct __attribute__((packed)) {
    uint32_t local_version; /* 本地当前版本号 */
    uint8_t  hw_ver;        /* 硬件版本 */
    uint16_t fw_ver;        /* 固件版本 */
    uint8_t  node_id;       /* 节点 ID */
} CAN_JoinFrame_t;

/* ===== EMERGENCY (紧急帧) ===== */
/* CAN ID: (0x07 << 6) | (src_node << 3) | 0x00 */
/* DLC=1, 最高优先级 */
typedef struct __attribute__((packed)) {
    uint8_t emergency_code; /* 0=急停, 1=安全光幕触发, ... */
} CAN_EmergencyFrame_t;
```

### 5.3 CAN 中断处理

```c
/* CAN FIFO0 接收中断 — 高优先级 */
void CAN_RX_IRQHandler(void) {
    CAN_RxHeaderTypeDef header;
    uint8_t data[8];

    HAL_CAN_GetRxMessage(&hcan, CAN_RX_FIFO0, &header, data);

    uint8_t func_code = (header.StdId >> 6) & 0x1F;
    uint8_t src_node  = (header.StdId >> 3) & 0x07;

    switch (func_code) {
        case CAN_FUNC_DATA_SYNC:
            CAN_HandleSync(src_node, data);
            break;
        case CAN_FUNC_HEARTBEAT:
            CAN_HandleHeartbeat(src_node, data);
            break;
        case CAN_FUNC_ACK:
            CAN_HandleAck(src_node, data);
            break;
        case CAN_FUNC_EMERGENCY:
            CAN_HandleEmergency(src_node, data);
            break;
        // ... 其他帧处理
    }
}
```

---

## 6. Modbus RTU 从站设计

### 6.1 寄存器映射

使用 Modbus 保持寄存器 (功能码 03/06/16) 映射到 RAM 向量表：

```
寄存器地址      | 读写 | 映射内容
───────────────┼─────┼──────────────────────────
0x0000          | R   | 系统状态字
0x0001          | R   | Version 低 16 位
0x0002          | R   | Version 高 16 位
0x0003          | R/W | 节点掩码 (NodeMask)
0x0010          | R/W | CMD 命令码
0x0011          | R   | ParamLen
0x0012~0x0019   | R/W | Params (8 寄存器 = 16 字节)
0x0020          | R/W | 气缸1 命令 (0=CLOSE, 1=OPEN)
0x0021          | R   | 气缸1 状态
0x0022          | R/W | 气缸2 命令
0x0023          | R   | 气缸2 状态
0x0030          | R/W | 锁定命令 (0=UNLOCK, 1=LOCKED)
0x0031          | R   | 锁定状态
0x0040          | R/W | LED 命令 (0=OFF, 1=GREEN, 2=RED, 3=YELLOW)
0x0041          | R   | LED 状态
0x0050~0x005F   | R/W | RF 通道 1~16 (每个 1 寄存器)
0x0100~0x0107   | R   | 各节点在线状态 (bit0~7 = Node1~8)
0x0110~0x0117   | R   | 各节点心跳计数
0x0200~0x027F   | R/W | 扩展参数区 (128 寄存器 = 256 字节)
```

### 6.2 从站状态机

```
Modbus 从站状态:
  IDLE → 收到帧 (地址匹配) → PROCESSING → 发送响应 → IDLE
  IDLE → 收到帧 (广播 0x00) → PROCESSING → IDLE (不响应)
  IDLE → 收到帧 (地址不匹配) → IDLE (忽略)

帧间隔: 3.5 字符时间 (约 1.75ms @ 115200bps)
响应超时: 100ms
```

### 6.3 写操作流程

```
1. PLC 通过 RS485 发送 Write Multiple Registers (0x10)
   → 写入 CMD + Params + NodeMask 寄存器
2. 从站接收 → 更新本地 RAM 向量表 → version++
3. 从站通过 CAN 广播 DATA_SYNC 帧
4. 所有节点 (包括发起节点) 的 RAM 表同步更新
5. 各节点检测 NodeMask → 匹配则执行动作
6. 从站回复 Modbus ACK
```

---

## 7. FreeRTOS 任务架构

### 7.1 任务列表

| 任务名 | 栈大小 | 优先级 | 周期 | 功能 |
|--------|--------|--------|------|------|
| defaultTask | 512B | Normal | delay(1) | 空闲 (保留) |
| SCPITask | 4KB | Low | delay(10) | SCPI 调试控制台 (保留) |
| ModBusSlaveTask | 4KB | Normal | 事件驱动 | Modbus RTU 从站处理 |
| CAN_RxTask | 2KB | High | 事件驱动 | CAN 接收处理 |
| CAN_TxTask | 2KB | Normal | 事件驱动 | CAN 发送处理 |
| StateMachineTask | 4KB | Normal | delay(50) | 主状态机处理 |
| SnapshotTask | 2KB | Low | delay(600000) | 定时快照写入 (10min) |

### 7.2 任务间通信

```
ModBusSlaveTask ──(Queue)──> CAN_TxTask     "收到写命令，触发同步广播"
CAN_RxTask ──(Queue)──> StateMachineTask    "收到远程命令，触发动作执行"
CAN_RxTask ──(直接)──> RAM_Vector           "更新反射内存"
StateMachineTask ──(直接)──> RAM_Vector     "读取本地 IO 状态，更新向量表"
Timer ──(信号量)──> SnapshotTask            "定时触发快照"
EXTI (掉电) ──(ISR→信号量)──> SnapshotTask  "紧急快照"
```

### 7.3 新增互斥锁

```c
osMutexId_t VectorMutexHandle;    /* 保护 RAM 向量表读写 */
osMutexId_t FlashMutexHandle;     /* 保护 W25Q128 操作 */
osMutexId_t COMMutexHandle;       /* 已有: 保护 SCPI/USART 资源 */
```

---

## 8. SCPI 命令扩展

### 8.1 新增节点管理命令

```
SYSTem:NODE:ID?             查询本节点 ID (1~8)
SYSTem:NODE:ID <1-8>        设置本节点 ID (需重启生效)
SYSTem:NODE:COUNt?          查询在线节点数量
SYSTem:NODE:STATus? <1-8>   查询指定节点状态
SYSTem:NODE:LIST?           列出所有在线节点

CONFigure:NODEMask <0-255>  设置广播目标节点掩码

READ:VECTor:VERSion?        查询 RAM 向量表版本号
READ:VECTor:CRC?            查询 RAM 向量表 CRC
READ:VECTor:RAW?            导出 RAM 向量表原始数据 (调试用)

CONFigure:BAUDrate <9600|115200|230400|460800>
                             扩展波特率支持
```

### 8.2 保留的原有命令

所有原有 SCPI 命令保持不变（气缸、锁定、LED、RF 开关控制等），这些命令在多机模式下操作**本节点**的 IO，不会触发 CAN 广播。跨节点控制通过 Modbus 进入。

---

## 9. 数据流详解

### 9.1 PLC 写入完整流程

```
时间线:

T=0ms    PLC ──Modbus(0x10)──> Node#1 USART3
           写入: CMD=0x01 (开门), NodeMask=0b00000110 (Node2+Node3)

T=1ms    Node#1 ModBusSlaveTask:
           - 解析 Modbus 帧
           - 申请 VectorMutex
           - 更新 RAM 向量表 (version++, cmd, params, node_mask)
           - 释放 VectorMutex
           - 发送消息到 CAN_TxTask 队列

T=2ms    Node#1 CAN_TxTask:
           - 读取 RAM 向量表 version/cmd/params/node_mask
           - 组装 CAN_DATA_SYNC 帧
           - CAN 发送 (DLC=8, ~0.13ms @ 500kbps)

T=3ms    Node#2, Node#3 CAN_RxTask (中断):
           - 解析 CAN 帧
           - 比较 version: 广播(5) > 本地(4) → 接受
           - 申请 VectorMutex
           - 更新本地 RAM 向量表
           - 释放 VectorMutex
           - 发送 ACK 帧

T=4ms    Node#2, Node#3 StateMachineTask (下个周期):
           - 检测 RAM 向量表 version 变化
           - 解析 cmd + params
           - 检查 node_mask 包含自己 → 执行动作
           - 更新 IO 输出 + 向量表中的状态字段

T<20ms   动作完成，各节点 RAM 表一致

总延迟: < 20ms (满足实时性要求)
```

### 9.2 掉线恢复流程

```
T=0      Node#2 上电启动
            - 初始化 CAN、SPI、RAM 向量表
            - 从 W25Q128 加载快照到 RAM 向量表 (version=3)
            - 发送 NODE_JOIN 广播 (附带 version=3)

T=1ms    Node#1 收到 NODE_JOIN:
            - 比较版本: Node#1 最新 version=5 > Node#2 version=3
            - 发送 DATA_SYNC 帧 (version=5, 完整向量表)

T=2ms    Node#2 收到 DATA_SYNC:
            - 版本差 > 1 → 发送 DATA_REQUEST 请求完整数据
            - Node#1 通过 DATA_RESPONSE 分包发送完整 1024B 向量表
            - Node#2 逐包接收并校验

T=15ms   Node#2 RAM 向量表恢复到 version=5
            - 发送 ACK 确认
            - 更新 node_status[1].online = 1

T<20ms   恢复完成，Node#2 与全网一致
```

### 9.3 紧急停机流程

```
T=0      Node#3 急停按钮被按下 (硬件中断)
            - ISR 检测到 STOP_BUTTON = 1
            - 设置 RAM 向量表: sys_state = EMERGENCY, version++
            - 立即发送 CAN_EMERGENCY 帧 (最高优先级, DLC=1)

T=0.5ms  Node#1, Node#2 CAN_RxTask:
            - 收到 EMERGENCY 帧 → 立即通知 StateMachineTask
            - 更新 RAM 向量表 sys_state = EMERGENCY
            - 执行紧急动作 (开门、解锁)

T=1ms    Node#3 SnapshotTask:
            - 紧急快照写入 W25Q128 (利用电容保持供电)

T<5ms   所有节点进入 EMERGENCY 状态
```

---

## 10. 启动流程

```
main()
  │
  ├─ HAL_Init()
  ├─ SystemClock_Config()        (72MHz)
  ├─ MX_GPIO_Init()
  ├─ MX_DMA_Init()
  ├─ MX_IWDG_Init()
  ├─ MX_USART1_UART_Init()       (SCPI 控制台)
  ├─ MX_USART3_UART_Init()       (Modbus 从站)
  ├─ MX_CAN_Init()               ★ 新增: CAN1 500kbps
  ├─ MX_SPI2_Init()              ★ 新增: SPI2 18MHz
  │
  ├─ Flash_Init()                (内部 Flash 配置)
  ├─ W25Q128_Init()              ★ 新增: 外部 Flash 初始化
  │
  ├─ RAM_Vector_Init()           ★ 新增: 初始化 RAM 向量表
  │   ├─ 从 W25Q128 加载最新快照 (如有)
  │   └─ 若无快照 → 填充默认值 → version=1
  │
  ├─ CAN_Protocol_Init()         ★ 新增: CAN 协议栈初始化
  │   ├─ 配置 CAN 滤波器
  │   └─ 使能 CAN 中断
  │
  ├─ SCPI_Init()                 (保留: 调试控制台)
  ├─ SCPI_SyncIdnFromFlash()
  │
  ├─ osKernelInitialize()
  ├─ MX_FREERTOS_Init()          ★ 扩展: 新增任务和队列
  │   ├─ ModBusSlaveTask
  │   ├─ CAN_RxTask
  │   ├─ CAN_TxTask
  │   ├─ StateMachineTask
  │   ├─ SnapshotTask
  │   └─ 互斥锁/队列
  │
  ├─ osKernelStart()
  │
  └─ Task 并发执行
      ├─ Node#1 发送 NODE_JOIN 广播
      ├─ 等待其他节点响应
      └─ 进入正常同步循环
```

---

## 11. 实施计划

### 阶段 1: 硬件驱动层 (预计 3 天)

| 模块 | 文件 | 内容 |
|------|------|------|
| CAN 驱动 | `Hardware/CAN/Inc/can_bus.h`, `Src/can_bus.c` | CAN1 初始化、发送、接收、滤波器配置 |
| SPI Flash 驱动 | `Hardware/Flash/Inc/w25q128.h`, `Src/w25q128.c` | W25Q128 初始化、读/写/擦除、CRC 校验 |
| GPIO 更新 | `Core/Inc/gpio.h`, `Core/Src/gpio.c` | 添加 CAN/SPI 引脚配置 |
| CubeMX IOC | `.ioc` | 使能 CAN1 (Remap)、SPI2、更新引脚分配 |

### 阶段 2: 核心数据层 (预计 4 天)

| 模块 | 文件 | 内容 |
|------|------|------|
| RAM 向量表 | `Core/Inc/ram_vector.h`, `Core/Src/ram_vector.c` | 向量表结构体、初始化、读写、校验接口 |
| 快照管理 | `Core/Inc/snapshot.h`, `Core/Src/snapshot.c` | 快照写入/加载、双区切换、日志式存储 |
| CAN 协议栈 | `Core/Inc/can_protocol.h`, `Core/Src/can_protocol.c` | 帧组装/解析、同步流程、心跳、ACK 管理 |
| Modbus 从站 | `Hardware/BsmRelay/Inc/modbus_slave.h`, `Src/modbus_slave.c` | 从站状态机、寄存器映射、帧处理 |
| 分散加载 | `*.sct` | 锁定 RAM 向量表地址 0x2000A000 |

### 阶段 3: 应用集成层 (预计 4 天)

| 模块 | 文件 | 内容 |
|------|------|------|
| FreeRTOS 扩展 | `Core/Src/freertos.c` | 新增任务、队列、互斥锁 |
| 状态机适配 | `Hardware/BsmRelay/Src/StateMachine.c` | 多节点状态联动、紧急停机会广播 |
| SCPI 扩展 | `Hardware/libscpi/port/scpi-def.c` | 新增节点管理命令 |
| main.c 适配 | `Core/Src/main.c` | 新增初始化流程 |
| Flash 配置扩展 | `Hardware/Flash/Inc/flash.h`, `Src/flash.c` | 扩展 node_id 字段，复用 device_id |

### 阶段 4: 测试验证 (预计 3 天)

| 测试项 | 内容 |
|--------|------|
| CAN 物理层 | 2 节点 CAN 通信、500kbps/1Mbps 稳定性 |
| RAM 同步 | 多节点同时修改、版本冲突、掉线恢复 |
| W25Q128 | 读写擦除验证、双区冗余切换、寿命测试脚本 |
| Modbus 从站 | PLC 读写寄存器、异常响应、超时处理 |
| 系统集成 | 8 节点满载测试、急停响应时间、掉电恢复 |

---

## 12. 内存预算分析

```
SRAM 总计: 48KB

固定开销:
  .data + .bss            ~4KB   (全局变量/DMA缓冲区/SCPI缓冲区)
  FreeRTOS 内核            ~3KB   (调度器/队列/信号量)
  FreeRTOS 堆 (heap_4)    10KB   (任务栈/动态分配)
  主栈 (MSP)               1KB

任务栈:
  SCPITask                 4KB
  ModBusSlaveTask          4KB
  CAN_RxTask               2KB
  CAN_TxTask               2KB
  StateMachineTask         4KB
  SnapshotTask             2KB
  defaultTask            0.5KB
  ─────────────────────────────
  任务栈合计:           18.5KB

核心数据:
  RAM 向量表               1KB   (0x2000A000)
  CAN 协议缓冲             1KB   (收发队列)
  Modbus 帧缓冲            1KB   (256B × 2 + 解析缓冲)
  W25Q128 页缓冲         0.25KB  (共享缓冲)
  ─────────────────────────────
  核心数据合计:         3.25KB

总使用: 4 + 3 + 10 + 1 + 18.5 + 3.25 ≈ 39.75KB
剩余:   48 - 39.75 = 8.25KB (安全余量)
```

---

## 13. 命令码定义

```c
/* 公共区命令码 (供 Modbus 和内部状态机使用) */
typedef enum {
    CMD_NONE            = 0x0000,   /* 无命令 */
    CMD_SYS_RESET       = 0x0001,   /* 系统复位 */
    CMD_SYS_EMERGENCY   = 0x0002,   /* 急停 */

    /* 气缸控制 */
    CMD_CYLINDER_OPEN   = 0x0100,   /* 开门 (参数: 通道号 1~2) */
    CMD_CYLINDER_CLOSE  = 0x0101,   /* 关门 (参数: 通道号 1~2) */

    /* 锁定控制 */
    CMD_LOCK            = 0x0200,   /* 锁定 */
    CMD_UNLOCK          = 0x0201,   /* 解锁 */

    /* LED 控制 */
    CMD_LED_OFF         = 0x0300,   /* 灭灯 */
    CMD_LED_GREEN       = 0x0301,   /* 绿灯 */
    CMD_LED_RED         = 0x0302,   /* 红灯 */
    CMD_LED_YELLOW      = 0x0303,   /* 黄灯 */

    /* RF 开关 */
    CMD_RF_CONNECT      = 0x0400,   /* RF 通道切换 (参数: 通道 0~15, 端口 1~16) */
    CMD_RF_DISCONNECT   = 0x0401,   /* RF 通道断开 */

    /* 系统查询 */
    CMD_QUERY_STATUS    = 0x0500,   /* 查询节点状态 */
    CMD_QUERY_VERSION   = 0x0501,   /* 查询版本信息 */

    /* 快照控制 */
    CMD_SNAPSHOT_SAVE   = 0x0600,   /* 强制保存快照 */
    CMD_SNAPSHOT_LOAD   = 0x0601,   /* 强制加载快照 */
} Vector_Cmd_t;
```

---

## 14. 风险与应对

| 风险 | 影响 | 应对措施 |
|------|------|----------|
| CAN 总线冲突 (多节点同时发送) | 数据丢失 | CAN 控制器硬件仲裁 + ACK 超时重传 (3 次) |
| W25Q128 写入中掉电 | 快照损坏 | 双区冗余 + 写入 CRC + 上电校验回退 |
| 版本号溢出 (uint32_t) | 同步逻辑失效 | 按 200 次/天更新计算, 需 ~58,000 年溢出 — 可忽略 |
| RAM 向量表地址冲突 | 数据被覆盖 | 分散加载文件锁定地址 + 启动时静态断言 |
| Modbus 从站响应超时 | PLC 通信失败 | 从站应答超时 < 50ms, PLC 重试机制 |
| 新节点加入时的网络风暴 | CAN 带宽占满 | 限制同时加入的节点数, 分时逐个同步 |

---

## 15. 文件清单

### 新增文件

```
Core/Inc/ram_vector.h            RAM 向量表头文件
Core/Src/ram_vector.c            RAM 向量表实现

Core/Inc/can_protocol.h          CAN 协议栈头文件
Core/Src/can_protocol.c          CAN 协议栈实现

Core/Inc/snapshot.h              快照管理头文件
Core/Src/snapshot.c              快照管理实现

Hardware/Flash/Inc/w25q128.h     W25Q128 驱动头文件
Hardware/Flash/Src/w25q128.c     W25Q128 驱动实现

Hardware/Can/Inc/can_bus.h       CAN HAL 封装头文件
Hardware/Can/Src/can_bus.c       CAN HAL 封装实现

Hardware/BsmRelay/Inc/modbus_slave.h  Modbus 从站头文件
Hardware/BsmRelay/Src/modbus_slave.c  Modbus 从站实现
```

### 修改文件

```
Core/Inc/main.h                  添加新外设宏定义
Core/Src/main.c                  添加初始化流程
Core/Src/freertos.c              添加任务/队列/互斥锁
Core/Src/gpio.c                  添加 CAN/SPI 引脚配置
Core/Src/stm32f1xx_it.c          添加 CAN 中断处理
Hardware/Flash/Inc/flash.h       扩展 node_id 相关字段
Hardware/Flash/Src/flash.c       扩展 node_id 相关函数
Hardware/libscpi/port/scpi-def.c 添加节点管理命令
Hardware/libscpi/port/scpi-def.h 添加命令声明
Hardware/BsmRelay/Src/StateMachine.c  多节点联动适配
MDK-ARM/*.sct                    锁定向量表地址
```

---

## 附录 A: STM32F103RCT6 可用引脚速查

```
已用引脚:
  PA6  (RF_C5)       PA7  (RF_C6)       PA8  (LED)
  PA9  (USART1_TX)   PA10 (USART1_RX)   PA13 (SWDIO)
  PA14 (SWCLK)       PA15 (JTAG_TDI)    PB0  (RF_C7)
  PB1  (RF_C8)       PB3  (JTAG_TDO)    PB10 (USART3_TX)
  PB11 (USART3_RX)   PC6  (RF_C1)       PC7  (RF_C2)
  PC8  (RF_C3)       PC9  (RF_C4)

新增引脚 (无冲突):
  PB8  (CAN1_RX, Remap)    PB9  (CAN1_TX, Remap)
  PB12 (SPI2_NSS)          PB13 (SPI2_SCK)
  PB14 (SPI2_MISO)         PB15 (SPI2_MOSI)

剩余未用引脚:
  PA0~PA5, PA11~PA12, PB4~PB7, PB12~PB15(已用)
  PC0~PC5, PC10~PC15, PD0~PD2
```

## 附录 B: 参考文档

- STM32F103RCT6 Datasheet (RM0008)
- W25Q128JV Datasheet
- MCP2551 CAN Transceiver Datasheet
- Modbus Application Protocol Specification V1.1b3
- SCPI Standard V1999.0
