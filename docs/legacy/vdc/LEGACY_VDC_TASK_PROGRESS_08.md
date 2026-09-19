# VDC task progress archive 08

Status: Frozen
Domain: VDC
Canonical: `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_08.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`
Last updated: 2026-09-19

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

