# SYNC_IO 触发系统重构计划

Status: Active
Domain: SYNC_IO
Canonical: `docs/SYNC_IO_REFACTOR_PLAN.md`
Related: `docs/SYNC_IO_RESOURCE_PLAN.md`, `docs/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md`, `docs/TRIGGER_SYNC_TODO.md`
Last updated: 2026-07-07

## 1. 背景

当前硬件约束已经冻结，`SYNC_IO` 不再是可任意复用的 GPIO 池，而是一个固定硬件 profile：

| 资源 | 固定约束 |
| --- | --- |
| `GPIO16..19` | `SYNC_IO` 输入组 |
| `GPIO20..23` | `SYNC_IO` 输出组 |
| `GPIO19` | `RJ45_TRIG_IN` |
| `GPIO23` | `RJ45_TRIG_OUT` |
| `GPIO26/AUX0` | 差分 RX，BiSS `CLK_IN` |
| `GPIO27/AUX1` | 差分 RX，BiSS `DATA_IN` |
| `GPIO28/AUX2` | 差分 TX，BiSS `CLK_OUT` |
| `GPIO29/AUX3` | 差分 TX，BiSS `DATA_OUT` |
| `ENC_COUNT` | 固定 `A/B/Z = GPIO16/GPIO17/GPIO19` |

因此触发系统需要从“函数直接操作 pin/PIO”重构为“硬件约束 profile + 资源仲裁 + 模式驱动”的结构。这样可以同时支撑：

- 固定硬件约束下的量产固件；
- `SEQ_STEP`、`ENC_COUNT`、`BISS_TAP`、差分触发等多模式复用；
- 后续双核或 RTOS 化时，把实时路径与管理路径拆开。

## 2. 目标架构

```text
sync_io_hw_profile
  -> sync_io_resource / owner / safety
  -> sync_io_core
  -> mode drivers
       - seq_step
       - enc_count
       - biss_tap
       - aux_diff_trigger
       - self_cal
  -> TriggerFB / SCPI / UI
```

### 分层职责

| 层级 | 职责 | 不做什么 |
| --- | --- | --- |
| `sync_io_hw_profile` | 定义冻结 pinout、方向、合法组合和默认语义名。 | 不访问 PIO/DMA。 |
| `resource/owner/safety` | 统一声明资源占用、互斥、运行中禁止修改项。 | 不解析 SCPI。 |
| `sync_io_core` | 初始化 PIO 程序、公共 GPIO/PIO/DMA 原语、trace。 | 不决定业务模式状态机。 |
| `mode drivers` | 每个模式提供 validate/arm/disarm/is_running 表驱动入口。 | 不绕过硬件 profile。 |
| `TriggerFB/SCPI/UI` | 管理面配置、状态机、命令兼容和错误上报。 | 不直接拼底层 pin 组合。 |

## 3. P0 重构步骤

1. 建立硬件 profile 单一入口。
   - 所有固定 pinout 使用 `components/sync_io/inc/sync_io_hw_profile.h`。
   - `TRIG:ENC:APIN` 只允许 `16`。
   - `ENC_COUNT` 固定使用 `GPIO16/GPIO17/GPIO19`。
   - AUX 固定为两收两发：`AUX0/1` 输入，`AUX2/3` 输出。

2. 建立 mode driver 表驱动接口。
   - 新增 `sync_io_mode.h`。
   - 每个 mode 声明 `id/name/resources/validate/arm/disarm/is_running`。
   - P0 先接入 `ENC_COUNT` wrapper，不搬迁 PIO/DMA 内核。

3. 收口 `ENC_COUNT` 边界。
   - 新增 `sync_io_mode_enc_count.h/.c`。
   - mode wrapper 负责配置结构、硬件 profile 校验和资源声明。
   - 现有 `sync_io_enc_count_arm()` 保持 ABI 稳定，后续再迁移实现细节。

4. 明确 RJ45 触发输出语义。
   - BiSS target crossing 输出使用 `GPIO23/RJ45_TRIG_OUT`。
   - 当前内部仍复用 marker PIO SM，需要在 P1 拆成独立语义名或纳入资源仲裁。

### P0 验收

- `cmake --build build-sync-refactor` 通过。
- `TRIG:ENC:APIN 16` 可用，`TRIG:ENC:APIN 26` 返回执行错误。
- `TRIG:BISS:PINs?` 返回 AUX 两收两发固定映射。
- BiSS crossing 触发路径输出到 `GPIO23`。

## 4. P1 重构步骤

1. 引入资源 owner 表。
   - 资源粒度覆盖输入组、输出组、AUX RX、AUX TX、PIO SM、DMA channel、IRQ。
   - 每个 mode arm 前先 claim，disarm 后 release。
   - 运行中禁止 SCPI 修改会影响 PIO/DMA 的字段。

2. 拆分 `SEQ_STEP` mode。
   - 从 `sync_io.c` 中拆出配置校验、arm/disarm、runtime trace。
   - 保留公共 PIO/DMA helper 在 `sync_io_core`。

3. 拆分 `BISS_TAP` mode。
   - 明确 `AUX0->AUX2`、`AUX1->AUX3` 透传关系。
   - RX 解码、TX 透传、target crossing trigger 分离。
   - 把 BiSS TAP 的资源占用与普通差分触发模式互斥。

4. 整理 RJ45 trigger mode。
   - 将旧 `MARKER_OUT` 命名迁移到 `RJ45_TRIG_OUT` 语义。
   - 若仍需要 marker 功能，作为输出模式之一，而不是底层固定名称。

### P1 验收

- `SEQ_STEP`、`ENC_COUNT`、`BISS_TAP` 任一 mode 运行时，冲突 mode arm 会失败并给出明确错误。
- AUX0/1 写输出失败，AUX2/3 读输入失败。
- BiSS TAP 可以同时监听上行并向下行透传。
- trace 中能区分 mode id、资源冲突和硬件约束错误。

## 5. P2 重构步骤

1. 支持双核/RTOS 调度边界。
   - 实时 mode driver 可放到 core1 或高优先级任务。
   - SCPI/UI/日志保留在管理侧。

2. 增加模式级自检。
   - PIO FIFO 状态、DMA restart、IRQ 活性、丢帧计数、触发计数统一查询。
   - 支持板端 loopback 或外部回放闭环。

3. 增加长期兼容层。
   - 保留历史 SCPI 查询命令。
   - 对已关闭能力返回稳定错误码，而不是静默忽略。

### P2 验收

- 单核路径与双核路径使用同一 mode driver 表。
- 板端闭环验证脚本可以自动覆盖 `SEQ_STEP`、`ENC_COUNT`、`BISS_TAP`。
- 日志能还原一次触发链路的配置、资源、arm、事件、disarm 全过程。
- 调试 LOG 遵循 `docs/LOG_SYSTEM_TODO.md`：只记录低频生命周期和可恢复异常，
  硬实时路径使用 trace、计数器或故障证据，不直接输出文本日志。

## 6. 当前迁移清单

- [x] 硬件 profile 已建立。
- [x] `ENC_COUNT` 固定 pinout 已收口。
- [x] BiSS crossing 输出已切到 RJ45 trigger 语义入口。
- [x] `sync_io_mode.h` mode driver 接口落地。
- [x] `ENC_COUNT` mode wrapper 落地。
- [x] `ENC_COUNT` 进入 mode ops 查询表。
- [x] `SEQ_STEP` mode wrapper 拆分。
- [x] `BISS_TAP` RX mode wrapper 拆分。
- [x] `BISS_TAP` AUX2/AUX3 透传输出落地。
- [x] 资源 owner 表基础能力落地。
- [x] 资源冲突 SCPI 查询落地。
- [x] 资源冲突错误码和 UI 展示落地。
