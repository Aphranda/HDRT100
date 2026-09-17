# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`
Last updated: 2026-09-17

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

当前执行方向由用户进一步明确为预编码最小时间戳、确定性特等席运输和 Core1
直接匹配，见 `VDC-PROGRESS-20260916-038/039/040/041/042/043` 与 `VDC-FAST-001/002/003`。
此前“先核对或替换 OSAL 发布时基”的下一步由该方向覆盖；已通过的分片运输和
本地采用证据保留，不提升为逐圈确定性或锁相完成。

提交边界：`e6ff06a` 已提交 priority RX/栈与调度收敛、RefMem 缩容、验收分级及匹配的
四板 quick P3 凭证；暂存源码指纹 `fb2b46871987abe1426a52207966c93942b75fea1a58a53adb396be2d8589a52`
已由真实 pre-commit 硬件门禁核验，见 `VDC-PROGRESS-20260917-001`。严格质量告警仍保留，
不授予单圈同步编码、三从闭环或锁相完成。

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

### VDC-PROGRESS-20260917-010：typed 原生记录与三从真实 DCO 更新

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。以下数字为本轮证据快照，非容量、
  时序或精度契约。新增诊断记录与读回，不改变既有本地控制算法，不授予锁相或正式 VDC 发布。
- 代码与匹配凭证已提交为 `8d69d05`，真实 commit hook 核验当前 staged 源码通过。
- `vdc_priority_trace.h/.inc` 复用既有采集池，记录实际 MATCH 区间、源事件、运行代际和
  DECISION 的真实 DCO 前后状态；MATCH 按 raw 事件时间有界降采样。Core0 仅提交停止态
  请求，Core1 处理 ARM/STOP/RELEASE 并确认；冻结记录由读取 lease 保护，全部 STOP 后
  分页核对 payload/file CRC。新旧采集互斥，未完成读回不自动释放原件。
- 独审发现并修复两处交接风险：RELEASE 的 published ACK 先于实际归还可能造成后继
  命令无人消费；typed 覆写后未退休旧 completed 元数据可能被旧 READ/SAVE 错标为旧格式。
  真实原子写入交错、旧函数失败/修复后通过及跨格式负例均保留。相关回归 523 项、新记录器
  24 项通过；不是用 mock ACK 或独立测试数组替代真实 owner/共享存储。
- Release App A/B/Boot、Flash link 与独立 linked 资源审核通过：净增静态 RAM 388 B，
  主 RAM 余 38020 B，仍只有原 7600 B 采集池；新增完整 trace service/match 路径含 IRQ
  和异常帧为 584/576 B，既有最大启用路径仍为 2872/3072 B。可选 TRIGGER_MEASURE/MODEL
  保持关闭，不据静态栈结果宣称 WCET 或运行水位通过。首轮 PowerShell 日志重定向返回值
  与已完成构建不一致，原日志保留；结构化 subprocess 复核 returncode=0。
- 同源码四板 quick P3：`PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL=25/14/0/0，
  build_id=`20260917004302`，源码指纹
  `d468840dbf3f823d5cd619ca0664d94b3835011d05170c1ff287832cb4caeefe`；包 SHA256
  `0a0668a2244bfe0f25eb59c86c0cbaaef5f6d17a927ea13716d23d2a4ef6492a`。
  保持固定范围、复用已测线序，严格质量告警单独保留。
- `trace-short-r1/` 八秒静默专项通过，运行零查询、无诊断输出。NO2/NO3/NO4 分别保存
  35/36/35 条 MATCH 和各 7 条 DECISION，覆盖 7.664/7.750/7.570 秒；记录无丢弃，
  三从的 ARM/STOP/RELEASE 均获 Core1 ACK，原件分页和整文件 CRC 通过。

| 板卡 | 真实采用次数 | 零调整次数 | DCO 序号 | 实际频率修正 |
|---|---|---|---|---|
| NO2 | 6 | 1 | 1→7 | 0→+1257 ppb |
| NO3 | 3 | 4 | 1→4 | 0→+157 ppb |
| NO4 | 6 | 1 | 1→7 | 0→+3025 ppb |

- 上表为该轮快照；末次原生决定、FOLLOW 保留快照及真实 Domain 模型逐字段一致。NO2/NO3
  现在已有真实更新证据，不能继续把此前零调整解释为未接入，也不能将本次更新次数归因于
  控制算法修复：本切片没有改变控制律，重新部署/启动与输入条件均与上轮不同。
- 区间分析显示模型残差仍持续下降，未收敛。单点 residual 全宽为本地投影宽与远端宽之和：
  NO2/NO3/NO4 分别约 4.102–6.930 / 5.594–8.634 / 4.282–7.710 µs。NO3 的本地 raw
  起点窗口为 707 tick，另两从为 334 tick；后续投影还引入 bridge 和量化不确定性。
  当前约一秒估计采用两个端点的独立上下界，no_adjust 也更新基线，不会积累更长窗口来缩界。
  同本地模型段端点分析仍能识别负漂移，见 `analysis/analysis.json` 和
  `analysis/typed_residual_dco.svg`；图保留上下界，不用中点拟合代替物理频差或锁相证据。
- 同 epoch 本地起点虽可作为公共潜变量，当前接口未表达相关性；每次 bridge、NO1 逐帧
  latch 重装与远端模型变化不能直接抵消。原生记录未含完整 bridge 三元组或远端分解，
  不能从总宽反推出唯一原因。下一 gate：继续 `VDC-FAST-003`，先证明时间输入/投影缩界的
  数学与身份条件，选择最小切片并重复 host→Release→四板 P3→原生专项，比较频差区间和
  漂移；不能削掉上下界或强迫非零修正。物理输出模型采用、相位/100 ns、ACK、单圈期限及
  VDC 正式发布仍未验收。
- 原件根目录：`out/HardwareAcceptance/20260917/dpll-priority-trace-r1/`，包含固定验收计划、
  `code-review.json`、`resource-review.json`、`resource-independent-review.json`、`p3/`、
  `trace-short-r1/` 与全部命令日志。新测试及旧真实函数反例在同日 `typed-trace-host-r1/`。
  独立 `hardware-review.json` 完成原生字节、分页 CRC、区间算术、真实 DCO、身份、
  零 RUN 查询和 STOP/恢复核验，结论 `PASS_SCOPED_TYPED_NATIVE_TRACE_SLICE`。
  四板最终 STOP，临时 FOLLOW/SYNC/MATCH 与 TAP/会话/负载配置恢复；不操作 NO5，未修改 OTA。
  保留原有 `tdma_flight_engine.c` 工作区改动，代码与文档分离提交。

### VDC-PROGRESS-20260917-009：诊断输出扰动、示波器窗口失败与静默恢复

- TODO task ID：`VDC-FAST-003`；父任务 IN PROGRESS。固件维持 `6261640`，无新增生产代码，
  不重跑 P0T/OTA。以下数字为本轮证据快照，不能作为频差或精度事实源。
- 复用已验证 Rigol 脚本及四路接线，探头均为 1×。`follow-scope-r1/` 在示波器设置阶段
  请求水平偏移与读回不一致，未打开板端串口；`follow-scope-r2/` 改居中窗口，但较长
  时基下 SING 后未观察到新的 WAIT，未 START，四板已 STOP、输出关闭；根因未归定。
  保留两次失败，不通过加长等待或沿用旧 STOP 状态冒充新采集。
- `follow-scope-r3/` 恢复既有短时基，NO1/CH1 上升沿触发，完整导出四通道 RAW，
  各 10M 点、20 ns 间隔、200 ms 窗口。TRIAL 应答在主板输出请求后约 656 ms 返回，
  早于预设首次输出；运行中没有板端或示波器查询，全部 STOP 后才导出。CH1 有 17 个
  有效上升沿，CH2–CH4 在该窗口内均无有效边沿；三从诊断输出末态 `last_error=6`
  表示调度尝试失败。稀疏输出还存在较大时序跳变，不能把描述性直线拟合当作晶振频差。
- 同轮 FOLLOW 专项失败：三从分别完成 3/2/2 次零调整后，末态 reason 为 BINDING；
  observer reason=`TDMA_EVENT_PRE_FAULT`、fault=`TDMA_EVENT_FAULT_OVERFLOW`，
  对应 DMA 环覆盖门禁，不能误称 RXSTALL。完整导出不代表有效闭环测量，零边沿也不代表
  无需调整。当前 phase-only 输出没有每沿模型/回退身份，不作为锁相判据。
- 关闭 SELFTEST 后，以原脚本完成 `follow-after-scope-r1/` 八秒静默对照，重新通过。
  三从各完成 7 次区间跨零的合法零调整，末态 FOLLOW reason=STOP、observer reason/fault
  均零，joined=8119/8120/8121；NO4 保持 DCO 序号 7、+545 ppb。对照支持停止使用当前
  有扰动的诊断输出，不证明其内部具体延迟根因已经修复，也不证明频差已经收敛。
- 原件位于 `out/HardwareAcceptance/20260917/dpll-priority-follow-r1/` 下上述目录；
  `capture_follow_scope*.py` 保留各版本，`scope-capture-review.json` 记录执行前独审。
  `follow-scope-r3/scope/analysis.json`、`slopes.svg` 为辅助输出分析，不能作为锁相证据。
  四板最终 STOP，临时 FOLLOW/SYNC/MATCH/SELFTEST 关闭，原配置恢复；示波器 STOP 并恢复
  EXT/NORM。原有 `tdma_flight_engine.c` 工作区修改未暂存或覆盖。
- 下一 gate：继续 `VDC-FAST-003`，先给 typed 匹配/频率判定接入有界 SRAM 原生记录，
  复用现有采集容量或互斥存储，记录源事件、代际、残差区间及实际 DCO，STOP 后统一读取。
  不依赖旧 `dpll.update_seq` 变化，不引入逐次 SCPI 或额外诊断脉冲。按独立切片完成
  host、Release、资源、同源码四板 quick P3 和专项，再判断宽区间需要优化时间输入还是
  估计窗口；物理输出精度仍单独验收，不能通过删掉区间上下界强制产生 DCO 修正。
  具体候选边界写入同目录 `next-typed-trace-plan.json`，是下一切片草案，尚未实现或冻结为契约。

### VDC-PROGRESS-20260917-008：typed 本地频率控制与真实 DCO 采用

- TODO task ID：`VDC-FAST-003`；父任务仍 IN PROGRESS。以下数值为本轮证据快照，非产品
  容量、时序或精度契约。完成互斥 typed 本地频率控制切片，不代表三从锁相。
- 代码、测试和对应四板凭证提交为 `6261640`；暂存源码指纹及真实 commit hook 均核验通过。
- STOP 配置 `VDC_BOUNDARY_MODE_TYPED_FOLLOW`；MATCH generation 与 mode/session 在同一
  请求元组发布。Core1 只消费本服务真正匹配的新事件，在 committed-model guard 外准备
  同生命周期、同本地模型的两个事件区间，guard 内复验身份、年龄、实际模型与 DCO 后，
  调用既有 Domain 连续调频入口。频率误差从时间间隔比计算并向外取整，负反馈按
  `vdc_priority_follow.h` 的窗口/死区和既有单步/Domain 总限幅执行；零跨越区间不强制调节。
  自身更新后重新建立基线；STOP、换代及陈旧取消，BUSY/缺帧不制造样本。
- 最终相关 host 回归 498 项通过；Release A/B/Boot 与 Flash link 检查通过。独立资源
  审核静态 RAM 增加 960 B，主 RAM 余 38408 B；新增 prepare/apply 完整调用链含 IRQ/异常
  帧 544/768 B，既有最大启用路径 2872/3072 B 未增加，可选 TRIGGER_MEASURE 保持关闭。
  `VDC-PRIORITY-01` v4 经 C11 独审批准，保持 pending；不据资源审核宣称 WCET 达标。
- 同源码四板 quick P3 位于 `out/HardwareAcceptance/20260917/dpll-priority-follow-r1/p3/`：
  `PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL=25/12/0/0，复用既有线序，固定范围不扩大。
  build_id=`20260916232405`；源码指纹
  `526b8ff77416c3ca8972b90c17bc7de61c29b2795025ba0fdc42cba84bd2670e`，包 SHA256
  `5e33501aafa0f94e4767a9e411915e6d4b298962841ca2b141d4188c880a40d9`。严格质量告警仍保留。
- 首次专项 `follow-short-r1/` 在 START 前 FOLLOW 查询超时：临时脚本只登记无符号标量，
  公共串口读取器仍把独立 `1` 当 ACK 丢弃。真实读取器离线复现此行为后，以独立
  `capture_follow_r2.py` 登记布尔响应；原脚本、失败报告不改写，固件和 P3 指纹不变。
- `follow-short-r2/` 八秒静默专项通过。NO2/NO3 各完成 7 次合法零调整，实际 DCO 序号
  仍为 1、rate 为零；末次频率区间分别为 [-8175,4291]、[-6443,3112] ppb。跨零区间
  只能支持“不确定修正方向”，不能支持“无需同步”或已经锁定。NO4 完成 6 次真实采用、
  1 次零调整，末态 DCO 序号 7、rate +545 ppb；末次区间 [-8731,-216] ppb，修正 +54 ppb，
  序号 6→7、rate +491→+545 ppb，与 Domain 末态一致。独立整数重算与原始快照一致。
  运行零查询，全部 STOP 后读回并恢复 FOLLOW/SYNC/MATCH、TAP、会话和原有负载配置。
- 证据入口：上述目录中的 `acceptance-plan.json`、`code-review.json`、`c11-review.json`、
  `resource-review.json`、两轮 `input-probe.json` 及 `actions.jsonl`；软件日志为同日
  `priority-follow-host-final.log`，构建目录 `out/build-priority-follow-r1/`。
  独立 `hardware-review.json` 的 72 项原件复核通过，含源码/包/凭证、四区间任意精度
  重算、实际 DCO、运行零查询、STOP 及恢复；补录 wrapper 与 observer helper 的 SHA。
- 下一 gate：继续 `VDC-FAST-003`，观测实际漂移并缩小频率估计不确定性。现有 internal
  capture 按 `dpll.update_seq` 变化取样，typed 控制更新的是 `dco_update_seq`，不能据旧
  trace 推断新路径未执行或全程收敛；先复用四通道触发波形或独立完善板端记录。
  GPIO 模型采用、斜率改善、ACK、相位控制、单圈期限、100 ns 精度及 VDC 正式发布仍未验收。

### VDC-PROGRESS-20260917-007：observer DMA 排水修复与三从有限窗口持续匹配

- TODO task ID：`VDC-FAST-002`、`VDC-FAST-003`；父任务仍 IN PROGRESS。以下数值均为本轮
  快照，非产品事实源。本切片解除本地事件 FIFO 排水阻塞，尚未接入 typed 本地 DCO。
- 代码、测试及对应四板凭证已提交为 `102bbd7`；`check-staged`、手动及真实 commit hook
  均核验通过。独立资源、软件和两轮硬件证据复核未发现本切片阻断；文档分离提交。
- TDMA owner 为三路 observer 配置固定 SRAM DMA ring，Core1 在既有 TDMA/VDC/DPLL
  边界有界消费；history/feed 仍由 Core1 前景单写。DMA 使用 NORMAL 有限完成计数，
  不使用 RP2350 ENDLESS 模式下不会递减的 `TRANS_COUNT`；整圈覆盖、复制后覆盖、
  总线/计数异常均使 epoch 失效。STOP 先停 SM，再有界停 DMA；超时保留 channel/ring，
  阻止 persona/FIFO 复用并允许重试。DMA 不凭空 FIFO 或稳定计数声明无在途旧词。
- 中间写指针版本在 `observer-dma-fix-p3-r2/` 通过基础 quick P3，专项
  `observer-dma-pointer-match/` 三从匹配 53/28/6 次后退休；不能作为持续性成功。
  最终版本改用完整完成计数并按 DMA 容量设置 join 等待窗口，保留严格超时退休，
  未采用同 epoch 清 pending 后继续的方式。原失败和中间证据不删除、不追认为最终凭证。
- 软件：真实 device DMA block/STOP helper 提取并链接真实 observer 的 28 项通过，
  覆盖 full/multi-wrap、复制竞争、计数模式/耗尽、claim 回滚、STOP 超时/重试及在途旧词；
  observer 回归 24 项、matcher/priority RX 回归 264 项通过。Release A/B/Boot 与 Flash
  link 检查通过。资源复核主 RAM 余 39368 B，净减少 772 B；observer 最深整合栈
  2592/3072 B，既有 follower/NO1 路径无新增超限，可选 TRIGGER_MEASURE 仍禁用。
- 当前源码四板 quick P3：`out/HardwareAcceptance/20260917/observer-dma-normal-p3/`，
  固定 P0/P3/T3/TDMA，复用既有线序并核对 UID/build；耗时约 200 s，
  `PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL=25/15/0/0。原严格 TDMA
  `passed/closed_loop_passed/realtime_gate_passed=false`、启动 barrier 超时与诊断继续
  原件保留；快速分级通过不等于严格质量通过。build_id 仍为 `20260916224543`，
  新包 SHA256 为 `e6c5041cac94dfad21d01c422a5229147487ae8d706085c9fa35d558ee6cc759`，
  当前源码指纹 `bab6480d7031cdbe3c0d98e7958fa1b1f2020ee0a13d302cb292d855cd3c17b8`；
  必须用当前 receipt，不能仅以相同 build_id 复用中间版凭证。
- 同源码静默专项 `observer-dma-normal-match-r1/`（3 s）和 `-r2/`（8 s）均通过；
  运行查询数均为零，全部 STOP 后读取并恢复配置。三从成功匹配数分别为
  103/99/68 与 237/273/198。第二轮 observer joined 为 8094/8094/8095，末态
  reason/fault 均零；三从最后成功匹配事件均为 8093，与最新 retained RX 相同。
  matcher reason=STOP 是正常停止退休，不是运行中失效。启动 SEQUENCE 恢复记录仍保留，
  不能宣称全程无错、每帧匹配或单圈期限已达标。
- 证据：`observer-dma-resource/resource-review-final.json`、`match-continuity.json`
  与两轮 `input-probe.json`；测试原件 `out/pytest/event-dma-main-r1/`、
  `out/pytest/observer-dma-final-main/` 及同日 `observer-dma-*-host.log`。实测最大服务
  间隔/耗时仍需后继预算优化；保守 age 下界也未证明长期 epoch 无限存活。本轮残差含
  本地未闭环绝对偏移，不应当作链路 delay 或锁相误差结论。
- 下一 gate：进入 `VDC-FAST-003` 的独立 typed 本地 rate estimator/DCO 切片，复用
  `dpll-priority-match-r1/next-dco-plan.json`。从两个同生命周期有效事件形成带单位的
  ppb 区间，以互斥模式在 Core1 committed guard 内复验并应用真实本地 DCO；不将 ns
  residual 直接当 ppb，也不伪造远端 model token。随后独立测试、Release、四板 quick P3
  和真实 DCO/斜率专项；不重做 P0T，不将启动首帧和 100 ns 精度反向设为当前前置。

### VDC-PROGRESS-20260917-006：精确源事件匹配与本地观测 FIFO 阻塞

- TODO task ID：`VDC-FAST-002`、`VDC-FAST-003`。
- 状态：IN PROGRESS。Core1 已在 committed-model guard 前接入 typed live handoff：按完整
  source event sequence 直接索引历史槽位，复验 ARM/observer epoch、ordinal、完整序号、
  TIMER1 enable bracket、模型生存期和 RX 代际；用实际 committed DCO 投影计算 local 区间，
  加已装载 reverse-DATA 矩阵转置得到的暂定正向 CS delay，输出 signed residual。此切片不发
  DCO、ACK 或锁相命令。
- 软件/资源：exact、matcher、SCPI、wrapper fixture 相关独立回归分别通过；Release A/B/Boot
  通过。独立资源复核记录主 RAM 余量约 40140 B、matcher 完整 DPLL 相位栈约 1204/3072 B，
  既有最坏 follower/NO1 路径无回归。精确查找不扫描历史、不动态分配、不屏蔽 IRQ；matcher
  workspace 和快照静态分配。`next-dco-plan.json` 仅为后续只读方案，未接入 DCO。
- 四板基础 P3 使用 `QUICK_DIAGNOSTIC`、固定 P0/P3/T3/TDMA 范围完成
  `PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL 为 25/15/0/0；严格调度告警保留。证据根为
  `out/HardwareAcceptance/20260917/dpll-priority-match-r1/p3/`，源码 build 为
  `20260916191056`，不能把基础 P3 结果写成匹配专项通过。
- 匹配专项首轮 `match-short-r1` 保留失败：三从 typed 接收约 3037 条且末条邮箱/CRC 一致，
  但默认 TAP 未启用时本地 observer 分别在 source event 64/57/55 后 INVALID；RECOVERY
  零计数只表示该配置下不记录恢复，不能据此断言具体硬件原因。NO2 成功配对一次，source
  event 64/carrier 66、delay 82 ns 和 residual 算术独立复核；NO3/NO4 未匹配。
- 使用已验收显式 TAP（NO2/NO3/NO4 prefix 120/109/98、delay 15）复测，保留
  `match-short-r2` 与 `match-short-r3` 原始 actions 和 STOP 后诊断。r2 中 observer 均按
  既有恢复路径推进，但 NO1 未形成有效 offer（TX encoded=0），三从只收到约 80 个早期载荷；
  r3 在同源码复位、复用当前矩阵与显式 TAP 后，NO1 形成有效 typed 候选约 605 个，NO2/NO3
  各成功匹配 source event 149，carrier 152，delay 分别 82/164 ns；NO4 未匹配。三从仍
  出现 `TDMA_EVENT_FAULT_STALL`，该位来自 observer SM1–SM3 的 RXSTALL 合并，只能证明
  观测器组至少一 SM 溢出，不能归因 DATA RX DMA。r3 recovery reason=SEQUENCE、fault=1
  的原始证据保留，专项仍 FAIL。
- 根因方向已收窄：每帧 observer RX/TX 计数器写两个字，JOIN_RX 8-word FIFO 约容纳四帧；
  TDMA phase 之间服务间隔过长时 FIFO RXSTALL，继续使用会破坏时间轴，observer 正确退休。
  下一切片评估在 TDMA owner 内固定容量的原始 batch 暂存/前景单写者消费，或缩短观测服务
  间隔；必须保留溢出、epoch 失效和固定容量证据，不能清 sticky 后冒充连续时间。不要把
  载荷持续接收误判为本地事件观测或 DCO 闭环完成。
- 后续只读 DCO 设计见 `next-dco-plan.json`：typed 输出无 remote model token，不能把
  generation/event sequence 伪造成 token；须先完成三从稳定 residual，再增加互斥 typed-follow
  mode、Core1 rate estimator 和有界本地 DCO commit。首帧/P0T/100 ns 精度不是当前 FIFO
  阻塞的前置条件。
- 下一 gate：`VDC-FAST-002`，独立设计/资源审查后实现 observer raw harvest 或等价有界
  服务修复，重新编译并运行匹配源码 quick P3；之后才重跑三从匹配专项并进入 `VDC-FAST-003`。

### VDC-PROGRESS-20260917-005：同步记录直接交接与 IRQ 热路径收敛

- 代码与匹配四板 P3 凭证已提交为 `c144ad8`，`check-staged` 与真实 pre-commit
  已核对本轮源码指纹通过；文档分离提交。
- 对应 `VDC-FAST-001/002`。本轮先复核上一实板原件：三从 priority RX 已发布
  2644/2644/2646 条，而 DPLL phase 的 latest-only 诊断仅复制 270/269/267 条。
  主要缺口在消费者节拍，不能称为物理链路丢帧。STOP 后读取旧固件的分段计时，
  确认复制、校验、发布多段都有成本；CRC 内核和环形复制原本已在 RAM。
  以下计数、时长、资源均为本次快照，非产品事实源。
- 新增 STOP 配置门禁注册的固定 RX sink。TDMA 在成功校验/发布后，立即在 Core1
  IRQ 同步交给 typed 定长解码入口；不等待 DPLL phase，不执行 PI、通用解析、
  存储或 RTOS。保留原慢速轮询作对照，独立状态避免 IRQ/前景多写者。载荷圈回绕
  交接使用发布后 epoch；STOP、DMA 坐标退休和 epoch 耗尽撤销入口。同身份却
  时间区间不同拒绝为冲突，普通启动载荷、typed 成功、重复、相邻不同事件分开计数。
- IRQ glue、候选定位、验证、发布、CRC 外层、小型 codec/sink 和 watchdog 标记
  迁至主 SRAM；保留全部 CRC 与身份检查，消除随后会被完整覆盖的工作记录清零。
  未扩大候选 IRQ 预算，未修改 OTA 实现。新明细 `SYSTem:VDC:PRIORity:RX?`
  只在 STOP 且入口已退休时读回；该诊断 getter 不授予实时控制消费资格。
- Host：owner/原 ingress/codec 89 项、SCPI/发送回归 267 项、直接 sink 92 项通过；
  后续 owner 4 项复核通过。独立方另复核 97 项。A/B/Boot Release 与 Flash link
  检查通过。独立 A/B map/dis/ELF 核验：主 RAM 扣预留 heap 后 41828 B，较上一
  切片减少 4128 B；scratch gap 仍 24 B。新 IRQ 最深软件栈 200 B，follower 完整
  前景加 IRQ/异常帧 2708/3072 B，NO1 2872/3072 B。此为已审四板 persona，
  不是全局栈或运行水位证明。VDC-PRIORITY-01 v2 保持 pending，独立 C11 已批准该范围。
- 四板固定范围 quick P3 一轮完成，耗时约 192 s，`PASS_WITH_WARNINGS`；
  INFO/WARN/ERROR/FATAL 为 23/20/0/0，DPLL 专项 SKIPPED。严格质量仍未通过：
  TRN-01/SCK re-arm 裕量及 TDMA startup stable barrier 失败原件保留。复用已确认
  线序，没有重新 P0 寻序。build `20260916183948`，1208 个源码文件，指纹
  `2cc7f6ea4af4d49ef27d9e414bf657f2fd410754d6fd1ae3a750a7641224049a`。
- `direct-short-r1` 三秒静默专项 PASS，零 RUN 查询，最终四板 STOP 并恢复
  load/diagnostic/burst，禁用同步与 session，撤销临时 trial。三从直接入口收到
  2961/2963/2963 条，与各自同 epoch 的 TDMA 成功发布数严格相等；其中 typed
  各 2902 条、普通启动 59/61/61 条，typed 拒绝和冲突均零。相邻不同源事件各
  749 个、重复各 2153 次；NO1 本轮生成候选 749 个，计数一致不替代完整逐事件
  线上追踪。三从末条 typed 与 NO1 同事件 2947、carrier 2965，区间和 CRC 均对应。
- IRQ body 的三从平均 cycles 从约 28103/28324/28117 降至
  3787/4140/4115；最大从 50706/48485/46334 降至 4496/5187/4724。
  按 `BOARD_SYS_CLOCK_HZ` 换算，平均约 112–113 us 降至 15–17 us，最大约
  185–203 us 降至 18–21 us。四段 maxima 不相加，body 之外仍有 IRQ 进出与
  finish/accounting tail；不同轮次不能作为硬 WCET 证明。仍超过
  `PROJECT_CORE1_PRIORITY_RX_IRQ_CYCLES` 候选预算，严格相位预算与物理同圈
  截止时间未闭合。原 poll 本轮仅复制 185/179/120 条，不再代表直接入口数量。
- 证据根 `out/HardwareAcceptance/20260917/dpll-priority-rx-direct-r1/`：
  `stopped-baseline.json`、`irq-cost-review.json`、`implementation-host-r1.json`、
  `owner-host-r1.log`、`owner-final.log`、`integration-host-r1.log`、`build-r1.log`、
  `code-review.json`、`resource-review.json`、`c11-review.json`、`hardware-review.json`、
  `p3/acceptance.json`、`direct-short-r1/input-probe.json` 与 `actions.jsonl`。
  `capture_direct_rx.py` 经主控审阅后用 Windows 原生 Python 串口入口执行，
  复用已有配置/STOP/helper；未改变验收程序或当前源码指纹。
- 独立实板结论 `PASS_SCOPED_FOUR_BOARD_DIRECT_TYPED_RX`：54 处证据/源码哈希、
  最终 A/B ELF/map/dis、P3 分级及 211 条原始操作记录核对通过；三从原始 header
  身份/transport CRC32 与 mailbox CRC16 均重算通过，无本切片提交阻断。
- 下一 gate：直接入口已消除已准入记录等待 DPLL phase 的交接缺口；继续核对
  物理帧到入口期限及 IRQ 预算；`VDC-FAST-003` 已整理按 source event
  精确索引与本地 delay/模型残差方案。不得把原 poll 诊断当作新通道丢帧，也不得
  把收到/解码当作从板 DCO、ACK 或锁相完成。计划见 `next-event-match-plan.json`。

### VDC-PROGRESS-20260917-004：Core1 同步发布与三从真实接收

- 代码与匹配 P3 凭证已提交为 `686e2db`，暂存源码指纹已由 `check-staged` 和
  真实 pre-commit 核验通过。文档分离提交；文档检查器与 18 项自回归通过，既有
  `TDMA-FLIGHT-BITMAP-01` 登记格式 WARN 保留。
- 对应 `VDC-FAST-001/002`。本轮代码把已提交 DCO 的真实 origin 事件区间在 Core1
  编成 typed 邮箱，TDMA owner 直接交给既有 DMA exchange 双缓冲；不经过普通
  FIFO/overlay 的 Core0 每事件准备。STOP-only generation、完整运行绑定、模型
  生效时间、重复事件冻结和区间溢出均检查；普通异步工位可取消但不等待其 ACK
  才发送，工位 ACK 前不复用。物理 bank 仍由真实 selection 退休。
- 字段及边界登记为 `VDC-PRIORITY-01` pending，独立方 `p0_root_review` 完成 C11，
  见 `VDC_CROSS_REVIEW_04.md`。先前“等待普通取消 ACK”、STOP 注册竞争和普通同步
  fallback 类型放开三项发现均已修复；同期修正 codec 区间溢出检查和旧测试中
  已启用 class 被误列为 forbidden 的断言。失败原件保留，不追认为测试通过。
- 以下数值均为本轮快照，非事实源。provider 的实际生产函数 host 252 项通过，
  codec+provider 合计 253 项通过；集成 priority RX/ingress、物理邮箱、SCPI 共
  110 项通过，完整 adapter C 回归通过。A/B/Boot Release 与 Flash link 检查通过。
  独立资源复核绑定最终 ELF/map/dis：主 RAM 保留 heap 后 45956 B（比上一切片少
  772 B），scratch gap 24 B；新 provider 链含 IRQ/异常最大 1776/3072 B，原 NO1/
  follower 最大路径仍为 2872/2716 B。仅覆盖已审四板 persona，不授予全局栈或 WCET。
- 证据根 `out/HardwareAcceptance/20260917/dpll-priority-tx-r1/`。首轮 `p3/` 在
  TRN-00 发车前的 NO3 `TOPology 4,2,0` 返回 `<timeout>`/`-200`，没有训练观测；
  失败仍保留。用相同固件与 OTA 原件 `resume` 后 `p3-resume/` 固定范围
  `PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL 为 24/27/0/0；严格质量未通过，
  DPLL 专项在该 P3 中 SKIPPED。复用确认线序，没有重跑 P0 扫描或更改 OTA。
  build `20260916181047`，1205 个源文件，指纹
  `f30660c864bd307471a08c8a689b04c80f64140742baa466b7f5b73ca3ab6b18`。
- 短时实板专项首轮 `typed-short-r1/` 在 START 前被旧采集 helper 的 LOAD:MASK
  复合 ACK 过滤阻断。仅修 out 采集脚本的 ACK 注册后，`typed-short-r2/` 成功；
  复用当前板上矩阵，四板 STOP/UID/build 屏障、显式 session/generation、ARM/START、
  有限自主 trial、静默运行及最终 STOP 后读回全有原始 actions，未在 RUN 查询采样。
  最终四板停止，开关禁用并恢复原 load/diagnostic/burst，trial 已撤销。
- NO1 生成 684 份编码候选；该计数不是物理提交或送达总数。三从 Core1 原始记录
  消费累计分别为 270/269/267，包含启动普通记录，不能称全部为 typed 次数。
  三从末条均为 class `0x14`，完整载荷圈序号 2648；body 指向源事件 2645、运行
  generation 1789582893，纳秒下界 142840259535，width 1687。三从末条 32 B
  邮箱均与 NO1 保留候选逐字节一致，header 身份 CRC 与邮箱 CRC 独立校验通过。
  载荷圈与事件相差不等于测得运输延迟；本轮尚无逐圈最晚到站证明。
- NO1 已提交 DCO 序号从 456 至 543、model token 从 1 至 88；此为主板模型确实
  在更新的快照，不代表 typed 从板控制已经接入。当前新通道只运输/解码保留，
  NO2–NO4 尚未在此路径按同事件、本地 delay 应用 DCO。width 保留投影与计数
  不确定度，不能宣称 100 ns 或共同时间锁相。
- 主要原件：`provider-delivery.json`、`provider-host-r1.log`（旧失败）、
  `provider-host-r2.log`、`integrated-host.log`、`build-final.log`、
  `owner-review.json`、`priority-tx-resource-review.json`、`c11-review.json`、
  `hardware-review.json`、
  `p3-resume/acceptance.json`、`typed-short-r2/input-probe.json` 和 `actions.jsonl`。
  硬件辅助入口 `capture_typed_tx.py` 由主控审阅后以 Windows 原生 Python 执行，
  复用既有串口 helper；旧脚本保存为 `capture_typed_tx_r1.py`。
- 独立硬件复核为 `PASS_SCOPED_FOUR_BOARD_TYPED_TRANSPORT`，无本切片提交阻断：
  核对 37 处引用哈希、源码指纹、四板 UID/build 及 196 条操作记录；三从 transport
  CRC32、immutable identity CRC32 和 mailbox CRC16 均独立重算通过。四板 STOP
  有确认，运行采集期间没有查询；结论仅覆盖本次真实 typed 运输，不提升为严格
  P3 质量、单圈截止时间、从板 DCO 应用或锁相通过。
- 下一 gate：继续 `VDC-FAST-001/002` 的单圈期限、消费者能力和接收端运行绑定；
  再按 `VDC-FAST-003` 直接索引同一源事件、加本地已测 delay 并实际应用 DCO。
  三从收到真实同步数据这一缺口已闭合；不回到 RefMem 分片作为实时前置。

### VDC-PROGRESS-20260917-003：P0 瞬态拒绝恢复与 typed 同步接收切片

- 代码与匹配凭证已提交为 `e6861f8`；`check-staged` 和真实 pre-commit 均核对本轮
  源码指纹通过。文档独立提交，文档检查器与 18 项自回归通过；既有
  `TDMA-FLIGHT-BITMAP-01` 登记格式 WARN 保留。独立代码审查记录为
  `typed-ingress-code-review.json`，限定本轮 body 解码保留范围，无提交前阻塞。
- 对应 `VDC-FAST-001/002`；用户要求先修 P0，再回到同步特等席。以下次数、资源、
  build 和统计均为本轮快照，非产品事实源。未修改物理接线或 OTA 实现。
- 纠正前述故障归因：r1/r2 均在下一候选的 `SYST:TDMA:RING:TOPology` 配置阶段
  收到 `<timeout>` 和错误队列 `-200`，尚未开始该候选发送；不是最后显示的成功边失效。
  P0 邻接判据采用 DMA 活动，探测流 `rx_frames=0`/`magic_fail>0` 不等于该判据失败。
  NO2→NO3 原样定段复测 16 轮中 15 轮成功，失败轮反馈 session 为零且 STOP
  config/applied 均为 154；因此不能归因于活动反馈会话或 RJ45 被移动。
  具体固件拒绝分支（快照、锁、待处理意图或 STOP 退休等）仍未知。
- `configure_pair_topology()` 复用 `_set_stopped_topology()` 的既有有界恢复：
  清楚记录错误队列基线与 session，只有已知瞬态拒绝可有限重试，每次重新证明 STOP，
  必须收到精确 topology tuple 和新的 applied generation 后才 ARM。未知超时、
  持续拒绝、活动 session、旧 generation 均失败；原快速等待参数和邻接判据不变。
  已移除早期逐对清 session 的尝试；该命令的 `"OK",0` 被通用读取器拆成 `,0`，
  不能把当时记录的 `<timeout>` 当成有效 ACK，也不据此宣称板端拒绝清会话。
- 修复后同段 16/16 轮成功，其中一次真实首尝试拒绝经有界恢复成功，四板清理均通过。
  修复前后原件分别在 `out/HardwareAcceptance/20260917/p0-pair-repeat.json` 和
  `p0-pair-repeat-fixed.json`。完整失败保留在 `dpll-typed-sync-r1/p3/p0t-topology/`
  与 `dpll-typed-sync-r2/p3/p0t-topology/`；本次恢复不能追认旧轮通过。
- 主线新增独立同步类 `TDMA_PROCESS_IMAGE_VDC_PRIORITY_SYNC_MESSAGE_CLASS`、
  定长 `VDC_PRIORITY_CODEC_BODY_SIZE` body 编解码及 Core1 有界 typed 解码保留。
  普通 process-image whitelist 不变；真实邮箱外层 source/target/header/CRC
  仍由 TDMA priority validator 检查。codec 的 CRC helper 仅覆盖 body，不替代邮箱 CRC。
  成功/拒绝计数保留在内部快照，未新增 SCPI 输出；无效 body 不覆盖上次解码记录，
  重复 carrier 不重复解码，换 ARM epoch 清理状态。记录未用于匹配或 DCO 授权。
- Host：P0/calibration 三组 97 项通过；codec/priority RX/Core1 ingress 三组 89 项通过。
  本轮修复了 ingress 测试夹具漏链接生产 codec 的错误，并增加 typed 成功、拒绝、重复、
  普通类隔离及 STOP/ARM 用例。A/B/Boot Release 与 Flash link 检查通过。
  独立 A/B 资源复核：主 RAM 保留 heap 后 46728 B（比前一切片少 64 B），scratch gap
  24 B；既有 follower/NO1/DPLL Core1 路径分别为 2716/2872/2012 B，未增长。
  仅为已审正常 persona 的静态路径分析，不代表全局栈、运行时水位或 WCET 证明。
- 当前源码四板 P3：build `20260916174836`，1200 个源文件，指纹
  `b928dbe4460a4ee49493438a016e5caa3ddeb7cb46caee2431196b9047ed95ef`。
  固定 `QUICK_DIAGNOSTIC` 范围 `PASS_WITH_WARNINGS`，P0/P1/P2/P3/T0 为 PASS，
  T1/T2/T3/TDMA 为 PASS_WITH_WARNINGS，DPLL 为 SKIPPED；INFO/WARN/ERROR/FATAL
  分别为 30/24/0/0。完整环序仍为 NO1→NO2→NO3→NO4→NO1，P0 清理和最终四板
  stopped handoff 有效。原 TDMA `passed/closed_loop_passed/realtime_gate_passed`
  均 false，`diagnostic_passed` true；`strict_gates_passed` false，严格失败保留。
- 证据根 `out/HardwareAcceptance/20260917/dpll-typed-sync-r3/`：
  `p3/acceptance.json`、`p3/alarms.json`、`p3/diagnostic.json`、
  `p3/p0t-topology/summary.json`、`p3/tdma-process-image/summary.json`、
  `p3/tdma-stopped-handoff.json`、`typed-sync-resource-review.json`。
  本轮硬件命令从既有 Windows 原生 Python/PowerShell 会话继续执行，以保留串口流程：
  `python tools/hardware_acceptance/p3_hardware_acceptance.py run --tdma-only --diagnostic-continue --out-dir out/HardwareAcceptance/20260917/dpll-typed-sync-r3/p3`。
- 下一 gate：P0 编排缺口已闭合，回到 `VDC-FAST-001/002` 的 NO1 确定性 typed 发布，
  先明确时间单位、绑定代际的预安装/失效和完整邮箱验证。后续 `VDC-FAST-003` 接入
  Core1 完整事件匹配、本地 delay 与 DCO。当前没有实板 typed TX 数据，不能把
  普通邮箱回归、body 解码或快速 P3 凭证说成同步单圈交付、三从闭环或锁相。

### VDC-PROGRESS-20260917-002：Core1 priority RX 原始记录消费接通

- 对应 `VDC-FAST-002`；本切片只闭合“TDMA priority RX 保全 → Core1 读取”的入站边界。
  `sync_dpll_fb_service()` 在既有 Core1 服务边界执行一次最新候选快照和一次
  `tdma_priority_rx_copy_live()`，使用完整 carrier sequence、epoch、retained 位图和
  guard 复验；失败不退休候选，下一次服务重试。latest-only 的跳过数单独记录，不能解释成 FIFO 丢帧。
- 新增 stopped-only `SYST:TDMA:FLIGHT:PRIOR:CONSumed?` 读回，输出 Core1
  服务/消费/重试/重复/序列间隔/时钟无效计数、有效 body 计时及最后原始记录。
  普通 class（本轮实板读回为 `0x10`）只保留原始字节，不进入 VDC residual、时间戳、ACK、
  DCO 或锁相控制。原 priority `REC?` 的 STOP 诊断读取保持不变。
- live-copy host 组合及 Core1 ingress 组合共 108 项通过、无跳过；覆盖 6/8 节点、
  short-enum、STOP/ARM/rebase、换代、同槽覆盖、guard 竞争、序号回绕、时钟失效和
  普通/未知 class。资源独审绑定 build `20260916162422`、源码指纹
  `2d2163a96166a8f9a7d2faae26fba502d27e02c6db40d27b7451ff9b594772bb`、1197 个源文件；
  A/B/Boot Release 和 Flash link 检查通过。当前 persona 资源快照：主 RAM 保留堆后
  46792 B，follower Core1 路径余 356 B，NO1 路径余 200 B；均为本构建快照，非全负载保证。
- 四板 `QUICK_DIAGNOSTIC` P3 已完成，固定范围结果为 `PASS_WITH_WARNINGS`，TDMA
  原始过程结果和严格质量失败均保留；本轮未执行 DPLL 观察，不能宣称严格 P3 或锁相。
  STOP/UID/build 屏障后，NO2/NO3/NO4 分别出现 244/239/240 次 Core1 原始记录消费，
  最新 mailbox class 均为普通过程类；NO1 未安装 follower priority consumer。消费 body
  最大值和 IRQ-entry→read 最大值见 `consumer-readback-r1.json`，均为当前板端快照，
  不是 WCET 或物理飞行/相位时间。三从读回的 retained record 与 producer exact record
  在仍保留时逐字一致；不在槽位时只保留独立 CRC 结果并明确标记。
- 证据目录：`out/HardwareAcceptance/20260917/dpll-priority-sync-r1/`，包括
  `ingress-test-delivery.json`、`independent-review/linked-ingress-review.json`、
  `p3-r1/acceptance.json`、`p3-r1/alarms.json`、`consumer-readback-r1.json` 和
  `measured-build-r1/manifest.json`。严格门限仍为 false；本切片只证明 Core1 原始
  运输记录消费，未证明同事件关联、预编码时间戳、DCO 更新、100 ns 精度或锁相。
- 下一 gate：继续 `VDC-FAST-002/003`，增加独立 typed synchronization class 和
  NO1 origin 事件发布；随后在同一 Core1 固定索引入口完成完整 event identity、时间区间、
  本地 delay 和 DCO 应用。不得把本轮 `0x10` 普通邮箱记录重新解释为特等席同步记录。
此前 `22a22b5` 已提交参考发布节奏切片与匹配 quick P3 凭证，源码指纹
`3dd87e9aee4b3899f3592c47fb30df3b899dcda290531d4b32514a615785ef98`，
见 `VDC-PROGRESS-20260916-037`。此前 `6530114` 已提交 029–034 的接收 ACK、历史事件桥、本地跟踪、RAM 导出、
等待时间基与桥接窗口修复，以及显式复用已知线序的验收工具和匹配 quick P3 凭证。
该阶段源码指纹为
`f429dde66d6f732160d84930f2b11b14b5b9d853fd023788736d74346100bbeb`，
当时提交与工作源码匹配，见 `VDC-PROGRESS-20260916-036`。此前 `7c46b2f` 只覆盖至
028，`5a22acc`、`643d703` 分别提交后继文档；当时未提交及无新凭证的判断仍是
对应历史事实，见 `commit-consolidation-r1/r2/r3` 的证据。本轮未绕过指纹门禁，
quick 四板流程通过不代表所有实时相位无超限或 DPLL 锁相。

当前执行已按用户最新指令切换到 NO1 自主 PI/发布时间戳、NO2–NO4 接收/环路 ACK、
加本地已测 delay 后本地跟踪，见 `VDC-PROGRESS-20260916-027/028`。
集中式逐从 AUTO 不再作为前置，完整 delay/bias 与物理精度后续验收。
显式 REFERENCE 发布和基础环路 ACK 均已实测，见 `VDC-PROGRESS-20260916-028/030`。
ACK 固件直接四板 TDMA 闭环通过，三从回执与发送末档逐字节对应；该轮三从 DCO
更新均为零。自主阶段 NO1 PI 和三从持续本地跟踪仍待验。完整 P3 的 P0T 准备失败
保留于 029；按用户最新指令，已知线序停止继续探测，校准/工具/SD 问题单列，
只有 TDMA/DPLL 本身阻塞主线。本地事件历史桥的源码、Release 与直接四板 TDMA
回归已完成，见 `VDC-PROGRESS-20260916-031`。集成本地跟踪的软件、Release 与
直接四板 TDMA 已完成，见 `VDC-PROGRESS-20260916-032`；该轮三从无 DCO
更新。等待配对的时间基错配已修复，见 `VDC-PROGRESS-20260916-033`。
桥接采样窗口缩短后，NO4 首次有可追溯的本地 DCO 采用，见
`VDC-PROGRESS-20260916-034`；NO2/NO3 当轮完整参考接收为零，三从持续闭环仍
未完成。035 的停止态 FIFO/RefMem 差分未观察到镜像丢弃；036 的 PHYS 快照确认
三从在 FIFO 前存在大量观察丢弃，并在复位后再次记录 NO4 的真实本地采用。
037 的发布节奏切片已通过当前源码 P3；两轮专项均改善三从完整参考与匹配 ACK，
NO2/NO3 已进入本地控制判断，但误差区间跨零而保持，NO4 再次非零采用。
037 当时下一步为缩窄真实观测不确定度、提高新鲜配对连续性，并核对 OSAL 发布时基；
观察丢弃计数不直接等于缺失分片数，发布间隔不是物理驻留保证，不重跑 P0T 寻优。

以下保留前一路线 checkpoint。显式 AUTO 与 RATE 窗口已接入，初版软件、双槽构建和对应源码严格四板 P3 通过；
自动多轮实板专项仅部分从板响应，尚未闭合。AUTO 专用采集修复的软件及构建通过，
P3-r2 的配置阶段失败保留，随后同源 resume 严格通过。AUTO-r5 证实采集完整，
仍有后继命令未决。Core0 完整结果暂忙交接及 coarse OPMode 维护态工具修复后，
对应源码四板严格 P3 通过，AUTO 完成采集但仍只有部分从板实际应用。后继延后
RX acquire 的修复已完成软件、资源及构建；P0T 启动确认工具修复后，当前源码
四板严格 P3 已通过。后续 AUTO 在启动确认及旧记录交接阶段中止，尚无同源
有效多轮采集；先收敛专项采集入口，再验证运输效果，见
`VDC-PROGRESS-20260916-019/020/021/022/023/024/025/026`。
上一轮源码已完成同命令有界重复运输，软件、双槽构建和当轮严格四板 P3 通过。
同轮专项中 NO2/NO3/NO4 均实际应用一次并返回精确 ACK，重复接收未重复应用；
原始证据独立复核通过，见 `VDC-PROGRESS-20260916-018`。当时自动频率控制尚未
接入，后继方案分析已开始；单次成功不能证明持续锁相。
前轮 017 已修复 TDMA 重配置遗漏继承 active servo CRC，当轮 NO3/NO4 应用及 ACK
成功，NO2 未收到完整命令，该失败原件保留，不由本轮通过追认。
前一诊断切片 016 严格四板 P3 通过：当轮 NO2/NO3 首次拒绝于 APPLY，NO4 没有完整
命令 RX，三从应用均为零。两轮未完整接收的节点不同，运输可靠性继续独立跟踪。
逐从接线见 014，历史三从接收成功见 015；014 的严格校准失败原件继续保留，
不能套用于不同源码的当前状态；当时自动频率控制未接入。
连续重基原语本身的历史严格验收见 `VDC-PROGRESS-20260916-013`，不能用该原语
当时无生产调用的 P3 或 host 数学验证宣称从板闭环。

模型关联反馈已完成当时源码四板严格 P3 与运输/配对专项，见
`VDC-PROGRESS-20260916-012`；独立原件复核通过，代码已提交为 `6716dc5`，当时尚未接通从板控制。
此前准备迁移见 `VDC-PROGRESS-20260916-011`：缓存、配对和差分由 Core0 RefMem
单写者准备，Core1 只发布或退休授权。011 切片当时源码的四板 P3 严格通过，专项完整通过轮次
为 r2/r4；r1/r3 的短 TX 历史缺证保留。NO1 DPLL 超限显著减少，仍未满足局部及
完整静态表预算。当时各从实际 DCO 应用仍为零；后继接逐从专属校正和 Core1
连续重基应用，不能以诊断配对代替闭环。原始配对及运输基线分别见
`VDC-PROGRESS-20260916-010/009`。

最新同条观测留存及本板时间锚见 `VDC-PROGRESS-20260916-008`：严格四板 P3、专项和
原件独立复核通过；首次专项因 NO2 START 缺少真实应答中止，同源新轮次证明三从
恢复后持续留存完整记录，STOP 后历史不变。TDMA 完整相位超限及 RX 覆盖计数仍保留，
不由此宣称完整时序或锁相通过。该历史切片的原始反馈后继见上方当前 checkpoint。
观察器自身恢复见 `VDC-PROGRESS-20260916-007`。NO1 运行中发射记录留存见
`VDC-PROGRESS-20260916-005`：软件、Release、
当前源码四板 quick P3 和 STOP 后同序对账已通过。输出投影准备见
`VDC-PROGRESS-20260916-004`；当时实际 DCO 应用尚未接通。指定主机接收切片见
`VDC-PROGRESS-20260916-003`。目标整理见
`VDC-PROGRESS-20260916-002`，随后贯通各从反馈和专属校正，再消除漂移、收敛精度。

执行顺序与任务状态统一见 `VDC_DOMAIN_TODO.md` 的“当前主线：四板数据运输与真实闭环”
及其中 `VDC-FLIGHT-001` 至 `VDC-RECOVERY-001` 的任务依赖表；
本文仅追加每个切片已经发生的验证、失败和下一 gate，不复制第二份迁移顺序。

当前执行依赖按 `VDC-PROGRESS-20260916-027` 纠偏；此前各记录的“当前”及
“下一 gate”保留历史含义，不将首帧可用、全窗无错或完整绝对时间映射作为运输前置。

### VDC-PROGRESS-20260917-001：阶段告警分级与固定验收范围

- 用户授权按 P0–P3、T0–T3、TDMA、DPLL 的细化表优化验收。新增纯分级策略及主流程接线，INFO/WARN/ERROR/FATAL 与阶段 PASS/PASS_WITH_WARNINGS/FAIL/SKIPPED 分开记录；范围启动前写入 `fixed-scope.json`，`--strict-stage` 显式指定本轮严格目标。
- 快速验收要求实际拓扑、目标档位延迟、矩阵装载读回和逐板有效新帧运输。偶发缺收、启动稳定窗口、调度质量告警单列；缺节点、无有效新数据、STOP 或原生证据失效仍阻断。DCO 零调整不自动报错，未执行 DPLL 为 SKIPPED，不能由 TDMA 运输通过宣称锁相。
- P3 汇总修正非必验 LIMITED_RX 高频探测的范围：原件保留，目标稳定档位及完整频率覆盖仍必须验证，不能拿更多高频探测试验补齐缺失目标档位。纯分级模块不修改原始 passed/失败记录。
- 新快速凭证类型为 `QUICK_GRADED_RECEIPT_SCHEMA`，绑定固定范围、报告、判定输入与原始阶段证据；提交校验重算分级、矩阵读回及 STOP 原生交接。旧凭证兼容，新增证据不可缺失或混配。本轮没有修改固件/PIO/OTA 实现。
- host 组合测试 155 项通过、无跳过；测试最后一轮于上一日启动，证据保留 `out/HardwareAcceptance/20260916/p3-alarm-grading-r1/policy-and-integration-r4.xml`、`test-delivery.json`。独立历史原件与负测不代替当前源码 P3。
- 当前源码四板快速 P3 已于本日完成，运行证据位于 `out/HardwareAcceptance/20260917/p3-alarm-grading-r1/`；沿用已测线序，不重跑 P0T，不操作 NO5。新结果 `PASS_WITH_WARNINGS`：P0–P3/T0–T3 为 PASS，TDMA 为 PASS_WITH_WARNINGS，DPLL 为 SKIPPED。实测报告快照：INFO 25、WARN 20、ERROR/FATAL 均为零；条目含分阶段/逐板与总体归纳，不等于独立故障数量。
- 新凭证绑定源码 `fb2b46871987abe1426a52207966c93942b75fea1a58a53adb396be2d8589a52`，四板仍运行同一固件 build `20260916151903`，包 SHA-256 `3be05d080112b40cd33549322af3408549bdecd48f57b385efe99480c21d9222`；工具和测试变更未改变固件字节。A/B Release 随 P3 构建通过，全部板卡 STOP 后再次读回身份、build 和诊断，`stopped-diagnostics.json` 的 all_stopped 为 true。
- 原严格质量结果仍为 false，原始启动稳定窗口超时保留；四板 receive_missing 增长、VDC deadline 及部分从板 RefMem 超预算/隔离等均列 WARN，未改为数据零缺失或锁相通过。实际基本新帧运输、矩阵读回、原生采集/STOP 与源码指纹通过，故本轮固定范围调试验收可完成。详见 `p3-r1/alarms.json`、`diagnostic.json`、`acceptance.json` 和 `fixed-scope.json`。
- 后续工具加固记录：独审提出可进一步在凭证校验器中直接比较 diagnostic 原件的 failures 与 receipt 数组，防止手工同步重写摘要数组造成遗漏。当前生成器使用同一数组，本轮由独审直接核验三方一致；该额外加固不扩大本轮验收范围，不隐去原始质量问题。
- 下一 gate：验收工具切片完成后回到 `VDC-FAST-002`。运输/调度的严格质量问题继续按主线处理，不将本工具优化扩为锁相实现或重新开放 RAM 缩容。
- 提交记录：用户授权先提交当前改动。`e6ff06a` 包含当前固件、工具、测试和凭证；提交前 `check-staged` 及提交钩子均实际核验匹配源码/原件，非此前“无 staged 代码”的跳过检查。文档单独提交，未 push；后继最小同步记录与 Core1 接线仍在只读审计，未混入实现。

### VDC-PROGRESS-20260916-043：RefMem 缩容专项完成与主线接续

- 对应 `REFMEM-RAM-001/002/003/004` 和 `VDC-FAST-002`。用户授权的 RAM 插入任务已实施，详细事实由 `REFMEM-TASK-20260916-001` 承接；042 中“尚未改动”仅为当时移交状态。
- 实测快照，非容量事实源：RefMem 静态表缩至 18 KiB，A/B 各净释放 46 KiB；build `20260916151903` 已部署 NO1–NO4，四板 STOP 后布局版本、容量、初始化、目录有效标志及八逻辑槽读回全部通过。OTA 实现与 DPLL/VDC 有效载荷、owner 和同步机制保持。
- host 兼容矩阵和独立复核通过，真实旧布局包由 owner 拒绝，新包正常装载，active/rollbackable 保持。当前六节点产包须显式指定 `--tdma-node-count 6`，SD 整包工具默认八节点的限制保留。
- 当前源码四板 quick P3 调试流程完成，严格质量检查仍失败：启动稳定窗口超时、持续运输与 VDC/RefMem 调度问题未闭合，不能宣称产品严格验收或锁相通过。全部证据在 `out/HardwareAcceptance/20260916/refmem-static-shrink-r1/`；缩容前 r5 原件保持不变。
- 范围纠正：用户指出验收范围不能随调试扩大。r5 与本轮 P3 profile/配置指纹一致，快速凭证校验器本来允许保留质量失败；此前据此把 RAM004 保留未完成，是混淆快速调试验收与严格产品验收。现 `REFMEM-RAM-001/002/003/004` 均按既定专项和快速流程完成，原始 strict 失败仍保留，不更改脚本或报告掩盖失败。
- 下一 gate：回到 `VDC-FAST-002`，先核对 STOP 后 Flash/XIP 实际配置，再以单项可归因改动收敛运输与调度。后继修复须重新 host、Release/resources、四板 P3 及预先明确的专项；不重复缩容、不改 OTA、不引入普通解析/RefMem 分片/RTOS 前置，不把新诊断发现自动变为当前切片的退出条件。

### VDC-PROGRESS-20260916-042：空通道调度减负与 IRQ 分段实测

- TODO task ID：`VDC-FAST-002`；日期：2026-09-16，保持 IN PROGRESS。同一接收入口失败切片继续收敛，未接入紧凑发送、Core1 时间配对或新的 DCO 控制。
- Core1 在 priority IRQ 关闭时经 TDMA owner 读取精简计数；inactive 的非 TDMA 相位复用已确认状态，跳过 IRQ 等待及统计。TDMA 前后仍读取实际状态，识别本拍 ARM/STOP；不改变静态周期、相位预算、入口截止判据或完整墙钟收费。原先空通道完整快照与等待是调度负担，但不能据此认为所有迟到均已解决。
- 新增固定 IRQ 四段计时：入口至复制完成、复制后 DMA/外头复验、记录校验、发布至原主体结束；ARM 清零、STOP 保留，只统计成功发布且时钟跨度有效的记录。`SYSTem:TDMA:FLIGHT:PRIORity:TIMing?` 只在 transport STOP 且 lane inactive 后读取有界快照，RUN 不用 SCPI 采样。各段最大值可来自不同样本，不得求和作为 WCET；原 IRQ 主体仍不包含 finish、诊断统计及返回，完整相位墙钟继续包含这些成本。
- 软件与资源快照，非容量或时序契约：联合 17 项 pytest 通过，包含实际 dispatcher 的 136 个 C 用例及 physical 的四配置矩阵；独立复跑 5 项通过。A/B Release 与 Flash 链接通过；分段统计增加 24 B 主 RAM，IRQ 自身栈增加 16 B。新链接重算 NO1 最深链余 200 B，从板保守含 IRQ/扩展异常帧余 356 B，SCRATCH_X 间隙 24 B，主 RAM 保留 2 KiB heap 后余 28 B。放行仅适用于现有四板 persona、MODEL/TRIGGER_MEASURE 禁用、trigger AO IDLE；不是全系统栈证明，候选 IRQ 尾预算尚未实证。
- 四板 `p3-r5` 诊断流程完成，`strict_gates_passed=false`，启动稳定门禁超时的原始失败保留。实测窗口 VDC run 增量为 NO1 2666、NO2 366、NO3 418、NO4 475；前轮四板均为零，表明空通道调度修复有实际收益。VDC/RefMem 仍有 deadline 增长，NO2 RefMem 有 overrun/隔离。普通 process-image 的 receive_missing 增量为 3/2/4/3，NO1 反馈序号超窗，NO3 在周期采样中有 DOWN/recovery；因此运输和调度均未通过，不能以独立 priority 序列 gap 为零替代整体环路健康。四板 STOP 确认，末态未观察到看门狗重启。
- IRQ 快照：三从各 666 次 IRQ、661 次成功发布，序列 gap 为零；各 5 次拒绝包含启动过程，不能解释成稳态丢帧率。主体均值约 119–122 µs、峰值约 197–200 µs；各段最大值范围分别约 92–101、24–25、84–85、45–49 µs。末尾保留原始记录读回通过，仍为普通邮箱类别而非已接通的 DPLL 同步记录。全体分段皆有显著成本，当前证据不足以将 XIP、CRC 或某一函数独立认证为根因。
- 当前 build 为 `20260916145029`，源码指纹 `bad9841ced42cffd559e6972adee834550653042aaaae7142d613fc20de8b810`，包 SHA256 为 `3420454d24a62167be559a6d14cecae7cd83fcc7b55c47ae65dc44c45bfc70a7`。旧 build ID 的中间编译保留，实际硬件使用独立新标识和冻结包；未修改 OTA 实现、未重测已知线序。证据位于 `out/HardwareAcceptance/20260916/dpll-fifo-ingress-r1/`：`host-combined-r5.xml`、`measured-build-r5-identified/`、`independent-review/source-and-resource-r5-identified-review.json`、`p3-r5/acceptance.json`、`stopped-diagnostics-r5.json`、`irq-timing-readback-r5.json`、`priority-readback-r5/summary.json`、`comparison-r4-r5.json`。
- 下一 gate：按已测分段及实际链接定位固定复制/校验/发布的成本，优先选一个可归因优化，再软件、资源与四板 quick P3；继续记录剩余调度迟到。REFMEM 静态缩容评审独立进行，不把 lease/普通分片引入特等席，不用放宽时间门禁替代实测修复；主线目标保持 active。
- 接续移交：用户随后授权将 RefMem 评审转 TODO、先释放 RAM，并要求保存 VDC 接续入口。当前 RefMem 生产/测试仍未改动，尚无缩容构建或硬件证据，下一步按 `REFMEM-RAM-001/002/003/004` 推进后返回本切片。实际测试入口和 Python 产包器布局版本联动风险见 `out/HardwareAcceptance/20260916/refmem-static-shrink-r1/event-core-test-handoff.json`；旧包版本拒绝、active/rollback 保持和当前字段访问上界须验证。独立最终硬件报告为 `independent-review/hardware-r5-review-r2.json`，完整确认本轮运输回退。
- 后续只读诊断候选：`irq-cost-r5/analysis.json` 与 `placement-and-measurements.json` 已绑定当前冻结 ELF，建议 STOP 后读取 XIP_CTRL、cache hit/access 计数和 QMI timing/read format/read command，使用 Core0 局部输出，无需新增静态 RAM；该接口尚未实现。读取配置和计数没有写副作用，不读消费型 FIFO、不写或重配 Flash/XIP。当前 ELF 的 boot2 缺省及 BOOTRAM 恢复路径不能证明板端正使用慢读模式，更不能据此归责 OTA；需实际寄存器证据后再选优化。

### VDC-PROGRESS-20260916-041：快速通道接入的栈重叠修复

- TODO task ID：`VDC-FAST-002`；日期：2026-09-16，保持 IN PROGRESS。本轮仍收敛同一独立接收入口切片，尚未接通紧凑发送和 Core1 时间配对。
- `tdma_service_core1_service()` 将通用调度与待处理 intent 的大缓冲分置于独立调用；空 intent 不进入 payload 缓冲。adapter 将 prepared RX 与同步 packet、异步 overlay 与兼容模型缓冲分开，避免互斥路径的大栈帧叠加。保留原 flight engine 映射快照及并发语义；没有采用本轮评估的紧凑 map 改写。
- 资源方案引用 `project_configure_app_target()` 中的 `PICO_CORE1_STACK_SIZE` 及两槽链接脚本的 `__scratch_x_end__ <= __StackOneBottom` 断言。快照，非容量契约：Core1 预留由 2 KiB 改为 3 KiB，使用现有 SCRATCH_X 空隙，不增加主 RAM 或堆；仅扩大栈仍不足以容纳原始深链，因此保留上述生命周期拆分。A/B Release 和 Flash 链接检查通过，最终调用链与硬件结果继续逐项复核，不宣称全系统栈上界已证明。
- 软件原件位于 `out/HardwareAcceptance/20260916/dpll-fifo-ingress-r1/`：`service-stack-delivery-r2.json`、`service-stack-host-r2.xml`、`priority-host-final-r4.xml`、`release-build-r4.log`、`stack-linked-r4/`。service 相关 79 项、快速通道及接口 16 项回归通过。adapter 旧夹具将已合法的 boundary-command 类视为非法的失败已保留；改用实际非法类并加入合法类及异步/注入队列优先级用例后，O3 的 6/8 节点与 baseline/current 四组完整单元均通过，见 `event-core-stack-final-r1.json`。baseline 对照仅替换 adapter/engine，其余源码与最终测试相同，不称为全仓历史重验。
- 独立链接审核快照（非全系统栈证明）：两槽 NO1 首次 beacon 为 2872 B、余 200 B，NO1 不安装 follower priority IRQ；从板异步 TDMA 保守计入 IRQ 及扩展异常帧为 2700 B、余 372 B；generic TX 为 2220 B，ARM/STOP 分别为 1968/1360 B。SCRATCH_X 数据为 1000 B，距新预留栈底余 24 B；该物理分区间隔不是调用栈余量。实读四板 load mask 均为 91；另有越栈的可选 TRIGGER_MEASURE 和未用 MODEL 路径不在本轮配置放行范围，见 `independent-review/qualified-stack-r4-candidate.json`、`pre-p3-stopped-r4.json`。
- 四板结果：`p3-r4` 的过程数据节点、启动稳定性和持续收发均通过，稳态没有新增 bad/reject/missing；四板各保存完整原生记录，STOP ACK 与配置代际匹配，未观察到看门狗重启。三从 priority 发布分别为 795/794/795，末尾保留记录共 12 条经独立 CRC 重算通过，同序三从邮箱逐字一致。但邮箱类别为普通过程数据 `0x10`，只证明独立运输/保全，不代表 DPLL 时间戳或 Core1 事件匹配已接通。源码指纹 `786ca3d323ba7b29a5c374121a09fc4a030fbf775ab69d067b303190a6567ced`，实际 build 为 `20260916141927`；原先读回 wrapper 误传旧 build 的 host 断言失败保留，按正确 build 重读相同冻结记录通过，见 `priority-readback-r4-correct-build/`。
- 门禁仍未通过：`strict_gates_passed=false`，当前硬失败在 VDC/RefMem 调度，四板 VDC 在采样窗口内 run 增量均为零；NO2/NO4 的 RefMem 另有 overrun 与隔离。三从 IRQ 均值约 109–114 µs、最大约 181–199 µs（快照，非 WCET；尚不含完整尾部/返回），超过现有候选预算。NO1 未安装该 IRQ 也发生 VDC 饥饿，不能把全部相位错失归因于 IRQ。`BUDGet` 的相位 peak 是 ARM 全局高水位归属，不是各相位独立峰值；`sample_failures` 同时含时钟/归因失败，不能等同内存快照读取失败。见 `stopped-diagnostics-r4.json`、`independent-review/hardware-r4-review.json`；映像保存在 `measured-build-r4/`。
- 下一 gate：同一失败切片内缩减无 lane 的相位交接及完整快照开销，保留静态周期、墙钟门禁、配额和 ARM/STOP 语义；用板端最小分段计时定位 IRQ 耗时。修复后重新软件、资源、四板 quick P3 与冻结读回，未通过前不扩展紧凑发送或 DCO 接线。

### VDC-PROGRESS-20260916-040：独立接收入口实现及四板失败定位

- TODO task ID：`VDC-FAST-001/002`；日期：2026-09-16，保持 IN PROGRESS。本切片先保全现有 CRC 邮箱原始记录，尚未接入紧凑事件发送、Core1 时间配对或新的 DCO 控制；不以原始槽位替代单圈交付及锁相验收。
- 实现范围：follower 帧末 IRQ 从既有 DMA SRAM 按冻结几何复制固定头与参考邮箱，复验覆盖边界、CRC、来源及目标后发布独立索引槽位；DMA 仍唯一读取 DATA FIFO，普通 Core0 RX tail 不共享。记录序号跨零退休旧 epoch，单写者 guard 正常回绕；STOP 后保留可读记录。应用层按静态相位开放有额度及截止时间的 IRQ 窗口，保留完整墙钟门禁，候选 IRQ 预算尚未实测通过。
- 软件及资源快照，非事实源：接收/游标/SCPI 回归 84 项、应用及 TRN-03 回归 136 项、最终主控和独立组合检查各 21 项通过。首次 A 链接主 RAM 越界 500 B 原件保留；仅将新增槽位、IRQ 工作区、调度工作区及统计迁入 SCRATCH_X，并去掉重复记录的整份栈复制。后继 A/B Release 通过，预留堆后主 RAM 剩 52 B，SCRATCH_X 数据距原 Core1 栈底 1088 B；已审 DPLL 前景叠加 IRQ 与扩展异常帧为 1988 B，原栈为 2048 B。该选中路径审核不是全系统栈证明，TDMA 回调路径继续核查。
- 硬件失败：`p3-r1-console.log` 记录误传已复用拓扑摘要，工具在硬件操作前拒绝；改用原始测量副本后执行 `p3-r2`。源码指纹 `cf005479c640768e97a40ccaadf110824cffcc4b4b66ff97d7d326e93c118c3a`，四板部署 build `20260916131239`；quick 流程完成且生成诊断凭证，但 `strict_gates_passed=false`。SCK 校验、重放行选择、四板闭环和停止记录交接均有失败；不得以进程零退出或凭证 `passed` 宣称本切片验收通过。
- STOP 后只读定位：NO2–NO4 均有 `WATCHDOG_TIMEOUT / CORE1_STALL`，末态调度计数重新起算；NO1 保持连续运行。看门狗残留进度在 RefMem 或同步触发服务完成之后，不能据此断言具体 IRQ 指令为根因。另发现新调度准备路径导致大量相位入场错过，NO1 优先状态查询返回 execution error；重启后三从查询正常但记录全零，不能当作成功接收或丢包计数证据。
- 证据统一位于 `out/HardwareAcceptance/20260916/dpll-fifo-ingress-r1/`：`release-build-r1/r2.log`、`host-regression-r2.xml`、`app-priority-host-r5.xml`、`host-final-root-r1.xml`、`linked-resources-r1/`、`independent-review/final-code-review-r2.json`、`p3-r2/acceptance.json`、`readback-r1/summary.json`、`stopped-diagnostic-r1.json`、`watchdog-r1.json`。失败固件、包和链接原件冻结在 `failed-build-r2/`，不由后续增量构建覆盖。
- 后继栈审核纠正（快照，非事实源）：旧审计漏识别 ARM `subw sp`，上述选中 DPLL 路径数字不得用于当前固件整体放行。A/B 实际 prepared RX 调用链为 3656 B，超过原 Core1 栈；反汇编及地址计算证明，该可达路径中 `inspect_input` 的 map 复制会覆盖新 priority 表的 436 B，是否触发本轮 WDT 仍待实板复测。旧已验收映像相同链为 3616 B，当时 SCRATCH_X 无新增数据；旧验收不能证明该潜伏越界安全。当前映像未启用 MSPLIM 栈保护，不声称保护机制已捕获故障。证据为 `tdma-stack-overlap-review-r1.json` 与 `tdma-stack-baseline-comparison-r1.json`。
- `p3-r3` 的四板 OTA 完整成功后，主控终止协调器，未进入后继环路试验；没有中断 OTA 子进程。四板 UID/build 及 transport STOP 已逐一只读确认，部署映像与包冻结在 `stack-stop-r3/`；该轮不计 P3 通过。原件见 `p3-r3-stack-stop.json` 和 `stack-stop-r3/summary.json`。准备开销、单写者 RMW 和 IRQ 标记修复已编译，但尚无当前源码硬件闭合证据。
- 下一 gate：拆分 TDMA generic intent、prepared RX 与 overlay 的大局部缓冲生命周期，先消除已证实的栈写覆盖，再复核前台及 IRQ 栈叠加。四板目前 STOP，不推进紧凑发送或 Core1 控制接线。修复仍在同一功能切片内，须重新软件、Release、匹配源码 quick P3 及接收专项；不重跑已测线序、不改 OTA 实现。

### VDC-PROGRESS-20260916-039：FIFO 直接匹配候选的可执行验证

- TODO task ID：`VDC-FAST-001`，保持 IN PROGRESS；日期：2026-09-16。
  以下数字为离线实验及源码分析快照，非冻结 wire、容量或物理时序契约。
- 按用户最新方向收敛为 TX 定额提出预编码记录、RX 本圈压入特等席槽位、Core1
  直接索引匹配。现有 `tdma_flight_fifo` 的 RX 是 Core1 生产/Core0 消费的普通
  镜像，不能把同一 tail 同时交给 Core1 和 Core0；复用其双缓冲/代际交接思路，
  特等席有明确的消费归属。PIO FIFO 继续由现有 TDMA/DMA owner 服务，CPU 不抢读。
- `compact_candidate.c` 是 out 内可执行设计实验：在既有邮箱位置组合事件低位、
  时间低位及上下文/有效标记，复用生产 `tdma_process_image_crc16_ccitt()`。
  实验 class 不获现有运输准入；用源时间窗口唯一恢复时间高位，用已合格外层
  物理事件序号恢复事件低位，保留原时间区间并向外扩大量化误差，不取中点。
  主板一次广播供三从使用，无通用分片；完整物理邮箱长度没有缩短。
- 独立 Python wire/CRC oracle 与 C 实验共 1309 个用例通过，覆盖三从同一编码、
  序号和时间低位回绕、每一位损坏、有效 CRC 下错误身份、时间歧义/溢出和失败
  输出不变。生产 `vdc_feedback_match_cache_lookup()` 原本即按 seq 掩码直索引；
  实验复用真实缓存验证槽位别名覆盖、完整序号检查及 STOP 退休，没有重建巨表。
- Cortex-M33 的候选编码/解码 object 为 468 B，data/bss 均零；编译器报告各函数
  静态栈 56 B。该资源证据仅覆盖候选函数，不含硬件入槽、完整 IRQ 调用链、
  生产索引缓存分配或后续 PI，也不是运行 WCET。未修改固件、未部署、未运行新 P3。
- 模型语义审查修正此前“先冻结 NO1 模型”的建议：源端编码事件当时实际输出时间，
  普通连续 rate/PI 修订属于源端私有状态，不必每次交换远端模型上下文。
  现有主 PI 重定基没有连续性比较，现有 LOCAL_FOLLOW 在任一模型 token 变化后
  重建最小时间窗口；接入新消费者时须分别处理真实时间轴跳变及普通模型修订，
  不能照搬同 token 门禁而使持续 PI 下的窗口永远无法形成。该发现尚未修复。
- 接收保全按实际物理 cadence 计算：此前 819.2 µs 是连续 10 MHz 字节流的保守
  DMA 覆盖算术，不是当前线上已测覆盖时限；约 240 µs 占线时间也不等于发车间隔。
  普通整表轮询不能单独证明本圈 FIFO 交付，应使用已拥有的帧边界和有界装卸入口，
  并实测其固定干扰预算、停止收敛和到站期限，不把完整 PI 搬入中断。
- 证据目录：`out/HardwareAcceptance/20260916/dpll-deterministic-lane-r1/`；入口为
  `compact-candidate-summary.json`、`compact-candidate-cases.json`、`tx-design-review.json`、
  `rx-design-review.json` 和 `model-continuity-review.json`。后者覆盖前者冻结模型建议。
  下一步接通特等席 FIFO 的硬件到站生产入口及 Core1 消费边界，依照逐切片规则验证；
  不宣称候选已经完成单圈交付、持续闭环或锁相。

### VDC-PROGRESS-20260916-038：实板采用与外触发证据归档，主线转向预编码确定性时间戳

- TODO task ID：`VDC-LOCAL-003/004`、`VDC-FAST-001/002/003`；日期：2026-09-16。
  本条数字为证据快照，非已冻结 wire 格式、资源预算或精度契约。
- 用户再次确认从板本地跟踪：NO1 发布闭环时间戳，各从加本地已测 delay，
  各自形成误差并由 Core1 应用 DCO；不恢复主机逐从计算校正的旧路线。
- 在 037 的同一固件上完成附带示波器的本地跟踪采集，NO2 原生 RAM 和 MODEL
  确认 DCO 序号 1→2、频率 0→+207 ppb，remote command 序号仍零。
  NO3/NO4 本轮无候选，NO4 保留此前 +206 ppb；参考接收、匹配 ACK 和 ACK 发布
  的增量 NO2 均为 5，NO3/NO4 均为 4。原生 CRC、串口读取重建和解码经独立复核。
  NO3/NO4 首次 STOP 回执异常保留，后续 STOP、模式清除和 SELFtest 关闭确认；
  原 `acquisition_passed=false` 不因局部采用或波形恢复而改写。
- 示波器冻结波形恢复完整，每路 10M 点、20 ns 间隔、窗口 200 ms。初次导出将
  PRE 的传输区间点数误作总深度而断言失败，续读同一冻结记录时未重新 RUN/ARM。
  稀疏诊断输出有时序跳动，且本帧未证明覆盖 NO2 更新后的输出；不能据此判断
  零步长合理或锁相。NO1 OUT4→EXT 的独立 STOP 态验证通过：单独 CH1 边沿
  后仍 WAIT，OUT4 标记触发，四路原始波形完整；这不是运行态独立 marker 实现。
  现有全组 MASK/通用输出调度会干扰 OUT1，独立 OUT4 owner 仍是后续测量工作。
- 用户进一步要求：DPLL/VDC 锁相关键时间戳必须走确定性发布，尽量压缩数据，
  以已经编码好的固定记录交给 Core1 直接完成最快的有界匹配。
  后续进一步指定类似 FPGA 查找表：事件序号直接索引固定槽位，核对完整身份和
  上下文后计算差值；表保存尚未配对事件，不按每个可能的时间值分配表项。
  特等席目标是本圈已装载的有效记录在同一物理圈内送达目标板的 Core1 匹配入口；
  记录的事件年龄、运输用时和实际 DCO 应用边界分别验收，不能将上一圈时间戳
  误配成本圈事件，也不能把单圈运输表述为已达到最终物理同步精度。
  用户明确普通解析、RefMem 分片和 RTOS 任务属于后续一等座、二等座及无座路径：
  其他 VDC 跟随数据、RefMem 数据、普通控制和日志分级推进；特等席仅承载锁相
  必需的最小同步记录及其必要检查，不以前述普通处理路径的完善作为闭环前置。
  原 TDMA 域“特等席独立快速通道”已有固定字段与受保护记录要求；现行参考
  发布却在 `distributed_refmem_tdma_flight_sync_publish()` 用 OSAL 逐片节流，
  `distributed_refmem_feedback_published()` 推进的是 FIFO 发布，不能当物理发送。
  RX 还经过 `tdma_rx_prepare_t` 普通工位和可丢弃 `tdma_flight_fifo` 镜像。
  仅换硬件时钟、延长分片驻留或加快末端匹配都不足以证明端到端确定性。
- 下一 gate：`VDC-FAST-001`。优先复用已有同步编码/固定位置，区分逐事件必须
  携带的信息与提前安装的上下文；低位时间、短序号或模型标记不能未经回绕/代际
  证明直接截断，不得压掉仍有意义的时间区间误差。逐事件生成与到站保全由 TDMA
  owner 的硬件或有界快路径负责，Core1 固定索引匹配及控制，Core0 配置/低频
  元数据/记录不成为逐片执行前置。该方向尚未实现，不宣称新快路径或四板验收通过。
- 证据：`out/HardwareAcceptance/20260916/dpll-scope-hold-r1/` 下的
  `capture-r2/input-probe.json`、`capture-r2/scope-recovered/`、
  `independent-dpll-capture-review-r2.json`、`independent-scope-recovery-review-r2.json`、
  `ext-io-r2/` 和 `ext-output-owner-review.json`。确定性路线只读设计证据单列于
  `out/HardwareAcceptance/20260916/dpll-deterministic-lane-r1/`；本条不修改登记表
  状态、不冻结新 wire/资源契约、不改变已验收固件。

### VDC-PROGRESS-20260916-037：参考发布节奏改善三从运输，本地保持原因可追溯

- TODO task ID：`VDC-LOCAL-003/004`；日期：2026-09-16；前者保持 IN PROGRESS，
  后者仍 PENDING。本条数字为实测或分析快照，非产品时序、容量或精度契约。
- Core0 在 REFERENCE 模式统一使用 `DISTRIBUTED_REFMEM_REFERENCE_PUBLISH_INTERVAL_MS`
  发布下限，覆盖参考、ACK 和末片后的普通分隔帧；只在实际到期且 FIFO 可用时冻结
  新事件，活跃组的 ARM 复验先于节奏及背压返回。保持原片序、CRC、成功后推进、
  STOP/config/session/role/clock 取消与唯一 owner；无新缓冲、PIO 或 Core1 等待。
  当前值为 6 个 OSAL 名义毫秒。其首末片名义间隔为 90 个 OSAL 毫秒，不能解释为
  已测物理驻留或本地事件年龄；`VDC_LOCAL_FOLLOW_MAX_AGE_MS` 未放宽。
- 软件：补充最后三项前相关回归 261 项通过，最终节奏测试 16 项通过；独立运行
  22 项通过，去掉限速的旧行为红测按预期失败。覆盖 ACK、末片、提前冻结、时钟回绕、
  FIFO 失败/背压和 pending 期间 ARM 取消。首轮新夹具误用不存在的 codec 接口、
  次轮投影 stub 固定旧 role/clock 的失败保留，修正只在测试夹具。
- A/B Release 和 Flash 链接通过，BSS 尾地址和栈边界与 036 相同。首次 PowerShell
  日志把 SDK stderr 提示包装为 NativeCommandError；后续 P3 构建实际退出为零并
  绑定最终产物。源码指纹见 checkpoint，文件数 1183，build `20260916113003`。
  `p3-r1` 复用已测线序，四板 OTA、8 次 P3、TDMA 四门均通过，
  `passed/strict_gates_passed=true`、失败列表为空，耗时 197.011 s。
  NO1 DPLL overrun/deadline 增量 103/96、NO3 1/1 保留，quick gate 不证明全相位 WCET。
- P3 后一次记录软件复位，沿用 036 的原矩阵直接 TDMA 四门通过；两次专项使用
  冻结 PHYS/FIFO/RefMem 采集器，运行期零查询，全部 STOP 后导出 RAM。第二轮不
  复位，真实 baseline 保留首轮 DCO 前态，不能将已有调节当作新采用。

  | 轮次 | 节点 | 完整参考增量 | 主板匹配 ACK 增量 | 本地 observation / 非零采用 |
  |---|---|---|---|---|
  | 8 s | NO2 | 8 | 7 | 1 / 0 |
  | 8 s | NO3 | 13 | 13 | 1 / 0 |
  | 8 s | NO4 | 8 | 7 | 1 / 1 |
  | 10 s | NO2 | 11 | 11 | 2 / 0 |
  | 10 s | NO3 | 19 | 19 | 3 / 0 |
  | 10 s | NO4 | 16 | 16 | 3 / 0 |

- 两轮逐从运输检查均通过。首轮 NO2/NO3 的原生误差区间分别为
  [-7177,1879]/[-8101,732] ppb，Core1 记录零步长、`applied=false`，DCO 序号
  保持 1；不是尚未进入本地控制。NO4 区间 [-9346,-826] ppb，按生产保守边界
  计算得到 +206 ppb，DCO 序号 1→2、MODEL token 1→2，remote command 序号仍零。
  三从记录的有向 delay 分别为 242/162/82 ns，与原矩阵一致。
- 第二轮所有候选仍跨零，均零步长保持；NO4 保持前轮 +206 ppb/序号 2。
  最窄接近单侧的 NO3 区间仍为 [-5723,19] ppb，不能取中点或截宽制造采用。
  两轮原 assessment 均 `passed=false`、`errors=[]`：运输通过，但未满足三从
  实际采用；不改写为锁相通过。PHYS 观察丢弃仍大量存在，不能把参考增长直接
  当作逐分片无损或精确因果证明。
- 独立原生复核重算两轮误差区间、RAM CRC、MODEL 原始端点及 ACK；零步长与
  STOP 后 endpoint 的 BINDING 拒绝计数分开，不将后者当成零步长原因。
  发布计数的两次停止态读取外窗也不足以支持物理 6 ms 下限。当前 ELF 显示
  runtime 初始时钟为 150 MHz，SysTick 按调度器启动时钟配置，而任务内 board_init
  才切换到 250 MHz；这支持 OSAL tick 加速的源码解释，但未实读 SysTick reload，
  不将推算比例当成硬件测量。名义节奏改善与物理时基修复必须分开验收。
- 证据根：`out/HardwareAcceptance/20260916/dpll-fragment-spacing-r1/`。
  入口为 `software-and-resource.json`、`spacing-tests-independent-review-r1.json`、
  `independent-design-review-r1.json`、`p3-independent-review-r1.json`、
  `independent-capture-review-r1.json`、`independent-clock-domain-review-r1.json`、`p3-r1/`、
  `firmware-manifest.json`、`pre-capture-reset.json`、`tdma-direct-r1/`、
  `capture-r1/r2`、`analysis-r1/r2.json` 和 `capture-comparison.json`。
  代码与凭证提交 `22a22b5`，文档分离；两轮末态均全板 STOP、参考/本地跟踪/会话
  清零、临时许可证撤销。OTA 实现及 NO5 未改。
- 下一 gate：先核对发布 OSAL 名义毫秒与真实时间的初始化关系，不能由发布计数
  除以请求采样时长推导实际驻留；该时基核对不升级为新的全局重构前置。
  继续 `VDC-LOCAL-003` 的真实桥接窗口缩窄和新鲜配对连续性，使 NO2/NO3 能形成
  有可信方向的非零本地调节；每个功能切片各自 P3。三从持续收敛、NO1 自主 PI、
  100 ns 输出及恢复尚未完成，不用重复 P0T、首帧精细证明或 SD 故障阻塞推进。

### VDC-PROGRESS-20260916-036：当前源码 quick P3、提交收敛及物理层观察缺口

- TODO task ID：`VDC-LOCAL-003/004`；日期：2026-09-16；`VDC-LOCAL-003`
  保持 IN PROGRESS，`VDC-LOCAL-004` 仍 PENDING。
  本条数字均为单轮证据快照，非产品时序或精度事实源。
- 新增 quick 四板 `--reuse-topology` 入口：构建/OTA 前冻结原始通过的 P0T 原件，
  检查线序、anchor、有向邻接、节点号与已提交 node map；复位后重新读取当前
  UID/build/NO。凭证穿透核对冻结原件和 reset 身份，不把复用标成新测量。
  后续 P3 链路和 TDMA 仍实测；没有改 OTA 实现，没有操作 NO5。
- 软件与构建：验收入口最后小修前本批相关 host 回归 573 项通过；该入口最终
  76 项及独立
  92 项通过；A/B Release 和 Flash 链接通过。原接口缺失红测、首次 pytest
  目录夹具失败及独审发现的 UID-change 留证缺口均保留。新独立构建目录为
  `out/build/p3-known-topology-r1/`，不再复用会覆盖旧凭证包的构建产物路径。
- `p3-known-topology-r1/p3-r1` 的源码指纹为本页 checkpoint 所列 036 阶段指纹，build 为
  `20260916110233`；四板 OTA、8 次 P3 测量及 TDMA 四项 gate 全通过，
  `strict_gates_passed=true`、`diagnostic_failures=[]`，各板原生记录 14 条。
  流程耗时 325.574 s，包含冷构建和 OTA；这不是 8 s 专项采样耗时。
  DPLL 观察仍为 TDMA-only 范围；NO1 局部 DPLL overrun/deadline 增量
  99/95 作为 Debug 诊断保留，不能由 quick gate 推断所有相位满足预算。
- 代码与新凭证提交为 `6530114`，共 51 个路径；提交前及 hook 的 staged
  指纹检查通过，源码原始字节保留。无关 HTML 未纳入；文档单独提交。
- 为沿用 034/035 的同一已测矩阵，专项前再次直接验证 TDMA。
  `tdma-direct-r1` 在 START 前遇到 recorder ARM ACK 超时，原件显示旧记录器
  已完成状态；四板均 STOP。一次软件复位后 `tdma-direct-r2` 四项 gate 全绿，
  无 P0T，不追认首次失败。
- `dpll-physical-counters-r1/capture-r1` 完成 8 s 自主运行和四板 STOP 后 RAM
  导出。新增 PHYS 双读仅在 baseline/end 进行，109 字段与生产 SCPI 顺序核对；
  FIFO/RefMem/PHYS 两次端点读值一致，运行中零查询，模式/会话/许可证已撤销。
  正常 baseline 在物理 ARM 后，计数均零；原始 runtime 缺少 adapter start
  generation，故按末次物理 ARM 的 end 累积量报告，不强称独立证明同 ARM 差分。

  | 节点 | PHYS 观察丢弃 | RX ring overrun | FIFO 镜像丢弃增量 | 完整参考增量 | 新本地采用 |
  |---|---|---|---|---|---|
  | NO2 | 2259 | 897 | 0 | 0 | 0 |
  | NO3 | 2334 | 738 | 0 | 1 | 0 |
  | NO4 | 2398 | 602 | 0 | 6 | 1 |

- NO4 复位后的原生 observation/commit 与 MODEL 一致：rate 0→411 ppb、DCO
  序号 1→2、remote command 序号仍零，有向 delay 为 82 ns。本地端点宽度
  2231/2231 ns；这是新一轮 +411 ppb，不是先前 +450 ppb 的状态重读。
  原冻结 assessment 仍对 NO2 未留存的零占位 wire 报 CRC mismatch，原 FAIL
  保留；专项整体没有证明三从持续闭环，缺数据不能解释为线上坏 CRC。
- 证据：`out/HardwareAcceptance/20260916/p3-known-topology-r1/` 的
  `known-topology-source-independent-review-r2.json`、
  `known-topology-hardware-independent-review-r1.json`、`pre-stage.json`、`p3-r1/`；
  以及 `out/HardwareAcceptance/20260916/dpll-physical-counters-r1/` 的
  `independent-review-r1.json`、`independent-capture-review-r1.json`、
  `firmware-manifest.json`、两轮直接 TDMA、复位原件、
  `capture-r1/input-probe.json`、原生 RAM 和 `analysis-r1.json`。
- 下一 gate：`VDC-LOCAL-003`。先以最小切片验证 typed 分片驻留时间是否覆盖
  两阶段 RX 观察间隔，并核对历史容量、片组年龄和 STOP 取消；观察丢弃是
  FIFO 前的证据，尚非每个缺片的因果证明。保留严格片序、CRC 和独立本地校正，
  不能把 FIFO 发布时间当物理驻留起点，也不能只按装配超时放慢而越过
  `VDC_LOCAL_FOLLOW_MAX_AGE_MS` 的本地事件年龄门禁。
  不因这组计数直接扩大 FIFO 或放宽配对。通过新源码四板 P3 后，以三从完整参考、
  ACK 和实际 DCO 更新联合验收；持续斜率、NO1 PI、100 ns 及恢复仍待完成。

### VDC-PROGRESS-20260916-035：停止态 FIFO/RefMem 差分缩小定位范围

- TODO task ID：`VDC-LOCAL-003`；日期：2026-09-16；状态保持 IN PROGRESS。
  仅扩展 `out/` 冻结采集包装器，生产源码仍为 034 的 `95853bce…`；无新固件。
- 在全板 START 前和确认全部 STOP 后双读 FIFO、RefMem，保留原 raw、action
  索引与重复读值；MODEL 精确 timeout 只在停止态有界重试一次，格式错误不重试。
  相关 27 项和独立 34 项通过；8 s 四板采集、RAM 导出与撤销清理完成。
- 当轮快照：四板 FIFO 镜像丢弃增量均为零，RX 发布/领取/释放分别同为
  2623/2334/2399/2468，末态队列为空；RefMem 无坏邮箱或拒收。
  两个 FIFO drop 字段是同一计数别名，parse 字段是当前占用，不能累加为丢包。
  RefMem 双读一致只说明端点观测稳定，不证明旧 scalar getter 为联合一致快照。
- NO2/NO3/NO4 完整参考增量为 0/2/4，无新 LOCAL 记录；NO4 baseline/end
  均为已有 rate 450 ppb、DCO seq 2，不重复认领为本轮采用。未留存 wire 的
  assessment CRC 错误仍保留，不能推断线上数据损坏或 DCO 算法拒绝有效输入。
- 证据根目录：`out/HardwareAcceptance/20260916/dpll-transport-counters-r1/`，
  包括 `capture-r1/`、`analysis-r1.json`、`hardware-independent-review-r1.json`
  与 `tx-rx-path-review-r1.json`。只读路径审查确认正常 TX selection/退休保护，
  但 TX acquire 不等于物理发送完成；FIFO 前的 origin latest bank、follower
  扫描 clamp 仍可遗漏观察。typed REFERENCE 外层 target mask 是广播，真正目标
  位于完整内层记录，旧“其他目标在组装前必被过滤”的概括已纠正。
- 下一 gate：读取现有停止态 PHYS 计数，先区分物理观察遗漏与 FIFO 交接损失；
  不把聚合计数当片组身份，不新增无证据的 RAM 扩容。后继结果见 036。

### VDC-PROGRESS-20260916-034：缩短桥接采样窗口及 NO4 首次本地采用

- TODO task ID：`VDC-LOCAL-003/004`；日期：2026-09-16。将桥接内部通用 raw
  reader 改为直接 TIMER1/TIMER0/TIMER1 三组 hi/lo/hi 只读采样，完整配置检查
  留在前后；保留高字一致性、严格递增、真实抢占宽度、TIMER0 量化和溢出检查。
  不屏蔽中断，不取区间中点，不改变控制增益、wire、配对窗口或存储容量。
- 软件快照：原 tight-bracket 反例失败，preempted-bracket 通过；修改后相关
  56 项、rate/clock 5 项和独立 46 项通过。A/B ARM 汇编确认九次 MMIO 顺序正确，
  夹持内还有一次 literal load 和一次 cmp，但无函数调用、乘除、分支；采样后
  DMB 及完整配置比较保留。自身栈帧由 272 B 降至 264 B，静态 RAM 无增量，
  预留堆后余量仍 144 B；均为当轮快照，不是系统栈/WCET 证明。
- Release 源码指纹为
  `95853bce494726235df2f5827a7eaa81f3f352a42e2fa19a70e41dc8121a68f6`，
  包 SHA256 为 `9480543b56292df6298feade249668c0e9c948bb1276a66aa9d972229ee5aa43`。
  四板 OTA 和直接 TDMA 四项 gate 通过，原生板端记录各 14 条、STOP handoff
  通过。复用已测矩阵，无 P0T；未生成新 P3 提交凭证，源码仍未提交。
- `local-capture-r1` 完成 8 s 运行和全部 STOP 后 RAM 导出，运行中零查询，模式、
  会话及许可证撤销完成。NO2/NO3/NO4 完整参考接收增量为 0/0/5，LOCAL
  记录为 0/0/2、零 dropped。冻结采集器对 NO2 的未接收全零占位记录做 decode，
  导致 assessment 报 `record CRC mismatch`；这是缺数据的工具判定失败，不能
  当成线上帧 CRC 损坏证据。原失败和空记录保留，整轮判定仍 FAIL。
- NO4 原生 observation/commit 与基线/末态 MODEL 一致：事件序号 5073→6332，
  实测有向 delay 82 ns，误差区间 -9182..-1800 ppb，按原负反馈规则采用
  +450 ppb，DCO 序号 1→2、rate 0→450 ppb，remote command 序号保持零。
  这是一次本地采用证据，不是三从闭环、持续收敛或物理锁相证据。
- NO4 本地端点投影宽度为 2827/3095 ns，远端为 1687/1687 ns；此前 033 的
  NO3 HOLD 宽度为本地 7295/7295 ns、远端 6427/6051 ns。各轮、各板采样不同，
  不能据此给出确定改善百分比；宽度仍包含 ARM 锚点和量化误差。
- 同源 `local-capture-r2` 尝试 10 s 复采，但 START 前 NO2 MODEL 查询超时，
  没有发送 START；随后四板 STOP、RAM 导出、模式/会话/许可证清理完成。
  NO3 BOUNDary 首次超时由已有有界重试处理；原日志和失败保留，无新运行结论。
- 证据根目录：`out/HardwareAcceptance/20260916/dpll-bridge-window-r1/`；入口为
  `after-build-r1/manifest.json`、`bridge-window-source-independent-review-r1.json`、
  `direct-hardware-audit-r1.json`、`local-capture-r1-main-audit.json`、
  `capture-comparison-r1.json` 及两轮 `input-probe.json`/原生 RAM。
- 下一 gate：继续 `VDC-LOCAL-003`，以 owner 发布的停止态证据定位 NO2/NO3
  完整参考分片缺口；现有 aggregate reject 不能区分丢分片、交接覆盖和 ACK
  背压。保持每从独立校正、STOP 取消和真实误差资格，再证明三从持续采用；
  当前源码 quick P3、NO1 持续 PI、漂移斜率、100 ns 与异常恢复均未闭合。

### VDC-PROGRESS-20260916-033：等待配对时间基修复及四板复验

- TODO task ID：`VDC-LOCAL-003/004`；日期：2026-09-16。仅将等待本地历史追上
  接收参考的年龄计算改为生产者使用的 `osal_tick_ms()`；本地事件年龄继续使用
  `board_uptime_ms()`。未改变配对窗口、缓存、wire、控制增益或误差资格。
- 两个时钟刻意不一致的 host 反例中，旧代码在 fresh/wrap 两项失败，expired
  项通过；修复后相关 98 项通过。旧集成夹具仅补 OSAL 时钟桩，失败 XML 和
  修改前文件均保留。此证据证明提前消费缺陷已修复，不证明硬件运输或锁相完成。
- 当轮 Release 源码指纹为
  `26c65c6015eb529e35d5c90eae86d1514553ba75f6f7f5e15fb21d560cfc3e7b`，
  包 SHA256 为 `77cd05a69c8d4e9d74469c058643b52365ce23cd1d5d5641c123e184450d9978`；
  A/B RAM 和堆余量与 032 一致。四板 OTA、直接 TDMA 四项 gate 通过，
  原生板端记录各 14 条、STOP handoff 通过。复用既有矩阵，无 P0T；
  `full_p3_passed=false`，未生成新提交凭证。
- 使用冻结的 `capture_local_r2.py`，以参数绑定本轮 manifest/OTA/TDMA，执行
  同样的 8 s 采集。流程无错误，三从运输/匹配 ACK 通过，运行中零查询，STOP
  后 RAM 读回及模式/会话撤销完整。NO2/NO3/NO4 完整参考接收 1/2/4 条，
  LOCAL 记录 0/2/0 条、均零 dropped。上述为当轮快照。
- NO3 记录是一组未采用 HOLD：同模型间隔约 1.894 s，误差区间
  -10108..4188 ppb，本地两端宽度均 7295 ns，实测有向 delay 为 162 ns。
  三从 DCO 序号均保持 1、rate 保持零；NO3 原生提交记录与基线/末态 MODEL
  完全对应，remote command 序号未变。本轮 `local-capture-r1-main-audit.json`
  明确 FAIL，不宣称本地闭环或斜率改善。
- 证据根目录：`out/HardwareAcceptance/20260916/dpll-local-follow-clock-r1/`；
  `before-manifest.json`、`red.xml`、`green.xml`、`author-freeze-r1.json`、
  `after-build-r1/manifest.json`、`direct-hardware-audit-r1.json`、
  `local-capture-r1/input-probe.json` 及对应 main audit。所有修改保留工作区，
  旧固件、旧采集和原失败证据未覆盖。
- 下一 gate：继续 `VDC-LOCAL-003`，先用停止后有界诊断区分完整参考接收、历史
  追赶、投影拒绝与配对窗口各阶段，不能把 aggregate reject 当作丢包组数。
  同时准备缩短 TIMER1/TIMER0 桥接采样夹持区间的独立切片，保留完整前后时钟
  配置复验、抢占导致的真实宽度和 TIMER0 量化界限；不得人为截宽、强制取中点、
  用到达时间替代事件时间。完整参考稀疏和固定 ARM 锚点误差仍分别待证。

### VDC-PROGRESS-20260916-032：本地跟踪集成、RAM 导出及四板未采用定位

- TODO task ID：`VDC-LOCAL-003/004`；日期：2026-09-16。Core0 有界采集本地历史，
  按同事件序号配对 NO1 MODEL 时间戳并加入实测有向 delay；显式 LOCAL_FOLLOW
  模式由 Core1 SyncDpllFB 独占 DCO 连续重基和调频，旧远程命令入口暂停。
  候选和游标复用已有存储；接收 ACK 不等待控制。以上实现未提交，未冻结新契约。
- 软件验证快照：扩展回归 864 项通过，41 项因旧夹具缺模式查询接口编译失败；
  修正夹具并增加互斥负测后相关 46 项通过。最终相关回归 214 项、历史接口 6 项
  通过。独审将真实 prepare/getter/owner/Domain/capture 联结，8 场景和 3 份原生
  解码通过。原失败 XML 均保留，后继通过不覆盖失败记录。
- 独审发现 Core1 连续调用栈下界至少 2168 B，大于当轮 2048 B 栈。拆分候选读取、
  DCO 应用和最终 owner 复验后，Release 栈估算含调度上游为 1648 B，余量 400 B；
  不据此宣称全系统中断栈或 WCET 合格。A/B static_end 保持 `0x2007f770`，
  预留堆后余 144 B，静态 RAM 无增量（均为当轮快照，非容量契约）。
- 为避免 SD 故障阻碍采集，增加 `SYSTem:SYNC:VDC:DPLL:TRACe:READ?`，仅在
  TDMA STOP 已应用且记录冻结后由 Core0 分页读取已有 RAM；重组原生 header、
  payload/file CRC，不在运行环路查询。生产导出测试 20 项和独审复跑通过。
- 部署源码指纹 `bbb51bc77be16f8aada05da9ac25536b68f3550dd1bf98a00d311e112dba1421`，
  包 SHA256 `a277fb44e3d5a9688c9bf7cffc5f2fde4ee3f0658db4a7796e82610e2f5c7c61`，
  绑定 `after-build-r2/manifest.json`。四板 OTA、直接 TDMA 的 passed、
  closed_loop_passed、realtime_gate_passed、diagnostic_passed 均通过，四份原生
  TDMA 记录各 14 条，最终 STOP。复用既有矩阵，无 P0T、SD 保存或 OTA 实现修改。
  `full_p3_passed=false`，没有生成新 P3 提交凭证。
- `local-capture-r1` 在启动前 BOUNDary 查询超时退出，未发 START，清理完成。
  冻结原脚本/原件后，r2 仅对 STOP 态该查询最多尝试两次，超时仍记录；格式错误
  不重试。`local-capture-r2` 完整运行与导出无流程错误，三从参考接收和匹配 ACK
  均通过；首 START 至全部 STOP 的动作日志无查询，模式和会话清零核验通过。
- 实板快照：NO2/NO3/NO4 完整参考接收为 1/5/5，NO2/NO3 无 LOCAL 记录，NO4
  仅一对 observation/commit、零 dropped。NO4 的同模型间隔约 1.104 s，误差
  区间为 -23642..8570 ppb，跨零故 HOLD；本地两端投影宽度 17887/5691 ns，
  实测有向 delay 为 82 ns。三从 DCO 序号均保持 1、rate 均为零、remote 序号
  均为零。接收成功不等于采用，HOLD 也不是锁相；本轮本地控制验收失败。
- 定位发现等待 history 追上参考的 cache miss 分支，使用 `board_uptime_ms()`
  减去来自 `osal_tick_ms()` 的 `rx.last_rx_ms`，实板两时间域不同，会错误地提前
  消费尚待配对的参考。后继同域修复与红绿证据放在
  `out/HardwareAcceptance/20260916/dpll-local-follow-clock-r1/`，此处不追认通过。
  配对稀疏和投影区间宽分别处理；不扩大缓存/年龄门限、不强制取中点校正。
- 证据根目录：`out/HardwareAcceptance/20260916/dpll-local-follow-integrated-r1/`；
  入口为 `local-follow-source-independent-review-r2.json`、
  `local-follow-ram-independent-review-r1.json`、`direct-hardware-audit-r1.json`、
  `local-capture-r2/input-probe.json` 和 `local-capture-r2-main-audit.json`。
  后者明确 `passed=false`，记录三从零采用、RAM 原件及基线/末态模型一致。
- 下一 gate：`VDC-LOCAL-003` 修复同域等待年龄并执行新源码直接四板 TDMA/本地
  采集，先核实三从候选与实际采用，再优化同模型时间差的不确定度。NO1 自主 PI、
  消除斜率、物理输出采用、100 ns 与异常恢复继续待验，长期目标未完成。

### VDC-PROGRESS-20260916-031：本地事件历史桥及直接四板回归

- TODO task ID：`VDC-LOCAL-003`；日期：2026-09-16。按用户指令复用已测线序与
  矩阵，不再运行 P0T。此次仅实现 TDMA owner 的有界历史读取接口，尚无生产
  Core0 消费者，不据此宣称时间戳已配对、DCO 已采用或锁相。
- `tdma_runtime_owner_copy_event_history_window()` 通过历史和 LIVE 双 guard，
  原子复制同 observer epoch 的记录及 TIMER1 enable 锚点；一次读取最多
  `TDMA_EVENT_HISTORY_WINDOW_CAPACITY` 条，忙或换代不修改调用方输出。历史淘汰
  显式报告丢失，STOP/换代后由后续消费者重新核验授权；不读取 PIO/FIFO/DMA。
- r1 构建失败是把 host 布局写死在断言中，原件 `build-r1.log` 保留。r2 改为
  相对旧布局验证 guard 位于已有尾部填充，ARM 独立重编译确认结构体前后均为
  568 B；A/B static_end 均保持 `0x2007f770`，预留 heap 后余量仍为 144 B
  （本轮链接快照，非运行时栈或 WCET 保证）。
- 作者相关测试、主控附加回归 77 项、独审 r1/r2 的 8/5 项通过；独立 C 用例
  同时覆盖常规及 short-enum ABI。源码/资源独审见
  `history-source-independent-review-r2.json`。冻结源码指纹为
  `9b255ffdaf82158a695fc112840eddb7a97093b718c0202e594f6c637443b7ce`，
  包 SHA256 为 `7ef847fa82379d1eb49569b2cd3f72c7f3b72d32efa3f96aeda489ac67120d39`；
  旧 ACK 专项证据只适用于 030 当轮固件，不替代本轮运行证据。
- 四板 OTA 全部成功，耗时约 124.59 s；随后直接 process-image TDMA 验证的
  passed/closed_loop_passed/realtime_gate_passed/diagnostic_passed 均为 true。
  四份板端原件各含 14 条记录，STOP handoff 通过，最终四板 STOP；这些数值均为
  本轮快照。NO1/NO2 的 DPLL 局部 overrun/deadline 反馈保留，不能从 TDMA 通过
  推导整张静态表预算合格。SCPI 仅配置/触发，运行数据由板端记录后导出。
- 证据根目录：`out/HardwareAcceptance/20260916/dpll-history-bridge-r1/`；
  构建绑定见 `after-build-r1/manifest.json`，部署及原件交接见
  `direct-hardware-audit-r1.json`、`ota-four-board-r1/summary.json` 与
  `tdma-direct-r1/summary.json`。本轮是直接 TDMA 回归，`full_p3_passed=false`，
  未生成完整 P3 凭证，未暂存或提交。
- 下一 gate：继续 `VDC-LOCAL-003`，复用现有 Core0 缓存留存本地 RX 历史，按同
  事件与 NO1 时间戳加已测有向 delay 配对，再由 Core1 唯一 owner 实施本地跟踪。
  不增加首帧合格、全窗无错或校准寻优前置；缺失样本跳过，ACK 不等待本地配对。

### VDC-PROGRESS-20260916-030：直接 TDMA 通过、三从接收 ACK 实测与主线纠偏

- TODO task ID：`VDC-LOCAL-002/003`、`VDC-FLIGHT-001`；日期：2026-09-16。用户明确
  只有 TDMA/DPLL 本身阻塞主线，已测出线序即停止 P0T；校准寻优、工具和 SD
  故障单列，不等完整校准恢复才接通本地控制。没有修改 P3 通过条件或生成替代凭证。
- 当前 ACK 固件直接执行既有 process-image 短帧验证，复用先前实测矩阵；
  `tdma-direct-r1` 的 passed/closed_loop_passed/realtime_gate_passed/diagnostic_passed
  均为 true，约 15.36 s，四板最终 STOP。独审重新解码四份板端原件及原判据；
  基础接收增量为 277/278/277/277（本轮快照）。其中 NO1/NO3 的 DPLL 局部超限
  仍属诊断反馈，TDMA gate 通过不代表 DPLL 或整张调度表时间预算合格。
- 主控已完成的 P0T snapshot 最小工具修复仅保留原响应、区分无效观测和真实换代、
  对无效格式额外只读一次；作者 115 项、独审 147 项通过。重编译七份固件产物与
  已部署 ACK 产物逐字节一致，工具源码指纹为
  `a23fd620f022a68f4894f3d6c474890f4d2000bb3d64b1695d655bcbf50bd343`。
  该工具硬件恢复不再前置；完整 P3 仍未通过，不能放行提交。
- `ack-direct-r1` 在 START 前导出旧记录时遇 NO3 SD SAVE FAILED，未运行 ACK。
  旧 TDMA 原件此前已经 RAM 导出；不修 SD 才继续主线。后继 `ack-transport-r1`
  仅以 STOP 前后板端留存快照核对传输，未调用 recorder/SD，首 START 至全部 STOP
  之间没有查询。原失败及 SRAM 副本保留，后继不授连续原生波形或 SD 可靠性结论。
- 传输专项原始动作跨度约 13.829 s（快照）：NO1 发布增量 103，NO2/NO3/NO4
  完整参考接收增量 2/8/14，ACK 发布增量 2/8/14，NO1 精确匹配回执增量 2/7/13。
  三条主板末次 ACK 均与对应从板保留 TX 历史逐字节相等；NO2/NO3 还等于其最后
  接收参考，NO4 已接收更新参考。各节点最近主板 proof 比最后 ACK 新，均不相等，
  不将有限末档推断为全程逐条一致。NO3 ACK reject 增量为 4；不宣称无损运输。
- 旧 collector 原报告为 FAIL：错误地使用仅适用于从板的 master FLIGHT 空快照
  校验主板 config。新增 `audit_transport.py` 仅离线改用本轮已确认的 master ARM
  与 reference TX 身份重算两项检查，其余 wire/CRC/session/计数/历史判据不变；
  主控复核通过，原脚本、原 FAIL 报告及动作日志未修改，未为修正摘要重复采样。
  四板真实 START/STOP、配置读回、REFERENCE/session 清零及 ORIGIN 撤销齐全。
- 证据目录：`out/HardwareAcceptance/20260916/dpll-receipt-ack-r1/`；入口为
  `direct-tdma-binding-independent-review-r1.json`、`ack-transport-r1/input-probe.json`、
  `ack-transport-r1-failure-independent-review.json`、`ack-transport-r1-main-audit.json`、
  `ack-transport-r1-independent-review.json`。最终独审含原始回放和三十项反例，
  仅覆盖上述冻结源码与部署固件，不覆盖后继本地事件桥修改。
  本次关闭基础运输 ACK，三从 DCO 更新仍为零；不声明本地控制、锁相或完整 P3。
- 下一 gate：`VDC-LOCAL-003` 先以现有 history/LIVE 双 guard 和 TIMER1 锚点提供
  有界同事件历史桥，再写入既有 Core0 cache，加入本地有向 delay 和显式跟踪模式，
  由 Core1 唯一 owner 实际更新 DCO。ACK、TDMA 不等待该配对；丢样记录后等下次。

### VDC-PROGRESS-20260916-029：接收 ACK 接线及 P0T 准备失败收敛

- TODO task ID：`VDC-LOCAL-002`、`VDC-FLIGHT-001`；日期：2026-09-16。新增 typed
  RECEIPT_ACK：完整保留参考后回显原事件内容，NO1 使用每来源最近完整 FIFO
  发布证明做逐字节对账，匹配后才增加确认计数。复用原快照 union 与固定配额；
  旧控制读取入口显式拒绝 proof arm。ACK 不等待本地配对或 DCO，也不阻塞发车。
- 软件快照：独审发现并修复 pending admission 暂借首片序号后未恢复，以及
  REFERENCE 切回 MODEL/RATE 后旧 ACK 被当作排序前值的两个问题；失败原件保留。
  冻结后的作者受影响测试 141 项、owner 227 项、共享路径 176 项通过；独立
  反例 8 项、PROof 接口 4 项和采集器相关 51 项通过。它们不证明实板确认或锁相。
- A/B Release、Flash 链接、冻结产物和独立资源复核通过；静态 RAM 无增量，
  预留堆后仍余 144 B（构建快照，非运行栈/WCET 保证）。源码指纹
  `f173b6f57018bb32384dfe9be4c0ac30ea14c7f85c1272f1aadec9fae6b1e841`，包 SHA256
  `e7c7f99943aadd80e8235c43a45d2c7a79758e2db11153f72b0643ec961a5736`。
  `VDC-REFERENCE-01` v2 设计/文档 C11 为 ACCEPT_WITH_DEVIATION，保持 pending。
- 当前固件已成功更新四板，但 `p3-r1` 在 P0T 的 NO3→NO1 活动查询中止：身份字段
  被缺失占位值替代，触发 ARM 生命周期门禁；缺少原查询文本，不能判定是实际
  换代、串口超时或物理断链。仅完成前段 pair，未测出边的空 adjacency 不算坏链。
  清理四板均有真实 STOP 应答和状态读回；失败见 `p3-r1-failure-audit.json`。
- 同包 `p3-resume-r1` 未重复刷机，重新初始化及验收；此次 NO1 OPMode APPLY
  返回超时且 active 仍为旧档，另三板完成切换，尚未启动 pair。再次全 STOP。
  后续只读观察仍见 NO1 staged 与 active 不一致、apply 未增而 reject 增；
  REFERENCE/session 均为零，新 ACK 模式尚未启用。OUT 诊断曾误发未注册 STATE?
  并产生 Undefined header，其后错误队列不能唯一归属之前 APPLY；误操作与原件
  一起记录于 `diagnostic-followup.json`，不将其当固件故障或抹去失败。
- 证据目录：`out/HardwareAcceptance/20260916/dpll-receipt-ack-r1/`，包含作者冻结、
  精确增量、独审、双槽产物、两轮失败和原始查询。ACK 专项尚未执行，不能以
  host/Release 通过关闭 LOCAL-002。P0T 入口修复单独落于
  `out/HardwareAcceptance/20260916/dpll-p0t-profile-r1/`，只复用已验收 STOP/profile/
  counter/error 归因的有限恢复，不扩大 timeout、不修改固件或 OTA。
- P0T 档位工具冻结后，作者相关测试 109 项、独立回归 104 项通过（测试快照）；
  重编译七份产物与已刷入 ACK 固件逐字节一致。该工具源码指纹为
  `19f7ec4fcc48f6968f8563b044b81898c58416d61c2da69b8dd61979b9ecf2d8`。
  同包恢复轮四板档位均一次 APPLY 成功；完成十组探测并测得完整有向四边，
  第十一组 NO4→NO2 在 START 前 baseline 的 TDMA 字段全为缺省值，PHYS 仍有读数，
  因身份检查中止。十二组未全完成，仍为 P0T/P3 FAIL；四板 STOP/probe 清理通过。
  `p3-failure-independent-review-r1.json` 保留限定归因，不能追认全部拓扑验收。
- P0T 的有效“不相邻/无活动”是正常负向扫描结果；格式缺失是无效观测，不能
  混为坏链或实际换代。后继最小工具切片在
  `out/HardwareAcceptance/20260916/dpll-p0t-snapshot-r1/` 保留原始查询，
  仅针对格式无效允许有限只读恢复；真实身份变化仍拒绝，不重发控制命令。
- 下一 gate：入口软件修复/独审、同源构建与已刷固件逐字节一致证明、重新严格
  四板 P3，随后完成 ACK 专项。通过后接 NO1 自主 PI 输入和显式从板本地跟踪；
  snapshot 缺证、实际采用和精度各自验收，不把不同失败合并归因。

### VDC-PROGRESS-20260916-028：显式参考事件发布与三从接收通过

- TODO task ID：`VDC-LOCAL-001/002`、`VDC-FLIGHT-001`、`VDC-FEEDBACK-001`；日期：
  2026-09-16。STOP-only REFERENCE 模式复用既有 MODEL 编码、固定分片配额和完整组
  暂忙交接，由 Core0 投影 NO1 已提交 DCO 对应的原始发射事件，冻结后逐目标发送。
  三从只保留指定主机、当前会话且发给本机的完整事件；与 PROBE/AUTO 互斥。
  未增加静态缓冲、PIO 程序或实时等待。`VDC-REFERENCE-01` 经独立 C11 评审为
  ACCEPT_WITH_DEVIATION，保持 pending；不授予 ACK、控制或锁相资格。
- 软件与构建快照（非产品容量或时序契约）：作者四组 host 共 391 项通过，独立
  补测 68 项通过；首次夹具 setup 失败原件保留。A/B Release、Flash 链接和资源
  复核通过，静态 RAM 无增量，预留堆之后余量仍为 144 B；这不是运行时栈/WCET
  证明。源码指纹 `5b97f7b5ddb7f4902b8afab78337c056e25b8c3e44d1f5fc2fcfe90fb53df2d0`，
  包 SHA256 `a51ae8e2926301ac85b336972cf720ab8e8a246b41fe9708071b91957cd30c70`。
- 当前源码四板 `p3-r1` 首次严格通过，passed/strict_gates_passed 均为 true，
  diagnostic_failures 为空；DPLL 为 TDMA-only 跳过，不能当作锁相。receipt SHA256
  `74785e9c613038c14b1b963ee8005ecc5d4d78d609c3968f6bca15ef5380924c`。独审核对
  原生记录、四板 OTA 提交及冻结产物；OTA 实现未改，NO5 未操作。
- 专项 `reference-r1` 在启动前失败：REFERENCE 设置真实返回成功，采集器却将
  查询单值 1 过滤成 ACK，读回超时。该轮未 START，四板 STOP 和模式/会话清零
  均有原始应答；失败不覆盖。OUT-only 采集器注册 scalar-one 查询后，真实串口
  reader 负例与相关回归共 15 项通过，未修改已验收固件。
- 同固件 `reference-r2` 成功。以下均为当轮快照：NO1 完整发送组增量 120，
  NO2/NO3/NO4 完整接收增量分别 27/37/32；独立按原始字节解码，CRC、来源、
  目标、会话和本地准入一致。采集期间零查询，四份板端 TDMA 记录各 20 条且无
  missed，全部 STOP 后导出；最终 REFERENCE/session 均真实读回零。
- 结论限制：主机只留最近两条 TX，只有 NO3 尾记录可逐字节命中，另两板不能
  宣称精确发布历史已对账，`all_three_exact_publications_proved=false` 保留。
  三从 DCO 更新增量仍为零；事件区间宽度及 RX reject 计数均不支持物理精度、
  无损运输或锁相声明。此切片关闭有身份的参考事件接收，不关闭整个 LOCAL-001/002。
- NO1 输入核查：普通 bootstrap 有 DCO 更新，自主阶段 trace 仍为空；
  `clock_observation.valid` 未接通自主 TX/RTT 记录，不能把 bootstrap 计数或参考
  发布当作持续自主 PI。后继从同一稳定原始记录准备诊断输入，不伪造正式时间
  标志；自环共用振荡器也不提供独立频率基准。见 `master-input-audit-r1.json`。
- 证据：`out/HardwareAcceptance/20260916/dpll-reference-publish-r1/` 保存作者冻结、
  host 和 C11 评审；`out/HardwareAcceptance/20260916/dpll-local-follow-r1/` 保存
  build、`p3-r1/acceptance.json`、两轮专项、`reference-main-audit-r1.json`、
  `build-p3-independent-review-r1.json` 与 `reference-r2-independent-review-r1.json`。
  接收独审结论为 PASS_REFERENCE_RECEPTION_ONLY，专项动作日志首尾约 54 秒，不含
  build/OTA/P3。文档两检查器及 18 项测试通过，旧登记 ID 告警保留；hook 与
  check-staged 因无 staged 代码跳过，不构成提交放行。命令记录见
  `out/doc-audit/20260916-local-follow-priority-r1/reference-028-gates.json`。
  无 staging/commit；旧失败原件继续保留。
- 下一 gate：单独接通 typed RECEIPT_ACK，在完整接收并保留后沿环路回传原参考
  身份，主机逐从核对；不依赖本地同序事件配对或 DCO 应用。随后接 NO1 自主诊断
  PI 输入和从板同序缓存、路径 delay、显式本地跟踪；每项仍须当前源码四板 P3。

### VDC-PROGRESS-20260916-027：切换为主板时间戳发布与从板本地 delay 跟踪

- TODO task ID：`VDC-LONGTERM-001`、`VDC-FLIGHT-001`、`VDC-FEEDBACK-001`、
  `VDC-LOCAL-001/002/003/004`；日期：2026-09-16。用户明确 NO1 不计算各从校准，
  自主 PI 调节并发布闭环时间戳；三从接收后沿环路返回 ACK，并在本地加已测 delay。
  已调整 TODO 执行顺序，暂停集中式 `VDC-FEEDBACK-004`，保留旧代码、验收及失败原件。
- 只读代码核查：普通 compact mailbox 当前携带量化 phase/rate/quality，见
  `distributed_refmem_tdma_flight_build_compact_mailbox()`；不能由普通邮箱接收成功
  宣称完整闭环事件时间戳已经发布。已有 typed feedback、事件投影和指定主机接收
  路径可复用，仍须按实际发布/接收事件对账。当前 `sync_dpll_fb_step()` 与域角色
  语义保持 FOLLOWER 只采用主机命令，本地跟踪须显式接入，而非单纯解除 PI 旁路。
- 路径审计：`vdc_domain` 矩阵按本地节点/reference 查询，实际累加 reference 到本地
  的 DATA 反向环路径；`vdc_ring_observer_residual()` 已使用 reference 加 delay
  再与 local RX 比较的符号。现有 provisional 表保留 diagnostic-only 与未闭合 bias
  代际，适合先推进诊断调频；逐段转发驻留与 endpoint bias 的完整精度另行验收。
  校准中已经扣除的往返合成 bias 不再当作单向 delay 重复相加。
- 现有校准证据位置：`out/HardwareAcceptance/20260916/dpll-p0t-start-r1/`
  `p3-resume-r1/calibration-parameter-handoff.json`。本条只做接线审计与计划切换，
  未改变固件、操作硬件或生成新锁相结果；旧 P3 不证明新模式已实现。
- 最小验收区分三层事实：ACK 证明收到对应时间戳；本地采用/DCO 更新证明控制接通；
  internal 斜率和示波器证明收敛与物理精度。时间戳绑定硬件事件而非软件接收时刻，
  数据送达不依赖从板已经锁相；NO1 本地 PI 不等待全从 ACK 才更新。
- 下一 gate：`VDC-LOCAL-001` 核验 NO1 实际 PI 更新与闭环事件发布，再逐切片接入
  三从 ACK 和本地 delay 跟踪。首发精细证明、完整 bias、集中式 AUTO 入口修复不再
  阻塞基础闭环；各功能切片仍执行软件、Release、当前源码四板 P3 与专项。
- 文档验证：两项检查器通过（保留既有登记 ID 告警）；pytest 首次因临时目录父路径
  不存在发生 setup 错误，创建任务目录后重跑全部通过。独立只读审核通过；hook 与
  check-staged 因没有 staged 代码而跳过硬件门禁，不构成新 P3 或提交放行。
  验证证据见 `out/doc-audit/20260916-local-follow-priority-r1/gates.json` 和
  `tests-r2.xml`。本轮无固件改动，无需为纯计划更新刷机。

### VDC-PROGRESS-20260916-026：P0T 启动确认闭合及专项采集入口复核

- TODO task ID：`VDC-FEEDBACK-004`；日期：2026-09-16；保持 IN PROGRESS。
  证据根目录 `out/HardwareAcceptance/20260916/dpll-p0t-start-r1/`。
  仅修改拓扑工具及测试：START 须真实 ACK 和清洁错误队列；失败后双板确认
  STOP 才可重新 ARM，采用新计数基线，跨配置观测不得计入邻接。准备及清理
  失败保留部分原件并使流程失败；不修改固件、OTA 或超时预算。
- 以下数字为验证快照，非产品门限：旧工具拒绝 START 仍误通过的红测已复现；
  作者 49 项及独立反例 39 项通过。`author-freeze-r1.json` 冻结工具切片；
  `source-reuse-independent-review-r1.json` 独审源码与固件复用通过。
  双槽 Release、Flash linkage 通过，源码指纹
  `ef0f141619504decda1cbce0259366e11807838e3018f83d22d6ddbececc0d1e`，
  package SHA256 `0506d2fbe5b57e0e02adc57822e9d216b45570e543ff412da78d722202bc9705`。
  `firmware-reuse-root-r1.json` 逐字节验证 ELF、反汇编、map 和包与 025 已刷版本相同，
  并核验四板 OTA 发送长度、CRC 和启动提交；因此使用受支持 resume，未重复刷写。
- `p3-resume-r1/acceptance.json` 已终态，passed/strict_gates_passed 均为 true，
  diagnostic_failures 为空。复位、拓扑、校准和 TDMA 均重新执行，耗时约 74.1 s；
  旧失败拓扑不复用。独审 `p3-independent-review-r1.json` 367 项通过，四板原生
  TDMA 各 14 条；24 次 START 均首次真实 OK、ERR 为零，未演练实板重试分支。
  `p3-admission-root-r1.json` 在启动 AUTO 前独立核验严格状态、源码和包身份。
- `auto-r1` 在 NO1 START 合成应答处严格拒绝，尚未发 TRIAL；提前停止导致 TDMA
  短记录不满足完整采集门禁。四板真实 STOP、配置完成及 AUTO/session 撤权后，
  `auto-r2` 完整重新配置，但 NO4 ARM 返回 OK/status=1 后 ERR 为 -200，仍拒绝。
  归属审计发现 ARM 命令 override 在预清错误队列后、真实 ARM 前插入四板配置及
  BOUNDary 查询，查询超时可能污染后续错误归属；不能断言 ARM 已成功或仅是旧错误。
- `auto-r3/prearm-error-drain.json` 记录后继四板 fresh STOP 及错误队列清零。
  同目录 capture 因目录预先存在，在构造阶段退出，无硬件动作。新目录 `auto-r4`
  在旧记录交接时出现 epoch mismatch，未配置 ARM：此前失败采集产生新 recorder
  代际，工具却只允许与 P3 旧原件比对。各失败原件保留，不能用于闭环效果判断。
- 下一 gate：收敛专项工具的真实 ARM 前配置/错误归属，以及按 UID、build、epoch
  关联失败采集后的旧记录；不放宽未知 ACK、不吞错误、不提升旧记录。再完成同源
  全从多轮 AUTO 原件与独审，之后进入 `VDC-DRIFT-001`；当前不宣称频偏或精度达标。
  只读传输审计另发现 follower 在 `phys_commit_overlay()` 拒绝时释放 READY job、
  可能被下一分片替换；该条件已由 host FIFO harness 复现，但尚未证明是本轮稳定
  四板根因，故暂不修改生产代码，后续需补真实 DMA 选择与 fragment 序列关联证据。

### VDC-PROGRESS-20260916-025：快照暂忙时保留尚未取得的 RX 分片

- TODO task ID：`VDC-FEEDBACK-004`；日期：2026-09-16；保持 IN PROGRESS。
  证据根目录 `out/HardwareAcceptance/20260916/dpll-preacquire-r1/`。
  仅修改 RefMem 两文件中的绑定读取、刷新及 Core0 service；未知绑定时不取得
  RX 租约，已知 inactive 正常退休和接收，已知 STOP 优先于无关 VDC getter。
  两个 refresh 均执行以保留到期处理；TX 发布仍调用原入口，不增加缓冲或等待。
- 以下数量为验证快照，非产品门限：原实现真实 service 红测证明暂忙后 FIFO
  分片仍被 acquire/release，`red-r2.xml` 为明确断言失败。初次红测受 Windows
  assert 弹窗/转储影响，已终止本轮测试进程并保留日志；仅 host harness 增加
  SIGABRT 退出处理后复现真实断言，未以超时充当缺陷证明。
  `green-r1.xml` 共 159 项通过，覆盖新分片/生命周期案例及原交接、运输回归。
- `author-freeze-r1.json` 与 `author-change-r1.diff` 冻结本切片；双槽 Release、
  Flash linkage 通过。`after-build-r1/manifest.json` 源码指纹
  `8a1c8c52183edfe108d43e082df403d1fb2e3c0f1af5c1ec573392e9955c8820`，
  package SHA256 `0506d2fbe5b57e0e02adc57822e9d216b45570e543ff412da78d722202bc9705`。
  `resource-review-root-r1.json` 显示 BSS 增量为零，保留 CRT heap 后静态余量
  144 B，Core1 已查主/从路径 1488/1184 B；不授予完整高水位或整表时序合格。
- 独审 `source-independent-review-final-r1.json` 通过，独立运行 159 项回归及
  首末片额外 12 例；候选设计作者另作实现复核及 17 项补充例，关系和范围单独
  记录于 `independent-review-r1/`，不替代最终独审。
- 新源码四板 P3 run 已终态失败于 `p3-r1/p0t-topology/summary.json`，缺少
  NO1→NO2 邻接；NO2 enabled/adapter_started 已置位，但 UP/DOWN 及 adapter service
  尚未运行，工具未保存 START 原应答。不能据此认定物理链路或本切片导致故障。
  未启动同源 AUTO，不能复用 024 源码凭证。主控随后按四个已知 UID 独立发出
  STOP 并核验 config/applied 一致，`post-p3-stop-r1.json` 四板均通过。
- 下一 gate：补齐 P0T START 原响应和失败后的 STOP→重新 ARM 有界恢复，再完成
  本切片严格 P3、全从多轮 AUTO 原件及独立复核；持续未知仍可能
  使有限 FIFO 溢出或组装到期，普通 RX 会随同一 view 延后，按实测继续定位。

### VDC-PROGRESS-20260916-024：维护态 OPMode 有界恢复

- TODO task ID：`VDC-FEEDBACK-004`；日期：2026-09-16；保持 IN PROGRESS。
  证据根目录 `out/HardwareAcceptance/20260916/dpll-stopped-config-r1/`。
  为解除当前专项入口的配置拒绝，仅修改 coarse CLK 工具的 STOP 后 OPMode APPLY
  及对应测试；固件实时路径、命令有效期、OTA 实现均未修改。
- helper 逐次记录 STOP/config ACK、完整 staged/active profile 与 CRC、应用/拒绝
  计数、原始应答及错误队列。只对可归属的明确拒绝进行有界同参重应用；未知应答、
  已应用但缺应答、配置或计数漂移均失败，成功必须有精确应答及本次应用计数推进。
  不把 ring config generation 推进作为 OPMode 完成条件，也不重试 START。
- 以下为验证快照，非产品门限：主控及独审各 55 项回归通过，独立真实 helper
  正负例 27 项通过。`author-freeze-r1.json` 和 `source-independent-review-r1.json`
  绑定已审源码；审查范围仅 coarse APPLY，其他准备入口另行定位。
- 双槽 Release 与 Flash linkage 通过，`after-build-r1/manifest.json` 源码指纹
  `09a2def1de113bc31fa9d19f948fa5b5d41b2d332ab9fc962ca81c87cfc8ed53`。
  ELF、map 及 package 与 022 切片逐项哈希相同，package SHA256 为
  `e1228f6856fac18fbf4e5c8153b032d93abbaeb9125476a757fe8f088a3e343a`；
  工具变更不增加目标 RAM 或 Core1 路径；逐项复算见 `resource-equality-root-r1.json`。
- `p3-r1/acceptance.json` 已终态，passed/strict_gates_passed 均为 true，
  diagnostic_failures 为空，源码及冻结包一致。四板 coarse APPLY 均首试成功，
  原件显示应用计数从 1 到 2；证明正常集成路径，不声称实板重现拒绝后恢复。
  主控在独立步骤核验严格状态及源码/包后，启动 `auto-r1`。
- AUTO 已终态，flow_completed=true、errors 为空，四板 STOP、AUTO/session 撤权
  及全部 STOP 后导出顺序通过。NO1 原件为 12 条记录，包括 5 次 offer 和 2 次 ACK；
  NO4 实际应用两次，累计调频 1948 ppb，NO2/NO3 实际应用为零。
  两板 TRACE 状态记录数为零且 dropped 为零，导出保留空记录原因，未创建二进制；
  旧专项评估器访问缺少的 path 时以 KeyError 失败。实际应用结论同时由 MODEL
  前后读回、角色计数和已有原件确认，不能把该评估异常解释为采集丢样或全从通过。
- 下一 gate：依据原件继续分段定位运输缺口，修复已由真实 service 反例复现的
  refresh 快照暂忙后仍 acquire/release 中间分片问题；新切片另存
  `out/HardwareAcceptance/20260916/dpll-preacquire-r1/`，重新完成软件、构建、P3、
  AUTO 与独审。该代码风险尚非所有实板未决的已证根因。后续稳定频偏收敛、
  物理精度和恢复目标不变。

### VDC-PROGRESS-20260916-023：长期目标复核与当前验收入口

- TODO task ID：`VDC-LONGTERM-001`、`VDC-FEEDBACK-004`、`VDC-DRIFT-001`、
  `VDC-PRECISION-001`、`VDC-RECOVERY-001`；日期：2026-09-16。
  本次按用户要求归纳长期目标，保持 IN PROGRESS；沿用 TODO 既有目标和里程碑，
  更新近期执行顺序，不新增运行时契约。仅修改 TODO 与进度文档，未操作硬件。
- 复核 `out/HardwareAcceptance/20260916/dpll-complete-handoff-r1/` 原件：
  `p3-resume-r2/acceptance.json` 的 passed/flow_completed 为 true，
  strict_gates_passed 为 false；唯一 diagnostic failure 为 coarse CLK level 7
  阶段 NO2 OPMode APPLY 返回 timeout 和执行错误。后续 TDMA 完成不消除该失败。
  `auto-r1/input-probe.json` 在 model_preflight 拒绝非严格 P3，flow_completed
  为 false；本轮没有 AUTO 硬件实采，不能据此判断交接修复的实板效果。
- `topology-start-diagnosis-r1.json` 与 `opmode-apply-diagnosis-r1.json` 为只读
  控制流程诊断：STOP 读回不保证后续控制快照立即可得；拒绝可能已有部分副作用。
  后继方案须区分 TOPology、OPMode APPLY 和 START 的完成条件，保留原始返回，
  使用 STOP、配置身份及应用计数核验有界恢复；不采用通用 timeout 重试。
- 下一 gate：先收敛配置流程并完成当前源码严格四板 P3，再验证全从多轮 AUTO。
  internal 稳定收敛、实际输出精度及恢复分别验收；首帧精细优化仍不是内部闭环前置。

### VDC-PROGRESS-20260916-022：Core0 完整命令暂忙交接

- TODO task ID：`VDC-FEEDBACK-004`；日期：2026-09-16；保持 IN PROGRESS。
  证据根目录 `out/HardwareAcceptance/20260916/dpll-complete-handoff-r1/`。
  仅修改 RefMem 私有接收实现及新增专项测试，未改变 wire、Core1 TTL/ACK、PIO/DMA
  或 OTA。完整组在最终绑定快照暂忙时保留原 payload、admission epoch 和真实完成
  时间，Core0 后续服务最多一次延后重验；普通数据消费继续，换代/STOP/到期取消。
- 以下为测试及资源快照，非产品门限。`before-r2.xml` 在修复前有 7 项失败和 7 项
  通过；`final-r2.xml` 的 167 项通过。首个 before.xml 的临时目录 setup 失败、
  中间测试结果保留。正式回归不把尚未修复的更早片段丢失作为应保持的成功行为。
- `source-independent-review-r1.json` 与 `resource-independent-review-r1.json`
  通过；独立回归 111 项及额外 6 个反例覆盖旧记录、AUTO、取消短路、CRC 损坏和
  普通 FIFO 消费。作者冻结见 `author-freeze-r1.json`，源码报告 SHA256 为
  `46eed75ff046440056b9d7b8809c2e330cb0b31dfd0e1c016351e300c710c64c`。
- 双槽 Release 与 Flash linkage 通过，`after-build-r1/manifest.json` 源码指纹
  `26442174648421f27c2a1033f1f033ff2bba1ab0988af80c4fdc884347673f38`，
  package SHA256 `e1228f6856fac18fbf4e5c8153b032d93abbaeb9125476a757fe8f088a3e343a`。
  相比采集修复增加 8 B 全局 BSS，保留 CRT heap 后静态余量 144 B；Core1 主/从
  已查路径仍为 1488/1184 B，Core0 选定路径 1372 B，RefMem 任务栈为 8192 B。
  不据此授予完整任务、IRQ 嵌套、高水位或整表 WCET 合格。
- 新源码四板 quick P3 已结束：`p3-r1` 在 NO3 TOPology 配置失败；
  `p3-resume-r1` 的拓扑探测缺少 NO2→NO3 邻接，NO3 收发未运行且缺少 START
  原始应答，不能认定物理链路损坏。`p3-resume-r2` 完成诊断流程但严格门禁失败，
  AUTO 随后被预检拒绝，详见 023；不能复用前轮采集源码凭证。
  更早 refresh getter 暂忙后继续释放 FIFO 的中间片丢失已由
  独立真实 service 反例复现，见前一证据目录 `preacquire-busy-r1/diagnostic.json`；
  该路径不在本切片扩大修改，不把两个 host 风险直接认作实板未决根因。
- 文档检查器及 18 项文档回归通过，测试原件为 `docs-tests-r1.xml`。
- 下一 gate：收敛配置/启动流程后完成本切片新源码严格四板 P3 与 AUTO 实采；
  依据原件决定后继片段准入修复，
  全从自动控制、internal 稳定收敛及物理锁相仍未完成。

### VDC-PROGRESS-20260916-021：采集修复同源验收与全从自动控制缺口

- TODO task ID：`VDC-FEEDBACK-004`；日期：2026-09-16；保持 IN PROGRESS。
  证据根目录 `out/HardwareAcceptance/20260916/dpll-auto-frequency-r1/`。
  以下计数均为本轮证据快照，非产品门限。
- P3-r2 的 TRN-00 失败位于 ring_preparation：NO4 TOPology 配置超时并返回执行
  错误，尚未 ARM/注入，不能作为锁相测量失败。没有存活硬件进程后，核对源码
  `7e2b53ba908c2f4f60e452264fd4d73b532c3371c851c423bf8e8f68dc99c50c`
  与冻结 `after-build-r2` 相同，复用其包和 p3-r2 成功 OTA 执行正式 resume。
- `p3-resume-r1/acceptance.json` 的 passed/strict_gates_passed 为 true，
  diagnostic_failures 为空；四板重新初始化、校准与 TDMA 验收通过，未重新烧录，
  NO5 未操作。独审 `p3-resume-independent-review-r1.json` 核验全部凭证附件及
  原生记录的哈希、CRC、UID/build/epoch 和 STOP；初次失败未追认为通过。
- reviewed `capture_auto.py` 未修改，SHA256 为
  `cd8bb8fc01e60268116a0fe9100aaeeaf9275fc336cedd11297380ee2462b47d`。
  `auto-r4` 在 NO2 TRACE ARM 超时后中止，尚未执行自主 TRIAL；不完整 TDMA
  导出失败保留。四板 STOP 和 AUTO/session 撤权读回成功，同源有界复测为 auto-r5。
- `auto-r5` 已终态，flow_completed=true、errors 为空，但 AUTO 专项仍失败。
  NO1 完整保存 19 条 AUTO 事件：7 条观测、7 条 offer、5 条 ACK；没有 legacy
  背景记录且 dropped 为零。三从分别实际应用 1/2/2 次，均有对应精确 ACK，
  四份 TDMA 原生记录完整。NO2 第二命令和 NO4 第三命令未得到实际应用/ACK，
  不以部分多轮成功授予全从 AUTO 资格；四板 STOP、AUTO/session 清零已完成。
  NO2 end BOUNDary 查询的原始 timeout 保留，不解析为零计数；实际应用结论来自
  TRACE、MODEL 及主端原始 ACK 的对账。`analysis-r5/` 独立复算的区间均向零移动，
  仍不是稳定收敛或物理锁相。后继未决发生在 STOP 前足够早，不能以采集截断解释。
- `complete-contention-diagnosis-r1.json` 用真实 FIFO/接收函数复现 COMPLETE 后
  binding getter 暂忙丢弃完整组装结果：后继重放末片只计 duplicate，须新完整组
  才能恢复。它是可复现代码风险，尚不能证明实板未决均由此造成。
  后继仅修复 Core0 有界保存并重验完整结果，复用现有缓冲，维持原有效期和换代取消；
  实施与验收证据放 `out/HardwareAcceptance/20260916/dpll-complete-handoff-r1/`。
- 下一 gate：后继切片的真实接收反例、资源与独审、双槽构建、新源码四板 P3、
  全从 AUTO 原件；不得复用本条旧源码凭证为后继提交放行。

### VDC-PROGRESS-20260916-020：长期目标归纳与最新验收状态校正

- TODO task ID：`VDC-LONGTERM-001`、`VDC-FEEDBACK-004`、`VDC-DRIFT-001`、
  `VDC-PRECISION-001`、`VDC-RECOVERY-001`；日期：2026-09-16。
  按用户要求归纳长期目标，区分单次实际应用、全从多轮自动控制、internal 频偏
  收敛、物理精度和恢复验收。目标保持 IN PROGRESS；任务入口见 TODO 的长期目标。
- 本次只更新 TODO 和进度文档，未修改控制实现或操作硬件，未改变契约状态。
  保持 HAOFV 唯一 owner、固定特等席、有界交接、Core1 静态预算以及 STOP 后导出。
  首帧精细证明仍归 TDMA 后续优化，不作为内部闭环推进前置。
- 核对既有证据根目录 `out/HardwareAcceptance/20260916/dpll-auto-frequency-r1/`：
  初版 `p3-r1` 的通过只绑定 `after-build-r1`；最新 `after-build-r2` 的源码指纹为
  `7e2b53ba908c2f4f60e452264fd4d73b532c3371c851c423bf8e8f68dc99c50c`。
  `p3-r2/trn00-marker-accepted-row/summary.json` 为 passed=false、passed_row_ids
  为空、recommended_row=null，未产生本轮严格通过凭证。校准门禁失败不证明 DPLL
  控制失败，也不能以初版 P3 覆盖当前源码。
- `auto-r3-delivery-diagnosis.json` 只读诊断保留完整准入与实际应用的区别：
  NO2 完整命令 RX/应用为零，NO3 已应用但后继命令缺口在准入之前，NO4 有多次应用。
  原件没有逐组发送时刻，聚合 reject 包含其他目标命令，不能据此断言 PIO 物理丢片。
  部分节点向零响应仍不足以关闭全从自动闭环或收敛任务。
- 文档验证：两个检查器通过，仅保留既有登记 ID 告警；文档回归测试共 18 项通过
  （本次验证快照）。首轮因 pytest 临时父目录不存在而出现 setup 错误，创建
  `out/doc-audit/vdc-longterm-20260916-020/` 后使用新 basetemp 重跑通过。
  pre-commit、log-check、check-staged 与 diff 检查通过；无 staged 源码时 P3 检查
  跳过，不代表当前未提交固件获得硬件提交资格。
- 下一 gate：核实最新校准失败并取得同源四板 P3，再完成全从多轮 AUTO 专项；
  保留采集完整性、逐从应用/ACK 及新模型窗口证据，之后进入 internal 稳定收敛验收。

### VDC-PROGRESS-20260916-019：显式 AUTO、RATE 窗口与四板基线验收

- TODO task ID：`VDC-FEEDBACK-004`；日期：2026-09-16；保持 IN PROGRESS。
  通过独立 RATE 域保存源相对频率坐标和绝对命令年龄依据，Core0 持有同模型窗口，
  Core1 逐从生成有界负反馈；精确 ACK 后允许下一命令，须应用后新模型的完整窗口。
  正常跨核争用不再被误当作 AUTO 关闭；mode getter 暂不可得时暂停本拍准备，保留
  现有片组、序号和 baseline。FOLLOWER 本地 PI 继续旁路，OTA 实现未改。
- 本轮证据目录：`out/HardwareAcceptance/20260916/dpll-auto-frequency-r1/`。
  以下数量为本轮验证快照，非产品门限。`owner-host-r4.xml` 的 101 项 owner/真实
  Domain、多板模拟与独立 Fraction 校验通过；`rate-contention-r1.xml` 的 170 项
  RATE/transport/matcher/projection 回归通过；`capture-host-r1.xml` 的 45 项
  实际 C append、解码、损坏反例及 SCPI 验证通过。模拟收敛不是实板频偏收敛。
- 双槽 Release 与 Flash linkage 通过，冻结 `after-build-r1/manifest.json`：
  源码指纹 `50f90ab138dc1f338057386a316862878fa60aebba2d206f4acdfd9ac98f61cd`，
  package SHA256 `b928926222f589927ec5088b1d2b1edafca6bd735a7ce1d0419a3418cc558ff5`。
  `resource-review-root-r1.json` 显示 BSS 增加 24 B、保留 CRT heap 后余量 156 B；
  已检查 Core1 主/从调用路径分别为 1488/1184 B，RAM step 位置保持，capture
  内联路径没有 CRC 调用；不授予完整 IRQ 嵌套、高水位或整表 WCET 合格。
- `p3-r1/acceptance.json` 的 passed/strict_gates_passed 均为 true，
  diagnostic_failures 为空，与冻结源码一致；四板 OTA、校准和 TDMA 通过。
  NO5 跳过，DPLL 自动收敛与物理锁相不由基础 P3 证明。
- 板端维护缓冲复用于 AUTO observation/offer/apply/ACK/HOLD。命令为 decoded
  语义记录，ACK 为实际收到的原始反馈；观测与 offer 成对保留，HOLD 去重。
  `capture_auto.py` 只在 STOP 授权，板端有限窗口记录，全部 STOP 后导出；
  `preflight-r1` 已通过当前源码/包/四板 P3 绑定检查，尚不代表专项执行通过。
- 专项 `auto-r1` 在 START 前失败：采集工具未将 AUTO 查询登记为可返回单值一的
  query，合法响应被过滤为 timeout；已修正运行进程的响应分类，真实 backend
  fake-serial 独审通过。`auto-r2` 仍在 START 前失败：继承的 prior-native 路径
  要求嵌套二进制同时出现在 receipt 顶层 artifacts，而 strict receipt 经已绑定
  handoff 传递绑定二进制。修正为逐层复算 receipt/handoff/native 身份与哈希，
  再核 UID/build/epoch/count/terminal status；四板原件离线核对及独立反例通过。
  两轮失败原件保留，均已确认全板 STOP、AUTO 与 session 清零，未进入运行环路；
  固件指纹未改，不把工具修复追认为此前专项通过。后继专项为 `auto-r3`。
- `auto-r3` 已完成实际采集与导出，flow_completed 为 true、errors 为空，但 AUTO
  专项拒绝通过：主板记录含旧 DPLL 背景事件，保留 75 条后因成对空间不足丢 2 条，
  其中 62 条 legacy、5 条观测、5 条 offer、3 条 ACK；NO2/NO3/NO4 实际应用分别
  为零/一次/四次。NO2 没有完整命令 RX，NO3 下一命令尚未完成接收；不能将运输
  未决解释为 HOLD 或估计方向错误。独审为 `auto-r3-independent-review-r1.json`。
  `analysis-r3/` 从真实二进制独立复算全部保留区间：NO3 从
  [-3014,-2633] 到 [-2402,-1967] ppb，实际首次增量 +658 ppb；NO4 从
  [-5521,-5018] 到 [-4503,-4013] ppb，首次增量 +1000 ppb。均向零响应，但
  主端后续截断，NO4 后两次只有从板实际应用记录，不能补造主端闭环证明。
  四板 STOP 与 AUTO/session 清零读回通过，不授予全从自动收敛或物理锁相。
- 后继最小修复仅冻结 TRACE ARM 的采集模式，AUTO-only 不追加 legacy 背景事件，
  不修改 DPLL 控制或扩大缓冲。`capture-mode-host-r2.xml` 的 47 项通过；前轮
  capture-mode-host-r1 保留一个旧测试依赖已废止 service 签名的失败，修正为
  实际 `sync_dpll_fb_step` 入口后通过。独立实际 C 模式测试见
  `capture-mode-source-independent-review-r2.json`。双槽 Release/Flash linkage
  通过，`after-build-r2` 源码指纹为
  `7e2b53ba908c2f4f60e452264fd4d73b532c3371c851c423bf8e8f68dc99c50c`；相比初始
  AUTO 再增加 4 B BSS，当前保留堆后余量 152 B，已查主/从栈仍为 1488/1184 B。
  新源码须独立完成 `p3-r2` 与后继 AUTO 专项，不能复用初版通过结果。
- 下一 gate：完成当前源码四板多轮 AUTO 专项，逐从核查真实应用、精确 ACK、
  新模型整窗、独立区间复算与 internal 频偏变化，再推进 `VDC-DRIFT-001`。
  未决、持续 HOLD 或满缓冲必须保留原件并定位，不能判作自动控制完成。

### VDC-PROGRESS-20260916-018：同命令有界重复与同轮三从实际应用

- TODO task ID：`VDC-FEEDBACK-003`；日期：2026-09-16；自动控制父任务保持 IN PROGRESS。
  修复单组 FIFO 发布完成即退休、缺片后没有再次完整接收机会的问题；不把聚合计数
  当作某次丢片位置证明。Core0 复用不可变 offer 和原记录，按
  `REFMEM_VDC_BOUNDARY_COMMAND_MAX_GROUPS` 限制完整组总数，组间普通 mailbox 成功
  后才继续，逐片复验原身份及年龄；取消和组间同 ID 换字节均不可恢复。
- Core1 精确 ACK 退休匹配 offer，保留已用额度和命令。TX-done 只在整个有限批次
  发布完成时通知；ACK 提前退休不伪造完成，在途重复靠从板去重与应用序号保持
  至多一次。契约修订及 C11 见 `VDC_CROSS_REVIEW_02.md`，登记保持 pending。
- 本轮证据根目录：`out/HardwareAcceptance/20260916/dpll-command-delivery-r1/`。
  以下数量为本轮测试/硬件快照，非产品门限。`host-author-r2.xml` 的 115 项专项及
  `host-adjacent-r1.xml` 的 55 项相邻回归通过；覆盖真实分片丢失/乱序后的重复恢复、
  重复应用拒绝、普通帧交接、FIFO 失败、快照争用、STOP 和原有效期，不代表任意
  丢包模式均可交付。双槽 Release/Flash 检查通过，冻结于 `after-build-r1/manifest.json`。
  源码指纹为 `e48095e7df0d532c46f6b9fc41f69951fd58e0d136bf998efe7892e8df8d5fdc`。
- `resource-review-root-r1.json`：双槽新增 BSS 4 B，扣除保留 CRT heap 后链接余量
  180 B；已检查的 Core1 主/从路径分别为 1408/1152 B，RAM step 放置不变。
  这不是完整 IRQ 嵌套、运行期栈高水位或整表 WCET 合格证明。
- `p3-r1/acceptance.json` passed/strict_gates_passed 均为 true，diagnostic_failures
  为空，四板 OTA 和 TDMA 门禁通过。`oneshot-r1/input-probe.json` 为同轮实板专项：
  NO2/NO3/NO4 完整命令接收均为一次，实际应用均为一次，模型 token 均从 1 到 2，
  applied sequence 从 0 到 1，rate 从 0 到 +100 ppb，NO1 三个 peer 均精确 ACKED。
  重复接收分别为 1/1/2，未重复应用。主端发布 8 个完整组、138 个片段，保留一次
  取消，批次完成计数为 2；不能由这些聚合计数独自推断某片的物理丢失位置。
- 主端两槽原始 TX 历史仅覆盖本轮 NO2 的重复记录，不声称覆盖 NO3/NO4 原始 TX；
  三从均有实际完整 RX、保留应用命令及原始返回 ACK，命令身份和实际模型逐项对账。
  四份当前原生 RAM 记录各 20 点且完整，运行中无 SCPI 查询，errors 与 SD 失败均
  为空，四板 STOP、probe/session 撤权及读回通过。
- 继承的 MODEL 配对专项仍为 false：NO4 的 `direct_pair_wire_association` 缺证，
  保留历史窗口与原始字节覆盖不足，不以当前命令应用通过改写该结果。本轮四板
  旧 DPLL TRACE 为零更新记录；原生 TDMA 记录完整不能替代 internal 残差收敛证据。
  当前专项只确认本轮单次命令实际应用及精确 ACK。
- 新入口 `capture_delivery.py` 继承既有流程，仅修正早 ACK 时不要求批次完成计数
  非零的评估语义；改查真实 Core0 完整组与本会话原始 TX 历史，保留全部精确
  RX/实际应用/模型/ACK 条件。6 项离线评估测试通过，旧轮 NO2 失败仍保持失败。
  独立评估器/owner 补测 34 项通过，源码与 C11 审核为
  `source-capture-c11-independent-review-r1.json`，结论 ACCEPT_WITH_DEVIATION。
- 硬件独审 `p3-independent-review-r2.json` 重算源码、包、原件绑定和链接资源；
  r1 审核器曾按系统 GBK 读取 UTF-8 JSON 失败，错误报告保留，r2 修正读取方式，
  未修改硬件原件。`oneshot-independent-review-r1.json` 的 87 项检查通过：独立
  CRC/字段解码、实际模型、连续重基算术、四份原生记录、全部 STOP 及撤权。
  主控最终同源核验为 `root-final-binding-r1.json`。实现代码已提交为 `dd8cba1`；
  提交前 check-staged 及 pre-commit 已按完整 staged 源码指纹核验当前 P3 凭证，
  不以先前无 staged 源码时的跳过替代本次提交门禁。
- 后继只读设计为 `automatic-frequency-readonly-design.json`：固定 RX 锚点的误差
  可在同生命周期差分中消除，但每次时钟桥读取及参考逐帧重读的误差不能直接抹去。
  候选是在明确新反馈语义下准备频率观测坐标，复用既有缓存和逐从状态，保留命令
  原始年龄检查；该候选未实现、未冻结，也未授予自动控制许可。
- 下一 gate：`VDC-FEEDBACK-003` 已完成，进入 `VDC-FEEDBACK-004` 的自动频率控制。
  固定单次增量不证明估计方向、自动调频、internal 斜率收敛或物理精度；后继须按
  各从独立测量窗口生成校正，并在实际应用后等待新模型窗口。

### VDC-PROGRESS-20260916-017：重配置继承当前伺服参数身份

- TODO task ID：`VDC-FEEDBACK-002/003`；日期：2026-09-16；父任务保持 IN PROGRESS。
  根因不是命令 CRC 错误：启动已加载非默认伺服参数，TDMA 激活却通过 default clock
  model 把 clock/DCO 内的 servo CRC 恢复为默认值。模型运输保留该不一致，最终
  Domain 候选校验正确拒绝，所以 NO1 默认参数正常而使用非默认参数的从板无法应用。
- 修复只在 `vdc_domain_activate_tdma_configuration_checked()` 继承当前 active servo
  CRC，再由现有路径派生 DCO。正常/临时 TDMA 配置共用该修复，保留原始参数、
  FOLLOWER 本地 PI 旁路、CRC 校验、连续重基和所有命令准入条件。
- 以下为本轮测试快照，非产品门限：修复前真实 Domain 的普通/临时配置回归均失败；
  修复后相关 Domain/owner/model-feedback 共 95 项通过，独立重跑 Domain 6 项通过。
  回归使用实板参数，覆盖初始化后调参、激活、重复重臂、实际频率应用及同点连续；
  人为污染候选 CRC 仍须原子拒绝。软件通过不代替实板实际应用。
- 失败复现与前后测试在 `out/HardwareAcceptance/20260916/dpll-boundary-command-r1/`
  的 `domain-servo-crc-repro-r1/report.json`、`servo-crc-before-r1.xml`、
  `servo-crc-after-r1.xml`。独审为 `dpll-boundary-reject-r1/source-review-servo-crc-r1.json`。
- 本轮构建/硬件证据根目录：`out/HardwareAcceptance/20260916/dpll-servo-rearm-r1/`。
  双槽 Release/Flash 链接通过，冻结在 `after-build-r1/manifest.json`；
  `resource-review-root-r1.json` 确认静态布局、RAM step 放置及已检查的调用路径栈
  与 016 相同，不新增持久状态。源码指纹为
  `d73ea00a6b3eb762d6e49b50ab9810c2906eb75d84848c2fa4419b0389c97683`。
- 当前源码 `p3-r1/acceptance.json` passed/strict_gates_passed 均为 true，
  diagnostic_failures 为空；`oneshot-r1/input-probe.json` 完成单次诊断及终态导出。
  NO3/NO4 的边界 apply_count 均为 1，模型 token 从 1 到 2、应用序号从 0 到 1、
  rate 从 0 到 +100 ppb，NO1 对应目标进入 ACKED；各自专项逐条检查通过。三从
  模型 servo CRC 均保持实际参数身份。NO2 本轮完整命令 RX 为零，应用仍为零，
  主端该命令为 EXPIRED_UNRESOLVED，故整个专项 passed=false，不冒充全从板通过。
- 四份本轮原生 RAM 记录完整，运行中无 SCPI 查询，errors 为空，四板 STOP、
  probe/session 撤权和读回通过。单次固定增量只证明执行/反馈链路，自动误差控制、
  internal 斜率收敛、物理输出及精度仍未验收。016 中 NO4 的运输失败继续保留，
  不以本轮该板通过追认历史；当前 NO2 缺失表明完整命令交付仍需改善。
- 独立硬件复核：`p3-r1-independent-binding-review-r1.json` 与
  `oneshot-r1-independent-review-r2.json`。专项审计 r1 曾误用上轮 TRIAL job 值作常量，
  r2 按本轮原始 ACK 及记录身份纠正；旧审计原件保留，不修改原采集结果。
- 下一 gate：`VDC-FEEDBACK-003` 补齐命令可靠交付及所有从板的实际应用/ACK。
  缺片与普通帧插入会取消当前分片组，但现有聚合计数不能定位具体丢片；不得把
  无 ACK 当作未应用而叠加新校正，也不把再次盲跑完整 P3 当作运输问题的修复。

### VDC-PROGRESS-20260916-016：运行期拒绝历史留存

- TODO task ID：`VDC-FEEDBACK-002`；日期：2026-09-16；保持 IN PROGRESS。
  本切片只增加诊断，不改变命令选择、身份、新鲜度或实际 DCO 应用条件。
  下述数量均为本次源码及目标产物快照，非容量、精度或完整 WCET 契约。
- `boundary_reject()` 留存当前显式 probe 的首个拒绝和分支位图，STOP、probe/session
  撤权后仍可读；新的非零 probe 清零。它是板级历史，不是逐命令的时间线，后继
  分析须区分广播中的其他目标命令与本板命令。STOP-only `BOUNDary?` 诊断状态升级，
  保留原字段并追加历史；TDMA 命令 wire 不变。原最后拒绝码及累计计数行为不变。
- owner/SCPI 软件回归共 99 项通过，模型及命令编解码/运输相邻回归 37 项通过。
  双槽 Release 与 Flash 链接通过，meta 增加 8 B；扣除保留 CRT heap 后链接余量
  184 B，RAM data end 保持不变，主/从新路径栈分别为 1408/1152 B。完整 IRQ 嵌套
  和静态表 WCET 仍未证明。独审通过对新增诊断逆向去除，恢复上轮已审源码 hash，
  证明既有控制判断未改变。
- 新专项入口仅在 all-board STOP 后导出；独立字段查询失败保留原始响应及错误，
  继续尝试其余字段和原始 TX/RX，不把部分导出转为成功。schema 解析、RUN 拒绝、
  查询失败继续取证及 check-only 由 80 项离线测试和独审覆盖。旧采集器及旧失败保留。
- 证据根目录：`out/HardwareAcceptance/20260916/dpll-boundary-reject-r1/`；构建为
  `after-build-r1/manifest.json`，资源为 `resource-review-root-r1.json`，相邻测试为
  `host-adjacent-r1.xml`，采集器审核为 `source-review-capture-rejections-r1.json`。
  owner/SCPI 测试原件在前一证据根目录 `reject-history-host-tests-r1.xml`。
- 当前源码四板 quick P3 `p3-r1/acceptance.json` 的 passed/strict_gates_passed
  均为 true，diagnostic_failures 为空；源码指纹为
  `24ed746c9be1caf4254653151c5363e1e6c8f07c6c219b07c1a3d0ae1b6f8258`。
  独立源码/资源复核为前一证据根目录的 `reject-history-independent-review-r1.json`，
  四板凭证、包、UID 和 STOP 原件复核为本目录的 `p3-r1-independent-binding-review-r1.json`。
- `oneshot-r1` 已完成单次诊断及终态导出，四份本轮 RAM 记录完整，errors 为空，
  唯一未通过的 RAM 专项检查是逐从实际应用与 ACK。NO2/NO3 的首次拒绝均为 APPLY，
  后继才有 AGE/IDENTITY；NO4 未保留完整命令 RX，仅有 BINDING 拒绝，不能把上一轮
  三从接收成功外推到本轮。全板 STOP、probe/session 撤权与读回完成，应用仍为零。
- 随后在 STOP 下只读核对参数：`stopped-servo-COM4.txt`、`stopped-servo-COM3.txt`、
  `stopped-servo-COM6.txt` 中三从 active servo CRC 均为 3148516704，而已提交 DCO
  模型中的 servo CRC 为默认值 755336680；NO1 参数与默认一致。源码重配置路径调用
  default clock model 后只继承 active slew，没有继承 active servo CRC，造成候选 DCO
  校验拒绝。下一 gate 为该具体重配置缺陷的 host 复现、最小修复及当前源码四板 P3；
  NO4 的接收/绑定问题保持独立跟踪，不能由参数 CRC 不一致概括全部失败。

### VDC-PROGRESS-20260916-015：长期目标归纳与实际应用缺口

- TODO task ID：`VDC-LONGTERM-001`、`VDC-FEEDBACK-001/002/003/004`；日期：2026-09-16。
  长期目标保持 IN PROGRESS。本次整理只更新 TODO 和进度文档，不新增控制实现、
  不重新启动硬件、不改变已有契约状态。下述数量和命令参数均为实验快照，非产品门限。
- 将目标归纳为实际应用与确认、自动消除频偏、相位精度、恢复四个里程碑；internal
  用于快速观测各从实际应用与残差斜率，四路输出用于最终物理精度复核。首帧精细
  优化仍归 TDMA，完整绝对时间映射不阻塞内部调频应用。节点/主机/长周期扩展后置。
- 依据 `oneshot-r4/input-probe.json`，显式单次 +100 ppb 诊断完成运行及 STOP 窗口，
  NO1 发出三条专属命令，NO2–NO4 均留存完整命令 RX；三从边界实际应用计数和
  模型应用序号仍为零，不能宣称闭环已接入或已消除频偏。STOP 可覆盖最后拒绝码，
  不能以该码解释整个 RUN 期间的累计拒绝，实际拒绝分支仍待定位。
- 原始报告 passed=false；NO1 对目标槽的末次 STOP 后边界查询超时，原始主机
  TX 导出未完成；SD 写入失败及其他导出缺口分别保留，不与从板应用失败混为一因。
  四份本轮原生 RAM 记录完整，运行窗口内无 SCPI 查询；末尾四板 STOP、probe/session
  清零及读回留证。受限诊断不提升严格 P3、自动控制、锁相或物理精度资格。
- 证据根目录：`out/HardwareAcceptance/20260916/dpll-boundary-command-r1/`；
  原件为 `oneshot-r4/input-probe.json`、`oneshot-r4/actions.jsonl` 及关联 native 文件。
  当前源码构建和 P3 仍见 014；此次文档整理不代表新固件或新的 P3 通过。
- 下一 gate：`VDC-FEEDBACK-002` 定位并复现拒绝，再以最小修复完成
  `VDC-FEEDBACK-003` 的实际应用与精确 ACK，随后开放自动频率控制。

### VDC-PROGRESS-20260916-014：逐从边界命令接线与单次应用验收

- TODO task ID：`VDC-FEEDBACK-001`；日期：2026-09-16；父任务保持 IN PROGRESS。
  本切片按 pending 契约 `VDC-BOUNDARY-01` 接入生产命令运输和实际 Domain 调用，
  当前改动尚未提交。以下数字均为实验及资源快照，非产品容量、WCET 或锁相契约。
- 复用既有特等席分片和主机未使用的反馈发送记录，增加独立命令解码；普通负载
  路径保留。Core0 准备和发布，Core1 唯一 owner 逐从绑定测量、模型、会话与 ARM
  生命周期，在每次发布和最终应用边界重新检查新鲜度及取消条件。仅显式 STOP
  授权开启单次诊断额度，超时未决不自动重发，返回新模型与精确应用序号才确认。
- 软件反例覆盖单次额度、过期、模型/角色/STOP 变化、晚到 ACK 和域内连续重基。
  最终 owner 测试 77 项、SCPI 19 项及相邻回归 163 项通过；RAM 放置修正后
  owner/model 86 项通过。证据为 `owner-r3.xml`、`scpi-tests-r3.xml`、
  `regression-r1.xml`、`placement-host-r1.xml` 和 `source-review-owner-r4.json`。
  初始夹具、旧运输测试及对照失败均保留，不以新通过追认。
- 双槽 Release 和 Flash 链接通过。增加 hook 后曾发生旧 RAM step 被编译器内联
  到 XIP 的放置回归，已用显式 noinline 恢复；`after-build-r1` 不用于当前验收，
  最终产物冻结为 `after-build-r2/manifest.json`。BSS 增加 400 B，扣除保留 CRT
  heap 后静态链接余量为 192 B，不是运行时空闲 RAM；RAM text/data 对齐前仅余
  8 B。新主机调用路径 1400 B，Core1 栈余 648 B；完整中断嵌套及 WCET 尚未证明。
  原件为 `resource-placement-correction-r1.json`、`resource-review-independent-r4.json`。
- 最终源码指纹 `8fe68066ea1f2b15b1af6e520f757308a317f5d5d377b2f7074d0e18a54696f9`，
  包 SHA256 `3b60f1b9131cddecd7ffa768d59626054b261cfec5b2830c05e3b65505164fe5`。
  `p3-r5` 完成四板 OTA 和 TDMA 短帧流程，TDMA passed、closed_loop_passed、
  realtime_gate_passed 均为 true，四板 STOP 原生记录完整。strict_gates_passed
  为 false：coded level 7、TRN-01 和 TRN-03 分别失败；所选 SCK 行的最小余量
  为负，不能声明正式校准及时序合格。此前 r1 至 r4 的超时、拓扑/校准失败及错误
  放置版本结果原样保留；定向复测通过不替代正式凭证，也不追认旧失败。
- `oneshot-r1` 被严格预检拒绝，hardware_started 为 false，未发送校正。
  独立报告 `p3-r5-diagnostic-admission-review-r1.json` 仅准许基于该确切源码、包、
  四板和三项保留失败，显式进行一次受限内部校正诊断；默认严格入口不改，当前
  尚待该专项实际应用及 ACK 原件。不会因该授权提升正式 P3、自动控制或物理锁相资格。
- `oneshot-r2` 在配置前导出旧 P3 记录时，NO3 的 SD SAVE 从 BUSY 进入 FAILED，
  未发送任何 ARM、START、非零 session 或非零 probe；这不是 DCO 应用失败。
  SAVE 前三板 RAM 原件已经保留，并与 r5 原生记录逐字节一致；NO4 尚未轮到导出。
  清理时主板无模型会话的旧 MATCH 格式被新格式解析器拒绝，该次生效前失败亦保留。
  四板最终 STOP 的配置均已应用，PROBe 0、SESSION 0 及零会话读回齐全。
  独立核验见 `oneshot-r2-failure-review-data.json`。后继诊断改用 STOP 后原生 RAM
  作为 TDMA 数据载体，并单独核对存储任务及写缓冲退休，不把 SD 失败改为保存成功。
- `oneshot-r3` 已验证旧记录的 RAM/写缓冲退休路径，随后在首次 START 前中止：
  采集器把 STOP-only 的 `BOUNDary?` 基线放在 ARM 后，实际 enabled/adapter_started
  均已置位，固件正确拒绝；不能把该拒绝归为随机串口超时而盲重试。三从原始边界、
  Domain 应用计数及模型应用序号均为零，四板已 STOP 并撤销授权。NO1 记录仅 ARM
  未 START，取消后原生 CRC/身份完整但无采样点，须保留为失败，不能作为当前成功
  记录。新诊断入口将边界基线移到所有 probe ACK 后、首次 ARM 前，并逐条核对真实
  动作时间；原有模型基线仍在首次 START 前读取。历史取消记录仅允许结构保留和
  写缓冲退休，本轮成功采样条件不放宽。
- 证据根目录：`out/HardwareAcceptance/20260916/dpll-boundary-command-r1/`。
  下一 gate：先证明逐从命令实际应用一次和精确反馈确认，再改善同模型频率观测
  窗口并接自动校正。现有区间跨零不证明校正方向，不能把单次固定扰动视为消除漂移。
  首帧优化和完整绝对时间映射不作为内部应用前置，OTA 实现保持不变。
- 后继只读观测方案见 `frequency-next-analysis-r1.json` 和
  `frequency-common-anchor-analysis-r1.json`：现有 matcher 每次成功都会移动基线，
  单纯延长采集不会自动形成长窗口；逐事件 TIMER0/TIMER1 桥接也扩大频率区间。
  可评估同模型长窗口，或显式区分的 raw TIMER1 按实际 DCO rate 缩放的频率坐标。
  后者不能混入当前命令的 output-time 新鲜度字段，尚未实现或验证，不新增当前
  单次应用前置，也不宣称物理路径误差已消除。

### VDC-PROGRESS-20260916-013：从板连续重基调频原语

- TODO task ID：`VDC-FEEDBACK-001`；日期：2026-09-16；父任务保持 IN PROGRESS。
  代码及匹配 P3 凭证已提交为 `a159f5a`。
  本切片只实现 Domain 数学应用原语，不新增 wire、manager 接线、自动控制器或锁相
  资格。下述数字均为本次验证快照，非容量、时序或精度契约。
- 新增 `vdc_domain_apply_follower_rate_delta()`：Core1 外层 guard 下，校验指定主机、
  本地目标、角色代际、schedule/servo CRC、clock/DCO epoch/run、旧 DCO 和应用序号。
  在实际本地服务时刻计算旧输出，再以该输出重建时间锚并吸收旧 phase，受检加法
  更新 rate；候选验证和同点完全相等后才提交。旧/新 rate 保持前向，序号耗尽不回绕，
  拒绝保持整个 context 字节不变。成功只更新 DCO 及应用元数据，旧绝对时间专属
  元数据清零，不触发 oscillator trim、不改本地 PI、lock 或 quality。
- 外层责任未混入 Domain：session、ARM、observer、model token、新鲜度、单次 step
  限额与 STOP 取消仍由未来 Core1 命令 owner 准入；不能把这里的测试当作外层并发
  或运输证明。旧绝对生效时间 API 未改动，不将本地时间填入其共同时间字段。
- 软件：新增定向 pytest 四项通过，含 138 个显式边界和 800 个确定性随机整数
  oracle 案例；测试执行完整生产 Domain。现有投影、replay、命令 owner 和模型 owner
  共 135 项回归通过。独立 Fraction oracle 1680 例通过，覆盖应用点及未来时刻映射、
  正负频率、phase、长 uptime 和溢出；复用作者夹具的全 context 比较，独立补充数学
  预期。旧 API 在同一对照中产生 -369 ns 同点跳变，新原语保持连续。原件为
  `author-tests-r1.xml`、`root-regression-r1.xml`、`domain-review-r1.json` 及独审附件。
- Release/资源：A/B 和 Flash 链接检查通过，`after-build-r1/manifest.json` 冻结
  产物。没有新持久缓冲或 context 字段；BSS、CRT heap 及两核栈边界均未改变，扣除
  CRT heap 后静态链接余量仍为 592 B，不是运行时 free RAM。原语 ARM 对象自身栈
  帧为 152 B、text 为 492 B；未包括调用者、callee、IRQ/FPU 开销，不宣称完整调用
  链已验收。当前无生产调用，两槽链接器均剔除该原语，P3 不会实际执行新 API。
- 当前源码首次四板 quick P3 严格通过：`p3-r1/acceptance.json` 的 passed/
  strict_gates_passed 均为 true，diagnostic_failures 为空；范围是 TDMA-only，
  DPLL 为 `SKIPPED_TDMA_ONLY`。指纹
  `9c41906b277ee859e1a132519a45e374773ea5c8fb3ab0f0f734e8c2533d3c59`，1159 个源码文件，
  包 SHA256 `bfca51b9cb9455e1bdb8a5914f50d527189fc669e43712e2cd9275fc948be93d`；
  缓存 build 标识仍为 `20260915204256`，以源码、包及 OTA receipt 联合绑定。
  主控 `p3-r1-binding-review-root.json` 的 27 项产物/当前源码绑定检查通过。
- 独立硬件复核 `hardware-review-r1.json` 的 62 项通过：四板 UID/OTA、源码及产物
  hash 一致，四份 native 原件各 14 点、CRC/长度/epoch 完整。四板真实 STOP 应答
  后从冻结 RAM 导出，末尾再次 STOP 并核对运行态归零；本轮没有 SD 写读证明。
  NO1 DPLL 诊断 overrun/deadline 增量仍为 41/40，NO2 历史 peak 超预算也保留，
  不把 TDMA-only 严格通过扩大为完整 DPLL 调度通过。未操作 NO5，未改 OTA。
- 证据根目录：`out/HardwareAcceptance/20260916/dpll-boundary-apply-r1/`。
  作者交付为 `author-domain-handoff-r1.json`，独立源码/方案审核为
  `domain-review-r1.json`，资源边界为 `resource-review-root-r1.json`，文档独审为
  `doc-review-r1.json`；其后只补入真实代码提交及硬件复核结果。
- 下一 gate：按 `control-design-review-r1.json` 审核并接入专属服务边界命令，先做
  受限单次诊断校正，逐从证明实际应用和回传 ACK；到期未决命令保留身份，不在
  未知是否已应用时叠加下一次校正。随后用稳态观测窗口评估自动频率控制。
  上轮 r4 区间跨零不授予方向确定性；NO1 最终模型有效起点在自主切换前，不能把
  baseline/end 间的模型变化解释为自主段持续换模，也无需先新增 NO1 保持功能。
  首帧证明、完整绝对时间和 GPIO 精度不阻塞这个内部应用切片；最终物理锁相、
  完整 DPLL 静态预算及恢复仍未完成。

### VDC-PROGRESS-20260916-012：事件关联已提交 DCO 的模型反馈

- TODO task ID：`VDC-FEEDBACK-001`、`VDC-FLIGHT-001`；日期：2026-09-16。
  状态：IN PROGRESS。模型反馈切片硬件验收、专项及独立复核通过，代码提交为 `6716dc5`；长期目标未完成。
  本节数字均为实验快照，非冻结契约、精度或时序资格。
- 纠偏：内部反馈以 Core1 实际提交的 DCO 为受控对象；可选 GPIO 输出的消费确认、
  PIO 完成和物理精度留待后续。独立边界审核原件为上一证据根目录中的
  `control-owner-scope-review-r1.json`。不把延迟事件套入更新后的模型；事件早于
  当前模型有效起点时跳过，不补造历史。Core1 guard 包围真实 service 和提前返回，
  包括延后发布大快照的 servo 提交；Core0 准备采用稳定模型副本并在结束时复验。
- 实现：STOP 配置单调非零 session，原始模式保持默认。模型模式复用原分片配额与
  唯一稀疏缓存，冻结每事件输出区间和模型 token，NO1 同序配对；不增加第二套缓存。
  本板时钟桥验证 TIMER0/TIMER1 时钟关系并传播采样包围及整数微秒量化，Core0 完成
  投影和差分，Core1 不做 wire 解析。跨模型结果是坐标区间变化率，包含真实相位跳变，
  不直接授予控制资格。后继频率应用必须连续重基或明确排除跳变贡献。
- owner：模型 session 内禁止 Core0 自测/compact 观测直接提交 Domain，也拒绝校准
  激活及拓扑改写；ready 请求在 Core1 guard 内应用。先完成配置，再在 ARM 前启用
  session；禁用后旧 session 不能复用。STOP 后保留历史，但当前资格仍由运输授权、
  参考 epoch、session 和原有 ring 生命周期复验，迟到准备不恢复旧结果。
- 软件：`root-host-r3.xml` 为 269 pass / 1 fail；失败是旧 owner 测试未声明既有
  Core0 准备入口。补齐测试桩后 `root-owner-regression-r5.xml` 的 29 项通过；中间
  r4 的错误顺序断言失败原件保留。模型 owner 9 项、投影独立 14 项（含独立精确
  算术随机验证）及桥配置相关 11 项均有原件。独立集成复核 26 项通过，并用反向
  单行反例验证两项修复：TX getter 检查分片 session，缓存年龄覆盖毫秒量化误差。
- Release：首次链接因小包装入口跨过 BSS 对齐边界越界；包装改放 XIP，原实时
  step 仍在 RAM，`build-r3.log` A/B 及链接检查通过。BSS 末端 `0x2007f5b0`，较
  上轮增加 432 B；扣除固定堆后链接余量 592 B，不等于运行时空闲 RAM。归档
  `after-build-r3/manifest.json` 绑定 ELF/dis/map 和源码指纹。
- 证据根目录：`out/HardwareAcceptance/20260916/dpll-model-feedback-r1/`。
  `p3-r1` 完成四板 OTA，但 P0 缺少 NO3→NO4 边；`stopped-triage-r1.json` 四次
  STOP 均真实应答，无 CORE1_STALL，故障读回为空。同包 `p3-r2` 在 NO3 OPMode
  APPLY 超时且 active_level 仍为默认档时失败；四板再次 STOP 的原件为
  `stopped-triage-r2.json`。`no3-profile-triage-r1.json` 记录 session 为零、默认档
  与 staged 目标不一致，以及重新应用目标档后的正常应答/读回；旧 Execution error
  原样保留。未修改 OTA 或放宽准入。同包 `p3-r3` 退出成功并生成该轮源码的
  QUICK_DIAGNOSTIC 凭证，TDMA process-image/FIFO 闭环通过；但
  `strict_gates_passed=false`，保留 TRN-01 SCK 训练及 TRN-03 无满足重臂余量候选
  两项失败，不能写成严格验收通过。`root-artifact-recheck-r1.json` 的 25 项绑定检查
  通过，源码 `54199b32ffc15ad7e9c28dbe93644582693d3f81705a123343624de8a937997c`、
  文件数 1157，包 `815dfb5d8ae6e2cd80735588baae95fc4d87a5c25d36c532d548acb6bb14827a`。
  后继模式迁移测试与修复改变源码指纹，该凭证不能用于后继源码验收。
- 模式迁移修复：`domain-transition-red-r2.xml` 复现 raw 转 model 后旧 pair 被重新
  标为 schema2、原始 tick 年龄混入毫秒字段的问题。Core0 仅在 schema/session
  变化时清空公开 pair、active、年龄及私有基线，保留累计计数；同域历史保留规则
  不变。`domain-transition-green-r1.xml` 的相关 host 测试 246 项通过；`build-r4.log`
  A/B Release 与链接检查通过，静态 RAM 不变。`after-build-r4/manifest.json`
  绑定本轮源码、ELF/dis/map 和更新包。`p3-r4` 严格通过，
  `strict_gates_passed=true`、诊断失败为空，源码指纹为
  `d6f322de8840ed01a55dc2a0b4a0cabf9b477572740e8a344704b2073dbdcbf7`；
  `root-artifact-recheck-r2.json` 的 19 项构建、凭证及 TDMA STOP 绑定复核通过。
  增量独审 `domain-transition-review-r1.json` 的 17 项双向迁移、换会话、异常首样本
  及反向反例验证通过。
- 专项采集器已冻结，作者组合测试 165 项、独立复核组合测试 134 项通过，分别见
  `author-capture-handoff-r1.json` 与 `capture-model-review-r1.json`，两组存在重叠，
  不相加计数。保持无 RUN 查询、全部 STOP 后导出及 session 清理回读；只允许
  显式诊断准入已审核的两项校准失败，其他绑定与失败不豁免。
- 首次模型专项 `capture-r1/input-probe.json` 失败：三从 LIVE 观测增长、已提交模型
  有效，但反馈 TX 分片和完整组均为零，NO1 RX 也为零，MATCH 保持空 schema1。
  工具严格 schema2 解码因此拒绝；空记录的 CRC 拒绝不能解释为线上坏帧。
  四板 STOP 屏障、无 RUN 查询、原始导出及 session 清零回读通过。当前定位发送前
  模型投影/时钟桥拒绝，增加仅 STOP 查询的零静态 RAM 诊断后重新验收；不得将
  普通 TDMA P3 通过替代模型反馈专项。下一 gate 仍为模型运输及配对实测。
- 时钟桥定位：追加 `SYSTem:VDC:FEEDback:BRIDge?`，仅在 ring STOP 时只读导出
  配置与独立采样，不改变原准入、不增加静态 RAM。`bridge-diagnostic-root-r2.xml`
  相关测试 24 项通过，独立复核组合 31 项通过；`build-r5.log` 构建通过。
  `p3-r5` 诊断流程完成但严格门禁失败，保留 NO4 在 coarse CLK 阶段 OPMode APPLY
  超时及 `-200` 原始错误，不授予模型专项准入。随后仅进行 STOP 配置诊断，
  `bridge-hardware-r1.json` 四板真实 STOP 读回均为
  `XOSC_STATUS=0x81001000`，包含 STABLE、ENABLED 和历史 BADWRITE；其余配置
  匹配，原 exact-status 校验导致 configuration_supported/bridge_valid 均为零。
  修复仅区分当前稳定状态和历史粘滞位，完整前后配置比较仍保留该位，不清 MMIO。
  `bridge-sticky-root-r1.xml` 相关测试 25 项通过；独立
  `bridge-sticky-review-r1.json` 用四板真实寄存器重放证明旧拒绝、新接受，其他时钟
  故障及采样中状态变化仍拒绝。`build-r6.log` A/B 构建通过，静态 RAM 不变。
  新源码 `p3-r6` 诊断流程完成，但 NO2 coarse CLK 阶段 OPMode APPLY 超时，
  严格门禁仍失败；TDMA 闭环通过。`bridge-hardware-r2.json` 随后四板真实 STOP
  读回 configuration_supported/bridge_valid 均为一，历史 BADWRITE 保持置位。
  同包 `p3-r7` 严格通过，诊断失败为空；`root-artifact-recheck-r3.json` 的 19 项
  当前源码、包及 TDMA STOP 绑定复核通过。当前源码指纹为
  `fdb6e9008821e4ea4f4fdb4e58d4d37825bc4a6142142b10a215647ca04e85b7`，
  构建归档为 `after-build-r6/manifest.json`。
- 修复后模型专项 `capture-r2` 已实际收到三从模型反馈，NO1 完整 RX 分别为
  103/106/92，match 分别为 68/72/47；NO2、NO4 全部专项检查通过。
  NO3 唯一缺口是最新保留配对未落入发送端短历史，不能完成直接 wire 对账；
  本轮整体仍判失败，不能以通过计数覆盖缺证。原始字节运输、区间独立复算、
  无 RUN 查询和四板 STOP/session 清零均通过。`capture-r3` 在 START 前因 NO2
  MODEL 查询返回 `<timeout>` 中止，随后 end 读回正常，未发 START；原始失败及
  四板 STOP/session 清零保留，不把新会话中的旧 wire 当成新运输样本。
- 同源 `capture-r4` 全部专项检查通过：本轮三从新增完整 RX 为 109/102/102，
  新增配对为 62/53/66；逐从完整 wire 对账、至少一个保留配对端点的直接 wire
  关联、Fraction 区间复算和会话身份一致性通过。两端均有配对记录，但短 TX 历史
  不保证覆盖每个旧端点；不得扩大为全部历史 wire 已复核。准入时最大参考年龄
  为 84/85/81 ms，不是 STOP 时新鲜度。无 RUN 查询、全部 STOP 后导出和四板
  session 清零回读通过。区间仍较宽，实际 applied_command_seq 仍为零；不宣称
  纯频偏精度、频率漂移消除或物理锁相。后继接逐从专属命令及 Core1 连续重基应用，
  候选只读方案见 `next-control-slice-review-r1.json`，须独立实现/验收。
- 文档独审索引：`doc-review-r3.json` 及 TODO 单行增量 `doc-review-r4.json`，仅确认模型运输/配对切片的事实与边界，不授予控制、精度或锁相资格。
- 最终有界独立硬件审核为 `model-hardware-review-r1.json`，36 项检查通过；核对产物绑定、四板 STOP/session 清零、运行期零查询及失败原件。当前源码指纹由主控门禁复核，CRC/区间复算由已有独立报告覆盖；本报告不冒领这些重复验证，也不授予实际控制、锁相或完整 DPLL 时序资格。
  实际逐从命令、DCO apply、漂移消除和物理锁相仍未完成；不得以本轮投影测试替代。

### VDC-PROGRESS-20260916-011：Core0 准备迁移与跨核授权退休

- TODO task ID：`VDC-FEEDBACK-001`、`VDC-FLIGHT-001`；父任务和长期目标保持 IN PROGRESS。
- 状态：DONE，仅关闭诊断准备迁移切片；实际控制未接通，时序及锁相均未验收。
  本节数字为实验快照，非协议、容量、时序或精度契约。代码提交：`f415bea`。
- 变更：唯一 Core0 RefMem task 在真实 `distributed_refmem_service()` 内准备稀疏
  参考、逐源配对及原始差分，每次至多一条参考和一个来源，复用既有数组及发布区。
  Core1 只发布紧凑授权或撤权；同绑定重新授权使用新 token，耗尽保持撤权。
  Core0 私有 preparation generation 控制缓存退休；发布及 getter 复验当前 token
  和参考 epoch，迟到 worker 不能复活 STOP 前结果。debug_continue 应用、角色应用
  或待应用分支提前返回前撤权；正常 tune/domain/finalize 分支可保持授权。未增加
  DCO/控制应用。
- TDMA 来源：LIVE 的 latch SM/CS 与 sample epoch 在同一 guard 下冻结，raw getter
  末尾再复验。first reset 显式退休 LIVE.active；轻量 epoch facade 是 SRAM-only、
  单次有界尝试，忙时跳过，确认 inactive 才撤权。原始参考 getter 仍有有界 TIMER1
  读取。LIVE 元数据增加 8 B，DMA 记录及既有 SCPI 字段数量不变。
- 软件与源码独审：主控 `root-host-r1.xml` 140 项通过后，真实 service 的 debug+
  pending-role 反例暴露撤权遗漏，红测 `authorization-owner-red-r2.log` 为 1 fail /
  11 pass；修复后作者关联集 87 项通过。独立测试累计 73+41+12 项通过，前两组在
  单行早退修复前完成，最终 12 项覆盖真实 service 边界；
  `source-resource-review-r1.json` 的 56 项复核无遗留源码发现，SHA256
  `5971fb9313dac6b668e211f29b7f176f8423daab2ecda6bb2c822373eb5014b4`。
  覆盖实际 RefMem/SyncDpllFB 分支、token ABA/耗尽、暂停 worker、参考换代和取消。
- Release：`build-r2.log/json` 通过，最终 A/B ELF、dis、map、manifest 与 package
  冻结于 `after-build-r2/`。A/B BSS 增加 56 B，末端 `0x2007f400`，链接剩余
  3072 B，扣除固定堆 2048 B 后为 1024 B；不是运行时余量。ScratchX 未增加，
  Core1 stack 未改变。静态链估计 Core1 授权约 520 B、Core0 准备约 1388 B，
  后者在既有 8192 B RefMem 栈内；均不是包含中断的最坏栈证明。
- 源码 SHA256 `ced34b16520dac5e87a98bcd8d19384ac03cb0afdcacae2137d72db99eb17c6f`，
  文件数 1150；包 SHA256
  `9d8434f289d7da4bd2d9ca581e6903d8403489c418c22df3ac78a14d5b416b81`。
  必跑 `p3-r1` 的 run 完成四板 OTA，但 P0 未发现 NO2→NO3 边；该失败保留。
  `stopped-triage-r1.json` 四板真实 STOP、SOFTWARE_REBOOT、无 CORE1_STALL，
  FAULT:LAST 均空。同包 OTA 记录 resume 的 `p3-r2` passed/strict 均 true，
  diagnostic_failures 为空；未改源码或放宽门禁。提交前 staged 指纹与凭证一致。
- 专项原件：r1–r4 都完成流程，errors 为空；完整 passed 的轮次是 r2/r4。
  r1 NO1 最新 NO4 RX 序号 6207 不在源端 [6362,6309] 短 TX 历史中；r3 NO3 RX
  6319 不在 [6457,6392] 中，两次均判缺证，不能据此断言字节损坏或追认为成功。
  r1/r3 的算术、年龄、生命周期及原生记录检查通过，原件不删除。
- 采集修正：旧工具逐板等待 STOP ACK，r1/r2/r3 首末派发差为 235/219/234 ms。
  out-only `capture_parallel_stop.py` 仅覆盖 stop_all，四独立既有 session 并发发
  STOP，主线程记录原始 ACK/时间；全 ACK 后才做 runtime 读回，全读回通过才导出。
  无重开/IDN 或合成 ACK，限次只重试失败板，原错保留。作者及独审各 31 项离线
  测试通过，工具独审 `parallel-stop-review-r1.json` SHA256
  `bcf56aa0068d21e3442a05bd14bd6dfae6e858e6c515e60a3f5533636429126d`。
  r4 首末派发在主机时间戳分辨率内相同，全 ACK 为 94 ms；r2 为 281 ms。
  一轮通过只支持减少主机停止间隔，不能证明物理同时 STOP 或旧缺证的唯一根因。
- 完整验收 r2/r4：三从匹配增量 63/62/58 与 61/60/58，缺参考 29/36/42 与
  32/29/27，陈旧为零；全部最新完整 RX 与源历史逐字节匹配。每轮每板 20 条 native、
  零 recorder missed、SD/RAM 一致，全部真实 START/STOP，RUN 查询为零；各从实际
  apply 和各板 DPLL trace 记录仍为零。四轮相邻 ARM 为 16→17→18→19，observer
  为 3→5→7→9，主机 reference epoch 为 1→2→3→4、generation 为 1→3→5→7。
- 主控 `main-recheck-r1.json` 88 项通过。独审 `hardware-review-r2.json` 共 1023 项，
  仅保留 r1/r3 对账缺证及其终态共 4 项失败，没有新增发现；45 项相邻生命周期
  检查通过，明确批准保留并提交此诊断准备切片，不代表全部历史采集全绿。SHA256
  `cdf77d485f9ec27754209c598304d9641c0ca77cd622cb85e38934cd38360326`。
- 时序：`timing-comparison-r1.json` 统一使用首末 native 样本窗口；NO1 四轮调用
  3793/3793/3793/3792 次，DPLL overrun 为 13/15/9/10、deadline 为 12/15/7/10，
  旧版本为 537/551、506/523。累计峰值 203.808 us、预算 136 us，仍未达标；累计
  峰值不是当前 RUN 窗口 WCET。r4 NO3/NO4 各新增 DPLL overrun/deadline 2/2。
  r2/r4 四板 TDMA overrun 为 3/82/82/85 与 3/79/97/82，deadline 为 3/55/50/65
  与 2/58/60/62；三从 RX overrun 为 60/50/46 与 66/53/44。旧对照为 56/49/16
  与 40/21/19，保留不利观察但不归因为 Core0 迁移。各板最小 heap 20352 B、
  RefMem watermark 1230 words，不证明 Core0 CPU 有余量；独占准备 CPU 时间未测。
- 证据根目录：`out/HardwareAcceptance/20260916/dpll-feedback-core0-r1/`。当前四板
  已 STOP，未改 OTA、未操作 NO5。下一 gate：继续 `VDC-FEEDBACK-001`，将有效
  原始区间关联实际输出消费的 DCO 模型，再接逐从命令和连续重基的频率校正。
  同模型时间差路线见上一切片 `next-frequency-control-audit-r1.json`；完整绝对
  时间映射不作为初期频率闭环前置。局部超限继续记录，不隔离健康 TDMA。

### VDC-PROGRESS-20260916-010：稀疏主机参考配对与原始差分

- TODO task ID：`VDC-FEEDBACK-001`、`VDC-FLIGHT-001`；长期目标保持 IN PROGRESS。
- 状态：DONE，仅关闭诊断参考配对切片；未接控制或 DCO 应用，DPLL 局部及完整
  静态调度预算均未通过。本节数字是实验快照，非协议、容量、时序或精度契约。
- 代码提交：`6345dae`。Core0 在原有完整反馈快照内发布已解码字段；Core1 每次至多
  获取一条既有 LIVE 主机参考、轮询一个来源。固定稀疏缓存按完整测量序号和参考
  代际精确查询，缺项跳过，不插值。按来源保留两条配对证据并向外取整计算原始
  频差区间；源 ARM/clock/observer 换代在缺项和陈旧分支前先退休旧基线。
- TDMA owner 将保留的 raw latch 和 TIMER1 enable 前后区间转换为参考区间；其
  LIVE getter 有界读取 TIMER1、无懒初始化，不是纯 SRAM 读取。当前 facade 还在
  LIVE guard 外读取同核 phys epoch/SM/CS；不能原样迁移为 Core0 跨核调用。
  `SYSTem:VDC:FEEDback:MATCh?` 仅导出诊断历史及当前有效性，STOP 不抹除历史。
- 软件：`root-host-r1.xml` 120 项、`independent-host-r1.xml` 102 项通过；纯算法
  包含 5021 个 Fraction 对照，集成覆盖换代缺项/陈旧、回到旧命名空间、宽年龄
  括号及 getter 取消。采集工具作者与独审各 110 项离线回归通过。实现/资源复核
  `implementation-resource-review-r1.json`、工具复核 `capture-tool-review-r1.json`
  保留已修问题及边界，不以测试替代硬件。
- Release 与资源：`build-r1.log` 缺少 SCPI 头文件、`build-r2.log` RAM 溢出均保留。
  BSS 增加 6996 B；原先少量 RAM 代码使对齐额外占用一页，显式 noinline 留在 XIP
  后 `build-r3.log` A/B 链接通过。A/B BSS 末端为 `0x2007f3c8`，链接剩余 3128 B，
  扣除固定堆 2048 B 后为 1080 B；不是运行时栈/堆余量。已检查常规 Core1 调用链
  约 1340/2048 B，尚非含中断的最坏栈证明。完整恢复 ELF/dis/map/package 存于
  `restored-build-r5/`；早期资源报告指向的可变构建目录不能充当冻结二进制。
- 当前源码 SHA256 `5757fa763833457dc8c2c5c9420246699870bd4c092f9aa39cf2338b1c5f5b40`，
  文件数 1149；包 SHA256
  `4fd62db14ed1488a3cb1e3d341a2b6d3695fcbc6d4ff81dc2cd4e382e3d04c43`。
  恢复时执行 `p3-r4` 的 run 并更新四板同包，流程通过但严格校准失败；`p3-r5`
  使用同包成功 OTA 记录 resume，重新执行校准、回放及环路后 passed、strict 均为
  true、diagnostic_failures 为空。当前凭证与 staged 源码一致，pre-commit 通过。
- 四板专项：恢复后同次启动内 `capture-r4/r5` 均 passed、flow_completed，errors
  为空；各轮各板 20 条 native 记录、零 recorder missed、SD/RAM 相同，全部真实
  START/STOP OK，运行期查询为零。三从匹配增量分别为 82/79/74、63/78/66，
  缺参考增量 26/31/38、34/33/42，陈旧为零。第二轮各源累计最大准入年龄
  79.256344/76.415376/71.636376 ms。ARM 从 16 到 17，observer 从 3 到 5，
  主机参考 epoch 从 1 到 2、generation 从 1 到 3；STOP 后历史保留但 inactive。
  最新完整 RX 字节与源端短历史对账；较早匹配对已不在线帧短历史内时明确为未覆盖，
  不虚构逐条线帧证明。两轮从机实际应用计数及各板 DPLL trace 记录数均为零。
- 主控原始 CSV/CRC/native/动作复核 `main-recheck-r1.json` 88 项通过；独审
  `restored-hardware-review-r1.json` 520 项通过，SHA256
  `d74357d97d277a61f147aebf7a910a31e4e8a465494fd053fa8275d9f2a1880e`。
  原始区间只传播给定的参考括号，未包含 GPIO/SM 检测误差，不是物理频率置信区间
  或输出域残差，不能用它宣称锁相。
- 时序限制：native baseline 到末样本，两轮 NO1 DPLL overrun 分别 +537/+551、
  deadline +506/+523，预算 136 us、累计峰值 299.528 us；第二轮峰值未再增加。
  首样本到末样本窗口分别约 5.700/5.704 s，DPLL 调用 3795/3798 次；不与
  baseline 窗口的调用分母混算。第一轮 NO2 DPLL 新增 1/1，其他从板及第二轮三从
  新增零。baseline 到末样本 TDMA overrun 为 3/83/70/54 和 3/63/94/55，deadline
  为 3/59/49/31 和 2/38/51/33；三从 RX overrun 为 56/49/16 和 40/21/19，
  observation drop 为 1504/1488/1468 和 1493/1470/1449。各板读回最小 FreeRTOS
  heap 为 20352 B。原件全部保留；DPLL 局部超限不新增健康 TDMA 隔离。
- 失败与回退：原实现 `p3-r1/capture-r1` 通过，但已暴露 NO1 DPLL 超时；
  `capture-r2` 因 NO3 START 仅有合成应答被工具中止，真实 STOP 完成，不算重臂通过。
  scratch-X 放置实验 `after-build-r4/` 构建通过，`p3-r2/r3` 拓扑读取失败；
  `stopped-triage-r1.json` 记录 NO1/NO2 CORE1_STALL、最后进度 DPLL entry，
  该进度不是故障 PC，静态布局不能证明根因。已撤回该放置实验并恢复原源码和原包；
  `scratch-experiment-review-r1.json` 保留审计，不宣称性能收益。恢复 `p3-r4`
  的 TRN01 候选覆盖、TRN03 margin 和 startup barrier 失败导致 strict=false，
  `capture-r3` 在操作硬件前拒绝；后续成功不追认这些失败。
- 证据根目录：`out/HardwareAcceptance/20260916/dpll-feedback-match-r1/`。
  下一 gate：继续 `VDC-FEEDBACK-001`，优先将参考缓存、配对和差分准备整体迁至
  Core0 单写者，复用现有发布区；先冻结 LIVE 硬件来源并建立 Core1 独立退休代际，
  实测 Core0 覆盖和 CPU 成本。只读成本审计见
  `out/dpll-feedback-match-r1/next-match-cost-audit-r1.json`。随后关联实际输出模型、
  接通逐从校正与连续 DCO 应用；完整绝对映射和首帧证明仍不作运输前置。

### VDC-PROGRESS-20260916-009：三从原始反馈运输与重臂对账

- TODO task ID：`VDC-FEEDBACK-001`、`VDC-FLIGHT-001`；长期目标保持 IN PROGRESS。
- 状态：DONE，仅关闭原始反馈运输切片。软件、构建、当前源码四板 P3、两轮专项及
  原件独审均通过。本节数字均为实验快照，非冻结协议、时序预算或产品精度事实。
- 代码提交：`07dfda6`；提交时 staged 源码指纹与 P3 凭证匹配，pre-commit 通过。
- 实现：Core0 将当前显式 TAP 的同条 LIVE 编成固定原始反馈记录，经既有 VDC 区
  分片发送；组内冻结，只在 FIFO 发布成功后前进，组间保留普通 VDC 发布。
  NO1 按来源独立重组并保留完整诊断历史；普通 RefMem/ACK/Control 字段继续处理。
  STOP、DATA 暂停及确认绑定变化取消部分组装，竞争只跳过。未接 PI 或 DCO 应用。
- 修复：历史记录身份比较改为内层 source/target，避免新身份首组取消后无法重试；
  无活动反馈组时 TAP/LIVE 暂不可用仍发布普通 mailbox。补齐 RX/overlay 准备、
  origin adapter 和物理 seed/publish 四处反馈 class 准入，保留 CRC/来源/目标校验，
  旧命令 class 与未知 class 仍拒绝。仅 RefMem codec/parser 通过不足以证明飞行运输。
- 软件：主控反馈与旧消费者回归 52 项通过，真实 prepare/adapter/物理容量测试
  10 项通过；独审同范围 62 项通过，采集工具作者及独审各 70 项离线测试通过。
  `implementation-review-r1.json` 记录 22 项检查及已修发现，SHA256
  `b18e3c37e8a1b2960ed1eb218afe66cf8d2e5b3b246a55b5e6e695eb7b898a33`。
- Release：`build-r2.log` 真实子进程 exit 0，A/B 链接与 Flash 检查通过；原
  `build-r1.log` 的 PowerShell stderr wrapper 异常保留，后续子进程检查不覆盖旧日志。
  A/B BSS 相对已验收同条留存版本均增加 1572 B，链接地址余量 10124 B；不是运行时
  栈/堆水位。冻结 map/package 见 `after-build-r2/`、`resource-review-r2.json`。
- 源码指纹 `6f3d22ea8a96d2443d8299828c6991e1f5fc65a51b515bb99a72f195afb75a51`，
  文件数 1141；包 SHA256
  `f0a90843c4f8c69c89d3cd0915ce1db2b89a05bc160c05d5c9dd3a44c7677aba`。
  `p3-r1/acceptance.json` 的 passed 与 strict_gates_passed 均为 true，
  diagnostic_failures 为空，四板均更新同包；不以旧 build label 单独证明版本。
- 四板专项：`capture-r1` 与 `capture-r2` 均 passed、flow_completed，errors 为空。
  每轮每板 20 条 TDMA native 记录、零采集 missed，SD/RAM 逐字节一致；运行区间
  查询数为零，四 START/STOP 均真实 OK，应答完成后才导出。DPLL trace 各板均为零
  更新记录，保留该事实；不能宣称 trace 已证明闭环响应。
- 第一轮三从完整发布增量 124/131/132，NO1 完整接收增量 110/112/109，最终完整
  字节对应源端历史索引 0/0/1；末次组装耗时 80/75/75 ms。普通 NO1 VDC 接收增量
  2287/2279/2286。第二轮完整发布 123/129/130、接收 121/118/124，均匹配历史
  索引 0，组装 86/90/75 ms，普通接收增量 2287/2249/2262。全部来源 ARM 从
  16 推进至 17、observer epoch 从 3 至 5，每轮各源一次取消且 STOP 后 active 为零。
  实际 follower_apply_count 均保持零。主控独立原始 CSV/CRC/native/动作复核
  `main-recheck-r1.json` 共 59 项通过；工具仅最新 RX 记录，不提供所有接收测量序号历史。
- 原件独审：`hardware-review-r1.json` 共 301 项通过、无遗留 finding，结论为
  `APPROVE_DIAGNOSTIC_RAW_FEEDBACK_TRANSPORT_AND_STOP_ARM_RESTART`，SHA256
  `840577d169fe2a403c0d7e35dfbaa804a16e537188fe04b4650e0aafd76aadfc`。
  独立解码原始 CSV/CRC、两轮八份 native 和 SD/RAM，比对 P3 引用哈希与原始动作；
  不授予时序、无丢失或锁相资格。
- 限制：第一轮各源拒收增量 128/158/204、第二轮 26/94/71，重组超时均零；完整发布
  不等于完整送达。native baseline 到末样本，第一轮 NO1–NO4 TDMA overrun 增量
  1/188/85/50、deadline miss 1/145/54/34；第二轮 1/193/78/35、1/147/53/24。
  三从 RX ring overrun 两轮分别 40/41/31、31/9/17，observation drop 分别
  1504/1480/1449、1508/1488/1464。运输通过不追认完整时序、无丢失或精度通过。
  组装耗时也不是测量年龄；source epoch/run 不是共同会话，跨重启旧记录拒绝、
  同次主机参考与输出域换算仍属后继控制工作。
- 证据根目录：`out/HardwareAcceptance/20260916/dpll-raw-feedback-r1/`。下一 gate：
  分离提交后，按实测反馈延迟评估主机同序参考缓存与输出域关联，
  接通逐从校正及实际 DCO 应用。首帧精细证明和完整绝对时间映射不作为运输前置。
  只读后继审计 `next-control-audit-r1.json` 指出现有主机 DMA 原始池不能覆盖分片延迟，
  需要有界保留同序参考；raw 差分可消固定偏差，但不会直接反映 DCO 校正效果。
  缓存、采样时实际 DCO 身份和连续重定锚按后继独立审核/验收推进，不能用原始 PIO
  斜率代替输出模型残差，也不把本审计当作控制已接通。

### VDC-PROGRESS-20260916-002：归纳四板数据运输与真实闭环长期目标

- TODO task ID：`VDC-LONGTERM-001`、`VDC-FLIGHT-001`、`VDC-FEEDBACK-001`、
  `VDC-DRIFT-001`、`VDC-PRECISION-001`、`VDC-RECOVERY-001`。
- 状态：IN PROGRESS。用户要求优先让三从收到数据、进入真实闭环，以 internal 快速
  判断应用和漂移，最终复核实际输出；具体任务与依赖已落入 TODO，不冻结新 wire 契约。
- 实现快照，非已验收事实：工作区新增指定 master 的普通 mailbox 稳定接收记录及
  `SYSTem:REFMEM:SYNC:TDMA:VDC:FLIGHT?` 只读查询；未开放命令原型、未接 DCO 应用。
- 作者软件证据：`out/HardwareAcceptance/20260916/vdc-flight-rx-r1/host-review-summary.json`，
  新接收与兼容测试结果待主控随实现切片复核；Release、资源差额和当前源码四板 P3
  尚未完成，不能用旧固件证据替代。独审已报告采集 wrapper 的成功判定和失败恢复
  问题，须修复后再用于专项验收。
- 下一 gate：`VDC-FLIGHT-001` 的独审修正、Release/资源核算、当前源码 quick P3 及
  指定来源/序号/数据一致专项；接收通过后进入 `VDC-FEEDBACK-001`，目标仍保持执行中。

### VDC-PROGRESS-20260916-003：指定主机接收记录与四板专项

- TODO task ID：`VDC-FLIGHT-001`、`VDC-FEEDBACK-001`。
- 状态：IN PROGRESS。普通 mailbox 指定主机接收切片已完成软件、构建、四板 P3 和
  接收专项；完整长期闭环仍未完成，以下数字均为本轮快照，非产品事实源。
- 实现：Core0 RefMem 独占接收记录发布，校验来源槽、目标、CRC、READY 和新序列；
  绑定当前角色及已 ACK 的 ring 配置，旧 FIFO 代际不复活。STOP 保留历史、active 清零，
  只读 `SYSTem:REFMEM:SYNC:TDMA:VDC:FLIGHT?` 导出；不开放旧命令原型或应用 DCO。
- 软件/独审：新接收测试 20 项、兼容测试 26 项通过；源码和作者产物 hash 已复核。
  `vdc-flight-rx-r1/host-review-summary.json` 与
  `dpll-flight-receive-r1/implementation-review-r2.json` 保存证据。采集脚本成功判定与
  缺失字段恢复已修复，主控 6 项离线测试及独审 17 项纯模拟检查通过。
- Release：A/B 链接及 Flash 检查通过；map BSS 净增 104 B，链接 RAM 剩余地址空间
  12148 B，不能当作运行栈/堆水位。见 `dpll-flight-receive-r1/resource-review-r1.json`
  和冻结 `after-build/`。源码指纹
  `79069f098382b323b253cc338397f2dc41e34c294dcfea19302db37a76368586`，
  固件包 SHA256 `f0622ddf3dc0e76e08e6debafd6b10b50f4fecc98b252f76955e286f5bf06384`。
- P3：`dpll-flight-receive-r1/p3-r1/acceptance.json` 的 `passed` 和
  `strict_gates_passed` 均为 true，四板均完成新包 OTA；旧 build label 未作为唯一身份。
- 专项：`dpll-flight-receive-r1/capture-r1/input-probe.json` 无采集错误，运行期间查询数
  为零；NO2/NO3/NO4 接收增量分别 2258/2257/2151。三从最终同源 slot 0、mailbox
  序号 3522、CRC 59331、运输序号 6300，phase=-176 ns、rate=2260 ppb；相位/频率与
  NO1 STOP 后 DCO 量化值一致。四板各 20 条板端记录，原生 CRC 通过，SD/RAM 逐字节
  一致。分析见 `dpll-flight-receive-r1/receive-analysis-r1.json`。
- 证明范围：接收增量包含 bootstrap 与自主阶段；本轮证明三从保留记录一致及与主机
  最终相位/频率对账，没有同 mailbox 序号的主机 TX 全字段归档，不声明逐帧完整送达。
  三从实际 DCO 应用增量为零，trace 均无新记录，不据此判断锁相或精度。
- 下一 gate：收敛提交本接收切片；`VDC-FLIGHT-001` 的精确发送版本关联补证可与
  `VDC-FEEDBACK-001` 的观测/命令身份设计共同准备，先完善各从反馈到专属校正的方案。
  必须使反馈体现本从实际 DCO 校正效果；现有 internal 的 clock residual 与 DCO
  输出模型需明确对应，不能用不会响应 DCO 的斜率驱动伪闭环。
- 后继只读审计：`dpll-flight-receive-r1/feedback-next-audit.json` 保存字段、量纲、
  有效条件、输出模型、候选承载、资源估算和测试映射；执行候选落入
  `VDC_COMMAND_TRANSPORT_PLAN.md`，尚未冻结编码或新增反馈控制代码。
- 主控/独审收敛：`dpll-flight-receive-r1/hardware-review-r1.json` 的 40 项核验通过，
  结论为 APPROVE_RECEIVE_SLICE；原生 drop/schedule miss 记录保留，不据接收通过宣称
  全环时序或无丢帧。实现已提交 `3e76053`，提交 hook 核验匹配 P3 凭证通过。
- 上述相对证据路径均位于 `out/HardwareAcceptance/20260916/`，四板已 STOP，串口已关闭。

### VDC-PROGRESS-20260916-004：统一 DCO 输出投影与诊断脉冲计算

- TODO task ID：`VDC-FEEDBACK-001`、`VDC-FLIGHT-001`。
- 状态：IN PROGRESS。本切片准备能反映实际 DCO 校正的输出域残差，尚未接通
  各从反馈、主机独立控制器、专属命令或实际应用；以下数字均为本轮快照，非产品事实源。
- 实现：Domain 新增不可变 DCO 的纯投影和同事件取模相位残差；现有 manager 的
  phase-only 诊断脉冲 deadline 复用该投影，旧 clock 映射保持。调用者仍须提供
  真实本地 RX 时间、对应事件的 NO1 输出相位和当时 DCO 模型，函数不授予样本资格。
- 软件：新增投影与原 Domain 入口共 13 项测试通过，批量覆盖 20433 场景；既有
  manager/replay 兼容 85 项通过。独审以独立大整数 oracle 执行 7404 场景均通过。
  测试范围内诊断 deadline 对应输出整周期的计算误差不超过 1 ns；phase 增加
  300 ns 使计算 deadline 提前 300 ns。这不是 GPIO 精度或实板锁相证据。
- Release：A/B 及 Flash 链接检查通过，BSS 净增零，链接 RAM 剩余地址空间
  12148 B，不代表运行栈/堆水位。源码指纹
  `f4e2a8d6195f966cb82f254acddaf4f14945c47ddc37f8c2d84f426c200408fd`；
  固件包 SHA256 `a81dce6244bfc70397369b0b3e14aef70e24f49d2c8c8fb3df0ae58363b165ba`。
- 四板 quick P3：`p3-r1/acceptance.json` 的 `passed`、`strict_gates_passed`
  均为 true，诊断失败列表为空，耗时 186.343 s；验收范围是四板 TDMA 集成，未验收锁相。
- 接收补测：`capture-r1/input-probe.json` 与 `receive-analysis-r1.json` 均通过；
  NO2/NO3/NO4 接收增量 2190/2244/2208，最终同源 slot 0、mailbox 序号 3506、
  CRC 52363、运输序号 6323，phase=-172 ns、rate=2160 ppb，与 NO1 最终 DCO
  量化值一致。四板各 20 条原生记录通过 CRC，SD 与 SRAM 逐字节一致，运行期间
  查询为零。三从实际 DCO 应用增量仍为零，trace 无新记录，不声明闭环或精度。
- 前序固件的 STOP 后只读尾部诊断：`transport-tail-r1/input-probe.json` 记录
  NO1 准备尾序号 3524，三从最后保留 3522；“最新准备值”不能代替同序发送版本。
  此诊断绑定前序接收固件，与本切片 P3 安装的新包分开，不据一次尾差推导通用历史容量。
- 证据目录：`out/HardwareAcceptance/20260916/dpll-output-projection-r1/`，其中
  `host-review-summary.json`、`implementation-review-r1.json`、`resource-review-r1.json`
  和冻结 `after-build/` 保存原件。命令采用当前 Windows 环境的原生 Python/PowerShell
  入口；提交 hook 使用项目 Git Bash。
- 主控/独审收敛：`hardware-review-r1.json` 的 42 项核验通过，结论为
  APPROVE_OUTPUT_PROJECTION_SLICE；源码已提交 `2e014e1`，匹配 staged 源码的
  P3 凭证及提交 hook 通过。板端 drop/schedule miss 原件保留，不声明逐帧完整送达
  或整表 WCET 已全部闭合；四板已 STOP，采集流程恢复配置并撤销临时许可，串口关闭。
- 下一 gate：推进 `VDC-FEEDBACK-001`
  的真实有效观测、反馈运输和专属校正/应用，精确版本身份随接线闭合。

### VDC-PROGRESS-20260916-005：NO1 运行中原始发射记录留存

- TODO task ID：`VDC-FEEDBACK-001`。
- 状态：IN PROGRESS。新增主机本地观测承载，为后续匹配各从反馈提供来源；
  未接远端反馈、控制器或 DCO 应用。以下数字均为本轮快照，非产品事实源。
- 实现：Core1 在既有自主 origin service 内，从已完成的 cyclic raw record 作
  一次有界复制；不读仍由 ARM 改写的 state raw 字段，不消耗原 adapter 观察游标。
  复验发布推进量、epoch/fault/format 和首尾 sequence，物理 owner 核对复制时间界；
  Core0 只读受 guard 保护的独立稳定副本。成功/失败 STOP、persona 或 ARM 退休
  active，历史保留。`READ:CALibration:ORIGin:LIVE?` 在全部 STOP 后读取。
- 软件：helper 六项测试覆盖 16531 场景（含 16020 次逐字节 DMA 交错），通过；
  原 STOP 生命周期及实际 collector/getter/SCPI 序列化共两项通过。独审重跑 helper
  场景和两个集成可执行文件均通过；旧冻结查询输出格式不变。
- Release：A/B 和 Flash 检查通过，BSS 净增 112 B，链接 RAM 剩余地址空间
  12036 B，不代表运行栈/堆水位。源码指纹
  `ca6cdd472cef9f50db9351eaa112e911d631a516f58683941cad15f26847ed40`；
  固件包 SHA256 `91e622b4d52995305ed6d565ca3bc96f55dd2799dbdedbd8769b3ea57bf4cdc2`。
- 四板 quick P3：`p3-r1/acceptance.json` 的 passed/strict_gates_passed 均为 true，
  诊断失败列表为空，耗时 181.162 s。P3 范围为四板 TDMA 集成，不含输出锁相。
- 专项：`capture-r1/input-probe.json` 通过，错误列表为空、运行期间查询为零。
  NO1 运行中 copy_count 从零增加到 4051，reject_count 为零，STOP 后 retained=1、
  active=0。最后保留 epoch=1、published_version=12150、sequence=6319、
  identity=753201517；原始记录全部字段与冻结池 age=1 的同序记录相同，冻结尾版本
  12152 减去该 age 对应的版本步数也等于保留版本。三从普通指定主机接收增量
  2199/2254/2179，未产生 origin live 副本或从机 DCO 应用。
- 范围：本轮证明运行中取得完整本地发射记录，不证明它是合格边沿点时间；
  单槽会跳过物理圈次，不构成延迟反馈的同序历史缓存，也不代表特等席反馈已接通。
- 后继路线独审：`../dpll-feedback-event-r1/route-review-r1.json` 支持直接使用各从
  独立 PIO 边沿/线上序列。该入口不消费 DMA packet，因此 DMA 候选到边沿关联不再
  作为前置；真正待接线的是相对 CS 的序列采样配置、内部同事件配对、observer 自身
  退休/恢复、本板时钟锚，以及 NO1 对应事件留存。首样可跳过，不要求全局首帧证明。
- 证据异常：主控误将前序投影基线复制到已存在的 `before-build/`，覆盖旧映射和包。
  全 out 按原 SHA 搜索未找到旧映射副本；旧接收切片报告中的 before-map 原件现在
  不可复核，未改写旧报告或声称恢复。`baseline-path-collision-r1.json` 保留详情。
  本轮资源比较使用独立 `before-current-r1/`，与前序投影 `after-build/` 的 SHA 相符；
  新包、旧硬件采样和本轮验收原件不受该路径碰撞影响。
- 证据目录：`out/HardwareAcceptance/20260916/dpll-origin-live-r1/`；helper 和
  集成证据分别见 `host-review-summary.json`、`host-integration-r1.xml`，实现独审见
  `implementation-review-r1.json`，资源比较见 `resource-review-r1.json`。
- 收敛：`hardware-review-r1.json` 的 54 项检查通过，结论为
  APPROVE_LIVE_ORIGIN_COPY_SLICE；实现提交 `b91ac8c`，staged 源码 P3 凭证和
  提交 hook 通过。四板已 STOP，原始应答确认配置恢复及临时许可撤销，串口关闭；
  原生 drop/overrun/schedule miss 保留，不据本次通过宣称逐帧无损或所有时序已闭合。
- 下一 gate：按 `VDC-FEEDBACK-001` 接入直接
  PIO 观测和逐从反馈，不以本地留存通过关闭反馈或锁相目标。

### VDC-PROGRESS-20260916-006：独立 CS 采样配置与启动重复序号定位

- TODO task ID：`VDC-FEEDBACK-001`。
- 状态：IN PROGRESS。完成采样配置和原始帧头诊断；稳态序号专项未通过，尚未接通
  反馈运输、独立控制器或从板 DCO 应用。以下数字均为本轮快照，非产品事实源。
- 实现：新增 STOP-only `SYSTem:TDMA:EVENt:TAP` 和只读 `TAP?`。Core0 在既有
  ring control guard 内检查 STOP/配置 ACK、pending、stopped update 和 geometry，
  发布有 guard 的 SRAM intent；Core1 在 ARM 冻结 prefix/delay/generation，显式
  路径不依赖 DMA alignment 或 overlay 训练。读回分别记录 requested/applied/actual；
  actual_valid 仅表示 PIO/OSR 装载，STOP 后保留历史。默认关闭，PIO 程序保持原样。
- 软件：主控最终 tap/control/geometry/adapter 共 11 项、service/candidate 兼容
  3 项通过；作者相关事件回归 19 项及独审 3 项通过。新 tap 覆盖 160 组实际启动
  组合与边界，service 覆盖尚未 physical start 的 ARM、失败 STOP、失败 START、
  竞争写入及配置恢复。旧提取式 fixture 缺少已有 first-window seams 的失败与修复
  记录保留在 `host-event-summary.json`，不作为生产故障。
- Release：A/B 和 Flash 检查通过，BSS 净增 56 B，链接 RAM 剩余地址空间
  11980 B，不代表运行栈/堆余量。源码指纹
  `5066212ab27d0145de512af70b31676b5008aace9d603232c65abbb44f92bfa2`，
  固件包 SHA256 `274b9dbcbd44c4ca68611a5806b1cb19530c658cc3af21d5110aebb2ca1bcdd7`。
  基线使用前序不可变 after-build 的独立副本，保存逐文件 SHA 对账。
- 四板 quick P3：`p3-r1/acceptance.json` 的 passed/strict_gates_passed 均为 true，
  diagnostic_failures 为空；证明默认配置下四板 TDMA 集成，不代表锁相验收。
- 帧头专项：`header-r1/input-probe.json` 通过。NO2/NO3/NO4 的 CS 相对 prefix
  分别为 24/13/2 bit、WAIT-high delay 为 15 cycle，实际读到完整原始字
  `0x5444A400`，对应 magic 和本次 164 B 包长。重复常量导致旧观察器以 SEQUENCE
  拒绝并退休，符合该诊断预期；主机接收增量为 2173/2225/2198，健康运输继续。
- 序号专项失败：`sequence-r1/input-probe.json` 的 transport_capture_passed 为 true，
  但最终 passed 为 false。三从 requested/applied/actual 均匹配 prefix 120/109/98，
  仅首个空快照 ACTIVE；随后均 joined=1、sequence=1、ordinal=0、fault_bits=0，
  下一次原始序号仍为 `0x01000000`，触发 reason=SEQUENCE，之后永久保持 INVALID。
  该原件支持启动重复序号导致旧观察器退休，不支持稳态连续观测或反馈输入已合格。
  本轮接收增量为 2197/2184/2198，三从 DCO 实际应用增量均为零。
- 两次采集均无流程错误，运行期间 SCPI 查询为零；所有查询/导出在 START 前或
  全板 STOP ACK 后执行。原生记录、SD/SRAM、配置恢复与许可撤销保留原件，
  不以帧头通过覆盖序号专项失败，不由稀疏快照推导逐帧无丢失。
- 证据目录：`out/HardwareAcceptance/20260916/dpll-event-tap-r1/`；资源见
  `resource-review-r1.json`，实现独审见 `implementation-review-r1.json`。
- 收敛：`hardware-review-r1.json` 独立复核当前源码 P3、帧头、失败序号、原生 CRC、
  SD/SRAM 原始字节及动作屏障，结论为 APPROVE_STOP_ARM_CONFIGURATION_AND_HEADER_DIAGNOSTIC_ONLY；
  steady_sequence_observation_passed 仍为 false。实现提交 `084b42f`，匹配 staged
  源码的 P3 凭证及提交 hook 通过。四板已 STOP，requested tap 恢复默认，应用历史
  保留，临时许可已撤销、串口关闭；该提交不表示反馈运输或锁相已完成。
- 下一 gate：`VDC-FEEDBACK-001` 的观察器自身有界恢复；坏样本退休后在后继 service
  重建本地 epoch，保留失败原因，重新从任意稳态有效序列开始，不重启健康 TDMA、
  不恢复全局首帧或 DMA 候选关联前置。完成独立软件与当前源码四板 P3 后，再接
  同事件测量留存、本板时钟锚和反馈运输。
- 后继只读方案：`next-observer-recovery-plan-r1.json` 优先处理显式采样下的
  SEQUENCE 拒绝且无硬件 fault。恢复只动观察器 SM，保留物理 ARM 首次 enable 的
  RXSTARTCUT 原件，不能复用会重读 requested 或覆盖首次档案的完整 prepare/start。
  `next-feedback-measurement-plan-r1.json` 保留后续本板时钟域审计：manager 的
  time_us、TIMER1 tick、逻辑环路时间及现有 DCO 本地锚不可直接混用，不要求完整
  跨板绝对时间映射作为原始反馈运输前置。

### VDC-PROGRESS-20260916-007：启动序号拒绝后的观察器自身恢复

- TODO task ID：`VDC-FEEDBACK-001`。
- 状态：IN PROGRESS。为稳态原始反馈输入增加观察器自身恢复；不接通反馈运输、
  主机独立控制器或从板 DCO 应用。以下数字均为本轮快照，非产品事实源。
- 实现：显式 tap 下纯 SEQUENCE 拒绝先保留失败、退休当前 epoch；后继 Core1
  service 分开执行 RESET_PENDING、WAIT_IDLE/start，每次只做一个有界步骤。
  保持同一 ARM 冻结的采样配置，严格递增 observer epoch，epoch 内检查不放宽。
  只重置观察器 SM/FIFO/IRQ，不重新 ARM TDMA、不动 DMA 或转发 SM；首次
  RXSTARTCUT 原始字段保留，当前 epoch 样本字段清空，旧候选不能跨代复用。
- 失效边界：STOP、persona/ARM/tap 绑定或时钟变化、epoch 耗尽及硬件 fault 取消
  恢复。既校验失败相位的 final fault，也在后继重置前复验 sticky stall/bad-PC，
  不把主动 disabled 当故障。其他错误的恢复策略未扩展。
- 诊断：独立 `SYSTem:TDMA:EVENt:RECovery?` 保留累计失败、尝试、启用、延期和取消
  计数、最后失败及物理 ARM/tap 绑定；getter 有界复制，失败保留输出。enable_count
  仅表示重新启用，不是有效样本或 DCO 应用。last_batch_sequence_first 只指向采集
  批次首词，不能与最后发布的 sequence/ordinal 混拼为同次测量。native EVENT ABI 不变。
- 软件：主控最终相关测试 9 项通过；作者相关事件回归 20 项通过，新恢复可执行
  文件覆盖 13 组生产路径，包括多轮启动重复、CS 延期、dirty enable、STOP、绑定/
  时钟/epoch 取消、晚到故障、首次档案及 DMA/转发 SM 不变。独审复跑专项、核对
  生成的生产函数体与源码一致；采集判定 15 个模拟正负例通过。测试发现并修复
  恢复重置前晚到 sticky fault 被清除的问题；FDEBUG W1C 注入时机的 fixture 修复
  及前序失败保留在 `host-event-recovery-summary.json`。
- Release：A/B 与 Flash 检查通过，BSS 净增 148 B，链接 RAM 剩余地址空间
  11832 B，不代表运行栈/堆余量。源码指纹
  `f4b1e5a3709e6e06168d6d23e4d740cfe6cc841e74f5cefe5d9ca830b37957ab`，
  固件包 SHA256 `d1ad89071de8207babbb32acdde40597981cd5e89d813eca3a8c44a2d0faaca8`。
- 四板 quick P3：`p3-r1/acceptance.json` 的 passed/strict_gates_passed 均为 true，
  diagnostic_failures 为空；该门禁验证默认 tap 配置下的 TDMA 集成，恢复路径另做专项。
- 专项首轮 `capture-r1` 保留失败：NO2 START 应答超时，底层 helper 返回合成的
  `OK(no payload; verified by state readback)`，实际未执行所称的状态读回；采集器
  拒绝该文本。NO1 尚未 START，随后四板真实 STOP ACK，因此不能据零恢复计数判断
  恢复实现失败。独审见 `capture-r1-failure-review.json`，不将合成文本提升为成功 ACK。
- 同源码、同采集脚本的 `capture-r2` 完成，passed/flow_completed 为 true，errors
  为空，运行期查询为零。三从失败/尝试/启用各增加一次，从 observer epoch 3 恢复到
  epoch 4；各有 19 个 ACTIVE 原生快照，同代 sequence/joined/published 分别推进
  4704/4646/4587，原物理 ARM 首档仍属于旧 epoch。最后失败为 SEQUENCE 且 fault
  为零，STOP 后 pending 为零。主机没有进入恢复，三从主机接收增量为
  2148/2281/2283，实际 follower_apply 增量仍全零；不据此宣称反馈闭环或锁相。
- 恢复 service 部分的累计峰值为 257/213/104 µs，采样 EVENT service 最大值为
  245/401/328 µs；后者在恢复时重置，二者均不是完整 TDMA phase WCET。
  原生记录是稀疏观测，不据连续序号差推断逐物理帧无损。全部 STOP 后导出，
  requested tap 已恢复、临时许可撤销。`hardware-review-r1.json` 独立复核 111 项通过，
  disposition 为 APPROVE_OBSERVER_ONLY_RECOVERY；主控 `main-recheck-r1.json` 再次
  解码四板原始记录并核对 SD/SRAM 全等及恢复判据。原生窗口 TDMA overrun 增量为
  2/1/4/1，NO3 deadline miss 增加一次，三从 RX ring overrun 增量为 30/20/13，
  observation drop 同样保留；本切片不关闭整体时序门禁或授予逐帧无损。
- 收敛：实现与匹配 P3 凭证提交 `dc2ee12`，staged 源码核验和提交 hook 通过；
  文档另行提交，长期目标及逐从反馈任务保持 IN PROGRESS。
- 证据目录：`out/HardwareAcceptance/20260916/dpll-event-recovery-r1/`；软件摘要见
  `host-event-recovery-summary.json`，独审见 `implementation-review-r1.json`，
  资源原件见 `resource-review-r1.json` 和独立冻结的 before-build/after-build。
- 下一 gate：当前恢复专项已完成原件独立复核，分离提交后保留同一完整 event
  record 及本板时间锚，接原始反馈运输，
  不把完整跨板绝对时间映射或最终输出精度作为原始运输的前置。

### VDC-PROGRESS-20260916-008：同条观测留存与本板 TIMER1 锚

- TODO task ID：`VDC-FEEDBACK-001`。
- 状态：DONE（仅同条诊断观测留存切片）。在已验收的观察器恢复基础上，为反馈准备完整本地原始记录；
  反馈运输、主机专属控制与从机 DCO 应用仍未接通，不关闭父任务。
- 实现范围：最终 fault 复验后取同条完整 event，配合同 epoch 的 TIMER1 enable
  前后区间、物理 ARM 身份和频率一次发布。Core1 独占发布与退休，Core0 getter
  有界读取独立 atomic SRAM 副本；新 epoch 私有锚与旧发布记录分离。
- 生命周期：ANCHOR_VALID 是所留存记录的历史采集属性；STOP/INVALID/ARM/clock
  失效清 ACTIVE 并保留原件。时钟入口不初始化或写 TIMER1，无循环读取；高字变化、
  未初始化或配置不符返回失败，不能因此重启健康 TDMA。TIMER1 raw、ARM 相对
  cycles 和 manager 的 time_us 时间域分别保留，不授予正式时间或控制资格。
- 软件快照：主控最终组合回归 15 项通过。完整 LIVE 生产路径覆盖 10 组：批内最后
  同条记录、首序号为零、最终 fault、不完整批次、恢复换锚、STOP/ARM、时钟取消、
  缺锚、空队列时绑定变化及 getter 竞争/读取期限。TIMER1 device/no-device 用例
  覆盖初始化前拒绝、真实高低高读取变化、source/pause/hz 变化、空输出及 host 拒绝。
  SCPI/owner 真实函数体覆盖非零高字、低字回绕、无记录和诊断资格位。
- 失败保留：独审发现正常 STOP 后反复 service 给旧记录追加 BINDING；现只退休仍
  有效的候选，重复 STOP 后完整历史保持。空队列时 ARM 变化也须主动退休，不等
  下一批记录。作者广回归曾为 24 通过、1 个 disabled fixture 漏头文件失败，修正
  后定向通过；主控最终回归包含该 disabled 路径。早期 clock/LIVE fixture 失败
  原件保留，不把修正前结果追认为成功。
- Release：初版 A/B 链接通过，但新增 RAM 函数使 data 末尾跨过工作区对齐页，
  RAM 总占用增加 4232 B，原件保留于 `after-build/`、`resource-review-r1.json`。
  随后仅将配置检查 `vdc_timestamp_clock_is_current()` 放回常规 XIP，计数器采样
  `try_read_ticks64()` 仍驻 RAM；原调用链本就使用 XIP 的 clock_get_hz，不改变
  Flash 操作的 Core1 park 边界。调整后的相关 host 5 项通过，A/B 与 Flash 检查通过。
  `after-build-r2/`、`resource-review-r2.json` 显示 RAM 净增 136 B，链接余量
  11696 B，不代表运行栈/堆水位；没有修改 PIO 程序、缓冲区对齐或 OTA 实现。
- 独审：r2 实现审核 53 项及独立 host 8 项通过，见
  `implementation-review-r2.json`。r1 审核产出时恰逢主控调整函数放置，源码指纹与
  旧独审 host 不同而未通过；旧报告保留，不将该绑定失败当作生产逻辑错误。
- P3 首轮 `p3-r1` 在拓扑识别失败：相邻 NO1→NO2→NO3→NO4 已检测到数据，
  NO4→NO1 未检测到 RX，未进入留存专项。四板 OTA 及初始化阶段已完成；
  `topology.log` 与 `p0t-topology/summary.json` 保留，不能由该结果定位为 LIVE 故障。
  r1 的拓扑清理仅有 best-effort 代码路径，缺少原始 ACK，不能声称已由该轮证据
  证明四板 STOP。独立失败归类见 `p3-r1-failure-review.json`。
- 同源 `p3-r2` 流程完成但 strict_gates_passed 为 false。TRN-01 的 22 次测量均
  accepted、身份失败为空；NO2/NO3 各自 8 次均只取得偏移 1，未满足两候选覆盖，
  NO4 则已有 0/1 两候选。TRN-03 未找到满足重装预算的组合行，最小余量为负。
  后继 TDMA startup barrier 超时；四板 START 均真实 OK，NO1 的运输坏帧/reject
  增长，不能简化为 ACK 丢失或没有发车。STOP_BEFORE_EXPORT 与 STOP_FINAL 均有
  四板真实 ACK 和 stopped 读回，失败原件全部保留。同源 `p3-r3` 严格通过，
  strict_gates_passed=true、diagnostic_failures 为空；三从均有实测 SCK 两候选，
  组合矩阵选出满足重装预算的行。该结果不追认前两轮，也没有放宽门禁。
- 专项：`capture-r1` 在 NO2 START 缺少真实 ACK 处中止，NO1 尚未 START；底层返回
  合成字符串不能代替实际状态验证，旧 LIVE 历史不是本轮观测。随后四板 STOP ACK
  及关闭读回均有效，失败独审保留于 `capture-r1-failure-review.json`。
  同源 `capture-r2` 完成，四板 START/STOP 均真实 OK，采集期间查询数为零；
  四板各 20 条 native 原件，SD 与 RAM 字节相同。三从恢复各一次，新 epoch 内
  19 个 ACTIVE 快照的序号/ordinal 增量为 4706/4643/4579；最终 LIVE 序号均为
  6325、epoch 为 4、flags 为 13，保留完整记录与同 epoch 的本板时间锚，
  与最后 native 摘要的推进一致，STOP 后两次 LIVE 读取完全一致。
- 数字均为本轮证据快照，非事实源：TIMER1 锚区间宽度为 284/839/286 ticks，
  对应 1136/3356/1144 ns；这是观察器启用的包围区间，不是边沿误差或输出精度。
  当前不能把区间中点当作已达目标的时间戳。指定主机接收增量为 2257/2264/2229，
  三从实际 DCO 应用增量均为零。反馈运输与控制接线仍待后继完成。
- 时序与丢样保留：native baseline 到最后样本的 TDMA overrun 增量为
  2/20/30/20、deadline miss 为 1/11/11/12；三从 RX ring overrun 为
  34/25/62、observation drop 为 1503/1476/1447。这不是全相位 WCET 或无丢样
  验收，亦不能仅比较不同轮次摘要就归因于新留存代码；后续运输负载继续记录此项。
- 独立硬件审核 `hardware-review-r1.json` 共 125 项通过，批准范围仅诊断 LIVE
  留存；SHA 为 `fbf520748b219cba105b2c055c62724da1c620efdea51b1a65d491cb3c5815fd`。
  主控另行重解四板 native、核对 SD=RAM、STOP 与 LIVE，见 `main-recheck-r1.json`。
  软件验证完整记录关联；稀疏 native 只证明后继摘要一致性，不能单独证明每条原始
  RX/TX 的物理对应。代码提交 `cb91cec`，staged P3 指纹门禁通过。
- 证据目录：`out/HardwareAcceptance/20260916/dpll-event-live-r1/`。构建前已从前序
  不可变 after-build 冻结本轮 before-build，逐文件 SHA 见 `baseline-binding-r1.json`。
- 下一 gate：`VDC-FEEDBACK-001` 按固定特等席配额运输各从原始反馈，逐来源重组、
  取消旧会话并核对完整记录；随后接通 NO1 专属校正与从板实际应用。
  不恢复首帧或完整绝对时间映射前置，长期目标保持 IN PROGRESS。

### 前序实现回顾

`VDC-RESOURCE-001` 当前编译容量的 compact RX 资源切片已闭合，有限采集的 STOP 后
交接通过当前源码四板 quick P3 验证，见下方 `VDC-PROGRESS-20260914-006`。
自主 origin 补测发现时间输入尚未接通，见 `VDC-PROGRESS-20260914-007`；事件与资源
审计见 `VDC-PROGRESS-20260914-008`；原始记录原型、当前容量目标链接及板端预采见
`VDC-PROGRESS-20260914-009`；配置矩阵、raw 退休及两次重臂补测见
`VDC-PROGRESS-20260914-010`；owner 几何与目标容量资源补测见
`VDC-PROGRESS-20260914-011`；其余编译容量链接及真实地址构造、上限容量四板预采见
`VDC-PROGRESS-20260914-013`；RefMem 向量更新的栈/复制收敛与快速验收对照见
`VDC-PROGRESS-20260914-014`；真实构造块取消与复用的软件补证见
`VDC-PROGRESS-20260914-015`；实板暂停构造任务的取消探针见
`VDC-PROGRESS-20260914-016`；交接分阶段计时及两轮自主切换原件见
`VDC-PROGRESS-20260914-017`；有界邮箱合批及当前四板交接对照见
`VDC-PROGRESS-20260914-018`；完整校准 CRC 的 SRAM 实现、初始化失败恢复及普通/
自主相位分离复核见 `VDC-PROGRESS-20260914-019`；就绪阶段合批、构造取消与重臂
交接对照见 `VDC-PROGRESS-20260914-020`；原生记录逐槽离线检查及从板观察缺口见
`VDC-PROGRESS-20260914-021`；RX 同相位补充捕获的失败与回退见
`VDC-PROGRESS-20260914-022`；RX/TX latch 直接初始化及四板对照见
`VDC-PROGRESS-20260914-023`；RX 消费能力和缓冲余量的离线审计见
`VDC-PROGRESS-20260914-024`；TDMA review 06 的证据口径复核见
`VDC-PROGRESS-20260914-025`；RX 站台等待、初次观察及 drop 原因的四板测量见
`VDC-PROGRESS-20260914-026`；最新帧策略的 latch 身份阻断及可执行反例见
`VDC-PROGRESS-20260914-027`；从板连续事件观察的 PIO 原型与边界反例见
`VDC-PROGRESS-20260914-028`；共享采样区 RAM 验收见 `VDC-PROGRESS-20260915-001`，
成对事件计数及有界联合读取原型见 `VDC-PROGRESS-20260915-002`；生产观察器接入与
原生记录复核见 `VDC-PROGRESS-20260915-003`；自主模式观察 FIFO 容量与服务路径
定位见 `VDC-PROGRESS-20260915-004`；最终启用前 CS 准入复验见
`VDC-PROGRESS-20260915-005`；有界原始事件保留基础见
`VDC-PROGRESS-20260915-006`；准入尝试诊断与失败采集自动留证见
`VDC-PROGRESS-20260915-007`；DMA 私有捕获凭据到 RX station 的贯通及 RAM 布局
收敛见 `VDC-PROGRESS-20260915-008`；READY 有界候选查询与原生记录见
`VDC-PROGRESS-20260915-009`，其自主准入拒绝及部分窗口原件保留；foundation 专用
读取与随后获准的完整原生窗口见 `VDC-PROGRESS-20260915-010`；首次 observer 启用
的 DMA 坐标括号及 capture 失效退休见 `VDC-PROGRESS-20260915-011`；后继首帧坐标
证明与启动路线的只读收敛见 `VDC-PROGRESS-20260915-012`；首次 CS 保护及首帧采集
缺口见 `VDC-PROGRESS-20260915-013`；板端有限发帧与最早原始前缀保留见
`VDC-PROGRESS-20260915-014`。当前入口仍为
`VDC-TIME-002`，补齐硬件配置、交接时延
及调度失败证据后，才开放全窗计时验收。按 `VDC-TIME-001` 至 `VDC-TIME-004` 补齐
`VDC-TDMA-001` / `VDC-EVID-001` 的自主时间戳输入，再推进 `VDC-SCHED-001`、
`VDC-ROLE-001`；全表 WCET 和正式锁相仍未闭合，
命令接线须等待契约独立审核。`VDC-TDMA-001`、
`VDC-CAL-001` 和 `VDC-EVID-001` 继续提供正式 evidence；`VDC-SERVO-001/002` 在
正式 evidence 未闭环前的 host/replay 或板端诊断不得用于发布板端目标锁。

本轮按用户要求完成未提交改动对账，先隔离 resident 命令发送并通过普通四板 quick
P3，详见 `VDC-PROGRESS-20260915-026`；后续非法目标位移修复和完整 resident 编译
隔离分别见 `VDC-PROGRESS-20260915-027/028`。每项功能修改后立即 P3；基础流程的
复核不改变上述时间输入、命令应用和正式锁相的未完成状态。隔离验收中重复出现的
粗校准配置拒绝及准备流程有界恢复见 `VDC-PROGRESS-20260915-029`；命令区任务写者
与在线 reset/读交错修复见 `VDC-PROGRESS-20260915-030`；本地角色回切时已接收
命令的退休见 `VDC-PROGRESS-20260915-031`；FIFO 发布入站取消见
`VDC-PROGRESS-20260915-032`；FIFO 借用与 STOP 回收互斥见
`VDC-PROGRESS-20260915-033`；本地 ring 配置绑定与命令取消见
`VDC-PROGRESS-20260915-034`；固定校准配置复测与示波器诊断链路见
`VDC-PROGRESS-20260915-035`；普通循环边界和事件观察器分段计时见
`VDC-PROGRESS-20260915-036`；transport CRC 等价优化及同槽对照见
`VDC-PROGRESS-20260915-037`；事件 lift 快路径及收益边界见
`VDC-PROGRESS-20260915-038`；从板热点 SRAM 放置与完整对照见
`VDC-PROGRESS-20260915-039`。

### VDC-PROGRESS-20260915-039 — 从板事件热点 SRAM 放置，普通运行尾延迟下降

- TODO task ID：`VDC-SCHED-001`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  038 已提交为 `d238d35` / `54c6fff`。上轮算术简化未证明整体提速，本轮仅将
  `tdma_event_observer_feed()`、`tdma_rx_start_cut_monitor()` 和
  `tdma_rx_dma_counter_observe()` 的函数体放 SRAM；算术、身份/几何复验、
  时间括号、DMB、局部 counter 副本及已退休后的故障累计全部保留。现有 Flash
  helper 仍会调用，不声明关停 XIP 后可运行，也不修改 OTA、预算或命令开关。
- 资源快照，非容量契约：A/B 三段共 4120 B（函数指令共 4118 B），加链接开销后
  data 末端增加 4136 B，利用对齐间隙使 BSS 末端总成本为 4096 B；最终 BSS
  末端 `0x2007c6cc`、C heap 链接跨度 14644 B，不能当实时空闲堆。FreeRTOS
  `configTOTAL_HEAP_SIZE`、两核 scratch 栈边界不变，四板前后 RTOS 读回的
  free/minimum 均为 20344 B。相同函数体并不保证所有外部调用成本相同。
- 软件：85 项生产 observer/start-cut/RX/profile 回归通过，双槽 release 与
  Flash 链接门禁通过。首轮 82 通过、1 个夹具编译失败；恢复原函数声明仍复现
  缺 `tdma_service_timing.h`，补齐该生产头后通过，原失败保留。构建冻结时的
  `d6400ca3…` 指纹早于此纯 host 夹具修复；最终 P3 重新构建并绑定源码 SHA
  `2fdc1c7d9ff13c8c7a30e8a7fc89cf657a7814adf27b3d32fa89a14c2c4addfd`，
  包 SHA 为 `3aba55b99d3be1553c82fc16e9c7d963b91b0f50a8d4e8e3b21d5ac99397239d`。
- 验收失败与恢复分别留证：`event-hot-ram-r1/p3/` 耗时 183.282 s，四板 OTA/
  短帧流程完成，但 SCK 候选不足、无重臂余量合格组合，`strict_gates_passed=false`。
  使用既有 `resume` 和同一包/成功 OTA 记录重做复位、校准、闭环，
  `p3-resume-r2/` 耗时 77.334 s，`passed/flow_completed/strict_gates_passed=true`、
  诊断失败为空；不改门限，不覆盖首轮失败，也不把 77 s 称为完整构建/部署验收。
  为控制槽位，验收前另用冻结的 038 包重装一次，四板通过，额外耗时 106.922 s。
- 性能对照仍固定旧 row35/matrix、板序、完整槽元组和 START 后 RESET 协议；
  038 两轮与新 r1/r3 比较，以下为稳定采样段快照，保留不等长窗口实际分母：

  | 板卡 | 放置前 TDMA overrun/run | SRAM 放置后 TDMA overrun/run |
  |---|---:|---:|
  | NO1 | 0/1669 | 0/1501 |
  | NO2 | 16/1664 | 0/1500 |
  | NO3 | 19/1664 | 0/1498 |
  | NO4 | 24/1666 | 0/1501 |

- 三从板合计由 59/4994 变为 0/4499，支持保留本切片。新 r3 三从板普通 RUN
  完整峰值为 807.664/804.848/796.992 µs，序号均非 RESET 首相位；这些峰值内
  FEED 为 11.296/33.656/17.040 µs，START_CUT 为 4.720/5.268/4.672 µs，
  子段不是独立最大值。新 r1 NO4 的 879.880 µs 为 STOP 过渡，r3 NO1 的
  843.844 µs 为 RESET 首相位，均原样保留，不提升为稳定运行比较。
- 新 r2 的 NO2 STOP 后 OTHER 读回为 `UNAVAILABLE`，夹具误报为 reset mismatch；
  RESET/PEAK/RUN 实际代际一致。原始失败 summary 不改为通过，四份完整 native
  经生产离线判据复核，稳定段无超限，但不补造缺失 profile。r3 仅对 STOP 后
  明确 `UNAVAILABLE` 的只读导出至多重试三次，保存全部响应，完整采集通过。
  `event-hot-ram-after-r1/r2/r3/` 均保留，完整 paired 对照使用 r1/r3。
- 原件复核覆盖两次验收的八份、完整 paired 比较的十六份及失败导出的四份
  native binary，CRC/身份/epoch/完整记录与归档 JSON 一致；稳定时间线逐条
  对应板端样本，三从板 ACTIVE、无故障且 joined/published/elapsed 连续递增。
  最终四板 STOP/config ACK、SELFtest 全零、recorder SAVED；SAVE 不表示 SD
  与 SRAM 字节比对。证据在 `out/HardwareAcceptance/20260915/event-hot-ram-r1/`。
- 下一 gate 回到 `VDC-TIME-002`：先在 TDMA owner 内完成 STOP 清字段前捕获
  几何、全部退休后发布及新 ARM 显式选择，再单独处理自主准备与首次 DMA 发车
  的从板就绪边界，补首物理事件/DMA 帧/完整身份关联。只读路线见
  `vdc-next-time-r1/assessment.json`；未知拓扑不扩成当前四板阻断。
  本轮仅证明普通模式短窗收益，仍为 850 µs TDMA / 1.5 ms 整表预算快照，不能
  关闭自主更新 WCET、500 µs、`VDC-TIME-003/004`、命令应用或锁相门禁。后续
  四路示波器继续以 NO1/CH1 上升沿触发，本轮没有新增波形或正式时间戳资格。

### VDC-PROGRESS-20260915-038 — 事件 lift 等价快路径，尾延迟仍待闭合

- TODO task ID：`VDC-SCHED-001`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  037 已提交为 `aa6c988` / `cd51d8f`。本切片仅在
  `tdma_event_observer_lift()` 参数校验及 `upper < base` 拒绝之后，增加
  `upper - base < TDMA_EVENT_WRAP_PERIOD` 的唯一候选快路径：`lower <= base`
  时输出 `base`，否则保留 NO_CANDIDATE。这里 `q=0` 表示无需额外完整周期，
  原始计数器跨零的补偿仍在 `base` 中；等号边界和多周期窗口继续走通用路径。
  错误优先级、拒绝时不写输出、溢出检查、整批失败退休及诊断身份均保持。
- 软件与资源快照，非容量契约：生产 C 生命周期和 bigint 差分等 36 项回归
  通过，新增精确错误码、边界两侧、非法参数与输出 sentinel 检查；双槽 release
  构建和 Flash 链接门禁通过。目标反汇编中 RX/TX 的 lift 命中 `q=0` 条件时
  各绕过三次软件 64 位除法，运行命中率未记录；内联后的 FEED 从 3436 B
  降为 3296 B，入口地址不变，主 BSS、
  SCRATCH_Y 与栈底链接末端不变。不把指令变化换算为实测微秒收益。
- 当前源码四板 P3 为 `tdma-event-lift-r1/p3/`，耗时快照 187.310 s；
  `passed/flow_completed/strict_gates_passed=true`，诊断失败为空，四板普通
  process-image/FIFO 闭环通过。源码 SHA 为
  `e59738382c51178a5f0056d049d0bcfd23b88845b348491e09f563bda201c8cd`，
  包 SHA 为 `4b8245caf28e2a99bbc4f0dfda36df5c0e64d3ed9870a87cabea67e4506b0882`。
  为控制槽位，先用既有 OTA 重装冻结的 CRC 固件一次，四板成功，额外耗时
  113.532 s，不计入 P3。OTA 实现、配置及命令运输开关未改。
- 同槽比较沿用 037 两轮 CRC 基线；新两轮约 16 s，使用同一 row35/matrix、
  板序和 NO1/NO2/NO3 槽 2、NO4 槽 1，均在 START 后 RESET、SRAM 记录，
  STOP 后导出并 SAVE。下表为稳定采样段快照；旧 r1 窗口较短，保留实际分母：

  | 板卡 | CRC 基线合计 TDMA overrun/run | lift 快路径合计 TDMA overrun/run |
  |---|---:|---:|
  | NO1 | 0/1502 | 0/1669 |
  | NO2 | 18/1503 | 16/1664 |
  | NO3 | 23/1504 | 19/1664 |
  | NO4 | 18/1505 | 24/1666 |

- **未证明从板整体提速或尾延迟问题解决**：NO4 比例未改善，完整峰值内的
  FEED 子段仍有明显波动。新 r1 三从板普通 RUN 峰值为
  914.572/905.044/903.912 µs；新 r2 NO2 的 964.792 µs 属于 STOP 过渡
  `256→0`，不与普通 RUN 相比，NO3/NO4 普通 RUN 峰值为
  915.020/905.624 µs。这些是各轮完整相位峰值，FEED 子段不是独立 FEED 最大值。
  保留快路径的依据是等价算术简化、代码缩小、无新增持久 RAM 和功能验收通过；
  不能据此关闭 `VDC-SCHED-001` 或宣称稳定超限率已下降。
- 原件复核：P3 的四份及前后比较的十六份 native binary 均通过 CRC/身份/
  epoch/JSON 比对，记录完整且 missed/reason 为零；稳定时间线逐条对应 native
  样本。新两轮三从板观察器均 ACTIVE、reason/fault 为零，joined 与 published
  同步递增，RX/TX elapsed 递增。最终四板 STOP、config ACK、SELFtest 全零且
  recorder SAVED；SAVE 只证明状态/epoch/长度，不声称 SD 与 SRAM 字节比对。
  证据根目录 `out/HardwareAcceptance/20260915/tdma-event-lift-r1/`；两轮比较
  原件分别在 `tdma-event-lift-after-r1/`、`tdma-event-lift-after-r2/`。
- 下一 gate：分解 `event_start_cut` 的身份/几何复验、MMIO、时间读取和发布开销，
  结合 Flash 放置评估收益；保留已退休后继续累计故障原因及 observer guard。
  当前仍为 850 µs TDMA / 1.5 ms 整表预算快照，本切片未修改预算，不表示达到
  500 µs 或整表 WCET。随后按既有依赖补共同 session、命令交接/应用与实际锁相。
  示波器继续用 NO1/CH1 上升沿触发、四路 DPLL 输出及 1× 探头；本轮未新增
  波形或锁相结论，未变更契约登记或 HAOFV owner 边界。

### VDC-PROGRESS-20260915-037 — transport CRC 等价优化与同槽四板对照

- TODO task ID：`VDC-SCHED-001`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  036 已提交为 `385a9ff` / `cb3cff5`，本轮只优化
  `tdma_transport_crc32_update()`：用反射多项式的半字节表替换逐位循环，保持
  任意初值、增量结果、取反约定，以及全部 identity/transport 校验调用、字节
  范围和拒绝顺序。不可变表与单一内核放 SRAM，不借用 DMA 或增加跨核可变状态。
  命令运输、PIO、调度预算、配置和 OTA 实现均未改变。
- 软件：实际生产内核对全部单字节、初值各位、空输入、非对齐、不同长度及分块
  连续计算进行多项式/zlib 差分；29 项 Python 回归通过，完整 transport 与
  adapter C suite 通过。release 双槽构建和 Flash 链接检查通过。
- 资源快照，非容量契约：初次 `noinline` 构建仍生成常量长度克隆，内核占
  356 B，BSS 对齐导致主 RAM 末端增加 4096 B；该构建未部署，日志保留。
  增加 `noclone` 后最终内核为 56 B、SCRATCH_Y 常量表为 64 B；内核利用已有
  BSS 对齐间隙，主 RAM 末端与 036 相同。SCRATCH_Y 到启动栈底剩 536 B，
  BSS 末端到 HeapLimit 的范围为 18740 B，不等同 RTOS 实时空闲堆。
- 当前源码四板 P3：`tdma-crc-r1/p3/` 耗时 177.459 s，
  `passed/flow_completed/strict_gates_passed=true`、诊断失败为空；四板 OTA、
  校准与普通 process-image 闭环通过。源码 SHA 为
  `91c29728498960a5841b3fa9c07b24bd25f8f36e85c49a613ae5fdb3db53b949`，
  包 SHA 为 `7901b4020c2ab08e7963ff8f9bc2b3d6395551b8f0dca35fb0f7e99cd79b936c`。
  双槽 ELF/map/反汇编及包已固化，不能按复用的 build ID 区分代码。
- 对照控制：保留 036 原始 profile，并在改代码前补测一次；为了让新 P3 部署后
  回到同一应用槽，先用既有 OTA 重装一次冻结的原固件到另一槽，四板通过，耗时
  104.031 s（本轮比较准备，不计入新 P3 耗时）。新源码 P3 使用本轮校准矩阵，
  其后的比较采集显式恢复固定的旧 matrix、row35 和板序；前后槽位读回均为
  NO1/NO2/NO3 槽 2、NO4 槽 1。两轮新采集仍采用 START 后 RESET、SRAM 记录、
  全部 STOP 后导出和 SAVE，每轮约 16 s，四板闭环均通过。
- 下表为同配置、同槽位下主板短窗测量快照，非 WCET 上界。完整峰值均来自
  普通 CYCLE_BOUNDARY 到 RUNNING，选中序号非 RESET 首相位：

  本次板端快照中 TDMA 预算为 850 µs、整表周期为 1.5 ms，取自静态调度快照；
  本切片未修改 `app_realtime_profile.c`，不表示已经达到先前的 500 µs 目标。

  | 轮次 | 完整 OTHER 峰值 µs | 稳定采样段 TDMA overrun/run | deadline 增量 |
  |---|---:|---:|---:|
  | 036 原固件 | 1014.576 | 139/832 | 101 |
  | 本轮原固件补测 | 1014.480 | 100/832 | 92 |
  | CRC 优化 r1 | 775.940 | 0/667 | 0 |
  | CRC 优化 r2 | 787.224 | 0/835 | 0 |

- 原两轮主板合计 239/1664 次超限，新两轮为 0/1502，支持保留本切片。
  新 r1 更早通过 startup，稳定采样段短于其余轮次，因此保留实际分母，未当作
  相同长度采样；整个 profile 和稳定采样段也是不同窗口。所选完整峰值中的
  TX 子阶段不构成所有 TX 尝试的最大值，不能据其差值认定 CRC 独占多少时间。
- 从板仍有超限：新两轮稳定段合计 NO2 为 18/1503、NO3 为 23/1504、NO4 为
  18/1505。新 r1 的 NO2/NO3 OTHER 峰值属于 STOP 过渡，分别保留为
  1007.176/961.396 µs，不与旧普通运行峰值拼接比较。未关闭身份检查或观察器，
  未宣称整张静态表、长期 WCET、所有从板预算或原先较短预算达成。
- 证据：`out/HardwareAcceptance/20260915/tdma-crc-r1/` 保存源码范围、构建发现、
  P3、固化包、profile 分解和独立审核；`tdma-crc-before-r1/`、
  `tdma-crc-after-r1/`、`tdma-crc-after-r2/` 保存固定配置原件，与 036 的
  `tdma-detail-profile-r1/` 对照。native binary 的 CRC/身份/完整记录与归档
  JSON 一致，各轮 SAVE 确认 SAVED；最终四板 STOP/config ACK、selftest 全零，
  无新电气关闭采集或 SD 字节对比。未新增契约登记或提升 DPLL lock。
- 下一 gate：主板短窗收益已复现，继续 `VDC-SCHED-001` 的从板事件 FEED 热点。
  优先评估无额外持久 RAM 的计数回绕 lift 等价快路径，保持多回绕歧义、边界、
  错误原因及整批故障退休；独立软件/P3 后再比较。只读模型和 SRAM 放置评估在
  `tdma-opt-candidates-r1/event-placement.json`，不代表候选已实现或有硬件收益。
  完成时序与时间输入后继续共同 session、命令接收/应用和实际四板锁相；示波器
  沿用 NO1/CH1 触发配置，当前未用本轮 TDMA 数据替代输出锁相证明。

### VDC-PROGRESS-20260915-036 — 普通发帧与从板事件服务耗时归因

- TODO task ID：`VDC-SCHED-001`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  本切片只扩展 `TDMA_SERVICE_TIMING_VERSION` 的计时记录和离线解析，不改变命令
  启用、收发算法、PIO、预算或 OTA。事件观察继续由 Core1 物理 owner 有界服务；
  Core0 负责原生记录，SCPI 仅触发，全部 STOP 后导出及顺序 SAVE。
- 先用 035 的当前固件和固定校准配置补完整 profile：r1 在 START 前 RESET，主板
  OTHER 峰值包含 ARMED 到 RUNNING；r2 在四板 START 后等待再 RESET，仍测得
  CYCLE_BOUNDARY 到 RUNNING 的高耗时。两轮四板闭环通过，说明问题不只出现在
  首次启动。RESET 由 Core1 在下一相位消费；两轮选中的峰值均非首个 RESET 相位，
  各自 reset generation 一致、invalid 为零，未把空的自主 RUN 组当普通运行数据。
- 新探针：事件服务分 ENTRY、HARVEST、CONVERT、FEED、FINAL_CHECK、START_CUT、
  RETAIN、PUBLISH；FEED 包含读取后的故障复验和诊断字段复制，FINAL_CHECK 包含
  最终故障触发的二次 feed。ENTRY 包含等待状态下的启动尝试，START_CUT 只计随后
  的 monitor；RETAIN 包含退休和旧 `service_max_us` 更新，最后快照复制另计 PUBLISH。
  普通 reference 发帧四条入口累计到 REFERENCE_TX，物理 callback 计为其子段
  REFERENCE_SUBMIT。父子不重复相加，完整峰值不跨 sequence 拼接。
- 软件与资源：相关测试共 30 项通过，包含真实 owner 路径、永久故障/最终复验、
  start-cut 硬件模型、普通发帧及异步 pending 的计时归属、旧版本解析。
  首次测试因证据目录父路径缺失失败；随后发现旧 owner 测试夹具缺少已有
  start-cut monitor 替身，补齐调用检查后通过；失败日志均保留。
  release 双应用槽构建及 Flash 链接门禁通过。目标 map 中计时静态区增加
  320 B，SCPI 局部 snapshot 增加 256 B（本轮构建快照，非 RAM 容量契约）。
- 当前源码四板 P3：`tdma-detail-r1/p3/` 耗时 203.115 s，
  `passed/flow_completed/strict_gates_passed=true`，诊断失败为空；四板 OTA 和
  普通 process-image 闭环通过。源码 SHA 为
  `b248a154ebc5286fea255b3c8e2a489cf10a948c320f7616502797e6995b07ee`，
  包 SHA 为 `9fdcba5c400de65294152b7e9f3ea0c7b18b7051e2b543a8675aa787f47f4764`。
  固化双槽 ELF/map/反汇编和包；复用的 build ID 不作为版本区分依据。
- 新分段复测仍用固定的旧校准配置、START 后 RESET，四板约 16.2 s 完成闭环。
  下表为同条 OTHER 峰值的测量快照，非 WCET 上界或产品事实源，单位 µs：

  | 板 | 完整相位 | 物理服务 | 时间换算 | 事件 FEED | START_CUT |
  |---|---:|---:|---:|---:|---:|
  | NO1 | 1014.576 | 6.456 | 0 | 0 | 0 |
  | NO2 | 926.684 | 237.116 | 15.348 | 57.104 | 52.140 |
  | NO3 | 967.636 | 373.192 | 16.536 | 127.568 | 54.108 |
  | NO4 | 978.928 | 381.096 | 26.964 | 140.212 | 59.492 |

- 主板该相位为 CYCLE_BOUNDARY 到 RUNNING，REFERENCE_TX 占 651.768 µs，其中
  SUBMIT 占 105.556 µs，其余 546.212 µs 包含构造、校验和提交后的记账，尚不能
  全部称为 CRC 或复制。三从原生窗口的首条样本尚未启用观察器，随后样本 ACTIVE、
  published 持续增长且无 fault；换算只占事件服务一部分，不能按四次除法推算
  回收数百微秒。
  算术等价快路径的只读分析可以作为候选，尚未实施或声称硬件收益。
- 证据与复核：均位于 `out/HardwareAcceptance/20260915/` 下，旧固件 profile 在
  `tdma-phase-profile-r1/`、`tdma-phase-profile-r2/`，新固件在
  `tdma-detail-profile-r1/`，构建/P3/分析和独立审查在 `tdma-detail-r1/`。
  主控重解 P3 四份及 profile 十二份 native binary，CRC、身份、完整记录及
  归档 JSON 一致；profile 原始响应重新解析、嵌套余量非负。各轮 SAVE 确认 SAVED，
  不扩称为 SD 字节对比。最终四板 STOP，诊断输出关闭。
- 限制与下一 gate：profile 在 STOP 后仍更新，覆盖 RUN、STOP/idle 和导出服务，
  不等同隔离的稳定窗口 WCET；新增探针、快照复制及 OTA 槽位改变影响时间，不能
  将旧/新峰值差当算法收益。现有 P3 不约束 TDMA phase WCET，全表时序仍未闭合。
  `VDC-SCHED-001` 下一步优先定位普通发帧准备/校验/记账及从板 FEED/START_CUT，
  按收益选择一个等价或有界优化并独立 P3，保留身份与故障复验。共同 session、
  可信时间映射、命令实际应用和正式锁相继续未完成；示波器按 035 的 NO1/CH1
  触发配置用于后续同窗输出验证，本轮未新增波形或提升 lock 结论。

### VDC-PROGRESS-20260915-035 — 固定配置快速复测与四通道触发采集

- TODO task ID：`VDC-SCHED-001`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  前一切片已分离提交为 `e3d8737` / `4164b11`。本轮使用其已部署固件和既有
  工具进行测量，没有增加生产功能、改变预算或重新宣称命令启用态通过。
- 前后归因复核：033/034 两轮不仅源码不同，校准偏移行、部分有向链路延迟、
  实际应用槽位也不同；相同 build ID 不能替代源码/包 SHA。原 166/343 是含启动
  的记录 baseline 至末样本计数，稳定窗分别为 71/833 与 148/833 次 TDMA 超限。
  因此不根据这两轮直接归因或回退本地 STOP 取消修复。
- 快速流程：固定 034 的 matrix、偏移行和板序，直接运行既有
  `trn03_closed_loop.py`，SCPI 只触发，板端 SRAM 定时记录，全部 STOP 后导出。
  前两次复测因上轮记录仍 FROZEN 而拒绝 ARM，失败原件保留；按既有
  `diagnostics_tdma_record_save()` 保存至 SD、确认 SAVED 后再 ARM，恢复成功，
  未放松生命周期保护。随后每轮导出完再 SAVE，使下一轮可重复执行。
- 测量快照，非事实源：r3/r4/r5 分别耗时 15.125/14.984/15.031 s，闭环和既有
  实时 gate 均通过。各板每轮 14 个样本，记录器 missed/reason 均零；独立重解
  十二份 binary，终结 CRC、身份及 JSON 副本一致，STOP/config ACK 和 SAVED
  读回通过。本轮未做 SD 文件逐字节回读，不能把 SAVED 扩称为 SD/SRAM 一致。
  主板稳定窗 TDMA overrun/run 分别为 117/834、153/832、104/833，deadline
  增量为 85/112/76，超限率范围 12.48% 至 18.39%，合计 374/2499。
  034 的稳定窗比例落在本次范围内，只能说明当前固件仍有时序压力；稀疏
  last-runtime 不是逐周期分布，现有 gate 仍未覆盖 TDMA WCET。
  采样配置固定不等于完全相同启动历史，尚未进行受控旧/新固件 A/B。
- 外部观测：用户确认 RIGOL HDO4404 的 CH1 至 CH4 对应 NO1 至 NO4 DPLL 输出，
  探头均为 1×，以 NO1/CH1 正沿为触发源。本轮实际确认 SING 后 WAIT、armed
  时 SING/CH1 读回、随后自动 STOP，在同次 STOP 中导出四路 RAW BYTE。
  每路完整 block、preamble、请求/读回范围、SHA 和触发状态序列均保留。
  四路共同时间轴为 20 ns 采样、每路一百万点，窗口约 20 ms；上升沿数量为
  3/3/4/3，均有真实脉冲（本轮诊断快照，非精度或稳定性事实源）。
- 采集失败与修正记录：r1 在 SING 后立即读到旧 STOP，后续复验发现 WAIT，
  未导出；r2 观察到 WAIT 但等待内无触发，也未导出。r3 完成新触发后，仪器
  sweep 自动读回 NORM，保守检查拒绝；原件保留，随后只补读其冻结帧，未冒称
  新采集。该帧只有 NO1 输出，诊断启动顺序调整为从板先于 NO1 后，r4 四路均
  有脉冲。r3 输出关闭的短 ACK 等待超时，后续原始查询确认四板已关闭；r4
  控制响应及最终 selftest 全零、TDMA STOP/config ACK 均闭合。
- 范围：本次输出是 TDMA STOP 下既有 phase-only 诊断 persona，不是正常
  resident 命令驱动的产品输出。其 DCO 过旧时可回退本地 monotonic deadline，
  每次 service 只排一个脉冲，不能按配置周期补出未采到的脉冲、强行按同周期
  配对或宣称连续锁相。最终关闭是软件读回，未补测关闭后的电气波形。既有
  `scope_dpll_capture.py` 的默认滚动采集、缩放和原始数据保留仍需独立工具
  切片修正；本轮 `out/` 中诊断采集不代表该工具已验收。
- 证据：`out/HardwareAcceptance/20260915/command-stop-timing-baseline-r1/`
  至 `command-stop-timing-baseline-r5/`、`command-stop-timing-save/` 保存原生记录、
  失败、SAVE 读回和 `timing-review.json`；`scope-trigger-r1/` 至
  `scope-trigger-r4/` 保存各次原件、板控和波形。示波器后续用于相对 NO1 的
  相位、漂移、缺脉冲和恢复观测，需与同窗板端命令应用、角色、会话和可信时间
  映射对账；探头/通道延迟及最终门限仍待验证，不能单靠内部 LOCKED 或单帧判断。
- 下一 gate：`VDC-SCHED-001` 先补同 reset generation 的完整 TDMA phase profile，
  区分 outer、adapter、RX、overlay 和 owner 成本，嵌套阶段不重复相加；按校准
  配置与应用槽位受控后再做代码 A/B。命令会话、定时应用及正式输出锁相任务
  继续保持未完成，不因测量链路可用而提升状态。

### VDC-PROGRESS-20260915-034 — 本地 ring 配置绑定与 STOP/ARM 命令取消

- TODO task ID：`VDC-CMD-002`、`VDC-ROLE-005`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  前一切片已分离提交为 `66919c2` / `91adaaa`，随后开始本切片；本切片代码及 P3
  凭证已提交为 `e3d8737`，文档另行提交。
- 反例：普通 TDMA STOP/ARM 更新 ring config sequence，却不改变 VDC epoch/run
  或本地角色代际。冻结旧源码的真实 service STOP/ARM、RefMem receiver/getter、
  manager 函数与完整 Domain 执行后，STOP 前已收且未来未到的命令在重新 ARM 后
  写入 DCO；Core0 看见或漏看中间 STOP 均复现。无 STOP 正测与已应用命令不重放
  对照通过。adapter 与硬件时钟为 host stub，不能作为已发生的实板故障归因。
  旧紧凑 clock getter 还在 config/result 之间缺少末尾配置复验；真实 runtime
  configure/service 插入该窗口后，getter 返回旧 config，现场 config 已变化。
- 修复：RefMem 把 local role 与 `consumer_ring_config_seq` 一起放在命令 guard
  内绑定；变化时作废 retained 值，保留同 wire 会话的来源排序水位。Core0 只在
  enabled/started、config/applied ACK 及 schedule/slot 一致时绑定非零配置；
  关闭态绑定零并取消待准备 resident 记录和组装、推进 FIFO admission epoch。
  快照暂忙不取消原值，下次 RefMem 服务再试；普通 mailbox 仍继续。
  `refmem_sync_vdc_receive_admitted_frame()` 覆盖 resident、node-load COMMAND
  和遗留 follower completed 入口，关闭期间不能通过兼容路径写入大序号污染水位。
  Core1 先读取当前配置，再 guarded copy，并在来源/目标处理及消费任何序号前
  复验 config/applied；STOP/config 在复制中到来时退出，不调用应用端或消耗序号。
  紧凑 clock getter 在同一次有界尝试内复验 config/result 两个 guard，并携带 ACK；
  时间映射与 ring observer 拒绝未 ACK 配置。Core1 没有新增锁或等待。
- 软件与构建：主控相关 Python 合跑 84 passed，文档检查器回归 18 passed
  （本轮快照，非事实源）；Domain、RefMem、ring runtime 和 time mapping C
  runners 均通过。新增 STOP 场景执行真实完整 Domain，包含漏看 STOP、未 ACK、
  复制中途换配置/绑定、快照忙恢复、零代际及兼容入口大序号拒绝；关闭期普通
  DELTA 仍更新，重新 ARM 后新命令可应用。clock getter 负测执行真实
  configure/service 插入原子读取窗口并验证重试有界。默认双应用/Boot 构建及
  命令开启分支 compile-only 通过；独立只读审查无阻断项，逐文件哈希核对一致。
- 资源快照，非事实源：ARM receiver/retained command/compact clock snapshot
  分别保持 456 B/72 B/152 B，新字段使用既有对齐空间；默认双应用 BSS 起点各移
  后 8 B，BSS 终点、HeapLimit/StackLimit 均未变化，RAM 高水位未增加。
- 硬件验收：源码指纹
  `a7ddeb069eecf6968ecb4c89ce80e22a2768037805f36961b9de7b014f62626d`
  的当前四板 P3 按既有 quick diagnostic profile 通过，凭证
  `strict_gates_passed=true`，闭环/现有实时门禁通过、diagnostic failures 为空。
  耗时 190.348 s，各板原生记录 14 条、记录器 missed=0（本轮快照，非事实源），身份、
  完整性和 20 项产物哈希核对通过；导出前全部 STOP，config/applied ACK 一致，
  FIFO_RESET 均 OK 且回收读回正常。包与双应用 map 已归档。
- 时序复核范围：`schedule-comparison.json` 保留相邻两轮原生采样窗口的逐板对照。
  本轮 VDC/DPLL 相位的 overrun/deadline 增量均为零，但 TDMA 相位仍有超预算：
  主板本轮 overrun 增量为 343，上轮为 166；采样到的 last runtime 最大值分别为
  897.536 us 与 834.508 us（窗口快照，非 WCET 事实源）。当前
  `trn03_closed_loop.py::validate_dpll_schedule()` 不以 TDMA phase WCET 作门禁，
  因此 P3 通过不关闭静态调度预算；本轮未做同工况 A/B，不能将该差异归因到此次
  配置读取。该时序风险继续由 `VDC-SCHED-001` 跟踪，不修改验收工具或预算掩盖它。
- 证据：`out/HardwareAcceptance/20260915/command-stop-r1/`；`before-stop/`
  保存旧源码、真实完整 Domain 反例及正对照，`before-clock/` 保存旧快照交错反例。
  `after-stop-r3-result.json`、`software-verification.json`、`source-review.json`、
  `source/`、`build-result.json` 和 `target-layout.json` 保存测试、冻结源码与资源。
  `p3/acceptance.json`、`p3/diagnostic.json`、`p3/tdma-process-image/`、
  `review-final.json` 保存硬件原件及主控核验。
- 范围与下一 gate：本切片只闭合本地 ring 生命周期的命令准入，不建立四板共同
  session。config sequence 为零时只关闭命令准入，后续非零 ARM 可恢复，普通
  TDMA 回绕行为不变。最终 Core1 检查之后到来的 STOP 由后继 owner 边界完成，
  不承诺 SCPI 请求瞬间撤销。已进入 TX image/FIFO/PIO/DMA 的旧片段、上游旧输入
  和完整旧记录重发仍需 wire 有效期与共同 session 规则；命令默认禁用，实际定时
  应用和输出锁相未验收，`VDC-CMD-002` 仍 PENDING。继续增加功能前，先对本轮
  TDMA 超预算增多做同工况对照，保留校准/板卡/负载配置及原始窗口，避免把随机
  工况差异或 gate 未覆盖项当作已定位的回归。
- 实测设备准备：用户接入
  `USB0::0x1AB1::0x0610::HDO4A244301137::INSTR`，本轮只读 VISA 查询确认
  RIGOL HDO4404、序列号匹配，原始设置保存在
  `out/HardwareAcceptance/20260915/scope-connect-r1/instrument-state.json`。
  用户确认 CH1 至 CH4 分别连接 NO1 至 NO4 的 DPLL 输出，实物探头均为 1×；
  该连接记录不包含实际输出相位测量。

### VDC-PROGRESS-20260915-033 — Core0 FIFO 借用保护与 STOP 回收互斥

- TODO task ID：`VDC-CMD-002`、`VDC-ROLE-005`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  032 已分离提交为 `3a42dcc` / `34aa9fc`，随后开始本切片；本切片代码与 P3
  凭证已提交为 `66919c2`，文档另行提交。
- 反例：旧 service 仅核 enabled/adapter/engine 状态后直接清 FIFO；已 acquire 的
  RX view 仍持旧 slot 指针，后续 Core1 发布可覆盖它，旧 release 又能释放新 slot。
  真实旧 service/FIFO harness 输出 `reset=1, old_pointer_overwritten=1,
  old_release=1, acquired_new=0, drops=1` 后断言失败（本轮快照，非事实源）。
  这是 API 允许交错的反例；当前 RefMem 任务优先级高于 SCPI，不能把低优先级
  SCPI 抢占 RefMem 当作已测实板故障根因。SCPI raw TX/RX 与 RefMem 共用 FIFO
  也需要任务间互斥，因此单纯把 reset 移至 RefMem 末尾不足以关闭所有借用窗口。
- 修复：`core0_guard` 串行化 Core0 FIFO publish/reset，成功 acquire 持有按 slot
  编码的 guard，release 先归还 slot 再解锁，避免归还因第二次 try-lock BUSY 丢失。
  Core1 不读、不取新 guard；可在旧 release 解锁前重新发布已 FREE 的 slot，旧
  release 此后不再写该 slot。错误 slot、未借用或重复 release 被拒绝。
  API 现在要求同一 FIFO 同时最多一个 Core0 RX view；两个实际接收 caller 均逐个
  acquire/parse/release，忙时保留数据等下次服务，不清理别人的 view。
  `tdma_service_reset_flight_fifo_checked()` 持 `ring_control_guard` 核物理 STOP、
  config/applied ACK、engine inactive 与停止态更新空闲，防 ARM/config 与回收交错。
  SCPI 仅对 BUSY 按 `SCPI_TDMA_FIFO_RESET_WAIT_LOOPS` 有界让出后重试，真正完成
  才返回既有 OK；NOT_STOPPED/INVALID 立即拒绝。未改 OTA 或命令使能。
- 软件与构建：主控 Python 合跑 64 passed（本轮快照，非事实源）；FIFO、service
  scheduler、adapter C runners 均通过。测试执行生产 FIFO memcpy 中途的 Core0
  竞争、owner FREE 后的 Core1 重发、借用期间拒绝、失败恢复、STOP ACK 未到及
  真实 service reset 边界中的 ARM/config 竞争；SCPI handler 执行真实函数，但
  reset 返回和 RTOS 调度为观测 stub，不冒充实板强制交错。
  默认双应用/Boot 构建、命令开启分支 compile-only 通过，独立只读审查无阻断项。
- 资源快照，非事实源：ARM FIFO 从 1888 B 增至 1896 B，RX slot/view 保持
  296 B/40 B；默认双应用 BSS 各增加 8 B，HeapLimit/StackLimit 不变。
- 硬件验收：当前源码指纹
  `db0ae540cf417d02801fe57d712bb2056f81abb1f718ed86cb01a4710d2a2be1`
  的四板 P3 严格通过，闭环、实时门禁与诊断均通过，无强制放行；耗时
  188.407 s（本轮快照，非事实源）。四板 `FIFO_RESET` 均返回 `OK`，回收后
  队列/活动 TX 读回通过；各板原生记录 14 条、missed=0，身份与完整性核对通过。
  导出前四板均 STOP，config/applied generation ACK 一致。主控复核凭证与源码
  一致及 20 项产物哈希，并归档包与双应用 map；本轮仅验收命令禁用态四板基线，
  不作为实板强制交错、命令定时应用或实际输出锁相的证明。
- 证据：`out/HardwareAcceptance/20260915/fifo-core0-reset-r1/`；`before-reset/`
  保存冻结输入与旧版反例；`after-service-result.json`、`pytest-final-result.json`、
  `pytest-final-completion.json`、各 C runner result、`build-result.json`、
  `target-layout.json`、`source-review.json`、`source/` 及 `independent-review.json`
  保存软件、目标资源和冻结指纹；`p3/acceptance.json`、`p3/diagnostic.json`、
  `p3/tdma-process-image/` 与 `review-final.json` 保存严格验收与主控原件复核。
- 范围与下一 gate：本切片保护 FIFO 借用与回收，不关闭完整命令 STOP/session
  取消。`reset_stopped()` 仍保留 admission epoch；Core1 正确停机仍由 service
  与调用方提供，底层 FIFO 不操作 PIO/DMA。上游旧输入、完整旧记录重发、共同
  session、远端重启、payload 版本及交付上界仍需闭合；命令默认禁用，实际输出
  锁相未验收。

### VDC-PROGRESS-20260915-032 — RX FIFO 发布代际与旧命令入站取消

- TODO task ID：`VDC-CMD-002`、`VDC-CMD-003`、`VDC-ROLE-005`、`VDC-VERIFY-001`；
  状态 IN PROGRESS。031 已分离提交为 `e333da5` / `85cde62`，随后开始本切片。
  本切片代码与当前 P3 凭证已提交为 `3a42dcc`，文档单独提交。
- 反例：实际 FIFO 中旧命令的起始片在角色 A→B→A 后才被解析，随后同一记录的后续
  片段使旧未来命令获得新角色绑定；Core0 漏过中间 B 时也会发生。以下为反例快照，
  非事实源：两次旧版执行均接收 command sequence 41，ordinary mailbox 更新
  15 次，最终拒绝断言失败；同代对照通过。完整 payload 分片数超过 FIFO 容量，
  测试按真实容量排队起始片再送后续片，不伪造同 source 多邮箱。
- 修复：TDMA FIFO 新增独立 `rx_admission_epoch`，只由 Core0 RefMem 接收任务
  在线推进；Core1 在 RX payload 复制前捕获，slot/view 原样携带。Core0 身份或
  角色刷新先取得新 grant，再 reset/bind/cache；同身份不推进，owner 不可用或
  代际耗尽时返回无效 grant 并保持拒绝，禁止回绕复用。接收只过滤旧代际 command
  mailbox，保留同 view 普通 RefMem 更新与正常 release。使用已有片段拒绝计数，
  不增加实时解析、wire 字段、PIO/DMA 操作或 OTA 改动。
- 软件与构建：真实 FIFO、RefMem、TDMA service scheduler 和 adapter C runners
  通过；主控 Python 合跑 41 passed（本轮快照，非事实源）。完整 ingress 测试执行
  实际 refresh、receive、mailbox parser、分片、frame CRC 与接收保留，owner 查询
  为 stub；覆盖已排队片段、复制中途实际刷新、同代半组装保留、身份变化、grant
  失败重试及新完整命令恢复。独立 FIFO 测试另覆盖 acquired view、head/transport
  回绕、合法 STOP reset 和耗尽后普通数据连续。独立只读审查无阻断项。
  默认双应用/Boot 构建及命令开启分支 compile-only 通过，开启分支未部署。
- 资源快照，非事实源：目标 ARM ABI 的 FIFO、RX slot、RX view 分别保持
  1888 B、296 B、40 B；新增字段使用已有 padding，默认双应用 BSS、HeapLimit、
  StackLimit 均未改变。原件见 `target-layout.json` 与目标 map。
- 硬件验收：当前源码指纹
  `b540c4bd4ac225faa0aed0ed55b117d7f79292407baa7fa749b141f44b248124` 的独立
  `p3_hardware_acceptance.py run --tdma-only` 严格通过；acceptance/diagnostic 的
  strict gates 均通过且失败列表为空，TDMA closed-loop/realtime 通过。
  以下为本轮快照，非事实源：耗时 181.704 s，四板各 14 条原生记录，missed=0；
  四板全部 STOP，配置代际 ACK 一致。`review-final.json` 核对当前源码与凭证指纹、
  20 项原件散列、板端记录身份及 STOP 交接；包和双应用 map 已封存。
  本轮验收的是默认禁用命令的四板基线，不能提升为命令启用态或硬件 DCO 验收。
- 证据：`out/HardwareAcceptance/20260915/command-ingress-epoch-r1/`；
  `before-ingress/` 保存旧源码 SHA、原始 harness/exe、断言失败和同代对照；
  `after-ingress-result.json`、`ingress-final-result.json`、各 C runner result、
  `build-result.json`、`source-review.json`、`source/` 及 `independent-review.json`
  保存软件、构建与主控冻结证据。最终测试 fixture 要求新增 admission API，旧版
  反例须使用冻结的旧 harness，不能混入新 fixture 后声称重跑旧源码。
- 范围与下一 gate：取消的是 producer 已捕获旧 tag 的 FIFO 发布，含排队和复制
  中途；边界是 Core0 刷新，不是精确的 Core1 角色激活时刻。DMA/station 尚未进入
  发布的旧输入、之后完整重发的旧记录仍须协议有效期与切换规则。
  `reset_stopped()` 保留 admission epoch，仅回收队列；它要求无在用 view，
  SCPI reset 与 RefMem 借用的端到端协调仍未验收，不据此宣称完整 STOP 取消。
  命令保持默认禁用；共同 session、远端重启、版本兼容、实际交付上界、共同时间
  及主从定时应用仍需闭合，四板实际输出锁相未验收。

### VDC-PROGRESS-20260915-031 — 角色代际绑定与已接收命令防重放

- TODO task ID：`VDC-CMD-002`、`VDC-CMD-003`、`VDC-ROLE-005`、`VDC-VERIFY-001`；
  状态 IN PROGRESS。030 已分离提交为 `8f530ed` / `50588c0`，随后才开始本切片。
- 反例：旧实际 manager 函数体在 A→B→A 及 FOLLOWER→MASTER→FOLLOWER 后均重复
  提交旧 retained 命令。`out/HardwareAcceptance/20260915/command-role-fence-r1/`
  的 `before-owner/` 保存旧函数来源 SHA、可执行 harness、两次断言失败及原始输出。
  Domain 应用为观测 stub，因此它证明重复提交路径，不冒充物理 DCO 的波形反例。
- 修复：Core1 将当前实际 `control.profile.generation` 传给命令 getter，
  `refmem_sync_vdc_copy_command_for_generation()` 在同一 guard 内核对 context
  的本地 consumer generation 与完整副本。Core0 RefMem 任务在本地角色代际变化时
  作废所有 retained 值并取消当前片段组装，保留各来源 command/frame 序号水位；
  排序依据不再使用 `valid`。已见旧命令换新 transport sequence 也不能恢复有效。
  同代际调用无副作用；epoch/run 等接收身份 reset 才退休历史。parser 在组装前
  要求 transport/context 本地 generation 一致，避免两次快照跨代消耗新命令。
- HAOFV 与资源：角色身份由 Core1 拥有，接收状态仍由 Core0 RefMem 任务唯一写入；
  不比较本地 role generation 和远端 command generation，不改 wire 或 OTA。
  默认 release 双应用/Boot 和 command 开启分支 compile-only 通过，开启分支未部署。
  以下为目标 map 快照，非事实源：接收 context 从 448 B 增至 456 B，双应用 BSS
  均增加 8 B，Heap/Stack limit 不变；原件见 `target-layout.json` 和 link map。
- 软件结果：真实 receiver/binding/copy 的 C runner 通过；角色、待执行命令、Core0
  延迟/漏代际与复制交错均有正反验证。以下为本轮快照，非事实源：owner/ingress
  Python 合跑 30 passed，C 测试包含 4 次复制中途写入。新增 ingress harness
  执行实际 refresh 函数及 parser 准入前缀，使用真实 RefMem 库和 mailbox CRC，
  不覆盖后续全片交付。该测试曾暴露函数提取器误匹配更早的 `if (refresh(...))`；
  修正为行首有类型的定义后全绿，失败原件见 `ingress-extractor-before.json`。
  最终独立只读审查通过，仍明确 manager 的 Domain 应用端是观测 stub。
- 硬件结果：当前源码指纹
  `14f00d1d4131e4987addf7189bd7121454f33f8e34e0c33a62f292d4375bb759` 的独立
  `p3_hardware_acceptance.py run --tdma-only` 通过；`p3/acceptance.json` 与
  `diagnostic.json` 均为 strict gates passed，失败列表为空，短帧 closed-loop/
  realtime 通过。以下为本轮快照，非事实源：总计 198.460 s，每板 14 条原生记录，
  missed=0；四板全部 STOP，配置代际均已 ACK。`review-final.json` 核对当前源码
  与凭证指纹、20 项原件散列及板端记录身份，固件包及双应用 map 已另存本切片目录。
  本轮部署并验收的是命令默认禁用态，不提升为角色切换的实板命令功能验收。
- 范围与下一 gate：本切片只关闭已接收/已见命令在角色回切后的重放，不关闭完整
  角色切换协议。切换前已准备或排队、切换后才首次完整收到且序号更大的未来命令，
  仍可能满足本地新 tag；必须继续建立 TDMA ingress fence 及切换生效定义，不能
  靠清组装、丢首帧或随意增加本地 run 代替。远端 generation 重启、共同 session、
  Domain clock/oscillator 请求历史、版本兼容和交付上界仍是命令启用前置。
  命令运输保持默认禁用，不据此宣布三从应用或正式锁相完成。

### VDC-PROGRESS-20260915-030 — 命令区唯一写者与在线 reset 一致性

- TODO task ID：`VDC-CMD-002`、`VDC-CMD-003`、`VDC-VERIFY-001`；状态 IN PROGRESS。
  普通四板基线已分离提交为 `d01c824`（代码）和 `9f62d1e`（文档）；本切片继续
  保持 `DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED` 默认关闭。
- 原因与收益：原接收区可由 Core1 及 Core0 的 SCPI/RefMem 不同任务触达，在线
  调用冷 `init` 还会归零 guard。真实 reader copy 中途 reset 再发布的反例接受了
  旧 command sequence 与新 phase 的混合副本。这是可复现的数据一致性缺陷，
  有修复价值，不通过隔离命令运输而将其永久搁置。
- 修复：仅 Core0 RefMem 任务刷新/退休命令区，service 入口先取得可信身份再
  接收；Core1 和 SCPI 不再直接清空。snapshot 失败只暂停命令准入，普通 flight
  继续。在线 `refmem_sync_vdc_reset()` 在原 guard 内清空并更新身份，保留发布
  序列；忙标记后增加写屏障。Core1 在序号消费前拒绝旧 epoch/run，并在会话变化
  时重置本地序号水位；未更改 wire、OTA 或 DCO 使能。
- 软件证据：`out/HardwareAcceptance/20260915/command-lifecycle-r1/` 保存旧源文件、
  当前源码副本、diff、编译命令、失败/成功原件和独立审查。以下为本轮快照，非事实源：
  旧实现反例产生 `seq=11, phase=-22` 后断言失败，修复后交错/身份/回绕测试通过；
  host runner 38/38 通过，生产 manager/service 函数体测试 20 passed。首次 host
  运行曾因临时 CRC 链接缺失 `portable_ota_port.h` 失败；测试改用既有 CRC fixture
  后完整重跑通过，旧失败另存 `host-unit-r1-failure.log`。
- 构建与边界：默认 release 双应用/Boot 构建及 flash link 通过，启用 command
  的 RefMem 分支另做 ARM compile-only 验证且未部署；最终独立只读审查未发现本
  切片阻断项。双应用 link map 与 `target-symbols.json` 保留新增会话水位及 reset/
  copy 的实际地址和符号尺寸，不从单个符号推算总 RAM 变化。Python 测试使用真实
  时间映射，但 Domain actuator 是观测 stub，
  只证明 manager 的准入及应用尝试，不能证明真实 DCO 或完整 Domain 换会话行为。
- 硬件结果：独立执行 `p3_hardware_acceptance.py run --tdma-only`，当前源码指纹为
  `43fe95f0777f4b97fa58f35f7391c8f8a4537874c2c06b7347dff2aca4ec28b6`，build
  `20260915022619`。`p3/acceptance.json` 与 `diagnostic.json` 的严格门禁均通过，
  失败列表为空；四板短帧 closed-loop/realtime 通过，四板全部 STOP 后交接原生记录。
  以下为本轮快照，非事实源：预算计时 194.015 s，每板 14 条样本且 missed=0。
  `review-final.json` 复核当前源码/凭证指纹、20 项原件散列、记录 build/身份和 STOP
  代际 ACK；本轮固件包另存切片目录。通过范围是默认命令禁用态的四板 P3。
- 下一 gate：`VDC-CMD-002/003` 继续保留同会话角色/来源 A→B→A、远端 generation
  重启、`vdc_domain_publish_clock_model()` 任意换会话后的 Domain history 退休、
  schedule 与完整 STOP 取消；payload 版本、共同 session 和交付上界仍是启用前置。
  不将本切片提升为命令启用态、三从应用或正式锁相完成。

### VDC-PROGRESS-20260915-029 — 粗校准 STOP 后配置拒绝的有界恢复

- TODO task ID：`VDC-TDMA-001`、`VDC-VERIFY-001`；状态 IN PROGRESS。028 的三轮
  验收原件均已复核，短帧闭环通过但严格校准仍有失败；本切片仅处理已重复出现的
  TOPology 准备拒绝，不开放新的 VDC 命令功能。
- 诊断：两轮失败前已读回 `ring_enabled=0`、`ring_adapter_started=0`，且当前
  config generation 与 applied generation 相等。TOPology handler 失败推入
  execution error 而不返回结果 tuple，主机因等待结果显示 timeout。这不是固件
  执行耗时，也不是 quick action timeout 太短。STOP ACK 不能证明 Core0 控制锁、
  队列退休、intent completion 以及所有快照读同时可用；当前通用错误未区分具体
  拒绝点，不能声称已经定位某个 seqlock 或锁为实测根因。
- 修复：`calibration_clk_train.py::_set_stopped_topology()` 仅在无结果且 error
  queue 明确返回 execution error 时，按 `TOPOLOGY_ATTEMPT_LIMIT` 有界重发同一
  配置；每次先重新核验 STOP 与当前代际 ACK。错误 tuple、其他错误及 STOP 失效
  不放行。成功必须收到正确 tuple，再等待新的 config generation 与 applied
  generation 相等，全部板完成后才 ARM；ARM 后继续校验实际 topology。STOP 的
  runtime topology 会被 owner 清空，不用这些零字段校验 staged topology。
- 所有者边界：相同参数的重复配置可收敛到同一目标，但 setter 并非无副作用事务，
  成功会发布新 STOP/config generation，失败前也可能已更新部分配置。因此保留
  每次拒绝原件、重新确认停止并等待新代际，不复用配置前 ACK，不在固件增加等待。
- 软件反例：已有 `arm_training_persona()` 在临时拒绝后中止的反例先失败；修复后
  覆盖重试成功、固定上界、错误 payload/错误码、STOP 丢失、旧代际及新代际未 ACK
  的正反验证通过。与 coarse/coded/topology/TRN-03 相关 Python 回归共 159 passed
  （本轮快照，非事实源）；双应用/Boot 目标构建及 flash link 检查通过。
- 证据：`out/HardwareAcceptance/20260915/coarse-topology-recovery-r1/` 保存修复前
  工具/测试副本、当前源文件、diff 和 `software-verification.json`。当前源码指纹为
  `a05085436c36d50903646f27cfb3e490f3fbf1b7719adf055514993b6bf9dc03`；随后独立
  执行正式 `p3_hardware_acceptance.py run --tdma-only`，包含当前构建与四板部署。
- 硬件结果：该目录 `p3/acceptance.json` 与 `diagnostic.json` 的
  `passed/flow_completed/strict_gates_passed` 均为真，失败列表为空；coarse CLK、
  SCK training、replay matrix 和 TDMA closed-loop/realtime 均通过。四板全部
  STOP，原生记录交接通过。以下为本轮快照，非事实源：总预算计时 184.344 s，
  每板 14 条记录，四种 reference 配置共 16 次 TOPology_APPLIED 均复核新代际
  ACK。本轮没有实板重试，重试分支的恢复和拒绝边界由 host 反例覆盖，不能声称
  已在硬件上复现并消除某个具体锁冲突。
- 主控复核：`review-final.json` 核对当前源码指纹、当前凭证、20 项引用原件散列、
  每次 topology 前后代际及四板 STOP 记录；固件包另存切片目录。此前三轮失败均
  保留，后来的通过不追认旧结果。当前完成普通四板 quick 诊断基线收敛，不提升为
  命令启用态或 DPLL 锁相验收。
- 独立只读复核：`command_gate_audit` 确认拒绝不提升为成功、新代际 ACK 和全板
  ARM 屏障成立。底层具体暂态拒绝点及 SCK 候选稳定性仍需单独诊断，不由有限恢复
  或一次通过追认历史失败。下一 gate 保持 `VDC-VERIFY-001` 与既有命令前置依赖。

### VDC-PROGRESS-20260915-028 — resident 命令编译隔离与四板收敛

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-002`、`VDC-CMD-003`、`VDC-VERIFY-001`；
  状态 IN PROGRESS。前序 027 完成独立 P3 后才开始本切片。
- 变更：`DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED` 默认关闭，使用条件
  编译隔离 resident 命令 prepare、TX 编码/游标、RX fragment 分派、identity 同步
  及私有 record/组装状态。普通 compact mailbox 继续处理，command class 在普通
  RX 中被拒绝。诊断 snapshot 字段保持存在且清零；通用命令校验、共同时间准入和
  follower 时间门禁保留。旧 standalone 排他条件仍在，不能称为旧 VDC 功能恢复。
- 软件与资源：`pico2-release` 双应用及 Boot 构建通过；host runner 38/38 通过；
  文档检查器和 DPLL capture Python 回归 36 passed。使用真实目标编译命令分别
  覆盖开关关闭/开启，均可编译；预处理原件确认 prepare/parser/identity helper 和
  私有 record/fragment 状态只存在于开启分支。开启分支仅做编译验证，没有部署。
  以下为目标对象快照，非事实源：`s_tdma_flight_sync` 从 1280 B 降至 1104 B，
  释放 176 B。数据及命令见 `compile-isolation.json`，不把局部 RAM 回收当成 WCET
  或全表调度收敛证据。
- 当前源码指纹为
  `742074468a80d50a6bfdb8a0c38e1daf15e3da6f3ce978873f26ddeeacf9a652`。
  增量构建沿用 build `20260915022619`，必须同时核对 source/package SHA-256；
  build ID 相同不代表两切片固件相同。完整证据根为
  `out/HardwareAcceptance/20260915/command-isolation-r1/`。
- 首轮 `p3/` 已完成正式 `run --tdma-only`，四板部署和短帧 closed-loop/realtime
  通过、全部 STOP；但 `strict_gates_passed=false`，保留 SCK training 和 replay
  row selection 两项失败。以下为本轮原件快照，非事实源：17 次 SCK 测量全部有效，
  NO2→NO3 的 8 次结果只有一个偏移候选，不满足候选覆盖；四组候选均未满足重臂
  预算，最小从板余量为 -1 sample。`[1,0,1,0]` 是偏移值，不是缺样数量。
  `p3-failure-review.json` 保存分析；基础流通过不抵消校准失败。
- 恢复范围：源码及固件包 SHA-256 与首轮部署一致，随后使用受支持的 `resume
  --tdma-only` 入口，仅复用该轮成功 OTA 原件，重新核验 live build、软件复位、
  校准及 TDMA。恢复原件单独写入 `p3-resume-r2/`，不覆盖首轮失败。
- 第二轮 `p3-resume-r2/` 的 SCK training 与 replay matrix 均通过，短帧闭环、
  实时检查及 STOP 后记录交接通过；但粗 CLK 校准准备阶段 NO4 的
  `SYSTem:TDMA:RING:TOPology 4,3,0` 返回 timeout，伴随 SCPI execution error，
  `strict_gates_passed` 仍为假。它是本轮原始控制流程失败，不能推定为前一轮
  SCK 候选不足的同一原因，也不能据此归因到 resident 隔离代码。
- 第三轮 `p3-resume-r3/` 在 NO2 再次出现同一 TOPology 准备拒绝，其余校准及
  短帧通过；三轮 `strict_gates_passed` 均为假。四板每轮均 STOP，原生记录交接
  通过；`review-final.json` 复核三轮源码、包及引用原件 SHA-256。以下为本轮
  `acceptance.budget` 快照，非事实源：首轮含 build/OTA 为 185.410 s，两轮同源码
  恢复为 75.078 s、71.510 s；不把嵌套 timing duration 累加为总耗时。停止盲目
  复测后，准备流程修复转入 029；本条不能单独声称严格 P3 已通过。
- 独立只读复核：`command_gate_audit` 检查开关两态、私有字段引用、普通 builder
  和旧排他条件，未发现需追加的隔离修复。TDMA TODO 同时清理重复任务行和悬空
  引用，稳定 Task ID 保留，未提升任务或契约状态。
- 下一 gate：重新启用前须闭合 context reset 的唯一写者/guard、payload 版本
  兼容，以及发布间隔/FIFO 背压/重复片段下的交付上界。它们作为原型待办保留；
  每项修改单独做 host、目标构建、四板 P3 和对应功能验收。命令 apply、DCO 跟随、
  全表 WCET 及 formal lock 仍未完成，OTA 实现与配置不变。

### VDC-PROGRESS-20260915-027 — 非法命令目标位移修复及四板验收

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`、`VDC-VERIFY-001`；状态 IN PROGRESS。
- 已确认缺陷：`refmem_sync_vdc_receive_frame()` 在校验 unicast target 范围前计算
  `1u << command.target_slot`，外层及 payload CRC 正确的非法目标也会触发未定义
  位移。现在先用 `REFMEM_SYNC_NODE_COUNT` 判断范围，再以短路表达式计算 mask；
  广播及正常目标继续沿原校验路径。
- 反例：新增 `test_vdc_invalid_targets_preserve_retained_command()`，先保留一条
  有效命令，再输入越界、位宽边界及接近整数上限的目标，核验逐次拒绝、原命令序号
  和 accepted count 不变。`-fsanitize=shift -fsanitize-undefined-trap-on-error`
  在修复前触发 trap，修复后退出成功；原始结果见 `target-before.json` 与
  `target-after.json`，早期测试构建失败日志也保留。这证明真实健壮性缺陷，不能
  倒推它就是历史零分片/prepare 拒绝的原因。
- 独立验收：完成相关 host 和 `pico2-release` 构建后，单独运行
  `python tools/hardware_acceptance/p3_hardware_acceptance.py run --tdma-only
  --build-dir out/build-after-command-gate --out-dir
  out/HardwareAcceptance/20260915/command-target-fix-r1/p3`。
  `passed/flow_completed/strict_gates_passed` 均为真，无 diagnostic failures；
  TDMA closed-loop/realtime gate 通过，`left_running=false`，四板 STOP 后原生
  记录交接通过。凭证源码指纹为
  `c23d0f0990585231fba103f3c8a8d7daf63c87bce75750d6835027e422426253`。
- 证据：`out/HardwareAcceptance/20260915/command-target-fix-r1/` 保存反例、源码
  副本、diff 与 P3 原件。通过后才进入下一项 resident 编译隔离；没有合并两项代码
  变化共用一轮 P3。命令启用态及锁相未验收，OTA 实现/配置和 NO5 未改动。
- 下一 gate：`VDC-CMD-002/003` 先保持隔离，关闭普通四板基线收敛切片；后续按
  TODO 依赖逐项补齐所有者、时间输入与交付证据。

### VDC-PROGRESS-20260915-026 — 未提交改动对账与普通四板基线隔离

- TODO task ID：`VDC-TDMA-001`、`VDC-CMD-001`、`VDC-CMD-002`、`VDC-CMD-003`、
  `VDC-CMD-004`、`VDC-CMD-005`、`VDC-VERIFY-001`；状态 IN PROGRESS。
- 用户要求：先列清未提交修改和实际阻塞，评估收益；低收益增量记录后局部回退或
  隔离。先保证四板基础流程，再逐项加回，每次功能变化必须先跑 P3，避免错误累计。
- 审计基点为 `7fa834dfe531a902380f4c842f2cf5013c9acb47`。以下数量为本轮快照，
  非事实源：开始时有内容差异的 tracked 文件为 28 个，增加 1440 行、删除 951 行；
  另有 7 个 status 显示修改但 Git 内容 diff 为空，8 个 untracked 文件。不能从 diff
  判断每块代码来自哪台设备或哪位作者，也不能把这些增量全部视为同一个 patch。
- 逐文件路径、类别、行数、SHA-256、完整 diff 和工作区副本保存在
  `out/HardwareAcceptance/20260915/uncommitted-audit-r1/`。其 `inventory.json`
  在本次发送隔离增加 6 行之后、本文更新之前生成；`before-switch-reconstructed/`
  仅移除本轮精确加入的开关和条件，按 LF 保存两份源文件，可复核隔离前后差异。
  未知 HTML 仅登记路径/散列，没有清理或修改；OTA 配置、另一设备单板成果均保留。

| 改动组 | 实际内容 | 处置及证据边界 |
|---|---|---|
| RefMem 命令 wire 与副本 | `refmem_sync_frame.h/.c` 增加 epoch/run、广播目标，payload 由旧版尺寸扩展；`refmem_sync.h/.c` 增加顺序分片重组、guard 和有界稳定复制。 | 保留实现及负测。wire 仍是待审原型，混用旧固件的兼容性不能由同版本四板 P3 推定。 |
| resident 发送/接收接线 | `distributed_refmem.h/.c` 新增 MASTER record prepare、分片发片、FOLLOWER 重组、会话清理和诊断；resident enabled 时抑制旧 standalone TX/RX。 | 本轮只禁用新发送选择；普通 compact TDMA 继续，不恢复旧 standalone VDC 命令。 |
| TDMA 元数据 | `tdma_process_image_layout.h` 增加命令 message class/fragment 布局；`tdma_ring_runtime.h/.c` 的 clock snapshot 增加周期、超时和 ring sequence。 | 保留；status 中其余 PIO/flight/adapter 文件无 Git 内容差异，不能归因为本轮新 PIO 行为。 |
| VDC/DPLL 时间应用 | `vdc_time_mapping.h/.c` 新增共同时间映射、新鲜度、到期/迟到、序列回绕函数；manager follower 消費由原 uptime 比较改为共同时间检查；domain 新增 late 计数。 | 这些正确性约束有价值；运输和映射尚未闭合，不能以关掉时间校验来取得 apply。 |
| 构建、SCPI、工具与测试 | CMake 加入时间映射文件；SCPI 和两个解析工具扩展诊断字段；RefMem、VDC、时间映射及 Python 测试同步。 | 双应用/Boot 构建通过，host 脚本 38/38；诊断字段本身不能证明新功能成功。 |
| 文档、凭证与外部文件 | TDMA TODO 大幅整理；VDC 方案/进度及索引变更；新增 RAM/review 文档；P3 receipt 更新；未知 HTML。 | 文档行数变化不等于运行路径变化。原始 P3 成败按各轮目录保留，未知文件不动。 |

- 已确认的第一处断点是 MASTER prepare 到 record/fragment 的交接；prepare 失败会
  回落普通 mailbox，不直接停止 TDMA。旧 `four-board-clock-diag/command-clock-readback.json`
  为 prepare 尝试/拒绝均 1228、reason 5；`four-board-clock-diag-r3/direct-window-readback.json`
  的 MASTER 为 1787/1787、reason 16；`four-board-clock-map-r5/direct-window.json`
  为 1785/1785、reason 15。对应四板 fragment/complete/accept 均为零。以上为历史
  原始读回快照，数字和枚举解释须绑定当轮源码，不将旧元组套用到新增字段后的布局。
- 原因边界：已有摘要把 reason 15 概括为“窗口已开始”；当前源码的 reason 15
  实际用于 `local_now_ns < local_rx_timestamp_ns`。窗口顺延和 fresh-now 映射虽已在
  原型中修改，但缺少最新版本的命令成功原件，不能据此宣称根因已修复，也不能从
  零分片推定 CRC 或接收器已有故障。此前独立启动 barrier 超时同样保留为未解决事实。
- 价值判断：共同时间身份、新鲜度、CRC、会话取消及有界跨核读取必须保留。当前低
  收益做法是把尚未完成的定时应用依赖与发送准备同时接入，再反复运行只覆盖 TDMA
  基础流的 P3；P3 通过无法定位命令层错误。当前隔离该原型，后续每步除 P3 外还需
  对应的正向功能读回，首先证明完整接收，再证明定时应用。
- 单项代码变化：在 `distributed_refmem.h` 加入
  `DISTRIBUTED_REFMEM_VDC_COMMAND_TRANSPORT_ENABLED`，当前关闭；在
  `distributed_refmem_tdma_flight_sync_publish()` 的 `resident_master` 条件中短路。
  校验和旧会话保护继续存在，未绕过 HAOFV owner，未启用从板本地 PI。该变化是
  发送功能隔离，不是对 HEAD 的全量源码回滚，更不是四板锁相完成。
- 软件验证：`python tools/cmake_build_auto/cmake_build_auto.py --preset pico2-release
  --build-dir out/build-after-command-gate` 通过，build `20260915022619`；编译容量
  `PROJECT_NODE_CAPACITY` 保持当前配置，实板使用四节点。`powershell -NoProfile
  -ExecutionPolicy Bypass -File tools/tests/run_host_unit_tests.ps1` 通过 38/38。
  使用 Windows PowerShell 是因为该 host runner 为 `.ps1` 且环境无 `pwsh`。
- 已复核的隔离前 P3：`four-board-clock-map-r5/` 及
  `four-board-command-map-r6/diagnostic.json` 的四板 quick 结果通过；r6 为
  `passed=true`、`strict_gates_passed=true`。这已经说明基础 TDMA 可以通过，不能把
  禁用命令之后的成功反过来当作“命令代码曾造成基础 TDMA 故障”的因果证据。
- 本次隔离的 P3：`python tools/hardware_acceptance/p3_hardware_acceptance.py run
  --tdma-only --build-dir out/build-after-command-gate --out-dir
  out/HardwareAcceptance/20260915/four-board-baseline-command-disabled-r1` 通过。
  `acceptance.json` 的 source tree SHA-256 为
  `bf2f19e9ad51f0a062accd5ebd6ae9a423bf785737b6eebc08a9477e6163a15f`；
  `diagnostic.json` 为 `passed=true`、`flow_completed=true`、
  `strict_gates_passed=true`、`failures=[]`。保留 quick profile 的
  `diagnostic_continue=true` 事实，不提升为产品验收或 formal lock。
  TRN-03 的 realtime/closed-loop/diagnostic/startup barrier/soak 均通过，
  `left_running=false`，四板 STOP 后原生记录均可读；原件与散列见
  `tdma-stopped-handoff.json`。`timing.json` 的总预算计时约 188.5 s（快照，
  非事实源），含四板部署；NO5 未参与。
- 文档及工具验证：docs_check 的 strict names、doc_regression 均通过；文档检查器
  与 DPLL capture Python 回归合计 36 passed。原有 `TDMA-FLIGHT-BITMAP-01` 格式
  WARN 保留。`check-staged` 当前返回 no staged code change，只表示本轮没有暂存
  源码，不冒充提交指纹验收；本轮未提交。
- 独立只读复核：`command_gate_audit` 认可该变更作为普通四板 TDMA 发送隔离，
  不认可将它描述为完整 VDC 回退。还指出恢复时须核对
  `(REFMEM_SYNC_VDC_FRAGMENT_COUNT + 2u) * schedule.period_ns` 的提前量是否
  覆盖实际发布间隔和 FIFO 背压；这是待验证风险，尚不是实测首个阻塞。
- 下一 gate：沿 TODO 的时间输入/角色/契约前置条件推进；每个后续增量分别完成
  P3 和功能证据，不一次重新打开全部命令逻辑。命令 apply 与锁相继续未完成。

### VDC-PROGRESS-20260915-025 — common-time/command-record 阻塞诊断贯通

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-002`、`VDC-TDMA-001`；状态 IN PROGRESS。
- 变更：将 TDMA adapter 的 observation/build 计数与最近拒绝原因贯通到 runtime
  snapshot，并新增只读 `SYSTem:TDMA:RING:CLOCK:DIAGnostic?`；将 MASTER
  command-record 的 prepare 尝试、拒绝原因、record 活跃/分片位置和最近 effective
  common time 加入 `SYSTem:REFMEM:SYNC:FLIGHT?` 尾部字段。运行时仍只在既有
  Core1/TDMA 边界更新，SCPI 只在 STOP 后读取。
- 软件验证：`pico2-release` 固件双应用、Boot、USB namespace、flash map/link 检查
  全部通过；build identity 为 `20260915012243`。本切片未改变 OTA 配置。
- 当前解释：四板上一轮 `vdc_command_fragment_rx/complete/accept=0` 只能说明
  command record 尚未发车；下一次四板短窗应先读取 prepare last reason，再区分
  common-time mapping 拒绝、window/identity 拒绝和实际 fragment transport 缺口。
- 硬件状态：尚未用本次 build 重做四板 P3；不得据此宣称 command apply、DCO 跟随或
  formal lock。下一 gate：当前源码指纹下重新完成四板 quick P3，再用新增字段定位
  首个真实拒绝原因。

### VDC-PROGRESS-20260915-016 — resident VDC command fragments and current-source P3

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-002`、`VDC-CMD-003`；状态 IN PROGRESS。该切片
  只验证固定 process image 上的受限 resident 运输原型，不冻结共同时间、过期策略或
  正式 follower application 契约。
- 实现：新增 `TDMA_PROCESS_IMAGE_VDC_COMMAND_MESSAGE_CLASS`。Core0 将完整 VDC
  command payload 按 `REFMEM_SYNC_VDC_FRAGMENT_COUNT` 分片，mailbox 保留 source、
  target mask、`uint16_t` transport sequence、fragment index/count 和固定数据区；
  follower 逐片校验 CRC、source、target、index/count 与回绕连续序列，完整重组后再
  通过既有 frame/payload CRC、epoch/run 和 command sequence 准入。完整命令由
  `vdc_command_guard` 保护，Core1 经 `refmem_sync_vdc_copy_command()` 有界复制。
  STOP/ARM、role、epoch/run、schedule CRC 和 control generation 变化会清理组装状态。
- 读回：`SYSTem:REFMEM:SYNC:FLIGHT?` 末尾追加 fragment RX/complete/reject、command
  accept 和最后 command sequence；`tools/tdma_ring_monitor/flight_bitmap_validate.py`
  已同步字段表，供 STOP 后统一读回，不用于实时采样。
- 软件验证：`powershell -ExecutionPolicy Bypass -File tools/tests/run_refmem_sync_tests.ps1`
  通过；host fragment/seqlock/负测共 21 项通过。目标构建
  `cmake --build out/verify/vdc_resident_fragment` 通过双应用、Boot、USB namespace、
  flash map/link 检查；随后 `powershell -ExecutionPolicy Bypass -File
  tools/tests/run_host_unit_tests.ps1` 的 37/37 host unit test scripts 通过。
- P3：加入 SCPI 只读计数后按当前源码重新运行
  `python tools/hardware_acceptance/p3_hardware_acceptance.py run`，build `20260915001009`
  仍在五板 OTA 阶段因缺失序列号 `839E1AE79EA20F31` 失败；本轮原始证据保留于
  `out/HardwareAcceptance/20260915/p3-081003/`，前一轮 `p3-080313` 也保留，不能以旧
  receipt 替代当前源码验收。
- 边界：当前四板实际 fragment RX/complete/accept、effective time 对账和 DCO 输出
  跟随尚未取得；不能据此宣称共同时间、正式 LOCKED 或四板锁相。另一设备的单板改动
  保持未提交，OTA 保持不变。
- 下一 gate：恢复缺失板卡后重跑当前源码 P3；随后在四板 STOP 后读取 fragment
  receive/complete/reject、command accept、source/generation/schedule CRC/sequence
  和 DCO application 计数，再决定 `VDC-CMD-001` 是否具备独立交叉审核和登记条件。

### VDC-PROGRESS-20260915-017 — common-time deadline mapping and session fence

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`、`VDC-CMD-004`；状态 IN PROGRESS。本条
  仍是实现切片，不改变 `VDC-CMD-001` 的独立审核状态。
- 命令 payload 增加 `epoch_id`/`run_id`，接收端同时校验 frame header 与 payload 的
  session identity；旧 session 即使 source、schedule 或 command sequence 重用也会被
  拒绝。payload 尺寸变化由 `sizeof(refmem_sync_vdc_command_payload_t)` 驱动分片计数，
  没有新增独立同步帧。
- VDC owner 新增有界 `vdc_dpll_manager_map_local_to_common_time()`：只接受当前
  schedule CRC 下的 hardware-latched、COMMON_TIME/CYCLE_PHASE observation，并以
  `feedback_timeout_ns` 限制 anchor age。MASTER 先将本地 TDMA window 映射到共同时间
  再生成命令；FOLLOWER 以同一映射比较 deadline，映射无效时保持上一可信 DCO 输出。
- 验证：`cmake --build out/verify/vdc_resident_fragment` 通过双应用、Boot、flash
  map/link 检查；`run_refmem_sync_tests.ps1` 通过，并新增旧 session 拒绝负测。全量
  `run_host_unit_tests.ps1` 为 37/37 通过。当前源码 P3 build `20260915002136` 已重跑，
  仍在五板 OTA 阶段因缺失序列号 `839E1AE79EA20F31` 失败，原始证据在
  `out/HardwareAcceptance/20260915/p3-082128/`。
- 下一 gate：补齐 mapping 的 C/host 边界负测（无锚、过期锚、schedule/session mismatch、
  overflow），然后恢复缺失板卡重跑 P3；四板上对账 common deadline、实际 apply 时间和
  DCO 读回后，才进入 `VDC-CMD-001` C11 交叉审核。

### VDC-PROGRESS-20260915-018 — latest-source validation rerun

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`、`VDC-CMD-004`；状态 IN PROGRESS。本条只
  记录验证结果，不改变 wire 契约、锁相状态或 C11 审核状态。
- 软件验证：重新执行 `cmake --build out/verify/vdc_resident_fragment`、
  `run_refmem_sync_tests.ps1` 和 `run_host_unit_tests.ps1`；构建检查通过，refmem sync
  通过，全量 host unit 为 37/37。
- 当前源码 P3 已再次运行，build `20260915002546` 在五板 OTA 阶段失败，唯一报告为
  `missing_serial_numbers=839E1AE79EA20F31`；原始证据保留于
  `out/HardwareAcceptance/20260915/p3-082540/`。该结果不能替代缺失单板恢复后的五板
  验收，也不能推导四板锁相或 `FORMAL_LOCKED`。
- 下一 gate：补齐 mapping 的 C/host 边界负测（无锚、过期锚、schedule/session mismatch、
  overflow），恢复缺失板卡后重跑当前源码 P3，再执行四板 STOP 后 fragment/command/DCO
  对账；在此之前 `VDC-CMD-001` 继续等待独立 C11 交叉审核。

### VDC-PROGRESS-20260915-019 — stateless common-time mapping boundary gate

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`、`VDC-CMD-004`；状态 IN PROGRESS。本条只
  记录边界验证切片，不改变 wire 契约、锁相状态或 C11 审核状态。
- 实现：新增 `vdc_time_mapping_map_local_to_common_time()` 无状态映射模块。VDC owner
  先复制 TDMA ring clock snapshot，再调用该函数；函数拒绝空快照、未启用/未启动环路、
  无效周期或反馈超时、schedule CRC 不匹配、无关联硬件 latch、时间倒退、过期 anchor
  和共同时间加法溢出。Core1 的调用仍保持有界，不增加解析、存储或等待。
- 软件验证：新增 `run_vdc_time_mapping_tests.ps1`，覆盖有效映射和上述拒绝边界；专项
  测试通过。固件 `cmake --build out/verify/vdc_resident_fragment` 通过双应用、Boot、
  USB namespace、flash map/link；全量 host unit 为 38/38；`test_dpll_vdc_monitor.py`
  为 24/24。
- 当前源码 P3 已重新运行，build `20260915003253` 仍在五板 OTA 阶段因缺失序列号
  `839E1AE79EA20F31` 失败，原始证据保留于
  `out/HardwareAcceptance/20260915/p3-083247/`。这不是四板共同时间或锁相证据。
- 下一 gate：缺失单板恢复后重跑当前源码 P3；随后四板 STOP 后对账 fragment、command、
  common deadline、实际 DCO apply 和输出记录，才进入 `VDC-CMD-001` C11 交叉审核。

### VDC-PROGRESS-20260915-020 — receiver-owned late command retirement

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`、`VDC-CMD-004`；状态 IN PROGRESS。本条只
  记录定时应用的迟到处理切片，不改变 wire 契约、锁相状态或 C11 审核状态。
- 实现：新增 `vdc_time_mapping_classify_effective_time()`。FOLLOWER 在同一份 TDMA
  snapshot 上先完成共同时间映射，再按当前激活的 `cycle_period_ns` 判定命令为提前、
  窗口内到达或超过最大迟到窗口。提前命令继续保留等待；窗口内命令进入唯一 DCO
  应用者；超过一个周期的命令以非法命令退休、记录计数并保持上一可信输出，发送端无法
  通过 wire 字段放宽该门禁。该边界随 STOP 后周期配置变化，不增加 mailbox 字段。
- 软件验证：映射专项测试通过，新增提前/准时/迟到/非法输入边界；全量 host unit 为
  38/38，固件双应用、Boot、USB namespace、flash map/link 构建通过。
- 当前源码 P3 已重新运行，build `20260915003820` 仍在五板 OTA 阶段因缺失序列号
  `839E1AE79EA20F31` 失败，原始证据保留于
  `out/HardwareAcceptance/20260915/p3-083813/`。该结果不构成四板实际应用或锁相证据。
- 下一 gate：恢复缺失单板后重跑当前源码 P3；四板运行时需对账提前、准时和迟到命令的
  接收/退休计数、common deadline、DCO apply 及实际输出，再进入 `VDC-CMD-001` C11
  交叉审核。

### VDC-PROGRESS-20260915-021 — command sequence wrap continuity

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`；状态 IN PROGRESS。本条只记录命令序列
  回绕切片，不改变 wire 契约、锁相状态或 C11 审核状态。
- 实现：FOLLOWER 的实时命令准入改用有界 signed-difference 序列比较，避免
  `UINT32_MAX -> 1` 回绕后的新命令被简单数值比较误判为陈旧；零序列、重复和真正倒退
  仍拒绝。逻辑位于无状态映射/准入辅助模块，未增加 Core1 等待或 mailbox 字段。
- 软件验证：新增 sequence 首值、重复、倒退、回绕和零值负测；映射专项测试通过，固件
  构建通过，全量 host unit 为 38/38。
- 当前源码 P3 已重新运行，build `20260915004220` 仍在五板 OTA 阶段因缺失序列号
  `839E1AE79EA20F31` 失败，原始证据保留于
  `out/HardwareAcceptance/20260915/p3-084212/`。该结果不能替代四板命令接收或锁相
  证据。
- 下一 gate：恢复缺失单板后重跑当前源码 P3；四板需在实际序列回绕、迟到和 STOP/ARM
  场景下读回接收/退休/apply 计数及 DCO 输出，再进入 `VDC-CMD-001` C11 交叉审核。

### VDC-PROGRESS-20260915-022 — resident transport wrap regression

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-002`；状态 IN PROGRESS。本条只补充运输层回绕
  证据，不改变 wire 契约或审核状态。
- 测试：`test_refmem_sync.c` 新增独立 VDC context 的命令序列回绕负测，先接受
  `UINT32_MAX - 1`，再接受 `1`，确认完整帧、payload CRC、session、来源和目标校验均
  通过且最新命令被保留；重复/倒退规则仍由既有测试覆盖。
- 验证：`run_refmem_sync_tests.ps1` 通过。该切片只改 host/real-C 测试，不替代板端
  resident 接收和三从 DCO apply 证据；上一条当前源码 P3 原件仍为
  `out/HardwareAcceptance/20260915/p3-084212/`。
- 下一 gate：缺失单板恢复后，以当前固件重跑 P3，并在四板实际 transport sequence
  回绕和 command apply 场景中完成 STOP 后计数对账，再推进 `VDC-CMD-001` C11 审核。

### VDC-PROGRESS-20260915-023 — explicit late-command readback

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`、`VDC-VERIFY-001`；状态 IN PROGRESS。本条
  只增加迟到处理的板端可观测性，不改变 wire 契约、锁相状态或 C11 审核状态。
- 实现：Domain 新增 `follower_late_command_count`，仅由 Core1 的超过最大迟到窗口
  分支递增；`SYSTem:SYNC:VDC:DPLL:ROLE:STATus?` 在原字段末尾追加该计数。离线
  `dpll_observation_capture` 解析器和回归测试同步更新，因此 STOP 后可以区分非法命令、
  陈旧命令与明确的迟到退休。
- 软件验证：`run_vdc_domain_tests.ps1`、`run_host_unit_tests.ps1`（38/38）以及
  DPLL/VDC Python 回归（42/42）通过；固件双应用、Boot、USB namespace、flash map/link
  构建通过。
- 当前源码 P3 已重新运行，build `20260915005037` 仍在五板 OTA 阶段因缺失序列号
  `839E1AE79EA20F31` 失败，原始证据保留于
  `out/HardwareAcceptance/20260915/p3-085031/`。该结果不构成四板计数或锁相证据。
- 下一 gate：恢复缺失单板后重跑当前源码 P3；四板 STOP 后必须读回 late/invalid/stale/
  apply 与 resident fragment/command 计数，再进行 `VDC-CMD-001` C11 交叉审核。

### VDC-PROGRESS-20260915-024 — 四板当前源码 P3 与 resident command apply 对账

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-003`、`VDC-CMD-004`、`VDC-VERIFY-001`；状态
  IN PROGRESS。本条只记录四板真实验证和阻塞定位，不改变命令契约、registry 状态或
  锁相结论。
- 四板 P3：使用当前源码 build `20260915005602`，四块板为
  `0010071E65B5CB38`、`FB276192BEF9CCE1`、`2BD5090FE009FA2A`、`A1E549202D18ED6A`。
  `run --tdma-only` 完成四板 OTA、P0T、校准、TRN-00/01/02/03、process-image/FIFO
  和 STOP 后冻结记录；receipt 的 `strict_gates_passed=true`、`failures=[]`，不含 NO5
  DPLL 观测。原始证据：
  `out/HardwareAcceptance/20260915/four-board-tdma/`；该 receipt 属于
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，不能提升为正式锁相。
- 四板命令应用试验：复用同一 TRN-03 矩阵，以一主三从启动短运行窗口；运行期间未做
  串口采样，STOP 后统一读回。证据位于
  `out/HardwareAcceptance/20260915/four-board-command-apply-r2/`，原始对账为
  `command-apply-readback.json`。NO1 读回 MASTER，NO2--NO4 读回 FOLLOWER/source
  slot 0，四板 STOP 响应均为 `OK`。
- 结果：TDMA resident process-image 的普通 transport 计数继续增长且没有 transport
  reject；但四板 `vdc_command_fragment_rx_count`、`vdc_command_fragment_complete_count`、
  `vdc_command_accept_count` 均保持为零，三块从板的 `follower_apply_count` 也未增长，
  DCO follower 快照保持无有效更新。该结果说明当前共同时间/窗口准入尚未让 MASTER
  生成并发车 resident VDC command，不能把普通 TDMA 通过误称为命令应用通过。
- 失败边界：TRN-03 独立短窗的 startup barrier 仍超时，但 `left_running=true`；随后已
  由主控发送四板 STOP 并完成读回。该诊断失败和所有原始快照保留，未修改 OTA、未操作
  NO5、未清理另一设备的单板修改。
- 下一 gate：先从 `vdc_dpll_manager_map_local_to_common_time()` 的 hardware-latched
  observation/anchor 失败路径入手，补充运行态 TDMA status 的 common-time、correlation
  flags 和 resident command-record 是否建立的 STOP 后证据；修复或证明该准入后，再重跑
  四板 fragment/command/apply/DCO 对账，随后才提交 `VDC-CMD-001` 独立 C11 交叉审核。

### VDC-PROGRESS-20260915-015 — RX scan/drop 根因审计与验证基线复核

- TODO task ID：`VDC-TIME-002`；状态 IN PROGRESS。本条只记录当前源码审计和软件验证
  基线，不改变 TDMA/VDC 契约，也不授予 observer、首帧或锁相资格。
- `tdma_pio_spi_phys_rx_scan.inc` 当前在 `produced - scan_produced > keep` 时将游标前移
  并递增 `rx_observation_drop_count`；`keep` 由调用方 `max_words` 加观察扫描余量构成，
  因而在两帧已完成而单次请求上限小于物理帧跨度时，会把“为保护环形覆盖而丢弃旧前缀”
  与“首帧交接丢失”合并计数。该计数不能直接解释为物理首帧丢失，下一切片必须先把
  backlog/clamp、scan cursor 和 observer 首 ordinal 分开记录，再评估是否需要增大保留窗口；
  不放宽 sequence 或身份门禁。
- 当前全量 host/real-C 回归基线为 1655 passed、1 skipped、2 failed、15 errors。失败/错误
  集中在已有的 event-service/fixture 提取和 latch/origin 组合测试（例如 fixture 未提供
  `tdma_rx_start_cut_monitor`、`tdma_pio_spi_phys_event_selected`），未形成新的生产硬件证据；
  该结果保留在 `out/pytest/runs/`，不能用来替代本轮 52 项专项通过结果。代码切片未提交，
  不覆盖另一设备的单板修改。
- 下一 gate：继续 `VDC-TIME-002`，先为 scan backlog 建立不增加实时等待的分层计数和真实 C
  负测，再回到“STOP 完整退休后冻结 geometry、下一 ARM 显式 generation 选择”的 observer
  预启动实现；`VDC-TIME-003/004`、DPLL 正式锁相仍保持 PENDING。

### VDC-PROGRESS-20260915-014 — 板端有限发帧与最早原始前缀保留

- TODO task ID：`VDC-TIME-002`；状态 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260915/dpll-origin-bounded-burst/`。以下数字均为本轮快照，
  非事实源；本切片关闭有限发车及最早 raw 前缀保留，不授予 packet/event 身份或锁相。
- `TDMA_RING_FLAG_DIAGNOSTIC_BURST_MASK` 在既有 config.flags 中编码下一 ARM 的诊断
  额度。`SYSTem:TDMA:RING:BURSt` 只在 STOP applied 后配置，零保留普通无限行为；
  非零只允许 diagnostic reference 的 physical process-image。Core1 adapter 在
  phys_tx 接受后、FSM 操作前记账；耗尽后继续完成与 RX，跳过 TX 准备及自主准入。
  未增加 PIO 指令、SM、DMA、帧缓冲或 Core1 等待。
- 重复 START 不填充额度；TRAIN、adapter/runtime/physical 自主入口均拒绝有限模式。
  STOP 保留计数，新 ARM 成功才重置；ARM 失败使 diagnostic_burst_valid 失效，避免
  新配置与旧计数混用。独立审查发现的物理公开入口防御缺口及失败 ARM 读回混配已修复，
  `reviewer-code-r1.json` 支持上述代码边界；不将耗尽或 STOP 后 pending 清零当完成。
- 最终 `host-r2` 52 项通过，包含真实 C adapter/runtime、Core0 生命周期与实际 SCPI
  callback，覆盖忙时拒绝、物理已发后 FSM 失败、同序号 bootstrap、取消重臂、失败
  ARM、重复 START 与自主互斥。早期 host-control-r1/build6-r1 在复核修复前已通过，
  保留原件；最终构建为 build6-r2/build8-r1。
- 6/8 节点 A/B 构建通过，静态 RAM 增加 8 B，扣 heap 后余量为 17148/13388 B；
  SCRATCH_X 数据仍为零。源码指纹为
  `254f0fd49d2a3ea93550164f57ea9e9cb2ddf2f96611b21448c8982e7fe265ac` / 1098；
  六节点包 SHA 为 `f40c893ceaf3c16881bf698edc17c231b17efc760e31e6fd6640ddd2c2f89c99`。
- 当前四板 P3 用时 178.171 s，quick passed、strict_gates_passed=true；普通短帧
  passed/closed_loop/realtime_gate/diagnostic 全 true。STOP 后四板 native SD 读回与
  SRAM 逐字节一致。本轮仍为整表 1500 µs、TDMA 预算 850 µs，不证明 500 µs 达标，
  也不追认 013 的严格校准失败或完成 DPLL 验收。
- 专项预声明主板额度为两次：全板 ARM 无 START→STOP/ACK→重臂，从板预置采集，
  主板 START 后留复制时间，再重复 START，最后全板 STOP/ACK。主板实际接受/完成
  均为两次，TX timeout、clock/data timeout、recovery 无增长；RX_GATE_REJECT=7
  原样保留。三从各自 pre-produced=0、capture-produced=346、retained=346、最终
  produced=346，F=173，首 raw 坐标为零，复制期间没有后续流量覆盖。没有 RUN 查询。
- 最早 raw 前缀已经保留。固定 `[0,F)`、`[F,2F)` 分区各得到一个 168 B 合法
  packet，三板两帧内容 SHA 一致，sequence=1、hop=0，identity/transport CRC
  原样通过。header 起点分别为 24/13/2 bit，对应本轮 physical A/b=3/0、1/5、0/2；
  这些偏移是位相几何，不是首帧丢失。原始证据与输入哈希见
  `first-prefix-diagnosis-r1.json`。这仍不证明同步 CS 绝对锚、observer ready 或
  连续 event 身份：每板 `rx_observation_drop_count=1`、scan produced 小于 DMA
  produced，DPLL trailer=0，quota2 重复 sequence=1 也不能替代连续性。
- 三从通用 SAVE 再次在各自 12 s 截止时仍 RUNNING，专项 exit=1 保留。之后只读确认
  同一 job 7 已 DONE，原文件 2110/2126/2144 B 成功下载，node/build/generation/epoch
  核对一致；没有重新 SAVE、重新采样或延长原截止门。保存延迟原因仍未确定。
- 下一 gate：继续 `VDC-TIME-002`，将已验证 A/b 作为本轮 observer provenance，先处理
  scan/drop 和重复 bootstrap 的连续性，再实现有代际的训练几何冻结及 ARM 前 observer
  准备。不能复制旧
  DMA 原点的 A/b 后假置 alignment locked，也不能以两帧有限诊断代替连续 resident
  闭环。TIME-003/004 仍 PENDING，正式时间映射、命令应用及实际输出锁相继续未完成。

### VDC-PROGRESS-20260915-013 — 首次 capture CS 保护与首帧采集缺口

- TODO task ID：`VDC-TIME-002`；状态 IN PROGRESS。设计证据位于
  `out/HardwareAcceptance/20260915/dpll-observer-prelaunch-design-r1/`，实现、测试与
  硬件原件位于 `out/HardwareAcceptance/20260915/dpll-initial-cs-gate/`。以下数值
  均为本轮快照，非事实源；本切片未授予正式时间戳或 packet/event 身份。
- 独立设计审查支持先保护首次 capture：原程序直接进入 WAIT SCK，CS 高期间不足
  一字节的 SCK 也可能改变位相而 DMA 仍为零。`tdma_pio_spi_phys_arm()` 在全部
  seed/PULL/latch rearm 后、enable 前，仅对 process follower 注入一次 WAIT RXCS
  low。未增加 CPU 等待、静态状态、PIO 指令槽、SM 或 DMA。既有 STOP 成功退休后
  restart 清除挂起指令；CS 已低或 disabled 期低脉冲仍可能消费 WAIT，不授予 clean。
- 完整预启动路线仍需独立冻结训练几何及稳定配置/时钟/校准绑定，复用 config 发布
  链与既有全板 ARM ACK。物理 owner 在 START 前已有服务机会；不得抢占周期专用的
  stopped_update token，也不得假置 overlay alignment locked 来绕过训练。
- 软件：`host-r2` 336 项通过；真实 C PASS 命令与组装/重定位 PIO 模型覆盖首次 CS
  前的零散时钟、完整首字节和后续帧。三个负例分别在帧外采样、首位相位偏移、缺失
  首位时精确失败。模型不覆盖 disabled 期脉冲、跨板同步器、DMA 延迟或目标 PC 恢复。
  `review-code-r1.json` 无源码/模型阻塞；目标首帧资格明确保留。
- 6/8 节点 A/B 构建通过，静态 RAM 增量为零，扣 heap 后余量 17156/13396 B，
  SCRATCH_X 数据为零。源码指纹为
  `8911a9576f2b59f6ee62a4a71c89cf947a7622886e0ec25ab5abb4661f41bca8` / 1097；
  六节点包 SHA 为 `ebf1daef696679f448427c6d827a5ba79a9e25622477daa792edcbc1f76d48a3`。
- 当前 P3 流程耗时 178.079 s，quick passed，但 strict_gates_passed=false：保留
  NO2 粗 CLK 校准的 OPMODE APPLY 超时及 Execution error。后续普通短帧
  passed/closed_loop/realtime_gate/diagnostic 全 true；四板各 14 槽及 baseline，
  missed/reason 为零，STOP 后顺序 SD 读回与 SRAM 逐字节一致。实际整表为 1500 µs、
  TDMA 预算 850 µs，不能称 500 µs 达标。没有专用 ARM 耗时测量，不用稳态峰值代替。
- 首帧专项执行了全板 ARM 无 START→STOP/ACK→重臂；采集前三从 capture PC 为 4、
  DMA produced 为零。三从预置 SCK 采集后，主板短时 START/STOP，从板继续复制，
  全板 STOP/ACK 后才查询/保存。RUN 无 SCPI 查询，NO5 未操作。
- `first-frames-r1.json` 未通过首帧资格：主板 START/STOP 响应分别为 62/63 ms，
  实际完成 22 帧；三从快照定界时 produced=1038，仅保留 512 B，首坐标已为 526；
  最终发布 produced=3806。首帧已丢失，复制期间也不能排除覆盖。没有重采挑窗。
  SCK 样本为 256×4 ns，首高样本为 8/11/11；这只是采样起点后的偏移，没有同步
  CS 样本与物理 ARMED 回执，不能称精确 CS→首 SCK 门限或首帧 DATA 通过。
- 通用 capture SAVE 在各自 12 s 截止时仍 RUNNING，原件保留。之后一次 STOP-only
  恢复读取确认原 job 均 DONE，无再次 SAVE，三份原生 SD 文件成功下载并核对
  generation/epoch。恢复不追认先前截止门；通用保存延迟原因尚未确定，不能归因 OTA。
  `analysis-r2.json` 使用真实 realtime_gate_passed 字段，r1 的空字段保留并更正。
- 下一 gate：仍在 `VDC-TIME-002` 内先实现或复用 owner 有限发帧/最早帧保留能力，
  补齐首次 DATA、CS/SCK 建立时间、STOP 取消及目标恢复证据，再进入冻结几何与早启
  observer 集成。不得在健康 RUN 暂停/abort DMA 强求边界；串口 ACK 不能决定帧数。
  本次普通环路通过不关闭首帧验收、严格校准、身份或锁相缺口，TIME-003/004 仍 PENDING。

### VDC-PROGRESS-20260915-012 — 首帧坐标证明与启动路线收敛

- TODO task ID：`VDC-TIME-002`；状态 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260915/dpll-rx-coordinate-proof-r1/`。本轮仅做只读
  源码/官方硬件文档审计及离线数学模型，未修改固件、PIO 或执行新硬件采集。
  以下数量与页码为快照，非事实源；011 已分离提交并封存，520 件文件独立复验通过。
- 条件性结论：若两次完成写计数处于同一已证明 idle、未完成字数上界为 `Q_i`，
  且物理边界余数 `r` 与跨度 `F` 独立已知，则边界位于
  `[max(C_i), min(C_i+Q_i)] ∩ (r+Fℤ)`。区间宽度小于 `F` 时至多一个候选。
  `coordinate-interval-model.json` 的 189800 组有限整数枚举通过，并用六份原始
  cut 做假设上界敏感性分析；模型没有证明目标硬件的 Q、idle 或 r，不能直接准入。
- 官方 RP2350 datasheet §12.6.4.2 说明 DREQ credit 在 transfer issue 时扣除；
  已读出 FIFO、尚未完成 SRAM 写入的数据仍在内部流水线。外设 FIFO 深度及 credit
  寄存器宽度不能直接当作总 Q；§12.6.7.3 的错误后抑制/地址偏移上界也不能当作
  正常流水线总容量。官方 PDF、哈希和逐页摘录保留，本轮未获得足以关闭 Q 的证明。
- 当前绝对 PC 5 为 capture 程序反复执行的 WAIT SCK high；IRQ3 为 service
  读后清的粘性观察，不是精确物理计数。当前完成写数已超过一帧，在合法初始命令及
  无重启等前提下可证明曾经过首个 terminal；仍须证明其后没有漏钟、额外采样或
  命令停顿才能归纳边界余数。启动 cut 的 SM2 RXSTALL/TXSTALL/RXUNDER/TXOVER
  均清，但当前运行监测只将其中 RXSTALL 纳入退休，不能据此证明整个后续窗口。
- 时序缺口：首次 DMA count 在 start_pad_before 之前读取，末次 count 在
  start_pad_after 之后读取。即使排除两个高电平 pad 样本之间藏入完整帧，也不能
  自动把外侧两次 count 限定在同一 idle；首读可能仍在前帧尾，末读可能已到下一帧。
  必须补齐见证或另建包含新增输入字数的保守模型，不能把当前等计数当作精确边界。
- 后继优先评估受控首发路线：训练后完整 STOP/取消，冻结与当前配置/时钟代际绑定
  的几何描述；ARM 阶段先使从板 capture 和 observer 就绪并证明零起点，再允许
  origin 首次发帧。复用现有全板 `ARM_CONFIG_APPLIED_ACK` 和 started barrier；
  本轮原件已有全板 ACK，不重复建设第二套确认。现有 START 文本响应不能替代
  observer 就绪证明，现有 observer 仍在训练后晚启用，旧窗口不能追认零起点。
- 下一 gate：完成该 ARM 前准备方案的 owner 状态、代际、静默、取消和迟到拒绝
  审查后，才实现最小诊断切片。不得在健康 RUN 中暂停/abort DMA 来强求边界；
  不新增忙等，不借用其他域 PIO/DMA。当前身份、正式时间戳和锁相仍未证明，
  `VDC-TIME-003/004` 保持 PENDING；本设计不冻结跨域契约。

### VDC-PROGRESS-20260915-011 — 首次 observer 启用的 DMA 坐标括号

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`；状态 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260915/dpll-rx-start-cut/`。以下数量、时间、容量与
  代际均为本轮快照，非事实源；本切片只补充诊断，不授予物理身份或时间戳。
- TDMA owner 在首次实际 observer enable 两端记录 DMA completed-write count、
  epoch、计时、capture FIFO/PC/pad/fdebug 和配置。使用真实 counter 的局部副本
  lift 坐标，不推进 live scanner/drop accounting；capture RXSTALL、DMA 故障、
  epoch/geometry/role/clock 变化或 observer 失效只退休 cut。首次 STOP 后终止
  运行监测，完整 disarm 的两项取消 ACK 成功后冻结 STOPPED；新 ARM 先清旧代。
- `tdma_rx_start_cut_t` 为 128 B，owner、双缓冲及控制标量共新增静态 RAM 396 B。
  `SYSTem:TDMA:FLIGHT:RX:CUT?` 通过有界一致性 getter 读取冻结副本；getter
  不读 live PIO/DMA，但读取系统计时器。全板 UID/build/STOP/config ACK barrier
  后每板只查询一次，原始错误不重试。cut 单独导出到主机，不在 native SD 记录内；
  通用 event snapshot、原生每槽 schema 和 PIO/DMA 分配保持原样。
- 软件：`host-export-r2` 13 项、`host-adjacent-r1` 163 项、`host-owner-r1` 4 项、
  `host-cut-unit-r2` 1 项通过；覆盖真实启用/service/STOP、feature OFF、真实
  counter/lifecycle、并发读、SCPI 序列化及全板 STOP barrier。失败原件与 fixture/
  host facade 修正见 `tooling-notes.json`，不称生产内存协议因此被修复。
- 构建：6/8 节点 A/B 均通过，用时 18.953/16.828 s；源码指纹为
  `8654c754bdc2cc39f2f4f0c2a256649028e23ad4b4cfb81c6c085ba8305d13e1` / 1096，
  六节点 package SHA 为 `6c96a3f6290e6e7b9aeff406f71d7b846709f08d3df2739c423b6f4609b95074`。
  四份 map 净增均为 396 B，6/8 节点余量 17156/13396 B（已扣 heap）。DATA/BSS
  间隙仍 16 B，SCRATCH_X 数据为零；monitor/getter/SCPI 回调自身栈 96/48/152 B，
  新函数在 XIP。该局部栈不代表完整调用链或 WCET；归档 ELF 的内联启动顺序另审。
- 当前 P3 用时 184.219 s，quick 与 strict 均 true、diagnostic_failures 为空；
  普通短帧 passed/closed_loop/realtime/diagnostic 全 true。四板各 14 槽加
  baseline 完整，SD 保存读回 8.188 s，逐字节及 SHA 与 SRAM 一致。三从全窗
  query/matched 增量为 678/675、693/691、707/705，其余为 unavailable，
  stale/missing/ambiguous 均未增长。本轮通过不追认 010 的严格失败，也不能归因
  cut 修复了校准。
- 普通 cut：NO1 为预期 UNAVAILABLE；三从均记录成功，flags 127，retire_reasons
  仅 STOP；observer epoch 1、ARM 15、DMA epoch 1/1，物理跨度为 173 words。
  alignment byte/bit 为 3/0、1/5、0/2；两端 produced 分别均为 519、346、346，
  FIFO 均零、PC 均 5，capture SM2 RXSTALL 未置位。整个启用括号耗时为
  12.040/12.028/13.736 µs，仅为该括号，不是完整 observer 启动或 monitor WCET。
- 唯一自主 `capture-r1` 用时 39.782 s、准入 ACCEPTED/epoch 36，config/applied
  初末 70/70、model 初末 24/24；NO1 slots 3–17 为 persona 16，STOP 自主相位
  累计 6397。四板各 18 槽加 baseline 完整、missed/terminal reason 均零；SD
  保存读回 8.000 s，与 SRAM 字节/SHA 一致。RUN 无 SCPI 查询采样。
- 自主窗口三从 event epoch 2，published=joined 为 7098/7180/7257、无 INVALID，
  最大服务间隔 1576/1586/1579 µs。全窗 query/matched 增量为 1894/1891、
  2109/2107、2379/2377，余项仅 unavailable；这些增量包括准入前普通阶段。
  cut 的 observer epoch 2、ARM 16、两端 DMA epoch 1/1，STOP only，括号为
  12.044/12.028/12.044 µs。cut 在从板 observer 初启时取得、早于主板自主 grant，
  不能称自主切换边沿锁存；FIFO 空且 count 相等仍不能排除在途写或启用前积压。
- 自主总门仍失败，diagnostic 为 true；startup 三个健康样本在
  1.009730/1.510902/2.006068 s，第三个超过预声明门限。NO1 仍保留
  `adapter_tx_not_growing`、`physical_flight_persona_mismatch`，soak 保留
  `periodic_interval_gate_failed`；未重采挑窗或放宽门限。
- 时间反馈以 `timing-feedback-r2.json` 为准，r1 复制来的两条过时说明保留并纠正。
  按内部 total_ticks 选取的 PEAK 外层 NO1–NO4 为
  1578.616/1097.692/1076.596/1118.852 µs；NO1 超过整表周期。该记录是
  513→1537、trial 0→36 的自主准入迁移，010 所选记录为准入前迁移，两者不能
  隔离本轮 cut/monitor 成本；嵌套 stage 不可求和，PEAK 也不是全窗外层最大。
  完整 observer service_max_us 普通为 315/325/395、自主为 424/427/425，
  包含 startup 与完整服务，不等于 monitor 独立成本；全表 WCET 仍未闭合。
- 下一 gate：`VDC-TIME-002` 证明 pending DMA writes、prestart backlog 与唯一
  物理边界，再建立 capture coordinate 到 event ordinal 的关系；当前所有 cut
  恒为 unresolved、inflight unknown、prestart backlog unexcluded。物理身份、
  正式时间戳及锁相均未证明，`VDC-TIME-003/004` 仍 PENDING。

### VDC-PROGRESS-20260915-010 — 自主准入只读 foundation 身份

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`；状态 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260915/dpll-origin-foundation-read/`。以下数量、时间、
  容量与代际为本轮快照，非事实源；本切片解除准入对无关诊断快照可用性的依赖。
- 实现：新增 `tdma_service_get_foundation_crc32()`，在既有 intent guard 下有界
  读取实际 CRC；首末 acquire、标量读取及中间 acquire fence 保证版本复验。
  上界沿用 `TDMA_SERVICE_SNAPSHOT_RETRY_LIMIT`，不等待锁、不调用综合快照。
  空指针或尝试耗尽返回 false，并清零非空输出；成功返回的零值只是实际 CRC，
  不能当作配置有效或授权，模型匹配仍由原准入谓词验证。输出不能 alias service。
- Calibration 仅用该标量替代整个 `tdma_service_snapshot_t`。综合诊断快照函数、
  foundation 写入路径与 Core1 授权实现不变；保留 revoke-before-attempt、参数、
  角色、stage、cadence、余量、expiry、model 及最终 config/model 复验。Core1
  在准入、构造及运行边界仍比较实际 foundation CRC；无新增实时 PIO/DMA 工作。
  这有意改变读取可用性的依赖，不能称新旧函数在所有忙碌情形下返回行为完全相同。
- 软件：`host-admission-r1` 20 项通过，保留冻结旧函数的 47 场景 oracle，仅在
  owner 可用性等价时比较 grant、epoch、逻辑调用顺序和拒绝原因；另对全部场景
  注入无关快照不可用，验证实际门禁仍生效。真实 SCPI 回调在成功时才返回偶数
  epoch，失败保留拒绝并撤销旧授权；实际 Core1 stale/prepare 路径新增 foundation
  变化拒绝。getter 的锁与代际行为由独立生产函数测试覆盖，不由该 model stub 证明。
- 并发：`host-foundation-r2` 通过，五组、16 次成功检查使用真实 service/scheduler/
  registry/ring 结构与生产读取函数。实际 scheduler.lock 占用使综合读取失败，
  专用读取仍成功且不释放锁；result/registry/ring 的不可用不再影响标量读取。
  覆盖 NULL、零 CRC、odd guard、复制后 writer、持续代际变化、末次尝试及单次
  guard 回绕；耗尽最多 128 次 guard load，无陈旧输出替代。r1 漏链接
  `tdma_profile.c` 的失败原件保留，补真实依赖后通过；未声称实板争用时序已穷尽。
- 构建：6/8 节点 A/B 均通过，用时分别 15.890/15.250 s。当前指纹为
  `eaba0fbd9ec75f7276cb5b5b5301804204dd6b96950c4e29072ab98e925c28bd` / 1091；
  build ID 沿用 `20260914184059`，六节点 package SHA 为
  `e216cfffa428e20f3f1d7381d8c0abb1ce92c011770052b8d04ba7e1900ace5c`。
  `current-plan-r1.json` 的源码与构建归档分开绑定，不以 build ID 代替指纹。
- 资源：四份 map 静态 RAM 未增加，6/8 节点余量仍为 17552/13792 B（已扣 heap）；
  DATA end 为 `0x20008ff0`、BSS 为 `0x20009000`、SCRATCH_X 数据为零。
  实际 ELF 中 getter 位于 Flash，指令为 acquire load/scalar load/DMB/acquire
  load，SU 为 4 B。准入函数自身栈从旧 ELF 的 3088 B 降至当前 2288 B，减少
  800 B；综合快照自身为 936 B。仅为函数局部栈，不能推断完整 Core0/SCPI 栈
  高水位或 WCET。证据脚本路径选择和同名报告冲突另见 `tooling-notes.json`，
  成功编译/反汇编产物保留，未覆盖重跑。
- 当前 P3 用时 188.641 s，quick passed、普通短帧 passed/closed_loop/realtime/
  diagnostic 全 true；四板各 14 槽加 baseline 完整，STOP/ACK 后 SD 保存读取
  8.094 s，与 SRAM 一致。普通三从 query/matched 增量为 678/675、692/690、
  707/704；NO4 missing 增长一次，其他未匹配为 unavailable，不能将其删除或
  称逐包关联无缺口。稀疏快照未直接记录这次 missing 的单次现场。
- 严格失败：`strict_gates_passed=false`，NO3 coarse CLK level 7 的
  `SYSTem:TDMA:RING:TOPology 4,2,2` 返回 timeout/Execution error；coded marker
  的四个 trial 原件完整，但 NO4 best/second distance 同为 242、margin 为零，
  `mixed_peak` 门失败。两者均保留，不能以 ordinary passed 代替严格校准。
- 唯一自主 `capture-r1` 用时 39.782 s，准入 ACCEPTED，epoch 36，初末配置
  66/66、初末模型 22/22，observed_mask 为 1023；NO1 persona 从普通态转至
  16，并在 slot 3–17 保持，STOP profile 中自主相位计数为 6418。四板各 18 槽
  加 baseline 完整，terminal reason/missed 均为零；字节数依次为
  8592/11532/11600/11564 B，未满固定记录区。全板 STOP/ACK 后 SD 保存读取
  8.188 s，UID/build/epoch/CRC 可解码，SD 与 SRAM 逐字节一致；RUN 内无 SCPI
  查询采样，NO5 未操作。
- 原生诊断：三从 event epoch 为 2，published 为 7126/7192/7271，均与 joined
  一致、无 INVALID，最大服务间隔为 1586/1581/1585 µs。整个记录窗口的
  query/matched 增量为 1914/1911、2114/2112、2378/2376；剩余分别为
  3/2/2 次 unavailable，stale/missing/ambiguous 未增长。该增量包括准入前普通
  阶段，不能全部归为自主匹配；baseline 的旧 RETIRED 候选也不能当本次查询。
  候选与记录字段检查通过，仍是历史诊断，无物理身份、时间戳或 DPLL 正式资格。
- 总门仍失败：第三个健康 startup 样本最晚 2.009369 s，超过预声明 2 s；前两个
  健康样本在 1.007962/1.509057 s。NO1 还保留 `adapter_tx_not_growing`、
  `physical_flight_persona_mismatch`、`periodic_interval_gate_failed`。
  `analysis-r1.json` 诊断连续性为 true 仅证明当前记录范围；未追加采集、未放宽
  时间门、未以本次准入成功追认 009 或确定其具体失败子读取。
- 时间反馈：按内部 total_ticks 选取的 STOP PEAK，其外层相位耗时 NO1–NO4 为
  1443.404/1098.596/1045.568/1076.392 µs。NO1 该记录仍在自主 trial 前的状态
  迁移；其他板与 009 的流量模式不同，不由数值变化推断 getter 的独立性能收益。
  PEAK 不是全窗外层最大值，完整静态表 WCET 仍未闭合。
- 下一 gate：`VDC-TIME-002` 继续补齐物理 packet/event 身份、首事件/DMA anchor
  与晚启用积压排除；保留 startup、普通计数/persona 与严格校准失败分别追踪。
  `VDC-TIME-003/004` 仍 PENDING，不将完整诊断记录提升为正式时间输入或锁相。
- 后继只读设计：
  `out/HardwareAcceptance/20260915/dpll-rx-identity-design-review-r1/identity-design-review.json`
  核对当前 observer=ON 的 follower TX 为 32/32 指令、4/4 SM，RX 指令也为
  32/32；旧 observer=OFF 的 TX 余量不能借用于当前配置。建议下一切片先记录
  首次 observer 启用前后的 DMA 完成坐标区间及 capture RXSTALL，保持 PIO/SM/DMA
  资源不增；FIFO 为空或两次 count 相同不能单独排除在途 DMA 写入，CPU 观察不能
  冒充边沿锁存。先证明首帧边界、积压与丢字排除，再考虑坐标/ordinal 关联；
  未唯一的区间明确保留 unresolved，不授予物理身份。该报告是待实现设计，
  不代表新增原型、资源预算或门禁已经验收。

### VDC-PROGRESS-20260915-009 — READY 有界事件候选关联与原生记录

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`；状态 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260915/dpll-rx-event-candidate/`。以下容量、数量、时间、
  代际和资源均为本轮快照，非事实源；仅验收诊断候选的软件、目标资源和普通模式路径。
- 实现：TDMA physical owner 在私有 DMA copy 交接时 pin observer epoch；RX station
  在既有 current/age/config/map/timestamp 及 Core0 decode 门通过后，于 READY
  release 前查询一次。仅凭据 flags 恰为 `TDMA_RX_CAPTURE_PRIVATE_COPY` 才进入
  pin/query；有凭据但 pin 为零时明确拒绝，READY 不借用最新 observer epoch。
  普通 origin 回传也可能具有私有凭据，并报告 UNSUPPORTED；自主 origin 和 legacy
  无凭据路径跳过。查询返回 void，不改变 RX 结果或原有时间戳/DPLL 准入。
- 失效边界：event start 绑定 DMA ARM，STOP/prepare/INVALID 退休；同时复验
  capture ID、DMA observation epoch、observer pin、独立 history.active 和原始
  DMA copy 范围。非零 bit shift 必须计入额外 word，精确覆盖边界拒绝。最多查询
  `TDMA_EVENT_HISTORY_CAPACITY` 项，不新增 MMIO、FIFO harvest 或等待；BUILDING
  取消仍等待 worker ACK，不能提前覆盖输入。
- 候选通过现有版本化双缓冲交给 Core0；ACTIVE query 仅置 dirty，随下一次已有
  event publish 发布。快照保留历史查询上下文，退休置 RETIRED；历史 MATCH_PRESENT
  不等于当前 live lease。event sequence 与 packet sequence 分别保留，禁止用
  packet 字段伪造 event 身份；`TIMESTAMP_VALID`、`DPLL_ELIGIBLE` 始终不置位。
- 原生记录 V3 只追加 candidate 组，保留 V1/V2 decoder。目标快照为 453 words，
  candidate 结构 176 B、序列化 172 B，bitmap 15 words，完整 sample 最大 1920 B。
  Storage 固定 16 KiB 未增加；窗口完整性以实际 terminal/长度核对，不能保证所有
  字段每槽都变化时仍装得下整个窗口。新 decoder 重解 008 四板 V2 原件逐字段一致。
- 软件：最终 `host-station-r2`、`host-service-r1` 真实调用路径通过；record/collector
  28 项、legacy schema 固定 SHA 2 项通过。`host-physical-r1` 3 项通过，包含
  七组真实 pin/query 场景、64 次 query oracle、实际 start/STOP/service/final fault、
  disabled 编译分支及 candidate 64 位字段/退休 flags 撕裂注入。SDK/MMIO facade
  不能替代实板物理身份。原 guard copy 函数体不变，仅移至公共编译分支。
- 资源：6/8 节点 A/B 全通过，净静态 RAM 增加 716 B，余量 17552/13792 B（已扣
  heap）；DATA end 为 `0x20008ff0`、BSS 为 `0x20009000`，SCRATCH_X 数据为零。
  目标 station 为 904 B、pin offset 为 28；event/physical snapshot 为 320/768 B。
  pin/query 位于 Flash，函数自身栈为 12/160 B；async/rx_ex/rx_once 为
  112/64/416 B。Core0 record_sample/app_record_snapshot/record_service 为
  3800/2984/128 B，局部数组较旧版共增 352 B；这些 SU 数字不能当完整调用链或
  实测高水位，Storage task 配置以 `app_tasks.c` 为事实源。
- 身份：`current-plan-r1.json` 绑定源码指纹
  `9d35742b80ae5acc785ebfb892c1d133b21a58f72f26efc1b30594a4691fd170` / 1090。
  build ID 沿用 `20260914184059`，六节点 package SHA 为
  `099fb3d97ba498ca0a1e4547e860a5e28f9e9344654829eba391dbf27ef684df`；归档源码、
  四份 map、目标 ABI 和原始 SU 已独立核对，不能仅凭 build ID 识别固件。
- 当前 P3 用时 184.078 s，quick passed 为 true，普通短帧 passed/closed_loop/
  realtime/diagnostic 全 true。`strict_gates_passed=false`：coarse CLK level 7
  在 NO3 执行 `SYSTem:TDMA:RING:TOPology 4,2,1` 时 timeout/Execution error，
  原因未闭合，不能简称物理时钟校准精度失败。普通原生窗口四板各 14 槽加 baseline
  完整，STOP/ACK 后 SD 保存读取 8.078 s，逐字节一致。
- 普通候选：NO2/NO3/NO4 query 增量为 678/693/706，matched 为 675/691/704，
  unavailable 为 3/2/2，stale/missing/ambiguous 均未增长。各从板记录直接捕获
  13 个不同 matched query；其余只由计数反映，不是逐包 trace。NO1 query 720 次
  均 unavailable；`candidates-p3-r1.json` 字段一致，未开放正式资格。
- 唯一自主采集 `capture-r1` 用时 29.094 s，在 TRIAL 命令返回 timeout 后提前
  STOP。NO1 admission 原件为 reason 14（`CALIBRATION_ORIGIN_ATTEMPT_OWNER_UNAVAILABLE`）、
  trial epoch 34、config/applied 63/63：owner 快照读取失败，observed_mask 为 15，
  尚未执行 OWNER 后的 cadence/model/recheck；不能把未记录的模型代际零解释为
  模型失效。拒绝路径压入 Execution error，不返回数字 epoch，主机等待数字约
  0.511 s 后显示 timeout；这不证明串口故障或准入计算耗时。handoff 为 UNAVAILABLE。四板
  autonomous_phase_count 全为零，不能声称已进入自主模式或测得自主候选匹配。
  请求各 18 槽，实际 NO1 4 槽、三从各 5 槽加 baseline，terminal reason 为 1、
  missed 为零，collection 均 false；保留提前终止，不追加择优轮次或放宽 timeout。
- 部分窗口：三从 query/matched 增量为 399/395、416/414、429/427，仍属普通模式；
  baseline 保留上一代 RETIRED 候选，随后当前查询更新，不得混为当前 ARM 的匹配。
  `analysis-r1.json` 连续性和总门为 false，diagnostic 为 true 不能代替完整窗口。
  全部 STOP/ACK 后顺序 SD 保存读取 5.828 s；四板部分原件的 UID/build/epoch/CRC
  可解码，SD 与 SRAM 逐字节一致。RUN 内只发控制命令，未查询采样，NO5 未操作。
- 时间反馈：`timing-feedback.json` 的 PEAK 按内部 total_ticks 选取，该记录外层
  耗时 NO1–NO4 为 1466.680/952.660/949.436/881.064 µs；不能称全窗外层最大值。
  本轮未自主准入，调用组合、状态与 008 不同，不能以数值下降证明候选实现收益、
  自主稳态或全表 WCET；008 的 NO2 UNAVAILABLE 保留。
- 下一 gate：保持 `VDC-TIME-002` IN PROGRESS，先沿准入 reason 14 追踪拒绝条件，
  保留 NO3 严格拓扑失败并区分是否相关；同时只读收敛 packet/event 物理身份、
  首事件与 DMA anchor、额外 word 覆盖和退休证明。后续实现必须先闭合对应门禁，
  不将 sequence 相等提升为正式时间戳。`VDC-TIME-003/004` 仍 PENDING；实际锁相
  未证明，OTA 实现及载荷池保持现状。
- 后续只读审计：`admission-next-gate-review.json` 确认准入只使用综合 owner 快照的
  foundation CRC，却同时依赖 result/registry/ring/scheduler 的可用性；scheduler
  使用一次 try_lock，争用是可达机制，但本次原件不足以确定失败于哪个子读取。
  最小修复候选为既有 intent guard 下的有界 foundation 专用读取，保留 revoke、
  所有实质准入谓词、最终 config/model 复验和 Core1 foundation 再验。物理身份还需
  独立 header 身份或硬件 event/DMA 坐标、晚启用首帧及积压排除、覆盖与 mailbox
  完整性证明；本轮只完成审计，未实现或验收这些后继路径。

### VDC-PROGRESS-20260915-008 — 私有报文携带 DMA 捕获凭据

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`；状态 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260915/dpll-rx-capture-provenance/`。以下容量、数量、
  时间及代际为本轮快照，非事实源；本切片只完成 private-copy provenance 基础。
- 实现：TDMA physical owner 的 `tdma_pio_spi_phys_rx_ex()` 同调用交付私有 packet
  与 `tdma_rx_capture_t`，记录独立 ARM/capture ID、本次 observation epoch、
  candidate、复制前后 produced、frame words、bit shift 及 persona。两个 ID 在
  本 boot 内不因物理对象初始化或重臂重复，饱和后只停止诊断资格；失败、旧 backend
  及自主 origin 不生成凭据。STOP/RX 重装/ARM 入口退休 ARM 有效位。
- 同次观察：沿用已有两次 DMA 水位观察和 header 复验，不新增寄存器读取。
  candidate/frame words 包括交付前剥离的物理包头；非零 bit shift 实际读取一个
  额外原始 word。`TDMA_RX_CAPTURE_PRIVATE_COPY` 只证明私有复制与 header 检查，
  不证明 transport/mailbox CRC 或物理事件身份。成功后的凭据整理使用本次已有
  局部值，不能重读水位或将后续状态冒充捕获现场。
- 交接：可选扩展 callback 只能在 STOP 修改，更换 backend 清除旧扩展。只有
  IDLE station 接收本次 packet/capture；Core0 解析私有副本并保留凭据。REQUESTED/
  BUILDING/READY 取消沿用 ACK 所有权规则，不提前覆盖 worker 字段。字段容纳于
  原 station/注入队列 union；本切片未查询事件历史，也未产生诊断候选 lease。
- 软件：`host-station-r2` 真实 adapter 路径通过，覆盖同取消代际下连续包 ID 不同、
  worker 暂停、副本不变、各阶段 STOP、脏输出清零和 backend 切换。初轮缺少
  `assert.h` 的编译失败保留。`host-physical-r3` 共 34 项通过，六组专项执行真实
  RX ARM/dispatcher/rx_ex/旧 wrapper，覆盖偏移、反向、ring wrap、覆盖/代际变化、
  容量失败、连续包、重臂及 ID 饱和。SDK/latch/autonomous facade 不证明物理边沿。
  event adapter 回归通过；event service 初轮 fixture 缺新成员，补公共结构后通过。
- RAM 收敛：r1 热段增加导致 BSS 跨过既有对齐边界，6/8 节点余量降至
  14172/10412 B。保留原件及 r1 实板 P3；r2 仅构建，六节点仍跨界，未部署。
  最终 r3 将成功后的整理移至 Flash helper，仅扫描控制函数使用 `Os`，保留 SRAM
  复制叶；删除由 dispatcher 保证的内部重复清零，公开失败清理不变。四份目标
  map 恢复原 BSS 起点，余量为 18268/14508 B（已扣 heap 预留），净增静态 RAM
  16 B，恢复 r1 的 4096 B；SCRATCH_X 数据为零。热段为 1040 B，目标函数自身
  栈 async/helper/rx_ex/rx_once 为 112/16/64/416 B，不能当全调用链高水位。
- 当前构建：`current-plan-r3.json` 绑定指纹
  `74c67aaa7de5c5597aa027ebab9b915498268f22e3032d6d86b450f39ede66fc` / 1088，
  6/8 节点 A/B 均通过。build ID 沿用 `20260914184059`，六节点 package SHA 为
  `d15405355b43f9b364fa01cbb98b991e819da4ff6ab06841887422f79cdf5188`；各版本
  源码、构建、反汇编及 SU 归档分开保存，不能用 r1 凭证放行 r3。
- 当前 r3 P3 用时 178.265 s，普通短帧 passed/closed_loop/realtime/diagnostic
  均为 true，`strict_gates_passed=true`、diagnostic failures 为空。四板各 14 槽
  加 baseline，STOP/ACK 后顺序 SD 保存读取 6.907 s，与 SRAM 原件一致。
- 预声明唯一自主 `capture-r1` 用时 39.422 s，准入 ACCEPTED，trial epoch 为
  36、初末配置为 70、初末模型为 24。四板各 18 槽加 baseline，collection 全通过；
  三从 epoch 为 2，published 为 7167/7239/7294，无 INVALID，最大服务间隔为
  1588/1588/1581 µs，RX/TX FIFO 最大均为 4 words。STOP/ACK 后 SD 保存读取
  7.406 s，四板与 SRAM 逐字节一致；RUN 内未用 SCPI 查询采样。
- 失败保留：自主 passed/closed_loop/realtime 仍为 false，第三个健康样本最晚
  2.016150 s，超过预声明 2 s startup 门；NO1 仍有 `adapter_tx_not_growing`、
  `physical_flight_persona_mismatch`、`periodic_interval_gate_failed`。diagnostic
  为 true 只表示原生记录和从板观察连续性。未追加轮次，未追认旧失败。
- 时间反馈：`timing-feedback.json` 按原始 STOP peak 对照前切片。PEAK 由内部
  `total_ticks` 选取；NO1 该记录的外层完整相位耗时为 1484.596 µs，并非已证明的
  全窗外层最大值，且发生在 trial 前状态迁移，不能称自主稳态；NO3/NO4 对应耗时
  为 1043.324/1032.832 µs，NO2 原响应 UNAVAILABLE。可用记录低于前轮同板记录，
  但调用组合和 cache 状态不同，不能据此证明 `Os`/helper 的独立收益、完整 WCET
  或全窗预算通过；缺失项不以旧值补齐。
- 下一 gate：继续 `VDC-TIME-002`，在 Core1 READY 边界验证捕获仍属于当前 ARM/
  observer 代际后执行有界 history 候选查询；显式处理覆盖、迟到、错序和 STOP/
  INVALID 退休。同 sequence 只作候选，FLIGHT_MUTABLE 头 CRC 不替代所有邮箱
  CRC；物理 anchor 与身份仍待证明。原生记录尚未导出逐包凭据字段，实板结果只
  证明本切片资源及原 TDMA/observer 连续性，`VDC-TIME-003/004` 保持 PENDING。

### VDC-PROGRESS-20260915-007 — 临时准入原因与失败采集留证

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`；状态 IN PROGRESS。证据根为
  `out/HardwareAcceptance/20260915/dpll-origin-admission-diagnostic/`。以下数量、
  时间、容量及代际是本轮快照，非事实源。正式时间戳和 DPLL 资格未改变。
- 实现：Calibration 的串行 Core0 控制路径记录最后一次已进入准入函数的尝试，
  按实际短路分支保存 reason、请求参数、已观察字段 mask、初末配置/模型代际及
  reason 对应的 observed/expected。`READ:CALibration:ORIGin:DIAGnostic?` 仅在
  STOP/ACK 后读取；helper 不可得只报告该层不可得，不用事后快照猜内部原因。
  STOP/REVOKE 保留历史；ACCEPTED 和 attempt 计数均不授予当前权限。
  SCPI 参数解析失败发生在准入函数之前，仍维持旧行为，不创建尝试或撤销旧 grant。
- 语义验证：`host-core-r1` 的三组真实 C/SCPI 测试通过，其中 47 个场景与
  `5799b7f` 原函数逐字冻结的 oracle 对照，返回、grant 字段、epoch、helper
  读取顺序及次数一致；覆盖所有短路 reason、代际、mask、计数饱和、64 位输出、
  REVOKE 历史保留及 STOP/ACK 查询门。`host-existing-r2` 共 26 项通过，含
  16 项既有准入和 10 项 collector 编排测试。后者使用 facade，不证明真实串口
  或原生记录 CRC；当前实板负测和原生导出另行提供证据。
- 工具：`tdma_autonomous_record.py` 单次申请有限许可证，RUN 内只发控制命令；
  拒绝、非法代际、部分 START 或导出失败均保留为错误。异常后仍尝试全部 STOP
  和 REVOKE，全部 STOP/ACK 成立才导出 SRAM；一个板导出失败不丢弃其余板，
  后续可选诊断查询失败不丢弃已导出的原件。失败最终仍抛出，不自动重试或择优。
- 参数勘误：此前 `VDC-PROGRESS-20260915-005` 及旧采集计划将 8192 标为 events/
  `grant_event_limit` 的描述有误。真实 `calibration_manager_origin_trial_configured()`
  第二参数是 `rearm_budget_ticks`；本轮请求为 8192 clk_sys ticks、256 次 abort
  polls、30 s 期限（250 MHz 下为 7500000000 ticks），未证明任何事件数量配额。
  旧封存原件不改写，本轮 `acquisition-plan.json` 明确纠正口径。
- 资源与构建：`current-plan-r1.json` 绑定源码指纹
  `116d29292bb64076e94f2691095c8490d26d4569ff4865400fe1c4ccacf39f0c` / 1087，
  6/8 节点 A/B 均链接通过。build ID 沿用 `20260914184059`，必须同时核对
  package SHA，六节点为 `c91efb6172c9097e52bd6b1706a676f353873c2c14b6e8fe481871355abb86e3`。
  目标 map 的 `s_origin_attempt` 为 80 B，扣 heap 预留后的余量为
  18284/14524 B，SCRATCH_X 数据为零；无新增 Core1 诊断消费路径。
- 当前源码 P3 用时 184.562 s，普通短帧 passed/closed_loop/realtime/diagnostic
  全为 true，`strict_gates_passed=true`、diagnostic failures 为空；本轮安全 SCK
  行为 `[1,0,0,0]`、最小 margin 为零。四板各 14 槽加 baseline，STOP/ACK 后
  顺序 SD 保存读取 7.766 s，CRC/UID/build/epoch 与 SRAM 原件一致。新一轮通过
  不能追认此前 coarse/SCK 拒绝已通过，也不能据此归因旧 grant 失败。
- 预声明负测 `negative-stopped-r1`：四板停稳后唯一 TRIAL 返回 `<timeout>`，
  ERR 为 -200；reason 为 `RING_DISABLED`，mask 仅含 ring，observed/expected
  为 0/1，请求值完整。REVOKE 前后历史逐字段一致，live grant enabled 为零。
  这直接证明拒绝可表现为主机无数值响应，不能据此推断执行耗时超预算。
- 预声明唯一自主 `capture-r1` 用时 39.969 s，准入 ACCEPTED，trial epoch 为
  40、配置初末均为 71、模型初末均为 24，mask 为 1023。四板各 18 槽加 baseline，
  collection 全通过；三从 epoch 为 2，published 为 7120/7185/7253，完整记录内
  无 INVALID，最大服务间隔为 1579/1585/1588 µs，FIFO 最大为 4/4/2 words。
  顺序 SD 保存读取 8.766 s，与 SRAM 逐字节一致。本轮没有发生 grant 拒绝，
  `VDC-PROGRESS-20260915-006` 的具体拒绝原因仍 unknown。
- 自主总门禁保持 false：第三个连续健康样本最晚在约 2.012 s 完成，超过预声明
  2 s startup 门；NO1 另有 `adapter_tx_not_growing`、
  `physical_flight_persona_mismatch` 和 `periodic_interval_gate_failed`。
  diagnostic 为 true，只说明本次完整记录与从板观察连续性，不能提升为全部自主
  门禁、真实输出锁相或物理时间精度通过；不追加择优轮次，不放宽期限。
- 下一 gate：准入诊断完成后继续 `VDC-TIME-002`，先将真实 DMA 复制前后水位、
  observation epoch、候选范围/bit shift 与跨 ARM 的独立代际随私有 packet 交给
  RX station；独立验收后再做 Core1 READY 边界的原始事件候选查询。station 取消
  epoch 不作逐包 ID，FLIGHT_MUTABLE 的头 CRC 不等于所有邮箱 CRC 通过；
  同 sequence 仅是候选，物理身份与 anchor 仍待证明。`VDC-TIME-003/004` 保持
  PENDING，OTA 实现和 NO5 保持本轮既定范围。

### VDC-PROGRESS-20260915-006 — 原始事件有界历史与失效退休

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。状态：IN PROGRESS。承接已封存的
  启动准入切片，先补齐身份关联需要的原始事件保留；此切片尚未接入 packet 查询、
  逐 capture lease 或 DPLL 输入。
- 证据：`out/HardwareAcceptance/20260915/dpll-event-history/`。以下构建、容量、
  数量及时间是本轮快照，非事实源；深度以 `TDMA_EVENT_HISTORY_CAPACITY` 为准。
- 实现边界：原先每批只将末项写入快照，新历史由 TDMA Core1 physical owner 独占，
  在整批 observer 校验及最终 sticky-fault 复验之后接收完整批次；分别检查源 joined
  数、epoch、ordinal、sequence、原始时间和诊断 flags，失败不留下部分新记录。
  容器接受正确的 sequence 回绕，生产 observer 仍在 sequence 上限拒绝，不能据此
  宣称生产跨回绕连续性；ordinal 不得回绕。STOP、重新准备和 INVALID 退休
  历史，旧 epoch 查询和已淘汰条目不使用 latest 回退。
- 时间/容量边界：compact 条目只保存 RX/TX elapsed、raw counter、sequence 与
  ordinal；context 保存保守初始 start 区间，查询不会将逐事件收窄的 anchor 伪装为
  同一精确时间点。固定保留深度只限制资源与工作量，尚未证明覆盖所有准入配置的
  最坏 DMA/捕获/解析等待；复制出的诊断条目也不是跨 STOP/INVALID 有效的 lease。
- 验证边界：本切片沿用板端记录 schema，实板验证只判断静态资源与原观察器/TDMA
  运输连续性；逐项历史查询由执行真实生产函数的 host 测试验证。完整报文身份、物理 anchor、
  timestamp_valid/dpll_eligible 均未开放，`VDC-TIME-003/004` 继续 PENDING。
- 软件验证：`host-core-r1` 运行完整容器实现，共 11 组 C 用例，覆盖多批次淘汰、
  非法尾项整体退休、丢失/截断批次、旧 epoch、重复候选、时间溢出及 ordinal 边界；
  `host-integration-r1` 的 adapter/service/observer 共 4 项 pytest 通过。真实
  owner→phys→observer 路径在 RX 等待/队列早退时逐项查询整个保留窗，验证每批
  非末条记录；feed 后的最终 fault 注入使旧记录和新批次全部不可查询。独立
  source/host 审核结论为 `PASS_LIMITED_TO_DIAGNOSTIC_EVENT_HISTORY_BASE`。
- 当前源码/资源：`current-plan-r1.json` 指纹为
  `f3e9b0296c3904018b11ce640117026f812344286a21c3aa5bff7874dfff23a9`，6/8 节点
  A/B 构建均通过，build ID 为 `20260914184059`，同时绑定各 package SHA。
  目标 nm 的 history 对象为 568 B，条目为 512 B，其余是 context/对齐；两容量
  静态 RAM 增量均为 568 B，扣 heap 预留后的余量分别为 18364/14604 B，
  SCRATCH_X 数据为零。主机 sizeof 为 576 B，目标枚举 ABI 为 small，不混用。
  `target-abi-r1.json` 复用目标实际编译参数，函数自身静态栈为 append 96 B、
  lookup 40 B；不等于 Core1 调用链栈高水位，lookup 本轮尚未接入生产消费路径。
- 当前源码 P3：`p3-run-r1` 耗时 182.359 s，普通短帧 passed/closed_loop/realtime/
  diagnostic 均为 true，四板各 14 个定时槽加 baseline 完整，STOP/ACK 后顺序
  SD 保存读取耗时 8.547 s，SD/SRAM 一致。三从 epoch 为 1，published 为
  674/689/703，最大服务间隔为 1588/1583/1577 µs，FIFO 为 2/2/1 words。
  本轮 coarse/coded 校准通过，但 `strict_gates_passed=false`：TRN-01 SCK 的
  link 1 候选覆盖失败，TRN-03 没有满足 flight re-arm budget 的实测 SCK 行，
  选择行 `[1,0,1,0]` 的最小从板 margin 为 -1 sample。原件留存，不能提升为严格
  校准或物理时间精度通过；前一切片的 coarse NO2 失败仍保留于其封存目录。
- 自主对照失败：预声明的唯一 `capture-r1` 在四板 START 和 PROFILE RESET 完成后，
  `CALibration:ORIGin:TRIAL` 返回 `<timeout>`；工具将其转整数时异常，28.328 s
  结束并执行全部 STOP/REVOKE，没有完成自主窗口。`capture-r1-recovery` 救回
  epoch `1789411695` 的 SRAM 原件，四板仅 4/5/5/5 个定时槽加 baseline，已写槽
  CRC/身份均正确，但 terminal reason 为 STOP、collection 全为 false；随后顺序
  SD 保存读取 6.656 s，SD/SRAM 完全一致，不将救援成功改成采集成功。
- 拒绝证据：恢复时首先读到 `-200 Execution error`；正确的 STOP 后
  `READ:CALibration:ORIGin?` 显示 version/trial_id/enabled 均为零，HANDoff 为
  UNAVAILABLE。SCPI 准入回调在返回 ERR 时不输出数值，因此 `<timeout>` 不能证明
  命令执行超过预算。具体拒绝分支尚未记录；该准入链不直接读取 SCK replay-safe
  字段，不能仅凭本轮 SCK 失败归因。救援中误发的 `CALibration:ORIGin?` 缺少 READ
  前缀，错误命令原件单独保留，不能用作 grant 证据。
- 有限结论：source/host、目标资源和普通短帧证据闭合，独立审核允许仅按
  `PASS_LIMITED_TO_DIAGNOSTIC_EVENT_HISTORY_BASE` 提交；自主补测未闭合，不追加
  择优轮次。已有从板 ACTIVE 样本来自授权失败前的普通发车，不能代表自主连续性。
- 下一 gate：先为 grant 准入建立可观察的拒绝 reason/阶段，再按预声明计划复测
  完整自主窗口；保持既有授权条件及有限许可证，不以增加 timeout 或忽略 ERR 放行。
  随后逐 capture 唯一编号随真实 DMA observation epoch、复制范围和复制后
  复验结果进入 RX station，捕获时保留所需候选，Core0 只解析 station 私有 packet，
  Core1 在 READY 后复验配置/map/取消代际及龄期，再做诊断关联。不能把
  RX_PREPARE 的 STOP 取消 epoch 当逐 packet token，也不能仅以 sequence 相同给
  最新事件贴上调用者提供的 expected identity；同序列异配置、覆盖、错序、重臂与
  迟到分别拒绝。后续物理 anchor 与正式资格另行验收。

### VDC-PROGRESS-20260915-005 — 最终启用前 CS 准入复验

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。状态：IN PROGRESS。承接上一
  checkpoint 的 NO3 启用前双 CS 已低反例，不改变时间戳或 DPLL 资格。
- 证据：`out/HardwareAcceptance/20260915/dpll-event-observer-start-gate/`。
  以下构建、数量、容量和时间均为本轮快照，非事实源；旧失败仍保留于上一切片。
- 实现：`tdma_event_start` 在配置/seed 完成后的最终 pad-before 快照上复验双 CS；
  任一为低时仅发布已有等待/pad 状态并返回，观察 SM 保持关闭，不递增 epoch。
  下个已有 TDMA owner 相位可以重试，每次工作有界，没有内部轮询或新 PIO/DMA。
  这不承诺有限的总启动等待时间；既有有限流程/许可证负责结束未启动的观察。
- 拒绝边界：最终检查至 enable 之间仍可能遇到边沿，启用后原 DIRTY_START 检查保留，
  INVALID 不因电平恢复而复活，必须显式 STOP/ARM。等待状态继续由 Core1 唯一写者
  经版本化双缓冲发布，不授予正式 timestamp 或完整 packet identity。
- 软件/资源：adapter pytest 通过，执行真实启动、退休、快照和读取函数；使用板级
  RX/TX CS 宏注入单 RX 低、单 TX 低、双低。每组连续三次延后均不启用、不推进
  epoch 或 enable 计时区间，SM0 不变；恢复高后只启用一次。三种启用后低组合均
  永久拒绝，显式退休和重新准备后才进入新 epoch。既有跨核复制/IRQ/退休覆盖保留。
  独立 source/host 审核通过；6/8 节点 A/B 构建通过，静态 RAM 增量为零。
- 当前源码：`current-plan-r1.json` 绑定源码指纹
  `b33ccea73ae8a10b1f62f4965221b9a44d7fd38e55e8d44ac934b3f90448b45f`；增量构建
  沿用 build ID，身份同时核验 package SHA、OTA CRC 与源码指纹。容量 6/8 的 A/B
  扣除 heap 预留后静态余量分别为 18932/15172 B，SCRATCH_X 数据占用为零。
- 当前源码 P3：`p3-run-r1.json` 耗时 181.250 s，quick diagnostic 凭证通过，普通
  短帧 passed、closed_loop、realtime、diagnostic 均为 true；四板各 14 个定时槽加
  baseline 完整，SD/SRAM 一致。凭证的 `strict_gates_passed=false`：coarse CLK
  level7 中 NO2 的 `TOPology 4,1,3` 超时及 `-200 Execution error` 原件保留，不能
  将后续普通短帧通过描述为全部严格校准通过。
- 固定三轮自主观察：按 `acquisition-plan.json` 完成 `capture-r1/r2/r3`，不追加择优
  轮次。wire 周期 1 ms、Core1 整表 1.5 ms、startup 2 s 加运行窗口 6 s、状态快照
  500 ms、有限 grant 30 s/8192 rearm ticks（原 events 口径勘误见 007）；每轮四板均为 18 个定时槽加 baseline，
  collection 为 COMPLETE、无漏采，全部 STOP/ACK 后顺序保存 SD，与 SRAM 逐字节
  一致。快照间隔沿用上一轮容量修正，不能等同于原 250 ms 记录密度。
- 三从连续性：各轮 observer epoch 依次为 2/3/4，从首次 ACTIVE 至末样本均为
  ACTIVE/fault0/FDEBUG0，FIFO 高水位均为 4/4/2 words。NO2/NO3/NO4 的 published
  依次为 7117/7191/7269、7169/7239/7316、7142/7205/7267；最大服务间隔分别为
  1590/1583/1592、1591/1594/1626、1600/1588/1593 µs。采集耗时分别为
  42.688/39.281/39.375 s，STOP 后 SD 保存读取为 7.375/7.297/7.390 s。
- 结论边界：三轮 `diagnostic_observer_continuity=true`；原工具 passed、closed_loop、
  realtime 仍为 false，diagnostic 为 true，startup 与主板自主 persona/software TX
  条件失败保留。板端快照没有捕获 waiting 且最终低 CS 的短暂状态，延后分支只具有
  host 注入覆盖，不能声称实板直接命中或任意启动均可成功，也未证明物理首事件精度。
- 下一 gate：启动准入切片验收后继续 `VDC-TIME-002` 的原始事件与已校验 packet
  身份候选关联，显式绑定 observer/ARM 代际、逐 capture lease、DMA 复制范围和
  取消退休；拒绝覆盖、同序列异身份和旧会话。随后补物理时钟 anchor；诊断候选不
  授予 timestamp_valid/dpll_eligible，`VDC-TIME-003/004` 继续 PENDING。

### VDC-PROGRESS-20260915-004 — 自主事件 FIFO 与解析交接解耦

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。状态：IN PROGRESS。普通模式
  验收之后继续自主发车诊断；不开放正式时间戳、共同时间映射或锁相准入。
- 证据：`out/HardwareAcceptance/20260915/dpll-event-observer-autonomous/`。
  以下数量和时间均为本轮快照，非事实源。`preflight-r1.json` 核对原普通模式
  固件包、源码指纹、四板 UID/build 与 STOP/ACK；失败原件单独保留。
- 修复前：`capture-r1` 采集耗时 40.312 s，STOP/ACK 后顺序 SD 保存与读取耗时
  9.875 s。每板 34 个定时槽加一条 baseline 全部有效，无漏采；CRC、build、UID、
  epoch 和 SD/SRAM 原件一致。记录完整不代表事件有效。
- 失效证据：三从在定时 slot 8 首次报告 `TDMA_EVENT_PRE_FAULT`，fault 为
  `TDMA_EVENT_FAULT_STALL`，FDEBUG 指向两个 counter SM 的 RXSTALL；RX/TX FIFO
  高水位各 8 words，sequence 为 5 words。NO2/NO3/NO4 发布计数停在
  395/638/521，最大服务间隔 4479/4500/4542 µs，此后保持 INVALID。
  失效前相邻快照的 elapsed/ordinal 增量给出约 1000.3 µs 的自主事件间隔，
  与矩阵的 wire 周期相符；Core1 整表周期是另一配置，不能混为发车周期。
- 定位：原 `tdma_pio_spi_phys_event_service` 仅从 capture 路径调用，而
  `tdma_pio_spi_ring_adapter_rx_once_impl` 在 RX_PREPARE 未退休时提前返回，导致观察 FIFO
  等待 Core0 解析交接。固定 FIFO 可容纳的完整事件数量由
  `TDMA_EVENT_FIFO_WORDS` 与成对 counter 输出决定；本轮五事件积压已超过该容量。
- 门禁边界：修复前工具的 diagnostic_passed 为 true，但 passed、closed_loop 和
  realtime 均为 false；主板还有自主 persona 不匹配和一次停止/恢复观察，必须保留，
  不能全部归因于从板 observer。三从原有 TDMA 节点门禁通过，事件失效后只停观察。
- 修复：在 `tdma_pio_spi_phys_service_tx` 的 origin 条件早退前服务观察 FIFO，
  删除 capture 中的旧调用。既有 `tdma_runtime_owner_service_phys_tx` 每个获准执行
  的 TDMA 相位先处理生命周期，再进行有界物理服务；RX_PREPARE 繁忙不再阻挡观察。
  原 OTA/显式跳步、armed/persona、epoch 和 fault 条件保留，不增加 PIO/DMA。
- 软件/资源：四个 observer 测试模块共 6 项通过。新增测试执行真实 component、
  runtime owner、物理服务及 consumer 调用链，在 24 个模拟相位中覆盖 RX 准备各
  非 IDLE 状态、队列早退、无待发 TX、capture 不重复、未 ARM/错误 persona/未初始化
  及 FIFO 堵塞永久拒绝。寄存器和无关 owner 边界仍为 stub，不证明实板时序。
  独立源码/host 审核通过；容量 6/8 的 A/B 构建通过，静态 RAM 增量为零，
  记录 schema 和 PIO 程序未变。当前源码/固件身份见 `current-plan-r1.json`。
- 当前源码 P3：`p3-r1` 完整流程 180.407 s，普通短帧 passed、closed_loop、realtime、
  diagnostic 全部通过；每板 14 个定时槽加 baseline 完整有效，三从最大服务间隔
  降到 1575/1590/1580 µs。四板 STOP/ACK 后顺序保存 SD，与 SRAM 原件逐字节一致。
- 自主复测的容量失败：`capture-r2` 中三从从首次 ACTIVE 至最后已记录 slot 30
  持续无 fault，FIFO 高水位为 4/4/2 words；但原生记录只写入 31/34 槽，
  `TDMA_RECORD_OVERFLOW` 终止，不能声称整窗通过。事件持续变化增加 delta 文件
  长度，耗尽 `STORAGE_MANAGER_FILE_WRITE_MAX_BYTES`；不是 STOP 或 FIFO 堵塞。
  工具泛化的 startup timeout 在本轮实际由 collection_errors 导致，独立原因见
  `recorder-capacity-correction-r2.json`。因此保持运行窗口和逐事件验证不变，后两轮
  仅将状态快照间隔改为 500 ms；快照分辨率与区间门禁密度减半，不等价于原采样密度。
- 启动拒绝：`capture-r3` 四板均完成 18 槽，但 NO3 在启动时报告
  `TDMA_EVENT_FAULT_DIRTY_START`，未发布事件。最终 enable 前后的 pad 快照中
  RX/TX CS 都已为低，两次快照间只有 RX DATA 改变；不能描述为该区间内 CS 才下降。
  原早期 CS 检查在配置/seed 之前，最后 pad-before 后没有阻止 enable 的准入复验。
  故障拒绝有效，启动竞态未修；NO2/NO4 在完整记录中保持 ACTIVE，所有原件保留。
- 显式重臂：`capture-r4` 用相同固件/配置重新 STOP/ARM，三从进入新 epoch，四板
  18 个定时槽加 baseline 全部有效、无漏采、reason 为 COMPLETE。NO2/NO3/NO4
  启动等待后至窗末均 ACTIVE、fault 为零，发布 7094/7185/7258 条事件；最大服务
  间隔 1640/1590/1592 µs，最大服务耗时 301/312/304 µs，FIFO 高水位为
  4/4/2 words。采集与导出 39.625 s，STOP/ACK 后顺序 SD 保存及读取 7.828 s，
  CRC/身份/SD-SRAM 字节一致。该窗口支持本配置成功启动后的 FIFO 服务容量，
  不消除上一轮启动失败，也不是更长周期配置或任意调度阻塞的证明。
- 验收边界：`analysis-r4.json` 的诊断事件连续性通过，原工具 passed、closed_loop、
  realtime 仍为 false。startup 的三次连续稳定观察受启动填充、自主切换及采样间隔
  影响；主板软件 TX 计数与普通 persona 检查也不适配自主模式。保留这些失败，不修改
  gate 来授予完整自主验收，更不授予 `timestamp_valid`、`dpll_eligible` 或锁相。
- 下一 gate：服务与解析解耦切片完成，继续 `VDC-TIME-002` 的最终 enable 前 CS
  准入复验及有界等待，保留 enable 后 DIRTY_START 拒绝和显式重臂要求；随后补齐
  物理首事件、完整 identity 和时钟 anchor。`VDC-TIME-003/004` 继续 PENDING。

### VDC-PROGRESS-20260915-003 — 成对事件观察器生产接入与原生记录

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。状态：IN PROGRESS。RAM 使能验收
  后返回时间输入主线；本切片仅发布诊断数据，不开放正式时间戳或锁相准入。
- 证据：`out/HardwareAcceptance/20260915/dpll-event-observer-production/`。以下构建、
  数量、容量和时间均为本轮快照，非事实源；第一次失败及修复前源码/固件单独保留。
- 实现：`PROJECT_TDMA_EVENT_OBSERVER` 选择 follower 的成对计数与序号采样程序，
  真实汇编模板与已审核原型一致；控制与观察共用原 TX PIO 的固定布局，不新增 DMA。
  纯 C consumer 每次最多接收 `TDMA_EVENT_STREAMS * TDMA_EVENT_FIFO_WORDS` 个字、
  输出 `TDMA_EVENT_MAX_RECORDS` 条诊断记录。半对等待、共同 epoch、多次回绕唯一
  提升、绝对年龄约束和发布前故障复验均在 TDMA owner 内完成；失效只停观察 SM。
- 生命周期与交接：模式切换及 SM 复位前退休 pending 和旧 epoch；旧 TX latch 不再
  消费新的成对 FIFO。事件状态由 Core1 私有工作区生成，经版本化双缓冲交给 Core0；
  复用旧槽前 DMB、提交时 release，读取有界复验版本及复制时限。原生记录 schema
  升级后仍可解码旧版封存记录，文件保存继续位于全部 STOP/ACK 后。
- 软件证据：核心/记录器 19 项、loader/PIO 16 项、adapter 1 项通过。包含生产 C
  生命周期、3440 组 bigint 时间提升对照、真实 pioasm 安装/冲突回滚、双向切槽、
  发布前读取旧完整快照、旧槽复用/撕裂拒绝及 requested role 改变后的退休。独立
  审核结论仅为诊断源码与 host 范围通过；PIO stub 不证明真实采样相位。
- 首轮失败：quick P3 流程耗时 190.484 s，普通模式环路节点在 soak 中均健康，三块
  从板事件发布增长且无事件 fault；NO2 一个原生样本 `valid_mask=0x37` 使整窗采集和
  启动 gate 失败。该位图只能定位为物理快照不可用，未区分 odd/changing guard 与
  复制期限分支；双缓冲修复消除单槽写入窗口，没有放宽读取复验。四板 SD 与 SRAM
  原件一致。第二轮更新固件后，NO2 TOPOLOGY 未确认使 MARK 校准准备失败，原件保留。
- 双缓冲复测：复用已核验固件/OTA 的校准验收耗时 77.579 s，不含编译与传输；NO3
  又有一个物理快照不可用样本，整窗 gate 仍失败。完整 snapshot 耗时 2297 µs，不是
  event copy 独立耗时，也不能据此认定唯一失败分支。随后仅为读取的固定次数复制及
  复验屏蔽调用核中断，统一恢复原 PRIMASK，避免 RTOS 抢占消耗复制期限；未引入
  跨核等待。该结果期限不是关中断 WCET，实际时长仍须独立测量。
- 当前普通模式验收：`p3-r4` 对应源码指纹见 `current-plan-r3.json`，四板真实更新后
  quick P3 用时 186.937 s，短帧 passed/closed_loop/realtime 与诊断门禁均通过。
  每板原生记录 14 个采样槽均有效、无漏采；NO2/NO3/NO4 发布 674/690/703 条事件，
  启动等待后至窗末均保持 ACTIVE 且 fault 为零，RX/TX FIFO 高水位均为两个字，
  sequence 为一个字；主板不启用此 follower 观察器。
  四板 STOP/ACK 后顺序 SD 保存，`p3-sd-r4.json` 核对全部文件与 SRAM 原件一致。
- 资源/耗时：最终容量 6/8 的 A/B 链接均通过，扣除链接器 `.heap` 保留后的静态 RAM
  余量为 18932/15172 B，
  相对 RAM 回收切片新增 1820 B，OTA 未改。从板本轮 service 最大耗时为
  255/240/275 µs，服务最大间隔为 4562/4515/4586 µs；这些包含启动/调度影响的
  诊断最大值不能当作自主发车下的容量证明或新增阶段的独立 WCET。
- 下一 gate：继续核对当前固件短帧闭环及自主发车下的 FIFO 容量/服务间隔，随后补
  物理首事件、CS 相对前缀、完整 packet identity 与物理时钟 anchor。
  `timestamp_valid`、`dpll_eligible` 继续为 false；`VDC-TIME-003/004` 保持 PENDING。

### VDC-PROGRESS-20260915-002 — 成对事件计数与有界联合读取原型

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。状态：IN PROGRESS；RAM 切片
  验收提交后已返回时间输入主线。本条完成离线原型，不是生产加载或板端锁相验收。
- 证据：`out/HardwareAcceptance/20260915/dpll-event-epoch-prototype/`。以下布局、
  容量、案例数及公式参数均为原型与刺激快照，非事实源；旧失败原件保留。
- 联合读取：`epoch_model.py` 建模共同启动条件、固定容量 staging、前后及提交前
  状态复验、部分记录等待、有界超时、永久 INVALID、STOP 退休 ACK 与新代际。
  运行中不清 rising IRQ；只停止观察 SM。旧代际输出在失效/STOP 后不可重新使用。
  粗时间区间筛选回卷候选必须恰好唯一；累计时间还须与当前 FIFO 年龄区间及同一
  启动区间相交。过期记录在消费前拒绝，不能由晚到 packet 复活。
- 原方案反例：独立复核发现 RX/TX 同时少一条时，相对时差仍正常，旧三路按下标
  联合会把后续 raw 配给前序 sequence，之后才因超时失效。真实执行反例见
  `unpaired-both-raw-loss-counterexample.json`。因此未将旧模型测试通过当作身份
  绑定通过，也不靠放宽时差门禁接入 DPLL。
- 新候选：同一个 counter SM 通过两次 `IN ...,32` autopush 输出 X 原始倒计时及
  Y 事件倒计数，再递减 Y。sequence 每帧至少移入完整序号位数，因此可去掉冗余
  ISR 清零。`paired-placement.json` 实际汇编原控制与新程序：控制 10 字、共享
  counter 10 字、sequence 12 字，固定起点 0/10/20，总计 32 字；仍在 follower
  TX PIO 的原四个 SM 内，未新增 DMA、未借 PIO0/DMA7。
- 模型验证：`r1-paired-result.json` 的 159 个案例通过，覆盖机器码相位/前缀、X
  回卷、序号采完前截断、机器码模型中的 autopush stall、单双路完整记录遗漏、单 word
  遗漏、半对等待及 STOP 交错。硬件 Y 值必须按共同 seed 连续递减，再与 wire
  sequence 联合；仅比较 RX 的 Y 等于 TX 的 Y 仍不足。修复前失败、旧模型的
  阻断反例与最终候选结果分别保留，不能混算为生产准入。
- 时间/容量变化：新 raw 在检测后一个模型周期输出；相邻完整事件使用
  `2*d + 5 + w0 + q*(2*M+1)`，首点开销为一个周期。其中 `M=2^32`，`d` 是 raw
  模差，`w0` 来自 raw 大小关系；q 仅由粗时间区间唯一选择，不读模型 wrap 真值。
  同时 raw FIFO 的八 words 仅容四个完整事件；半对是正常瞬态，不直接当作故障。
- 边界与下一 gate：Y 只计本 SM 检测到的事件，尚未证明全部物理 CS 均被捕获。
  仍须证明当前自主发车的序号唯一性、完整帧/身份候选来源、共同初始化、CS/SCK
  和 DATA 相位、RX/TX 转发时差及四事件容量下的最坏服务间隔，再做目标 C/loader
  与 STOP 路径、链接/P3、四板原始计时和实际物理 anchor。所有原型输出的
  IDENTITY_BOUND、有效时间戳及 DPLL eligible 继续关闭；ARM 粗区间不取中点冒充
  pad 时刻。`VDC-TIME-002` 未关闭，`VDC-TIME-003/004` 保持 PENDING。

### VDC-PROGRESS-20260915-001 — 为时间输入调试回收共享采样区 RAM

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`；资源实施由 SYNC_IO 的
  `SYNC-RAM-001` 承接，详见 `SYNC-PROGRESS-20260915-001`。
- 状态：DONE（RAM 使能切片）。用户选择最大收益的共享采样区缩容，明确保持 OTA
  实现不变。容量派生、owner 租约、行为回归、当前源码四板 quick P3、STOP 后 SD
  原生记录一致性和四板各两轮 RAW 重臂均通过；主线 `VDC-TIME-002` 仍在推进。
- 证据：`out/HardwareAcceptance/20260914/dpll-ram-arena16/` 保留软件验证与失败；
  `out/HardwareAcceptance/20260915/dpll-ram-arena16/` 保留最终构建和硬件验收。
- 边界：回收静态余量用于继续 TDMA/DPLL/VDC 调试，不提升内部/正式锁相状态；
  不以 map 通过证明高频采样可靠性，也不借用其他 PIO 或 DMA owner。
- 下一 gate：返回 `VDC-TIME-002`，在 `VDC-PROGRESS-20260914-028` 原型上补共同 epoch、有界联合
  harvest、丢事件永久失效及 raw 时间提升；生产加载与实际时间锚仍须独立验收。

### VDC-PROGRESS-20260914-028 — 从板连续事件观察的 PIO 可执行原型

- TODO task ID：`VDC-TIME-002`、`VDC-RESOURCE-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。完成实际 pioasm 与机器码指令模型；未修改生产 persona、
  固件或硬件，未开放正式时间输入。模型通过不代表四板绑定或锁相通过。
- 日期：2026-09-14。
- 证据：`out/HardwareAcceptance/20260914/dpll-event-observer-prototype/` 的
  `placement.json`、`continuous-result.json`、`joint-gap-counterexample.json`、
  `identity-ambiguity.json` 及独立审核。以下容量、编号和时间均为源码/刺激快照，
  非事实源；PIO/RAM 生产准入仍以 owner 配置、目标链接和实板验收为准。
- 资源方向：当前 process follower 的 TX PIO 为控制转发加旧 latch，RX PIO 已满。
  原型以实际控制程序和两份新观察程序进行固定地址汇编：控制 10 字、RX/TX 共用
  连续计数 9 字、wire sequence 采集 13 字，起点分别为 0/10/19，总计 32 字。
  候选使用 TX PIO 的四个 SM；无新增 DMA、未借 PIO0/DMA7。该结果仅证明静态
  指令布局可容纳；未执行真实控制 SM、生产加载器或 GPIO/资源安装路径。
- 单次与连续边界：20 字单次 raw+sequence 原型通过 1560 组相位刺激，但采集
  DATA 时倒计时暂停，且短 CS 高脉冲可漏检，不能直接成为逐圈时间通道。6 字
  连续计数原型在低电平期间 X 回绕会重复产生 fall；实际机器码反例已保留。
  9 字版本补齐两个回绕分支，并在 CS rising 发布相对 IRQ，序号 SM 在发布前
  检查 sticky，跨帧截断进入错误分支；正常帧等待并清除对应 IRQ 后接下一帧。
- 连续模型：272 组相位/前缀/SM 顺序刺激、20 组计数回绕、64 组普通截断通过。
  无 stall/无漏事件且单次可辨回绕时，指令模型满足相邻事件间隔
  `2 × raw decrement + 4 × event delta + wrap count`。当前 wrap 数使用模型内部
  真值，尚未实现仅凭 raw 与有界 epoch 的生产重建。FIFO 满负测证明会 stall，
  未证明真实 harvest 能在前后 sticky 检查后原子提交或永久失效；对象重置仅是
  模型新 epoch，不能当作 STOP/重臂硬件退休验收。
- 新反例：联合间隔扫描共 864 组，其中 CS 高仅一个模型周期的 108 组均失败；
  可出现三路 FIFO 数量一致且无 stall，但保留的物理帧不同。后续逐条复核旧标签
  的 36 个案例：RX/sequence 保留第 1、3 帧，TX 保留第 1、2 帧；不能称为三路
  同漏中间帧。更正证据见新日期目录的 `prior-gap-label-correction.json`，旧原件
  不改写。较宽间隔在有限刺激中通过，
  不能将其最小值固化为芯片门限。真实 transport 编解码另生成相同 sequence、
  不同 schedule/identity 的两个 CRC 正确帧，序号观察无法区分；不能直接置
  IDENTITY_BOUND。每个 epoch 的来源/配置/序号唯一性必须独立证明或补采身份。
- 下一 gate：先实现三路共同 epoch 的有界 harvest、raw 回绕提升、缺样/溢出
  永久失效和显式重新锚定；证明已准入 CS 高宽、首 SCK、真实 DATA 相位与前缀，
  覆盖尾位截断、IRQ 设置/清除竞争、旧 FIFO 与 STOP/persona 退休。分辨率和
  ARM timer 区间不能代替 pad 边沿精度。只有身份策略和这些负测闭合后才进入
  当前资源链接/P3/四板原始计时诊断，继续维持 TDMA resident 与 HAOFV owner
  边界。`VDC-TIME-002` 未关闭，`VDC-TIME-003/004` 保持 PENDING。

### VDC-PROGRESS-20260914-027 — 最新帧选择前的边沿身份反例

- TODO task ID：`VDC-TIME-002`、`VDC-EVID-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。完成当前源码的独立只读审核与可执行反例；未修改固件、
  未操作硬件或开放正式时间输入，当前板端仍为上一切片已验收版本。
- 日期：2026-09-14。
- 证据：`out/HardwareAcceptance/20260914/dpll-frame-edge-binding-audit/` 的
  `binding-counterexample-r2.json`、`binding-probe-r2-command.json`、基线源码及独立
  审核记录。以下编号和周期为测试刺激快照，非实板测量或物理时延事实源。
- 反例：编译实际 `tdma_rx_scan.c`、`tdma_transport_frame.c` 和从当前源码抽取的
  `tdma_pio_spi_phys_take_local_tx_edge_ex()`。真实编码/解码均通过的帧 100 与 102，
  通过真实 capture-hint 提供各自 identity；受控边沿源保留帧 100 的时间戳，调用者
  换成帧 102 后，真实 helper 仍返回新 identity 和完整 TIMESTAMP/SEQUENCE/IDENTITY
  有效标志。正例、不可用及空 context 负例也执行。该结果证明接口没有独立的
  事件身份校验，不能用随后 packet CRC 正确来证明边沿属于该 packet；不宣称已
  测出实板错配率。
- PIO 边界：指令模型核对当前 `tdma_pio_spi_flight_clock_latch` 的全部指令，
  在默认未合并 FIFO 的刺激中，第一帧低电平填满 FIFO，后续帧 PUSH noblock 丢弃，
  最早条目仍属于第一帧。RX 在取得 packet 后读取该 FIFO，adapter 再将时间戳写到
  所选 view 的 sequence/identity 下；TX helper 直接采用 expected 字段并置 BOUND。
  当前 earliest 策略也会在 clamp 后前跳，已有绑定前提同样未闭合；不能把 latest
  回退到 earliest 当作同圈证明，也不能把单次 latch 可读当作逐圈保全。
- 独立复核的其他约束：latest 可能跳过 trailer 关联所需的前序本地证据、可靠命令
  或 ACK；未锁定 overlay 且 hint 仅有单帧稳定证据时，跨帧选择还会破坏相邻候选
  累积。最新几何完整候选仍需 Core0 完整 CRC，末候选损坏/变长/换相时不可默认
  它是最新有效帧。序列前跳可通过 receive-health，而 missing 计数是超时语义，
  不能据此认定中间帧均被消费。当前 scanner 与 adapter 测试分别注入 DMA 和已绑定
  edge，未覆盖真实硬件 helper 对旧 FIFO/新 packet 的组合。
- 处置与下一 gate：不直接替换全局 locate，也不以清标志或降低门禁作为完成。
  先在 TDMA owner 内验证由硬件事件产生的 epoch/事件序号或 DMA 位置与原始计时
  的联合记录，再以独立捕获身份匹配候选帧；expected sequence/identity 只能作为
  查询条件。边沿保全、普通镜像退休、可靠命令/ACK 消费分别证明，保留原有 FIFO
  overflow、DMA copy 后复验、缺样、STOP/重臂及 persona 退休。通过事件绑定负测、
  当前资源链接和四板原始记录后，才考虑已锁定固定布局的 follower 镜像 latest；
  普通 origin、bootstrap 与重同步继续独立验收。`VDC-TIME-003/004` 保持 PENDING。

### VDC-PROGRESS-20260914-026 — RX 站台等待与观察裁剪诊断

- TODO task ID：`VDC-TIME-002`、`VDC-RESOURCE-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。诊断切片通过当前源码 quick P3；未改变捕获/接受节奏，
  未接入正式时间输入，`VDC-TIME-003/004` 保持 PENDING。
- 日期：2026-09-14。
- 证据：`out/HardwareAcceptance/20260914/dpll-rx-wait-diagnostics-r2/` 的
  `rx-review-r1.json`、`resource-review-r1.json`、`review-final-r1.json` 和
  `p3-receipt-r3.json`。以下数字是本轮快照，非事实源；身份以源码及包 SHA 为准，
  增量目录复用的 build ID 不能单独区分本轮与上一固件。
- 实现：`tdma_service_timing.c` 在现有 Core1 phase/RESET 生命周期内汇总 station
  轮询与捕获后年龄、初次 DMA 观察间隔/积压，以及 epoch、clamp、stale hint、
  frame-copy、discovery-copy 五类丢弃；复用已有时钟读数。DMA epoch 丢失仍保留
  导致它的长观察间隔，STOP、运行类别、配置及 trial 变化才切断会话间隔；内部
  FSM 的高位变化不误拆同一会话。原有 DMA 复制复验、worker 退休及 STOP 取消保留。
- 数据来源：新增 `SYSTem:TDMA:PROFile:RX?` 在全部 STOP/ACK 后读取 SRAM 汇总，
  RESET 位于启动后的控制阶段，窗口延伸至 STOP；原生 TDMA 记录覆盖独立 ARM 的
  完整采样窗，全部 STOP 后顺序 SD SAVE/readback 并核对散列。RX 汇总没有写入旧
  原生记录 schema，不能将两种窗口的计数相等当作校验条件。station 年龄是 owner
  服务时刻的捕获后年龄，不等于 Core0 CPU 时间或准确的任务完成时延。
- 两轮从板结果：NO2/NO3/NO4 的 clamp 次数分别为 3139/3138/3138 和
  3132/3133/3134，其他四类 drop 均为零；每板累计裁剪 790617–802686 个观察
  words，不能换算成精确丢帧数。REQUESTED/BUILDING 轮询合计分别为 1/1/0 和
  5/1/0，占全部 station 轮询最高不足 0.08%。初次观察最大间隔分别为
  4485.076/4474.936/3153.708 µs 和 4535.772/4375.612/3143.744 µs；最大积压为
  1213/1215/1043 和 1213/1049/1043 words，超过本配置环形观察容量。间隔最大值
  与积压最大值未逐事件配对，不能据此反推精确 DMA 速率。
- 调度与全窗：从板完整相位峰值为 706.612–753.064 µs，两轮原生窗内 TDMA
  overrun/deadline 增量均零；对照上一切片 716–752 µs 的波动，未证明稳定提速。
  四板 RX ring overrun 增量分别为 0/6/1/10 和 0/3/4/2，missing 与 latch miss
  增量均零。NO1 TDMA overrun/deadline 仍为 497/471 和 488/466；启动拒收、
  原 TRN03 自主模式 gate false 与全部逐槽失败保留，不能关闭全窗或全表 WCET。
- 资源：首个上限容量构建失败是 `.data` 增加 128 B 越过对齐边界，导致 `.bss`
  后移 4096 B、RAM 超出 3484 B。将仅在 phase entry/exit 使用的 context 记录
  移回 Flash 后，最终 `.data` 相对基线增加 16 B、`.bss` 增加 264 B；容量 6/8
  的 A/B 链接均通过，RAM 余量为 4372/612 B，SCRATCH_X 数据占用仍为零。嵌套
  clock/record 探针保留 SRAM；编译器单函数栈报告已归档，不宣称动态整栈水位。
- 验证与失败：相关 host 测试 109 项通过，间隔边界和放置调整后重跑的 55 项是
  其中子集。首次 quick P3 用时 208.531 s，TDMA 闭环通过但 NO1 coded-marker
  completion timeout；resume 在 NO2 APPLY timeout 后于 11.078 s 终止。
  STOP/配置恢复后 r3 严格门禁通过，79.047 s 不含 build/OTA。一次离线 audit
  早于 SD summary 完成而失败，随后读取完整原件复核通过；本地 launcher 已增加
  上轮 SAVE terminal JSON 前置条件，保留该编排失败，避免只凭 STOP 放行下一轮。
- 下一 gate：观测支持优先检查“捕获/接受跨 service、窗口保留较旧帧”的积压，
  尚不支持将 Core0 构造等待定为主因。先评估最新完整帧选择与旧观察退休的有界
  策略，分别验证新数据准入、帧身份、copy 后 DMA 复验、hint 失效与 STOP/重臂；
  不将运输丢失与观察裁剪混为一谈。时间戳快速通道仍按 owner raw 身份与边沿
  条件单独验收，不能依赖整帧解析或以本轮诊断替代正式时间输入。

### VDC-PROGRESS-20260914-025 — TDMA review 06 的模式、身份及窗口复核

- TODO task ID：`VDC-TIME-002`、`VDC-EVID-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。完成用户指定 review 的只读证据复核；没有固件或板端操作，
  不改变 review 的 C11 状态、契约登记或正式时间输入门禁。
- 日期：2026-09-14。
- 证据：`out/HardwareAcceptance/20260914/dpll-review06-audit/review06-audit-r1.json`。
  原 review 单独归档；复核绑定其引用的 latch-direct-seed/handoff-r2 summary、
  origin 原生记录、完整逐槽报告、源码和上一封存。以下数字为该轮快照，非事实源。
- P1/P2 的主因判断与原件不符：summary 的 25 个相邻区间中，origin 的
  receive_rejected、bitmap_incomplete 和 receive_missing 增量全部为零，25 次
  可比较的 feedback identity 全部匹配。实际区间错误为
  `physical_flight_persona_mismatch` 25 次、`adapter_tx_not_growing` 24 次，来自
  旧检查器对普通 persona 和软件 TX 增量的假设；已由进度 021 的离线工具区分，
  原 TRN03 gate 继续保留 false。不能因此推导为本窗持续 bitmap 故障并直接修复。
- 身份与时刻混用：review 引用的不等 identity 属于 `runtime_before`，当时是普通
  origin persona 11，TX/RX sequence 为 276/275；相差 4,540,504 ns 的时间戳也来自
  该快照，且 ring_last_error 为零。它们没有同圈前提，不能据此证明自主模式持续
  关联倒置。`runtime_after` 已是自主 persona 16，TX/RX sequence 均 6369、identity
  均 191664741；运输身份一致仍不等于已获得有效的同圈物理时间戳。
- “时间戳前提闭合”未被证明：flags=2/resolution=8 ns 来自普通模式 before；自主
  after 是 flags=1（DIAGNOSTIC_ONLY）、resolution=0，reference TX 和 feedback RX
  时间戳均为零。origin 原生 slot 6–33 的 legacy latch_count 恒为 318，不能用
  切换前增长证明自主时间输入连续。`tdma_pio_spi_phys_origin.inc` 明确清除 CPU
  latch armed，由自主 graph 拥有 raw capture/rearm；量化分辨率不能替代实际边沿
  精度、逐圈关联或 formal qualification。
- WCET 与整窗边界：四板 DPLL cumulative max 在 summary 的 before/after 均相同；
  该 summary 覆盖期 NO1 新增 overrun/deadline 为 3/3，从板均零，不能写成四板本轮
  新增超限。完整原生窗口另保留启动 reject/bitmap 5/4/4/2 和 RX ring overrun
  0/6/3/0；解码错误为零不证明无观察覆盖、物理零误码或全窗已通过。摘要窗口与
  原生完整窗口、历史最大值与本窗增量必须分别引用。
- 采纳与下一 gate：保留 review 关于关联、共同时间、DPLL 实际更新预算和严格
  准入分别验收的检查清单；不采纳其未经模式/身份区分的 P1/P2 根因判断及物理层
  闭合结论。当前仍为 `VDC-TIME-002`：先补站台等待、DMA 初次观察间隔及 drop
  原因，完成 raw 身份/保持量和同圈计时前置条件；`VDC-TIME-003/004` 保持 PENDING。
  不提前接通命令、去除临时许可证、隔离健康节点或声明四板锁相。
- 工作区：外来 review 05/06 与索引更新保留；本轮 RX diagnostics 初始化在检查到
  外来文档散列变化时终止，尚未创建基线副本或修改固件，随后优先完成本次用户
  指定复核。终止记录与外来新散列已归档，下轮从新的工作区快照继续实现。

### VDC-PROGRESS-20260914-024 — RX 单站台吞吐与观察缓冲余量审计

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。仅完成只读源码及两轮完整原生窗口审计；没有新固件部署或
  板端操作，`VDC-TIME-003/004` 保持 PENDING。
- 日期：2026-09-14。
- 证据：`out/HardwareAcceptance/20260914/dpll-rx-consumer-capacity/` 的
  `capacity-audit-r2.json`；绑定上一封存、当前源码散列和六份从板原生记录，逐槽
  保留启动与后续失败。以下数字均为本配置的源码推导或测量快照，非事实源。
- 结构性限制：`tdma_pio_spi_ring_adapter_rx_once_impl()` 在 station 非 IDLE 时直接
  进入接受路径；REQUESTED/BUILDING 等待或 READY 接受均不捕获下一帧。即使 Core0
  立即完成，捕获与接受也至少分占两个 Core1 service。当前完整表周期为 1.5 ms，
  因此理论捕获上限约 333.333 次/s；不能用缩短一项 CPU 操作证明突破该限制。
  `task_refmem_sync()` 先运行 RefMem，再调用 Core0 prepare service，随后 delay；
  现有原生记录没有逐次 station 等待或捕获间隔，尚未证明具体等待来源及最大值。
- 两轮完整窗口：NO2/NO3/NO4 service 增量分别为 5499/5498/5498 和 5499/5500/5498，
  捕获为 2514/2537/2557 和 2508/2536/2560，约每个 service 捕获 0.456–0.466 帧。
  observation drop 增量为 2180/2202/2222 与 2193/2217/2242，明显多于 ring overrun
  的 5/0/0 与 6/3/0。drop 合并窗口裁剪、epoch、过期 hint 与复制复验等原因，
  不是丢帧数，也尚不能全部归为主动跳帧。从板本窗相位 overrun/deadline 仍均零，
  证明局部预算通过不足以保证观察吞吐或无覆盖。
- 容量边界：`TDMA_PIO_SPI_RX_RING_WORDS` 为 1024，每个 SRAM word 只承载一个
  观察字节；虽然占用 4096 B RAM，对当前物理帧 173 个观察字节仅约 5.919 帧容量。
  scanner 的最大窗口来自 `TDMA_PIO_SPI_RX_DMA_WORD_MAX` 加
  `TDMA_RX_OBSERVATION_SCAN_WORDS`，本次为 616 words；有效 hint 在裁剪后仍选取
  窗口内最早完整帧。复制本次 168-word 帧后，退休游标相对初次 produced 的积压
  至多 448 words；这是相对初次计数的几何量，必须再加入复制期间及下一次观察前
  的 DMA 增量，不能当作复制结束时的实时积压上界。初版报告命名未区分此时刻，
  已在 r2 更正并保留 r1；未将名义物理帧周期代入为实测保持时间。
- O2/O4 边界：DMA 初次观察建立 completed words 和 epoch，复制后复验排除期间
  的覆盖及 epoch 变化，两者不是可直接删去的重复采样。完整窗口的稀疏快照也不能
  给出逐圈物理周期、准确跳帧数或最大 worker 延迟。
- 下一 gate：`VDC-TIME-002`。先为 station 等待、初次捕获间隔和裁剪/epoch/复制
  拒绝补有界板端计数与最大值，独立核算 RAM 和整相位开销，再选择捕获节奏或
  明确的载荷合并策略；特等席时间戳与整帧解析解耦。同相位无条件追加捕获仍维持
  拒绝结论，身份、代际、DMA 复验与 STOP 取消必须保留。全目标与正式锁相不关闭。

### VDC-PROGRESS-20260914-023 — RX/TX latch 直接初始化与四板对照

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。当前重装切片已验收；完整观察连续性、正式时间输入及全表
  WCET 仍未闭合，`VDC-TIME-003/004` 保持 PENDING。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-latch-direct-seed/`。
  `current-plan.json` 绑定部署包与回退基线，`review-final-r1.json` 为总复核；
  提交身份和文件散列由 `commit-proof.json` / `slice-manifest.json` 绑定。
  以下数值均为测量或构建快照，非事实源。
- 实现：`tdma_pio_spi_phys_clock_latch_rearm()` 和
  `tdma_pio_spi_phys_tx_clock_latch_rearm()` 使用 CPU 注入的 `MOV X, ~NULL`，
  替代 FIFO 写入、PULL 和 OSR→X 搬运。既有 latch 程序只消费 X；保留禁用 SM、
  清 FIFO、restart、恢复 PC、记录 epoch、启用 SM 的次序。resident PIO 指令、
  owner/资源、静态 RAM 及接收/STOP 门禁不变，未重引入同相位补充捕获。
- 软件与目标：最终 latch 用例 15 项、相关观察/计时用例 89 项通过，共 104 个
  不重复用例；涵盖真实 C rearm/read、dirty state、无效输入、溢出和实际 PIO 源码
  的首边沿/FIFO 保留行为。容量 6/8 的 A/B/boot 增量构建分别为 27.328/27.547 s；
  目标反汇编均为 `0xa02b`，重装中不再有 FIFO 传送轮询。两包各减少 72 B，主 RAM
  余量分别 4636/876 B、SCRATCH_Y 使用 1448/1648 B，SCRATCH_X 数据分配为零。
  RX/TX rearm 的编译器局部栈为 32/40 B，不代表完整调用链或动态水位。
- 源码提交：`447b93c`。源码指纹为
  `836c8e42d6bd842ed18ab24da2ea71e2abd83ca5695073677561f91f54578b82`，
  共 1069 个受验文件；当前容量 build `20260914112635` 的包 SHA-256 为
  `3eb8c621be8499da61afef1f422a2d0a62b5e0d0bc9046d67dcf29dbe59ef6b7`。
  增量 build ID 与基线相同，不能单凭 build ID 区分包；以源码和包散列为准。
- P3 失败与恢复：首轮四板 OTA/quick 流程为 195.688 s，普通 TDMA 三项 gate
  通过，但 NO2 coarse CLK APPLY 失败、TRN01 SCK gate 失败及无可用 rearm margin
  行导致 strict=false。第二轮 resume 在 P0T 的 NO1 profile APPLY 失败，10.906 s
  退出且没有 receipt。STOP 后 NO1 的 stage/apply/reject 为 1/0/1、last_result 为
  BAD_ARGUMENT，不能归为单纯丢 ACK；一次有界 STAGE/APPLY 重试成功，具体拒绝
  分支尚未证明。第三轮用同一包及已验证 OTA 的正式 resume，81.797 s，strict=true、
  diagnostic failures 为空，普通 TDMA passed/closed_loop/realtime 均 true。
  恢复耗时不含 build/OTA，不能冒充全流程加速结果；三轮原件和恢复读回均保留。
- 自主有限采集：两轮各板原生样本均为 34 条、无漏采；四板 missing 和 latch miss
  增量均零，从板 TDMA overrun/deadline 增量均零。NO2/NO3/NO4 OTHer 完整相位峰值
  两轮分别为 716.452/732.512/737.296 µs 和 725.280/737.060/752.080 µs，均在当前
  配置预算内；回退对照为 740.984/749.244/722.224 µs。RX/TX 重装分项涨跌不一，
  这些是整相位峰值内的分项，非独立 stage 最大值或时延分布，稳定微秒收益未证实。
- 未闭合反证：NO1 两轮 TDMA overrun/deadline 为 484/454 与 459/433；四板 RX ring
  overrun 分别为 0/5/0/0 与 0/6/3/0，reject 两轮均 5/4/4/2。原始自主三项 gate
  均 false，逐槽审计也未通过；不能用缺失计数为零代替 RX 无覆盖或全窗连续性。
- 存储和观测边界：START 至最终 STOP 无 SCPI 查询，全部 STOP/ACK 后逐板 native
  SD SAVE/readback，各轮均成功且字节匹配；命令时间证明各板保存没有重叠。首轮
  STOP 后 profile 读回的 UNAVAILABLE 与工具异常保留，工具改为保留原响应并有界
  重试，后续读回完整。最终四板 STOP/config ACK/phase/grant inactive，未操作 NO5，
  未隔离健康 TDMA 节点。外来文件散列和上一封存均复核未变。
- 下一 gate：`VDC-TIME-002`。继续拆分 DMA 初次观察、复制后复验及跨 service 消费
  间隔，解决 RX 覆盖、普通 origin 超限及可恢复 APPLY 拒绝；保留代际/复制一致性
  与 STOP 取消，不以扩大同相位工作或关闭诊断使门禁通过。未接通新的 DPLL 正式
  时间输入，不声明四板锁相。

### VDC-PROGRESS-20260914-022 — RX 同相位补充捕获的失败与回退

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。拒绝当前合并试验，恢复上一已提交实现；完整观察连续性和
  全表 WCET 仍未闭合，`VDC-TIME-003/004` 保持 PENDING。
- 日期：2026-09-14。
- 失败证据根：`out/HardwareAcceptance/20260914/dpll-rx-station-pipeline/`；
  `rejected-source-checkpoint.json` 绑定未提交源码、差异、测试及先前封存，
  `rejection-review.json` 保留逐槽增量与完整相位分解。回退复核单独保存在
  `out/HardwareAcceptance/20260914/dpll-rx-pipeline-rollback/`，不改写前轮证据。
  以下数值均为本轮测量快照，非事实源。
- 试验边界：process follower 接受一个 READY 结果、退休 station 后，在同一 service
  中至多捕获一帧交给 Core0。未增加 station、静态缓冲或资源借用，保留身份/代际/
  年龄检查和 STOP 取消。66 项相关 host 用例通过；前两次测试夹具错误保留，容量
  6/8 的 A/B 和 boot 构建通过，静态 RAM 不变。普通模式 quick P3 为 191.063 s，
  passed/closed_loop/realtime 和 strict 均通过，但不能覆盖自主试验中的回归。
- 自主试验：各板原生记录 34 条且无漏采，missing 增量均零；NO1 至 NO4 的 TDMA
  overrun/deadline 增量分别为 509/467、14/6、7/2、14/11，RX ring overrun 为
  0/34/15/3。原始 passed/closed_loop/realtime 均 false。从板超限分布于后续多个
  采样区间，不是只在启动时出现；平均接收速率提高不能证明最大观察间隔缩短。
- 完整相位：NO2/NO3/NO4 的 OTHer 峰值从 728.108/760.668/742.696 µs 升至
  881.324/908.356/894.040 µs，超过当前配置的 TDMA 850 µs 预算。峰值内 RX
  acceptance 为 121–173 µs、捕获为 188–213 µs，加上 request、overlay 和 owner
  其余工作，不能在同相位内无条件追加。stage 为嵌套区间，不能全部相加。
- 计时归因：四板累计 scheduler max 在采集首末相同，不能把历史最大值当成本窗
  新产生的峰值。NO1 两端均为自主 persona 的采样区间中 overrun/deadline 未增长，
  普通 origin/交接区间的失败仍计入完整窗口；跨板 slot 不作为共同时间对齐证据。
- 回退软件验证：源码指纹恢复为
  `ef5b67799fd11a2151750cca1d526ddc6f6322a986b4f6f9b91c0bf6945df20a`；
  两种容量重建的 package 和全部 ELF 与封存基线逐字节相同，89 项相关 host 测试
  通过。试验源码/测试已完整归档，其他工作区改动散列保持原样。
- 回退硬件复核：当前源码重新四板 OTA/quick P3 为 192.000 s，三项 TDMA gate
  及 strict 均 true、无 diagnostic failure。两次有限自主采集各板均 34 条、无漏采，
  从板 TDMA overrun/deadline 增量两轮均零；第二轮 NO2/NO3/NO4 完整相位峰值为
  740.984/749.244/722.224 µs。两轮 RX ring overrun 为 0/1/2/7 与 0/20/2/11，
  missing 均零，不能宣称观察缺口已修复。第二轮 NO1 TDMA overrun/deadline 为
  498/475，交接 7107.624 µs，原始自主三项 gate 仍 false。
- 存储失败与恢复：回退首轮 NO3 native SAVE 为 FAILED、storage error=6；四板
  RAM 原件完整，另三板 SD 散列一致。NO3 的一次 PEAK 查询返回 UNAVAILABLE，
  保留原响应和工具异常。软重启时 USB ClearCommError 也作为失败保留；随后按
  UID/build 验证 STOP/ACK，重启清空的校准相位须由下一轮重新装载，不能把重启后
  STOP 成功误写成校准通过。第二轮重新装载配置后的四板 STOP/相位检查通过，
  native SAVE 和 SD 字节一致性全部通过；未用主机回写副本冒充原生保存。
- 保存顺序勘误：本轮复核沿用的 `save_capture.py`，发现其实际使用四线程。
  前述切片关于“顺序保存”的描述不准确；保留原始命令时间序列和失败，未改写
  封存原件。回退第二轮改为逐板保存并由首末命令时间证明无重叠，耗时 41.094 s。
  本轮全部 START 至最终 STOP 之间仍无 SCPI 查询；最终四板 STOP/config ACK/
  grant inactive，未操作 NO5。总复核见回退目录 `review-final-r1.json`，失败试验的
  `slice-manifest.json` 标记 optimization_accepted=false；本轮仅提交文档进度。
- 下一 gate：保持独立的 RX 接受与捕获相位，先降低 latch 读取/重装、DMA 观察
  及现有单相位开销。重新合并前必须评估完整相位的剩余预算和后续保留量，不能以
  profiling 时钟代替 owner 的生产预算，也不能取消覆盖/代际/STOP 检查。当前
  `VDC-TIME-002` 不关闭，未接通新的 DPLL 输入，不声明四板锁相。

### VDC-PROGRESS-20260914-021 — 原生记录逐槽检查与接收观察缺口

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。离线诊断工具切片已验收；完整窗口连续性、同圈时间输入和
  全表 WCET 仍未闭合，`VDC-TIME-003/004` 保持 PENDING。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-observation-audit/`。
  `current-plan.json` 绑定源码、部署包与前轮封存；`review-final-r1.json` 为总复核，
  提交身份及逐文件散列由 `slice-manifest.json` / `commit-proof.json` 绑定。
  以下数值均为测量快照，非事实源。
- 实现：新增 `tools/calibration_ring_validate/tdma_observation_audit.py`，直接解码
  原生二进制并校验 CRC、build/board/采集 epoch、预期槽数/间隔、配置代际、persona/
  FSM、回传序号/身份、接收代际、FIFO 推进以及拒收/缺失/overrun 增量。全部采样槽
  和 baseline 计数增量都进入报告，启动失败和中途失败不能被后续恢复隐藏。跨组
  字段非同时采样，仅在序号相同时比较身份，否则明确记为不可比较。
- 模式边界：STOP 后的成功 HANDoff 必须与独立控制记录中的 trial/config/clock
  匹配，才能解释自主 persona 下的软件 TX 平台；实际回传、接收代际和 FIFO 仍须
  前进。普通 origin/follower 继续要求 TX 推进。该工具是独立诊断伴随工具，未修改
  TRN03 或 P3 门禁；它不证明逐样本 grant、逐圈 raw 身份、物理发车连续性、有效
  时间戳或 WCET。现有采样 schema 没有这些完整字段，STOP 冻结的尾部记录也不能
  替代整窗。`TDMA_RING_RUNTIME_REASON_TIMESTAMP_MISSING` 单列为时间输入缺口。
- 软件与构建：173 项相关测试通过，使用生产 C recorder 输出验证停滞、回放/倒退、
  正向回绕、半区间跳变、身份/代际错配、CRC 损坏、缺少交接上下文以及启动和中途
  错误。容量 6/8 的 A/B、boot 增量构建分别 5.610/5.532 s，部署包及静态 RAM 段
  与前轮完全相同；主 RAM 余量仍为 4636/876 B。本切片只有主机工具/测试变更，
  不声称改变实时执行耗时。源码指纹为
  `ef5b67799fd11a2151750cca1d526ddc6f6322a986b4f6f9b91c0bf6945df20a`；
  build ID 仍为 `20260914112635`，包散列见 plan，结合当前 OTA 原件区分验收。
- 当前源码 quick P3：含增量构建和四板 OTA 共 192.468 s，短帧 passed/closed_loop/
  realtime 均 true，strict_gates_passed=true，无 diagnostic failure。STOP 后原生
  TDMA SD 保存和字节一致性通过；该普通模式通过不提升后续自主诊断的失败结果。
- 四板有限自主交接：trial/config 为 36/71，总交接 5904.296 µs；每板原生记录
  各 34 条、无漏采，NO1 有 28 条自主样本。启动阶段接收拒绝增量为 5/4/4/2，
  missing 均零，但 RX ring overrun 为 0/2/0/2；NO2 的早期接收未就绪也被保留。
  当前报告为 `dpll-observation-audit-handoff-r1-observations.json`，
  完整窗口 observation_checks_passed=false；原始 passed/closed_loop/realtime
  仍全部 false。全窗 NO1 TDMA overrun/deadline 增量为 460/435，从板两类增量均零。
- 旧证据复核：前轮两次原生文件经 SD 散列和控制记录重新绑定，未改写原件；RX
  ring overrun 增量分别为 0/2/10/7 和 0/1/5/12，missing 同样均零。这是原有
  观察缺口的新检出，不是本轮主机工具造成的固件回归。源码
  `tdma_pio_spi_phys_capture_words_async()` 在 DMA 写入超过未观察位置一个
  `TDMA_PIO_SPI_RX_RING_WORDS` 容量时累计 overrun，之后按有界新窗口推进游标。
  因而 missing 为零不能证明无接收观察覆盖；overrun 也不直接证明物理环路停发。
- 收尾与下一 gate：首次 START 至最终 STOP 之间没有 SCPI 查询，全部 STOP 后
  顺序保存原生 SD；最终四板 STOP/config ACK/grant inactive，未操作 NO5。
  继续 `VDC-TIME-002`，先定位从板 RX 消费积压/覆盖与启动拒收，同时保留普通
  origin 超限、准备/准入完整相位、交接及其他配置的门禁；补齐逐圈身份与实际边沿
  证据后才进入 `VDC-TIME-003/004`。本轮未接通新的 DPLL 输入，不声明正式锁相。

### VDC-PROGRESS-20260914-020 — 就绪阶段有界合批与取消后重臂

- TODO task ID：`VDC-TIME-002`。
- 状态：IN PROGRESS。当前四板交接时间缩短，两轮 transport missing 增量为零；
  完整窗口的自主模式验证、普通 origin 超限及物理边沿误差仍未验收。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-origin-ready-batch/`。
  实现提交：`60257e0`；文档单独提交，最终提交身份由证据 manifest 绑定。
  `initial-checkpoint.json` 绑定前轮封存与四板 STOP；最终源码和包见
  `current-plan.json`，逐阶段原件见 `handoff-r1-review.json` /
  `handoff-r2-review.json`，取消探针见 `cancel-r1-review.json`，总复核见
  `review-final-r1.json`。以下数值均为本轮测量快照，非事实源。
- 实现：`tdma_runtime_owner_origin_poll()` 按显式白名单合并已就绪的
  STOP→PERSONA→BUILD_BEGIN 和 BUILD_STEP→SEED→SMS→INSTALL。
  `TDMA_ORIGIN_PREPARE_BATCH_MAX_STEPS` 限定单次步数，
  `TDMA_ORIGIN_PREPARE_BATCH_YIELD_CYCLES` 到达后不再追加步骤；该阈值取 TDMA
  静态预算的一半，不等于最后一步或完整相位的 WCET 证明。每步重验 owner grant、
  配置代际、时钟、期限和 STOP，physical poll 继续核对冻结配置与安装资源。
  MAILBOX 与 STOP、BUILD_BEGIN 与 Core0 任务接收之间仍让出；未就绪的
  BUILD_STEP 一次读取后立即返回，不忙等，不提前复用仍有 DMA/worker 占用的 union。
- 软件与构建：33 项相关测试通过。新用例覆盖合批顺序、Core0 未完成、精确预算边界、
  五种可合并边界上的撤销/模型/时钟/配置/期限/STOP 变化，以及每阶段失败不能继续
  安装。首轮 expiry 用例预期同次 FAILED，但推进时钟也越过预算，实际先 BUSY
  让出；保留 `host-r1` 超时记录，修正为验证下一次调用前拒绝且没有后续 physical
  操作，`host-r2` 全部通过。固件未因此放宽门禁。
- 资源与部署：容量 6/8 的 A/B、boot 增量构建分别 8.359/8.296 s；主 RAM 余量仍
  为 4636/876 B，全部静态 RAM 段与前轮一致，SCRATCH_X 数据为零；owner poll
  静态栈从 40 B 到 64 B，physical poll 仍为 400/464 B，Core1 预留栈 2048 B。
  `.su` 不替代完整调用链或动态栈水位验收。当前源码指纹为
  `a8dfd83dac6184387894961d1250bfa0e93bbc65abc9d7eefe5a8f52f850f6e5`，
  四板部署包 SHA-256 为
  `0dc165fc11bf3540a247c35f16c825fbaec1e417f19f25bcda6bc15f56dc714f`；
  增量 build ID 仍为 `20260914112635`，必须结合 SHA 和 OTA 原件区分。
- 当前源码 quick P3：包含增量构建与四板 OTA 共 183.141 s，短帧 passed/
  closed_loop/realtime 均 true，strict_gates_passed=true，无 diagnostic failure；
  STOP 后原生 TDMA SD 保存及字节一致性通过。本轮没有重复初始化/校准超时，
  不表示前轮间歇故障根因已解决。
- 交接对照：两轮 trial/config 为 36/71、68/84；总交接由前轮
  12904.364/12735.268 µs 降为 8281.476/5889.468 µs。STOP 进入至 INSTALL 返回
  从 10478.576/10515.896 µs 降为 6167.708/3291.404 µs，分别缩短 41.14%/68.70%。
  STOP→PERSONA、PERSONA→BUILD_BEGIN 的体外间隙从跨调度周期缩到约数十微秒以内。
  r1 的 BUILD_STEP 观察三次，SEED 调用体 410.096 µs 后触发合批让出；r2 观察两次，
  SEED/SMS/INSTALL 同次推进。准备体内合计 1144.180/839.728 µs，体外合计
  7137.296/5049.740 µs，包含 Core0 构造、调度和等待，不全部当作空闲时间。
- 取消与恢复：两轮正常交接之间插入有限 BUILDCancel 许可证。真实 builder 发出
  一个 descriptor run 后暂停，公共 STOP 延迟取消一次，最终入口清零、worker IDLE，
  trial/config 为 52/77。`cancel-r1` 的传输门禁失败是主动取消试验原件；后续新代际
  handoff-r2 成功安装，证明本配置工作区可再次借出。该探针不证明任意指令边界竞态
  或微秒级取消上界，其余物理配置仍保留门禁。
- 连续性与检查器边界：两轮各板原生记录各 34 条、无漏采，transport missing
  增量均为 0/0/0/0。原始 passed/closed_loop/realtime 仍为 false；soak 的唯一
  错误类别来自 NO1 `physical_flight_persona_mismatch` 和 `adapter_tx_not_growing`。
  `trn03_closed_loop.py` 当前要求普通 origin persona 和软件 TX 计数，自主 persona
  的硬件发车不能套用该假设。保留失败结果，后续须用已授权 persona、硬件计数和
  原始计时身份建立完整窗口检查，不能简单允许任意 persona 或忽略 TX 停滞。
  missing 为零也不证明切换期间物理发车没有间隙。
- 完整相位：NO1 自主 RUN 峰值为 572.184/587.664 µs；普通 origin
  CYCLE_BOUNDARY→RUNNING 的峰值为 1579.056/1460.528 µs。全窗 TDMA overrun
  增量仍为 484/478，deadline miss 为 453/455，三块 follower 两类增量均零。
  合批后准备阶段的独立完整相位峰值尚未取得，不能以局部调用和 RUN 峰值关闭全表
  WCET；整表和 TDMA 预算仍由原有项目配置符号定义。
- 收尾与下一 gate：三轮首次 START 至最后 STOP 之间没有 SCPI 查询；全部 STOP
  后顺序保存原生 TDMA SD，字节匹配，最终 config ACK/grant inactive，未操作 NO5。
  继续 `VDC-TIME-002`，补齐独立准备/准入峰值和普通 origin 的 RefMem 发布分解，
  审核自主模式完整窗口检查，再验证物理边沿间隙及其余配置；不新增 DPLL 输入或
  宣布正式锁相，`VDC-TIME-003/004` 继续等待依赖。

### VDC-PROGRESS-20260914-019 — 校准 CRC 驻留与启动相位复核

- TODO task ID：`VDC-TIME-002`。
- 状态：IN PROGRESS。当前源码 quick P3 与 CRC 等价性验证已通过，完整窗口连续性、
  普通 origin 超限及独立准入耗时仍未闭合；`VDC-TIME-003/004` 不提前开放。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-origin-admission-crc-final/`。
  实现提交：`c2e6684`；本记录及 TODO 单独提交，提交身份由证据 manifest 绑定。
  `initial-checkpoint.json` 绑定前序邮箱合批切片和首个失败尝试；部署身份见
  `current-plan.json`，总复核见 `review-final-r1.json`，普通/自主相位增长见
  `phase-windows-reviewed-r2.json`。后者修正 r1 对状态方向的文字解释，数值未改。
  以下数值均为本轮测量快照，非事实源，不构成全表 WCET 或物理边沿误差界。
- 实现：`tdma_origin_calibration_crc32()` 使用与 Calibration 原 CRC 相同的
  CRC-32/ISO-HDLC 四位查表算法。准入仍重算完整 `tdma_ring_calibration_stage_t`，
  不缓存 CRC，不缩小校验范围；owner grant/config/clock/expiry、STOP 取消和 DMA
  退休边界保持。函数驻留主 SRAM，常量表放在 SCRATCH_Y 的 Core0 栈下方数据区。
  `TDMA_SERVICE_TIMING_VERSION` 新增 ADMIT、嵌套 CALIBRATION_CRC 和 BEGIN 阶段，
  解码工具兼容旧版本；未新增 PIO/DMA 或改变 DPLL 时间戳资格。
- 软件与资源：51 项相关测试通过，CRC 对照真实 portable OTA 库，覆盖容量 2/6/8、
  各长度和非对齐输入、完整 stage 逐位变化及 owner 准入拒绝。容量 6/8 的 A/B 与
  boot 增量构建均通过，分别 4.532/4.844 s。主 RAM 余量为 4636/876 B；CRC 函数
  60 B、表 64 B，SCRATCH_Y 数据为 1448/1648 B，距各自预留 Core0 栈底仍有
  600/400 B。Core0/Core1 栈各保留 2048 B，SCRATCH_X 数据为零。静态栈文件、
  ELF/map 和实际符号复核分别见 `build6-archive`、`build8-archive`、
  `stack-archive-r1.json` 和 `resources-reviewed-r1.json`。
- 失败与修正：首次把表也放主 `.data`，跨越 DMA 工作区对齐边界，容量 8 链接
  RAM 超出 3220 B。该次容量 6 已误启动的 P3 保留，短帧通过但 NO4 coarse CLK
  APPLY 超时，strict=false。原件封存于相邻 `dpll-origin-admission-crc/`，
  `attempt-manifest.json` 的 SHA-256 为
  `c7c222d201e6d0d25da7dd71eccfdc8a5c510093b18abf56c0f2d8b1e44bb8e4`；
  调整表的 RAM 落点后才形成当前容量 6/8 均通过的版本。
- 部署与恢复：当前源码指纹为
  `0ce28df0b5079b30d234f40c6f409b7cd98cba0983de2a8f1b4c5e758875e0d0`，
  包 SHA-256 为 `e7edb9c2a945376c3b156df48c56be33f14dce517f51bfd90a551fdccb685755`。
  增量 build ID `20260914112635` 与首个尝试相同，必须以源码/包 SHA 和 OTA 原件
  区分。当前 `p3-r1` 四板 OTA 完成，但 NO2 在 P0T 的 APPLY 超时，115.266 s 后
  退出。原清理器把未初始化 grant/校准偏移也判为失败，实际 STOP 已响应；另存
  lifecycle-only 复核，显式撤销许可证后四板 stopped/config ACK/grant inactive
  全部成立，不把未初始化偏移认定为校准通过。
- 当前硬件验收：`p3-r2` 使用正式 `resume` 入口，复用当前包和成功 OTA，并重跑
  软件复位、拓扑、校准及短帧闭环。80.235 s 完成，短帧 passed/closed_loop/
  realtime 均 true，strict_gates_passed=true，无 diagnostic failure；STOP 后
  原生 SD 字节校验通过。该时长是跳过 build/OTA 的恢复流程，不能写成全流程耗时；
  未增大超时或修改门禁，初始化间歇超时的根因仍未解决。
- 自主交接对照：两轮总交接为 12904.364/12735.268 µs，相对前轮一增一减，不能
  宣称交接总时长改善。STOP 进入至 INSTALL 返回为 10478.576/10515.896 µs，
  准备调用体合计 751.716/1118.864 µs，其余包含跨周期调度、Core0 工作和等待。
  四板每轮各记录 34 条、无漏采，transport missing 增量均为 1/1/1/1；两轮完整
  窗口三项门禁均 false。首次 START 至最后 STOP 无 SCPI 查询，全部 STOP 后
  原生 SD 保存及字节一致性复核通过，临时 grant 已撤销，NO5 未操作。
- 耗时构成：两轮 NO1 自主 RUN 峰值为 552.520/657.524 µs，OTHER 峰值为
  1458.756/1490.044 µs。后者 context 769→513 对应普通 resident
  CYCLE_BOUNDARY→RUNNING，其中 RefMem 发布为 557.676/601.760 µs。所选峰值的
  CRC/BEGIN calls 均为零，因此只能说明峰值已落到其他调用，不能据此给出 CRC
  的单独耗时或提速倍数。全窗 TDMA overrun 增量为 490/471，deadline miss 为
  473/446；从第一条自主样本到末样本两类增量均零，不能用该子段替代整窗验收。
  整表/TDMA 预算仍由 `PROJECT_CORE1_PROFILE_1500US_CYCLES` 和
  `PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES` 定义，板端快照为 1500/850 µs。
- 下一 gate：继续 `VDC-TIME-002`，独立捕获成功准入而不依赖总峰值选中，定位普通
  origin 的 RefMem 发布及完整相位成本，压缩 STOP→INSTALL 的有界调度间隙；
  补齐边沿/SD 波形和其他物理配置，保持 DMA 退休、CRC/grant 和取消门禁。
  本轮未接通新的自主 DPLL 输入，不能声明四板实际输出锁相。

### VDC-PROGRESS-20260914-018 — 冻结邮箱有界合批与交接对照

- TODO task ID：`VDC-TIME-002`。
- 状态：IN PROGRESS。邮箱合批已取得当前拓扑的时延对照，完整交接连续性、原点相位
  超限及其他物理配置继续保留门禁；`VDC-TIME-003/004` 和命令接线仍未开放。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-origin-mailbox-batch/`；
  `current-plan.json` 绑定前序 manifest、STOP、源码和固件包，逐轮记录见
  `handoff-r1-review.json` / `handoff-r2-review.json`，对照见 `review-final-r1.json`。
  以下数值均为本轮测量快照，非事实源，不构成 WCET 或物理边沿误差界。
- 实现：`tdma_pio_spi_phys_origin_mailbox_batch()` 每次最多检查
  `TDMA_ORIGIN_PREPARE_MAILBOX_BATCH` 个冻结邮箱，工作量不随编译容量无限增加。
  仍逐个验证 magic/version/class/source/target/CRC；检查长度、容量和游标后才寻址。
  STOP 保留在下一 poll，继续经过 owner grant/config/clock 复验，旧 DMA 退休后
  才复用 persona union。未增加 PIO/DMA、静态 RAM、时间戳资格或 DCO 应用。
- 软件与构建：29 项相关测试通过；新测试在编译容量 2 至 8 下覆盖全部准入节点数、
  每邮箱逐字节损坏、重算合法 CRC 后的非法结构/来源/目标、后批损坏不能提前读取、
  长度/NULL/游标/容量越界和输入不变。既有 grant 撤销、代际/clock/expiry 变化、
  STOP 和构造任务取消回归继续通过。容量 6/8 的 A/B 与 boot 增量构建分别
  7.718/7.219 s；预留堆后 RAM 余量仍为 4716/956 B，physical poll 静态栈仍 400 B，
  SCRATCH_X 无数据增长。容量 6 实板使用四节点；其余物理节点配置不继承 HIL 结论。
- 部署身份：增量构建保留 build ID `20260914104205`，不能只凭 build ID 区分切片。
  本轮源码指纹为 `c5d553099a2e1babbe10423ee8ef61a34fbf96397eec4fb973db5fb531b42355`，
  新包 SHA-256 为 `b65a4b27ef35dd638d362f1693b935a6b9653c529be3102c9bec87bf3944e4d1`；
  archive、当前包、P3 凭证与四板 OTA 完成记录共同绑定本轮固件。
- 四板 quick P3：外部 196.578 s，短帧 passed/closed_loop/realtime 三项均 true，
  本轮 strict_gates_passed=true，无 diagnostic failure。四板原生记录各 14 条、
  无漏采，STOP 后 SD 字节一致。前轮严格校准超时仍保留在其封存目录；本轮通过
  不表示该间歇性失败根因已修复，也不关闭全表 WCET。
- 交接对照：两轮 NO1 trial/config 分别 36/71、52/77；软件总交接由前轮
  16868.584/16933.140 µs 降至 12444.264/12996.216 µs，分别缩短 26.23%/23.25%。
  mailbox 调用次数从各 4 次降为各 1 次，首次 mailbox 至 STOP 从约 6 ms 降至
  1511.436/1501.712 µs；合批调用体为 23.872/94.024 µs。第二轮初始等待
  1003.964 µs，比第一轮 429.964 µs 更长，不把总时差全部归因于 CRC 或合批。
  全部准备调用体为 690.480/1198.268 µs；STOP 进入至 INSTALL 返回仍为
  10502.864/10490.540 µs，缩短邮箱检查尚未缩短这段软件停环区间。
- 预算边界：本轮板端整表为 1500 µs，TDMA WCET 预算为 850 µs，事实源分别为
  `PROJECT_CORE1_PROFILE_1500US_CYCLES`、`PROJECT_CORE1_PHASE_TDMA_WCET_CYCLES`
  及板端时钟；不是早期整表方案的 500 µs。两轮邮箱调用体小于该预算，但 NO1
  完整 TDMA 相位峰值为 1558.196/1937.848 µs，全窗 overrun 增量 497/472、
  deadline miss 增量 474/446。三块 follower 该相位两类增量为零。稀疏原生样本
  与单个 peak 不能证明所有调用的 WCET，更不能以邮箱局部耗时代表完整相位通过。
- 连续性与收尾：每轮四板原生记录各 34 条、无漏采，首次 START 至最后 STOP
  之间无 SCPI 查询，STOP 后 SD 字节一致。r1 的 transport missing 增量依 NO1
  至 NO4 为 1/1/0/0，r2 为 1/1/1/1；两轮完整窗口三项均 false，不提升稳定子段。
  最终四板 STOP/config ACK、临时 grant inactive，NO5 未操作；未采集新的 DPLL
  trace 或物理边沿波形，不能据此声明实际输出锁相。
- 下一 gate：在 `VDC-TIME-002` 内继续定位 STOP→INSTALL 的交接间隙与 NO1 完整
  相位超限，补齐板端边沿/SD 波形证据和其他物理配置；保留 grant/CRC 拒绝、DMA
  退休与构造取消门禁，完整切换窗口闭合后才进入后续时间输入准入。

### VDC-PROGRESS-20260914-017 — 自主 origin 交接分阶段计时

- TODO task ID：`VDC-TIME-002`。
- 状态：IN PROGRESS。完成软件交接计时切片；完整窗口连续性未通过，尚未测得物理
  边沿间隙或通用 WCET，不开放 `VDC-TIME-003/004` 和命令接线。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-origin-handoff-timing/`；
  `current-plan.json` 绑定前序 manifest、STOP、源码与部署包；两次复核见
  `handoff-r1-review.json` / `handoff-r2-review.json`，主控汇总见
  `review-final-r1.json`。以下数字均为本轮测量快照，非事实源。
- 实现：`tdma_origin_handoff.c` 由 Core1 单 writer 记录每个准备阶段的首次进入、
  调用次数和累计调用体耗时，guard 保护跨核快照；DONE/FAILED 冻结到下一有效 begin。
  `READ:CALibration:ORIGin:HANDoff?` 仅 STOP/config ACK 后可读，BUSY 表示未完成。
  调用体包含 owner 授权复验和 physical poll，记录器 bookkeeping 在计时体之外；
  总交接包含跨周期等待、Core0 构造与排队、其他任务和仪器开销，不能全部称为空闲。
  本切片没有修改 grant 准入、wire、PIO/DMA 或 DCO，也没有测量 Core0 builder CPU。
- 软件与资源：相关测试 22 项通过，含时间低字回绕、累加/终态冻结、非法顺序/溢出、
  grant 撤销和 STOP 读回准入。容量 6/8 的 A/B 与 boot 增量构建通过，分别为
  22.359/21.875 s；记录器增加 BSS 160 B，预留堆后主 RAM 余量为 4716/956 B。
  SCRATCH_X 未增加数据；目标与资源原件见 `capacityN-checkpoint.json`。
  部署容量 6，build `20260914104205`；源码指纹为
  `7912871406ab71ad0b55cf88f6694e6f23dc1fbdee87264a533bd72287f04ff3`。
- 四板 quick P3：外部 184.656 s，短帧 passed/closed_loop/realtime 三项均 true，
  四板原生记录各 14 条，无漏采，STOP 后 SD 字节一致。但 strict_gates_passed=false：
  coarse CLK level 7 时 NO2 的 `SYSTem:TDMA:RING:TOPology 4,1,1` 超时，随后返回
  `-200,"Execution error"`，失败原件保留于 `p3-r1/diagnostic.json`。诊断流程完成
  及 quick 凭证有效不代表严格校准通过，未重复 P3 覆盖该失败。
- 两轮自主交接：NO1 trial/config 为 36/64 和 52/70，总时长分别 16868.584 和
  16933.140 µs；累计 Core1 调用体为 1213.440 和 1146.284 µs，体外时间为
  15655.144 和 15786.856 µs。四次 mailbox 检查调用体仅 163.152/134.004 µs，
  从首次 mailbox 进入到 STOP 阶段进入却跨 5982.020/5914.160 µs。STOP 阶段进入
  到 INSTALL 返回为 10554.988/10622.972 µs；这些软件边界不等于 wire 边沿间隙。
- 完整窗口：每轮四板原生记录各 34 条、无漏采，NO1 各有 28 条 persona 16 样本，
  接收序列持续增长，全部 STOP 后 SD 字节一致。两轮四板 transport missing 均各
  增加 1，完整窗口短帧/连续性三项均 false；不得提升稳定子段或以记录无漏采掩盖
  传输 missing。首次 START 至最后 STOP 之间无 SCPI 查询；最终四板 STOP/config
  ACK，临时 grant inactive，NO5 未操作。未增加 DPLL trace 或正式时间戳资格。
- 下一 gate：先在 `VDC-TIME-002` 合并有界的冻结邮箱检查，验证准入容量的相位预算、
  坏 CRC 拒绝、每次 poll 的 grant 复验及 STOP 生命周期，再复测完整切换窗口。
  四次检查合为一次名义上可减少三个静态周期，但这是待验证估算；其余准备动作仍需
  分阶段，不能将约 1.2 ms 的累计调用体整体塞入单个 TDMA 相位。

### VDC-PROGRESS-20260914-016 — 构造中取消的板端诊断探针

- TODO task ID：`VDC-TIME-002`。
- 状态：IN PROGRESS。补齐当前四板拓扑上 NO1 构造任务的确定性取消证据，父任务
  继续保留其他硬件配置与交接时延门禁；不提升自主时间输入或正式锁相状态。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-build-cancel-probe/`；前序 manifest
  由 `initial-checkpoint.json` 绑定，目标身份见 `current-plan.json`，逐轮复核见
  `probe-r1-review.json` / `probe-r2-review.json`，主控复核见 `review-final-r1.json`。
  以下数字均为本轮快照，非事实源。
- 实现：新增显式有限诊断 `CALibration:ORIGin:TRIAL:BUILDCancel`，通过既有
  Calibration grant 和 Core1 owner 准入，冻结 trial/config 代际。Core0 在真实
  builder 的 emitting pass 写出首个描述符后让出执行；Core1 在 BUILD_STEP 发现
  PAUSED 后进入现有故障／STOP 路径，STOP 未得到 worker ACK 时保留 workspace。
  Core0 仅恢复带 PAUSED 标记的 CANCELLED 任务，清空 entry 并先撤销活跃标记再
  发布 IDLE。普通 TRIAL 不启用探针；没有新增 PIO/DMA、时间戳资格或 DCO 应用。
- 诊断读取：`READ:CALibration:ORIGin:BUILDCancel?` 仅在 ring STOP/config ACK
  且 worker IDLE 后可用，读取保留的清理结果，不再访问已复用的 persona union。
  此接口记录生命周期事实，不提供物理边沿时间或微秒级取消时延上界。
- 软件：相关测试 20 项通过；容量 2/6/8 的真实构造矩阵额外覆盖 10/50/70 组探针
  场景，共 130 组，包括暂停前后取消、取消早于 claim、旧 PAUSED 状态不能复活
  writer，以及 poison 后重新构造逐字节一致。此前 5824 组逐块取消继续通过。
  记录与命令分别见 `host-r1` / `host-r2`；本轮没有用 host 调度交错代替芯片测量。
- 资源与构建：容量 6/8 的 A/B 与 boot 增量构建分别耗时 17.985/17.500 s。
  新增静态 RAM 32 B；预留堆后主 RAM 余量为 4876/1116 B；SCRATCH_X 未增加数据，
  详见两份 `capacityN-checkpoint.json`。未测容量不继承本轮目标资源或 HIL 结论。
  当前部署容量 6，build `20260914101340`；源码指纹为
  `8b55a9557bc99ec7bde13b7ddd7be38962fe03efdddc98efb1334f2d67779173`。
- 四板 quick P3：内部 184.464 s、外部 184.656 s，构建复核 2.945 s、OTA 105.547 s；
  strict_gates_passed 和短帧三项标记均 true。四板原生记录各 14 条，无漏采，STOP
  后 SD 字节一致。quick 时间不包含首次增量编译及额外诊断实验，也不证明整表 WCET。
- 两轮板端探针：NO1 的 trial/config 分别为 36/71 和 52/78，均在 emitting pass
  写出 1 个描述符后暂停，记录 1 次取消未确认、入口清空成功、最终 RETIRED/IDLE。
  第二轮新代际证明已重新 ARM 并建立新构造任务。每轮四板原生记录各 34 条，无漏采，
  全部 STOP 后 SD 一致；采集期间无 SCPI 查询。这里只证明 NO1 作为 origin、当前
  拓扑和固定构造块的暂停取消，不宣称所有板卡角色或任意指令竞态已实测。
- 保留失败：两轮主动取消的连续性三项标记均 false；r2 另有
  `explicit startup barrier timed out`，不得由取消清理成功覆盖。P3 后的辅助封装
  曾因命令日志与 STOP 结果同名退出，实际 STOP 成功；失败原件保留于
  `p3-finish-r1.json`，后续命令日志改用独立名称，SD 保存已完成。
- 恢复对照：普通短帧 r1 已在约 1.26 s 内取得三个连续健康样本，但 NO1 最后一个
  原生样本被主机 STOP 取消，终止原因 2、33/34 条，因 collection error 汇总为
  启动屏障失败。保留 r1 原件；本轮 `collect_normal.py` 在停止截止时间中计入既有
  启动触发预算，未改变启动健康门限，也未增加实时查询。r2 外部 41.844 s，四板各
  34 条且无漏采，启动屏障和短帧三项标记均 true，STOP 后 SD 字节一致。该恢复
  使用普通 origin，不能代替自主时间输入；NO1 TDMA overrun、上游迟到与部分 DPLL
  相位超限仍保留于原始记录，不宣布全表 WCET。最终四板 STOP/config ACK，临时
  许可证 inactive，NO5 未操作。
- 下一 gate：`VDC-TIME-002` 的其他硬件配置与实测交接时延上界，然后进入
  `VDC-TIME-003` 的完整窗口连续性与边沿误差界；`VDC-TIME-004` 和命令接线仍未开放。

### VDC-PROGRESS-20260914-015 — 真实 DMA 构造块取消与复用补证

- TODO task ID：`VDC-TIME-002`。
- 状态：IN PROGRESS。完成当前软件补证，不关闭实际硬件配置/取消门禁。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-build-cancellation/`；前序 manifest
  由 `baseline-plan.json` 绑定，当前身份和主控复核见 `current-plan.json` /
  `review-final-r1.json`。以下计数与耗时均为本轮快照，非事实源。
- 变更：扩展 `test_tdma_origin_build_job.py` / `tdma_origin_build_graph_cases.c`，
  生产 builder 仅在 host 编译时重命名单步入口，真实 Core0 job 经过包装器在每个
  块前后注入取消。覆盖最终块已写出 entry、worker 尚未发布 READY 的窗口；取消
  返回 false 时禁止 take/request/复用，worker 退休后 entry 全部失效；将 builder
  和输出填入 poison 后，迟到入口无写入，再构造与正常图逐字节一致。固件未修改。
- 软件结果：容量 2/6/8 分别通过 448/2240/3136 组，共 5824 组；对应 26 张基准图，
  覆盖各容量下的运行节点数、local slot 0、连续 active mask 和记录开关。未将此
  子矩阵扩称为所有物理配置。连同既有三层 DMA STOP、物理 owner 退休、raw 和准入
  测试共 19 项通过。原有 mock 写入中断与本次真实块边界互补，均不是芯片实测延迟。
- 反向验证：仅在 `out/` 副本去掉最终取消清理，真实构造测试触发断言；原始退出码
  和 stderr 保留于 `mutation-result-r2.json`。r1 辅助探针因 pytest 临时目录名错误
  未编译，失败原件保留；修正入口后成功检测反例，没有修改生产代码以制造失败。
- 构建与身份：复用容量 6 live build，构建复核通过；A/B map/ELF/package 归档于
  `build-archive/`。build 仍为 `20260914093110`，包 SHA 与前序完全一致；测试变更
  后源码指纹为 `969902c55fc43dc652dc7feb60dbe8845245e2528cddfb0d30581e3f25eb6365`，
  为该指纹重新执行 P3，不沿用前序 receipt。
- 四板 quick：内部 181.431 s、外部 181.656 s；构建复核 2.401 s、OTA 106.033 s。
  strict_gates_passed 和短帧 passed/closed_loop_passed/realtime_gate_passed 均 true。
  四板原生记录各 14 条、无漏采，全部 STOP 后 SD 字节一致；config ACK、临时许可证
  inactive。SCPI 仅控制流程，未操作 NO5。原始调度计数仍单独保留，quick 聚合通过
  不等于整表 WCET、前序间歇 SCK 失败根因或正式锁相已闭合。
- 下一 gate：`VDC-TIME-002` 的芯片上构造中取消、配置切换及有界交接；之后才开放
  `VDC-TIME-003/004`。本轮没有新增自主 timestamp、命令运输或 DCO 应用。

### VDC-PROGRESS-20260914-014 — RefMem 向量快照收敛与两种容量快速验收

- TODO task ID：`VDC-TIME-002`、`VDC-SCHED-001` 的前置资源/调度修复。
- 状态：IN PROGRESS。当前切片完成，父任务未关闭，`VDC-TIME-003/004` 保持 PENDING。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-refmem-vector-projection/`，主控复核见
  `review-final-r1.json`，身份见 `current-plan.json` / `capacityN-checkpoint.json`。
  以下容量、耗时、帧大小及计数均为本轮快照，非事实源。
- 变更：VDC owner 新增 `vdc_dpll_manager_get_vector_snapshot()`，在既有发布 guard
  内有界复制旧 RefMem 向量实际消费的字段；排除完整 path table、observation matrix
  和无关诊断。RefMem 仍由 Core1 发布，保留代际准入、交替向量、CRC 和 seqlock；
  未增加静态副本，未改变 wire 布局、PIO/DMA、DCO owner 或从机命令接线。
- 软件：容量 2/6/8 的生产 getter/fill 提取测试，与固定旧源码生成的每容量 32 组
  向量 CRC 对照通过；另验 NULL、无效发布、奇数 guard、有界重试和复制后写入/回绕。
  这属于序列化 CRC 对照，不宣称逐字节穷举等价。相关 Python 共 22 项、原有 RefMem
  VDC vector host C 测试通过，命令和日志分别见 `projection-tests-r2` / `vector-tests-r1`。
- 目标资源：容量 6/8 的 A/B 与 boot 均通过，投影在 ARM ABI 下均为 632 B，原完整
  snapshot 分别为 1384/1584 B。容量 8 旧 realtime 和嵌套向量 helper 栈帧已合计
  2088 B，超过保留的 2048 B Core1 栈；这尚不证明实际内存破坏或超时因果。新 helper
  被编译器内联，realtime 帧为 1056 B，getter 为 48 B；未将 libc/ROM、调度祖先和
  中断嵌套计入整栈证明。`.su`、反汇编和 map 均保存，SCRATCH_X 不新增数据。
  主 RAM 预留堆后余量仍为容量 6 的 4908 B、容量 8 的 1148 B。
- 构建失败：r1 辅助配置覆盖 SDK CPU flags，汇编失败；r2 RAM 代码增长使 BSS 跨越
  对齐边界，容量 8 链接溢出。两次未部署，日志及失败 map 保留。合并重复向量 flags
  计算后增量 r3 通过，容量 8 `.data` 相对旧版本减少 72 B，未借用栈或其他 owner RAM。
- 当前源码指纹：`8e29cee4206bcb2d466e3c8c4f2e9f82f679f200528256df296d6de10b36d108`。
  容量 8 build `20260914093008`，容量 6 build `20260914093110`；package SHA 分别
  由独立 archive 和 checkpoint 绑定，P3 复核未更改归档包。仅操作 NO1–NO4。

| 配置 | quick 内部 / 外部耗时 | 构建复核 / OTA | strict_gates_passed | 四板 RefMem 保留峰值上限 |
|---|---:|---:|---|---:|
| 容量 8 | 185.370 / 185.625 s | 3.250 / 106.200 s | true | 72.728 µs |
| 容量 6 | 177.585 / 177.844 s | 2.731 / 98.860 s | false | 70.064 µs |

- 两轮短帧 passed/closed_loop_passed/realtime_gate_passed 均 true，各板原生记录
  14 条且无漏采，STOP 后 SD 字节一致。容量 6 的严格失败为 TRN-01 SCK 与 TRN-03
  replay 选行，最小 follower margin 为负；没有用本轮容量 8 成功覆盖该间歇失败。
  quick 使用可写增量构建目录；上述时间不包含首次构建和额外诊断采集，不能将本轮
  两种容量补测总和当作每次日常验收成本。与前序 513 s 相比，单轮流程约减少六成半。
- 实际更新补测：分别使用固定历史诊断矩阵、显式 provisional DPLL 和 clock evidence，
  普通 origin 下各板 26 条原生 TDMA 记录、76 条 DPLL trace，更新序列持续增长；
  first START 到 all STOP 无查询，TDMA SAVE 释放 StorageAO 后再保存 DPLL，读回一致。
  两容量四板 RefMem 自身 overrun/deadline miss 增量均零，未被隔离；保留峰值上限
  分别为 72.728/74.684 µs，低于既有 96 µs 预算。最终容量 6 的两种旧向量均读回
  非零 publish/source update sequence。此结果支持修复有效，不代替自主更新 WCET。
- 保留边界：尽管 quick 聚合标记与短帧通过，原始记录仍显示 NO1 TDMA overrun、
  VDC/DPLL/RefMem start miss；不得写成整表 WCET 通过。普通主板内部状态为 LOCKED，
  三从板为 CHECKING，命令接收和应用增量仍零；未验证自主时间输入、物理输出锁相
  或正式 quality。最终四板为当前容量 6 STOP、config ACK、临时许可证 inactive。
- 下一 gate：继续 `VDC-TIME-002` 的运行配置和有界取消；保留严格校准和上游迟到
  缺口。日常默认 quick、复用增量目录；OTA 已是主要主机耗时，进一步加速须以刷写
  各阶段原始计时另开工具切片，不能通过略过源码、设备身份或硬件判据放行。

### VDC-PROGRESS-20260914-013 — 编译容量矩阵和上限容量四板预采

- TODO task ID：`VDC-TIME-002`；`VDC-TIME-003/004` 保持 PENDING。
- 状态：IN PROGRESS。仅补证，无固件或正式时间输入变更，未接入从机命令。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-capacity-matrix/`，主控复核见
  `review-final-r1.json`。以下数字为本轮快照，非事实源。
- 身份：源码指纹与前序 quick 加速切片一致，仍为
  `87e98c7fef8e30d2607079b3d2c9946ec98c1eeb9f1bfcb1f99299bbc225aea2`。
  各容量 package、A/B map/ELF 和实际地址绑定见 `capacityN-layout.json`；容量
  6 的封存 build 仅读取。容量 2/8 同时构建产生相同时间 build 字符串，配置身份
  必须同时使用容量、package SHA 和布局，不能只按 build 字符串互换。
- 软件与资源：原始计时/记录/增量构造测试 54 项、准入测试 10 项通过。容量
  2～8 的 A/B 链接全部通过，保留堆、Core1 栈和既有 SCRATCH_Y 快照；12 个
  live origin 缓冲的真实地址、对齐和非重叠检查通过。实际地址构造共 1,939,224
  组，覆盖每个准入运行节点数/本地槽、连续 owner mask、tail/prefix、选定 guard/
  abort 和记录开关；单步最大 22 个描述符。非连续 active mask 的覆盖沿用前序
  host 矩阵，不扩张成本轮实板拓扑结论。

| 编译容量 | BSS | 预留堆之后主 RAM 余量 | 描述符峰值 / 分配 | literal 峰值 / 分配 |
|---:|---:|---:|---:|---:|
| 2 | 458796 B | 10196 B | 274 / 320 | 125 / 140 |
| 3 | 460088 B | 8904 B | 284 / 320 | 127 / 140 |
| 4 | 461404 B | 7588 B | 294 / 320 | 129 / 140 |
| 5 | 462728 B | 6264 B | 304 / 320 | 131 / 140 |
| 6 | 464084 B | 4908 B | 314 / 320 | 133 / 140 |
| 7 | 466464 B | 2528 B | 324 / 352 | 135 / 140 |
| 8 | 467844 B | 1148 B | 334 / 352 | 137 / 148 |

- 四板 quick：容量 8 build `20260914085410` 配置为四节点运行，NO5 未操作。
  内部流程 218.486 s、外部命令 219.500 s，增量复核 9.573 s、OTA 111.963 s。
  此时其他容量仍在编译，不能把与前序 185.790 s 的差异归因为固件容量。receipt
  流程完成，但 `strict_gates_passed=false`；启动稳定门通过，NO1 RefMem 曾达
  26100 cycles，超出 24000 cycles 预算，随后被既有调度器隔离。四板原生 SRAM
  各 14 条、无漏采，STOP 后 SD 一致；短帧严格失败保留，不能用诊断完成替代。
- 自主预采：有限许可证下各板 34 条原生记录；切换期间每板一次真实 missing，
  完整窗口失败。旧普通 persona 判据的 mismatch 另行保留。预选 3～6 s 诊断
  窗口没有新增 missing/reject/TDMA overrun，不能替代整窗。NO1 原始记录 epoch
  1、sequence 8149～8155 连续，timer 读取夹区为 252 ns，TDMA 完整相位峰值
  603.020 µs；这些原始计时不是物理边沿或共同时间。自主 DPLL trace 为零，命令
  接收/应用增量仍为零。原生 TDMA SD 与主机导出后写入 SD 的 raw 副本分开核验，
  后者两次读回一致。
- 恢复与限制：容量 8 普通模式恢复后，旧 raw 各 age 均 UNAVAILABLE；运输闭环
  通过，四板各 26 条记录。此入口未要求 DPLL schedule gate；NO1 中段 TDMA
  overrun 119、VDC start miss 105，不能宣称全表 WCET 通过。随后刷回容量 6
  build `20260914084228` 并完成普通短帧恢复及 SD 一致性；同样保留中段 TDMA
  overrun 176、VDC start miss 191。最终四板 STOP、config ACK、临时许可证
  inactive，运行期间无 SCPI 查询。
- 辅助脚本失败：首次回退 STOP 导出缺少 checkpoint 指纹；补齐后冷启动尚无
  staged phase，部分 origin 查询 UNAVAILABLE。两次失败原件保留。单独核对
  冷启动 STOP/config ACK 后再装载矩阵，最终配置后的相位/许可证检查全部通过；
  没有把冷启动缺省值冒充已装载配置。
- 下一 gate：`VDC-TIME-002` 的对应物理拓扑/运行配置、有界取消和容量 8 RefMem
  失败仍未闭合。目标链接缺口已补齐；真实总线/边沿、切换连续性、同圈输入和正式
  锁相继续按原依赖推进。日常切片复用匹配配置的可写构建目录并使用默认 quick；
  本次容量矩阵作为独立补证，不加入每轮快速验收，也不重建已封存产物。

### VDC-PROGRESS-20260914-012 — quick 验收目录扫描加速

- TODO task ID：`VDC-TIME-002`、`VDC-VERIFY-001` 的快速迭代支撑切片。
- 状态：扫描加速和当前四板 quick 验收完成；`VDC-TIME-002` 保持 IN PROGRESS，
  长期锁相目标未完成。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/p3-quick-scan/`，对照前序
  `dpll-raw-capacity/p3-r1/timing.json`。以下时间和数量均为快照，非事实源。
- 原因与变更：此前已经使用 QUICK_DIAGNOSTIC；主要开销来自 USB 命名空间和
  Flash 清单检查先遍历历史产物再过滤，文档锚点扫描也进入历史目录。改为
  `os.walk` 下行前剪枝，保留源码违规规则；文档的 `SCAN_EXCLUDE_DIRS` 排除
  `out/`，其构建或测试副本不能补足真实源码缺失的锚点。未改 P3 采集数量、超时
  门限、OTA 块大小或短帧判据，未操作 NO5。
- 软件验证：旧实现的三处目录遍历由负测复现；改后相关 28 项测试通过，覆盖忽略
  目录不进入、真实违规检出、Windows C 扩展名匹配和仅旧产物存在的锚点拒绝。
  USB 检查为 0.453 s、Flash 清单为 0.531 s；文档回归从上一切片 139.250 s
  降到 0.546 s。Flash 清单结果与之前完全一致。检查器和 skill 副本通过
  `--skill-sync`，登记表模板同步现有 canonical；没有变更契约登记状态。
- 构建身份：源码指纹
  `87e98c7fef8e30d2607079b3d2c9946ec98c1eeb9f1bfcb1f99299bbc225aea2`，build
  `20260914084228`。新容量 6 首次完整构建为 93.469 s，A/B RAM、PIO 程序和
  Flash 清单与前序一致。package 与 map 绑定见 `source-checkpoint-r2.json`。
- 同类 quick 对照：增量构建复核由 326.346 s 降到 3.190 s；含该构建复核、四板
  OTA、复位、拓扑、校准与短帧的 P3 内部总时长由 513.447 s 降到 185.790 s，
  缩短 327.657 s，约 63.8%。外部命令总耗时为 186.062 s，四板 OTA 占 105.978 s。
  首次完整构建另计，两段实测合计约 280 s；不能把有 cache 的 quick 耗时当作
  从空目录首次编译的耗时。各阶段明细见 `review-final-r2.json`；初版 review 对
  重复阶段名称取最后一项，复核版已改为累计，原件保留。
- 硬件结果：当前源码 quick receipt 的 `strict_gates_passed=true`、失败列表为空，
  短帧 passed/closed_loop_passed/realtime_gate_passed 均为 true。四板各 14 条
  原生 SRAM 记录完整、无漏采，全部 STOP 后 SD 字节一致，最终 config ACK、
  临时许可证 inactive。该 quick 范围的门禁通过不代表 DPLL 命令、共同时间或
  正式锁相已完成。
- 下一 gate：返回 `VDC-TIME-002` 的剩余目标容量和硬件配置，再按既有顺序处理
  全窗切换连续性、边沿误差和同圈输入。快速迭代复用同配置构建目录，各轮验收
  证据另存；源码改变后仍运行当前指纹的 P3，不能用旧 receipt 或 replay 放行。

### VDC-PROGRESS-20260914-011 — 目标容量 RAM 缺口与 UI 状态副本收敛

- TODO task ID：`VDC-TIME-002`、`VDC-CONFIG-001`、`VDC-TDMA-001`、`VDC-EVID-001`。
- 状态：`VDC-TIME-002` 保持 IN PROGRESS；`VDC-TIME-003/004` 保持 PENDING，
  未关闭长期锁相目标。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-raw-capacity/`。`plan.json` 绑定
  前序 manifest，`current-plan.json` / `source-checkpoint-r2.json` 分别绑定两种容量。
  以下容量、字节数、组合数及构建号均为本次快照，非事实源。
- 原始失败：未修改 UI 时，容量 8 的实际 A 链接主 RAM 超出 2428 B；失败 map、
  日志和源码指纹保留于 `build-capacity8-r1`，没有部署该失败产物。B 链接尚未产生
  map，不能伪造修复前 B 对照，也不能从容量 6 的 host 测试推断容量 8 可部署。
- 资源修复：UI 只消费触发器的 19 个标量，却持有包含对齐序列表的完整 3072 B
  TriggerVector。Sync Trigger owner 新增 `sync_trigger_status_t` 和
  `sync_trigger_get_status()`，沿用现有临界区复制这些状态；UI 改为保存紧凑副本。
  原完整 getter、TriggerVector、序列表和硬件 owner 不变；未借用 Core1 栈或关闭
  原始计时记录。host C 测试覆盖空指针、未初始化、极值、锁内复制、解锁后源更新和
  UI 实际消费字段的等价性，`status-tests-r1` 通过。
- 目标链接：修复后容量 6/8 的 A/B 和 boot 均通过。ARM ABI 的 status 为 52 B；
  容量 6 UI 从 7168 B 缩到 3392 B，BSS 回收 3776 B，预留 2048 B heap 后主 RAM
  余量为 4908 B。容量 8 UI 为 3592 B，BSS 相对失败 A map 回收 3576 B，heap 后
  余量为 1148 B；新 A/B 主 RAM 布局一致。gdb 复核完整 TriggerVector 布局未变，
  SCRATCH_X 未分配数据，Core1 栈和 SCRATCH_Y/主核栈边界通过。详见
  `resource-review.json` / `source-checkpoint-r2.json`，回收空间尚未分配给命令缓冲。
- 配置几何：实际 owner 使用连续 active mask；在其固定 PIO/SM/DMA 分配下，覆盖
  每个运行节点数/local slot、准入 tail/prefix、选定 guard/abort 边界和记录开关。
  容量 6 为 359940 组、容量 8 为 532140 组，最大单步均为 22 runs；最大 run/
  literal 分别为 314/133 和 334/137。探针调用真实生产 builder，使用合成 SRAM
  地址；不证明实际总线延迟、SM 启动偏移或边沿误差。首次容量 6 探针有编译错误，
  首次 checkpoint 有失败 B map 路径错误，均保留原件；修正辅助入口后成功，未改变
  生产图逻辑。原有 active mask、raw 退休/回绕和取消测试仍由前序证据分别证明。
- 当前源码：指纹
  `61dc542b045cc01fe402269b4b8bbdd1c2e1a07c332f834a81cf9f07d40774e8`；容量 6 build
  `20260914080902`，容量 8 build `20260914075453`。两包 SHA 在 `current-plan.json`
  中区分，map 绑定见 `source-checkpoint-r2.json`。当前硬件回归只部署容量 6 到 NO1–NO4；容量 8
  只有目标链接，没有本轮 HIL。未操作 NO5。
- 当前 P3：quick 流程完成，SCK 训练、replay 矩阵和短帧的
  passed/closed_loop_passed/realtime_gate_passed 均通过；STOP 后四板原生 TDMA SD
  一致。`strict_gates_passed=false`，本轮唯一失败项为总时长 513.447 s 超过
  450 s；构建复核为 326.346 s、OTA 为 103.567 s，详见 `p3-r1/timing.json`。
  不能把本轮 SCK 成功外推为间歇失败根因已解决，更不能直接提升正式锁相。
- 自主预采：为保持与前序配置对账，显式使用 `current-plan.json` 绑定的既有诊断
  matrix；没有把它替换成本轮新矩阵。四板各 34 条原生记录完整，无漏采，SD 字节
  一致。NO1 保留 epoch 1、序列 8181–8187 的完整 raw 记录，读取区间均为 252 ns，
  仅为 timer 访问诊断。自主整窗三项判据仍失败，四板各一次真实 missing；预选
  3–6 s 中段接收增量为 917/917/917/918，拒绝、missing 和各相位 start miss/
  overrun 无增长。主板 TDMA RUN 保留峰值为 594.996 µs，不能从此次单窗变化推断
  UI 修复降低了实时 WCET。四板自主 DPLL trace 为零，真实更新路径仍未接通。
- 导出修复：原生 TDMA SD 通过后，主机 raw 副本上传的 BEGIN 和首次 DATA 出现
  应答超时。辅助脚本误用了通用应答过滤器，合法复合应答不能返回；事务状态证明
  BEGIN 和首块 DATA 已执行。改用既有 `storage_file_upload` 完整应答入口，核对
  transaction/path/length/CRC/offset 后续传，两次 SD 读回与主机副本一致。两次失败
  均保留；该文件仍是主机导出后写 SD 的副本，不是板端原生 recorder。没有修改生产
  SCPI 工具或增加超时以掩盖问题。
- 普通恢复：三项短帧判据通过，四板各 26 条记录和 SD 一致；NO1 七个 raw age
  查询均 UNAVAILABLE，旧自主记录没有复活。普通中段仍有主板 TDMA overrun 198、
  VDC start miss 207，不能关闭全表 WCET。最终四板 STOP/config ACK、临时许可证
  inactive。全窗、中段、raw/SD 和失败对照见 `review-final.json`。
- 快速迭代：按用户要求，下一切片先降低 quick 验收自身耗时。优先修复源码扫描器
  遍历历史 `out/` 后才排除的目录开销，保持既有扫描范围和 P3 硬件判据，重新测量
  增量构建与完整 quick 流程；不能以跳过必跑门禁或提高超时门限替代提速。
- 剩余门禁：编译容量 2/3/4/5/7 尚待当前源码目标链接；容量 8 及其他硬件配置尚未
  验收，四板运行不能证明八块物理节点。`VDC-TIME-002` 的完整准入条件继续保留，
  后续才进入 `VDC-TIME-003` 的切换连续性及实际边沿误差界，再进入
  `VDC-TIME-004` 的 trailer/同圈输入。raw timer 读取不授予 COMMON_TIME 或 formal
  qualification；本轮不接 follower command，不据 RAM 修复宣布 DPLL 已锁相。

### VDC-PROGRESS-20260914-010 — 原始计时配置矩阵、重臂与归档退休

- TODO task ID：`VDC-TIME-002/003`、`VDC-TDMA-001`、`VDC-EVID-001`、`VDC-SCHED-001`。
- 状态：本次配置/生命周期补测完成；`VDC-TIME-002` 保持 IN PROGRESS，
  `VDC-TIME-003/004` 保持 PENDING，长期目标未完成。
- 日期：2026-09-14。
- 变更：扩展 `test_tdma_origin_raw_time.py`、`tdma_origin_raw_graph.c` 和
  `tdma_origin_record_frozen_cases.c`，未改变生产固件或 PIO 逻辑；测试与当前源码
  P3 凭证提交 `8ece6da`，文档另行提交。下述组合数、容量和时序均为快照，非事实源。
- 证据根：`out/HardwareAcceptance/20260914/dpll-raw-lifecycle/`。`plan.json` 绑定
  前一封存 manifest；`current-plan.json` / `source-checkpoint-r2.json` 绑定当前
  build `20260914071502`，源码指纹
  `0bc3adba3bd2da8a23bd79e485e7aaba32a6ba3e7ae0fcfc6af79feff3d5c700`，package SHA
  `8c11f84ced9724aec9f6f9057cfdf2baba265c6fe8cdcc8407cf8fade4759de4`。
- 配置矩阵：真实生产 builder 编译容量 2–8、各运行节点数的全部有效 active mask/
  local slot，组合 guard 的 7 个边界值、abort 的 3 个值及记录开关，共 124488 组
  构造通过；最大单步为 22 个 run，上限 24。当前容量 6 最大占 314/320 runs、
  133/140 literals；容量 8 最大占 334/352、137/148，详见 `matrix-review.json`。
  稀疏 mask/非零 local 的真实图执行与 mailbox overlay 对账通过。探针固定
  PIO/SM/DMA、地址、prefix 和 padding，不能推断全部几何或实际总线延迟已覆盖。
- 生命周期：生产 STOP/frozen read/persona/invalidate 路径覆盖 88 B raw record
  每个内部复制分割点的退休交错、guard 回绕、published_version 回绕、FAULT 原始
  诊断保留、旧 RTT 格式/epoch 拒绝和 persona 复用后 STOP 不复活旧归档；既有
  admission/build-job 有界取消回归通过。配置/图模型 49 项和生命周期/准入/构造
  15 项通过，完整命令见 `matrix-tests-r1`、`lifecycle-tests-r1`。
- 目标资源：A/B 构建通过，`.data`、`.bss`、heap 地址/大小与上一实现一致，见
  `layout-review.json`；本轮没有新增 RAM。目标链接仍只覆盖当前编译容量，其他
  容量只有 host 矩阵，不能借本轮四板 HIL 关闭全部准入配置的目标验收。
- 当前 P3：四板 OTA 和 quick 流程完成，receipt 为
  FOUR_NODE_TDMA_QUICK_DIAGNOSTIC；`strict_gates_passed=false`。SCK 训练及
  replay 行选择失败，没有满足飞行重装预算的实测行，完整失败保留于
  `p3-r1/diagnostic.json` / `p3-receipt-r1.json`。后续生命周期采集显式使用
  `current-plan.json` 绑定的既有诊断 matrix，未把旧校准或 quick 完成提升为严格
  校准通过。未操作 NO5。
- 两次自主预采：`resident-r1/r2` 均先普通启动，再有限自主许可，四板每轮各
  34 条 SRAM 记录完整、无漏采。全部 STOP 后原生 TDMA SD 字节一致；NO1 raw
  epoch 从 1 增至 2，各保留连续 7 条完整记录，序列分别为 8176–8182 与
  8122–8128，第二代 timer 晚于第一代。epoch 为本地原始归档代际，不是分布式
  session。FIFO 存在、arm 前 TX CS 为高、计时前后 high/low/high 一致；读取区间
  均为 252 ns，邻圈 arm 间隔分别为 1000.020–1000.636 µs 和
  1000.016–1000.600 µs，不能作为实际边沿偏移、抖动或锁相精度。
- 全窗失败：两轮均保留 `passed/closed_loop_passed/realtime_gate_passed=false`。
  四板每轮各有一次真实 receive_missing，启动/切换的拒绝及超限仍保留；旧
  evaluator 的普通 persona/software TX count 规则也不适用于自主，不能因此
  忽略真实缺失。预选 3–6 s 中段接收增量分别为 917/916/915/917 与
  917/918/916/916，拒绝/缺失以及各相位 start miss/overrun 无增长。中段通过不
  替代全窗，完整对照见 `review-final.json`。
- 耗时边界：两轮自主主板保留的 TDMA RUN 完整峰值为 652.024 µs 与 592.484 µs；
  本轮生产逻辑未变，同一 build 也有峰值变化，不能据此归因于某段代码或宣布
  优化。四板自主 trace 均为零，真实 DPLL 更新 WCET 仍未测得。
- 切换审计：`handoff-review.json` 重新解码上一 build 的原件，只将 missing 定位
  在普通转自主的采样区间，未测出精确停发时长。源码顺序为 MAILBOX → STOP →
  PERSONA → BUILD_BEGIN/BUILD_STEP → SEED → SMS → INSTALL；Core0 在固定上界
  内完成构造，并非每个 label 等一个 Core1 周期。普通服务和自主图共享 workspace
  union，必须证明安全准备与有界交接，不能边跑旧 DMA 边覆盖，也不能压掉 missing。
- 存储与恢复：先完成 TDMA SAVE/释放 StorageAO，再处理 DPLL，零 trace 如实保留。
  每轮 728 B raw 主机导出另存 NO1 SD、两次读回一致，明确区别于板端原生 recorder。
  `restored-r1` 普通短帧三项判据通过，四板各 26 条原生记录与 SD 一致；NO1 的
  raw age 查询全部为 UNAVAILABLE，后续 STOP 未复活旧归档。恢复的中段仍记录
  主板 TDMA overrun 228、VDC start miss 230 和 DPLL start miss 1，三项运输判据
  不代表全表 WCET 通过。最终四板 STOP/config ACK、许可证 inactive；各轮首次
  START 至全部 STOP 无 SCPI 查询采样。
- 主控复核：`audit.py` 从原生 `.bin` 重解码 board/build/epoch/CRC/长度，比较 SD
  字节，核对两代 raw、退休查询、源码/package 和 P3 引用 SHA；保留 quick 严格
  校准失败、自主整窗失败及普通模式超限。软件/P3 凭证与文档分别通过相应提交
  门禁，最终提交身份和证据哈希由本目录 `slice-manifest.json` / `commit-proof.json`
  在提交后封存。
- 下一 gate：`VDC-TIME-002`。补齐其余准入几何/目标容量资源与取消验收；再闭合
  严格校准、`VDC-TIME-003` 的切换连续性及边沿误差界，之后才开放同圈
  trailer/evidence、真实自主 DPLL 更新预算及命令契约后的接线。

### VDC-PROGRESS-20260914-009 — 自主原始计时原型与四板预采

- TODO task ID：`VDC-TIME-002/003`、`VDC-TDMA-001`、`VDC-EVID-001`、`VDC-SCHED-001`。
- 状态：原型已实现，`VDC-TIME-002` 保持 IN PROGRESS；全窗计时正式验收及输入接线
  仍为 PENDING，长期锁相目标未完成。
- 日期：2026-09-14。
- 变更：代码与当前源码 P3 凭证提交 `6127da3`。自主 DMA 在既有 TX owner/预留 SM 内
  配置 latch，每圈清 FIFO、重装计数器，使能前后采 Timer1 raw high/low/high；
  boundary 暂停后先判断 FIFO 非空再读取，缺边沿不等待。格式由
  `TDMA_ORIGIN_RECORD_FORMAT_RAW_TIME` 描述，冻结读取与 SCPI 追加原始字段。
  CPU 不在发车路径打时间戳，不改 PIO 指令、wire trailer、DPLL eligibility 或
  COMMON_TIME/formal flag。独占 sniffer lease 内合并冗余禁用写，释放/FAULT 仍关闭。
- 证据根：`out/HardwareAcceptance/20260914/dpll-origin-raw-time/`。以下容量、计数和
  时序均为快照，非事实源；`plan.json` 绑定前一封存 manifest，`current-plan.json`
  与 `source-checkpoint-r2.json` 绑定本轮源码和 package。
- 资源：真实 builder 编译容量 4/5/6/8，各容量的运行节点矩阵构造及图执行通过；
  探针使用连续 active mask、固定 local slot/guard，不能外推所有配置值。容量 6、
  运行 6 占 314/320 runs、133/140 literals。原始记录由 48 B 增至 88 B；A/B 目标
  workspace 由 7712 B 增至 8120 B，消耗后续对齐 padding 408 B，剩余 72 B。
  总 `.data`/`.bss`、heap 位置不变，heap 外余量仍为 1132 B。不能据此声称新增记录
  零 RAM 成本；其他容量仍需目标配置验收，详见 `graph-matrix.json` / `layout-review.json`。
- 软件与构建：既有 origin 构造/记录/准入、command DMA 和 timestamp clock 回归
  21 项通过；执行真实 DMA 图的模型 20 项通过，覆盖连续发布、序列回绕、完整 raw
  字段、缺 latch、坏 mailbox、缺回传、raw 跨字和旧 FIFO。模型不模拟实际总线/PIO
  延迟。初次缺 include 路径及合成回绕起点错误分别保留于 `graph-model-r1/r2`，修正
  后 `graph-model-r3` 通过；配置变体和新 raw 生命周期的定向组合覆盖尚未全部闭合。
- 当前硬件：build `20260914063601`，源码指纹
  `1ac9b61466444cbf9564b39f6e8384fc7ce7b09c4818227d6ab045bd1fea7e1b`，package SHA
  `246b6b857a77ba38b8701874e01fb64378ebd38037c3371946952716f18d751b`。四板 OTA、
  quick P3/短帧通过，`p3-r1/diagnostic.json` 的 strict gates 通过；凭证范围为
  FOUR_NODE_TDMA_QUICK_DIAGNOSTIC，不含四板正式锁相。未操作 NO5。
- 自主预采：`resident-r1` 先普通启动、再有限自主许可。四板各 34 条 SRAM 记录完整、
  无漏采，STOP 后原生 TDMA SD 字节一致。主板保留连续序列 8169–8175 的 7 条完整
  raw 记录，epoch/首尾 sequence 一致，运输检查和 FIFO 存在成立，arm 前 TX CS 为高；
  Timer1 high/low/high 一致，前后读取区间均为 252 ns，邻圈 arm 间隔为
  999.896–1000.736 µs。该区间及 arm 间隔不是实际边沿误差或锁相精度。
- 全窗失败：原 evaluator 保留 `passed/closed_loop_passed/realtime_gate_passed=false`。
  普通 persona/software TX count 规则不适用于自主 origin，但四板各有一次真实
  receive_missing 增量，切换期间的 reject/调度超限仍需定位；不能因为旧规则不适用
  就放行整窗。预选 3–6 s 窗口四板 UP/DOWN 连续，接收增量为 916/917/917/916，
  拒绝/缺失及各相位 start miss/overrun 无增长。完整窗口与预选窗口同时保留于
  `review-final.json`，不会以裁短窗口消除失败。
- 自主耗时：主板按同状态/配置/许可证保留的 TDMA RUN 完整峰值为 596.672 µs，
  owner 508.796 µs、adapter 383.020 µs、RX parse 189.308 µs、origin publish
  67.828 µs；嵌套区间不能相加。相较上一切片峰值增加 51.628 µs，但这不是受控
  A/B 因果结论，也不是 DPLL 更新 WCET。四板自主 trace 均为零，主板仍为
  diagnostic-only、resolution 为零，不能宣称真实 DPLL 更新预算已满足。
- 存储与恢复：先保存 TDMA/释放 StorageAO，再处理 DPLL，零 trace 如实保存状态。
  raw 的 728 B SCPI 导出另存 NO1 SD 并两次读回一致；这是主机导出的 SD 副本，
  不是板端原生 recorder 文件。初次 BEGIN 应答被通用串口筛选丢弃，后续恢复误把
  active 字段当事务 ID；两次失败、事务读回与正确续传保留于 `resident-raw-sd-r1/r2/r3`
  和对应 JSON，未删除旧文件或重建事务。辅助诊断的未定义查询及错误响应也保留。
  随后 `restored-r1` 普通短帧严格三项通过、四板各 26 条记录和 SD 一致；该恢复流程
  仅验证运输，trace 为零不证明普通 DPLL 更新或锁相。最终四板 STOP/config ACK、
  许可证 inactive，首次 START 至全部 STOP 无 SCPI 查询。
- 主控复核：`audit.py` 从原始 `.bin` 重解码，核对 board/build/epoch/CRC/长度/SD，
  复核 raw 字段、源码指纹、package 和 P3 凭证引用 SHA；失败原件保留。实现提交前
  staged 指纹门禁及 pre-commit 通过；文档另行执行回归门禁并独立提交后封存。
- 下一 gate：先补齐 `VDC-TIME-002` 的非连续 mask/local slot/guard 组合和 raw 的
  STOP/重臂/取消证据，再解决 `VDC-TIME-003` 的切换缺失与边沿偏移/抖动。通过后
  才能执行 `VDC-TIME-004` 同圈 trailer/evidence，继而测真实自主 DPLL 更新 WCET；
  不跳到命令接线或 PI 调参。

### VDC-PROGRESS-20260914-008 — 自主计时事件、资源与区间模型审计

- TODO task ID：`VDC-TIME-001/002`、`VDC-TDMA-001`、`VDC-EVID-001`。
- 状态：事件与资源只读审计 DONE；原始计时原型 IN PROGRESS；硬件验收 PENDING。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-origin-time-audit/`。以下容量、数量和
  模型结果为快照，非事实源；`plan.json` 绑定上一切片封存 manifest 与当前源码指纹。
- 事件顺序：DMA 发车字可能在 PIO guard 期间提前进入 FIFO；control 的 boundary
  token 在 CS 拉高后产生，既不是发车边沿也不是返回 CS。预留 TX latch 程序占位
  仍在，但 `origin_configure_sms()` 未配置/使能；RTT 只保存两类 CS 事件的相对
  倒计数。Timer1 从 `clk_sys` 计数，现有 CPU 读取采用 raw high/low/high 复验。
- 实际 builder：`graph_audit.c` 编译当前生产 builder 并使用实际 workspace 容量，
  编译容量 4/5/6/8、各自允许的运行节点数均构造成功。当前容量 6 下，运行节点
  4/5/6 分别占 293/304/315 个 run、122/124/126 个 literal；实际分配为 320/128，
  因此此探针满节点只余 5 个 run、2 个 literal。构造输入使用连续 active mask、
  local slot 为零及固定 guard/prefix；literal 去重可能随配置值变化，不是本轮 live
  profile 的容量读回。通用 builder 的 384/160 上限不是可用 SRAM；native sizeof
  单独标记，未当作目标 link map。
- 候选操作核算：复用现有 emitter 对 FIFO 清理、重装、Timer1 前后 high/low/high、
  暂停及缺 FIFO 分支计数。独立操作为 18 个 run，其中两次 mask 写替换已有操作，
  净增估算为 16；运行节点 4/5/6 的候选总数为 309/320/331，满节点超出当前分配。
  probe 新用 8 个 literal，完整图的去重与准入仍待复核；不能把此探针当已集成的
  可执行 DMA 图。候选 raw 字段增加 32 B，producer 加归档的布局估算增加 288 B，
  临时副本、对齐和目标链接成本尚未闭合。
- 时基模型：`timer_latch_model.py` 解释当前四条 latch 指令，显式假定 DMA 访问
  顺序、使能区间、GPIO 和启动延迟。2400 组候选中 2269 组区间包含模拟实际边沿，
  131 组跨字不一致被拒绝；直接以先读 timer 加倒计数计算单点，反例偏差达到
  41 个 tick。以上为合成输入，不能换算成实板精度结论；模型证明需要传播误差区间。
- 生命周期边界：模型确认 FIFO 满时首字保留、无边沿不产字、未清旧 FIFO 会读取旧
  值，并区分采样内部跨字回绕和两个一致样本之间的回绕。来源/session/STOP 等
  拒绝条件在模型中仅为抽象输入，不算生产实现负测通过；必须由下一集成切片验证。
- 验证：现有 origin 构造/冻结记录/准入及 timestamp clock 回归 17 项通过，文档
  自回归测试 18 项通过；原件与实际命令分别保存于 `origin-tests-r1` 和
  `docs-tests-r1` 日志。文档检查、pre-commit 与证据复核完成后单独提交和封存。
- 范围与回退：本轮未修改生产固件、PIO、构建、工具或测试，也未操作四板/NO5；
  没有新 build 或新 P3 receipt，不提升上一轮整窗失败及锁相结论。硬件终态仍引用
  上一切片最终 STOP 原件，未将其冒充本轮 live 查询。文档单独更新 TODO 子任务和
  Draft 方案，不改变契约登记状态。
- 下一 gate：`VDC-TIME-002`。先将候选事件记录纳入真实 builder、完整生命周期及
  静态资源预算，完成目标链接；随后按既有流程执行当前源码 P3 和四板原始计时
  验收。时间准入和 trailer 接线属于后续 `VDC-TIME-004`，不能直接开展锁相调参。

### VDC-PROGRESS-20260914-007 — 自主 origin 与 DPLL 输入缺口补测

- TODO task ID：`VDC-TDMA-001`、`VDC-EVID-001`、`VDC-SCHED-001`、`VDC-CMD-001`。
- 状态：IN PROGRESS；目标模式的时间输入和全窗口连续性尚未闭合。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-resident-baseline/`。以下计数、时长、
  构建号与测量值均为快照，非架构事实源；`plan.json` 绑定上一切片 manifest。
- 范围修正：前序 `dpll-four-board-profile`、compact RX 与停止验收切片的 DPLL 更新
  对照使用普通 origin，没有启动自主 origin 许可证。其内部 LOCKED、调度迟到及
  命令增量结论仍属于原运行模式，不证明 wire 自主环路中的 DPLL 更新已经实现。
  原先资源回收和短帧结果仍有效，不能因此提升 resident 或锁相任务。
- 当前源码/硬件：沿用已通过 quick P3 的 build `20260914051939`，指纹
  `cfe924fbc3e53593280ccea6ddcefe778d69c0a6e180cb7a9c6cb17b7486aef8`；本轮没有
  修改固件、PIO、构建或生产工具，也没有用新手工报告替换 P3 receipt。
- 实板流程：四板先配置 provisional/clock evidence，普通启动后显式签发有限自主
  origin 许可证并采集；首次 START 到全部 STOP 无 SCPI 查询。`resident-r1`
  完成后全部 STOP/撤销许可证，再保存 TDMA、释放 StorageAO lease，处理 DPLL
  trace。四板各 34 条板端记录完整、无漏采，SRAM/SD 字节一致；DPLL trace 均为
  零条，不生成空样本的锁相结论。随后 `restored-r1` 恢复普通配置并通过短帧/SD。
- 原验收失败保留：自主模式整窗 `passed/closed_loop_passed/realtime_gate_passed`
  为假。NO1 的旧 evaluator 仍要求普通 persona 和软件 TX count 增长；这些不适用于
  自主发车，但不得据此抹掉切换期间真实的 missing 增量，以及 NO4 的短暂 DOWN/
  recovery。`review-final.json` 同时保留原错误及逐板区间计数，未修改验收器放行。
- 固定中间窗口：预选启动后 3–6 s 的原始记录，四板 UP/DOWN 连续、接收/拒绝/丢失
  对账正常；接收增量为 917/916/916/917，拒绝和丢失增量均为零。主板 persona 为
  16、FSM 为 5，自主模式成立；四板 TDMA/VDC/DPLL start miss 与 overrun 均无增长。
  这是定位稳定模式的窄窗口，不替代整窗失败或真实更新 WCET。
- 主板自主计时：STOP 后读取已按自主状态/同配置/同许可证筛选的 `PROFile:RUN?`，
  对应 5221 次自主 service 中保留的完整峰值为 545.044 µs；其中 owner 468.5 µs、
  adapter 358.704 µs、RX handoff 204.412 µs、origin publish 73.228 µs。嵌套区间
  不能相加；该峰值未包含有效 DPLL 更新，不能与普通 origin 的峰值直接归因比较。
- 无输入直接原因：主板中间窗口 timestamp flags 为 diagnostic-only、resolution
  为零。`tdma_pio_spi_ring_origin_invalidate_time()` 清理旧观测；物理 origin RX
  显式返回零边沿时间戳；DMA `L_STAGE` 每圈清零 DPLL trailer。
  `tdma_origin_observation_t` / `tdma_origin_record_t` 是运输/RTT 事实，缺少可用于
  鉴相的绝对边沿时间。DPLL 相位在执行而 trace 不增长，与此路径一致；不能通过
  修改 valid flag、保留旧 observation 或使用 CPU 提取时间伪造输入。
- 验证与复核：`audit.py` 重新解码当前 `.bin`、核对 build/board/epoch/CRC/长度及 SD，
  绑定源文件 SHA，保存全部失败和预选窗口；`resident-r1-stopped-readback.json`
  保留自主峰值和停止后诊断读回。最终四板 STOP/config ACK、许可证 inactive。
  文档按自回归门禁验证后单独提交，原件由 `slice-manifest.json` 封存。
- 下一 gate：先推进 `VDC-TDMA-001` / `VDC-EVID-001` 的自主边沿计时与 trailer
  关联，设计边界已补入 `VDC_COMMAND_TRANSPORT_PLAN.md`；随后才测自主 DPLL
  实际更新成本。命令运输、共同时间和正式输出锁相仍未接通，不以无输入低耗时
  或普通模式 LOCKED 关闭长期目标。

### VDC-PROGRESS-20260914-006 — 有限采集交接验收与资源切片闭合

- TODO task ID：`VDC-RESOURCE-001`、`VDC-SCHED-001`、`VDC-VERIFY-001`。
- 状态：`VDC-RESOURCE-001` 当前编译容量切片 DONE；调度和锁相任务保持 IN PROGRESS。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-stopped-acceptance/`；以下构建号、
  大小、次数和时长均为本切片快照，非架构事实源。`plan.json` 绑定前序封存 manifest。
- 粗校准调查：TOPOLOGY 使用完整控制响应时限，固件拒绝时可能只发布 SCPI 错误队列，
  原工具的超时不能区分配置拒绝和丢响应。`_control_command()` 现在记录动作起点、
  耗时及失败后一次错误队列读回；不重试动作、不抬高时限、不把超时晋级为成功。
  独立粗校准重复八轮、P0T 后粗校准及软件复位后重跑均通过，本次 P3 也未复现；
  旧失败原件保留，间歇问题根因仍未确认，复发时以 `error_after` 继续定位。
- 交接检查：四板有限采集在全部 STOP 后导出，使用 `validate_tdma_stopped_handoff()`
  复验 START/STOP ACK、板卡集合、配置生效、build/epoch、冻结终态和原始 `.bin` 的
  CRC/长度/采集完整性。proof 与逐板原始字节 SHA256 纳入 receipt；其他模式继续
  原有运行中交接。该 proof 只替换有限采集不适用的 live handoff，不改变收发、
  调度或质量门禁，也不以 summary 缓存中的通过标志代替原始记录。
- 软件与旧证据复核：相关校准/P3/板端记录/启动/TRN-03 回归 234 项通过，包含 C
  recorder 真实输出及缺板、STOP 失败、旧 build、epoch、CRC、长度和漏采负测。
  `previous-handoff-audit.json` 仅离线验证旧字节交接，保留旧 P3 的失败结论；
  当前硬件验收单独执行，未使用 replay 或旧凭证放行。
- 当前源码：build `20260914051939`，1050 文件指纹
  `cfe924fbc3e53593280ccea6ddcefe778d69c0a6e180cb7a9c6cb17b7486aef8`。
  A/B/Boot 构建和链接检查通过；`source-checkpoint-r2.json` 确认 A/B `.data/.bss/heap`
  布局与上一切片完全相同、PIO 头文件字节一致，保留已验证的 1104 B RAM 回收及
  1132 B heap 外余量。本轮只修改验收工具和测试，没有固件或 PIO 改动。
- P3：四板 OTA、软件复位、配置校准和 process-image/FIFO 短帧流程完成；
  `p3-r1/diagnostic.json` 的 `strict_gates_passed=true`、`failures=[]`，STOP 后交接
  proof 验证四板各 14 条记录。约 416 s 的流程在当前配置时限内，凭证范围为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`；该范围通过不等于 full 验收、全表 WCET
  闭合或正式锁相，也不覆盖前序 TOPOLOGY 超时原件。
- 四板对照：一次 `candidate-r1` 显式启用 provisional/clock evidence，随后
  `restored-r1` 恢复普通配置；两轮短帧 passed/closed-loop/realtime gate 通过。
  首次 START 到全部 STOP 之间无 SCPI 查询；先保存 TDMA、释放 StorageAO lease，
  再处理 DPLL。TDMA SRAM/SD 字节相同，DPLL CRC 与重复 SD 读取通过；普通恢复
  没有 DPLL trace 样本，明确记录为空，不作为更新路径或锁相证据。
- 调度结果：`comparison-r1.json` 的实际更新窗口中，主板 DPLL run/start miss/
  overrun 为 2184/645/6，TDMA overrun 为 1371；三从板 DPLL start miss/overrun
  均无增长。普通恢复后四板 DPLL start miss/overrun 均无增长，主板 TDMA overrun
  仍增长 474。该对照支持继续区分真实更新负载与上游迟到，不能用无更新路径或
  稀疏 last-call 峰值关闭静态预算。enabled/quarantined mask 没有新增节点隔离。
- 锁相结果：实际更新每板 76 条 DPLL trace；NO1 全为内部 LOCKED，NO2–NO4 全为
  CHECKING，三从板命令接收和应用增量仍为零。`candidate-r1-lock-review-r2/`
  保留 trace、分析图和 STOP 后读回，仍未证明命令运输、共同时间应用或实际输出锁相。
- 最终复核：`review-final.json` 核对当前源码、包、链接产物、P3 proof、四板原始
  记录、SD、调度与锁相边界；四板最终 STOP、配置 ACK 且临时许可证 inactive。
  代码/凭证与 VDC 文档分离提交，证据由 `slice-manifest.json` 和 `commit-proof.json`
  封存；当前资源切片关闭不改变长期目标 active 状态。
- 下一 gate：`VDC-SCHED-001`。先分解主板上游迟到和 DPLL 实际更新成本，再复核
  `VDC-ROLE-001`；命令契约仍为 Draft，运输接线必须等待阶段基础与独立审核。

### VDC-PROGRESS-20260914-005 — compact RX 专用状态与停止后导出

- TODO task ID：`VDC-RESOURCE-001`、`VDC-SCHED-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS；资源代码、目标链接和四板短帧已复核，严格 P3 尚未闭合。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-compact-rx-state/`；以下大小、次数与
  构建号均为本切片快照，非架构事实源。`plan.json` 绑定前序审计封存 manifest。
- 实现：仅将 resident compact DELTA 的实例改为 `refmem_sync_delta_context_t`，
  保留 peer、mirror 和本地 quality，移除该实例未使用的 ACK/fence/remote-quality。
  栈上私有 view 复用原接收校验与排序逻辑；通用 receiver 保留全部维护能力，wire
  不变。有效非 DELTA 输入在修改 peer/mirror 前以 BAD_TYPE 拒绝，不使用布局强转、
  动态分配或额外静态指针。旧 DELTA payload 接受及计数语义保持，不混入协议修复。
- 行为对照：旧 HEAD 接收器独立编译后，与当前通用和 compact receiver 对照；容量
  4/5/6 每种运行 16384 组通用输入与 2341 组 DELTA 输入，比较逻辑 snapshot、全部
  通用 context 及 compact 的 peer/mirror/quality，通过；含序列回绕、CRC、截断、
  错误身份、重复、stale、gap、NULL 和重置。现有及新增 C unit tests 通过，原件见
  `differential-results.json`、`refmem-new-tests-r1.log`。
- 接口回归：相关 Python 首轮 159 项通过、ARM 桩函数测试失败；桩函数仍沿用动态
  节点改造前的无参签名和缺失 staged config 的调用次数预期。同步测试桩后通过；
  两次失败与最终通过分别保留，不改变 ARM 生产代码。
- 目标链接：A/B/Boot 构建和 Flash link checks 通过。`source-checkpoint-r2.json`
  绑定 build `20260914044559` 及当前源码；A/B 的 compact 实例由 2140 B 降到
  1036 B，`.bss` 减少 1104 B，heap 外余量由 28 B 增至 1132 B；`.data`、heap
  保留量和 PIO 字节未变。容量 4/5 的 736/920 B 仍仅为 host sizeof 对照，未称为
  对应目标固件的 RAM 验收。
- 采集约束修复：标准短帧工具原先在记录窗口结束后、RING 仍运行时导出。有限采集
  现在在首次 START 后通过 finally 尝试全部 STOP，再读取冻结记录；START 或 STOP
  失败保留各板原始动作，不把失败导出为成功样本。四板 quick P3 不再请求
  `--leave-running`；原有运行中交接门禁仍保留，停止导出不能冒充该门禁通过。
  对应短帧/P3 软件回归 164 项通过，含多板顺序、启动失败和停止失败注入。
- P3：当前源码四板 OTA、软件复位及短帧诊断流程完成，源码指纹为
  `fdc2e509bf6565a375524ca7ad3061febcc77c55c9f2870bdbf69289faa69f67`。
  `p3-r1/diagnostic.json` 保留 NO1 粗校准 TOPOLOGY 超时，以及停止导出导致未交接
  运行中环路两项失败；`strict_gates_passed=false`，凭证只属于四板 QUICK_DIAGNOSTIC。
  本轮总流程约 334 s，在配置时限内；不能因此覆盖前序超时或宣称严格验收通过。
- 四板对照：`candidate-r1/r2` 使用相同 pinned matrix 启用真实 DPLL 更新，随后
  `restored-r1` 恢复普通配置；三轮均 `passed/closed_loop_passed/realtime_gate_passed`
  为真。各轮记录均在全部 STOP 后导出，先保存 TDMA 释放 StorageAO，再保存 DPLL；
  TDMA SRAM/SD 字节一致、DPLL CRC 解码及重复 SD 读取通过，命令记录中没有运行窗口
  查询。标准 P3 的板端短帧记录也在全部 STOP 后导出并保存 SD。
- compact 接收：两轮四板接收增量分别为 4696/4700/3132/1570 和
  4704/4709/3140/1572，拒绝及坏 mailbox 增量均为零。这些停止读回差值包含配置过程，
  不能当作稳态包率；板端没有直接导出每个 peer/mirror/quality，存储行为等价性由
  独立 host 对照证明。原始读回见各轮 `*-compact.json`。
- 调度与锁相：同一板端稳定窗口内，四板 DPLL overrun 增量为 10/0/1/0 和 9/0/0/0；
  主板 DPLL start miss 为 642/635，TDMA overrun 为 1343/1374，仍未闭合全表 WCET。
  enabled/quarantined mask 保持前序值，未新增健康节点隔离。两轮每板各 76 条 trace，
  NO1 均为内部 LOCKED，三从板仍为 CHECKING，命令接收和应用增量仍为零；本次 RAM
  回收没有补齐命令路径，不能宣称四板锁相。
- 最终复核：`review-final.json` 汇总当前源码、目标布局、短帧、调度与锁相边界。
  四板恢复 STOP、配置 ACK 且临时许可证 inactive；代码/凭证与 TODO/进度分离提交。
- 下一 gate：资源任务保持 IN PROGRESS，先解决严格配置/校准及符合停止采样方式的
  验收交接缺口，再闭合 `VDC-SCHED-001`、`VDC-ROLE-001`。命令接线仍须等待阶段基础
  与契约独立审核，不能将 QUICK_DIAGNOSTIC 凭证等同于目标完成。

### VDC-PROGRESS-20260914-004 — 命令时间域反例与固定邮箱候选

- TODO task ID：`VDC-CMD-001`、`VDC-CMD-002`、`VDC-CMD-004`、`VDC-RESOURCE-001`。
- 状态：IN PROGRESS；契约未冻结，固件和硬件验收状态不提升。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-command-contract-audit/`。
  `audit-results.json` 绑定当前 HEAD、相关源码 SHA256、提取的函数体和前序封存 manifest。
  以下计数/容量是本轮快照，非架构事实源。
- 原函数 host 反例：在编译容量 4/5/6 下运行当前 manager 消费函数体，每种配置包含
  容量探针与 7 个行为案例。同时间域到期/未来正例成立；uptime 领先时提前尝试应用、
  落后时错过已到期命令、过期无年龄上限，以及最大序列回绕到 1 被跳过均复现。
  getter、当前时间和 Domain apply 为受控 stub，结果是 manager 的应用尝试，不是
  新的板端应用或锁相证据。当前 clock conversion 原函数的 identity model 仍保留
  异步启动 epoch 差，不能仅靠调用转换 API 完成共同时间初始化。
- 时间锚反例：对 adapter 的 `sequence * cycle_period + reference_tx_phase` 算式，
  连续 reference latch 的合成输入在 phase 回绕处产生一个 nominal period 的增量差。
  这是算术反例，不是新采样的硬件故障；它不否定该字段用于关联，但阻止把关联标签
  直接当作与物理时间等速的共同绝对时钟。
- 已有 RefMem host 回归通过。首个编译命令遗漏 OTA CRC header include，失败命令
  和 stderr 保留于 `refmem-compile.json`；按既有测试脚本补 include 后在
  `refmem-compile-r2.json` 及 `refmem-run.json` 记录成功，不覆盖原始失败。
- RAM 审计：compact 路径只生成 DELTA，context 对外只提供 peer、mirror、quality。
  `resource-probe-r1.json` 记录容量 4/5/6 中未使用的 ACK/fence/remote-quality 数组
  分别占 736/920/1104 B。它们是 native sizeof 与调用点审计，尚未移除，不能称为
  target RAM 已释放；下一切片必须用原/新行为对照、目标 link map 和 P3 验证。
- 方案：`VDC_COMMAND_TRANSPORT_PLAN.md` 将 local-to-common 映射与主机信号模型分开，
  提出未来 sequence 的 latch reservation、完整时间锚和固定邮箱记录分片候选；
  MASTER 与 FOLLOWER 都须按共同时间提交，不能发送已应用快照后声称同步提交。
  session fence、记录字段和提前量待审核，未登记契约、未启用 parser 或实时 apply。
- 验证：本轮文档门禁结果另存证据根的 `docs-*-r1.log`；没有运行 OTA、板端采样或
  新 P3。此前短帧/调度/正式锁相失败保持原结论。
- 下一 gate：`VDC-RESOURCE-001` 先落实 compact 状态回收的行为等价性及当前源码
  build/P3，再继续阶段基础和 `VDC-CMD-001` 可执行规格/独立审核。

### VDC-PROGRESS-20260914-003 — 锁相长期目标与分阶段执行清单

- TODO task ID：`VDC-LONGTERM-001`、`VDC-SCHED-001`、`VDC-ROLE-001/002`、
  `VDC-CMD-001`、`VDC-CONFIG-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS；本次只更新执行计划和审计索引，不提升固件或锁相验收状态。
- 日期：2026-09-14。
- 变更：按用户要求将长期目标拆为调度/角色基础、命令契约、稳定运输、共同时间应用、
  四板锁相优化、恢复/配置/长稳阶段；保留既有 Task ID，将 `VDC-ROLE-002` 细分为
  `VDC-CMD-001` 至 `VDC-CMD-005`，新增 `VDC-CONFIG-001` 跟踪配置矩阵验收。
  明确审计可先行而接线须等前置 gate；长期分段观测作为扩展，不阻塞当前短时验证。
- 基线证据：沿用 `VDC-PROGRESS-20260914-002` 的封存四板记录和
  `out/HardwareAcceptance/20260914/dpll-four-board-profile/transport-audit.json`。
  本次没有运行硬件采集或生成新的 P3 凭证。
- 源码审计：`distributed_refmem_tdma_flight_parse_mailbox()` 将 VDC 字段写入
  `last_vdc_*` 诊断状态，manager 从独立的 `s_vdc_command_context` 读取命令。
  `distributed_refmem_get_vdc_follower_command()` 使用裸结构复制，需在
  `VDC-CMD-002` 覆盖发布和 reset 的一致性交接；本轮未以并发实测宣称发生撕裂。
- 时间与序列审计：`vdc_dpll_manager_consume_follower_command()` 将
  `effective_vdc_time_ns` 与 `vdc_dpll_manager_now_ns()` 比较，并使用普通大小比较
  跳过命令序列；共同时间映射和回绕语义分别纳入 `VDC-CMD-004` 与
  `VDC-CMD-001/003`。这两项不是三从板零接收增量的实测原因，不能混同运输缺口。
- 验证记录：`out/doc-audit/20260914-vdc-lock-todo/` 保存本次文档检查命令与结果。
  TODO 本身不冻结新 wire 契约；Architecture 和登记表状态不因本次计划更新而改变。
- 下一 gate：完成 `VDC-CMD-001` 的编码/时间域/资源预算及正反测试方案，独立审核后
  再按 TODO 的前置条件实施；调度、角色及正式 evidence 缺口继续保留。

### VDC-PROGRESS-20260914-002 — 四板锁相复测与 owner 读取成本

- TODO task ID：`VDC-SCHED-001`、`VDC-ROLE-002`、`VDC-LOCK-001`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-four-board-profile/`；下述记录数、
  耗时和构建资源均为本切片快照，非架构事实源。`plan.json` 绑定前序封存 manifest、
  基线源码和固件；原目录保持封存。
- 锁相基线：`baseline-r1` 四板各冻结 76 条 SRAM 记录，先保存 TDMA 记录并释放
  StorageAO lease，再保存 DPLL trace。四板 SD 的 CRC 解码和重复读取字节核对通过。
  NO1 在约 590 ms 的记录内全部为 `VDC_DOMAIN_LOCK_LOCKED`，内部残差为 -9 至
  9 ns；NO2--NO4 全部为 `VDC_DOMAIN_LOCK_CHECKING`，三从板命令接收和应用增量
  均为零。NO1 缺失 bias generation，离线工具另按 update_seq 步长标记
  `decimated_trace`；该计数还包含域内部状态更新，不能据此单独断言采样丢失。
  离线结果仍为 `not_proven`；
  内部状态为 LOCKED 不等于四板锁相、可信 corrected jitter 或正式同步验收通过。
  原件与图见 `baseline-r1-lock-review-r2/`。
- 调度基线：`baseline-analysis.json` 使用板端稳定区间的计数差；四板 DPLL
  overrun 增量为 232/96/96/63，主板 start miss 为 606。稀疏 last-call 采样不能
  用作每次调用分布或各分支 WCET，累计 max 也不能用作本窗口峰值。
- 优化边界：`vdc_dpll_manager_consume_follower_command()` 改为读取 Core1 已拥有的
  active control profile 与 local slot，移除每次整份域快照复制；Core0 的 guarded
  published snapshot、角色/代际应用顺序、命令校验、PI 和静态预算保持原有语义。
  临时 C harness 对旧、新实际函数体运行 1024 组边界组合，逻辑字段结果一致；
  本配置整份快照为 1384 B。相关 Python/host 回归和 Domain C harness 通过。
- 构建拒绝：首次优化触发编译器内联，SRAM service 增长将后续 DMA BSS 对齐到
  下一页，链接超出 RAM；保留 `build-command-r1.log` 和失败 map。用 `noinline`
  保留原 Flash 调用边界，不通过缩减记录器或改调度预算解决该拒绝。
- 构建复核：A/B/Boot 及 Flash link checks 通过，build 为 `20260914025954`，
  源码指纹为 `12a75cdf4252b1dda0f96430c37907c33e7d924cad13498519186ba40bd2dff5`。
  `source-checkpoint-r2.json` 与 `layout-review.json` 证明静态 RAM 和 PIO 字节保持
  原值；函数局部栈分配由 1500 B 降到 112 B，heap 外余量仍为 28 B。
- P3：`p3-r1` 完成当前源码四板 OTA、软件复位、校准与短帧诊断流程；
  `strict_gates_passed=false`。粗校准中 NO2 的 TOPOLOGY 命令超时，且包含构建
  扫描的总流程超过配置时限；两项失败均保留于 `diagnostic.json`，凭证范围为
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC`，不得称为严格 P3 通过。控制配置拒绝并非
  仅见于前序 NO3，本切片未将其归因于单板硬件或声明已修复。
- 四板对照：`optimized-r1/r2` 都完成真实更新、短帧闭环和 STOP 后 SD 字节核对。
  相同稳定区间内，四板 DPLL overrun 从基线 232/96/96/63 降为 11/1/0/0 和
  8/1/0/0；两个优化区间的 deadline 增量分别与 overrun 相同。主板 start miss
  仍为 619/627，主板 TDMA overrun 仍为 1325/1323；未新增负载隔离，不能据此
  关闭全表 WCET。稀疏 last-call 中位数分别由基线 82.066/81.322/77.776/60.036 us
  降至首轮 69.572/52.264/65.334/41.014 us、次轮 59.182/47.548/70.938/42.650 us，
  这些值仅描述被采样调用，不代表 prepare/servo/finalize/publish 的独立分布。
- 优化后锁相：两轮每板各 76 条原始 trace；NO1 全部为内部 LOCKED，NO2--NO4
  全部为 CHECKING，三从板命令接收和应用增量仍为零。只读观测并未被性能优化
  转换为控制命令或正式锁相证据。`review-final.json` 统一复核基线、两轮对照、
  编译布局与 P3 原始失败。
- 恢复：`restored-r1` 恢复普通短帧模式并通过闭环，四板 DPLL 稳定区间 overrun
  和 deadline 增量为零，但没有新 trace 更新，不能用此结果代替真实更新门禁。
  最终四板 STOP/config ACK、临时许可证 inactive；两类记录已顺序保存并核对。
- 命令路径审计：`transport-audit.json` 证明 resident mailbox 的 VDC 字段目前
  进入 `last_vdc_*` 诊断字段，而 DPLL 的 getter 读取独立、按来源保留的命令区；
  既有命令接收路径仍使用 RefMem window intent。本切片不将诊断字段直接当命令，
  也未证明所有 RX stall 的根因；来源、序列、代际和共同生效时间仍须在
  `VDC-ROLE-002` 闭环。
- 原始工具失败：首次锁相汇总读取了错误 JSON 字段名，保留失败输出并在新目录
  重建报告；首次等价 harness 比较 C struct padding 导致失败，后续改为比较所有
  逻辑字段，旧/新函数体和两次输出均保留。这些是报告/harness 失败，不改判为
  板端丢记录或命令行为差异。
- 下一 gate：`VDC-SCHED-001` 保持未关闭，继续分解主板真实更新的剩余超限和
  上游 TDMA 迟到；`VDC-ROLE-002` 优先闭合 resident mailbox 到来源命令区的
  语义交接，并单独复核控制配置超时。正式锁相 gate 保持未关闭。

### VDC-PROGRESS-20260914-001 — DPLL 静态相位入口余量

- TODO task ID：`VDC-SCHED-001`、`VDC-ROLE-001`、`VDC-ROLE-002`、`VDC-VERIFY-001`。
- 状态：IN PROGRESS。
- 日期：2026-09-14。
- 证据根：`out/HardwareAcceptance/20260914/dpll-phase-admission/`；下述板端计数和
  构建数值均为该切片快照，非架构事实源。前序 TDMA 切片的 manifest 由本目录
  `scope.json` 按 SHA 引用，原目录保持封存。
- 原因：默认完整表的 DPLL 窗口恰好等于 WCET，`app_realtime_run_phase()` 只在
  计数器恰好命中起点时容纳完整 WCET。基线 `baseline-r3` 的四板普通短帧闭环通过，
  DPLL 全窗执行比例仅约 3.5%--6.4%，并无新 TRACE 更新；原始板端记录已 STOP 后
  保存 SD，读回与 RAM 导出逐字节相同。历史对照见 `historical-dpll-review.json`。
- 变更：`PROJECT_CORE1_DPLL_ENTRY_MARGIN_CYCLES` 为全部离散静态表声明入口余量；
  DPLL WCET 和其他执行相位宽度/WCET 保留，DPLL 后续相位整体平移，尾部 guard
  仍为空闲。未修改 runtime 准入检查、PI 参数、主从应用语义、wire 或 PIO/DMA。
- 软件：相关 Python/host 回归 66 项及 VDC Domain C harness 通过；四档完整表均
  通过目录闭合检查。A/B/Boot 与 Flash link checks 通过，build 为
  `20260914022144`，源码指纹为
  `baf58ba35b89cef7122b2727abbdf6042bbeb615cbe7db9f91d9f58bfc64539f`。
  `source-checkpoint-r1.json` 证明静态 RAM 没有增加，原 heap 外余量快照仍为 28 B。
  同一目录回归以旧配置编译时，明确在 DPLL 入口余量断言失败。
- 板端调度：`normal-r1` 与 `restored-r1` 均完成普通短帧闭环；稳定区间的主站
  DPLL 执行比例分别为 99.58% 和 98.98%，三个从站均为 100%，这些区间 DPLL 的
  overrun/deadline 增量均为零。基线相同区间为约 3.11%--5.94%。主站仍有
  start miss，TDMA phase 本身也存在 overrun/deadline；不能把短帧工具的
  `realtime_gate_passed` 布尔解释为全表 WCET 已通过。
- 真实更新路径：`provisional-r1` 显式启用既有调试 observation，短帧功能闭环
  通过，四板各冻结 76 条 DPLL 记录。记录跨度约 503--599 ms，仅覆盖该有限
  TRACE 区间；NO3 含一条 follower state transition，其余为本地观测，不存在
  follower applied command。三台从机的命令 apply/接收进展增量均为零，bias
  generation 仍缺失。SD 数据完成 CRC 解码及重复读取字节核对，原始数据位于
  `provisional-r1-dpll-sd`，离线诊断图位于 `provisional-dpll-analysis/plots`。
- 更新相位的剩余成本：同轮板端调度稳定区间内，NO1--NO4 的 DPLL overrun
  增量依次为 216/107/85/80，deadline 增量为 211/104/83/75；主站 DPLL 执行
  比例降为 78.47%，三个从站仍为 100%。调试负载未新增隔离，TDMA 节点继续
  收发；仍须拆分真实更新分支并闭合当前 WCET，不能继续以无更新路径代替验收。
- P3：`p3-r1` 在 NO3 MARK preparation 的 TOPOLOGY 配置被拒绝后中断；完整
  软件复位重验 `p3-r2` 完成四板校准到短帧流程，但粗校准中的 NO3 TOPOLOGY
  超时仍保留在 `diagnostic.json`，`strict_gates_passed=false`。本切片使用当前
  源码的 QUICK_DIAGNOSTIC 凭证，不宣称严格 P3、完整实时预算或正式锁定通过。
- 周期/恢复：四板对所有已编译周期目录逐项返回完整静态表及 generation ACK；
  最终恢复默认周期，STOP/config ACK、inactive 临时许可证和 SD 保存核对通过。
  `review-final.json` 是原始证据复核入口，最终 manifest 与提交回执分别封存。
- 原始拒绝：`baseline-r1` 的临时采样脚本误将复合 TRACE ARM ACK 视为单字段；
  `baseline-r2` 未先保存取消的 TDMA 记录而遭重 ARM 拒绝。取消记录按原 reason
  导出至 `baseline-r1-recovered` 并保存 SD 后才执行新基线，原失败未覆盖或改判。
- 下一 gate：`VDC-SCHED-001` 保持 IN PROGRESS，先拆清并约束真实更新分支的
  prepare/servo/finalize/publish 成本及主站继承迟到，复核 NO3 控制配置拒绝，
  再推进 `VDC-ROLE-002` 的来源/序号/共同生效时间闭环。只读审计
  `followup-audit.json` 指向 manager 的 uptime 比较、
  非回绕安全序号过滤和 RefMem window intent 接线；它们尚未修改，也不能据此
  宣称已解释所有命令缺失。调度恢复不等于 peer command apply、可信 jitter 或 formal lock。

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

### VDC-PROGRESS-20260915-040 — STOP 后几何冻结与新 ARM 显式选择

- TODO task ID：`VDC-TIME-002`；状态 IN PROGRESS。
- TDMA owner 保存训练几何的拓扑、定向端点、PIO/DMA、引脚、相位、物理长度及 map 代际；完整 adapter STOP（含 RX station ACK）后才发布 `FROZEN`。部分取消、ARM 早退、persona/clock/资源变化和代际耗尽会退休描述符。
- 新 ARM 通过受保护 runtime config 选择精确 generation，并绑定新 config、ARM epoch、observation epoch 和 map generation；同一代际重复使用被拒绝。训练完成时记录 observation epoch，避免把 ARM 前空闲等待误当作训练来源。
- 软件 84 项回归、双槽 release 构建和 Flash 链接门禁通过；BSS 增加 792 B，FreeRTOS 堆和两核栈边界未变（资源快照，非容量契约）。当前源码四板 quick P3 严格通过，板端记录与 SD 下载逐字节一致；NO2–NO4 的冻结→选择→退休→陈旧拒绝 HIL 通过，NO1 保持 origin。证据位于 `out/HardwareAcceptance/20260915/frozen-geometry-r1/`。
- 仍未证明 CS 相对首帧坐标、完整自主身份关联、VDC 时间输入、命令应用或示波器正式锁相；下一步继续 TIME-002 的自主首次发车与 observer 就绪边界。

### VDC-PROGRESS-20260915-041 — 新 ARM 初始 observation epoch 的候选准入

- TODO task ID：`VDC-TIME-002`；状态 IN PROGRESS；日期：2026-09-15。
- 代码提交：`d0a1efe`，本修复独立验收完成，父任务继续推进。
- 真实 `tdma_rx_dma_counter_reset()` 在每次 ARM 将 observation epoch 初始化为零；候选关联曾将这个合法初值无条件判为陈旧。本切片仅移除零值的额外拒绝，仍先验证非零 ARM/capture 身份、当前 ARM 有效性和 capture ID，再比较当前 observation epoch。observer/history/pin、复制范围及全部诊断资格标记保持原语义。
- 新增真实 counter reset/observe 回归：合法初值可以关联；计数不动但超过观测期限时旧候选被拒绝；STOP 后新 ARM 同样从零开始也不能复活旧 token。红测编译成功并在新 pin 断言失败，修复后相关软件回归通过，独立源码复核无阻断项。双槽 release 构建、Flash 链接检查和当前源码四板 quick P3 严格通过；全部 STOP 后的 SD 读回与 SRAM 原件逐字节一致，链接 RAM、堆和两核栈边界未变化。
- 本轮证据快照（非容量或时序契约）：软件回归 60 项通过，候选 harness 含 8 组、68 次查询；4 份原生记录及 37 项 hash 复核通过。源码指纹 `97899839f4ca72ec5f249aa3a5e80438c7d3cfd5145fb2253cf99cd5367f2560`，增量构建标识 `20260915022619`；构建标识沿用缓存，实际源码与 package hash 由本轮 receipt 核验。原件位于 `out/HardwareAcceptance/20260915/initial-observation-epoch/`，汇总为 `audit.json`。零初值的正反行为由真实生产函数 host harness 验证，普通 P3 只覆盖集成回归。
- 下一 gate：把 selected geometry 接入独立的 observer 预启动绑定与 START 前有界 DMA 观察。普通 origin 限发单帧先验证首条原始记录，随后再独立拆自主 origin 的非发射准备和显式首发；不通过单帧 sequence 相等授予物理身份，不放宽连续序列、配置、到期或取消检查。
- 范围：本修复不实现 observer prelaunch、CS 相对首帧坐标、自主正式时间输入、主从命令应用或 DPLL 锁相。只读后继设计与源码复核分别保存在该目录的 `next-slice-review.json` 和 `source-review.json`。

### VDC-PROGRESS-20260915-042 — 所选几何观察器预启动与普通首帧专项

- TODO task ID：`VDC-TIME-002`；父任务保持 IN PROGRESS；日期：2026-09-15。本次只关闭所选几何预启动和普通 origin 限发单帧切片。
- TDMA owner 在 RX ARM、最终 capture WAIT、几何绑定及 SM 使能后，ARM 返回前同步尝试一次 observer 启动；失败走公共 STOP，不允许晚启。START 前的独立物理服务有界维护真实 DMA counter；冻结 A/b 保持独立诊断假设，不恢复或伪造 live alignment。配置、ARM/observation/map、时钟、训练不一致或观察器故障会退休诊断，不因局部计时失败隔离仍健康的数据面。GEOMetry 诊断读回追加 observer 生命周期；实际字段以 `scpi_cmd_system_tdma_ring_geometry_q()` 为准。
- 主控复核发现正常 disarm 先取消几何再停止 observer 会误记几何失效；上板前已改为先退休 observer，再冻结/退休几何并撤销 RX ARM。测试执行真实 ARM 末段和 disarm 前缀，覆盖正常 STOP 及 STOP 前已有故障，保留同代首个拒绝原因；新训练产生的新 FROZEN 代际清理其 observer 尾字段，不嫁接旧绑定。
- 验证快照（非容量或时序契约）：最终相关软件回归 77 项通过；双槽 release 构建、Flash 链接检查和当前源码四板 quick P3 严格通过。链接静态 RAM 增加 180 B，堆及两核栈边界未变。独立源码复核无阻断；主控审计核对 8 份 P3/专项原生记录与对应 SD 下载逐字节一致，以及 69 项文件 hash。当前源码指纹 `048e2507c0a74792e61e3510064aed5f84abab2e30dc6b095d9b82b53cb56977`；增量 build 标识仍为 `20260915022619`，版本由本轮源码、package 与 OTA receipt 共同绑定。
- 普通限发专项 `hil-r3`：三从所选 observer 在 START 前均为 ACTIVE，跨过空闲等待后 ARM/observer/observation 绑定不变，实时 alignment 和 RX 完成计数仍为零。板端 baseline 尚无已发布事件；首发后每从板仅发布一条 `sequence=1, ordinal=0` 的成对事件，epoch 不变。主板重复 START 未增加限发配额；STOP 后 CUT 仅含正常 STOP 退休原因，未决身份/在途/积压标记保持。SCPI 查询计数来自实际调用审计，采集区间为零；全部 STOP ACK 后顺序保存 SD。
- 失败原件：`hil-r1/r2` 在 ARM/START 前的 source seed 发布被拒绝；后继读回保留两次发布拒绝计数，四板 FIFO 均有一个 ready 槽和一个 active 槽。专项补齐既有 STOP 后 FIFO RESET，验证队列清空、冻结几何完全不变后再发布；另用真实主机响应解析器复现并修正专项脚本未识别 `FLIGHT:TX` 复合应答的问题。诊断时误用未定义的 `FLIGHT:STATus?` 及错误队列原件也保留。没有通过增加超时、复用已消费几何或放宽首帧判据追认失败；受 P3 指纹约束的源码未因脚本修正再次变更。
- 证据目录：`out/HardwareAcceptance/20260915/observer-prelaunch-r1/`，入口为 `audit.json`、`software-summary.json`、`source-review.json`、`p3/acceptance.json`、`hil-r3/hil.json` 及两组 SD 保存报告。最终四板 STOP、配置 ACK，主板限发恢复为关闭，各板选择几何清零；未操作 NO5、未修改 OTA。
- 下一 gate：独立实现自主 origin 非发射 READY 与显式首发，再证明首物理边界、唯一 packet/event 坐标及自主完整窗口。当前单帧序列相等不授予物理身份、正式时间戳或 DPLL qualification；所选路径的实际 WCET、TIME-003/004、主从命令、共同时间应用及示波器锁相均未关闭。

### VDC-PROGRESS-20260915-043 — 自主非发射 READY、显式放行与 DMA reload 校验

- TODO task ID：`VDC-TIME-002`；父任务保持 IN PROGRESS；日期：2026-09-15。本次关闭可控自主首发及对应取消/兼容切片，不关闭物理身份、正式时间输入或锁相。
- TDMA owner 在 INSTALL 中配置不触发的 loader，保持 SM、DMA 和驱动未启动；READY 按静态相位复验配置、时钟、资源、FIFO、PC、plan 和 loader。Core0 只发布绑定 trial/config 的显式 release 请求，Core1 消耗请求并在最终授权/到期复验后执行唯一触发。重复、旧代、耗尽及 STOP 取消仍拒绝；原有 TRIAL 在 READY 后通过同一 release 路径自动启动，不改变后续硬件自主循环的发车 owner。
- STOP 终态读取使用独立发布 token，避免 STOP ACK 早于下一相位取消时读到未完成结果。已发布终态保持不可变，迟到请求或清理不覆盖它；首次物理拒绝的 code/observed/expected 在公共 STOP 清零工作区前复制。SCPI schema 以 `scpi_calibration_origin_release_q()` 为准，release 尝试原因以 `tdma_origin_release_attempt_reason_t` 为准，physical_reject 的高位物理原因以 `tdma_origin_reject_reason_t` 为准；历史 schema 不补造不存在的物理诊断字段。FIFO 拒绝支路额外读取一次非破坏性 FSTAT 用于归因。
- 实板 INSTALL 拒绝已定位并修复：`positive-r2` 的首次拒绝为 LOADER 的计数检查，live 剩余数为零而期望待启动描述符长度。RP2350 的 TRANS_COUNT 写入只设置 RELOAD，触发后才装入 live counter；READY 改为读取完整 `DBG_TCR`，连同模式位一起精确比较。旧 host facade 把写入直接反映到 live 读数，曾掩盖该问题；修正模型后先复现与实板相同的拒绝，再验证正常 reload、残余 live、错误长度及自触发/无限模式。此前 `positive-r1` 没有细分原因，仅保留其 INSTALL 失败事实，不反向追认原因。
- 验证快照（非容量、时序或精度契约）：最终相关 host 241 项通过；双槽 release 构建与 Flash 链接检查通过；静态 RAM 相对前一已提交基线增加 200 B，堆及两核栈边界未变。源码指纹 `8ee638d0acd2e5d0aaa5a0b4bd40bba622e374362a4d841dcb8d7d309bb29fe6`，增量 build 标识仍为 `20260915022619`，由源码、package、OTA 与 receipt hash 共同识别。当前四板 quick P3 的 passed/strict_gates_passed 为 true；diagnostic_continue 为 true、failures 为空，不能据此声明自主时序或 DPLL 门禁通过。
- 专项原件：`positive-r3` 的主板 READY 连续原生样本覆盖静止区间，owner 执行 1237 次 READY 复验，保持约 1.854 s 后接受一次显式放行；重复请求被拒绝，四板后段接收计数均增长。`expiry-r1` 验证未放行时到期取消，`cancel-r2` 验证 STOP 后旧请求不能复活，`legacy-r1` 验证原有自动启动恢复运输。各项均保留完整计划记录，运行采集期间无 SCPI 查询；全部 STOP/ACK 后逐板保存 SD 并逐字节核对。专项使用几何选择关闭的路径，不授予首自主记录、唯一帧身份或物理精度。
- 流程失败仍保留：`cancel-r1` 的 owner 取消和旧请求拒绝正确，但提前 SCPI STOP 也取消了未完成的原生采集，完整窗口验收失败。后继在许可证仍有效时先完成 READY 采集，再 STOP/ACK 并重试旧 release；该证据不包含 STOP 边沿前后的连续原生样本。两版专项工具继承的“STOP 不取消 recorder”旧注释已被失败原件否定，实际语义以该失败与后继控制顺序为准；未改动已绑定原件的脚本。离线检查后继补齐实际脚本 hash，早期来源标签不完整的报告仍保留。
- 证据目录：`out/HardwareAcceptance/20260915/origin-ready-release-r1/`。入口为 `audit-r3.json`、`software-summary-r3.json`、`physical-reload-summary-r1.json`、`source-review-r3.json`、`p3-r3/acceptance.json`、四项专项及各自 SD 保存报告；`install-count-diagnosis-r2.json` 保存寄存器根因。主控从二进制重新解码并复算各轮实际脚本判据，核对 P3/专项的 20 份原件与 SD、429 项 hash，并保留三个失败轮次及其 SD 原件。四板最终 STOP、原配置恢复；未操作 NO5，未修改 OTA 实现或配置。
- 下一 gate：有界保留首自主原始记录并绑定当前记录代际、版本及 seed 后继序列，随后证明唯一 packet/event 坐标和完整自主窗口。正式时间输入、真实 DPLL 更新预算、主从命令与共同时间应用、示波器锁相及恢复门禁继续分别验收；长期目标保持 active。

### VDC-PROGRESS-20260915-044 — 首自主原始记录的一次性留存与原生采集

- TODO task ID：`VDC-TIME-002`；父任务保持 IN PROGRESS；日期：2026-09-15。本次关闭首自主原始记录留存切片，不关闭同一物理帧身份、完整自主连续性、正式时间输入或 DPLL 锁相。
- TDMA owner 的初次记录入口先将完整 `tdma_origin_record_t` 复制到专用首档，再独立写入 `TDMA_ORIGIN_FIRST_RECORD_VERSION`，随后进入原 ring0。后续循环不再进入首档入口，原槽位与发布版本的映射保持；缺返回、运输未获准或缺边沿的首条均保留，不等待后续成功条替代。专用存储放在 physical owner 末尾，避免扩大共用 workspace 的对齐占用。
- Core1 在已退休的 SEED 边界清首档并发布本地代际和独立的 seed 后继序列；新 ARM、persona 或生命周期失效撤销读取许可。`tdma_pio_spi_phys_origin_get_first_record()` 在一次有界复制前后复验 guard、许可代际和 commit，检查原始格式及首尾序列；序列按无符号回绕处理。提交前中断不可读；完整首档在 STOP 后仍可读取，包括 STOP 尚未成功但资源仍由原 owner 保有的情形，不能因此释放仍在用的资源或授予正式输入资格。
- Core0 Storage task 经 runtime owner 读取首档，按 `DIAGNOSTICS_TDMA_RECORD_SCHEMA` 新增可选 `origin_first` 组。读取复验失败后清除本次全部字段及 available，防止 delta 继承旧代内容；普通模式、三从、READY 未释放和记录关闭时不要求该组可用，原基本快照有效掩码保持。历史版本的字段顺序、类型及长度保持；当前原生格式与 DMA raw-time 格式分别版本化，均不赋予物理时间戳或 DPLL 权限。
- 软件与资源快照（非容量或时序契约）：最终联合 host 306 项通过，专项工具离线检查 44 项通过；实际构造图覆盖编译容量矩阵、有效 mask/local slot、guard/abort/记录开关，以及首条未获运输校验、逐字中断、提交后原 ring0 发布前 STOP、跨代复制、版本/序列回绕和多轮覆盖。当前六节点矩阵最大 317/320 个描述符、134/140 个 literal，单步最大 22/24；双槽 release 与 Flash 链接检查通过。相对前一已提交基线静态 RAM 增加 200 B，其中 physical owner 增加 104 B、记录 delta 状态增加 96 B，workspace、堆和两核栈边界未变。目标编译的局部栈报告为 app snapshot 3096 B、record_sample 3992 B、recorder service 128 B、首档 getter 56 B；这些局部值不是完整任务调用树峰值或 WCET 证明。其他编译容量的 host/ABI 结果不代替相应目标配置的硬件验收。
- 当前源码指纹为 `b8c30841fd4c3a58bb8e4cc40bfc0b624a23822e704f305ad18bfdafd106aaf9`，增量 build 标识仍为 `20260915022619`，以源码、package、OTA 与 receipt hash 联合识别。`p3-r1` 的 passed/strict_gates_passed 为 true，diagnostic_continue 为 true、failures 为空；四板短帧 closed_loop_passed/realtime_gate_passed 为 true。P3 跳过 DPLL 观测，不证明自主时序、全部 TDMA WCET 或锁相。
- 实板正向 `positive-a-r1`：首条 sequence 为 242，匹配 seed 241 的后继，记录 epoch 为 1；原生后续 10 个样本首档全组不变。STOP 尾档 sequence 为 3914、published_version 为 7346，前进 3672 圈，精确符合原槽位发布映射。首条 flags 为零而尾档获得运输校验标记，实证未以较晚成功条替换首条；不能仅据 flags 推断具体 CRC 失败原因。
- 新 ARM 后的 `cancel-r1`：记录 epoch 为 2，完整计划窗口内首档始终不可用且字段全零；先完成 READY 采集，再在授权期限内 STOP，旧 release 被拒绝。该窗口不包括 STOP 边沿前后的连续原生采样。再次 ARM 的 `positive-b-r1`：记录 epoch 为 3，首条 sequence 为 243，后续 10 个原生样本不变；尾档 sequence 为 3902、published_version 为 7320，前进 3659 圈，首条仍未获运输校验、尾档标记有效。两轮三从的可选首档均不可用。
- 验收工具独立复核补齐了跨 trial 代际比较及尾档版本/序列精确对账；正向释放边界不能把不同 guarded read 的 FSM 和首档当成同瞬时快照，因此仅在完整采样时间包络明确早于 release 请求时要求首档为空，包络仍保留显式工程时钟速率假设。未释放轮全窗空首档是独立负证据。首版 app host 夹具曾因重复定义已有 TDMA 类型而编译失败，修正夹具后通过；原失败 XML 保留。未放宽物理身份、CRC、序列连续性或取消规则。
- 证据目录：`out/HardwareAcceptance/20260915/origin-first-record-r1/`，入口为 `audit-main-r1.json`、`software-summary-main-r1.json`、`physical-summary-r2.json`、`source-review-r1.json`、`p3-r1/acceptance.json`、三轮专项及对应 SD 保存报告。主控从原生二进制重新解码并执行实际 HIL validate，核对 427 项 hash；P3 与三轮专项共 16 份记录均在四板 STOP/ACK 后顺序保存 SD 并逐字节一致。各轮采集中无 SCPI 查询，四板最终 STOP、SD 已保存、原配置恢复；未操作 NO5，未修改 OTA 实现或配置。
- 下一 gate：将受控自主首发与三从 observer/capture 起点关联，证明唯一 packet/event 坐标并保留完整自主窗口。本轮几何选择关闭且经历普通 bootstrap，首档不可覆盖不等于三从具有自主零起点。随后接入正式时间输入、验证真实 DPLL 更新预算，再推进命令、共同时间应用和示波器锁相；长期目标保持 active。

### VDC-PROGRESS-20260915-045 — START 当前代准入、取消与自主重臂回传阻塞

- TODO task ID：`VDC-TIME-002`；父任务保持 IN PROGRESS；日期：2026-09-15。本次收敛 START 控制修复，普通四板 P3 和未放行取消专项通过；正向自主重臂恢复仍失败，首 RX 窗口留存尚未实现。
- 代码提交：`bb44c11`；独立受限硬件复核为 `hardware-review-r1.json`，回传候选原因的只读证据为 `rx-return-diagnosis-r1.json`。后者不是已验证修复或根因认证。
- `tdma_ring_runtime_set_data_enabled()` 原先把 `adapter_started` 当作 ARM 成功依据，但失败 ARM 的待清理状态也保留该字段。本切片一次有界读取配置和结果 guard，要求当前配置已由 adapter/applied 发布、无 pending STOP、TRAIN 请求已接受，并在发布前复验；奇数或变化直接拒绝。Core0 的 `tdma_service_ring_start()` 复用既有无等待控制锁，串行化 START 与 STOP/ARM/configure；DATA 发布后复验配置退休，覆盖 Core1 自主 lifetime 的取消写者。普通结果发布不撤销已接受请求，Core1 不增加锁或等待。
- 语义边界：成功只表示观察到当前代 ARM 完成发布且未观察到待清理，并提交 DATA 请求。result guard 未包围所有 adapter 字段的早期写入，不能宣称同瞬时完整硬件快照或持续健康；TRAIN 后可能仍需下一 Core1 相位恢复 persona。发布后取消可以返回拒绝并清 DATA，但不证明请求从未短暂可见。observer ACTIVE、物理首帧和同圈身份仍需各自原件。
- 软件与资源快照（非容量或时序契约）：旧生产代码的真实 C 红测有四个断言失败；修复后相关回归 100 项和完整生产 service/runtime 原子交错 33 项通过，runtime/service scheduler 两套 C suite 通过。覆盖部分 ARM、清理未完、奇数 guard、旧代发布、读取中变化、Core0 控制互斥及 Core1 在 DATA store 前后取消。双槽 release/Flash 链接通过，静态 RAM 增量为零，堆及两核栈边界不变。源码指纹 `008e770147d04660c49dd56e56c67b9865d400a0d40feb449e7d2c4588c4f53c`，增量 build 标识仍为 `20260915022619`，以源码、package、OTA 与 receipt hash 联合识别。
- 当前源码 `p3-r1` 的 passed/strict_gates_passed 为 true，diagnostic_continue 为 true、failures 为空，DPLL 观测为 `SKIPPED_TDMA_ONLY`。它只关闭普通四板短帧集成回归，不关闭自主回传、TDMA 全部 WCET 或锁相。独立源码复核见 `source-script-review-r3.json`，P3 原件复核见 `p3-evidence-review-r1.json`。
- `positive-r1` 在 NO3 START 收到 helper 将超时包装成的 `OK(no payload; verified by state readback)`，没有实际进行所声称的状态查询；专项要求原始 OK，正确停止且未执行 release。STOP 后错误队列保留 Execution error，不能推断具体 runtime 拒绝分支。失败、部分采样和对应 SD 均保留，不把这项结果追认为成功。
- `positive-r2` 的 START 均明确返回 OK；三从选用新冻结代际重臂，首发前 baseline 和前段样本均显示新 ARM/observer 绑定、ACTIVE、零事件和零 DMA 完成。四板计划样本各 20 条、missed/reason 均为零；显式 release 后 NO1 accepted 全窗固定为 237，首档 sequence 241 与 seed 240 的后继一致，尾档已到 4959 但首尾 flags 均为零。NO2 的 overlay_prepare_count 增长至 1251，NO3/NO4 在计划采样窗口内仍为零，后两板 RX observation drop 接近接收次数减一。STOP 后冻结描述符已显示训练完成，因此不能宣称整个 ARM 永久未训练。重臂清 live alignment、一次 discovery hint 与后续扫描跳帧导致连续确认不足是当前候选原因，尚未通过独立实板修复确认；原 `hil.json` 保持 FAIL，`restart-analysis-r1.json` 仅补充受限诊断。
- `cancel-r1` 未放行时完整窗口内源保持 READY、首档不可用，三从保持新 observer ACTIVE 且事件/DMA 均为零；完成计划采样后全板 STOP/ACK，源终态为已取消，release_ticks 为零，旧 release 由冻结拒绝计数和原因确认不能复活。终态读回距工程最早到期约 4.29 s，计算显式假设时钟速率误差上界，不是校准共同时间，也不覆盖 STOP 边沿两侧的连续采样。取消专项通过。工具以实际 action 索引严格检查 STOP→ARM→START，时间允许相邻操作落在同一主机计时 tick；离线正反检查 32 项通过。旧工具与错误夹具原件保留。
- 证据目录：`out/HardwareAcceptance/20260915/ring-start-ack-r1/`。主控 `audit-main-r1.json` 从二进制重解码并复算当前判据，核对 60 项 hash、P3/两轮失败/取消共 16 份原生记录及逐字节相等的 SD 下载，明确 `autonomous_restart_recovery_passed=false`。运行采集期间没有 SCPI 查询，全部 STOP/ACK 后顺序保存 SD；最终 `final-board-state-r1.json` 确认四板 STOP、记录已保存、诊断模式和主板不限发配置恢复。未操作 NO5，未修改 OTA 实现或配置。
- 下一 gate：先独立解决重臂后自主有效回传停滞，每个功能切片继续 host→release→当前四板 P3→专项原件复核；随后保留首 RX 原始窗口、证明 packet/event 同物理帧关联，才开放 TIME-003/004。后继依次验证共同时间和实际更新调度预算、主从命令定时应用、NO1/CH1 触发的四路输出锁相、恢复与长稳；长期目标保持 active。

### VDC-PROGRESS-20260915-046 — 重臂后单帧发现刷新与四板自主回传恢复

- TODO task ID：`VDC-TIME-002`；父任务保持 IN PROGRESS；日期：2026-09-15。本次关闭重臂后 live alignment 训练停滞及自主有效回传恢复切片，正式时间输入、同物理帧身份、时序总门禁和 DPLL 锁相继续待验收。
- 真实异步捕获红测复现前轮候选：首次 Core0 私有发现仅含一帧，结果交付时 DMA 已前进多个帧长，后续持续跳帧；普通 RX 成功但相邻确认始终不足。TDMA owner 现在只在 process image 尚未训练且未锁定时，用有效 hint 预测最近一对已完成邻帧的位置，有界复制到原私有 job，再由 Core0 按既有运输解码规则确认相同位移、长度及精确 stride。hint 定位本身不授予训练或物理身份，不改冻结几何的独立诊断含义，不增加实时解析或动态存储。
- 原有效 RX hint 在刷新 REQUESTED/BUILDING 期间继续服务，同上下文的无效刷新结果不清除它；陈旧 request/observation/persona/配置仍拒绝。Core1 对每次复制分别进行 DMA 完成计数、epoch 和覆盖复验；刷新失败不推进普通 RX 游标，且本次不再发第二次 discovery，避免形成三次复制。达到训练要求或已锁定后不再刷新。STOP 后 worker 尚占用的 job 必须完成取消 ACK 才能复用；坏首帧、缺字和旧代记录不能因后继成功而升级。
- 软件及资源快照（非容量、时序或精度契约）：相关 RX/cursor 49 项、候选/预启动/DMA/adapter 集成 9 项通过；覆盖各位移、持续跳帧、单帧/不完整/损坏第二帧、取消三态、复制覆盖等值拒绝、代际变化及整数边界。刷新私有窗口上限 604 B，复用原 616 B 缓冲；刷新加普通复制上限 901 次 DMA word 读取，全函数既有坏 header 加初始发现路径保守上限仍为 913 次，每调用至多两次复制。它们是操作数上界，不是芯片 WCET。FLIGHT_MUTABLE 的 CRC 保护范围保持既有语义，不声称覆盖全部可变 payload。
- 首版目标构建因训练辅助代码内联进 SRAM 跨过 BSS 对齐边界，多占 4096 B，原 map 和源码已保留；将训练刷新及 READY 结果合并显式放入 Flash，稳态入口短路后重新构建，双槽 release 和 Flash 链接通过，静态 RAM 净增为零，堆和两核栈边界未变。刷新 helper 仅在未训练且未锁定分支调用，READY helper 仍按 job 结果就绪调用，不能豁免其静态调度预算。源码指纹 `161dd3f2aed96cf86d0ba510807b4593d3f2350984b05a2c6238a75a0d1f1dd6`，build 标识仍为 `20260915022619`，通过源码、package、OTA 和 receipt hash 共同识别。
- 扩展 host 曾在 `test_descriptor_completion_and_bounded_stop` 编译失败，直接重放保存 stderr 确认旧夹具缺少前序新增的 `tdma_geometry_trained()` 依赖。补齐只记录通知的 facade，验证已满足训练、首成功提交仅调用一次、失败与 STOP 不额外调用；原 DMA/STOP 断言保持，编译和运行命令、返回码及输出均留存。该夹具未包含本轮 RX 文件，原失败不归因于刷新算法，后继完整集成回归通过。
- `p3-r1` 四板 OTA 和初始化完成后，NO2 的 OPMode APPLY 超时，读回 active 为旧级别、reject_count 增加，last_result 为 BAD_ARGUMENT。四板 STOP/ACK 后只重试一次 STAGE/APPLY，原始应答及 active 读回确认成功；没有具体子分支证据，不追认为原 APPLY 成功或归因为 RX。保持同源码重新运行 `p3-r2`，passed/strict_gates_passed 为 true，diagnostic_continue 为 true、failures 为空，普通四板短帧通过；DPLL 为 `SKIPPED_TDMA_ONLY`。
- 新目录内 `positive-r1` 在普通 bootstrap 的 START 收到 helper 对无有效响应的包装文本，严格原始 OK 判据拒绝，尚未 ARM recorder，records 为零；原失败保留。新 `positive-r2` 通过：三从 STOP→选新冻结几何→ARM 后，首发前 observer ACTIVE、事件和 DMA 完成均为零；四板各完成 20 条原生计划样本，missed/reason 为零，运行采集期间无 SCPI 查询。后段诊断窗口 NO1 accepted 从 409 增至 1509，三从分别为 156→1254、135→1233、114→1213；三从 prepare/published/selected 均增长。selected 计数表示既有 DMA selection 退休观察，不单独证明某片已在物理线上发出。这里的新 `positive-r2` 与前轮 `ring-start-ack-r1/positive-r2` 失败原件分别保存，不能混用。
- 首自主归档 sequence 为 240、flags 为零；后续首档保持不变，STOP 尾档 sequence 为 4956、flags 为一，记录的 sequence 前进 4716。恢复运输没有用较晚成功帧替换未获运输校验的首条。`cancel-r1` 在未 release 的完整窗口保持主板 READY、空首档及三从预启动静止，完成记录后在工程最早到期前 STOP，旧 release 拒绝；工程速率假设和取消终态读回保留，不能称为已校准共同时间。
- 时序仍有缺口：正向原生 baseline 到末样本的 TDMA overrun 增量为 NO1 零、三从 18/54/61，deadline miss 增量为零及 6/36/40。对应 profile reset 后整相位 peak 约 656/895/930/933 µs；NO1 峰值跨 READY→自主迁移，NO4 峰值含 STOP 清理，不能全部当稳定 RUN 或刷新函数耗时。NO2/NO3 峰值在已接收数据的处理相位，未含 RX copy；当前资料不能从这些峰值单独推导刷新时延。累计历史 max 不当作本窗 WCET，恢复回传不等于时序通过，不新增健康节点隔离。
- 证据目录：`out/HardwareAcceptance/20260915/rx-training-refresh-r1/`。入口为 `audit-main-r1.json`、`software-summary-main-r2.json`、`host-evidence-r1.json`、`design-source-script-review-r2.json`、`hardware-review-r2.json`、`p3-r2/acceptance.json`、`positive-r2/hil.json` 和 `cancel-r1/hil.json`。主控重解码、复算实际 HIL 判据并核对 51 项 hash；独立审核复核这些 hash、重解 12 份原生记录并重建 58 页 SD 原始应答。P3/正向/取消共 12 份原生记录在四板 STOP/ACK 后保存 SD，下载逐字节相等。代码提交 `50b46c1`。最终四板 STOP、记录已保存、诊断配置和主板不限发状态恢复；未操作 NO5，未修改 OTA 实现或配置。
- 下一 gate：继续首 RX 原始窗口一次性留存，再证明自主首发与三从 packet/event 的同物理帧关联和完整连续性，才开放 `VDC-TIME-003/004`。从板处理相位预算另行闭合；之后推进真实 DPLL 更新、命令运输与共同生效时间、示波器锁相及恢复长稳，长期目标保持 active。

### VDC-PROGRESS-20260915-047 — 首 RX 原始窗口、首事件及 STOP 后档案留存

- TODO task ID：`VDC-TIME-002`；父任务保持 IN PROGRESS；日期：2026-09-15。本次关闭所选几何预启动路径的首 RX 诊断档案切片，不关闭物理帧身份、完整自主连续性、正式时间输入、静态调度或 DPLL 锁相。
- TDMA owner 在 persona workspace 外有界保留 `tdma_rx_first_window_t`。原始坐标固定为首窗口，不按冻结偏移移动起点，不搜索较晚帧；DMA 完成计数达到窗口长度后只尝试一次，独立复验复制后的 epoch、配置、覆盖及硬件故障。复制失败仍保留已写字节，首失败不能重试替换。原始 FIFO 首事件独立保留，部分批次可累积，只有完整首 ordinal 且最终故障检查通过才置事件有效；原始字节有效与事件有效分别判定。
- 新 admitted config 撤销旧档，同 config 的失败重试不清档；persona unload 包括 load 失败回滚均退休原 lifetime。未绑定 observer 的早期 ARM 拒绝保留稀疏档案，不能继承旧 observer 的状态/故障。真实 STOP 先取消 pending，只有 DMA 和 worker/scanner 清理 ACK 后才标记 STOPPED；完成档案保留首原件。Core0 经 runtime owner 有界读取，失败清零，原生 `DIAGNOSTICS_TDMA_RECORD_SCHEMA` 增加 `rx_first` 组并兼容旧版本；跨核读取和原始档案本身均不授予 VDC evidence 资格。
- 软件和资源快照（非容量、时序或精度契约）：主控原生格式/应用/集成测试分别 60/2/8 项通过；物理路径修正前广测 108 项通过，独立审核修正稀疏 STOP 后的最终定向回归 7 项通过。覆盖编译容量、真实 persona unload/rollback、实际 STOP 入口及 ACK 尾部、DMA 复制复验、首 FIFO 部分事件和失败不重试；不能将修正前广测表述为最终源码全量重跑。双槽 release/Flash 链接通过；静态 RAM 相对前一已提交基线增加 1020 B，堆及两核栈边界不变。最终 ELF 复核的 native snapshot 最深已知链为 9572 B，加保守异常上下文为 9808 B，相对 Storage task 的 12288 B 配置余 2480 B；这只覆盖该调用链。四板 STOP 后 Storage 高水位均余 626 words，即 2504 B，不能替代任务全部路径的静态上界。
- 当前源码指纹 `9977b06303996436435d430b1ca4cae11cf48da490d3b48d45a4e406ca36b1b2`，增量 build 标识仍为 `20260915022619`，以源码、package、OTA 与 receipt hash 联合识别。`p3-r1` 用时 190.548 s，passed/strict_gates_passed 为 true，diagnostic_failures 为空；普通四板短帧通过。DPLL 观测为 `SKIPPED_TDMA_ONLY`，该 P3 不覆盖自主全相位时序或锁相。
- `positive-r1` 完成四板各 20 个计划样本，missed/reason 为零。三从首发前 observer ACTIVE、零事件/零 DMA；首原始窗口各保留 173 B，复制前后完成坐标均为 173，首事件 ordinal 为零、word mask 完整，首因和硬件故障为零。冻结 A/b 分别为 3/0、1/5、0/2；按该几何离线提取，调用当前生产运输解码器均通过，sequence 为 242、identity 为 194528521，与首事件序列及主板首档字段一致。主板首档 flags 仍为零，未获回传运输校验；字段相等和诊断档案不可替换不能提升为同物理帧身份或正式时间戳。
- `positive-stop-r1` 验证完成首档在 STOP 后原始字段不变，仅退休标记增加。`cancel-r1` 的 NO3 START 无有效应答，底层返回 `OK(no payload; verified by state readback)` 占位文本，但该分支未实际查询；HIL 严格拒绝并保存四份取消记录，没有 release，也不追认为成功。同固件另开 `cancel-r2`，完整未 release 窗口及到期前 STOP 取消通过；`cancel-stop-r1` 保持零 raw/事件，原因明确为 STOP 未完成。pending 阶段的 `copy_before_ticks` 随初始 DMA 观察更新，不代表执行过复制；离线聚合最初将它误列为不可变字段而失败，修正为未尝试复制条件下单调前进，原失败保留。`rearm-r1` 取得非零新配置和实际 ARM ACK，原生 baseline 的配置与 ACK 一致，旧首档全零；这些 STOP/重臂探针是主动 CANCEL 封存的 baseline-only 证据，不是成功 RUN 窗口。
- 时序门禁仍 FAIL：正向原生 baseline 到末样本的 TDMA overrun 增量为 NO1 零、三从 59/112/103，deadline miss 增量为零及 42/69/73。同 reset 的完整相位 peak 约 642.320/950.476/1068.012/1002.064 µs；NO1/NO3 峰值含 STOP 清理，NO2/NO4 为数据处理相位。三从首复制及前后观察/复验括号分别约 34.468/20.400/19.152 µs，不能当作纯 memcpy 或完整相位耗时，也不能解释全部增时。当前 TDMA 预算仍取 `app_realtime_profile.c` 对应静态配置；首档功能通过不豁免其实际全相位超限，也不新增健康 TDMA 节点隔离。
- 证据目录：`out/HardwareAcceptance/20260915/rx-first-window-r2/`。入口为 `software-summary-main-r1.json`、`event-core-handoff.json`、`implementation-independent-review-r1.json`、`final-build-stack-independent-review-r1.json`、`final-hardware-doc-independent-review-r1.json`、`audit-main-r2.json`、`p3-r1/acceptance.json` 及各专项原件。主控从二进制重解码、复算 HIL/STOP/重臂判据，核对 147 项 hash；含失败轮次的 28 份原生记录均在四板 STOP/ACK 后保存 SD，下载逐字节一致。独立审核另行重解这些原件，并重建 91 页 SD 原始应答核对相等。代码提交 `08fb5c2`。最终 `final-board-state-r1.json` 确认四板 STOP、记录已保存、诊断配置及主板不限发状态；未操作 NO5，未修改 OTA 实现或配置。
- 下一 gate：继续证明自主首发、三从首 packet/event 与实际物理边沿的唯一对应，再验收完整自主窗口、接入正式时间输入，依次开放 `VDC-TIME-003/004`。真实 DPLL 更新的共同时间与静态预算、主从命令定时应用、NO1/CH1 触发的四路输出锁相以及恢复长稳仍按依赖逐项推进；长期目标保持 active。

### VDC-PROGRESS-20260915-048 — 所选首帧入口的实际等待见证

- TODO task ID：`VDC-TIME-002`；父任务保持 IN PROGRESS；日期：2026-09-15。本切片关闭 capture 在 observer 就绪前提前采入部分位的具体歧义，不授予完整物理帧身份、正式时间戳或 DPLL 锁相。
- TDMA owner 在 observer 实际启用前后分别读取 capture SM 的 `EXECCTRL.EXEC_STALLED`，通过 `TDMA_RX_START_CUT_CAPTURE_ENTRY_WAIT_BEFORE/AFTER` 留存；后半段使用 OR，保留前半段事实。selected prelaunch 同时核对两位及当前安装的 process follower 入口 PC，不满足则以 `TDMA_GEOMETRY_OBSERVER_CAPTURE_ENTRY` 进入原有拒绝/STOP 路径，不重注入、不等待后帧替换失败。普通未选择几何路径保持原启动行为。见证依赖已审计的末次 `WAIT 0 RXCS` 及其后无重装/重启的唯一 owner 生命周期；它不是任意等待指令或 PC 值的通用身份凭据。
- 旧生产源码在最终同一夹具下编译成功，运行以断言退出，复现已释放 WAIT、零 DMA 和空 FIFO 被接受；原件为 `old-production-final-fixture-red-r2.json` 及对应目录。此前旧模型提取器未适配几何绑定调用、首轮新增测试遗漏档案 admission 的失败分别保留；修正夹具后最终六组文件共 127 项通过，测试前后源码 hash 一致。覆盖启用前/期间释放、前后 PC 错误、前错后恢复、首次拒绝不重试、普通路径、STOP/重臂和首档生命周期；host 不代表电气边沿证据。
- 资源快照（非容量契约）：首 cut 保持 128 B 和 schema 1，未增加 PIO 指令或全局缓冲；双槽 Release 和 Flash 链接通过，静态 RAM 增量为零，堆及两核栈边界未变。源码指纹 `2e61755a453b740193a28061463f4e0bba3f1460616cec485918173fa6cfcbd9`，build 标识继续为 `20260915022619`，以源码、package、OTA 和 receipt hash 联合识别。独立审核同时核对最终 ELF 的两次 MMIO 读取、标记保留及拒绝分支。
- `p3-r1` 为诊断流程完成、strict_gates_passed 为 false：TRN-00 residence trial 0 的 NO4 `TOPOLOGY 4,3,0` 返回 `<timeout>` 和执行错误，尚未进入 ARM；没有证据将其归因于新增 selected 检查。原始失败与四份普通 TDMA 记录保留，STOP 后 SD 全等。同源码有界重跑 `p3-r2`，passed/strict_gates_passed 均为 true，diagnostic_failures 为空，四板短帧通过；DPLL 为 `SKIPPED_TDMA_ONLY`。新通过不追认首次失败。
- 正向 `positive-r1` 与未放行 `cancel-r1` 均完成四板各 20 个计划样本，期间无 SCPI 查询；全部 STOP 后顺序保存 SD。三从两次入口等待位均为一，PC 与原生记录的实际安装偏移均为 4，cut flags 为 895，ARM/observer/config/map 与首档一致。正向固定首窗口各 173 B，A/b 为 3/0、1/5、0/2；生产运输解码器通过，sequence 为 241、identity 为 451227504，原始 sequence FIFO word 与冻结几何提取的对应位段一致。主板首档 flags 仍为零，不能凭内容相同赋予首回传运输资格。取消窗口维持零 raw/事件，STOP 在最早工程到期前取消旧 release；工程时间界不作正式共同时间。
- 主控独立入口审计共 17 项离线检查通过：旧实板记录缺少新等待位明确拒绝，实际 cut 应答与动作原件全等，入口 PC 同时与原生安装偏移核对，保留未知/诊断标记。整体审计重新解码含失败 P3 的 16 份原生记录，重建 79 页 SD 应答并逐字节核对，复算 HIL 判据及当前源码/receipt 等 104 项 hash；证据为 `audit-main-r1.json`。最终四板 STOP、记录已保存、诊断配置恢复，NO1 不限发；未操作 NO5，未修改 OTA 实现或配置。
- 时序快照（非 WCET 契约）：正向完整相位 peak 为 NO1 715.648 µs、三从 918.316/932.768/990.988 µs；NO1 包含 STOP 清理，三从为数据处理相位。baseline 至末样本的 TDMA overrun 增量为 0/45/30/44，deadline miss 为 0/31/17/27。当前 `app_realtime_profile.c` 对应预算的全相位门禁仍 FAIL；该入口检查不改变预算，也不证明总体提速，不隔离仍健康的 TDMA 节点。
- 证据目录：`out/HardwareAcceptance/20260915/physical-frame-identity-r1/`，入口为 `event-core-handoff.json`、`software-summary-main-r1.json`、`implementation-independent-review-r1.json`、`hardware-independent-observations-r1.json`、`start-gate-self-test-r3.json`、`start-gate-positive-r1.json`、`start-gate-cancel-r1.json`、`audit-main-r1.json`、`p3-r2/acceptance.json` 及各原始目录。代码提交 `30b5335`。
- 下一 gate：按 `physical-identity-next-evidence.json` 优先闭合实际 origin 唯一发车/首描述符与首档、首 CS 完整时钟数、capture/sequence/control 同位采样及无丢样，再合成首窗口不跨 CS 拼接证明。实际分频的周期量化和跨板同步相位必须计入；已有固定周期模型不能直接推广。之后依次验收 TIME-003 完整自主窗口、TIME-004 正式输入、真实更新调度/角色、命令运输和共同时间定时应用、FLL/PI 与质量、示波器输出锁相及恢复长稳；长期目标保持 active。

### VDC-PROGRESS-20260915-049 — 首帧优化退出锁相前置，有效样本驱动推进

- TODO task ID：`VDC-TIME-002/003/004`、`VDC-SAMPLE-001`；旁路任务为 `TDMA-REFINE-001/002/003`；日期：2026-09-15。本记录只关闭执行方案收敛，不关闭正式时间输入、调度、命令运输或实际锁相。
- 用户明确：启动首帧可以不可用，稳定传输后开始 DPLL；飞行数据已可流转，错误帧丢弃只减少更新机会，不直接干扰锁定。因此首发/首档对应、精细采样与首窗口不跨 CS 的三项完整证明归 TDMA 后续优化，均不作为 DPLL 推进前置。新增 `VDC_STABLE_INPUT_PLAN.md`，调整 TODO 依赖，保留已有代码及全部首帧成功/失败原件。
- 待实现语义为：任意合格稳态样本可以建立当前代际时间锚；偶发坏帧、缺帧、重复和旧样本跳过本次更新，保持积分、有效时间锚和可信 DCO，不要求连续无错固定样本数。收到旧代际数据与当前基准实际换代分开；持续缺失按年龄与保持误差处理，不能把过去锁定状态永久当作当前精度保证。下一有效更新核对真实时间间隔，Ki 步长与完整 HOLDOVER 分开切片。
- 只读源端审计原件位于 `out/HardwareAcceptance/20260915/first-frame-coordinate-r1/`：`origin-n1-proof.json` 保留实际 builder/PIO/记录顺序的局部因果结果；旧 repository 测试夹具缺 retirement seam 的失败仍保留，仅 out 副本补 seam 后的运行通过，不称为仓库原测试已通过。`capture-stall-lifetime-review.json` 只证明所观察 capture 生命周期内无对应 stall，不证明精度或完整物理身份。`origin-stable-window-reuse.json` 中连续无错窗口及 loss 立即撤销的早期建议由 `dpll-valid-sample-semantics-addendum.json` 和用户最新指令覆盖，不扩展为新门禁。
- 四板已有数据流转可直接复用：上一正向原件的 baseline 至末样本有效 RX 均增长，三从候选查询均匹配；这些事实不等于正式时间输入已接通。源码 `tdma_pio_spi_phys_origin_rx()` 自主时间戳仍为零，原始时间记录只供诊断。此前 P3 明确为 `SKIPPED_TDMA_ONLY`，尚无本轮 DPLL 输入阶段计数或 trace 实测结论。
- 实际控制差距已定位到 `vdc_domain_reject_requires_reacquire()`：多数输入拒绝会走 `vdc_domain_reset_lock_acquisition()`，清积分与频率锚；现有无新输入/重复输入返回已可复用。FLL 使用有效锚间隔，Ki 仍消费静态 `servo.update_period_us`；保持年龄字段存在不证明超时 HOLDOVER 迁移已实现。这些是下一可归因功能切片，不能用放行无效时间戳替代修复。
- 本方案按项目 collaboration/doc-self-regression 流程验证，独立审查确认首帧不阻塞、无连续 K 门禁、旧样本与实际换代区分及 HAOFV owner 边界。纯文档不构建/刷机或重跑 P3；文档门禁结果存入 `out/doc-audit/20260915-dpll-stable-input-r1/`，本记录不追认旧硬件失败，不冻结新契约。
- 下一 gate：用现有四板板端 trace 和 observer 阶段计数测量自主有效输入；TDMA native 从启动留证，DPLL trace 在计划自主阶段触发以免 bootstrap 填满。全板 STOP ACK 后顺序保存及读回，用计数增量与实际覆盖判断输入卡点，不将 STOP 后的 INACTIVE 当作 RUN 拒绝。`VDC-SAMPLE-001` 单独修复坏样本跳过，每个实际功能变化立即完成 host、当前源码构建、四板 P3 和专项，再返回时间输入及示波器锁相主线；长期目标保持 active。

### VDC-PROGRESS-20260916-001 — 单样本跳过收敛与四板自主时间输入定位

- TODO task ID：`VDC-SAMPLE-001`、`VDC-TIME-002`；日期：2026-09-16。关闭本次样本拒绝的软件与集成切片，实板坏样本恢复待自主有效输入接通；不关闭 `VDC-TIME-003/004`、完整 HOLDOVER 或实际锁相。用户进一步明确 DPLL/VDC 时间同步是 TDMA 特等席负载，已在执行方案中落点，继续复用固定 process-image/trailer 和优先预算。
- `vdc_domain_reject_requires_reacquire()` 将单份样本的来源、CRC、时间有效性、窗口和代际等拒绝与本地 disabled/非法参数/无效 schedule 分开；跳过时不清积分、DCO、有效时间锚和有效样本累计，不按 bad-count 直接重锁。拒绝计数继续增加，质量年龄不刷新，正式 gate 仍拒绝坏输入。紧凑输入将本地 schedule 无效单独归为 BAD_SCHEDULE，避免被 BAD_FRAME 的跳过策略混同。既有 debug continue 仍不执行被拒绝样本的 servo 校正。
- 没有新增时间戳、wire 字段、PIO、缓存、独立 Domain 重复过滤或 Ki 步长/HOLDOVER 功能。FOLLOWER 的本地原始观察记录保持既有行为；单独 publish clock/path/dictionary 的实际换代清理尚未闭合。set_ready(false) 保持原 OFF 语义，不宣称它清积分；紧凑配置异常的历史状态处理也没有扩大修复。当前作用范围由 `host-review-summary.json` 与独审报告列明。
- 软件与资源快照（非产品容量或时序契约）：85 项 host 测试通过，含 72 个真实拒绝入口的启动/捕获/锁定及 strict/debug 场景、8 个显式控制或配置异常对照、既有完整 C Domain suite 和原 replay 测试。旧生产源码的来源、窗口、紧凑 BAD_FRAME、delay-generation 四项反例失败保留，debug 对照通过；两次 activation 夹具错误及修正也保留。A/B Release 和 Flash 链接通过，静态 RAM 尾端、堆及两核栈边界均不变，Flash 各增加 392 B。
- 当前源码首次四板 quick P3 严格通过，passed/strict_gates_passed 为 true、diagnostic_failures 为空，范围为 TDMA-only，DPLL 标记 `SKIPPED_TDMA_ONLY`。源码指纹 `b06f286d6a68a3a3bc069fead718ee89ec236d682d1391a13dd42004ca2ed115`，包 SHA256 `77cf838802cda67006a00b2a80617e001b9ced1a2cc76ab3c7d023d13b82a5f5`；缓存 build 标识仍为 `20260915022619`，不能单靠该标识识别新旧固件。主控核对 source/receipt/OTA/包及原件，未修改 OTA 实现或配置、未操作 NO5。代码与匹配 P3 凭证提交为 `cf4206f`。
- 四板自主输入定位分别在旧固件 `baseline-r1` 和本次新固件 `after-skip-r1` 执行，均先确认全板 STOP，核对 UID、角色及 load mask，再以现有普通 bootstrap/自主 TRIAL 触发。实读四板 load mask 均为 91，VDC/DPLL 已开，角色为一主三从；未因候选配置文件的 mask 数值推断实际未启用。TDMA SRAM 从启动记录，DPLL trace 在预声明自主阶段 ARM，共同观测时段约 4.5 s；期间无查询，全部 STOP ACK 后顺序保存 SD/读回。完整探测流程分别约 33.3/40.4 s，不含构建、OTA 和 P3，适用于快速输入定位。
- 新固件正向原件快照：四板有效 RX 增量分别 1772/1745/1722/1697，坏帧及运输错误增量均为零，三从成对事件无 fault。自主主板采样点的 timestamp resolution 为零、flags 为 diagnostic-only，正式 TX/RX 时间为零；三从本地 latch 有读数，但 observer eligible 增量均为零。四板自主 trace 均为零记录、零 dropped。主板 observer accepted 增量为 162，基线对应为 160；这些计数包含 bootstrap/切换，不能当作自主持续更新。零 trace 不证明精度不合格或 PI 发散，STOP 后 last_result 也不代表 RUN 的拒绝原因。
- 证据目录为 `out/HardwareAcceptance/20260916/dpll-sample-skip-r1/`（`host-review-summary.json`、`implementation-independent-review-r1.json`、`resource-comparison-main-r1.json`、`audit-main-r1.json`、`p3-r1/acceptance.json`）及 `out/HardwareAcceptance/20260916/dpll-input-probe-r1/`（新旧原件、`comparison-main-r1.json`）。旧包/ELF/map 已在 build 前冻结，避免同名增量产物覆盖后混淆基线。生产 API 的保持行为由 host 反例对照证明，四板原件证明当前集成与输入现状，尚未实板注入坏样本。独立原件复核重解 CRC、重组原始 SD/RAM 回复并逐字节核对；文档与源码分离提交。
- 下一 gate：从稳定运行的有效帧直接接通自主 origin 的 reference TX 时间与接收侧时间关联，进入既有特等席及 VDC evidence 路径；确认实际 accepted/DCO 更新后再做实板坏样本保持、真实更新预算、共同时间命令应用及示波器锁相。首档成功、固定连续无错窗口、首 CS 精细证明均不是前置。既有全相位时序失败和正式精度任务保留，不能借本次 P3 通过追认；长期目标保持 active。
