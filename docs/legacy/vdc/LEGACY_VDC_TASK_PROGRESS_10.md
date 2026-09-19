# VDC 历史任务进度 10

Status: Frozen
Domain: VDC
Canonical: `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_10.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/vdc/VDC_DOMAIN_TODO.md`
Last updated: 2026-09-19

> 按 C14 从主日志迁出的最旧连续记录；正文及同日顺序逐字保留。此处状态和数字为当时证据快照，当前任务以主域 TODO 为准。

### VDC-PROGRESS-20260918-033：主 OUT1 同步触发路径亦未在 CH1 捕获

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条只做
  STOP 态输出观测，不改变 DPLL/VDC 算法和持久化配置。
- r21 使用主输出 `REALtime:IO:OUTPut:WIDTh 1000` +
  `REALtime:IO:OUTPut:IMMediate`，示波器触发源为 CH1、正边沿、1 V，且在发射
  前已读回 `WAIT`。触发后仍为 `WAIT`，`SYSTem:ERRor?` 为 `0,"No error"`，宽度
  查询为 1000。原始记录在
  `out/HardwareAcceptance/20260918/no1-out1-immediate-ch1-r21/summary.json`。
- 与 r18 的静态 OUT1 驱动合并判断，SIO 和主同步 PIO 两条主输出路径都没有在
  示波器 CH1 形成可见边沿；这不是 DPLL 收敛或 TDMA 运输证据。下一 gate 仍是
  在 MCU 侧测试点或隔离器后端确认 OUT1 电平，再恢复四板 RAW 采样。

### VDC-PROGRESS-20260918-032：主 SMA 输出静态探测仍无外部电平

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条只做
  STOP 态物理诊断，没有修改固件、PIO、Flash、OTA、path-delay 或 output-delay。
- r18 将示波器触发源改为 CH1，先确认 `WAIT`，再在 NO1（COM5）执行主输出
  `MASK 0→1`（GPIO16/OUT1）。示波器约 220 ms 后仍为 `WAIT`；NO1
  `REALtime:STATus?` 为 `IDLE`，`SYSTem:ERRor?` 为 `0,"No error"`，说明
  静态输出命令已接受且没有 RUN persona 残留。原始记录在
  `out/HardwareAcceptance/20260918/no1-out1-ch1-trigger-r18/summary.json`。
- r19 的 STOP 只读确认 VDC RUN 已停止、realtime 状态为空闲；记录在
  `out/HardwareAcceptance/20260918/no1-run-owner-stop-r19.json`。
- r20 逐位驱动 NO1 主输出组并读取 NO2 主输入，四个位均保持输入掩码 `0`；该组
  不是当前 TDMA RJ45 运输线，因此只作为主 SMA/线缆诊断，不能解释为 TDMA 链路
  失败。记录在 `out/HardwareAcceptance/20260918/no1-to-no2-static-link-r20.json`。
- 结论：在不改变 DPLL 的前提下，当前外部示波器仍没有可用的 NO1 物理边沿。下一
  gate 是确认探头是否接在产品 SMA_OUT1..4（GPIO16..19）以及隔离器/连接器侧，
  或先用示波器直接测 MCU 侧测试点；确认主输出电平后再做 EXT 和 RAW 四路采样。

### VDC-PROGRESS-20260918-031：NO1 外部 EXT 触发物理路径仍未闭合

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条是停止态
  观测诊断，不修改固件、PIO、Flash、OTA、path-delay 或 output-delay。
- r14 在 NO1（COM5）释放后以 `REALtime:IO:OUTPut:MASK 8` 驱动 OUT4，示波器
  配置 EXT 正边沿；输出寄存器读回由 `0` 变为 `8`，但四通道只有约 0.27 V
  基线噪声，不能作为边沿证据。原始记录在
  `out/HardwareAcceptance/20260918/no1-manual-ext-trigger-r14/summary.json`。
- r15 先用 `:RUN` 确认示波器进入 `WAIT`，再驱动 OUT4；约 220 ms 后仍为
  `WAIT`，错误队列为 `0,"No error"`。这排除了“单次触发尚未置 WAIT”的脚本时序
  原因，但没有证明 EXT 端收到边沿。记录在
  `out/HardwareAcceptance/20260918/no1-manual-ext-trigger-r15/summary.json`。
- r16 改用固件已有的 `REALtime:IO:RJ45:WIDTh` + `REALtime:IO:RJ45:IMMediate`
  触发状态机，示波器仍保持 `WAIT`；但 r17 的硬件映射读回确认该命令驱动的是
  独立 RJ45 输出 GPIO26，而不是主输出组 OUT4/GPIO19，因此 r16 不能作为 OUT4
  的反证。记录在 `out/HardwareAcceptance/20260918/no1-rj45-immediate-ext-trigger-r16/summary.json`。
- r17 只读确认 NO1 固件为 `20260918024406`，主输出映射为 GPIO16..19，RJ45
  输入/输出为 GPIO27/26，OUT4 静态输出已恢复为 0；记录在
  `out/HardwareAcceptance/20260918/no1-io-profile-r17.json`。
- 结论：当前不能把 NO1 OUT4→示波器 EXT 当作可用触发源，也不能据此宣称四板
  物理锁相。下一 gate 是核对 OUT4 实物线缆/示波器 EXT 输入端和输入门限，或临时
  将同一 OUT4 接到 CH1 做直接电压观测；确认物理边沿后才恢复 RAW 深存储四路采样。

### VDC-PROGRESS-20260918-030：RAW 深存储四路采集未取得有效 NO1 触发

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条只记录
  既有采样失败，不改变任何运行配置。
- r11 使用 20 ns 网格但采样窗口与 START 时刻未对齐，未捕获有效边沿；r12 获得
  NO2 156、NO3 200、NO4 200 个边沿，NO1 为 0，故 NO1 触发不能用于四路同序
  相位判断。r13 在四板软件侧有输出规划/提交记录，但示波器 `TRIG:STAT?` 运行约
  1 s 后仍为 `WAIT`。对应原始证据分别位于
  `out/HardwareAcceptance/20260918/dpll-bridge-export-delay-zero-r11/`、
  `out/HardwareAcceptance/20260918/dpll-bridge-export-delay-zero-r12/` 和
  `out/HardwareAcceptance/20260918/dpll-no1-trigger-diagnostic-r13/`。
- 这些结果只能证明采样对齐和触发路径尚未闭合，不能反推 DPLL 算法失败，也不计入
  10 µs、1 µs 或 100 ns 锁定等级。下一 gate 由 `VDC-PROGRESS-20260918-031`
  的 EXT 物理确认决定。

### VDC-PROGRESS-20260918-029：四板新会话控制链 smoke 通过，示波器分辨率不足

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条验证新
  feedback session 的 STOP 配置、四板 ARM/START、运行期间零板卡查询、统一 STOP
  和原 RAM 时序恢复；没有写 Flash、修改 OTA 或 path-delay。
- 证据根为 `out/HardwareAcceptance/20260918/dpll-bridge-export-delay-zero-r10/`。
  NO1–NO4 使用零 output delay，四路都返回 200 个周期边沿，四板最终错误队列均为
  `No error`，输出 timing 已恢复到原配置。上一轮主机 readback 参数错误已修复，命令
  日志按阶段增量保存。
- 本轮示波器读取走的是便捷 NORM 点数接口，原件只有 1000 个样点、约 200 µs 的采样
  间隔；它只能证明控制链和连续周期存在，不能给出微秒、纳秒或 100 ns 相位结论，
  也不计入物理锁定等级。
- 下一 gate：复用同一新 session 控制链，但把示波器采集切换到已验证的 RAW/深存储
  分块读回（目标保持 20 ns 级采样），同时保留 bridge/PIO enable、RUN 模型代次和
  共同 ordinal 原件；若采集失败，仍须保留四板 STOP 与恢复证据。

### VDC-PROGRESS-20260918-028：RUN 模型代次与共同时间锚离线对账完成

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条只复核
  `dpll-bridge-export-delay-zero-r7` 与 `dpll-bridge-export-delay-candidate-r8`
  的既有 `input-probe.json`，没有修改固件、PIO、Flash、path-delay、output-delay
  或 OTA；新增报告是诊断证据，不改变物理锁相结论。
- 证据报告为
  `out/HardwareAcceptance/20260918/dpll-run-model-audit-r1/model-timeline-audit.json`。
  两个会话四板的 `last_target_vdc_ns` 都与 `last_ordinal × period_ns` 一致，说明
  当前 `anchor_vdc_ns=0` 与共同 ordinal 网格一致；没有证据支持先修改公共锚。
- 三从的 phase snapshot `after_base_vdc` 与末态 `follow_model.base_vdc_time64_ns`
  一致，证明本地 phase follow 的坐标修正已写入 Domain 模型。与此同时，RUN 聚合的
  `last_model` 在三从均早于 STOP 导出的末态 follow token，且原始记录没有逐边沿
  model token；不能把末态模型投射到整段波形，也不能把跨会话相位变化归因于单一
  output delay。
- 当前主线收敛为：保持零 output delay，补齐同一会话内的模型代次/bridge/PIO enable
  与共同 ordinal 关联；先证明运行窗口实际采用的模型和边沿，再做单因素 delay/path
  分离。公共锚、phase follow 和运输路径暂不改动。
- 下一 gate：在明确 DCO 收敛窗口后重复零值基线，并让采集原件保留每段输出的模型代次
  或等价边界记录；四板安全 STOP、运行期间零查询和外部示波器证据仍是必需条件。

### VDC-PROGRESS-20260918-022：RUN bridge 初始三元组导出切片完成四板 quick P3

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条为诊断导出切片
  的有限验收快照，不改变 OTA、活动 path-delay 表或实时调度策略。
- `VDC_RUN_OUTPUT_SCHEMA` 升为 9；首次成功 RUN bridge 采样把
  `timeline_raw_before`、`timeline_local_ns`、`timeline_raw_after` 保留到 STOP 诊断，
  并通过 `SYSTem:VDC:OUTPut:RUN?` 导出。三元组仅用于把 bridge 采样区间与首次 PIO
  enable/实际边沿关联，不是物理边沿或跨板相位结论。
- Host 回归为 176 项通过。Release 构建目录为
  `out/build/dpll-bridge-export-20260918`，flash-link contract 三项均为 `OK`；相对
  SRAM bridge r2，应用 `.text` 增加 0x20 字节、`.data` 增加 8 字节、`.bss` 增加
  0x18 字节。新增字段不进入 Core1 热入口的额外循环路径。
- 同源码四板 quick P3 使用 `--tdma-only` 完成，证据根为
  `out/HardwareAcceptance/20260918/p3-bridge-export-r1/`；NO1--NO4 均完成 STOP，
  NO5 未参与。报告为 `PASS_WITH_WARNINGS`，计数 `INFO=31/WARN=22/ERROR=0/FATAL=0`；
  receipt 已绑定本次源码树、固件包和四板摘要。既有 VDC/RefMem/DPLL 调度告警仍存在，
  不能据此宣称严格实时预算、VDC 发布或 100 ns 锁相已经闭合。
- 下一 gate：使用新的 RUN 三元组做同会话示波器复测，分别标注 bridge 区间、首次 PIO
  enable、path/output delay 和实际 NO1--NO4 边沿；保持运行期间零查询，并在单因素
  复测后再处理首次 enable 偏移与严格调度告警。

### VDC-PROGRESS-20260918-023：schema 9 三元组与四路波形同会话关联

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条是单次示波器
  会话的调试快照，所有计数和相位数字均为证据快照，非产品事实源；没有修改固件、
  path-delay 表或 OTA。
- 使用匹配 schema 9 的采样包装器和已连接示波器，NO1 CH1 上升沿触发，延迟窗口 2 s、
  20 ms/div，运行期间板卡查询为 0；四板和示波器均安全恢复。证据根为
  `out/HardwareAcceptance/20260918/dpll-bridge-export-scope-r1/`，原始 RUN 查询、
  scope 数据、解析器副本及 SHA256 均保留在 `capture/` 与 `dpll-run-timeline-r2/`。
- 四板各得到 200 个上升沿，采样网格为 20 ns，无粗大缺口。随后按共享时间轴和数组
  索引建立共同 ordinal；索引相位中位快照为 NO2 `-319.97 ns`、NO3 `-140.14 ns`、
  NO4 `420.17 ns`，对应线性斜率快照为 `0.556/-0.188/0.754 ppm`。解析结果明确
  `physical_lock_qualified=false`，所以不能宣称 100 ns 锁相。
- `scope-analysis/bridge-correlation.json` 显示四板首次 bridge 三元组的本地采样
  区间宽度相同，首次 PIO enable 的 raw enclosure 也相同；各板输出 delay 仍为零。
  这证明导出字段可用于关联诊断，但尚未证明跨板时间相位或消除首次 enable 偏移。
- 下一 gate：在共同 ordinal 基础上保持零查询，分别改变 output delay 或 path delay；
  每次仍需保留 bridge 三元组、PIO enable 锚、波形和 STOP 状态，不能用最近边沿中位
  替代严格 100 ns 判据。

### VDC-PROGRESS-20260918-024：output delay A/B 与零值对照完成

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条是两次独立
  启动会话的调试快照，参数和相位均为证据值；只在 STOP 状态配置 output delay，未改
  path-delay 表、Flash 或 OTA，结束后四板均恢复零值并通过 readback。
- 补偿组使用 NO2 `+320 ns`、NO3 `+140 ns`、NO4 `-420 ns`，运行期间零查询，四路
  各 200 个边沿且共同 ordinal 成立。相对 NO1 中位为 NO2 `579.83 ns`、NO3 `140.17 ns`、
  NO4 `40.28 ns`。完整证据在
  `out/HardwareAcceptance/20260918/dpll-bridge-export-delay-r3/`。
- 随后的零 delay 对照重试成功（第一次重试因 `START` 返回
  `OK(no payload; verified by state readback)` 的工具 ACK 适配失败，四板仍安全 STOP，
  无波形结论）。零值对照共同 ordinal 也成立，中位为 NO2 `419.89 ns`、NO3 `720.93 ns`、
  NO4 `579.94 ns`；证据在
  `out/HardwareAcceptance/20260918/dpll-bridge-export-delay-zero-r5/`。
- `dpll-bridge-export-delay-r3/delay-ab-comparison.json` 表明 output delay 能改变边沿，
  但两个独立启动状态的基线相位也发生变化，不能把本轮参数提升为持久校准，更不能据此
  宣称 100 ns 锁相。当前优先问题转为启动后 DCO/PIO 相位状态的可重复收敛，以及工具对
  无 payload ACK 的兼容。
- 下一 gate：在同一 DCO 状态或明确等待收敛后重复 A/B，记录 bridge 三元组、首次 enable
  锚、共同 ordinal 和相位斜率；先解决 ACK 兼容，再评估是否需要调整 output delay。

### VDC-PROGRESS-20260918-025：四板运输与本地 DCO 跟随证据闭合

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条只闭合本次
  RUN 会话的运输/DCO 证据，不改变固件、Flash、path-delay 或 output-delay 配置。
- 零 delay 对照的原生记录显示 NO2--NO4 `matched_observation_passed=true`，typed
  observation 接收分别为有限运行快照中的万级计数，typed reject 均为 0；三板 local
  follow model 均 `valid=1`、`lock_state=1`。NO4 的 DCO 序号在会话中实际前进，NO2/NO3
  末态保持当前 DCO 序号但 follow 已应用有限调整，说明“收到 NO1 事件并在本地跟随”
  与“物理输出边沿已对齐”是两个独立闭合点。
- 证据文件为
  `out/HardwareAcceptance/20260918/dpll-bridge-export-delay-zero-r5/capture/scope-analysis/dpll-follow-closure.json`，
  原始 `input-probe.json`、RUN 三元组和波形均可回溯。该证据不能替代共同 ordinal、
  100 ns 波形门禁，也不能宣称最终 VDC 一致发布。
- 当前主线阻塞已收敛为启动后 DCO/PIO 输出映射的可重复性和 output/path delay 的独立
  误差预算；运输、ACK、匹配和本地跟随不再作为本轮物理锁相的首要阻塞。
- 下一 gate：在同一 DCO 状态或明确等待收敛后重复 output-delay A/B，保留 follow model、
  bridge/enable 锚及共同 ordinal；同时修复 `START` 无 payload ACK 的采样工具适配，避免
  工具假失败掩盖实际运行状态。

### VDC-PROGRESS-20260918-026：同一采样包装器完成四板零 delay 波形复测

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条是一次新的
  四板示波器诊断快照；未改固件、PIO、Flash、OTA、活动 path-delay 或正式工具，只在
  `out/` 采样副本中保留无 payload 控制返回并要求后续状态读回。
- 证据根为
  `out/HardwareAcceptance/20260918/dpll-bridge-export-delay-zero-r7/`。示波器为 NO1
  CH1 上升沿触发，四路各 200 个边沿，采样网格 20 ns，运行期间板卡查询为 0；四板
  完成 STOP，示波器恢复 `STOP/EXT/NORM`，采集报告 `passed=true`、无 cleanup error。
- 共享时间轴和共同 ordinal 在本会话成立，零 output delay 的相位中位快照为 NO2
  约 `-439 ns`、NO3 约 `-60 ns`、NO4 约 `+240 ns`；NO3 的 p01--p99 约为
  `-80..-40 ns`，说明该次有限窗口进入 100 ns 量级，但 NO2/NO4 仍未达到完全锁定。
  这些数字是波形快照，不能跨启动会话直接推导持久 delay 或 path 校准。
- 同会话 `capture/input-probe.json` 仍显示 NO2--NO4
  `matched_observation_passed=true`，typed reject 为零，三从 follow model `valid=1`、
  `lock_state=1`。这进一步把当前缺口限定为启动后输出映射可重复性、output/path delay
  误差分离和持续斜率收敛；不能宣称四板 100 ns 锁相或 VDC 正式发布。
- 下一 gate：在同一 DCO 状态或明确等待收敛后重复单因素 output-delay A/B，继续保留
  bridge/PIO enable 锚、共同 ordinal、相位斜率和 STOP 状态；任何候选值先停留在
  RAM 调试配置，不直接写入 Flash。

### VDC-PROGRESS-20260918-027：候选 output delay A/B 完成并恢复零值

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条为独立启动
  会话的输出补偿诊断快照；只在 STOP 状态通过 SCPI 写入 RAM 请求，未执行 Flash 保存、
  OTA 或固件/PIO 修改，采样结束后四板均恢复 `0 ns` 并读回无错误。
- 候选值为 NO2 `+440 ns`、NO3 `+60 ns`、NO4 `-240 ns`，依据上一零 delay 会话的
  有限相位快照生成。证据根为
  `out/HardwareAcceptance/20260918/dpll-bridge-export-delay-candidate-r8/`，对比文件为
  `delay-candidate-comparison.json`；四板示波器仍使用 NO1 CH1 上升沿触发，四路各
  200 个边沿、共同 ordinal 成立，运行期间零查询且安全 STOP。
- 本次候选会话相位中位为 NO2 约 `+659 ns`、NO3 约 `-280 ns`、NO4 约 `+42 ns`。
  相对 `VDC-PROGRESS-20260918-026` 的跨会话变化分别约 `+1098 ns`、`-220 ns`、
  `-198 ns`；NO4 进入 100 ns 量级，但 NO2/NO3 没有按候选值呈现可重复的固定增益。
  因此这些结果只证明 output delay 能影响边沿，不能推导持久校准或 path delay。
- 同会话四板运输、匹配、ACK 和 local follow model 仍有效；当前首要缺口收敛为启动后
  DCO/PIO 输出映射状态的可重复性，以及 output delay、path delay、bridge/enable 和
  频差残差的独立误差预算。不能宣称四板 100 ns 锁相或 VDC 正式发布。
- 下一 gate：保持 output delay 零值，先用同一 DCO 状态或明确的收敛窗口重复零值基线，
  再只改变一个 output delay；比较共同 ordinal 的相位变化与 bridge/enable 锚，候选值
  继续只留在 RAM 调试配置。

### VDC-PROGRESS-20260918-021：NO1 OUT4 外部触发路径未捕获

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条为触发路径
  诊断快照，不改变固件、PIO、Flash、OTA 或输出补偿。
- 使用与 `VDC-PROGRESS-20260918-020` 相同的四板构建、输出时序和零运行查询约束，
  仅将示波器触发源改为已接入的 NO1 OUT4 `EXT`。示波器设置读回为
  `EDGE/EXT/POS/1.5 V/NORM`，但整个四板运行窗口没有完成一次 single acquisition；
  `capture_complete=false`，子流程以 `No completed single trigger` 结束。
- 四板均完成收尾，恢复输出时序，示波器最终读回 `STOP/EXT/NORM`，没有 cleanup error。
  证据根为 `out/HardwareAcceptance/20260918/dpll-bridge-sram-ext-r2/`。该目录只能作为
  EXT 路径未捕获的负证据，不能与 CH1 四路波形混合分析。
- 下一 gate：在再次做物理锁相判断前，先独立确认 NO1 OUT4 是否实际产生与 CH1 同序的
  脉冲、触发电平/边沿及探头连接；确认后再重复同会话 EXT 采样。当前有效相位结论
  仍以 CH1 触发的 `dpll-bridge-sram-r2/capture/scope-analysis/review.json` 为准。

### VDC-PROGRESS-20260918-020：SRAM bridge 版本同会话四路示波器复测

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条为单次硬件
  会话快照，非产品事实源；使用提交 `9c410a71` 对应 receipt/build
  `20260918021531`，未改变 Flash、OTA、path-delay 表或 DPLL 参数。
- 复用已审查的 `capture_late_scope.py`，配置 `40000,48000,32000`、NO1 CH1
  上升沿触发、延迟窗口 2 s、20 ms/div；四板 ARM/START 后运行期间没有板卡查询，
  捕获脚本通过，结束时四板和示波器均恢复 STOP/EXT/NORM。证据根为
  `out/HardwareAcceptance/20260918/dpll-bridge-sram-r2/`，原始波形和本地记录均保留。
- 离线复核 `analyze_resume_scope_cursor.py`：四路各 200 个上升沿，采样网格 20 ns，
  窗口约 1.9--2.1 s，未发现粗大缺口；NO1 周期中位误差接近 0 ns。相对 NO1 的最近
  边沿中位（未建立共同 ordinal）为 NO2 `580 ns`、NO3 `380 ns`、NO4 `260 ns`，
  有限窗口斜率约 `-0.186/-1.329/-0.333 ppm`。这是实际输出仍未达到 100 ns 的证据，
  不能用最近边沿算法替代严格同序判据。
- 与 `VDC-PROGRESS-20260918-018` 的跨会话结果相比，零输出补偿下偏移仍发生变化，
  所以当前证据无法把误差唯一归因于 path delay、RUN bridge 或首次 PIO enable；
  SRAM 采样切片本身没有改变“物理锁相未完成”的结论。原始证据明确标注
  `physical_lock_qualified=false`。
- 下一 gate：在同一会话导出初始 bridge 三元组、首次 PIO enable 锚和每次输出映射的
  代际，建立可复核误差预算；随后只改一个因素重测。仍须保持运行期间零查询、四板
  STOP 收尾，并按 host、Release/资源、quick P3、专项波形和分离提交闭环。

### VDC-PROGRESS-20260918-019：SRAM bridge 采样切片完成四板 quick P3

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条数字均为
  有限验收快照，非产品事实源；本切片只改变 `vdc_timestamp_bridge_sample()` 的
  有界 SRAM 采样位置，未修改 OTA、活动 path-delay 表或其他工作区中的
  `tdma_flight_engine.c`。
- Host 回归保持 245 项通过；Release 构建目录为
  `out/build/dpll-bridge-sram-20260918`。反汇编确认采样函数位于 SRAM，采样区仅含
  TIMER0/TIMER1 MMIO 读取、回绕比较、内存栅栏和返回；无 XIP 调用、等待、配置写入
  或持久状态。资源快照为 text 871644、bss 462656，bss 未增加。
- 同源码四板 quick P3 命令使用 `--tdma-only` 完成，证据根为
  `out/HardwareAcceptance/20260918/p3-bridge-sram-r1/`，四板为 NO1--NO4，NO5
  DPLL 观测按范围跳过，最终均完成 STOP。报告 `alarms.json` 为
  `PASS_WITH_WARNINGS`，计数 `INFO=33/WARN=18/ERROR=0/FATAL=0`；receipt 已绑定
  本次源码树、固件包、四板 OTA 摘要和 TDMA 摘要。
- 该结果不是严格实时门禁通过：`tdma-process-image/summary.json` 的
  `diagnostic_passed=true`，但 `realtime_gate_passed=false`、`closed_loop_passed=false`。
  四板严格阶段仍报告 VDC/RefMem deadline 或负载告警，DPLL 反馈的 overrun/deadline/
  WCET 项被标记为 diagnostic-only；因此本切片不能宣称 100 ns 锁相、VDC 发布或
  DPLL 实时预算已经闭合。
- 下一 gate：用本次 SRAM 采样版本做同会话示波器专项，比较 RUN bridge 映射区间与
  实际 NO1--NO4 边沿；随后独立处理首次 PIO enable 和严格 VDC/RefMem 调度告警。
  每项仍须 host、Release/资源、同源码四板 quick P3 和专项原始证据后再叠加。

### VDC-PROGRESS-20260918-018：输出 delay A/B/A2 复测，定位 RUN 映射不确定度

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本条数字均为
  有限调试快照，非产品事实源。未改固件、活动 path-delay 表、Flash 或 OTA；其他
  设备的 `tdma_flight_engine.c` 工作区状态保持。本轮没有新的固件切片或 P3 凭证。
- 复用已校验源码凭证的 `capture_late_scope.py`，保持 `--scope-offset 2
  --scope-scale .02 --profile 40000,48000,32000`，补齐 `baseline-a2`。A2 全流程
  约 62 s，采集、清理和配置恢复成功；四板已 STOP，示波器恢复 STOP/EXT/NORM，
  运行期间板卡/示波器查询为零。RUN 末态确认四板输出 delay 都为零。
- 证据根 `out/HardwareAcceptance/20260918/dpll-output-delay-ab-r1/`：早期
  `capture/` 的示波器偏移拒绝仍保留，不能算有效运行；`baseline-a`、`candidate-b`、
  `baseline-a2` 是三次独立有效会话。B 的输出 delay 分别为 0/-620/-1240/-1780 ns，
  结束后恢复为零且未 STORE。离线 `audit_restart_mapping.py` 生成
  `restart-mapping-audit.json`，绑定原始报告、原生记录、波形分析及相关源码 SHA256。
- 相对 NO1 的最近周期边沿中位（单位 ns，NO2/NO3/NO4）：A 约
  +3126/+579/+1100，B 约 -517/-1100/-1361，A2 约 -820/+80/-260。
  A→A2 同为零输出补偿，变化约 -3946/-499/-1360 ns，故跨会话 A/B 不能隔离
  delay 传递增益，也不能证明每跳固定偏移或唯一根因。
- A2 波形窗口 1.9–2.1 s，采样网格 20 ns，每通道 200 个上升沿，无粗大缺口；
  三从相位范围约 -859～-800、+40～+120、-300～-200 ns，有限窗口斜率约
  +0.044/+0.376/-0.174 ppm。NO3 中位接近零不代表通过 100 ns 门禁；尚未建立
  共同 ordinal。原生记录容量先于 STOP 耗尽，亦不能由该窗口外推完整持续输出。
- 代码与末态对账确认：RUN 每次请求仅取一个 TIMER0/TIMER1 bridge，之后使用
  `vdc_timestamp_timeline_local_to_raw()` 的 `raw.hi` 安排边沿。三次输出映射最大
  区间宽约 1432～4392 ns；独立输出 enable enclosure 约 1144～1152 ns。事件
  observer 的 72 ns enclosure 不等于输出锚点精度。区间宽是误差界，不是已测的
  固定引脚偏移；现有 RUN 末态未导出初始 bridge 三元组，不能唯一分配各项误差。
- 下一 gate：先对 RUN 固定映射进行有界收敛与采样窗口收紧，再独立缩短首次
  enable 区间；不改变不可撤回前缀，不把 DCO 更新当成硬件共钟变化。每个实现切片
  依次执行 host、Release/资源、同源码四板 quick P3、原生/波形专项。独立路径确认
  不阻塞此项修复；三从持续收敛、单圈时延、物理锁相与一致发布仍未完成。

### VDC-PROGRESS-20260918-017：从板 MATCH 接入有界时钟映射并完成四板专项

- TODO task ID：`VDC-FAST-003`、`VDC-OUTPUT-001` IN PROGRESS。本条数字是本轮调试
  快照，非产品事实源；证据根为 `out/HardwareAcceptance/20260918/`，未改 OTA、PIO
  程序或其他设备的 `tdma_flight_engine.c` 工作区改动。
- 代码切片将 follower MATCH 从无状态单次投影切换为 `vdc_dpll_manager_project_mapped_feedback_event`。
  从板使用独立 Core1 `vdc_clock_mapping_cache_t`；只有不同事件完成模型、ring、observer、
  RX、path 和残差复验后才提交 `mapping.next`。重复事件、投影/算术/绑定拒绝和竞争样本
  不学习缓存；STOP、退休、禁用、请求代际切换、矛盾或 epoch 耗尽清空或退休当前代际。
  保留原绝对 DCO 投影准入、半开上界、token/horizon、回绕和溢出约束。
- Host 回归：`test_vdc_priority_match.py` 285 项通过（含 21 项真实 production mapping
  集成）；FOLLOW/FOLLOW_CONFIG 127 项通过；clock mapping、SCPI 邻接 43 项通过。
  初轮 278/4 的失败来自 fake bridge 未模拟 TIMER0 整微秒，夹具修正为微秒量化后恢复，
  不是产品失败或静默放宽边界。文档门禁 38 项通过，docs_check 149 文件无 FAIL。
- 独立资源审查记录 MATCH 工作区 1760 B，较前一审查快照增加 328 B；映射投影约 288 B、
  cache 64 B，选定嵌套调用链局部栈小计 728 B，均不能替代完整运行栈高水位或 WCET 证明。
  mapped projector 位于 XIP，未新增 PIO/SM/DMA/GPIO；完整 TDMA 超预算仍保留。
- 四板 quick P3 `p3-match-mapping-r1/` 结果为 PASS_WITH_WARNINGS（INFO 25、WARN 18、
  ERROR/FATAL 0），当前凭证绑定源码指纹。示波器/原生专项 `dpll-match-mapping-scope-r1/`
  通过，NO1 CH1 rising SINGLE 触发且运行期间查询为 0；三从事件 raw enable 宽均 72 ns。
  有限 1.9–2.1 s 波形窗口的最近边沿相对 NO1 中位约 NO2 +0.620 us、NO3 +1.240 us、
  NO4 +1.780 us，拟合斜率约 −0.15/−1.57/+0.35 ppm（均为有限窗口快照）；三从仍有
  0.50–2.00 us 的可见相位偏差，不能宣称 100 ns 锁相。原生 tail 样本数和模型变更随板
  不同，不能把单个短尾区间外推为全运行上界。
- 下一 gate：保留本轮映射收窄及拒绝不污染证据，继续分离 RUN 输出 bridge/enable、实际
  delay 和频差残差；在新的输出切片完成 host、Release/资源、四板 P3 和有限示波器复测，
  再决定是否需要进一步压缩 MATCH 工位或调整预算。

### VDC-PROGRESS-20260918-016：事件计时锚缩至 72 ns，保留映射与输出误差主线

- TODO task ID：`VDC-FAST-003`、`VDC-OUTPUT-001` IN PROGRESS。本条数字均为调试
  快照，非产品事实源。证据根 `out/HardwareAcceptance/20260918/`，未改 OTA、
  PIO 程序、phase 策略或 delay，保留其他设备的 `tdma_flight_engine.c` 改动。
- 先量化剩余误差：`dpll-phase-residual-audit-r1/phase-residual-audit.json`
  复算旧 r2 原生记录，phase 对跨零区间保持，否则只把最近端点推到零；已记录的
  66 条相位动作符合代码。它没有额外相位 deadband，频率 deadband 改零也不能
  改变这批频率决定。本地 raw enable 宽达微秒，叠加 TIMER0 量化和投影桥接后，
  区间包含零并不证明实际相位已接近零；中点也不能冒充已测真实误差。
- 晚窗口基线 `dpll-late-scope-r1/`：保持 40000/48000/32000 us 时间参数、
  每块十六边沿、静默运行，四板均 CANCELLED，无 STARVED。1.9–2.1 s 窗口
  四通道各 200 上升沿、20 ns 采样网格；NO1 周期误差约 -17.12～+19.53 ns，
  三从最近 NO1 相位中位约 +1.84/+3.60/+1.48 us，NO3 仍含相位动作。
  请求 5 s 偏移的两次示波器设置被读回检查拒绝，见 `dpll-late-scope-check-r1/r2/`，
  当时未 START；r3 验证实际支持的 20 ms/div、2 s 中心后才采集。
- 代码切片 `7b289126`：原 `try_read_ticks64` 前后各做时钟资格检查，使这些检查
  落入 PIO enable 的计时窗口。新增 TDMA 私有 `tdma_event_enable_anchor_capture`
  SRAM helper，将资格检查放在窗口外；窗口仅保留 H/L/H 原始读取、同一 SDK
  同步 enable、H/L/H 和 fences。不屏蔽 IRQ、不重试、不裁窄观测区间；组内
  回绕、逆序、时钟失败拒绝发布 anchor，仍保留原来的一次 enable 行为。
  dirty-start、epoch、故障与退休检查不变，不将首帧证明新增为 DPLL 前提。
- host/Release/审核：`enable-anchor-host-r3.txt` 最终 28 项通过，包含 device
  分支 MMIO 顺序、失败原子性、回绕及现有 observer 生命周期。首次扩展测试的
  两项禁用配置失败由收窄 include guard 修复；另一项 cut fixture 缺相邻 LIVE/
  PIO 接口，在 `cut-fixture-head-repro.txt` 证明旧 HEAD 同样失败后补齐夹具，
  保留真实 cut 算术并验证 ARM 退休/清空调用。`enable-anchor-release-r2.txt`
  Release 与 A/B/boot 链接通过；`enable-anchor-review-r1/review.json` 独立审核
  绑定最终源码和镜像，确认 144 B helper 位于 SRAM、局部栈 48 B，采样段没有
  函数调用或 IRQ 屏蔽，仅一次原子 SET 启用；不新增常驻数组，未证明全栈水位。
- 四板 P3：初次 `p3-enable-anchor-r1/` 因验收运行期间修正源码而被指纹检查
  判 FAIL，不使用其凭证。冻结最终源码后重跑 `p3-enable-anchor-r2/`，约 233 s，
  PASS_WITH_WARNINGS，INFO 25/WARN 18/ERROR 0/FATAL 0；严格质量仍未通过，
  DPLL 为 TDMA-only 范围外，专项证据另列。当前 receipt 与源码指纹绑定，旧
  build ID 不能替代 package/ELF/source 哈希。
- 专项 `dpll-enable-anchor-scope-r1/` passed=true：三从保留的 39/39/37 个
  MATCH 中 raw enable 区间均为 18 tick、72 ns；晚窗口基线分别为 335/335/336
  tick、约 1.34 us。后段残差区间中位由约 3454/3446/3458 ns 降为
  2036/1904/2166 ns。四板提交 713/724/734/744 块后主动取消，无 STARVED。
  1.9–2.1 s 窗口 NO1 周期约 -18.48～+19.40 ns；三从最近 NO1 相位中位约
  +0.10/+0.96/+1.36 us，但 NO2/NO4 含约 -775/-1280 ns 的周期缩短，不能将
  中位接近 100 ns 当作锁相通过。
- 复测 `dpll-enable-anchor-scope-r2/`：四板再提交 713/723/733/744 块，均
  CANCELLED、退休 TXSTALL 位零；三从保留的 47/47/49 个 MATCH 再次为 72 ns。
  原专项 passed=false：NO2 STOP 后读取 offset 7044 的 RAM 页超时，工具保留
  冻结记录未释放。`recovered-no2/recovery.json` 无重 ARM 地补读同 capture ID
  的 7768 B，全文件 CRC 通过、此前 56 页字节一致、前后状态相同，随后 RELEASE；
  不改写原失败。三从后段残差区间中位约 3052/2188/2943 ns，晚波形相位中位约
  -0.50/+1.16/+0.88 us，仍有可见相位动作和 session 间变化。
- 结论边界：两轮 `anchor-width-comparison.json` 保存逐板宽度、原件哈希与外部
  波形对比，r2 显式标记恢复的原生记录。72 ns 是本轮 epoch 的计时锚区间，
  不是 GPIO 准确度、全运行误差上界或共同 ordinal 对齐证明。原生记录仍是有限
  抽样/冻结前缀，波形只覆盖有限窗口；不同初始模型不能作严格物理单因素 A/B。
  完整 TDMA 相位累计最大耗时仍超预算，不因输入锚变窄而宣称实时验收通过。
  原生二进制、RAW 块及分级 P3 的独立复核见
  `dpll-enable-anchor-scope-r1/independent-review.json`。
- 下一 gate：事件 enable 的可避免软件宽度已明显降低，接着收窄 follower
  MATCH 的 TIMER0/TIMER1 映射区间，并核对 RUN 输出 bridge/enable 的独立误差。
  优先复用已有有界时钟映射与 owner 生命周期；先证明输入区间、再做实际波形
  delay/频差调整，不用固定 delay 隐去 session 间漂移，不把区间中点当真值。
  当前输出 anchor 仍约 1.15 us，本切片未修改它。最终四板 STOP、origin 许可
  撤销、RAM 参数恢复原值、示波器 STOP/EXT/NORM，见 `enable-anchor-final-state.json`。

### VDC-PROGRESS-20260918-015：精确递推重建后缀，两轮四路持续至 STOP

- TODO task ID：`VDC-FAST-003`、`VDC-OUTPUT-001` IN PROGRESS。以下数字为调试
  快照，非产品事实源；证据根为 `out/HardwareAcceptance/20260918/`。只修改 VDC
  私有规划与相关 host 测试，不改 OTA，保留其他设备的 TDMA 工作区改动。
- 断流复现：r6 原生 schema 8 已记录 invalidation/outcome 的序号和硬件时间，
  NO3/NO4 最后模型失效距退休约 7.70/8.41 ms；旧 DMA_NOT_READY 结果早于失效，
  不能推定之后 DMA 一直不可用。生产 client 的计时回归在块尾前 6.75 ms 更新模型，
  穿插两次 cached-only 调度，旧四边沿切片无法及时提交；修前两失败、两通过，
  见 `late-model-before.txt`。这证明一条可导致断流的路径，不覆盖全部历史故障。
- 修复：`vdc_output_edge_cursor.h` 以一次标量精确反解初始化商余数，连续同模型网格
  的后继边沿直接递推；RUN 在单次有界调用内重建固定后缀，PRESTART 和不符合连续
  网格条件的情况保留标量切片。已准入前缀不改写；模型失效、STOP/session 与后端
  最终 guard 保留。规划后重查一次此前 busy 的 DMA，避免继续使用过时观察。
  cached-only 不执行新规划。`edge-cursor-host-r3.txt` 记录 259 项通过，包括独立
  Fraction oracle、极值、失效重建、动态 DMA ready 和 prefix 不变。
  `edge-cursor-review-r1.json` 独立源码复核 ACCEPT_WITH_NOTE，无阻断发现；代码与
  当前源码 P3 receipt 已提交为 `da59c19f`。
- Release 与资源：`edge-cursor-release-r1.txt` 构建及双 slot/boot 链接检查通过。
  `edge-cursor-resource-r1/audit-summary.json` 与 `release-audit.json` 绑定 ELF、
  反汇编和源码：cursor 内联在 SRAM，标量 seed/fallback 与 Domain 投影仍有 XIP
  调用；入口栈从历史 uniform 的 728 B 增至 880 B，已核对算术链 1132 B，Core1
  保留 3072 B，但该链不包含全部调用者和 IRQ/FPU，不冒充完整栈水位。常见成功
  路径旧四标量加 raw、新十六递推加 raw 均有 44 次已核对的 64 位除法，不能仅凭
  逆映射次数下降声称 WCET 下降。未增加常驻数组或动态分配。
- 当前源码四板 quick P3：`p3-edge-cursor-r1/` PASS_WITH_WARNINGS，INFO 25、
  WARN 18、ERROR/FATAL 零，总耗时约 188 s；复用已测线序，DPLL 不计入本次 P3。
  build ID 沿用构建目录值 `20260917215027`，实际新镜像以 receipt 源码指纹和
  ELF/package 哈希区分，不能只凭 build ID 等同旧固件。严格质量失败原件保留。
- 同一候选时间参数 40000/48000/32000 us、固定十六边沿，静默采集后全部 STOP：
  `dpll-edge-cursor-scope-r1/` 专项 passed=true，四板分别提交 715/725/736/748 块，
  采用 20/23/39/47 次模型变化；均 CANCELLED，未 STARVED，最小补给余量约
  5.80/5.59/5.21/3.30 ms。`dpll-edge-cursor-scope-r2/` 四板再提交
  712/723/732/742 块，均 CANCELLED，最小余量约 6.49/3.78/5.72/5.64 ms。
  但 r2 专项 passed=false：NO4 STOP 后 `FEEDback:MODel?` 超时；原生记录和波形
  完整，后续独立 STOP 补读成功，见 `edge-cursor-final-state.json`，不追改原失败。
- 实时边界：`stopped-comparison.json` 分别保存两轮完整调度和退休记录。
  r1 TDMA phase 最大耗时约 1226/1494/1426/1465 us，仍超过配置预算；原 r6 为
  1339/1615/1578/1400 us，输入/初态不同，且调度最大值可能累计，不能作严格单因素
  比较或隔离规划器 WCET。`fast_wall_*` 只测 cached-only，不覆盖普通批规划。
  四路未以 STARVED 终止且 STOP 时仍有库存，支持有限运行连续性改善，不替代
  全程逐边沿观测、严格实时预算或无限期运行证明。
- 外部证据：两轮 `capture/scope-analysis/` 保存四十个 RAW 块校验及四通道图。
  独立复核见 `dpll-edge-cursor-scope-r1/independent-review.json` 及其中 r2 补充：
  两轮八十个 RAW 块、八份原生二进制核验通过，仅接受有限观测结论，不提升为
  锁相或实时预算通过。r2 窗口末尾三从相对最近 NO1 边沿约 +1.46/+4.10/+2.22 us，
  仍是周期取模后的偏差，尚未达到 100 ns。
  r1 NO1 也有约 -2.36 us 的短周期，三从发生相位步阶并在窗口后段靠近 NO1。
  三从首次原生相位修正包含整毫秒量级的 ordinal 跨越；最近 NO1 配对按周期取模，
  不能把该图当共同 ordinal 对齐或绝对误差证明，含步阶的拟合也不能当频差。
  原分析器因 Windows 时间量化使 arm 返回与首 START 同时间戳而拒绝；新离线
  分析保留早于 START 的 SINGLE 指令与串行 acquire 顺序证明，仅允许该边界相等，
  不修改 RAW，原分析失败保留。仍不宣布粗锁定或 100 ns 锁相。
- 收尾与下一 gate：四板已核验 STOP，origin 临时许可已撤销，RAM 参数恢复
  12000/16000/6000 us，示波器 STOP/EXT/NORM；候选未写 Flash。接下来使用
  同次原生 model/ordinal 和更晚稳定窗口波形，区分 NO1 模型步阶、从板相位采用及
  剩余 delay/频差，再收敛微秒级残差；同时保留完整 TDMA 超预算问题。不要重测
  已确认线序。为控制 C14 日志大小，本轮逐字轮转最旧连续十条至归档 02，正文
  比对证据为 `vdc-rotation-evidence.json` 与 `vdc-rotation-original-body.txt`。

### VDC-PROGRESS-20260918-014：修复标量假超时，外部波形确认三从相位靠近

- TODO task ID：`VDC-FAST-003`、`VDC-OUTPUT-001` IN PROGRESS。以下数字为调试
  快照，非产品事实源。证据根为 `out/HardwareAcceptance/20260918/`；未修改固件、
  PIO 或 OTA 实现，保留其他设备的 `tdma_flight_engine.c` 工作区改动。
- 工具切片（提交 `9864fa5e`）：`SESSion?=1` 被共用串口读取器当作 ACK 丢弃，导致配置前假超时。
  `scpi_serial.py` 登记全长/全短 SESSION 标量头，并使已知 U32 查询保留合法 `1`；
  普通非标量查询仍过滤 ACK。相关 pytest 85 项通过，独立只读复核无阻断发现；
  Release 链接检查通过，`p3-scpi-one-r1/` 当前源码四板 quick P3 为
  PASS_WITH_WARNINGS（ERROR/FATAL 均零），复用已测线序，DPLL 不计入本次 P3。
  固件 build 保持 `20260917215027`。`scalar-one-hil-r4.json` 在 STOP 下实测
  LOAD:MASK 的同一 U32 读取分支返回 `1` 并恢复原值；SESSION=1 的直接重设负例
  保留于 `session-one-hil-r1.json` 至 `session-one-hil-r3.json`，旧 session 受
  `vdc_model_feedback.inc` 单调水位约束拒绝，不冒充 SESSION=1 实板通过。
- 采样工具负例：`dpll-resume-scope-r1/r2/` 分别保留原错误队列和标量假超时；
  r3/r4 停在示波器偏移读回不符，尚未 START。后续仅操作示波器验证设置，
  使用正常 NO1 上升沿触发、20 ms/div 和延后中心；不再用被冻结波形上的偏移
  编辑推定下一次设置成功。RAW 导出须先统一各通道传输范围，再读取 PRE；
  PRE 的点数随分段变化，不是通道采样时间轴变化。
- `dpll-resume-scope-r5/`：静默运行后，三从原生记录及末态模型通过原有限专项；
  实际频率更新均 10 次，相位更新 41/35/46 次。规划/提交/低水位参数为
  24000/32000/16000 us，NO1 输出 6 块后 STARVED，三从持续至主动取消。
  原生退休复算确认 NO1 输入耗尽，其 service 最大间隔约 2.425 ms，不能仅归咎
  一次超长调度停顿。首次波形导出因 PRE 点数误判中止；`capture/scope-recovered/`
  在未重触发条件下补齐四通道，CH1 哈希与首次导出完全一致。实际 300–500 ms
  窗口内 NO1 无边沿、三从各 201 个，不能据此计算相对 NO1 的锁相。
- `dpll-resume-scope-r6/`：临时参数 40000/48000/32000 us、每块仍 16 边沿，
  四通道 RAW 完整导出，采样网格 20 ns。300–500 ms 窗口每通道均 200 个上升沿；
  NO1 相邻周期相对标称的误差为约 -17.35～+19.60 ns。三从相对最近 NO1 边沿的
  偏差由约 +59.40/+73.30/+64.12 us 降至 +4.00/+6.66/+5.40 us，存在真实物理
  相位步阶，不能再把三从视作仅收到数据但输出完全未动。最近边沿配对按周期取模，
  不等同共同 ordinal 证明；含步阶的整体拟合不当作频差，也不宣布粗锁定或 100 ns。
  图与复算为 `capture/scope-analysis/relative-edges.svg`、`review.json`。
  独立复核为 `dpll-resume-scope-r6/independent-review/review.json`：四十个 RAW 块
  校验及物理步阶复算一致；原生首次相位修正幅度与物理步阶相符，但尚无严格的
  trigger/raw 时间桥接，不能逐事件宣称因果已闭合。
- 连续性仍未通过：r6 NO1/NO2 主动取消，NO3/NO4 分别提交 261/518 块后 STARVED；
  旧采集器的 `passed=true` 只覆盖有限原生/输出专项，不能提升为全程连续。
  两轮初始 DCO 状态不同，不能当严格单因素 A/B。均已四板 STOP、撤销 origin
  许可，RAM 时间参数恢复原值；未将候选写入 Flash。
  最终核验见 `dpll-resume-final-state.json`，示波器恢复 STOP/EXT/NORM。
- 下一 gate：从已确认的物理相位动作继续，分解预规划失效、DMA 提交窗口与不可改写
  前缀之间的补给缺口，修复后走独立切片 P3。延续原生 phase/model 与输出事件对照，
  先保证四路持续，再收敛剩余微秒级边沿差；不重做已确认线序或首帧校准。

### VDC-PROGRESS-20260918-013：恢复自主 origin 与新代 FOLLOW 绑定，三从实际 DCO 更新

- TODO task ID：`VDC-FAST-003`、`VDC-OUTPUT-001` IN PROGRESS。以下为有限调试
  快照，非产品事实源。原始证据为
  `out/HardwareAcceptance/20260918/priority-resume-r1/raw.json` 与
  `priority-resume-r2/raw.json`、`models.json`、`review.json`；后者目录与前者同级。
  四板读回 build 均为 `20260917215027`。本轮没有修改或刷写产品固件；保留工作区
  `tdma_flight_engine.c` 改动，专项不替代当前源码 P3 凭证。
- 复核纠正：普通 ARM/START 不自动获得自主 origin 许可，当前代码已有显式
  `CALibration:ORIGin:TRIAL` 的 owner 交接调用，不能引用历史“无调用者”记录认定
  当前实现缺失。早期短许可证到期后的 STOP 也不能解释为物理释放失败；原始
  HANDOFF 为 DONE、RELEASE 为 RELEASED，未报告物理拒绝。
- 配置生命周期：STOP 后先核对 config/applied ACK，已有 feedback session 保持；
  NO1 SYNC 与三从 MATCH 使用新的共同 generation，然后重新提交三从 FOLLOW 1，
  使本地控制请求锁存本轮 MATCH generation。全部 ARM 后从尾板到主板 START，
  再申请有限 origin 许可。运行期间不查询，有限窗口结束后四板 STOP、撤销许可，
  只在 STOP 后读取诊断。运行时读取 STOP-only 查询导致的“操作不可用”不作为
  硬件失联证据。
- r1 使用 generation 2，NO1 编码 7,326 次，三从各 typed accept 12,345 次，
  匹配分别 3,259/3,143/2,993 次；FOLLOW 仍锁存 generation 1，状态为 BINDING
  退休，未采用新代数据。该负例说明 `FOLLow?=1` 只表示请求模式，不能证明
  当前 generation 的控制有效。
- r2 使用 generation 3 并重新提交 FOLLOW，静默窗口 12 秒。NO1 编码 7,349 次；
  NO2/NO3/NO4 typed accept 为 12,290/12,273/12,295，typed reject 均为零，
  成功匹配为 2,851/2,872/2,659，实际 DCO 更新为 11/11/12 次。末态
  DCO sequence 为 12/12/13，频率为 +6,803/+7,300/+9,182 ppb，均与独立
  `FEEDback:MODel?` 读回一致。物理接收缺口计数仍非零，不宣称逐圈必达。
- `review.json` 从原始整数复算末次频差区间：NO2 为 [-1083,33] ppb，NO3 为
  [-1101,13] ppb，两者跨零而保持；NO4 为 [-1199,-81] ppb，最后实际增加
  +40 ppb。模型一致与区间计算已核对，但这些是末态诊断，不能替代逐次原生
  时间序列、相位收敛或示波器证据。最终四板 STOP/config ACK，许可证显式撤销。
- 下一 gate：沿已恢复的启动顺序，复用 typed 原生记录与 schema8 RUN 输出工具，
  绑定同次事件/模型和四通道波形，继续验证长时间轴补给、相位修正及相对 NO1
  的边沿差。无需重做已测线序或修改 origin 启动固件；100 ns、ACK 全链和
  完整实时预算仍开放。

### VDC-PROGRESS-20260918-012：ARM 等待策略复核仍未启动 adapter

- TODO task ID：`VDC-FAST-003`、`VDC-OUTPUT-001` IN PROGRESS。针对 011 中的 ARM
  超时，第二轮使用持久串口会话、较长超时和分段训练重试四板启动；本次 NO4 在
  `ARM` 状态等待阶段超时，前一轮为 NO2，故障节点随轮次变化。两轮最后状态都显示
  `ring_enabled=1`、配置/调度身份已写入，但 `ring_adapter_started=0`、UP/DOWN
  未运行；随后四板均显式 STOP。该结果排除了单纯短会话和等待时长不足，当前阻塞
  聚焦 TDMA adapter 启动前置条件或配置应用路径。
- 本轮没有继续修改固件，也没有改变 DPLL/VDC 门限；示波器仍保持 STOP 后的低电平
  证据。只有 adapter 在四板同时进入运行态并形成稳定序列，才有必要重复外部触发
  和 NO1--NO4 internal 采样。
- 下一 gate：对比 P3 通过轮与手工 `tdma_start_ring.py` 的 topology、OPMode、
  calibration generation、schedule CRC 和 adapter error 字段，找出启动前置差异；
  修复后必须重新执行当前源码四板 quick P3，再恢复 DPLL 运输/相位验证。

### VDC-PROGRESS-20260918-011：四板运行态与外部示波器复核

- TODO task ID：`VDC-FAST-003`、`VDC-OUTPUT-001` IN PROGRESS。四板 P3 收尾后，使用
  `tools/scope_dpll_capture/scope_dpll_capture.py` 对
  `USB0::0x1AB1::0x0610::HDO4A244301137::INSTR` 做只读单次采样，CH1--CH4 对应
  NO1--NO4、CH1 为触发通道。证据根为
  `out/HardwareAcceptance/20260918/scope-dpll-validity-fix/`；仪器识别成功，但触发
  状态为 `WAIT`，四路均无上升沿且幅度接近零。采样发生在板端 STOP 后，不能用于
  DPLL 精度或锁相判定。
- 为获取运行态波形，按四板物理顺序调用
  `tools/tdma_ring_monitor/tdma_start_ring.py`（当前 Release build，短训练窗口）。
  NO2（`FB276192BEF9CCE1`）在 ARM 状态查询阶段超时；随后只读状态显示各板序列计数
  有变化，但 UP/DOWN 运行标志未同时成立。四板已发送显式 `SYSTem:TDMA:RING:STOP`，
  未写入 Flash。该失败与 `out/HardwareAcceptance/20260918/p3-055020/` 中 TDMA
  运行质量告警同向，当前优先级回到 TDMA 启动和持续运行闭环。
- 下一 gate：先在四板相同源码上闭合 ARM/START、UP/DOWN、FIFO 退休和稳定序列，形成
  可持续运行窗口后再重复外部触发采样；只有 NO1--NO4 同序边沿和内部接收/采用事件
  同时存在，才进入 DPLL 相位/频率锁定判定。本条不改变 `valid_from_raw` 修复或任何
  DPLL 门限。

### VDC-PROGRESS-20260918-010：同模型有效起点变化的基线失效修复

- TODO task ID：`VDC-FAST-003`、`VDC-OUTPUT-001` IN PROGRESS。修复
  `components/vdc_dpll_manager/src/vdc_priority_follow.inc`：当已保留基线的模型
  身份未改变、但发布的 `valid_from_raw` 被改写时，Core1 现在先废弃旧基线并等待
  新基线；已确认的 phase successor 仍按独立 `rate_epoch` 路径保留，不受该检查误伤。
  这样可避免同一模型 token 下把有效期前后的两个事件拼成一次频率差分。
- 主机验证：`test_vdc_priority_delta.py`、`test_vdc_priority_phase.py`、
  `test_vdc_priority_follow.py`、`test_vdc_priority_follow_config.py` 共 175 项通过；
  私有工作区大小与公共快照 ABI 保持不变。测试仅证明边界和所有权语义，不证明板端
  连续输出或物理锁相。
- Release 资源验证：构建目录
  `out/build/dpll-validity-fix/` 完成 `pico2-release` 编译、双应用镜像和链接检查。
  当前源码四板 quick P3 使用 `--tdma-only` 完成，证据根为
  `out/HardwareAcceptance/20260918/p3-055020/`，结果为
  `PASS_WITH_WARNINGS`（30 INFO、24 WARN、无 ERROR/FATAL）；默认五板配置因 NO5
  未连接而停止，四板范围已显式固定，DPLL 外部观测未纳入本轮判定。
- 提交：`80435e65 fix(vdc): retire baseline on same-model validity change`，P3 凭证
  已绑定提交源码指纹。下一 gate 仍是 `VDC-FAST-003`：在不改变特等席运输边界的前提
  下，复采 NO1--NO4 的实际接收/采用事件，再统一到同一时间轴核对补给连续性、DCO
  生效和 internal 残差；不能用本次模型边界修复替代连续输出或 100 ns 锁相证据。

### VDC-PROGRESS-20260918-009：长时间轴补给窗口 A/B 复采

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003` IN PROGRESS。以下均为四板有限
  20 秒调试快照，数字不是 WCET 或产品契约；证据根为
  `out/HardwareAcceptance/20260918/dpll-run-timeline-r1/timing-longaxis-r7/`。
- 修正采集器后，候选 `OUTPut:TIMing=(24000,32000,16000)` 在四板 STOP 状态下
  锁存成功，1 ms 输出对应 16 边沿块；schema8 RUN/native 采集通过，采集期间查询
  为零，最终四板均由显式 STOP 以 `CANCELLED` 收尾。配置试验未写入 Flash，外层
  harness 已将四板 RAM 值恢复为原请求。
- 与同固件的默认 `(12000,16000,6000)` 复采对照，NO1–NO4 的有限运行由全部
  `STARVED` 变为全部 `CANCELLED`。末次服务对应的条件 runway 由约
  `-0.47/-0.81/-1.32/-2.13 ms` 变为 `+23.59/+20.35/+20.11/+18.96 ms`；
  服务最大间隔仍约 `3.90/4.70/4.88/4.25 ms`。这说明扩大规划/承诺时间轴和
  16 边沿块在本轮显著提高了补给余量，不能外推为长期连续性或锁相完成。
- NO4 曾出现一次 `dma_not_ready`，但最终缓存计划仍完成提交；该字段和聚合计数
  没有逐次时间戳，仍需后续相关 trace 才能区分 DMA 退休、模型失效和服务调度的
  因果关系。示波器未用于本轮精度判定，DPLL/100 ns 结论保持未宣称。
- 下一 gate：在保持候选窗口可配置、STOP/代际取消和前缀不可改写的前提下，重复
  长时间轴 A/B，并将服务间隔、提交间隔、FIFO 退休和物理边沿统一到同一时间轴；
  只有连续输出证据闭合后，才进入 DPLL 锁相及 VDC 发布判定。

### VDC-PROGRESS-20260918-008：schema8 RUN 输出采样与断流因果复核

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003` IN PROGRESS。schema8 适配的 RUN
  输出工具只写入 `out/`，未进入产品源码；本条数据和数值均为一次调试快照，非
  WCET、连续性或精度契约。
- 新 build `20260917204319` 的四板有限 RUN 采样已完成，证据根为
  `out/HardwareAcceptance/20260918/dpll-run-retry-r1/capture-r1/`，采集期间查询数
  为零，STOP 后统一导出 native 与示波器数据。四板 RUN schema 均为 8；NO1 提交
  1140 个块后由采集清理得到 `CANCELLED`，NO2/NO3/NO4 分别提交 92/57/107 个块，
  最终均为 `STARVED`。四板 `last_submit_failure` 均为 `NONE`，本轮没有观察到新的
  guard 或 NOT_READY 提交拒绝，因此不能宣称后缀重试已经在实板上被触发。
- STOP 原生差分分析位于
  `out/HardwareAcceptance/20260918/dpll-run-retry-r1/refill-retirement-analysis.json`。
  NO2–NO4 仍同时出现 TX FIFO 空、PIO stall、DMA 剩余为零；最后服务到末沿界的
  负 runway 约为 0.23/0.83/1.28 ms，服务最大间隔约为 3.59/4.74/3.15 ms（快照）。
  这继续指向 Core1 服务空窗导致的末端耗尽；没有证据表明 `NOT_READY` 缓存保留能
  消除该问题。NO1 的末段由显式 STOP 取消，不能与从板 STARVED 混为一谈。
- scope 采样成功导出，当前专项只确认数据留存和退休寄存器因果；没有进行四路同序
  100 ns 判定，也没有运行 DPLL 锁相专项。下一 gate 是在保留 `GUARD` 清除、
  `NOT_READY` 有界复用的规则下，继续优化 Core1 服务调度/提交提前量并进行受控
  A/B；不得以 `PASS_WITH_WARNINGS` 或后缀缓存命中数替代连续输出证据。

### VDC-PROGRESS-20260918-007：提交拒绝分类与私有后缀重试边界

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003` IN PROGRESS。本条记录当前源码
  的 host 验证；尚未形成新的板端或 Release 证据，数字均为快照，非产品契约。
- `SYNC_IO_RUN_OUTPUT_SUBMIT_FAILURE_*` 将后端提交拒绝拆为参数、原始时钟读取、
  guard、编码、时钟、启动、deadline 和 DMA 等分支；`VDC_RUN_OUTPUT_SCHEMA` 更新
  后，STOP-only `RUN?` 额外导出 `last_submit_failure`。该字段只说明最近一次拒绝
  分支，不能把一次拒绝改写成连续输出或锁相通过。
- 私有未提交后缀的处理边界已固定：只有 `NOT_READY` 保留完整后缀，下一次 Core1
  服务必须重新通过 generation、代际、前驱尾点、时钟和 DMA 准入后才能重试；已提交
  前缀不重写。`GUARD` 表示硬件剩余运行道已进入最小保护窗口，和 RAW、CLOCK、
  ENCODING、START、DEADLINE、DMA、ARGUMENT 一样清除后缀，不能用重试掩盖断流。
- `tests/python/test_vdc_run_output.py` 的生产客户端与 parser 回归共 149 项通过；
  覆盖 guard 拒绝后清除、NOT_READY 拒绝后复用同一缓存、模型/前驱变化失效及取消
  生命周期。该 host 结果不证明板端重试安全，也不放宽 `STARVED` 判据。
- Release build `20260917204319` 的 A/B/Boot 双槽链接检查通过；同源码四板
  `--tdma-only --diagnostic-continue` quick P3 已完成，证据根为
  `out/HardwareAcceptance/20260918/dpll-run-retry-r1/p3/`，结果为
  `PASS_WITH_WARNINGS`（23 INFO、22 WARN、0 ERROR/FATAL）。P0 复用拓扑的身份和
  build 读回通过；T1/T3 训练质量与 TDMA 严格闭环失败原件保留，不能视为连续输出
  或锁相通过。该 P3 切片没有执行 RUN 输出专项，因此不把它解释为 schema8 实板重试
  已验证。
- 工作区仍保留另一设备对 `components/tdma/src/tdma_flight_engine.c` 的外部修改，
  未修改、未暂存。当前 schema8 代码、测试和新 receipt 尚未提交；下一 gate 是用同
  源码进行 schema8 RUN 输出采样，重点核对 `last_submit_failure`、后缀复用/清除与
  FIFO 退休时序。若实板仍在末沿后才服务，继续调整调度/准入时序，不能仅依赖缓存
  保留结案。

