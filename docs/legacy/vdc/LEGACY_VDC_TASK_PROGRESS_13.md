# VDC 历史进展快照 13

Status: Frozen
Domain: VDC
Canonical: `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_13.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/vdc/VDC_DOMAIN_TODO.md`
Last updated: 2026-10-14

> 按 C14 从进展日志迁出的最旧条目段；条目逐字保留、未编辑，顺序与源文件相同。
> 本快照为历史证据冻结件，当前状态以域 TODO 为准。

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


