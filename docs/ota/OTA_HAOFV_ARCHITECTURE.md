# OTA HAOFV 域架构

Status: Active
Domain: OTA
Canonical: `docs/ota/OTA_HAOFV_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/HAOFV_FLASH_ARCHITECTURE.md`, `docs/ota/OTA_TODO.md`, `docs/ota/OTA_TASK_PROGRESS.md`
Last updated: 2026-09-06

本文定义 OTA 域在 HAOFV 下的稳定运行语义、owner、Function Block、Vector、Boot Control 和失败恢复边界。跨域 Flash owner、BCB 与统一 stream 契约仍以 `HAOFV_FLASH_ARCHITECTURE.md` 和 `DOCS_REGISTRY.md` 中现有 pending 契约为事实源；本文不自行激活或复制这些契约。

## 文档接口

| 文件 | 唯一职责 |
|---|---|
| `OTA_HAOFV_ARCHITECTURE.md` | 稳定语义、owner、不变量、状态模型和验证映射。 |
| `OTA_TODO.md` | 当前任务状态、依赖和退出门禁。 |
| `OTA_TASK_PROGRESS.md` | 代码、构建、板端测试、失败和回退证据。 |

历史 copy-to-active、Direct A/B、portable migration 和开源方案比较已迁入 `docs/legacy/ota/`，只能作为历史输入，不能覆盖本文和 Flash canonical。

## 范围与边界

OTA 域负责 package/session、inactive image 安装、镜像验证、pending 意图、Boot 交接和 App confirm。它不拥有物理 Flash erase/program，不拥有 TDMA maintenance window，也不直接解释系统级 HardFault。

稳定边界：

- SCPI、USB CDC、USBTMC、UART、SD 和 TDMA adapter 只能表达 intent、传输数据或读取 snapshot。
- `OtaAO` 是 App OTA 生命周期、事件队列和 `OtaVector` 的唯一运行 owner。
- `OtaFB` 只执行 ECC 状态迁移、短校验、intent 提交和 completion 消费。
- `BootControlStore` 负责 BCB schema、select、append、commit、lane generation、GC 和回退语义。
- App 的物理写入只能由 core0 `FlashTransactionAO` 执行；Bootloader 只使用 `BootFlashService`。
- `DiagnosticsAO`/`DiagnosticsVector` 负责 watchdog、exception 和 crash record；OTA 只投影与会话相关的失败原因。

## Owner 矩阵

| 事实或动作 | 唯一 writer/owner | reader | 禁止 |
|---|---|---|---|
| OTA session/ECC | `OtaAO` + `OtaFB` | ingress、System、Diagnostics | transport 复制状态机。 |
| `OtaVector` | `OtaAO` | SCPI、UI、Diagnostics | 查询路径现场扫描 Flash 或修改状态。 |
| BCB 选择结果 | `BootControlScanFB` 私有 context | `OtaFB` | 全局 mutable hint、const 强转或跨事务复用。 |
| BCB 物理 append | `FlashTransactionAO` 内的受控 BootControl transaction | `OtaAO` 读 completion | OtaAO、SCPI 或 transport 直接 program/erase。 |
| Flash job/result | `FlashTransactionAO`/`FlashTransactionVector` | `OtaAO`、Diagnostics | 把 accepted 当作 committed。 |
| Boot test/confirm/revert | Bootloader + App confirm protocol | OtaAO、Diagnostics | `END` 直接 reboot；未验证 pending 直接激活。 |
| crash record | fault handler 原始采集，`DiagnosticsAO` 解释和发布 | SCPI、报告工具 | 把系统 fault enum 放入 OTA Vector。 |

## 架构不变量

### OTA-HAOFV-01：AO 与外部入口

外部入口只投递事件或 immutable buffer lease。`OtaAO` 每次 service 必须遵守传入执行预算，并在预算耗尽时保留当前 ECC 状态，等待下一 tick。

### OTA-HAOFV-02：FB 非阻塞

`OtaFB` action 不等待 Flash、锁、队列、USB 或完整 BCB 扫描。每次 action 只推进一个有界步骤并返回 `BUSY`、`DONE` 或 `FAILED`。

### OTA-HAOFV-03：单次 BCB selection

一次 pending 事务只生成一个显式 `pota_bcb_selection_t`。metadata 解码、security policy、append planner 和 transaction prepare 共同消费该 selection；禁止通过 store 内隐藏 hint 把一次选择隐式传给下一 API。

### OTA-HAOFV-04：固定内存与栈预算

BCB scan、body、commit、seal、readback 和 CRC workspace 使用 AO 私有固定 context 或 transaction context。校验函数不得在深调用栈复制完整 `POTA_BCB_PAGE_SIZE` 对象；CRC 使用分段输入或调用方 workspace。

### OTA-HAOFV-05：Flash 唯一 writer

`OtaFB` 只提交 versioned Boot Control append intent。`FlashTransactionAO` 负责 resource acquire、core1 park、program、readback、commit、release 和 terminal completion。

### OTA-HAOFV-06：Vector 单一事实源

SCPI/UI/Diagnostics 只读取 seqlock 或等价版本化的 `OtaVector`、`FlashTransactionVector` 和 `DiagnosticsVector`。查询命令不得触发 BCB 扫描或写操作。

### OTA-HAOFV-07：Boot 边界

`SYST:OTA:END` 只能使会话进入验证和 pending 流程。达到 `READY_TO_REBOOT` 后，只有显式 `SYST:OTA:BOOT` 才允许软件重启；新 App 自检通过后由 `SYST:OTA:COMM` 完成 confirm。

## 组件关系

```text
Ingress adapters
      │ event / immutable lease
      ▼
OtaAO ──────────────────────────────── writes ──> OtaVector
  ├─ OtaFB
  ├─ BootControlScanFB
  └─ fixed OTA/BCB context
      │ versioned append intent
      ▼
FlashTransactionAO ─────────────────── writes ──> FlashTransactionVector
  └─ FlashTransactionFB + BootControlStore transaction
      │
      ▼
FlashMap -> Raw Flash HAL

Fault handler -> retained FaultRecord -> DiagnosticsAO -> DiagnosticsVector
```

`BootControlScanFB` 是 `OtaAO` 内部固定实例，不创建第二个 task 或第二个 Flash writer。portable `BootControlStore` 保持无 RTOS、无 SCPI、无产品地址依赖，供 App 与 Bootloader 复用。

## OtaFB ECC

公共状态保持既有协议兼容：

```text
IDLE -> CHECK_PERMISSION -> ERASE_SLOT -> RECEIVING
     -> VERIFYING -> MARK_PENDING -> READY_TO_REBOOT
     -> PENDING_CONFIRM -> COMMITTED
```

`MARK_PENDING` 内部细分为：

```text
SCAN_BEGIN
-> SCAN_BUSY
-> PREPARE_PENDING
-> SUBMIT_APPEND_INTENT
-> WAIT_APPEND_COMMIT
-> PUBLISH_READY
```

| 子状态 | 单次 action | 返回 |
|---|---|---|
| `SCAN_BEGIN` | 初始化固定 scan context。 | `BUSY` |
| `SCAN_BUSY` | 最多读取并处理一个 BCB page。 | `BUSY/DONE/FAILED` |
| `PREPARE_PENDING` | 从 selection 解码 metadata，执行 slot/security policy，准备 immutable update。 | `BUSY/FAILED` |
| `SUBMIT_APPEND_INTENT` | 向 `FlashTransactionAO` 提交一次 versioned intent。 | `BUSY/FAILED` |
| `WAIT_APPEND_COMMIT` | 读取 completion snapshot，不阻塞等待。 | `BUSY/DONE/FAILED` |
| `PUBLISH_READY` | 更新 OtaVector 为 `READY_TO_REBOOT`。 | `DONE` |

## Boot Control scan 与 selection

portable API 目标形态：

```c
pota_bcb_result_t pota_bcb_scan_begin(
    pota_bcb_scan_t *scan, const pota_bcb_store_t *store);

pota_bcb_step_result_t pota_bcb_scan_step(pota_bcb_scan_t *scan);

pota_bcb_result_t pota_bcb_scan_result(
    const pota_bcb_scan_t *scan, pota_bcb_selection_t *selection);
```

`pota_bcb_selection_t` 是显式、不可变的事务输入，至少携带 result、newest view、lane generation 和 selection generation。调用者在 owner generation 改变后必须丢弃 selection。

scan context 包含 cursor、page scratch、candidate 和 newest view；不得依赖函数栈上的 page-sized 数组。App 路径逐 tick 扫描，Bootloader 可以在无 RTOS 上下文使用同一 step API 循环到 terminal。

## Boot Control append intent

OtaAO 提交的是逻辑 intent，不是裸 page 命令：

```c
typedef struct {
    uint32_t requester;
    uint32_t store_generation;
    pota_bcb_selection_t selection;
    pota_bcb_update_t update;
} boot_control_append_intent_t;
```

`FlashTransactionAO` 内部按 BootControlStore 契约推进 erase/GC、body program、readback、commit program、readback 和 seal。只有 terminal completion 的 `committed=true` 才允许 OtaFB 发布 `READY_TO_REBOOT`。

## OtaVector

`OtaVector` 由 OtaAO 唯一写入。稳定字段组应覆盖：

- session：state、target slot、package/session generation；
- progress：received、durable、verified 和 selected image progress；
- BCB：scan state、lane/page cursor、selected sequence/lane generation；
- Flash：intent/job ID、accepted/programmed/verified/committed；
- result：OTA error、Boot result、recovery reason；
- lifecycle：started/completed timestamp 和 vector sequence。

字段组通过 seqlock 或等价 version 机制发布。SCPI 文本只是 Vector 投影，不成为第二事实源。

## 失败与恢复

- scan 遇到 torn/CRC/schema/map 错误时继续寻找旧有效记录；双 lane 无有效事实则 fail closed。
- selection generation 不匹配时拒绝 append，重新进入 `SCAN_BEGIN`，不复用旧 selection。
- Flash intent accepted 但未 committed 时保持 `MARK_PENDING`，不得发布 ready。
- reboot 前失败保持旧 confirmed App；Bootloader 只消费 durable committed BCB。
- fault handler 记录 exception、PC/LR/SP 和 fault status；DiagnosticsAO 在下次启动解释，不由 OTA 伪装成普通 timeout。

## 验证映射

| Gate | 必须证明 |
|---|---|
| FB/AO budget | 每次 scan/transaction service 有界；`budget_us` 生效。 |
| Owner | App BCB program/erase 只来自 FlashTransactionAO。 |
| Selection | 一次 pending 只扫描一次；无 hidden hint 或 const mutation。 |
| Stack | CRC 与 scan 不创建 page-sized 深栈副本；stack-usage gate 通过。 |
| BCB | full lane、GC、torn body/commit/seal、旧记录回退和 security counter。 |
| Vector | SCPI 查询只读 snapshot，不触发 Flash IO。 |
| Boot | A→B、B→A、未确认回滚、损坏 BCB fail closed。 |
| Hardware | 当前源码指纹的构建、COM7 双向 OTA 和仓库 P3 hardware acceptance receipt。 |

## 历史迁移

旧 OTA 文档已迁入 `docs/legacy/ota/`。历史快照中的地址、容量、构建号和单次板端结果不属于本文稳定事实；当前实现状态和证据只写入 `OTA_TODO.md` 与 `OTA_TASK_PROGRESS.md`。
