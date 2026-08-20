# 校准域任务记录

Status: Active
Domain: CALIBRATION
Canonical: `docs/calibration/CALIBRATION_TASK_PROGRESS.md`
Related: `docs/calibration/CALIBRATION_DOMAIN_TODO.md`, `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/vdc/VDC_TASK_PROGRESS.md`
Last updated: 2026-08-20

本文档记录校准域从方案、粗捕获到双向测距和 VDC/DPLL 接入的实际进展。记录中的 HIL
结果必须绑定 build、拓扑、profile、接线和证据目录；未绑定这些上下文的数字只能作为
诊断快照，不能作为 active calibration 或产品精度承诺。

## 当前任务状态

| 任务 | 状态 | 结论 |
|---|---|---|
| 校准域职责与 TDMA/VDC 边界 | `[x]` | 校准拥有测量与接受门禁，TDMA 负责传输与编排，VDC/DPLL 消费 accepted snapshot |
| 第一阶段 CLK RTT 粗捕获 | `[x]` | 已完成板内最小实现和四板 HIL，仍为 diagnostic-only |
| 第二阶段编码 marker/相关测距 | `[~]` | codebook 离线评估已完成，固件、PIO/DMA、相关器和 HIL 待实现 |
| 第三阶段双向同时对比法 | `[ ]` | 已进入方案和任务拆分，尚无双板/四板实测结果 |
| VDC/DPLL active calibration gate | `[ ]` | 依赖正式 hardware latch、bias、generation/freshness 和 P3 结果 |

## CAL-TASK-20260820-002 - 校准域待办与任务记录建立

- 状态：完成文档拆分；代码和第三阶段板端验证未完成。
- 日期：2026-08-20。
- 任务目标：
  - 依据 TDMA CLK 分级训练方案建立校准域可执行 TODO。
  - 把 P0/P1/P2/P3/P4 的 owner、交付物、门禁和阻塞项单独记录。
  - 为后续双向同时对比、endpoint bias、四板 residual 和八节点扩展保留证据入口。
- 完成内容：
  - 新增 `CALIBRATION_DOMAIN_TODO.md`，明确校准域与 TDMA、VDC/DPLL、SYNC_IO 的边界。
  - 新增本任务记录，区分已完成的方案/粗捕获与尚未实现的正式校准流程。
  - 将第一阶段结果标记为 build/topology/profile 绑定的 diagnostic snapshot。
  - 明确正式 active per-link delay 必须经过四时间戳 hardware latch、bias generation、
    重复统计、topology freshness 和 VDC/DPLL gate。
- 验证结果：
  - 文档回归命令待本次文档修改完成后统一执行。
- 关联文件：
  - `docs/calibration/CALIBRATION_DOMAIN_TODO.md`
  - `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`
  - `docs/calibration/README.md`
  - `docs/README.md`
- 下一步：
  - 先完成 P0 hardware latch/evidence 字段与 P2 marker golden vector，再进入双板 P3 HIL。

## CAL-TASK-20260820-001 - TDMA CLK 训练方案与双向测距补充

- 状态：完成方案更新；实现和板端验证待完成。
- 日期：2026-08-20。
- 任务目标：
  - 将 TDMA 训练物理测量从整圈平均分摊推进到逐链路双向时间传递。
  - 明确 `CLK/DATA/SYNC` 同 epoch 边沿关联、residence 扣除、path-sum 和 asymmetry gate。
  - 保持 EtherCAT DC 风格的训练状态、质量和接受门禁。
- 完成内容：
  - 方案加入 P3 `t1/t2/t3/t4` 定义和双向同时对比方程。
  - 明确等长差分线缆是对称性工程证据，不替代 endpoint bias/reference loopback。
  - 明确四板回环是逐链路结果的系统级 residual/HIL 门禁，不是单链路可观测性的替代品。
  - 明确只有正式 hardware latch、bias generation、重复统计和 topology freshness 通过后，
    才能生成 active per-link delay。
- 验证结果：
  - 本任务为方案和架构记录，未声称完成双板或四板 P3 实测。
- 关联文件：
  - `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`
  - `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
  - `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`
  - `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`
- 下一步：
  - 实现 P0/P2 基础件；完成同 persona 板内 bias reference loopback 后再执行双板 HIL。

## CAL-TASK-20260820-000 - 第一阶段 CLK RTT 粗捕获基线

- 状态：完成诊断基线；不能用于正式 VDC/DPLL 校准。
- 日期：2026-08-20。
- 完成内容：
  - 板内完成 CLK forwarding、master burst/capture、PIO IRQ、service 和 guarded snapshot。
  - 四板 HIL 完成四主轮换和多个 SPI profile 的粗区间捕获。
  - 训练结果保留 overlap、mixed、超时、错误增量、master、唯一板卡地址、profile 和
    `DIAGNOSTIC_ONLY` 标志。
- 结果边界：
  - 结果只对当次 build、当前拓扑、接线、收发器和 profile 有效，详情以训练方案中的
    HIL evidence directory 为准。
  - 粗 RTT 包含线缆、收发器和 follower residence，不能直接当作单 link delay，也不能
    作为完整帧 feedback timeout。
  - 当前 latch/时间戳尚未满足正式 DPLL gate，不能清除 diagnostic-only。
- 关联实现：
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `components/tdma/inc/tdma_pio_spi_phys.h`
  - `components/tdma/src/tdma_runtime_owner.c`
  - `tools/tdma_ring_monitor/tdma_clk_train.py`
  - `tools/tdma_ring_monitor/tdma_clk_codebook_eval.py`
- 下一步：
  - 将粗 bracket 作为编码 marker 的有界搜索输入，禁止扩大其含义为 active calibration。

## 证据记录规则

后续每次代码或板端验证追加一条任务记录，至少包含：

- 任务编号、日期、状态、目标和实际完成内容；
- build ID、固件/工具版本、板卡唯一地址、logical slot、拓扑、线缆/收发器和 profile CRC；
- 训练 epoch/sequence、accepted/rejected、硬件 latch source/resolution/flags、DMA overrun/
  stall、margin、residence、path-sum、bias generation、calibration generation 和 freshness；
- 执行的 host unit、HIL、烧录、SCPI smoke 或长稳命令，以及证据目录；
- 失败原因、影响范围、回滚/恢复动作和下一步。

当前未完成项目不能通过“代码已经存在”“工具能查询”或“软件 timer 有数值”标记为完成。
