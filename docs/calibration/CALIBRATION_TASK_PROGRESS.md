# 校准域任务记录

Status: Active
Domain: CALIBRATION
Canonical: `docs/calibration/CALIBRATION_TASK_PROGRESS.md`
Related: `docs/calibration/CALIBRATION_DOMAIN_TODO.md`, `docs/calibration/CALIBRATION_TDMA_CLK_TRAINING_PLAN.md`, `docs/calibration/CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/vdc/VDC_TASK_PROGRESS.md`
Last updated: 2026-08-26

本文档记录校准域从方案、粗捕获到双向测距和 VDC/DPLL 接入的实际进展。记录中的 HIL
结果必须绑定 build、拓扑、profile、接线和证据目录；未绑定这些上下文的数字只能作为
诊断快照，不能作为 active calibration 或产品精度承诺。

## CAL-TASK-20260826-010 - TRN-03A/B 四板短帧与 process-image 闭环

- 状态：TRN-03A staging/ARM gate、TRN-03B raw-flight 和 process-image/FIFO 四板闭环已通过；
  本任务仍保持 `DIAGNOSTIC_ONLY`，下一阶段进入 TRN-03C active candidate gate。
- 代码闭环：process follower 的 END 路径在 PIO IRQ 后显式回到 command pull，避免落入 PASS
  多采一个 raw byte；RX DMA absolute producer 使用 fixed-frame boundary 重建并保持单调；单帧
  解析失败排入 PASS 维持环路，迟到结果通过 `overlay_late_coalesce_count` 与真实
  `overlay_prepare_fail_count` 分离。
- staging/拒绝证据：TRN-03A 写后读回、ARM 和负门禁目录为
  `out/training/trn03a_g210_stage_after_data_fix_20260826/` 与
  `out/training/trn03a_negative_gates_final_20260825/`；训练输入统一来自
  `out/training/trn03a_g210_matrix_clk_sys_phase_20260826.json` 的 row0。
- raw-flight 证据：四板产品 flight persona 的最近复验证据位于
  `out/training/trn03b_g210_raw_flight_after_data_fix_retry2_20260826/`，与 process-image 使用同一
  calibration/topology/profile/schedule identity。
- 最终 process-image HIL（诊断快照，非事实源）：release build 为 `20260826055402`，四板异步
  OTA 目录为 `out/ota/trn03-process-late-coalesce-v9-20260826/`；STOP -> ARM 重复通过证据为
  `out/training/trn03b_process_late_coalesce_v9_row0_retry1_20260826/`。四节点 up/down、sequence、
  transport CRC、TX/RX FIFO、map apply 和 bitmap 均增长，follower replacement 增长，adapter
  bad 与 overlay prepare failure 均不增长，finally STOPPED 通过。
- PIO 计数证据（同一诊断快照）：follower 的 absolute producer 与
  `overlay_frame_boundary_count * overlay_physical_byte_count` 保持同一 fixed-frame 坐标，
  alignment byte/bit 在观测窗口前后不漂移。Node2 的迟到解析被计入 late coalesce，不再阻塞
  core1 或污染物理失败计数。
- SD raw SCK 证据（同一诊断快照）：四节点 `ring_capture_analysis.json` 均通过 frequency gate，
  period/high/low/duty 由 PIO raw sample 独立派生；逐节点 `1 us` SVG 位于最终证据目录的
  `analysis/`，不依赖 transport frame 是否能在局部窗口完整解码。
- 回归与交付：定向 Python 回归结果为
  `out/pytest/trn03-process-late-coalesce-results.xml`；release 构建目录为
  `out/build/trn03-process-late-coalesce-v9/`。代码提交 `c81defc` 已推送到
  `feature/rtos-multicore-haofv`。
- 下一步：TRN-03C 汇总 per-link path/residence/loop delay、PIO cycle budget 与 residual，执行
  bias、hardware latch、freshness、CRC、重复性和 rollback 门禁；通过前不得发布 active
  calibration。

## CAL-TASK-20260826-009 - MARK/SCK/DATA 统一相位训练路径

- 状态：已完成代码与 host 工具收敛，并完成本 build 的四板 OTA、SCK 零 offset 基线及
  推荐矩阵动态加载复验；MARK/DATA 的统一路径复验和 TRN-03 staging 尚未完成，因此保持
  `DIAGNOSTIC_ONLY`，不发布 active calibration。
- 统一模型：代码事实源为 `calibration_training_phase.h` 与 `calibration_phase.py`；MARK、
  SCK、DATA 均按 `link_base_delay = measured_link_delay / 2`、`effective_phase =
  round(link_base_delay / sample_period) + node_offset` 计算。codebook half-chip 只参与波形
  编解码，不再作为物理链路 base。
- 统一阶段：三种信号均输出 `HAOFV_UNIFIED_PHASE_TRAINING_V1`，按 PIO 起始边沿、raw capture、
  SD 保存、离线相关/SVG、零 offset 基线、全量 Node offset 矩阵、动态加载和 residual repeat
  gate 执行；电气引脚和 PIO persona 只作为 adapter 差异。
- 统一矩阵：`build_offset_rows()` 是全量 Node 笛卡尔积唯一生成器；MARK 调用该生成器，SCK
  和 DATA 通过 `build_observed_offset_matrix()` 生成相同通用 row，并保留信号专用兼容字段。
  offset 范围与 Node 容量引用 `CALIBRATION_TRAINING_PHASE_*`，不在各训练项目重复定义。
- 固件构建：`out/build/sck-independent/DHRT100_UPDATE.pkg` 已成功生成；本次诊断快照的
  `payload_sha256` 为 `b3006a96e61e0495174421b3c5dd6ee4ff721c4cd8dd28c5609f35542c4e57ad`。
- 四板 OTA（诊断快照）：`out/ota/unified-phase-20260826/summary.json` 记录全部目标 Node 更新
  成功并读回 package build；板卡身份、端口、旧/新 build 和逐板日志均保存在该目录。
- SCK 零 offset 基线（诊断快照）：证据目录
  `out/training/sck_unified_baseline_0000_20260826/`。全环独立 repeat 全部 accepted；各 link
  的最终 offset 众数均为 `+1` sample，生成的推荐矩阵为 `[+1,+1,+1,+1]`。全部 SD raw
  capture 均生成对应的 `node*_link*_sck_capture_replay_1us.svg`。
- SCK 动态加载复验（诊断快照）：证据目录
  `out/training/sck_unified_offset_p1p1p1p1_20260826/`。推荐矩阵加载后全环 repeat 全部
  accepted 且无 gate failure；residual 以零拍为众数，少量 trial 落在相邻的负一拍，最终
  推荐矩阵保持 `[+1,+1,+1,+1]`。这属于原始采样网格的量化分布，不再回写第二套算法。
- 回归：公共路径及 MARK/DATA/SCK/TRN-03 定向 Python 测试通过；MARK、DATA、SCK C host
  单测通过。测试数量属于本次运行快照，事实源为 `out/pytest/` 和构建日志。
- 下一步：用同一 package 和统一工具复验 MARK、DATA 的零 offset 基线、逐 Node SVG、矩阵
  动态加载及 residual repeat；三类矩阵均 accepted 后复验 TRN-03 staging 写后读回与 raw-flight。

## CAL-TASK-20260825-008 - MARK 扩样检查点与 SCK 独立训练决策

- 状态：已完成 MARK 矩阵工具归因修复、固定 identity 的零 offset 扩样和离线 SVG 对齐；
  已确认 SCK 必须使用自身 capture origin 独立完成两级训练。SCK 解耦尚未实现，本检查点
  不继续刷板、不发布 active calibration，TRN-03A 恢复为进行中。
- 代码检查点：当前分支与远端均为提交 `b69f1a8`；板端仍运行 build `20260825083548`。
  本轮没有固件修改，因此没有重新构建或 OTA。
- MARK 工具修复：
  - `calibration_marker_train.py` 与 `calibration_marker_offsets.py` 将 offset 归因从错误的
    `source_node` 改为实际加载 capture offset 的 `destination_node`；matrix 默认固定
    epoch/codeword，并让每个 row 进行多次独立 `STOP -> ARM -> inject`。
  - 默认 repeat 数、逐 node gate、distance 统计和推荐 row 已固化在工具中；本地
    `topology_generation` 不再错误要求跨板相同，topology/profile/schedule CRC 仍必须一致。
  - 定向回归为本轮诊断快照：`43 passed`，pytest 目录为
    `out/pytest/mark_matrix_repeat_fix/`。
- MARK 零 offset 扩样（诊断快照，非事实源）：固定 codebook `1`、epoch `91` 和
  `[0,0,0,0]`，共执行 `32` 次独立 trial。证据目录为
  `out/training/mark_baseline_0000_repeat8_cb1_e91_g136_20260825/` 与
  `out/training/mark_baseline_0000_repeat24_cb1_e91_g144_20260825/`。

| Node | accepted | best lag 分布 | distance median | distance max |
|---|---:|---|---:|---:|
| node0 | 25/32 | `0:29, 1:3` | 441.5 | 1202 |
| node1 | 32/32 | `0:15, 1:17` | 1 | 228 |
| node2 | 32/32 | `0:25, 1:7` | 37.5 | 480 |
| node3 | 29/32 | `0:27, 1:5` | 129.5 | 954 |

- 解释：独立 ARM 后环路会落入相邻的 raw sample 相位状态，一拍量化变化属于正常观测；
  静态 offset 必须依据更大样本的拍差直方图、众数/中位数和拒绝比例选择，不能要求每次
  distance 完全相同，也不能凭单张 SVG 决定最终矩阵。node0 返回和 node3 的拒绝必须保留
  为有效失败 evidence。
- SVG 对齐：g136、g137、g138 的候选 delay 分别为 `[-4,0,0,-4] ns`、
  `[+4,+4,+4,+4] ns` 和 `[-4,0,-4,-4] ns`；换算关系为
  `offset_delta_samples = -candidate_delay_ns / sample_period_ns`，本轮 sample period 快照为
  `4 ns`。JSON/SVG 位于基线目录的 `baseline_analysis/g136/`、`g137/`、`g138/`。
- 失败搜索证据：`[+3,-2,0,+2]` 与 `[+2,-2,0,+2]` 两个候选 row 均各执行独立 repeat，
  因 node0 返回重复门禁失败；证据分别位于
  `out/training/mark_matrix_repeat8_p3m2p0p2_cb1_e91_g120_20260825/` 和
  `out/training/mark_matrix_repeat8_p2m2p0p2_cb1_e91_g128_20260825/`。失败不从训练集删除。
- SCK 架构审计：当前 `calibration_training_sck.c` 和 `calibration_sck_train.py` 的公式/请求
  仍包含 `source_marker_offset_sample_count`、`destination_marker_offset_sample_count` 和
  `marker_to_sck_samples`，会把 MARK 的一拍量化误差叠加到 SCK。正确路径是 SCK 使用 PIO
  内部启动、已知 SCK burst 和返回 raw capture，独立形成 SCK-TRN-01/02 offset matrix；
  `mark_sck_skew` 只作为最终 guard/window 验收，不回写任一信号的物理 offset。
- 下一步：先移除 SCK 相位公式对 MARK offset 的依赖并补 host 回归；随后用固定 identity、
  独立 repeat 和全量 node matrix 运行四板 SCK 基线，保存 SD raw capture、逐 node `1 us` SVG、
  拍差分布和失败比例；完成后重新生成并验证 TRN-03A staging，再进入 raw-flight。

## CAL-TASK-20260825-007 - TRN-03B 原始短帧飞行 persona 接通

- 状态：完成 raw byte-level flight 的代码、工具分级、host 单测和固件构建；尚未刷入四板，
  `raw-flight` HIL 未通过前不得进入 `process-image`，TRN-03B 保持进行中。
- 完成内容：
  - reference 在 TDMA ARM 时选择 `TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_ORIGIN`，follower 选择
    `TDMA_PIO_SPI_PROGRAM_PERSONA_FLIGHT_FOLLOWER`；reference 预装 DATA DMA 并产生有界 CS/SCK
    burst，返回流由独立 RX capture 接收。
  - follower PIO 在同一 wire pass 中再生 SCK、反向流水 DATA 并捕获 RX；修复 `push` 清空 ISR
    导致转发字节为零的问题。ring adapter 的 `PHYSICAL_FLIGHT` 模式不再执行完整 RX 后的
    第二次 software TX，产品 runtime 固定选择该模式，host fake phys 保留 store-forward 测试模式。
  - RX scanner 支持 bit-shift 恢复 packet magic；历史 `copy_normal_capture` API 兼容 NORMAL 与
    两种 flight persona，保留既有 SCPI、SD capture 和 SVG 工具链。
  - `trn03_closed_loop.py` 增加 `raw-flight` 与 `process-image` 两级门禁；前者验证 persona、
    sequence、CRC 和物理计数，后者才要求 TX/RX FIFO 与 map apply 增长。
- 验证结果：
  - `tools/tests/run_tdma_pio_spi_ring_adapter_tests.ps1` 通过，构建目录为
    `out/pytest/build-tdma-flight-ring-adapter`。
  - TRN-03 定向 Python 回归通过，临时目录为 `out/pytest/trn03-flight-runtime-temp`。
  - `tools/cmake_build_auto/cmake_build_auto.py --preset pico2-release` 构建通过，产物目录为
    `out/build/trn03-flight-runtime`；具体 package size/CRC 属本轮构建快照，以目录内产物为准。
- 未完成边界：当前 follower 是透明 byte pipeline，尚未在本 node 的固定 segment 到达时从
  active TX image 替换内容；飞行修改后的 WKC、尾部 transport CRC、segment 完整性以及四板
  SD capture/SVG 证据均未完成，不能宣称 EtherCAT-style process-image flight 已闭环。
- 下一步：使用现有多板异步 OTA 工具刷入本轮 package，只执行
  `trn03_closed_loop.py --stage raw-flight`。若失败，优先核对 persona、bit-shift magic、DMA
  produced words、timeout/stall、训练 offset 和 tail budget，并保存全部 node 的 SD capture/SVG。

## CAL-TASK-20260825-006 - TRN-03B NORMAL 波形闭环与捕获消费恢复

- 状态：TRN-03B 的板端 NORMAL RX/TX 捕获、Core1 latch、SD 保存、四板并行下载和逐 node
  SVG 已形成可重复诊断闭环；捕获请求不再因一次未消费而永久停在 `PENDING`。TDMA 短帧环路
  本身仍未通过，因此 TRN-03B 保持进行中，不发布 active calibration。
- 已确认原因：旧请求入口把 `PENDING` 当作不可覆盖状态。若某次 core0 发布恰好未被 Core1
  及时消费，所有后续 latch 都被拒绝，偶发 park/stall 或观察窗口错过会被放大为永久不消费。
  修复后 mailbox 采用 latest-wins，新 sequence 可覆盖旧 pending；generation、capture epoch
  和 guarded snapshot 仍阻止保存旧证据。host 默认进行有界重试，并记录 `latch_attempts`。
- 诊断边界：旧固件首次停顿发生时尚无阶段计数，因此不能从既有证据严格区分 Core1 瞬时
  park/stall 与 intent 未观察。新固件增加 `core1_service_count`、
  `intent_read_fail_count`、`last_seen_sequence`、`copy_attempt_count`、
  `copy_fail_count` 和 `consumed_sequence`。本轮最终四板 HIL 中，每个 node 首次 latch 即被
  消费，intent 读取与物理快照复制均无失败；这证明恢复路径和复制路径有效，但不倒推旧瞬态
  的唯一成因。
- 波形语义修正：RX 保存 SCK 上升沿原始采样流；TX 保存最近一个完整短帧，而不是连续历史
  尾部。捕获容量和版本引用 `TDMA_PIO_SPI_NORMAL_CAPTURE_BYTES` 与
  `TDMA_PIO_SPI_NORMAL_CAPTURE_VERSION`。旧数据长度可由完整帧长度整除，历史工具截取的
  尾部恰为零填充，因此旧 SVG 的全零 TX 不是内存破坏。V2 分析器找不到完整 RX 帧头时发布
  `N/A` 和原因，不再误报零 shift。
- 固化工具：`tools/calibration_ring_validate/trn03_waveform.py` 负责 V1/V2 读取、完整帧校验、
  方向矩阵对比和 SVG；`trn03_closed_loop.py` 负责 Core1 latch、SD save、四板并行下载、分析
  及 finally STOP；`trn03_matrix.py` 把 capture 元数据纳入 TRN-03 配置。训练层仅使用
  node/link/loop 术语。
- 构建与 HIL（本轮 bench 快照，非事实源）：Release 产物位于
  `out/build/trn03b_capture_retry`，build `20260825051259`；四板异步 OTA 证据为
  `out/ota/trn03b_capture_retry_20260825/summary.json`。最终运行证据位于
  `out/training/trn03b_capture_retry_final_20260825/summary.json`，各 node 的 SD 保存、下载、
  消费诊断和 STOPPED 回退均成功。
- SVG 证据：`out/training/trn03b_capture_retry_final_20260825/analysis/` 下的
  `node0_ring_capture_1us.svg` 至 `node3_ring_capture_1us.svg`。本轮波形显示 node0 发送完整帧，
  node1 在 node0 marker/SCK 下只捕获到 idle-low DATA，node2/node3 未形成后续 TX；这与
  “完整帧 RX 后才 TX”的环形等待一致。下一步必须实现 PIO cyclic/cut-through 数据转发，
  不能靠继续调整 offset 解除该等待。
- 回归证据：定向 Python 测试结果为
  `out/pytest/trn03b_capture_retry_20260825.xml`；本条中的通过数、build 和计数均仅代表本轮
  evidence snapshot，不是接口常量。

## CAL-TASK-20260825-005 - TRN-02 固定阶梯退出与 TRN-03A 四板 ARM

- 状态：`TRN-02B/D` 已完成固定 operating-profile 阶梯的四 link 多次重复门禁，
  `TRN-03A` 已完成完整 replay matrix 的自动生成、四板 staging、ARM 状态读回和
  STOPPED 回退。`TRN-03B/C/D` 尚未完成，当前结果不发布为 active calibration，
  也不向 VDC/DPLL 提交。
- 固件与拓扑（bench 诊断快照，非事实源）：四板 build 为 `20260825023511`，物理 node
  顺序为 `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A ->
  A1E549202D18ED6A -> 0010071E65B5CB38`。MARK 使用 codebook 1 和 node offset
  `[+1,-1,0,+1]` 拍；DATA 使用 codebook 0、reverse 方向和 configured offset
  `[+5,+5,+5,+5]` 拍；以上均为本轮训练配置，不是固件写死值。
- TRN-02 正向证据（本轮快照）：level 7/8/9 分别绑定 calibration generation
  `101/102/103`。每档 residence 的四条 link 均为三次 `1` 拍且 span 为 `0`；每档 DATA
  均为 `12/12` accepted，四 link repeat span 不超过工具参数
  `max_offset_span_sample`。三档 profile gate 的 identity、完整 link 集、DMA/PIO/timeout
  和 residence 配对检查全部通过。
- 固化工具：`trn02_profile_gate.py` 改为按 level 接受 DATA/residence 成对 summary，拒绝
  缺档或混合 identity；新增 `trn03_matrix.py`，从 paired evidence 自动派生 NORMAL PIO
  周期、DATA window、codeword、forward residence、loop delay 和 link budget。生成器通过
  源码锚点测试约束 persona、系统时钟、bit cycles 与 operating-profile 表，并先按 RP2350
  PIO 16.8 分频器量化 `clkdiv` 后再派生各 cycle，禁止人工填写 residence。
  `trn03_stage.py` 增加 stage/link 全字段写后比对、逐 node ARM 状态读回和
  STOPPED 回退读回；训练报告不复用 `slot` 名称。
- TRN-03A HIL（本轮快照）：level 9 generation 103 的完整四 link matrix 在四板均读回
  `complete=1`、valid bitmap `15`；ARM 后四板均为 ring enabled、adapter started 且 local
  node 为 `0..3`，随后 STOP 后均恢复 disabled/stopped。此前空矩阵、缺 link、
  diagnostic-only 和预算过期的四板负向门禁继续作为拒绝证据。
- 证据索引：profile gate 为
  `out/training/trn02_profile_gate_g101_g103_20260825.json`；三档 matrix 为
  `out/training/trn03_matrix_level7_g101_20260825.json`、
  `out/training/trn03_matrix_level8_g102_20260825.json`、
  `out/training/trn03_matrix_level9_g103_20260825.json`；正式 ARM 为
  `out/training/trn03a_level9_g103_arm_quantized_20260825/summary.json`；负向门禁为
  `out/training/trn03a_negative_gates_final_20260825/summary.json`；Python 固化回归为
  `out/pytest/calibration_trn03_final_20260825.xml`。
- 下一步：进入 `TRN-03B`，按 ring role 装载产品 flight persona 并先执行四板
  `raw-flight`；通过后再要求 TX/RX FIFO、segment replacement 和 map apply 同时增长。
  失败必须保持当前 STOPPED 回退语义。

## CAL-TASK-20260824-004 - TRN-02 profile gate、offset 故障注入与 TRN-03A staging

- 状态：TRN-02 的三组 profile 证据已由固化工具聚合；offset 故障注入已增加离线回归，
  但 TRN-02B/D 仍不能宣称完成，因为 forward residence 尚未与每个 profile 的同一
  calibration generation 绑定。无 residence 或 generation/profile 不匹配时，gate 明确失败。
- 固化工具：`tools/calibration_ring_validate/trn02_profile_gate.py` 汇总 level 7/8/9
  的 12/12 link trials、profile identity 和 residence 兼容性；
  `tools/calibration_ring_validate/trn02_offset_fault.py` 生成偏移故障的预期失败类别。
  当 offset 将窗口推出边界时，预期为 `TIMEOUT_EXPECTED_WINDOW_MISSED`；结果禁止
  active candidate 和 TRN-03 staging。
- TRN-03A 代码入口已增加 2..8 节点的 per-link calibration stage、generation/topology/
  profile/schedule identity、PIO 时序字段和 link budget 检查；缺链路、混合身份或
  不可重放预算在 ARM 前拒绝，并保持 STOPPED。当前仍需补齐板端 stage/query 编排和
  四板 HIL，因此 TRN-03A/B/C/D 不改变为完成。
- 验证：`out/training/trn02_profile_gate_20260824.json` 明确记录当前 gate failure；
  Python calibration tests 10/10、TRN-02 offset fault manifest、C training-data 和
  TDMA ring-runtime host tests 均通过。

## CAL-TASK-20260824-003 - TRN-02 四链路单 profile 收敛与 TRN-03 入口审计

- 状态：`TRN-02A` 和 `TRN-02C` 已完成；`TRN-02B` 和 `TRN-02D` 仍为进行中。当前
  四链路单 profile 矩阵通过，但尚未满足多次独立 repeat 和固定频率阶梯全 profile
  验证，因此不得进入四板 TRN-03 ARM/START。
- 固件与拓扑（bench 诊断快照，非事实源）：四板均运行 build `20260824125459`；
  accepted physical order 为 `0010071E65B5CB38 -> FB276192BEF9CCE1 ->
  2BD5090FE009FA2A -> A1E549202D18ED6A -> 0010071E65B5CB38`。
- 运行时训练参数（本轮快照）：MARK 方向为 forward，DATA 方向为 reverse；
  MARK per-Node offset 为 `[+1,-1,0,+1]` 拍，DATA per-Node configured offset 为
  `[+5,+5,+5,+5]` 拍；采样周期为 `4 ns`，逻辑基准 delay 为 `40 ns`。方向和 offset
  均由 host 按现场拓扑动态提交，未写死到 PIO 程序。
- HIL 结果（本轮快照）：calibration generation `87`，epoch `126..129`，四条
  directed link 全部 accepted；topology/profile/schedule CRC 和 sample period 在矩阵内一致，
  DMA overrun、PIO stall 和 timeout 均未增长。残差为 `[-1,-1,0,0]` 拍，故校准后
  逻辑 delay 为 `[36,36,40,40] ns`，均在 `40 ns ± 1` 拍内。
- delay 语义：校准后逻辑 delay 按 `base_delay + residual` 计算；物理捕获中心按
  `base_delay + configured_offset + residual` 计算。DATA configured offset 只调整 initiator
  capture phase，不并入同时参与长等待的 `base_delay_ns`，避免重复补偿。
- 证据目录：`out/training/trn02d_data_offsets_5555_e126_g87_20260824/`；根
  `summary.json` 记录 `TRN-02D_REPEAT_MATRIX`、`repeats=1`、四链路各自判定和统一
  identity bundle；每条链路目录保存 SD capture 下载、离线 replay JSON 和 `1 us` SVG。
- 构建与部署证据（本轮快照）：固件位于 `out/build/trn02-configured-data-offset/`；
  四板异步 OTA 证据位于 `out/ota/trn02-configured-data-offset_20260824/`；配置 offset 与
  delay 标签回归报告位于 `out/pytest/calibration_configured_offset.xml` 和
  `out/pytest/calibration_delay_label.xml`。
- TRN-03 入口审计：现有矩阵只含一个 profile CRC，且每条链路只有一次最终
  repeat；`TRN-02B` 要求的 forward residence 仍需与同一 generation 证据绑定。下一步
  使用已固化的 `tools/calibration_ring_validate/calibration_data_train.py` 跑完 operating
  profile 固定阶梯的四链路多次独立 repeat；所有 profile 通过后才关闭
  `TRN-02B/D` 并开始 `TRN-03A`。
- 回退点：任一 profile/link/repeat 失败均保留 raw evidence，整环保持 STOPPED，
  不修改 active calibration generation，不向 VDC/DPLL 发布本轮 diagnostic window。

## CAL-TASK-20260824-002 - TRN-01 退出与 TRN-02 入口准备

- 状态：`TRN-01A..D` 已按当前 build/拓扑的 diagnostic 门禁收口；`TRN-02A` 进入实现期，
  尚未开始独立 DATA 码元训练，不得把 CS marker offset 提升为 DATA window 或 active calibration。
- 固件与拓扑（bench 诊断快照，非事实源）：四板均运行 build `20260824090429`；accepted
  physical order 为 `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A ->
  A1E549202D18ED6A -> 0010071E65B5CB38`。板卡身份只使用 `*IDN?` 唯一地址，COM 号仅为
  本轮临时传输端点。
- TRN-01 退出证据：
  - 零 offset 基线使用 epoch `90`、calibration generation `60`，四节点全部 accepted；ARM
    阶段四节点均为 PREPARED 且 `dma_capture_count=0`，证明 INJECT 前不存在假 marker 边沿。
  - 依据离线全局重叠方向动态加载 `[+1,-1,0,+1]` 个 sample 的四节点 offset；一拍为
    当前 capture 的 `4 ns` 采样周期。本轮 epoch `91`、calibration generation `61`，四节点
    仍全部 accepted，firmware distance 为 `9/7/8/9`，离线最佳残差均为 `0 ns`。
  - 两轮均满足同一 epoch/sequence/marker CRC、normal polarity、完整 marker flags、node0
    返回 marker、DMA overrun/stall/timeout 为零；训练结束恢复 NORMAL persona。
  - 一拍结果只说明当前 CS marker capture window 在该 build、topology、codebook 和节点状态下
    收敛；它不是独立 DATA offset，也不清除 `DIAGNOSTIC_ONLY`。
- 证据目录：
  - 零 offset：`out/training/trn01_arm_inject_marker_latch_baseline_e90_0000_20260824/`；
  - 一拍复核：`out/training/trn01_arm_inject_one_sample_p1m1p0p1_e91_g61_20260824/`；
  - 两个目录均包含 `summary.json`；一拍目录额外保存四节点 SD capture、离线 replay JSON 和
    参考/原始/最佳对齐三行的 `1 us` SVG。
- TRN-02 入口审计：
  - 当前 marker persona 在 CS 输入/输出线上生成、转发和捕获 coded marker；现有相关器、
    golden vector、CRC/epoch/polarity/best/second peak/margin 基础逻辑可以复用。
  - 当前尚无独立的 marker 后 DATA TX/RX PIO 路径，也没有 `marker_data_skew`、DATA
    best/second peak、training window/guard staging。因此 marker HIL 不能替代 `TRN-02A/B`。
  - 下一硬件入口固定为 `node0 -> node1` 单链路：先由 PIO 在 marker 后发送已知 DATA
    codeword，再以 P3 per-link diagnostic candidate 形成有界搜索窗口；只有单链路连续 trial
    accepted 后才轮换其余 directed link。
- TRN-02A 软件门禁基座：
  - 新增 `calibration_training_data` guarded request/evidence/snapshot，独立保存 source/destination、
    epoch/sequence、codebook/CRC、generation/CRC bundle、sample period、marker-to-DATA 间隔、
    base delay、搜索区间、guard、best/second peak、margin、polarity、window 和 skew。
  - evaluator 覆盖 generation、旧 epoch、sequence、CRC、evidence flags、相关器拒绝、反相、
    capture truncation、DMA/stall/timeout、distance、margin、search range 和 edge order；接受结果
    始终保留 `DIAGNOSTIC_ONLY`。
  - 固化 `tools/tests/run_calibration_training_data_tests.ps1`；host C 测试覆盖正常、单拍错位、
    反相、截断、重复峰、旧 epoch、越界搜索、DMA fault 和非法窗口。
  - 固件 A/B/Boot 在 `out/build/calibration-marker-arm-inject-marker-latch/` 完成构建，Flash map、
    persistence、wire、SCPI namespace 和三镜像 link gate 通过；本轮未 OTA，因为独立 DATA PIO
    和板端 SCPI 尚未接入。
- 回退点：TRN-02 任一失败只停止 training persona、保留本任务的 TRN-01 diagnostic evidence
  和旧 active generation；不得 ARM 四板 TDMA，不得向 VDC/DPLL 发布结果。

## CAL-TASK-20260824-001 - TRN-01 字段基座与四板 marker 基线

- 状态：`TRN-01A` 进行中；C snapshot、seqlock store、拒绝矩阵和 host evidence parser
  已建立，尚未接入 core1 marker persona 或板端 SCPI，因此不得声称 `TRN-01` 环路通过。
- 四板基线（本轮诊断快照，非事实源）：使用现有固化工具
  `tools/calibration_ring_validate/calibration_clk_coded.py`，按 accepted physical order 在统一
  旧固件上执行四主 coded-marker trial，四个 master 均 accepted，marker 完整性、DMA 和
  PIO fault 门禁通过。证据位于 `out/training/p2_marker_baseline_20260824/`；该结果仍是 P2
  coded RTT，不包含每个 follower 的 marker capture/forward tick，不能替代 `TRN-01D`。
- 实现：新增 `calibration_training_marker` request/evidence/snapshot 模型，覆盖 identity、node、
  epoch/sequence、marker/codebook/CRC/polarity、generation/CRC bundle、硬件 capture/forward tick、
  residence、DMA/PIO/timeout 和 `DIAGNOSTIC_ONLY`；新增
  `tools/calibration_ring_validate/calibration_marker_train.py`，只校验固件导出的硬件证据，
  不允许 host 生成或修正边沿时间戳。
- 验证（本轮构建/测试快照，非事实源）：C marker 单测通过；校准相关 Python 回归通过；
  Release 固件在 `out/build/trn01-marker/` 完成编译和 Flash link gate，构建产物尚未部署到
  四板。pytest 和 C host-test 临时产物均位于 `out/pytest/`。
- 下一步：实现 `TRN-01B/C` 的 core1 PIO marker capture/cut-through persona 与 bounded
  intent/prepare-ack，增加板端 `MARKERTRN` 状态查询；只有四板同 epoch 的 capture/forward
  evidence 经 host 工具验证后，才把 `TRN-01A` 和 `TRN-01D` 关闭。

## CAL-TASK-20260822-007 - P2 分辨率输入下的四板 TDMA 环路运行

- 状态：诊断运行完成但环路未闭合；校准数据仍为 `DIAGNOSTIC_ONLY`，不得进入 active
  calibration 或 VDC/DPLL。
- 目标：在 P2 编码 marker 已将 raw 采样细化到 4 ns 的前提下，使用校准结果为 TDMA
  四板环路提供初始 loop-delay 窗口，并记录运行态证据。
- 固件与身份：四块板均为 DHRT100 build `20260822111137`；身份只取 `*IDN?` 唯一地址，
  COM3/COM5/COM6/COM4 仅为本轮临时传输端点。accepted physical order 为
  `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A -> A1E549202D18ED6A ->
  0010071E65B5CB38`。
- P2 输入：四主节点 level 8 重复结果为 `10/10` accepted，best lag 为 `100/101`
  个 raw sample，即 `400/404 ns`；单次硬件分辨率按 `sample_period_ns=4 ns` 记录。
  本轮只将 `402 ns` 和 `8 ns` 容差作为候选窗口，不能视为已经写入板端的正式校准值。
- TDMA 运行：使用既有
  `tools/tdma_ring_monitor/tdma_start_ring.py`，按四个唯一地址配置 node count 4、
  reference node 0，level 8，训练 `4096` cycles，然后 ARM/START。四板 topology、node
  map 和训练命令均完成；证据目录为
  `build-product-release/tdma_ring_p2_runtime_20260822`。
- 运行结果：启动快照中四板均为 `ring_node_count=4`、`ring_adapter_started=1`、
  `ring_up_running=1`，但 `ring_down_running=0`；参考板 TX 增长而 RX 未增长，四板
  `ring_adapter_rx_bad_count` 未见增长。因此本轮结论为 `UP_ONLY / LOOP_NOT_CLOSED`，
  不是稳定四板环路通过。
- 配置边界：对四板查询 `SYSTem:TDMA:RING:LOOP:DELay?` 均未得到响应，不能据此声称
  `loop_delay_ns=402,tolerance=8` 已生效。需先确认板端 build 已包含该 SCPI/配置路径，
  在 STOPPED 状态写入并读回，再重新 ARM/START。
- 只读监控：运行期间使用
  `tools/tdma_ring_monitor/tdma_ring_monitor.py` 对 NO.1/NO.2 的持久串口端点执行
  90 秒只读采样；监控证据目录为
  `build-product-release/tdma_ring_p2_runtime_monitor_20260822`。监控不得替代四板全量
  运行态证据，结束后必须对全部节点执行 STOP 并关闭串口。
- 下一步：确认并 OTA 含 loop-delay 配置的统一固件；停止当前环路后四板读回 loop-delay、
  feedback timeout、up/down 状态，再以 10 MHz/25 MHz 逐级复测。只有四板 `up_running=1`、
  `down_running=1`、参考节点反馈证据增长、bad/overrun 不增长且重复统计通过，才可进入
  P3/DPLL 前置门禁。

## CAL-TASK-20260822-006 - P3 双向测距线角色动态映射

- 状态：代码完成，待四板硬件复测；P3 仍为 `DIAGNOSTIC_ONLY`。
- 固件新增 `CLK_DATA` 与 `CS_DATA` 两组请求。两组分别选择独立的 PIO catalog persona，
  core1 在每次请求完成后停止 SM/DMA 并卸载，不能在同一已装载 persona 内切换信号组。
- P3 以 `forward_line`、`return_line`、`sync_line` 表达角色，不把 CLK/CS/DATA
  物理名称当成功能。`CLK_DATA` 为 `forward=CLK, return=DATA, sync=CS`；
  `CS_DATA` 为 `forward=CS, return=DATA, sync=CLK`。同步线只打开捕获窗口，
  不进入 `t1/t2/t3/t4` 或 path-sum。
- 两组共用逻辑 `t1/t2/t3/t4` 掩码和 path-sum 方程。当前网线链路按正反向对称处理，
  `delay_estimate = corrected_path_sum / 2`；`asymmetry_ns` 和对应 gate 保留，
  后续非对称介质仍可拒绝或降级 active eligibility。
- SCPI `CALibration:P3:STARt` 保留原 5 参数，第 6 参数 `signal_group` 可选，`0=CLK_DATA`、
  `1=CS_DATA`；验证工具 `--signal-group BOTH` 会逐组执行并分别记录证据。
- 本轮验证：主机 P3 测试 8/8、P3 plan 测试 10/10，DHRT100 双核固件和 Flash link gate 通过；
  尚未在四板上 OTA/复测两组信号，不能据此宣称三线硬件校准完成。

## CAL-TASK-20260822-003 - Accepted calibration evidence SD source

- 状态：SD source evidence 已接入；Flash Calibration NVS 仍未实现，不能恢复为 active。
- `calibration_bias_snapshot_validate()` 通过后，`CALibration:SAVE` 才会提交有界文件写入
  intent；Storage manager 以临时文件 + 原子 rename 写入 `/cal/accepted_<unique-id>_g<generation>.json`。
- 文件固定携带 `unique-id`、build、generation、sample/accepted/rejected count、persona/profile/
  topology generation、mean bias、spread 和 snapshot CRC；`active=false` 明确表示它是后续
  Flash M2-03 的输入证据，而不是 VDC/DPLL live fact。
- 当前四块板的 reference-loopback 复测仍为 `edge_mask=0x5`、`result_valid=0`、四边沿时间戳为
  零，因此没有执行成功的 SAVE；该失败证据保存在
  `build-dhrt100-p3-dpll-20260822/calibration_bias_reference/COM3..COM6`。
- 固件 build `20260822095115` 的双镜像、OTA package 和 Flash link gate 通过；四板物理
  P3 仍保持 `DIAGNOSTIC_ONLY`，不能绕过 bias/freshness gate 写入 accepted source。

## CAL-TASK-20260822-001 - P3 path snapshot admission and VDC bridge

- 状态：代码门禁和四板诊断闭环均完成；当前不清除 `DIAGNOSTIC_ONLY`。
- 路径快照增加独立 `max_asymmetry_ns` 门限；active 快照必须具备硬件锁存、bias/topology
  freshness、重复统计和 asymmetry 有效标志，并通过快照 CRC。
- VDC/DPLL manager 新增 Calibration snapshot 发布入口，逐链路复制 delay/jitter、generation
  和 freshness，构造 accepted VDC path-delay table；默认零延迟 staging 表仍被拒绝。
- `calibration_bidirectional` host tests、DHRT100 双核固件构建和 Flash link gate 已通过。
- 四板已恢复枚举；已先用 `*IDN?` 唯一地址确认环序，再执行完整 10/25/30 MHz 频率阶梯。

## CAL-TASK-20260822-004 - P3 当前固件四板复测

- 状态：当前 DHRT100 build 的四板逐链路 P3 复测完成；所有诊断 trial 通过，但结果仍为
  `DIAGNOSTIC_ONLY`，未生成 active path-delay，也未提交 VDC/DPLL。
- 固件与拓扑（bench 诊断快照，非校准事实源）：
  - 四块板均为 DHRT100 build `20260822095115`；板卡身份由 `*IDN?` 返回的唯一地址确定，
    COM3/COM5/COM6/COM4 仅作为临时传输端点。
  - accepted physical order 为
    `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A -> A1E549202D18ED6A ->
    0010071E65B5CB38`。
- HIL 参数与结果：
  - 使用 `tools/calibration_ring_validate/calibration_link_p3.py`，每条相邻链路执行
    10/25/30 MHz、每档 3 次、32 脉冲、256 capture words；证据目录为
    `build-dhrt100-p3-dpll-20260822/p3_recheck_20260822`。
  - 36/36 trial accepted；每条链路和每档频率均为 3/3，initiator/responder edge mask
    为 `9/6`，DMA overrun 和 PIO stall 均为 0。
  - 10 MHz 实测频率 10 MHz、占空比约 52%；25 MHz 实测频率 25 MHz、占空比约 50%；
    30 MHz 实测约 30.303 MHz、占空比约 48.485%，保持 `LIMITED_RX`，稳定档最高为 25 MHz。
  - delay estimate 范围为 80..82 ns；四条链路均值为 80..81.333 ns，重复标准差为
    0..0.943 ns；responder residence 保持 20 ns。以上均为当前 build/拓扑/线缆/收发器的
    诊断观测，不是 endpoint bias 扣除后的单向事实。
- active gate 复核：
  - `calibration_path_delay_probe.py` 对四块板均返回 `MISSING`，`timestamp_resolution_ns`
    和 `timestamp_flags` 仍为 0；因此本轮不能发布 active path-delay。
  - 下一步仍是接入同一 PIO persona 的板内三线 reference loopback，生成有效 bias
  generation，再补 freshness、asymmetry 和故障注入后运行 active snapshot admission。

## CAL-TASK-20260822-005 - P3-3 板内 reference loopback 复测

- 状态：四块板均完成当前固件的短 reference-loopback 检查；四块板均未形成有效
  Calibration bias snapshot，P3-3 仍被物理三线回环连接阻塞。
- 固件与身份：四块板均返回 DHRT100 build `20260822095115`；`*IDN?` 返回的唯一地址为
  `0010071E65B5CB38`、`FB276192BEF9CCE1`、`2BD5090FE009FA2A`、`A1E549202D18ED6A`。
  COM3/COM5/COM6/COM4 仅是本轮临时传输端点。
- 验证命令与证据：使用既有
  `tools/calibration_loopback_validate/calibration_loopback_validate.py`，每板 64 words、
  1 次短窗口；证据目录为
  `build-dhrt100-p3-dpll-20260822/reference_loopback_recheck/<unique-id>`。
- 结果：四板均为 `runs_passed=0`；固件快照没有有效四边沿，`result_valid=0`，residence、
  raw path-sum 和 delay estimate 均为 0；`SYST:ERR?` 均为 `0,"No error"`。因此这是
  reference-loopback 缺失/未闭合的有效拒绝，不是 SCPI、USB 或 PIO 错误。
- HAOFV 结论：不得用本轮零值生成 bias generation，也不得执行 `CALibration:SAVE`；接好每块
  板同一 PIO persona 的 `CLK_TX->CLK_RX`、`DATA_TX->DATA_RX`、`SYNC_TX->SYNC_RX` 后，
  必须重新运行多轮 bias 采样并通过 generation、CRC、freshness 和 accepted-count 门禁。

## CAL-TASK-20260822-002 - P3 四板逐链路三档复测

- 状态：四条相邻链路的 P3 诊断 HIL 完成；结果仍为 `DIAGNOSTIC_ONLY`，未生成 active
  per-link calibration，未提交给 VDC/DPLL。
- 日期：2026-08-22。
- 固件与拓扑（bench 诊断快照，非校准事实源）：
  - 四板均运行 DHRT100 build `20260822085100`；板卡身份只取 `*IDN?` 唯一地址，临时
    COM 端点为 COM3/COM5/COM6/COM4。
  - accepted physical order 为
    `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A -> A1E549202D18ED6A ->
    0010071E65B5CB38`；拓扑证据目录为
    `build-dhrt100-p3-dpll-20260822/calibration_topology_20260822`。
- HIL 参数与结果：
  - 使用 `tools/calibration_ring_validate/calibration_link_p3.py`，每条链路执行
    10/25/30 MHz、每档 3 次、每次 32 脉冲和 256 capture words；完整证据目录为
    `build-dhrt100-p3-dpll-20260822/calibration_p3_20260822`。
  - 36/36 trial accepted；四条链路、三档频率均为 3/3，initiator/responder edge mask
    分别为 `9/6`，合并四边沿完整，DMA overrun 和 PIO stall 均为 0。
  - 10 MHz 实际频率为 10.000 MHz、占空比约 52%；25 MHz 为 25.000 MHz、占空比约
    50%；30 MHz 为约 30.303 MHz、占空比约 48.48%，按策略归类为 `LIMITED_RX`，不进入
    稳定 profile。
  - 4 ns PIO/DMA 采样量化下，observed delay estimate 范围为 78..82 ns，单链路重复
    jitter 为 0..2 ns；responder residence 为 20 ns。该值是当前 build/拓扑/线缆/收发器
    的对称假设观测值，不是 endpoint bias 扣除后的单向事实。
- HAOFV 边界与结论：
  - SCPI/core0 只提交有界 intent；PIO、SM、DMA、方向控制和边沿收割均由 TDMA core1
    owner 持有，Calibration 只配对同 epoch raw evidence 并执行质量门禁。
  - 本轮证明四板逐链路 P3 传输、频率/占空比门禁和三档策略可重复通过；不证明 endpoint
    bias、长时间温漂、四段 cumulative/residual 或 VDC/DPLL active 准入。
- 下一步：完成 P3-3 板内 endpoint bias/reference loopback，补齐缺边沿/乱序/重复、
  SYNC/CRC/epoch、频率偏差、asymmetry、DMA/stall 和 topology/profile freshness 故障注入，
  再以新的 generation 运行 active snapshot admission。

## 当前任务状态

| 任务 | 状态 | 结论 |
|---|---|---|
| 校准域职责与 TDMA/VDC 边界 | `[x]` | 校准拥有测量与接受门禁，TDMA 负责传输与编排，VDC/DPLL 消费 accepted snapshot |
| 线序与环路顺序测量 | `[~]` | host 隔离探测、闭环判定和 NO 提交已迁入 calibration 命名空间；板内 generation/freshness 待实现 |
| 第一阶段 CLK RTT 粗捕获 | `[x]` | 已完成板内最小实现和四板 HIL，仍为 diagnostic-only |
| 第二阶段编码 marker/相关测距 | `[~]` | 动态 persona、固定双 DMA、板端相关和四主最小 HIL 已完成；重复门限及板内多板协调待完成 |
| 第三阶段双向同时对比法 | `[~]` | 四板四链路 10/25/30 MHz 三档诊断 HIL 已完成 36/36；endpoint bias、故障注入、freshness/active gate 待完成 |
| VDC/DPLL active calibration gate | `[ ]` | 依赖正式 hardware latch、bias、generation/freshness 和 P3 结果 |

## CAL-TASK-20260821-010 - COM8 当前固件重写与单板回环复核

- 状态：当前 HEAD 已通过 OTA 重写并提交到 slot 2；短接线接好后单板双向回环 10/10
  accepted。结果继续保持 `REFERENCE_LOOPBACK + DIAGNOSTIC_ONLY`，active calibration 仍为
  `FIELD_DEFAULT`。
- 日期：2026-08-21。
- 固件与板卡（bench 诊断快照，非校准事实源）：
  - 源码 commit 为 `f1e942ee0b4b8057ab7077e17be2755035421d35`，build ID 为
    `20260821130800`；OTA package 由 `build-rtos-multicore-smoke/RP2350_TRIG_UPDATE.pkg`
    提供。
  - COM8 当次对应唯一地址 `839E1AE79EA20F31`；OTA boot/commit 后 slot 2 为 committed，
    `SYSTem:ERRor?` 返回 `0,"No error"`。证据目录为
    `build-rtos-multicore-smoke/ota_boot_commit_calibration_current`。
  - 验证保持 12 V 断开，仅使用 USB 供电，避免已知高侧电流采集前端共模拓扑再次发热。
- 单板回环（bench 诊断快照，非校准事实源）：
  - 第一次 10 轮为 0/10，固定只有本地 TX 边沿，随后确认原因是回环线未接；这不是 GPIO、
    PIO persona 或固件回归证据。
  - 接好 `CLK_TX -> CLK_RX`、`SYNC_TX -> SYNC_RX`、`DATA_TX -> DATA_RX` 短接线后，同一
    build 重跑 10 个独立 epoch，10/10 accepted；每轮 `edge_mask=0xF`、latch flags `0x7`、
    reject reason 为 0，residence 为 960..980 ns、raw path-sum 为 100..120 ns、对称假设下的
    observed delay estimate 为 50..60 ns。
  - 证据目录为
    `build-rtos-multicore-smoke/calibration_loopback_com8_build_20260821130800_rewired`；其中
    `summary.json` 绑定 build、板卡地址、epoch、四边沿、公式结果和最终错误队列。
- HAOFV 边界与恢复：
  - 回环仍由 TDMA core1 owner 持有 PIO/SM/DMA、驱动器方向和四边沿收割；core0/SCPI 只
    提交 bounded intent 并读取 guarded snapshot，host 未注入实时边沿时间戳。
  - STOP 后常驻 TDMA service count 持续递增，维护 persona 已退出；单板未形成 accepted
    topology，因此保持 `ring_enabled=0`、adapter stopped 和 adapter error=0，不强行 START。
  - `READ:CALibration:CLOCk:CODEd?` 与 `READ:CALibration:P3?` 当前无新运行结果；endpoint
    bias/reference generation、topology/profile freshness、active/staging CRC 和 VDC/DPLL
    consumer gate 仍未完成，禁止清除 `DIAGNOSTIC_ONLY`。
- 传感器（USB-only bench 诊断快照，非器件精度事实源）：
  - `SYSTem:DIAGnostic:SENSors?` 返回板载温度约 34.0 C、RP2350 内部温度约 39.2 C；电流
    前端输出约 1.4465 V，frontend plausibility 为 true，未校准 nominal estimate 约 79 mA。
  - 该结果只说明 12 V 断开时前端处于合理零点附近；在高侧共模拓扑修复前不得恢复 12 V。

## CAL-TASK-20260821-009 - P3 四板逐链路双向测距

- 状态：P3-1/P3-2 和四板逐链路诊断 HIL 完成；结果继续保持 `DIAGNOSTIC_ONLY`，未生成
  active per-link calibration，未提交给 VDC/DPLL。
- 日期：2026-08-21。
- HAOFV 实现边界：
  - TDMA core1 owner 动态装载 initiator/responder PIO persona，持有 SM、DMA、驱动器方向和
    4 ns GPIO24..31 采样；core0/SCPI 只发布 guarded intent 和读取 snapshot。
  - initiator 记录本地 `t1=CLK_TX`、`t4=DATA_RX`，responder 记录本地
    `t2=CLK_RX`、`t3=DATA_TX`；host 只配对同 epoch evidence 并计算
    `path_sum=(t4-t1)-(t3-t2)`。
  - 新增 `CALibration:P3:STARt/STOP`、`READ:CALibration:P3?` 和
    `tools/calibration_ring_validate/calibration_link_p3.py`；板卡身份只使用 `*IDN?`
    唯一地址，COM 仅是当次端点。
- OTA 与 HIL（bench 诊断快照，非校准事实源）：
  - 四板升级到 build `20260821100236`，证据目录
    `build-product-release/calibration_p3_ota_20260821`。
  - 四板物理顺序为 `0010071E65B5CB38 -> FB276192BEF9CCE1 ->
    2BD5090FE009FA2A -> A1E549202D18ED6A -> 0010071E65B5CB38`。
  - 先做 10 MHz 四段单轮 smoke，4/4 accepted；完整阶梯再对四段分别执行
    `10/25/30 MHz`、每级 3 个独立 epoch。基础四边沿、CLK 频率/占空比、DMA/stall 门禁
    为 36/36 accepted，证据目录为 `build-product-release/calibration_p3_full_20260821`。
  - 各 link 的单程对称估计均落在 `80..82 ns`；该 2 ns 台阶来自 4 ns RTT 采样量化后
    除以二。每轮 responder residence 均为 20 ns，DMA overrun 和 PIO stall 均为零。
  - 采样得到 10 MHz 为 10.000 MHz、source duty 52%；25 MHz 为 25.000 MHz、source
    duty 50%；30 MHz 多数为约 30.303 MHz、source duty 48.48%。最后一段 responder
    30 MHz duty 最低为 42.42%，当前在 4 ns 采样诊断容差内，但须由示波器复核电气波形。
  - 发现基础门禁遗漏返回 DATA pulse width 后，工具已增加 `data_high_ok`（理想半周期
    量化值允许一个 sample period 误差）。30 MHz 四段各补跑 10 轮，共 39/40 accepted；
    NO.1→NO.2 第 6 轮的 DATA_RX 高电平只有 8 ns，被 `initiator_data_width` 正确拒绝。
    证据目录为 `build-product-release/calibration_p3_30m_data_gate_20260821`。因此 25 MHz
    是当前四段保守稳定档；30 MHz 改为 `LIMITED_RX` 有界诊断接收档，不能进入 active
    profile。后续每次 P3 验证固定执行
    `calibration_link_frequency_policy.REQUIRED_FREQUENCY_LADDER_MHZ` 完整阶梯，即使稳定档
    失败也不跳过 `LIMITED_RX_FREQUENCY_MHZ`；该档任一拒绝记录 `FALLBACK_25MHZ` 并按
    `LIMITED_RX_FALLBACK_MHZ` 回退，但不使已通过的稳定档总判定失败。
- 剩余门禁：
  - 同 persona endpoint bias/reference generation 尚未完成，因此当前 `delay_estimate` 只是
    对称假设下的 observed value，不是产品单向延迟事实。
  - 尚未完成缺边沿/乱序/epoch/frequency/asymmetry 故障注入、topology/profile freshness、
    四段 cumulative 与整圈 residual 对比及长时间温漂统计。
  - P3 snapshot 尚未形成 active/staging CRC、generation 和 VDC/DPLL consumer gate；不得
    清除 `TDMA_PIO_SPI_P3_FLAG_DIAGNOSTIC_ONLY`。
- 验证：
  - `cmake --build build-product-release -j 4` 通过并生成 A/B 固件、UF2 和 OTA package。
  - `python -m pytest tests/python/test_calibration_link_p3.py
    tests/python/test_calibration_link_plan.py -p no:cacheprovider`：10 项通过。
  - P3 基础 HIL 的 12 个 link/frequency level 全部 3/3 通过；增加 DATA width 门禁后的
    30 MHz 加严复测为 39/40，详细四时间戳、频率、占空比、脉宽、residence、path-sum
    和失败门禁见两份 evidence `summary.json`。
  - `calibration_link_p3.py` 与 `calibration_link_plan.py` 已拒绝不完整频率列表，保证每次
    验证包含 `LIMITED_RX_FREQUENCY_MHZ`；summary 分离 `STABLE_REQUIRED` 与 `LIMITED_RX`，
    并按 `LIMITED_RX_FALLBACK_MHZ` 发布 fallback 状态。该策略只改变诊断评分，不放宽
    DATA/CLK 频率、占空比或脉宽门限。

## CAL-TASK-20260821-006 - P2 重复统计与 TDMA PIO 时序校正

- 状态：P2 重复诊断完成；普通 TDMA TX PIO 时序校正完成；P3 正式准入仍未满足。
- 日期：2026-08-21。
- P2 重复 HIL（build `20260821044448`，accepted physical order）使用
  `tools/calibration_ring_validate/calibration_clk_coded.py`，每个 master 独立执行 10
  轮，结果目录：
  - `build-product-release/calibration_clk_coded_p2_four_repeat10_level7`
  - `build-product-release/calibration_clk_coded_p2_four_repeat10_level8`
  - `build-product-release/calibration_clk_coded_p2_four_repeat10_level9`
- 结果摘要：
  - 10 MHz：40/40 accepted，NO.1/2/4 的 lag span 为 1，NO.3 为 2；各主节点最小
    margin 为 `22/79/25/57`。
  - 25 MHz：40/40 accepted，四主 lag span 均为 1；各主节点最小 margin 为 `4/14/26/36`。
  - 30 MHz：40/40 accepted，NO.1 lag span 为 2，其余为 1；各主节点最小 margin 为
    `14/42/47/14`。
  - 三档均无 DMA overrun、PIO stall、marker flag 缺失或 mixed peak；结果仍为
    `DIAGNOSTIC_ONLY`，不能据此缩短 40 ns robust marker，也不能生成 active delay。
- 发现并修正普通 TDMA TX PIO 时序偏差：原实现实际为 7 cycles/bit、2:5 高低比，
  与分频函数声明的 6 cycles/bit 不一致；修正为 6 cycles/bit、3:3 高低比。静态工具
  `tools/tdma_ring_monitor/tdma_pio_timing_check.py` 及测试
  `tests/python/test_tdma_pio_timing_check.py` 已通过；报告为
  `build-product-release/tdma_pio_timing_check_10_25_30_fixed.json`。
- OTA：四板均使用唯一地址白名单升级到最终 build `20260821061831`；证据目录
  `build-product-release/ota_final_pio_fix_20260821`。中间 build `20260821061232` 曾试验
  5 ms command-mailbox commit guard，但四板 coded START 仍由 guarded snapshot 确认、CDC
  payload 超时，且一次后续 trial 遇到端点重枚举；该无收益 guard 已从最终源码和固件移除。
- 最终 build 使用 NO.1 做一次四板 coded smoke 通过：lag=100、distance=89、margin=351，
  marker flags 完整且 DMA/stall 为零；证据目录
  `build-product-release/calibration_clk_coded_final_smoke`。该数字仍是 bench 诊断快照。
- HAOFV 边界：PIO waveform 属于 TDMA transport/persona 实现；Calibration 只消费
  profile/raw evidence 和 coded correlation 结果，不把静态 timing check 当作硬件 latch
  或 VDC/DPLL calibration evidence。示波器仍需确认 rise/fall、线缆和收发器影响。
- 下一步：在修正 waveform 的 build 上重新跑 10/25/30 MHz 的多板 coded HIL；只有在
  40 ns fallback 的重复统计满足统一 margin/lag gate 后，才逐级评估更短 marker；随后再
  补 `TRAIN_PREPARE/ACK/commit`、endpoint bias 和真实 `t1..t4` hardware latch，决定是否
  进入 P3 双板 diagnostic 阶段。

## CAL-TASK-20260821-007 - 码元档位筛选与回环反射时序复核

- 状态：P2 candidate 筛选完成一轮；32 ns 通过，24 ns 退化，20 ns 因板卡暂时未全部
  重新枚举而未作无效判定；P3 正式准入仍未满足。
- 日期：2026-08-21。
- OTA：四板按 `*IDN?` 唯一地址白名单升级到 build `20260821062825`，证据目录为
  `build-product-release/ota_coded_marker_20260821`；四板 post-commit build 查询均一致。
- coded marker 单轮筛选使用 `tools/calibration_ring_validate/calibration_clk_coded.py`：
  `codebook=3`（32 ns）为 4/4 accepted，lag histogram 为 `100/100/101/100`，无
  DMA overrun、PIO stall 或 mixed peak，最小 margin=133；证据目录
  `build-product-release/calibration_clk_codebook3_screen`。这些数字是绑定 build、拓扑
  和接线的 bench/诊断快照，不是产品精度事实源。
- `codebook=2`（24 ns）仅 NO.1 accepted，NO.2--NO.4 为
  `correlation_manchester` reject（证据目录 `build-product-release/calibration_clk_codebook2_screen`），
  因此不能缩短 robust marker；在四板重新枚举后再决定是否补测 20 ns，不能把缺板状态
  当作 20 ns 结果。
- 回环反射校准静态复核已扩展 `tools/tdma_ring_monitor/tdma_pio_timing_check.py` 与
  `tests/python/test_tdma_pio_timing_check.py`：`tdma_pio_spi_clk_burst` 在 10/25/30 MHz
  的理论占空比均为 50%，频率误差在静态门限内；`tdma_pio_spi_clk_forward` 不使用本地
  baud divider，频率由上游 RX CLK 决定。报告为
  `build-product-release/tdma_pio_timing_check_reflection_20260821.json`；电气
  rise/fall、ISO1452、连接器和线缆影响必须以示波器实测。
- HAOFV 边界：Calibration 只接受 coded snapshot/raw evidence；PIO waveform、burst、
  forward 和 DMA 资源仍归 TDMA/core1 owner，core0/SCPI 不进入边沿热路径。

## CAL-TASK-20260821-008 - P3 逐链路低频推进基线

- 状态：P3 尚未开始正式测量；已完成低频优先策略、在线板卡确认和 active path-delay
  基线读取，当前 firmware 不具备板间四边沿 persona，故不能生成逐链路结果。
- 日期：2026-08-21。
- 四板按 `*IDN?` 唯一地址重新枚举成功，physical order 保持
  `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A -> A1E549202D18ED6A`，
  build 均为 `20260821062825`；证据目录为
  `build-product-release/inventory_p3_frequency_baseline`。
- 已用 `tools/calibration_ring_validate/calibration_path_delay_probe.py` 读取四板当前
  path-delay 与 TDMA 状态；四板均返回 `MISSING`，`ring_enabled=0`，
  `ring_timestamp_resolution_ns=0`，没有 active per-link calibration；证据目录为
  `build-product-release/calibration_p3_baseline_path_delay`。
- 频率/占空比门禁沿用 `10 -> 25 -> 30 MHz`。静态报告
  `build-product-release/tdma_pio_timing_check_reflection_20260821.json` 已证明 burst
  在三档为 50% duty，forward 为上游边沿再生；示波器仍需确认电气波形。
- 下一步必须先完成 P3-1/P3-2：板间 `CLK/DATA/SYNC` 同 epoch marker、四边沿 hardware
  latch、residence/path-sum snapshot 和逐 link SCPI/工具入口；完成后才能从 NO.1→NO.2
  的 10 MHz 第一段开始，再依次推进 NO.2→NO.3、NO.3→NO.4、NO.4→NO.1，并逐级提升
  到 25/30 MHz。
- 物理方向已复核：同一 BiSS 段为 A.CLK_TX `GPIO25` -> B.CLK_RX `GPIO28`，同时
  B.DATA_TX `GPIO29` -> A.DATA_RX `GPIO24`，SYNC 使用 `GPIO26/27` 关联 epoch；ISO1452
  固定方向与 P3 的 `t1/t2/t3/t4` 双向方程一致。

## CAL-TASK-20260821-005 - P2 coded marker 四板最小闭环

- 状态：SCPI/工具最小闭环和四主单轮 HIL 完成；结果保持 diagnostic-only，产品级协调与
  重复门限未完成。
- 日期：2026-08-21。
- 完成内容：
  - 新增 `CALibration:CLOCk:CODEd:STARt/STOP` 和
    `READ:CALibration:CLOCk:CODEd?`；core0 只提交 guarded intent，板卡唯一地址、build、node、
    topology/profile/schedule CRC、baud 和 generation 由固件自动绑定。
  - 修复 STOPPED 维护态的 topology metadata 生命周期：Calibration 只读 TDMA service 的
    last accepted `ring_staged_config`，不改变 `RING:STOP` 清空 live runtime 的既有语义。
  - 新增 `tools/calibration_ring_validate/calibration_clk_coded.py`，按 `*IDN?` 唯一地址发现、
    follower 先于 master、四主轮换、guarded snapshot 判定、STOP/IDLE 收尾并输出 UTF-8
    JSON/CSV；COM 号仅为当次传输端点。
  - `M255_MANCHESTER_20` 主峰可见但严格字段门禁拒绝；回退
    `M255_MANCHESTER_40` 后，accepted physical order 的四个 master 单轮全部通过。
- HIL 快照（build `20260821044448`，10 MHz，非阈值事实源）：
  - accepted order 为 `0010071E65B5CB38 -> FB276192BEF9CCE1 -> 2BD5090FE009FA2A ->
    A1E549202D18ED6A`；四主 best lag 为 `100/100/101/101` raw samples。
  - best distance 为 `23/152/0/154`，margin 为 `442/184/485/179`；四主 marker flags 全通过，
    DMA overrun 和 PIO stall 均为零。
  - 单轮结果仍使用显式宽松 diagnostic gate，不能据此冻结 distance/margin threshold，不能
    清除 `CALIBRATION_CLK_CODED_FLAG_DIAGNOSTIC_ONLY`。
- OTA 与证据：
  - 四板 OTA 完整通过，四个唯一地址均运行 build `20260821044448`；证据目录
    `build-product-release/ota_coded_p2_20260821_final`。
  - 四主 HIL 证据目录
    `build-product-release/calibration_clk_coded_p2_four_accepted_order`。
  - NO.1 调试 UART COM7 读取 15 秒无 fault/watchdog 日志；UART 不是板卡身份。
- 遗留：
  - coded START/STOP 的 CDC action response 在 persona 切换时可能超时；工具仅在 snapshot
    sequence 和 request metadata 完全匹配时确认接受，产品闭环仍需消除此响应时序问题。
  - 实现 TRAIN_PREPARE/ACK/commit sequence，并完成四主多次重复、10/25/30 MHz 统计和
    profile/topology change、掉线、commit miss 故障注入。

## CAL-TASK-20260821-004 - P2 动态 PIO persona 与 coded raw transport

- 状态：固件算法基座、动态 persona、固定双 DMA 和 Calibration core1 最小闭环已完成；
  板端触发接口、多板协调、硬件 capture-origin HIL 和正式阈值尚未完成。
- 日期：2026-08-21。
- HAOFV 边界：
  - Calibration 生成和解释 marker，执行有界相关、generation 门禁及 accepted/rejected 发布。
  - TDMA core1 是 PIO/SM/DMA persona 唯一 owner，只搬运 packed TX sample 和 bounded raw capture。
  - core0 只能向 Calibration guarded command mailbox 发布 request/gate；SCPI 不直接操作 PIO/DMA。
- 完成内容：
  - 实现 candidate M255 Manchester marker 的 C/Python golden vector、header/反码/CRC、正反相
    相关和旧 epoch、缺失/重复、截断、低 margin 故障注入。
  - PIO2 改为 `tdma_pio_spi_program_persona_t` 动态装载：`NORMAL`、`CLOCK_COARSE`、
    `CAL_LOOPBACK`、`CLOCK_CODED` 互斥存在，切换前检查两个 SM 和 TX/RX DMA 均停止；失败
    尝试恢复上一 persona，完成后恢复普通 persona。
  - `CLOCK_CODED` master 使用 `TDMA_PROFILE_DEFAULT_TX_DMA_CHANNEL_ID` 和
    `TDMA_PROFILE_DEFAULT_RX_DMA_CHANNEL_ID` 驱动 packed TX 与 oversampling RX 固定窗口；
    follower 只启用 CLK forwarding，不解析 marker。
  - DMA resource claim 已从 `TdmaFoundationProfile` 传播到 ring runtime、service 和物理层，
    profile 声明与硬件实现不一致时拒绝 arm。
  - Calibration core1 构建 marker、提交 raw request、收割 guarded capture，并调用
    `calibration_clk_coded_process_core1()`；成功结果继续保留 `DIAGNOSTIC_ONLY`。
- 验证结果：
  - `run_calibration_clk_marker_tests.ps1`、TDMA profile/runtime/adapter/service 与 RefMem-TDMA
    相关主机单测全部通过；Python codebook evaluator 为 6/6 通过。
  - `cmake --build build-product-release` 完成 A/B 编译和统一包生成；build
    `20260821041729`，该编号仅为本轮构建快照，非接口事实源。
  - 当前没有板端 coded HIL，不能把 software `capture_origin_tick` 当作正式 hardware latch，
    不能清除 `DIAGNOSTIC_ONLY`，也不登记 candidate marker 为冻结跨域契约。
- 下一步：
  - 增加最小 SCPI/工具触发闭环并实测 candidate 半码元档位；随后实现
    `TRAIN_PREPARE/ACK/commit sequence` 和四主轮换。
  - 以 PIO/DMA hardware capture origin、重复统计、profile/topology freshness 和 persona
    恢复作为 HIL 放行门禁。

## CAL-TASK-20260821-003 - 线序与环路顺序测量迁入校准域

- 状态：host 工具、命名和文档所有权迁移完成；板内 topology generation/freshness 尚待实现。
- 日期：2026-08-21。
- 任务目标：
  - 将有向线序/邻接矩阵、单闭环判定、anchor 旋转、node map 和 NO 提交归入校准域。
  - 校准测量工具统一使用 `calibration_*` 名称；TDMA 只提供隔离 probe transport 和计数证据。
  - 禁止 TDMA START 根据调用参数隐式写入 NO，避免未校准顺序覆盖已接受 topology。
- 完成内容：
  - `tdma_ring_autodetect.py` 迁移为
    `tools/calibration_ring_validate/calibration_ring_topology.py`，默认报告目录改为
    `calibration_ring_topology_*`。
  - 输出增加 `measurement_domain=calibration` 和 measurement phase；保留 directed pair
    evidence、adjacency、ring order、node map 和完整 snapshot。
  - `--anchor-id` 决定 accepted ring 的 NO.1；`--assign-no` 只在单闭环判定通过后提交映射，
    支持写后读回及 `--reboot-verify-no` 持久化复核。
  - `tdma_start_ring.py` 删除 NO 分配和启动时隐式 NO 写入；它只消费调用者提供的 accepted
    calibration order 显式映射到 TDMA/RefMem 边界的 local/reference slot ID。
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
  - TDMA 只保留 PIO/SM/DMA、core1 command mailbox、persona、窗口和 raw evidence transport。
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
- build ID、固件/工具版本、板卡唯一地址、local node、拓扑、线缆/收发器和 profile CRC；
- 训练 epoch/sequence、accepted/rejected、硬件 latch source/resolution/flags、DMA overrun/
  stall、margin、residence、path-sum、bias generation、calibration generation 和 freshness；
- 执行的 host unit、HIL、烧录、SCPI smoke 或长稳命令，以及证据目录；
- 失败原因、影响范围、回滚/恢复动作和下一步。

当前未完成项目不能通过“代码已经存在”“工具能查询”或“软件 timer 有数值”标记为完成。
