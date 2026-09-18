# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`
Last updated: 2026-09-18

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

### VDC-PROGRESS-20260918-035：主 DCO 时间坐标重基已完成四板验证

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。复核 r17/r18 后确认，
  证据的逻辑 TDMA/TIMER1 坐标曾直接进入主 DCO `base_local_tick64`，而 RUN 使用
  TIMER0 本地时间；这可在模型块切换时把 ppb 速率变化放大为毫秒级边界跳变。r17
  的五个异常周期与该坐标差的速率步变定量吻合；r20 的延迟采样窗口没有后续模型
  更新，不能作为反证。上述数值均为验收快照，非精度契约。
- 最小修复：`vdc_dpll_manager` 在 Core1 交接处记录 `time_us_64()*1000` 服务边界，
  通过 `vdc_tdma_evidence_preparation_t.local_apply_time_ns` 传入 Domain；逻辑
  `observed_time_ns` 继续只参与 PI/FLL。主 DCO 首次使用该本地锚点，后续先在该时刻
  计算旧模型输出再重基新速率；本地时间倒退时拒绝该次提交。未把 TIMER1 RX provenance
  直接当作 TIMER0 坐标。
- 验证：新增 host 跨坐标回归，`test_vdc_dpll_replay.py` 85 passed；相关映射、跟随、
  输出反解和模型测试 166 passed；Release `pico2-release` 编译、flash-link 约束和
  资源生成通过。四板当前源码 quick P3 为 `PASS_WITH_WARNINGS`，无 ERROR/FATAL，证据
  在 `out/HardwareAcceptance/20260918/p3-coordinate-r1/`。
- 物理复测 `out/HardwareAcceptance/20260918/dpll-coordinate-r22/`：零 delay、
  `24000,32000,16000` 补给配置、CH1 正边沿触发、20 ns 采样；NO1–NO4 采样审计未出现
  `>1 us` 周期异常。NO1 的 `local_model_raw` 已显示 `base_local_tick64` 与 RUN
  时间轴同为百毫秒量级，并不再是早期几十毫秒的逻辑锚点。该轮证明异常被消除，尚未
  证明跨板 100 ns 锁相；下一 gate 是同一会话下完整有效边沿的相位/斜率统计。

### VDC-PROGRESS-20260918-034：RUN 软件退休与四路物理边沿已同时取得

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本轮只复用既有
  DPLL/VDC、PIO/DMA 和示波器路径，没有修改 DPLL 参数、Flash、OTA、path-delay 或
  output-delay；所有结果均为证据快照，非产品事实源。
- 无示波器四板复测 `dpll-run-isolation-r15` 通过：四板分别退休 712/723/734/745
  个 RUN block，source retirement、model change 和提交计数均为正，停止后四板
  `PIO=0`、`DMA=0`，无提交失败。原始软件报告为
  `out/HardwareAcceptance/20260918/dpll-run-isolation-r15/input-probe.json`。
- `dpll-run-isolation-r17` 以 CH1 正边沿触发、20 ns 网格和 10 M 点 RAW 深存储，四
  通道均完成完整读回；离线门限边沿计数快照为 CH1/CH2/CH3/CH4：96/174/200/200，
  CH2–CH4 的周期中位为 1 ms。以 CH1 触发附近的首个同序边沿计算，NO2/NO3/NO4
  相对偏移约为 +18.88/+17.88/+17.26 µs；该计算原件为
  `out/HardwareAcceptance/20260918/dpll-run-isolation-r17/capture/scope/run-edge-analysis.json`。
- 采样本身完成，但四板包装器因 NO2 的 `local_model` 末态查询得到 `<timeout>`
  而返回失败；这不撤销已保存的四路 RAW 数据，也不能把本轮当作完整 DPLL 锁相
  验收。包装器报告和子进程日志在 `out/HardwareAcceptance/20260918/dpll-run-isolation-r17/`。
- 结论：RUN 从环路模型到实际 GPIO16..19 的 PIO/DMA 路径已被物理边沿证明可达，
  但启动相位仍在十微秒量级，未达到粗锁定；下一 gate 是让物理采样不依赖末态
  模型查询（保留超时原件），随后仅改变每板 output-delay 做同会话 A/B，并以全部
  有效同序边沿的最大绝对差重新判级。不得用中位数或包装器 PASS 替代锁相判据。

### VDC-PROGRESS-20260918-035：大幅负 output-delay 跨网格，A/B 已回退

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本轮仅在 STOP
  态通过既有 SCPI 设置 output-delay，未修改 path-delay、DPLL PI、Flash 或 OTA；
  所有相位数值均为证据快照，非产品事实源。
- 在 NO2/NO3/NO4 分别设置 `-18880/-17880/-17260 ns`、NO1 保持零值后，四板 RUN
  包装器和示波器采样均完成；原始目录为
  `out/HardwareAcceptance/20260918/dpll-run-isolation-r18/`。四路仍为连续约 1 ms
  周期，证明 signed output-delay 的 STOP 配置和物理输出链有效。
- 以 CH1 首个边沿为参考，负延迟使三从最近同周期边沿跳到 NO1 之前约
  `173/189/186 µs`（NO2/NO3/NO4）；这是公共 1 ms 网格选点跨界造成的 ordinal 跳变，
  不是可用于锁相的补偿结果。大幅负延迟方案已撤销，四板最终 output-delay 均读回
  `0`，RUN/TDMA 均 STOP，错误队列均为 `No error`。
- 结论：output-delay SCPI/持久化路径可用，但补偿必须在不改变公共 ordinal 的小窗口
  内进行。下一 gate 是保留零值基线，先为启动 ordinal 和跨板同序边沿建立稳定对应，
  再用单板小步长（不跨网格）复测；不得用大负延迟直接抵消启动时差。

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

### VDC-PROGRESS-20260918-006：服务序号与退休点相关采样

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003` IN PROGRESS。本条仅记录本轮
  schema7 诊断快照，数值不是产品契约；原始证据根为
  `out/HardwareAcceptance/20260918/dpll-run-uniform-r3/capture-r2/`。
- 为每次有效 Core1 服务增加 `service_sequence`，并把最后一次客户端 outcome、
  缓存失效分别绑定到服务序号和 backend service tick；STOP 导出字段总数由 schema6
  的旧布局扩展为 schema7。主机 RUN 解析回归与时间轴回归共 170 项通过。
- 四板采样期间查询数为零，最终均为 `reason=STARVED`，且退休寄存器同时显示 TX FIFO
  空、PIO TXSTALL 置位、DMA remaining 为零；因此本轮确认的是输入耗尽事实，不把
  `PASS_WITH_WARNINGS` 或有限采样当作连续输出通过。四板最后服务到退休约 0.43 µs，
  末次服务已落在最后下降沿完整时间界之后约 33 µs 至 1.30 ms。
- NO1 的最后客户端结果为 `submit_rejected`，发生在服务序号 756；前一次缓存失效为
  序号 754，之后没有形成第三个十边沿块。NO2–NO4 的最后 outcome 分别落在序号
  1232/1435/1179，缓存失效序号为 1239/1421/1185；这些序号只建立先后关系，不能
  单独证明 DMA 不可准入、模型重建或 deadline 哪一个是根因。
- 本轮最小结论是“短服务体量已足够小，但服务空窗和提交竞态仍可把补给推迟到末沿之后”。
  下一切片应在保持 schema7 相关字段的前提下，细分 `submit_rejected` 的内部拒绝原因，
  或先验证提交失败保留私有后缀的重试策略；不得通过放宽 STARVED 判据宣称连续性。

### VDC-PROGRESS-20260918-005：固定脉宽单字 FIFO 后端（待四板专项）

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003` IN PROGRESS。以下为实现与离线
  验证快照，非产品事实源；新证据根为
  `out/HardwareAcceptance/20260918/dpll-run-uniform-r1/`。
- RUN 在 STOP/PREPARE 计算固定高段 tick，并调用
  `sync_io_run_output_prepare_uniform`。PIO 状态机初始化时把高段计数置入 ISR，
  每个脉冲只从 TX FIFO 取低段计数；原双字可变脉宽 API 保留。首块仍由 CPU 预装
  一个字，其余 DMA 按实际字数提交；模式切换、末项宽度、STOP/失败清理由统一
  owner 处理，已提交前缀不可改写。
- 主联合回归 748 项及另行时间轴回归 22 项通过；联合覆盖真实 pioasm、编码边界、
  backend 故障注入和有限 FIFO 模型。理想模型的 DMA 退休后库存快照
  由双字约 5 ms 增至单字约 9 ms；该数字是模型快照，非目标板 WCET 或连续性证明。
- 新 Release build `20260917190453` 双槽链接通过；资源审计显示主 RAM 扣 heap
  后约 22348 B、scratch 约 24 B、uniform 增量约 32 B，完整 RUN 新栈上界约
  1536 B，仍有四个 SDK XIP 叶子。上述均为本构建快照，事实以链接和审计原件为准。
- 文档契约 `VDC-PRIORITY-01` 更新至 v15，仍 pending；独立 C11 结论为
  `ACCEPT_V15_PENDING_SEMANTICS_NO_HIL_OR_PRODUCT_RELEASE_CLAIM`，原件在
  `design-review/c11-v15-semantic-independent-r1.json`。同源码四板 quick P3
  PASS_WITH_WARNINGS，181.917 秒，25 INFO/18 WARN/0 ERROR/FATAL；严格质量
  失败保留。实际 scope 复采尚未完成，不能据 host/P3 通过宣称连续或 DPLL 锁相。
- uniform 四板实际采集 `capture-r2` 已完整 STOP/导出 scope 与 native：schema 6
  的 `fifo_words_per_edge=1` 四板均生效，但 NO1–NO4 仍因 FIFO 耗尽 STARVED。
  服务末间隔约 2.17–2.65 ms，退休时末沿界已落后约 0.33–2.30 ms；短服务
  wall 约 28.7–46.7 µs，超预算计数为零。以上为本轮有限采集快照，不能外推
  WCET 或锁相；最终 STOP 读回和示波器 STOP/EXT/NORM 均通过，原件在
  `capture-r2/`。
- 独立专项复核 `design-review/uniform-capture-independent-r2.json`（SHA256
  `800d2630a645e8cebe2850f5ca905dcafdb753aaeeb5d760e834212d202e17c4`）确认四板
  实际 uniform、高段 500 tick、采集期零查询及 STOP 收尾；将 harness 因检测到
  STARVED 而返回非零视为专项 FAIL 原件保留，不改写为通过。
- 该结果表明单字编码增加了 FIFO 库存密度，却未跨过 Core1 补给调度空窗。
  下一 gate 转为对齐调度服务间隔、模型后缀准入和 DMA 退休期限的相关时序证据，
  再决定是否调整 TDMA 服务预算；不通过放宽连续性判据结案。

当前执行方向由用户进一步明确为预编码最小时间戳、确定性特等席运输和 Core1
直接匹配，见 `VDC-PROGRESS-20260916-038/039/040/041/042/043` 与 `VDC-FAST-001/002/003`。
此前“先核对或替换 OSAL 发布时基”的下一步由该方向覆盖；已通过的分片运输和
本地采用证据保留，不提升为逐圈确定性或锁相完成。

提交边界：`e6ff06a` 已提交 priority RX/栈与调度收敛、RefMem 缩容、验收分级及匹配的
四板 quick P3 凭证；暂存源码指纹 `fb2b46871987abe1426a52207966c93942b75fea1a58a53adb396be2d8589a52`
已由真实 pre-commit 硬件门禁核验，见 `VDC-PROGRESS-20260917-001`。严格质量告警仍保留，
不授予单圈同步编码、三从闭环或锁相完成。

### VDC-PROGRESS-20260918-004：有限长时间轴与可配置补给窗口

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`、`SYNC-OUT-002` IN PROGRESS。
  本条数字为证据快照，非产品事实源；证据根
  `out/HardwareAcceptance/20260918/dpll-run-timeline-r1/`。
- 同次有限 RUN 复用初始 raw/local bridge，后续绝对目标映射到同一硬件时间轴；
  规划和准入仍读取新鲜 raw 时间，不累积舍入周期，不修改 NO1 PI。较长未提交
  后缀每次最多规划四个边沿；模型 token 或前驱改变时丢弃部分/完整缓存。
  backend 支持有界实际边沿数，先全量校验再编码；首个单边沿块不启动零长 DMA。
- `OUTPut:TIMing` 提供规划/承诺/低水位完整三元组；STOP 且客户端空闲时设置，
  PREPARE 锁存并联合检查周期/容量/静态表。默认请求 12000/16000/6000 µs，
  本轮 1 ms 输出对应十边沿块，最多十六边沿；默认不是已证明最优配置。
  Product Config v5 保留 v1–v4 CRC 域及非零输出 delay，仅显式 STORe 写 Flash。
- 主联合 host 671 项及独立时间轴 22 项通过，后者覆盖 20 秒运行/低字回绕、
  分批规划、库存水位、模型变化、STOP 与容量拒绝。文档检查器测试 38 项通过。
  双槽 Release build `20260917175835`，源码
  `feb9aac77b4b41978730b409d95a80dc84b20a46324d3a582bfb0ac0e9249596`，包
  `e06ee9f9df635c043d80bdb83c5a037d4163aca6922f30becce5388950cb91f6`。
  新增 Python 测试在构建过程中落地但不参与固件编译；最终源码指纹在 P3 前冻结。
  双槽 ELF/BIN/包一致；扣 heap 后主 RAM 22380 B，完整 RUN 栈 1524 B，
  cached 栈 1256 B，继承路径最大 2872/3072 B，scratch 余 24 B。
  实际 cached 调用图仍含 schedule getter、clock_get_hz、memcpy 和 GPIO helper
  四个 XIP 叶子；不沿用前轮耗时作为本轮 WCET，详见 `design-review/`。
- 同源码四板 quick P3 PASS_WITH_WARNINGS，182.546 秒，25 INFO/18 WARN，
  0 ERROR/FATAL；strict/closed-loop/realtime 质量失败保留，不当作输出专项通过。
- 配置专项首轮因工具错误地把 SET 三元组当成 OK 超时，原件保留于
  `config-hil/live-r1/`；无 Flash 写入，四板 STOP 和原请求值已读回。修正工具后
  `live-r2` PASS，37.531 秒：NO2 测试参数保存/重启/召回、原持久值恢复后再次
  重启及原 RAM 值恢复完成，四板 delay/PI/角色/基线/身份保持；RAM/Flash 无待恢复项。
  两次 RESET 的串口 ClearCommError/PermissionError 原件保留，随后重新枚举、
  身份和不同于 RAM sentinel 的参数读回闭合，不称重启全程无异常。
  新负测直接编译真实 callback/libscpi，裸 OK 不能冒充 SET 成功。
- 首次输出复采 `capture-r1` 在 START 前因 NO2 重启清除临时训练矩阵而失败；
  `TRN03STG` 返回 EMPTY，未形成有效波形，四板 STOP 留证。后续重新装载同次
  P3 已测矩阵并核对读回，`restore-stage-r1` PASS；不重新扫描 P0 或训练延迟。
- `capture-r2` 在 NO4 ARM 被 adapter 278 拒绝，未进入时间轴规划/输出；装载工具
  切换至 raw-flight。后续 STOP 恢复 process-image、clock evidence、provisional
  与 debug admission；`restore-context-r2` 全部读回通过。首次恢复脚本把
  provisional 回读误按六字段解析（实际七字段），失败与四板 STOP 留于
  `restore-context-r1`，未改固件。
- `capture-r3` 已真实运行并完整导出四路 scope/native；RUN 查询零，四板请求值
  与锁存值一致，十边沿块实际生效，bridge_samples 均为 1。NO1–NO4 分别提交
  34/105/577/412 块，分批规划 155/324/1746/1264 次，缓存失效 29/3/5/9 次；
  快路成功补给 0/8/35/20 次，wall 最大 31.208/47.500/62.128/48.532 µs，
  调用与样本分别 59/99/330/185 且逐板相等，超候选预算全零。此为有限窗口，非 WCET。
  输出专项仍 FAIL，四板均 STARVED；NO4 一次 submit 拒绝，其余零。退休时
  原末下降沿完整界已过去约 0.47/0.81/1.32/2.13 ms。末态计数不能确定每次
  DMA 就绪或缓存失效的因果顺序，详见 `capture-r3/refill-retirement-analysis.json`。
- NO1 启动邻域可见 190 个脉冲（总准入 340），块内 171 个周期误差约
  −0.181..+19.858 ns，18 个块界误差约 +28.469 ns..+56.200 µs；15 个块界
  超过仅用于诊断分组的 100 ns。20 ns 采样及插值不证明纳秒物理精度；本轮
  模型修订 31 次，不足以逐边界归因。固定映射已采用而块界波动仍在，不能再
  把全部波动归于每块重采 bridge，也不能把跨启动窗口与前轮作为受控 A/B。
  图与原件见 `no1-review/capture-r3/`；保留历史内部 PI −9..+9 ns 基线。
- 四板最终 STOP 58/16/58/59 均已应用，输出 PIO/DMA 关闭；示波器恢复 STOP/
  EXT/NORM，已由 `final-stopped-state.json` 另行读回核验，scope error 为零。
  配置能力与实际采用已验证，连续性未通过；下一切片优先核对
  模型更新使可编辑后缀重建的代价，以及 DMA 退休至末沿之间的实际补给期限。
  参数试验可以同源码 STOP 修改后复采，无须烧录/P0；不通过放宽断流或精度
  判据来宣称完成，不提前修改 NO1 PI。
- 契约 `VDC-PRIORITY-01` v14 保持 pending；独立 C11 接受稳定语义范围，原件
  `design-review/c11-v14-semantic-independent-r1.json`。扩大块容量不增加 DMA
  退休后 FIFO 余量；固定映射不消除真实模型更新造成的边沿变化，连续性和
  相对 NO1 同序物理边沿差仍为后续专项门禁。
- 代码提交 `95006a3`，真实 pre-commit 核验当前 staged 指纹及 P3 凭证通过；
  文档单独提交，外来 TDMA 工作区文件保持未暂存，未 push。

### VDC-PROGRESS-20260918-003：相位内缓存交接与 SRAM 热路径验证

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`、`SYNC-OUT-002` IN PROGRESS。
  本条数字为证据快照，非产品事实源。保留 NO1 历史内部 PI 残差基线及现有
  算法；内部残差、输出周期误差与跨板同序边沿差仍分别验收。
- 完整 TDMA callback 因相位剩余时间不足跳过时，原本连同尾部输出补给一起
  跳过。现按 `PROJECT_CORE1_RUN_OUTPUT_HANDOFF_WCET_CYCLES` 在同一 TDMA
  相位内重新核时，只接续仍有效的私有后缀；不生成首块、不桥接或反解、不借
  后续相位/GUARD。IRQ 在短服务期间关闭，剩余 ingress 配额不变；完整服务
  skip/start-miss、失败返回和 full-only phase_run_count 保留。短服务 runtime
  单独按候选预算检查并进入原期限账，生命周期/STOP 和 CAS 整次所有权保留。
- RUN schema 追加短服务调用、成功准入、空缓存、body 上界与 caller wall/超时
  统计。caller wall 不含后续统计报告本身，后者由相位尾期限检查覆盖；延迟报告
  复验 request，不能污染新请求。CAS 拒绝及重新准备后的统计清零不能当作完整
  采样；快照只在 STOP/退休后导出，RUN 查询仍为零。
- 首轮证据根 `out/HardwareAcceptance/20260918/dpll-run-fallback-r1/`：联合 host
  237 项、Release 和同源码四板 quick P3 通过，P3 为 PASS_WITH_WARNINGS，
  189.453 秒，25 INFO/18 WARN/0 ERROR/FATAL；严格/closed-loop/realtime 仍 false。
  有限输出专项 FAIL，四板均 STARVED。真实短服务准入 44/70/15/40 次，但最大
  caller wall 为 327.152/345.884/342.832/376.292 µs，分别超候选预算
  211/244/73/147 次；保留调用/报告差 2/1/0/1，不将入口接通当作短交接达标。
  四板 STOP44/43/44/44 已应用，原生每板 76 条记录及原始 SCPI 字段独审一致。
- 同轮 NO1 波形 190 个完整脉冲，142 个块内周期误差约 −17.993 至 +19.535 ns；
  47 个块界中 44 个超过百纳秒，极值 −129.641/+139.315 µs，至少 18 个异常
  不能各自对应一次独立模型切换。采样二十纳秒，不能作独立纳秒精度认证；
  启动波形窗口未覆盖最终耗尽，不以窗口内未停输否定末态 STARVED。单轮块数
  或间隔变化不构成受控 A/B，逐块原因仍待定位。独审见该根 `no1-review/`。
- 后继证据根 `out/HardwareAcceptance/20260918/dpll-run-sram-r1/`：仅将客户端、
  后端补给和身份/时钟读取热函数显式放置主 SRAM，并禁止重新内联到 XIP；
  保留算法、PIO/DMA、STOP、编码、PI 和候选预算。共享 SDK 叶函数仍须按最终
  链接审计，不使用全局 SDK/OTA 放置选项，不宣称仅凭源码注解满足预算。
- 新源码联合 host 504 项通过，独立 237 项通过；Release build
  `20260917171629` 双槽链接与安装包绑定通过。实际目标热符号共 18 个进入
  主 SRAM，主 RAM 扣 heap 后余量 22864 B，较首轮减少 8192 B（含布局变化），
  scratch gap 24 B，完整 RUN 栈 1644/3072 B，继承最大 2872 B。
  `memcpy`、`clock_get_hz` 和退休拉低输出的 SDK 叶函数仍在 XIP。
  源码指纹 `2aac1dc32955a9737d4582d558cf5e38da5a4dd886321f6f97f32a7a1711a23d`，
  包 SHA `64f93374ae051cc1be46a266bd6020d0334fb9a4bef8ee825697407dba6668bb`；
  最终资源及仅驻留属性变更的独立核对见新根 `runtime-review/`。
- 新根 `p3-r1` 为同源码四板 quick P3，PASS_WITH_WARNINGS，189.390 秒，
  25 INFO/18 WARN/0 ERROR/FATAL；四 START 精确 OK，STOP41 已应用，
  每板 14 条原生记录和有效 process-image 推进。严格/closed-loop/realtime
  仍 false，固定快速验收范围未扩大，匹配凭证和独审保留。
- 新根 `capture-r1` 有限输出专项仍 FAIL，仅四板输出 STARVED 断言失败。
  四板准入 2474/384/582/725 块；短服务成功准入 108/18/33/53 次，
  caller wall 最大 46.480/42.944/38.576/42.644 µs，535/109/157/213 次调用
  均有 wall 样本，候选预算超时为零。以上仅证明这次保留样本符合候选预算，
  不授予普遍 WCET；与旧轮初态不同，不以块数变化证明因果改善。
  本轮四板运行态准入拒绝均为零，末态仍 FIFO 空/TXSTALL/DMA idle，完整补给
  连续性未闭合。四板 START 精确 OK，RUN 查询零，STOP44 已应用、PIO/DMA
  关闭；每板 76 条原生记录、NO2–NO4 模型/相位末态与既有判据吻合。
  原始记录、波形和独审均在新根，不覆盖首轮超预算和断流失败。
- 新 NO1 波形仅覆盖 9896 个准入脉冲中的 191 个启动脉冲；143 个块内间隔
  误差约 −13.884 至 +19.579 ns，47 个块界中 39 个超过百纳秒，极值
  −107.260/+109.319 µs，大跳变仍存在。全程模型变化多于窗口异常数，不能再
  套用上一轮的“至少部分异常没有独立换模”计数下界，也不能反推全由模型更新
  造成。原生事件前缀晚于波形窗口，无法逐块绑定；采样仍不作纳秒精度认证。
  独立报告与冻结清单见新根 `no1-review/capture-r1/`。
- 下一 gate：按用户提出的连续长时间轴方向，在同一硬件 tick 坐标中递推
  后续编码，提前规划可修改后缀，以硬件剩余执行时间确定补给水位；保持已
  承诺前缀不可改写、DPLL 更新可及时接入及 STOP/代际取消。软件缓冲边界不应
  成为重新桥接或硬件重启边界；当前 PIO 并未每块重启，问题仍须由完整规划
  服务可用性、时间映射和模型接续分别定位。保留 NO1 PI，不授予连续输出、
  百纳秒锁相或 VDC 发布完成。

### VDC-PROGRESS-20260918-002：Core1 自主周期释放与断流复核

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`、`SYNC-OUT-002` IN PROGRESS。
  证据根 `out/HardwareAcceptance/20260918/dpll-run-release-r1/`；以下数字为本轮
  证据快照，非产品事实源。保留用户指出的 NO1 旧内部 PI 小残差基线，不改 PI。
- 仅将 `core1_realtime_entry()` 的周期等待由 SDK `sleep_until()` 改为
  `busy_wait_until()`。旧 Core1 路径是共享 alarm pool 的非 RTOS WFE 分支，
  不是 Core1 上的 RTOS 任务调度；新最终链接函数直接读取硬件 TIMER0，
  没有共享锁、alarm 或跨核唤醒依赖。绝对周期、迟到追赶、STOP 周期重基和
  Flash lockout poll 保留，不增加 GUARD 工作或修改静态相位预算。
  空闲期主动轮询增加；该结构修正不等于实测周期抖动或输出连续性已达标。
- 实际入口 host 覆盖绝对释放、迟到、Flash poll 顺序、四档周期重基及回绕，
  联合 14 项通过。Release build `20260917162329` 双槽 ELF/BIN/包绑定通过；
  静态 RAM 增量零，RUN 栈 1684/3072 B，继承最大 2872 B，扣 heap 后 RAM
  31088 B。源码指纹 `d2681712175cd88e90a8db29511609001ffeaa52df42ef1125f39d9e15955dcf`，
  包 SHA `731af8c3320c3cfb453086a3d9b80dc76e9b2aca8a8887f58c31df8a51713b71`。
  独立实现、最终链接与资源审计见 `runtime-review/`。
- `p3-r1` 首次失败保留：NO3 START 返回旧 helper 的超时替代文案，并非真实
  ACK 或已完成读回；其原生记录有 ARM 基线、未触发且零采样，另外三板各有
  完整记录，但不能据此证明四板有效数据闭环。四板 STOP41 已应用。
  未改源码和门槛的一次有界 `p3-r2` 复测为 PASS_WITH_WARNINGS，195.125 秒，
  24 INFO/21 WARN/0 ERROR/FATAL；四板 START 精确 OK、各有 14 条记录及有效
  process-image 推进，凭证匹配当前源码。严格/closed-loop/realtime 仍 false；
  首次 START 未成功的具体原因未证明，不称已修复启动偶发问题。
  代码、入口测试及匹配凭证已提交 `48ac0c6`，真实 pre-commit 源码指纹门禁通过；
  另一工作流的 TDMA 文件修改未暂存或提交。
- `capture-r1` 同源码有限静默专项仍 FAIL：四板 START 精确 OK，静默 11.016 秒、
  RUN 查询为零；分别准入 44/106/133/558 块，缓存命中 32/89/116/463 次，全部
  STARVED。末次服务间隔为 7.763620/1.301848/3.488548/7.188696 ms；NO2/NO3
  各有一次补给拒绝。退休观察均为 FIFO 空、TXSTALL、DMA idle/remaining zero。
  去掉共享唤醒依赖未消除长服务空窗，不以跨初态计数比较推定改动改善或恶化。
  本轮未出现前轮的 FOLLOW 末态原因断言失败，不追改前轮结果。
  四板 STOP44/43/44/44 已应用，输出 PIO/DMA 关闭；示波器完成冻结采集，
  波形及退休分析在 `capture-r1/`，独立 NO1 分块复核在 `no1-review/`。
- NO1 新波形有 176 个完整脉冲，对应全部 44 块，末脉冲后窗口内保持低态。
  132 个块内周期误差约 −0.443 至 +19.361 ns；43 个块界误差约 −122860.057
  至 +5580 ns。40 个超过百纳秒的偏差全部在块界，至少 27 个不能各自归因于
  一次模型切换；聚合计数不能标定具体异常对应的更新事件。
  其中单次大负跳变比前轮大，但非受控 A/B，不能直接归因本轮 release 修改。
  原生前缀仍晚于波形及输出耗尽，不能逐块归因 PI、bridge 或模型变化。
  采样间隔二十纳秒且使用插值，不能当作独立纳秒精度；内部 PI 小残差基线保持。
- 下一 gate：核对 `app_realtime_run_phase()` 整相位迟到跳过与 TDMA 尾部输出
  服务的依赖，在既有静态预算内保证必要快速交接并定位不可分割服务空窗；
  不能只扩大 DMA 块或将可取消软件缓存当作硬件库存。后继只读候选与指令模型
  见 `out/HardwareAcceptance/20260918/dpll-run-handoff-r1/design-review/`，尚未实现。
  再独立隔离块间 bridge 重投影和模型连续性；不重复 P0、不改 OTA、不调 NO1 PI，
  不宣称连续输出、锁相或 VDC 发布完成。

### VDC-PROGRESS-20260918-001：有限后缀预规划与 NO1 块边界定位

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`、`SYNC-OUT-002` IN PROGRESS。
  午夜前的 host/Release/设计证据在 `out/HardwareAcceptance/20260917/dpll-run-prefetch-r1/`；
  后续硬件及独审根为 `out/HardwareAcceptance/20260918/dpll-run-prefetch-r1/`。
  以下数字均为本轮证据快照，非产品事实源。
- DMA 忙时提前准备一个私有后缀；不推进已准入序号/末边沿，命中后省去重新
  bridge 采样和模型反解。准入仍复验 STOP、身份、模型、前一末序号/下降沿及
  完整时钟配置；原子取消意图由 owner 清缓存，准备/终态清理，每次最多一轮
  规划和一次准入。保持 TDMA 内原服务入口、NO1 PI、桥接公式和后端安全边界。
  RUN schema 追加预规划、成功缓存准入和模型/尾部失效计数，仅退休后读取。
- 联合 host 193 项、独立 client 121 项和综合独审 187 项通过；Release-r1
  build `20260917155805`，两槽 ELF/BIN/包一致；缓存净增静态 RAM 184 B，
  RUN 调用链栈 1684/3072 B，继承路径最大 2872 B，扣 heap 后 RAM 31088 B。
  来源与限制见新根 `runtime-review/`，不是实测 WCET 或输出精度证明。
- 新根 `p3-r1` 同源码四板 quick P3 为 PASS_WITH_WARNINGS，耗时 238.609 秒，
  25 INFO/18 WARN/0 ERROR/FATAL；继续保留原始严格质量失败。源码指纹
  `1cce4002f2d838bc58dd4688ce9602280cf19458ff740a33ae3f72dc15e26735`，
  包 SHA `a348f0dab29452d34574474be0c734912d6e610ae8776b5db923237a686775cf`。
  独审确认耗时增长集中于 OTA ACK 等待，本轮不改 OTA；复用线序和非 OTA 流程
  未增加。代码及匹配凭证已提交 `0903d87`，真实 pre-commit 硬件门禁通过；
  前轮诊断一并收敛，另一工作流的 TDMA 文件修改未暂存或提交。
- NO1 旧内部 PI 残差 −9 至 +9 ns 的基线继续保留。午夜前的只读
  `no1-review/no1-independent-review.json` 将两轮旧物理输出复核到四脉冲块交界：
  超过百纳秒的周期偏差全部位于交界，块内约二十纳秒内；至少部分异常不伴随
  模型切换。既有原生 trace 晚于这些波形，不能归因具体峰值或等同引脚锁相。
  逐块 bridge 重投影和模型换代连续性为后继独立切片，不先调整 NO1 PI。
- 新根 `capture-r1` 有限静默输出专项仍 FAIL：四板分别准入 319/161/71/43 块，
  成功缓存命中 251/138/63/39 次，提交拒绝均为零，随后全部 STARVED。
  末次服务间隔为 7.569644/2.070688/6.019932/5.979860 ms，各自跨过最终下降沿
  的完整时间界；FIFO 空、TXSTALL、DMA idle/remaining zero 的首次退休观察一致。
  预规划已经实际使用，但不能填补服务空窗；计数与旧轮不同初态不是受控 A/B。
  同轮另有三从 phase-endpoint 失败保留：旧采样器要求 FOLLOW 末态 STOP reason，
  实际为 BINDING reason；离线复验末态模型序号、频率和相位基点一致。
  取消来源尚未区分 RUN 绑定变化与 STOP 交接，不改脚本判据或把原失败改判通过。
  四板 STOP44 已应用、输出 PIO/DMA 已停，RUN 查询为零，示波器恢复 STOP/EXT/NORM。
- 新波形 NO1 可见 190 个完整脉冲，142 个块内周期误差约 −1.273 至 +19.768 ns，
  47 个块边界误差约 −3079.889 至 +1480.167 ns；42 个超过百纳秒的偏差全部在
  块边界，至少 18 个不伴随模型变化。采样间隔二十纳秒且使用插值，不能由此
  授予独立纳秒精度；NO1 的断流晚于此短窗，以退休原件确认。
  原生 NO1 前缀仍晚于波形窗口，具体逐块映射原因未证明；独审与图见新根
  `no1-review/capture-r1/`。新缓存不消除原有块边界阶跃，未改 PI 或桥接算术。
- 下一 gate：在 HAOFV 静态相位预算内独立解决快速交接和长服务空窗，并核验
  FIFO 源退休时机；随后以逐块证据隔离 bridge 重投影与模型连续性。
  不扩大本切片 quick P3 范围、不重复 P0 寻优、不改 OTA、不宣布连续输出或锁相。

### VDC-PROGRESS-20260917-031：有限输出补给诊断与断流分型

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`、`SYNC-OUT-002` IN PROGRESS。
  证据根 `out/HardwareAcceptance/20260917/dpll-run-refill-r1/`；完整本轮记录保存在
  `checkpoint-031.txt`。以下数字均为本轮证据快照，非产品事实源。
- 新增有效服务/成功准入间隔、首次退休前 PIO/DMA 观察及 PREPARED/RUNNING
  客户端结果计数；不改变准入或调度，只在退休后导出。联合 host 169 项通过，
  Release-r2 与独立资源审计通过，同源码四板 `p3-r1` 为 PASS_WITH_WARNINGS，
  耗时 185.812 秒，19 WARN/0 ERROR/FATAL；严格质量失败保持原值。
- `capture-r1` START ACK 失败保留，NO1–NO3 零块、NO4 有输出后取消；未放宽
  超时 sentinel 判据。`capture-r2` 四板各提交 16/458/172/269 块后 STARVED；
  NO3/NO4 最后服务空窗 7.779484/6.081336 ms，跨过同一末尾边沿的时间区间；
  NO1/NO2 前次服务仅剩约 197–206/264–273 µs，各有一次计划后提交拒绝。
  四板首次退休观察均为 FIFO/DMA 耗尽；运行态 ring/model/bridge 暂忙计数为零。
  示波器确认 NO1 的 64 个完整脉冲后中断。专项仍 FAIL，四板 STOP47 已应用，
  PIO/DMA 已停，静默窗口无实时查询；离线复核与原始波形位于 `capture-r2/`。
- 用户确认按粗锁定、精锁定、完全锁定逐级推进，目标和统一物理判据见 TODO；
  先连续输出，再粗锁定。NO1 的微秒级边沿变化须区分参考、模型校正与输出路径，
  不能仅由晶体 ppm 规格推定随机抖动或判定锁相失败的唯一原因。
- 用户补充旧多板 PI 中 NO1 基本在 ±20 ns。已查到 2026-09-14 的
  `baseline-r1-lock-review-r2/plots/dpll_residual_analysis.json` 内部 NO1 残差为
  −9 至 +9 ns，支持保留旧 PI 收敛基线；该内部量不能直接等同当前引脚周期抖动。
  下一步对照内部模型与实际边沿，优先分离时间映射和输出执行误差。
- 下一 gate：缩短补给服务空窗、提前准备下一有限块并做快速交接；另成独立
  host/Release/同源码四板 P3/输出专项切片，再缩小 bridge/enable 时间锚误差。
  本轮代码已验收但暂未提交：并行自回归检查器/测试修改使主工作区与验收指纹
  不同；隔离工作树尝试又受原始证据绝对路径约束，未绕过门禁或改写凭证。
  保留全部并行修改和已验收源码，待两项工作的提交边界协调后继续。

### VDC-PROGRESS-20260917-030：有限持续 PIO 输出与启动观察收敛

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`、`SYNC-OUT-002` IN PROGRESS。
  证据根 `out/HardwareAcceptance/20260917/dpll-run-executor-r1/`。本节数字为
  验证快照，非产品事实源；有限输出切片不授予百纳秒精度、产品 RUN 或锁相。
  代码提交 `ad63f75` 已经真实 pre-commit 核验当前 P3 指纹；他人
  `components/tdma/src/tdma_flight_engine.c` 未修改、未暂存、未提交。
- 按 node-sequence 分支的预编码、资源预留及退休机制实现小型持续后端；
  当前只驱动 OUT1，统一 SYNC_IO 管理全部语义 IO 的边界不变。使用现有
  scheduled persona 的 PIO0 SM1、DMA2、八条指令及共享 arena 的八个源字；
  每块四个脉冲，所有 pull 在低态，高态不等 FIFO。DMA 源退休不代表边沿完成，
  模型更新只改新后缀，取消/断流/时钟异常/有限时长到期异步退休。
- VDC Core1 固定入口位于 TDMA owner 之后：STOP 锁存 delay 和请求；START
  后验证当前 session/role/clock/slot/schedule，再以整数反解与共同网格准备未来
  边沿。复核修正了外层尚未退休就被 Core0 覆盖、DMA 忙遮蔽绑定失效、ARM
  误当 START、首块前 STOP 遗留及完整诊断快照进入实时栈等问题。
  `SYSTem:VDC:OUTPut:RUN` 提交有限请求，`:STOP` 只提交取消，`RUN?` 仅退休后
  导出。真实 parser 测试发现旧 lexer 的数字尾空白截断，局部在副作用前拒绝，
  未改第三方 parser。Release-r2 的 signedness 编译失败保留，r3/r4 修复通过。
- 首版 owner/PIO/bridge 回归 109 项、客户端/真实 parser 62 项、既有固定输出/
  数学/相位回归 159 项通过。`runtime-review/` 对新调用链按地址建图，保留
  同名静态函数，未发现新 RTOS、堆分配、日志或阻塞依赖。future_raw 由
  非作者 `event_core` 独审：四项生产测试、10284 组有理数端点案例及 6250
  个内部样本通过，额外 raw 一拍上界不能删除；见独立数学报告。
- 首次 P3-r1 同源 `a93a1c0902cb424d17e90068323e184eead2694305ca9a3eba10f4bc04324d6b`
  完成，耗时 190.672 秒，25 INFO/18 WARN/0 ERROR/FATAL；原严格、closed_loop、
  realtime 为 false，diagnostic 为 true，分级 `PASS_WITH_WARNINGS`。
  四板各十四条原生记录、序号推进及 bad 增量零、STOP 已应用，独审见
  `runtime-review/p3-final-independent-review.json`。
- `capture-r1` 输出专项失败：四板均只准入首块，随后 CLOCK 退出，
  `anchor_after=UINT64_MAX`；这只能指向 raw/PC 初次观察无效，不能归因于
  时间锚跨度超限。没有新示波器触发，不能宣称出脉冲。三从末态相位提交
  33/43/50 次，有限原生前缀分别记录 27/31/35 次，末态与 DCO 模型一致；
  两种覆盖不可混写。零 RUN 查询、全板 STOP、示波器恢复 STOP/EXT/NORM。
  初版脚本冻结副本与采集记录 SHA 相同，失败原件不被后续复测覆盖。
- 启动观察修订为 enable 后一次 raw 观察、单次 PC、上界 raw，保留原始
  before 下界及 PC 必须已进入低段的规则，不轮询、不放松准入。新增独立
  PC/offset/raw flags 和两次原始读数，区别异常来源；RUN? 末尾追加字段。
  实际 C/MMIO/真实 parser 回归 113 项通过，独审连同编码/桥接/门禁共
  132 项通过。Release-r5 build `20260917145647`，A/B ELF→BIN→包一致；
  源码 `e7acef7796e738af1edd7a1da1f2fbf1f968f8e03b82b3960df1839081e0ac8a`，
  1267 文件，包 SHA `f6301d1e12d7839dc498e5e8e0153382f73cc2aa496dbfd0f489e3a1d1213acc`。
  新 RUN 限定路径栈上界 1572/3072 B，既有限定最大路径 2872 B，
  主 RAM 扣 heap 后余 31664 B、scratch 余 24 B；不以此宣称 WCET 或精度。
- 修订后 P3-r2 耗时 198.391 秒，25 INFO/19 WARN/0 ERROR/FATAL，
  `PASS_WITH_WARNINGS`；严格/closed_loop/realtime 仍 false，diagnostic true。
  四板序号及 accepted 增量 262–264，bad 增量零，STOP 代际已应用；同指纹与
  原始证据独审见 `runtime-review/p3-r2-final-independent-review.json`。
- `capture-r2` 首次启用通过：四板 `start_pc=27`、`program_offset=23`、
  `start_raw_flags=3`，已进入低段。输出周期 1 ms 时分别准入 293/117/215/86
  块、采用 31/1/2/0 次模型变化，后续均因 TXSTALL/STARVED 退休，专项 FAIL
  保留。计划/绑定拒绝均为零，不足以排除未计数 snapshot 暂不可读、DMA 尚未
  ready、客户端门禁冲突或整个 TDMA 相位被跳过，不能直接定因为 CPU 过慢。
  冻结 RAW 实际看见四路约 2 µs 脉冲；NO4 在窗口尾部约 80 ms 无输出，
  波形也证明中断，不能以多脉冲基本检查通过替代持续输出通过。
- 同固件 `capture-r3` 仅将输出周期改为 10 ms，TDMA 静态周期仍为 1.5 ms；
  有限静默窗口后四板输出正常 CANCELLED、PIO/DMA 停止，分别准入
  287/292/295/300 块，源退休 286/291/294/299 次，采用模型变化
  8/31/33/31 次。三从相位末态提交 26/28/26 次，与频率及 DCO 末态一致；
  原生有限前缀相位为 22/21/21 次。零 RUN 查询，四板 STOP 后导出，
  示波器恢复 STOP/EXT/NORM，专项脚本 PASS，耗时 56.609 秒。
  首次 enable 区间仍为微秒量级，bridge 区间也未达到百纳秒；该通过只授予
  本次低频有限输出与动态后缀采用，不把 1 ms 失败改成通过。
- `capture-r3/scope-run-analysis.json/.svg` 独立解码同一冻结 RAW：20 ns 采样、
  NO1 CH1 触发附近 -10 至 +190 ms，四路各二十个完整脉冲，脉宽中位约
  2.007–2.009 µs，无窗口内部长间隔；未覆盖整段十一秒。相对 NO1 最近边沿
  的中位差约 46.920/43.061/53.680 µs，未认证同 ordinal。约 80/160 ms
  三从相对差共同跳变与 NO1 自身短周期同位置，不能直接称三从共同漂移或
  算法变差。四路首脉冲各自对齐展示只用于比较脉冲形状，不是实际边沿重合。
- C11 稳定语义为 `ACCEPT_V13_PENDING_STABLE_SEMANTICS`，登记保持 pending；
  数学独审、实际指令测试、资源与硬件证据分别留存。下一 gate 为补给间隔与
  暂忙早退的有界观测，定位 1 ms 断流；随后缩小 bridge/enable 公共偏移并
  验证真正相位精度。原始失败和已通过低频基线同时保留，长期目标继续进行。
  最终九文档与实施事实独审为
  `ACCEPT_V13_PENDING_CONTRACT_AND_FINITE_10MS_EXECUTION_SCOPE`，原件
  `runtime-review/c11-v13-final-independent-review.json`；动态模型/原生/STOP
  独审见 `runtime-review/capture-r2-r3-independent-review.json`。

### VDC-PROGRESS-20260917-029：共同边沿规划与状态机分支借鉴

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003` IN PROGRESS；证据根
  `out/HardwareAcceptance/20260917/dpll-run-output-r1/`。本节数字为验证快照，
  非产品事实源。本切片实现边沿数学准备，尚无持续 RUN 硬件 consumer，
  不能据此宣称物理 delay 已生效、单圈期限满足或百纳秒锁相。
  代码提交 `ac57b8e` 已通过同指纹 pre-commit 硬件门禁；他人
  `components/tdma/src/tdma_flight_engine.c` 未暂存、未修改、未提交。
- `vdc_domain_dco_output_to_local_ns` 导入精确整数反解：正频率用 floor 几何，
  负频率按 ceil 平台取最早交点；支持负截距所需的超宽中间需求，不使用浮点、
  二分搜索或动态分配。失败保持输出，成功仍保留实际投影可能越过目标的整数余量。
  `vdc_output_edge_plan` 选择共同严格未来网格点，尊重未提交序号下限，
  独立有符号 delay 在本地物理轴只加入一次；本地 ns 尚未换算成 TIMER1 原始 tick。
- 首轮 host-r1 的 inverse/既有 phase 共 18 项通过，planner 因夹具少传 context
  编译失败；修复后 r2 的一项新边界预期多算一拍，独立 oracle 与实现一致，
  已修正测试输入并保留失败。host-planner-r3 共 5 项通过；独立方最终生产
  inverse/planner 合并 10 项通过，覆盖 30000/1800 个随机输入及极值、平台、
  空指针、非法模型和失败不变。候选/生产函数一致性与独审见
  `control-review/production-math-review.json`，纯计划测试不充当 FIFO 不变性证明。
- 新目录 Release `out/build-run-output-r1` 成功，build ID `20260917135528`；
  A/B/boot Flash 链接检查通过。源码指纹
  `8d36ff03a46a454e7c65f29e0ef0c2f7c3a76a06b1c33c30cc0d13f9ef32290e`，
  1255 文件。资源独审 `resource-review/release-binding-review.json` 验证
  ELF→BIN→PKG 绑定；静态 RAM 增量为零，主 RAM 余 32428 B、scratch 余 24 B，
  原审计范围内 Core1 最大栈仍为 2872/3072 B。inverse 对象为 832 B，但未被
  生产调用，最终已链接 GC；planner 也尚无生产调用，不能把候选 host 成本当成
  Core1 WCET。既有 forward 仍在原运行链，不将其误记为未接入。
- 同源码四板 quick P3-r1 完成，耗时 185.953 秒，`PASS_WITH_WARNINGS`，
  25 INFO、18 WARN、0 ERROR/FATAL；复用已测线序，没有重新 P0T 扫描。
  严格 TDMA、闭环及实时质量失败原件保留，DPLL 专项为 SKIPPED_TDMA_ONLY。
  本切片专项为纯数学实际 C/oracle 验证，未生成新物理输出锁相证据。
  四板采样窗均有 TX/RX 序号推进及有效新输入，坏帧增量为零；结束均已应用
  STOP，原生记录逐板保留。独立凭证/分级复核见
  `resource-review/p3-reference-review.json`。文档 C11 为
  `ACCEPT_V12_PENDING_CONTRACT_SCOPE`，见
  `control-review/c11-v12-final-independent-review.json`；登记保持 pending。
- 根据用户建议，只读审计
  `origin/feature/node-sequence-reservation-trigger`，固定 SHA
  `f4ab099eea2fa399149bff7b1861ceca94347d9b`；没有 checkout 或整分支合并。
  `branch-review/node-sequence-review.json` 与 `source-bindings.json` 保留
  文件行、git blob 和当前实现对照。可采纳四路预编码同拍写、Core1 执行交接、
  PIO 内部 READY/IRQ 握手、written/completed 分层回执及 STOP 退休顺序。
- 分支现有 executor 采用 100 ns 指令 tick，静态 DMA 无限重放，资源声明为
  31 条 PIO 指令及 3–4 SM/4–5 DMA；这些是分支代码快照，不能直接作为
  当前连续 DPLL 输出后端。其硬件触发没有建立跨板共同 VDC/TIMER1 时间锚，
  active 后 pull 与回执背压可能延长脉冲，原方案不能原样用于持续精确输出。
  全分支覆盖还会丢失当前输出改进并牵入 Flash/OTA 改动，未采纳。
- 后续方案保持统一 SYNC_IO 管理全部 IN/OUT；DPLL/VDC/SYNC 都是特等席。
  借鉴预编码和硬件执行做法，使用当前 board 时钟/divider 的指令节拍，
  不由 CPU 逐边沿调度。下一 gate：STOP 资源预留及 Core1 owner 交接，
  小型 PIO 执行器低态预取完整动作，有界有限块补给与已提交前缀保护，
  原始时间锚/迟到/断流/STOP 留证，再做同源码四板 P3 与有限四通道示波器专项。
  已有固定模型诊断输出不能代替持续模型更新；长期目标保持进行中。

### VDC-PROGRESS-20260917-028：独立输出 delay 持久化与本地相位提交

- TODO task ID：`VDC-TUNE-003` DONE、`VDC-FAST-003` IN PROGRESS、
  `VDC-OUTPUT-001` PENDING。证据根为
  `out/HardwareAcceptance/20260917/dpll-local-phase-r1/`；本节数字为实测/构建
  快照，非产品事实源。输出 delay 为每板独立有符号 ns，默认零，正延后、负提前；
  不重复加入 MATCH 已使用的链路传播 delay。实际 RUN 输出 consumer 尚未接入，
  配置持久化不等于物理边沿补偿生效，也不宣称百纳秒锁相。
  代码提交 `c4df028` 已通过真实 pre-commit 同指纹硬件门禁；他人
  `components/tdma/src/tdma_flight_engine.c` 保持未暂存、未修改、未提交。
- 新增 STOP-only `SYSTem:VDC:OUTPut:DELay <ns>`、`DELay?`、
  `DELay:DEFAult`、`DELay:RECall`、`DELay:STORe`。SET/default/recall 经
  TDMA STOP metadata guard；显式 STORE 经 Core0 既有维护与 FlashTransaction。
  Product Config v4/76 B 保留 v1/v2 的 64 B、v3 的 72 B CRC 域，旧记录启动只迁移
  RAM，补偿缺省零，保留 PI、角色、基线、USB 与板号；不修改 OTA 实现。
- 首次 `delay-hil-r1` 在任何 STORE 前发现真实目标上的通用 int32 参数转换会
  将 `2147483648` 静默截成最大值。失败及清理原件保留；现改为完整单个十进制
  token 的带符号溢出检查，拒绝小数、指数、单位、进制别名、引号及多余参数，
  所有拒绝发生于 RAM 修改前。真实 libscpi 输入与配置 harness 共 52 项通过，
  见 `delay-parser-host.xml`；不能只凭 mock 参数测试声称真实解析正确。
- 集成 trace 回归 190 项、phase 回归 147 项通过；独立 Domain 整数预言机、
  相位控制/频率共存、取消与模型收据复核保留于 `control-review/`。
  相位模式默认关闭，STOP 显式开启，新 FOLLOW 绑定锁存；同事件残差向最近零边界
  有限平移，Domain 实际修改 base_vdc 后须取得完整模型发布收据。仅已确认的自身
  相位平移允许频率估计坐标归一化；频率改模、未知模型及 STOP 取消退休基线。
  schema4 原生记录区分 MATCH、频率 DECISION 与实际 PHASE；负向时间坐标平移
  仅用于调试捕获，不授予单调 RUN/GPIO 输出。公共 FOLLOW ABI 保持兼容。
- 最终 Release build `20260917124808`，源码指纹
  `e4aad13282ae46ab801b5d1104044b28343876afe2ed48172978554c2052ed26`，
  1252 文件。增量构建复用 build ID，必须结合源码/package 哈希辨识；双槽
  ELF→BIN→package 精确绑定见 `resource-review-strict/release-binding-review.json`。
  相对进度 027 主 RAM 增用 588 B，余 32428 B，scratch 余 24 B，已审 Core1
  最大栈仍为 2872/3072 B。严格 parser 后续修复仅 Core0 代码变化，RAM 与
  Core1 可达调用边均无增量；没有将 Flash、SCPI 或 RTOS 加入实时处理。
- P3-r1 在 ARM 前 NO3 topology 配置超时/Execution error，原件保留，不推定
  物理链路或相位算法故障；P3-r2 通过但因 parser 后续修改而不再是当前凭证。
  最终同源码 `p3-r3` 用时 180.547 秒，`PASS_WITH_WARNINGS`，25 INFO、19 WARN、
  0 ERROR/FATAL。严格 TDMA、闭环及实时门禁原始失败继续保留；DPLL 未列为该轮
  P3 必验，不将快速流程通过提升为严格质量或锁相通过。
- `delay-hil-r2` 实板通过，工具耗时 19.797 秒，终端包络 22.985 秒。
  四板核验 UID/build/STOP/初值，仅 NO2 显式保存 125 ns，用不同 RAM 哨兵再重启
  证明 Flash 回读；随后保存原持久值、再次哨兵/重启，最终恢复原 RAM 请求。
  两次 STORE、两次 RESET；合法正负边界、非法值不变、默认与召回分别验证，
  PI/角色配置/基线/身份不变，RAM/Flash 无遗留恢复项；NO2 重启后的角色
  applied generation/pending 状态改变，不能声称完整角色状态元组不变。
  独审逐条复算 234 条指令通过，见 `parser-review/delay-hil-r2-independent-review.json`。
  实机原 RAM/Flash 值均为零；两者不同时的恢复由 host 覆盖。实机持久化目标限于 NO2，
  不写成四板分别完成保存/重启专项。详见 `delay-hil-r2/report.json` 与命令原件。
- 首次相位专项 `native-phase-r1` 在 START 前发现 NO2 重启后的 TRN03STG 为
  EMPTY；未 ARM/运行，相位执行尚未发生。失败保留，后续使用本轮 P3 的同一
  矩阵重新装载并逐板读回，不重新扫描线序、不校准、不追加 Flash 保存。
- `stage-restore-r1` 装载/读回通过，但该工具将 forwarding 切至 raw-flight；
  后续 r2/r3 在 NO4 ARM 以 adapter 278（PHYS geometry）拒绝。STOP 后恢复
  process-image 模式，r4 已运行并保留 NO3/NO4 各 55 个 MATCH，但 FOLLOW 在
  首 ticket 执行前以 BINDING 取消，没有 PHASE。NO2 还缺少重启前的 VDC
  provisional 绑定与 debug admission。原件及独审完整保留，不将相位调用前的
  取消误报为 delta 算法拒绝；BINDING 的精确子条件未单独留证，不指定猜测根因。
- 随后恢复 NO2 VDC 配置，并用既有短 TDMA 工具恢复四板的完整 P3 运行上下文，
  包括 clock evidence、provisional 和 debug admission。`tdma-context-restore-r1`
  四板 ARM 成功，但因原 TDMA recorder 仍保留旧记录而在新 recorder ARM 拒绝，
  未 START，已 STOP；这一轮不记为通过，也未清掉旧记录。无需修改生产源码。
- 恢复完整上下文后 `native-phase-r5` 通过，终端耗时 35.781 秒，静默运行约
  11 秒、临时许可 20 秒，运行零查询，全部 STOP 后导出。NO2/NO3/NO4 实际确认
  相位提交分别为 109/6/36 次，均 committed=applied，拒绝零；频率实际调整
  2/0/8 次，不调整 9/8/5 次，末态 −46/0/+1805 ppb，末态 DCO 序号 112/7/45。
  这些是控制值，不是外部残余频差或锁相精度。NO2 刚重启，原生首相位残差约
  −109 秒，以有界步幅逐次追赶；不能把时间原点差当成晶振频差。
- schema4 原件只保留有限前缀：三从 MATCH/DECISION/PHASE 数量分别为
  24/4/48、55/8/6、39/8/29；满池后的过程不可由末态补造。同一次运行中
  相位提交后继续有频率决定，STOP 最近相位尝试（包括跨零 held）与最近频率
  决定共同核对真实 Domain 模型；不宣称完整频率归一化链或物理输出已验证。
  四板原生 binary 重解码及控制量独审见 `control-review/native-r5-independent-review.json`；
  590 条操作、零 RUN 查询、有限许可、STOP 后导出及清理独审见
  `parser-review/native-r5-lifecycle-review.json`。契约 C11 结论为
  `ACCEPT_V11_PENDING_CONTRACT_SCOPE`，登记保持 pending，原件见
  `control-review/c11-v11-final-independent-review.json`。
  NO3/NO4 最新内部残差区间仍为数微秒宽，NO2 尚在追赶，不能标为 LOCKED。
- `final-stopped-state.json` 独立确认四板 STOP、会话零、输出 PIO/DMA idle，
  示波器 STOP/EXT/NORM。下一切片为 `VDC-OUTPUT-001`：确定性输出 owner
  在启动锁存独立 delay，采用共同未来 VDC 边沿，记录模型/生效边沿，再以 NO1
  相对三从的实际上升沿差验证相位、斜率与抖动；长期目标继续 IN PROGRESS。

### VDC-PROGRESS-20260917-027：基线 SCPI 配置与显式 Flash 保存

- TODO task ID：`VDC-TUNE-001` DONE、`VDC-FAST-003` IN PROGRESS。
  证据根为 `out/HardwareAcceptance/20260917/dpll-scpi-baseline-r1/`，本节数字
  为本轮快照，非产品事实源。用户选择先开放早期基线替换次数和窗口，
  本轮不改变频差估计、响应比例、死区、评估档位或 NO1 PI。
  代码提交为 `8b12944`，包含匹配的 P3 凭证，真实 pre-commit 按当前 staged
  指纹通过；他人 `tdma_flight_engine.c` 修改未暂存、未提交。
- 已实现 STOP-only `SYSTem:VDC:PRIORity:FOLLow:BASEline <次数>,<窗口ns>`、
  `BASEline?`、`BASEline:DEFAult`、`BASEline:RECall`、`BASEline:STORe`。
  工厂值为 2 次/250000000 ns；次数 0 关闭质量替换，窗口为正且不超过工厂上界。
  Core0 发布整对原子配置，新的有效 FOLLOW 绑定一次锁存；旧 pending、模型更新
  和基线重建不混入新参数。STOP 退休请求，后续须重新显式请求 FOLLOW。
- Product Config 增量迁移为版本 3/72 B，保持旧 64 B 前缀与旧 CRC 域。
  新固件只读启动迁移保留有效旧 PI/角色/USB/板号；显式保存走既有 journal 和
  Core0 FlashTransaction。新维护入口持有 TDMA control guard 排斥 ARM/config，
  不把 Flash 放入 Core1 或有界 metadata callback。该迁移不承诺旧固件识别新记录。
- 软件回归分别为控制/时钟/配置首轮 153 项、最终配置专项 40 项、集成 163 项；
  数量有重叠，不求和。真实 Product Config C 测试和独立迁移/写失败/CRC/轮转
  复核通过。控制专项一次测试缺少 Core1 上下文失败保留，修复仅补测试上下文，
  生产实现未为测试放宽。原件见 `control-candidate/`、`control-review/`、`review/`。
- Release build `20260917114329`，源码指纹
  `1b7bd8e8925d94d9e8d3dfe72d6528a90e9890e30072ebccde70c3f8838a7379`，
  共 1241 文件。双槽 ELF/BIN/package 逐字绑定独审通过，package SHA 为
  `12c4f97aaf49d5979f1685956d0e77502ed508febe2392064b0d1e090ed62f42`。
  新静态对象共增 20 B，其中配置字使用原有对齐空隙，最终 RAM 余量减少 16 B
  至 33016 B；scratch 余 24 B。已审 Core1 最大栈保持 2872/3072 B。
  新 getter 自身零栈、一次原子读取，但位于 XIP，不宣称已证明 RAM 常时延。
- 匹配源码四板 quick P3 用时约 191.343 秒，`PASS_WITH_WARNINGS`：25 INFO、
  18 WARN、0 ERROR/FATAL。独审重算 29 份引用、实际四板升级、每板 14 条 TDMA
  原生记录及终态 STOP；较前基线无新增 WARN。严格 TDMA/实时门禁仍失败，
  `strict_gates_passed=false`；本切片不把快速流程通过写成严格质量或锁相通过。
- 四板实读新 build、UID、STOP 和默认基线配置后，复用既有 11 秒静默原生
  采集器，运行零查询、全部 STOP 后导出，流程约 32.718 秒通过。
  三从实际 DCO 更新 5/6/8 次，末态分别 +81/+302/+2727 ppb；这些是本地
  控制值，不是相对 NO1 的外部残余频差，也不据此推断精度变差或锁相。
- STOP-only HIL 工具完成 33 项离线测试和独审后执行，238 条原始命令完整保留，
  流程终态通过，用时约 32.61 秒（工具内部 31.5 秒）。四板核身份、build、
  STOP 和初值，NO2 实测合法边界及 7 组非法输入拒绝/请求值不变，区分工厂默认
  与召回；显式保存 1 次/125000000 ns 后，先写不同 RAM 哨兵再软件重启，
  读回保存值。随后恢复原持久化 2 次/250000000 ns，再次哨兵/重启验证，
  最终恢复原请求。PI 前六字段、角色前两字段与身份不变，代际字段原样留证，
  不要求跨重启相同。两次重启、清理均成功，无遗留 RAM/Flash 恢复项。
  主要耗时来自非法命令等待响应；不属于实时环路成本。
  实机保存/重启目标仅为 NO2；维护/ARM 竞争和新绑定锁存由真实 C host 覆盖，
  本轮原生回归采用默认参数，不声称非默认运行轨迹已全部硬件验证。
- 最终四板会话为空、PIO/DMA idle，示波器 STOP/EXT/NORM 实读通过。
  原件入口为 `scpi-hil/run-r1/report.json`、`commands.jsonl`、
  `review/p3-hardware-review.json`、`review/native-independent-review.json`、
  `review/origin-join-independent-review.json` 和 `final-stopped-state.json`。
  HIL 工具初审发现重连沿用旧身份验证集合，已修复并以错 build 重连负测验证，
  此问题在操作硬件之前闭合，初轮离线产物保留。
  独立 HIL 审核逐条重放全部命令，确认恰好两次 STORE/两次 RESET、非法输入
  错误队列及状态恢复，见 `control-review/run-r1-scpi-hil-independent-review.json`。
  `VDC-PRIORITY-01` v10 C11 结论为 `ACCEPT_V10_PENDING_CONTRACT_SCOPE`，
  登记状态仍为 pending，不由参数保存切片升级为已锁相。
- 用户明确 ±50 ppb 是后续频差优化目标，以当前频差直接推进实际输出 100 ns
  量级锁相，因此本配置切片不再追加无关的 STOP 固定频率波形门禁。
  下一 gate 为复用同事件 MATCH 残差，接通 Core1 本地相位校正、
  Domain 提交及 RUN 输出确定边界采用，记录事件/模型/生效边沿。
  频差优化独立记为 `VDC-FREQ-001`，不阻塞相位闭环；ACK、误差预算和一致 VDC
  发布继续分别验收，不把当前配置切片完成提升为长期目标完成。
  `phase-next-plan.json` 为只读接续方案：特别覆盖频繁相位改模不能饿死现有长窗
  频率估计，以及现有纳秒命名输出接口仍用微秒启动坐标，不能仅移除 STOP 门禁
  或凭接口名字声明百纳秒相位。先形成真实相位修正证据，再收紧完整误差预算。

### VDC-PROGRESS-20260917-026：单次启动连续观察与独立 STOP 末态

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-continuous-settling-r1/`，数字为本轮
  快照，非产品事实源。固件保持代码 `94c77a8`、build `20260917105000`、
  源码指纹 `9417181385bd49e04917e6494f61ba9503dec6caca9084636dcfc3c3b099223e`。
  重新核对匹配凭证与实际四板身份，未重刷、重测 P0 或改变控制算法。
- 采集器经过 51 项离线正负测试和独立复核，单次 START 后静默约 18 秒，
  临时 TRIAL 为 20 秒。TRIAL 发送前至 NO1 STOP ACK 包络约 18.188 秒，
  全板停止读回包络约 18.469 秒，余约 1.531 秒；该包络依赖工程时钟假设，
  不声明物理时钟标定。运行零查询，全部 STOP 后导出并验证 CRC/ACK/代际。
- 三从各保留 76 条有限原生前缀，均按真实 follower FULL 语义冻结：首次
  被拒绝追加使 dropped 为 1，不把该事件造进原件，也不称全窗已记录。
  共 198 MATCH、30 DECISION 的数学与控制量独立复算通过。每板前缀之后
  另有 4 个完成结果，仅用 STOP 计数证明数量差；未知中间过程不拼接。
  STOP 末次决定、局部投影、真实 DCO 及绑定另行核验。
- 全窗 NO2 无实际调频、13 次不调，末值 −1166 ppb；NO3 调频 2 次，
  −941→−909 ppb；NO4 调频 3 次，1475→1558 ppb。NO2 首组约 8 秒档
  为 [−156,+3] ppb，因跨零未调；NO3 相应档为 [−176,−17] ppb，实际 +8 ppb。
  这些决定符合现有保守控制律，不说明物理零误差。
- NO1 从 START 前 −1072 ppb 调到首成功编码时的 −1162 ppb，此后至 STOP
  committed token 115 不变；对本轮有效跟随窗口证明没有新的已发布模型。
  该证明不等于晶振不漂移，也不把启动前变化归入有效窗口。
- 新鲜固定输出的 40 个波形块、20 组阈值/通道组合与真实末态绑定独审通过。
  相对 NO1 的条件频差约为 NO2 [−146,−53]、NO3 [−149,−73]、
  NO4 [−172,−82] ppb；仍偏慢，未达锁相或 100 ns 相位资格。
  四板会话已清空、PIO/DMA idle，示波器 STOP/EXT/NORM 读回通过。
- 原件入口为 `source-binding.json`、`review/hardware-stage-review.json`、
  `review/native-independent-review.json`、`physical-review/physical-review.json`、
  `scope-fixed-r1-all-edge.json` 和 `final-stopped-state.json`。
  原始有限前缀与独立末态的范围分别保存，不以 scope 成功追认完整过程。
- 下一 gate：按用户新增要求先将早期基线次数/窗口接入 STOP-only SCPI 和
  显式 Flash 持久化，以缩短后续迭代；保持 HAOFV Core0 写 Flash/Core1 固定
  快照边界，兼容旧 PI/角色配置，每次功能变化仍跑匹配源码四板 P3。
  现有 PI 等参数的 SCPI/保存能力与未开放项分别盘点，不能宣称全参数已实现。
  用户随后明确频率稳定度不应阻塞相位闭环：完成参数入口后优先接通同事件
  残差→本地相位提交→RUN 物理输出，不以频差接近零为前置。只读路径审核见
  `out/HardwareAcceptance/20260917/dpll-scpi-baseline-r1/control-review/phase-path-gap-review.json`。
  当前 rate 更新保持相位连续，既有 fixed-output 请求不携带公共 VDC 起点，
  因此本轮 STOP 固定模型波形不证明绝对相位或运行态锁相。MATCH 模型坐标偏移
  也不能直接解释成实际 GPIO 偏差；时间戳、delay、输出量化和有效校正间隔须
  在基础相位闭环接通后共同预算，ACK 与一致 VDC 发布仍待完成。
  用户追加确认将 ±50 ppb 留作后续频差目标，以当前频差水平直接推进实际输出
  100 ns 量级锁相；两者为任务目标而非测量结论，不新增频差达标的前置门禁。

### VDC-PROGRESS-20260917-025：有界窄基线采用及示波器反馈纠偏

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-baseline-quality-r1/`；本节数字均为
  本轮快照，非产品事实源或物理精度契约。代码提交 `94c77a8`。
- 首档消耗前，同模型与完整绑定下允许最多两次早期基线替换，每次新源区间
  至少减半、完整远端间隔上界不超过 250 ms；实现符号见域架构。最多增加
  500 ms 参考坐标预算，不是缺帧时的墙钟保证。使用新事件自己的真实端点，
  没有候选即保留原基线，不新增质量前置或退还已消费档位。正常重建、取消、
  STOP 和实际应用清理私有次数，响应比例及五档估计保持。
- 控制/窗口回归 115 项、映射/投影/trace 集成回归 158 项通过，两组覆盖存在
  交集，不累加。Release build `20260917105000`、源码指纹
  `9417181385bd49e04917e6494f61ba9503dec6caca9084636dcfc3c3b099223e`
  通过双槽资源及源码独审。私有状态增加 8 B，公共快照保持 216 B；主 RAM
  扣堆余量 33032 B、scratch 24 B。prepare 局部栈增加 16 B，当前 persona
  Core1 最大链仍为 2872/3072 B，无新增可达函数/调用边，不代替 WCET。
- 同源码四板 quick P3 用时约 229 秒，23 INFO/22 WARN、零 ERROR/FATAL。
  相对上一轮新增 T1/T3 各两项告警：有效训练样本未覆盖足够候选、SCK 行的
  最佳 rearm margin 为 −1 sample。实际运输通过，严格质量仍失败；不把
  本轮质量差异归因于基线策略。源码/包/双槽 ELF/OTA/板端原件及暂存指纹
  均复核，`p3-warning-comparison-map-retention.json` 保留告警对账。
- 第一段静默原生采集：165 MATCH、38 DECISION，三从实际应用 8/8/9 次，
  末 DCO −1451/−1232/+626 ppb。三从首 MATCH 的源事件均为 61、宽 1687 ns；
  首 DECISION 实际基线均为后续事件 65、源宽 567 ns，相隔约 4.001 ms。
  证明真正使用了更窄的后续基线；稀疏记录不证明中间确切替换次数。没有
  完整源端 DECISION 端点对，不凭空补出遗漏坐标。NO1 首成功编码至 STOP
  的 committed token 38 不变，末参考为 −1906 ppb。
- 第一段新鲜四通道固定输出波形独审通过，40 块原始数据、整数输出计划、
  原生末态模型与 2048 脉冲完成状态对应。相对 NO1 的条件频差约为
  NO2 [+211,+307]、NO3 [+217,+322]、NO4 [−367,−273] ppb；仍有漂移。
- 保持同一固件和从板已采用 DCO，追加一段有限静默窗口：165 MATCH、
  39 DECISION，三从各实际应用 8 次，末 DCO −1166/−941/+1475 ppb。
  NO2/NO3 的首样本已宽 683 ns，首决策沿用；NO4 从 1687 ns 改用后续
  683 ns 基线。NO1 在首成功编码前由 −1906 调到 −1072 ppb，此后至 STOP
  token 78 不变；不能将两段看成同参考连续延长或受控 A/B。
- 第二段新鲜波形 `scope-fixed-r2b` 独立复核与 all-edge 重算通过，三从
  条件频差分别约为 [−315,−211]/[−283,−198]/[−352,−273] ppb。
  NO2/NO3 最后约 1.5 秒档估计 [−546,−55]/[−552,−61] ppb，实际增频
  +27/+30 ppb，与外部方向一致。NO4 最后新基线仅约一秒档，远端差分宽
  774 ns、估计 [−673,+102] ppb，暂不调不等于零误差或永不调整。
- 第二段首次 scope 失败保留在 `scope-fixed-r2/`：NO3 STOP 后诊断的
  `model_unchanged=2` 表示快照争用，随后安全 STOP 快照返回 1；未导出波形。
  原脚本与原判据在新目录重采通过，未修改固件、放宽判据或覆盖失败。
  所有运行窗口零查询，全部 STOP 后读取；最终四板会话清空、PIO/DMA idle，
  示波器 STOP/EXT/NORM 已读回 `final-stopped-state-r2.json`。
- 下一 gate：继续用外部频差约束内部估计与实际响应，优先在同一稳定参考的
  连续自主窗口内观察末次调整后的后档收敛，避免反复 START 引入 NO1 参考
  变化而误判方向或振荡；延长观测时预先安排有限记录容量及静默期限。
  再据原件判断是否需要进一步收窄末端区间或修改控制响应，不能以内部跨零
  代替实际零漂移。当前只关闭有界基线策略切片，ACK、实时模型输出、
  100 ns 相位精度及一致 VDC 发布仍未完成。

### VDC-PROGRESS-20260917-024：映射约束保留上限实板收窄与外部复测

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-map-retention-r1/`；以下数字均为
  本轮快照，非产品事实源或物理精度契约。
- 仅提高 `VDC_CLOCK_MAPPING_MAX_CONSTRAINTS` 的保留上限，当前实现为
  64 次；缓存仍是一组交集。原桥接准入、当前桥临时求交、矛盾拒绝、模型/时域
  重置、事务式提交以及跟随控制律保持。满额后的新约束仍参与当前事件，
  但不成为下一事件的持久约束；不增加数组、遍历、普通解析或 Core0 依赖。
- ORIGIN 原生版本随重放语义升级，布局和容量保持，代码头文件为事实源。
  固件编译期绑定 schema 与映射上限；解码器对历史 schema 2 固定使用 8 次，
  对 schema 3 固定使用 64 次，不以当前固件配置解释历史记录。从板记录不变。
  旧原件保持，真实历史源记录及从板原件均复核兼容。
- 映射专项 41 项、控制/投影/TX 集成 350 项通过。trace 三模块先通过 129 项，
  增加真实 C 原件版本误标拒绝后 origin 模块 37 项通过；后者含重叠，不相加。
  独立有理数 oracle 覆盖超过原上限的随机序列；第 64/65/66 条测试宽度为
  747/499/747 ns，保留标记为 1/0/0，证实瞬时额外约束没有泄漏到后续事件。
  满额重置、回退、空交集、耗尽和完整输出不变均验证。
  初次 host 命令路径错误、trace 临时父目录缺失以及旧凭证预检拒绝均保留；
  更正后通过，预检拒绝未产生硬件动作。
- Release build `20260917101448`、源码指纹
  `674a38125964995cee5560b6d27fef806a7f4df14bb41816b1351c0ed68b2a19`
  已完成双槽资源重建、同源码四板 quick P3 和原件独审。静态 RAM 增量零，
  主 RAM 余量 33040 B、scratch 余量 24 B；当前 persona Core1 最大栈
  2872/3072 B，无新增可达函数或调用边，不据此证明 WCET 或全负载预算。
  P3 用时约 178 秒，25 INFO/18 WARN，无 ERROR/FATAL；严格质量失败保留。
  代码已提交 `dc49df4`，真实 pre-commit 已核暂存源码与本轮 P3 凭证匹配。
- 第一段同窗原生采集通过：165 次 MATCH、34 条决定，三从实际更新 10/9/9 次，
  末态 1730/1909/4303 ppb，逐条与 Domain 模型一致。运行零查询，全部 STOP
  后读取。NO1 源前缀 76 条、约 122.04 ms，首条至 STOP 的已发布模型 token
  不变；不把查询包络中的启动变化当成整个跟随窗口的参考漂移。
- 第一段同一原件分别按上限 8/64 复算，平均编码宽度为 533.32/444.42 ns，
  收窄约 16.67%；尾部 12 条为 522/365 ns，收窄约 30.08%。新上限与实板全部
  记录精确一致，逐事件为原上限区间的子集。新结果平均分解为 latch 252.42 ns
  加映射歧义 192 ns；尾部为 254+111 ns。三从各一个同源事件核对正确，
  完整决定端点对为零。该短前缀不能证明全窗逐帧交付；不能用上轮不同输入
  的候选降幅替代本轮实际收益。图见 `analysis/source-prefix-width.svg`。
- 第一段末态的新鲜四通道固定输出采集与独审通过，原始块、整数输出计划、
  原生末态模型和全部批次完成状态一致。相对 NO1 的三从条件频差约为
  [−361,−275]/[−403,−301]/[−453,−345] ppb，仍偏慢，未锁相。
  这些界依赖采样单元和定频模型，不是已标定模拟误差界或绝对相位证明。
- 保持相同固件和已采用 DCO 的第二段原生采集独审通过：165 次 MATCH、24 条
  决定，三从实际更新 0/0/1 次，末态 1730/1909/4323 ppb；初态确实继承。
  NO1 启动前查询为 1843 ppb，首成功编码前改为 1694 ppb，此后至 STOP
  已发布模型不变。源前缀约 141.04 ms；同输入上限 8/64 均宽为
  833.95/546.32 ns，尾部 12 条为 793.33/375 ns，全部实际编码精确复现。
- 第二段新鲜固定模型波形与末态绑定独审通过，三从条件频差约为
  [−174,−95]/[−222,−156]/[−276,−172] ppb。相比第一段更接近零，但 NO1
  末态降低 149 ppb，三从仅 NO4 增加 20 ppb，不能解释为三从均主动继续
  收敛，也不作为跨初态的上限调整 A/B 收益证明。两段原始波形分别保留。
  所有采集运行零查询，四板及示波器最终 STOP、会话清空、PIO/DMA idle 已读回。
- 下一 gate：第二段 NO2/NO3 首组基线 62/64 的源区间宽 1647/1595 ns，
  保留到约 8 秒档；该档远端差分宽 2978/2926 ns、本地均宽 1 ns，
  仍跨零保持。后续源前缀尾部均值已降至 375 ns，但不会追改旧基线。
  两板末次决定已换成基线 8062，远端差分均宽 1738 ns，不归因于首组基线。
  8062 同时是上一组末次当前事件，由差分宽减去已知基线宽，两板均得到
  1331 ns；作为下一组基线后，末次宽度分解为 1331+407 ns。这只恢复宽度
  贡献，没有凭空补出缺失的 MATCH 或绝对端点。
- `analysis/baseline-width-study.json` 对同模型已记录 MATCH 做有限反事实：
  改用约 200 ms 后宽 375 ns 的基线 262/264，对同一约 4 秒处的当前 MATCH，
  NO2 频差界从 [−336,134] 变为 [−224,−44] ppb，NO3 从 [−355,102] 变为
  [−255,−75] ppb。局部差分保留两层真实整数取整，再与绝对端点差求交；
  20 次独立 Fraction 结果与真实 C `vdc_model_project_event_delta` 一致。
  这是同一记录对的候选估计，不是缺失 DECISION 的重建，也不预测改策略后的
  实际控制轨迹。下一切片据此验证有界 DPLL 基线选择/保留策略；不重新加入
  TDMA 首帧优化或重复校准前置。本轮未修改该策略，未消除漂移，ACK、
  实时模型输出、100 ns 锁相及一致 VDC 发布继续保持未完成。

### VDC-PROGRESS-20260917-023：同窗源记录核对与远端区间收窄候选

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-origin-follow-window-r1/`；以下数字均为
  实测或离线重算快照，非产品事实源或精度契约。本切片没有修改生产实现，沿用
  进度 022 的 build `20260917091644` 和源码指纹
  `0efc73091de851f479dfacf9016e1997292b7ef15ff959f20a4343fa1f96d164`；
  采集前核验既有同源码四板 P3 凭证，不冒充新构建或新 P3。
- 复用 schema 2 ORIGIN 和三从 MATCH/DECISION；新代际下四板 ARM ACK 均在
  START 前完成核对，有限静默采集期间零查询，全部 STOP 后分页读取、CRC
  复算并释放。流程约 29 秒；采集工具相关 host 34 项及两次无硬件预检通过。
  独审确认 165 次 MATCH、29 条决定；NO2/NO3/NO4 实际更新 2/1/3 次，末态
  1822/1963/4448 ppb，逐条与真实模型一致。四板及示波器最终停止状态另行读回。
- NO1 原生池满即冻结，共 76 条成功编码，源事件 69 至 198，覆盖约 129.04 ms。
  三从各有一个同源事件可精确核对，其远端区间与源编码一致；没有完整控制
  决定的两个源端点同时落在前缀内。因此不把前缀当成全窗逐事件运输证明，
  不据此重建所有决定的源端区间。逐板导出绑定与 19 项误绑定拒绝检查通过，
  原页/CRC/ACK/STOP 与对齐报告经独审核验。
- 更正参考变化解释：本轮 START 前与 STOP 后查询为 token 80→120、DCO 序号
  653→693、1905→1857 ppb，但全部 ORIGIN 均为 token 120/序号 693/1857 ppb。
  首成功源事件发生于终态模型发布约 22.76 ms 后；同 boot/session 的已发布模型
  token 只增不回绕，首条与 STOP token 相同，证明两者之间没有新的成功发布模型。
  这不证明物理振荡器恒定或未发布的瞬态不存在，也不能外推旧轮的中间轨迹。
  进度 022 的起止 MODEL 差仅是含启动过程的查询包络，不足以证明整个跟随
  窗口内参考持续变化；不再将扩充全窗遥测作为下一项算法试验的前置。
- `analysis/new-prefix-width-study.json` 以原始桥接记录和严格区间重新计算：
  `VDC_CLOCK_MAPPING_MAX_CONSTRAINTS` 当前上限精确复现全部 76 条。
  上限候选 8/16/32/64 的平均编码宽度为 1063.79/702.37/587.37/554.58 ns，
  尾部 12 条均值为 965.33/600.33/435.33/371.33 ns；各候选逐事件区间嵌套，
  无矛盾及模型/时域重置，独立有理数复算一致。
  当前平均宽度分解为 latch 252.32 ns、映射歧义 811.47 ns；增加保留次数
  收窄的是映射交集，不能消除 latch 宽度或直接证明物理锁相。
- 缓存保存一组交集，上限控制继续保留多少次约束，不是对应长度的数组。
  下一 gate：保持当前跟随响应，单独验证映射保留上限候选；覆盖模型/时域
  重置、矛盾拒绝及历史 trace 解码兼容，再做 Release/资源、同源码四板
  quick P3、原生决定和绑定末态模型的新鲜示波器采集。离线收益尚未导入固件，
  不宣称实时耗时下降、跨零消除或外部精度改善。
  本轮无新波形；外部剩余漂移仍以进度 022 的两段独立波形为准。
  ACK、实时换模型、100 ns 锁相和一致 VDC 发布继续保持未完成。

### VDC-PROGRESS-20260917-022：外部反馈驱动的有界跟随响应试验

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-follow-response-r1/`；以下数字均为
  本轮试验快照，非产品事实源或物理精度契约。
- 在进度 021 的已采用模型上保持原四分之一响应，追加有限静默窗口建立基线。
  独审核验 165 次 MATCH、29 条决定；三从实际更新 1/1/5 次，末态
  1167/1361/3598 ppb，NO1 前后 1492→1384 ppb。NO2/NO3 的同一 baseline
  确实走到后档后更新，不能把早档跨零保持解释成未接收或未执行。
- 追加基线的新鲜固定模型四通道采集通过：相对 NO1 的三从条件频差约为
  [−477,−402]/[−442,−388]/[−682,−603] ppb，仍偏慢。
  与旧窗口的差异含参考变化及继续运行，不是控制增益的隔离 A/B 结论。
- 单因素候选只调整 `priority_follow_delta()` 的最近零界响应，保留原区间、
  窗口、deadband、单步和总限幅、生命周期、连续 Domain 提交及 NO1 PI。
  从四分之一改为二分之一；这是当前策略试验，代码为事实源。没有缩窄远端
  区间或用中点代替确定方向，也不改普通 AUTO/RefMem 路线的控制策略。
- 实际倍率关系为 `R_new=R*(1e9+current+delta)/(1e9+current)`，不能声称误差
  精确减半。host 核对当前试验频率范围的方向、截断、饱和及不越零响应；
  软件可表示的极低倍率只证明数值/限幅安全，不扩展为全配置稳定性保证。
  静止数学轨迹不是硬件收敛或锁相证据。
- 候选八模块 417 项通过，按独审建议补充显式截断/饱和断言后跟随模块 57 项通过；
  导入后三个相关模块 82 项通过。以上有重叠，不相加为不同测试数量。
  实现独审确认仅响应比例和对应测试变化；采集器仅适配 STOP 后的决定复算，
  不增加运行查询。保留旧采集器和基线原件。
- Release build `20260917091644`、源码指纹
  `0efc73091de851f479dfacf9016e1997292b7ef15ff959f20a4343fa1f96d164`
  通过双槽资源重建及同源码四板 quick P3。可用 RAM 33040 B、静态增量零，
  当前 persona 条件下 Core1 最大栈 2872/3072 B；链接调用图无新增边。
  P3 为 24 INFO/21 WARN，无 ERROR/FATAL；相对进度 021 新增 T0 residence
  质量失败、对应执行反馈及继承的 T3 矩阵质量告警。有效输入/运输仍通过，
  不将旧或新增质量失败抹去，也没有隔离证明告警由增益变化造成。
- 第一段原生独审 165 次 MATCH、40 条决定，三从实际更新 7/8/8 次；
  末态 609/729/3078 ppb，NO1 791→793 ppb。逐条核到二分之一规则的真实
  Domain 采用，末次 +54/+52/+93 ppb；新鲜四板固定输出批次均完成。
  外部条件频差约为 [−473,−403]/[−538,−458]/[−577,−518] ppb，仍偏慢。
- 保持候选固件与已采用 rate 再运行一个有限静默窗口，独审确认初态继承。
  第二段 165 次 MATCH、38 条决定，三从再更新 8/8/9 次，末态
  1718/1894/4317 ppb。NO1 起止查询 793→1905 ppb，三从新增
  +1109/+1165/+1239 ppb；查询包含启动过程，缺少同窗轨迹，不能分配
  具体因果或将累计 rate 增加直接解释为消除静态误差。
  NO2/NO3 末次区间 [−875,13]/[−979,85] ppb 跨零保持；NO4 仍更新 +33 ppb。
  第一段末次远端差分区间宽度为 1610/1610/1606 ns；第二段为
  1598/1598/1334 ns，本地差分宽度均为 1 ns，按各段原生记录分别绑定。
- 第二段绑定末态模型的固定输出也成功，三从相对 NO1 条件频差约为
  [−456,−402]/[−469,−388]/[−464,−357] ppb。两段分别核原始块、模型及整数
  计划，不能跨不同模型合并区间。所有频差界均依赖采样单元/定频假设，
  不包含已标定模拟时变误差；两段斜率仍未消除，未授予精度达成。
  四板与示波器已实际 STOP，会话清空、PIO/DMA idle，示波器无错误。
- 当时下一 gate（后由进度 023 承接）：先关联同窗 NO1 参考变化与从板误差，核远端编码宽度和参考
  响应延迟，再决定下一单因素调整；不盲目继续提高增益。
  当前三段记录没有 NO1 的中间 rate 轨迹，起止 MODEL 不能补齐缺失样本。
  优先复用既有 schema 2 ORIGIN 的 rate/DCO/事件/编码记录；其有限容量满即冻结，
  先核同窗短前缀的实际覆盖，不能声称覆盖整个跟随窗口或先新增遥测。
  三栏原始边沿漂移图见本根 `analysis/relative-phase-three-runs.svg`，各栏按
  自己的模型独立归零；不同参考/初态，不作为受控 A/B 或绝对相位证明。
  不将 quick P3、频率方向或静止模型输出当成
  ACK、实时换模型、绝对相位、100 ns 锁相或一致 VDC 发布完成。
  代码已提交 `d365f2e`，真实 pre-commit 已核暂存源码对应 P3 凭证。

### VDC-PROGRESS-20260917-021：固定输出快照准入与轻量时钟资格

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。本切片恢复用于算法纠偏的
  外部固定模型观测，不修改三从控制律。证据分别保留在
  `out/HardwareAcceptance/20260917/dpll-fixed-start-r1/`、
  `out/HardwareAcceptance/20260917/dpll-fixed-start-r2/` 与
  `out/HardwareAcceptance/20260917/dpll-fixed-start-r3/`；以下数字为实测快照，
  非产品事实源。
- Core0 的初始准入、PIO 启用前复验及启用后复验仅对明确的 ring/model
  快照暂忙重读。每阶段采用 `VDC_FIXED_OUTPUT_SNAPSHOT_ATTEMPTS` 次数上限，
  首次完整观察不设新增墙钟有效期；首次明确暂忙后才启动
  `VDC_FIXED_OUTPUT_SNAPSHOT_BUDGET_US`，仅判断能否发起下一次观察。
  已发起观察若完整证明有效，不因耗时而反转为失败；
  真正的模型、会话、时钟、自检或运行状态变化仍拒绝。后两阶段逐字绑定
  原模型，不重发硬件启动；运行 service、STOP 与状态读取行为保持。
  原原因码与固定状态字段保持兼容，新原因码区分各阶段暂忙、超时及调用上下文。
- 只读检查 ARM 中断/异常上下文，调用方屏蔽中断时拒绝，不替调用方开启中断。
  重读属于 Core0 诊断，不进入 Core1、RTOS 等待或普通负载搬运。重读预算不是
  被 IRQ 抢占时的硬 WCET，也不能保证启用后被拒绝的批次从未发出首沿。
- r1 软件、Release/资源、四板 quick P3 与原生专项通过，但外部采集未通过：
  首次 NO2、第二次 NO3 均为 PRE_DEADLINE；第二次 NO2 完成批次，NO4 STOP 后
  模型读取另有超时。原因码细分是真实收益，但不能因此声称恢复可靠采集。
  四板及示波器终态已独立读回；失败和原生记录保留。
  r1 原生独审为 165 次 MATCH、36 条决定，三从实际更新 8/9/10 次；不是锁相。
- r1 的 fixed clock 准入每轮读取整份 diagnostic，但实际只使用
  `configuration_supported && clock_ready`。r2 新增
  `vdc_timestamp_clock_configuration_supported()`，直接复用原 private 配置谓词，
  保留 ready acquire、平台、时钟源、PLL/XOSC、分频、timer/debug 等资格；
  不采集未被 fixed 准入使用的桥接时间戳。原桥接和诊断函数保持原义，
  不缓存资格、不初始化硬件、不改变时钟或重读门限。
  源级成功路径约从 111 次 MMIO 降到 24 次，仅为访问计数，不是实测耗时或根因证明。
- r2 候选及导入后合计 175 项相关 host 通过，覆盖静态配置等价、非法配置、
  不支持的平台、只读与无初始化、计数器采样失败、快照恢复/耗尽、时限边界、
  会话/模型切换及失败释放；r1 另有模型 owner 10 项通过。
  实现和设计独审分别保留在两根 `design-review/`。
- r2 Release 两槽资源重建通过：可用主 RAM 33040 B、静态增量零，当前 persona
  条件下 Core1 栈最大 2872/3072 B、scratch 余量 24 B；轻配置函数位于 XIP，
  未进入 Core1 调用图。资源审计修正了同名 local 符号按名称覆盖造成的误报，
  多重集与链接 map 相符；不将这些资源数字当作 WCET 或全负载证明。
- r2 同源码四板 quick P3 通过，25 INFO/18 WARN，无 ERROR/FATAL；原生专项
  独审为 165 次 MATCH、31 条决定，三从实际采用 9/10/10 次，末态模型一致。
  但首次外部复采 NO2 即 INITIAL_DEADLINE、request=0，未启动硬件；
  无有效四通道波形。证据不能区分该次具体读前/读后超时或证明特定抢占根因。
- r1/r2 曾把整段观察墙钟耗时加入准入，可拒绝本来有效的完整观察。
  r3 改为上述额外重读准入预算，保留失败原件；候选和导入后各 178 项 host
  通过，独审确认只有 validator 行为变化，轻量 clock 与运行清理保持。
  覆盖有效首读/有效重读跨预算、未知观察耗尽后不再发起读取及真实失效拒绝。
- r3 Release build `20260917084807`、源码指纹
  `c140d1ca1170d6373939b2fb03db10300843d059efaa91f0f7a67e70e5b68a06`
  已完成资源与同源码四板 quick P3：资源与 r2 相同，P3 为 25 INFO/18 WARN，
  无 ERROR/FATAL，WARN 集合与 r2 相同。原生独审 166 次 MATCH、33 条决定，
  三从各实际更新 10 次，末态 rate 为 1134/1281/3422 ppb，模型与原件一致。
  本轮只修诊断准入，不以采用次数或不同初始状态的运行比较控制律优劣。
- `scope-fixed-r1/r2` 连续两次新鲜四通道固定模型采集成功，四板每次均完成
  2048 脉冲，模型不变，PIO/DMA idle，运行查询为零；每轮约 30 秒。
  两轮全边沿条件模型均可行：相对 NO1 的 NO2/NO3/NO4 频差分别约为
  [−638,−559]/[−669,−592]/[−904,−831] ppb 和
  [−652,−541]/[−668,−534]/[−960,−849] ppb。独立复核原始块及整数计划，
  这是固定批次的剩余频差条件区间，不是已校准误差界、绝对相位或实时锁相。
  三从仍偏慢，不能把更新成功或内部区间跨零解释成无需调节。
- 本轮 NO1 原生窗口前后 rate 为 1625→1492 ppb，内部参考并非已证明恒定；
  旧轮与本轮的初始模型、温漂和参考轨迹未受控，不能将外部差异归因为诊断修复。
  外部结果只提供模型绑定的离线校验方向，不直接把区间中点写回 DCO。
  三从末次实际修正仍为 +32/+46/+36 ppb，方向与后续外部偏慢相容；
  由 `priority_follow_delta()` 对区间最近零边界采用当前响应比例得出，
  不是外部剩余频差的固定比例，也没有证据说明 DCO 符号算反。
  末次远端差分区间宽度为 1170/1158/1170 ns，本地宽度均为 1 ns。
  优先继续核验远端宽度与保守响应，但这不是已证明的唯一剩余频差来源。
  `final-stopped-state.json` 已实际读回四板 RING/PWM/PIO/DMA 停止、会话为零，
  示波器 STOP/EXT/NORM、错误队列为空。
- 下一 gate：保持已恢复的外部采集，继续以外部相位斜率对照内部估计及实际
  DCO 步幅，区分远端编码不确定性、参考变化和保守响应，每次只改一个算法因素。
  候选仅调整最近零边界的响应比例，须独立测试与四板 P3/专项；尚未实施。
  两轮原始边沿的独立归零漂移图见 r3 根 `analysis/relative-phase-drift.svg`，
  图中斜率仅作描述，不能代替全边沿约束或绝对相位证明。
  代码切片已提交 `51d4333`，暂存源码指纹由真实 pre-commit P3 门禁核验。
  实时换模型、绝对相位、接收/采用 ACK、100 ns 锁相和一致发布仍未完成。

### VDC-PROGRESS-20260917-020：外部全边沿约束与采集启动拒绝

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-external-feedback-r2/`；以下数字均为
  本次分析快照，非产品事实源。生产固件、控制增益和契约未修改。
- 用户确认用示波器真实反馈纠偏算法。外部数据用于离线核验估计方向、响应及
  剩余漂移；板上 NO1 PI 与三从本地跟踪继续走 Core1 确定性路径。SCPI 仅控制
  有界采集，运行中不查询，全部 STOP 后导出；不引入示波器在线依赖。
- `external-review/all-edge-review.json` 独立校验进度 019 成功固定输出的四路
  原始块、模型及累计 PIO 整数计划，并对每路保留的全部边沿求周期约束。
  使用原始 ADC 过阈值相邻采样单元与累计取整误差，对所有边沿对取交集；
  拟合仅用于描述，不作为确定性误差界。各阈值和未建模误差分别保留。
  条件交集下 NO2/NO3/NO4 相对 NO1 分别约为 [−237.31,−150.00]、
  [−111.67,−18.75]、[−376.08,−294.50] ppb，方向均为增大 rate。
  这是旧采集的新离线分析，不是本轮取得的新波形。
- 上述条件界不含已标定的模拟时变延迟、采样孔径或温漂界。灵敏度分析中，
  仅额外假设每沿 ±1 ns 误差，NO3 的方向即未决；NO2 在假设 ±5 ns、NO4
  在假设 ±10 ns 下仍偏慢。这些是假设预算，不是仪器校准结果。
  若参考及振荡器保持不变，已采用 rate 为 c、实测频率比为 R 时，理想增量为
  `(1e9+c)*(1/R-1)`；不能将外部区间中点直接写入当前模型，或重复叠加已采用增益。
- 当前已部署 build 的 `scope-fixed-r1/r2` 均保留 FAIL：三从各完成固定批次，
  NO1 START 返回超时；第一轮末态 reason=4、尚无编码摘要，第二轮 reason=5、
  已有编码摘要及剩余 transfer_count=4087。两次分别定位到不同启动拒绝阶段，
  不合并为模型错误或示波器配置错误，不认定快照争用为已证根因。
  两次均未取得有效四通道波形，不能用来比较漂移变化。
- 原 `final-state` 检查要求 transfer_count 清零，因中止批次保留未消费计数而失败；
  原件保留。后继 `final-stopped-state.json` 实际读回四板 RING 停止、会话清空、
  PWM 关闭及 PIO/DMA idle；NO1 批次失败仍明确保留。示波器为 STOP/EXT/NORM、
  错误队列为空。硬件停止与批次成功分别判定，没有放宽波形采集成功条件。
- `origin-review/origin-review.json` 另行只读重放旧源端原件：未观察到 model token
  重置；增加有限约束数量可能缩小远端编码宽度，但尚无当前 build 的单因素验收，
  不改 token 生命周期，也未将该候选与本轮外部分析混入同一实现。
- 下一 gate：先细分固定输出启动拒绝、恢复可重复的新鲜四通道采集；随后绑定
  同一模型和观测窗口，对照内部区间、外部斜率与实际步幅，每次只改一个算法因素，
  经软件、资源、同源码四板 quick P3 及专项验证。绝对相位、实时换模型、100 ns
  锁相和一致 VDC 发布仍未完成；原严格质量告警继续保留。

### VDC-PROGRESS-20260917-019：连续事件共钟差分与外部反馈纠偏

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-external-correction-r1/`；以下数字均为
  本次证据快照，非产品事实源。
- 修改前同固件、同参数的 `native-r1` 通过：NO2/NO4 在后档实际调整
  +10/+25 ppb；NO3 最终后档仍为 [−501,+61] ppb，不能视为已同步。
  三从各有完整原生记录，全部决定经独立 CRC/分页、Fraction、生命周期和
  末态模型复核。NO3 此次本地差分宽 2001 ns、参考差分宽 2490 ns；单纯增加
  增益不能改变跨死区的零步准入。NO1 包围窗端点为 1829→1838 ppb，不能据此
  假定具体配对期间参考恒定。见 `native-review.json`、`control-review.json`。
- 此轮修改前 `scope-raw` 成功，`scope-fixed` 在启动请求返回超时后失败；
  原件保留，不能声称取得新的固定模型物理频差。该次清理后的停止状态见
  `final-board-scope-state-r2.json`，后继硬件验收状态需按各自终态另行核验。
- `VDC-PRIORITY-01` 修订为 v8 pending，明确 typed 两端共同指向
  `F(floor(X(event)))`。新增 `vdc_model_project_event_delta` 在同模型、共同
  observer 起点及已准入的共钟生命周期内抵消公共偏移，保留先本地整数 ns、
  再 DCO 缩放的两层外向取整；继续与原绝对端点差求交，空交集仍拒绝。
  旧 helper、微秒阶梯余量及旧测试保持原义；远端区间、增益、死区和调度不变。
- 候选经隔离工作区验证后逐文件核验导入：原有 425 项 host 与新增 7 项通过，
  新套件含 14457 组真实 Domain/Fraction 数值例；非整数 ns 时钟必须保留内层
  取整的反例已纳入。数学、实现与 C11 独审分别见 `continuous-delta-review.json`、
  `implementation-review.json`、`contract-review.json`。
- Release 两槽资源重新分析通过：静态 RAM 增量为零，余量 33040 B；当前 persona
  的 Core1 栈仍为 2872/3072 B、scratch 余量 24 B。候选在本次时钟下的本地差分
  算术宽度至多 1 ns，不是物理精度或 WCET 证明。
- 代码已提交 `2d4656e`（`feat(vdc): use common-clock continuous event deltas for typed control`），
  staged 指纹经真实 pre-commit 与四板凭证核验通过，文档分离提交。
- 同源码四板 quick P3 为 `PASS_WITH_WARNINGS`，23 INFO、22 WARN、零 ERROR/FATAL；
  build `20260917062327`，源码指纹
  `304cbe74baf3737b42e2ce67baed602faf7d5b79bd2faeefda9cd46217a1f906`。
  包、两槽 ELF、四板 OTA 及原生短帧经独审一致。相较上一切片警告净增六项：
  T1 SCK 候选覆盖和 T3 replay 余量分别新增质量/执行告警，NO4 VDC 新增超时、
  隔离及 max-runtime 告警，NO1 RefMem deadline 少一项。NO4 VDC 最大值由
  188→408.608 us，quarantine 由 8→9；是真实新增质量劣化，尚无单变量因果证据，
  不称为“基线不变”或无回归。严格质量仍未通过，见 `p3-review.json`。
- 部署后 `native-candidate-r1` 通过：三从 165 条 MATCH、35 条决定独立 CRC/Fraction
  重放通过，每条本地差分宽度为 0–1 ns，远端上下界保持原件；三从各有十次真实
  DCO 更新，末态为 1348/1568/3715 ppb。OTA 后从零修正起步，不能用更新次数
  相较修改前的变化推断收敛速度；频率/物理效果仍按各自窗口验证。
  见 `native-candidate-review.json`、`native-comparison.json`。
- `sustained-candidate-r1` 的后继静默持续窗口通过，末态为 NO1/NO2/NO3/NO4
  1919/1920/2204/4424 ppb；NO2/NO3 末次实际调整 +20/+39 ppb，NO4 末次未决。
  原件独审确认本轮请求重置计数，DCO 序号差分为三从实际新增 21/23/21 次更新；
  末匹配距末接收约 4.81/4.18/4.14 ms，无 observer 溢出，STOP 模型一致。
  见 `sustained-candidate-review.json`。
  此处证明持续输入及真实控制仍可运行，不授予锁相、全负载 VDC 或发布完成。
- `scope-raw-candidate/scope-fixed-candidate` 成功取得新 SINGLE 波形，采用上述
  末态模型。主控在相同电压阈值下拟合 NO2/NO3/NO4 相对 NO1 为约
  −196/−73/−338 ppb；拟合仍有剩余漂移。采样/取整条件界与所采用修正的预测
  相容，但两次有限采集不是同时观测，不能忽略温漂或把小频差估计当作精度认证。
  图为 `scope-fixed-candidate/physical-dco-comparison.svg`；它展示有限固定模型
  执行效果，不证明运行中模型切换、绝对相位或 100 ns 锁相。
  `physical-review.json` 独立复算与该拟合相容；NO2/NO3 的小修正增益方向未被
  条件界单独分辨，NO4 正向修正可分辨。示波器仅覆盖约 0.2 s，不能把板端完整
  批次计数当成全部脉冲均被外部观测。NO3 末次内部区间与后继外部拟合点并非
  同一时间窗或同一采用前模型，不能声称该内部区间包含外部点或据此拟合校准常量。
- 固定输出采集工具仅追加失败后的诊断：必须四板 output/RING STOP 都实际返回
  OK，才在清空会话前读状态；不重发 START，不放宽成功判据，见
  `scope-capture-review.json`。此前失败是本轮 NO1 MODEL 拒绝，和上一轮 NO2
  资源/参数拒绝分开记录；单次快照争用仍只是候选解释，不能归咎示波器配置。
  最新 `final-board-scope-state-final.json` 已确认四板全部停止，示波器
  STOP/EXT/NORM、错误队列为空。
- 下一 gate：对照物理剩余漂移、远端区间及 NO1 参考轨迹，选择下一个单因素
  频率收敛切片；接收/采用 ACK、绝对相位、失联恢复、一致发布和严格调度质量
  仍未完成。不重复实现已经验证的本地共钟差分，也不把实际 0–1 ns 算术宽度
  改写成跨板物理精度。

### VDC-PROGRESS-20260917-018：有界长窗口产生真实后档 DCO 调整

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-longwindow-adoption-r1/`；以下实测数字
  为本次快照，非产品事实源。此前隔离的长窗口候选现已导入当前主线并验证，
  不再把它列为未部署候选。
- 代码已提交 `0e7cede`（`feat(vdc): extend bounded follower frequency observation windows`），
  staged 源码指纹与四板凭证由真实 pre-commit 核验通过；文档分离提交。
- `VDC_PRIORITY_FOLLOW_*_INTERVAL_NS` 在原有早期机会后增加有界后档；
  `VDC_PRIORITY_FOLLOW_MAX_EVALUATIONS` 限定每个基线的计算次数。
  只在误差区间仍无法支持调整时保留同一本地模型基线；最新票据年龄、完整事件
  与会话/模型/观察器锚点检查不变，STOP、换模型、过期、错误次序和无效区间
  仍取消或重建。远端只提供两个已准入端点，不声称得到 NO1 瞬时频率。
- `vdc_model_scale_elapsed_ns()` 用商余拆乘和外向取整避免较长间隔乘法溢出；
  `priority_follow_rate_interval()` 在无符号域扣除偏置后才转换，保留完整量化界
  与已准入绝对端点差的求交。没有放宽单事件绝对投影年龄，也未修改增益或死区。
  五档跳过不补算、BUSY/票据替换不退还机会。数学/生命周期独审见
  `design-code-review.json`，425 项 host 通过。
- Release 两槽独立资源复核通过：无新增静态主 RAM，余量仍为 33040 B；
  当前 persona 的 Core1 深链重新分析为 2872/3072 B。新阈值表位于 Flash，
  工作区与诊断对象未扩容。此为链接资源证明，不是 WCET 或全负载证明。
- 同源码四板 quick P3 为 `PASS_WITH_WARNINGS`，25 INFO、16 WARN、零 ERROR/FATAL，
  严格实时质量仍未通过。源码指纹
  `4a749af74b7aa84fb0238ea437703a3ca4d1a2f06c93a2c7df05231c4a6f3e5e`，
  build `20260917054056`；包、两槽 ELF、四板 OTA、原生短帧与分级凭证独立一致，
  见 `p3-review.json`。已测线序和 TAP 复用，未改 OTA、未操作 NO5。
- `sustained-r1` 保留 FAIL：三从 observer 无故障且末段匹配有效，但 NO2/NO3
  STOP 后模型读取超时，不能用后继数据补填。该轮 NO4 末决定在约 4 s 窗口
  得到 [−1315,−550] ppb，实际调整 +137 ppb，DCO 序号与真实模型一致。
  同参数 `sustained-r2` 完整通过，三从均无 observer 故障、末态模型一致；
  长档等待期间允许最后决定早于最后接收，不能将其误判为断流。
  VDC quarantine 仍存在，不声称完整 VDC 调度恢复，见 `sustained-review.json`。
- 补充 `sustained-r3` 保留 FAIL：NO4、NO3 START 成功后，NO2 START 仅返回
  超时占位 ACK，NO1 尚未 START；没有真实状态读回或有效持续窗口，随后四板
  STOP。`sustained-r4` 完整通过，约 40.297 s 内三从末段匹配、模型一致且
  observer 无故障；NO4 在 8.000475282 s 得到 [−566,−122] ppb，实际 +30 ppb，
  DCO 序号 43→44。NO2/NO3 末次仍为 4 s 未决区间，不冒称最终后档失败。
  见 `sustained-followup-review.json`。
- `native-r1` 的静默原生专项通过：三从各 63 条记录，其中 55 MATCH、8 DECISION，
  跨度约 10.94 s，无 dropped/observer 故障；全部 24 条决定的分数运算、实际
  DCO 序号、残差、生命周期及末态模型经独立原件 CRC/分页重组复核。
  NO3 同一 baseline 65 实际经过 1/1.5/1.8/4/8 s 五档；最后一档约 8.002 s，
  区间 [−813,−213] ppb 支持真实 +53 ppb 调整。NO2/NO4 在约 4 s 分别调整
  +3/+10 ppb。此处证明原来未决的观察可经后档产生真实调整，不宣称锁相。
- `capture_longwindow.py` 复用既有 STOP、CRC、会话及有效记录判据，保留有限
  临时许可，运行期间不查询；4/8 s 档是否自然出现只作事实记录。容量规划是
  典型估算，不是最坏上界，满容量与丢记录仍失败。原生决定未携带每条 raw
  端点，因此全部记录复算区间/量化宽度，只有末条利用 STOP 快照完整复算
  raw 差分投影；不虚构缺失历史。见 `capture-review.json`、`native-review.json`。
- 第一组 `scope-raw/scope-fixed` 成功，采用模型 NO1/NO2/NO3/NO4 为
  +1676/+1416/+1864/+4016 ppb；固定输出相对 NO1 的频差拟合约
  −456/−324/−536 ppb，与采用修正的预测相容。相较进度 017 的拟合幅度下降，
  但 NO1 模型、温度和观测时刻不同，不作为单变量因果或连续收敛证明。
  固定模型输出仍不能证明实时换模型、绝对相位或 100 ns 同步。
- 补充物理采集未完成：`scope-raw-r2` 成功，`scope-fixed-r2` 因 NO4
  `model_unchanged=2` 失败，该值表示模型快照读取争用，不是已证模型改变；
  `scope-fixed-r3` 在 NO2 固定输出请求超时后失败。两次失败及清理原件保留，
  不用相邻模型字段相等追认采集，不宣称追加运行后的物理漂移继续下降。
  最新 `final-board-scope-state-r2.json` 确认四板 RING、PIO/DMA、PWM 和会话
  均停止，示波器 STOP/EXT/NORM、错误队列为空。
- `next-step-review.json` 复核保守步幅为靠零端点的四分之一，实际采用后重建
  基线。NO1 在原生与后继持续观测包围窗内分别由 1641→1676、1676→1829 ppb，
  但缺少具体配对窗口内的参考轨迹。不能把剩余频差仅归因于增益或量化。
- 下一 gate：继续 `VDC-FAST-003`，对照后档未决区间、NO1 参考变化及外部剩余
  漂移，确认后续频率收敛；随后补齐接收/采用 ACK、相位精度和一致发布。
  示波器外部反馈用于核验实际输出误差符号、调节方向和响应；同一模型/事件
  窗口绑定内部区间与物理相位斜率后，先区分最终档仍跨死区的分辨率问题、
  稳定参考下连续同号小步的响应问题以及参考变化，再选择单一纠偏切片。
  采样配置与 STOP 后导出留在调试侧，Core1 保持确定性处理；有限固定输出
  波形只支持执行效果核验，不能替代运行中闭环轨迹或绝对相位验收。
  不移除量化界或以区间中点伪造确定性。VDC 隔离和 observer 服务预算独立
  跟踪，不将当前通过扩大为全负载运行。

### VDC-PROGRESS-20260917-017：调度入口迁入 SRAM 与外部剩余漂移复测

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260917/dpll-observer-backlog-r1/`；下列数字均为本轮
  证据快照，非产品事实源。此次仅改变 `app_realtime_run_phase()`、
  `app_priority_counts()`、`app_priority_deadline()` 的主 SRAM 放置，不改变
  控制算法、静态表预算、迟到准入、隔离规则、DMA 容量或估计窗口。
- `baseline-sustained` 在无原生 trace 的静默窗口仍失败：三从 observer DMA
  积压达到 66/74/78 字，最大服务间隔 22.416/30.060/38.854 ms，末次匹配比
  末端接收早约 35/37/29 s；不能把末态 DCO 当作全窗持续跟踪。只排除“必须开启
  trace 才会失败”，并未证明唯一根因。结构复核见 `event-review.json`。
- 实际生产调度边界等 host 回归 158 项通过，Release 两槽链接与独立栈复核通过。
  迁移代码节为 3028 B，计入对齐后主 RAM 实际增加 4096 B，扣最小堆后余
  33040 B；当前启用 persona 的 Core1 栈分析仍为 2872/3072 B，scratch 余量
  24 B 不变。作用范围和排除项见 `code-review.json`、`resource-review.json`。
- 新源码四板 quick P3 为 `PASS_WITH_WARNINGS`：25 INFO、18 WARN、零 ERROR/FATAL，
  严格调度仍未通过；原始失败不改写。源码指纹
  `493ab59cd83fca6fe65e844c739281aae597ebf6e25e77cc8d9ef23cbb346e15`，
  build `20260917051203`；包、两槽 ELF、四板 OTA、原生记录及凭证独审一致，
  见 `p3-review.json`。代码与匹配凭证已独立提交 `ec9b0b0`，真实 pre-commit
  核验通过。不重复测 P0T，复用已确认线序和 TAP 输入。
- `after-sustained-r1` 在 NO4 START 时因 ACK 不匹配退出，未形成观测窗口；
  四板 STOP 原件保留。返回文字中的“verified by state readback”是现有工具
  在 timeout 时生成的占位说明，不是本轮实际读回证明；不能用计划时长或包装层
  零查询计数追认该失败。后继两轮沿用原参数与原判据，未放宽 ACK。
- `after-sustained-r2/r3` 均通过：三从 observer 无故障，最大积压为
  8–10 字、最大服务间隔约 4.1–4.5 ms，末段匹配与末次真实 DCO 决定均有效，
  停止因果为 STOP。主机命令包围时长分别约 44.094/40.328 s，名义采集等待
  均为 40 s；不将端点证据解释成连续残差轨迹。首次失败及两轮比较见
  `review-comparison-final.json`。
- 对照限制：基线 quarantine 为 8，候选为 9；候选额外隔离了超时 VDC，
  第二轮 VDC run_count 不再增加。两种固件的 ARM-lifetime VDC 背景最大值均为零，
  该字段不是调用次数，不能证明实际负载相同。可保留本切片用于 TDMA/DPLL 调试，
  不能独归因于 XIP，也不能宣称 VDC 调度恢复或完整预算通过。observer 自身
  批次耗时仍可能超过 VDC/DPLL 相位预算，此项独立保留。
- `scope-raw` 与 `scope-fixed` 成功，四板固定输出均完成全部脉冲，模型前后一致，
  四通道由 NO1/CH1 正沿新单次触发，静默采集后统一读回。实际采用频率为
  NO1/NO2/NO3/NO4 的 +2778/+2164/+2635/+4686 ppb；相对 NO1 输出频差拟合约
  −822/−696/−980 ppb，与采用修正的预测相容。两类波形属于先后采集，温漂和
  模型差异保留；不能将相较进度 016 的变化独归 SRAM 或宣布 100 ns 锁相。
  见 `scope-fixed/analysis.json`、`scope-fixed/physical-dco-comparison.svg`。
  最终四板 STOP、反馈会话归零，示波器 EXT/NORM/STOP 已实际读回，见
  `final-board-scope-state.json`。
- 下一 gate：继续 `VDC-FAST-003`，将真实输出剩余漂移与内部区间、最终档位
  保持原因联合复核，再评估隔离且未部署的长窗口候选。VDC 隔离和 observer
  服务成本另作有界切片，不能用扩大环或放宽准入掩盖；备用细分诊断仅完成
  `diagnostic-design.json` 设计，尚未实现。固定模型输出不替代实时采用、绝对
  相位、ACK、恢复和一致 VDC 发布。

### VDC-PROGRESS-20260917-016：固定已采用模型的连续物理输出

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。本切片按用户要求用外部示波器反馈
  校核 DPLL，证据根为 `out/HardwareAcceptance/20260917/dpll-fixed-output-r1/`。
  主控原件/指纹复核入口为 `main-summary.json`，最终硬件状态为 `final-board-scope-state.json`。
  代码、测试及匹配四板 P3 凭证已提交 `d2021b8`，真实 pre-commit 核验通过；文档分离提交。
  下列测试、容量与时间均为证据快照，非产品事实源；此处不授予锁相或 VDC 发布完成。
- 新增 `vdc_dpll_manager_fixed_output_start()`：只在 Core0、TDMA 已停止且当前非零会话
  对应真实 committed model 时准入。启动前后复验模型与时源，不退回本地裸时钟；正常
  seqlock 争用保留批次，实际模型或运行上下文改变则取消。SCPI `SYSTem:VDC:OUTPut:FIXed`
  只配置触发有限批次，查询保留所采用的模型身份、编码参数、硬件终态与完成计数。
- `sync_io_rate_schedule_generate()` 对累计边沿执行逆频率量化，避免逐周期取整吞掉小
  修正。复用现有 PIO0 scheduled-trigger persona、DMA 和共享工作区，有限批次由硬件
  连续执行，无逐沿 Core0 重装。真正完成还要求最后下降沿后的 PIO PC、DMA 与 FIFO
  共同满足终态；不能以 FIFO 空单独宣布完成。固定模型输出不自动采用后来模型，实时
  换模型及绝对相位另验。
- 独审发现并修复三项：正常模型读争用误取消、准备期旧接口夺取共享工作区、新失败
  请求混入旧批次统计。准备期抢占的红测试保留；最终独立资源/取消测试及相关综合
  回归通过，详见 `test/coverage.json`、`host-final.xml`、`review/code-host-final-review.json`。
  Release 与链接资源复核通过；新增静态 RAM 为 344 B，已启用 Core1 栈上界仍为
  2872/3072 B，数字仅为本次链接快照。通用运行状态 getter 不读取固定输出的 Core0
  缓存，也不在 Core1 分配其完整状态对象。
- 首轮四板 quick P3 为 `PASS_WITH_WARNINGS`，严格质量未通过；后继 `native-r1`
  整体失败保留。三从都有 typed 参考输入；NO2/NO3 的 observer DMA 环覆盖使观测
  失效、跟踪取消，不能把零调整解释为无需调整。NO4 原生决定与真实 DCO 一致，
  但记录跨度不足专项判据；固定输出当时未启用，不据此推定该新增输出路径的因果。
- `raw-r1` 已取得四通道原始时钟。`physical-r1` 在 ARM 前因新增 STOP handler 未
  输出成功 ACK 而超时，保留失败原件。真实 handler 红测试复现；修复为既有
  `scpi_port_result_ok()`，修复后相关 host 75 项通过。新源码 `p3-r2` 为
  `PASS_WITH_WARNINGS`，无 ERROR/FATAL；严格启动屏障告警保留。增量 build 字符串
  沿用缓存，实际版本由源码 `a07aa4e055f389019e81f011f5d199d73d285d1a27072c233ef6976f6cee770f`、
  package、四板 OTA 凭证与新 ELF 联合绑定；独审已逐字节核对包内两个应用与 ELF。
- `native-r2` 三从均有真实 DCO 更新，全部决定与末态模型一致；NO4 后段仍发生
  observer DMA 覆盖，整体专项 FAIL 保留，不能追认为持续跟踪通过。
  `physical-r2/r3` 分别在 NO3/NO2 启动准入被拒绝；状态为资源或参数拒绝，现有
  原件不能区分快照争用与资源，不能宣称根因已证。STOP、资源表和失败原件保留。
- 有界复试 `physical-r4` 成功：四板固定模型身份前后一致、各完成全部 2048 脉冲，
  DMA/FIFO/末沿终态及全部 STOP ACK 核验通过。与 `raw-r2` 的完整 DCO 字段相同，
  两轮均为 NO1/CH1 正沿新单次触发、四通道同帧 RAW、采集期间零查询。最终停止
  输出、反馈会话归零，示波器恢复 EXT/NORM/STOP。
- 外部快照（拟合值，非精度契约）：NO2/NO3/NO4 原始相对频差约 −223/−527/−2860 ppb；
  固定 DCO 实际输出约 −2409/−2144/−4686 ppb。NO1/NO2/NO3/NO4 当时采用
  +3786/+1598/+2096/+1984 ppb；实际变化与原始频差乘模型频率比例的预测相容，
  确认本批次执行方向和幅度，并未消除相对漂移。不能把原始 PWM 再加一次修正当成
  实测，也不能从当前差值单独裁定 PI 增益或长期稳态；原始与固定采样分轮，温漂等
  系统误差尚未完全计入。图与分析在 `physical-r4/physical-dco-comparison.svg`、
  `physical-r4/analysis.json`；独立多阈值/分段重算与完整原件绑定通过，见
  `physical-r4/independent-final-review.json`。首次离线分析的 NumPy 布尔序列化失败亦保留。
- 裸 PWM 不包含 NO1 的正修正，不能从裸时钟→固定输出的相对负频差增大推定从板
  调反。以此次 NO1 修正不变、从板修正仍为零作离线反事实，预计三从起点约为
  −4.0/−4.3/−6.6 ppm，现有从板正修正会缩小这部分差值；此起点是模型推算，
  不是同时实测。原生窗口中的已留存 NO1 模型均为相同频率修正，尚无该窗口内
  参考持续变化证据。短时运行、保守步长与量化区间、NO4 观测中断均须分开评估。
- 下一 gate：用已闭合的外部频率观测对照 NO1 参考和三从采用轨迹，
  定位跟随滞后、宽区间未决与 observer 服务积压，再评估隔离的有界长窗口候选；
  不直接用此次静态差值写校正值或放宽量化界。实时换模型、绝对相位、持续锁相和
  VDC 发布仍待验收；observer 连续性失败不扩大为固定模型输出的新增锁相门禁。

### VDC-PROGRESS-20260917-015：外部原始频差与现有 DCO 修正对照

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。用户授权以真实示波器反馈纠偏。
  已用现有固件的连续硬件 PWM、NO1/CH1 正沿触发取得四路同帧冻结 RAW，证据为
  `out/HardwareAcceptance/20260917/dpll-scope-raw-r2/`。采样期间零查询，全部输出停止后
  导出；四板时源配置与模型前后一致，原始块、脚本和独立复算均保留。
- 以下为本轮快照，非物理精度事实源：原始频差相对 NO1，NO2/NO3/NO4 约为
  −235/−430/−2867 ppb；计入当前模型后的推算残差约为 +1/−194/−1068 ppb。
  NO2 暂不调整有依据；NO3 的保守采样界仍包含零；NO4 的负剩余误差方向在保守
  采样界内仍明确，支持继续正向补偿。原始 PWM 不受 DCO 调节，此推算不是实际 DCO
  输出、绝对相位或锁相证明。
- 失败保留：首次请求低于既有硬件测频接口下限；扩大记录时水平偏移读回不符及未
  观察到新 WAIT 均拒绝，不把旧 STOP 波形当新采样。最终示波器恢复 EXT/NORM/STOP，
  四板停止、PWM 关闭。采用 NO1/CH1 触发是因为当前固件未启用 RJ45 单脉冲命令；
  用户 OUT4 到 EXT 的接线不代表该命令实际产生过触发。
- 有界长窗口候选已完成主机验证，隔离于 `out/worktrees/dpll-follow-longwindow`，未部署；
  为归因，当前主树继续采用已验收估计策略，先补固定模型的物理输出证据。

### VDC-PROGRESS-20260917-014：计数回绕伪歧义修复与持续跟踪

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。本切片修复实际中断 DPLL 输入的
  TDMA observer 缺陷，未调整 FOLLOW 增益、估计档位或真实量化界。以下数字均为本轮
  证据快照，非容量、时序或精度事实源；证据根为
  `out/HardwareAcceptance/20260917/dpll-observer-lift-r1/`。
- 代码、测试与匹配 P3 凭证已提交为 `061ddc5`；真实 pre-commit 已核验暂存源码。
- 修复前同固件 `dpll-priority-longwindow-r1/sustained-r3/` 运行期间零查询；
  NO2–NO4 原始 recovery 均记录 `TDMA_EVENT_LIFT_AMBIGUOUS`、fault bits 为零，
  有效事件在约 34.36 秒停止。该时刻对应 PIO 计数周期；临时许可和 observer
  有限期限尚未到期。原运行 FAIL 保留，不归因为 RJ45 接触或码元错误。
- 连续 DMA 不一定提供 FIFO-empty 见证，前事件 age 下界可长期停留于起点；旧算法
  随总运行时长扩大相邻事件候选范围，误纳入多一整圈的候选。`lift_sample()` 仅对
  非首事件使用历史 `anchor_bounds + elapsed` 与原 age 求交，保留加法溢出、空交、
  首事件及真正长间隔多候选拒绝。公共 lift、PIO、DMA、状态结构与有效期限不变，
  不伪造 empty 见证、不清故障、不新增持久 RAM 或重试。
- 独立真实 feed 原版在 ordinal 22908 的事件复现 reason 11；同输入修复后通过 60000 个
  事件、跨两次回绕。最终独立 36 项、累计 676009 个 feed 事件通过，含真实长 gap、
  首事件、近 uint64 边界、STOP/restart、fault、部分批次及随机 PIO 指令计数。
  host 的两圈模拟不扩大生产有限运行许可。
- 联合 host 初轮为 1041 通过、一个失败和 24 个编译错误；原因是两份旧 fixture
  未跟进实际 RX/observer 拆分及公开时钟声明。补真实调用链、核黑窗仅 observer
  服务并保持 OTA 全部排除，修正 stub linkage 后原 25 项定向通过。原失败保留，
  合并覆盖 1066 项，不表述为一次全绿运行。
- App A/B/Boot Release 与 Flash 检查通过；独立 linked 审计静态 RAM 增量为零，
  预留堆后主 RAM 余 37480 B、scratch gap 24 B。observer 深链含 IRQ/异常为
  1000 B，event service 其他较深路径为 1240 B；既有总体已审最大仍为
  2872/3072 B。此为局部资源检查，不代替 WCET 或全部运行水位。
- 首轮四板 quick P3 为 `PASS_WITH_WARNINGS`，无 ERROR/FATAL；其后 `sustained-r4`
  三从 observer 到约 40.38 秒、MATCH 到 event 40088，FOLLOW 均为预期 STOP 12，
  不再因回绕 binding 退休。NO3 STOP 后 model getter 超时，整体专项仍为 FAIL；
  NO2/NO4 模型核对成功不能替代缺失的 NO3 原件。四板 STOP 和配置恢复完成。
- 最终测试指纹 `p3-final` 为 `PASS_WITH_WARNINGS`，202.078 秒，25 INFO、14 WARN，
  无 ERROR/FATAL。原严格结果为 false；保留启动 barrier 超时、NO1 反馈超窗、
  NO1/NO2/NO4 receive_missing 及 VDC/RefMem deadline 增长。本轮 T1/T3 无告警，
  不以分级通过声明严格运输或调度通过。
- 最终 `sustained-r5` 专项通过，40 秒静默、完整流程 52.187 秒，运行期间零查询。
  三从 observer 到约 40.39 秒、MATCH 均到 event 40088，FOLLOW 为正常 STOP 12；
  末次决定与真实 Domain 模型全部一致。NO2/NO3/NO4 实际更新为 0/0/23 次，
  零调整为 64/65/29 次、拒绝为零，末频率为 0/0/+1552 ppb。前两板宽区间跨零
  不能解释为已同步；四板 STOP 和配置恢复完成。r4 的失败不被本轮通过覆盖。
- 本轮 build `20260917030314`，最终源码指纹
  `53edd7d19c68785746b99d5ba767b1a9c6ae1b493f4992c17efcc90614009eac`，1231 文件；
  包 SHA256 `891a1d3ca3ceea1d8e76c0fc8dcaa82902afa817a5693020f35da2cfbc910b8e`。
  首轮 P3 的测试源码指纹另保留于 `receipt-r1.json`，不代替最终凭证。
- 随后同固件 `native-r1` 已继续主线观测：三从原生记录均完整读回、CRC 有效，
  末次原生决定与 FOLLOW 快照一致；NO1 origin 记录完整重放。NO2 STOP 后模型查询
  超时，整体专项仍 FAIL，不以原生记录替代缺失末态。原生分析可独立使用：NO2/NO3
  各 12 次零调整、四组基线的最终档位仍跨零；NO4 在不变本地模型的约 5.04 秒片段
  中，模型残差端点斜率界为 `[-1802,-315] ns/s`，随后仅调整 +11 ppb。
  这是同模型残差证据，包含远端模型变化，不能直接认作物理振荡器频偏。
  图与逐段区间见 `native-r1/analysis/typed_residual_dco.svg` 及 `analysis.json`。
- 此输入连续性切片已获得跨回绕实板证据；下一最小候选为在真实误差界与增益不变下
  延长未决基线，并先解决长间隔投影乘法和比值的溢出边界，再经独立回归与四板 P3。
  具体计划见 `next-step-analysis-r2.json`；候选尚未实现，不扩大 trace 池或静默丢决定。
  保留真实量化界评估频差分辨率和持续漂移。末态记录不能证明全窗斜率、逐圈必达、
  物理输出锁相或 100 ns 精度；不重复 P0T，不将启动首帧优化重新作为 DPLL 前置。

### VDC-PROGRESS-20260917-013：NO1 共钟区间收窄与原生同事件验收

本轮完成 NO1 typed 新事件的有限共钟桥接求交；不是锁相完成。以下数字为本轮
实测快照，非容量、时序或精度事实源。证据根为
`out/HardwareAcceptance/20260917/dpll-priority-clock-r1/`；独立数学、测试、源码、
资源及 C11 审查原件在同日 `dpll-priority-mapping-r1/`。

- Core1 保留有限半开约束，完整事件端点经真实 Domain 映射后与原投影求交；
  容量及跨度以 `vdc_clock_mapping.h` 为准。只在最终复验和编码成功后提交，
  同事件编码不变；模型/期限重建、矛盾退休、STOP 取消均有真实 TX 测试。
  不改变 Domain now、执行器、FOLLOW 量化界或每帧独立 latch。
- 新 origin 原生 schema 使用原维护池，从新 SYNC 的 Core1 空缓存 ACK 开始，
  连续记录全部成功贡献。FULL 固定最后记录对应的缓存，不随之后 TX 改写。
  旧从板 schema 及 STOP/read lease/CRC/RELEASE 保留，SCPI 运行期间零查询。
- 软件验收合并覆盖 878 项相关测试，通过记录见 `host-summary.json`。原广测
  835 通过、一个旧 TX SCPI fixture 缺新增头依赖；补真实 include 后定向通过。
  独立专项 85 项含 5124 组 Fraction 输入及真实 Domain/TX/原生记录，不把
  provider 假投影测试当数学证明。全部初始失败原件保留，未伪称单次全绿运行。
- App A/B/Boot Release 与 Flash 检查通过。独立 linked 深链审核：新增静态
  RAM 540 B，预留堆后主 RAM 余 37480 B，scratch gap 24 B；共享池仍 7600 B。
  TDMA→TX→mapped projector 加 IRQ/异常为 2068 B，origin trace 深链 1584 B，
  既有已审最大仍 2872/3072 B。局部资源证明不代替 WCET 或全部运行水位。
- 同源码四板 quick P3 为 `PASS_WITH_WARNINGS`，221.203 秒，25 INFO、13 WARN、
  无 ERROR/FATAL。本轮保留 TDMA explicit startup barrier timeout、NO1/NO3/NO4
  receive_missing、VDC/RefMem deadline 增长；T1/T3 本轮未报告告警，不能照抄
  前轮失败。`strict_gates_passed=false`，不得写严格运输/调度已通过。
- 首次 `clock-short-r1` 专项通过，8 秒静默，完整流程 28.687 秒。NO1 连续
  76 条中 75 条收窄；**同事件单桥宽度中位 1687 ns，实际编码中位 863 ns**，
  中位减少 824 ns。原区间范围 1331–3875 ns，编码范围 391–1687 ns。
  全部原始桥、真实 DCO、实际编码及最后缓存独立重放一致，不取中点冒充真值。
- 三从与 NO1 原生窗口各有一条完全相同事件匹配：NO2 event 72，2647→863 ns；
  NO3 event 55，2163→1159 ns；NO4 event 46，2651→1687 ns。接收到的 remote
  上下界逐字相同。该证据只覆盖重叠原生事件，不泛化为所有帧单圈必达。
- NO2/NO3/NO4 的 MATCH 为 37/37/36，DECISION 为 10/7/7；真实 DCO 更新
  **5/6/7 次**、零调整 5/1/0、拒绝均零。序号及频率分别为 1→6、0→+395 ppb；
  1→7、0→+262 ppb；1→8、0→+2240 ppb。末次原生决定与真实 Domain 末态一致。
  NO4 完整频差区间从 `[-6576,-2199]` 到 `[-3915,-435] ppb`，仍在纠偏。
- 三从全窗残差端点变化区间分别为 `[-15854,-6934]`、`[-21603,-7971]`、
  `[-28267,-20163] ns`，仍有负向漂移。不能据更新次数或区间变窄宣称锁相。
  原生分析与图见专项 `analysis/typed_residual_dco.svg`；逐板区间见
  `comparison.json`，同事件核验见 `same-event-comparison.json`。
- 全部 STOP、ACK、CRC 读回、共享池 RELEASE 与配置恢复完成。STOP 后调度表
  仅为累计快照，缺 START 前差分，不能将其峰值归因于本轮新增映射或宣称 WCET。
  后续继续收窄频差判定与验证持续漂移收敛；不重复实现共钟缓存，不重跑 P0T 寻优，
  不引入启动首帧精细证明作为 DPLL 前置。

本轮 build `20260917021736`，源码指纹
`e211b774f0186eb807458e98ead1d79282a313d7a51e238155ca64c0230159a1`，1230 文件；
包 SHA256 `3cb1ce47136fa9abb94806e9651029baf4f3f31c88c9b659e7dca6632f1551fb`。
`VDC-PRIORITY-01` v7 经独立 C11 保持 pending；长期目标与物理精度尚未完成。

### VDC-PROGRESS-20260917-012：同模型基线保留与有限档位复评

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。以下数字为本轮证据快照，非容量、
  时序或精度契约。本切片仅在宽频差区间与死区相交时延长同模型观测，保留全部真实误差界。
- 代码与匹配 P3 凭证已提交为 `4a3bc40`，真实 pre-commit 核验 staged 源码通过。
- `priority_follow_prepare_core1()` 在既有完整端点上限之后检查有限档位；未到档位不做
  相关投影或频差除法，进入计算前即消耗所有已到达档位。错过档位不补算，BUSY 与新鲜
  票据替换不退还机会。`priority_follow_apply_core1()` 只在区间与死区相交、未全落入
  死区且还有后档时保留原基线。自己的 DCO 更新、模型/生命周期改变、STOP、最终档位、
  到期、限幅或失败应用均保持既有重建/取消语义；事件消费和快照格式保持原界。
- 档位与上限引用 `VDC_PRIORITY_FOLLOW_MIN_INTERVAL_NS`、
  `VDC_PRIORITY_FOLLOW_SECOND_INTERVAL_NS`、`VDC_PRIORITY_FOLLOW_FINAL_INTERVAL_NS`、
  `VDC_PRIORITY_FOLLOW_MAX_EVALUATIONS` 和 `VDC_PRIORITY_FOLLOW_MAX_INTERVAL_NS`。
  新私有状态使用工作结构原有尾部填充，不占诊断 reserved 或新增 wire 字段。
- 相关软件回归 410 项及新增独立窗口测试 18 项通过。首次相关回归为 400 通过、10 个
  fixture 编译错误：旧 model owner fixture 未接入上一切片新增的两个 trace wrapper
  入口。补充仅验证 guard 外执行及调用顺序的 seam 后，10 项复跑通过；真实 recorder
  行为仍由原生 trace 测试覆盖。新增测试首轮 15 项通过，补边界时 C harness 曾触发
  misleading-indentation 编译失败，修正测试换行后完整 18 项通过；失败原件均保留。
- 独立窗口测试连接真实 matcher、控制器、Domain 和 recorder，原生两次决策保留同一
  baseline，先跨死区不调整、后档真实采用。计数探针覆盖等待不计算、BUSY、未决替换、
  漏档、三档耗尽、完整上界、重复事件、窄死区、限幅、拒绝和生命周期取消。将实际
  `0ade494` 控制器替换进临时 TU 后，三个关键用例均失败，未改工作区生产代码。
- App A/B/Boot Release、Flash link 与独立 linked 资源审查通过：主 RAM 余 38020 B、
  scratch 余 24 B，准备/应用完整链含 IRQ 和异常帧分别 584/768 B，既有启用路径最大
  2872/3072 B，均未增加。这不是实际运行水位或 WCET 证明。
- 当前源码四板 quick P3 为 `PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL=23/19/0/0；
  build_id=`20260917014743`，源码指纹
  `7291856b177a20715b9322d80da05cbdef38e36b382a2181f288763fd61addb7`，包 SHA256
  `694c27984df8b96ea5805d5887582e5865c561c774cff7bfb02cd9d822ac7385`。
  复用已测线序。本轮 T1 SCK 质量、T3 新测 SCK 行选择和 TDMA startup barrier 均失败，
  已有兼容参数及基本运输仍可用，按固定范围保留 WARN；四板 receive_missing 与
  VDC/RefMem deadline 增长也保留，不能宣称严格训练、连续性或调度通过。
- `window-short-r1` 首次专项通过；运行八秒零查询，无 GPIO 诊断输出，完整配置、采集、
  STOP 后原生读回与恢复流程约 25 秒。三从 MATCH 分别 38/39/38 条，各 12 条 DECISION；
  同一基线的后续决策分别 7/8/8 次，每基线最多三次，实际间隔跨入后档。原件无丢弃，
  分页及整文件 CRC、末次 FOLLOW 与 Domain DCO 一致，采集池显式 RELEASE。

| 板卡 | 真实更新 / 零调整 | DCO 序号 | 实际频率修正 | 频差区间全宽范围 |
|---|---|---|---|---|
| NO2 | 2 / 10 | 1→3 | 0→−98 ppb | 2934–7383 ppb |
| NO3 | 1 / 11 | 1→2 | 0→−22 ppb | 2761–6985 ppb |
| NO4 | 2 / 10 | 1→3 | 0→+23 ppb | 2583–8000 ppb |

- 原生同基线证据直接证明后档采用：NO2 的 baseline 1900 从约一秒的
  `[-711,4306] ppb` 到约 1.503 秒的 `[268,3489] ppb` 后采用 −67 ppb；NO3 的
  baseline 3695 首档 `[-1528,3450] ppb`，约 1.827 秒为 `[88,3031] ppb` 后采用
  −22 ppb。NO4 的 baseline 3714 首档 `[-3629,1028] ppb`，约 1.811 秒为
  `[-2945,-69] ppb` 后采用 +17 ppb；后一个基线再次延长后采用 +6 ppb。
  数据见 `window-short-r1/retained-baseline-applies.json`；各组前后保持实际本地模型，
  没有从缺失的 MATCH 基线重造端点。实板 DECISION 只证明已完成决策的档位，包含
  BUSY/替换的全部准备计算上限由独立 host 覆盖。
- 后档频差宽度确实收窄，但仍为微秒级输入对应的千 ppb 量级。全窗模型 residual
  变化区间 NO2/NO3 为正、NO4 为负；长稳、物理输出采用、相位/100 ns 均未完成。
  前轮与本轮启动、模型和输入不同，不能据跨轮斜率或更新数量宣称算法独立消除了漂移。
- 证据根目录：`out/HardwareAcceptance/20260917/dpll-priority-window-r1/`，固定验收范围
  见 `acceptance-plan.json`；软件为 `software-summary.json`、`coverage.json`、原始日志
  和 JUnit，源码、资源及 C11 v6 独审分别为 `code-review.json`、
  `resource-independent-review.json` 与 `c11-review.json`。实测入口为 `p3/`、
  `window-short-r1/input-probe.json`、`comparison.json` 和 `analysis/typed_residual_dco.svg`。
  独立 `hardware-review.json` 重算二进制、分页、CRC、频差与同基线档位，核对真实
  DCO 链、运行零查询及 STOP/释放/恢复，结论为
  `PASS_SCOPED_BOUNDED_SAME_MODEL_WINDOW_SLICE`。
- 下一 gate：继续 `VDC-FAST-003`，按既有纳秒事件模型证明并收窄 NO1 参考投影的
  TIMER0/TIMER1 共同映射区间，保留每帧独立 latch 和真实量化约束；有限缓存与原生
  分解证据放在同一功能切片验收，不以额外诊断或首帧优化阻塞主线。既有本地差分量化界
  和 Domain 提交/调度逻辑保持，不把粗服务时刻读数与事件纳秒模型坐标混用。后续继续
  实际输出采用、ACK、单圈期限、物理精度及 VDC 正式发布。四板最终 STOP，配置恢复，
  未操作 NO5、未修改 OTA；保留他人 flight engine 工作区改动。

### VDC-PROGRESS-20260917-011：相关本地差分缩界与原生决策验收

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。以下数字为本轮证据快照，非容量、
  时序或精度契约。本切片收窄本地实际输出间隔的估计界，保持绝对 MATCH、远端区间和控制律。
- 代码与匹配凭证提交为 `0ade494`，真实 pre-commit 已核验 staged 源码指纹。
- 同一绑定、observer 起点和本地 committed model 下，raw 差消去公共起点；保留
  `VDC_MODEL_CORRELATED_DELTA_QUANTIZATION_NS` 的双侧量化余量，在 DCO 缩放前计入。
  新候选与原绝对端点差求交，空交、非正下界、生命周期或模型不一致均拒绝并重新建立基线。
  两端须在模型有效起点之后；不将 NO1 逐帧 latch 重装和独立 bridge 当作公共误差消去。
  新除法仅在完整 `VDC_PRIORITY_FOLLOW_MIN_INTERVAL_NS` 窗口后执行，等待路径不增加此计算。
- 软件回归 366 项及独立差分/原生记录测试 38 项通过，后者含 20088 组数值输入，
  覆盖真实 Domain 映射、TIMER0 所有微秒内整数相位、正负 rate、边界、空交及取消。
  同事件宽 bridge 的 host 对照由 10014 ns 缩至 2002 ns；实板跨轮观测不等价于此同事件对照。
- App A/B/Boot Release、Flash link 与独立资源审查通过。静态 RAM 净增零，余 38020 B；
  完整准备链含 IRQ/异常帧由 544 B 增至 584 B，既有最大启用链仍为 2872/3072 B。
  此为 linked 静态栈分析，新增有效窗口计算的实际 WCET 尚未完成。
- 当前源码四板 quick P3 为 `PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL=25/14/0/0；
  build_id=`20260917012507`，源码指纹
  `221b62e15eedb43d0fbe4b7e8174f526cd0546f1b832eb66e63f70d94ba34069`，包 SHA256
  `7628e62b90f1ac7f2e0b1563b219f2b5e7087705624e1ac524d87352d6712632`。
  本轮 startup barrier 通过；feedback 序号超窗、receive_missing 和阶段 deadline
  质量失败保留，不把快速范围通过写成严格运输/调度通过。
- 首次 `delta-short-r1` 采样后，NO3 的 STOP 后模型查询返回 `<timeout>`，专项失败。
  原生记录及相关差分检查已完成，四板 STOP/释放/恢复原件均保留；未放宽判据。
  同源码有界复采 `delta-short-r2` 通过，运行零查询，全部 STOP 后核验分页/文件 CRC、
  FOLLOW 决策与实际模型，再释放采集池、恢复配置。

| 板卡 | MATCH / DECISION | 本轮实际更新 / 零调整 | DCO 序号 | 修正值变化 | 本地间隔全宽 |
|---|---|---|---|---|---|
| NO2 | 39 / 7 | 4 / 3 | 3→7 | −215→−860 ppb | 2001 ns |
| NO3 | 38 / 7 | 2 / 5 | 3→5 | −182→−385 ppb | 2001 ns |
| NO4 | 39 / 8 | 0 / 8 | 1→1 | 0→0 ppb | 2000 ns |

- 复采沿用首次采样后的真实 DCO，未重新假定从零开始。NO4 每个频差区间均跨零，
  不调整符合现有区间控制律，不能证明无需同步。末态 retained baseline/raw/rate/hz
  可独立重算候选；MATCH 降采样未必留存每个决策基线，不伪造全部决定的旧法反事实。
- 上一轮本地决策全宽约 5.5–9.8 µs，本轮约 2 µs；频差全宽由上一轮约
  8.3–14.1 kppb 变为本轮约 4.6–6.6 kppb。两轮启动、模型及输入不同，只作观测比较，
  不据此宣称物理漂移改善的因果结果。NO2/NO3 本轮全窗 residual 变化仍明确为正；
  NO4 的端点变化区间跨零。原生模型残差不等于实际输出锁相，100 ns 目标未完成。
- 证据根目录：`out/HardwareAcceptance/20260917/dpll-priority-delta-r1/`，包括
  `code-review.json`、`c11-review.json`、`resource-review.json`、
  `resource-independent-review.json`、`p3/`、两轮采样原件与日志；独立 host 原件在
  `out/HardwareAcceptance/20260917/typed-delta-host-r2/`。观测入口为
  `delta-short-r2/comparison.json` 和 `delta-short-r2/analysis/typed_residual_dco.svg`。
  独立 `hardware-review.json` 重算原生字节、CRC、区间及真实 DCO，并核验运行零查询、
  STOP/释放/恢复，结论为 `PASS_SCOPED_CORRELATED_LOCAL_DELTA_SLICE`。
- 下一 gate：继续 `VDC-FAST-003`，优先验证区间跨死区的 no_adjust 不立即丢弃同模型
  基线，在现有 `VDC_PRIORITY_FOLLOW_MAX_INTERVAL_NS` 内有界延长有效观测；模型采用、
  生命周期失效及到期仍须重新建立基线。只读端点分析支持这一更小候选，不代表新控制
  行为已实现或实板收益已证实。随后分解 NO1 逐帧 latch、bridge 和量化界，区分公共项
  与每次独立项。保留当前本地量化余量，不强迫非零调整；物理输出采用、相位、ACK、
  单圈期限与 VDC 正式发布仍分别验收。四板最终
  STOP，未操作 NO5、未修改 OTA；代码与文档分离提交，保留他人 flight engine 改动。


## 进度记录

（本节历史条目已按 C14 轮转，见文末 `## 归档索引`。）

## 验证与证据规则

每个 checkpoint 必须至少记录：

- 对应 TODO Task ID 和 owner；
- 当前源码/build/config identity；
- host/build/test 命令及结果；
- OTA/HIL/NO5/SD 原始证据路径；
- 失败原因、后继状态、回退点和下一 gate。

诊断 replay、host 单测、TDMA short-frame、NO5 外环观测和正式 VDC lock 是不同证据等级，
不得相互替代。formal promotion 失败时保留失败证据，不修改为成功状态。


## 归档索引

规则：此处只登记已迁出的历史证据段，条目正文逐字保留、未编辑。轮转规则见 §0 C14 / 环5。

| 文件 | ID 区间 | 条目数 | 归档日期 |
|---|---|---|---|
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_02.md` | VDC-PROGRESS-20260917-010..VDC-PROGRESS-20260917-001 | 10 | 2026-09-18 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_01.md` | VDC-PROGRESS-20260916-043..VDC-PROGRESS-20260906-002 | 146 | 2026-09-17 |
