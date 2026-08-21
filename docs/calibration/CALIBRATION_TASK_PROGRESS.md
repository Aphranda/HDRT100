# 校准域任务记录

Status: Active
Domain: CALIBRATION
Canonical: `docs/calibration/CALIBRATION_TASK_PROGRESS.md`
Related: `docs/calibration/CALIBRATION_DOMAIN_TODO.md`, `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/vdc/VDC_TASK_PROGRESS.md`
Last updated: 2026-08-21

本文档记录校准域从方案、粗捕获到双向测距和 VDC/DPLL 接入的实际进展。记录中的 HIL
结果必须绑定 build、拓扑、profile、接线和证据目录；未绑定这些上下文的数字只能作为
诊断快照，不能作为 active calibration 或产品精度承诺。

## 当前任务状态

| 任务 | 状态 | 结论 |
|---|---|---|
| 校准域职责与 TDMA/VDC 边界 | `[x]` | 校准拥有测量与接受门禁，TDMA 负责传输与编排，VDC/DPLL 消费 accepted snapshot |
| 线序与环路顺序测量 | `[~]` | host 隔离探测、闭环判定和 NO 提交已迁入 calibration 命名空间；板内 generation/freshness 待实现 |
| 第一阶段 CLK RTT 粗捕获 | `[x]` | 已完成板内最小实现和四板 HIL，仍为 diagnostic-only |
| 第二阶段编码 marker/相关测距 | `[~]` | codebook 离线评估已完成，固件、PIO/DMA、相关器和 HIL 待实现 |
| 第三阶段双向同时对比法 | `[~]` | 公式、reject gate 和单板纯 PIO 闭环已通过；endpoint bias 及双板/四板实测待完成 |
| VDC/DPLL active calibration gate | `[ ]` | 依赖正式 hardware latch、bias、generation/freshness 和 P3 结果 |

## CAL-TASK-20260821-003 - 线序与环路顺序测量迁入校准域

- 状态：host 工具、命名和文档所有权迁移完成；板内 topology generation/freshness 尚待实现。
- 日期：2026-08-21。
- 任务目标：
  - 将有向线序/邻接矩阵、单闭环判定、anchor 旋转、slot map 和 NO 提交归入校准域。
  - 校准测量工具统一使用 `calibration_*` 名称；TDMA 只提供隔离 probe transport 和计数证据。
  - 禁止 TDMA START 根据调用参数隐式写入 NO，避免未校准顺序覆盖已接受 topology。
- 完成内容：
  - `tdma_ring_autodetect.py` 迁移为
    `tools/calibration_ring_validate/calibration_ring_topology.py`，默认报告目录改为
    `calibration_ring_topology_*`。
  - 输出增加 `measurement_domain=calibration` 和 measurement phase；保留 directed pair
    evidence、adjacency、ring order、slot map 和完整 snapshot。
  - `--anchor-id` 决定 accepted ring 的 NO.1；`--assign-no` 只在单闭环判定通过后提交映射，
    支持写后读回及 `--reboot-verify-no` 持久化复核。
  - `tdma_start_ring.py` 删除 NO 分配和启动时隐式 NO 写入；它只消费调用者提供的 accepted
    calibration order 来配置 TDMA local/reference slot。
  - 第一阶段和码本工具同步迁移为 `calibration_clk_train.py`、
    `calibration_clk_codebook_eval.py`，相应 Python 测试也改用 calibration 命名。
- 当前边界：
  - 工具已能生成 host 诊断 topology，但固件尚未发布带 CRC、generation、freshness 和
    accepted/rejected reason 的 `CalibrationTopologySnapshot`。
  - 本次不新增 wire layout 或阈值冻结契约；阈值仍来自显式工具参数/profile。
- 四板实测（build `20260821021250`，bench 诊断快照）：
  - 使用 `calibration_ring_topology.py --level 7 --adjacency-only` 完成全部有向板对隔离扫描，
    accepted ring 为 `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A ->
    A1E549202D18ED6A -> 0010071E65B5CB38`；本轮未写 NO。
  - 首轮发现 RX counter 普通回退被误算为 32-bit wrap，产生一个无 raw-word/edge 的假分支；
    工具已限定 wrap 只在 counter 高端到低端时成立，普通回退发布 regression 并按零增量处理。
  - 修正后 topology PASS；随后 `calibration_clk_train.py` 沿 accepted order 执行 10 MHz
    四主轮换，四个 master 均得到 `[400,500) ns`，无 mixed point，结果保持 diagnostic-only。
- 证据目录：
  - `build-product-release/calibration_ring_topology_four_10m_final`
  - `build-product-release/calibration_clk_train_four_10m_final`

## CAL-TASK-20260821-002 - 第一阶段归属迁移与供电入口 A/B 复测

- 状态：第一阶段文档归属迁移完成；四板粗捕获复测通过，结果保持 diagnostic-only。
- 日期：2026-08-21。
- 任务目标：
  - 将 CLK RTT 第一阶段流程、bracket 解释、四主结果和质量结论统一迁入校准域。
  - TDMA 只保留 PIO/SM/DMA、core1 command slot、persona、窗口和 raw evidence transport。
  - 将默认训练阶梯收敛到 operating profile level 7/8/9 对应的 `10 -> 25 -> 30 MHz`。
  - 通过把供电入口从 NO.2 移到 NO.1 的 A/B 复测，判断相邻码元桶差异是否跟随供电入口。
- 固定上下文：
  - build：`20260821021250`；物理环序：
    `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A -> A1E549202D18ED6A -> NO.1`。
  - 板卡身份来自 `*IDN?` 唯一地址；本轮 COM3/COM5/COM6/COM4 仅为临时传输端点。
- 验证结果（bench 诊断快照，非通用时序事实源）：
  - 供电入口 NO.2：10 MHz 四主均为 `[400,500) ns`；25 MHz 仅 NO.2 落入
    `[440,480) ns`；30 MHz 的 NO.2/NO.3 落入 `[434,467) ns`，其余为相邻低桶。
  - 供电入口 NO.1：10 MHz 四主仍为 `[400,500) ns`；25 MHz 改为 NO.3 落入
    `[440,480) ns`；30 MHz 仅 NO.2 落入 `[434,467) ns`。
  - 全部 trial 通过且没有 mixed point；相邻桶没有跟随供电入口移动。当前现象按码元边界
    量化解释，不把 40 ns/约 33.3 ns 的桶差声明为真实供电传播延时。
- 证据目录：
  - `build-product-release/tdma_clk_train_four_10m`
  - `build-product-release/tdma_clk_train_four_25m`
  - `build-product-release/tdma_clk_train_four_30m`
  - `build-product-release/tdma_clk_train_four_power_no1_10m`
  - `build-product-release/tdma_clk_train_four_power_no1_25m`
  - `build-product-release/tdma_clk_train_four_power_no1_30m`
- 边界：
  - 第一阶段 bracket 不生成 active path-delay、endpoint bias、feedback timeout 或 DPLL 样本。
  - 后续由 P2 编码 marker/过采样在 coarse bracket 内缩窗；TDMA 不复制测量公式和评分。

## CAL-TASK-20260821-001 - 单板回环双向测距预研

- 状态：本轮单板前期算法和纯 PIO/DMA 闭环目标完成；结果保持 reference-only / diagnostic-only，
  endpoint bias generation 和双板 P3 仍待完成。
- 日期：2026-08-21。
- 任务目标：
  - 使用单板 TX/RX 三线回环先验证双向同时对比法的公式和 reject gate。
  - 保持单板结果为 `REFERENCE_LOOPBACK`，不进入 active calibration 或 VDC/DPLL。
  - 核对小板现有 resident TDMA loopback 的实际物理状态，为后续 PIO edge-latch 接入准备证据。
- 完成内容：
  - 新增 `calibration_bidirectional.h/.c`，实现 `t1..t4` 本地时钟域检查、
    `residence_B = t3 - t2`、`path_sum = (t4 - t1) - residence_B`、endpoint bias 扣除、
    clock-rate bound、DMA/epoch/persona/topology/bias gate 和 `active_eligible` 判定。
  - 双板语义不比较 `t1` 与 `t2` 的绝对先后，只分别检查 A 域的 `t4>=t1` 与 B 域的 `t3>=t2`。
  - 新增 `test_calibration_bidirectional.c` 和 `run_calibration_bidirectional_tests.ps1`；
    覆盖有效回环样本、坏顺序、缺边沿、DMA 错误、persona/topology/bias/clock-rate/reference
    policy、negative path 以及 hardware-latched 非回环样本。
  - 将模块加入顶层 CMake，主固件 A/B 均完成重新编译和打包。
  - PIO 回环已收敛到 `TdmaRuntimeOwner`：停止态复用 TDMA 已声明的 PIO、TX/RX SM 和
    `TDMA_PIO_SPI_RX_DMA_CHANNEL`；`CALibration:LOOPback:STARt <words>` 只提交有界 intent，
    `READ:CALibration:LOOPback?` 只读取 guarded raw/result snapshot。
  - 固件端执行 `t1..t4` edge decode、SYNC/edge-mask/顺序门禁和现有
    `calibration_bidirectional_evaluate()`；单板结果始终带 `REFERENCE_LOOPBACK` /
    `DIAGNOSTIC_ONLY`，不会进入 active calibration 或 VDC。
- 单板实测结果（诊断快照，非校准事实源）：
  - 当前 USB CDC 枚举为 `COM8`，`COM7` 本轮未枚举；板卡地址为
    `839E1AE79EA20F31`，运行 build 为 `20260820082350`。
  - `tdma_single_board_loopback.py` 15 秒窗口完成 `STOP -> LOCAL -> ARM -> TRAIN -> START`；
    TX/RX sequence 和 adapter TX/RX 均增长，adapter/phys bad、magic fail、ring overrun
    均无增量。
  - `TDMA:FLIGHT:TX` 本次返回超时，RX 仍为空；因此 flight FIFO 镜像未通过，不能把本次运行
    记为飞行处理闭环。
  - `simultaneous_feedback_loop_evidence=0`、`ring_last_error=TIMESTAMP_MISSING`，符合
    当前没有 PIO/DMA 四边沿 latch 的预期；没有生成 `t1..t4`，没有生成 active delay。
  - OTA 后 COM8 最终运行 build `20260820172113`，活动槽 A 已提交；板卡地址仍为
    `839E1AE79EA20F31`，SCPI 错误队列为空。
  - resident TDMA IO 对照确认 TX=`GPIO26/25/29`、RX=`GPIO27/28/24`；同一短回环线下
    5 秒窗口 TX/RX 均前进 `1195` 帧且物理坏帧无增长，排除 IO 映射和接线错误。
  - 纯 PIO 回环先后修正 capture SM 覆盖 TX pin direction、返回输入未显式 mux 到 PIO2，
    并把 250 MHz 下过窄的 marker 扩展到可穿越 ISO1452 的有界窗口。
  - 最终 DMA 完成 `128` words，sample period 为 `20 ns`；snapshot 为
    `edge_mask=0xF`、`flags=0x7`、`reject_reason=NONE`、epoch `2`，四时间戳依次为
    `1060/1120/2080/2140 ns`，SYNC 匹配且 `result_valid=1`。
  - 固件端计算得到 residence `960 ns`、raw path-sum `120 ns`、reference delay estimate
    `60 ns`；`active_eligible=0`，未写入 VDC/DPLL active fact。上述数值仅是该 build、
    当前短线与收发器的诊断快照，不是 endpoint bias 或产品精度事实源。
  - 连续 10 个独立 epoch 全部满足四边沿、PIO/DMA、SYNC、公式和 reference-only 门禁，
    epoch 从 `3` 递增到 `12`，SCPI 错误队列为空；firmware-reported residence 范围为
    `960..980 ns`、raw path-sum 为 `100..120 ns`、delay estimate 为 `50..60 ns`。
  - 加入 core0 低频传感器 diagnostics 后，OTA build `20260820174134` 再完成 10 个独立
    epoch；四边沿、PIO/DMA、SYNC、公式和 reference-only 门禁仍为 10/10 通过，
    residence、raw path-sum 和 delay estimate 保持在前述诊断快照范围，证明 ADC service
    未进入 core1 实时边沿路径。证据仍为 `REFERENCE_LOOPBACK + DIAGNOSTIC_ONLY`。
  - 同一 build 上 `SYSTem:DIAGnostic:SENSors?` 连续读取确认板载温度与 RP2350 ADC8
    内部温度有效且无热告警；电流前端输出近低轨并伴随 U24 明显发热，已判为硬件前端
    故障并断电。电流值不得用于本任务的算法或 active calibration 结论。
  - 10 轮结束并 STOP 后重新进入 resident TDMA persona，5 秒只读窗口 TX/RX 各增长
    `1203` 帧，adapter/phys bad、magic fail、ring overrun 均无增长，证明 PIO2 SM/DMA
    维护 persona 能退出并恢复 TDMA owner 运行态。
- 验证命令与证据：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools/tests/run_calibration_bidirectional_tests.ps1`
    通过，输出 `calibration_bidirectional tests passed`。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成新的 A/B 固件和 update package。
  - `python tools/tdma_ring_monitor/tdma_single_board_loopback.py COM8 --duration-s 15
    --poll-interval-s 0.5 --out-dir build-rtos-multicore-smoke/tdma_single_loopback_current`
    完成物理回环预检；原始摘要见该目录 `summary.json`。
  - `python tools/calibration_loopback_validate/calibration_loopback_validate.py COM8
    --out-dir build-rtos-multicore-smoke/calibration_loopback_com8_final` 通过；raw SCPI 和
    snapshot 见该目录 `summary.json`。
  - `python tools/calibration_loopback_validate/calibration_loopback_validate.py COM8 --runs 10
    --out-dir build-rtos-multicore-smoke/calibration_loopback_com8_repeat10` 通过 10/10 轮；
    每轮 snapshot、epoch 和诊断范围见该目录 `summary.json`。
  - 传感器集成后的同 build 复验目录为
    `build-rtos-multicore-smoke/calibration_loopback_com8_sensors_adc8/summary.json`，通过
    10/10；随后因 U24 硬件过热主动断电，未把 TDMA persona 恢复工具的端口打开失败计为通过。
  - 校准 STOP 后重新 ARM/START resident TDMA，再执行
    `tdma_single_board_loopback.py --skip-ring-setup` 通过；persona 恢复证据见
    `build-rtos-multicore-smoke/tdma_restore_after_calibration/summary.json`。
- 还需完成：
  - 将当前 10 轮短窗口扩展为长时间重复性/温漂统计，再补 bias generation/reference
    profile；本次不清除 diagnostic-only。
  - 先完成 bias/reference loopback，再执行双板 P3 HIL；当前结果不能清除 diagnostic-only。
- 关联文件：
  - `components/calibration_manager/inc/calibration_bidirectional.h`
  - `components/calibration_manager/src/calibration_bidirectional.c`
  - `tests/unit/test_calibration_bidirectional.c`
  - `tools/tests/run_calibration_bidirectional_tests.ps1`
  - `tools/tdma_ring_monitor/tdma_single_board_loopback.py`
  - `tools/calibration_loopback_validate/calibration_loopback_validate.py`
  - `build-rtos-multicore-smoke/tdma_single_loopback_current/summary.json`
  - `build-rtos-multicore-smoke/calibration_loopback_com8_final/summary.json`
  - `build-rtos-multicore-smoke/calibration_loopback_com8_repeat10/summary.json`
  - `build-rtos-multicore-smoke/calibration_loopback_com8_sensors_adc8/summary.json`
  - `build-rtos-multicore-smoke/tdma_restore_after_calibration/summary.json`

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
  - `tools/calibration_ring_validate/calibration_clk_train.py`
  - `tools/calibration_ring_validate/calibration_clk_codebook_eval.py`
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
