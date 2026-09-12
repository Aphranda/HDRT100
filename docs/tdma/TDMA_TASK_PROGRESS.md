# TDMA 基础件主域任务进度

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_TASK_PROGRESS.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`
Last updated: 2026-09-12

本文档记录 TDMA foundation 的阶段性任务进度、验证结果和后续动作。待办事项放在 `TDMA_DOMAIN_TODO.md`。

当前 `TDMA-FLIGHT-002F` 的紧凑 RX 副本切片见 `TDMA-PROGRESS-20260912-023`，
证据根为 `out/HardwareAcceptance/20260912/tdma-flight-rx-copy/`。已降低私有存储与
包复制成本，完整 WCET、正式 RAM 及 STOP 应答问题仍开放，节点容量后续项保持后置。
前序 RX 分项计时切片与预算复评见
`TDMA-PROGRESS-20260912-022`，证据根为
`out/HardwareAcceptance/20260912/tdma-flight-rx-attribution/`。暂时保留现有门限，
按用户要求先完成当前切片验收，节点容量后续项保持后置。
前序原始 DMA 候选异步发现切片见
`TDMA-PROGRESS-20260912-021`，证据根为
`out/HardwareAcceptance/20260912/tdma-flight-async-scan/`。Core0 扫描私有窗口，Core1
按提示重新复制 live ring；完整 WCET、正式 RAM 和特等时间戳快速通道分别验收。
先闭合本切片，节点容量后续项保持后置。前序普通 RX Core0 解析切片见
`TDMA-PROGRESS-20260912-018`，证据根为
`out/HardwareAcceptance/20260912/tdma-flight-async-rx/`。随后完成的六节点编译
容量隔离核算见 `TDMA-PROGRESS-20260912-019`，证据根为
`out/HardwareAcceptance/20260912/node-capacity-ram-audit/`。核算未修改运行固件，
编译容量统一配置、准入及本轮硬件复核见 `TDMA-PROGRESS-20260912-020`，证据根为
`out/HardwareAcceptance/20260912/tdma-node-capacity/`。该实现保持固定 wire/存储布局，
严格失败和正式 RAM/WCET 缺口继续保留，`TDMA-FLIGHT-002I` 保持 IN PROGRESS。
前序 follower Core0 overlay 实现与验证记录见
`TDMA-PROGRESS-20260912-017`，证据根为
`out/HardwareAcceptance/20260912/tdma-flight-async-overlay/`。
按用户进一步要求推进异步准备与解析边界，设计记录见
`TDMA-PROGRESS-20260912-016`；该项纳入列车调度基础，四级服务策略保持后置。
最近完成的列车调度 RX 工作量限制切片证据根为
`out/HardwareAcceptance/20260912/tdma-flight-rx-budget/`，执行记录为
`TDMA-PROGRESS-20260912-015`。限制可丢弃镜像的同拍重复处理，并共享 magic 候选前缀；
完整 Core1 WCET、无更新稳态与 service blackout 仍需分别闭合，乘客调度继续后置。
前序列车调度分项计时切片证据根为
`out/HardwareAcceptance/20260912/tdma-flight-service-timing/`，入口为 `hardware-review.json`
与 `slice-manifest.json`，执行记录为 `TDMA-PROGRESS-20260912-014`。已取得正常和临时
许可证自主模式的完整 phase 归因；重复 RX 捕获/扫描是自主从站的主要开销，完整
Core1 WCET 与正式 RAM 仍失败。乘客调度保持后续任务。
当前异步候车平台与四级服务设计核验入口为
`out/HardwareAcceptance/20260912/tdma-flight-async-platform/design-audit.json`；
执行记录为 `TDMA-PROGRESS-20260912-013`，本次尚未改变固件或运行硬件。
最近已完成提交的 RX 观察副本与 DMA 接收计数切片证据根为
`out/HardwareAcceptance/20260912/tdma-flight-rx-observation/`；
执行记录为 `TDMA-PROGRESS-20260912-012`，尚未形成自主循环或产品验收结论。
前序 Calibration 临时许可证与实际自主 origin 试验的复核证据根为
`out/HardwareAcceptance/20260912/tdma-flight-origin-admission/`，入口为
`slice-manifest.json` 与 `hardware-review.json`。用户明确要求后，新证据使用日期目录
`20260912`；此前 `20260911/tdma-flight-origin-admission/` 内源码测试、构建、P3、
短帧基线及首次自主采集保留原位，新报告用路径和 SHA-256 引用，不改写历史失败。
该索引不是自主循环、完整 WCET 或产品验收凭证。
前序 TDMA owner intent 有界读取与窗口等待拆拍证据根为
`out/HardwareAcceptance/20260911/tdma-flight-service-boundary/`，入口为
`slice-manifest.json`；该索引不是自主环路或完整 WCET 验收凭证。
前序 origin 准备态累计事实发布修复证据根为
`out/HardwareAcceptance/20260911/tdma-flight-origin-status/`，入口为
`status-manifest.json`；该索引不是自主环路验收凭证。
前序 origin 分步准备候选与状态发布审计证据根为
`out/HardwareAcceptance/20260911/tdma-flight-origin-prepare/`，入口为
`prepare-manifest.json`；该索引不是自主环路验收凭证。
前序 origin 物理层与异步 adapter 工作树集成证据根为
`out/HardwareAcceptance/20260911/tdma-flight-origin-integration/`，入口为
`integration-manifest-r4.json`；该索引不是自主环路验收凭证。
前序 STOP 生命周期切片证据根为
`out/HardwareAcceptance/20260911/tdma-flight-origin-stop/`，入口为 `slice-manifest.json`；
完整 origin 图离线证据入口为
`out/HardwareAcceptance/20260911/tdma-flight-origin-frame/frame-manifest.json`；
前序 reference 头部离线审计入口为
`out/HardwareAcceptance/20260911/tdma-flight-origin-audit/audit-manifest.json`；
最近一次 follower recurrence 固件切片证据入口为
`out/HardwareAcceptance/20260911/tdma-flight-follower-recurrence/slice-manifest.json`；
前序 live-header XOR 切片证据入口为
`out/HardwareAcceptance/20260911/tdma-flight-live-crc/slice-manifest.json`；
前序有限同钟采集的证据入口为
`out/HardwareAcceptance/20260911/tdma-flight-b0/slice-manifest.json`。
前序 P3 与回归硬件证据入口为
`out/HardwareAcceptance/20260911/tdma-flight-baseline-ab/archive-index.json`。
该索引记录原始工作树根、原路径、归档路径和逐文件 SHA-256；复制后已逐文件核对。
原文件和报告内路径保持原样，严格失败与诊断继续状态保持原样；归档索引不是验收凭证。
以下历史生成路径仍用于说明取证来源；同名子目录可由归档索引定位。

### TDMA-PROGRESS-20260912-023 - 紧凑 RX 副本与共享相邻字节复制

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字均为实验快照，
  非事实源。证据根为 `out/HardwareAcceptance/20260912/tdma-flight-rx-copy/`。
- 变更：Core1 私有归一化帧改为对齐的字节数组，DMA word ring 不变；
  `tdma_pio_spi_phys_rx_ring_copy()` 共享相邻字节，错位复制长度 N 时只读 N+1 个原始字，
  无错位只读 N 个。legacy、Core0 扫描工位输入与当前 live 帧沿同一 helper 复制；
  完整区间准入、复制后 epoch/覆盖复验、锁定的 wire 相位与 latch/RTT 绑定顺序不变。
  已验证帧长与容量后使用 memcpy 交入 packet；计时版本不变，未改生命周期、资源、
  wire 布局、编译节点容量或预算。代码和文档分别提交，提交与门禁见 `commit-proof.json`。
- 软件与构建：真实 scanner 的全部位相位、两种 ISR 方向、字节值、非对齐目标哨兵、
  零长、SRAM/序号回绕、DMA 覆盖/epoch、取消及 wire 相位负测继续通过。
  RX/计时/bit-pipeline `79 passed`，相关 flight 回归 `132 passed`，文档检查器
  自回归 `18 passed`。flight 首次回归因独立夹具漏提取新 helper 失败，补齐真实函数
  与字节缓冲后通过，原 `flight-tests-r1.log` 保留。
  Release A/B/Boot、Flash 链接检查通过，build `20260912104508`，源码指纹
  `601abc9187498bbb80d1b12108e6b4ec8f93313734cc2c21491fc0472eed84c6`，
  `1017` files，全部 `634` 个 app 翻译单元使用六容量。两镜像的私有帧均由
  `1184 → 296 B`，链接余量 `4520 → 5408 B`，实际释放 `888 B`；正式要求由
  `DEFAULT_MIN_FREE_BYTES` 定义，本轮仍为 `49152 B`，RAM FAIL 保留。
  `build-inspection-r1.json` 核对 map 与反汇编，代码/数据布局变化不能直接当作时延收益。
- P3 与闭环：真实 `p3-r1` 完成四板 OTA、复位、链路和训练测量；凭证为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，`passed=true`、`strict_gates_passed=false`。
  coarse CLK NO2 `ARM result=8`、process-image NO4 `ARM result=5` 和未交接运行环
  的失败保留。独立 `matrix-strict-r1.json` 首次严格通过，确认成功后才运行生命周期。
  `lifecycle-r1` 的 process-before、raw-transition、process-restored+clock-train
  均首次 `passed/closed_loop_passed/realtime_gate_passed=true`，`diagnostic_continue=false`；
  四板 SD 原件重建、包对身份、mailbox CRC 与本地 owner 装卸边界均通过。
  最终 NO3 STOP 应答超时，因此生命周期包装命令退出 `1`、保留 `cleanup_error`；
  四板读回已停，一次原超时参数的 STOP 重发通过。`stop-recovery-scope-r1.json`
  区分三段功能通过与收尾失败，恢复不将原包装命令提升为全程成功。
- 计时：`timing-r1` 先完成目标 `25 s` 普通窗口，再签发 trial `912028`、epoch `66`、
  期限 `180 s` 的许可，完成目标 `90 s` 自主窗口；窗口内不执行 SD ARM/SAVE/download，
  最终撤销与四板 STOP 成功。普通完整记录峰值按 NO1..NO4 为
  `1221.272/927.764/1051.148/1038.900 µs`。自主完整记录峰值及其同条子项如下；
  origin 专用 RX 不报告这些子项，零不能解释为无接收成本。

  | 节点 | 上轮完整 µs | 本轮完整 µs | 本轮取帧 µs | 本轮包复制 µs | 本轮时间换算 µs | 本轮 latch µs |
  |---|---:|---:|---:|---:|---:|---:|
  | NO1 | 842.792 | 902.872 | 0 | 0 | 0 | 0 |
  | NO2 | 1082.260 | 1004.324 | 391.204 | 14.984 | 14.972 | 25.184 |
  | NO3 | 1236.568 | 1080.560 | 446.988 | 4.592 | 19.732 | 26.284 |
  | NO4 | 1260.060 | 1083.240 | 453.552 | 6.276 | 14.284 | 60.068 |

  从站自主完整峰值下降约 `7–14%`，包复制由上轮同类完整记录的
  `79.836/98.776/71.556 µs` 缩短；NO1 完整峰值上升约 `7%`，NO2 取帧也上升，
  不能宣称所有路径稳定加速。两轮均使用同版本计时，但属于独立有限窗口，存在布局
  与干扰差异；表中子项来自各自完整峰值，不是可拼接的独立最坏值。
  两个窗口 RX 均有进展，transport/schedule/profile 坏帧计数零增长，调度 overrun
  仍增长、所有节点完整 WCET 均 FAIL。自主从站观察丢弃增加
  `33748/31865/30016`，普通可丢弃镜像不能代替特等时间戳逐圈无损保全。
  同条自主从站记录中，RX_CAPTURE 之外仍有约 `492–533 µs`；该相减值不能作为
  删除 RX 后的性能承诺。明细见 `comparison-r1.json` 与 `measurement-conclusion-r1.json`。
- 自主原件：采集准备 `r1` 的初始 NO2 STOP、`r2` 的初始 NO3/最终 NO1 STOP
  应答超时保留，四板快照均已停止；一次有界软件复位后 `capture-prepare-r3` 成功。
  `capture-r1` 使用独立 trial `912030`、epoch `14`、期限 `180 s` 的许可；按主机
  单调时钟，四板采集/导出在发出授权后约 `42–57 s` 内完成，下载可在许可到期后
  结束，不再执行到期后的自主计时。四板原件重建、包对身份、mailbox CRC 和本地
  owner 装卸审计全部通过，原采集错误计数零增长；最后撤销至 epoch `16`、inactive，
  四板 physical armed/runtime enabled/adapter started 均为零。前一计时许可撤销至
  epoch `68`；中间存在软件复位，epoch 不跨启动比较，两窗口也不能拼成连续运行证据。
- 复核与封存：`review-r1.json`、`slice-manifest.json` 记录当前源码、构建和原件 SHA，
  切片结论为 PARTIAL。主机 map 提取正则首次失败、对照报告与包装记录重名失败、
  默认 staged 凭证未同步的首次门禁失败分别保留；修复工具副本、复核既有输出并将
  本轮真实 `run` 凭证原样同步后继续，未覆写旧证据或手工修改验收结论。
- 下一 gate：`TDMA-FLIGHT-002F` 保持 IN PROGRESS，当前 `380 µs` 门限保留。继续
  细分 live ring 取帧与 owner/adapter 交接，核实复制、计数/提示/复验、布局和干扰，
  再选择有界优化；正式 RAM、完整 WCET、STOP/准入问题、特等逐圈保全、service
  blackout 和同圈多 owner 仍需独立闭合。动态节点保持后置，registry/C11 状态不变。

### TDMA-PROGRESS-20260912-022 - RX 内部分项计时与 Core1 预算复评

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字均为实验/评估快照，
  非事实源。证据根为 `out/HardwareAcceptance/20260912/tdma-flight-rx-attribution/`。
  用户授权结合装卸成本与特等席稳定余量评估适度调整门限；本轮保持预算和节点容量不变。
- 归因边界：Release 反汇编证明 SDK `__rev` 是 `rbit`，host 桩语义一致，未据外部调用
  猜测其为瓶颈。`TDMA_SERVICE_TIMING_VERSION` 追加 RX 取帧、包复制、时间换算、
  latch 读取/重装子项，父子区间包含式，不能相加；origin 专用路径不报告这些子项。
  SCPI 自动输出同一 enum 的版本和长度，主机解码器兼容旧版并拒绝未知/错长 schema。
  recorder 的 Core1 单 writer、短 seqlock、边界 reset 与完整 peak 记录不变。
- 软件：计时/schema 与 RX/bit-pipeline 回归 `78 passed`，相关 flight、生命周期、
  准入与 schedule 回归 `132 passed`。首次构建因物理层漏包含计时头文件失败，补齐后
  Release A/B/Boot 和 Flash 链接检查通过，失败日志 `build-r1.log` 保留，成功记录
  为 `build-r2.json`。build `20260912095527`，源码指纹
  `6b93be59a500f27c8a47723702ffb260408dccf0ef3077f8dfeda3cab6ec1d45`，
  `1017` files，全部 `634` 个 app 翻译单元仍使用六容量。构建产物 SHA-256 见
  `source-checkpoint-r1.json`。记录器主 SRAM 增加 `96 B`，链接余量
  `4616 → 4520 B`；正式要求由 `DEFAULT_MIN_FREE_BYTES` 定义，本轮 `49152 B`，
  `ram-r1.log` 保留 FAIL。
- P3：`p3-r1` 完成四板 OTA、复位和四链路双组测量，凭证为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，`passed=true`、`strict_gates_passed=false`。
  coarse CLK NO2 `ARM result=5`、coded-marker、SCK 候选覆盖/重装余量及启动 barrier
  失败保留。SCK link 1 的八次有效读数仅覆盖 offset `1`，未满足两个候选要求；
  选中行最小重装余量为 `-1`。严格矩阵构造拒绝，未生成 `matrix-r1.json`；主控
  误发后继生命周期命令在缺少配置处失败，未进入运行，初始/最终 STOP 均成功。
  `calibration-recovery-scope.json` 区分该编排失败与板端失败，不将其计为有效硬件试验。
  一次同固件 `p3-r2` 恢复重新测量后，独立 `matrix-r2.json` 严格通过，自动 row `8`；
  恢复 P3 中 coded-marker 和启动 barrier 失败仍保留，不覆盖初始 run 凭证。
- 生命周期：`lifecycle-r2` 的 process-before、raw-transition、带 clock-train 的
  process-restored 均首次严格通过，`passed/closed_loop_passed/realtime_gate_passed=true`、
  `diagnostic_continue=false`。四板 SD 原件重建、有限包对身份、mailbox CRC 和局部
  owner 装卸边界均通过，transport/schedule/profile 错误零增长。
- 试验恢复与原件：使用 `256 B` 下载页，普通和自主两批四板 SD 原件均首次下载成功。
  计时准备前，`trial-prepare-r1` 的 NO4 STOP、`trial-prepare-r2` 初始 NO1/最终 NO3
  STOP 应答超时保留；四板读回已停，一次有界复位后 `trial-prepare-r3` 成功。
  `trial-r1` 的授权 epoch `14`、期限 `360 s`；采集/下载耗时约 `363.926 s`，
  后置自主计时首条记录晚于到期拍，NO1 physical armed/adapter started 为零，
  所以原 trial 退出码仍为失败。SD 原件审计全通过不能抹去计时失败；撤销后 epoch
  `16`、inactive，四板 STOP 成功。独立 `trial-r2` 使用 epoch `30`、期限 `180 s`，
  不执行 SD ARM/SAVE/download，完成目标 `90 s` 自主计时；撤销后 epoch `32`、inactive，
  最终四板 physical armed/runtime enabled/adapter started 均为零。两窗口属于不同
  授权 epoch 的同源码证据，不能拼成单个连续无损运行窗口。
- 计时：普通目标 `25 s` 的完整记录峰值按 NO1..NO4 为
  `1223.220/1070.984/1304.272/1238.160 µs`。自主完整峰值及其同条记录内的 RX 子项如下；
  origin 专用路径不报告这些 RX 子项，表中的零不代表没有接收成本。

  | 节点 | 完整 phase µs | RX 取帧 µs | 包复制 µs | 时间换算 µs | latch µs |
  |---|---:|---:|---:|---:|---:|
  | NO1 | 842.792 | 0 | 0 | 0 | 0 |
  | NO2 | 1082.260 | 341.796 | 79.836 | 20.660 | 63.284 |
  | NO3 | 1236.568 | 509.784 | 98.776 | 12.556 | 43.288 |
  | NO4 | 1260.060 | 613.536 | 71.556 | 18.624 | 26.400 |

  自主 follower 的取帧占 RX_CAPTURE 约 `61–81%`，该区间还包含 DMA 观察、提示处理、
  live/private copy 和复验，不能归结为单纯 memcpy。包复制是额外逐字到逐字节转换；
  Release 循环本体是 load/store/compare/branch，没有函数调用或显式 wire wait。
  剩余 CPU、存储访问/干扰成本需继续验证，未仅凭 `__rev` 外部调用定位根因。
  两个有效计时窗口均有 RX 进展、transport/schedule/profile 错误零增长，但所有节点
  完整 phase 仍超 `380 µs`；同条自主 follower 记录的 RX_CAPTURE 之外仍有约
  `506–538 µs`。这些是包含计时开销的有限记录，不是 WCET 上界或删除 RX 后的性能承诺。
  `attribution-r1.json` 保留完整同条分解与前轮对照，不把本轮计时布点当作性能优化。
- 门限评估：事实源为 `PROJECT_CORE1_CYCLE_CYCLES`、`PROJECT_CORE1_PHASE_TDMA_*`、
  `PROJECT_CORE1_TDMA_SOFTWARE_MARGIN_CYCLES` 和板级时钟。当前快照每周期 `1000 µs`，
  TDMA 窗口 `400 µs`、WCET `380 µs`，下一 VDC 从窗口末端开始；全表声明执行预算合计
  `959.2 µs`。旧串行最大 wire `245.6 µs` 加声明软件预留 `120 µs` 为 `365.6 µs`，
  仍不能把声明预留当作装卸实测。自主硬件与 CPU 可重叠，不能再次从 CPU 预算扣除
  用户约 `240 µs` 物理飞行时间。以该典型圈周期估算，每个调度周期最多跨过约五圈；
  若每圈处理 `140 µs`，同拍约需 `700 µs`。特等席需硬件逐圈保全、固定队列及有界
  收割，容量由最短圈周期和最长消费停顿加边界/在途余量确定，不能以典型值代替上界。
  `390 µs` 候选仅余 `10 µs` 起始/收尾空间，必须测得足够余量才可考虑；`400 µs`
  耗尽窗口余量，更大的值须整体重排 profile/phase 并复核其他 mandatory 负载及 C11。
  当前保留 `380 µs`，详细模型和候选见 `budget-assessment-r1.json`、`schedule-r1.svg`。
- 收尾：本轮功能取证闭合，切片验收结论为 PARTIAL。源码复核及成功/失败证据入口为
  `implementation-review-r1.json`、`review-r1.json`、`slice-manifest.json`；代码与
  文档分离提交及门禁证据见 `commit-proof.json`。`TDMA-FLIGHT-002F` 保持 IN PROGRESS，
  正式 RAM、完整 WCET、校准/STOP 应答问题、逐圈特等席、service blackout 和同圈多
  owner 门禁仍开放。下一候选是压缩 CPU 私有 RX 帧的表示并去掉逐字转换，继续归因
  live ring 取帧与 owner 交接；必须保留 DMA word ring、epoch/覆盖复验和 wire/latch
  边界，并重新完成对应源码的软硬件验收。动态节点后置，registry/C11 不变。

### TDMA-PROGRESS-20260912-021 - 原始 DMA 候选发现异步工位与当前切片验收

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字均为实验快照，非事实源。
  证据根为 `out/HardwareAcceptance/20260912/tdma-flight-async-scan/`。按用户要求先
  完成本切片验收；未继续调整节点容量，也未操作 NO5 或另一台设备的单板工作。
- 实现：独立 `tdma_rx_scan_t` 保存至多 `TDMA_RX_SCAN_WINDOW_BYTES` 的已完成
  DMA 字节副本，Core0 用真实 transport CRC 解码器发现几何位置、bit shift、帧长
  和连续帧证据。Core1 直接定位当前完整帧、重新复制 live ring，并复验覆盖与
  observation epoch；原 RX 工位仍在 live copy 后绑定当时 latch/RTT，后台完成
  时刻不充当接收时刻。tail 独立建模，不能从最大 payload 长度反推实际帧间距。
  工位忙时不等待、不覆盖输入；旧请求、persona 或配置提示拒绝。STOP/训练/origin
  切换取消提示，被取消 worker 只归还私有工位，重新 ARM 等待取消 ACK。parser
  重同步不能改变已锁定 overlay 相位；维护路径保留原扫描器。
- 软件：真实 legacy/async DMA 捕获共 `25` 项，相关 flight 回归共 `150 passed`。
  覆盖所有 bit shift、ISR 方向、跨环回绕、worker 暂停后读新帧、复制时覆盖和 epoch
  变化、取消 ACK、CRC 假头、陈旧配置提示及容量越界。补充资源、PIO、NO5 隔离、
  调度和计时回归中 `77 passed / 7 failed`，失败来自预算工具未跟随共享头文件
  常量位置；补入 `CONFIG_PATHS` 后调度/计时 `8 passed`，原失败保留。
- 构建：Release A/B/Boot 及 Flash 链接检查通过；build `20260912091130`，源码
  指纹 `75dcc6c58fed028c7973f13ebd2a40a9fd6ee99948b968b868b6de29eae8fd83`，
  `1016` files，全部 `634` 个 app 翻译单元仍使用六容量。摘要和产物 SHA-256 见
  `source-checkpoint-r1.json`。扫描工位占 `704 B`，物理对象增加 `40 B`，主 SRAM
  净增 `744 B`，链接余量 `5360 → 4616 B`；正式要求仍由
  `DEFAULT_MIN_FREE_BYTES` 定义，本轮为 `49152 B`，`ram-r1.log` 保留 FAIL。
- P3：四板 OTA 和四链路双组测量通过。凭证为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，`passed=true`、`strict_gates_passed=false`。
  coarse CLK 的 NO4 `ARM result=8`、首次 process-image 的 NO3 `result=8` 及未
  handoff running 保留失败。本轮 TRN00/01/02 独立生成严格 `matrix-r1.json`，
  `passed=true`、`diagnostic_continue=false`；自动 row `3` 的 DATA 为 `[5,5,5,5]`。
  本次自动行成功不消除前轮自动选行/CRC 失败，也没有复用旧 build 矩阵。
- 生命周期：`lifecycle-r1` 的过程映像通过，raw 切换在 NO1 flight-map 准入
  `result=5` 处拒绝，发生在物理 ARM 前；`lifecycle-r2` 的 NO3 同类拒绝保留。
  第二轮 STOP 响应有一次超时，后续四板 physical/runtime/adapter 停止快照均为零，
  不能用状态读回抹去串口超时。原始动作、失败快照和有界恢复见
  `lifecycle-rejection-r1.json`、`reset-recovery-r1.json`。软件复位后
  `lifecycle-r3` 先保留 NO2 `result=8` 及 STOP 恢复，随后 process-before、
  raw-transition、带 clock-train 的 process-restored 均严格通过，
  `passed/closed_loop_passed/realtime_gate_passed=true`、`diagnostic_continue=false`。
- 原始采集：`lifecycle-r3` 四板 SD 片段重建、有限包对身份、mailbox CRC 与局部
  owner 装卸边界全部通过，采集期间 transport/schedule/profile 错误零增长。
  `trial-r1` 的自主采集在 NO2/NO4 下载时出现 non-hexadecimal 响应，原 summary
  和整体 trial 退出码仍为失败；两板原运行快照的 TDMA 错误增量均为零。STOP 后以
  较小串口页重读同一批已导出文件，核对 build、capture tag/sequence 与状态完全
  不变，未重做 ARM/SAVE；`recovered/` 保留重读日志。自主四板原件均可重建，
  有限包对身份、mailbox CRC 与装卸边界通过，但重读不能消除首次串口下载失败。
- 计时：`trial-r1` 使用有限期 trial `912022`，普通采集目标 `25 s`、自主目标
  `90 s`，实际命令、样本与完整 profile 记录均保留。普通完整峰值按 NO1..NO4 为
  `1243.000/1015.344/1156.704/1170.200 µs`；自主为
  `869.380/1049.844/1227.028/1260.388 µs`。自主 follower 最坏完整记录内的
  RX capture 为 `567.540/721.836/709.588 µs`，相较上一轮
  `1706.128/2110.744/2073.776 µs` 有明显下降；普通模式完整峰值未一致下降。
  比较见 `timing-compare-r1.json`，这是分次有限观测，不是 WCET 上界。
  四板仍未通过当前 `380 µs` 完整 phase 预算，两个计时窗口均有 RX 进展且
  transport/schedule/profile 错误零增长。剩余 live copy、交接和 owner 工作仍须
  用完整记录继续归因，不能以扫描已移出 Core1 宣布实时通过。
- 收尾：trial 授权 epoch `54`，撤销后 epoch `56`、inactive；四板最终 physical
  armed/runtime enabled/adapter started 均为零。源码复核、成功/失败尝试、原始
  SD、构建与计时证据入口为 `review-r1.json`、`slice-manifest.json`，本轮验收
  结论为 PARTIAL。代码提交 `590b9a0` 已通过当前 staged 源码的 P3 凭证门禁；
  文档独立提交，全部文档门禁和提交后指纹核对见 `commit-proof.json`。
- 状态：`TDMA-FLIGHT-002F` 保持 IN PROGRESS；启动准入与串口失败、正式 RAM、
  完整 Core1 WCET、origin 准备、service blackout、同圈多 owner 及特等逐圈保全
  继续保留各自门禁。registry/C11 不变，长期目标保持 active。

### TDMA-PROGRESS-20260912-020 - 编译节点容量实现与四板复核

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002I`。以下数字均为实验快照，非事实源。
  证据根为 `out/HardwareAcceptance/20260912/tdma-node-capacity/`。按用户顺序先封存
  RX 切片，再实施容量辅助切片；未覆盖前序证据，也未操作另一台设备的单板工作。
- 实现：`PROJECT_NODE_CAPACITY` 默认六，CMake 和头文件校验二至八的编译容量，
  统一 Board Identity、TDMA、VDC、RefMem sync、Calibration path/training 本地
  存储。active profile 仍定义实际在线数；固定 SHORT、RefMem 目录、应用模型表及
  Calibration 持久化 stage 不裁剪。既有 HAOFV/profile 准入和直接 runtime、adapter、
  Calibration 入口均约束本地容量，adapter 同时拒绝非法 reference 编号。
- 兼容性：`CALIBRATION_PATH_CRC_LINK_COUNT` 保留原主机 import CRC 的固定逻辑
  序列，缺少的尾项补零。VDC 本地路径表 CRC 随内部布局计算，不声称该 CRC 跨容量
  相同。真实存储 codec/validator 验证共同拓扑的字节兼容及超容量解码拒绝。
- 软件：容量测试 `17 passed`，覆盖二至八边界、失败不修改已有 profile/编号、
  runtime/stage 拒绝、RefMem source canary/peer 保护、VDC 矩阵及六/八容量的二/四/六
  节点存储互读，并验证六容量拒绝七/八节点持久化拓扑；
  RX、command DMA、TRN03、nonblocking 和 origin admission 回归 `134 passed`。
  adapter、runtime、service、VDC、Calibration path/store 和 RefMem application
  contract 测试通过；八容量历史 profile/runtime/adapter/service/VDC/RefMem sync
  真实编译运行通过。早期 fixture 缺依赖、CRC 上下文及临时运行脚本失败保留在
  `capacity-tests-r1/r2/r3`、`legacy-eight-r1/r2`，后续修正未放宽产品判据。
- 构建：六/八容量 Release A/B/Boot 完整构建及 Flash 链接检查均通过。两组 app
  各 `632` 个翻译单元采用对应容量；六容量 build 为 `20260912081155`，源码指纹
  `36fdfc894e34b866ae10cf6498857c0461b1f35905f7635bd416c6561ad83d16`，
  `1012` files；产物摘要见 `source-checkpoint-r1.json` 与 `ram-compare-r1.json`。
- RAM：两套 app 的主 SRAM 净省均为 `2768 B`，scratch_y 另省 `200 B`；
  `.bss` 为 `468448 → 465680 B`，`link_free_bytes` 为 `2592 → 5360 B`。
  正式要求仍由 `DEFAULT_MIN_FREE_BYTES` 定义，本轮快照为 `49152 B`，六容量仍缺
  `43792 B`；两组正式 RAM 检查均 FAIL。没有放宽 RAM/WCET 或借用 PIO0/DMA7。
- P3：四板 OTA 均确认本轮六容量 build，四链路双组测量通过；凭证 scope 为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，`passed=true`、`strict_gates_passed=false`。
  coded marker 保留 ARM 拒绝、串口响应形状异常和缺 summary 失败；原始 process-image
  保留启动屏障超时。`matrix-r1.json` 从本轮 TRN00/01/02 原件独立生成，
  `passed=true`、`diagnostic_continue=false`，未移植旧 build 的矩阵或凭证。
- 严格失败：`lifecycle-r1/process-before-r1` 在启动屏障通过后，NO1 观察窗口新增
  一个 transport CRC 错误，其他节点未增长；`diagnostic-lifecycle-r1` 的恢复段再次
  记录 NO1 transport CRC 增长。原始计数、坏帧诊断和有界 STOP 均保留；不能用随后
  恢复或诊断流程抹去这些失败。`crc-diagnostic-r1` 随后保留 NO1 在整个采集期间
  新增 `43` 个 transport CRC 错误；四板 SD 分段均能重建、有限包对通过，但原采集
  总结果仍失败。有限窗口的好包不能覆盖窗口之外的坏帧。
- 候选行恢复：当前矩阵自动选择 row `1`，DATA 为 `[5,4,5,5]`；显式 row `3`
  为 `[5,5,5,5]`，两者均来自本轮矩阵候选，未覆盖原矩阵或借用旧验收。
  `lifecycle-row3-r1` 使用既有 `--offset-row-id 3`，process-before、raw-transition、
  process-restored 全部 `passed/closed_loop_passed/realtime_gate_passed=true`、
  `diagnostic_continue=false`。随后四板采集的 transport/schedule/profile bad 增量
  均为零，SD 原件重建、有限包对身份、mailbox CRC 与 owner 装卸边界审计通过。
  这是显式候选行的有限恢复证据；自动选行与采样余量仍未闭合，不能直接把原失败
  归因于容量实现，也不构成最坏时序、同圈多 owner 或长期零误码证明。
- 自主复核：`trial-row3-r1` 使用有限期 trial `912020`，普通采集目标 `25 s`、
  自主采集目标 `90 s`；自主 persona、四板 SD 采集和原件审计通过，最后撤销授权、
  确认四板 physical armed/runtime enabled/adapter started 均为零。普通完整记录
  峰值依 NO1..NO4 为 `1221.004/987.268/1246.964/1262.896 µs`；自主为
  `877.932/2292.692/2517.876/2541.132 µs`，均未满足当前 `380 µs` phase 预算。
  自主 follower 最坏完整记录中的 RX capture 仍达 `1706.128/2110.744/2073.776 µs`。
  这些是有限观测，不是 WCET 上界；容量收益不能代替 scanner/origin 的后续拆分。
  命令、原始拒绝、软件/构建/OTA/运行证据、候选行对照和主控复核分别收录于
  `row-comparison-r1.json`、`review-r1.json` 与 `slice-manifest.json`，提交证明另见
  `commit-proof.json`；代码、文档按仓库门禁分离提交。
- 状态：`TDMA-FLIGHT-002I` 保持 IN PROGRESS。实现、编译收益及四板候选证据不等于
  六板/混合容量实板通过；严格校准/启动/CRC 失败、完整 Core1 WCET、正式 RAM、
  scanner/origin 异步拆分及特等逐圈通道仍需各自验收。registry/C11 状态不变，
  长期目标 `TDMA-FLIGHT-002` 保持 active。

### TDMA-PROGRESS-20260912-019 - RX 切片封存后的六节点 RAM 隔离核算

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002I`。以下数字均为实验快照，非事实源。
- 顺序：先封存 `TDMA-PROGRESS-20260912-018`，代码提交 `2b7fb79`、文档提交
  `f47751e`，再以干净 HEAD 进行容量核算。该核算没有修改受控固件、运行配置或板端
  状态，没有重用旧 P3 凭证为六节点候选放行。
- 方法：使用前一切片 `build-r2/compile_commands.json` 的 ARM Release 原始参数，
  在证据目录内复制源码，只将 `BOARD_IDENTITY_MAX_NODES`、`TDMA_RING_NODE_MAX`、
  `VDC_DOMAIN_NODE_COUNT`、`REFMEM_SYNC_NODE_COUNT`、`CALIBRATION_PATH_MAX_LINKS`
  和 `CALIBRATION_TRAINING_PHASE_MAX_NODES` 从八改为六。八节点/六节点各重编译
  158 个项目翻译单元，316 次编译全部成功；当前 map 内存活静态对象的八节点尺寸与
  已验收对象逐项相符，未链接的对象和 Flash 常量不计收益。
- 范围：保留 `TDMA_FLIGHT_SHORT_SLOT_COUNT`、SHORT packet/payload 布局、
  `DISTRIBUTED_REFMEM_TABLE_SIZE`、`DISTRIBUTED_REFMEM_NODE_COUNT`、应用模型表，
  以及 `TDMA_RING_CALIBRATION_LINK_MAX` 和 Calibration 持久化 payload 容量。
  仅限制实际在线节点数不会自动释放静态 RAM。RefMem 固定目录不能按节点比例缩减；
  校准 stage/训练缓存进一步裁剪须先解耦运行容量和持久化格式，本次收益不包含该项。
- 静态对象收益：RefMem 三处同步上下文合计 1,728 B、VDC command retention 128 B、
  飞行同步的 per-source 序号 8 B；Calibration 三份 path snapshot 与 import links
  合计 704 B；VDC runtime path table 200 B，其中矩阵 112 B、路径 entry 88 B。
  主 SRAM 合计 **2,768 B**；发布 snapshot 另省 **200 B scratch_y**，不计入主 SRAM
  余量。堆、任务栈预留、DMA ring 和固定 packet 池未缩减。
- 链接复核：两组隔离对象各重链接一次，八节点各段尺寸/地址与验收 ELF 完全相符。
  六节点 `.bss` 从 468,448 B 降为 465,680 B，`link_free_bytes` 从 2,592 B 增至
  **5,360 B**，主 SRAM 净增 **2,768 B（2.703125 KiB）**。两组正式 RAM checker
  均返回 FAIL，要求仍为 49,152 B；六节点候选仍缺 43,792 B。容量缩减不能代替
  完整 Core1 WCET、原始 RX scanner 拆分和特等快速通道验收。
- 原件：`input-r1.json` 保存源码哈希、容量边界和完整编译命令；
  `compile-results-r1.json`、`report-r1.json` 保存逐对象结果；
  `link-report-r1.json` 保存编译器版本、ELF/map 哈希和完整链接指标。
  `eight-baseline/`、`six-local/` 保留测量 ELF/map、链接日志及正式 RAM FAIL 日志。
  这些是容量测量产物，不是可部署六节点固件或硬件验收凭证。
- 后续：`TDMA-FLIGHT-002I` 保持 `PENDING`。独立实现编译容量单一配置、HAOFV
  准入、超容量拓扑/编号拒绝及兼容性验证，再完成软件测试、受影响构建和真实 P3/短帧
  复核；不更改 registry/C11 状态，`TDMA-FLIGHT-002` 继续执行。

### TDMA-PROGRESS-20260912-018 - 普通 RX 的 Core0 异步解析与采集证据绑定

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字均为实验快照，非事实源。
- 变更：新增 `tdma_rx_prepare_t` 固定工位，Core1 交入 packet 副本、原始 RX/TX latch、
  RTT、origin observation 与 owner 匹配资格。Core0 完成 transport decode/CRC、
  自主 origin mailbox 完整性检查和参考节点坏帧诊断；Core1 只消费当前 epoch、
  配置/map、时间资格和采集年龄合格的结果，继续独占 health、novelty、FIFO 与生命周期
  事实。结果提交不重读最新硬件时间，也不延长旧帧的接收年龄。
- 所有权：Core0 不持有 adapter、engine、FIFO、DMA 或 origin bank 指针；bank 复用
  不替换已采集帧的 observation 与 owner generation。STOP 先取消资格并停硬件，
  若 worker 已 claim 则保留静态工位直到取消 ACK，旧 READY 不能进入新 ARM。
  忙工位不等待、不再次采集和覆盖输入；这仍是允许缺口的普通观察路径。
- 软件：adapter 主机回归覆盖 Core0 暂停、输入复用、采集时的 latch/RTT 保全、
  REQUESTED/BUILDING/READY 取消、ARM 拒绝、epoch/config/map/age 失效、坏帧诊断、
  异步 bootstrap→origin、成对元数据/bank 复用以及 mutable remote mailbox CRC 拒绝。
  实际 scanner、DMA、非阻塞与短帧工具相关回归 `129 passed`；文档检查器单测
  `18 passed`。首轮测试在启动后绑定 RTT callback，绑定按既有规则被拒，fixture 修正
  后回归通过；失败原件保留，未更改接口生命周期来适配测试。
- 资源：首次 `build-r1` 因 RAM 超出 `296 B` 链接失败，未部署。随后把物理工位与
  原有注入队列做 STOPPED 互斥绑定，拒绝重绑及注入覆盖，不增加 DMA/PIO/任务栈。
  ARM ABI 工位 `840 B`，复用既有 `2496 B` 池，整机静态区相对前版仅增加 `8 B`。
  `build-r2` 的 Release、A/B/Boot 链接门禁通过；正式 RAM 仍以余量 `2592 B`
  小于要求 `49152 B` 判 FAIL，未放宽门槛。build 为 `20260912071055`，源码
  SHA-256 与产物哈希绑定在 `source-checkpoint-r1.json`。
- 实板：四板 OTA 和 P3 quick diagnostic 流程完成并签发当前源码凭证，
  `strict_gates_passed=false`。初始启动屏障在原 `2 s` 窗口内仅获得所需两个稳定
  样本中的一个；首样本含 receive reject/incomplete 增长，失败原件保留。
  同源码同 generation 的实测矩阵通过且 `diagnostic_continue=false`。
  `lifecycle-r1` 首次 process-image ARM 在 NO2 遇到 `arm_result=8` 配置拒绝；
  既有有界恢复 STOP 后第二次严格通过。raw-flight 与恢复 process-image 首次严格
  通过，三段通过记录均无 diagnostic continue。四板原始 SD capture 全部重建，
  有限 CRC、sequence 与 owner 范围审计通过。
- 计时：普通模式与有限许可证自主模式均完成完整 phase 记录，下表单位为微秒。
  RX 分项取各自有限查询记录中有调用的最大值，与完整峰值不一定在同一拍，不能
  相加或视为 WCET 上界。`RX_PARSE` 现在度量 Core1 的结果验证与事实提交。

  | 节点 | 普通完整峰值 | 自主完整峰值 | 自主 RX capture 采样最大值 | 自主 RX_PARSE 采样最大值 |
  |---|---:|---:|---:|---:|
  | NO1 | 1249.580 | 893.948 | 65.664 | 307.660 |
  | NO2 | 1002.432 | 2484.624 | 1954.332 | 208.244 |
  | NO3 | 1238.584 | 2499.692 | 2027.832 | 204.188 |
  | NO4 | 1206.168 | 2508.672 | 2083.900 | 215.324 |

- 对照：相对上一 overlay 切片，普通完整峰值 NO1 下降约 `6.8%`，从站下降约
  `23.8%–26.7%`；自主 NO1 下降约 `26.7%`，从站下降约 `7.5%–16.0%`。自主
  RX_PARSE 的有限分项样本最大值从 `[639.568,487.064,506.780,473.772]` 降为
  表内值，但所有节点完整 phase 仍超出当前 schedule 预算。普通 NO1/NO3/NO4 的
  查询未采到有 RX_PARSE 调用的记录，明确记为未采到，不能记作零耗时。
  从站自主完整峰值仍由原始 DMA 扫描主导，结果提交本身也仍需收敛。
- 基础运输：两种计时窗口的 transport/schedule/profile 错误增量均为零；普通观察
  drop 增量为零，自主为 `[0,31662,27834,27420]`。普通镜像允许缺口，这不证明
  时间戳逐圈无损。`stage-comparison.json` 保留每个分项有调用记录的样本数和范围。
- 自主取证：trial `912018`、grant epoch `76` 下确认自主状态，四板原始 SD 下载
  与有限 owner/CRC 审计首次通过，完成自主计时窗口后撤销到 epoch `78` inactive。
  `final-stopped.json` 证明四板 `armed/ring_enabled/adapter_started` 全为零。
- 证据入口：`out/HardwareAcceptance/20260912/tdma-flight-async-rx/` 中的
  `hardware-review.json`、`slice-manifest.json`、`source-checkpoint-r1.json`、
  `ownership-review.json` 与原始日志；分离提交与门禁核验见 `commit-proof.json`。
  完整 Core1 WCET、正式 RAM、原始 DMA 扫描、origin 准备、service blackout、
  Core0 拥塞、同圈多 owner 和特等快速通道逐圈保全继续作为未闭合项。

### TDMA-PROGRESS-20260912-017 - follower overlay 的 Core0 异步准备

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002F`。以下数字均为实验快照，非事实源。
- 变更：Core1 复制有效 RX 模型、本地 mailbox 和紧凑固定授权，通过
  `tdma_overlay_prepare_t` 向 Core0 发布请求。现有 Core0 数据任务执行纯编码、校验与
  overlay 构建，不写 adapter/engine 的 Core1 事实或硬件寄存器。完整 READY 发布后，
  Core1 检查 epoch、map generation、alignment 和闲置池，再绑定固定上限的 DMA
  描述符。已就绪 TX 不依赖本拍普通 RX；无新版本保留现有硬件计划。
- 生命周期：输入是独立副本，FIFO 槽复用不影响构建；输出只借用现有闲置计划池，
  DMA 选择后继计划前旧池不可复用。STOP 立即执行硬件停止，取消已发布请求；若
  Core0 仍持有写权限，STOP 保持 pending，迟到 worker 只交还权限，不能进入新 ARM。
  该 worker 的轮询有界，不在 Core1 等待后台计算。
- 软件：adapter 主机负测覆盖无 RX、坏 RX、Core0 暂停、源槽复用、整图生产者到本地
  mailbox 规范化、旧 DMA 池保全、取消前后 ARM、旧 READY 结果和越权 layout 拒绝；
  实际物理 grant/commit 与 DMA 总线模型共同检查旧计划持续运行、epoch/alignment
  失效及选择后退休。相关 Python/实际 C 回归 `129 passed`，adapter host runner 通过。
  首轮缺失 include、fixture 签名未同步的失败原件保留。
- 资源：首次构建因 RAM 链接区超出 `204 B` 失败，未上板。授权快照随后缩为固定本地
  segment 资格，TX 输入只复制 `TDMA_FLIGHT_SHORT_SLOT_SIZE`；仍使用既有计划池，
  不放宽 RAM/WCET 门槛。最终 job 为 `476 B`，链接余量相对前版减少 `492 B`；
  `build-r2` 的 Release 及 A/B/Boot 链接门禁通过，正式 RAM 仍以余量 `2600 B`
  小于要求 `49152 B` 判 FAIL。build 为 `20260912061753`，源码指纹及产物哈希见
  `source-checkpoint-r1.json`。
- 实板：同包四板 OTA 与 P3 quick diagnostic 流程完成；首次短帧启动屏障失败原件
  保留。第一轮观测包含 receive reject/incomplete 增长，后续出现干净样本，但未达到
  原启动期限内所需连续样本数，不能写成严格 P3 通过。使用本源码同 generation
  实测数据生成 `matrix-r1.json`，重装安全行通过且 `diagnostic_continue=false`。
  `lifecycle-r1` 的 process-image → raw-flight → process-image 三段均首次严格通过；
  四板原始 SD capture 可重建，CRC/sequence/owner 范围审计通过，基础运输错误增量为零。
- 计时：普通与有限临时许可证自主模式均采集完整 phase；下表分项来自各节点同一次
  自主完整峰值，单位为微秒，事实源为 `trial-r1/normal-audit.json` 与
  `trial-r1/autonomous-audit.json`，不是 WCET 上界证明。

  | 节点 | 普通完整峰值 | 自主完整峰值 | 自主 RX capture | 自主 RX parse | 自主 overlay |
  |---|---:|---:|---:|---:|---:|
  | NO1 | 1341.272 | 1219.204 | 64.688 | 639.568 | 0.000 |
  | NO2 | 1368.072 | 2685.240 | 1880.912 | 487.064 | 60.320 |
  | NO3 | 1625.776 | 2976.964 | 2107.376 | 394.524 | 104.936 |
  | NO4 | 1596.108 | 2874.376 | 2152.672 | 380.464 | 79.900 |

- 对照：相对 `tdma-flight-rx-budget` 切片，从站普通完整峰值下降约 `39.7%–44.1%`，
  自主下降约 `19.6%–27.1%`；NO1 普通/自主分别下降约 `0.4%/1.1%`。全部节点仍超出
  当前 schedule 的完整预算，RX 捕获/解析仍是主要开销。自主计时区间基础运输错误
  增量为零，普通观察副本 drop 增量为 `[0,12009,20481,21282]`，不证明同步样本无损。
- 自主取证：临时许可 epoch `66` 下确认自主 state，完成计时后撤销到 epoch `68`
  inactive，四板 STOP 成功。四板初始 SD 下载均截断，原 capture/trial 失败保持原样；
  停机后只重读相同冻结文件，未重新 ARM/SAVE。`capture-audit-r2.json` 的原始重建与
  有限 owner/CRC 审计全通过，原窗口基础运输错误增量为零；不据此消除串口下载失败。
- 证据入口：`out/HardwareAcceptance/20260912/tdma-flight-async-overlay/hardware-review.json`
  与 `slice-manifest.json`，各次失败、软件检查、构建、P3、矩阵、短帧、原始波形和
  计时对照均保留引用。普通 RX 捕获/解析、origin 准备、完整 Core1 WCET、无更新
  节拍、service blackout、同圈多 owner 与特等同步时间戳逐圈保全仍未完成。

### TDMA-PROGRESS-20260912-016 - 将异步准备与解析纳入列车调度基础

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B/002F`。用户再次明确“检票入站”
  时间不纳入列车环路；因此异步准备/消费属于当前列车基础，服务等级策略仍后置。
- 变更：依据已完成的代码提交 `285ff44` 和完整计时，更新 Architecture 的待实施
  owner 边界及 TODO 进入顺序。`002F` 进入 IN PROGRESS；`002G/002H` 仍 PENDING。
  本记录未实现新的 Core0 后台构建、RX 卸载或跨核发布，也不提升 registry/C11 状态。
- 源码核验：follower 的 `prepare_process_overlay()` 仍位于 `if (rx_ok)` 内；其实现
  在 Core1 解码、复制模型、执行 flight engine 并构图。物理层
  `tdma_pio_spi_phys_prepare_process_overlay()` 还混合了纯计划构建与 DMA 发布。
  既有 flight FIFO 已提供 Core0 payload 发布和业务消费，但 payload READY 不等于
  DMA 可直接使用的完整计划，不能仅改变函数调用所在核。
- 下一 gate：先拆开纯 overlay 构建与硬件提交，明确输入/构建池 lease、停止取消与
  epoch，再接入 Core0 准备和 Core1 有界发布；已准备 TX 不依赖本拍普通 RX 解析成功。
  普通解析在固定记录交接后执行，其结果经版本机制返回唯一 TDMA owner；不得直接
  从 Core0 修改 runtime/flight engine。随后验证 Core0 拥塞、迟到准备和解析停顿时
  环路节拍、旧版本复用、STOP、资源与完整 Core1 WCET。
- 证据：`out/HardwareAcceptance/20260912/tdma-flight-async-platform/owner-boundary-r2.json`。
  本项为源码边界与执行顺序核验，不生成新固件或新的硬件通过声明；基础运输完整性
  和同步时间的独立硬件保全仍按原契约约束。

### TDMA-PROGRESS-20260912-015 - 限制同拍 RX 工作量与 SCK 前置证据恢复

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。以下数字为实验快照，非事实源。
- 变更：以 `0888b0a` 为基线，代码提交 `285ff44`。硬件飞行副本每 phase 只调用一次捕获/解析；软件
  store-forward 保留独立的有界排空。scanner 每个候选只读取共享字节前缀，再在
  寄存器检查各 bit phase；保留 header、CRC、对齐稳定性及复制后 epoch/覆盖复验。
  不增加静态 RAM、PIO/SM/DMA 资源或乘客服务等级。
- 软件与构建：实际 scanner 的两种 ISR 方向、全部 bit phase、SRAM wrap、假 magic、
  部分帧、覆盖和计数歧义回归通过；持续 ready 的 adapter 每 phase 只处理一个副本，
  坏帧仍拒绝，后继正常序列可恢复且无软件重发。相关 Python/实际 C 回归
  `129 passed`，adapter host runner 通过；首次测试 stub 编译错误保留。上一版真实
  scanner 对新 SRAM 读取预算按预期失败，见 `old-scanner-check.json`。
  Release build `20260912050200`，源指纹
  `2dc1b351547ec9130e391ff846c837f6b4967625d51595195ecec28e8d53189b`，源文件数
  `1001`，产物摘要见 `source-checkpoint-r1.json`；RAM 仍为 free `3092 B` / minimum
  `49152 B`，门禁失败，不放宽预算。
- P3 与前置拒绝：`p3-r1` 四板 OTA 成功；顺序复位后 `p3-r2` 使用同包和在线 build
  核验恢复实测。两份真实凭证均仅为 `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC` PASS，
  `strict_gates_passed=false`。粗 CLK ARM8、coded marker、SCK 候选覆盖不足、启动超时
  及 NO2 flight map ARM5 原件保留。`lifecycle-r1/r2` 在主机检查 SCK opposite-edge
  重装余量时拒绝，未进入自主试验。
- SCK 取证：单链路 ARM 回复截断、SD 下载非法 hex、原时限内未收齐整行分别保存。
  `sck-waveform-r2-tail/` 仅收集同一已保存 epoch `177` 的迟到回复，未重新捕获或改写
  原失败。七个存储页完整回复均超过原 `3 s` 期限，范围约 `3.03–3.25 s`；已收字节
  的最大相邻间隔约 `16 ms`，本记录不能证明 idle-gap 提前结束是根因。
  `sck-recovery-audit.json` 独立验证头部/CRC/训练身份，并恢复 residual `0`；该原件
  缺完整板端状态元组，未被补入合格矩阵。
- 校准恢复：持久连接补测 `sck-matrix-r3` 仍因 link0 候选覆盖失败；使用已有
  `--short-open` 的有界对照 `sck-short-open-r4` 共 `13` 次通过，三个 follower 都取得
  `0/1` 候选。与同源码、同 Calibration generation 的 DATA/residence 原件生成
  `matrix-r4.json`，`passed=true`、`diagnostic_continue=false`，选中 `[1,0,0,0]`，
  最小重装余量 `0` sample。未使用旧源码矩阵、手改候选或放宽重装预算；对照结果
  尚不足以证明持久连接导致采样偏置。
- 短帧闭环：`lifecycle-r3` 的 process-image → raw-flight → 恢复 process-image 最终
  均为 `passed/closed_loop_passed/realtime_gate_passed=true`、`diagnostic_continue=false`。
  raw-flight 首次 NO2 ARM8 拒绝，经 STOP 后下一次通过，原失败保留。四板 SD 原始
  segment 重建、CRC/sequence/owner 范围审计通过，采集窗口基础 transport 错误增量
  为零，最终 STOP 通过。证据见 `lifecycle-r3/validation.json`、`capture-audit.json`。
- 完整计时：`prepare-r1` 显式诊断配置通过；`trial-r1` 取得临时许可 epoch `76`，
  实读自主状态 `5`，普通模式取样后，在自主原始采集结束后继续取样。下表的每个
  peak 及其子项均属于某一次完整 phase；前后是同板、同 persona、同 clk_sys 和预算
  的独立有限窗口，不能把峰值降幅当作已证明的 WCET 上界。

  | 节点 | 普通完整 peak µs | 自主旧 peak µs | 自主新 peak µs | 自主新 RX capture µs / calls | 自主新 RX parse µs | 自主新 overlay µs |
  |---|---|---|---|---|---|---|
  | NO1 | 1346.896 | 1185.548 | 1233.220 | 43.392 / 1 | 545.972 | 0 |
  | NO2 | 2409.916 | 8537.728 | 3617.076 | 1944.840 / 1 | 192.600 | 1267.876 |
  | NO3 | 2696.252 | 9580.068 | 3702.320 | 1654.108 / 1 | 413.008 | 1261.336 |
  | NO4 | 2857.184 | 9691.712 | 3945.428 | 1932.944 / 1 | 468.568 | 1353.172 |

- 归因与限制：自主从站有限 peak 下降约 `57.6–61.4%`；NO1 增加约 `4.0%`，不宣称
  全节点一致加速。全部采样的 RX capture calls 不超过一次，完整 `SCHEDULE` 预算
  仍为 `380 µs`，全部节点仍失败；RX capture 和 overlay 准备仍是从站主要开销。
  自主计时窗口基础 transport 错误增量均为零，可丢弃观察副本的 drop 增量依次为
  `0/31921/35380/35278`，不代表逐圈记录无损。原始数据与逐节点 overrun、计数和
  对比范围见 `trial-r1/normal-audit.json`、`autonomous-audit.json` 及两份 comparison。
- 自主采集与停止：NO2–NO4 原始捕获通过；NO1 首次下载声明 `512 B`、实际 `487 B`，
  原 capture 和首次 audit 失败保留。撤销许可并确认四板 STOP 后，仅重读同一冻结
  捕获，原始 segment/CRC/sequence/owner 复核通过，见
  `trial-r1/recovered/capture1-node0/recovery.json`、`capture-audit-r2.json`。
  该恢复不改变原 trial 的非零退出码。许可撤销后 inactive，四板
  `armed/enabled/adapter_started=0`，无本轮 cleanup error。
- 证据已由 `hardware-review.json` / `slice-manifest.json` 封存。下一 gate 继续收敛
  RX capture、解析与 overlay 准备，按 owner/版本/静态资源边界推进异步准备与消费；
  完整 Core1 WCET、无更新稳态及真正 service blackout 均保持开放。
  不由局部工作量限制推导逐圈同步样本或列车调度完成；
  `TDMA-FLIGHT-002B` 保持 IN PROGRESS，`002F/002G/002H` 保持 PENDING，registry/C11 不变。

### TDMA-PROGRESS-20260912-014 - 列车调度分项计时与重复 RX 开销归因

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。以下数字为实验快照，非事实源。
- 变更：以 `4d8a715` 为基线，代码提交 `6284fb0`。为已有 Core1 TDMA phase 加入固定记录；使用 clk_sys
  TIMER1 原始拍数，保全最近完整 phase 与某一次最慢完整 phase 的嵌套步骤。Core0
  单次尝试读取，reset 请求由 Core1 下次 phase 消费。完整 `SCHEDULE` 仍包含计时
  开销；本切片没有改变发车、续转、CRC、所有权、PIO/DMA 资源或乘客服务策略。
- 软件与构建：计时器实际 C 回归及 nonblocking/NO5/analyzer/TRN03 相关测试
  `133 passed`；实际 C adapter host runner 通过。Release build `20260912041214`，
  源指纹 `a59eb65d3a99ffe43d256b6b371493285f9d88a3cb387f6c5b6adfe2aeaef764`，
  源文件数 `1001`；构建产物和 SHA-256 见 `source-checkpoint-r1.json`。
- P3：首次 NO4 OTA 失败，原件见 `p3-r1/` 与 `ota-r1-audit.json`；同包第二轮四板
  OTA 成功，`forced_continue=false`。`p3-receipt-r2.json` 仅为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC` PASS，`strict_gates_passed=false`；CLK、coded marker
  和 NO3 `arm_result=5` 均保留失败。独立 `lifecycle-r1` 再现 NO3 flight map 配置拒绝，
  尚未进入物理 ARM；停止态 configured/active 快照不足以确定 validate/lock/active
  中哪个分支拒绝。受控复位恢复后，`lifecycle-r2` 的 process-image、raw-flight、恢复
  process-image 三段 `passed/closed_loop_passed/realtime_gate_passed=true`，均未诊断
  继续；四板原始 SD 重建、CRC/sequence/owner 改写范围复核通过，最终 STOP 通过。
- 计时结果：普通严格配置 `trial-r1/normal-audit.json` 的各板最慢完整记录见下表。
  自主诊断配置 `trial-r2/autonomous-audit.json` 使用新的 reset epoch，在原始采集结束
  后持续取样；表中子项全部来自对应那一次最慢完整 phase，不拼接独立峰值。

  | 节点 | 普通完整记录 µs | 自主完整记录 µs | 自主 RX capture µs / calls | 自主 RX parse µs / calls | 自主 overlay prepare µs |
  |---|---|---|---|---|---|
  | NO1 | 1439.972 | 1185.548 | 43.836 / 1 | 507.684 / 1 | 0 |
  | NO2 | 2622.348 | 8537.728 | 5524.812 / 4 | 1499.672 / 4 | 1185.160 |
  | NO3 | 2962.880 | 9580.068 | 6928.504 / 4 | 1341.600 / 4 | 1055.320 |
  | NO4 | 3015.644 | 9691.712 | 7096.264 / 4 | 1212.648 / 4 | 1101.032 |

- 归因边界：自主从站最慢记录主要消耗在 `TDMA_PIO_SPI_RING_ADAPTER_RX_POLLS` 的重复
  捕获/扫描，其次是多次解析和本地 overlay 准备。父子计时包含式，不能相加；有限
  peak 不是已证明的 WCET 上界，也不能和另一时刻的 `SCHEDULE` 当作同一次原子采样。
  完整 phase 预算仍按实际 `SCHEDULE`，本轮为 `380 µs`，全部节点未通过。
- 临时许可与采集：首次 prepare 未启用诊断配置，许可命令被拒绝，见
  `trial-r1/grant-rejection.json`；保留原始 `<timeout>` 和执行错误，没有更改门禁。
  `prepare-r2` 显式诊断配置通过后，`trial-r2` 取得 epoch `58`，实读自主状态 `5`。
  NO1 首次 SD 下载出现 declared `512 B` / actual `504 B`，其余三板采集和原始 owner
  复核通过。停机后仅重读同一已导出捕获，原始失败与恢复审计分别留档，不能提升原
  capture 的 `passed`。自主计时窗口内基础 transport 错误增量见各板原始 audit。
- 失败/恢复：许可证已撤销。NO4 首次 STOP 返回超时，但同时刻最终快照四板均为
  `armed/enabled/adapter_started=0`；`stop-confirmation-r1` 再次取得四板 OK 与停止
  快照。复位曾有并行 USB 枚举失败，随后顺序重试成功，全部原日志保留。
  RAM gate 仍失败：free `3092 B`，minimum `49152 B`；不放宽阈值。
- 证据根：`out/HardwareAcceptance/20260912/tdma-flight-service-timing/`；源码、命令、
  P3、前置闭环、原始捕获、计时和失败通过 `hardware-review.json` / `slice-manifest.json`
  关联，官方 P3 凭证另由 staged gate 核验。主控复核不改变 registry/C11 状态。
- 下一 gate：先压缩单 phase 重复 RX 工作并优化有界扫描，再拆分仍超预算的解析/
  overlay 准备；每个实现切片重新 build/P3/短帧/原始波形和完整计时。随后完成真正
  service blackout、无更新稳态和 STOP 生命周期。`TDMA-FLIGHT-002B` 保持 IN PROGRESS，
  `002F/002G/002H` 保持 PENDING；本轮不宣称列车调度或乘客逐圈服务已完成。

### TDMA-PROGRESS-20260912-013 - 异步候车平台与四级服务设计核验

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B/002C/002F/002G/002H`，归属 `TDMA-FLIGHT-002`。
- 变更/提交：按用户要求先完成在制代码提交 `7bfbcdef6efb3d5869caf358986b39e58139b326`
  与文档提交 `252ea7bdaff8ab45629b6c06e83bbcdf40415499`。提交后工作区干净；真实源码
  指纹与本轮 P3 相符，代码 commit 通过非空暂存区硬件门禁。新建的
  `tdma-flight-rx-observation/commit-proof.json` 绑定两个提交、实际 hook 日志和提交态
  文档 hash；原 sealed manifest 的提交前文档快照保留。没有 push。
- 本次范围：读取现有代码与 HAOFV/状态机/VDC 契约，维护 TDMA 三件套中的待实施设计和
  退出门禁。未修改固件、PIO、工具或测试实现，未烧录或启动板卡；不引用历史 P3 作为
  新实现验收，也未改变 registry/C11 状态。
- 源码核验结果：
  - `tdma_flight_fifo_core0_publish_tx()` 已能发布固定池的完整版本，物理双计划也已有
    pending/selected 生命周期；follower 的 `prepare_process_overlay()` 仍受本次
    `rx_ok` 门控，违背方向独立推进的目标。
  - `TDMA_PIO_SPI_RING_ADAPTER_RX_POLLS` 会在每次 service 内重复运行 RX 观察与解析；
    单次 scanner 上界不等于整个 TDMA phase 的工作量上界。本次未进行逐 action 实测，
    不能把前轮超预算全部归因于该循环。
  - `tdma_process_image_layout.h` 已固定 timestamp trailer、VDC、RefMem、ACK/control
    和 optional diagnostic 区域；这只提供布局基础，尚不保证独立准备或逐圈卸载。
    当前 composite mailbox 由 `distributed_refmem_tdma_flight_sync_publish()` 统一
    生成，后续要隔离各域准备并保留唯一装配/CRC writer。
  - 自主交接的 `tdma_pio_spi_ring_origin.inc` 清除原正式 timestamp evidence，
    `tdma_pio_spi_phys_origin.inc` 的 RX observation 返回无正式时间戳；因此既有
    自主 persona 证据不能证明特等席每圈有效同步样本。现有协议使用
    `TDMA_PROCESS_IMAGE_DPLL_OBSERVATION_SEQUENCE_LAG`，不能把前圈样本冒充本圈 latch。
  - 普通 RX mirror 允许拥塞丢弃，不能用作特等席的无损接收队列；必须核算独立记录
    的最坏停顿容量、持续消费能力和资源准入，并保留 overflow/invalid 缺口。
  - `tdma_traffic_scheduler.c` 已区分使用控制封装的有时限 VDC 跟随命令；服务等级应
    按业务含义映射。RefMem ACK/fence、STOP 等生命周期入口也不能跟随普通日志降级。
- 方案结果：明确进站准备、完整 READY、硬件边界 LOAD/UNLOAD 与后台消费；特等逐圈
  同步样本、一等 VDC 跟随数据、二等 RefMem、无座普通控制与 Log 分别给出保留位置、
  版本、迟到/拥塞策略和证据要求。短帧微量 Log 需先定义静态预算和分片契约；既有
  LONG/maintenance 日志能力不能直接视作每圈短帧能力。架构正文记录设计边界，不冻结
  新 contract；整体目标及正式 Core1 WCET、RAM 和产品准入仍未通过。
- 执行顺序调整：用户随后明确“先解决列车调度，再解决乘客调度”。本次四级设计留档，
  `TDMA-FLIGHT-002F/002G/002H` 均为后续 `PENDING`；当前回到自主续转、无更新稳态、
  完整 Core1 WCET 和 STOP 边界。基础运输校验继续执行，特等正式同步能力不冒充已实现。
- 验证与证据：
  - 只读核验：`out/HardwareAcceptance/20260912/tdma-flight-async-platform/design-audit.json`，
    含源码 hash、真实符号/行号、基线提交、解释与下一 gate。
  - 文档门禁与提交凭据：同目录 `docs-*.log`、`doc-tests-r1.log`、`pre-commit-r1.log`、
    `p3-staged-r1.log`、`design-review.json`；最终结论以该复核文件和原始日志为准。
  - 项目文档门禁通过 Git Bash 执行；UTF-8 源码读取和只读指纹记录使用 PowerShell/
    原生 Python。按用户当前日期要求，所有新产物保存在 `20260912`，不改写旧目录。
- 下一 gate：先取得完整 owner phase 与各步骤的 clk_sys 归因，收敛重复轮询、重准备及
  软件发车依赖，完成正反例、fresh build/P3、短帧闭环、无更新自主循环与原始波形。
  列车调度及完整 Core1 时间门禁通过后，再推进异步乘客更新、特等硬件时间样本和
  四级隔离；不提前实现乘客优先级或提升产品同步、周期级飞行声明。

### TDMA-PROGRESS-20260912-012 - RX 观察副本生命周期、计数回归与有界扫描

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。本节数字为实验快照，非事实源。
  证据均位于本文开头的 RX 观察切片目录；前序原始失败保留。取证时为未提交工作树，
  当前实现已提交为 `7bfbcde`；提交时 pre-commit 核验当前源码的真实 P3 诊断凭证通过，
  不代表严格 P3、正式 RAM 或完整 Core1 WCET 通过。文档与实现分离提交。
- 旧 scanner 在 DMA 覆盖期间可发布混合副本；`scanner-before-r1.log` 的覆盖、完整计数
  周期歧义和扫描工作量反例保留。RX ring 只供可丢弃解析，不能用其重新定位已发布的
  wire slot。当前 scanner 限制每次候选数，完整复制后复验写入边界及 observation epoch。
- 初版 build `20260912025324` 四板 OTA 成功，但 `p3-r1/p0t-topology/summary.json`
  的拓扑失败：无真实 DMA 写入的板卡因软件坐标跳变虚增 16,777,216 字，形成错误的
  全连接邻接图。`counter-r1-source/` 保存该实现；`counter-before-r2.log` 用真实生产
  C scanner 复现长空闲间隔无数据仍增加公开计数。该失败不能归因于线路或另端单板合入。
- 修复将公开接收累计值与 observation epoch 分离。累计值只增加硬件余量的模差，
  歧义之后为下界；失效标记单独拒绝跨缺口副本。DMA 自重装周期由实际物理帧长度投影，
  同时保持 ring 和帧相位，仍由 TDMA owner 在 ARM 配置，不新增资源或逐帧续装动作。
  未锁定 alignment 在计数失连续时重新收集稳定样本；已发布 alignment 保持固定。
- `scanner-after-r4.log` 的 133 项相关 Python/C 回归通过，`sequence-r3.log` 的实际
  C counter/旧 sequence 单测通过；覆盖无数据长间隔、复制中覆盖、同余计数跨 epoch、
  硬件重装零边界、丢整周期后的帧相位及有界扫描。`scanner-after-r3.log` 的旧字符串
  fixture 断言失败保留，更新硬件装载绑定后通过。
- 当前 build-r2 `20260912031142`，源码指纹
  `0ff0c8b7d6309d98a8a6ec3c6b173e6b6c31741d6cec3055d209dcdc0b345bae`，997 文件；
  A/B/Boot、package 和 Flash 链接检查通过。`ram-r2.log` 余量 3,452 B，低于 49,152 B，
  正式 RAM 仍 FAIL。`p3-receipt-r2.json` 为 QUICK_DIAGNOSTIC、passed=true、
  strict_gates_passed=false；coded marker 失败、短帧 ARM result=8 与未交接运行环保留。
  `topology-audit-r2.json` 核验四条实际连接和八组未连接方向，后者接收字节增量均为零。
- `lifecycle-r1` 使用本轮 P3 的新矩阵，process-before、raw-transition、process-restored
  三阶段首次尝试均 passed/closed_loop_passed/realtime_gate_passed/diagnostic_passed=true，
  diagnostic_continue=false。集成采集因 NO3 StorageAO FILE_WRITE error=6 失败；其余
  三板原始采集通过且 transport 错误增量为零。NO3 保留原 capture tag/sequence，第一次
  SAVE 重试仍失败、第二次成功，`recovered/capture1-node2/recovery.json` 留下原始响应。
  `raw-audit.json` 核验四板共 32 段 SHA-256 与完整重建、有限帧配对、owner 改写边界和
  mailbox CRC；恢复原件不把原集成采集改写为通过。
- `baseline-schedule-r1.log` 的区间跨 STOP，adapter 计数已被清零，负 RX delta 不能解释
  为 transport 退步或连续性证据；NO3 失败采集没有运行中的 after snapshot，不补造该
  增量。scheduler 仍记录持续超预算，完整 Core1 WCET 不通过。
  `prepare-trial-r1` 已完成显式诊断短帧启动，不替代前述正常模式三阶段闭环。
- `trial-r1` 使用 360 s 有限许可证（trial=912012、epoch=66），实际进入 AUTONOMOUS、
  persona16；下载完成后继续观察至少 90 s。四板原始采集首次均通过；`raw-audit.json`
  核验另外 32 段及有限 owner/CRC 配对。`runtime-audit-r2.json` 的 14 组自主样本显示
  四板接收持续推进且 transport 错误增量均为零，三个 follower 均在配置/persona/armed
  保持不变时出现 self-trigger DMA count 重装。origin 使用不同计数模式，其普通 count
  增加不作自重装证据；首版 runtime 审计未区分模式的索引字段不用于该结论。
- 同区间 NO2/NO3/NO4 软件观察丢弃分别增加 432/27,449/28,460，拆拍扫描分别增加
  10/118/84；这是解析副本丢弃，不能改写为线路错误，也不能据此免除 Core1 预算。
  NO1 TDMA 最近样本范围 179.292–692.148 µs；NO2/NO3/NO4 样本最高分别
  3,528.620/6,657.104/4,489.756 µs，均有当前区间 overrun 增长，超过 380 µs 预算。
  完整 Core1 WCET 明确 FAIL，不能只报告自主线路连续。`CALibration:ORIGin:REVOKe`
  成功后发布 epoch=68、enabled=0；`final-stop/final-stopped.json` 确认四板停止。
- 下一切片优先核对 `tdma_pio_spi_ring_adapter_rx_poll()` 中
  `TDMA_PIO_SPI_RING_ADAPTER_RX_POLLS` 对 scanner 上界的乘算及整段解析/overlay 成本，
  在 owner service 边界收紧工作量；目前没有各内部步骤的实板耗时归因，不能把全部超时
  归因于 scanner。尚未注入 service blackout，未证明同圈多 owner 更新、最坏仲裁、
  故障恢复或长稳；历史自主 trial 的间歇 transport 错误根因也没有被本次有限窗口证明。
  registry/C11 状态不变，正式 RAM、严格 P3 与产品准入保持未通过。复核入口为当前
  切片的 `hardware-review.json` 和 `slice-manifest.json`。

### TDMA-PROGRESS-20260912-011 - 临时许可证接入、实际自主运行与失效闭环取证

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `d3e7bf2` 的未提交
  工作树。以下数字均为实验快照，非事实源；新证据根及旧来源见本文开头，复核入口为
  `hardware-review.json`。用户授权“调试阶段发临时许可证”，产品门禁没有豁免。
- Calibration 发布有限期、版本化的 `calibration_origin_timing_t`，绑定配置、校准/拓扑、
  model epoch、foundation/DeploymentGate、board/persona/resource 与时钟。唯一 TDMA
  owner 在已接受返回帧、旧 TX 完成且下一次 TX 决策前消费；每个准备步骤复验，FAULT、
  失效及到期通过 STOP 清理。已消费 epoch 跨 STOP 保留，清理失败不自动恢复旧发车器。
  SCPI 入口为 `CALibration:ORIGin:TRIAL`、`CALibration:ORIGin:REVOKe` 及两个只读查询。
- 软件：旧证据根 `admission-r3.log` 的 125 项 Python 回归通过；实际 C adapter、model、
  scheduler、RefMem、VDC 回归通过。覆盖 odd/stale/revoked/expired、配置/模型/资源变化、
  准备失效、FAULT STOP、清理失败与同 epoch 禁止重启。`admission-r1/r2.log` 的 fixture
  编译失败保留。最初全局 snapshot 一次尝试导致旧 SCPI 竞争超时，已拆分新 `try_get`
  有界接口与既有 Core0 重试接口，当前 P3 周期采样无查询错误；不能声称所有读取均有界。
- 当前 build-r2 为 `20260912015104`，源码指纹
  `23969c8f87eea46bc678fdc7d7dd03c5953ecd7dd49bdfde876c8882713da8cd`，995 文件；
  A/B/Boot、package 与 Flash 链接检查通过。`ram-r2.log` 余量 3,484 B，低于 49,152 B，
  正式 RAM FAIL。当前 P3 四板 OTA 通过，凭证为 QUICK_DIAGNOSTIC、passed=true、
  strict_gates_passed=false；粗 CLK ARM result=8、无安全 SCK 候选、启动超时均保留。
  前序 build-r1 已被当前 build 取代，不以其验收替代当前源码。
- 前置恢复：旧证据根 `sck-r4` 的 15 次原始采样形成 `matrix-r4.json`；该输入测于
  build-r1，当前 build 独立完成 `lifecycle-r3` 的 process-before、raw-transition-r2、
  process-restored-r2。三阶段 passed/closed_loop_passed/realtime_gate_passed/
  diagnostic_passed=true，diagnostic_continue=false；32 个 SD 段重建和 owner/CRC
  审计通过。早期 SCK 复用运行身份、参数范围拒绝、unsafe matrix、启动观察期限不足及
  ARM resume 竞争记录保留。`baseline-review.json` 保留该来源区别与完整 WCET FAIL。
- 首次自主试验：旧证据根 `trial-r1` 以显式调试参数 rearm=8192 ticks、abort=256 polls、
  duration=90 s 发布 epoch=96；这些参数不是实测产品授权值。实板进入 AUTONOMOUS、
  persona16、armed=1，返回 sequence 和完整 owner generation 持续推进；诊断准入有效，
  product_valid=0、reject_mask=7。`run_trial.py` exit=0 只表示观测到 persona，不代表
  capture 通过。NO1/NO2 下载截短，NO3/NO4 transport bad 分别增加 2/1，整次 capture FAIL。
- SD 原件恢复后，新证据根 `raw-audit-r1.json` 核验四板共 32 个分段 hash/完整重建，
  有限窗口内配对、节点改写边界与活动 mailbox CRC 通过；零相位采样也能分别解码完整
  CRC 有效包。旧 owner 审计因 integration FAIL 在分析前中止，原错误保留。
  成功窗口不能排除间歇线路错误，也不能消除原始 transport 错误增长或证明同圈多节点更新。
- 许可证失效实板证据：`expiry-r1` 发布 epoch=112，12 s 后自动停止；首次读到停止态距
  主机请求起点 12.078 s。`revoke-r1` 经正常 ARM 后发布新 epoch=128，再次进入自主态，
  主动撤销后 0.156 s 读到停止态。两次均继续观察至少 3 s 未重启，最终四板均为当前
  build、enabled/started/armed=0、config/applied 一致，origin DMA 无忙状态。以上包含
  串口查询延迟，不能当作固件 STOP WCET。到期保留 Calibration 发布记录，运行时已停止；
  主动撤销则更新 epoch 并清 enabled。同 epoch 消费负测仍以 host 为证，不能由新 epoch
  重入试验代替。两次前置短帧闭环通过，但启用了 diagnostic_continue。
- `scanner-probe-r1/report.json` 执行未修改的生产 scanner：DMA 在复制期间推进并覆盖
  16 个尚未读取字节，scanner 仍发布混合副本，且只读取一次 produced。该反例证明活动
  RX ring 复制存在竞态；没有证明它就是实板 transport 错误的根因。修复需同时约束扫描
  工作量、检测副本覆盖并保持 wire slot 所有权；单纯 modulo pointer 不足以证明任意
  停顿后的完整生命周期。本轮没有据此修改 scanner 固件或屏蔽 transport 错误。
- 完整 Core1 WCET 仍 FAIL：首次自主试验的 NO1 TDMA 最近执行样本约 398–562 µs，
  超过该配置 380 µs 预算，overrun 持续增长；历史最大值不能归因到某一个函数。
  下一 gate 为 RX 副本竞态/transport 错误定位与完整 WCET 收敛，然后执行真正 service
  blackout、同圈更新、拥塞和故障/长稳验收。正式 RAM、严格校准和 C11 仍开放，
  `TDMA-FLIGHT-002B` 保持 IN PROGRESS，registry 状态未提升，未暂存或提交。

### TDMA-PROGRESS-20260912-010 - owner intent 有界读取与窗口等待拆拍

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `d3e7bf2` 的未提交
  工作树；证据根 `out/HardwareAcceptance/20260911/tdma-flight-service-boundary/`。
  以下数字为实验快照，非事实源；前序证据由 origin status manifest 保留。
- 复核自主发车准入接口时，确认当前 `tdma_service_core1_service()` 的 intent 读取
  无限重试与计划窗口忙等。改为每拍一次完整读取，sequence/abort/payload 在同一
  guard 下验证后才发布 ARM 或 completion；读到写入态或版本变化则返回。常驻环路
  service 和 STOP 仍先推进。窗口未到时保留命令并发布 WAITING_FOR_WINDOW；后续拍
  已错过窗口则记录 WINDOW_MISSED，不靠忙等等待边沿，也不在过期后调用 adapter。
- `before-r1.log` 在实际旧 service 上复现五项失败：写入中断不返回、复制期间版本
  变化后错误完成、冻结时钟下的窗口等待。`pytest-r1.log` 119 项通过，最终新增
  resident/STOP 用例由 `pytest-r3.log` 验证，共六项针对性用例、120 项不重复 Python
  用例通过。`pytest-r2.log` 保留测试先 START 后 ARM 的前置条件失败，fixture 已按
  独立 service 边界应用 ARM 后修正。实际 C scheduler、VDC、RefMem 回归均通过。
- build-r1 `20260912005540` 的 A/B/Boot、package 与 Flash 链接检查通过；源码指纹为
  `18797259e0b0585939ee0537b9c73a7e3a266324be409398baf93905a7c1f019`，990 文件。
  `ram-r1.log` 余量仍为 3,900 B，低于 49,152 B，正式 RAM FAIL，无预算豁免。
- `p3-r1/` 四板 OTA 全部通过、无 forced continue；coded marker 校准通过。P3 凭证
  仍为 FOUR_NODE_TDMA_QUICK_DIAGNOSTIC、passed=true、strict_gates_passed=false：
  粗 CLK 阶段 NO1 ARM result=5 对应 FLIGHT_MAP_REJECTED；TDMA startup barrier 超时。
  首个样本各板 receive_rejected/rx_bitmap_incomplete 计数增长，随后只有一个稳定
  样本，未达到所需样本数。拒绝分类与样本事实不能当作硬件根因，原始失败均保留。
- `lifecycle-r1/` 的 process/raw/clock-restore 三阶段 passed/closed_loop_passed/
  realtime_gate_passed/diagnostic_passed 均为 true，diagnostic_continue=false。
  32 个原始 SD 段的 hash 与完整重建一致；包配对、follower owner 边界及活动
  mailbox CRC 通过。RX 增量 20,928/20,931/20,939/20,935，transport 错误无增长。
- `hardware-review.json` 保留完整 WCET FAIL：TDMA phase 历史最大值为
  2,711.088/2,599.416/3,194.852/2,916.120 µs，预算 380 µs；本窗口 overrun
  增加 45,325/24,874/30,530/27,042。最大值包含启动/P3，不能归因到本次修改的
  函数。final-stopped 核对四板当前 build、enabled/started/armed=0、config/applied
  一致；origin 历史 TX_BUSY=5 保留，follower 为零。
- 下一 gate 仍是 Calibration 版本化重装窗口授权、现有 capability/DeploymentGate
  投影及下一次旧 TX 决策前的真实 owner 交接。当前正常 ARM 仍使用旧 origin，尚无
  persona16、service blackout 或同圈更新证据。有界读取不替代多 writer 仲裁，其他
  result-frame 读取、raw scanner、adapter action 和兼容窗口时间表示仍须审查；
  不能把本切片写为完整 HAOFV/WCET 通过。不提交未启用的独立 builder，不提升契约状态。

### TDMA-PROGRESS-20260912-009 - 修复准备态累计事实回退

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `d3e7bf2` 的未提交
  工作树；证据根 `out/HardwareAcceptance/20260911/tdma-flight-origin-status/`。
  以下数字为实验快照，非事实源；前一指纹的成功与失败保留在 prepare manifest。
- `tdma_pio_spi_ring_adapter_publish_status()` 复用原有累计事实序列化，在
  PREPARING 提前返回前也执行，保留收发总数和最近序号；running 仍为零，旧绝对
  时间仍失效，不增加 RX/FIFO 操作，不嵌套或跨拍持有 snapshot writer guard。
  新回归先要求 bootstrap 确有非零累计事实，再验证 BUSY、DONE 和 poll 失败时
  对外事实不回退、时间资格仍无效；原始失败由 `counter-r1.log` 保留。
- `adapter-r1.log` 的实际 C adapter/engine/FIFO/FSM 回归通过；`pytest-r1.log`
  相关 114 项通过。build-r1 `20260912002825` 的 A/B/Boot、package 与 Flash 链接
  检查通过；源码指纹为
  `1d30d09afc70c0d667765817dcdd97d2cc1522ad3579211b630e7b5ea36583bb`。
  `ram-r1.log` 余量仍为 3,900 B，低于 49,152 B，正式 RAM FAIL。
- `p3-r1/` 四板 OTA 均通过、无 forced continue；TDMA process-image/FIFO 流程
  通过。凭证仍为 FOUR_NODE_TDMA_QUICK_DIAGNOSTIC、passed=true、
  strict_gates_passed=false：粗 CLK 阶段 NO2 ARM result=8 与 coded marker gate
  失败均保留，不能升级为产品或完整校准验收。
- `lifecycle-r1/` process/raw/clock-restore 的 passed/closed_loop_passed/
  realtime_gate_passed/diagnostic_passed 均为 true，diagnostic_continue=false。
  32 个原始 SD 段的 hash 与完整重建一致；包配对、follower owner 边界和活动
  mailbox CRC 全部通过。RX 增量 18,642/18,636/18,646/18,643，transport 错误
  无增长；局部独立窗口不证明全局同一圈更新。
- `hardware-review.json` 保留完整 WCET FAIL：TDMA phase 历史最大值为
  2,809.148/2,615.200/2,958.868/2,960.172 µs，预算 380 µs；本窗口 overrun
  增加 39,467/21,518/26,662/23,556。最大值包含启动/P3，不能单独归因到采集。
  final-stopped 验证四板当前 build、enabled/started/armed=0、config/applied
  一致；origin 的历史 TX_BUSY=5 保留，其他节点为零。
- 下一 gate：接入 Calibration 重装窗口与 profile/epoch 授权、已有 capability/
  DeploymentGate 的准入投影和真实 owner 调用；实际调用点必须在下一次旧 TX
  决策之前。`core1-review.txt` 另列 raw scanner 积压扫描与 intent seqlock 无界
  重试，不能根据 phase 最大值直接归因，后续仍须逐 action 的 clk_sys 实测。
  正常 ARM 仍使用旧 origin；host 准备态修复不等于硅上已进入该状态，也不证明
  自主循环、service blackout、拥塞或同圈更新。`TDMA-FLIGHT-002B` 保持 IN PROGRESS，
  不提交未启用的独立 builder，完整 WCET、RAM、硬件 mailbox 授权及绝对 VDC 时间开放。

### TDMA-PROGRESS-20260912-008 - origin 分步准备候选、当前源码回归与状态发布缺陷

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `d3e7bf2` 的未提交
  工作树，证据根为 `out/HardwareAcceptance/20260911/tdma-flight-origin-prepare/`。
  以下测试、build、时序和内存数字均为实验快照，非事实源；不改变 registry 状态。
- `tdma_origin_plan_begin/step/cancel()` 将两遍构图拆为固定块，BUSY 不发布入口，
  cancel/失败清除可安装入口。离线同步 wrapper 保留，物理准备改用分步接口。
  `builder-report.json` 对 1,016 个活动 mask/位置组合与独立保存的旧 builder 逐字节
  比较，执行图、常量和入口一致；每次构图 86 步，单步最多 23 个描述符，87 个取消
  边界通过。工作量有界不是 clk_sys WCET 证明。
- physical begin/poll 冻结配置和 seed；mailbox 逐个校验，完整 DMA STOP、persona、
  构图、seed、禁用态 SM 配置和安装分步推进。每步复核配置/时钟，安装前复核资源；
  失败保留清理责任。adapter 增加 `RESIDENT_PREPARING`，暂停旧发车和 FIFO 消费，
  DONE 当次不再收割。common STOP 同时覆盖不存在 loader/executor 的旧 capture/output，
  disarm 不再追加无界 abort。`owner-r3.log` 用实际 C preparation/disarm 和 mocked
  MMIO 验证 97 个 poll 边界的 STOP、失败清理保留与成功取消。
- `adapter-r1.log`、`comm-fsm-r1.log` 和 `pytest-r3.log` 通过，后者 143 项。
  `layout-report.json` 由当前 ARM 编译参数及 ELF 交叉核对：workspace 7,696 B、origin
  7,480 B；548 B builder 复用 940 B 未发布字段，新增 physical prepare state 为 96 B。
  build-r1 `20260912000246` 构建/Flash 链接通过，源码指纹为
  `b13f76a831fd0db122140482c829d303083db787d3fea5806b7ab65e59ac78ce`。
  `ram-r1.log` 正式 RAM FAIL：剩余 3,900 B，门禁 49,152 B。
- `p3-r1/` 的 NO2 OTA 明确返回 VERSION_REJECTED、仍运行旧 build；其他板升级成功。
  `no2-ota-repro-r1/` 对同一 package/参数做一次单板重现后升级成功、无 forced continue。
  `ota-rejection-review.json` 核对 package 最低 bootloader 与源码常量一致，但没有失败
  当时接收 header/内存约束的证据，根因仍未解释，禁止用重试成功覆盖原始拒绝。
- `p3-r2/` 重新执行四板真实 OTA，均通过、无 forced continue；P3 凭证为
  FOUR_NODE_TDMA_QUICK_DIAGNOSTIC，passed=true、strict_gates_passed=false。
  粗 CLK topology readback mismatch、coded marker gate 失败、TDMA startup barrier
  超时均保留。startup 在首次样本记录活动 mailbox 不完整/拒绝增长，后续只有一个
  稳定样本；不能把诊断流程退出成功写为严格环路通过。
- `lifecycle-r1/` 独立短帧检查在 NO4 ARM 以 RUNTIME_CONFIG_REJECTED 拒绝。
  原始 physical/runtime/FIFO 快照保留，随后四板 STOP 的 enabled/started/armed 为零、
  config/applied 一致；这些快照不能把拒绝归因到 PIO。后续同矩阵有界重现与原始
  波形复核以 `hardware-review.json` 为准，不覆盖此前失败。
- `lifecycle-r2/` 同矩阵的 process/raw/clock-restore 均通过，三阶段
  passed/closed_loop_passed/realtime_gate_passed/diagnostic_passed=true、
  diagnostic_continue=false。32 个 SD 原始段的 hash 与完整 capture 重建一致；包配对、
  follower owner 边界及活动 mailbox CRC 通过，不能外推为全局同一圈更新。
  RX 增量为 19,130/19,130/19,135/19,134，transport 错误无增长。TDMA 历史最大值为
  2,801.252/2,711.688/2,829.392/2,954.044 µs，对应预算 380 µs；当前窗口 overrun
  增加 40,888/22,706/27,939/24,721，完整 WCET FAIL。历史最大值包含启动/P3，不能
  单独归因到采集。final-stopped 验证四板当前 build、enabled/started/armed=0、
  config/applied 一致；origin 保留 TX_BUSY=5，follower 为零。
- 补充状态审计 `counter-r1.log` 复现未启用路径的真实缺陷：PREPARING 提前返回将
  对外累计收发计数及序号置零，ring runtime 会复制这些值；内部累计值仍在。
  `status-candidate-r1.log` 在 out/ 的候选副本上复用统一状态序列化后通过回归，尚未
  改变本次 P3 所绑定源码。该缺陷必须修复并重新绑定验收后才可接入实际启动。
- 下一 gate：先完成当前短帧/原始证据复核与状态发布修复，再接 Calibration 重装
  授权、capability/DeploymentGate 和真实 owner 调用。调用边界必须位于 adapter
  接受返回后、下一次旧 TX 决策前；不得直接嵌套已有 service 的 snapshot writer guard。
  `next-runtime-admission.txt` 记录具体落点。正常 ARM 仍使用旧 origin，未验证
  persona16、自主发车、service blackout、拥塞或同圈更新；完整 Core1 WCET、正式 RAM、
  硬件 mailbox 授权/参与证据和绝对 VDC 时间继续开放，不提交未启用的独立 builder。

### TDMA-PROGRESS-20260912-007 - origin 物理层与异步 adapter 候选集成、当前源码回归

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `d3e7bf2` 的未提交工作树，
  证据根为 `out/HardwareAcceptance/20260911/tdma-flight-origin-integration/`。
  以下 build、时序、资源和测试数字均为实验快照，非事实源；验收范围由
  `hardware-review-r4.json` 和原始报告限定，registry 状态不变。
- `tdma_origin_plan_build()`、`tdma_origin_exchange`、物理 startup、互斥 workspace
  与 persona 资源管理已编译进候选。origin 独占 executor/sniffer 经统一角色声明，
  STOP 保留完整 DMA 依赖树的 BUSY/ABORT 约束；不借 PIO0/DMA7。workspace ARM
  大小为 7,696 B，origin 使用 7,480 B；所有支持的节点位置已检查构图容量。
- runtime 绑定真实物理回调，adapter 新增自主态：仅已接受的 bootstrap boundary
  可以交接；失败保留清理责任直到 common STOP 成功。自主 service 每次最多收割一份
  RX 与尝试一份本地 shadow，硬件 pending 时保留后续 FIFO descriptor。返回包先核对
  所属 bank 的 sequence/identity/generation、完整本地 mailbox 和活动 mailbox 元数据/CRC，
  再进入 receive-health/FIFO；driver generation 与完整 owner generation/sequence
  分别关联。观察缺口只计为未观察周期，不伪造 timeout、SENT、ACK 或已完成窗口。
  旧绝对时间和 DPLL trailer 在交接时失效。正常 ARM 尚无 `start_origin` 调用者，
  本轮硅上回归仍走旧 origin，不能声明自主飞行或 service blackout 通过。
- 软件：`adapter-host-r2.log`、`origin-owner-r6.log`、`comm-fsm-r1.log` 通过；实际 C
  adapter/engine/FIFO/FSM 覆盖跳圈、非法配对、pending publication、失败 STOP 与重启拒绝，
  physical startup 使用 mocked MMIO。`pytest-r6.log` 为 143 通过。build-r4
  `20260911231115` 构建及 Flash 链接检查通过，源码指纹为
  `1f61640c5cc4babc42db526f62bf853c1332529a0453554f0356987a81879115`。
  `ram-r4.log` 正式 RAM FAIL：剩余 3,996 B，低于门禁 49,152 B；不作 RAM 豁免。
- `p3-r2/` 四板真实 OTA 成功，但拓扑阶段未检测到 NO4→NO1，P3 退出失败且未生成
  成功凭证。`failed-pair-r1/` 只做一次同参数邻接重现：接收 DMA 增加 44,850 words，
  两端原始 SD 时钟均有 2,392 个上升沿；它证明该次恢复了物理活动，未解释原始失败。
  两节点邻接 probe 没有完整 DATA feedback，通用 burst 的 passed=false 仍保留。
  位解析以 `pad-edge-review-r2.json` 为准；旧版误用 profile 顺序解释 levels 位，
  修正为 pin_base 连续位后复核，原错误报告保留。
- `p3-r3/` resume 复核当前 build 并复用 r2 的真实 OTA 记录，拓扑、CLK/marker、
  频率阶梯、TRN00/01/02、新 TRN03 矩阵与 TDMA 流程通过。
  `p3-receipt-r3.json` 为 FOUR_NODE_TDMA_QUICK_DIAGNOSTIC，passed=true、
  strict_gates_passed=true、diagnostic_failures=[]；不覆盖 NO5/DPLL、正式 RAM、完整
  Core1 WCET 或自主 origin。前序 build-r3 的校准/启动失败继续保留，不被此凭证覆盖。
- `lifecycle-r2/` 使用本轮 r3 矩阵，process→raw→带 clock training 的 process 三阶段
  passed/closed_loop_passed/realtime_gate_passed/diagnostic_passed 均为 true，
  diagnostic_continue=false。四板各自完整采集并导出；32 个原始 SD 段的 SHA-256
  均匹配，重新解码后与完整 capture JSON 逐值一致。`owner-audit.json` 的包配对、
  follower owner 边界与活动 mailbox CRC16 均通过。各板独立窗口不能证明全局同一圈更新。
- `schedule-audit.json` 中 RX 分别增加 17,627/17,629/17,632/17,620，transport 错误
  无增长。TDMA phase 历史最大值为 2,661.148/2,477.512/2,860.680/2,966.344 µs，
  预算 380 µs；本区间 overrun 分别增加 37,019/20,540/25,118/22,697，完整 WCET
  仍失败。最大值包含前序启动/P3，不能单独归因到 SD 采集。`final-stopped.json`
  验证四板均为当前 build、enabled/started/armed=0、config_seq=applied_seq；origin
  保留 physical TX_BUSY=5，其他节点为零，不把历史错误清除成无错结论。
- 下一 gate：`next-admission-review.txt` 明确 Calibration 窗口、profile/epoch、资源
  capability 与分步启动边界。当前矩阵的 guard_cycles=0 不是 origin DMA 重装授权；
  cadence 的 guard_floor_ticks 也不是总线仲裁上界。同步 startup 的构图/停止/切换
  尚无完整 Core1 WCET 证明，需先拆为有界 owner action，再接入实际调用并取证
  persona、自主运行、service blackout、迟到更新/拥塞和 fault/STOP/restart。
  `TDMA-FLIGHT-002B` 保持 IN PROGRESS，不提交未完成启用的独立 builder 切片。

### TDMA-PROGRESS-20260912-006 - STOP 依赖链、清理结果传播与停止态调度竞争

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `541b512`，代码提交
  `237bfba`，证据根为
  `out/HardwareAcceptance/20260911/tdma-flight-origin-stop/`。本项数字均为实验快照，
  非事实源。origin C builder/PIO 候选完整保存在 `pending-origin/components/tdma/`，
  尚未安装；本切片不取得 executor、sniffer、PIO0 或 DMA7，也不声称自主 origin 已完成。
- 物理 STOP 在暂停自有 SM 后按 loader 到下游 output/capture 停止，每层重新清 EN，
  检查自有 ABORT/BUSY 并共用既有超时预算。失败不清 FIFO/IRQ、不回收池/资源；
  persona release 同时检查 capture，即使不存在 loader 也不得提前放行。
  coded/MARK/DATA 训练入口和显式 STOP 经过同一停止边界。
- 初版 build `20260911203945` 的独立 process/raw 检查通过，训练后恢复在 NO2
  TRAIN512 超时；`after-training-failure.json` 与 `stop-retry-no2.txt` 保留上层已
  STOPPED、物理 armed 仍置位且 PERSONA_BUSY 的失败。根因是 void STOP 丢失底层
  拒绝，上层随后丢弃清理入口。修复将 physical/adapter STOP 改为 bool，runtime
  保留 context 和显式 pending 并由 owner 重试；停止配置 applied ACK、新 ARM、
  adapter 重绑定与 profile 元数据替换均不能越过未完成 STOP。部分 ARM 的残留图
  同样保留清理入口，配置序号回绕不替代 pending 状态。
- build `20260911205929` 的 `p3-r2/` 完成真实四板 OTA 和新矩阵 process-image/FIFO
  检查；粗 CLK 校准拓扑读回不一致，凭证仍为 QUICK_DIAGNOSTIC、strict=false。
  `lifecycle-r2/` 的 process 通过，raw ARM 在 NO3 以 result=8 拒绝；一次有界重试
  `lifecycle-r3/` 在 reference ARM 同样拒绝。该码属于 RefMem ARM runtime config，
  不能解释为物理错误码。NO3 配置序号推进两次，与 service 配置后 scheduler resume
  失败撤回分支一致；失败后 physical armed 为零且停止 ACK 完成。
- 审计发现已关闭的 traffic admission 仍使 Core1 每拍取队列锁、刷新周期并扫描空队列。
  `traffic-before.log` 的真实 C 负测复现停止态推进周期与争锁；修复以原子关闭提示
  提前返回，开放提示仍须取锁后复核，队列所有权不变。`traffic-after.log`、
  `service-r3.log` 通过。它消除停止态 consumer 的无效竞争，不承诺任意并发控制
  操作永不拒绝，也不替代后续完整 owner mailbox 治理。
- 软件：`pytest-r6.log` 为 153 通过，包含真实 STOP C 例程的晚写入、独立 ABORT/BUSY、
  共享超时与重试；runtime/adapter/service C 用例覆盖失败清理、ACK、重绑定与回绕。
  `pytest-r1-setup.txt` 保留目录准备失败，`pytest-r4.log` 保留签名断言未同步的失败。
  build-r3 `20260911211655` 编译及 Flash 链接检查通过；`ram-r3.log` 正式 RAM 仍
  FAIL，剩余 2,660 B，低于正式门禁 49,152 B。
- 当前源码 `p3-r3/` 完成真实四板 OTA/校准/环路，凭证绑定源码指纹
  `bf3ea067356e6213204cf5b45ad6d4e7d3118ca3b2f414d3df26ccf62e3c0e28`。
  校准阶段均通过，启动屏障在两秒内仅取得所需两次中的一次稳定采样，故 strict=false；
  首次采样保留初始 receive/bitmap 拒绝增长，第二次及后续 soak 通过。该失败不能
  隐去，也不能据此直接归因为矩阵错误。QUICK_DIAGNOSTIC 流程完成不代表产品验收。
- `lifecycle-r4/` 使用明确记录的前序实测对照矩阵与既有四秒启动窗口，process→raw→
  带 clock training 的 process 三阶段均通过，closed-loop/realtime 门禁通过且
  diagnostic_continue=false；本轮无 ARM 拒绝。四板各自有限采集的 SD 原始段可完整
  重建 JSON；`owner-audit.json` 接受每板同钟入/出包、逐 follower 字节所有权、头部
  完整性和活动 mailbox CRC16。对照矩阵不是本轮校准证明，各板局部包对也不是全局
  同一圈更新或 service blackout 证明。
- `schedule-audit.json` 中各板采集/导出期间增加约 1.94 万接收帧，transport 错误无
  增长，但 TDMA phase 历史最大耗时约 2.396–2.930 ms，超过 380 µs 预算，超限计数
  继续增长；历史最大值包含前序 P3/启动，不能归因为纯采集开销。完整 Core1 WCET
  仍失败。`final-stopped.json` 验证四板均为当前 build、physical armed/runtime started
  为零，STOP 配置等于 applied，analyzer RELEASED、SD job DONE。
- 下一 gate：恢复完整 origin builder、资源准入、异步 completion 与安全内存生命周期
  集成，再执行新的当前源码 P3/原始波形与真实 service blackout。正式 RAM、严格 P3
  启动门禁、完整 WCET 与 C11 仍开放，`TDMA-FLIGHT-002B` 保持 IN PROGRESS，registry 不变。

### TDMA-PROGRESS-20260912-005 - origin 完整执行图、交接竞态与固定窗口候选

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `e87eea4`，证据根为
  `out/HardwareAcceptance/20260911/tdma-flight-origin-frame/`，入口为
  `frame-manifest.json`。本项数字均为离线实验快照，非事实源。生产源码未改，未执行
  硬件查询、OTA、ARM 或新的 P3；既有单板合入与上一固件验收来源保持独立。
- `candidate.pio` 与真实既有 DATA/RTT/latch 程序联合汇编，两个 TDMA PIO 均恰好
  占满 32 words。helper PACK/COMPARE 无 GPIO，候选独立 DMA8 执行器处理本地
  CONTROL 边界，DMA6 装载其 AL3，DMA4/5 分别捕获/输出；DMA8 与全局 sniffer
  尚未声明或获得资源准入。模型通过 19,968 个 helper、876 个 capture、27 个
  截断输入和 13 个重定位串接用例；不占用 PIO0 或 DMA7。
- `origin_graph.py` 生成完整有限图，修复常量零与未解析分支地址共用存储的错误。
  字宽捕获候选含 226 个 descriptor、77 个 literal，逻辑占用 3,924 B，fixture
  预留 4,608 B；这不是固件 RAM 准入。`check_graph.py` 执行 AL3、拆分的 DMA
  读写事件、有限 FIFO、真实 helper 指令、sniffer 和子通道中止/重装，输出与本仓库
  编译的 transport C oracle 一致。257 帧涵盖缺包、半包、坏 identity/transport、
  错 hop、旧 sequence 与错误 profile；四种中止不收敛/总线错误进入稳定 FAULT，
  保留资源。本地边界可在 DATA 停顿时继续；抽象步数不能换算成硬件周期上界。
- `check_graph_races.py` 覆盖最后一次捕获写入晚于剩余量快照的 16 种交错；即使
  后续写入完成，已记录的半包仍不被接纳。128 圈并发发布完成 128 次旧 shadow 池
  回收；RX 版本检查接受 68 份一致副本、丢弃 62 份跨复用观察。八种描述符变异均
  被所有权检查或 C oracle 检出。另保留未等替代 generation 就复用池导致混合 mailbox、
  以及无界挂起 reader 遭版本回绕 ABA 的反例；生产内存屏障、读取上界和 STOP/debug
  epoch 仍待实现，不能仅用 seqlock 版本相等宣称跨任意停顿安全。
- 窄访问候选：RX FIFO 低字节直接进入紧凑银行，省去 PACK；TX 使用 halfword
  高字节和 DMA stride。真实 DATA 指令的 36 组表示对照与 65,536 组 padding 检查
  通过。紧凑图加入四个显式活动槽的 CRC16 后为 224 个 descriptor、87 个 literal，
  逻辑占用 3,932 B；与真实 mailbox CRC16/transport C 一致，接受 32 个有效返回，
  拒绝覆盖各活动 mailbox 全部字节的 128 个损坏用例。字宽/紧凑路径在 17 帧中一致；
  八活动槽超出此 fixture 的 descriptor 上界而被拒绝，不作为新的产品容量限制。
  窄 FIFO 读写、CRC16 寄存器高位及真实总线行为仍需硅上验证；CRC 正确的错误
  source/target/class、逐圈参与与 V2 尚未闭合，DPLL trailer 当前明确无效。
- 节拍缺口：当前 AL3 图在校验分支结束后立即发车，不能证明固定帧间隔。
  `guard_candidate.pio` 将 bit count 与准备窗口 count 放入启动字，在 CS 撤销后
  先发布边界 token，再等待固定 PIO 窗口；替代 CONTROL/capture 各 10 words，
  PIO1 联合布局仍为 32 words。16 组窗口/长度与 144 组正 prefix 指令测试通过；
  迟到供给确实延长节拍的反例也保留。该窗口未与 AL3 图合并，须由 Calibration
  发布预算并完成供给最坏上界及有界迟到故障策略，不能只加等待后宣布 F2/F3 通过。
- 下一切片按 `working-design.txt` 集成完整 C builder、地址/PC 白名单、DMA8/sniffer
  仲裁、启动 alignment、异步 adapter completion 和 owner 生命周期，先审计所有
  calibration/RX/TX/follower workspace 的互斥性，再考虑复用内存。固件实现后执行
  软件/构建/当前源码 P3/四板原始波形闭环。上一 strict=false、正式 RAM 和完整
  Core1 WCET 失败仍开放；真实 service blackout、DPLL latch 与 C11 尚未完成。
  本项执行离线回归及文档门禁，`TDMA-FLIGHT-002B` 保持 `IN PROGRESS`，registry 不变。

### TDMA-PROGRESS-20260912-004 - reference 头部计算候选与返回映像交接审计

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B`。基于 `dd5d8c0`，证据根为
  `out/HardwareAcceptance/20260911/tdma-flight-origin-audit/`。本项数字均为离线实验
  或历史快照，非事实源。仅审计并更新文档，产品源码未改，未执行硬件查询、OTA 或 ARM。
- 当前依赖：reference 每圈仍由 Core1 构建 TX words、准备返回映像、更新 sequence/CRC
  并向 CONTROL FIFO 写入发车字；DATA 长度供给、TX completion 收割、clock-latch
  重装和 RTT seed 也不能直接作为 Core1 缺席下的持续路径。真实 pioasm 汇编得到
  origin 当前 PIO1 程序合计 25 words、PIO2 合计 12 words；PIO1 的四个 SM 均已有
  owner。空余指令不等于空余 SM，更不证明新增握手程序已经通过准入。
- `origin_header_model.py` 使用 SDK 的 RP2350 寄存器值编码候选 AL3 描述符，并以
  本仓库实际编译的 `tdma_transport_frame.c` 为独立 oracle。SUM 执行 sequence 加一，
  CRC32R 与输出反转/取反生成 identity 和 transport CRC；identity 排除 hop_count，
  transport CRC 字段按零输入。21,280 个用例及同图连续 2,048 次跨回绕一致，公开
  CRC check vector 与错误链、越权地址、未对齐、无界 count、DREQ 和 CRC 覆盖负测
  通过。计算图只写 reference header 指定字段，payload 保持不变；这不是返回包验证。
- 候选子图含 15 个 descriptor、逻辑工作存储 300 B，每次 66 个 output transfer
  和 60 个 loader word transfer，见 `header-model-report.json`。这些是描述符模型
  的事务数与存储量，不是 DMA 周期上界、固件 RAM 余量或完整 origin 资源预算。
  该图没有接入固件，末尾的模型重触发也不是可直接装板的物理发车程序。
- `return_ring_audit.py` 按当前 RX ring 符号和上一切片 physical frame 快照复现四类
  反例：固定地址读到上一旧帧、回绕后混帧、reader 落后遭覆盖、未完成帧发布混入旧尾部。
  在历史 301 words/frame 和当前 1,024 words/ring 下，frame start 遍历全部 ring
  位置才重复。另有 308,224 个起点/word 位置检查证明：若硬件另行保证完整固定帧
  cursor 及最多一个后继帧写入，则上一帧与下一帧不重叠。该有条件的内存结论不包含
  cursor 实现、真实 bit alignment、缺失/额外 clock 或截断返回的恢复证明。
- HAOFV 准入缺口：DMA6 command contract 目前仅适用于 process follower，资源仲裁
  尚未声明全局 sniffer；二者必须先显式声明及冲突验证。下一实现先完成硬件帧位置与
  返回映像交接，再联合证明 CONTROL/DATA/capture 边界、硬件完成及 sequence 关联。
  DMA selection 仍不能当作 wire completion、ACK/fence 或 DPLL 时间。完整方案和
  下一切片退出条件见 `design-audit.txt`。
- 验证范围：本项执行离线模型、C oracle 编译、现有 PIO 汇编和文档门禁；未产生新的
  P3 固件验收或多板波形。上一切片的 strict=false、正式 RAM 与完整 Core1 WCET
  失败继续保留。`TDMA-FLIGHT-002B` 保持 `IN PROGRESS`，registry 状态不变。

### TDMA-PROGRESS-20260912-003 - follower 自主续转、池交接与启动对齐修复

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B/006/007`。本项数字均为实验
  快照，非事实源。基于 `9b8d5f7`，证据根为
  `out/HardwareAcceptance/20260911/tdma-flight-follower-recurrence/`。
  最终 build `20260911182033`，源码指纹
  `da774652f2a50cb812c32efc57d8afba4b515458fc787447ca54474744112bd4`，共 979 个源文件。
- 固件：既有 DMA output/loader 通过 selection、数据段和 restart 控制段持续执行。
  Core1 仅初始化 loader，后续完整构建 inactive plan 后发布 SRAM successor 指针；
  单 pending publication 被 DMA 选择后才回收旧池。未就绪不 acquire TX，无新版本
  不重建计划，失败不更新接受版本。资源仍由 TDMA owner/Resource Arbiter 管理；
  PIO 指令、SM、RX FIFO owner 不增加。selected generation 可能领先物理 CS，
  只作内存交接证据，不是 SENT、wire completion、RefMem ACK/fence 或 DPLL 时间。
- 首轮真实失败：build `20260911180139` 的 P3 和独立复验均保留。既有校准矩阵下
  三个 follower 接收连续，但 reference 持续报 `BAD_ROUTE`。`round1-node*/` 的 SD
  原始采集及 `return-failure-decode.json` 证明 NO2 输入头 CRC 正确，输出将
  `hop_limit` 从 3 改为 131，CRC 也错误。`diagnostic-running.json` 显示首帧 bit
  shift 为 1，随后 10293 个包为 zero shift；第一次修改计划过早锁定了启动瞬态。
  `diagnose-return/` 明确使用 diagnostic continue 取证，未计为通过。
- 修复：ARM 后 PASS 由硬件重复执行；相邻完整 physical frame 的 byte/bit alignment
  连续一致，达到 `TDMA_PIO_SPI_OVERLAY_ALIGNMENT_STABLE_FRAMES` 后才允许首份
  修改计划。之后锁定该 ARM epoch 的槽位，解析副本丢失不得再次移动线上位置。
  真实 C scanner 回归覆盖首次 `1 → 0 → 0`、新 epoch 与锁定后的副本丢失；
  当前波形确认 NO2 正确推进 hop 且不再改动 hop_limit。持续异常初始流和真实时钟
  丢失的恢复证明仍开放，稳定样本门槛不能替代全部环境故障验证。
- 软件：bit/CRC/资源/字段解析 79 项通过；DMA/scanner/TRN-03 最终 112 项通过。
  实际 DMA 绑定函数的 host bus 验证连续 200 份计划、48 个发布时间点、池回收、
  generation 回绕、段间 idle、迟到 trigger、STOP 超时与重试；C adapter 覆盖无更新、
  busy 不取走 TX、失败后重试及无 TX bootstrap。C owner/alignment/value 163840 组
  通过，ResourceArbiter 负测通过。首轮 fixture 误提取前置声明和旧 one-shot/PASS
  静态断言失败均保留，已同步修正。命令及日志见 `software-verification.json`。
- 构建与 P3：Release 与 A/B/Boot Flash gate 通过。`ram-release-r2.log` 的 free
  为 2668 B，正式阈值为 49152 B，正式 RAM 门禁失败。两轮都执行真实 P3 `run`；
  最终 `p3-r2/receipt.json` 仍为 quick diagnostic，`strict_gates_passed=false`。
  coarse ARM/map、coded marker、SCK training/replay 和默认 2 秒 startup 屏障失败
  原样保留；runtime 稳态各 Node 与最后 handoff 通过不覆盖这些失败。
- 独立闭环：新矩阵的 SCK 行重装余量为负，独立工具在硬件动作前拒绝，日志为
  `controlled-fresh-matrix.log`。`controlled-prior-matrix-fixed/` 在当前新固件上使用
  前序 live-CRC 切片中满足重装预算的既有矩阵，通过 passed/closed_loop_passed/
  realtime_gate_passed，diagnostic_continue=false；沿用受控 4 秒 startup 窗口。
  既有矩阵本身有 diagnostic provenance，不能称为本轮新校准或严格产品验收。
- 沿途复核：`round2-owner-audit.json` 绑定四板完整同钟 packet 对及 SD 原始页，
  三个 follower 均无越权 byte，全部 active mailbox CRC16 有效；`round2-byte-phases.json`
  的有限窗口各 bit 位置输出 phase 均为 88 ns，采样量化为 8 ns，不是 DMA/环境最坏证明。
  采集继续使用既有 1024 B 下载页参数。`handoff-audit.json` 中发布和选择 generation
  持续推进，无新增准备失败；分次 SCPI 读数及 sticky IRQ3 不能换算成精确圈数或
  无 Core1 service 的证明。
- 完整调度：`schedule-audit.json` 区间每板接收约 28800 帧，基础 transport 错误
  增量为零；TDMA overrun/deadline 仍增长，累计 max 为约 2408–3328 us，WCET
  为 380 us。累计峰值包含启动事件，不把差异归因于单操作，也不据短帧工具通过
  宣称 HAOFV 完整 WCET 已满足。
- 生命周期：`raw-transition/` 与 `restored-process-training/` 在同一既有矩阵和当前
  build 下均通过非强制闭环；后者覆盖 clock training 后恢复 process。最终四板均
  STOPPED、analyzer RELEASED、SD DONE，见 `final-stopped.json`。成功与首轮
  失败、旧/新矩阵来源及最终源码指纹由 `slice-manifest.json` 分别绑定。
- 后续边界：reference 仍由 Core1 逐帧准备并写 CONTROL FIFO；自主 sequence/identity
  CRC、返回映像保留和硬件发车是下一独立切片。当前 LOAD MASK 不能屏蔽 mandatory
  TDMA service；未执行完整 Core1 blackout。正式 RAM、完整 WCET、严格校准、尾部
  完整性、最坏 DMA/环境和 HAOFV-879/C11 继续开放，登记状态不变。

### TDMA-PROGRESS-20260912-002 - 可跨 sequence 复用的 follower header 计划

- 日期：2026-09-12；TODO task ID：`TDMA-FLIGHT-002B/006/007`。本项数字均为实验
  快照，非事实源。代码提交 `7259cb4`，基于 `2f6efd5` 并保留已有单板合入；证据根为
  `out/HardwareAcceptance/20260911/tdma-flight-live-crc/`，入口 `slice-manifest.json`。
  build `20260911172347`，源码指纹
  `fca9003c2f528f9a5916e56622887edb38bd687c21b73ae69c9063b78022514c`，共 979 个源文件。
- 固件：header 的 hop/CRC 由绝对值 REPLACE 改为对实际在途 bit 应用 XOR 差分，
  消除计划对预测 sequence CRC 的依赖；本地 mailbox 仍使用 owner 准备的值。
  process follower 将 ISR 改为右移，内部 LIVE/INVERT 分别选择当前 bit/反相 bit，
  DATA 程序仍为 28 条，PIO2 总预算仍为 32 条，完整 byte 时序预算不变。adapter
  仅对该 persona 的 RX observation copy 恢复 bit 顺序，raw/reference 表示保持原语义。
  header mask 也不能扩大到 sequence 等未授权字段。
- 软件：当前真实 C plan、transport encode/advance、pioasm 指令与实际 adapter RX
  归一化函数联合测试 44 项通过，DMA/资源套件 30 项通过，PIO patch 测试 9 项通过。
  C runner 保持 163840 组 owner/alignment/value 组合通过。固定计划覆盖全部合法 hop、
  sequence 基向量与回绕；所有 header 单 bit 错误保留原 CRC syndrome，旧绝对 CRC
  重放为负例。首次 DMA 测试因新增 transport helper 链接依赖缺失而失败，已同步修正
  Python fixture 与 C runner；原失败日志 `model-tests-r1.log` 保留。模型仍不证明 DMA
  仲裁/断粮最坏情况，也不证明硬件自主重复执行。
- 构建与资源：Release build、A/B/boot Flash gate 通过；双 plan 池和静态 RAM 占用
  未增加。`ram-release-r2.log` 中 free 2788 B，正式阈值 49152 B，正式 RAM 门禁仍失败。
- P3：真实 `run` 完成四板 OTA、校准和 quick diagnostic 流程，归档凭证为
  `p3-r1/receipt.json`，`strict_gates_passed=false`。coarse CLK 阶段 NO4 ARM/status
  查询超时，以及默认 2 秒 startup barrier 只取得一个稳定样本的失败均保留，尚未证明
  ARM 超时根因。不得把 quick diagnostic receipt 的 passed 解释为产品验收通过。
- 独立复验：`controlled-fresh-matrix/` 使用本轮生成的 replay matrix，在既有受控
  4 秒启动窗口下通过 passed/closed_loop_passed/realtime_gate_passed，
  diagnostic_continue=false；矩阵自身保留 diagnostic provenance。startup 首窗错误
  仍在原始记录，稳定屏障后才统计无新增基础错误。这不修复或覆盖 P3 默认窗口的失败。
- 沿途复核：`round1-owner-audit.json` 绑定四板原始 SD capture，每块板均找到完整同钟
  packet 对，三个 follower 窗口均无越权 byte 改动，全部 active mailbox CRC16 有效。
  `round1-byte-phases.json` 的各 physical bit 位置均观测到 RX SCK 上升后 88 ns 输出，
  采样量化为 8 ns；仅是当前有限窗口，不是同步器/DMA/环境最坏延迟证明。采集使用已有
  1024 B 下载页参数，四板导出通过，不声称修复前序大页串口截断问题。
- 完整调度：`schedule-audit.json` 的区间内各板 RX 增量约 23000，基础 transport 错误
  增量为零，但 TDMA overrun/deadline 继续增长；累计 max runtime 为约 2387–2930 us，
  phase WCET 为 380 us。累计最大值含先前启动事件，不把差异归因到某一个操作。
  `full_haofv_wcet_accepted=false`，独立短帧工具的 realtime gate 不能覆盖这个失败。
- 生命周期：`raw-transition/` 与 `restored-process-training/` 在本轮矩阵下均通过非强制
  闭环，后者覆盖维护 clock training 后恢复 process。初次 raw 命令误带仅适用于 process
  的参数，被主机解析器拒绝；纠正后执行，原日志 `raw-transition.log` 保留。最终四板
  build 一致、STOPPED、capture RELEASED、SD DONE，见 `final-stopped.json`。
- 下一 gate：推进 follower descriptor recurrence 和不可撕裂的 generation 发布。
  reference 的 CONTROL FIFO 发车、返回映像保留、sequence/identity CRC 更新
  是另一独立门禁；不得以旧帧固定重放代替持续循环。未冻结的后续设计、资源风险和
  DMA 预取选择与 wire completion 的区别保存在 `next-slice-notes.txt`。
  当前仍由 Core1 逐帧供给；B1、自主循环、完整 WCET 与长期目标均未完成，登记状态不变。

### TDMA-PROGRESS-20260912-001 - bit 沿途更新与稀疏 DMA 集成验证

- TODO task ID：`TDMA-FLIGHT-006/007`。本项所有 build、数量、容量和时序数字均为实验
  快照，非事实源；证据根为 `out/HardwareAcceptance/20260911/tdma-flight-owner-bit/`，
  入口 `slice-manifest.json`；软件初检另见同日 `tdma-flight-owner-bit-plan/`。保留另一设备的 UI/单板提交
  `d5ed7bc`，在 HEAD `708e59b` 上集成；build `20260911164308` 对应源码指纹
  `e55b1f78fddf0662af69ef373c15f80d4a495dc6f4a65259d7a67d242213b230`，共 979 个源文件。
- 固件：process follower 改用内部 LIVE/ZERO/ONE token，header 与 local mailbox 为
  稀疏存储窗口，窗口本身不授予修改权。头字段仅允许 hop/transport CRC，其他 slot、
  DPLL trailer 和越界 bitmap 被拒绝。两个版本的 plan 包含各自描述符和 token；最终
  descriptor 自 chain 以终止装载，段间 output 空闲不能触发池复用。loader 属于已有
  DMA_FORWARD 仲裁资源，独立 SDK claim，STOP/回滚/维护切换停止加载与输出，超时保留
  资源。内部 ARM 初始化 ISR 并 prime OSR，跨帧保留 ISR，RX FIFO 副本丢弃不阻塞流水。
- 软件：C 单测通过 163840 组所有权/对齐/数值组合，另验证越权 header/slot/trailer、
  force 等值更新、非法 token/terminal/descriptor/tail。新测试用实际 C plan 和 pioasm
  指令字解释双帧，覆盖装载偏移、RX 丢弃和完整 byte 时序正/负测。模型套件 73 项通过，
  后续 DMA/资源/TRN 套件 159 项通过；DMA host bus 验证 AL3 段间空闲、终段禁止越界
  chain、未 claim/损坏 plan 拒绝，以及加载写竞争、停止超时与重试。模型不包含真实
  DMA 仲裁延迟。相关 C transport/resource tests 与 warnings-as-errors 构建通过。
- 资源：实际 DATA 程序 28 条，配合原 clock-latch 4 条占满 PIO2；plan 双池 2296 B，
  SRAM LIVE 常量另计。最终 linker free 2788 B，低于 release 要求 49152 B，正式 RAM
  门禁失败保留，不能用稀疏池装得下代替产品余量通过。
- P3：`p3-r1/receipt.json` 是本轮真实 `run` 的归档副本，quick diagnostic 流程完成，
  `strict_gates_passed=false`。coded marker/TRN-01 失败、新 SCK 行 re-arm 余量为负、
  NO4 ARM result 8 与无 running handoff 均保留。对新矩阵执行非强制复验又被主机 SCK
  门禁拒绝，见 `controlled-r1.log`，没有修改矩阵数值来放行，也不将 ARM result 8 的
  根因等同于该 SCK 失败。
- 受控闭环：使用前序已验证的合法 baseline matrix、当前新 build 运行
  `controlled-baseline-matrix/`，passed/closed_loop_passed/realtime_gate_passed 均为 true，
  diagnostic_continue=false。该矩阵复用只用于相同夹具/profile 的固件对照，不代表新
  calibration gate 通过；startup 首窗出现填充期拒绝，稳定屏障之后未新增基础错误。
- 生命周期：`raw-transition/` 证明新 process persona 停止并释放 loader 后可进入 raw；
  `restored-process-training/` 再恢复 process、执行可选 coarse clock training 的维护切换
  并重新启动短帧，二者均在非强制模式通过。最终四板 STOPPED、capture RELEASED、
  SD job DONE 的读回见 `final-stopped.json`；该终态绑定本轮 build。
- 沿途证据：`round1-owner-audit.json` 包含三个 follower 窗口，`round2-owner-audit.json`
  包含四板完整包。每个 follower 的输出只改变授权 header/local mailbox，所有 active
  mailbox CRC16 均有效，DPLL trailer 逐 byte 保持。相同审计在旧 R3 窗口得到
  `follower_owner_boundaries_accepted=false` 和 CRC 失败，见 `old-r3-negative-control.json`。
  每个窗口均由 SD bin 重建，并以完整 transport header CRC、sequence/hop 和同钟
  RX/TX 重叠配对；FLIGHT_MUTABLE 的 transport CRC 不覆盖整个 payload。
- 时序：两个 `round*-byte-phases.json` 按实际 RX SCK 上升沿及 physical byte 位置分组。
  第二轮全部观测位置为 88 ns，第一轮有一个位置出现 96 ns；原 byte 首 bit 的系统性
  延后未在本次窗口复现。8 ns 采样量化、有限窗口、同步器绝对偏移和 DMA 压力限制均
  保留；不把观测 phase 写成新的产品常数或最坏延迟证书。
- 下载失败：两轮 NO1 的默认大页读取均返回截断 hex，采集/SD 写入已经完成。原失败
  summary 保持原样，`recovered-round2-node0/` 通过现有小页接口重新读取相同 epoch，
  校验每段 tag/build/CRC 并恢复完整 capture；没有重采或伪造缺失字节。该问题仍属待
  归因的主机/串口导出限制。
- 完整 WCET：`schedule-audit.json` 区间内各节点 RX 增量约 63000，基础 transport 错误
  增量为零，但 TDMA phase overrun/deadline 计数持续增长；累计 max runtime 约
  1768–2584 us，合同 WCET 为 380 us。独立短帧工具的 realtime gate 不覆盖完整 TDMA
  WCET，不能据其通过声称消除了 Core1 的逐帧供给成本。本切片尚无自主发车/续装和
  generation boundary handoff；B1/B2/B3、完整 WCET 与严格资源/校准仍待完成，契约
  登记状态不变，长期目标继续 active。

### TDMA-PROGRESS-20260911-007 - bit 所有权保护候选与完整指令/存储预算审计

- TODO task ID：`TDMA-FLIGHT-006/007`。以下数字均为离线候选快照，非事实源；本轮未改
  固件、正式工具或正式测试，未执行新 build/OTA 或板端重读。硬件状态只引用前项采集
  结束时的快照，不代表共享工作区后续任务结束后的状态。
  证据根为 `out/HardwareAcceptance/20260911/tdma-flight-owner-bit-plan/`，入口
  `candidate-review.json` 与 `workspace-addendum.json`。原 B0 与 owner 失败证据已通过
  文档提交 `288629c` 单独归档。期间另有 UI/单板验收提交 `d5ed7bc` 进入 HEAD；本候选
  使用的 TDMA 源码锚点未变，但 R3 全局源码指纹和 RAM map 不能代表该最新 HEAD。
- 机制：候选命令只允许选择实际在途 bit、写本地零/一、或在末尾保留位跳入固定 frame
  boundary；不允许外部输入任意 PIO 指令。ISR 同时保留上一 byte 与新采样 byte，RX
  `push noblock` 前后保存/恢复流水内容。这样解析 FIFO 丢弃不会改变透传，邻接 owner
  bit 不再来自准备脚本时的 whole-frame snapshot。内部命令需要安装位置重定位和
  ARM 前白名单检查；末 bit 必须位于不可更新的 tail，不能跳过本节点合法 bit。
- 指令可装载性：候选 `masked_follower.pio` 已由项目使用的 pioasm 汇编，DATA 程序
  28 条，加现有 clock-latch 4 条合计 32 条，保持原 PIO 分区和同 SM RX/TX 端点。
  该组合没有额外指令余量；任何后续改动都须重新核算完整 persona。
- owner 模型：`candidate-model.json` 解释实际汇编出的指令字，覆盖所有 8 种 shift、
  256 种邻居 byte、脚本准备后的邻居变代和双帧流水，共 2048 组、688128 个输出 bit。
  输出与独立“仅改本节点 logical segment 后再串行化”的 oracle 相同，RX 卸载与实际
  输入相同。另有完整 packet 大小及 RX 全丢弃对照；结果不等于固件已有 slot CRC 或
  当前业务 header/bitmap 的授权策略验证，实际 C builder 集成与沿途 CRC HIL 仍待完成。
- 完整路径：将第八 bit 的选择提前到 WAIT 前，使 byte 收尾与最终 frame boundary 都有
  确定时序；当前 D=15、period=25、high=12 拍的候选在第 24 拍回到下一 WAIT，采样和
  输出仍在名义 rising 后第 16/18 拍。`candidate-timing.json` 的 450 组延迟/周期/占空比
  检查中，258 组满足候选预算、192 组不足周期负测均被识别。算式仍是指令域候选；
  小 delay 编码、pad/synchronizer、真实边沿 jitter 和此前绝对模型偏移不能由此宣告解决。
- RAM 拒绝：以 R3 linker map 快照为准，`__end__` 到 RAM 顶仅余 2624 B；该双脚本池
  为 2464 B。直接整帧展开需 9824 B，增量 7360 B，超过剩余容量 4736 B，明确拒绝
  该存储实现。当前 RAM 本身也未达到正式余量目标，不以“增量能放下”冒充产品 RAM 通过。
- 稀疏替代：固定 header/local-mailbox/trailer 存储窗口使用 token 池，其它位置由固定
  PASS word 重复读取。`candidate-resources.json` 的 192 组 slot/byte-shift/bit-shift
  布局重建均与完整指令流相同；包含候选描述符的双版本主体最坏 2464 B，另需共享
  PASS 常量与元数据。这些是存储窗口，不授予窗口内任意 bit 的修改权。描述符重启、
  对齐和元数据仍未计入真实 linker；必须明确 TDMA owner 的 descriptor loader DMA
  角色、静态冲突表与生命周期，不能临时借用其它已冻结 DMA 端点。
- 带宽与下一切片：该候选在本运行点的命令 TX DMA 为 20 MB/s，原 RX 卸载仍为
  5 MB/s；稀疏存储不减少线路命令带宽。模型假定命令已到 FIFO，不能证明 DMA 仲裁、
  断粮或自主续装；优先核验 RP2350 descriptor/trigger/abort 与 SRAM/带宽准入，再实施
  006/007 固件切片，完成软件回归、build、P3、四板短帧和每 hop owner/CRC 采集后再迁移。
  本项不是 B1 自主发车通过，不提升 `TDMA-RESIDENT-01` 或其它登记状态。

### TDMA-PROGRESS-20260911-006 - B0 当前夹具字节流水证明与 byte 边界预算缺口

- TODO task ID：`TDMA-FLIGHT-002A`、`TDMA-FLIGHT-006`、`TDMA-FLIGHT-007`。以下数字、build 和结果均为
  本轮快照，非事实源；没有修改固件、PIO 或正式工具。源码仍为 `7f128cb`，build
  `20260911142352`。证据根为 `out/HardwareAcceptance/20260911/tdma-flight-b0-repeat/`，
  入口 `b0-observation-manifest.json` 与其后发现完整性缺陷的 `integrity-addendum.json`
  （联合读取，后者保留 owner/integrity 未通过及后续优先级），采样文件位于同日 `tdma-flight-b0/final-round5-*`
  至 `final-round8-*`；原始 bin、metadata、脚本和摘要均有 digest。
- 当前配置：四板、原基线 matrix、level 7、实际 SCK 10 MHz；通过既有 owner 命令在
  STOPPED 边界切换 process-image/raw-flight。`controlled/`、`raw-controlled/` 与
  `restored-process-image/` 的 passed、closed_loop_passed、realtime_gate_passed 均为
  true，diagnostic_continue=false。raw 对照没有请求 DPLL provisional，仍保留原负载
  mask；模式差异完整记录，不作为相同业务/DPLL 负载下的单变量 CPU 性能比较。
- process-image 重复窗口：新增两轮四板完整采集，CRC/sequence/hop 对齐与资源释放
  均通过；跟随节点 DATA 对应边沿范围继续落在 848–912 ns，reference 同一 bit 的
  发出到返回为 2952–3000 ns。原四板完整窗口与本次共覆盖独立的两个启动会话。
- raw 对照：两轮四板均保留完整 296-byte packet，进出 packet 逐字节相同、协议头 CRC
  正确、hop 保持原值。三个跟随节点共 14208 个 packet bit 与前一个 byte 的输入逐项
  一致；原始输入采样附近的全部相位点均一致，改按 7 bit 或 9 bit 深度对齐则失败，见
  `raw-byte-depth-r2.json`。这证明当前采样窗口中的固定上一 byte 流水映射，不能把
  零填充区或相同 clock ordinal 当作额外 packet/身份依据。
- B0 物理结果：本夹具 raw 跟随节点对应 DATA 边沿为 848–896 ns，最坏观测上界由
  各跟随节点窗口共同覆盖；reference 同一 bit 发出到返回为 2960–3000 ns。完整包
  进出窗口重叠成立。输入 DATA 到达相位会改变实际 transition-to-transition 延迟；
  固定字节流水深度、逐 bit 的时序位置与量化上下界共同构成本次物理能力结论，
  不声称所有 bit 的纳秒延迟完全相同，也不把同 bit 返回时间当作整帧串行化时间。
- 模型边界：独立 PIO 指令时序模型相对输出边沿存在约 16 ns 的绝对偏移，原始
  `model_matches_within_sample=false` 保留；该偏移未完全归因，不是新冻结硬件常数。
  模型只帮助定位 byte 分派位置，B0 结论来自原始完整包、逐 bit 映射与实测上下界，
  不将拟合模型的绝对时序失败改成成功。
- 新缺口：当前 `TDMA_PIO_SPI_FLIGHT_DATA_REARM_CYCLES` 只覆盖 bit body。按现有
  PIO 程序计数，raw 与 process-image 的 byte 边界回到 WAIT 分别还需不同的指令预算；
  当前 delay=15、名义 bit period=25 clk_sys 拍时，raw 边界名义上在第 25 拍返回 WAIT，
  process-image 则在第 28 拍。实测 process-image 的 byte 首 bit 输出更晚，而 raw
  没有同等延后。该算式是审计快照，仍需模型/真实 PIO 与完整资源准入回归后才能
  冻结预算；新增 `TDMA-FLIGHT-006`，禁止通过放宽 phase 或降低频率掩盖该缺口。
- 完整性补审：所有本批捕获的 transport flags 均含 `FLIGHT_MUTABLE`，按现有协议
  transport CRC 仅覆盖 header。此前“完整 packet + CRC”不代表 payload 整体受同一 CRC
  保护。`wire-integrity-audit.json` 另按真实 mailbox layout 校验 CRC16；raw 只有 origin
  mailbox 有效，其余为原样 padding，DPLL trailer 不在本次 segment CRC 覆盖范围内。
- owner 缺陷：process-image 跟随窗口中有 7 个出现本节点写入邻接 owner 的 CRC bit，
  使原本有效的邻居 mailbox 校验失败。`overlay-boundary-audit.json` 将变化定位到本地
  slot 前一 CRC byte 的低位，分别对应实际 alignment shift；字节对齐节点未出现该变化。
  下一 owner 重写自身 mailbox 后，reference 返回帧和现有基本计数可以恢复正常，因此
  端到端通过不能证明沿途所有权。新增 `TDMA-FLIGHT-007`，保留该未修复缺陷。
- 真实源码反例：`reproduce_stale_neighbor.c` 链接当前 `tdma_flight_overlay.c`，让邻居在
  本地脚本准备后更新，再按 bit 序列模拟原 PIO 的 PASS/REPLACE。字节对齐保留邻居，
  其余 7 种 shift 都覆盖邻居新 bit，见 `stale-neighbor-counterexample.json`。命令退出
  成功只表示反例复现，`owner_boundary_accepted=false`；未将反例写成产品测试通过。
- 状态：`TDMA-FLIGHT-002A` 的当前四板/profile 物理能力取证完成，允许进入 B1 方案
  和资源设计，并优先整改 `TDMA-FLIGHT-006/007`。该结论不覆盖新 persona、其它频率/环境、无 Core1 发车/续装、单圈多 Node
  更新、完整 WCET 或严格产品 P3；相关改动仍需对应源码的重新取证。`TDMA-RESIDENT-01`
  与 HAOFV-879 等登记状态保持原样，长期目标尚未完成。
- 终态：对照结束后恢复 process-image、独立复验短帧闭环，再经工具停止。`final-stopped.json`
  确认四板均为本 build、STOPPED，采集 RELEASED、SD job DONE。原 P3 strict 校准失败
  与 Core1 WCET 超限仍保留，不能用本次 B0 结果覆盖。

### TDMA-PROGRESS-20260911-005 - 有限同钟采集、显式存储恢复与 DATA 边沿取证

- TODO task ID：`TDMA-FLIGHT-002A`、`SYNC-LA-009`。以下 build、样本量、计数和时序数字
  均为本轮快照，非事实源。证据根为 `out/HardwareAcceptance/20260911/tdma-flight-b0/`，
  入口 `slice-manifest.json`；实现提交 `7f128cb`，当前源码 build 为 `20260911142352`。
- HAOFV 边界：有限采集复用 SYNC_IO analyzer owner、原单槽邮箱、persona manager 与
  Resource Arbiter。PIO 只读六 pad、自计数后停住，DMA 只写固定 workspace；Core1 冻结
  数据和版本化 metadata，Core0/StorageAO 分段导出，全部 DONE 后经原邮箱释放租约。
  旧 analyzer pending/inflight 批次先完成；没有把原始复制、CRC 或 SD 写入挂到 Core1。
- 软件与构建：相关 Python 用例合计 `86 passed`，analyzer 与 Resource Arbiter C 测试通过。
  App handoff harness 编译真实导出函数，覆盖旧批次保护、分段失败、错误 epoch、显式
  重试、尾段和零样本。真实 pioasm 核对既有程序机器指令未变，仅增加有限采集程序。
  `firmware-r3-index.json` 绑定 package、ELF/map、UF2 与生成 PIO header 的 digest。
- P3：当前源码 receipt scope 为 `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，staged fingerprint
  核对通过；`p3-r3/diagnostic.json` 的 `strict_gates_passed=false`。coarse CLK level 7
  在 NO1 ARM 返回 `result=5`，TRN-00 MARK offset row 未通过；外层 passed 只说明诊断
  流程完成。原始拒绝保留，不解释为严格产品 P3 或完整 HAOFV 验收。
- 独立闭环：同 build、原基线 matrix、DPLL provisional 的 `r3-controlled/summary.json`
  中 `passed`、`closed_loop_passed`、`realtime_gate_passed` 均为 true，
  `diagnostic_continue=false`。该工具的 TDMA 范围不能替代完整 phase/WCET。
- 四板原始采集：`final-round3-*` 与 `final-round4-*` 两轮均完整采得并下载每板
  8192 words、8 ns 同钟样本，全部 RELEASED、无 RXSTALL。`r3-schedule-deltas.json`
  记录采样、导出及主机间隔内各板 good RX 增量均超过 147000，基础收帧、transport、
  schedule/profile 错误增量均为零，enabled/quarantined mask 未变。它不是采集开销的
  单变量测量：TDMA phase overrun/deadline 计数持续增长，历史最大约 1.65–1.82 ms，
  高于本 profile 的 380 us WCET；长期目标保留该失败。
- DATA 取证：`r3-two-rounds-phase-sweep.json` 从原始 bin 重建完整 PHY packet，检查协议头
  CRC、sequence、identity 与 hop 进展；`FLIGHT_MUTABLE` 下该 CRC 不覆盖 payload，后续
  segment 完整性补审见 `TDMA-PROGRESS-20260911-006`。逻辑 TX/RX 是连接器名称；跟随节点 DATA 方向是
  `BOARD_TDMA_TX_DATA_IN_PIN` → `BOARD_TDMA_RX_DATA_OUT_PIN`，reference 则观测发出到
  返回。逐位对应边沿排除变化字节，两轮跟随节点观测范围为 848–912 ns，reference
  发出到返回为 2952–2992 ns；完整包在本地进出窗口内重叠。量化误差为各侧采样差分
  的一个 sample period 范围，不能声明模拟 pad 精确时延或跨板共同时间。
- 解码边界：直接在 clock rising sample 读 DATA 时，部分完整窗口没有通过 CRC，原结果
  保留在 `r3-round3-packet-edges.json`、`r3-round4-packet-edges.json`。后续仅在离线端
  扫描 bit 内采样相位，保留全部相位结果，选择完整 CRC 有效区的中点；没有修改 bin、
  修补 bit 或放宽 CRC。当前是有限重复窗口，尚不足以冻结最坏链路/环境和 pipeline
  恒定上界，`TDMA-FLIGHT-002A` 保持 IN PROGRESS。
- 线路节拍：`r3-reference-cadence-summary.json` 的 128 ns 分辨率 CS 长窗口测得有效
  传输宽度 240.896 us、相邻 RX CS 起点间隔 4115.2 us。首个 TX CS 起点被触发截断，
  不当作完整起点；低速采样的 CLK/DATA 存在混叠，只用于 CS 节拍，不参与 DATA 证明。
- 存储失败与恢复：候选包的目录分页失败保留，后来按 epoch 取回原数据。R2 的 NO3
  第三分段真实 WRITE_FAILED 后，仅前两段可取回，剩余 RAM 随后 OTA 重置，未假称
  完整恢复。R3 新增显式 `BURSt:SAVE <sequence>` 和 Core0 export failure/retry snapshot；
  `r3-export-retry-hil/summary.json` 通过真实目录冲突制造失败，拒绝错误 sequence，移走
  冲突目录后按正确 sequence 补齐原 capture，失败历史保留且未重采。原 profile apply
  失败、启动稳定样本不足及 P3 严格失败均由 manifest 指向原始目录。
- 终态：`r3-timeout-hil-summary.json` 证明 STOP 后无边沿产生零样本 TIMEOUT，header-only
  文件导出并释放租约；原 capture CLI 的 `passed=false`、`timing_valid=false` 保留。
  `r3-final-stopped.json` 确认四板均为本 build、STOPPED、采集 RELEASED、SD job DONE。
- 后续：先闭合本采集切片，再收敛 B0 的逐帧、最坏链路和 bit 内延迟解释。随后按既定
  阶段实施自主续转及 Core1 WCET 收敛；resident/flight、反馈与 HAOFV-879 契约状态不提升。

### TDMA-PROGRESS-20260911-004 - PIO WAIT 补丁位置保护与当前源码闭环

- TODO task ID：`TDMA-FLIGHT-005`；实现提交 `4083104`。以下 build、计数、采样预算和
  时序数字均为本轮快照，非事实源。证据根为
  `out/HardwareAcceptance/20260911/tdma-flight-005/`，入口 `slice-manifest.json`。
- 变更边界：`tdma_pio_spi_flight_process_follower_program_init()` 在 ARM 安装 WAIT 操作数时，
  使用 PIO public label 生成的 offset，替代手写下标。没有改变命令流、PIO 指令序列、
  phase、owner 或状态迁移。相关 host 测试覆盖插入指令后的重定位，以及错误数字下标、
  标签离开 WAIT、时钟边沿符号错配的拒绝；相关测试合计 `15 passed`。
- 汇编/构建：真实 pioasm 生成的全部程序机器指令与基线逐项一致，见
  `pio-equivalence.json`；当前源码 build `20260911130454`，四板 OTA 回读成功。
  默认 pre-commit 的源码指纹验收通过，scope 为 `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`；
  新 receipt 保留该 scope 和原始失败，不能解释为严格产品 P3 或长期目标验收。
- P3 原始结果：`p3/diagnostic.json` 的 `strict_gates_passed=false`。coarse CLK 在 NO4
  ARM 返回 `result=8`，coded marker 与 MARK offset 校准也失败；P3 Latency Cal 和
  TDMA process-image 子门禁通过。所有拒绝、诊断继续和原始日志保留。
- 独立闭环：沿用先前基线 matrix 与 DPLL provisional 设置，`controlled/summary.json`
  的 `passed`、`closed_loop_passed`、`realtime_gate_passed` 均为 true，
  `diagnostic_continue=false`。启动填充错误按原工具 startup barrier 单独记录；稳态四板
  good RX 增量分别为 1281、1285、1283、1283，基础收帧/transport/schedule/profile 错误
  增量均为零。此结果只覆盖 TDMA 工具定义的范围，完整 HAOFV WCET 仍未验收。
- 波形取证：原 SD job 超时保留为 `diagnostic_passed=false`；原 job DONE 后取回四板
  文件于 `recovered/`，没有重发 SAVE。离线 SCK 短窗检查通过，RX 窗口未含完整 packet，
  也没有同钟本地 TX 样本；内容相关偏移不能作为 B0 pipeline delay。
  `live-state.json` 确认四板均为本 build 且 STOPPED。
- B0 下一切片：`b0-budget-design.json` 核对现有 SYNC_IO workspace、DMA 与存储分段上限。
  设计候选为六 pad 同拍打包、硬件边沿触发、PIO 自计数后停住和有限 DMA；原始 sample
  index、实际 clk_sys/分频和 RXSTALL 必须随冻结缓冲发布。Core1 只执行有界控制，
  Core0/StorageAO 在保留 workspace 所有权期间分段导出。该文件是设计输入，尚未实现
  或通过资源/WCET/HIL 门禁；不新增冻结契约。

### TDMA-PROGRESS-20260911-003 - 独立工作树单变量回归实验

- TODO task ID：`TDMA-FLIGHT-004`、`TDMA-FLIGHT-005`。
- 日期：2026-09-11；实验目录为 `out/tdma-flight-ab-20260911/`，基于 `44e06d8` 的独立
  detached worktree。主工作树固件保持基线；以下 build、周期和测量值均为快照，非事实源。
- 非阻塞 PIO 组：只修改 `tdma_pio_spi.pio`，生成的 Git blob 为
  `d4b7f87d9c836a0362dc2419489233ac15d7fc91`，与归档最终 PIO 完全一致。
  保留原 operating profile、反馈/发车预算、overlay grace 和 RefMem 逻辑。
  overlay 主机测试与 build/四板 OTA 通过，build 为 `20260911122224`。
  同一基线 matrix 的 process-image 实测在原启动期限内失败；四板原始 SD 文件已取回。
  因而该非阻塞 PIO 改动在原节拍下即可独立触发回归，不能归咎于本组未修改的发车周期。
- 非阻塞组 P3 边界：构建与 OTA 完成后，coarse calibration 报告因输出目录位于独立
  worktree 外触发 `relative_to(root)` 异常。原日志保留；后续实验改用 worktree 内的
  `out/` 输出，未修改工具或放宽产品门禁。本组没有严格验收凭证。
- 反馈/发车预算组：恢复基线 PIO blob `1ce8a6996f7e16a9b23d92310e40b36a807a5add`；仅将
  `tdma_service.c` 两处 `cycle_period_ns * node_count` 改为 `cycle_period_ns`，并调整
  对应主机测试预期。level 7 周期表仍为原值；四节点预算从 4 ms 变为 1 ms。
  scheduler 主机测试及 build/四板 OTA 完成，build `20260911122909`；P3 的严格门禁和
  process-image 失败，使用原基线 matrix 独立复测同样未通过启动闭环。
  此实验改变现有共享的反馈超时与发车间隔，尚未将这两个用途彼此分离。
- 归档 2 ms profile 补测准备：只再应用归档的 operating profile 行，初次 P3 在 matrix
  生成时被 profile CRC 校验拒绝，尚未进入 TDMA 闭环。原因是工具的 `PROFILE_FACTS`
  仍镜像旧周期；同步归档对应工具元数据及其 source-consistency 断言后，matrix 测试
  `19 passed`，重新执行完整 P3。初次拒绝保留于 worktree 的 `out/ab-period2-p3/`；
  未引入归档里的串口重试、mode 重试或其他业务改动，也未关闭 CRC 校验。
- 归档 2 ms profile 结果：匹配工具后的 build 为 `20260911124102`，P3 matrix 生成完成，
  但 NO1 的 DPLL provisional owner 在 ARM 前拒绝激活。保留该失败后另执行不请求 DPLL
  provisional 的 TDMA process-image 探测，仍在原启动期限内出现 transport/bitmap 错误。
  四板 SD 原始文件已取回；`ab-period2-manifest.json` 记录完整来源。这一补测使用新的
  profile/matrix，且未请求 DPLL 激活，不能把它当作相同 DPLL 负载下的单变量性能比较。
- 波形边界：上述两组原始 SCK 短窗频率/占空比检查均通过，同时 process-image 失败。
  SCK gate 只能证明相应短窗时钟，不代表完整数据流、所有 frame 或 B0 delay 通过。
  SD 写入超时均保留原报告，在原 job DONE 后取回数据，没有重新 SAVE 或修改超时。
- 证据：`out/HardwareAcceptance/20260911/tdma-flight-baseline-ab/ab-noblock-manifest.json`、
  `ab-period-manifest.json`、`ab-period2-manifest.json` 绑定差异、package、生成 PIO header
  与原始波形 digest；同一目录下 `ab-*-controlled/`、`ab-*-recovered/` 保存独立复测和原始数据。
  反馈/发车预算组完整 P3 归档在同一目录下的 `ab-period-p3/`；归档包含原本位于独立
  worktree 的 `ab-period2-controlled/`、`ab-period2-p3/` 与 `ab-period2-matched-tools-p3/`。
- 当前结论：已分别证明非阻塞 PIO 改动和缩短共享预算可以使基线回归；归档最终 PIO
  的补丁位置正确。byte-dispatch 模型提供错位机制线索，但未证明全部历史 build 的
  电气失败过程；仍需显式 frame boundary、descriptor generation 与物理 byte 位置绑定。
- 恢复验证：`restore-baseline-ota/summary.json` 确认四板恢复 build `20260911120622`，
  `updated_count=4`、`failed_count=0`。沿用原 matrix、load mask 与 DPLL provisional
  设置，`restore-baseline-controlled/summary.json` 的闭环/实时门禁通过，四板稳态
  RX bad、transport/schedule/profile bad 增量均为零。capture 原 SD job 超时仍保留为
  `diagnostic_passed=false`；原 job 完成后四板原始文件取回于 `restore-baseline-recovered/`。
  硬件最后处于原基线 build 的 STOPPED；实验固件没有留作现场运行版本。
- 状态：`TDMA-FLIGHT-004` 完成上述改动族受控归因；不宣称原始混合 patch 的全部历史
  电气失败路径已经证明，也不宣称 B0 或自主循环通过。独立工作树保留实验源码与产物，
  主工作树没有合入这些固件/工具改动。
- 下一 gate：B0 同钟观测、`TDMA-FLIGHT-005` 指令位置保护；发车间隔、反馈健康窗口与
  Core1 phase 的独立契约随后按 HAOFV owner 边界收敛。

### TDMA-PROGRESS-20260911-002 - 当前源码四板基线与 SD 超时后取证

- TODO task ID：`TDMA-FLIGHT-002A`、`TDMA-FLIGHT-004`、`TDMA-FLIGHT-005`。
- 日期：2026-09-11；下列 build、计数和实测数字均为本轮快照，非事实源。
- 构建/OTA：当前固件源码与 `e45695c` 一致；独立目录
  `out/build/tdma-flight-baseline-20260911/` 完成构建，四板均更新并回读 build
  `20260911120622`。身份顺序为 NO1 `0010071E65B5CB38`、NO2 `FB276192BEF9CCE1`、
  NO3 `2BD5090FE009FA2A`、NO4 `A1E549202D18ED6A`；端口以实时身份查询为准。
- P3 失败：`p3-baseline/diagnostic.json` 为 `strict_gates_passed=false`；coded marker
  校准失败，process-image 首次在 NO2 ARM 时返回
  `DISTRIBUTED_REFMEM_TDMA_ARM_FLIGHT_MAP_REJECTED`。其子报告的 `passed`、
  `closed_loop_passed` 和 `realtime_gate_passed` 均为 false；外层 quick diagnostic
  完成及退出码不能替代严格验收。隔离 receipt 只留作证据，不用于放行源码提交。
- 未改固件的复测：沿用同一 calibration matrix 和默认时序门限，
  `baseline-process-retry/summary.json` 的上述三项及 `diagnostic_passed` 均为 true，
  `diagnostic_continue=false`。稳态四板 transport bad、bitmap/reject 增量为零。
  matrix 本身仍有 diagnostic 校准来源；本结论只证明过渡实现闭环，不证明正式校准、
  自主发车或单轮多 Node flight，也不消除首次 ARM 拒绝。
- Core1 边界：该复测的 `dpll_schedule_gate` 仍记录 DPLL overrun、deadline miss 和
  max runtime 超 WCET 的诊断反馈；`realtime_gate_passed=true` 是工具当前 TDMA 门禁
  范围内的结果，不能外推为全部 HAOFV phase/WCET 已通过。长期目标保留这一独立验收项。
- 原始捕获：`b0-raw-flight/summary.json` 的闭环/实时门禁通过，但
  `diagnostic_passed=false`：NO1 SD FILE_WRITE job 在工具原观测期限内仍为 RUNNING。
  未重发 SAVE、复位或放宽超时；后续查询确认原 job DONE，随后取回同 epoch 的四板
  `/cal/trn03b_node*_g1789128644_e1789128869.json`。原超时报告保留不变，后续证据在
  `b0-sd-job-followup.json`、`b0-recovered/all-recovery.json` 和 `b0-recovered/node*.json`。
- B0 边界：四板文件均只有 physical RX 与短窗 SCK 样本，TX 字节数为零。代码
  `tdma_pio_spi_phys_copy_normal_capture()` 明确不复制可变软件 TX history；相邻节点
  RX 内容对齐与 SCK 频率/占空比不能替代同钟 RX/TX 边沿、逐帧延迟上/下界。因此 B0
  未完成，也不能将缺少观测能力写成物理 cut-through 不可行。
- B0 观测审计：现有 SYNC_IO analyzer 同时采样 pad 电平，但
  `sync_io_logic_analyzer_hw_service()` 在 Core1 消费样本时填写 `hardware_tick`，且未
  导出原始 DMA sample index；不能将该时间戳当作采样边沿时刻。短窗 SCK 分析通过，
  但相邻 RX 内容相关偏移不代表传播时间；缺口与原始文件 digest 在
  `b0-capability-audit.json`。需要硬件触发的完整采样窗口或可审计的 sample position。
- 回归模型：`baseline/overlay-dispatch-model.json` 显示非阻塞 PULL 在 END 后预取
  fallback PASS、随后才收到脚本时，脚本替换位置会落后物理 byte。它只是 byte-dispatch
  模型，不模拟电气边沿或 DMA 仲裁，不能单独认定历史失败原因。
- 下一 gate：独立工作树 `out/tdma-flight-ab-20260911/` 对归档 PIO 与节拍改动分别执行
  单变量构建/四板实验；主工作树不叠加自主循环固件。B0 同时核对既有 SYNC_IO 只读
  analyzer 的同钟观测能力。首次 ARM map 拒绝的精确原因仍待复现。

### TDMA-PROGRESS-20260911-001 - HAOFV 自主循环长期目标启动与回归证据核对

- TODO task ID：`TDMA-FLIGHT-002`、`TDMA-FLIGHT-004`、`TDMA-FLIGHT-005`。
- 日期：2026-09-11。
- 变更/提交：启动用户授权的长期目标；起始 HEAD 为 `44e06d8`，工作树干净。
  当前固件源码与 `e45695c` 一致，后续差异为文档及示波器工具。尚未修改固件或 PIO。
- 实时目标快照，非事实源：用户报告物理环路约 240 us、Core1 操作约 1.5 ms；后续必须
  分别实测 wire、帧间等待、CPU phase 和业务更新周期，不能将上述数字直接冻结为合同。
- 验证：通过 `SYSTem:FW:BUILD?`、`SYSTem:BOARD:NO?`、`SYSTem:CORE?` 和 runtime 查询
  确认四块环内板可访问，均运行 build `20260911105221` 且 ring 停止（本次快照）。
  板卡身份和实际端口以 `baseline/live-identity.json` 为准，不沿用历史 COM/NO 对应关系。
- 回归证据：`baseline/history-index.json` 索引了原始验收与闭环结果。部分历史 quick
  diagnostic 总表虽为 `passed=true`，但 `strict_gates_passed=false`，对应 TDMA 子报告
  `closed_loop_passed=false`，不能用于证明正常闭环。原始失败保留不变。
- 静态复核：`out/bug-code-archive-20260911/uncommitted.patch` 实际为带 BOM 的 UTF-16
  归档；按其真实编码解码后在内存中重建 PIO 变体，未应用到源码。当前与归档最终版本的
  WAIT 补丁均指向相应指令，见 `baseline/pio-patch-audit.json`。这仅排除该归档最终
  版本的补丁下标错误，不能替代历史各 build 的单变量 A/B。空 FIFO 退化后的脚本/物理
  byte 对齐仍需验证。归档中声称 follower 未保存 packet 的改动也不是已证实缺陷：
  `resident_process_image()` 仅对 reference 返回真，现有条件已保留 follower 的收包。
- 软件验证：现有 ring adapter 主机单测通过，输出为 `baseline/adapter-tests.log`。
  文档检查与回归检查通过；登记表旧格式 ID 警告保持原样，未擅自改变契约状态。
- 构建/硬件：已启动当前工作树的 `p3_hardware_acceptance.py run --tdma-only`，独立 build、
  OTA 和 receipt 写入本轮证据目录；完成结果待后续记录，不宣称本条已通过硬件验收。
- 证据位置：`out/tdma-flight-goal-20260911/`，其中 `baseline/` 保存静态和板端读取证据，
  `p3-baseline/` 保存当前源码验收；`p3-baseline-run.log` 保存编排输出。
- 工具入口：板端访问及项目 PowerShell 主机测试使用 Windows 原生 Python/PowerShell，
  保持 COM 驱动及 `.ps1` 工具兼容；文档与源码按 UTF-8 读取。
- 失败/回退：未改写、删除或重新标记历史失败；不把 quick diagnostic 总体完成当作
  strict gate 通过。没有固件变更需要回退。
- 下一 gate：完成当前源码四节点基线与原始波形验证，再以单变量实验闭合回归归因；
  `TDMA-FLIGHT-004` 仍未完成，不叠加自主循环固件改造。

### TDMA-PROGRESS-20260902-003 - resident process image 架构决策记录

- TODO task ID：`TDMA-M6`、`TDMA-FLIGHT-001`；本记录只更新文档，不宣称代码或硬件验收完成。
- 架构决策：TDMA 的目标是“持续流中的循环内存”。启动时只注入一次 resident process
  image；进入 RUNNING 后，物理 frame 按 cycle 持续在环路中转发。每个 Node 在自己的固定
  窗口执行本地 UNLOAD/LOAD，多个 Node 可在同一轮完成各自 segment 更新；没有新 generation
  时继续透传上一版值。回到 origin 只表示 cycle boundary，不表示结束。
- 生命周期边界：只有 `STOP`、复位、故障或显式重新配置才停止 resident loop。`hop_limit`
  只约束单个物理 frame instance 的传播，`IDLE_BEACON` 只保留为 bring-up、维护或兼容
  路径，不得在产品 RUN 中替换 resident process image。
- 当前实现边界：`tdma_pio_spi_ring_adapter` 仍按周期构造 beacon，adapter/FSM 仍保留
  `FRAME_COMPLETE` 过渡语义；这两处列入 `TDMA-FLIGHT-001`，待固件迁移后重新执行
  TDMA 短帧闭环和 SD 原始波形验收。
- 验证范围：本次未修改 C/PIO/构建/工具/测试实现，未执行 OTA 或硬件验收；仅需执行文档
  自回归检查。
- C11 交叉审核记录：审核方为状态机域架构文档与 TDMA 当前实现快照；审核方式为域间
  文档交叉 + 代码层间对照；审核结论为 `PASS_WITH_NOTE`，备注是 resident cycle 仍为
  待实现迁移契约。审核日期：2026-09-02。

### TDMA-PROGRESS-20260831-001 - 四节点 TDMA 与 NO5 观测门禁解耦

- TODO task ID：`TDMA-HIL-001`、`TDMA-HIL-003`、`TDMA-DPLL-002`。
- 变更：硬件验收编排器新增 `run --tdma-only` / `resume --tdma-only`。该范围使用
  active profile 的 NO1--NO4 作为唯一 OTA、校准、训练和 TDMA 集合，生成独立的
  `HAOFV_HARDWARE_ACCEPTANCE_RECEIPT_TDMA_4NODE_V1`；不运行 NO5 DPLL 观测，也不将
  四板通过结果宣称为 DPLL 完整通过。默认 `run` 仍执行五板 OTA 和 NO5 只读观测。
- 验证：`test_p3_hardware_acceptance.py` 15 passed；TDMA/闭环/源码长度定向回归 101
  passed；`docs_check --strict-names` 120 文件通过；`doc_regression_check` freshness、
  registry、orphan 全通过（仅保留既有 contract ID 格式 WARN）。尚未因本次 host-only
  编排器改动刷写固件或改变 TDMA 实时路径。
- 下一步：NO5 已恢复后，先使用四板 TDMA-only gate 验证短帧稳定，再单独运行默认完整
  gate 验证 NO5 DPLL/VDC observation；两份 receipt 分别归档。

### TDMA-PROGRESS-20260831-002 - 验收动作时间探针与查询式等待

- TODO task ID：`TDMA-HIL-001`、`TDMA-DET-003`。
- 变更：验收编排器为每个子工具写入 `timing.json`，记录 UTC 起止时间、单调时钟耗时、
  argv、返回码和状态。串口 action 命令统一使用 `action_timeout`；P0T 邻接探测在
  START 后查询接收计数，达到门槛即结束，`topology_pair_wait_s` 仅作为失败上限。
- 失败基线：本轮四板 TDMA-only resume 在训练前置阶段失败，但已记录 P0T 212231 ms、
  粗 CLK 164235 ms、编码 MARK 566725 ms、P3 158974 ms；失败原因为后续 MARK 阶段
  设备在 *IDN? 发现时暂时不可用，非 NO5 依赖。证据：
  `out/hardware_acceptance/tdma-only-resume-20260831/timing.json`。
- 验证：验收、拓扑和 TDMA 工具定向回归 31 passed；全量 pytest 562 passed、1 skipped。
  本次仅修改 host 工具和配置，未刷写固件。

### TDMA-PROGRESS-20260829-001 - DPLL residual SD evidence capture

- TODO task ID：`TDMA-DPLL-008`、`TDMA-PAYLOAD-006`。
- 变更：提交 `66039fa` 增加固定 SRAM residual capture。Core1 只在 DPLL snapshot 发布边界追加固定记录；STOP 后由 Core0 调用 StorageAO 写入 `/traces/run/`，禁止 SD/FatFs 进入 TDMA/DPLL 热路径。
- 接口：`SYSTem:SYNC:VDC:DPLL:TRACe:ARM`、`STOP`、`STATus?`、`SAVE`；SD 二进制下载后由 `tools/dpll_observation_decode/dpll_observation_decode.py` 转为 `samples.json`，继续复用 `tools/dpll_residual_analyze/dpll_residual_analyze.py` 生成 SVG。
- 构建：`out/build/dpll-sd-capture-20260829/` 的 A/B/Boot、package 和 flash-link contract 通过；RAM 链接余量 `81772 B`、临时许可证下限 `80000 B`（均为快照，非事实源），FreeRTOS heap 未压缩，仍为 `128 KiB`。
- pytest：全量 `527 passed`；新增 decoder 正/反 CRC 测试通过。尚未执行本提交镜像的多板 OTA/HIL，因此 `TDMA-DPLL-008` 保持 `IN PROGRESS`。
- 下一步：对 NO1–NO4/NO5 执行异步 OTA；ARM→运行→STOP→SAVE→FILE:READ 下载，检查记录数、payload CRC、DPLL update sequence 连续性，再生成 residual SVG，并确认 TDMA schedule/WCET/error 无新增。

### TDMA-PROGRESS-20260829-002 - DPLL SD capture host orchestration gate

- TODO task ID：`TDMA-DPLL-008`、`TDMA-PAYLOAD-006`。
- 变更：提交 `eb9d852` 仅修改维护态主机工具和 SCPI 响应过滤；新增
  `tools/dpll_observation_capture/dpll_observation_capture.py`，并将 DPLL
  `TRACE:ARM/STOP/SAVE` 的复合响应登记到共享解析器。该提交不修改固件、TDMA
  SHORT wire、Core1/PIO/DMA phase 或任何实时预算。
- 工具边界：ARM 后只等待固定观测时间；STOP 后才由 Core0/StorageAO 写 SD，主机再
  下载、解码并生成 residual SVG。若样本数为零，工具 fail-closed，不调用 SAVE，避免
  把空捕获误判为有效波形；ARM 清理路径仍会对所有已尝试节点发送 STOP。
- 验证：全量 pytest `533 passed, 1 skipped`；`out/build/dpll-sd-capture-20260829/`
  A/B/Boot、flash-link contract 通过，`link_free_bytes=81772 B`，临时许可证下限
  `80000 B`，FreeRTOS heap 保持 `128 KiB`（均为快照，非事实源）。
- 五板实测：使用既有异步 OTA 镜像（NO1=COM3、NO2=COM5、NO3=COM6、NO4=COM4、
  NO5=COM25）执行 ARM→5 s→STOP；五板均返回 DPLL `service_count=0`，因此无 residual
  样本，工具明确报告 `trace contains no samples`，未生成 SVG，也未启动/改变 TDMA。
  当前阻塞属于 DPLL 服务未激活，不是 TDMA 时序回归；待 DPLL eligible 更新服务开启后
  重跑同一 SD→decode→SVG 流程。

## 文档接口

- 架构语义：`TDMA_DOMAIN_ARCHITECTURE.md`。
- 任务状态与门禁：`TDMA_DOMAIN_TODO.md`。
- 本文件只追加构建、测试、OTA/HIL、失败、回退和证据位置，不改变契约状态。

## 当前 checkpoint

当前四板/profile 的 B0 物理能力取证见 `TDMA-PROGRESS-20260911-006`；follower bit
所有权修复、live header CRC 变换、自主 descriptor 续转、generation 交接和启动对齐
验证见 `TDMA-PROGRESS-20260912-001/002/003`。这些已集成切片具备各自绑定源码的
短帧与原始波形证据，不能外推到 reference 自主发车或完整 Core1 WCET。
reference header 与返回 ring 反例见 `TDMA-PROGRESS-20260912-004`；完整 origin 图、
交接竞态、紧凑 RX/CRC 和固定窗口候选已完成离线执行，见 `TDMA-PROGRESS-20260912-005`。
STOP 依赖链、拒绝传播与停止态调度竞争的前置修复已提交，四板恢复闭环与原始 owner/CRC
验证见 `TDMA-PROGRESS-20260912-006`。C builder、物理资源/workspace 与异步 adapter
候选的工作树集成见 `TDMA-PROGRESS-20260912-007`；后续分步准备、当前源码回归和
准备态计数发布缺陷见 `TDMA-PROGRESS-20260912-008`，修复与对应指纹复核见
`TDMA-PROGRESS-20260912-009`；严格 P3 拒绝与原始失败均保留。
正常 ARM 仍走旧 origin。Calibration 有限期调试许可、DeploymentGate 诊断投影、真实 owner
交接及实际自主运行见 `TDMA-PROGRESS-20260912-011`；当前证据使用 `20260912` 新目录，
旧测试/build/P3/首次自主采样保留原路径。有限原始窗口与许可证失效取证不能消除
transport 错误或替代 service blackout；RX ring 复制竞态已有主机反例，实板归因待闭合。
RX DMA 计数与副本复制生命周期复验见 `TDMA-PROGRESS-20260912-012`；列车优先与四级
候车设计见 `TDMA-PROGRESS-20260912-013`。分项计时与当前源码实板归因见
`TDMA-PROGRESS-20260912-014`：自主从站的重复 RX 捕获/扫描成为主要开销；
`TDMA-PROGRESS-20260912-015` 限制同拍 RX 工作量并保留校准、下载与恢复原件。
继续收敛完整 phase 的剩余超预算工作，再推进无更新稳态及真正 service blackout。正式 RAM、完整 Core1 WCET、
硬件 mailbox 授权/参与证据和绝对 VDC 时间
仍未闭合；真正 TDMA service blackout 尚未取证，模型续转不能替代硅上证明。
`TDMA-FLIGHT-002` 继续执行，resident/flight 契约保持原状态。

### 历史 checkpoint（2026-08-28，保留原始状态）

拍级 Core1 schedule、mandatory-first Node mailbox 与四节点 TDMA 环路已形成闭环基线；NO5
仅作为 SMA/DPLL 观测节点，不加入 TDMA 物理环路。
`TDMA-DET-001`、`TDMA-DET-002`、`TDMA-DET-003`、`TDMA-PAYLOAD-001` 和
`TDMA-PAYLOAD-005` 已完成；DPLL observation 已改为固定 process-image trailer，启用后四板
transport bad 保持为零，但 NO1–NO3 mailbox bitmap 仍不完整，NO1 SD capture latch 仍超时。
因此硬件 observation 尚不能进入正式 VDC，`TDMA-PAYLOAD-002`、`TDMA-PAYLOAD-003` 和
`TDMA-PAYLOAD-004` 继续保持 `IN PROGRESS`。本次架构固化明确：正常 SHORT 不增加冗余
Node mailbox；recovery 使用独立双 buffer 和独立预算，并在发送时复用原 Node 固定 segment
offset；短帧诊断只保留基础摘要。

P0 SRAM 基础门禁已在 2026-08-28 通过：`out/build/dpll-p0-cal-workspace-20260828/`
的 `link_free_bytes=98356 B`，超过 RTOS 发布阈值 `98304 B`；校准 persona workspace
采用互斥 union，未改变 SHORT wire 或 Core1 phase。板端 OTA、RTOS 水位、四板基线和
NO5 观测仍未完成，因此 DPLL 尚不能进入 active matrix/eligible/servo 阶段。

## 验证与证据索引

| progress ID | TODO task ID | 证据 |
|---|---|---|
| TDMA-PROGRESS-20260912-020 | TDMA-FLIGHT-002I | `out/HardwareAcceptance/20260912/tdma-node-capacity/`：统一编译容量、真实边界与存储互操作测试、六/八完整构建、ARM RAM 对照和当前四板 P3/闭环证据；严格失败及正式 RAM/WCET 缺口保留。 |
| TDMA-PROGRESS-20260912-019 | TDMA-FLIGHT-002I | `out/HardwareAcceptance/20260912/node-capacity-ram-audit/report-r1.json`、`link-report-r1.json`：RX 切片封存后，固定 wire/RefMem/Calibration 存储布局下的六节点隔离编译、存活对象与 ELF/map 对照；保留正式 RAM FAIL，未部署候选。 |
| TDMA-PROGRESS-20260912-016 | TDMA-FLIGHT-002B/002F | `out/HardwareAcceptance/20260912/tdma-flight-async-platform/owner-boundary-r2.json`：将异步准备/解析纳入列车调度基础，区分纯构建、跨核版本交接与硬件提交；尚非运行验收。 |
| TDMA-PROGRESS-20260912-015 | TDMA-FLIGHT-002B | `out/HardwareAcceptance/20260912/tdma-flight-rx-budget/hardware-review.json`、`slice-manifest.json`：同拍 RX 限制、真实旧 scanner 反例、同源码 P3、SCK 原件恢复与有效矩阵、短帧闭环、完整 phase 对比和保留的失败。 |
| TDMA-PROGRESS-20260912-014 | TDMA-FLIGHT-002B | `out/HardwareAcceptance/20260912/tdma-flight-service-timing/hardware-review.json`、`slice-manifest.json`：固定分项计时、同源码 P3、复位恢复短帧、普通/自主完整 phase 归因、原始采集与下载恢复、STOP 确认及未通过的 RAM/WCET。 |
| TDMA-PROGRESS-20260912-011 | TDMA-FLIGHT-002B | `out/HardwareAcceptance/20260912/tdma-flight-origin-admission/slice-manifest.json`、`hardware-review.json`：当前源码临时许可证、实际自主运行、有限原始 CRC/owner 审计、到期/撤销/新 epoch 重入、复制竞态反例及保留的 transport/RAM/WCET 失败；旧原件由报告引用。 |
| TDMA-PROGRESS-20260912-010 | TDMA-FLIGHT-002B | `out/HardwareAcceptance/20260911/tdma-flight-service-boundary/slice-manifest.json`：owner intent 有界读取与窗口等待拆拍、当前源码前置闭环及完整 WCET 未通过证据。 |
| TDMA-PROGRESS-20260912-009 | TDMA-FLIGHT-002B | `out/HardwareAcceptance/20260911/tdma-flight-origin-status/status-manifest.json`：准备态累计事实发布修复、实际 C 回归、对应源码 build/P3 与生命周期证据复核；完整 WCET/RAM 和实际自主启用仍开放。 |
| TDMA-PROGRESS-20260912-008 | TDMA-FLIGHT-002B | `out/HardwareAcceptance/20260911/tdma-flight-origin-prepare/prepare-manifest.json`：分步构图/物理准备、取消边界、ARM workspace、当前源码回归、保留的 OTA/校准/启动失败及准备态计数缺陷。 |
| TDMA-PROGRESS-20260912-007 | TDMA-FLIGHT-002B | `out/HardwareAcceptance/20260911/tdma-flight-origin-integration/integration-manifest-r4.json`：物理层/异步 adapter 候选、当前源码四板 P3 诊断、短帧生命周期、原始重建与 owner 审计、STOP、未通过的 RAM/WCET 和下一准入边界。 |
| TDMA-PROGRESS-20260911-004 | TDMA-FLIGHT-005/002A | `out/HardwareAcceptance/20260911/tdma-flight-005/slice-manifest.json`：host 变异测试、汇编等价检查、P3/OTA、独立闭环、原 SD job 后续取证、停止状态和 B0 设计输入。 |
| TDMA-PROGRESS-20260911-001 | TDMA-FLIGHT-002/004/005 | `out/HardwareAcceptance/20260911/tdma-flight-baseline-ab/baseline/`：板卡身份、历史子门禁、PIO 指令下标、byte-dispatch 模型及 adapter 主机测试。 |
| TDMA-PROGRESS-20260911-002 | TDMA-FLIGHT-002A/004/005 | 归档根 `out/HardwareAcceptance/20260911/tdma-flight-baseline-ab/` 下的 `p3-baseline/`、`baseline-process-retry/`、`b0-raw-flight/`、`b0-recovered/`；严格失败、未改固件复测和原 SD job 后续取证分别保留。 |
| TDMA-PROGRESS-20260911-003 | TDMA-FLIGHT-004/005 | 归档根 `out/HardwareAcceptance/20260911/tdma-flight-baseline-ab/` 下的 `ab-experiment-index.json`、各组 manifest、`ab-*-p3/`、`ab-*-controlled/`、`ab-*-recovered/` 及 `restore-baseline-*/`；`archive-index.json` 保留两个来源工作树的路径映射和完整文件摘要。 |
| TDMA-PROGRESS-20260828-001 | TDMA-DET-001..003、TDMA-PAYLOAD-001..005 | `out/tdma_cycle_schedule/`、`out/tdma_process_image_budget/`、`out/build/pico2-release/`、`out/pytest/`。 |
| TDMA-PROGRESS-20260828-002 | TDMA-M1、TDMA-M2、TDMA-DET-003、TDMA-PAYLOAD-001/005 | `out/tdma_cycle_schedule/foundation_sync_trigger21.md`、`out/tdma_process_image_budget/current.md`、`out/ota/tdma_foundation_sync21_final_20260828/`。 |
| TDMA-PROGRESS-20260828-003 | TDMA-M4、TDMA-DET-003 | `out/build/recovery-20260828/DHRT100_UPDATE.pkg`、`out/ota/tdma-recovery-20260828-r2/summary.json`、`out/tdma/ring-baseline-20260828-four/`、`out/tdma/tdma_recovery_budget_20260828.md`。 |
| TDMA-PROGRESS-20260828-004 | TDMA-PAYLOAD-002、TDMA-DPLL-001、TDMA-HIL-002 | `out/build/dpll-load-20260828/`、`out/ota/dpll-fixed-load-20260828/`、`out/training/trn03b_four_dpll_fixed_load_20260828/summary.json`、`out/tdma_process_image_budget/dpll_fixed_load_20260828.md`。 |
| TDMA-PROGRESS-20260828-005 | TDMA-REL-002、TDMA-PAYLOAD-006 | 本次架构/TODO/登记同步；代码和 HIL 待按新 recovery owner 边界补齐。 |
| TDMA-PROGRESS-20260828-006 | TDMA-HIL-001、TDMA-DPLL-001/002 | `out/build/dpll-p0-20260828/`、`out/pytest/dpll-p0-20260828/`；P0 构建通过但 SRAM 门禁未通过，未执行 OTA/HIL。 |
| TDMA-PROGRESS-20260828-007 | TDMA-HIL-001、TDMA-DPLL-001/002 | `out/build/dpll-p0-ram-capture-pool-20260828/`；Calibration 维护态四个 8 KB 导出缓冲已合并，链接余量增加但仍未达到 SRAM 发布门禁。 |
| TDMA-PROGRESS-20260828-008 | TDMA-HIL-001、TDMA-DPLL-001/002 | `out/build/dpll-p0-staging-pool-20260828/`、`out/pytest/dpll-p0-staging-pool-20260828/`；RefMem staging lease 回收重复维护缓冲，pytest 506 passed/1 skipped，链接余量 65640 B，仍未达 SRAM 发布门禁。 |
| TDMA-PROGRESS-20260828-009 | TDMA-HIL-001、TDMA-DPLL-001/002 | `out/build/dpll-p0-cal-workspace-20260828/`、`out/pytest/dpll-p0-cal-workspace-20260828/`；任务栈/heap 收敛及 TDMA 校准 workspace union 后 RAM gate `98356 B` PASS，pytest `506 passed, 1 skipped`，尚待 OTA/HIL。 |
| TDMA-PROGRESS-20260828-010 | DPLL-LONG-001 / P0 | `out/build/dpll-p0-shared-sync-workspace-20260828/`、`out/pytest/dpll-p0-shared-sync-workspace-20260828/`；共享 SyncIO 维护 workspace 后 A/B/Boot/link contract 通过，RAM gate `98348 B` PASS，pytest `506 passed, 1 skipped`，长期任务已发布，尚待五板 OTA/HIL。 |
| TDMA-PROGRESS-20260828-011 | DPLL-LONG-001 / P0-P1 OTA gate | `out/ota/dpll-long-p0-send-20260828/`、`out/ota/dpll-long-p0-resend-com3-20260828/`、`out/ota/dpll-long-p0-baseline-20260828/`；COM3/4/5/6 发送达到 READY_TO_REBOOT，但新镜像提交后回滚，COM25 为 INVALID_STATE；P1 未放行。 |

## 失败与回退

- 新 mailbox wire version 与旧固件不兼容；多板验证必须使用同一 package 异步 OTA 完成后再 START。
- HIL 若出现 CRC、RefMem accept、TDMA deadline 或波形回归，回退整个 wire-layout 提交，不允许
  运行时关闭 mandatory 字段或借用 guard。

### TDMA-PROGRESS-20260828-006 - DPLL 长期任务 P0 基线门禁尝试

- TODO task ID：`TDMA-HIL-001`、`TDMA-DPLL-001`、`TDMA-DPLL-002`。
- 日期：2026-08-28。
- 构建：使用 `tools/cmake_build_auto/cmake_build_auto.py` 输出到
  `out/build/dpll-p0-20260828/`；A/B/Boot 构建和 flash link contract 通过，build id
  `20260828113417`，package CRC `0x2A4D458B`（均为快照，非事实源）。
- pytest：全量首轮 `505 passed, 1 failed`，唯一失败为 flash inventory 测试对合法 caller 数量的历史断言；
  已同步到当前 allowlist 并在 `6463823` 修复，定向测试 `7 passed`；最终全量复跑
  `506 passed`，证据目录为 `out/pytest/dpll-p0-20260828-final/`。
- SRAM 门禁：`tools/ram_budget_check/ram_budget_check.py` 报告当前链接余量
  `24680 B`，低于 `docs/arch/RTOS_HAOFV_TODO.md` P0-RAM 的 `96 KB` 发布阈值；上一份
  `recovery-20260828` 构建同样未达到该阈值，说明这是既有发布阻塞，不应归因于 DPLL 负载。
- OTA/HIL：因 SRAM 发布门禁失败，本轮未对五板 OTA，也未启动 NO1–NO4 环路；保持 fail-closed。
- 下一步：先完成 P0-RAM 的 staging/rollback/OTA buffer 生命周期复用和任务栈/heap 水位复核，
  重新构建并通过 SRAM 门禁，再重复 P0 OTA/HIL；在此之前不进入 active calibration 或 servo 调参。

### TDMA-PROGRESS-20260828-007 - 合并 Calibration 维护态导出缓冲

- TODO task ID：`TDMA-HIL-001`、`TDMA-DPLL-001`、`TDMA-DPLL-002`。
- 日期：2026-08-28。
- 变更/提交：代码提交 `5be632a`；仅合并 Calibration Domain 内互斥的 marker/data/SCK/ring
  导出 payload 存储，不改变任何 TDMA wire、PIO 原语或训练矩阵语义。
- 结果：四个历史 8 KB `char` 缓冲由一个带历史字段别名的 union 承载；构建产物
  `out/build/dpll-p0-ram-capture-pool-20260828/` 的链接余量从约 `24680 B` 增加到
  `49256 B`（快照，非事实源），Calibration 导出仍保持同步调用和固定容量。
- 验证：A/B/Boot 构建及 flash link contract 通过；`ram_budget_check` 仍未达到
  `RTOS_HAOFV_TODO.md` 的 96 KB 发布阈值，故未 OTA/HIL。
- 下一步：继续处理跨组件维护态 staging/rollback/OTA buffer 生命周期和任务栈/heap 水位；
  通过 SRAM gate 后才重复五板 OTA 与 NO1–NO4 TDMA 基线。

### TDMA-PROGRESS-20260828-005 - 固化原 Node 位置 recovery 与基础诊断边界

- TODO task ID：`TDMA-REL-002`、`TDMA-PAYLOAD-006`。
- 日期：2026-08-28。
- 变更/提交：本次仅更新 TDMA Architecture/TODO/Task Progress、登记表和 HAOFV 顶层可见性；未混入用户已有代码修改。
- 结果：明确 Core0 准备 recovery 数据，Core1 在固定 recovery window 选择 buffer 并装载 TX FIFO，PIO/DMA 负责发送；双 buffer 交替、每周期最多一帧、独立静态 recovery 预算、ACK/有界 retry/backpressure/fail-closed，以及原 Node 固定 segment offset 重传。
- 诊断边界：短帧和 recovery 只保留 CRC、sequence、FIFO、bitmap/WKC、profile identity、deadline/overrun/missing 和基础 quality；SD、SVG、原始波形和详细归因留在 Core0 或 maintenance/LONG 路径。
- 验证状态：本记录是架构固化，不宣称代码/HIL 完成；下一步先更新 recovery 与 process-image owner 的代码/工具，再执行构建、pytest、OTA 和四板 HIL。

## 下一 gate

五板 OTA 已完成，实际 TDMA/RefMem 运行边界为 NO1–NO4 四板环路，NO5 仅作环外观测。下一 gate
保持固定 wire plan，先定位 NO1–NO3 mailbox bitmap incomplete 并修复 NO1 SD capture latch，
再补齐频率/占空比、原始波形和 DPLL eligible observation/VDC lock 证据。任何新增负载必须先
通过静态预算和编译门禁，不能运行时借用 guard 或“看起来有余量”的拍。

## 按时间追加的任务记录

### TDMA-PROGRESS-20260828-011 - P0/P1 异步 OTA 与启动保持门禁未通过

- TODO task ID：`DPLL-LONG-001`、`TDMA-HIL-001`。
- 日期：2026-08-28。
- 任务目标：使用 P0 package 对 NO1–NO5 完成发送、重启、提交和逐板启动状态验证，确认新镜像稳定后再启动四节点 TDMA 基线。
- 执行动作：
  - 通过 `tools/ota_multi_update/ota_multi_update.py --send-only` 对 COM3/COM4/COM5/COM6/COM25 发送统一 package；所有写入产物保存在 `out/ota/`。
  - 按工具既有的 boot/reconnect/commit 流程复核 COM3；新 build 曾短暂读到 `20260828125440`，随后回滚。
  - 通过 `tools/multicore_board_validate/multicore_board_validate.py` 对当前旧 build 做只读基线快照，确认五板 core1 heartbeat 增长；VDC/DPLL skeleton service count 在部分旧固件上停滞，不作为 P1 TDMA 通过条件。
- 验证结果：
  - `out/ota/dpll-long-p0-send-20260828/summary.json`：COM3/4/5/6 发送成功并达到 `READY_TO_REBOOT`；COM25 发送前返回 `FAILED,2,"INVALID_STATE",4`。该 summary 的 `failed_count=1` 仅表示 COM25，不能把其它四板发送误判为失败。
  - COM3 新镜像启动后读取到 `SYST:FW:BUILD? = 20260828125440`，但提交/后续复核后恢复到 `20260828072954`，`SYST:OTA:RES?` 为 `MAX_ATTEMPTS`；证据位于 `out/ota/dpll-long-p0-resend-com3-20260828/`。
  - 当前五板仍为旧 build：COM3/4/5/6=`20260828072954`，COM25=`20260828025009`；未启动 NO1–NO4 新固件 TDMA 环路。
- 结论：P0 静态 build/RAM/pytest 通过，但板端启动保持和 OTA commit 未通过，DPLL-LONG-001 停留在 P0；不进入 active matrix、hardware latch、eligible gate 或 servo。
- 还需完成：
  - 先定位新镜像回滚原因（板端 heap/stack、水位、启动异常和 OTA boot-attempt 记录），必要时用单板受控复现；不得反复消耗 boot attempt。
  - 清理/恢复 COM25 的 OTA 状态后，再重新执行全体 send-only → boot/reconnect/commit，并保存逐板 `SYST:FW:BUILD?`、`SYST:CORE?`、`SYST:RTOS:STAT?`、`SYST:OTA:RES?`。
- 关联文件：
  - `out/ota/dpll-long-p0-send-20260828/summary.json`
  - `out/ota/dpll-long-p0-baseline-20260828/`
  - `tools/ota_multi_update/ota_multi_update.py`
  - `tools/ota_boot_commit/ota_boot_commit.py`
- 下一步：暂停 P1 TDMA 训练，先解决 OTA 启动保持门禁；修复后重新 build/pytest 并从 P0 重跑。

### TDMA-PROGRESS-20260828-010 - 发布 DPLL 基础件到闭环长期主线并完成 P0 静态复核

- TODO task ID：`DPLL-LONG-001`、`TDMA-HIL-001`、`TDMA-DPLL-001/002`。
- 日期：2026-08-28。
- 任务目标：按 P0→P7 固定依赖推进 DPLL：TDMA 基线、active calibration matrix、硬件 timestamp、eligible gate、最小 `SyncDpllFB`、`VdcVector`/NO5 验收、故障注入与长期稳定；任何后续阶段不得绕过 TDMA 或 Calibration 前置门禁。
- 完成内容：
  - 在 `TDMA_DOMAIN_TODO.md` 发布 `DPLL-LONG-001`、阶段退出门禁和不可变架构约束。
  - SyncIO capture DMA ring 与维护态 model pulse schedule 复用同一 workspace，并以互斥运行态保护两类 persona；未改变 TDMA SHORT wire、PIO 原语、Core1 phase 或 recovery 预算。
- 验证结果：
  - 使用 `tools/cmake_build_auto/cmake_build_auto.py` 构建到 `out/build/dpll-p0-shared-sync-workspace-20260828/`；A/B/Boot 与 flash link contract 通过，package/build 产物均在 `out/`。
  - `tools/ram_budget_check/ram_budget_check.py`：`link_free_bytes=98348 B`，达到 `RTOS_HAOFV_TODO.md` 的 `98304 B` 发布门禁。
  - 全量 pytest 输出到 `out/pytest/dpll-p0-shared-sync-workspace-20260828/junit.xml`，结果为 `506 passed, 1 skipped`。
  - 本轮尚未执行五板 OTA、板端 heap/stack 水位、NO1–NO4 TDMA 长稳或 NO5 观测，因此不宣称 P1 及后续阶段完成。
- 还需完成：
  - 按工具既有流程完成 send-only → no-commit boot → skip-boot commit 的异步 OTA，并逐板读取 `SYST:FW:BUILD?`、`SYST:CORE?`、`SYST:RTOS:STAT?`、TDMA/VDC/DPLL 状态。
  - 通过 P1 后再加载 active calibration matrix，进入 P2/P3；硬件 evidence 和 eligible gate 完整前禁止 DPLL servo/`LOCKED` 结论。
- 关联文件：
  - `docs/tdma/TDMA_DOMAIN_TODO.md`
  - `components/sync_io/src/sync_io_core_internal.h`
  - `components/sync_io/src/sync_io.c`
  - `components/sync_io/src/sync_io_model_sched.c`
- 下一步：执行 P0 板端 OTA/HIL；失败时保留证据并停留在当前阶段。

### TDMA-PROGRESS-20260828-009 - DPLL P0 SRAM 门禁通过

- TODO task ID：`TDMA-HIL-001`、`TDMA-DPLL-001`、`TDMA-DPLL-002`。
- 日期：2026-08-28。
- 变更：FreeRTOS heap 静态配置调整为 96 KiB；维护态任务栈收敛；TDMA coded/marker/
  data-training TX/RX workspace 按 persona 互斥关系合并为每方向一个 union。未改变 TDMA
  SHORT wire、PIO 原语、Core1 phase、recovery budget 或 calibration matrix。
- 构建：`tools/cmake_build_auto/cmake_build_auto.py` 输出到
  `out/build/dpll-p0-cal-workspace-20260828/`；A/B/Boot 和 flash link contract 通过。
- RAM：`tools/ram_budget_check/ram_budget_check.py` 报告 `link_free_bytes=98356 B`，相对
  `98304 B` 门禁余量 52 B，P0 静态 gate PASS。
- pytest：`out/pytest/dpll-p0-cal-workspace-20260828/junit.xml`，全量结果
  `506 passed, 1 skipped`；跳过项为未提供 COM 端口的 HIL。
- OTA/HIL：尚未执行。继续保持 fail-closed，先做异步 OTA、板端 heap/stack 水位、
  `SYST:CORE?` heartbeat 和 NO1–NO4 TDMA 基线；NO5 仅做环外观测。

### TDMA-PROGRESS-20260828-008 - RefMem staging maintenance pool

- TODO task ID：`TDMA-HIL-001`、`TDMA-DPLL-001`、`TDMA-DPLL-002`。
- 日期：2026-08-28。
- 变更/提交：代码提交 `f8e20de`，已推送；仅涉及 RefMem 维护态 staging 生命周期，不改变
  TDMA SHORT wire、Core1 phase、PIO 原语或 calibration matrix。
- 结果：内联镜像和 SD package 读取共用 registry-owned staging buffer；owner lease 为非阻塞
  且单一持有者，失败清空、成功提交后保留。A/B/Boot 构建和 flash link contract 通过。
- 验证：pytest 结果为 `506 passed, 1 skipped`，证据在
  `out/pytest/dpll-p0-staging-pool-20260828/junit.xml`。RAM 检查结果为
  `link_free_bytes=65640`，低于 `RTOS_HAOFV_TODO.md` 的 96 KB 发布门禁，故未 OTA/HIL。
- 下一步：完成 Storage/OTA staging 生命周期和任务栈/heap 水位复核；SRAM gate 通过后再执行
  五板异步 OTA、NO1–NO4 基线和 NO5 DPLL 观测。

### TDMA-PROGRESS-20260828-004 - DPLL 作为固定 process-image 负载

- TODO task ID：`TDMA-PAYLOAD-002`、`TDMA-DPLL-001`、`TDMA-HIL-002`。
- 日期：2026-08-28。
- 变更/提交：代码提交 `c027bf5`，工具说明提交 `3e6883e`，均已推送。
- 完成内容：
  - 删除 reference 每隔固定周期以独立 `IDLE_BEACON` clock-evidence 替换 process image 的路径。
  - 固定 SHORT payload 改为 Node image 加全局 DPLL observation trailer；frame N 的 trailer 携带
    frame N-1 的 reference TX hardware latch，sequence 由当前 transport header 隐式关联。
  - DPLL disabled 只清 trailer valid bit；enabled/disabled 的 payload class、flags、wire length、
    transport sequence 和 PIO physical byte count 保持一致。
  - 增加模 timestamp 回绕重建、固定帧型 A/B、预算工具和编译断言。
- 构建/测试：host adapter 单测通过；TDMA/TRN-03 定向 pytest `79 passed`，工具回归
  `68 passed`；`pico2-rtos-multicore-smoke` A/B/Boot 构建通过，build 快照为
  `20260828035648`，package CRC 快照为 `0x4C12155F`。
- OTA/HIL：NO1–NO4 四板异步 OTA 全部 PASS；NO5 未加入 TDMA/RefMem 环路。启用 DPLL fixed
  load 的四板 process-image 运行中，各 Node `ring_adapter_rx_bad_count` 与
  `ring_adapter_rx_transport_bad_count` 增量均为零，证明旧的周期性 transport CRC 失配已消失。
- 未通过项：整体 HIL 仍为 FAIL。NO1–NO3 存在 `receive_bitmap_incomplete` / receive reject；NO4
  通过 process-image gate。SD raw capture 在 NO1 返回 latch timeout，因此本轮没有生成波形分析。
  这些失败与 clock-evidence 插帧已解耦，不能据此宣称 `TDMA-PAYLOAD-002` 或 `TDMA-HIL-002`
  完成。
- 下一步：保持固定 wire plan 不变，定位 NO1–NO3 mailbox presence 的间歇缺失，并单独修复
  NO1 SD capture latch；随后重跑四板长稳和 DPLL eligible observation/VDC lock gate。

### TDMA-PROGRESS-20260828-003 - DPLL/VDC 阶段性闭环与非阻塞物理提交

- TODO task ID：`TDMA-DPLL-001`、`TDMA-DPLL-002`、`TDMA-DET-004`、`TDMA-HIL-002`。
- 日期：2026-08-28。
- 变更/提交：固件与工具提交 `b2083f6`，已推送到 `feature/rtos-multicore-haofv`。
- 完成内容：
  - flight-origin TX 改为提交即返回；PIO stall、DMA 完成和 clock-latch 在后续 core1 service 中回收，TDMA phase 不等待线缆、隔离器或整段 wire。
  - process-image overlay 使用双 resident buffer；解析结果写入非活动 buffer，DMA 忙时只记录一个 pending successor，由 frame-boundary service 有界提交，禁止覆盖活动脚本。
  - ring adapter 增加 TX completion callback，把延迟硬件 timestamp 绑定回原 sequence/identity CRC；DPLL/VDC 只消费该硬件证据。
  - `dpll_vdc_monitor` 固化 NO1..NO8/NO5 只读采样、触发间隔、同时反馈、DPLL/VDC vector 和 CSV/JSON/SVG 输出，并修复 standalone 入口。
- 构建/测试：`cmake --build out/build/dpll-vdc-20260828 --parallel 4` 通过，生成 `DHRT100_FACTORY.uf2` 和 `DHRT100_UPDATE.pkg`；定向 pytest `75 passed`；全量 pytest `488 passed, 1 skipped`，保留既有 `test_flash_inventory` raw caller 数量失败（10 对历史期望 9）。
- OTA/HIL：使用 `tools/ota_multi_update/ota_multi_update.py` 对发现的五板异步 OTA，全部提交并重启到 build `20260827203326`。随后用 `tools/tdma_ring_monitor/tdma_start_ring.py` 启动 NO1..NO4（BOARD:NO 顺序为 COM3/COM4/COM6/COM5），4096 个训练周期完成；最终四板 `up_running=1`、`down_running=1`、`rx_bad=0`，参考节点 `simultaneous_feedback_loop_evidence=1`。
- DPLL/VDC 结果：`tools/dpll_vdc_monitor` 采集 NO1..NO5 报告 `out/dpll-vdc-monitor/post-ring-20260828/summary.json` 和 `summary.svg`。TDMA 环路稳定，但所有节点 `timestamp_eligible=false`、DPLL `CHECKING`；当前启动会话尚未加载有效 active Calibration `PATH_DELAY`，因此不能宣称 DPLL lock 或 VDC 正式发布。
- 下一步：加载并校验 Calibration active path-delay/topology/generation，确认每个同圈 sequence 的 TX/RX latch 能进入 eligible sample；再以 NO5 只读观测验证触发间隔、同时触发和 VDC vector 发布，最后执行 SD 原始波形分析与五板长稳。

### TDMA-PROGRESS-20260828-001 - 拍级 schedule 与基础载荷预算

- TODO task ID：`TDMA-DET-001`、`TDMA-DET-002`、`TDMA-DET-003`、
  `TDMA-PAYLOAD-001`..`TDMA-PAYLOAD-005`。
- 日期：2026-08-28。
- 变更/提交：代码提交 `5582877`。
- 构建或验证：pico2 release A/B 与 OTA package 构建通过，package CRC 快照为
  `0xEF3AA1C4`（快照，非事实源）；TDMA/TRN-03 定向 pytest `89 passed`（快照，非事实源）。
- 结果：基础字段优先装入固定 Node body；mandatory 后的唯一静态余量用于诊断摘要，运行态
  无 opportunistic 空间。SCPI 在旧字段后追加 layout/VDC/ACK/control/CRC evidence。
- 证据位置：`out/tdma_process_image_budget/tdma_process_image_budget.md`、
  `out/build/pico2-release/DHRT100_UPDATE.pkg`、`out/pytest/tdma-mandatory-load/`。
- 下一步：完成全量定向回归与文档门禁，再进行五板异步 OTA/HIL。

### TDMA-PROGRESS-20260828-002 - 基础负载优先与次优先负载准入

- TODO task ID：`TDMA-M1`、`TDMA-M2`、`TDMA-DET-003`、`TDMA-PAYLOAD-001`、`TDMA-PAYLOAD-005`。
- 日期：2026-08-28。
- 变更/提交：`d656718`（预算与 Sync Trigger 基础负载闭环）；工具恢复提交为
  `cbbd791`、`88145d0`。
- 结果：固定 Node mailbox 和拍级 Core1 schedule 已按 mandatory-first 运行。本轮运行快照中的
  基础掩码为 `0x5B`（VDC、DPLL、SYNC_CAPTURE、REFMEM、SYNC_TRIGGER）；CALIBRATION、MODEL、
  TRIGGER_MEASURE 作为次优先负载默认关闭。Node mailbox 仍无 runtime-free 字节，mandatory
  后仅保留静态诊断容量；不得临时扩帧、追加第二帧、借用 guard 或抢占其他 Node 段。
- 构建/验证：release A/B 构建、定向 pytest、release check 和 pre-commit 均通过；五板统一
  OTA 全部通过，`enabled_mask=91`、`quarantined_mask=0`、`schedule_miss_count=0`，两轮
  schedule snapshot 的运行计数单调增加。
- 证据位置：`out/tdma_process_image_budget/current.md`、
  `out/tdma_cycle_schedule/foundation_sync_trigger21.md`、
  `out/ota/tdma_foundation_sync21_final_20260828/summary.json` 及各 Node 的
  `*_schedule_verify*.txt`。
- 边界：本轮证明的是确定性基础负载和准入闭环，不等同于硬件边沿 latch、DPLL 正式锁相或
  active calibration。次优先负载只有在新的静态 profile 明确给出完整 WCET、wire 和 guard
  余量后才能启用。
- 下一步：在不改变基础 schedule 的前提下补齐 SD 原始波形的频率/占空比长期证据，然后按
  phase 单独加载待办负载并执行每轮 OTA/HIL 回归。

### TDMA-TASK-20260825-001 - PIO-SPI raw-flight persona 运行路径

- 状态：完成代码接通、host 验证和固件构建；四板 raw-flight HIL 与 process-image replacement 未完成。
- 日期：2026-08-25
- 任务目标：把 follower 的短帧 wire forwarding 从“完整 RX 后由 core1 再发一次”迁到
  role-specific PIO/DMA flight persona，并保持现有 capture/SCPI 工具可诊断。
- 完成内容：
  - `tdma_pio_spi_phys_arm()` 按 reference/follower 选择 flight persona；reference 使用 DMA
    DATA、有限 CS/SCK burst 和回环 RX capture，follower 在 PIO 内执行 SCK 再生、DATA 流水与 RX capture。
  - adapter 增加 store-forward/physical-flight 模式；产品 runtime 使用 physical-flight，host
    fake phys 保留 store-forward，follower 不再产生重复 software TX。
  - 物理 RX scanner 增加 bit-shift magic recovery；PHYS 查询追加 persona 和 program switch 证据。
  - TRN-03B 工具拆分 `raw-flight` 与 `process-image`，避免透明 wire flight 被误报为完整
    process-image flight。
- 验证结果：ring adapter host 单测、TRN-03 Python 定向回归和 pico2 release 固件构建通过；
  输出分别位于 `out/pytest/build-tdma-flight-ring-adapter`、
  `out/pytest/trn03-flight-runtime-temp` 与 `out/build/trn03-flight-runtime`。
- 还需完成：四板异步 OTA 后先执行 raw-flight 门禁并保存 SD capture/SVG；通过后再设计
  fixed segment 在线替换、elastic buffering、WKC 和尾部 CRC。
- 关联文件：
  - `components/tdma/src/tdma_pio_spi.pio`
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `components/tdma/src/tdma_pio_spi_ring_adapter.c`
  - `tools/calibration_ring_validate/trn03_closed_loop.py`
- 下一步：只运行 `trn03_closed_loop.py --stage raw-flight`，不提前宣称 process-image 完成。

### TDMA-TASK-20260820-001 - 八槽 RX 位图快路径

- 状态：完成首版代码、主机/ARM 验证和两板 HIL；待 3..8 板扩展验证及契约交叉审核。
- 日期：2026-08-20
- 任务目标：按最大 8 板统一 process image，把 core0 RTOS 从无变化 slot 的快速过滤路径移出，同时保持单 writer、多方读取和 FIFO 非阻塞语义。
- 完成内容：
  - SHORT process image 固定为 8 × 32 B；每 slot 前 8 B 为 `magic/version/source_slot/target_mask/seq16` 快速头，后 24 B 对 core1 opaque。
  - core1 只扫描 remote slot 快速头并生成 `input_segment_mask`；core0 RefMem 只解析命中 slot，空 mask 不再回退全帧扫描。
  - TX/RX descriptor 同时校验 `slot_index/generation/sequence`；损坏项被丢弃并推进 ring，不阻塞 wire path。
  - RX seq16 去重改为 classify/commit 两阶段：RX FIFO 发布成功后才提交，FIFO 满时同一 mailbox 可以重试。
  - `SYSTem:TDMA:FLIGHT:PROCess?` 追加 bitmap scan/hit/duplicate 计数。
  - 新增只读 `SYSTem:REFMEM:SYNC:FLIGHT?` 和 `tools/tdma_ring_monitor/flight_bitmap_validate.py`，按 `*IDN?` 唯一地址完成 2..8 板 core1/FIFO/core0 计数关联验收。
- 验证结果：
  - MinGW host unit test scripts 27/27 通过，包含 flight FIFO、payload registry、ring runtime、service scheduler 和 PIO SPI ring adapter。
  - 双核 FreeRTOS A/B、Factory UF2 与 OTA package 构建通过。
  - 两板均通过 `*IDN?` 唯一地址识别，ring 上下行进入 running，`ring_adapter_rx_bad_count=0`。
  - 30 s 位图闭环中，转发节点 `map_apply +14921`、`bitmap_hit +14921`、`refmem_rx_accept +14922`；参考节点按设计不执行 map apply，`bitmap_hit +14916`、`refmem_rx_accept +14914`。
  - 两板 `fifo_rx_drop=0`、`refmem_rx_reject=0`、`refmem_rx_bad=0`、`bitmap_duplicate=0`。
  - 文档 strict names 和 doc regression 均通过。
- 后续验证：扩展到 3..8 板，并在 1 ms/100 us/10 us 周期和更高 SPI 速率下记录负载、FIFO 水位、端到端延迟与丢弃计数。

### TDMA-TASK-20260819-002 - EtherCAT-style fixed-offset flight processing

- 状态：完成 V1 完整帧软件 flight engine；未宣称 PIO/DMA 真正 cut-through。
- 日期：2026-08-19
- 任务目标：在严格 HAOFV 边界下，把 core0 发布的 active TX image 与 core1 的 cyclic frame boundary 连接起来，用冻结的 `TdmaProcessImageMap` 执行本地 input/output block 交换。
- 完成内容：
  - 新增 `tdma_flight_engine`，只依赖 `TdmaProcessImageMap` 和 `tdma_flight_fifo`，不调用 VDC、RefMem 或 Trigger 解码器。
  - map 只能在 ring STOP 状态由 TDMA service 配置；adapter START 时按 local slot 激活，RUN 中不允许改 offset。
  - follower 收到 `CYCLIC_PROCESS_IMAGE + FLIGHT_MUTABLE` 后复制完整 payload，提取本地 input mask，并替换本地 `FLIGHT_WRITE` 固定段；随后使用现有 V1 API 更新 transport CRC 和 hop。
  - active ring 明确限制为 2 至 8 节点；reference `hop_limit=node_count-1`，8 节点部署对应 7 hop。
  - TX image 在同一 frame 的所有本地 output segment 中复用同一个 generation；没有新 generation 时复用上一版，首帧无 active image 时原样旁路并记录 `TX_UNAVAILABLE`。
  - RX FIFO 满时只丢弃 core0 mirror，不影响 wire forward；新增 `SYSTem:TDMA:FLIGHT:PROCess?` 查询 map/byte/counter 证据。
- 验证结果：
  - `run_tdma_pio_spi_ring_adapter_tests.ps1` 通过，覆盖多 segment replacement、nonlocal byte preservation、generation reuse、长度拒绝和 RX mirror 满队列继续 forwarding。
  - `run_tdma_process_image_map_tests.ps1` 通过。
  - `RP2350_TRIG_UPDATE.pkg` 已用 OTA 写入 COM3 并重启/commit；build `20260819152706`，package CRC32 `0x81C762C4`。
  - COM3 `TRAIN 4096` + `START` + 25 MHz 单板回环通过；坏帧、物理 RX 错误、ring overrun 增量均为 0。`TIMESTAMP_MISSING` 仍符合尚未接硬件 edge latch 的架构边界。
- 还需完成：
  - 将正式 System Pack/DeploymentGate process-image map、generation/dirty/target/segment CRC 头接入 TDMA owner。
  - 实现尾部 CRC/WKC V2，并在 PIO/DMA 上证明 RX/TX overlap、固定 hop pipeline delay、无 underflow/overrun 后，才可称为严格 cut-through flight mode。
  - 接入硬件 TX/RX edge timestamp latch。
- 关联文件：
  - `components/tdma/inc/tdma_flight_engine.h`
  - `components/tdma/src/tdma_flight_engine.c`
  - `components/tdma/src/tdma_pio_spi_ring_adapter.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
- 下一步：先定义正式 System Pack process-image table 的 wire contract，再把 map staged/active 双缓冲接入 DeploymentGate；不要在运行态开放串口 map 写入口。

## 记录规则

每条任务记录使用以下格式：

```text
### TDMA-TASK-YYYYMMDD-NNN - 标题

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

TDMA Domain 当前目标是把公共 `components/tdma` 从 RefMem/VDC 之间的隐式基础件，升级为 HAOFV 下的显式 foundation domain：

```text
TdmaSchedulerAO
+ TdmaRuntimeFB
+ TdmaPayloadRegistry
+ TdmaRingRuntime
+ TdmaQualityVector
+ TransportAdapter
```

VDC 消费 observation evidence，RefMem 消费 data/completion evidence，二者都不拥有上/下行 TDMA 环路。

## 任务记录

### TDMA-TASK-20260819-003 - TDMA service 接管 flight FIFO 只读快照

- 状态：完成 `tdma_service` 级 FIFO 挂载、只读 snapshot 入口和编译门修正；resident ring 行为不变。
- 日期：2026-08-19
- 任务目标：
  - 把 `tdma_flight_fifo_t` 作为 TDMA owner 的内部固定池对象纳入 `tdma_service` 生命周期。
  - 只提供 snapshot 读回，不改变现有 ring runtime/adapter fast path。
  - 让编译/测试脚本明确把 `tdma_flight_fifo.c` 作为 `tdma_service.c` 的联动源文件。
- 完成内容：
  - `tdma_service_service_t` 新增 `flight_fifo` 成员，`tdma_service_init()` 内部执行 `tdma_flight_fifo_init()`。
  - 新增 `tdma_service_get_flight_fifo_snapshot()`，由 TDMA owner 统一暴露 FIFO 状态。
  - `run_tdma_service_scheduler_tests.ps1`、`run_refmem_realtime_tdma_tests.ps1`、`run_vdc_domain_tests.ps1` 补齐 `tdma_flight_fifo.c` 联动编译。
  - `test_tdma_service_scheduler.c` 增加 FIFO snapshot 断言，确认 TDMA owner 已持有该基础件。
- 验证结果：
  - `run_tdma_service_scheduler_tests.ps1` host 通过。
  - `run_refmem_realtime_tdma_tests.ps1` host 通过。
  - `run_vdc_domain_tests.ps1` host 通过。
  - `run_host_unit_tests.ps1` 全量通过 `27/27`。
- 还需完成：
  - 在 core1 cyclic frame boundary 接入 TX acquire/reuse，在 frame end 接入 RX publish。
  - 之后再把 FIFO snapshot 挂到 SCPI/diagnostic path 的适当位置，继续保持只读和 owner 单写规则。
- 关联文件：
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tools/tests/run_refmem_realtime_tdma_tests.ps1`
  - `tools/tests/run_vdc_domain_tests.ps1`
  - `tools/tests/run_tdma_service_scheduler_tests.ps1`
  - `tests/unit/test_tdma_service_scheduler.c`
- 下一步：
  - 保持 resident ring 不变，把 FIFO 接入 core1 flight engine 的 frame 生命周期。

### TDMA-TASK-20260819-002 - COM3 单板 TDMA 闭环烧录与训练验证

- 状态：完成 OTA 烧录、boot/commit、单板 ring 准备流程自动化和 25 MHz 单板回环验证。
- 日期：2026-08-19
- 任务目标：
  - 将当前 HAOFV 双 FIFO 基础件固件烧录到产品样板，并跑单板 RJ45 输出回接输入验证。
  - 把单板闭环工具从只读检查升级为包含 `STOP -> LOCAL -> ARM -> TRAIN -> START` 的完整准备流程。
  - 保持 HAOFV evidence 边界：电气/数据回环 PASS 不冒充正式 hardware timestamp closed-loop evidence。
- 完成内容：
  - `ota_multi_update.py` 对 COM3 产品样板升级 `RP2350_TRIG_UPDATE.pkg`，板端 build 从 `20260819123859` 切到 `20260819130134` 并完成 commit。
  - `tdma_single_board_loopback.py` 默认自动执行 ring setup：`STOP`、`LOCAL 0`、`ARM`、`TRAIN 4096`、`START`，并在 summary 中记录每步响应和 armed/started 状态。
  - 新增工具参数 `--skip-ring-setup`、`--local-slot`、`--train-cycles`、`--arm-wait`、`--start-wait`。
  - COM7 调试串口已接入并识别；本轮控制和验证仍只使用 COM3 USB CDC，避免占用调试 UART。
- 验证结果：
  - OTA package：`build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`，build id `20260819130134`，package CRC `0x86934747`。
  - 单板自动流程输出目录：`build-rtos-multicore-smoke/tdma_single_board_loopback_20260819_auto`。
  - 自动流程确认：`ARM` 后 `ring_enabled=1`、`adapter_started=1`、up/down 尚未跑；`START` 后 `up_running=1`、`down_running=1`。
  - 15 s / 25 MHz 回环 PASS：`ring_seq +14963`，`up_tx_sequence +7482`，`down_rx_sequence +7482`，`adapter_tx_count +7482`，`adapter_rx_count +7482`。
  - `adapter_rx_bad_count`、`phys_rx_bad_count`、`phys_rx_magic_fail_count`、`phys_rx_ring_overrun_count` 增量均为 0。
  - `ring_last_error=5(TIMESTAMP_MISSING)`、`simultaneous_feedback_loop_evidence=0`，符合当前未接 PIO/DMA 边沿硬件 timestamp latch 的边界。
- 还需完成：
  - 将 `TDMA_TX_IMAGE_FIFO`/`TDMA_RX_FRAME_FIFO` 接入 core1 flight engine 的 frame boundary/frame end。
  - 补真实 PIO/DMA edge timestamp latch，满足 `timestamp_resolution_ns <= 100` 且 `HARDWARE_LATCHED` 后再要求 `simultaneous_feedback_loop_evidence=1`。
  - 后续在双板/多板上验证 `TRAIN`、START 顺序、长线缆和干扰条件。
- 关联文件：
  - `tools/tdma_ring_monitor/tdma_single_board_loopback.py`
  - `build-rtos-multicore-smoke/tdma_single_board_loopback_20260819_auto/summary.json`
  - `build-rtos-multicore-smoke/ota_closed_loop_20260819/summary.json`
- 下一步：
  - 在不破坏现有 resident ring 的前提下，将双 FIFO 挂入 TDMA owner/flight engine，并补相应 SCPI snapshot。

### TDMA-TASK-20260819-001 - HAOFV 双 FIFO 基础件首版

- 状态：完成 core0/core1 双 FIFO 基础件、单元测试、ARM 编译门和固件构建；尚未接入 PIO/DMA 飞行引擎 fast path。
- 日期：2026-08-19
- 任务目标：
  - 按 HAOFV/TDMA Foundation 架构先实现 core0/core1 之间的两个 SPSC FIFO。
  - `TDMA_TX_IMAGE_FIFO` 支持 core0 提前发布完整 TX image，core1 在 frame boundary 锁定完整 generation，无新 generation 时复用上一版。
  - `TDMA_RX_FRAME_FIFO` 支持 core1 非阻塞发布 RX frame/input slice 镜像，满队列或缓冲池耗尽时只丢弃 core0 解析副本并增加计数，不影响 wire forwarding。
- 完成内容：
  - 新增 `tdma_flight_fifo` 模块，使用固定 TX 双缓冲、RX 固定缓冲池和 descriptor ring，不使用动态内存、mutex、RTOS 阻塞队列或 core0 ACK。
  - 冻结 TX ownership：`CORE0_INACTIVE -> CORE0_FILL -> CORE0_READY -> CORE1_ACTIVE -> CORE0_INACTIVE`。
  - 冻结 RX ownership：`FREE -> CORE1_FILL -> CORE0_PARSE -> FREE`。
  - snapshot 采用架构术语 `tx_image_stale_count` 和 `rx_mirror_drop_count`，并保留旧字段别名用于过渡。
- 验证结果：
  - Vivado/AMD MinGW GCC `D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin\gcc.exe` host 单元测试通过：`tdma_flight_fifo tests passed`。
  - `run_tdma_process_image_map_tests.ps1` ARM 编译门通过。
  - `run_tdma_ring_runtime_tests.ps1` ARM 编译门通过。
  - `pico2-rtos-multicore-smoke` 固件构建通过并生成 `RP2350_TRIG_UPDATE.pkg`。
- 还需完成：
  - 将 `tdma_flight_fifo_t` 挂到 TDMA owner/flight engine 生命周期中，只允许 TDMA Foundation 暴露 snapshot 和受控 publish/acquire API。
  - 在 core1 cyclic frame boundary 接入 TX acquire/reuse，在 frame end 接入 RX publish，保持 core1 不调用 VDC/RefMem/Trigger 业务解码器。
  - 后续再补 PIO/DMA 固定 offset 飞行替换、尾部 CRC/WKC 和硬件 timestamp latch。
- 关联文件：
  - `components/tdma/inc/tdma_flight_fifo.h`
  - `components/tdma/src/tdma_flight_fifo.c`
  - `tests/unit/test_tdma_flight_fifo.c`
  - `tools/tests/run_tdma_flight_fifo_tests.ps1`
  - `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
- 下一步：
  - 按 HAOFV owner 边界把 FIFO 接入 TDMA runtime，而不是让 VDC、RefMem 或 Trigger 域直接触碰跨核缓冲。

### TDMA-TASK-20260818-008 - 产品样板 IO 首轮迁移

- 状态：软件映射、资源隔离、LCD 开机横屏板测和 COM3 OTA 完成；主页 UI 与其余产品 IO 待继续验证。
- 日期：2026-08-18
- 任务目标：
  - 按 `RP2350B_QFN80_IO_CONSTRAINTS` 将现有 TDMA PIO-SPI 与板级 IO 迁移到产品样板。
  - 暂时将 BiSS + RJ45 Trigger 作为三线 SPI 使用，其中 RJ45 Trigger 作为 CS/frame-sync，复用既有 PIO 实现。
  - 接入 3 个 KEY、3 个 LED、4 路 SMA 输入和 4 路 SMA 输出，并避免旧调试 persona 抢占产品 IO。
- 完成内容：
  - TDMA 改用 PIO2 SM0/SM1：TX `CS=GPIO26/SCK=GPIO25/TX=GPIO29`，RX `CS=GPIO27/SCK=GPIO28/RX=GPIO24`；保持现有 CS/frame-sync PIO 协议，初始默认 10 MHz。
  - ISO1452 控制迁移到 `DE=GPIO30/31/32`、`/RE=GPIO40/41/42`；上电关闭 driver、使能 receiver，TDMA arm 完成 PIO 配置后才开启 driver。
  - KEY1/2/3 映射到 GPIO2/6/7，低有效并上拉；LED SYSTEM/ARM/FAULT 映射到 GPIO3/8/9。
  - 板载 CH343 调试 stdio 迁移到 UART0 GPIO0/1；UART1 GPIO4/5 与 DE GPIO13 保留给外部 RS485，默认接收。
  - SMA OUT1..4 映射 GPIO16..19；SMA IN4..1 映射 GPIO20..23。采集边界对每个 4-bit sample 做 bit reverse，使公共 mask bit0..3 始终表示逻辑 IN1..4。
  - SEQ gated PIO 移除 GPIO16..19 和 gate offset 3 的硬编码，按产品输入组与 `GATE_IN=GPIO20` patch 实际 wait-pin offset。
  - 禁用占用 PIO2/GPIO24..29 的 legacy AUX/BiSS tap/sync clock/RJ45 marker persona；相关兼容 API 保留但返回不可用。
  - 禁用占用 GPIO4..7、会与 UART1/KEY2/KEY3 冲突的 legacy debug-model overlay。
  - TF 迁移到 SPI1 GPIO10..12/15（card detect GPIO14）；LCD 迁移到独立 SPI0 GPIO34..39，并按样板新规格改为 ST7735S、原生 RAM `80x160`、offset `(24,1)` 与硬复位。
  - 产品样板 COM3 已确认 TF 卡 `CARD_READY`、SDHC/SDXC、FAT 挂载、目录/INFO/64 B 读回和递增 boot snapshot 写入；StorageAO 的资源 claim 从旧 `SPI0|SD` 收敛为产品板专用 `SD`（SPI1），不再与 LCD SPI0 互斥。
  - ST7735S 的硬件 `MV` 横屏在 offset `(1,26)` 和 `(1,24)` 下均出现逐行回绕斜切，因此控制器保持已验证稳定的原生竖屏扫描；刷新层将逻辑 `160x80` UI 顺时针软件旋转写入 `80x160` RAM。开机动画已适配横屏并经产品样板确认完整、无斜切；旧主页仍沿用大屏坐标，显示不全已转入正式待办。
  - 新增三键纯事件层：35 ms 去抖、press/release、short、700 ms long 和 250 ms repeat；UI 产品路径切换为 160x80 单卡片四行布局，KEY1 上一页/长按返回、KEY2 详情、KEY3 下一页/长按连翻。
  - 新增 `tdma_single_board_loopback.py`，只读检查单板 RJ45 输出回接输入后的预期频率/pin profile、UP/DOWN、sequence、TX/RX、坏帧和 overrun 增量；电气/数据 PASS 与硬件 timestamp feedback evidence 分层报告。
  - 产品 W25Q128JV 容量固定为 16 MiB；为兼容现有 bootloader/OTA 元数据，A/B 分区暂保持在低 4 MiB，剩余 12 MiB 待版本化分区迁移后启用。
- 验证结果：
  - `build-rtos-multicore-smoke` A/B 固件链接和 update package 均成功；软件横屏最终 build id `20260818141125`，package CRC `0x294FCB21`。
  - `ota_multi_update.py` 对产品样板 COM3 OTA PASS，板端运行 build `20260818141125`；用户确认开机界面显示正常，主页显示不全作为后续 UI 重构输入。
  - `run_tdma_profile_tests.ps1` ARM/host 测试通过。
  - `run_tdma_pio_spi_ring_adapter_tests.ps1` ARM/host 测试通过。
  - 三键 UI build `20260818151639`、package CRC `0x643A2D6A` 已通过 COM3 OTA boot/commit。
  - 未接回环网线的 3 s 基线：profile 为 TX CS/SCK/DATA `26/25/29`、RX `27/28/24`、10 MHz；TX sequence 增长 1116，RX 保持 0，工具按预期报告 `down_running=0`。
  - 产品样板 build `20260818154324` 单板网线回环连续两个 15 s 窗口通过电气/数据层验证：
    第二轮 TX/RX 均增长 7230 帧，UP/DOWN 均运行，adapter/phys bad、magic fail 和
    overrun 全部零增长。`ring_last_error=5(TIMESTAMP_MISSING)` 与
    `simultaneous_feedback_loop_evidence=0` 符合当前 diagnostic-only timestamp 边界，
    不作为 CS/CLK/DATA 回环失败，也不冒充正式时间戳闭环证据。
  - 产品差分回环完成 `15/20/25 MHz` 阶梯：15 MHz build `20260818154958`
    为 7344/7344 帧，20 MHz build `20260818155222` 为 7272/7272 帧，25 MHz
    build `20260818155435` 的 15 s 为 7359/7359 帧；各档 adapter/phys bad、
    magic fail、overrun 均零增长。25 MHz 追加 60 s 窗口为 29721/29721 帧且
    全部错误仍为 0。当前样板保留 25 MHz 测试固件，干净构建默认仍为 10 MHz，
    待长线缆、干扰和多板 HIL 后选择 20 或 25 MHz 量产档。
- 还需完成：
  - 产品样板已确认三个 LED 均为低有效；ST7735S 背光 GPIO35 已确认低有效。
  - 逐项实测 KEY、SMA OUT1..4、SMA IN1..4 的通道编号和输入反序，再进行 TDMA 单跳与闭环 HIL。
  - 实测 ISO1452 DE 与 `/RE` 时序，确认空闲、arm、disarm 和复位期间不存在总线争用。
  - 产品样板人工确认 160x80 页面无裁切，并逐项确认三个按键短按/长按/重复的方向和手感。
  - 单板 CS/CLK/DATA 电气与帧回环已通过；后续补 PIO/DMA 边沿硬件 timestamp latch，
    再验收 `simultaneous_feedback_loop_evidence=1` 和正式 round-trip correlation。
- 关联文件：
  - `boards/rp2350_trig/inc/board_config.h`
  - `boards/rp2350_trig/src/board.c`
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `components/sync_io/src/sync_io.c`
  - `components/sync_io/src/seq_step.pio`
  - `docs/hardware/RP2350B_QFN80_IO_CONSTRAINTS.md`
- 下一步：
  - 继续产品样板 KEY/SMA GPIO 冒烟验证，再接 BiSS/RJ45 线做 TDMA CS/CLK/TX 逻辑分析仪验证；主页 UI 按三键、小屏单卡片方案另行重构。

### TDMA-TASK-20260818-007 - PIO-SPI 外层包头假锁修正与 60 s HIL 回归

- 状态：完成根因定位、代码修正、host/unit 全量验证、A/B 构建、两板 OTA 和 60 s HIL 回归。
- 日期：2026-08-18
- 任务目标：
  - 解决 10 MHz / 500 Hz 长时间运行时偶发 adapter `RX_BAD_FRAME` 和 feedback RX 低于最佳基线的问题。
  - 保持当前 CS+DATA+CLK 三线单向腿和 500 Hz reference baseline 不变，只修正物理层切帧假锁。
  - 继续保持 timestamp evidence 边界：当前 timestamp 仍为 diagnostic-only，不置位 closed-loop evidence。
- 完成内容：
  - 定位到外层 PIO-SPI packet magic `54 44` 与内层 `TdmaTransportFrame` magic `54 44` 相同；当连续 DMA 扫描指针错过真实外层头时，可能错误锁定到内层 transport magic，并把后续约 257 B 数据当作一帧交给 adapter，导致 transport CRC 失败并成批吞掉后续短帧。
  - `tdma_pio_spi_phys_capture_words()` 增加二级 header 校验：外层 `frame_size` 必须与内层 `TdmaTransportFrame.packet_size` 一致，且内层 magic/version/class/header_size 必须可信，才接受该 candidate。
  - 增加 `rx_magic_fail_count` 的扫描失败诊断含义：它现在记录“DMA 有新字节但未找到可信外层包头”的次数，不能单独等价为 bit-level 坏帧；adapter `rx_bad` 和 phys `rx_bad/stall/overrun` 仍是主要故障指标。
  - 更新 host 单元测试中 `tdma_pio_spi_ring_adapter` 的 500 Hz 语义：reference 节点每两个 core1 service 发一次 beacon，forward 节点使用注入 RX + TX 捕获物理桩，避免测试 stub 自回灌导致重复转发。
- 验证结果：
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 全量通过，26/26 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke --target RP2350_TRIG_UPDATE -- -j 8` 通过，build id `20260818111944`，package CRC `0x72F2FD91`。
  - `ota_multi_update.py` 对 COM5/COM6 OTA PASS，两板均运行 build `20260818111944`。
  - `ring_rate_measure.py --window-s 15`：COM5 TX `499.9/s`、feedback RX `497.7/s`；COM6 RX/TX `497.8/s`；adapter `rx_bad=0`。
  - `ring_rate_measure.py --window-s 60`：COM5 TX `500.1/s`、feedback RX `498.0/s`；COM6 RX/TX `498.4/s`；adapter `rx_bad=0`，phys `rx_bad=0`、`stall=0`、`tx_timeout=0`、`ring_overrun=0`。
- 还需完成：
  - `rx_magic_fail_count` 需要后续拆成更清晰的 `candidate_reject` / `idle_scan_miss` / `real_magic_miss`，避免维护人员误读。
  - 继续 P0.5-4/5：在 PIO/DMA 边界补真实 TX/RX edge latch，只有 non-diagnostic 硬件 timestamp 才允许进入 `simultaneous_feedback_loop_evidence`。
- 关联文件：
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `tests/unit/test_tdma_pio_spi_ring_adapter.c`
  - `tools/tests/run_host_unit_tests.ps1`
  - `tools/tdma_ring_monitor/ring_rate_measure.py`
- 下一步：
  - 以 build `20260818111944` 的 `10 MHz / 500 Hz / adapter rx_bad=0` 作为当前 TDMA resident ring 基线，进入 PIO/DMA edge latch 方案落地。

### TDMA-TASK-20260818-006 - 共享硬件 tick 诊断时间戳接入

- 状态：完成共享时钟基础件、SyncIO 复用、TDMA PIO-SPI 诊断 timestamp 接入、A/B 构建、两板 OTA 和 HIL 验证。
- 日期：2026-08-18
- 任务目标：
  - 把 `timer1/CLK_SYS` 从 `sync_io` 私有实现抽成共享 timestamp clock provider，避免 TDMA 和 SyncIO 重复初始化同一个硬件计数器。
  - 让 TDMA ring snapshot 能看到非零硬件 tick timestamp 和真实分辨率，为后续 PIO/DMA 边沿 latch 铺路。
  - 保持安全边界：阶段一 timestamp 仍为 CPU 读取诊断值，必须保留 `DIAGNOSTIC_ONLY`，不得置 `HARDWARE_LATCHED`。
- 完成内容：
  - 新增 `vdc_timestamp_clock.h/.c`，提供 idempotent `timer1/CLK_SYS` 初始化、tick 读取、tick-to-ns 转换和 resolution 查询。
  - `sync_io` 的 capture latch timebase 改为复用共享 `VdcTimestampClock`，不再私有重置 `timer1_hw`。
  - `tdma_pio_spi_phys_tx/rx` 改为从共享硬件 tick clock 读取 TX/RX 诊断 timestamp。
  - `tdma_runtime_owner_init()` 为 PIO-SPI ring adapter 设置 timestamp metadata：resolution 来自 `VdcTimestampClock`，flags 保持 `DIAGNOSTIC_ONLY`。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke --target RP2350_TRIG_UPDATE -j 8` 通过，build id `20260818104829`，package CRC `0x7CBEE2DC`。
  - `ota_multi_update.py` 对 COM5/COM6 OTA PASS，两板均运行 build `20260818104829`。
  - `ring_rate_measure.py --window-s 15`：COM5 reference TX `499.7 frame/s`、feedback RX `497.5 frame/s`；COM6 forward RX/TX `498.2 frame/s`；物理层 `phys_bad/magic_fail/shift/stall/ring_overrun` 均为 0 增长。
  - `SYSTem:REFMEM:SYNC:TDMA:STATus?`：COM5 `ring_timestamp_resolution_ns=4`、`ring_timestamp_flags=1`、reference TX / feedback RX timestamp 均非零，`simultaneous_feedback_loop_evidence=0`、`ring_last_error=TIMESTAMP_MISSING`。
- 还需完成：
  - 阶段二实现 PIO/DMA 边沿 latch：TX timestamp 应绑定 CS/frame-sync 起始边沿或首 bit 边沿；RX timestamp 应绑定接收 CS/frame-sync/首 bit 边沿，而不是 CPU 抽取完整包的时间。
  - 只有边沿 latch 验证通过后，才允许 TDMA ring adapter 设置 `HARDWARE_LATCHED` 且清除 `DIAGNOSTIC_ONLY`。
- 关联文件：
  - `components/vdc_domain/inc/vdc_timestamp_clock.h`
  - `components/vdc_domain/src/vdc_timestamp_clock.c`
  - `components/sync_io/src/sync_io.c`
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `components/tdma/src/tdma_runtime_owner.c`
- 下一步：
  - 设计 PIO-SPI frame-sync 边沿 latch：优先评估 IRQ/core1 快速采样、PIO side-set timestamp token、DMA completion timestamp 三种路径，明确哪一种能满足 `<=100 ns` evidence。

### TDMA-TASK-20260818-005 - 10 MHz / 500 Hz TDMA 环路基线回归

- 状态：完成代码修正、A/B 构建、两板 OTA 和 15 s HIL 验证；作为 VDC/DPLL 下一阶段基线。
- 日期：2026-08-18
- 任务目标：
  - 回归到 10 MHz PIO-SPI 下误差最小的 500 Hz 两板环路状态。
  - 解决 1 kHz 试验后 reference 发包节拍被 wall-clock 相位和 core1 service 抖动影响的问题。
  - 保持 HAOFV evidence 边界：允许 `up/down_running` 证明 resident ring 运行，不允许用软件时间戳伪造 `simultaneous_feedback_loop_evidence`。
- 完成内容：
  - `tdma_pio_spi_ring_adapter` 将 reference beacon 从 wall-clock cycle 奇偶门控改为 core1 service 二分频：当前 core1 TDMA service 约 1 kHz，因此 reference 稳定约 500 Hz 发帧。
  - follower 保持“收到一帧立即转发一帧”，避免批量 RX 时只转发最后一帧。
  - `distributed_refmem_service()` 在 OTA 会话中跳过 node-load auto service 和 TDMA 维护日志；`distributed_refmem_log_tdma_ring_service()` 增加 OTA active 静默门，避免 CDC OTA 响应污染。
  - 保留 RX 连续 DMA ring、magic+length 扫描、DMA channel 4 和 `SYSTem:SYNC:VDC:TDMA:PHYS?` 诊断字段。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke --target RP2350_TRIG_UPDATE -j 8` 通过，build id `20260818101157`，package CRC `0x354CA0F3`。
  - `ota_multi_update.py` 对 COM5/COM6 OTA PASS，两板均运行 build `20260818101157`。
  - COM6 通过 `SYSTem:TDMA:RING:LOCAL 1` 切为 forward slot1。
  - `ring_rate_measure.py --window-s 15` 干净窗口结果：COM5 reference TX `499.7 frame/s`、feedback RX `498.1 frame/s`；COM6 forward RX/TX `499.2 frame/s`。
  - 物理层 `phys_bad=0`、`magic_fail=0`、`shift=0`、`stall=0`、`ring_overrun=0`，core1 loop 约 `999 frame/s`。
  - 当前 `ring_last_error=5`，即 `TIMESTAMP_MISSING`；这是预期状态，表示尚未接入 PIO/DMA 硬件 timestamp latch。
- 还需完成：
  - P0.5-4/5：在 PIO/DMA 边界补 reference TX / feedback RX 硬件 latch，形成 sequence、identity CRC、schedule CRC、TX timestamp、RX timestamp 同一圈 ring 的闭环证据。
  - P0.5-6：长时间只读 HIL 需要输出 summary + SVG，并区分 TDMA ring runtime 与 VDC lock quality。
  - 1 kHz 升频暂不作为当前基线；待硬件 timestamp/DPLL 闭环后再评估 pipeline 最坏情况延迟。
- 关联文件：
  - `components/tdma/src/tdma_pio_spi_ring_adapter.c`
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `tools/tdma_ring_monitor/ring_rate_measure.py`
  - `docs/tdma/TDMA_DOMAIN_TODO.md`
- 下一步：
  - 进入硬件 timestamp latch 方案拆解：先只增加真实硬件时间源与诊断字段，验证无误后再允许 `HARDWARE_LATCHED` 进入 `simultaneous_feedback_loop_evidence`。

### TDMA-TASK-20260818-003 - PIO SPI 下行闲置数据线改作 FRAME_SYNC/CS

- 状态：完成代码与文档更新，A/B 构建通过，两板 OTA 与方向性丢帧 HIL 通过；正式 closed-loop evidence 仍待硬件 timestamp latch。
- 日期：2026-08-18
- 任务目标：
  - 修正 1 MHz 下 `COM5->COM6` 方向性丢帧的物理层对齐风险。
  - 将发送端未用 RX/MISO 与接收端未用 TX/MISO 的互连线改作点对点 `FRAME_SYNC/CS`，让 RX PIO 在帧有效期间采样，而不是完全依赖无 CS 连续流 + magic 扫描恢复对齐。
- 完成内容：
  - `board_config.h` 冻结当前最小系统 TDMA 三线单向腿：发送端闲置 RX/CS `GPIO21`、TX/DATA `GPIO23`、CLK `GPIO24`，连接到对端闲置 TX/CS `GPIO16`、RX/DATA `GPIO18`、CLK `GPIO19`。
  - `tdma_pio_spi_phys` 增加 `tx_csn_pin/rx_csn_pin`，TX 发帧前拉低 CS，尾部字节移出后拉高 CS；RX PIO 使用已有 `csn_pin` patch，等待 CS 有效后按 SCK 采样。
  - RX DMA rearm 前等待 `RX_CSN` 回到空闲高电平，避免在一帧 CS 低尾部清 FIFO / 重新开 DMA。
  - 保留 magic/header 扫描作为保险和诊断，不再把它作为唯一帧同步来源。
- 验证结果：
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，build id `20260818072932`，package CRC `0x15177089`。
  - `ota_multi_update.py` 对 COM5/COM6 OTA PASS，两板均运行 build `20260818072932`。
  - `SYSTem:SYNC:VDC:TDMA:PHYS?` 确认 TDMA PHYS 末尾为 `tx_csn=21, rx_csn=16`。
  - `ring_rate_measure.py --window-s 15`：COM5 reference TX `494.9/s`、RX `473.7/s`、`rx_bad=0`；COM6 forward RX/TX `473.6/s`、`rx_bad=0`。与前一版 `COM5->COM6` 约 15% 丢帧、回传方向跟随丢帧相比，方向性丢帧已明显收敛。
- 还需完成：
  - 补 PIO/DMA 硬件 timestamp latch 后重新跑 5 min HIL，让 `simultaneous_feedback_loop_evidence` 从 `TIMESTAMP_MISSING` 进入正式闭环。
  - 继续评估剩余 `~4%` TX/RX 速率差是否来自 500 Hz 发射节流、core1 service 相位、DMA rearm 窗口或后续需要的 ping-pong/ring DMA。
- 关联文件：
  - `boards/rp2350_trig/inc/board_config.h`
  - `components/tdma/inc/tdma_pio_spi_phys.h`
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `docs/tdma/TDMA_DOMAIN_TODO.md`
  - `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
- 下一步：
  - 在当前 `rx_bad=0` 基础上推进硬件 timestamp latch；若长时间 HIL 仍出现丢帧，再从“真实连续 DMA ring / DMA IRQ ping-pong / PIO frame-length RX 程序”方向继续收敛。

### TDMA-TASK-20260818-004 - PIO SPI CS/frame-sync 速率阶梯验证

- 状态：完成 2/5/10/25 MHz 构建、两板 OTA 和方向性 HIL；当前稳定档回落为 10 MHz。
- 日期：2026-08-18
- 任务目标：
  - 在 `GPIO21->16` CS/frame-sync、`GPIO23->18` DATA、`GPIO24->19` CLK 的最小系统接线下，逐步提高 TDMA PIO SPI bring-up adapter 速率。
  - 找到当前跳线环境中可作为后续 VDC/DPLL HIL 的稳定物理层速率。
- 完成内容：
  - 依次修改 `BOARD_TDMA_SPI_BAUD_HZ` 为 2 MHz、5 MHz、10 MHz、25 MHz，每一档均重新构建、OTA COM5/COM6，并设置 COM6 为 slot1 后执行同一方向统计。
  - 25 MHz 出现坏帧后，将代码回到 10 MHz 稳定档，避免后续闭环验证建立在不稳定 transport 上。
- 验证结果：
  - 2 MHz，build `20260818074001`：COM5 RX `486.6/s`、COM6 RX/TX `486.8/s`，`rx_bad=0`。
  - 5 MHz，build `20260818074327`：COM5 RX `491.5/s`、COM6 RX `491.6/s`、COM6 TX `491.0/s`，`rx_bad=0`。
  - 10 MHz，build `20260818074618`：COM5 RX `490.7/s`、COM6 RX/TX `490.9/s`，`rx_bad` 不增长；COM5 一次 core query 被日志干扰为 `-1`，不影响 TDMA 字段判断。
  - 25 MHz，build `20260818075043`：COM5 RX `452.8/s`，COM6 RX `461.8/s`、TX `452.7/s`；COM6 `rx_bad` 从 `3` 到 `25`，PHYS `rx_bad_count=4`，不作为稳定档。
- 还需完成：
  - 后续若要冲 25 MHz，需要先评估跳线信号完整性、PIO RX 采样相位、GPIO drive/slew、CS setup/hold、以及 DMA rearm 方案。
  - 10 MHz 稳定档下继续推进 P0.5-4/5 硬件 timestamp latch 和正式 `simultaneous_feedback_loop_evidence`。
- 关联文件：
  - `boards/rp2350_trig/inc/board_config.h`
  - `docs/tdma/TDMA_DOMAIN_TODO.md`
- 下一步：
  - 重新构建并 OTA 10 MHz 稳定固件到 COM5/COM6，随后进入 timestamp latch / DPLL 闭环验证。

### TDMA-TASK-20260818-002 - 两板 resident ring UP/DOWN HIL 与 freshness 语义收敛

- 状态：完成代码修正、host 全量门禁、A/B 构建、两板 OTA 和短窗口 HIL；正式 closed-loop evidence 仍待硬件 timestamp latch。
- 日期：2026-08-18
- 任务目标：
  - 回答当前 TDMA 是否已经是环路测试：每块板是否同时运行上行 RX 和下行 TX。
  - 修正 `down_running` 只表示“本轮 service 恰好收到包”的瞬时语义，避免 500 Hz 发帧 / 1 kHz service 的间隔轮把实际运行中的 DOWN leg 清零。
  - 保持 HAOFV evidence 边界：运行状态可以有 freshness，`simultaneous_feedback_loop_evidence` 仍只认新序列、CRC 和硬件 timestamp correlation。
- 完成内容：
  - `tdma_pio_spi_ring_adapter` 增加 `last_rx_service_ns`，`down_running` 改为 RX freshness window 内保持运行；收到坏帧仍撤销 DOWN running。
  - `tdma_ring_runtime` 的 closed-loop evidence 增加“DOWN RX sequence 必须相对上一轮变化”的门禁，防止 throttled round 重用上一帧 feedback。
  - 清理残留时间单位命名：`1e3ns` 代码/工具字段恢复为 `us`，SCPI 指令表中的 `last_sample_age_1e3ns` 改为 `last_sample_age_us`。
- 验证结果：
  - `run_tdma_pio_spi_ring_adapter_tests.ps1` 通过。
  - `run_tdma_ring_runtime_tests.ps1` 通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，26/26。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 2 个 risk review 文件命名 warning。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，build id `20260818063242`，package CRC `0x4A379A27`。
  - `ota_multi_update.py` 对 COM5/COM6 OTA PASS，两板均运行 build `20260818063242`。
  - `SYSTem:TDMA:RING:LOCAL 1` 将 COM6 切为 forward slot1 后，`ring_rate_measure.py --window-s 10` 显示：COM5 reference TX `493.0/s`、RX `416.3/s`、rx_bad `+1`；COM6 forward RX/TX `492.5/s`、rx_bad `+0`。
  - `tdma_ring_monitor.py --duration-s 10` 显示 COM5/COM6 均 `up_running=1/down_running=1`；COM5 仍 `simultaneous_feedback_loop_evidence=0`、reason=`TIMESTAMP_MISSING`，因为当前 `tdma_pio_spi_phys_tx()` 还未产生硬件 TX timestamp。
- 还需完成：
  - P0.5-4/5：在 PIO/DMA 边界补 reference TX / feedback RX 硬件 latch，发布 `HARDWARE_TICK / <=100 ns / HARDWARE_LATCHED` timestamp，形成正式 `simultaneous_feedback_loop_evidence=1`。
  - 将本地 slot 选择从手工 `SYSTem:TDMA:RING:LOCAL` 迁入 System Pack / SlotClaim / board UUID 派生流程，避免两板重启后都回到 slot0。
- 关联文件：
  - `components/tdma/inc/tdma_pio_spi_ring_adapter.h`
  - `components/tdma/src/tdma_pio_spi_ring_adapter.c`
  - `components/tdma/src/tdma_ring_runtime.c`
  - `components/tdma/inc/tdma_pio_spi_phys.h`
  - `components/tdma/src/tdma_pio_spi_phys.c`
  - `tests/unit/test_tdma_pio_spi_ring_adapter.c`
  - `tests/unit/test_tdma_ring_runtime.c`
  - `tools/tdma_ring_monitor/tdma_field_parse.py`
- 下一步：
  - 进入 P0.5-5：先实现 PIO SPI bring-up adapter 的硬件 timestamp latch / metadata，再跑 5 min HIL 和 DPLL lock quality SVG。

### TDMA-TASK-20260818-001 - PIO SPI ring adapter host loopback evidence 模型收敛

- 状态：完成 host 测试模型修正、host 单测、文档检查和 RTOS multicore smoke 构建；两板 HIL 待后续执行。
- 日期：2026-08-18
- 任务目标：
  - 保留 `tdma_pio_spi_ring_adapter` 真实实现中的 4x RX polling 与 500 Hz emission 相位裕量策略。
  - 修复 host loopback 测试模型无限回放同一 TX 帧的问题，避免测试把同一帧重复计入 RX evidence。
  - 覆盖 500 Hz throttled round 行为：UP ready 保持，未发新帧时 down/evidence 关闭，下一发射周期再恢复。
- 完成内容：
  - `test_tdma_pio_spi_ring_adapter.c` 的 loopback physical stub 增加 `rx_pending`，每次 TX 只生成一个可消费 RX 帧。
  - 测试期望改为显式验证二分频发射周期：第二个 service 不推进 beacon/sequence/evidence，第三个 service 推进到下一帧。
  - 坏帧注入不再被同一轮 loopback 正常帧覆盖，可稳定验证 `RX_BAD_FRAME`、`down_running=0` 和 `ring_seq` 不前进。
- 验证结果：
  - `run_tdma_pio_spi_ring_adapter_tests.ps1` 通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，26/26 host test scripts passed。
  - `python tools\docs_check\docs_check.py` 通过，保留既有 2 个 risk review 文件命名 warning。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260818055634`，package CRC `0x32CE4358`。
- 还需完成：
  - 两块最小系统板上执行常驻 UP/DOWN TDMA HIL，验证实际 1 MHz 环路下 `ring_seq`、`idle_beacon_tx/rx`、`down_running` 和 VDC evidence 是否持续增长。
- 关联文件：
  - `tests/unit/test_tdma_pio_spi_ring_adapter.c`
- 下一步：
  - 回到 1 MHz TDMA/RefMem HIL 闭环，按 COM IDN 自动识别和 GPIO16-19/21-24 线序继续验证。

### TDMA-TASK-20260817-013 - 常驻双向 PIO SPI 物理层与 adapter 模块化接入

- 状态：完成常驻物理层、adapter REFERENCE/FORWARD role、adapter 注册表和 host/构建门禁；两板烧录 HIL 尚未执行。
- 日期：2026-08-17
- 任务目标：
  - 执行 P0.5-3 的固件侧：两板同时 UP/DOWN 常驻短帧，空闲持续 `IDLE_BEACON`，不依赖 host 交替维护命令。
  - 按 HAOFV adapter 边界预留模块化切换空间（当前 PIO SPI，后续 BISS-C/UART/RS485 可注册替换）。
- 完成内容：
  - 新增 `tdma_pio_spi_phys.h/.c`：首版为无 CS 3-wire PIO SPI 常驻物理层（downlink master TX + uplink slave RX 双 SM 同时 arm，复用已验证的 `tx_byte/rx_byte` PIO 程序和 4B magic+length 定界）；后续 TDMA-TASK-20260818-003 已将 bring-up 物理层修正为 `FRAME_SYNC/CS + DATA + CLK` 三线单向腿。
  - 新增 `tdma_pio_spi.pio`（TDMA 命名空间副本，避免 tdma 依赖 refmem 生成头）。
  - `tdma_pio_spi_ring_adapter` 增加 REFERENCE/FORWARD role：`local==reference` 时发新 `IDLE_BEACON` 并收环回帧；否则收上一板帧、`advance_hop` 转发（保持 origin/sequence/identity CRC、重算 transport CRC），`forward_count` 计入 snapshot；`set_phys_ctrl` 在 adapter start/stop 时驱动物理层 arm/disarm。
  - `tdma_service` 增加 adapter 注册表 `tdma_service_register_adapter_impl()`：`tdma_service_configure_foundation_profile()` 按 `resource.adapter_type` 绑定对应实现，未注册类型解绑并报 `ADAPTER_MISSING`；`tdma_ring_runtime_unbind_adapter()` 支持解绑。
  - `tdma_runtime_owner` 改为注册 `TDMA_ADAPTER_PIO_SPI` 实现（不再直接绑定），profile 激活时自动按 adapter_type 选择。
  - board_config.h 增加 `BOARD_TDMA_SPI_*` 宏（复用已验证 uplink/downlink 引脚）。
- 验证结果：
  - `run_tdma_pio_spi_ring_adapter_tests.ps1` 通过：REFERENCE 发 beacon + 收环回（evidence=1 需要硬件 timestamp metadata）；FORWARD 收帧转发（hop 0->1、identity/sequence 保持、`simultaneous_feedback_loop_evidence` 保持 0）；phys_ctrl arm/disarm 由 start/stop 驱动。
  - `run_tdma_service_scheduler_tests.ps1` 通过：PIO_SPI profile 绑定 SPI 实现、BISS_C profile 切换到 BISS_C 实现、未注册 UART 解绑报 `ADAPTER_MISSING`。
  - `run_tdma_ring_runtime_tests.ps1`、`run_tdma_transport_frame_tests.ps1` 回归通过。
  - `run_host_unit_tests.ps1`（全局 GCC `D:\Embedded\GCC\mingw64\bin`）通过，26/26 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标（含 `tdma_pio_spi.pio` pioasm 生成与 `tdma_pio_spi_phys.c` 编译链接）。
  - 本轮未烧录板卡；`up/down_running=1` 与 `simultaneous_feedback_loop_evidence=1` 的板端值待两板 HIL。
- 还需完成：
  - 两板烧录后验证常驻 IDLE_BEACON：`up/down_running=1`、`ring_seq` 增长、SCPI 查询不 disarm。
  - P0.5-4/5：ring timestamp evidence（reference TX、每 hop RX/TX、feedback RX）与 PIO 硬件 timestamp latch（`HARDWARE_TICK / <=100 ns`）后置位 `simultaneous_feedback_loop_evidence`。
  - P0.5-6：HIL 只读监控脚本。
- 关联文件：
  - `components/tdma/inc/tdma_pio_spi_phys.h`、`components/tdma/src/tdma_pio_spi_phys.c`
  - `components/tdma/src/tdma_pio_spi.pio`
  - `components/tdma/inc/tdma_pio_spi_ring_adapter.h`、`components/tdma/src/tdma_pio_spi_ring_adapter.c`
  - `components/tdma/inc/tdma_service.h`、`components/tdma/src/tdma_service.c`
  - `components/tdma/inc/tdma_ring_runtime.h`、`components/tdma/src/tdma_ring_runtime.c`
  - `components/tdma/src/tdma_runtime_owner.c`
  - `boards/rp2350_trig/inc/board_config.h`
  - `tests/unit/test_tdma_pio_spi_ring_adapter.c`、`tests/unit/test_tdma_service_scheduler.c`
  - `CMakeLists.txt`
- 下一步：
  - 两板烧录常驻环 HIL；随后 P0.5-4/5 的 ring timestamp evidence 与硬件 latch。

### TDMA-TASK-20260817-012 - PIO SPI ring adapter 绑定与 ADAPTER_MISSING 消除

- 状态：完成 transport 级 ring adapter、runtime owner 绑定、host 单测和 A/B 构建；物理双向 PIO/DMA 常驻帧（P0.5-3）与两板 HIL 尚未完成。
- 日期：2026-08-17
- 任务目标：
  - 执行 P0.5-1：为最小系统 PIO SPI bring-up adapter 绑定 `TdmaRingAdapterOps`，消除 `ring_last_error=4(ADAPTER_MISSING)`。
  - 执行 P0.5-2：发布 adapter 生命周期 evidence 和 idle beacon / timestamp 元数据。
  - 不得伪造 `simultaneous_feedback_loop_evidence`。
- 完成内容：
  - 新增 `components/tdma/inc/tdma_pio_spi_ring_adapter.h` 与 `components/tdma/src/tdma_pio_spi_ring_adapter.c`，实现 `tdma_ring_adapter_ops_t` 的 start/stop/service。
  - adapter 为 transport 级：只编解码 `TdmaTransportFrame`（空闲时构建并发送 `IDLE_BEACON` 短帧，解析 RX 帧并校验 schedule/ring CRC），不接触 `refmem_sync_frame` 或 VDC/RefMem 内帧。
  - service 维护 UP sequence、DOWN RX sequence、identity CRC、idle beacon TX/RX 计数、reference TX / feedback RX timestamp 元数据（source/resolution/flags 由 `tdma_pio_spi_ring_adapter_set_timestamp_metadata()` 声明），全部投影到 `tdma_ring_adapter_status_t`。
  - 物理层以可选 `phys_tx` / `phys_rx` 钩子接入（`tdma_pio_spi_ring_adapter_set_phys()`）；未接物理 TX 时 service 返回 false，ring runtime 报告 `EVIDENCE_MISSING` 而非伪造 running；RX 也可经 `tdma_pio_spi_ring_adapter_inject_rx()` 注入（host 测试）。
  - `tdma_runtime_owner_init()` 创建 adapter 并调用 `tdma_service_bind_ring_adapter()`；新增 `tdma_runtime_owner_get_ring_adapter()` 供板端后续接入物理钩子。
  - 新增 host 单测 `tests/unit/test_tdma_pio_spi_ring_adapter.c` 和 `tools/tests/run_tdma_pio_spi_ring_adapter_tests.ps1`，并纳入 `run_host_unit_tests.ps1`。
- 验证结果：
  - `run_tdma_pio_spi_ring_adapter_tests.ps1` 通过：未绑定 → `ADAPTER_MISSING`；绑定无物理 → `EVIDENCE_MISSING`、`adapter_started=1`、start/service 计数增长；回环 phys + 硬件 timestamp（100 ns / `HARDWARE_LATCHED`）→ `up/down_running=1`、`simultaneous_feedback_loop_evidence=1`、round trip=500 ns、beacon 计数增长；无硬件 timestamp 或 `DIAGNOSTIC_ONLY` → evidence=0 且 `TIMESTAMP_MISSING`；坏帧 → `down_running=0`、`rx_bad_count` 增长并恢复；队列溢出计数。
  - `run_tdma_ring_runtime_tests.ps1`、`run_tdma_transport_frame_tests.ps1` 回归通过。
  - `run_host_unit_tests.ps1`（全局 GCC `D:\Embedded\GCC\mingw64\bin`）通过，26/26 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标；build id `20260817130228`，package CRC32 `0x40859A25`。
  - 本轮未烧录板卡；`up/down_running` 与 `simultaneous_feedback_loop_evidence` 的板端值取决于物理钩子接入（P0.5-3）。
- 还需完成：
  - P0.5-3：两板同时 UP/DOWN 常驻短帧的 PIO/SM/DMA 双向物理层，经 `set_phys()` 接入 adapter。
  - P0.5-4/5：物理 timestamp 证据（`HARDWARE_TICK`、`<=100 ns`、硬件 latch）产生真实 correlation。
  - P0.5-6：HIL 只读监控脚本。
- 关联文件：
  - `components/tdma/inc/tdma_pio_spi_ring_adapter.h`
  - `components/tdma/src/tdma_pio_spi_ring_adapter.c`
  - `components/tdma/src/tdma_runtime_owner.c`
  - `components/tdma/inc/tdma_runtime_owner.h`
  - `tests/unit/test_tdma_pio_spi_ring_adapter.c`
  - `tools/tests/run_tdma_pio_spi_ring_adapter_tests.ps1`
  - `tools/tests/run_host_unit_tests.ps1`
  - `CMakeLists.txt`
- 下一步：
  - 实现双向 PIO SPI 物理钩子（master TX+RX / slave RX+TX 双 SM 同时 arm），两板各自常驻 IDLE_BEACON，再进入 HIL 验收。

### TDMA-TASK-20260817-011 - RTOS 启动期调度槽池收敛

- 状态：完成根因修复、host 单测、A/B 构建和 COM5/COM6 板端验证。
- 日期：2026-08-17
- 问题：
  - FreeRTOS 启动阶段先为 10 个任务分配约 94 KiB stack，随后 TDMA runtime owner 又按最大 32 个 1024 B frame slot 一次申请约 36 KiB。
  - 128 KiB heap 无法同时容纳任务和 TDMA 最大槽池，`tdma_runtime_owner_init()` 失败后应用未进入 ready，表现为 USB CDC 枚举但 SCPI 写超时。
  - RefMem 初始化观测字段显示失败停在 `DISTRIBUTED_REFMEM_INIT_STAGE_TDMA_PROFILE`；默认 foundation profile 的五类 traffic queue depth 总和为 28，超过当前 RTOS runtime active slot pool 的 8。
- 修正：
  - 保留 `TDMA_TRAFFIC_SCHEDULER_SLOT_COUNT=32` 作为实现上限。
  - RTOS 产品运行时先使用 8 个 active slot；默认五类 traffic 收敛为 `VDC=2, RefMem=3, Config=1, Bulk=1, Log=1`，总计 8 槽。
  - 后续 System Pack profile 的 queue depth 总和不得超过 active runtime slot capacity；需要扩大时必须先通过 RTOS heap 水位门禁。
- 验证：
  - `run_tdma_profile_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过。
  - `run_tdma_traffic_scheduler_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，新增 `test_default_profile_fits_runtime_slot_pool()`，用 8-slot runtime pool 配置默认 profile。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260817104554`。
  - COM5 BOOTSEL factory UF2 恢复后 `SYSTem:FW:BUILD?` 返回 `"20260817104554"`，`SYSTem:REFMEM:STATus?` 返回 `...,1,8,0`。
  - COM6 OTA boot/commit 后 `SYSTem:FW:BUILD?` 返回 `"20260817104554"`，`SYSTem:REFMEM:STATus?` 返回 `...,1,8,0`，`SYSTem:OTA:SLOT?` 返回 `2,0,2,0,1`。
  - 两板 `SYSTem:ERRor?` 均返回 `0,"No error"`。
- 后续：
  - 如果后续要把 runtime slot pool 从 8 扩大到 16/32，必须先在 `SYSTem:RTOS:STATus?` 和长时间 HIL 中确认 heap 水位、fragmentation 和 core1 service jitter，再更新默认 System Pack profile。

### TDMA-TASK-20260817-010 - Flight-mutable 短帧与节点数据装载路径

- 状态：完成 transport flight-mutable API、RefMem realtime 容量门禁、ProcessImageMap C 契约和架构流水线；System Pack 表、双缓冲与 PIO cut-through 尚未实现。
- 日期：2026-08-17
- 任务目标：
  - 参考 EtherCAT processing-on-the-fly，明确每个节点如何把本地 VDC/RefMem 小事实装入常驻短帧，而不是周期刷新 64 KB RefMem。
  - 让 immutable ring identity 与沿途可变 process image 解耦。
- 完成内容：
  - `TdmaTransportFrame` 增加 `FLIGHT_MUTABLE`，只允许 short frame 在已授权 payload slice 上更新内容并重算 transport CRC。
  - identity CRC 改为只覆盖不可变路由字段；payload 局部完整性归 segment owner CRC/version，避免节点更新 process image 后破坏反馈相关身份。
  - RefMem TDMA realtime binding 收紧为 260 B 内帧，critical delta 净载荷上限 224 B；总线无关 RefMem 协议仍保留 292 B 理论帧能力。
  - 新增 `tdma_process_image_map.*`，校验 segment owner、payload class、offset/length、策略 flags、唯一 ID、不重叠和 map CRC，并提供 local slot publish 权限查询。
  - 新增 `CYCLIC_PROCESS_IMAGE` payload class，归入最高优先级 short-frame traffic；默认 VDC/RefMem 硬预留各调整为一帧 292 B，修复旧 128 B VDC budget 无法容纳 216 B 诊断帧的问题。
  - 文档冻结 `fact commit -> dirty descriptor -> shadow process image -> cycle swap -> PIO/DMA flight update -> feedback` 主线。
- 验证结果：
  - `run_tdma_transport_frame_tests.ps1` 覆盖 flight payload patch 后 identity 不变、transport CRC 更新、payload 可见和后续 hop 转发。
  - `run_tdma_process_image_map_tests.ps1` 覆盖有效 map、owner publish、越权、重叠、重复 ID 和 CRC 拒绝。
  - `run_refmem_realtime_tdma_tests.ps1` 覆盖 260 B realtime delta 接纳和 261 B 拒绝。
- 还需完成：
  - `TdmaProcessImageMap` 正式 System Pack 表、active/shadow 双缓冲、compact VDC segment 和 dirty RefMem segment。
  - PIO SPI ring adapter 的 RX/TX overlap、固定 offset 更新和每 hop pipeline delay 实测。
- 下一步：
  - 先把 ProcessImageMap 接入 System Pack / DeploymentGate 并冻结 segment wire contract，再接 TDMA 自有 PIO SPI adapter；不能让 adapter 读取业务域内部对象。

### TDMA-TASK-20260817-009 - 通用 Transport Envelope 与长短帧门禁

- 状态：完成 32 B transport envelope、SHORT/LONG 容量和 traffic-class 门禁的代码/host 单测；尚未接入 PIO SPI ring adapter。
- 日期：2026-08-17
- 任务目标：
  - 解除物理 adapter 对 `refmem_sync_frame` 的绑定，使 VDC、RefMem、OTA、SD 和 LOG 可复用同一 transport。
  - 自动同步阶段保持短帧和确定性；宽松同步或维护窗口允许可靠长帧。
- 完成内容：
  - 新增 `tdma_transport_frame.*`，使用固定 32 B、小端 wire header；编码不依赖 C struct padding。
  - transport header 包含 frame class、origin、sequence、payload class、schedule/ring CRC、hop count/limit、identity CRC 和 transport CRC。
  - identity CRC 不随 hop 改变；transport CRC 覆盖当前 hop 和完整 packet，每次转发重算。
  - `SHORT` 总长上限 292 B、净载荷 260 B；`LONG` 总长上限 1024 B、净载荷 992 B。
  - scheduler 冻结 VDC/RefMem realtime 只允许短帧，reliable bulk/LOG 只允许长帧，配置流可选择两者但仍受 maintenance gate 控制。
  - 新增 `STORAGE_BULK` payload class，与 OTA 共同归入 reliable bulk traffic，不创建 SD 私有总线。
- 验证结果：
  - `run_tdma_transport_frame_tests.ps1` 通过，覆盖编解码、短/长帧、CRC、hop、origin feedback、hop limit 和短帧容量拒绝。
  - `run_tdma_traffic_scheduler_tests.ps1` 通过，覆盖 long VDC 拒绝、short LOG 拒绝和 long STORAGE 接纳。
  - `run_tdma_profile_tests.ps1` 通过，System Pack 生成器同步扩展 payload whitelist 和 reliable bulk mask。
  - `run_host_unit_tests.ps1` 使用 Vivado MinGW GCC 全量通过，24/24 host scripts passed。
  - `python -m pytest tests/python/test_refmem_pack_build.py -q` 通过，2 passed；仅保留 OneDrive `.pytest_cache` 权限 warning。
  - `python tools/docs_check/docs_check.py` 通过，保留既有两项 risk review 文件名 warning。
  - A/B 双目标构建通过；build id `20260817083335`，package CRC32 `0x0D7C870E`。
- 还需完成：
  - RefMem realtime binding 的 260 B 内帧和 224 B critical delta 上限已完成；仍需实现分片与 background/bulk 路径。
  - 建立 TDMA 所有的 PIO SPI ring adapter，同时常驻 RX/TX SM，并让 adapter 只处理外层 transport。
  - 将 idle beacon、origin feedback 和硬件 timestamp evidence 接入 `TdmaRingAdapterOps`。
- 关联文件：
  - `components/tdma/inc/tdma_transport_frame.h`
  - `components/tdma/src/tdma_transport_frame.c`
  - `components/tdma/src/tdma_traffic_scheduler.c`
  - `components/tdma/inc/tdma_profile.h`
  - `tests/unit/test_tdma_transport_frame.c`
- 下一步：
  - 先完成 RefMem critical delta 的短帧分片边界，再实现 TDMA PIO SPI ring adapter，避免物理层继续理解业务帧。

### TDMA-TASK-20260817-008 - 双向 adapter 与硬件反馈证据契约

- 状态：完成 ring adapter/runtime 证据基础件和 host 测试；PIO SPI 双向物理 adapter、常驻 IDLE beacon 和两板 HIL 尚未完成。
- 日期：2026-08-17
- 任务目标：
  - 消除 ring profile 配置成功后直接报告 `up_running/down_running=1` 的假运行状态。
  - 为同时 UP/DOWN runtime 冻结 adapter lifecycle 和 reference TX / feedback RX 硬件时间戳相关条件。
- 完成内容：
  - `TdmaRingRuntime` 新增 `TdmaRingAdapterOps.start/stop/service`，core1 只接受 adapter 返回的 configured/running 和事件证据。
  - 未绑定 adapter 时保持两条 leg 停止并报告 `ADAPTER_MISSING`；profile 只表示配置，不表示物理运行。
  - 闭环相关同时检查 sequence、frame CRC、schedule CRC、timestamp 顺序、feedback timeout、`HARDWARE_LATCHED`、非诊断标志和 `<=100 ns` 分辨率。
  - snapshot 增加 adapter start/stop/service 计数、adapter 原始错误码、idle beacon TX/RX 计数、reference/feedback sequence、CRC、timestamp 和 round-trip。
  - `SYSTem:REFMEM:SYNC:TDMA:STATus?` 只在原响应末尾追加上述维护字段，不改变旧字段顺序。
- 验证结果：
  - `run_tdma_ring_runtime_tests.ps1` 通过：无 adapter 不运行；诊断时间戳不产生闭环证据；匹配硬件证据产生闭环；sequence mismatch 立即撤销证据。
  - `run_tdma_service_scheduler_tests.ps1`、`run_vdc_domain_tests.ps1` 和 `run_refmem_realtime_tdma_tests.ps1` 通过。
  - 修复 `tdma_service_get_snapshot()` 未清零输出结构的问题，避免未绑定 traffic scheduler 时读取未初始化的兼容字段。
  - `run_host_unit_tests.ps1 -HostGccDir D:\\Xilinx\\2025.2\\tps\\mingw\\10.0.0\\win64.o\\nt\\bin` 通过，23/23 host scripts passed。
  - A/B 双目标构建通过；build id `20260817075745`，package CRC32 `0x8AEF6A19`。
- 还需完成：
  - 为最小系统 PIO SPI adapter 增加独立 RX/TX pin group、双 SM 同时 arm 和异步 DMA service。
  - 由 adapter 常驻生成/接收 `IDLE_BEACON`，并发布真实 PIO/DMA timestamp evidence。
  - 两板 HIL 通过后才允许 TDMA snapshot 报告物理反馈闭环成立。
- 关联文件：
  - `components/tdma/inc/tdma_ring_runtime.h`
  - `components/tdma/src/tdma_ring_runtime.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_tdma_ring_runtime.c`
- 下一步：
  - 将可配置的 GPIO16-24 PIO SPI 双向 adapter 挂到 `TdmaRingAdapterOps`，先取得 running/idle evidence，再补硬件 timestamp correlation。

### TDMA-TASK-20260817-007 - 唯一 runtime owner 与三级 traffic scheduler

- 状态：完成软件调度基础件、公共 owner、三级门禁、per-class completion token、23/23 host 门禁和 A/B 固件构建；尚未形成两板物理闭环 evidence。
- 日期：2026-08-17
- 任务目标：
  - 判断当前 TDMA 是否足以支撑 VDC 闭环，并先关闭软件调度与 owner 层的阻断项。
  - 冻结 `VDC_REALTIME > REFMEM_REALTIME > maintenance`，低优先级流不抢占实时短帧。
  - 消除 VDC/RefMem 各自维护 TDMA service 的双 runtime 现象。
- 完成内容：
  - 新增 `TdmaTrafficScheduler`，实现五类固定队列、周期预算、deadline、time-aware gate、maintenance gate、fault/backpressure/drop-oldest/drop-newest 和基础质量计数。
  - maintenance gate 默认关闭；配置、OTA、LOG 只有 TDMA owner 确认不同步或进入显式维护窗口后才可执行。
  - 新增 `tdma_runtime_owner.*`；VDC 注册 observation payload，RefMem 注册 data payload 并绑定物理 adapter，二者共享唯一 service，core1 每轮只推进一次。
  - 调度帧池从 128 KiB FreeRTOS heap 一次性申请，避免 32 个 long-frame 槽进入 `.bss`；申请失败直接阻止应用初始化。
  - 修正优先级重排下的序号语义：service 内部执行序号与 scheduler enqueue token 分离，并为五类流发布持久 completion token，避免 VDC 后入队先完成时误确认 RefMem intent。
  - result/error/timestamp/frame completion 按 traffic class 独立持久化；VDC 读取 VDC completion metadata，RefMem 读取 RefMem completion frame，后完成的低优先级流不再覆盖实时流证据。
  - `SYSTem:REFMEM:SYNC:TDMA:STATus?` 在旧字段末尾追加 scheduler 配置、enqueue/dispatch、队列水位、fault、last result/class 和五类 completion token。
  - 新增 service/scheduler 集成测试，证明配置先入队时仍按 VDC、RefMem、配置顺序执行；maintenance gate 关闭时配置帧保持排队。
- 验证结果：
  - `run_tdma_traffic_scheduler_tests.ps1` 通过。
  - `run_tdma_service_scheduler_tests.ps1` 通过。
  - `run_refmem_realtime_tdma_tests.ps1` 和 `run_vdc_domain_tests.ps1` 通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，23/23 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标；build id `20260817073126`，package CRC `0xEDF7B9AF`。
  - 本轮未烧录板卡；`simultaneous_feedback_loop_evidence` 仍为 0，不能据此进入产品 DPLL 闭环调参。
- 还需完成：
  - 实现同时 UP/DOWN adapter runtime、IDLE_BEACON 和硬件 RX/TX timestamp correlation。
  - 发布正式 `TdmaQualityVector` 并完成两板 HIL。
- 关联文件：
  - `components/tdma/inc/tdma_traffic_scheduler.h`
  - `components/tdma/src/tdma_traffic_scheduler.c`
  - `components/tdma/inc/tdma_runtime_owner.h`
  - `components/tdma/src/tdma_runtime_owner.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_tdma_service_scheduler.c`
- 下一步：
  - 先完成 per-class completion metadata，再接 adapter/timestamp correlation；硬件证据通过后才进入 DPLL 闭环。

### TDMA-TASK-20260817-006 - TdmaRingRuntime 基础件拆分

- 状态：完成独立基础件、service 聚合接入、reason code、21/21 host 门禁和 A/B 固件构建；硬件 HIL 尚未执行。
- 日期：2026-08-17
- 任务目标：
  - 将 ring config、core1 runtime 推进和 ring snapshot 从 `tdma_service.c` 单体拆出。
  - 保持现有 `tdma_service_configure_ring_runtime()` 和维护 snapshot 字段兼容。
  - 冻结 adapter/scheduler/timestamp 后续共用的 ring reason code。
- 完成内容：
  - 新增 `tdma_ring_runtime.h/.c`，独立维护 config/result seqlock、config/reject/service/ring seq 和双向运行状态。
  - `tdma_service` 改为聚合 `tdma_ring_runtime_t`，配置、core1 service 和 snapshot 均通过基础件 API 完成。
  - 保留原有 ring snapshot 字段，并新增 `ring_config_reject_count`；维护查询只在末尾追加新字段。
  - 配置校验把 UP/DOWN group 缺失或相同明确映射为 `DIRECTION_CONFLICT`，其余 topology/flag/CRC 错误映射为 `BAD_CONFIG`。
  - 冻结 `EVIDENCE_MISSING`、`ADAPTER_MISSING`、`TIMESTAMP_MISSING`、`PAYLOAD_STARVATION`、`WINDOW_MISSED` 和 `RESOURCE_CONFLICT` 编号，供后续 owner 接入。
  - runtime service 仍强制保持 `simultaneous_feedback_loop_evidence=0`，未提供软件直接置位接口。
  - 新增独立 host 单测及测试脚本，并纳入全量 host 列表。
- 验证结果：
  - `run_tdma_ring_runtime_tests.ps1` 通过。
  - `run_refmem_realtime_tdma_tests.ps1` 通过，包含 config reject 和兼容 snapshot 断言。
  - `run_vdc_domain_tests.ps1` 通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，21/21 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标；build id `20260817063552`，package CRC `0xF15A8DA6`。
  - 本轮没有烧录板卡，不形成 UP/DOWN 同时物理运行或硬件闭环 evidence。
- 还需完成：
  - 建立五类固定队列、time-aware gate 和 reason-to-quality 映射。
  - 由 adapter/timestamp correlation 提供其余 reason 的真实发布源。
- 关联文件：
  - `components/tdma/inc/tdma_ring_runtime.h`
  - `components/tdma/src/tdma_ring_runtime.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_tdma_ring_runtime.c`
  - `tools/tests/run_tdma_ring_runtime_tests.ps1`
- 下一步：
  - 全量闭环后进入五类固定队列和 time-aware gate 的数据结构设计。

### TDMA-TASK-20260817-005 - TdmaPayloadRegistry 基础件拆分

- 状态：完成独立基础件、service 聚合接入、维护快照、20/20 host 门禁和 A/B 固件构建；未执行硬件 HIL。
- 日期：2026-08-17
- 任务目标：
  - 将 payload binding、whitelist、capacity 和 admission 从 `tdma_service.c` 单体拆出。
  - 保持 VDC/RefMem 既有注册 API 和 RX-window 语义不变。
  - 为后续逐类队列、policing 和 DeploymentGate 提供可查询的 registry 水位。
- 完成内容：
  - 新增 `tdma_payload_registry.h/.c`，支持固定 8-entry registry、binding 注册/替换、active whitelist、short/long capacity 和 frame admission。
  - registry 使用独立 seqlock snapshot，发布 config seq、registration seq、used、admitted、reject、last result 和 last payload class。
  - `tdma_service_register_payload()` 和 submit admission 改为委托 registry；对外 API 名称和 producer 调用方式保持不变。
  - foundation profile 激活时同步配置 registry whitelist 和 capacity；如果现有 binding 与候选 profile 不兼容，runtime profile 配置拒绝。
  - 保留 RX window 的 `frame_size=0` 语义，它表示接收窗口尚无 payload，不按空 TX frame 拒绝。
  - RefMem 兼容 snapshot 和 `SYSTem:REFMEM:SYNC:TDMA:STATus?` 在末尾追加 registry 水位字段。
  - 新增独立 host 单测及测试脚本，并纳入全量门禁。
- 验证结果：
  - `run_tdma_payload_registry_tests.ps1` 通过。
  - `run_refmem_realtime_tdma_tests.ps1` 通过，包含 profile 配置后 registry seq/binding 集成断言。
  - `run_vdc_domain_tests.ps1` 通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，20/20 host scripts passed。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标；build id `20260817061911`，package CRC `0x75320FF1`。
  - 本轮没有烧录板卡，不形成 UP/DOWN 同时运行或硬件闭环 evidence。
- 还需完成：
  - ring runtime 已由 TDMA-TASK-20260817-006 拆出。
  - 在 registry admission 之上建立五类固定队列和 time-aware gate。
  - 将 registry reject/deadline/budget 统计并入正式 `TdmaQualityVector`。
- 关联文件：
  - `components/tdma/inc/tdma_payload_registry.h`
  - `components/tdma/src/tdma_payload_registry.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_tdma_payload_registry.c`
  - `tools/tests/run_tdma_payload_registry_tests.ps1`
- 下一步：
  - ring runtime 与 reason code 已完成；继续 scheduler queue runtime。

### TDMA-TASK-20260817-004 - Active Profile 到公共 Runtime 的激活闭环

- 状态：完成配置事务闭环、维护可观测性、定向 host 单测和 A/B 固件构建；未执行两板硬件闭环。
- 日期：2026-08-17
- 任务目标：
  - 让 RMTP 第 10 张 `TdmaFoundationProfile` 在激活后真正配置公共 TDMA runtime，而不是只停留在 active table view。
  - 在 TableRegistry 切换前拒绝与当前 VDC ring/schedule 不一致的 candidate，避免 active 表和 runtime 半提交。
  - 保持维护查询只读，并为后续 HIL 暴露 profile、ring 和 feedback evidence。
- 完成内容：
  - `refmem_realtime_tdma` 增加正式 foundation-profile 配置入口，业务上层不再访问内部 `scheduler` 成员。
  - prepared table views 增加 TDMA profile 的受控 copy getter；commit/discard 后 getter 立即失效。
  - VDC ring plan 增加 `cycle_period_ns`，作为 TDMA profile 的跨域只读调度契约。
  - 激活门禁比较 node count、local/reference、upstream/downstream、feedback、ring flags、ring/profile CRC、schedule CRC 和 cycle period。
  - factory profile 在 `distributed_refmem_init()` 中通过同一门禁装入 runtime；System Pack candidate 在 registry 激活和 model commit 后自动装入 runtime。
  - 新增 `REFMEM_TABLE_ACTIVATE_ERR_RUNTIME_PROFILE`，不一致候选映射为配置验证失败 NACK。
  - `SYSTem:REFMEM:SYNC:TDMA:STATus?` 在原字段末尾追加 profile CRC、owner、adapter、whitelist、ring config/runtime 和 feedback evidence。
- 验证结果：
  - `run_refmem_realtime_tdma_tests.ps1` 使用 Vivado MinGW GCC 通过。
  - `run_refmem_table_registry_tests.ps1` 使用 Vivado MinGW GCC 通过。
  - `run_vdc_domain_tests.ps1` 使用 Vivado MinGW GCC 通过。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过 A/B 双目标；build id `20260817060622`，package CRC `0x0271E168`。
  - 本轮没有烧录两板，也没有产生同时 UP/DOWN、硬件 timestamp correlation 或 `simultaneous_feedback_loop_evidence=1` 的 HIL 证据。
- 还需完成：
  - payload registry 已由 TDMA-TASK-20260817-005 拆出；ring runtime 仍待拆分。
  - 实现五类固定队列、time-aware gate、policing/backpressure 和 `TdmaQualityVector`。
  - 在两板固件中同时常驻运行 UP/DOWN leg，再进入闭环 timestamp HIL。
- 关联文件：
  - `components/distributed_refmem/inc/refmem_realtime_tdma.h`
  - `components/distributed_refmem/src/refmem_realtime_tdma.c`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/distributed_refmem.c`
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/vdc_domain/src/vdc_domain.c`
  - `middleware/scpi_port/src/scpi_system_snapshot_commands.c`
- 下一步：
  - 先拆分 `TdmaPayloadRegistry` 并建立可查询的 admission snapshot，再实现逐类固定队列。

### TDMA-TASK-20260817-003 - RMTP Foundation Profile 与 DeploymentGate 闭环

- 状态：完成正式表镜像、跨表资源门禁、host 全量验证和固件构建；profile activation hook 已由 TDMA-TASK-20260817-004 完成。
- 日期：2026-08-17
- 任务目标：
  - 将 TDMA foundation profile 从独立 C contract 升级为可由 SD/System Pack staging、激活和回滚的正式 RMTP 表。
  - 在激活前完成 owner、NodeLoad、SlotClaim、RealtimeCapabilityContract、物理 IO、payload 和容量一致性检查。
  - 冻结可供后续 time-aware scheduler 使用的周期容量、guard band 与 queue RAM 契约。
- 完成内容：
  - RMTP 表数量由 9 扩展为 10，新增 table id 9 `TdmaFoundationProfile`，owner 标记为 `TDMA_AO`。
  - 新增 71-word 固定 `u32 little-endian` profile row；编码/解码逐字段执行，不依赖 C struct padding。
  - profile 增加 cycle period、cycle capacity、guard band 和 queue RAM capacity；validator 拒绝总预留预算、单类 MTU 和队列 RAM overcommit。
  - staged candidate 必须恰好存在一个已加载 `TdmaSchedulerAO`，profile owner 必须匹配 NodeLoad local slot、owner resource claim、IO/IP claim 和 SlotClaim/RealtimeCapabilityContract。
  - TDMA adapter IO 改由 foundation owner 独占；业务 FB 只能通过 payload/intent 使用 TDMA，不能重复声明 adapter IO。
  - Python System Pack 生成器、pack builder、registry HIL validator 和 SCPI load validator 全部改为从统一 10 表定义派生 table count、mask 和 stage payload size。
  - active profile 到公共 runtime 的自动配置与 VDC schedule 交叉门禁已由 TDMA-TASK-20260817-004 补齐。
- 验证结果：
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，19/19 host scripts passed。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；仅保留 OneDrive `.pytest_cache` warning。
  - `python tools\docs_check\docs_check.py` 通过，保留既有两项 risk review 文件名 warning。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过；最终 build id 和 package CRC 见本次提交记录。
- 还需完成：
  - 建立五类固定队列、time-aware gate、policing/backpressure 和 `TdmaQualityVector`。
  - 在两板硬件上同时运行 UP/DOWN leg，并用硬件 timestamp correlation 置位真实闭环 evidence。
- 关联文件：
  - `components/tdma/inc/tdma_profile.h`
  - `components/tdma/src/tdma_profile.c`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `components/distributed_refmem/src/refmem_application_model.c`
  - `components/distributed_refmem/src/refmem_table_registry.c`
  - `tools/refmem_table_image/refmem_table_image.py`
- 下一步：
  - profile activation hook 已闭环；继续逐类固定队列与 time-aware gate。

### TDMA-TASK-20260817-002 - Foundation profile、HAOFV owner 与 TSN-style 流治理

- 状态：完成首版代码契约与 host 单元验证；RMTP profile 表已由 TDMA-TASK-20260817-003 补齐，真实逐流 scheduler 尚待实现。
- 日期：2026-08-17
- 任务目标：
  - 将 `TDMARingProfile` owner 从 VDC 迁入 TDMA Foundation。
  - 将 TDMA 表达为 HAOFV 可装载 system node，并冻结运行期资源声明。
  - 面向 VDC、RefMem、配置、OTA 和 LOG，吸收 TSN 的流分类、准入、time-aware gate、整形和背压思想。
- 完成内容：
  - 新增 `tdma_profile.h/.c`，定义 `tdma_ring_profile_t`、`tdma_foundation_profile_t`、adapter、PIO/SM/DMA/core1 resource、frame capacity、payload whitelist 和 profile CRC。
  - `vdc_tdma_schedule_profile_t` 改为只读嵌入 `ring_binding`；VDC schedule CRC 绑定 TDMA ring CRC，ring topology/CRC 校验由 TDMA owner 实现。
  - `tdma_service_configure_foundation_profile()` 将 owner、adapter、PIO/SM/DMA、core1 service、capacity、IO/IP claim、payload whitelist 和 profile CRC 冻结到 runtime snapshot。
  - payload registry 按 active whitelist 做 admission，未登记 payload 被拒绝。
  - 固定五类 traffic class：VDC realtime、RefMem realtime、config control、OTA bulk、LOG best effort；每类定义周期预算、帧数、队列深度、deadline、gate/shaping/preemption 和 overflow policy。
  - RefMem Application Model 增加 TDMA baseline capability、`TdmaSchedulerAO` instance、NodeLoad、resource/IP claim 和 DeploymentGate check；默认模型只允许一个 active TDMA owner。
- 验证结果：
  - `run_tdma_profile_tests.ps1` 使用 Vivado MinGW GCC host 执行通过。
  - `run_vdc_domain_tests.ps1` 使用 Vivado MinGW GCC host 执行通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 全部通过，19/19 脚本均执行 host 测试。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；仅有 OneDrive `.pytest_cache` 无法写入 warning，不影响测试结果。
  - `python tools\docs_check\docs_check.py` 通过，保留既有两项 risk review 文件名 warning。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，最终 build id `20260817051719`，package CRC `0x086043EE`。
- 还需完成：
  - 正式 RMTP/System Pack 表镜像和 DeploymentGate staged-candidate 校验已由 TDMA-TASK-20260817-003 完成。
  - 实现 core1 逐类队列、time-aware gate、policing/backpressure 和质量计数。
- 关联文件：
  - `components/tdma/inc/tdma_profile.h`
  - `components/tdma/src/tdma_profile.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
- 下一步：
  - RMTP 表项和 activation hook 已完成；继续 scheduler queue runtime。

### TDMA-TASK-20260817-001 - 上/下行 TDMA 边界升格为基础件

- 状态：完成首版边界升级、文档主域、单元测试和构建验证；后续进入 TDMA system node / 同时 UP-DOWN runtime。
- 日期：2026-08-17
- 任务目标：
  - 根据架构纠偏，将上行/下行 TDMA 从 VDC 描述中拆出，作为独立基础件主域。
  - 保持 HAOFV 边界：TDMA owner 管 runtime、payload、adapter 和 evidence；VDC 只消费 timestamp，RefMem 只消费 payload completion。
- 完成内容：
  - 新建 `docs/tdma/` 标准三件套和 README。
  - `tdma_service` 已增加 ring runtime config/snapshot 字段，用于表达 active node、local/reference slot、UP/DOWN group、ring seq、running state、profile CRC 和 schedule CRC。
  - `tdma_service_configure_ring_runtime()` 拒绝 bad config：节点数不足、slot 越界、UP/DOWN group 相同、缺少 simultaneous flag、CRC 为 0。
  - 单元测试增加公共 TDMA ring runtime contract，验证 `up_running/down_running` 可由 runtime 发布，但 `simultaneous_feedback_loop_evidence` 仍保持 0，防止把配置就绪误判为闭环。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过 ARM GCC 编译门禁；当前环境未找到 host gcc，因此 host 执行跳过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，输出 `vdc_domain tests passed`。
  - `python tools\docs_check\docs_check.py` 通过，`files=90 warnings=2`；仅保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 和 `VDC_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `python -m py_compile tools\docs_check\docs_check.py` 通过。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260817043820`，package CRC `0xB91E63B5`。
- 还需完成：
  - 更新 `docs/README.md`、HAOFV 架构和 VDC 文档。
  - 后续把 TDMA 作为 HAOFV system node 接入 NodeLoad / DeploymentGate。
  - 后续建立真实 core1 同时 UP/DOWN runtime 和硬件 timestamp correlation。
- 关联文件：
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_refmem_realtime_tdma.c`
  - `docs/tdma/README.md`
  - `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
  - `docs/tdma/TDMA_DOMAIN_TODO.md`
- 下一步：
  - 按 HAOFV 索引和 VDC 文档收敛 TDMA ownership，再运行 host/doc 验证。

### TDMA-TASK-20260828-003 - 有界恢复重传负载与四节点烧录验证

- 状态：代码、预算、构建、五板 OTA 和四节点 TDMA HIL 已完成；NO5 保持 SMA/DPLL 观测角色。
- 日期：2026-08-28
- 任务目标：
  - 为可靠错误恢复提供两个独立 recovery buffer，限制每周期最多发送一帧，且不侵入正常 traffic class 或 guard/VDC/REFMEM 预算。
  - 将 recovery 预算投影到正式预算工具，并用同一固件包完成板端验证。
- 完成内容：
  - 固化双 buffer `EMPTY/READY/IN_FLIGHT` 状态机、单周期单帧 dispatch、一次有界 retry、deadline/retry-limit fail-closed、成功清空和 backpressure 计数。
  - scheduler/service/RefMem snapshot 与 SCPI status 暴露 recovery reserve、buffer、depth、queued/dispatched/sent/retry/exhausted/backpressure 计数。
  - profile validation 将 `TDMA_RECOVERY_RESERVED_BYTES_PER_CYCLE` 纳入 capacity 门禁；预算工具输出正常 `712 B`、恢复 `128 B`、计划 `840 B`、余量 `56 B`，并记录两个 buffer/单周期一帧约束。
- 验证结果：
  - `run_tdma_profile_tests.ps1`、`run_tdma_traffic_scheduler_tests.ps1`、`run_tdma_service_scheduler_tests.ps1`、`run_refmem_realtime_tdma_tests.ps1` 通过；Python TDMA field/budget pytest `12 passed`。
  - `python tools/cmake_build_auto/cmake_build_auto.py --preset pico2-rtos-multicore-smoke --build-dir out/build/recovery-20260828` 通过，app/A/B/boot Flash link gate 全部通过；build id `20260828025009`，package 位于 `out/build/recovery-20260828/DHRT100_UPDATE.pkg`。
  - `ota_multi_update.py` 对 COM25、COM3、COM4、COM5、COM6 异步 OTA：`passed=True`、`failed=0`，设备均 commit 到 `20260828025009`，证据在 `out/ota/tdma-recovery-20260828-r2/summary.json`。
  - 按已验证 NO1→NO4 顺序 COM3/COM4/COM6/COM5 运行 `tdma_start_ring.py`，8 个训练周期后四节点 `up_running=1`、`down_running=1`，TX/RX 增长且 `ring_adapter_rx_bad_count=0`；证据在 `out/tdma/ring-baseline-20260828-four/`。
  - 误将 NO5 加入 TDMA ring 时 ARM 在 NO2 超时；已确认 NO5 是 SMA/DPLL 观测节点而非 TDMA 环路节点，随后对全部五板执行 STOP，设备处于安全停机状态。
- 下一步：
  - 在四节点 TDMA 环路保持稳定的前提下，增加受控故障注入，验证 recovery queued→dispatched→ACK 清空、双 buffer 交替、第三个并发错误 backpressure 和 retry exhaustion；再更新 TDMA-M4/M5 退出门禁。
