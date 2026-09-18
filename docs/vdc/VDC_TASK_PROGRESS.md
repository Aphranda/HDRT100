# VDC 内部主域任务进度

Status: Active
Domain: VDC
Canonical: `docs/vdc/VDC_TASK_PROGRESS.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/vdc/VDC_DOMAIN_TODO.md`, `docs/tdma/TDMA_TASK_PROGRESS.md`, `docs/state_machine/HAOFV_STATE_MACHINE_TASK_PROGRESS.md`
Last updated: 2026-09-19

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

### VDC-PROGRESS-20260919-008：探针外部对照与 VDC 发布漏更新修复

- TODO task ID：`VDC-OBS-007`、`VDC-SNAPSHOT-001`，保持 `IN PROGRESS`。本条数字
  为当日实验快照，非产品事实源。先复核六百秒内部区间：残差端点包络与同段最大
  宽度相关系数约 0.983/0.985/0.990，与最大服务间隔相关性较弱；分段极值不保证
  同事件，不能推导因果或 GPIO 误差。独立只读分析 `probe-ab-residual-analysis.json`
  建议优先检查时间戳/匹配区间宽度，未据此修改闭环增益。
- `probe-scope-ab-a1/b1/a2/` 使用相同 timing `24000,32000,16000 us`、输出 delay
  `0/-28/-88/-116 ns`，按 OFF→ON→OFF 三次独立热启动，每轮每五秒采样、六十秒
  检查点。三轮各十二组四路新鲜 RAW 窗口，每从二十四个实际上升沿；2 ns 网格、
  2.5 V 交点、相对 NO1 未独立居中，三轮全在 ±50 ns 内。ON 一轮三从范围为
  −29.84..−14.82、−31.96..−14.99、−12.00..9.93 ns，未观察到明显增大，但独立
  启动/温漂和稀疏窗口限制保留，不宣称零开销或全程精度。四板运行查询为零，
  无提前输出退休，STOP/恢复完成；ON 严格内部报告保留 NO1 首段 COUNTER_RESET。
  SHA/RAW 重解码对比和图在 `probe-scope-ab-analysis/`。
- 修复前 STOP 读回 `publication-before-fix/` 证实：NO2–NO4 FOLLOW 已有新调频与
  相位更新，Core0 DCO consumer 和 RefMem DPLL 向量仍为 DCO seq=1、0 ppb。
  原因是两消费者按 `dpll.update_seq` 去重，而本地 rate/phase 只推进 DCO 序号。
  本切片让完整快照带同次稳定偶数 guard 的 `publication_revision`；失败不确认，
  零值回绕可消费，每个 RefMem beat 仍至多一份向量。旧 wire、证据序号、clock
  模型和质量语义保留，不把更新成功当作正式 LOCKED 或失联 aging 完成。
- 组合回归 315 项通过；既有 `test_core1_overrun_quarantines_only_the_faulting_load`
  因旧文本期望失败，`application/src/app.c` 与该测试文件和 HEAD 完全一致，证据
  `vdc-publication-existing-test-failure.json`。新增真实 Domain/管理/RefMem 测试
  对旧 HEAD 的 rate/phase/quality 漏更新均可复现，新实现及布局 golden 最终
  十六项通过。独立方同意 `VDC-PUBLICATION-01 v1 pending`，原件
  `publication-independent-review.json`。
- 新源码 Release 与 `p3-vdc-publication-r2/` 完成，build `20260918204248`，
  PASS_WITH_WARNINGS、ERROR/FATAL 均零，严格质量失败保留；首次 r1 仅因复用的
  topology 非原始测量身份在硬件前拒绝，r2 使用原始已测矩阵，未重扫线序。
  `publication-after-fix-r1/` 静默十秒、运行查询零，四板模型/Core0 consumer/
  RefMem DPLL 的 DCO 序号、source、lock、phase、rate 五个公共字段一致。
  三从 DCO 序号 112/114/113、频差 2226/2538/4675 ppb 已实际发布；旧 DPLL
  证据序号仍为一，证实修复不靠篡改证据序号。对账见
  `publication-readback-comparison.json`，不把局部字段一致称完整发布验收。
- `publication-scope-r1/` 在 START 前 NO3 RUN 配置超时，恢复时原 error queue
  非空导致保留 restore-needed；最终读回四板均 STOP、参数与原值一致。原失败
  不覆盖。同参数有界复测 r2 连续运行至六十秒检查点；首个示波器采传耗时
  5.187 秒，十秒窗口遗漏，检查点正确 FAIL 并停止。其余十一个有效四路窗口
  每从二十二边沿，范围 −30.33..−13.12、−30.51..−16.04、−12.04..9.71 ns，
  均在 ±50 ns 内，未见已采窗口恶化；不能宣称完整覆盖或全程精度通过。
  NO1 首段 COUNTER_RESET 仍使内部严格判定 false，运行查询零，STOP/RELEASE/
  恢复完成。RAW 重解码及图见 `publication-scope-analysis/`。源码提交
  `dbc77b6d`；独立最终核对见 `publication-final-evidence-review.json`。
- 证据根 `out/HardwareAcceptance/20260919/`。下一 gate：内部启动/运行/停止
  分级与分钟检查点有界告警，完善无示波器自检；继续 quality/valid/freshness
  和失联恢复，完整 VDC 发布未完成，不能以本切片取消正式质量门禁。

### VDC-PROGRESS-20260919-007：内部汇总扩窗与连续证据推进

- TODO task ID：`VDC-OBS-006`、`VDC-OBS-007`，保持 `IN PROGRESS`。本条数值为
  2026-09-19 实验快照，非产品事实源。显式双参数 summary ARM 新增独立窗口 schema，
  旧单参数版本保持固定间隔；十秒段复用原有 76 个百字节槽位，未新增记录环。
  request 参数与 ACK 一致交接，STOP 冻结头保存实际跨度，服务缺口门限不放宽。
- 生产记录器及历史解码/工具组合测试 278 项通过，真实 libscpi 参数解析 106 项
  通过。覆盖六百秒模拟、计时低字回绕、旧新 schema、STOP 前/上/后边界、FULL
  不可变、RELEASE/ACK、无效参数不抢池不消费 ID、缺口和字段饱和。独立只读代码
  审核 `internal_probe_review` 未发现阻塞项；正式 WCET 和无扰 A/B 尚未证明。
- `p3-summary-window-r1/` 完成 Release、四板 OTA 与固定范围 quick P3，build
  `20260918195501`，`PASS_WITH_WARNINGS`，ERROR/FATAL 为零。沿用已确认线序，
  未扩展 P3 必验范围或修改 OTA。四板专项通过 `internal-summary-probe.py` 复用
  已验证配置和并行 START，运行期不查询板卡，全板 STOP 后才下载并释放记录。
- `internal-window-r1/` 五秒首轮通过，四板 schema/跨度/CRC 正确，三从有效匹配、
  调频和相位更新，STOP/RELEASE 与外层 RAM 参数恢复完成。
- `internal-window-r2/` 静默六十秒，四板首末成功跨度均超过六十秒；三从成功数
  12747/13012/13269，调频 17/36/13 次，相位 563/579/562 次。稳定部分内部残差
  区间分别 −111..104、−113..105、−120..105 ns。严格报告仍为 false：NO1 第一段
  COUNTER_RESET 保留；其他段无服务缺口、空输入、时钟异常、未绑定或字段饱和。
  四板 STOP/CRC/RELEASE/参数恢复均完成，未将 DCO 修正量写成实测晶振频差。
- `internal-window-r3/` 静默一百二十秒，各板保存十三段；三从首末成功跨度均约
  120.46 秒，成功数 25690/25876/26499，调频 13/28/31 次，相位 1073/1110/1118 次。
  稳定部分内部区间 −141..106、−135..106、−127..119 ns；仅 NO1 首段保留
  COUNTER_RESET。`vdc_priority_tx_core1()` 首次消费新 generation 时重置 TX 工作区，
  summary 可能先读到旧 generation 计数，是该边界现象的代码解释；不凭此抹除原件
  或将其升级为整轮严格通过。STOP、CRC、RELEASE、恢复成功，运行查询数为零。
- 扩窗代码及匹配 P3 receipt 已提交 `9d734c82`，staged 指纹核验通过；文档双检查、
  38 项文档测试、pre-commit 与逃生门审计通过。Windows PowerShell 运行原生工具，
  hook 使用 `D:/Aphranda/Git/bin/bash.exe`，避免当前 PATH 缺少 `sh` 的问题。
- A/B 两槽链接 map 对比上一包：记录池均为 7600 B、工作区均为 288 B；request
  从 32 B 变为 36 B，扩窗只增加该参数字段。原件为 `window-ram-comparison.json`。
  为满足 C14，将最旧连续两条 20260917-024/023 逐字轮转至归档 06，正文 hash
  核验一致，见 `window-progress-rotation.json`；双文档门禁复跑通过。
- `internal-window-r4/` 单次静默 600.078 秒，无重启拼接，运行查询 0；四板各
  61 段，三从首末成功跨度约 600.50 秒。三从成功数 127209/128981/132202，
  调频 31/55/27 次，相位 5426/5470/5437 次。除 NO1 首段 COUNTER_RESET 外，
  四板无 SERVICE_GAP、NO_SUCCESS、CLOCK_INVALID、UNBOUND 或饱和，严格报告
  仍为 false；三从内部覆盖/成功间隔判据通过。最大服务间隔三从约 43.89/40.65/
  45.48 ms，不能将秒级缺口门限通过写成逐周期实时验收。十秒后内部残差区间
  极值 −223..110、−216..142、−220..113 ns，较短窗有所扩大，须跟进宽度与
  控制变化并做外部同窗对照，不能将其当成 GPIO 超差或完全锁相证明。
  全板 STOP/CRC/RELEASE/外层参数恢复完成；逐分钟重叠段与图见该轮
  `capture/analysis/`。分钟检查为离线回看，本轮未提供运行中每分钟止损。
  负残差最小值出现在 NO2/NO3 第十八段、NO4 第二十三段；末尾部分段区间为
  −98..95、−99..96、−98..98 ns，不能仅凭全程极值断言持续发散。稳定段最大
  区间宽度约 300/300/312 ns；它们不保证与残差极值属于同事件，不能直接相减
  推导相位真值。定位原件为 `analysis/residual-extrema-evidence.json`。
  独立复核原件 `window-r4-independent-review.json` 确认原生/分页 CRC、保存解码、
  十个分钟窗口、零运行命令与 STOP/RELEASE/恢复一致；严格 false 与精度边界保留。
- 证据根为 `out/HardwareAcceptance/20260919/`，每轮 `capture/analysis/` 保存
  原始 flags 的离线汇总和 SVG。扩窗只证明内部跟随观测能力，不授予 GPIO 精度，
  也未证明主机指定起止点与本地捕获端点严格对齐。下一 gate：独立连续长窗、
  启停边界分级、外部同窗开销对照及 VDC 发布；不能拼接多轮为连续六百秒。

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
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_06.md` | VDC-PROGRESS-20260917-024..VDC-PROGRESS-20260917-023 | 2 | 2026-09-19 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_05.md` | VDC-PROGRESS-20260917-022..VDC-PROGRESS-20260917-022 | 1 | 2026-09-19 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_04.md` | VDC-PROGRESS-20260917-021..VDC-PROGRESS-20260917-012 | 9 | 2026-09-18 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_03.md` | VDC-PROGRESS-20260917-011..VDC-PROGRESS-20260917-011 | 1 | 2026-09-18 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_02.md` | VDC-PROGRESS-20260917-010..VDC-PROGRESS-20260917-001 | 10 | 2026-09-18 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_01.md` | VDC-PROGRESS-20260916-043..VDC-PROGRESS-20260906-002 | 146 | 2026-09-17 |
