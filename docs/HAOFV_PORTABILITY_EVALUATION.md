# HAOFV 架构可移植性评估报告

## 文档信息

| 项目 | 内容 |
|---|---|
| 文档版本 | V1.0 |
| 评估日期 | 2026-06-29 |
| 评估对象 | RP2350_TRIG HAOFV 架构（Hybrid Active Object Function Block Vector Architecture） |
| 评估范围 | 代码级平台耦合分析 + 架构级可迁移性判断 + 目标平台迁移路线 |
| 代码基线 | TASK-20260626-020 完成后状态 |

---

## 1. 执行摘要

### 1.1 总体评分

| 维度 | 评分 | 说明 |
|---|---|---|
| OTA 子系统 | ★★★★★★☆☆☆☆ 6/10 → 9/10 | portable_ota 库已为多平台设计，修复 SRAM/XIP 硬编码后可达 9 分 |
| 事件/仲裁/诊断/功能块 | ★★★★★★★★★☆ 9/10 | 纯逻辑组件，零硬件依赖 |
| UI 子系统 | ★★★★★★★☆☆☆ 7/10 | U8G2 高度可移植，仅需 SPI 适配 |
| SCPI 命令解析 | ★★★★★★★★☆☆ 8/10 | 解析器无平台依赖，SCPI 标准兼容 |
| SCPI 传输层 | ★★★☆☆☆☆☆☆☆ 3/10 | 当前与 pico/stdio 紧耦合，修复后可达 8/10 |
| MCU 驱动层 | ★★★★☆☆☆☆☆☆ 4/10 | 接口设计干净但实现与 Pico SDK 深度绑定 |
| 硬实时触发路径 (PIO/DMA) | ★☆☆☆☆☆☆☆☆☆ 1/10 | RP 系列独有硬件，无可移植替代 |
| Bootloader | ★★★★☆☆☆☆☆☆ 4/10 | 含 ARM 汇编和 RP2350 特定寄存器操作 |
| **整体架构可迁移性** | **★★★★★★☆☆☆☆ 6/10** | 上层优秀，底层锁定；修复 5 个 P0/P1 项后可达 7.5/10 |

### 1.2 关键发现

1. **两极分化明显**：管理域（事件总线、资源仲裁、功能块、诊断）跨平台零成本复用；硬实时域（PIO、DMA 寄存器访问）与 RP2350 深度绑定。
2. **最大迁移屏障是 PIO**：3 个 PIO block、12 个状态机承载了全部同步触发硬实时功能。PIO 是 RP 系列独有的可编程 IO 协处理器，任何其他 MCU 都没有等价硬件。
3. **五个低成本改进可显著提升可移植性**：抽象 SCPI 传输层、清理 OSAL port 泄露、参数化 SRAM/XIP 地址、定义触发 IO 原语接口、建立 DMA 驱动抽象。总工作量约 4 周，可将整体评分从 6/10 提升到 7.5/10。
4. **portable_ota 库是可移植性标杆**：已从第一天起为 RP2350 + STM32 双平台设计，14 个 platform ops 函数指针实现了干净的依赖注入，迁移到新 MCU 只需实现这 14 个函数。

### 1.3 适用迁移场景

| 场景 | 难度 | 估算工作量 | 适用条件 |
|---|---|---|---|
| 新 RP2350 产品（不同引脚/外设分配） | 极低 | 1-2 天 | 仅需重写 `board_config.h` |
| 迁移到 RP2040 | 低 | 1-2 周 | 2 个 PIO block 需重新分配资源 |
| 迁移到 STM32F4（保留 OTA + SCPI + 基本触发） | 中-高 | 4-8 周 | 用 Timer + DMA 替代 PIO |
| 迁移到 STM32F4（完整触发功能） | 高 | 12+ 周 | 需外部 FPGA/CPLD 替代 PIO |
| 迁移到 STM32H7/G4（利用 HRTIM） | 中 | 4-6 周 | HRTIM 可替代部分 PIO 精确脉冲功能 |
| 迁移到 ESP32-S3 | 中-高 | 4-8 周 | RMT 外设可部分替代 PIO 脉冲输出 |
| 仅复用上层组件（OTA + 事件 + 仲裁）到任何 Cortex-M | 低 | 1-2 周 | 放弃 PIO 触发，保留管理域全部功能 |

---

## 2. 评估方法

### 2.1 评估维度

1. **代码级平台耦合度**：统计 `#include "hardware/"` 和 `#include "pico/"` 的直接引用次数、SDK API 调用分布、平台特定寄存器访问模式
2. **架构抽象质量**：逐层评估分层依赖方向是否正确、接口是否对平台透明、是否存在跨层泄露
3. **迁移成本估算**：按子系统分解迁移工作量，给出人天/人周级估算
4. **目标平台适配难度**：分别评估 STM32、ESP32、RP2040 等典型目标平台

### 2.2 代码扫描范围

扫描了以下目录的全部源文件（排除 `third_party/`、`build*/`、`tools/`）：

- `application/` — 应用入口和胶水逻辑
- `boards/rp2350_trig/` — 板级定义
- `components/` — 功能组件（sync_io, sync_trigger, ota_manager, diagnostics, event_bus, resource_arbiter, sync_config_ui）
- `drivers/mcu/` — MCU 外设驱动（spi, i2c, uart, flash, watchdog）
- `drivers/external/` — 外部器件驱动（lcd）
- `middleware/` — 中间件适配层（portable_ota_port, scpi_port, u8g2_port）
- `osal/` — OS 抽象层
- `bootloader/` — 启动程序

### 2.3 评分标准

| 分数 | 含义 |
|---|---|
| 9-10 | 零改动或仅需编译期宏切换即可迁移 |
| 7-8 | 接口已抽象，需重写底层 port 实现（工作量 < 1 周） |
| 5-6 | 接口设计合理但实现含平台耦合，需中等改造（1-3 周） |
| 3-4 | 存在架构级平台依赖，需重写适配层（3-6 周） |
| 1-2 | 深度绑定特定硬件，无等价替代（需重新设计硬件方案） |

---

## 3. 代码级平台耦合分析

### 3.1 Pico SDK 直接引用统计

对项目源文件（排除 third_party）的扫描结果：

#### `#include "pico/"` 引用分布

| 头文件 | 引用文件数 | 引用位置 |
|---|---|---|
| `pico/stdlib.h` | 4 | `application/src/main.c`, `boards/.../board.c`, `components/diagnostics/src/diagnostics.c`, `osal/port/baremetal/osal_baremetal.c`, `osal/port/freertos/osal_freertos.c` |
| `pico/time.h` | 1 | `components/sync_trigger/src/trigger_measure.c` |
| `pico/platform.h` | 1 | `drivers/mcu/watchdog/src/drv_watchdog.c` |
| `pico/error.h` | 1 | `middleware/scpi_port/src/scpi_port.c` |
| `pico/stdio.h` | 1 | `middleware/scpi_port/src/scpi_port.c` |

#### `#include "hardware/"` 引用分布

| 头文件 | 引用文件数 | 引用位置 |
|---|---|---|
| `hardware/gpio.h` | 5 | `board.c`, `sync_io.c`, `drv_spi.c`, `drv_i2c.c`, `drv_uart.c`, `lcd_st7789.c` |
| `hardware/pio.h` | 2 | `board_config.h`, `sync_io.c`, `sync_trigger.c` |
| `hardware/dma.h` | 1 | `sync_io.c` |
| `hardware/clocks.h` | 2 | `board.c`, `sync_io.c` |
| `hardware/irq.h` | 1 | `sync_io.c` |
| `hardware/sync.h` | 3 | `bootloader_main.c`, `osal_baremetal.c`, `drv_flash.c`, `drv_watchdog.c` |
| `hardware/flash.h` | 1 | `drv_flash.c` |
| `hardware/spi.h` | 2 | `board_config.h`, `drv_spi.h`, `lcd_st7789.h` |
| `hardware/i2c.h` | 2 | `board_config.h`, `drv_i2c.h` |
| `hardware/uart.h` | 2 | `board_config.h`, `drv_uart.h` |
| `hardware/watchdog.h` | 1 | `drv_watchdog.c` |
| `hardware/timer.h` | 1 | `trigger_measure.c` |
| `hardware/structs/scb.h` | 1 | `bootloader_main.c` |

**汇总**：25+ `hardware/` 直接引用，8 个 `pico/` 直接引用，链接 11 个 Pico SDK 库。

### 3.2 平台特定硬件访问模式

#### PIO 指令内存运行时 Patch（最深耦合）

```c
// sync_io.c — 直接修改 PIO 程序指令
uint16_t *instr = (uint16_t *)&pio->instr_mem[offset];
instr[1] = (instr[1] & 0x1f) | (trigger_offset << 5);
```

这绕过了 Pico SDK 的 `pio_add_program()` 封装，直接操作 PIO 指令内存字。任何其他 MCU 都没有类似硬件。

#### DMA 寄存器直接访问（4 处）

```c
// sync_io.c — 绕过 SDK 直接操作 DMA 硬件寄存器
dma_hw->ints0 = ints;                                  // 中断状态清除
dma_hw->ch[n].read_addr = seq_table_addr;              // 读地址复位
dma_hw->ch[n].al1_transfer_count_trig = seq_length;    // 传输计数触发
dma_hw->ch[n].transfer_count;                          // 剩余传输计数查询
```

这是为实现无限循环 DMA 传输而绕过 SDK ring buffer（RP2350 存在对齐 bug）的性能优化。迁移到其他 DMA 控制器需要完全不同的方案。

#### ARM 汇编和 SCB 寄存器

```c
// bootloader_main.c
scb_hw->vtor = vector_addr;     // RP2350 向量表偏移寄存器
__asm volatile("cpsid i");      // ARM 特权指令
__asm volatile("msr msp, %0" : : "r"(sp));  // ARM 主栈指针设置
```

这些是 Cortex-M 通用指令（非 RP2350 独有），但使用方式与 RP2350 SDK 的数据结构绑定。

### 3.3 硬编码平台常量

| 常量 | 值 | 位置 | 影响 |
|---|---|---|---|
| XIP Flash 基址 | `0x10000000u` | `drivers/mcu/flash/inc/drv_flash.h` | 硬编码，非 board_config 宏 |
| SRAM 起止地址 | `0x20000000u` - `0x20082000u` | `bootloader/`, `portable_ota_image_port.c` | 以 `RP2350_` 前缀命名，非通用 `BOARD_` 宏 |
| XIP 偏移常量 | `0x10040000` (Slot A), `0x101C0000` (Slot B) | 多处 | linker script 中定义，但 C 代码中也有硬编码引用 |
| 系统时钟 | `250000000u` (250 MHz 超频) | `board_config.h` | 通过 `BOARD_SYS_CLOCK_HZ` 宏定义，已参数化 |

---

## 4. 子系统可移植性逐项评估

### 4.1 低迁移难度子系统（9-10 分）

#### 4.1.1 portable_ota 库（9/10）

**架构特点**：14 个 platform ops 函数指针实现依赖注入，核心逻辑与平台完全隔离。

```c
// 平台无关接口 — 迁移只需实现这 14 个函数
typedef struct {
    bool (*flash_read)(uint32_t offset, void *buffer, uint32_t size);           // → HAL_FLASH_Read / memcpy
    bool (*flash_erase)(uint32_t offset, uint32_t size);                        // → HAL_FLASHEx_Erase
    bool (*flash_program)(uint32_t offset, const void *data, uint32_t size);    // → HAL_FLASH_Program
    bool (*mark_pending)(pota_slot_t slot, uint32_t image_size, uint32_t crc);  // → metadata 写入
    bool (*confirm_active)(void);                                                // → metadata 确认
    bool (*validate_vector)(uint32_t offset, uint32_t size, uint32_t run_off);  // → 向量表校验
    void (*ota_lock)(void);                                                      // → OSAL 互斥锁
    void (*ota_unlock)(void);                                                    // → OSAL 互斥锁
    void (*ota_yield_or_delay)(void);                                            // → OSAL delay
    void (*feed_watchdog)(void);                                                 // → 目标平台 WDT
    void (*invalidate_cache)(void);                                              // → CMSIS __DSB/__ISB
    void (*reboot)(void);                                                        // → NVIC_SystemReset
    uint32_t (*time_ms)(void);                                                   // → OSAL tick
    void (*log)(int level, const char *message);                                 // → 目标日志输出
} pota_platform_ops_t;
```

**STM32 移植已具指南**：`third_party/portable_ota/ports/README.md` 中明确给出了每个函数的 STM32 HAL 实现参考，包括双 bank flash、单 bank flash + 外部 QSPI 等不同布局的适配建议。

**待修复项**：
- `portable_ota_image_port.c` 中硬编码 `RP2350_SRAM_BASE/END`，应改为 `BOARD_SRAM_BASE/END` 宏（0.5 天）
- `drv_flash.h` 中 XIP 基址硬编码 `0x10000000u`，应改为 `BOARD_XIP_BASE` 宏（0.5 天）

#### 4.1.2 事件总线 + 资源仲裁 + 诊断 + 功能块模型（9/10）

这四个组件**完全零硬件依赖**：

| 组件 | 依赖 | 迁移工作 |
|---|---|---|
| `event_bus` | `osal_critical_enter/exit` | 零改动 — OSAL 已抽象 |
| `resource_arbiter` | 无 | 零改动 — 纯位掩码逻辑 |
| `diagnostics` | `osal_uptime_ms` | 零改动 — OSAL 已抽象 |
| `function_block` (ECC 引擎) | 无 | 零改动 — 纯状态机逻辑 |

所有数据结构、状态机引擎、环形缓冲实现均为标准 C，可在任何 C99/C11 编译器下工作。

#### 4.1.3 SCPI 命令解析器（8/10）

`third_party/scpi-parser` 是第三方平台无关库。SCPI 命令树定义（`scpi-def.c` 中约 50 个命令的注册表）只依赖 `scpi-parser` 的类型系统，不依赖任何硬件。迁移到新平台只需重新对接传输层。

### 4.2 中等迁移难度子系统（5-8 分）

#### 4.2.1 UI 子系统 — sync_config_ui（7/10）

**现状**：U8G2 单色 page buffer 模式 + 三栏运行时看板（SYSTEM / TRIGGER / OTA），通过 `resource_arbiter` 申请 `SPI0 + LCD`。

**迁移要点**：U8G2 上游支持数十种 MCU 和数十种显示控制器。迁移时只需提供目标平台的 SPI 发送函数。LCD 驱动（ST7789）的命令集是标准化的，`lcd_st7789.c` 中的命令序列可直接复用，仅需替换底层的 `spi_write` 实现。

**工作量**：1-2 天（重写 SPI 接口 + U8G2 callback）。

#### 4.2.2 MCU 驱动层（4/10）

| 驱动 | 接口质量 | 实现耦合 | 迁移难度 |
|---|---|---|---|
| `drv_flash` | ★★★★ 接口干净（erase/program/read） | `flash_range_erase/program` + `0x10000000` 硬编码 | 中 — 1-2 天 |
| `drv_spi` | ★★★ 接口使用 `spi_inst_t*`（Pico SDK 类型） | `spi_init()`, `gpio_set_function()` | 中 — 1-2 天 |
| `drv_i2c` | ★★★ 接口使用 `i2c_inst_t*`（Pico SDK 类型） | `i2c_init()`, `gpio_set_function()` | 中 — 1-2 天 |
| `drv_uart` | ★★★ 接口使用 `uart_inst_t*`（Pico SDK 类型） | `uart_init()`, `gpio_set_function()` | 中 — 1-2 天 |
| `drv_watchdog` | ★★★★ 3 个函数 | `watchdog_enable/update/reboot()` | 低 — 0.5 天 |

**关键问题**：驱动头文件中直接暴露 `spi_inst_t`、`i2c_inst_t`、`uart_inst_t` 等 Pico SDK 类型，导致所有包含这些头文件的代码被间接污染。迁移到 STM32 时需将这些类型替换为不透明指针或 `void*`。

#### 4.2.3 OSAL 抽象层（接口 9/10，实现 5/10）

**接口设计优秀**：仅 8 个函数 + 4 个类型，是迁移到新平台的最小契约。

**实现存在泄露**：
- `osal_baremetal.c` 直接依赖 `hardware/sync.h`（`save_and_disable_interrupts`）
- `osal_freertos.c` 直接依赖 `pico/stdlib.h`（`sleep_ms`, `to_ms_since_boot`）
- 两个 port 均缺少 ISR 抽象（无 `osal_isr_enter/exit`）

**修复方案**（1 天）：将 `sleep_ms` 替换为 `osal_delay_ms()` 的自实现（忙等待或调度器阻塞），将 `to_ms_since_boot` 替换为 FreeRTOS `xTaskGetTickCount()`。

#### 4.2.4 Bootloader（4/10）

**现状**：裸机程序，约 40 KB。最小硬件初始化 → metadata 读取 → slot 校验 → 跳转/回滚/复制。

**平台绑定项**：
- ARM 汇编特权指令（`cpsid i`, `msr msp`）— Cortex-M 通用，可保留
- `scb_hw->vtor` — Cortex-M 通用，但结构体名是 RP2350 SDK 特有的
- XIP Flash 直接内存访问 — 依赖 XIP 地址映射
- `hardware/sync.h` — RP2350 中断控制
- `hardware/structs/scb.h` — RP2350 SCB 寄存器结构体

**迁移要点**：VTOR/MSP 操作在 STM32 上使用 CMSIS 标准 API 替代（`SCB->VTOR`, `__set_MSP`）。中断控制使用 `__disable_irq()/__enable_irq()`。Flash 读取使用 `memcpy` 从 XIP 地址或 HAL Flash API。

**工作量**：3-5 天（替换平台特定寄存器操作 + 对接目标 Flash API）。

### 4.3 高迁移难度子系统（1-3 分）

#### 4.3.1 SCPI 传输层（3/10，修复后 8/10）

**现状**：SCPI 传输直接调用 Pico SDK stdio：

```c
// 当前实现 — 直接耦合 pico/stdio.h
int scpi_read_char(void)    { return getchar_timeout_us(0); }    // Pico SDK
void scpi_write_char(char c) { putchar_raw(c); }                 // Pico SDK
void scpi_flush(void)        { stdio_flush(); }                  // Pico SDK
```

stdio 在 Pico SDK 中默认启用 USB CDC（通过 TinyUSB），也可配置为 UART。但 API 是 SDK 特有的。

**缺失的抽象层**：

```c
// 建议增加的传输抽象接口（当前未实现）
typedef struct {
    int  (*read_char)(uint32_t timeout_us);
    void (*write_char)(char c);
    void (*flush)(void);
} scpi_transport_ops_t;

// 迁移到新平台只需实现 3 个函数
// STM32 UART 版本：HAL_UART_Receive(..., timeout) / HAL_UART_Transmit(...)
// STM32 USB CDC 版本：CDC_Transmit_FS(...) / CDC_Receive_FS(...)
```

**修复工作量**：2-3 天（定义接口 + 重构 `scpi_port.c` + 实现 Pico stdio port + 编译验证）。

#### 4.3.2 硬实时触发路径 — sync_io + sync_trigger + PIO 程序（1/10）

**这是整个架构中最大的迁移屏障**。当前全部 3 个 PIO block、12 个状态机被同步触发系统占用。

**PIO 程序清单与功能等效分析**：

| PIO 程序 | 指令数 | 功能 | 性能指标 | STM32 替代方案 | ESP32-S3 替代方案 |
|---|---|---|---|---|---|
| `sync_capture_4bit` | 4 | 4-bit 并行输入采样 | 10-50 MHz, DMA 连续 | Timer DMA burst + GPIO IDR 并行读取 | RMT 接收模式 + DMA |
| `sync_pulse` | 5 | 精确脉宽脉冲 | 6.7ns 分辨率 @150MHz | HRTIM（仅 STM32G4/H7, ~200ps）或普通 Timer（~50ns） | RMT 脉冲输出（12.5ns @80MHz） |
| `sync_clock` | 2 | 可编程频率时钟 | 最高 75 MHz | Timer output compare（~PCLK/2） | LEDC PWM |
| `seq_step` | 3 | 3 指令硬件闭环序列步进 | ~20ns 输入→输出延迟 | 不可直接替代。需 Timer gated mode + DMA → GPIO ODR，延迟 ~200ns+ | 不可直接替代 |
| `seq_step_gated` | 4 | 带门控的序列步进 | 同上 + gate 判定 | 同上 + 外部 gate 逻辑 | 同上 + 外部 gate 逻辑 |
| `enc_count` | 24 | 编码器计数 + 比较触发 | ~87ns/步, 最高 11.5 MHz | Timer encoder mode（STM32 原生支持，4x 解码）+ 中断/比较触发 | PCNT + 比较中断 |

**核心矛盾**：`seq_step`（3 条 PIO 指令的死循环：等低→等高→输出）是整个架构中"ARM 后 CPU 零介入"设计的基石。这种硬件自主闭环在任何其他 MCU 上都无法以同等确定性实现。

**迁移策略**：

```
选项 A：放弃硬件闭环，使用 Timer + DMA 近似
  - 输入→输出延迟从 ~20ns 增加到 ~200ns+
  - 抖动从 <6.7ns 增加到 ~50-100ns
  - 适合对时序要求不苛刻的工业应用
  - 工作量：3-4 周

选项 B：保留架构语义，用外部 FPGA/CPLD 实现 PIO 功能
  - 硬件延迟和抖动可与 RP2350 PIO 持平或更优
  - 增加 BOM 成本和板面积
  - 工作量：8-12 周（含 FPGA 开发）

选项 C：仅迁移管理域，放弃硬实时触发功能
  - OTA、事件、仲裁、诊断、UI 全部保留
  - 触发功能在新平台上用不同方案重新开发
  - 工作量：1-2 周（只迁移上层组件）
```

**工作量**：取决于选项，3-40 周。

#### 4.3.3 DMA 寄存器直接访问（1/10）

`sync_io.c` 中 4 处绕过 SDK 直接访问 `dma_hw->ch[n]` 的寄存器字段。这是为了解决 RP2350 DMA ring buffer 模式的对齐 bug 而做的性能优化。迁移到其他 DMA 控制器（如 STM32 MDMA 或通用 DMA）需要完全不同的实现。

**修复方案**（1 周）：在 `drivers/mcu/dma/` 下建立 DMA 驱动抽象层，提供 `drv_dma_channel_reset_read_addr()`、`drv_dma_channel_restart_transfer()` 等接口，将寄存器直接访问封装在驱动层内。

---

## 5. 架构可移植性设计评价

### 5.1 优秀设计实践

| 设计决策 | 可移植性贡献 | 评价 |
|---|---|---|
| portable_ota 14 函数指针依赖注入 | 定义了清晰的平台契约，迁移只需实现接口 | ★★★★★ 标杆 |
| 语义 IO 通道（`TRIG_IN`, `ARM_IN`...） | 上层不被 GPIO 号污染，迁移只需改 board_config | ★★★★★ |
| 单向依赖链 `app → components → boards → drivers → osal → SDK` | 上层组件自动获得平台无关性 | ★★★★ |
| 表驱动模式表（`s_mode_table[]`） | 新增/迁移触发模式不改变 ECC 引擎 | ★★★★ |
| 通用 ECC 执行引擎（`fb_ecc_execute()`） | 状态机逻辑与业务完全解耦，全平台复用 | ★★★★ |
| OSAL 最小接口（8 函数 + 4 类型） | 迁移到新 OS/平台仅需 ~100 行代码 | ★★★★ |
| `_Static_assert` 布局断言（portable_ota_port） | 编译期保证跨平台结构体兼容性 | ★★★★ |

### 5.2 需要改进的设计

| 问题 | 影响 | 优先级 |
|---|---|---|
| SCPI 传输层无抽象 | 整个 SCPI 子系统依赖 `pico/stdio.h`，迁移需重写 | **P0** |
| OSAL port 实现包含 `pico/stdlib.h` | OSAL 声称的平台无关性被内部实现破坏 | **P0** |
| XIP 基址 + SRAM 起止地址硬编码 | 迁移到不同内存映射的 MCU 需修改多处 | **P1** |
| 驱动头文件暴露 `spi_inst_t` 等 SDK 类型 | 包含驱动的代码被间接污染 Pico SDK 类型 | **P2** |
| DMA 寄存器直接访问散落在 `sync_io.c` | 性能优化代码与业务逻辑耦合，无法复用 | **P2** |
| 无 ISR 抽象（OSAL 缺 `osal_isr_*`） | ISR 中的事件投递无平台无关封装 | **P2** |
| PIO 指令运行时 patch 在初始化代码中 | 过于精巧的平台特性使用，无移植可能 | 接受（RP 特有） |

---

## 6. 改进建议与实施路线

### 6.1 改进清单

| # | 改进项 | 优先级 | 工作量 | 影响范围 | 提升评分 |
|---|---|---|---|---|---|
| 1 | 抽象 SCPI 传输层 — 定义 `scpi_transport_ops_t` 接口（read_char/write_char/flush），重构 `scpi_port.c` 解除 `pico/stdio.h` 依赖 | **P0** | 2-3 天 | `middleware/scpi_port/` | +0.5 |
| 2 | 清理 OSAL port 泄露 — 将 `sleep_ms`/`to_ms_since_boot` 替换为内部实现，移除 `pico/stdlib.h` 包含 | **P0** | 1 天 | `osal/port/baremetal/`, `osal/port/freertos/` | +0.3 |
| 3 | 参数化 SRAM/XIP 地址 — `RP2350_SRAM_BASE/END` → `BOARD_SRAM_BASE/END`，`DRV_FLASH_XIP_BASE` → 来自 `board_config.h` | **P1** | 0.5 天 | `bootloader/`, `portable_ota_image_port.c`, `drv_flash.h` | +0.2 |
| 4 | 新增 `drivers/mcu/dma/` 抽象 — 将 DMA 通道操作从 `sync_io.c` 收口到驱动层 | **P1** | 1 周 | `sync_io.c`, 新增 `drv_dma` | +0.3 |
| 5 | 定义触发 IO 原语接口 — `trigger_io_ops_t`（arm/disarm/pulse/read_capture），使 PIO 实现成为可替换的 port | **P2** | 1-2 周 | `sync_io`, 新增抽象接口 | +0.3（架构层面） |
| 6 | 驱动头文件类型抽象 — 将 `spi_inst_t*`/`i2c_inst_t*`/`uart_inst_t*` 替换为不透明类型或 `void*` | **P2** | 1 周 | 所有驱动头文件 | +0.2 |
| 7 | OSAL 增加 ISR 抽象 — `osal_isr_enter/exit`、`osal_queue_send_from_isr` | **P2** | 1-2 天 | `osal/inc/osal.h` + 两个 port | +0.2 |

**总计**：约 4 周可将整体可移植性评分从 6/10 提升到 7.5/10。

### 6.2 实施顺序

```
第一周：P0 项
  Day 1-2: 清理 OSAL port 泄露（#2）
  Day 3-5: 抽象 SCPI 传输层（#1）
  验证：baremetal + RTOS 双路径构建通过，OTA 闭环通过

第二周：P1 项
  Day 1:   参数化 SRAM/XIP 地址（#3）
  Day 2-5: 新增 DMA 驱动抽象（#4）
  验证：编译无警告，Trigger + OTA 板端 smoke 通过

第三-四周：P2 项（可选）
  Day 1-2: OSAL ISR 抽象（#7）
  Day 3-7: 驱动头文件类型抽象（#6）
  Week 4:  触发 IO 原语接口（#5）
  验证：双路径构建 + OTA 闭环 + Trigger 时序不变
```

---

## 7. 目标平台迁移路线

### 7.1 场景 A：新 RP2350 产品

**前提**：不同引脚分配、不同外设使能、可能减少 PIO block 占用。

**工作量**：1-2 天。

**步骤**：
1. 复制 `boards/rp2350_trig/` → `boards/new_product/`
2. 修改 `board_config.h` 中的引脚映射、外设实例 ID、PIO 分配
3. 修改 CMakeLists.txt 中的 `PICO_BOARD` 和 board 源文件路径
4. 重新构建 + 板端 smoke 验证

**风险**：无。这是 Pico SDK 项目标准移植路径。

### 7.2 场景 B：迁移到 RP2040

**前提**：RP2040 有 2 个 PIO block（vs RP2350 的 3 个），264 KB SRAM（vs 520 KB），无浮点单元。

**工作量**：1-2 周。

**步骤**：
1. 重新分配 PIO 资源：2 个 block 共 8 个 SM（vs 当前 12 个 SM），需合并或精简
2. 检查 SRAM 预算：264 KB 下 FreeRTOS 模式约需 35-55 KB + LCD，仍有充足余量
3. 调整 PIO 程序：RP2040 PIO 指令集基本兼容，但需验证 DMA 行为差异
4. 替换 RP2350 特有 API（如有使用）
5. 修改 `PICO_BOARD pico`

**风险**：DMA ring buffer 行为与 RP2350 不同，可能需要调整 `sync_io` 的 DMA 策略。PIO 资源减少可能需要合并 `sync_clock` 和 `sync_pulse` 到同一个 SM。

### 7.3 场景 C：迁移到 STM32F407（保留 OTA + SCPI + 基本触发）

**前提**：168 MHz Cortex-M4，1 MB Flash，192 KB SRAM，无 PIO/HRTIM。

**工作量**：4-8 周。

**Phase 1 — 基础设施（Week 1-2）**：
```
□ OSAL baremetal port（CMSIS __disable_irq + DWT timer）
□ SCPI 传输层（UART DMA + 环形缓冲，或 USB CDC VCP）
□ drv_flash（HAL_FLASH_* + Option Bytes 解锁）
□ drv_spi（HAL_SPI + DMA）
□ drv_watchdog（IWDG）
□ board_config.h（新引脚映射）
□ 构建系统（STM32CubeMX + Makefile/CMake）
```

**Phase 2 — OTA 闭环（Week 2-3）**：
```
□ 实现 pota_platform_ops_t 全部 14 个函数
□ Bootloader 移植（CMSIS SCB->VTOR / __set_MSP / __disable_irq）
□ Flash 分区规划（适配 1 MB 内部 Flash）
□ OTA 正向 + 负向矩阵验证
```

**Phase 3 — 触发基本功能（Week 3-6）**：
```
□ Timer encoder mode 替代 enc_count.pio（利用 STM32 硬件编码器接口）
□ Timer output compare + DMA burst → GPIO 替代 seq_step
□ Timer PWM 替代 sync_pulse（精度 ~50ns vs PIO ~6.7ns）
□ 重新定义 TriggerFB 中与 PIO 交互的接口
```

**Phase 4 — UI + 诊断（Week 6-8）**：
```
□ 对接 SPI LCD（HAL_SPI + DMA）
□ U8G2 移植（STM32 已有官方支持）
□ diagnostics 零改动
```

**关键取舍**：
- 接受触发抖动 ≥ 50ns（vs RP2350 PIO ~6.7ns）
- 接受输入→输出延迟 ≥ 200ns（vs PIO ~20ns）
- `seq_step` 无法实现硬件自主闭环（每个触发沿需 DMA 中断介入）
- 编码器计数可使用 STM32 原生 Timer encoder mode（实际性能可能优于 PIO 软件实现）

### 7.4 场景 D：迁移到 STM32H743（利用 HRTIM）

**前提**：480 MHz Cortex-M7，2 MB Flash，1 MB SRAM，含 HRTIM + 多个高级 Timer。

**工作量**：4-6 周。

**优势**：
- HRTIM 可提供 ~200ps 的脉冲分辨率，优于 RP2350 PIO 的 6.7ns
- 多个高级 Timer 可并行处理编码器、触发输入、序列输出
- M7 双精度 FPU + 大 SRAM 可做更复杂的触发判定

**注意**：HRTIM 仅部分 STM32 型号（G4/H7）具备，会降低后续可移植性。

### 7.5 场景 E：仅迁移上层组件

**前提**：新项目只需要 OTA + 事件总线 + 资源仲裁 + 诊断，不需要硬实时触发。

**工作量**：1-2 周。

**可直接复用的组件**（零改动）：
- `components/event_bus/`
- `components/resource_arbiter/`
- `components/diagnostics/`
- `components/function_block/`
- `components/ota_manager/`（OtaAO/OtaFB/OtaVector）
- `third_party/portable_ota/`
- `third_party/scpi-parser/`

**需要适配的组件**（2-5 天）：
- `middleware/portable_ota_port/`（实现 14 个 platform ops）
- `middleware/scpi_port/`（实现传输层 read/write/flush）
- OSAL port（8 个函数）

**可丢弃的组件**：
- `components/sync_io/`、`components/sync_trigger/`（全部 PIO/DMA 相关）
- `drivers/mcu/pio/`、PIO 程序
- 3 个 trigger 设计文档中 PIO 相关的部分

---

## 8. 风险与注意事项

| 风险 | 影响 | 缓解措施 |
|---|---|---|
| PIO 程序中的硬件闭环逻辑迁移后性能退化 | 触发抖动增大，用户可能不接受 | 迁移前与用户确认可接受的抖动上限 |
| STM32 Flash 擦写期间暂停取指（与 XIP 不同） | Flash driver 实现方式完全不同 | 参考 STM32 dual-bank 或使用 RAM 执行 Flash 操作 |
| RP2350 DMA ring buffer 替代方案在目标平台不可用 | `sync_io` 重构工作量增加 1-2 周 | 提前调研目标平台 DMA 能力 |
| STM32 USB CDC 与 Pico SDK TinyUSB 行为差异 | SCPI 传输层需要额外的流控逻辑 | 抽象 `scpi_transport_ops_t` 时考虑背压机制 |
| 编码器 PIO 程序 24 条指令的软硬件协同被打破 | STM32 Timer encoder 硬件接口与 PIO 不同的配置模型 | 在 TriggerFB 中增加编码器配置抽象层 |

---

## 9. 结论与建议

### 9.1 核心结论

HAOFV 架构在可移植性方面呈现**清晰的分层特征**：

1. **管理域**（事件总线、资源仲裁、功能块 ECC 引擎、诊断、portable_ota 库、SCPI 解析器）具有优秀的平台无关性，是跨 MCU 可复用的核心资产。
2. **适配域**（OSAL、驱动层、Board 配置、Bootloader）遵循了良好的接口设计原则，但实现中存在 SDK 泄露，通过 1-2 周的重构即可显著提升可移植性。
3. **硬实时域**（PIO、DMA 寄存器操作）是与 RP 系列 MCU 深度绑定的性能优化区域。这不是架构缺陷，而是发挥特定硬件优势的必然代价。

### 9.2 行动建议

1. **立即执行**（0.5-1 周）：完成 P0 项——抽象 SCPI 传输层 + 清理 OSAL port 泄露。这两项改动小、风险低、收益高。
2. **短期执行**（1-2 周）：完成 P1 项——参数化地址宏 + DMA 驱动抽象。为后续可能的 STM32 迁移打下基础。
3. **择机执行**（按需）：P2 项在确认目标迁移平台后启动，避免过度设计。
4. **如果考虑 STM32 迁移**：建议采用场景 C 路线（4-8 周），优先确保 OTA 闭环可用，触发功能按可接受的精度退化交付。
5. **如果仅复用上层**：管理域组件（OTA + 事件 + 仲裁 + 诊断 + 功能块）可立即在新项目中复用，约 1-2 周完成适配。

### 9.3 最终评分卡

```
子系统                       当前   修复后   迁移方式
─────────────────────────────────────────────────────
portable_ota 库              9/10   9.5/10   实现 14 个 platform ops
event_bus                    10/10  10/10    零改动
resource_arbiter             10/10  10/10    零改动
diagnostics                  9/10   9/10     零改动
function_block (ECC引擎)     9/10   9/10     零改动
scpi-parser                  9/10   9/10     零改动
sync_config_ui (U8G2)        7/10   8/10     重写 SPI 适配
SCPI 传输层                  3/10   8/10     实现 3 个 transport ops
drv_flash                    5/10   7/10     替换 Flash HAL + 参数化地址
drv_spi/i2c/uart             4/10   6/10     类型抽象 + HAL 替换
drv_watchdog                 6/10   7/10     替换 WDT API
OSAL 实现                    5/10   8/10     清理 SDK 泄露 + ISR 抽象
Bootloader                   4/10   6/10     替换平台特定寄存器操作
sync_io (PIO+DMA 逻辑)       1/10   1/10     PIO 无等价物，需硬件方案重设计
sync_trigger (触发控制面)    3/10   5/10     控制逻辑可复用，底层 PIO 依赖需重构
DMA 寄存器直接访问            1/10   4/10     建立 drv_dma 抽象后部分可复用

整体                         6/10   7.5/10
```
