# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/arch/RTOS_HAOFV_TASK_PROGRESS.md`
Last updated: 2026-08-14

本文档记录 Virtual Distributed Clock / VDC Domain 的阶段性任务进度、验证结果和后续动作。待办事项放在 `VDC_DOMAIN_TODO.md`，本文只记录已经发生的工作和可回溯结果。

## 记录规则

每条任务记录使用以下格式：

```text
### VDC-TASK-YYYYMMDD-NNN - 标题

- 状态：
- 日期：
- 任务目标：
- 完成内容：
- 验证结果：
- 还需完成：
- 关联文件：
- 下一步：
```

## 当前目标

VDC Domain 当前目标是把 VDC/DPLL 从同步域中的基础算法，升级为 HAOFV 内部基础主域：

```text
VdcSyncAO
+ SyncDpllFB
+ HoldoverFB / RelockFB
+ VdcVector
+ VdcClockModel
+ VdcQualityTable
+ TimestampDictionary
```

首阶段先完成文档主域和架构边界，不修改代码。

## 任务记录

### VDC-TASK-20260814-005 - TDMA + DPLL 融合架构补充

- 状态：完成
- 日期：2026-08-14
- 任务目标：
  - 将 TDMA 与 DPLL 的融合架构输入纳入 VDC canonical，而不是把二者作为可替代方案。
  - 明确 TDMA、DPLL 和低频驯服环的分层职责、writer 边界和数据契约。
  - 将性能描述收敛为待验证目标，避免在未实测前写成产品保证。
- 完成内容：
  - `README.md` 增加当前主线摘要：TDMA 硬实时环、DPLL 锁相环、低频驯服环和 core0/core1/PIO 边界。
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加 `TDMA + DPLL 融合控制模型`，定义三层控制环、TDMA observation window、DPLL servo/DCO contract、low frequency discipline 和 fused state machine。
  - `VDC_DOMAIN_ARCHITECTURE.md` 的内部数据模型新增 `VdcTdmaScheduleProfile`、`VdcDcoControl` 和 `VdcDisciplineModel`，核心字段增加 TDMA schedule CRC、DCO update seq、period adjust、slew limit、aging/temperature compensation 和 holdover drift bound。
  - 状态机从单一 `LOCKING` 细分为 `INITIAL_SYNC -> FREQ_LOCK -> PHASE_LOCK -> LOCKED`，便于后续分别验证初始同步、频率拉入、相位收敛和正式锁定。
  - `VDC_DOMAIN_TODO.md` 增加 TDMA schedule profile、DCO snapshot、observation window gate、低频驯服任务和对应验证项。
- 验证结果：
  - `python tools\docs_check\docs_check.py` 通过，`files=85 warnings=0`。
- 还需完成：
  - 冻结 `VdcTdmaScheduleProfile`、`VdcDcoControl` 和 `VdcDisciplineModel` 的 C 结构、字段单位和 snapshot guard。
  - 代码中实现 TDMA observation window 输入门禁和 DCO snapshot 提交。
  - 增加收敛验证脚本，记录 lock_time、RMS/peak offset、outlier ratio、DCO slew 和 HOLDOVER drift bound。
- 关联文件：
  - `docs/vdc/README.md`
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 按 P2 先冻结 VDC 数据契约，再进入 P3 的 DPLL/Clock Model 代码落地。

### VDC-TASK-20260813-004 - PIO/VDC 首版硬实时装配链补充

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将首版 `PIO_SM0: SYNC_RX_CAPTURE`、`PIO_SM1: SYNC_TX_FIRE`、DMA、core1 realtime、`task_vdc_sync` 和 `task_loop_engine` 的参考数据通路写入 VDC 主域。
  - 明确硬实时采样/输出链路和软件 DPLL/VDC owner 的边界。
- 完成内容：
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加“首版 PIO/VDC 参考装配链”，定义捕获链、DPLL 更新链和 FIRE_LOAD 输出链。
  - `RTOS_HAOFV_ARCHITECTURE.md` 增加 PIO/VDC 首版数据通路表和数据流。
  - `VDC_DOMAIN_TODO.md` 增加 PIO_SM0 FIFO/DMA/timestamp、PIO_SM1 FIRE_LOAD 小载荷、core1 写入边界和输出链路冻结待办。
  - `VDC_DOMAIN_TODO.md` 增加 PIO capture 到 VdcSlot、LoopEngine 到 PIO_SM1 的验证项。
- 验证结果：
  - 本任务为文档框架补足，尚未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 后续根据实际 PIO 资源、布线和 board profile 调整具体 PIO instance、GPIO、DMA channel 和 ring buffer 大小。
  - 后续在代码中定义 `FIRE_LOAD` 小载荷和 capture FIFO word 格式。
- 关联文件：
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
  - `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
- 下一步：
  - 进入 VDC P2/P4，把 timestamp sample、DPLL input ring 和 `FIRE_LOAD` payload 字段契约冻结。

### VDC-TASK-20260813-003 - 虚拟 DC 时钟参考框架补足

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 基于 LinuxPTP / Chrony / SOEM EtherCAT DC 的时间同步机制，补足 VDC 主域框架。
  - 将参考项目落到 reference clock、servo profile、error budget、DC sync pipeline 和 HOLDOVER model。
- 完成内容：
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加 VDC 框架补足章节。
  - 定义 `VdcReferenceClockTable`，首版可固定 A0，后续支持 candidate、priority、source 和 failover reason。
  - 定义 `VdcServoProfile`，覆盖 servo type、kp/ki、update period、step threshold、sanity frequency limit、lock threshold、outlier threshold 和 reset policy。
  - 定义 `VdcErrorBudget`，覆盖 offset RMS/max、frequency skew、path delay、delay stddev、dispersion 和 root distance 等价误差上界。
  - 定义 `VdcDcSyncPipeline`，覆盖 reference select、active calibration delay、timestamp dictionary/profile CRC、initial sync、drift compensation、LOCKED publish 和 T2/READY validation。
  - `VDC_DOMAIN_TODO.md` 补充 reference clock、servo profile、error budget、DC sync pipeline 和 holdover drift bound 待办。
- 验证结果：
  - 本任务为文档框架补足，尚未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 在 `HAOFV_VDC_DPLL_ARCHITECTURE.md` 增加质量字段与 PTP/Chrony servo 字段映射。
  - 在 `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` 增加 initial sync / drift compensation / holdover 检查链。
- 关联文件：
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
- 下一步：
  - 进入 VDC P2，冻结 `VdcClockModel`、`VdcServoProfile`、`VdcErrorBudget` 和 `VdcDcSyncPipeline` 字段契约。

### VDC-TASK-20260813-002 - VDC 外部时间同步参考拆分

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 将刚刚列出的外部参考项目按虚拟反射内存和虚拟 DC 时钟拆分。
  - 把 LinuxPTP / Chrony、SOEM / EtherCAT DC 和 VDC owner/FB 边界参考归入 VDC Domain。
- 完成内容：
  - `VDC_DOMAIN_TODO.md` 增加“参考项目收敛原则”，明确 VDC 只吸收 offset/rate、jitter、servo reset、HOLDOVER、reference clock、传播 delay、initial sync、drift compensation 和 timestamp 机制。
  - `VDC_DOMAIN_TODO.md` 增加 P1.5 外部时间同步参考机制工程化收敛章节。
  - `VDC_DOMAIN_ARCHITECTURE.md` 增加外部参考机制矩阵，区分 LinuxPTP/Chrony、SOEM/EtherCAT DC 和 IEC 61499 的 VDC 落地方式与不采用内容。
  - VDC P8 增加 PTP/Chrony-style 和 EtherCAT DC-style 验证项。
- 验证结果：
  - 本任务为文档拆分，尚未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 在 `HAOFV_VDC_DPLL_ARCHITECTURE.md` 增加 VDC/DPLL 质量字段与 PTP/Chrony servo 字段映射。
  - 在 `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md` 增加 EtherCAT DC-style initial sync / drift compensation / holdover 检查链。
- 关联文件：
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
  - `docs/vdc/VDC_TASK_PROGRESS.md`
  - `docs/refmem/REFMEM_DOMAIN_TODO.md`
  - `docs/refmem/REFMEM_TASK_PROGRESS.md`
- 下一步：
  - 按 VDC P2/P3 冻结 VDC 数据契约和 DPLL/clock model。

### VDC-TASK-20260813-001 - VDC 三份标准文档建立

- 状态：完成
- 日期：2026-08-13
- 任务目标：
  - 在 `docs/vdc/` 中建立 VDC 内部主域的三份标准文件和目录 README。
  - 将“虚拟 DC 时钟升格为 HAOFV 内部基础主域”的边界写入文档。
- 完成内容：
  - 新增 `VDC_DOMAIN_ARCHITECTURE.md`，定义 VDC Domain 的定位、职责边界、HAOFV 层级、内部数据模型、共同时间映射、状态机、跨域契约、SCPI 边界、目标代码形态和验证门禁。
  - 新增 `VDC_DOMAIN_TODO.md`，把文档同步、HAOFV 主域升级、数据契约、DPLL、CAL/SYNC/MEAS/TRIG 边界、RefMem 映射、代码组件化、SCPI/System Pack 和验证拆成 P0-P8 待办。
  - 新增 `VDC_TASK_PROGRESS.md`，作为 VDC 主域独立任务进度入口。
  - 新增 `README.md`，作为 VDC 目录入口。
  - 更新 `docs/README.md`、`docs/arch/README.md` 和 `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`，把 VDC 从 `sync/` 中拆出为内部基础主域。
  - 更新 `HAOFV_ARCHITECTURE.md`、`RTOS_HAOFV_ARCHITECTURE.md`、`RTOS_HAOFV_TODO.md`、`HAOFV_MAINTENANCE_TODO.md`、`sync/README.md` 和 RefMem 入口，明确 VDC 与 RefMem 并列：VDC 管共同时间，RefMem 管共同事实。
- 验证结果：
  - 本任务为文档生成，尚未执行构建、烧录或板端 SCPI。
- 还需完成：
  - 运行 `python tools/docs_check/docs_check.py`。
- 关联文件：
  - `docs/vdc/README.md`
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_TODO.md`
  - `docs/vdc/VDC_TASK_PROGRESS.md`
- 下一步：
  - 按 `VDC_DOMAIN_TODO.md` 的 P0/P1 更新索引和架构入口。
