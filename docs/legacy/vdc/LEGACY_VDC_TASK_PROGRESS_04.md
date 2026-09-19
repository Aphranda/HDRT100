# VDC task progress archive 04

Status: Frozen
Domain: VDC
Canonical: `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_04.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`
Last updated: 2026-09-18

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
