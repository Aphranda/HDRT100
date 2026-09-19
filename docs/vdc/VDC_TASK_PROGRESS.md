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

### VDC-PROGRESS-20260919-032：Core0 异步健康镜像与特等席授权边界

- TODO task ID：`VDC-SNAPSHOT-001`、`VDC-HOLD-001`、`VDC-RECOVERY-001`；本切片 DONE，父任务 IN PROGRESS。
- 用户要求：VDC 健康诊断不得占特等席或新增 Core1 发布；增设/扩容特等席必须先用户审核。已写入 `EXE-SEAT-01` 和 TDMA/VDC 待办。草稿的 Core1 镜像调用已撤掉，未进入硬件版本；实际实现只由 Core0 RefMem task 在临界区外、OTA 保护后采样既有 guarded health。
- 代码边界：独立健康扩展使用 VDC 保留区，完整携带源 revision、身份、事件和年龄，Core0 单写并有独立 guard/checksum。旧 wire、Core1 实时函数/两向量轮转、PIO/IRQ/静态预算均未增加工作或配额。STOP 查询 `SYST:REFMEM:VDC:PRIOR?` 返回采样诊断；FRESH/age 可能滞后，不能授权控制或与 legacy model 假定同代。当前 typed 正文已完整分配，普通 class optional 不能重复算作其余量。
- 验证：相关基线 84 项、新镜像/reader/SCPI 边界 84 项、文档门禁测试 38 项通过。Release A/B/BOOT 与链接检查通过；相对上一 gap build 的 text +640 B、data/bss +0 B（构建快照，非事实源）。主控复核独立只读审查 `review.txt`；相同实时函数大小不等于绝对无共享总线影响。
- 固定四板 P3：`p3-r1` 为 PASS_WITH_WARNINGS（25 INFO / 19 WARN / 0 ERROR / 0 FATAL，快照），build `20260919140543`，源码 SHA `fdbd901e9ab339643728ea66aafb230da6d0e5d2a8cca4a25068ffcc83b19db2`，1325 文件。复用已知环序，未操作 NO5。新增告警为 NO1 RefMem deadline miss 增 1，overrun 增 0；RefMem max 17105 cycles、配置 WCET 29000 cycles，旧 DPLL 超时仍存在，不能宣称完整静态表通过。对照见 `p3-phase-comparison.json`。
- 联合实测失败保留：`joint-gap-60s-r1` 四板镜像和模型末态一致，但 NO2/NO3 trace 以 BINDING 冻结，GUARD 分别在 59742/59883 ms 记录提前冻结，后续本地 STOP；NO3/NO4 还记录 SUCCESS_GAP。60 s 窗口 CH2 无上升沿，整轮 FAIL。已有 11 个有效窗口不代替最后失败。末态 MATCH 原因是 SOURCE，trace 的 BINDING 是退休汇总代码，不直接证明身份字段变化；NO3/NO4 首 bin 首次成功等待约 1046.839/1168.597 ms，超过 GUARD 成功间隔界，区别于 22 s 的注入暂停。复测对应约 759.595/890.291 ms。具体源失效与各板自主停机先后仍待对账。
- 同配置有界复测：`joint-gap-60s-r2` PASS，三从各恢复一次；新 offer 暂停至恢复约 304.607 ms，抑制 99 次（快照）。四板 STOP 后源/镜像 18 词一致且校验通过、模型一致；12 个外部窗口、48 个 RAW 文件 hash 核验通过，NO2/NO3/NO4 最大绝对相差约 36.098/27.389/36.947 ns。两轮 RUN 查询均为 0，最终四板 STOP、配置恢复，无清理错误。
- 原件：`out/HardwareAcceptance/20260919/typed-health-core0-mirror-r1/`，含 `resource-audit.json`、`runs-audit.json`、P3、两轮联合原件及各自 `joint-summary.json`；测试日志为 `out/health-mirror-core0-base-tests.log` 和 `out/pytest/refmem-priority-health-core0-review-r2/`。
- 交付：代码与同指纹 P3 凭证提交 `f4992cbc`；文档分离提交。
- 下一 gate：先对照 GUARD 首次成功等待与稳态缺口的判定边界、源失效与自主停机链，再继续 `VDC-SNAPSHOT-001`/`VDC-HOLD-001` 正式质量发布。末态相等不证明逐事件镜像及时性，复测通过不抹去首轮失败，稀疏波形不授予连续物理锁定。

### VDC-PROGRESS-20260919-031：同会话参考停更恢复与清除计划后再 ARM

- TODO task ID：`VDC-RECOVERY-001`、`VDC-SNAPSHOT-001`、`VDC-OBS-007` IN PROGRESS。数字均为本轮快照，非事实源；实现提交 `b9a082ec`。
- `vdc_priority_tx_gap.inc` 增加 STOP-only 单代诊断计划及 `SYST:VDC:PRIOR:TX:GAP` 配置/读回。Core1 按既有 TIMER1 执行半开窗口，截止后只由新源事件恢复；暂停仍检查身份，STOP 取消，默认关闭、不写 Flash。EMPTY 保留旧 DMA 邮箱，验证新参考停更/重复，不是物理断链。
- 证据根 `out/HardwareAcceptance/20260919/typed-reference-gap-r1/`。发送/SCPI 原回归 253 项、新 GAP 回归 174 项、health 27 项、映射/原生记录/summary/GUARD 155 项通过；中途旧 host 夹具缺 `.inc` 搜索路径导致 17 个 setup error，已修复。独立复核无生产阻塞，结论保留逐事件与连续运输证据边界。
- Release A/B/BOOT 通过，build `20260919131536`；相对前轮 BSS 增 168 B、text 增 1400 B。首轮 P3 通过后修改夹具使 `joint-gap-60s-r1` 在访问板卡前因指纹不符拒绝，失败留存。最终 `p3-r2` 为 PASS_WITH_WARNINGS：25 INFO/18 WARN/0 ERROR/FATAL，源码 SHA `39c19df801e0dc913eb7abab61f34232160b290496c0f3d32047c7a689c23f48`，1323 文件；staged 门禁通过。未修改 OTA 实现、未重复线序扫描、未放宽 P3。
- `joint-gap-60s-r2` 通过。计划首次有效观测后 22000 ms 暂停 300 ms；NO1 实际 22001.041280–22302.723544 ms，抑制 140 次 offer，序号 22059→22362，整数 tick 边界复核见 `gap-audit.json`。NO2/3/4 同事件范围的汇总成功间隔 303.972/302.728/301.695 ms，各取消一次旧基线，后续事件持续推进；同 session/generation，末态各记录一次恢复，Core1/Core0 模型一致。
- 该轮十二窗/48 RAW 哈希通过，三从最大绝对相差 61.913/31.924/48.982 ns，满足采样 ±100 ns，但 NO2 不满足所有样本 ±50 ns。新鲜度累计计数无逐次转换时间，十秒桶提供对应序列缺口旁证；未据此资格化暂停期间每帧运输、连续 DCO 保持或物理边沿精度。
- `joint-no-replay-60s-r1` 通过：计划已清零，新 generation 下 GAP 全部计数为零、三从 recoveries 均零；十二窗/48 RAW 哈希通过，最大绝对相差 42.069/30.746/45.275 ns，采样均在 ±50 ns 内。该轮证明清除后再 ARM 不重放；未清除旧计划的跨代拒绝由 host 负测覆盖。两轮 RUN 查询为零，四板 STOP/参数恢复，无 cleanup error；时钟 ppm/ppb 控制值不等于相对输出频率误差。
- 文档 C14 最旧连续条目迁入 `LEGACY_VDC_TASK_PROGRESS_10.md`，正文逐字保留，审计 `out/doc-audit/vdc-gap-rotation-20260919/rotation-audit.json`；未改既有同 ID 历史记录。
- 下一 gate：RefMem 独立诊断投影与正式 quality/valid/freshness 一致发布，逐事件恢复与 DCO 保持实证、物理失联及单板复位。整体锁相/VDC 发布目标未完成；稀疏外部与内部汇总均不授予连续物理锁相或精确事件关联。

### VDC-PROGRESS-20260919-030：typed 来源新鲜度独立发布

- TODO task ID：`VDC-SNAPSHOT-001`、`VDC-RECOVERY-001` IN PROGRESS；数字为本轮快照。实现 `c3d1bb6e`：FOLLOW 最终准入后单次记录新事件，Core1 每拍维护独立 TIMER1 年龄，STOP-only `SYST:VDC:PRIOR:FOLL:HEAL?` 读取；不污染正式 quality/DCO。FRESH 不是锁相，恢复计数包含读钟恢复。
- 初轮相关 host 154+133 项通过，最终新鲜度 27 用例通过，独立复核无阻塞；缺失/重复/旧代不续期、过期保持模型、恢复先建基线及退休负测通过。Release A/B/BOOT 通过，BSS 增 148 B、代码增 1112 B、wrapper 栈帧增 32 B；不授予完整 WCET。
- 证据根 `out/HardwareAcceptance/20260919/typed-reference-health-r1/`，`p3-r1` 为 PASS_WITH_WARNINGS：25 INFO/19 WARN/0 ERROR/FATAL。build `20260919123817`，源码 SHA `c38b1639ccc2569e355432a872c60ea7fc9e2c2bb19a52d3602956a032efa4c0`，staged 门禁通过。
- `joint-60s-r1` 保留 FAIL：四板 GUARD/波形通过，但 NO4 模型读回超时、错误队列影响恢复判定；NO3 峰值 51.986 ns。停态复核 `stop-audit-1789822167451896300.json` 确认参数恢复、模型可读；前次核验脚本误用缩写的失败也保留。适配器只对 STOP 模型读取有限重试并留痕。
- 同固件 `joint-60s-r2` 通过：模型均首次读回一致，十二窗/48 RAW 哈希一致、RUN 查询为零；NO2/3/4 最大绝对相差 34.353/34.083/43.852 ns。三从 typed accepted 为 12333/12544/12308，退休原因均 STOP；超时计数包含依次 STOP 尾段，不能单独认定 RUN 断流。四板已 STOP、恢复，无 cleanup error。
- 下一 gate：健康 TDMA 下 typed 参考中断/恢复实板注入、RefMem 独立诊断投影与正式质量；连续物理精度及内外精确事件关联仍未授予。

### VDC-PROGRESS-20260919-029：已验实现按指纹收敛提交

- TODO task ID：`VDC-SNAPSHOT-001`、`VDC-OBS-007`、`VDC-FREQ-001` IN PROGRESS。实现提交 `7a1f2434` 包含启动预检、异步留证、age/ready、外参模型发布和超时恢复；staged P3 门禁核对通过，build/源码 SHA 沿用 028，未合入文档。
- 下一切片：typed FOLLOW 当前有 AGE 取消和新样本重新建基线，但通用 quality 是正式证据口径，不能用 MATCH 成功填充。独立复核建议单独发布 typed 事件新鲜度，使用 TIMER1 事件年龄和已有 FOLLOW 期限；不续期重复事件，不授予 formal，随后对账 Core0/RefMem 与参考失联恢复。

### VDC-PROGRESS-20260919-028：外参超时重试与补偿 HOLD 恢复切片

- TODO task ID：`VDC-FREQ-001`、`VDC-SNAPSHOT-001` IN PROGRESS；以下数字为本轮快照。采集 TIMEOUT 异步排空并同 lease/generation 重试，完整新窗口才清除失效；DMA/时钟/结构故障仍终止。补偿无有效窗口进入 HOLD，冻结基线，恢复仅用正常单窗口斜率；不代表正式 Domain HOLDOVER。
- 初版完整超时窗口先被数学判坏，新增回归复现 1 失败/1 通过；改为结构检查→deadline→求值后，最终后端 44 项通过，独立复核无阻塞。此前管理/配置/发布相关 157 项及真实 Domain 集成 13 场景通过。
- 证据根 `out/HardwareAcceptance/20260919/vdc-reference-recovery-r1/`；最终 `p3-r2` 为 PASS_WITH_WARNINGS，23 INFO/22 WARN/0 ERROR/FATAL，Release A/B/BOOT 通过。build `20260919120914`，源码 SHA `ad0d95657fb57dac7259e4014594d0b80f31a71fca4e2686533a8a5a76facf83`；保留修正前 `p3-r1`，不混用凭证或授予严格 WCET。
- `timeout-stop-1789820052283056800.json`：STOP 下故意配置无法按时完成的窗口，三次读回同代 TIMEOUT/invalid 且资源保持；取消释放后恢复正常配置，重新使能得到 3 个有效窗口。配置已恢复、无 Flash 写入。此项证明实板持续 TIMEOUT 状态与显式重启，未测重试次数，不证明物理断接后同会话恢复。
- `joint-60s-r1/joint-summary.json`：内部健康、外部十二窗、四板末态模型一致性通过；48 份 RAW 哈希一致，RUN 查询为零，读取 1.813–2.141 s。相对 NO1 范围：NO2 [0.201,35.910]、NO3 [-10.940,28.000]、NO4 [3.318,38.000] ns；全部已采边沿在 ±50 ns 内。四板 STOP/释放/恢复正常。
- 下一 gate：typed 参考失联、正式 quality 与物理参考同会话恢复。内部 `complete_window_proven` 仍 false，同事件精确对应及未采区间物理精度不作通过声明；实现尚未提交。

### VDC-PROGRESS-20260919-027：外参 clock/DCO 采用与管理发布一致

- TODO task ID：`VDC-SNAPSHOT-001`、`VDC-FREQ-001` IN PROGRESS；数字为本轮快照，非产品资格。非零补偿成功后按原 guard 只发布 clock/DCO；DPLL/quality/capture 仍按原 evidence 完成边界推进，PI 和调频参数未改。
- 修复前集成测试 3 失败/5 通过，直接复现模型未发布；修复后相关 75 项通过，最终九场景集成回归通过。真实 Domain 覆盖 prepare→reference→servo、servo→reference→finalize、Core0/RefMem 去重及拒绝/取消/零步进；独立只读复核无阻塞项。Release A/B/BOOT 链接通过，新增 XIP 100 B，静态 RAM 边界及函数栈帧未变；不据此授予 WCET。
- 证据根 `out/HardwareAcceptance/20260919/vdc-reference-publication-r1/`；`p3-r1` 为 PASS_WITH_WARNINGS，25 INFO/18 WARN/0 ERROR/FATAL。build `20260919114207`，源码 SHA `6689805c9f68a1133a26515e8d158b00714a16a028767b8f5168cc6b91ecba0a`。沿用已测线序，PowerShell/Python 原生入口运行，未重扫 P0。
- `joint-60s-r1` 内部健康与外部窗口通过；十二个四路窗口、48 份 RAW 哈希一致，读取 1.812–2.125 s，无漏采/超时，RUN 板端查询为零。相对 NO1 范围：NO2 [-12.000,33.851]、NO3 [-1.827,28.197]、NO4 [4.000,38.061] ns。局部 planned 输出路径预算通过，不替代完整静态表或未采区间精度。
- STOP 后四板 Core1 保留模型与 Core0 DCO 的序号、频率、锚点及 CRC 全部一致；NO1 补偿 accepted/applied 均 60，基线 4412 ppb、DCO seq 104。ppb 为控制量，非绝对频率精度。Core0 历史 invalid 计数非零，本轮只证明末态有效且一致；RefMem 的逐字段一致由 host 集成验证。四板已 STOP、释放并恢复，无 cleanup error；实现未提交。
- 下一 gate：缺参考时保持/降级、重新输入后的恢复及旧会话退休；正式 quality、内外精确事件关联和连续物理输出资格保持未完成。

### VDC-PROGRESS-20260919-026：空转质量老化与 ready 发布切片验收

- TODO task ID：`VDC-SNAPSHOT-001`、`VDC-OBS-007` IN PROGRESS；以下数字为本轮快照，非产品资格，实现尚未提交。
- Core1 wrapper 在 step 提前返回前分别维护工作 Domain 和已发布视图的 age/health/holdover age；不推进 service、证据或模型序号，不进入带 capture 副作用的完整 publisher。ready 变化同步派生 quality 和已有 runtime 发布。保留 prepare/servo/finalize/service 边界，无参考及读钟失败不伪造样本。
- 相关 host 回归 58+63 项、Domain C tests 通过；旧 manager 负对照 9 失败/9 通过，复现 age/ready 缺陷。Release A/B/BOOT 及链接契约通过。证据根 `out/HardwareAcceptance/20260919/vdc-idle-publication-r1/`；`p3-r1` 为 PASS_WITH_WARNINGS，23 INFO/24 WARN/0 ERROR/FATAL。build `20260919112440`，源码 SHA `46d18204f99544c4e00cd4090a84c8cb401a65624d74c8b48f0378b281254585`；不授予严格 WCET 资格。
- `joint-60s-r1` 内部健康及外部窗口通过，十二个四路窗口、48 份 RAW 哈希一致，RUN 板端查询为零；相对 NO1 范围：NO2 [-8.742,40.365]、NO3 [-14.223,28.061]、NO4 [-3.340,36.057] ns。固定 delay 和外参参数同 025；读取 2.000–2.266 s，后台写盘最大 7.265 s，未漏采/超时，最终 flush 完成。全程含启动，不据此宣称稳态退化或未采区间精度。
- `idle-observation-1789817581103122500.json`：四板 STOP/配置 ACK、UID/build 和错误队列核验通过；NO1 age 从 116273504 增至 117298009 µs，accepted 保持 39，health 为 DEGRADED。NO2–NO4 为 CHECKING/无正式参考样本，age=0 不作为老化实测证明；本地 typed DCO 跟踪与正式质量样本分开。全部采集正常 STOP/释放/恢复。
- 下一 gate：独立修复参考补偿采用后的 runtime DCO 可见性，再推进失联/恢复、正式质量及 RefMem 一致发布；内部 `complete_window_proven` 和内外同事件精确关联仍未闭合。

### VDC-PROGRESS-20260919-025：启动预检、异步保存与三会话联合验证

- TODO task ID：`VDC-OBS-007`、`VDC-OUTPUT-001`、`VDC-PRECISION-001`、`VDC-SNAPSHOT-001` IN PROGRESS。以下数字为实测快照，非产品资格；工具/测试改动仍在工作区，本次文档提交不包含这些实现。
- 证据根 `out/HardwareAcceptance/20260919/probe-scope-correlation-r1/`。启动预检核对 STOP/config ACK、IDN/编号/UID、build、凭证矩阵及逐 link 内容、process-image 模式和错误队列。先前 NO1 重启丢失训练 RAM，重装矩阵又恢复 raw 模式，均已定位并恢复；新预检在 ARM 前拒绝此类配置。不把此项视为单板独立复位恢复已完成：此前大相位差导致 RUN 输出 STARVED，以及一次 ARM 前 trace status 超时，仍保留原始失败。
- 示波器 RAW/报告采用主机后台有界写队列，VISA 单线程；内存分析、不可变副本、最终 flush 和写失败保持分别验证。73 项相关测试及独立只读复审通过；`p3-async-evidence-r2/` 为 PASS_WITH_WARNINGS，25 INFO/19 WARN/0 ERROR/FATAL。源码 SHA `05f2725f56e905f722019337976b9331ff587a4b2aa17bbc40d0ffa66d0e45b2`，build `20260919101812`；固定 quick P3 不授予严格 TDMA 预算或 DPLL 精度。
- 同指纹、相同输出 delay `[0,-8,-68,-116] ns`、外参补偿 `100/4/10000`，零模型初值分别执行 `run-600s-async-r1`、`restart-120s-r1`、`restart-120s-r2`。三轮独立会话的内部健康与外部窗口均通过，共 168 个新鲜四路窗口、672 份 RAW 哈希复核一致；RUN 板端查询为零，STOP/释放/参数恢复正常。这是 STOP/ARM 重复启动，不是 MCU 复位或断电重启验证。
- 全部已采边沿相对 NO1 均在 ±50 ns 内；各轮 60 秒后样本合并范围：NO2 [-13.959,9.483]、NO3 [-15.940,16.159]、NO4 [-9.917,12.125] ns。比较保留原始中心，见 `restart-comparison.json/.svg`；十分钟单次采集 1.813–2.172 s，第 475 秒后台写盘耗时 1.031 s 未导致漏采或超时。
- 内部 GUARD/目标封存通过不等于原生解码器的 `complete_window_proven`：后者仍 false；内部残差、命令 ppb、GPIO 相差及绝对频率准确度分开解释。示波器用 CHAN1 触发，OUT4→EXT 接线仍存在但当前 RUN 仅输出 OUT1。稀疏窗口不证明空档或同 ordinal/同 bin 精确对应，不能由本轮直接完成内部自校准。
- 下一 gate：`VDC-SNAPSHOT-001` 的无新参考 age 与 ready 一致发布。只读复核确认 setter 还需更新派生 quality；age 维护不能伪造 service/evidence 计数，也不能通过完整 publisher 提前公开尚未 finalize 的四拍证据。参考补偿后 runtime DCO 可见性另列核查，后续仍需坏帧/失联恢复及正式 VDC 发布验收。

### VDC-PROGRESS-20260919-024：外参补偿参数外置及两分钟斜率对比

- TODO task ID：`VDC-TUNE-002`、`VDC-FREQ-001`、`VDC-DRIFT-001` IN PROGRESS。以下测量与版本数字为本轮快照，非产品事实源。
- `SYST:VDC:REF:DISC:CONF/CONF?/ACT?/DEFA/REC/STOR` 已接线：STOP 且补偿关闭时配置斜率、滤波分母、测量准入限幅；ARM 锁存代际/CRC，Core1 确定性采用。journal v7 保存参数，v1–v6 仅 RAM 补默认，上电不自动使能。默认仍为 `PRODUCT_CONFIG_VDC_REFERENCE_DISCIPLINE_DEFAULT_*`，未改 OTA。
- Release A/B 与 Flash 链接契约通过，独立综合审查相关回归 155 项、补充输出配置及持久化回归 66 项通过。四板 `out/HardwareAcceptance/20260919/p3-reference-profile-r2/` 为 PASS_WITH_WARNINGS（25 INFO、18 WARN、0 ERROR/FATAL）；源码 SHA `d31246fd8b07f02a9e68c8680116e751420daf37ac4de01a07ac013ccd55e3f2`。TDMA diagnostic 为 true，原始 passed/closed_loop/realtime 为 false，严格调度告警保留。
- 专项根目录 `out/HardwareAcceptance/20260919/reference-discipline/`。`profile-120s-100-r2` 与 `profile-120s-50-r1` 均通过；同固件、PI、训练输入、delay 和示波器设置，四板零模型初值，每组每五秒采样共 24 窗，RUN 板端查询为零，正常 STOP/参数恢复。`profile-comparison-r1/comparison.json` 核验 comparable=true，配套 SVG/PNG 保留原始边沿，不平移曲线。
- 第二分钟 NO2/NO3/NO4 峰峰值：100 ppb/s 为 12.06/13.51/19.86 ns，50 ppb/s 为 30.01/19.10/35.94 ns。50 降低全段峰值，但末态滤波目标 4957 ppb、已采用基线 4850 ppb，仍在限速追赶；原生分段 DCO 持续推进，不能判为最终稳态或优于 100。保持默认 100，尚未证明波动来自参考源或 NO1 PI 振荡。
- `profile-120s-100-r1` 第 100 秒 RAW 导出耗时 5.407 秒，保留 FAIL。后续 bench adapter `reduced_scope.py` 保持 1M 点/2 ns 采集，只导出同次 STOP 的连续前 650000 点，检查四路共同时间轴及每路两个真实上升沿；两轮导出约两秒，无超时。不改正式工具或放宽门限，稀疏窗口不证明空档精度。
- Flash 实板 `profile-flash-r1.json` 已证明非默认保存/召回/重启保留，但恢复原 RAM 参数遇 STOP 准入拒绝，保留失败。`profile-flash-r2.json` 记录错误读回及有界 RAM 重试，Flash 写入不重试；非默认保存/重启、上电关闭、原 Flash 100/4/10000 恢复并再次重启读回全部通过，cleanup_errors 为空。四板 STOP，NO1 参考及补偿关闭；NO1 重启后训练 RAM 需在下次试验按已测矩阵重装。
- 下一 gate：核对 HDO4404 参考源规格及测量误差，继续参考失联保持/恢复与长窗验收。外参补偿后的十分钟仍未通过，不以本轮短窗替代。

### VDC-PROGRESS-20260919-023：外参有无对照，区分启动暂态与稳态波动

- TODO task ID：`VDC-FREQ-001`、`VDC-DRIFT-001` IN PROGRESS。以下数字均为实验快照，非产品事实源。
- 证据根目录：`out/HardwareAcceptance/20260919/reference-discipline/`。早期 `ab-60s-a1/b1` 均通过，但 A 仅关闭补偿更新、保留此前基线，不能解释为相同零初值的有无外参对照。
- `ab-120s-a1/b1` 两组各两分钟通过，均在 STOP 下使用现有 provisional 激活重建四板模型并确认积分/频率为零，复用已测线序。相同 PI、输出 delay、示波器配置，每五秒一次同触发四路 RAW，RUN 板端查询为零；两组均正常 STOP 和恢复。
- `ab-120s-comparison-r2/comparison.json` 与 `phase-ab.svg`：第二分钟 NO2/NO3/NO4 峰峰值，无外参为 12.55/16.09/14.05 ns，有外参为 10.26/9.80/18.00 ns。有外参全段最大绝对偏差约 42.79 ns，第二分钟全部已采边沿在约 ±12 ns；支持调频捕获暂态假设，不足以证明振荡原因或未采空档精度。重建导致路径 CRC 更新，完整训练输入与实际 delay 已分别比对。
- 外参补偿后的十分钟尚未通过：`joint-600s-r1` 在 240 秒检查点因导出超时停止，`r4` 在 360 秒因第 315/320 秒导出超时停止；波形门限未超。`r2` 为 NO3 START 应答超时，`r3` 因未释放 trace 拒绝启动；原始 trace 已导出后释放，见 `no3-trace-recovery/`，失败原件保留。
- 下一 gate：通过外置参数接口对比调频斜率，保持 PI 不变；之后再验证参考失联保持/恢复。外参 IIR 与 MASTER PI 残差分离，不能仅凭 NO4 偏差较大认定 NO1 PI 抵消外参。

### VDC-PROGRESS-20260919-022：外部参考慢速补偿实际采用，四板短窗通过

- TODO task ID：`VDC-FREQ-001` IN PROGRESS。已推送前一切片代码 `0f42a6a0` 和文档 `30e073e0`；本条为后续补偿切片，未推送。
- `DISCipline` 独立、易失、STOP 启用；Core1 消费硬件窗口，绑定主板 origin/session/配置/时钟身份，滤波限速后提交绝对基线。Domain 合成基线与 PI 残差，保持同刻时间连续；取消冻结已应用基线。SCPI 顺序及字段见 TODO。
- host 初轮 196 项通过；修正主板身份后相关 113 项通过；文档回归 38 项通过。Release A/B 与链接契约通过；四板 `p3-reference-discipline-r2` 为 PASS_WITH_WARNINGS，25 INFO/18 WARN/0 ERROR/FATAL（本轮快照）。TDMA diagnostic 通过，原始 passed/closed_loop/realtime 均 false，未宣称严格调度通过。
- 失败原件：`out/HardwareAcceptance/20260919/reference-discipline/joint-60s-r1/`。错误使用 follower RX observer 准入导致 NO1 零采用；修正为现有 priority TX 的自主 origin 记录来源，重新构建/P3 后复测，未放宽相位门限。
- 通过原件：同目录 `joint-60s-r2/capture/input-probe.json`、四板原生记录和 `main-review.json`。参考 67 窗，采用 60 次，末态测量 +4496、滤波/基线 +4474 ppb；NO1 原生分段 DCO 范围由 +1391 推进至 +5847 ppb。四板 GUARD、十二个新鲜示波器窗口全通过，RUN 查询为零。
- 实测快照：NO2/NO3/NO4 相对 NO1 分别为 [7.30,43.97]/[-4.20,45.99]/[1.60,65.91] ns。采样空档、DMA 仲裁误差和外部仪器基准精度未定界，不宣称连续物理精度或绝对 ppb 资格。
- 四板 STOP，参考/补偿关闭，配置恢复且无 cleanup error，未改 Flash。下一 gate：失参考保持与恢复、同启动补偿前后频率对照及较长联合窗口；滤波/步进仍为编译策略。

### VDC-PROGRESS-20260919-021：外部参考 MONITOR 可配置，四板共存通过

- TODO task ID：`VDC-REFERENCE-002` DONE；`VDC-FREQ-001` 的基准补偿未实施。
- 实现：`SYST:VDC:REF:CONF/ENAB/STAT?` 和 `DEFA/REC/STOR`；STOP 配置端口、标称 Hz、边沿、窗口和超时。产品配置 v6 只保存参数，上电不自启；兼容 v1–v5 CRC/字段。SYNC_IO 独占参考 SM，DMA9/10 读取 TIMER1；Core1 必经 TDMA 相位推进一次有界状态，Core0 只在退休 ACK 后释放。RS485、RUN 输出及 TDMA 原 owner 保留。
- 软件验证（本轮快照）：相关解析/配置/Flash/timing/静态调度 130 项、SYNC 后端及既有资源回归 108 项、路由/观测回归 23 项、文档回归 38 项通过。Release A/B 构建及链接契约通过；P3 `p3-reference-monitor-r4` 为 PASS_WITH_WARNINGS（25 INFO / 18 WARN / 0 ERROR / 0 FATAL），未要求 NO5。TDMA `diagnostic_passed=true`，`passed/realtime_gate_passed/closed_loop_passed=false`；严格调度告警保留，与此前 `p3-vdc-publication-r2` / `p3-internal-seal-r1` 判级一致，不能宣称严格 TDMA 门禁通过。
- 专项证据：`out/HardwareAcceptance/20260919/reference-monitor/standalone-r2/report.json`。IN4=GPIO20，上升沿序号 3→5、下降沿序号 2；停用释放、IN3 无信号超时、非默认窗口 Flash 保存/召回通过。读数约 +3680～+3692 ppb 为“输入相对本地标称时钟”，不代表独立绝对精度。
- 同轮共存：`reference-monitor/joint-60s-r1/capture/input-probe.json`，参考完成 67 个窗口，末态 +4380 ppb；四板 GUARD 全通过，RUN 查询为零，示波器每五秒一窗共十二窗通过。相对 NO1 边沿范围：NO2 −10.07～+25.88 ns、NO3 −11.58～+4.10 ns、NO4 −7.94～+59.59 ns（实测快照）；稀疏窗口不覆盖空档，不宣称完全连续物理精度。
- 失败与修复：初版 DMA0/1 与 RS485 冲突，改为9/10；独立审查指出 sticky DMA error，取得通道后停态 W1C 清理并回归。`standalone-r1` 中 PREPARED/取消不推进，是服务误挂可隔离 SYNC 相位，改至必经相位后通过。失败原件保留，不能用早期 P3 替代 r4。
- 重启验证：`reference-monitor/boot-check-r3.json` 证明非默认参数恢复且 enabled/resource/state 均 idle。前两次测试在重枚举后立即恢复 RAM 配置遇执行拒绝，原件 `boot-check.json`、`boot-check-r2.json` 保留；r3 留存拒绝并有界重试 RAM 配置后恢复默认 Flash。USB 可查询不等于 STOP 写入准入已稳定，Flash 写入未重试。
- 收尾：四板 STOP；NO1 参考关闭，参数恢复 IN4/10 MHz/上升沿/1000 ms/2500 ms。DMA 仲裁误差仍标记未定界，未改变任何 DCO；下一切片为参考频偏稳定性/误差预算，再决定慢速基准补偿。

### VDC-PROGRESS-20260919-020：显式 delay 与批量示波器留证，联合长窗通过

- TODO task ID：`VDC-OBS-007`、`VDC-DRIFT-001`、`VDC-OUTPUT-001` IN PROGRESS。
  工具切片 `cd8fd17a` 增加 `--output-delays` 四个 int32 参数，经既有 ResumeTrial
  STOP/快照/应用/读回/恢复路径处理；默认保留旧基线，本次显式选择
  [0,-8,-68,-116] ns。示波器采样内的逐查询 JSON 重写合并为正常/异常退出时
  保存；RAW 块仍即时落盘。保留新 WAIT→STOP、四路同窗 RAW、哈希、分钟门禁，
  不放宽采集超时或相位判据。进程强杀可能丢失当前窗尚未保存的 JSON，不能用
  不完整原件签发通过。本条全部测量数字为当日快照，非事实源。
- 联合与采集 helper 测试 118 项通过；独立只读 reviewer
  `/root/joint_capture_review` 另跑 48 项通过、无阻断项，未连接硬件。
  复核及测试原件见 `out/HardwareAcceptance/20260919/p3-joint-batch-r1/`。
  同源码构建及固定四板 P3 PASS_WITH_WARNINGS，25 INFO / 18 WARN / 0 ERROR/FATAL；
  source SHA `a41895ce7ada7757b2fe308c9f508860d771cfbefe6b36f5aca7a8a2bd000fe8`，
  package SHA `15ba684592fee95a3cf01ea96e889a9b9a2c04b383cfbb7d6963a7357a7ec969`。
  固件未改；本次不以锁相质量扩充基础 P3。
- 证据根 `out/HardwareAcceptance/20260919/dpll-delay-center-r2/`。
  `plus20-batched-60s/` 十二窗通过；NO2/NO3/NO4 范围分别
  [-9.924,16.531]/[-16.014,5.986]/[-4.491,60.118] ns。NO4 的最大值在首个
  第五秒窗口，之后收敛，不能将该轮写成全窗 ±50 ns。
  `plus20-batched-600s/` 连续运行十分钟，120 个新鲜外部窗口、十个分钟门禁及
  四板内部 GUARD 全部通过；三从范围分别 [-28.127,-3.705]/[-29.588,-4.961]/
  [-21.783,2.188] ns，全部在 ±50 ns 内。该轮跨启动中心仍有变化，不追加补偿。
- `comparison.json` 保留旧轮 FAIL 与新轮 PASS：旧十分钟最长采集 7.328 s，
  第 550/555 秒超时；新十分钟最长 3.531 s，无漏采/超时，采集耗时中位 3.359 s。
  这证明本轮调度通过，不宣称主机偶发停顿已永久消除。主控从原始 SRAM/RAW
  重新解码并复算，见两份 `*-main-review.json`；长窗图为
  `plus20-batched-600s-comparison.svg`。两轮运行板端查询均为零，原生封存、
  计时资格、STOP/RELEASE、参数恢复全部通过，无 cleanup error，未写 Flash。
- 下一 gate：沿用显式新 delay 做跨启动与失联恢复；推进 VDC 无新 evidence 时
  的质量老化和 idle ready 发布缺口。稀疏 GPIO 窗口通过不等于未采样区间精度、
  精确事件关联、全程物理锁定或 VDC 一致发布完成。内置 RRDELay 尚不替代 RAW。

### VDC-PROGRESS-20260919-019：NO2/NO3 输出 delay 各增加 20 ns

- TODO task ID：`VDC-OUTPUT-001`、`VDC-PRECISION-001` IN PROGRESS。用户观察到
  CH2/CH3 稳定提前，授权在本轮 delay 基础上各加 20 ns。试验配置由
  [0,-28,-88,-116] 改为 [0,-8,-68,-116] ns，沿用 24000/32000/16000 µs 时间轴，
  参数为当日快照；只经既有 STOP-only SCPI 应用/读回，不改固件及 Flash。
  证据根 `out/HardwareAcceptance/20260919/dpll-delay-center-r1/`，固定方案见
  `plan.json`，入口 `capture.py` 默认使用新候选，`--baseline` 显式选择旧配置。
- `plus20-60s/` 与 `plus20-repeat-60s/` 两次独立启动均联合通过；每次十二个
  四路 RAW 窗口、四板 GUARD、计时完整性、CRC、目标封存、STOP/RELEASE 与
  原配置恢复完整，板端运行查询零。主控从原始字节重放，见两份 `*-main-review.json`。
  第一轮 NO2/NO3 中位 -7.789/-6.955 ns，范围 [-13.887,0.016]/[-14.014,5.268] ns；
  第二轮中位 +3.010/-0.474 ns，范围 [-2.064,7.946]/[-6.092,7.551] ns。
  NO4 参数保持，其两轮范围分别 [-12.231,11.203]/[-1.972,11.959] ns。
  对照旧十分钟 NO2/NO3 中位 -21.977/-25.303 ns，支持中心改善，但两次启动
  本身存在约十纳秒变化，不能宣称精确平移或冷启动重复性完成。
- 同一末态波形中，内置 RRDELay 在 CH2/CH4 返回 `9.900E+37` 无效值，CH3 为
  -4 ns，RAW 仍可正常提取边沿。`builtin-repeat.json`、`builtin-recovery.json`
  保留读数，最终示波器 STOP、error queue 零；中间重设测量后的 query timeout
  保留为工具失败，不等于 DPLL 故障。无效读数不能作为零误差或外部通过。
- 下一 gate：后续试验优先采用新候选 delay，继续改进内置延迟巡检与 RAW 回查的
  有界采样编排，再跑严格长窗。原通用联合入口仍使用旧基线；本次候选请用本证据
  入口，待下一工具切片显式参数化，避免误用。两次短窗不替代全程物理锁定、
  失联恢复或 VDC 一致发布；板卡已 STOP 并恢复本次试验前配置，未固化候选。

### VDC-PROGRESS-20260919-018：同轮十分钟外部窗口与内部健康复核

- TODO task ID：`VDC-OBS-007`、`VDC-DRIFT-001` IN PROGRESS。证据根
  `out/HardwareAcceptance/20260919/internal-joint-r1/`，下列数字为当日快照，非事实源。
  沿用已提交联合工具及最终 P3 指纹，未改固件/工具。`scope-on-chan1-600s/`
  连续运行，四板内部十个分钟检查点全部通过，原生目标封存及规划计时资格通过；
  最终输出 CANCELLED，板端运行查询零，STOP/RELEASE 与配置恢复完整。
- 外部 120 个四路冻结窗口全部读回有效，NO2/NO3/NO4 相对 NO1 的范围分别约
  [-32.000,-13.942]/[-36.014,-14.159]/[-7.478,13.402] ns，均在 ±50 ns 内。
  但第 550 秒采集耗时 7.328 秒，影响第 555 秒节拍；第 600 秒检查按既定规则
  因两次 acquisition_overrun 失败，整轮仍为 FAIL，不能称严格十分钟验收通过。
  细查最长约 3.781 秒间隔位于触发阈值查询结束至 SING 发送之前，落在主机
  报告保存/调度区间；该窗触发准入至完成观测约 328 ms，不能归因为 GPIO 抖动。
  `scope-on-chan1-600s-main-review.json` 与 `scope-on-chan1-600s-comparison.svg`
  保存主控逐份 SRAM/RAW 重放和对照；采集失败不覆盖健康及物理窗口事实。
- 运行期间重新验证发布缺口，证据
  `../vdc-idle-maintenance-r2/current-head-r1/results.json`：真实 Domain、Core1
  dispatcher、runtime/committed publisher 和 RefMem 路径仍复现六个异常场景，
  四个对照正常。无参考时预期 age 200000 µs，Domain/runtime/RefMem 仍为零；
  ready=false 可令 committed DCO 为 OFF，而 runtime/Core0 仍保留旧 LOCKED。
  旧复现脚本缺少新 GUARD 的禁用态 stub 导致的编译失败保留于 r1；r2 仅补该
  非激活诊断 hook，未替换受测 Domain/发布函数，不把 stub 当实板时序证明。
- 下一 gate：先按用户意见验证 NO2/NO3 输出 delay 各加 20 ns；随后以新鲜触发的
  示波器内置延迟巡检、定期 RAW 复核并减少逐查询写盘，重做严格长窗。
  VDC 发布后继只修复有界质量老化/ready 发布，不在该切片发明 HOLDOVER、
  改写 clock/DCO 含义或在单次丢样本时取消输出。持续物理锁定及正式发布仍未完成。
- 按 C14 将最旧四条逐字迁至 `LEGACY_VDC_TASK_PROGRESS_08.md`，索引闭包与原文
  哈希验证通过，见 `progress-rotation-08.json`；未删除历史证据。

### VDC-PROGRESS-20260919-017：内部探针可选示波器同轮复核

- TODO task ID：`VDC-OBS-007` IN PROGRESS。证据根
  `out/HardwareAcceptance/20260919/internal-joint-r1/`，数字为当日快照，非产品事实源。
  新入口 `tools/vdc_priority_trace/vdc_priority_joint_capture.py` 显式选择已验证
  bench adapter，`--scope off/on` 切换纯内部或同轮外部观测。保留全部 GUARD、
  CRC/身份、STOP/RELEASE/恢复；板端运行查询零。外部每五秒一份新冻结 RAW、分钟
  检查失败后收尾，不复用旧波形，内部与外部判据独立、联合通过要求两者满足。
  工具/测试切片已提交 `dcdc7e72`，实际 pre-commit 匹配最终 P3 指纹通过。
- 独立作者 40 项模拟测试通过；加入可选触发源后主控联合相邻回归共 111 项通过。
  `test-independent-review.json` 保留初版测试范围/哈希，最终测试见 `trigger-tests.txt`。
  示波器 STOP 负验证观察到新 WAIT，无输出时拒绝旧记录。初版 EXT 实测
  `scope-on-60s/` 12 次全部触发超时，内部四板通过、联合 FAIL，恢复完整。源码
  `sync_io_run_output.c:RUN_PIN` 只驱动 OUT1，OUT4 接线存在但没有 RUN 脉冲；故
  `--scope-trigger CHAN1` 为当前默认，EXT 仍可显式选择，不新增固件 GPIO 所有权。
- 最终固定 `p3-internal-joint-r2/` PASS_WITH_WARNINGS：23 INFO、23 WARN、
  0 ERROR/FATAL，源码 `ef0be959889f992c86d42683485a89841ea32314c75060bf57bc48574934b1a4`；
  包 SHA 仍为 `15ba684592fee95a3cf01ea96e889a9b9a2c04b383cfbb7d6963a7357a7ec969`。
- `scope-on-chan1-60s/` 联合通过：内部四板目标封存、外部 12 个四路窗口有效，
  两沿/窗，实际 NO2/NO3/NO4 相对 NO1 分别约 [-35.811,-13.924]、[-34.132,-15.885]、
  [-8.027,47.959] ns；四板规划计时无缺口/超限，最大约 200.208..205.576 µs。
  主控从 SRAM/示波器 RAW 重放，CRC、触发源、新 WAIT、停止和恢复匹配，见
  `scope-on-chan1-60s-main-review.json`；图为 `scope-on-chan1-60s-comparison.svg`。
- `scope-off-60s/` 同版本关闭分支通过：外部 SKIPPED、窗口零，内部四板目标封存、
  零运行查询、STOP/RELEASE/恢复完整，见 `scope-off-60s-main-review.json`。
- 用户提示后验证 HDO 内置 `:MEAS:ITEM? RRDELay,CHAN1,CHAN2/3/4`，并添加三组显示。
  同一末次冻结波形读得 -28/-26/-8 ns，与 RAW 边沿相差约 2 ns 内；单项查询
  约 2.5..3.2 ms，见 `scope-built-in-delays.json`。旧 `RDEL` 名称被 -222 拒绝的
  原件保留。当前联合脚本仍下载 RAW，后续可用内置读数快速巡检并定期 RAW 复核，
  须保持新触发完成和无效读数判定，不能把冻结旧读数当持续测量。
- 下一 gate：两种配置已可用，继续重复长窗与 VDC 质量发布。
  同轮并不证明精确同事件/同汇总段配对，稀疏外部窗口不证明未采样区间；内部残差
  与物理边沿是不同量，不以共同通过宣称绝对零扰动或全程锁定。EXT 失败原件保留。

### VDC-PROGRESS-20260919-016：目标自动封存与无查询联合探针复测

- TODO task ID：`VDC-OBS-007` IN PROGRESS。证据根
  `out/HardwareAcceptance/20260919/internal-seal-r1/`；下列数字为当日快照，非产品
  事实源。显式 GUARD 目标完成时先封存末段，保持 RUNNING 直到最后时钟、计数器和
  覆盖检查结束；异常仍否决 PASS。成功使用独立 TARGET_COMPLETE 原因冻结记录，
  不停止输出或环路；主机最终统一 STOP。普通 SUMMary、原生布局与记录池保持。
  GUARD 独立版本升级，解码工具只承认覆盖，不单独授予 GUARD 或物理锁相通过。
  代码已提交 `b539118b`，实际 pre-commit 匹配本次 P3 指纹通过。
- 主控 235 项回归、Release A/B/boot 链接通过；独立测试作者最终 119 项专项通过，
  两个隔离变异负控均检出，见 `independent-tests.json`。适配器 227 个策略检查通过。
  独立 `source-review.json` 复核源码、目标汇编与适配器，同意 `VDC-PRIORITY-01`
  v25 保持 pending；明确封存后不再记录参考，输出尾段另审，不宣称零扰动。
  `ram-check.json` 确认 A/B 静态 RAM 间隙仍为 20052 B，未增加记录池。
- 固定 `p3-internal-seal-r1/` PASS_WITH_WARNINGS：25 INFO、18 WARN、0 ERROR/FATAL，
  沿用确认拓扑，源码 `e91aafe851022f9a61c923dc37e0e7e5340bba282212ee12317962c3c0d24380`，
  包 SHA `15ba684592fee95a3cf01ea96e889a9b9a2c04b383cfbb7d6963a7357a7ec969`。
- `positive-60s/` 整轮健康与独立规划计时资格通过，主控从 RAM 原始字节重放，
  见 `positive-60s-main-review.json`。四板覆盖 60.001..60.004 秒、全部目标完成
  封存；计时缺口和预算超限均零，规划路径最大约 196.828/198.380/202.496/203.136 µs。
  输出最终 CANCELLED，CRC、STOP/RELEASE、恢复及零运行查询成立。三从稳定汇总段
  内部残差分别为 [-116,102]/[-123,106]/[-122,104] ns，属于估计区间，非 GPIO 精度。
  独立 `p3-short-review.json` 核对源码与全部凭证引用、原生页 CRC、保存的解码、
  判据、恢复和输出退休，结论一致；离线图为 `positive-60s-internal-follow.svg`。
- `positive-600s/` 连续十分钟整轮健康与独立规划计时资格通过；四板覆盖
  600.001..600.004 秒、全部十个分钟通过位完整，原生各 60 段，运行查询零。
  四板计时缺口/超限均零，规划最大约 228.364/221.724/217.800/207.848 µs，
  输出最终 CANCELLED，无本轮 STARVED，CRC、STOP/RELEASE 和配置恢复完整。
  三从稳定内部残差为 [-231,110]/[-194,112]/[-219,113] ns，长窗区间扩大，不能
  用本次健康通过宣称物理 ±100 ns。主控重放与图分别为
  `positive-600s-main-review.json`、`positive-600s-internal-follow.svg`。
- 下一 gate：按用户补充增加可配置外部示波器联合复核；关闭时保持内部静默自检，
  开启时在同一次运行中稀疏外部采样，分别报告内部健康、物理观测及窗口对应关系。
  单次长窗不宣称偶发故障消失；旧 OUTPUT_READ、STARVED、BINDING、非法运行态
  trace STOP 和超限失败原件均保留。后续仍需重复长稳、失联恢复与 VDC 质量发布。

### VDC-PROGRESS-20260919-015：活动输出释放预检与探针收尾顺序

- TODO task ID：`VDC-OBS-007` IN PROGRESS。证据根
  `out/HardwareAcceptance/20260919/output-observer-gate-r1/`；数字为当日快照，
  非产品事实源。Core0 周期 release 原先在检查后端状态前占用客户端门控，
  活动输出也产生竞争。现先只读预检已发布的 RETIRED/代际，符合才单次弱 CAS；
  锁内再次核对原请求及新后端快照，失败保留所有权请求，后续 service 独立重试。
  未改变 GUARD 读取失败、模型失效、STOP/释放或补给预算判据。
  代码切片已提交 `d66d0f81`，实际 pre-commit 匹配本次 P3 源码凭证通过。
- 主控 243 项测试通过，Release 双 slot 与 boot 链接检查通过。独立作者新增
  18 个真实生产入口场景，直接观察活动状态零次 CAS，四个隔离变异负控全部检出，
  见 `../output-probe-gate-r1/independent-tests.json`。独审 `source-review.json`
  确认目标单次弱 CAS 与锁内重验，未发现所有权阻断；不能由此宣称零干扰或
  所有 OUTPUT_READ 原因已排除。
- 固定 `p3-output-observer-gate-r1/` PASS_WITH_WARNINGS，25 INFO、19 WARN、
  0 ERROR/FATAL；沿用已确认拓扑。源码
  `b3067dff8f64011a5304dfaf41d915ebace57b0f1954a5a602ef5efd3ae52828`，
  包 SHA `87678d449b6e2859b2e233efd313f0f2c77c78473cc43ad164841e5c7634c0cd`。
- 首次 `positive-60s/` 四板分钟 GUARD 都通过，计时样本差均为零，已记录最大
  墙钟约 198.380/201.428/268.636/205.752 µs，预算超限零；但 NO2–NO4 最终以
  BINDING 冻结，整轮 FAIL。原编排在运行结束后先逐板停环路、再冻结 trace，
  因此需隔离收尾动作对终态的影响；不能直接豁免该原生终态或改写失败。
- `positive-60s-r2/` 曾尝试窗口结束先发 trace STOP 再停环路，八个纯内存顺序
  检查通过，但未覆盖固件准入边界。实板 NO1 返回 timeout，随后 error queue 为
  execution error；源码确认 trace STOP 与 ARM/RELEASE 同样走 STOP-only metadata
  gate。因此该编排不可用，不再重试。整轮 FAIL、父 restore 告警及原始脚本保留。
  最终四板 initial/final 配置完全一致、环路停稳，NO1 最终 error queue 已为空；
  这些恢复证据不能改写原告警为通过。
- 下一 gate：独立切片在 Core1 目标检查点通过时自动封存本次记录，用明确完成
  原因区别主机 STOP 与故障；普通 SUMMary 保持原行为，输出继续到统一 STOP。
  不开放运行态 trace 控制，不将 BINDING 改写为正常。再验证短窗和十分钟，要求
  完整计时样本、四板检查点及 CRC/STOP/RELEASE/恢复一致，随后重复长稳与物理精度。

### VDC-PROGRESS-20260919-014：原 TDMA 相位内独立规划补给与内部长检

- TODO task ID：`VDC-OBS-007` IN PROGRESS。证据根
  `out/HardwareAcceptance/20260919/output-replan-r1/`；本节数字均为当日快照，
  非产品事实源。针对上一切片复现的缓存失效后无规划机会，在完整 TDMA 不能
  准入时，优先以独立候选预算执行原输出规划主体，余量更小时才执行缓存交接。
  两入口互斥、只占原相位；保留模型失效、已准入前缀、STOP/身份取消与 DMA 退休。
- `VDC_RUN_OUTPUT_SCHEMA` 追加六个 `planned_*` 字段，分别观测调用、提交、
  推进规划的服务和 caller 墙钟。弱 CAS 只试一次，失败不阻塞。完整 TDMA 的
  skip/start-miss 与 phase_run_count 语义保持；实际输出服务按自己的预算留账。
  候选预算没有变成 WCET 证明，任意迟到下的补给机会也未保证。
  代码切片提交 `f14c58ab`，实际 pre-commit 硬件凭证核对通过；契约 v24
  独审接受 pending 范围，见 `c11-v24-review.json`，不授予长稳或产品发布。
- 主控回归 `tests-final.txt` 为 225 项通过；独立测试作者增加 dispatcher 边界、
  缓存失效负控与重建恢复、DMA busy、取消及统计用例，四个隔离变异均检出，见
  `../output-refill-r1/independent-tests.json`。Release 与双 slot/boot 链接检查通过。
  独审 `source-review.json` 未发现所有权/相位选择阻断，目标反汇编确认两新增
  入口单次弱 CAS。静态 RAM 增用 24 B、主区余 20052 B；wrapper 与共享规划体
  栈帧合计 888 B，不含更深调用，不是整条 Core1 峰值栈。
- 固定 `p3-output-replan-r1/` PASS_WITH_WARNINGS，25 INFO、19 WARN、0 ERROR/FATAL；
  复用已确认拓扑，未重扫。最终源码
  `092143a701b2341668a1918a076c6545d2b4a3e14ad7ff1e14f39e62f3e3c567`，
  包 SHA `a8219efad3189870277053a89b26afd0295be82fae24335fa60ad2f620ce0858`。
  build ID 沿用旧值，必须以指纹区分；P3 基础门禁未扩展为锁相门禁。
- `positive-60s/` 四板联合参考/输出检查通过，最终均 CANCELLED，未见 STARVED；
  运行查询为零，CRC/STOP/RELEASE/配置恢复完成。独立规划调用
  6447/3547/3746/3437 次、提交 387/271/269/246 次、推进规划
  466/456/460/396 次，已记录最大墙钟约 199.324/210.112/231.928/205.520 µs，
  未记录预算超限。NO1/NO3/NO4 调用与计时样本差为 1/3/1，NO2 为零，
  因此 `strict_replan_qualification` 为 FAIL，不能以健康 PASS 覆盖计时缺口。
- `positive-600s/` 整轮 FAIL 保留：四板均通过至 420 秒；NO3 在自身 480 秒检查
  返回 `OUTPUT_READ`，NO2 在自身约 479.894 秒以 BINDING 提前冻结并触发
  `EARLY_FREEZE`，NO1/NO4 在 540 秒检出缺参考。各板起点不同，以上本地经过
  时间不能直接排列共同时间线或证明唯一因果。最终输出均 CANCELLED，未见
  STARVED；四板自主停止接受/退休、原生 CRC、STOP/RELEASE/恢复完整，运行查询零。
  已记录规划最大墙钟约 209.308/209.064/212.212/210.140 µs、预算超限零，但样本
  缺口 9/15/16/9，严格计时亦 FAIL。不能将部分正常窗口写成十分钟长稳通过。
- 下一 gate：先核对 OUTPUT_READ 与客户端门控竞争、NO2 绑定冻结的发生路径，
  修复观察可用性及计时发布；失败不降级为 PASS，也不以旧快照填充当前观察。
  `next-measurement-plan.json` 保留门控与生命周期的待证假设。随后重复长稳并与
  外部稀疏波形交叉核验；内部残差/引擎状态不授予实际 GPIO ±100 ns 或产品发布。

### VDC-PROGRESS-20260919-013：参考与输出引擎联合探针及关闭监督对照

- TODO task ID：`VDC-OBS-007` IN PROGRESS。证据根
  `out/HardwareAcceptance/20260919/output-starvation-r1/`。先闭合长检假通过缺口，
  输出补给修复保持后继独立切片，固定 quick P3 范围不扩大。代码已提交
  `b6aedfd8`，真实 pre-commit 核对最终源码与硬件凭证通过。
- 同一进度 012 源码关闭分钟 GUARD、保留普通 SUMMary 的十分钟对照：首轮
  `guard-off-600s/` 在 NO1 START 未取得显式 OK 后停止，未进入采样；原件保留。
  父清理曾报告 NO1 error queue 非空，最终配置读回与原值一致，不能将该轮写成通过。
  复测 `guard-off-600s-r2/` 完整采样、运行查询零、原生 CRC 复解一致、清理恢复
  全部完成。参考通过，但 NO4 在约 468.828 秒 STARVED，NO1–NO3 到主机 STOP。
  因此分钟监督不是饥饿的必要条件；不能由该对照量化探针开销或唯一确定根因。
  数字为当日快照，详细末态见 `guard-off-600s-r2-analysis.json`。
- 独审 `vdc-refill-review.json` 用生产 client/planner 复现：模型改变使缓存失效，
  连续 cached-only 服务不能重建，在 FIFO 尾部耗尽前若没有完整规划机会即断流。
  模型不变和恢复完整服务两组对照可继续补给。关闭 GUARD 的 NO4 末次失效距尾部
  约 8 ms，之后仍有三次服务，与该路径相符，但不称为完整硬件因果证明。
  既有快速预算已出现少量超限，不能把完整规划直接塞进同一个短预算或重放旧模型。
- 新版 GUARD 元数据沿用同一显式 ARM 命令，普通 SUMMary 不受影响。首次运行
  绑定当时输出 request，读取/身份失败锁存；检查点通过一次客户端弱 CAS 和后端
  已发布快照，核对身份、RUNNING/OK、服务/提交新鲜度与末序号推进。观察器不调用
  service/submit；失败仍按原本板 STOP/退休机制执行。最终 PASS 后到主机 STOP
  的尾段另审实际退休原因，不用旧参考 passed 字段代替联合健康结论。
- 主机测试最终 206 项通过，集成 trace/summary/owner 的独立集合 191 项通过，
  文档检查器测试 38 项通过，集合不相加。观察器测试覆盖释放后证据、错误代际、
  门控重入、失败不覆盖输出和无硬件副作用；三个隔离变异负控均被检测。首次读取
  失败/错误会话恢复后仍失败的用例已补。构建复核发现强 CAS 可生成重试，已改为
  弱 CAS 单次尝试，目标反汇编确认无 retry backedge。
- Release linker 快照静态 RAM 增用 44 B，主区余 20076 B；新观察器及其调用的
  后端快照嵌套栈帧合计 568 B，不是整条 Core1 峰值栈，也不是 WCET 证明，见
  `resource-r2.json`。本次不宣称零干扰或 GPIO 精度。P3 r1 因运行中有上述源码
  修正而拒绝签发凭证，原失败保留；须以最终指纹的 P3 和后继联合探针专项为准。
- 最终 `p3-output-guard-r2/` 为 PASS_WITH_WARNINGS，25 INFO、18 WARN、0 ERROR/FATAL；
  源码指纹 `ffb2397a110f041e2a1d5dfff610debb38b4b6f1598ad2d351180e8e732c78d8`，
  1293 文件。build ID 复用，按源码和 package 哈希辨认。凭证引用的 29 份原件
  逐一复算匹配，见 `receipt-r2-hash-check.json`。
- `positive-60s/` 四板新版 GUARD 均在首分钟 PASS、原因零、输出 RUNNING/OK、
  无自主 STOP；成功参考分别 35649/15339/15548/15322，运行查询零。主机统一
  STOP 后四路原生末态均为 CANCELLED，capture/request/session 身份、CRC、
  STOP/RELEASE 与父层 RAM 恢复通过；不是只看冻结 PASS。耗时 83.891 秒为试验
  快照，未用内部 residual 或已准入序号宣称 GPIO 精度。
- `expiry-120s/` 保留整轮 FAIL：STOP 配置 NO4 为有限输出，其引擎约二十秒
  EXPIRED，参考继续成功；第六十秒 GUARD 精确锁存 OUTPUT_STOPPED|OUTPUT_STALE，
  原因 EXPIRED，参考异常 flags 全零，并完成原 config 的本板 STOP/退休。
  NO2/NO3 分别在约 59.761/59.888 秒以 BINDING 提前冻结，GUARD 保留 EARLY_FREEZE；
  NO1 首分钟通过，第二分钟记录缺参考后退休。负例脚本原先只接受其他节点首分钟
  通过、第二分钟缺参考，因此整轮未通过，不能把 NO4 检出成功改写为全流程 PASS。
  主机约一百二十二秒才清理，运行查询零、原生 CRC、STOP/RELEASE 与恢复完成。
  该负例证明输出失效能独立于参考被检出，不代表健康锁相或四板同步停止。
- 独立复核 `hardware-review.json` 重算 P3 原件引用、两轮共八份原生 CRC 与
  生命周期；`c11-v23-review.json` 接受 v23 pending 范围，未提升产品发布或锁相。
  本次 C14 仅逐字迁出最旧进度 028 至 archive07，索引和 README 已闭合。
- `positive-600s/` 四板参考与输出引擎联合检查通过：十个检查点全部 PASS，mask
  1023，输出状态 RUNNING/OK；最终统一 STOP 后四路原因均为 CANCELLED，没有
  STARVED。运行查询零，四份原生 CRC 重解码通过，覆盖约 603.157–603.772 秒，
  每板 61 段，STOP/RELEASE 与恢复无遗留。详细复核见 `positive-600s-main-review.json`。
  NO2/NO3/NO4 相位实际提交 5501/5543/5604 次、频率提交 39/69/14 次；稳定完整
  段内部残差包络 [-234,108]/[-234,112]/[-203,110] ns，不是 GPIO 实测精度。
  输出快速服务预算超限计数 34/13/19/13 仍保留，不能用联合健康 PASS 声称 WCET
  已闭合。本轮无故障不覆盖旧偶发 STARVED；补给路径未修改、根因与重复性仍待
  后继切片。以上数字均为当日证据快照，非长期保证或正式锁相资格。

### VDC-PROGRESS-20260919-012：内部分钟监督、失败本板停止与通过保持环路

- TODO task ID：`VDC-OBS-007`，参考监督子切片完成，整项仍 IN PROGRESS。代码提交 `37ca2351`，匹配凭证
  和 pre-commit 已核对。新增可选 GUARD ARM 与独立
  元数据，复用原汇总 schema/池；Core1 按分钟检查内部参考/覆盖，Core0 持 TDMA
  control guard 核对原 config/capture/session/generation 后，仅对 FAIL 请求
  输出取消和环路退休。busy 重试；停止接受、精确退休证据分别保留。不授予产品
  隔离或 GPIO 精度，运行零主机采样，主机等待结束后才取回结果。
- 独审发现首次时钟失败未先绑定 config、零配置回卷被当作无效两项问题，均已
  修复；退休锁存不能借后继 config。完整首轮回归 234 项、后继 43 项通过；
  修正停止策略后的最终聚焦回归 52 项通过，数字为当日快照，集合不累加。
  r5 的零配置预期错误保留，修正测试为 matcher 拒绝后仍能 STOP，未放宽 matcher。
- 初版 `positive-60s/` 保留 FAIL：NO1/NO4 达标后自动停机，NO2/NO3 在约
  59.758/59.877 秒提前 BINDING 冻结。四板冻结前持续成功且异常 flags 全零；
  自动停止/退休、主机清理及参数恢复均完成。各板 ARM 起点不同，不能让先达标者
  断环；修正为 PASS 只锁存目标判定，最终由编排统一 STOP，失败仍本板自动止损。
  原件及原适配器 `capture-before-pass-stop.py` 保留，不能把首轮写成通过。
- Release 双槽与匹配源码 `p3-internal-guard-r2/` 完成，PASS_WITH_WARNINGS，
  INFO/WARN/ERROR/FATAL 为 25/18/0/0，复用已确认线序，不扩大 P3。复用构建目录的
  build ID 仍为 `20260918221036`，须按当前源码/package 指纹辨认，不能凭同号混用。
  原件引用逐项 hash 核对见 `receipt-r2-hash-check.json`；初版 P3 r1 也保留。
  主 RAM 静态增量 164 B，余量 20120 B，仅为 linker 快照，不代表栈峰值或 WCET。
- 实验适配器独审补齐缺参考负例不能接受任意异常、结论不得沿用外部示波器文字；
  最终四十五项纯内存测试通过。正常轮验证 PASS 不自动断环；负例要求原生 CRC、
  零成功、NO_SUCCESS/UNBOUND 与原因位一致、首分钟失败和实际退休，不冒称健康。
  证据根 `out/HardwareAcceptance/20260919/internal-guard-r1/`；独立源码与 C11
  审查见 `design-review.json`、`c11-review.json`，v22 保持 pending。
- 修正版 `positive-60s-r2/` 四板 PASS，目标达成后均未自行断环；完整原生 span
  超过目标，异常 flags 全零，运行查询零，主机统一 STOP/RELEASE 与恢复通过。
  独立重解码/分页 CRC、原始动作、退休和外层恢复核对见
  `positive-r2-hardware-review.json`；继承的 `flow_completed=false` 是旧详细采集
  未使用标志，本轮按真实步骤证据判断，不修改旧字段或以 passed 单字段替代核验。
- `negative-120s/` 不发 origin TRIAL，四板均于目标中首个六十秒检查点判 FAIL；
  NO_SUCCESS/UNBOUND、成功间隔及字段饱和与原生零成功一致，记录跨度约
  60.001–60.003 秒，guard 的接受/环路退休/输出退休均已确认。主机约一百二十二秒
  才发送清理命令，证明自主停止不依赖主机轮询；运行查询零，清理/恢复无遗留。
  此负例通过只证明故障被检出且退休，不代表 DPLL 健康。数字均为当日试验快照。
- 同一源码 `positive-600s/` 四板参考监督通过，十个分钟检查点全 PASS（mask 1023），
  四板 TDMA 保持至主机 STOP。成功数 364333/173362/176176/164227；原生 CRC
  重解码和保存 JSON 一致，运行查询零，清理/恢复无遗留。完整启动/尾段保留，
  不将稳定段替代整轮判定；旧 `positive-60s/` 失败仍独立保留。
  全部从板自第十秒起的完整段内部区间包络分别为 [-218,111]、[-207,107]、
  [-215,139] ns，比短轮有所扩大；这不是实际 GPIO 误差，也不授予全程百纳秒锁相。
  数字为当日试验快照；原始结果、离线表与 SVG 见 `positive-600s/offline-analysis/`。
- 独立复核同时发现 NO2/NO3/NO4 的 `run_output_raw.reason` 均为 STARVED，
  提前结束输出；NO1 为主机取消。当前 GUARD 只检查参考/模型，并未检查输出
  健康，因此原试验 `passed=true` 只在参考子范围有效，整机长时输出验收失败。
  实际退出字段见 `output-retirement-600s.json`；不能以最终 idle 掩盖饥饿，
  也不能用全局 service 最大间隔单独断言原因。原件保持，不改写为健康。
  独立核验 `600s-hardware-review.json`：相对 PIO anchor 约在 169.311/70.240/
  553.306 秒饥饿，最后约 25 ms 未成功补给，期间仍有 service。NO2 末态为
  SUBMIT_REJECTED/NOT_READY，NO3/NO4 为 DMA_NOT_READY；最大 service gap 均
  小于配置的 refill 窗口，不能直接归因 CPU 停服。早于本 guard 的
  `internal-generation-r1/capture-600s/` 四板末态均为正常取消，需同固件监督
  开/关 A/B 判别当前开销与补给状态机的关系，不能先断言与探针无关。
- 下一 gate：`VDC-OBS-007` 与 `VDC-OUTPUT-001` 先补输出健康分钟判定、定位
  PIO 补给/缓存/提交路径并修复饥饿，再推进 `VDC-SNAPSHOT-001`、
  `VDC-RECOVERY-001` 的质量老化、一致发布和恢复。实际 GPIO 精度仍独立验收。

### VDC-PROGRESS-20260919-011：坏启动 seed 不再提前消费 origin 授权

- TODO task ID：`VDC-OBS-007`、`VDC-RECOVERY-001`，保持 IN PROGRESS。提交
  `c7d15493`：自动交接在一次性 admit 前检查完整 seed、邮箱 CRC/类型/槽位/目标
  以及帧序列/身份；坏候选继续原 bootstrap，下一有效回包可启动。直接启动也复验
  全部邮箱。授权拒绝、begin/poll 后失败仍须 STOP；不回卷授权、不改物理拒绝码，
  自主稳态路径无新增扫描，启动扫描成本仍需持续关注。
- 旧代码负例复现同类 MAILBOX 拒绝；新真实适配器回归证明坏候选不消费授权、不调用
  begin/poll，后续真实 TX/RX 可恢复。相关 60 项通过；另发现旧几何夹具缺少
  priority STOP 桩，补齐未安装 IRQ 的边界后 56 项通过，旧失败日志保留。
- 最终指纹四板 `p3-origin-seed-r3/` PASS_WITH_WARNINGS，build `20260918221036`，
  INFO/WARN/ERROR/FATAL 为 23/22/0/0，29 个引用 hash 核对通过。未重扫线序，
  不把锁相质量纳入 P3 基础门禁；上述数量均为当日快照，非产品事实源。
- 新固件独立十秒及六十秒内部复采通过，运行查询零。六十秒有效成功数
  33416/13883/14102/14004，三从调频 14/17/24、相位更新 557/563/573；异常 flags
  全零，首成功最大间隔 0.487/0.624/0.748/0.851 秒。原生 CRC/解码、STOP/RELEASE
  与恢复均完成；不能据此声称 GPIO 精度或全部跨启动可靠性。
- 证据根 `out/HardwareAcceptance/20260919/origin-seed-r1/`，含独立代码审查、旧版
  负对照、软件测试、`receipt-hash-check.json` 和 `capture-check.json`。上一长窗
  失败原件不覆盖，也未确定当时具体坏邮箱字节。下一 gate：设备端分钟健康判定
  与合法停止，再扩连续长窗；内部观察不代替实际输出精度及 VDC 失联质量发布。

### VDC-PROGRESS-20260919-010：静默启动取消非必要模型查询

- TODO task ID：`VDC-OBS-007`，保持 IN PROGRESS。上一轮 START 前可选 MODEL
  管理查询 timeout 三秒，trace 已计时，导致首成功间隔超限。新实验适配器仅
  配置阶段 NO1 的该查询明确省略一次，`model_before_start=null`，记录未发送；
  不伪造旧值或有效响应，其他配置/身份/owner ACK 与 STOP 后采集均保持。
  固件未改，源码指纹仍绑定 build `20260918210835` 的原 P3；原失败完整保留。
- 五项独立无硬件边界核验通过，见 `adapter-review.json`。新 `capture-60s/`
  静默 60.015 秒严格通过，四板各七段，异常 flags 全零，首成功间隔
  0.493/0.645/0.768/0.928 秒（当日快照，非产品事实源），未修改一秒门限。
  运行查询零，STOP/RELEASE/参数恢复通过，独立 CRC 与生命周期核验见
  `60s-review.json`。后继 `capture-600s/` 已结束并 FAIL：NO1 在约 0.454 秒冻结，
  四板有效参考成功数均为零；从板空段与计数饱和完整保留。该轮不是锁相精度失败，
  而是 NO1 自主 origin 尚未完成启动。全板 STOP/RELEASE/参数恢复完成，错误原件不覆盖。
- 按真实 SCPI 字段拆解：handoff 的 12 是阶段总数，不是 builder 拒绝码。
  release 的 `physical_reject=1966081` 拆为 `PREPARE_STAGE` 与 MAILBOX stage；
  仅 MAILBOX 被调用一次，后继 BUILD_BEGIN/STEP 均未进入。前置配置检查有独立
  CONFIG 拒绝编码，当前保留的是邮箱批校验失败；尚无失败 slot/原始 seed，
  不能断言是 CRC、class 或 mask。代码前置交接仅验 header，后置物理准备才验邮箱，
  正在验证不消费授权、不碰硬件的坏 seed 延后准入方案；不得放宽 CRC 或伪造 seed。
  独立核对原始分页/CRC、拒绝码与清理结果见 `capture-600s/independent-failure-review.json`；
  从板 17.17986918 秒为字段饱和值，不能当成真实最大间隔。
- 原固件同配置 `repeat-10s/` 单独复采通过内部覆盖/成功间隔检查，运行查询零，
  四板有效成功数 2345/1714/1698/1698（当日快照，非产品事实源），STOP/恢复完整。
  复采不能消除上一轮失败，也不能证明跨启动可靠或实际 GPIO 精度。
- 证据根 `out/HardwareAcceptance/20260919/internal-startup-r1/`。分钟自动止损、
  实际 GPIO 精度和失联质量老化仍需独立验证，不能由内部正常跟随直接授予。
  下一 gate：先用短窗闭合启动退避/拒绝，再实现同会话设备端分钟判定与合法 STOP；
  现有静默脚本只能结束后回看，本轮确实等待了完整长窗，不能称在线止损已完成。

### VDC-PROGRESS-20260919-009：内部探针 TX 基线绑定当前代际

- TODO task ID：`VDC-OBS-007`，保持 IN PROGRESS。新 capture 首个 service 可能
  先于新 TX provider，旧代 rejected 被采作基线，新代 owner 清零时误标为
  COUNTER_RESET。修复基线只采用当前 sync generation；未取得当前代基线时
  首个当前代快照从 owner 零初始化累计，开窗已有当前代基线则仅累计后续增量。
  同代回退、首次读取失败、已采当前代后的异代均保留异常；未忽略首段或尾段。
- 原汇总/解码/静默编排组合回归 214 项通过；新增真实 TX owner 正负对照为
  旧版八个预期失败、六个通过，新版十四项通过，完整 summary 文件 52 项通过。
  覆盖旧/新代、已有当前基线、真实回退与读取失败；schema、布局、池大小和
  严格解码门限未改。数量为当日快照，非产品事实源；独立审核同意 v21 pending。
- 新源码 Release 与 `p3-internal-generation-r1/` 四板 quick P3 完成，build
  `20260918210835`，PASS_WITH_WARNINGS、ERROR/FATAL 零，保留 27 项 WARN 及
  strict false。独立核对源码指纹与 29 个引用 hash，见 `p3-independent-review.json`。
- `capture-60s/` 静默六十秒通过，四板各七段，coverage/成功覆盖/间隔判定均过，
  异常 flags 全零，运行查询零，STOP/RELEASE/参数恢复完成。NO1 原生拒绝合计
  25226，STOP 后 TX 末态 25227、last_reject=STOP；差值与冻结后 STOP 退休新增拒绝一致，
  不宣称两者全等，首次拒绝精确计数仍由真实 owner 边界回归证明。
- 证据根 `out/HardwareAcceptance/20260919/internal-generation-r1/`；同配置
  `capture-600s/` 单次静默 600.078 秒，四板各 61 段、覆盖完整、异常 flags 全零，
  成功跨度均约 600.573 秒；三从频率更新 38/72/27、相位更新 5183/5231/5216。
  运行查询零，稳定段内部残差端点为 −198..103/−200..106/−193..107 ns，不能
  当作 GPIO 边沿误差。严格整轮仍 FAIL：首成功间隔 3.475..3.853 秒超过原一秒
  判据；配置末尾 NO1 MODEL 查询 timeout 恰为三秒，紧接四板 START，trace 从 ARM
  阶段已计时。后续 bin 最大成功间隔 0.093..0.264 秒，无异常；首 bin 聚合极值
  不能反推其每个运行间隔，不扣掉启动时间来重写通过。详见
  `startup-boundary-analysis.json`，下一切片处理这条非必要启动诊断与阶段边界。
- 600 秒外层恢复报告保留 NO1 error queue 非空和 restore-needed；最终四板
  initial/final 参数一致、TDMA 均 STOP、错误队列均零，STOP/RELEASE 完成。
  保留原 FAIL，不把最终参数一致称外层严格通过。分钟检查点设备端有界告警仍
  待实施，候选在 `minute-checkpoint-design-review.json`；本地 STOP 不等于四板
  联停。发布后继审计在 `vdc-publication-next-audit.json`：无新 evidence 的
  age 服务及 idle ready 发布需验证，正式 freshness/HOLDOVER 另行小切片闭合。
- 本切片源码提交 `4594a2b9`，匹配源码 P3 凭证与 pre-commit 通过；文档单独提交。
  独立长窗复核见 `capture-600s-independent-review.json`，不提升整轮失败结论。
- C14 按最旧连续条目逐字轮转 026/025 至 archive 07，索引闭包与正文 hash
  核验见 `progress-rotation.json`。不删除旧失败证据。

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
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_10.md` | VDC-PROGRESS-20260918-033..VDC-PROGRESS-20260918-007 | 27 | 2026-09-19 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_09.md` | VDC-PROGRESS-20260918-006..VDC-PROGRESS-20260918-004 | 3 | 2026-09-19 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_08.md` | VDC-PROGRESS-20260918-003..VDC-PROGRESS-20260917-031 | 4 | 2026-09-19 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_07.md` | VDC-PROGRESS-20260917-030..VDC-PROGRESS-20260917-025 | 6 | 2026-09-19 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_06.md` | VDC-PROGRESS-20260917-024..VDC-PROGRESS-20260917-023 | 2 | 2026-09-19 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_05.md` | VDC-PROGRESS-20260917-022..VDC-PROGRESS-20260917-022 | 1 | 2026-09-19 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_04.md` | VDC-PROGRESS-20260917-021..VDC-PROGRESS-20260917-012 | 9 | 2026-09-18 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_03.md` | VDC-PROGRESS-20260917-011..VDC-PROGRESS-20260917-011 | 1 | 2026-09-18 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_02.md` | VDC-PROGRESS-20260917-010..VDC-PROGRESS-20260917-001 | 10 | 2026-09-18 |
| `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_01.md` | VDC-PROGRESS-20260916-043..VDC-PROGRESS-20260906-002 | 146 | 2026-09-17 |
