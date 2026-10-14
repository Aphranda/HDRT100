# VDC 历史进展快照 11

Status: Frozen
Domain: VDC
Canonical: `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_11.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/vdc/VDC_DOMAIN_TODO.md`
Last updated: 2026-10-14

> 按 C14 从进展日志迁出的最旧条目段；条目逐字保留、未编辑，顺序与源文件相同。
> 本快照为历史证据冻结件，当前状态以域 TODO 为准。

### VDC-PROGRESS-20260919-006：内部探针六十秒跟随实证与启动边界

- TODO task ID：`VDC-OBS-006`、`VDC-OBS-007`，继续 `IN PROGRESS`。本条所有数值为
  2026-09-19 实验快照，非产品事实源。运行期无主机查询、无 SD/USB 导出；Core1 原有
  summary 路径仍执行有界取时/计数/极值更新，不宣称零 CPU 开销或 GPIO 精度。
- 新工具 `tools/vdc_priority_trace/vdc_priority_trace_capture.py` 修复 context manager、
  纯 OK 回包、CLI 时长类型及 trace 生命周期；新会话完全 STOP 下 ARM trace/ACK，
  然后 TDMA ARM、各独立端口有界并行 START、origin trial、静默等待，最后全板
  STOP/读回后统一冻结、CRC 下载和 RELEASE/ACK。恢复模式不宣称旧运行全程静默；
  FULL、无输入、短覆盖、服务缺口、计数重置和下载失败均保留，未导出记录不释放。
  当前槽池只允许工具 `MAX_QUIET_SECONDS` 窗口，禁止等待更久后将前缀写成全程。
- `internal-summary-r1/` 回收旧 capture 9401..9404：四板约两秒空记录，CRC 和
  RELEASE 均成功。只读复核指出 trace 将 ARM/TRAIN 视为已运行，TRAIN→DATA 的
  adapter 临时退休触发 STOP 冻结；通用 start-ring 也未完整复用优先路径配置。
  改用既有测量矩阵、TAP、load mask、新 SYNC/MATCH/FOLLOW/phase 和 origin grant，
  不重测线序、不在 trace ARM 后 TRAIN。未修改固件或 OTA 实现。
- 四板 P3 `p3-internal-summary-r2/` 首轮失败：NO3 START 超时、缺四板末态记录；
  P0/P3 通过，失败保留。同固件 resume 的 `p3-internal-summary-r3/` 为
  `PASS_WITH_WARNINGS`，无 ERROR/FATAL。工具后续并行 START 的最终指纹验收见
  `p3-internal-summary-r4/` 也为 `PASS_WITH_WARNINGS`，ERROR/FATAL 均为零；最终
  staged 指纹以原始 `acceptance.json` / `alarms.json` 和 check-staged 为准。
- 专项 r2 在子进程参数类型检查前退出，四板已恢复；r3 串行 START 的六十秒采集
  无有效匹配，NO1 记录不足一秒，三从空段及饱和完整保留。改回已验证的有界并行
  START 后，r4 五秒实测三从均匹配并更新控制。不能仅凭这次 A/B 把全部启动失败
  归因为串口启动偏差；保留 source/handoff 原件作为后续重复性证据。
- `internal-summary-r5/` 完成静默 60.0 秒，运行查询数 0，全板 STOP、CRC、RELEASE
  和 RAM 参数恢复均成功。NO1 有效发布观察 10388 次；NO2/NO3/NO4 有效匹配为
  8091/8072/8125 次，首末成功事件跨度各约 60.45 秒；调频更新 14/16/29 次，
  相位更新 544/554/564 次。四板无 SERVICE_GAP、CLOCK_INVALID、UNBOUND 或字段
  饱和，证明内部观测能看到持续跟随与实际控制更新，不是靠末态零调频推断锁定。
- 严格整轮报告仍 `passed=false`：NO1 第一段保留 COUNTER_RESET，NO2 最后一段
  约 18.2 ms 为 PARTIAL/NO_SUCCESS/TERMINAL；不能抹掉边界段来追求全绿。
  NO2–NO4 在本地记录起点十秒后的残差区间极值分别为 −116..112、−140..105、
  −114..111 ns；这些是区间观测，不是 GPIO 边沿误差。每板最大服务间隔约
  5.98/19.70/21.64/19.62 ms，未触发 summary 的秒级 SERVICE_GAP 不等于逐周期
  确定性验收。图和离线汇总位于 `internal-summary-r5/capture/analysis/`。
- 软件测试：summary/capture 共 94 项通过；并行 START 后 capture 21 项再次通过。
  文档门禁与其 38 项测试通过。证据统一在 `out/HardwareAcceptance/20260919/`。
  下一 gate：把初始化计数重置与 STOP 终止尾段显式分级，保留原始 flags；在现有
  SRAM 池内设计可配置汇总跨度/累计检查点，处理计数及长间隔饱和后验证连续
  120–600 秒。扩窗属于后续独立固件切片，须重新 host/Release/四板 P3；内部探针
  与外部同窗 A/B 的开销和 GPIO 精度对照仍待完成，不授予 VDC 正式发布。

### VDC-PROGRESS-20260919-005：无示波器内部长期跟随探针接续

- TODO task ID：`VDC-OBS-006`、`VDC-OBS-007`，继续 `IN PROGRESS`。在已有外部示波器
  分钟检查点策略之后，补充 `tools/vdc_priority_trace/vdc_priority_trace_capture.py`。
  工具对各板 ARM `VDC_PRIORITY_TRACE_SUMMARY_PHASE_SCHEMA`，静默等待时不发送任何
  SCPI 查询；结束后才 STOP、等待冻结、分页读取原生 SRAM、校验 CRC、离线解码并
  RELEASE。输出 `summary.json`、每板原件和 `decoded.json`，保留 coverage incomplete、
  SERVICE_GAP、COUNTER_RESET、CLOCK_INVALID、终止段等事实。
- 主机测试 `tests/python/test_vdc_priority_trace_capture.py` 已通过 2 项，覆盖 ARM
  命令/时长边界和静默区间零查询。该工具尚未以当前源码指纹完成四板 P3，也尚未
  形成新的实板内部探针证据；不能把 host 通过写成锁相或长稳通过。
- 下一 gate：先运行当前源码 quick P3，再在四板保持已验证 TDMA/DPLL 配置下做短时
  内部探针闭环；确认 ARM/STOP/CRC/RELEASE 和每板 summary 连续后，再按既有长时
  检查点扩展到长期 profile。内部探针只判定跟随、缺口、服务裕量和 DCO/相位残差，
  外部示波器恢复后仍需同窗 GPIO 边沿对照。

### VDC-PROGRESS-20260919-002：分钟检查点连续六百秒通过

- TODO task ID：`VDC-DRIFT-001`、`VDC-PRECISION-001`、`VDC-RUN-001`，仍为
  IN PROGRESS。使用同一 observer 修复固件、同一 timing `24000,32000,16000 us`、
  同一 delay `0/-28/-88/-160 ns`，外部示波器每 5 秒触发四路短窗；120 个计划窗口
  全部完成，60/120/180/240/300/360/420/480/540/600 秒检查点全部继续运行。
  证据根 `out/HardwareAcceptance/20260919/dpll-checkpoint-r2/`，源码指纹
  `cc1a8c7961b3b0862a479e92ce9b6fb3cbfea79405975d94533a2fe715134d27`。
- 运行报告 `passed=true`、`errors=[]`、`cleanup_errors=[]`、`restore_needed=[]`；
  四板没有提前退休，最终由显式 STOP 收尾。离线 SHA/PREAMBLE/RAW 校验通过，
  生成 `analysis/summary.json` 和 `analysis/sampled-phase.svg`；运行中板卡查询数为
  0，示波器恢复 STOP/EXT/NORM。间隔采样只覆盖实际窗口，未采样区间仍不作精度声明。
- 相对 NO1 的 2 ns 网格上升沿统计：NO2 −30.37..−9.97 ns（中位 −22.15 ns），
  NO3 −36.00..−14.04 ns（中位 −26.69 ns），NO4 −53.52..−32.50 ns（中位
  −43.41 ns）；三从均满足 ±100 ns，NO2/NO3 观测窗口满足 ±50 ns，NO4 仍超出
  ±50 ns 优化目标约 3.5 ns。每路 240 个配对边沿，完整窗口无缺沿。
- 该结果证明活参考期限修复后可连续运行十分钟，但不等于绝对 ordinal 已对账，
  也不自动完成 VDC 发布；VDC-PRIORITY-01 v19 继续 pending。下一 gate 是在
  不改变持续调度的前提下，单因素调整 NO4 本地 delay/路径补偿，先做短窗口 A/B，
  再复跑分钟检查点；任何修改仍需匹配源码 quick P3。

### VDC-PROGRESS-20260919-004：补给时基扩展后的六百秒运行

- TODO task ID：`VDC-OUTPUT-001`、`VDC-DRIFT-001`、`VDC-PRECISION-001`，仍为
  IN PROGRESS。保持 NO4 −116 ns，单因素把运行时基从基线扩大到
  `40000,48000,32000 us`（实验候选，未写 Flash）。120 个外部采样窗口和 10 个
  分钟检查点全部通过，四板只在最终 STOP 退出。证据
  `out/HardwareAcceptance/20260919/dpll-timing-ab-r2/`。
- 2 ns 网格相对边沿：NO2 −31.40..−11.89 ns，中位 −22.27 ns；NO3
  −31.27..−12.08 ns，中位 −22.84 ns；NO4 −8.93..+12.63 ns，中位 −0.03 ns。
  每路 240 个配对边沿，无缺沿，均满足 ±50 ns；运行期间板卡查询数为 0。
- 运行阶段无提前退休、无提交拒绝；NO3 收尾第一次检查错误队列发现 `-200`
  Execution error，报告因此保留 cleanup warning。随后独立 STOP/读回确认四板
  PIO/DMA idle、timing 恢复 `12000,16000,6000`、delay 恢复 0、错误队列清空、
  示波器 STOP/EXT/NORM。该警告不被改写为严格全流程通过。
- 当前候选仅证明较大补给时基能显著提高本轮连续性，尚未完成跨启动重复性、故障
  恢复或正式 VDC 发布。下一 gate 是在保留失败证据的前提下复核收尾错误来源，再
  用候选配置做重复长跑；确认后才考虑 SCPI/Flash 固化。

### VDC-PROGRESS-20260919-003：NO4 delay A/B 与长期补给复现

- TODO task ID：`VDC-OUTPUT-001`、`VDC-PRECISION-001`、`VDC-DRIFT-001`，继续
  IN PROGRESS。基线 NO4 delay 为 −160 ns；误将绝对值写为 +44 ns 的首轮 A/B 在
  60 秒即出现约 +155 ns，相对基线增加约 200 ns，证明 delay 是绝对值且不能由
  边沿差直接当作新值。该轮按检查点停止并恢复，证据
  `out/HardwareAcceptance/20260919/dpll-no4-delay-ab-r1/`。
- 修正候选为 −116 ns（相对基线增加 +44 ns）。60 秒 A/B 通过：NO4
  −0.28..+23.94 ns，中位 +7.94 ns，NO2/NO3 仍在 ±50 ns；四板无缺沿，参数和
  scope 均恢复。证据 `out/HardwareAcceptance/20260919/dpll-no4-delay-ab-r2/`。
- 同一 −116 ns 候选长跑在 60/120/180/240/300/360 秒均通过，但 370 秒后 NO2
  无上升沿；420 秒检查点停止。NO2 末态 `reason=3`（`STARVED`）、blocks=22998、
  PIO TX stall；NO1/NO3/NO4 末态由 STOP 收尾。该失败发生在与 NO3 不同的节点，
  说明全局输出补给/调度竞争仍未闭合，不能把 NO4 delay 候选写入 Flash 或称为
  长期锁相参数。证据 `out/HardwareAcceptance/20260919/dpll-no4-delay-ab-r3/`。
- 下一 gate：针对不同节点随机出现的 STARVED，增加提交/服务边界的关联证据，区分
  PIO FIFO 真实耗尽、DMA guard 拒绝和 Core1 服务间隔；修复后先 quick P3，再用
  −116 ns 做 60→600 秒检查点复测。VDC-PRIORITY-01 v19 继续 pending。

### VDC-PROGRESS-20260919-001：分钟检查点快速失败与 NO3 输出补给失败

- TODO task ID：`VDC-DRIFT-001`、`VDC-RUN-001`、`VDC-RECOVERY-001`，继续
  IN PROGRESS。为缩短迭代，持续专项保留 5 秒外部采样，并在每个 60 秒边界
  （60 至 600 秒）检查一次；任一采样缺失、相位超过 ±100 ns、输出不连续或示波器
  错误即停止该轮，执行 STOP、末态读取和参数恢复。策略函数离线五组测试通过，
  真实本轮重放在 120 秒检查点判定失败；证据
  `out/HardwareAcceptance/20260919/dpll-checkpoint-r1/`。
- 真实连续运行使用 observer 修复后的四板固件，未在 RUN 中轮询板卡，示波器每 5 秒
  新触发四路 2 ns 网格短窗。60 秒窗口 NO2/NO3/NO4 均在 ±50 ns 观测范围；约
  75 秒后 NO3 无 2.5 V 上升沿，采样缺失持续存在。NO3 末态 RUN reason=3
  （`SYNC_IO_RUN_OUTPUT_STARVED`），blocks=4532、plan_rejects=1、PIO debug
  TX stall，说明当前主阻塞从活参考期限转移为 NO3 输出补给/调度裕量；NO1/NO2/NO4
  仍持续规划到停止。完整原件、末态和停止日志见
  `out/HardwareAcceptance/20260918/dpll-observer-continuous-r1/monitor-r1/`。
- 本轮未完成 600 秒锁相验收，也不宣称四板持续输出合格。四板最终均 STOP、PIO/DMA
  idle，输出 timing 恢复 `12000,16000,6000`、delay 恢复 0，示波器恢复 STOP/EXT/NORM
  且错误队列为零；首次收尾曾记录 NO1 恢复错误，随后独立只读复核通过，原失败保留。
  独立复核结论为持续诊断实现有限接受，v19 registry 保持 pending；见
  `out/HardwareAcceptance/20260919/dpll-continuous-review/independent-review.json`。
- 下一 gate：只针对 NO3 复现输出 STARVED，比较补给低水位、提交间隔、PIO stall 与
  NO2/NO4；修复后先跑匹配源码四板 quick P3，再按分钟检查点重新验证。禁止把增加
  检查点逻辑、缺失波形或人工重启写成长期稳定锁相证据。

### VDC-PROGRESS-20260918-051：显式持续诊断输出与活参考期限修复

- TODO task ID：`VDC-DRIFT-001`、`VDC-RUN-001`、`VDC-PRECISION-001`，继续
  IN PROGRESS。以下数字均为实验快照，非事实源。连续模式代码提交 `e902352c`；
  RUN duration_ms=0 与 TDMA TRIAL duration_ticks=0 显式持续，有限模式保留。
  保留有界块规划、不可改写 DMA 前缀、STOP/会话/时钟/资源退休、溢出及计数饱和。
  固件相关测试 394 passed，Release A/B/boot 链接通过，证据根
  `out/HardwareAcceptance/20260918/dpll-continuous-scope-r1/`。
- 连续模式同源码 quick P3 见 `out/HardwareAcceptance/20260918/p3-continuous-output-r1b/`，
  23 INFO/22 WARN/0 ERROR/FATAL；首次错误使用派生 topology summary 的拒绝保留。
  source `7b205e0d6997e5847be4d8f200831db1ab230bf693bf41a499265577412d14fa`，
  package `3164b4592fdeaf415f4567e9da8e38044006f840994f7049953403b28238d3fc`。
- 外部监测每五秒重新触发四路 RAW 短窗，RUN 无板卡查询或可选 trace；保留闭环
  必需的活时间戳。r1/r2 因示波器缩短内存后残留 RAW 传输区间失败，NORM→RAW
  重置修复。r3 完成至 445 s，旧分析器因 NO3 超过 100 us 中止；r4 至 175 s，
  下一次触发超时中止。失败原件及 STOP/参数恢复保留；未完成 600 s 验收。
- 两次波形均在约 60 s 后漂移。停止后 observer 读回三从 reason=5 SERVICE_AGE、
  joined 约 59725；代码 `tdma_event_start` 硬设活参考 epoch_limit 为 60 s。
  这使输出继续而闭环参考停止，不能直接归因 PI 不稳定。修复为现有 API 的
  `UINT64_MAX` 范围，保留唯一 counter lift、join timeout、序号/算术检查与 STOP，
  不新增 RAM。证据根 `out/HardwareAcceptance/20260918/dpll-observer-continuous-r1/`。
- observer 专项 3 passed，含真实 C observer 连续 601000 事件、多次计数回绕和
  STOP/旧 epoch 拒绝。扩展套件 40 passed/1 failed：既有 service fixture 仍模拟
  `vdc_dpll_manager_now_ns`，生产 owner 已改用 `board_uptime_ms`，造成编译失败；
  未改写原失败。产物脚本仅纠正 mock 后重跑同一生产 owner 测试通过，见
  `tests-final.log`、`verify_service_fixture.py`、`service-fixture-r2.log`；不宣称全套绿。
- 修复后 Release A/B/boot 链接通过，RAM free=20288 B，正式 49152 B 门槛仍未满足，
  沿用至 2026-09-25 的已授权 16384 B 临时许可；八项既有 SYNC 文本资源失败保留。
  同源码四板 quick P3 约 175 s，25 INFO/18 WARN/0 ERROR/FATAL，DPLL SKIPPED_TDMA_ONLY，
  严格调度失败原件保留；见 `out/HardwareAcceptance/20260918/p3-observer-continuous-r1/`。
  source `cc1a8c7961b3b0862a479e92ce9b6fb3cbfea79405975d94533a2fe715134d27`，
  package `f044d49136a313e89ed24d0f69b746dac47f20692daae72b53e77d5c1fbf4968`。
- 下一 gate：同次连续十分钟外部专项及 STOP 末态对账；波形缺失单独记录，不用
  监测错误重启环路。C11 对持续诊断与 observer 修复独立审核，v19 保持 pending；
  不能由输出无整次期限推定长期锁相或 VDC 发布合格。

### VDC-PROGRESS-20260918-050：原池全时段汇总与十二秒运行观测

- TODO task ID：`VDC-DRIFT-001`、`VDC-PRECISION-001`、`VDC-VERIFY-001`，父任务
  继续 IN PROGRESS。本条数字为实验快照，非精度契约。证据根为
  `out/HardwareAcceptance/20260918/dpll-summary-trace-r1/`。
- 增加独立 follower/origin 汇总 schema，复用原维护池及原 origin 扩展暂存区；
  首次 service 观察到 ring 运行即开始，不等待首个成功输入。约一秒一段保留成功
  原始区间极值、模型/频率范围及 owner 拒绝、取消、保持和采用计数；无成功输入、
  缺口、计数器复位/饱和、时钟异常均显式标记，STOP 提交末尾部分段。计数快照
  暂忙保留原基线，不伪造归零。旧详细模式、STOP/ACK/READ lease/CRC 协议不变。
- 独立复核 R1 复现 service 末尾越过段界后 raw 回退，使新旧时间段重叠、原生
  decoder 拒绝。已修为冻结带 CLOCK_INVALID 的末段，保留最后真实有序端点；
  成功回调同样处理，首次读时钟无效则不伪造记录。原失败 BIN 与复现程序保留。
  最终固件相关 89 项、decoder/联通 172 项及 SCPI 2 项测试通过（套件有重叠，
  不相加为独立总数）；包括六十秒真实 matcher 流、空段、STOP 尾段和旧会话隔离。
- Release 两槽及 boot Flash 链接通过。map 中工作区仍 288 B、原池 7600 B，
  主 RAM 余量仍 20296 B；正式 49152 B 门槛未满足，沿用已授权临时 16384 B
  调试许可，八项既有 SYNC 文本资源失败保留。见 `resource-audit.json`、
  `release-clock-fix.log`、`ram-debug-final.log`，不将诊断构建通过提升为正式资源验收。
- 同源码四板 quick P3 在 `out/HardwareAcceptance/20260918/p3-summary-trace-r1/`
  完成，约 196 s，25 INFO/18 WARN/0 ERROR/0 FATAL；DPLL 为 SKIPPED_TDMA_ONLY，
  原严格调度失败保留。复用已确认线序，基础范围未扩展，OTA 实现未改。
  源码指纹 `6b26e520e3469b7baa480e33827046f87197cc05b8b837a2df37aae4cf0ba44b`，
  包 SHA256 `56baaa28e69c5be8a363274428a1da35b13b5c4f09a788bf82a91f6c6f986464`；
  build ID 仍复用，身份以哈希为准。
- `capture-r1` 完整采集与恢复通过，约 128 s；保持 output delay
  `0/-28/-88/-160 ns`、timing `24000/32000/16000 us`，未保存 Flash。
  四板各 13 段，覆盖约 12.247/12.480/12.697/12.905 s；NO4 首段
  NO_SUCCESS/UNBOUND，随后连续成功输入段约 11.905 s。离线全段输入完整检查
  因此为 FAIL，原件与判定保留；这是实际启动空段，不能写成零相位误差。
  其余段没有时钟/服务缺口/压缩饱和标记。成功数为 6801/2740/2852/2993，
  owner 拒绝不等于独立坏帧数，也不将汇总成功等同实际 GPIO 输出。
- 同轮示波器实际窗口为约 8.9..9.9 s，20 ns 网格，每路 1000 上升沿；
  三从周期近邻边沿差约 -20.22..19.77 / -20.22..19.66 / -40.11..0.11 ns。
  该后段进入 ±50 ns，但周期取模配对不能证明绝对同 ordinal 或全程百纳秒精度。
  完整原件、导出页/CRC、START/STOP 与 RUN 零查询审计见
  `capture-r1/summary-audit.json`；启动及整个输出窗口的精度仍需分别验证。
- `capture-r2` 同参数独立启动的采集与恢复通过，约 129 s；四板各 13 段，
  覆盖约 12.204/12.434/12.649/12.850 s。NO1 首段计数器重置、NO4 首段
  NO_SUCCESS/UNBOUND 被显式保留，所以全段输入完整检查仍为 FAIL；每板随后
  连续可用成功输入段均超过十一秒。示波器同窗口每路 1000 沿，三从周期相位
  约 -22.07..0.17 / -40.05..0 / -59.53..-19.83 ns：均在 ±100 ns，NO4
  不在 ±50 ns。不挑选首轮较好结果，不将两轮后段外推为持续完整锁相。
- 用户随后明确：DPLL 应持续闭环，开启后只用外部示波器每五秒触发采集一至两个
  周期；同一次运行分别汇总六十秒、五分钟和十分钟，运行中不启用内部采样或
  轮询板卡。下一切片增加持续输出模式，解除整次调试输出的时间上限，同时保留
  有界块规划、STOP/会话/时钟失效与硬件故障退休；不通过周期重启拼成长运行。
  本切片没有改变输出时长限制，不以六十秒软件测试冒充六十秒硬件运行。
- 源码已提交 `08af197f`。独立方 `phase_center_review` 复现并关闭 R1，批准
  `VDC-PRIORITY-01 v18 pending` 诊断切片及对应文档；结论与两轮原件复核见
  `independent-review-closed-r2.json`、`independent-final-evidence-r2.json`。
  原生完整性失败、第二轮 ±50 ns 未达及正式资源缺口保持原判。

### VDC-PROGRESS-20260918-049：两轮运行后段 ±50 ns 与十秒连续波形

- TODO task ID：`VDC-DRIFT-001`、`VDC-PRECISION-001`、`VDC-VERIFY-001`，均继续
  IN PROGRESS。本条数字为有限实验快照，非精度契约。证据根为
  `out/HardwareAcceptance/20260918/dpll-sustained-window-r1/`；固件保持 048 的
  `55aa4e48e668abb485361e54d8d0041b45db0b573387226729eb871b50997030` 源码指纹，
  使用既有匹配四板 P3 凭证，未部署/修改固件、未重复 P0、未写 Flash。
- `capture_window.py` 复用已审查的 MAIN 配置、新 WAIT/SING 和四独立连接并发
  START，四个显式 OK 且线程全部退出后才发 TRIAL；静默运行后 STOP 导出，保持
  output delay `0/-28/-88/-160 ns`、timing `24000/32000/16000 us`。请求时基与
  实际 PRE 均核验；迟窗按最近边沿计算周期相位，不套用首沿序号。
- `late-r1/r2` 完整流程分别约 141/137 s，通过采集与参数恢复。实际 PRE 均为
  8.9..9.89999998 s，每路 50 M 点、20 ns 网格；四路各 1000 沿，每从 998 对，
  未见大于 1.5 周期缺口或小于 0.5 周期额外边沿。
  r1 NO2/NO3/NO4 约 -20.225..19.773 / -20.222..2.478 / -40.111..0.054 ns；
  r2 约 -34.347..12.739 / -23.834..1.434 / -40.169..-19.725 ns，均在 ±50 ns。
  输出相对相位线性趋势 r1 约 -6.48/+3.03/-2.93 ns/s，r2 约 +2.76/+0.77/+1.08 ns/s；
  这是包含相位调整的有限窗口趋势，不是晶振绝对 ppb 或长期频率资格。
- 独立审核逐块复算两轮共 400 个 RAW 块、原生 CRC/身份、实际交点及一对一配对，
  结果一致；RUN 无板端或示波器查询，STOP/恢复通过。r2 分析初次因 Windows
  粗粒度 monotonic 时间相等触发严格 `<` 断言；原失败记录保留，同步 arm 返回及
  WAIT/SING 先于 START 已核实，改为 `<=` 后分析同一份原件，未重采掩盖。
- native 前缀限制已从代码和实测确认：四板均 76 条 FULL；r1 时间跨度
  130/4704/4840/4742 ms，r2 为 122/4722/4663/4739 ms。这些记录不覆盖第九秒，
  不用 STOP 模型端点冒充全程内部证据。MATCH 抽取并不能限制 ORIGIN/PHASE/DECISION
  的额外记录；后续考虑复用原池的有限时间桶统计，方案未实现、未增加 RAM。
- `continuous-r1` 已取得实际 0..9.9999998 s 的四路 50 M 点波形，200 ns 网格。
  NO1/NO2/NO3/NO4 分别 10000/9996/9996/9998 沿，起始输出时间不同，未见各自
  输出期间的粗缺口/额外边沿。启动相对周期相位偏差最高约 19 us；固定 0.4 s 后至末段的
  粗测范围约 -15..218 ns。200 ns 网格不能判定是否达到 ±100 ns，也不能直接将
  约一个网格的变化归为真实抖动；此轮仅补连续性与未持续发散证据。
- 十秒轮完整流程仍为 FAIL：STOP 后 NO2 原生 READ offset 1412 返回 timeout，
  上层报告 Wrong RAM page field count；四路波形完整，四板 STOP 与恢复成功。
  `recover_native.py` 仅在 STOP 读取同份保留捕获，完整 7768 B、CRC/身份/状态及
  首次已成功的 12 页逐字一致，恢复成功；原报告和错误未改写。分析显式引用独立
  recovery，`original_capture_passed=false`。这是导出恢复，不是重新运行通过。
- 原件与分析见各轮 `window-audit.json`、`periodic-phase.svg`，对照为
  `late-comparison.json`；恢复记录为 `continuous-r1/native-recovery/recovery.json`。
  独立十秒复算见 `independent-continuity-review.json`；文档门禁及 38 项自回归通过。
  迟窗整数周期身份、通道误差预算、冷启动和全程百纳秒资格仍未证明。
  下一 gate：有界全时段内部观测与连续精度证据；随后恢复/质量和 VDC 一致发布。
  保持固定 P3 范围。当前四板 STOP、参数恢复，示波器 STOP/EXT/NORM。

### VDC-PROGRESS-20260918-048：从板采样区间缩窄与两轮热启动有限 ±50 ns

- TODO task ID：`VDC-OUTPUT-001`、`VDC-PRECISION-001`、`VDC-FAST-003`，父任务
  IN PROGRESS。按新长期目标先减小中心偏移并验证重复性；本条数字为实验快照。
  证据根：`out/HardwareAcceptance/20260918/dpll-stable-lock-r1/`。未修改 OTA、
  PIO 程序、DMA/SM 分配、路径 delay 或控制算法；SCPI 仅配置/触发，RUN 零查询。
- 旧源码同参数 `startup-r1` 完整通过约 133 s：固定 0.4..0.9 s、每从 500 组条件
  同序边沿分别约 -20.94..0.11 / -39.77..-0.06 / -40.20..-19.11 ns，已在 ±50 ns。
  因此新切片不能将“首次达到 ±50 ns”归为自身成果。旧两轮加本轮合并仍有跨启动
  中心变化，NO3 合并跨度约 139 ns，单个固定 delay 不足以保证旧样本均在 ±50 ns。
- `tdma_event_enable_anchor_capture()` 将 TIMER1 读取改为 H1/H3/L1/enable/L2/H2/H4，
  两次高字读取移出低字包围区间；唯一同步 enable、完整边界、失败输出不变、时钟
  检查与 owner 保持。低字回卷在任一重叠 H/L/H 内均保守拒绝，不重试。
  此拒绝会令该 observer epoch 的锚点不可用，需既有机制重建 epoch，不能写成下一帧
  必然恢复。实现及匹配 P3 凭证独立提交 `8453f675`。
- 生产 helper 的 MMIO harness 覆盖全部五处回卷位置、64 位回卷、时钟、指针及
  单次 enable；相关回归 513 项通过。Release A/B 与 boot Flash 链接通过；实际两槽
  SRAM 汇编为 H/H/L/enable/L/H/H，低字区间含 DMB、寄存器准备与 SRAM 栈写，
  无调用、重试或 IRQ 屏蔽。见 `follower-anchor-tests.log`、`follower-anchor-release.log`
  和 `follower-resource-review.json`。RAM 空闲仍为 20296 B，仅符合已授权临时
  16384 B 许可；正式 49152 B 门槛及八项既有 SYNC 文本资源检查失败继续保留。
- 首次 P3 命令缺四板快速模式显式参数，在硬件操作前拒绝，日志保留。
  正确命令为 `python tools/hardware_acceptance/p3_hardware_acceptance.py run
  --config config/hardware_acceptance/p3_bench_quick.json --tdma-only --diagnostic-continue
  --build-dir out/build/p3-timer1-20260918 --out-dir out/HardwareAcceptance/20260918/p3-follower-anchor-r2
  --reuse-topology out/HardwareAcceptance/20260918/p3-phase-center-r3/known-topology-source.json`。
  约 181 s 完成，23 INFO/22 WARN/0 ERROR/0 FATAL；基础 TDMA 通过，DPLL 为
  SKIPPED_TDMA_ONLY。源码指纹 `55aa4e48e668abb485361e54d8d0041b45db0b573387226729eb871b50997030`，
  包 SHA256 `5bd2883814657093af2b55fb560e1f3d31c8026ea207c24082b79dab2e4ff61b`；
  build ID 复用，以上哈希为身份依据。凭证 20 项原件哈希复核，包另存证据根。
- 新源码 `follower-r1` 采集及恢复完整通过，但 P3 重启后首次相位动作为约
  +88/+183/+286 ms，已提交 ordinal 跨度比边沿计数多 88/183/285；原连续序号假设
  分析拒绝，不能将差额直接解释成同数物理丢脉冲。原生区间仍可独立复核：三从
  本地宽度 72→52 ns，MATCH 中位 192→172 ns，NO1 仍约 120 ns。
  `native-only-review.json` 明确不授予物理精度。`follower-r2` 在 NO1/NO4 START
  返回 timeout 后未发 TRIAL，全部线程结束后 STOP/恢复，失败未改写或冒充成功启动。
- 已建立模型后，相同 delay `0/-28/-88/-160 ns` 的 `follower-r3/r4` 两轮均完整通过，
  各约 131 s。每轮一秒连续四路记录覆盖启动和收敛；固定 0.4..0.9 s 窗口每从 500 组：
  r3 NO2/NO3/NO4 约 -19.94..39.49 / -20.17..20.08 / -39.88..20.06 ns；
  r4 约 -19.68..39.54 / -20.22..19.89 / -39.94..17.28 ns，均在 ±50 ns。
  两轮极值中点差约 0.16/0.13/1.42 ns，仅描述该有限样本，不提升仪器精度。
  三从本地原生宽度均为 52 ns、MATCH 中位均为 172 ns；原生 CRC/schema、模型末态、
  STOP、请求/实际/恢复参数均通过。原件见各轮 `startup-audit.json`、
  `native-scope-review.json`、`startup-conditional-phase.svg` 及根目录
  `start-center-comparison.json`；不同源码的固定 delay 分布分别统计。
- 独立复核重新校验两轮 400 个 RAW 块及八份原生二进制，按原始 preamble/1.5 V
  阈值复算各 500 对边沿，与上述范围一致。MATCH 中位为 172 ns，少量 176/180 ns
  样本仍保留，不宣称所有记录恒宽。文档门禁及 38 项自回归通过；按 C14 将最旧
  连续 018/017 条目逐字移至既有 archive 04，保留证据及索引闭包。
- 结论只支持区间缩窄及两次热启动的有限窗口重复性。仍为条件 `first_ordinal+j`
  配对，非独立首脉冲身份、断电启动、长期锁定或 VDC 有效发布证明；20 ns 样点间隔
  不等于通道间测量资格。下一 gate：保持同参数，扩展较晚/持续窗口并分开验证
  模型预热与冷启动；并行补齐正式序号、误差预算、恢复和 VDC 发布，不增加 P3 基础范围。


### VDC-PROGRESS-20260918-047：并发 START 首沿覆盖与连续一秒收敛复测

- TODO task ID：`VDC-OUTPUT-001`、`VDC-PRECISION-001`、`VDC-FAST-003`；父任务
  IN PROGRESS。本条数字为有限实验快照，非锁相契约。固件源码及 P3 沿用 045，
  未部署新固件、未改 OTA、未写 Flash。适配器和全部失败原件继续位于
  `out/HardwareAcceptance/20260918/dpll-extended-window-r1/`，代码事实源未修改。
- `capture_startup_parallel.py` 在 START 前检查四个已打开且身份核验的独立串口，
  每线程只访问一个连接；意图先保存，全部事务 join 后由主线程一次落盘。任何超时或
  异常均先 join、再 STOP，不盲重试、不在 RUN 查询。八项离线验证覆盖四个超时位置、
  异常、缺会话与共享句柄拒绝，首次日志换行问题和时钟相等断言失败均保留；修复后
  `parallel-validation-r3/report.json` 通过。独立复核见 `independent-parallel-start-review.json`。
- `startup-parallel-r1` 在 START 前因示波器时基读回不符失败并恢复。scope-only 对照
  确认 RUN 后重申 MAIN、设置时基并核验 OPC/错误队列可恢复配置；慢时基后再重申 MAIN，
  正确读取 offset。未通过的较大存储请求和一次查询超时保留；当前实际深度为 50 M，
  不把请求的 500 M 当成能力。`startup-parallel-r2` 四 START 均为真实 OK、首沿均可见，
  但 NO2 末态模型读取超时，原试验仍 FAIL。条件首序号对账的启动偏差为数十微秒，
  不与七秒后的稳定窗口混淆；收尾 STOP/恢复完整。
- `startup-one-second-r1/r2` 两轮完整通过，各约 131 秒；四板 STOP、输出补偿恢复，
  示波器 STOP/EXT/NORM。START 前确认 WAIT/SING，运行期间零查询；实际每路 50 M 点、
  20 ns 网格，连续覆盖 NO1 首沿前 0.1 s 至其后 0.9 s。维持输出候选
  `0/-28/-88/-160 ns`，不修改 path delay。四板首沿之前低电平及后续脉冲均在同份
  记录中可见，不再用不同轮次拼接启动与收敛。
- `audit_startup_v2.py` 按每板 `first_ordinal+j` 作条件分析，不做最近边沿或整数周期
  平移；原 `audit_startup.py` 按 046 hash 恢复。第一轮三从约十几微秒的偏差在
  0.4 s 前进入百纳秒范围。固定 0.4..0.9 s 窗口每从各 500 组：r1 NO2/NO3/NO4
  范围约 -60.17..0 / -79.09..-2.14 / -79.89..-19.94 ns；r2 同窗约
  -0.30..39.89 / 0.11..59.55 / -20.93..20.22 ns。窗口内没有漏周期形态；跨启动
  中心仍变化，未固化补偿。两轮原生 CRC/schema、会话、真实 DCO 末态及请求/实际/恢复
  读回一致，原件见各轮 `native-scope-review.json` 与 `startup-audit.json`。
- r1 原生首次相位 delta 为 -16578/-17451/-17782 ns；外部最大单周期延长约
  16580/17442/17780 ns，方向及幅度一致。`phase-step-magnitude.json` 仅支持实际
  引脚采用的量级对照，尚不是独立硬件事件标签的逐脉冲因果证明。
- 阶段汇总见 `VDC_DPLL_STATUS_REVIEW.md` 及其 A4 HTML/PDF：沿用指定模板，补齐
  250 MHz / 4 ns、用户确认的 5 m 网线 / 10 Mbit/s 四板环路、锁相组成、连续收敛、
  同脉冲全波形与上升沿（−2～+7 V），区分 ±50 ns 优化目标和 FPGA 纯硬件演进展望。
  `report-frequency.json` 从两轮 RAW 各核验 200 块，固定半秒窗口拟合相对 NO1 的
  输出平均频差：r1 为 -8.4/+2.2/+4.1 ppb，r2 为 +2.7/+6.1/+13.6 ppb；含相位校正，
  不等于晶振误差或长期精度。DCO 末态修正单列；无新增硬件运行或固件变更。
- 首沿可见消除了窗口裁剪，但若真实第一脉冲丢失，仅“先低后高”不能独立识别；因此
  `common_ordinal_qualified` 与 `physical_lock_qualified` 均保持 false。下一 gate：
  用独立首沿/序号标记或完整有限输出终止证据消除缺首脉冲歧义，补齐漏/多脉冲、整周期
  位移、裁剪及跨代负例；随后扩展持续稳定性、独立路径 delay、单圈期限、误差预算与
  VDC 一致发布。中点补偿、采样分辨率或这两轮有限结果均不替代最终发布验收。

### VDC-PROGRESS-20260918-046：冻结补读、4 ns 外部采样与首沿覆盖缺口

- TODO task ID：`VDC-OUTPUT-001`、`VDC-PRECISION-001`；父任务 IN PROGRESS。
  本条数字是本轮实验快照，非锁相或路径 delay 契约。固件、工具事实源和 P3 源码指纹
  未变，继续使用 045 的 `c24e06dd`、`p3-phase-center-r3/` 及临时 RAM 许可；本轮仅在
  `out/` 使用实验适配器并维护文档，没有新固件部署，也不新增 P3 精度门禁。证据根为
  `out/HardwareAcceptance/20260918/dpll-extended-window-r1/`。
- 扩展窗口 r1/r2 在 START 前因水平 offset 读回不符失败；r3 的 NO2 STOP 后 RAM
  页读取异常、波形按请求的 50 M 点读取而在实际 27.85 M 点边界被拒绝。四板 STOP、
  参数恢复完成，原失败保留。未重新 ARM 的补读获得 NO2 7768 B 完整 CRC/native，
  此前 42 页相同；四路 112 个波形块及原 CH1 的 27 块 hash/连续性独立复核通过。
  原件为 `compensated-r3/recovered/`、`independent-scope-recovery-audit.json`。
- 补读的实际时间轴为 -0.557 s 至 -20 ns，但四路均为低电平噪声、没有上升沿，
  明确拒绝作为任何相位证据。首次 STOP 查询前没有示波器 STOP 命令；后续读回 MAIN。
  不能把该现象确定归因为主动截断、roll 或 DPLL 丢失输出。50 ms/div 配置也未达到
  请求 offset；独立 scope-only 诊断保留，强制触发诊断中的非法小数写法失败同样保留。
  20 ms/div 已知配置恢复正常，不靠放宽断言使用错误窗口。
- `dense-r1/` 保持输出补偿 `0/-28/-88/-160 ns`，静默约七秒后 CH1 上升沿 SINGLE，
  实际 50 M 点覆盖 0.2 s，采样网格由先前 20 ns 提高到 4 ns。完整流程约 144.8 秒通过，
  四份原生解码、DCO 末态一致、RUN 参数请求/实际/恢复一致，运行期间零查询。三从
  相对 NO1 的有限范围约为 -31.85..-13.32 / -35.13..-21.03 / -63.70..-47.94 ns；
  各 199 组近邻配对，参考索引连续、无重复或间隙。窗口内未见漏周期；1.2/1.5/1.8 V
  三种阈值的结论一致。原生与波形绑定见 `comparison.json`，图及原始分析见
  `dense-r1/capture/scope-analysis/`。仍是 modulo 周期的有限相位，不是同 ordinal 锁定。
- `startup-r1/` 改为 START 前确认新 WAIT/SING、采用 -0.1..+0.1 s 同步四通道记录，
  其余参数保持。适配器离线覆盖正常流程及四个 START 拒绝位置，运行期间零查询；
  硬件完整流程约 131.2 秒通过，四板 STOP、输出补偿恢复且示波器 STOP/EXT/NORM。
  实测 START 发送跨度约 219 ms；NO1/NO2 的首沿在窗口内，NO3/NO4 的首沿已被裁剪，
  因而 `startup-audit.json` 明确保留 `common_ordinal_qualified=false`。
  内部 first_ordinal 及计划边沿连续不能补足未采到的首沿，也不能用整数周期平移修图。
- 下一 gate：保持当前补偿，优先建立首沿覆盖及同 ordinal 对账；评估在现有独立串口
  会话上有界并发发送 START、全部显式 ACK 后才触发 TRIAL，需先验证会话/日志并发、
  任意启动失败的 STOP 收尾；或修复较长触发窗口。随后扩大稳定期连续观测，保留独立
  forward-CS delay、单圈期限、完整误差预算及 VDC 一致发布，不将 4 ns 网格冒充物理精度。

### VDC-PROGRESS-20260918-045：冻结记录恢复与 Core1 相位中点策略

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`；父任务 IN PROGRESS。数字为有限实测
  快照，非发布或精度契约。旧算法输出补偿 `0/-120/-168/-232 ns` 的 r3/r4 两轮完整通过，
  三从有限窗口约 `0..+40 ns` / `-20..+20 ns`；此前约 -200 ns 轮仍保留，不宣称重复性闭合。
- 044 的 NO2 RAM 格式错误原始返回是 `<timeout>`，并有末态 -200；不是收到损坏页。
  未重新 ARM 的冻结记录恢复得到 7768 B，完整 CRC、schema 解码和此前 32 页逐字节一致。
  原失败不改写，见 `dpll-late-arm-r1/midpoint-r2/recovered-no2/`。固件只读链有快照/所有权
  暂忙拒绝，具体触发分支未记录；不以增大超时或放宽解析解决。r3/r4 后续完整导出成功，
  不能据此称瞬时读取拒绝已修复。该问题不阻止新的完整专项继续推进物理主线。
- 独立复核确认旧 `priority_phase_delta()` 只推向最近零端点，宽区间会依接近方向停在
  两侧。旧实测末态 `[-194,-2]` 与 `[8,200] ns` 和该机制一致，但末态非波形同窗，
  不声称已完成唯一因果归因。改为残差区间向零取整的中点，先限幅再取负；完整区间、
  频率优先、更新间隔、发布回执、累计平移归一化、STOP/身份取消不变。中点仅为控制估计。
- 初审发现 decoder 仍按旧端点规则拒绝合法新控制，故增加独立相位 schema 6，历史
  schema 4 保持严格原规则。真实 Core1 非对称跨零记录经实际 producer/decoder 联通，
  并覆盖 int64 两端、无界整数 oracle、限幅、历史错标、错误中点、模型/累计量及生命周期。
  六套主机回归 567 项通过，JUnit 为 `out/pytest/phase-center-20260918-r4.xml`。
- Release A/B 和 Flash link 通过；RAM 余量仍 20296 B，正式 49152 B 门限不满足，
  继续已授权临时许可。八项既有 SYNC 资源文本失败保留，其输入与 HEAD 按 LF 归一后相同。
  证据根 `out/HardwareAcceptance/20260918/dpll-phase-center-r1/` 的 `resource-inputs.json`
  及日志不冒充正式资源全绿。P3 r1 错把复用摘要当原拓扑而在硬件前拒绝；r2 为记录版本
  修复前的中间源码验收。最终源码 `p3-phase-center-r3/` quick P3 约 232.9 秒，
  PASS_WITH_WARNINGS、INFO/WARN/ERROR/FATAL=23/22/0/0；DPLL SKIPPED，TDMA 严格失败保留。
- 新策略零 output delay 两轮 `zero-r1/r2` 均完整通过，NO2/NO3/NO4 相位中位分别约
  `+39/+87/+160 ns`、`+20/+80/+160 ns`。NO1 schema5/三从 schema6 解码、末态真实 DCO
  一致性、四板 STOP 和恢复通过。观测仍为静默约七秒后的有限窗口、20 ns 网格、近邻
  modulo 周期配对，不替代同 ordinal 及全时段证明。后续按合并范围中心设置
  `0/-28/-88/-160 ns` 输出候选，独立验证物理重复性，不修改 path delay。
- 同候选 `compensated-r1` 完整通过，NO2/NO3/NO4 相对 NO1 的有限波形范围约为
  `-20..+20/0..+38/-40..0 ns`。仅输出 RAM 配置改变，运行期间零查询，STOP 后参数恢复；
  原生重放与真实 DCO 末态一致。`comparison.json` 绑定原件 hash、请求/实际/恢复值、
  会话、版本、DCO 和波形。`compensated-r2` 同参数复测完整通过，范围约
  `-21..+20/-21..+20/-39..+1 ns`；两轮有限窗口均在 100 ns 内，各 199 组近邻配对。
  不提升为全时段、同 ordinal、跨重上电或最终 VDC 发布已验收。
- `origin_bracket_audit` 最终独立复核通过 v17 pending，无剩余阻断；核验 16 份原生
  解码和 160 个波形块 hash，原件为上述证据根 `c11-v17-independent-review.json`。
  代码/匹配凭证提交为 `c24e06dd`，staged 指纹与 P3 门禁通过；文档独立提交。
  下一 gate 为更长观测及启动样本、同 ordinal/实际持续输出与时间误差预算闭合。

### VDC-PROGRESS-20260918-044：中点输出补偿的两轮逼近复测

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`；父任务仍为 `IN PROGRESS`。本条数字是
  有限波形快照，非锁相或路径 delay 契约。沿用较晚窗口、CH1 上升沿触发和运行期间零查询，
  仅在 STOP 配置输出 delay，试验结束恢复原值。证据目录为
  `out/HardwareAcceptance/20260918/dpll-late-arm-r1/`。
- 依据零补偿较晚窗口的相位范围，按 4 ns 量化取中心的反向值：NO1/NO2/NO3/NO4
  `0/-120/-168/-232 ns`。第一轮波形相对 NO1 约为 `-220..-180/-214..-162/-220..-180 ns`
  （NO2/NO3/NO4），说明该补偿实际生效但跨启动状态发生了整体漂移。
- 第二轮保持同一组参数，波形回到约 `-20..+20/-20..+40/-20..+20 ns`，有限窗口内三从均
  落在 100 ns 范围；这只能证明“中点补偿可逼近”的一次观测，不能证明跨启动重复性或持续
  100 ns 锁定。两轮都因 STOP 后原生 RAM 导出异常而记为失败：第一轮有 NO4 `local_model`
  超时，第二轮有 NO2 RAM page 字段格式错误；四板 STOP、delay 恢复和示波器收尾均完成。
- 当前可用的内部观测仍是 ORIGIN/MATCH/PHASE/FOLLOW 原生记录与 RUN 状态；它们能确认同事件、
  residual、DCO 采用和模型代际，但不能替代 GPIO 共事件边沿。示波器相对边沿仅用于输出逼近，
  不反推 path delay，也不授予 `physical_lock_qualified`。下一 gate 是先修复/隔离 STOP 导出
  读取的可重复性，再用相同中点参数做连续跨启动复测；若中心随启动改变，应转向频差收敛和
  输出映射的原因定位，而不是继续固化单个补偿常数。

### VDC-PROGRESS-20260918-043：输出补偿对照与 START 超时误报修复

- TODO task ID：`VDC-OUTPUT-001`、`VDC-FAST-003`；父任务 IN PROGRESS。数字均为有限
  实测快照，非精度契约。本轮先使用 `a7fdd387` 固件做 STOP RAM 输出补偿 A/B/A，
  无 Flash 参数保存、无路径 delay 修改，运行期间零查询。证据根为
  `out/HardwareAcceptance/20260918/dpll-origin-bracket-delay-r1/`。
- 成功采集 `zero-r2`、`candidate-r1`、`restore-r2`，分别约 59.8/63.0/59.3 秒。
  候选 NO1–NO4 输出 delay 为 `0,+140,+80,0 ns`，RUN10 实际值和 STOP 读回均匹配，
  每次结束都恢复原始零值；路径表不变。`comparison.json` 从原生二进制重新解码并
  检查 CRC、会话、请求/实际/恢复值及 STOP 退休，绑定证据 hash。

| 同源码波形窗口中位（相对 NO1，ns） | NO2 | NO3 | NO4 |
|---|---|---|---|
| zero-r2，零补偿 | -140 | -80 | -5 |
| candidate-r1，输出补偿生效 | +300 | +299 | +280 |
| restore-r2，恢复零补偿 | +161 | +200 | +260 |

- 候选撤销后 NO2 中位变化约 -139 ns，与撤销 +140 ns 接近；NO3 约 -99 ns，NO4
  无补偿仍变化约 -20 ns。说明存在可观测输出补偿响应，但独立启动的控制状态未固定，
  不能把三轮差值当作独立 transfer gain 校准，更不能由它反推路径 delay。
  NO1 原生模型频率三轮分别约 +2115/+3053/+3798 ppb，RUN enable 代理仍约
  988–1004 ns；NO1 编码宽度中位仍为 120 ns、三从 residual 宽度约 192 ns。
  零值轮临近波形窗残差在正侧约 `[+70,+280] ns`，候选/恢复轮在负侧约
  `[-245,0] ns`。近窗对应仅以 NO1 首 ordinal 建立近似 VDC 窗，不是逐 GPIO 归因。
  不同控制方向及主频变化是当前重复性核验的输入，不归因于示波器触发变化。
- 两次失败完整保留：`zero-r1` 为 NO2 START 无真实 ACK、末态错误 -200、RUN
  blocks=0；`restore-r1` 为 NO4 同类失败。其他从板虽有开环输出但模型未更新，NO1
  未取得本轮有效原生发布；不能使用旧 RELEASE 留存值证明新会话发车。两次最终
  四板 STOP/配置恢复通过，独立完整生命周期重采才得到上述有效窗口。
- 只读审核确认公共 `tdma_start_ring._board_command_on_serial()` 把 START timeout
  直接改写为 `OK(no payload; verified by state readback)`，实际没有 readback；采集器
  接受该值后继续主板 START/TRIAL 和整轮等待。固件 START 单次读取 result_guard、
  配置/ARM/train 身份并复验；瞬时发布竞争可能拒绝，Core0 control lock 等分支也
  未排除，现有证据不足以断言具体拒绝原因。末态 MATCH STOP reason 不是根因。
- 工具只对 START 保留 `<timeout>` 原件，不增加 RUN 查询或盲重试，不改变固件
  admission。现有调用链遇到非 OK 抛错并执行 finally STOP。相关四套 host 为
  173 项通过；实际 acquire/control/backend 注入四个板位的超时，均不继续 NO1 TRIAL、
  不进入静默等待、不查询 RUN，见 `check_ack_cleanup.json`。该注入不模拟完整 STOP
  硬件行为。较宽五套测试为 295 通过/1 失败：既有
  `test_core1_overrun_quarantines_only_the_faulting_load` 文本定位断言已不符合当前
  app 拆分，相关两文件与 HEAD hash 相同，保留 `preexisting-test-failure.json`，未改
  调度隔离行为或把失败写成全绿。
- 工具切片同源码四板 quick P3 为 `p3-start-ack-r1/`，约 183.7 秒，
  PASS_WITH_WARNINGS、INFO/WARN/ERROR/FATAL 为 25/18/0/0；TDMA 严格原始失败和
  DPLL SKIPPED_TDMA_ONLY 保留。build 仍为 `20260918081154`，源码指纹
  `bc42aeb8564b3fa64b98bd8196f5d7bba89dd1c9d38027eea62099ba255c5f2c`。
  生产固件没有修改，Release 复用有效构建并完成门禁；RAM 仍依 042 的临时许可，
  不宣称正式 RAM 或旧静态资源检查器通过。
- 工具与匹配凭证提交为 `e2ee1927`，pre-commit staged 指纹核验通过。随后
  `ack-fixed-r1` 真实四板采集约 59.0 秒通过，四个 START 均为真实 OK，零补偿，
  STOP/参数恢复无错；原生重放通过，NO1 宽度中位仍为 120 ns。P3 复位后从板由
  0 ppb 重新跟踪，外部 NO2/NO3/NO4 相位中位约 +200/+261/+519 ns，窗口最大
  约 +280/+360/+701 ns；不将采集流程通过当作锁相通过。此轮没有自然 START
  拒绝，错误分支提前退出由故障注入证明，实际拒绝后的硬件收尾仍待专门观测。
  文档两检查器和 38 项治理测试通过，既有其他域 WARN 保留。
- 下一 gate：保留 START 具体拒绝分支的 STOP 后诊断计划；继续分离主频变化、
  从板频率收敛与相位区间死区，不冻结本轮候选
  输出补偿。物理 100 ns、同圈期限、独立路径校准和 VDC 一致发布仍未完成。

### VDC-PROGRESS-20260918-042：NO1 enable 采样重排及内部原生区间收窄实测

- TODO task ID：`VDC-FAST-003`、`VDC-OUTPUT-001`；父任务 IN PROGRESS。数字均为
  本轮有限证据快照，非时序或精度契约。用户要求继续主线并询问内部观测手段，本轮
  使用既有有界 SRAM 原生记录、STOP 后导出及 NO1 CH1 正边沿触发外部波形；四板、
  探头 1×，运行期间零查询。未改 OTA、控制律、path delay、输出 delay 或记录 ABI。
- `tdma_origin_plan.c` 将 PADOUT 移到计时包围前，执行顺序为
  `PAD,Hafter0,Hbefore0,Lbefore,PIO enable,Lafter,Hbefore2,Hafter2,RX enable,launch`。
  两组 H/L/H 各自保留保护；低字回卷时真实 decoder 保守拒绝并允许下一事件恢复，
  不用中点伪造边沿精度。固定图增加一个 descriptor，无新增静态记录或 literal。
- 主机相关七套回归 300 项通过，JUnit 为
  `out/pytest/origin-bracket-20260918-r6.xml`。真实图交给真实 decoder，覆盖逐读回卷、
  低 PAD 拒绝、末高字/RX enable/发车次序和容量矩阵。六节点矩阵 12600 种配置，
  max runs/literals/step 为 318/134/22，容量为 320/140。此前旧 STOP fixture 缺
  `tdma_priority_stop`、`tdma_rx_first_window_retire` 导致编译错误，补齐 seam 后通过；
  新顺序日志首轮误将 helper enable 算作 data enable，r5 为 293 通过/7 失败，修正
  测试识别位后 r6 全绿。没有修改生产 STOP 行为来迎合测试。
- 代码与匹配 P3 凭证已提交为 `a7fdd387`，pre-commit 核验 staged 源码指纹通过。
- Release A/B 链接、产包及 Flash link 检查通过，build 为 `20260918081154`。
  标准 Release RAM 门限仍失败：链接余量 20296 B，小于 49152 B。沿用户已授权的
  调试临时许可证继续，临时下限 16384 B、到期 2026-09-25；不是正式 RAM 门禁通过。
  静态资源检查器的八项 SYNC 文本检查失败保留；其相关源码与 HEAD 相同，未在本切片
  修改检查器放行。日志、输入 hash、许可证见
  `out/HardwareAcceptance/20260918/origin-bracket-audit/resources.json`。
- 同源码四板 quick P3：`out/HardwareAcceptance/20260918/p3-origin-bracket-r1/`，
  `PASS_WITH_WARNINGS`，INFO/WARN/ERROR/FATAL 为 25/18/0/0，约 178.8 秒。
  P0 复用已确认线序，P0–T3 基础阶段通过，TDMA 保留 WARN，DPLL 不属于该凭证范围。
  源码指纹 `3de777fde2b46ca95c90a2411dc30530500a392821ad3a80bb7dec2cd9c3ad6d`。
- 专项根 `out/HardwareAcceptance/20260918/dpll-origin-bracket-r1/`：`zero-r1` 失败，
  NO2 START 返回无 payload 的特殊结果，NO1 HANDOFF 不可用、RELEASE 未发生，原生
  发布记录为空，NO2 RUN blocks 为零；不能把它算作有效锁相采样。最终 STOP 和参数
  恢复成功。仅凭这些记录还不能断言启动失败由本次 DMA 重排造成；当轮未执行该图。
  完整 STOP 后 `zero-r2`、`zero-r3` 分别约 61.6/61.8 秒通过，两次均恢复原始零 delay，
  RUN 已退休、PIO/DMA 已停，示波器恢复 STOP/EXT/NORM。
- 两次有效原生 NO1 区间均为 120–128 ns、中位 120 ns；旧两轮中位为 252 ns。
  三从 MATCH 残差中位均为 192 ns，第二轮 NO3 少量为 196 ns；旧中位为 324 ns。
  本地 raw 约 72 ns 与远端发布约 120 ns 相加相符。原生窗口交集内每从各一条
  同源序号与 NO1 编码端点完全相等，其余事件不虚构共同源记录。三从均有真实频率/
  相位采用，末态 Domain 与控制器核对通过；不以计数替代物理单圈期限。

| 波形窗口（触发后约 1.9–2.1 秒） | NO2 相对 NO1 | NO3 相对 NO1 | NO4 相对 NO1 |
|---|---|---|---|
| zero-r2 最小/中位/最大 ns | 159/339/480 | 220/400/580 | 320/699/1040 |
| zero-r3 最小/中位/最大 ns | 100/120/140 | 160/175/180 | 220/240/241 |

- 两轮均为 20 ns 采样网格、每通道 200 边沿，最近边沿按 1 ms 周期取模配对，尚未
  证明绝对同 ordinal；不宣称亚网格精度或全时长输出。r2 原生从板频率由 0 起步，
  前几次连续 +1000 ppb，NO1 发布模型约 +3669 ppb；旧基线主模型约 +960 ppb。
  r3 主模型约 +3658 ppb，承接已调节从模型；不能把所有波形差归为采样重排增益，
  也不能把 192 ns 区间直接当成 ±96 ns 的物理误差保证。
- 离线核验 `origin-bracket-audit/comparison.json` 绑定原件 hash、会话、实际 delay、
  STOP/退休和同事件端点；`native_residual_intervals.svg` 显示有限 MATCH 区间，
  各专项 `capture/scope-analysis/review.json` 保存实际 GPIO 波形统计。内部观测用于
  区分发布宽度、同事件匹配、控制采用与 RUN 补给；RUN 启动代理时间戳不是持续 GPIO
  捕获。独立代码审核未发现阻断；最旧 014..012 三条按 C14 逐字轮转到归档 04。
- 独立硬件复核确认 P3/专项 source hash、原件 hash、同会话和零查询证据一致；RUN
  源退休数为 blocks−1，STOP 快照仍保留非零在途尾部，故不将提交边沿数写作全部
  GPIO 已执行。文档检查/回归通过，38 项治理测试通过；既有登记/其他域轮转债务
  WARN 保留。命令使用 Windows 原生 Python（本机 Git Bash 的原生 Python 调用曾
  无输出）；hook 则显式执行 `D:/Aphranda/Git/bin/bash.exe .githooks/pre-commit`。
- 下一 gate：在频率跟踪较稳定的条件下复核重复启动的输出偏差，继续区分 RUN 执行锚、
  独立输出补偿和路径 delay。内部测量已收窄，物理稳定 100 ns、单圈期限及一致 VDC
  发布仍未完成；正式 RAM 余量和静态检查器债务保留。

### VDC-PROGRESS-20260918-041：TIMER1 零值与输出 delay 对照完成，跨启动偏差尚不可重复

- TODO task ID：`VDC-TIMEBASE-001`、`VDC-OUTPUT-001`。本轮使用既有 TIMER1 固件；
  生产源码未变，未写 Flash，未调整 path delay、PI 或 OTA。原生 RUN10 与 STOP
  读回均确认候选 output delay 生效，最终四板已 STOP、delay 恢复零值。
  数字均为有限采集快照，非事实源或物理精度契约。
- 证据根 `out/HardwareAcceptance/20260918/dpll-output-delay-timer1-r1/`：
  `zero-r2`、`candidate-r2`、`zero-r3` 的采集包装器通过，耗时分别约
  71.8/61.0/61.7 秒。均使用 CH1 正边沿触发，运行期间零 SCPI 查询，STOP 后读取
  原生记录与示波器；窗口约 0.9–1.1 秒、20 ns 网格、四路各 200 个边沿，无粗丢边沿。
  候选仅在 STOP 后设置为 NO1–NO4 的 `0,-380,-440,-660 ns`，不持久化。
- 最近边沿配对中位（NO2/NO3/NO4 相对 NO1）：零值 `zero-r2` 为
  `380/440/660 ns`，候选 `candidate-r2` 为 `-520/-521/-680 ns`，恢复零值
  `zero-r3` 为 `-120/-79/0 ns`。两轮零值变化约 `-500/-519/-660 ns`，不能
  从候选与基线的差直接推定输出 delay 增益或 directed path delay。
- 失败保留：首次 `zero` 因预建输出目录被 harness 拒绝，未开串口；
  `candidate-r1` 的子采集通过但最终 invariant 失败，因为适配器在父快照前设置
  候选值，恢复零值后被误报参数变化。修正为先保存原始快照、再在 RAM 配置阶段
  应用候选，`candidate-r2` 通过原值恢复校验。旧失败不改成 PASS；其物理记录仅
  用于诊断，同候选两次采样的中位约共同变化 `-400 ns`，归因尚未完成。
- 离线复核入口 `audit_comparison.py` 和 `comparison.json` 绑定原始报告哈希、
  相同生产源码指纹、delay 生效与恢复值、RUN 退休状态、模型和原生记录。
  波形使用 `analyze_resume_scope_cursor.py`，每轮结果在 `capture/scope-analysis/`。
  适配器语法及 RUN10/origin5 parser 验证通过；该 parser 检查不等于 delay 生命周期
  的故障注入测试。窗口配对是最近边沿，不构成共同绝对 ordinal 认证。
- 接续：TIMER1 迁移实现与复采已完成，TODO 将迁移切片标为 DONE；这不提升物理
  锁相或 VDC 发布状态。`VDC-OUTPUT-001` 继续对账主从模型相位锚、采样窗口内
  DCO 采用与 RUN enable 映射，再做固定条件的 delay 响应验证。当前候选未固化，
  不因一轮中位接近零就宣称稳定 100 ns。PowerShell 执行本轮 Python 命令；
  Git Bash 的原生 Python 调用返回非零且无输出，已使用有效的原生入口。

### VDC-PROGRESS-20260918-040：TIMER1 迁移完成首轮四板验收与零延迟物理复测

- TODO task ID：`VDC-TIMEBASE-001`。DPLL/VDC/SYNC 的本地同步坐标已切换为
  TIMER1 raw tick 及其派生纳秒；TIMER0 仍保留 SDK、超时和耗时诊断语义。时钟读取
  失败时不回退到 TIMER0，并为 TDMA window gate 返回确定的失败结果。
- Host 回归：TIMER1 坐标、RUN schema 10、origin schema 5、模型反馈、匹配、跟随、
  Domain、SYNC workspace 及兼容夹具均通过；专项集合 `801 passed`。Release 构建目录
  `out/build/p3-timer1-20260918` 生成双槽镜像、升级包，app/app-B/boot flash-link
  检查均为 OK。数字为本次证据快照，非事实源。
- 四板 quick P3 使用原始拓扑测量 `out/HardwareAcceptance/20260918/p3-coordinate-r1/known-topology-source.json`
  复用，结果为 `PASS_WITH_WARNINGS`，ERROR/FATAL 为零；证据目录为
  `out/HardwareAcceptance/20260918/p3-timer1-r1c/`。首次失败是误传 readback summary，
  第二次失败是并行文件写入触发源码指纹门禁，均未作为通过证据。
- TIMER1 专项采样 `out/HardwareAcceptance/20260918/dpll-timer1-r1/measurement/`
  使用 CH1（NO1 OUT1）上升沿触发，运行期间零 SCPI 查询，STOP 后读取 RUN10/origin5。
  20 ns 示波器采样的最近边沿中位相对 NO1 为 NO2 约 420 ns、NO3 约 479 ns、NO4
  约 700 ns；对应斜率约 −0.15/−0.48/−0.20 ppm。该结果证明新时间轴和持续输出可观测，
  尚未达到 100 ns，不能宣称物理锁相；最近边沿配对也不构成绝对序号认证。
- 下一 gate：保持 TIMER1 生产切片，分离并校准路径 delay 与输出 delay，随后用同一
  源码完成两轮零 delay/候选 output delay 复采；在三从物理边沿稳定进入 100 ns 前，
  `VDC-TIMEBASE-001` 和长期 VDC 发布目标保持 IN PROGRESS。

### VDC-PROGRESS-20260918-039：跨启动误差定位与用户确认 TIMER1 统一时间轴

- TODO task ID：`VDC-TIMEBASE-001`、`VDC-FAST-003`、`VDC-OUTPUT-001` IN PROGRESS。
  用户确认采用“DPLL/VDC/SYNC 统一使用现有 4 ns TIMER1，TIMER0 保留系统计时”。
  此条更新实施路线；尚未完成迁移或新固件硬件验收，不改变既有失败与精度目标。
- 只读代码/原件审查发现 RUN 每次请求只采一个 TIMER0/TIMER1 bridge，使用保守
  `raw.hi`；整微秒读数中的余量因此成为整段输出的固定偏移。逐板分别计算 r28−r26
  的 bridge 变化后，相对 NO1 的预测变化为 +92/−820/−1012 ns；示波器近邻中位变化
  为 −40/−1020/−1220 ns。再计入 enable 读回代理，剩余为 −48/−112/−120 ns。
  数字均为证据快照，非事实源；未跨板相减本地绝对时间。enable 代理不是精确启动
  时刻，关系稳定性对三从未独立完成全程认证，故此为定位证据而非已证明精确补偿。
  可复跑分析与输入哈希：`out/HardwareAcceptance/20260918/run-raw-audit-r19/bridge_bias.py`
  及 `bridge-bias.json`。NO1 两轮原生桥交集也支持其本地关系相容。
- 曾实现 PREPARED 有界求交候选：纯算术及相关回归 54 项、客户端/SCPI 生命周期
  166 项通过；构建日志已到双槽/boot flash-link OK 并生成包，但 PowerShell 重定向
  包装返回非零，不能记为完整构建命令成功。用户选择统一 TIMER1 后，该候选未刷板、
  未执行 P3、未提交；本轮自行新增的固件/测试改动已撤出生产目录，完整 diff、两个
  新文件和日志保存在 `out/HardwareAcceptance/20260918/dpll-run-relation-r1/`。
  已验收固件基线保持，候选 schema 10 未成为已发布格式。
- SDK 2.2.0 寄存器定义允许 TIMER0 选 CLK_SYS；但 `timer_time_us_32/64()` 直接
  返回硬件计数，alarm 也直接写微秒目标。全局改 TIMER0 会影响超时、通信与 Flash
  lockout，不属于用户选择的路线。当前 TIMER1 初始化已选 CLK_SYS，频率事实源为
  `BOARD_SYS_CLOCK_HZ`；4 ns 是当前配置分辨率，不是中断、读寄存器或 GPIO 精度。
- 已确认迁移不能只改 `vdc_dpll_manager_now_ns()`：同切片须覆盖 committed DCO
  坐标、`vdc_model_feedback.inc` 的 TX/MATCH/legacy event 投影、RUN 未来 tick
  反解、SYNC 目标/当前时间、原生 origin schema/replay，以及 Core0 发布视图。
  原桥接算法仍可解释历史原件，但不能把新 TIMER1 数据套入旧 TIMER0 模型。
- 独立只读复核已完成，补充确认 Domain 逻辑证据与本地 clock base 的差值、quality
  样本年龄及 legacy `boundary_fresh()` 微秒上界也须同域处理。TDMA 发送 timeout
  保留 SDK 时间入口，SYNC model deadline 成对迁移；原生 decoder/replay 新旧坐标
  必须分开。具体落点已写入 TODO，不以单独替换 now 函数冒充完成迁移。
- 下一 gate：依 `VDC-TIMEBASE-001` 完成原子坐标迁移及相关 host，再 Release/资源、
  同源码四板 quick P3、两次零 output-delay 的较晚窗口；不调整 path-delay、PI 或
  OTA。OUT4 接线保留，真实 RUN 外部触发镜像仍未实现。

### VDC-PROGRESS-20260918-038：较晚窗口两轮恢复，三从真实采用与外部相位对照

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本轮延续已验收
  固件与零 output-delay，未改 PI、path-delay、Flash 或 OTA。以下数字为有限实测
  快照，非事实源或锁相精度契约。用户再次确认 NO1 OUT4 接 EXT TRIG；当前 RUN
  仍仅驱动 OUT1，保留接线并使用 CH1 触发，不把 EXT 接通等同于已有触发输出。
- r26/r28 包装器均通过，静默期间零查询，STOP 与 RAM 参数恢复无错误；触发后
  约 0.9–1.1 秒窗口，四路各 200 边沿，未出现超过 1 us 的周期异常。r28 三从
  MATCH 为 1966/2056/1962，实际相位采用为 59/63/64；这些是整次运行计数，
  不证明每次更新都落在示波器窗口内。两轮均有真实跟踪，不能将其解释为纯开环输出。
- 同窗口近邻配对各 199 对、排除窗口外外推：r26 三从偏移中位为
  +980/+1340/+1400 ns；r28 为 +940/+320/+180 ns，范围分别为
  920–960/300–340/160–220 ns。正值表示从板边沿较晚。NO3/NO4 跨启动偏移仍不重复，
  不写死 delay；没有探头 deskew 与绝对 ordinal 认证，不宣称四板 100 ns 锁相。
  短窗口拟合只描述该窗口变化，不直接提升为晶振频差或长期精度。
- r25 失败保留。末态显示 NO1 `VDC_RUN_OUTPUT_BINDING_CANCELLED`，新 origin trial
  与旧 handoff 代际不一致；没有 FIFO fault 或 submit failure 支持“补给饥饿”归因。
  具体 origin 准入/启动拒绝分支仍待定位；后续同参数恢复不能注销该失败，也不将
  偶发启动拒绝扩大为禁止继续闭环实验的前置条件。
- r27 请求较晚偏移时，示波器将请求的 5 s 回读为 2 s，配置断言在板卡配置/START
  前失败。后继 trace 清理超时属于该次流程未初始化的级联错误；不是新的 DPLL
  运行失败，原件保留。后续采集须使用示波器实际支持并读回一致的窗口。
- 证据：`out/HardwareAcceptance/20260918/dpll-late-r26/`、`dpll-late-r27/`、
  `dpll-late-r28/`；RAW 哈希与逐边沿审计、可复跑 `compare_late.py`、
  `late-phase-comparison.json` 和 `late-phase-comparison.svg` 位于同日期
  `run-raw-audit-r19/`。本轮只采集/离线分析与更新文档，没有新的固件/P3 通过声明。
- 下一 gate：以恢复的较晚窗口继续分离跨启动偏移与运行内漂移，对照 RUN 输出锚
  和内部模型；小幅单板 output-delay A/B 只作为 RAM 实验并恢复，不从跨启动差值
  反推 path-delay。OUT4 同 SM 镜像另立独立实现/四板 P3 切片。

### VDC-PROGRESS-20260918-037：EXT 输出路由已定位，较晚窗口暴露 NO1 持续输出缺失

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。用户确认 NO1 OUT4
  接 EXT TRIG。r24 实测 EXT/POS 门限配置正确，单次采样从 WAIT 开始，结束仍 WAIT；
  四板 RUN 检查通过、三从模型更新为正、静默期间零查询、清理无错误。证据：
  `out/HardwareAcceptance/20260918/dpll-ext-r24/measurement/route-diagnosis.json`。
- 源码明确 RUN 仅驱动 OUT1：`sync_io_run_output.c` 的 `RUN_PIN` 为输出基址，
  `sync_pulse_uniform_out1.pio` 与 `sync_pulse_stream_out1.pio` 均只配置一个 SET 引脚。
  OUT4 没有 RUN 镜像，不能仅通过选择 EXT 获得 RUN 触发。后续镜像必须由 SYNC_IO
  owner/同 SM 实现并独立验收，不能在 Core0 软件补脉冲冒充确定性同步。
- 保留原接线，r25 使用 CH1 触发并采集较晚窗口。RAW 四路完整导出、示波器错误队列
  为空；触发后约 0.9–1.1 秒窗口中 NO1 零边沿，三从各 200 边沿。NO1 只准入 19 块、
  304 边沿，三从本轮 MATCH 与 RUN model change 均为零；包装器失败原件完整保留。
  这些均为有限证据快照，不能从三从稳定周期推出参考采用或锁相。
- 证据：`out/HardwareAcceptance/20260918/dpll-late-r25/late-window-audit.json` 与同目录
  `capture/input-probe.json`。两轮均完成 STOP 和 RAM 参数恢复，未改固件、PI、delay、
  Flash 或 OTA。本轮没有新的固件/P3 通过声明。
- 下一 gate：先定位 r25 中 NO1 输出补给停止与参考运输缺失的关联，保留 r24 正向
  对照；暂不进行 output-delay 调参。EXT 镜像与绝对 ordinal 认证分别跟踪，不替代
  当前持续输出和有效参考恢复。

### VDC-PROGRESS-20260918-036：r22/r23 示波器边沿已审计，ordinal 绑定仍未闭合

- TODO task ID：`VDC-LONGTERM-002`、`VDC-OUTPUT-001` IN PROGRESS。本轮没有修改固件、
  DPLL PI、path-delay、output-delay、Flash 或 OTA；仅复核已有四板证据并生成离线审计
  `out/HardwareAcceptance/20260918/dpll-coordinate-r23/strict-ordinal-audit-r2.json`。
  以下数值均为有限采样快照，非精度契约；r1 中跨板本地时钟相减的投影已撤销并标记无效。
- r22/r23 的四板 `generation/session` 均分别一致，NO2–NO4 的 RUN ordinal 区间与 NO1
  存在交集，follow model 有效且 typed DCO 采用计数为正；四路边沿周期约为 1 ms，说明
  运输、PIO/DMA 输出和本地模型采用链路仍在运行。
- CH1 是唯一示波器触发源。RAW 波形没有携带每个边沿的 RUN ordinal、model token 或
  generation 标记，所以最近边沿和取模相位只能作探索统计，不能作为同 ordinal 锁相
  判据。只统计 CH1 有效窗口内的近邻边沿，排除窗口外参考外推。两轮偏移明显变化，
  原因尚未确定，不能归因于 output-delay 或直接反推 path-delay。
- 各板本地时间原点不同，末态模型的本地时间不能直接跨板相减；因此不能据此推出
  相差多少个 ordinal。r22 三从的 ordinal 首末跨度亦不等于已准入边沿数，不能以
  区间有交集替代逐边沿连续性证明。
- 用户确认 NO1 OUT4 接入示波器 EXT TRIG。下一动作是改用真实 EXT 正边沿触发并采集
  较晚窗口，先观察持续输出与相对斜率；绝对同序精度认证仍需硬件时间或 ordinal
  关联，但不将该认证作为基础闭环调试的新增前置。暂缓跨启动的大幅 delay A/B。

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


