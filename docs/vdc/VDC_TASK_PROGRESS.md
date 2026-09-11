# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`
Last updated: 2026-09-10

本文只记录当前 VDC 迁移的实施 checkpoint 和证据闭环。任务状态以 `VDC_DOMAIN_TODO.md`
为唯一事实源，稳定语义以 `VDC_DOMAIN_ARCHITECTURE.md` 为准。重构前的长历史记录已移入
`docs/legacy/vdc/`，不再混入当前 checkpoint。

## 文档接口

| 字段 | 规则 |
|---|---|
| `Progress ID` | 每条记录唯一，格式为 `VDC-PROGRESS-YYYYMMDD-NNN`。 |
| `TODO task ID` | 必须引用 `VDC_DOMAIN_TODO.md` 中的稳定 Task ID。 |
| 状态 | 使用 `DONE`、`IN PROGRESS`、`PENDING`、`BLOCKED`；不在本文擅自改变 TODO 状态。 |
| 证据 | 命令、build/HIL、失败、回退和 `out/` 路径只记录已发生事实。 |
| 下一 gate | 必须指向一个 TODO Task ID 或明确外部阻塞。 |

## 当前 checkpoint

当前迁移顺序：

```text
VDC-TDMA-001
  -> VDC-CAL-001
  -> VDC-EVID-001
  -> VDC-ROLE-001 / VDC-ROLE-002 / VDC-ROLE-003 / VDC-ROLE-004 / VDC-ROLE-005
  -> VDC-SERVO-001 / VDC-SERVO-002
  -> VDC-LOCK-001
  -> VDC-SNAPSHOT-001
  -> VDC-HOLD-001
  -> VDC-RUN-001
  -> VDC-VERIFY-001
```

当前最高优先级 gate 是 `VDC-ROLE-001`；`VDC-TDMA-001`、`VDC-CAL-001` 和
`VDC-EVID-001` 继续作为它不变的 evidence 输入。`VDC-SERVO-001/002` 在正式
evidence 未闭环前只允许 host/replay 验证，不得用于发布板端目标锁。

### VDC-PROGRESS-20260910-012 — P3 phase-domain finding and fail-closed admission

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B、`VDC-ROLE-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 证据：`out/HardwareAcceptance/20260910/p3-094720/dpll-no1-4-internal/` 的原始 capture
  显示 NO1 是 self-loop source/reference，而 NO2--NO4 将远端 origin phase 与各自 raw
  RX counter phase 相减。该目录是单次采集快照，不是稳定性能事实源；重分析产物位于
  `out/pytest/p3-094720-observation-reanalysis/`。
- 结论：NO1 的 ns 级 raw spread 与从机 us 级 raw spread 不是同一已证明物理量。三台
  FOLLOWER 在该快照中没有成功应用 peer command，且 active path 缺 bias generation；
  因此没有任一节点可报告可信 output jitter、corrected residual 或 formal lock。
- 变更：TDMA observation 现在标识 same-clock、common-mapped 或 raw-local phase domain。
  MASTER 遇到跨板 raw local phase 以 `VDC_DOMAIN_GATE_LOCAL_PHASE_UNALIGNED` fail-closed；
  FOLLOWER 继续记录同一 observation，但只旁路 PI/DCO/local promotion。离线报告将
  `raw_jitter_*` 和可信 `jitter_*` 分开，未对齐或 generation 不完整时可信值为空。
- 验证：VDC domain、TDMA adapter host C tests 与 DPLL observation/decode/residual/waveform
  Python regressions 已通过；本 checkpoint 不替代当前源码 P3/HIL。
- 下一 gate：完成 `VDC-ROLE-002` 的 RefMem command receive 闭环，并由 hardware output
  observation owner 发布 generation-bound local-to-common mapping；随后执行同窗 NO1--NO4/
  NO5、role matrix、fault injection 和长期观测。

### VDC-PROGRESS-20260910-011 — dual-observer algorithm remediation objective

- TODO task ID：`VDC-OBS-ALG-001` 阶段 A--D。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 长期任务目标：把 NO1--NO4 的内部 DPLL 观测和 NO5 的外部波形观测统一为一个可审计、
  可重放、可比较的测量算法。两类算法当前都视为未证明正确；在物理边沿、共同时间锚、
  有向路径和质量准入闭合前，任何散点、微秒级偏差或单节点 ns 级曲线都不能解释为真实
  抖动、锁相成功或失锁。
- 统一样本必须能追溯 `source_slot_id`/`reference_slot_id`、自身 TX/RX 边沿、TDMA
  `sample_sequence`、共同绝对生效时间、segment continuity、CRC/调度结果以及
  `delay_generation`/`bias_generation`。MASTER 和 FOLLOWER 使用同一观测算法；FOLLOWER
  只旁路 PI、积分器、DCO 和本地 lock promotion，不得用主机命令应用记录替代自身相位观测。
- 分阶段交付：
  1. 审计并修正内部/外部 evidence 的物理方向，确保每个节点配对自身发出与自身接收的
     同一边沿；明确 `source`、`reference` 与 TDMA 反向数据路径，禁止把参考节点 TX 到
     本地 RX 当作自身环路观测。
  2. 以 TDMA correlated sequence 及共同绝对生效时间建立跨节点时间锚，按 active
     有向 delay/bias generation 做扣除；禁止用接收时刻、本地重建 cycle 或零默认值对齐。
  3. 建立 fail-closed admission：坏帧、CRC/调度错误、来源错误、序列缺口、segment drop、
     stale、generation 不一致和外部线缆不完整样本只保留 raw diagnostic，并单独统计覆盖率。
  4. 在同一窗口分别计算 raw phase、固定 path bias、transport-corrected residual、真实
     jitter、频率斜率、命令应用和置信度；NO5 只能做同窗只读相关，不能驱动 DPLL 或提升 lock。
  5. 用 host/C、故障注入、`1M3F`/`2M2F`/`3M1F`、主机切换、当前源码指纹 P3/HIL 和长期
     soak 验证；不通过时保留失败证据，不扩大锁定门限或使用旧 receipt/replay。
- 完成定义：NO1--NO4 与 NO5 对同一物理量给出一致的字段和质量语义；每个有效样本可由
  原始边沿重放并定位到 source/sequence/segment；不完整窗口不会生成 corrected jitter、
  `LOCKED` 或 `FORMAL_LOCKED`；主从角色差异只体现在 PI/DCO 控制权。
- 当前边界：最近源码 P3 `out/HardwareAcceptance/20260910/p3-082217/` 的
  `strict_gates_passed=false`，且 TDMA ARM/拓扑/coded-marker 前置失败，不能作为算法
  正确性或锁相证据。下一 gate 是完成 C 端 TX/RX evidence 生命周期审计，再补 admission
  和同窗关联测试。

### VDC-PROGRESS-20260910-007 - unified observation algorithm kickoff

- TODO task ID：`VDC-OBS-ALG-001` 阶段 A/B。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 长期目标：建立一套对 NO1--NO4 内部 DPLL 和 NO5 外部波形都适用的观测定义，
  以“同一 source/reference 有向路径、同一 delay/bias generation、同一 sample
  sequence 和同一共同绝对生效时间”为前提，分别输出 raw phase、transport-corrected
  residual、固定 bias、真实 jitter、频率斜率、命令应用和覆盖率；FOLLOWER 与 MASTER
  使用相同观测路径，FOLLOWER 只旁路 PI/DCO/本地 lock promotion。
- 当前问题陈述：已有 P3 诊断中 NO1--NO4 的内部曲线和 NO5 外部曲线量级不一致，现阶段
  不能把差异解释为真实节点抖动或锁相失败。优先排查内部各节点是否都在测量自身 TX
  到自身 RX 的同一边沿对、NO5 是否使用同窗外部边沿、共同时间锚是否来自 TDMA
  correlated sequence，以及有向反向 delay/bias 是否被正确扣除；坏帧、序列缺口、
  segment drop 和 generation mismatch 必须只进入诊断统计。
- 现有证据边界：当前源码 P3 证据目录为
  `out/HardwareAcceptance/20260910/p3-075811/`；该轮 `strict_gates_passed=false`，
  且报告尚未形成完整 metadata/generation/continuity 证据，因此不能证明内部或外部
  算法正确，也不能宣称 `LOCKED`/`FORMAL_LOCKED`。
- 下一 gate：逐节点核对 C 端 reference/local timestamp 的物理方向和 owner，补齐
  sequence/segment continuity、坏帧和缺口 admission，再用同一窗口比较 NO1--NO4 与
  NO5；完成前不调整锁相判定门限、不用接收时刻重建共同时间，也不以从机不调 PI 为
  理由减少观测点。

### VDC-PROGRESS-20260910-008 — observation provenance capture schema 5

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 变更：将 TDMA correlation flags、reference/local phase、common effective time 及
  双方 logical observation time 从 ring observation 传播到 VDC timestamp evidence、
  DPLL state 和内部 maintenance capture。MASTER/FOLLOWER 的本地观测记录现在可用
  同一 provenance 重放；FOLLOWER 仍只旁路 PI/DCO/本地 lock promotion。
- Capture：DPLL capture schema 升为 5，记录从 64 字节扩展为 100 字节；为保持现有
  8 KiB 文件和固件 RAM 预算，maintenance capture 上限调整为
  `VDC_DPLL_MANAGER_DPLL_CAPTURE_MAX_SAMPLES`（当前 76），不改变 TDMA 短帧或实时队列。
- 软件验证：`test_dpll_observation_decode.py` schema 1--5 兼容回归通过（8 项）；
  DPLL capture/residual/waveform 相关 Python 回归通过（62 项）；release 双镜像构建、
  flash-link contract 和 RAM 链接检查通过。
- 边界：schema 5 只完善可重放 provenance，尚未完成 NO1--NO4 与 NO5 的同窗/segment
  continuity 关联、坏帧排除和 formal lock 规则；任何 P3 结果仍不能宣称锁相成功。
- 下一 gate：补齐 schema 5 的长期窗口关联与坏帧/缺口 admission，随后执行当前源码
  指纹下 P3/HIL 和长期观测，不得使用旧 receipt 或诊断 replay 替代。

### VDC-PROGRESS-20260910-009 — schema 5 current-source P3 diagnostic

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 当前源码指纹下 P3 quick diagnostic 已完成，build `20260909235818`，证据目录为
  `out/HardwareAcceptance/20260910/p3-075811/`。流程 `passed=true`，但
  `strict_gates_passed=false`。
- 失败事实：coarse CLK ARM 被拒、coded marker gate 未通过、TDMA closed loop 返回失败，
  NO5 仍为 `insufficient_stable_circular_span_windows`。内部 NO1--NO4 capture 已执行，
  但报告中 NO2--NO4 仍只有单点，不能作为多点收敛或 formal lock 证据。
- TDMA 接收质量在该轮未出现持续坏帧扩散；启动阶段仍记录已有的单次 transport bad/
  process reject，必须与观测缺口分开归因。该轮 P3 只证明 schema 5 固件和观测流程可运行，
  不证明内部/外部算法或跨板锁相正确。
- 下一 gate：继续实现同窗 sequence/segment continuity 与坏帧 admission，优先让
  NO2--NO4 获得与 NO1 相同语义的多点本地 TX/RX 观测，再重复 P3/HIL。

### VDC-PROGRESS-20260910-010 — generation admission hardening

- TODO task ID：`VDC-OBS-ALG-001` 阶段 A/B。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 变更：离线 residual analyzer 现在要求每个可修正样本同时携带正值
  `delay_generation` 和 `bias_generation`；缺失、只存在一个、非整数、非正值或跨样本
  generation 变化时，样本保留 raw diagnostic，整段 `delay_correction_available=false`
  且 `transport_corrected_sample_count=0`。路径/方向校验仍独立保留具体拒绝原因。
- 软件验证：观测工具回归及 schema/capture/waveform 相关 Python 测试共 `65 passed`；
  `py_compile` 通过。新增覆盖缺失 generation、部分 generation 和 generation 漂移。
- 当前源码指纹下 P3 quick diagnostic 已完成，build `20260910002225`，证据目录为
  `out/HardwareAcceptance/20260910/p3-082217/`；流程 `passed=true`，但
  `strict_gates_passed=false`。原始失败事实为 coarse CLK topology readback mismatch、
  coded marker gate、2BD5090FE009FA2A 的 TDMA ARM/运行交接失败，以及由此导致内部
  DPLL/NO5 无有效 TDMA 前置窗口；不能据此判断 generation 算法或锁相状态。
- 下一 gate：在不改变 TDMA 短帧和 Calibration training 的前提下，补齐
  source/reference、sample sequence、segment continuity 和坏帧 admission，再用有效
  多窗口验证 NO1--NO4 与 NO5 的同窗 corrected residual。

### VDC-PROGRESS-20260910-001 — unified observation algorithm baseline

- TODO task ID：`VDC-OBS-ALG-001` 阶段 A。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 目标：统一 NO1--NO4 内部 DPLL 与 NO5 外部观测的 residual 语义，明确有向路径、
  固定 bias、真实 jitter、控制命令应用和观测缺口的边界。MASTER/FOLLOWER 共用观测
  计算；FOLLOWER 仅旁路 PI/DCO/本地 lock promotion。
- 已完成：`tools/dpll_residual_analyze/dpll_residual_analyze.py` 现在只在 path/delay
  元数据完整且方向一致时生成 transport-corrected residual；缺失、负值或方向不匹配
  时保留 raw residual，并输出 `delay_correction_available`、修正样本数和拒绝原因。
  path bias 不再把缺失字段静默归入 `0 -> 0`。
- 软件验证：`python -m pytest tests/python/test_dpll_residual_analyze.py -p no:cacheprovider`
  通过，`13 passed`；覆盖缺失 delay、错误方向、固定 delay 改变不影响 corrected
  jitter、命令应用不冒充 local residual 和 follower 元数据缺失。
- 当前源码指纹下 P3：`python tools/hardware_acceptance/p3_hardware_acceptance.py run`
  完成 quick diagnostic flow；证据目录为
  `out/HardwareAcceptance/20260910/p3-052612/`。TDMA、Calibration、内部 NO1--NO4
  观测和 NO5 观测流程均执行完成，但 receipt 的 `strict_gates_passed=false`，NO5
  失败为 `insufficient_stable_circular_span_windows`。该结果证明验收链路可运行，
  不证明 NO5 同窗关联或 `FORMAL_LOCKED`。
- 工具边界修正后的再次 P3 尝试使用 build `20260909213559`，在 Latency Cal 前置阶段
  停止：NO1 calibration profile apply 回读 `active_level=0`，其余三板为请求 level，
  因此没有进入 DPLL/NO5 算法验收。该硬件前置失败不能作为算法回归结论，需在下一次
  P3 前先恢复四板 calibration profile 一致性。
- 最新当前源码 P3 使用 build `20260909214240`，完整执行到内部/NO5 观测；flow
  `passed=true`，但 `strict_gates_passed=false`。失败事实包括一板 coarse CLK ARM
  被拒、coded marker gate 未通过，以及 NO5 的
  `source_dma_or_latch_dropped_records`、`source_dropped_records` 和
  `insufficient_stable_circular_span_windows`。证据目录为
  `out/HardwareAcceptance/20260910/p3-054234/`；该结果不能宣称正式锁相，且说明
  观测丢样与窗口完整性必须和 DPLL 控制状态分开判定。
- 边界：尚未完成 C 端 `reference_tx_phase`/`local_rx_phase` 的物理方向和共同绝对
  生效时间审计；尚未建立 NO1--NO4 与 NO5 的同窗关联，也不宣称板端锁相或
  `FORMAL_LOCKED`。
- 下一 gate：完成 `VDC-OBS-ALG-001` 阶段 B，审计并补齐 C 端 sequence、source/reference、
  delay generation 和共同时间锚字段，再运行相关 host/C 回归和当前源码指纹下 P3。

### VDC-PROGRESS-20260910-002 — observation algorithm problem statement

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 长期目标确认：NO1--NO4 内部观测和 NO5 外部观测必须使用同一条物理测量定义——
  同一 `source/reference` 有向路径、同一校准 delay/bias generation、同一 sample
  sequence 和同一共同绝对生效时间；输出分别报告 raw phase、transport-corrected
  residual、固定 bias、真实 jitter、频率斜率、命令应用和观测覆盖率。FOLLOWER 只
  旁路 PI/DCO，不得减少自身观测点或用主机命令应用记录替代本地相位残差。
- 当前 C 端已确认的算法缺口：TDMA observation trailer 只编码 frozen-cycle phase；
  接收端将 `reference_tx_timestamp_ns` 置零并只保留本地 RX timestamp；没有随样本
  传递共同绝对生效时间、delay generation 或 bias generation。`vdc_ring_observer`
  又从本地 RX timestamp 重建 window start，这个值不能作为跨板 absolute-time anchor。
  因此目前 NO1--NO4 与 NO5 的数值不能证明处于同一时间窗，散点、微秒级偏差和
  丢窗既可能是算法语义错误，也可能是观测缺口，不能直接解释为锁相或失锁。
- 阶段 B 首个代码切片已落地：TDMA adapter 根据已校验的
  `correlated_sequence * cycle_period + reference_tx_phase` 生成
  `common_effective_time_ns`，并以 `TDMA_RING_CLOCK_OBSERVATION_FLAG_COMMON_TIME`
  明确标记；VDC observer 的窗口、start/observed/done/apply 时间全部从该 logical
  TDMA 锚派生，不再从本地 RX timestamp 重建。local RX timestamp 仍仅作硬件接收事实
  和 provenance，短帧布局及 Calibration training 未改变。
- 已验证：VDC、TDMA adapter、TDMA ring runtime、TDMA service scheduler 和 RefMem
  realtime TDMA host C tests 通过；snapshot 可读回 common time。仍未完成 delay/bias
  generation、NO5 同窗关联和物理绝对时间闭环，因此不能据此宣称跨板锁相。
- 本次 P3 运行完成 quick diagnostic flow，证据目录为
  `out/HardwareAcceptance/20260910/p3-060954/`，使用异步 OTA build
  `20260909221000`；receipt 的 `strict_gates_passed=false`。失败事实为 coarse CLK
  topology readback mismatch 和 NO5 `insufficient_stable_circular_span_windows`，
  该结果不构成 common-time 算法或正式锁相通过证据。
- 启动条件：先在 TDMA/RefMem/VDC 之间冻结 source/reference、sequence、CRC、共同
  时间锚、delay/bias generation 的 owner 和拒绝规则；在锚点缺失时只允许 diagnostic
  raw evidence，禁止 corrected jitter、formal lock 或从机实时应用。契约冻结后再修改
  C 端字段/adapter/observer，并同步更新 host/C 单测和 P3 证据。
- 下一 gate：完成 `VDC-OBS-ALG-001` 阶段 B 的 delay/bias generation、物理方向和
  owner 审计；未完成前不把 NO5 外部曲线与内部 DPLL 曲线做跨板锁相结论。

### VDC-PROGRESS-20260910-003 — generation admission and capture schema v4

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 变更：正式 path table 下，Core1 evidence admission 现在要求非零且匹配 active
  `calibration_generation`/`bias_generation`；缺失或不匹配分别以
  `VDC_DOMAIN_GATE_DELAY_GENERATION`、`VDC_DOMAIN_GATE_BIAS_GENERATION` 拒绝，不能
  通过 debug continuation 进入 PI/DCO、corrected jitter 或 formal lock。临时训练表
  仍保持 diagnostic-only 语义。DPLL capture record 升为 schema 4，保留 generation
  provenance；decoder 继续兼容 schema 1/2/3。
- 软件验证：`run_vdc_domain_tests.ps1`、相关 Python 观测回归（88 passed）和
  `cmake --build --preset pico2-release --parallel 4` 通过；新增覆盖正式 generation
  缺失/错误的 C gate 和 schema 4 decoder generation 保留测试。
- P3 证据：当前源码指纹下 quick diagnostic 完成，证据目录为
  `out/HardwareAcceptance/20260910/p3-064132/`。receipt 的
  `strict_gates_passed=false`；TRN-01/03、TDMA startup barrier、NO5 RX bad counter
  和时间预算仍失败。该结果只证明验收流程到达内部/NO5 观测阶段，不构成正式锁相或
  `FORMAL_LOCKED` 证据。
- 边界：generation 目前已进入 C 端正式 admission 和 capture provenance，但 NO5
  waveform quality flags、内部/外部同窗关联、segment continuity 和质量报告仍未闭环。
- 下一 gate：补齐 raw-only/corrected-eligible 的 waveform flags 与同窗关联，再执行
  `1M3F`、`2M2F`、`3M1F`、主站切换及丢样/坏帧/背压故障注入。

### VDC-PROGRESS-20260910-004 — waveform quality separation and diagnostic SVG layers

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B/C。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 变更：NO5 waveform schema 4 的每条记录显式保留 `sample_seq` 和
  `quality_flags`。质量位区分 timestamp eligibility、sequence continuity、source
  drop、matched-window validity、gap、ambiguous edge、incomplete window、raw-only
  和 corrected eligibility。decoder 对旧 schema 继续兼容，但只把可证明的字段推导为
  弱质量事实；sequence/capture gap 会清除 corrected eligibility。
- 分析边界：raw tracking 继续保留用于诊断；phase/jitter/convergence、CSV 和 summary
  的正式统计只使用 corrected-eligible 且窗口完整的样本。SVG 同时显示 raw-only 灰色点、
  incomplete window 标记和 corrected 曲线，并写出质量 flags，避免把观测缺口误读为
  节点 jitter 或锁相失败。
- 软件验证：`python -m py_compile tools/dpll_waveform_capture/dpll_waveform_capture.py`
  通过；DPLL waveform/observation decode/residual analyzer 回归为 `44 passed`。
  新增 schema 4 quality 保留、置信度兼容和 SVG 分层覆盖。
- 边界：尚未完成 C 端与 NO5 的同窗 sequence/capture-generation 关联，也没有新的
  当前源码 P3/HIL 证据；本 checkpoint 不证明任一节点 `LOCKED` 或 `FORMAL_LOCKED`。
- 下一 gate：完成阶段 B 的 C 端 source/reference、delay/bias generation 和共同时间
  锚审计，再实现阶段 C 的内部/外部同窗关联及坏帧、丢样、跨 segment 缺口故障注入。

### VDC-PROGRESS-20260910-005 — schema-v4 build and P3 preflight result

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B/C、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- 构建：`cmake --build --preset pico2-release --parallel 4` 通过。schema 4 记录扩容后
  首次链接超出 RP2350 RAM；将 `VDC_DPLL_MANAGER_WAVEFORM_SEGMENT_MAX_RECORDS`
  从代码原值调整为当前符号定义的 416，并重新链接通过。分段、drop 计数和 decoder
  连续性语义未改变。
- 软件验证：VDC/RefMem/TDMA host unit scripts 全量 `37/37` 通过，观测 Python 回归
  `44 passed`，文档门禁与文档回归 `18 passed`。
- P3：当前源码指纹下运行 `python tools/hardware_acceptance/p3_hardware_acceptance.py run`
  使用 build `20260909231608`，在 Latency Cal profile apply 前置阶段停止；板
  `2BD5090FE009FA2A` 回读 `active_level=0`，其余三板回读请求 level 7。原始证据位于
  `out/HardwareAcceptance/20260910/p3-071601/p0t-topology/`。未进入内部/NO5 观测，
  不构成算法或锁相结论。
- 下一 gate：先恢复四板 calibration profile 一致性，再执行当前源码 P3；算法侧继续
  完成 C 端物理方向、generation 和同窗关联，不以本次硬件前置失败修改观测结论。

### VDC-PROGRESS-20260910-006 — current-source P3 reaches observation gates

- TODO task ID：`VDC-OBS-ALG-001` 阶段 B/C、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-10。
- P3：当前源码指纹下完整运行 `python tools/hardware_acceptance/p3_hardware_acceptance.py run`，
  build `20260909232400`，证据目录 `out/HardwareAcceptance/20260910/p3-072352/`。
  流程完成但 `strict_gates_passed=false`，profile 为 `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`。
- 原始失败事实：coarse CLK topology readback mismatch、coded marker gate 未通过，
  NO5 为 `insufficient_stable_circular_span_windows`。NO5 只有 1 个观测样本，
  `timestamp_eligible=false`、`phase_round_count=0`；NO1--NO4 也只有单点快照，
  均为 provisional，不能据此判断锁相或 jitter。TDMA startup barrier 最终稳定，但早期
  仍记录 NO1 transport/header 差异、process reject 和 bitmap incomplete；这些事实保留
  在 `diagnostic.json`，不能被观测算法摘要覆盖。
- 算法边界：本轮 schema 4 质量层、raw/corrected SVG 分层已进入当前源码 build，
  但由于硬件 gate 没有产生可用同窗窗口，未验证 corrected jitter 或内部/NO5 关联。
- 下一 gate：修复 P3 的 topology/coded-marker 前置状态并收集多窗口、多点 NO1--NO4/NO5
  样本；随后执行阶段 C sequence、capture-generation、segment continuity 和坏帧/丢样
  故障注入，仍禁止将 provisional/diagnostic 结果升级为 `FORMAL_LOCKED`。

## 进度记录

### VDC-PROGRESS-20260908-006 — configurable DPLL role and oscillator discipline priority raised

- TODO task ID：`VDC-ROLE-001`、`VDC-ROLE-002`、`VDC-ROLE-003`、`VDC-ROLE-004`、`VDC-ROLE-005`。
- 状态：IN PROGRESS。
- 日期：2026-09-08。
- 变更：将可配置 DPLL 控制角色提升为当前最高优先级。设计固定为每节点保留 PI
  能力，角色为 `MASTER` 时执行既有 local evidence 到 PI/DCO 路径，角色为
  `FOLLOWER` 时只接收显式 source slot 的已验证 peer command；从机 local evidence
  不得更新积分、rate、phase 或成为隐式 fallback。训练与 Calibration 只测量时延，
  不因角色改造改变。
- 实现计划：先完成 Domain control profile 与 role switch 清理，再完成按 source slot
  的 RefMem command retention 和 manager apply，随后接入 Flash/SCPI staging/store；
  再建立不改写 DDS phase owner 的本地晶振 trim、clock-model 连续性和 stale/fault
  freeze，最后执行主从组合、切换、陈旧/错误来源/丢命令/trim fault 的故障注入与 P3/HIL。
- 证据与边界：本 checkpoint 仅冻结任务优先级与验证边界，尚无本切片源码、构建或 HIL
  结果；不宣称角色模式已生效、DPLL 已收敛或 `FORMAL_LOCKED`。
- 下一 gate：`VDC-ROLE-001`。实现并运行 Domain host C 单测，证明 master local PI
  保持可用、follower 旁路 local PI 且 role switch 清理旧积分/连续锁定状态。

### VDC-PROGRESS-20260908-005 — four-slot live batch coalescing

- TODO task ID：`VDC-OBS-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-08。
- 变更：commit `6297816` 将 `SYNC_IO_LOGIC_ANALYZER_CORE0_BATCH_SLOTS` 扩展为
  四个，并让 Core0 drain 在同一 capture sequence 内合并多个 `READY` batch，直到
  调用方 capacity 用尽；不会读取 active producer ring，容量或 capture 不匹配的
  slot 会保留为 `READY`。实时 phase decoder 和 record/header/schema 未改变。
- 软件与构建：`run_sync_io_logic_analyzer_tests.ps1`、全量 Python 回归
  （`788 passed`）和 `cmake --build --preset pico2-release --parallel 4` 均通过；
  release package build id 为 `20260907155445`。
- P3 证据：快速五板诊断完成，证据目录为
  `out/HardwareAcceptance/20260908/vdc-live-batch-coalesce-p3-20260908/`；
  `check-staged`、pre-commit 和 staged 源码指纹通过，TDMA process-image 通过。
  本轮 NO1-NO4 内部 DPLL SD 采样与 SVG 已生成。
- 失败与边界：NO5 观测在 TDMA preflight 发现 NO1 的
  `ring_adapter_rx_bad_count` 增长后未写出完整 `summary.json`，因此本轮没有新的
  可比 NO5 dropped count；诊断证据保留在 `diagnostic.json` 和
  `dpll-no5-observation/progress.json`，不能宣称正式 DPLL lock 或 strict gate 通过。
- 下一 gate：继续 `VDC-OBS-001`，在稳定 TDMA preflight 后运行 NO5 长时间观测，比较
  4-slot 合并前后的 dropped/segment 连续性；若仍有背压，再评估 StorageAO 写入节流。

### VDC-PROGRESS-20260908-004 — segmented trace export compatibility

- TODO task ID：`VDC-OBS-001`、`VDC-OBS-003`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-08。
- 变更：commit `b11a74b` 修复离线导出工具只识别旧版
  `analyzer_<session>.bin` 的问题，使其同时发现固件 schema 2 的
  `analyzer_<session>_<segment>.bin` 分段文件，并保留 `segment_from_name` 身份；
  旧命名继续兼容。这样长期 live-batch 的分段不会在目录扫描阶段被静默漏掉。
- 软件验证：`tests/python/test_analyzer_trace_export.py`、
  `test_analyzer_trace_decode.py`、`test_analyzer_trace_batch_index.py` 共 `21 passed`；
  `py_compile` 通过。
- 构建与 P3：当前源码 build `20260908041206` 的四板 OTA、P3、TRN-00/01/02
  和 TDMA process-image/FIFO 证据位于
  `out/HardwareAcceptance/20260908/vdc-export-segments-p3-20260908/`；
  `check-staged` 和 pre-commit P3 指纹门禁通过。receipt 仍为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，DPLL/NO5 因 `--tdma-only` 跳过，且
  `strict_gates_passed=false` 的耗时边界仍保留。
- 失败与边界：本切片只修复导出发现，不代表已经完成 StorageAO 长期背压、drop
  interval、恢复点或断电恢复；也不能把 TDMA-only receipt 提升为
  `FORMAL_LOCKED`。完整 DPLL/NO5 失败原始样本和 SVG 继续保留在
  `out/HardwareAcceptance/20260908/vdc-live-batch-p3-20260908/dpll-no5-observation/`。
- 下一 gate：`VDC-OBS-001`。在真实板端长期 live-batch 上验证分段目录分页、下载、
  decoder/index 连续性，并补 StorageAO 背压、掉电/重启恢复的原始证据；完成前不推进
  `VDC-OBS-002` 或正式 DPLL lock。

### VDC-PROGRESS-20260908-003 — schema-v2 analyzer decode and sequence-wrap evidence

- TODO task ID：`VDC-OBS-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-08。
- 变更：commit `5661a94` 补齐离线 analyzer decoder/index 对固件 schema 2
  header 的解析，保留 `segment_index`、`first_record_sequence` 和 `batch_sequence`，
  并以 uint32 模运算识别记录与跨 segment 的序列间隔。`0xFFFFFFFF -> 0` 的正常
  wrap 不再被误报为 drop；该工具仍只描述已持久化的本地 pad-visible 数据。
- 软件验证：analyzer decoder/index 回归 `8 passed`；
  `tools/tests/run_sync_io_logic_analyzer_tests.ps1` 通过；
  `tools/tests/run_host_unit_tests.ps1` 全量 `37/37` 通过；`py_compile` 通过。
- 构建与 P3：当前源码 build `20260908032311` 的四板 OTA、P3、TRN-00/01/02
  和 TDMA process-image/FIFO 证据位于
  `out/HardwareAcceptance/20260908/vdc-observe-wrap-p3-20260908/`；
  `python tools/hardware_acceptance/p3_hardware_acceptance.py check-staged` 和
  pre-commit 的 P3 staged 指纹门禁通过。receipt 为
  `config/hardware_acceptance/p3_acceptance_receipt.json`，验收范围明确为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，DPLL/NO5 因 `--tdma-only` 跳过。
- 失败与边界：该 receipt 的 `strict_gates_passed` 仍为 `false`；唯一记录失败为
  验收耗时 `324.452s` 超过 `100.000s`，动作是 `DEBUG_BOUNDED_FORCE_CONTINUE`。
  该次运行不是完整 DPLL/NO5 验收，不能宣称 `FORMAL_LOCKED`。先前完整 DPLL/NO5
  失败样本、原始 segment 和 SVG 仍保留在
  `out/HardwareAcceptance/20260908/vdc-live-batch-p3-20260908/dpll-no5-observation/`。
- 下一 gate：`VDC-OBS-001` 保持 IN PROGRESS，继续做真实长期 live-batch，验证
  StorageAO 背压、drop interval、恢复点和断电恢复；在这些证据闭环前不推进
  `VDC-OBS-002`，也不重新宣称正式 DPLL lock。

### VDC-PROGRESS-20260908-002 — bounded live analyzer batch handoff

- TODO task ID：`VDC-OBS-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-08。
- 变更：commit `21cbe8e` 将 `EDGE_TIMESTAMP` 的 Core1 active ring 经固定大小、显式
  `READY` 状态的 batch slot 发布给 Core0；Core0 只读取已发布 slot。每批携带
  capture/batch/first-record sequence 和 drop 计数，STOP/complete 仅在最后批次排空后
  发布 shadow，重复 ARM 在 batch 或 shadow 未排空时拒绝。StorageAO header 同步记录
  capture、batch、segment 和 first-record identity，仍保持在 Core0 执行。
- 软件验证：`tools/tests/run_sync_io_logic_analyzer_tests.ps1`、
  `tests/python/test_sync_io_logic_analyzer_contract.py` 和
  `tools/tests/run_host_unit_tests.ps1` 均通过；后者包含全量 host unit suite。
- 构建与 P3：current-source build/P3 receipt 为 build `20260908020537`，证据目录为
  `out/HardwareAcceptance/20260908/vdc-live-batch-p3-20260908/`；
  `python tools/hardware_acceptance/p3_hardware_acceptance.py check-staged` 和
  pre-commit 均通过，TDMA process-image/P3 原始结果在同目录。
- 失败与边界：NO5 外部观测仍有 SD segment drop，raw phase gate 未通过；
  `dpll-no5-observation/waveform/analysis/dpll_convergence.svg` 保留失败波形，不能作为
  收敛或 `FORMAL_LOCKED` 证据。该诊断失败未改变 TDMA 短帧验收结论。
- 下一 gate：`VDC-OBS-001` 保持 IN PROGRESS，收集该 live-batch 路径的长时间 wrap、
  StorageAO 背压、drop interval 和断电恢复证据；在 `SYNC-LA-003/005` 退出门禁闭合前，
  不得推进 `VDC-OBS-002`。

### VDC-PROGRESS-20260908-001 — debug admission continuation and five-board diagnostic P3

- TODO task ID：`VDC-EVID-001`、`VDC-VERIFY-001`、`VDC-OBS-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-08。
- 变更：commit `d126fa2` 增加 debug-only admission continuation。recoverable evidence
  gate 的 raw code/slot/evidence sequence 由 VDC snapshot 保留，但该样本不进入 PI/DCO，
  不改变 accepted/rejected sample count；结构性 identity/schedule/window contract 错误仍
  严格拒绝。SCPI `DPLL:OVERRide` 使用 Core0 单槽 mailbox，TRN-03 只在同时指定
  `--diagnostic-continue` 和 `--dpll-provisional` 时启用，并记录 requested/applied
  generation。debug 状态下 RefMem 不发布 formal locked flag。
- 软件验证：VDC domain host C、RefMem VDC vector host C，以及 DPLL/NO5/SyncIO/TRN-03/P3
  Python 回归均通过；release build 和 staged hardware-acceptance fingerprint gate 均通过。
- 构建与 P3：五板 OTA 和默认 quick P0--P3/TRN-03 的 current-source diagnostic receipt
  位于 `out/HardwareAcceptance/20260908/vdc-debug-admission-p3-20260908/`。四板 TRN-03
  realtime/closed-loop、TDMA preflight 和 process-image soak 通过；每块 ring Node 的
  debug admission 都读回 `ACTIVE`，requested/applied generation 一致，TDMA receive 与
  transport reject 增量为零。NO1--NO4 internal DPLL SD capture 已保留。
- 失败与边界：该 receipt 的 strict gates 仍未闭合。NO5 raw waveform 有 SD segment drop，
  未产生完整 phase round；quick flow 也超过既有时间预算。两项原始原因保留在
  `diagnostic.json`、NO5 waveform segment 和 SVG 中，不能用于宣称收敛或
  `FORMAL_LOCKED`，也不应归因于 DPLL admission 或作为屏蔽 TDMA 节点的理由。
- 下一 gate：`VDC-OBS-001`。先完成 runtime producer-to-Core0 bounded batch 接口，再推进
  `VDC-OBS-002` 的分段流式 SD 写入，消除 NO5 长期观测的 storage backpressure/drop
  缺口；之后才重新评估 `VDC-EVID-001`、`VDC-SERVO-002` 和 `VDC-VERIFY-001`。

### VDC-PROGRESS-20260907-005 — T3 matrix identity and Windows progress publish recovery

- TODO task ID：`VDC-TDMA-001`、`VDC-EVID-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 结论：独立 T3 没有使用错误校准矩阵。验收内置和独立入口均调用
  `tools/calibration_ring_validate/trn03_closed_loop.py`；独立复现读取与验收相同的
  `trn03-matrix.json`，generation、topology/profile/schedule CRC、物理节点顺序和
  offset row 均一致。复位后独立 T3 的启动稳定门和四节点 process-image soak 通过，
  证明先前失败来自运行起点/首帧边界状态而非矩阵选择。
- 工具修复：`ProgressReporter` 不再固定复用 Windows 的 `progress.json.tmp`；每次发布
  使用唯一 pending 文件，目标被 IDE/扫描器短暂锁定时写入带序号 fallback 并继续实时
  gate。新增锁占用回归测试；`tests/python/test_trn03_closed_loop.py` 为 `108 passed`。
- 硬件证据：最终源码 build `20260907110305` 的 quick P3/五板 OTA 证据位于
  `out/HardwareAcceptance/20260907/vdc-t3-progress-fix-r2-20260907/`；首次 T3 因
  `2BD5090FE009FA2A` ARM transient (`arm_result=8`, `-200 Execution error`) 失败，原始
  证据保留。复位后使用同一 package 的 `resume` 证据位于
  `out/HardwareAcceptance/20260907/vdc-t3-progress-fix-r3-resume-20260907/`，T3
  `passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`，progress
  文件完整发布，NO1–NO4 SD 样本数为 `8/13/13/10`。
- 边界：NO5 外部观测仍因 sequence skew `14`、SD dropped count `424` 未通过；本轮
  TRN-01 SCK 仍无 replay-safe row。两项均保留为严格失败/诊断反馈，不能提升为
  `FORMAL_LOCKED`，也不屏蔽 TDMA 节点。
- 下一 gate：解决 SCK replay-safe 矩阵和 NO5 外部观测/SD 连续性，再推进
  `VDC-EVID-001`；保持 provisional DPLL 只作调试反馈。

### VDC-PROGRESS-20260907-004 — quick full-flow acceptance and T3 comparison

- TODO task ID：`VDC-TDMA-001`、`VDC-EVID-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 验收范围：按默认 `QUICK_DIAGNOSTIC` 执行 P0–P3、T0–T3 TDMA process-image/FIFO
  短帧闭环和 DPLL；未使用 `--full`。五板 OTA、P3、T3 均保留在
  `out/HardwareAcceptance/20260907/vdc-full-acceptance-r1-20260907/`。
- 结果：build `20260907095401`；OTA 五板通过；内置 T3
  `passed=true`、`realtime_gate_passed=true`、`closed_loop_passed=true`，并保持四节点
  TDMA 运行。NO1–NO4 内部 SD 样本数为 `7/9/12/10`；曲线分析仍为诊断级
  `low_decimated`，不能提升为 `FORMAL_LOCKED`。NO5 外部观测因 ring sequence skew
  `54` 未通过，严格总验收保持失败事实；DPLL 反馈不隔离 TDMA 节点。
- T3 对照：验收编排器内置调用与独立入口均为
  `tools/calibration_ring_validate/trn03_closed_loop.py`、`process-image`、512 cycles、
  `--dpll-provisional`、clock evidence enabled、1 s/0.25 s soak。独立复现分别保留于
  `vdc-independent-t3-r1-20260907/`（persistent session）和
  `vdc-independent-t3-r3-short-open-20260907/`（`--short-open`）；两轮都在启动稳定门
  因 NO1 `rx_bad/transport_bad` 与 process reject 增长而超时。差异是验收前序 P0–P2/SMA
  与刚 OTA 的干净起点，以及串口时序环境，不是两套 T3 实现。
- 下一 gate：保持快速验收默认不开 T0–T3 capture；先处理 NO5/启动稳定性和 NO1–NO4
  收敛数据，再推进 `VDC-EVID-001`/`VDC-VERIFY-001`，不得用 provisional 或诊断结果
  宣称正式锁相。

### VDC-PROGRESS-20260907-003 — DPLL feedback without node quarantine

- TODO task ID：`VDC-SERVO-001`、`VDC-SERVO-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 变更：DPLL 失锁、phase residual 超限和 DPLL phase 的 WCET/deadline 计数保留为
  调试反馈；只要 TDMA UP/DOWN、process-image、FIFO 和基础收发连续，Core1 不再因
  DPLL phase 超限新增 `quarantined_mask` 或屏蔽节点。验收报告将 DPLL feedback 与
  TDMA 节点健康分开记录，调参器继续使用失锁/residual/frequency/reject 反馈小步
  调整并回退，等待连续样本逐渐收敛。
- 软件验证：相关 TDMA/P3 Python 回归通过；固件构建和五板 quick P3 证据分别保留
  在对应 `out/HardwareAcceptance/20260907/` 目录。当前硬件诊断仍可能因内部捕获
  无样本、NO5 SD dropped count 或波形稳定窗口不足而不构成 formal lock。
- 下一 gate：在不隔离 TDMA 节点的前提下重新收集 NO1–NO4 `FILTer?`/SD residual
  曲线，确认调参后的连续样本确实收敛，再评估 `VDC-SERVO-002`。

### VDC-PROGRESS-20260907-002 — debug Type-II PI tuning path

- TODO task ID：`VDC-SERVO-001`、`VDC-SERVO-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 变更：补齐 `loop_filter_integrator_ppb` 和 anti-windup；保留 FLL
  `last_frequency_error_ppb` 与 PI 积分状态的可观测分离。新增 debug SCPI
  `DPLL:TUNE`/`COEFficient`/`FILTer?`/`DEFAult`，通过 Core0 mailbox、Core1 service
  boundary 和 requested/applied generation 生效；新增 `tools/dpll_servo_tune/`
  对 NO1–NO4 逐步试探、评分、接受/回退并写入 JSON 原始响应。
- 调试语义：异常可解析参数不因产品范围被拒绝；实时路径对中间值做饱和保护，
  参数变更清空旧 acquisition/integrator history，不能自动提升 formal lock。
- 软件验证：VDC domain host C tests passed；DPLL/SCPI/残差相关 Python tests
  passed；极端 profile、signed SCPI tuple 和 anti-windup 负测已覆盖。
- 构建与 P3：本切片修改了固件、SCPI 和验收工具，必须在当前最终源码指纹下重新
  build/OTA/P3；在新 receipt 产生前不得提交或宣称硬件闭环通过。
- 下一 gate：完成当前源码指纹下的五板 quick P3，并使用调参器收集 NO1–NO4
  `FILTer?`/vector/residual 曲线；仍以 `low_decimated`/振荡事实为诊断结果。

### VDC-PROGRESS-20260907-001 — quick capture policy and internal DPLL SD evidence

- TODO task ID：`VDC-TDMA-001`、`VDC-CAL-001`、`VDC-EVID-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-07。
- 变更：quick 验收默认关闭 T0–T3 SD raw capture；只有异常路径保留原始波形。新增 NO1–NO4 内部 DPLL `TRACE` ARM/STOP/SAVE、SD 下载和 residual 分析；NO5 继续作为外部只读 waveform observer。验收输出默认按 `out/HardwareAcceptance/YYYYMMDD/<run>/` 分区，显式 `--full` 才使用 full bench 配置。
- 软件验证：150 项 calibration/P3/OTA/state-machine Python 回归通过；VDC domain 与 resource arbiter host 单测通过。
- 构建与 P3：当前源码指纹下 quick P3/五板 OTA 完成，证据目录为 `out/HardwareAcceptance/20260907/vdc-internal-dpll-r2-20260907/`；NO1–NO4 各自产生可读 SD capture 与 residual SVG，NO5 waveform 另存于 `dpll-no5-observation/`。
- 结果：内部捕获链路通过，但样本量与 decimation 仍不足以宣称 formal convergence；分析报告标记 `low_decimated`，DPLL gate 失败事实保留在 diagnostic receipt。
- 下一 gate：继续定位 NO1–NO4 residual oscillation，并在增加稳定样本/完整证据后推进 `VDC-SERVO-001`。

### VDC-PROGRESS-20260906-004 — 三件标准文件基础重建

- TODO task ID：`VDC-TDMA-001`、`VDC-CAL-001`、`VDC-EVID-001`、`VDC-SERVO-001`、`VDC-SERVO-002`、`VDC-LOCK-001`。
- 状态：DONE。
- 日期：2026-09-06。
- 变更：重建 VDC Architecture/TODO/Task Progress canonical 正文；历史版本复制到 `docs/legacy/vdc/`。
- 架构结果：明确 STATE_MACHINE、TDMA Foundation、Calibration、VdcSyncAO、SyncDpllFB、VdcVector、RefMem 和 Trigger 的 owner 边界；分离资源生命周期状态机与 VDC 锁相状态机。
- TODO 结果：建立从 resident cycle、active calibration、formal evidence、FLL 粗锁、Type-II PI、promotion、snapshot、HOLDOVER 到 RUN/HIL 的唯一迁移顺序。
- 验证：本记录完成后执行 docs_check、doc_regression、文档 pytest 和 Git Bash pre-commit。
- 证据：历史原文快照位于 `docs/legacy/vdc/`；工具中间快照位于 `out/doc-archive/vdc-20260906/`。
- 下一 gate：`VDC-TDMA-001`。

### VDC-PROGRESS-20260906-003 — 状态机域对齐检查

- TODO task ID：`VDC-TDMA-001`、`VDC-EVID-001`。
- 状态：DONE。
- 日期：2026-09-06。
- 变更：对照 `docs/state_machine/HAOFV_STATE_MACHINE_ARCHITECTURE.md`、`HAOFV_STATE_MACHINE_TODO.md` 和任务进度，确认 VDC 只消费 `RESIDENT_INIT -> RUNNING` 后的 cycle/latch evidence。
- 结论：`STOPPED/STAGED/ARMED/RESIDENT_INIT`、persona 切换、resource fault 和 diagnostic capture 不能产生 formal DPLL evidence；`RUNNING` 内的 `CYCLE_BOUNDARY -> LOCAL_UNLOAD -> LOCAL_LOAD -> FORWARD` 才是正式 TDMA observation 的来源。
- 阻塞：状态机任务进度中的 NO5 DPLL phase/SD writer 阻塞仍属于上游验收事实；不得用 TDMA short-frame 通过替代 VDC formal lock。
- 下一 gate：完成当前源码指纹下的 TDMA resident/hardware-latch evidence，再推进 `VDC-CAL-001` 和 `VDC-EVID-001`。

### VDC-PROGRESS-20260906-002 — TDMA 确定性同步方法与锁相模型

- TODO task ID：`VDC-TDMA-001`、`VDC-CAL-001`、`VDC-EVID-001`、`VDC-SERVO-001`、`VDC-SERVO-002`、`VDC-LOCK-001`。
- 状态：DONE。
- 日期：2026-09-06。
- 结论：VDC 采用固定 process-image/trailer、同圈 T1/T2/T3/T4、Calibration path matrix、FLL-assisted acquisition、Type-II PI tracking 和 coarse/formal promotion。
- 参考：LinuxPTP、Chrony、NTPv4/RFC 5905、EtherCAT Distributed Clocks、IEEE 1588 hardware timestamp、White Rabbit 和 TSN/gPTP 方法边界已写入 Architecture。
- 下一 gate：先闭合 TDMA/Calibration/evidence，禁止以 `LOCKED` 或 replay passed 冒充 `FORMAL_LOCKED`。

## 验证与证据规则

每个 checkpoint 必须至少记录：

- 对应 TODO Task ID 和 owner；
- 当前源码/build/config identity；
- host/build/test 命令及结果；
- OTA/HIL/NO5/SD 原始证据路径；
- 失败原因、后继状态、回退点和下一 gate。

诊断 replay、host 单测、TDMA short-frame、NO5 外环观测和正式 VDC lock 是不同证据等级，
不得相互替代。formal promotion 失败时保留失败证据，不修改为成功状态。
