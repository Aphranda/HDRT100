# VDC 任务进度归档 15

Status: Frozen
Domain: VDC
Canonical: `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_15.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/legacy/vdc/` 其他归档分卷
Last updated: 2026-10-15

本文是 `docs/vdc/VDC_TASK_PROGRESS.md` 按 C14 轮转出的最旧条目（逐字迁移，倒排）。

## 归档条目
### VDC-PROGRESS-20260921-038：四板内部稳态包络替代最终端点分级

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001`、`VDC-VERIFY-001` IN PROGRESS；本轮只改 P3 host 证据消费与复算，不改固件、VDC owner、Core1 负载或特等席。
- 异常：scope-off 的 V1 P3 只检查 STOP 后 `final_phase_raw` endpoint，不能覆盖前一稳态区间的 residual 极值。对 `out/HardwareAcceptance/20260921/anomaly-fix-r1/p3-r4/` 离线重算时，旧端点为 94 ns、INFO，原生稳态包络为 119 ns、WARN；三从范围分别为 `[-113,+100]`、`[-112,+102]`、`[-119,+109]` ns（快照，非事实源）。
- 修复提交 `14742cee`：`HAOFV_P3_DPLL_SHORT_V2` 从 NO2--NO4 的 `summary.ram.bin` 提取稳态 residual extrema。完整六十秒 capture 必须满足 schema/interval、terminal/freeze、覆盖、全部 bin 有成功和成功最大间隔；首个 `INTERNAL_SETTLING_BINS` 启动 bin 完整保留、显式排除于稳态精度，最终 endpoint 必须落在最后稳态 bin。分页记录按 canonical 顺序重组并与 native bytes、`decoded.json` 双重一致。
- 身份绑定：summary 与 receipt 逐项绑定 staged 源码指纹、firmware build、按序四板 UID；三从 trace 绑定共同 session/generation、source/target slot、local/reference slot 与 node count。V1 endpoint-only 证据 fail-closed，scope-on 外部 RAW 路径未放宽。
- 软件验证：P3 DPLL/alarm/acceptance 与 VDC summary/seal 聚焦回归 296 项通过；扩大复核集合 559 项通过，独立复核 PASS。Python 编译、工具治理/catalog 与 whitespace 检查通过。
- 四板实测：`out/HardwareAcceptance/20260921/p3-internal-trace-v2-r1/p3-r1/`，build `20260921101121`，scope absent、external=SKIPPED，结果 `PASS_WITH_WARNINGS`（快照：INFO 32 / WARN 22 / ERROR 0 / FATAL 0）。NO2/NO3/NO4 约五十秒稳态包络为 `[-126,+98]`、`[-137,+99]`、`[-172,+103]` ns，总最大绝对值 172 ns，按实现阈值正确输出 `DPLL_PHASE_OUTSIDE_100NS`；完整 capture、分页重组、endpoint/末 bin 及共同 session/generation 全部通过。
- 边界与下一 gate：内部 owner residual 包络不是 GPIO 物理精度、连续波形或逐事件相关证据；启动大残差仍可审计。继续 `VDC-OBS-007` 的质量发布/恢复对账和 `VDC-PRECISION-001` 的物理测量不确定度，不因本轮 WARN 提升 formal lock 或产品 RUN。


### VDC-PROGRESS-20260920-037：示波器通道时延读回与收敛窗分级判据

- TODO task ID：`VDC-PRECISION-001`、`VDC-OBS-007`、`VDC-VERIFY-001` IN PROGRESS；以下数字为本轮实测快照，非产品资格。
- 目的：`VDC-PRECISION-001` 要求核验"探头/通道时延及测量不确定度"，本轮把两件此前只靠人工解释的事变成可复核证据——示波器自身通道时延随每个稀疏窗口读回留档；启动首点的未收敛现象写成显式、可审计、可关闭的判据，而不是默认豁免或事后口径。
- 通道时延读回：`tools/vdc_priority_trace/vdc_scope_evidence.py` 在同一 650k RAW 前缀捕获内逐通道查询 `:CHANn:TCAL?`，`channels` 每行新增 `channel_delay_s`，报告新增 `channel_delays_s` 与相对参考通道（默认 CH1）的 `channel_delays_relative_s`；查询失败或不可解析记 `None`，不伪造 0。本轮台架四通道均返回 `0.000000` s，即该仪器未声明通道间偏斜，十二个窗口读数一致（仅 1 个不同取值），故相对相位无需仪器偏斜修正——该假设现为逐窗实测事实，而非默认前提。
- 收敛窗分级判据：`tools/vdc_priority_trace/vdc_priority_joint_capture.py` 把相位判据分两级。±100 ns 仍是 fail-fast 粗门限，对含首窗的全部窗口生效（行为未变）；±50 ns 为稳态资格门限，仅对超出 `--settle-s`（默认 5 s，0 关闭）所声明收敛窗之后的窗口生效。每个 60 s checkpoint 记录 `settle_s`、`settling_windows`、`steady_windows`、`steady_failures` 与 `settling_exemptions`；判定层新增 `external_steady_state_passed` 与 `external_settling_exemptions`。
- fail-closed：checkpoint 未携带稳态分类时该项不得视为通过（`external_steady_state_passed=false` 且整体 `passed=false`）；收敛窗内超出 ±100 ns 仍判失败；缺 `due_s` 的窗口不能借用收敛豁免；`--settle-s` 必须有限且不超过 `--seconds`，否则在接触硬件前拒绝。host 测试覆盖首窗豁免与后窗不豁免、粗门限仍作用于收敛窗、0 关闭豁免、未分类不得通过、豁免计数可见及参数校验；本轮定向套件 282 项通过。
- 实测（60 s，`joint-60s-r3`，88.0 s）：12 窗全过粗门限，`external_steady_state_passed=true`、`external_settling_exemptions=0`，checkpoint 60 判 `settling=[5]`、稳态 11 窗、无稳态失败；首窗 NO2/NO3/NO4 最大绝对相位 5.945/3.945/19.944 ns 均在 ±50 ns 内，其余 11 窗最大绝对相位分别 ≤5.252/≤12.000/≤9.399 ns。同口径前一轮 `joint-60s-r2` 首窗 NO4 为 +61.986 ns（越 ±50 未越 ±100）：在本判据下该点计入 `settling_exemptions` 并被显式列出，r3 未用到豁免。两轮都说明首窗越 ±50 属正常收敛，不是需要人工解释的异常。
- 内部对照（同轮，6 bin / 60 s）：NO1 success 24103、freq/phase applies 0/0、最大成功间隔 49.633 ms；NO2 11687、18/542、56.008 ms；NO3 11760、36/569、51.379 ms；NO4 11610、15/550、52.114 ms；10 s 后内部残差包络 NO2/NO3/NO4 为 −116…+100 / −123…+102 / −113…+104 ns。内部残差与 GPIO 边沿是不同量、不同时间原点，不作逐事件或逐 bin 配对；`physical_continuous_lock_qualified` 与 `exact_event_correlation_qualified` 恒为 false。
- 原件：`out/HardwareAcceptance/20260920/joint-60s-r3/`（`capture/input-probe.json`、`capture/sample-*/{scope.json,phase.json,ch*.block}`、`capture/NO*/`、`capture/analysis/`）；对比图 `out/HardwareAcceptance/20260920/joint-60s-r3-comparison.svg`（新增收敛窗阴影、±50 ns 稳态带与收敛窗标注）；前一轮 `joint-60s-r2/` 与其对比图保留。P3 凭证 `out/HardwareAcceptance/20260920/settle-policy-preflight-r1/`（`PASS_WITH_WARNINGS`，33 INFO / 20 WARN / 0 ERROR / 0 FATAL）。
- 交付：实现与同指纹 P3 凭证提交 `4616d1b9`；文档分离提交。
- 下一 gate：继续 `VDC-PRECISION-001` 的测量不确定度与"全部有效边沿/窗口"口径，并把进度 036 的未观测区间界（5.046 s / 0.038 ns）接入发布验收；`VDC-VERIFY-001` 仍缺 RUN、角色/配置矩阵与长稳，本切片不上调 formal quality，也不宣称连续物理锁相。


### VDC-PROGRESS-20260920-036：离线同步计量工具与未观测区间有界化

- TODO task ID：`VDC-OBS-007`、`VDC-PRECISION-001`、`VDC-VERIFY-001` IN PROGRESS；以下数字为本轮快照，非产品资格。
- 目的：把"未观测区间有界化"从定性说法变成可复核证据，并给发布验收提供不依赖台架的离线计量。新工具 `tools/vdc/vdc_sync_metrology/vdc_sync_metrology.py` 只读冻结联合捕获证据（`capture/input-probe.json`；不连板卡、不连示波器、不引用固件符号），计算采样相位序列、窗内重复性、重叠 Allan 偏差、最小二乘频偏（1σ/2σ）与 `(|offset|+2σ)×最大空档`；收据绑定输入摘要（sha256/字节数）并记录工具自身哈希。
- fail-closed：缺时间戳或相位序列、捕获不完整、overrun、`large_phase_ambiguity`、窗口数不足、无任何平均因子达到最小差分、节拍抖动 >5% 均拒绝出收据（`--allow-incomplete`/`--allow-nonuniform` 才继续并在收据标注）；`continuous_lock_claimed` 恒为 false，`measurement_noise_floor_qualified` 恒为 false。host 测试 13 项通过：频偏回算、界=乘积、白相位噪声 ADEV 定律与 τ 标度、差分不足记录、缺时间戳/相位歧义/不完整/非均匀拒绝、输入摘要随内容变化、永不宣称连续锁相，以及真实 600 s 证据回归。
- 实测（600 s / 120 窗，`dpll-delay-center-r2/plus20-batched-600s`）：三通道频偏 +0.000124 / −0.002599 / +0.003526 ppb，2σ 0.0040 / 0.0044 / 0.0041 ppb，与既有"≤0.004 ppb（2σ）"口径一致；重叠 ADEV 在 τ=5/10/20/40/80/160 s 为 1.30/0.59/0.34/0.14/0.08/0.04 ppb（NO2，NO3/NO4 同量级），呈 ∝1/τ 的白相位噪声行为；反推白相位 σ 3.755/3.922/3.746 ns，与文档"约 3.8 ns rms、来源未定位"独立一致；τ=320 s 差分不足 8，工具拒绝外推。
- 未观测区间（新口径）：单窗可证观测约 1 ms（示波器记录 2 ms 内的边沿跨度），最大连续未观测空档 **5.046 s**、占空比 0.02%、p95 5.031 s、窗间无重叠；界 =（|offset|+2σ）× 5.046 s = **0.038 ns**，远低于 class A/B 阈值。60 s 轮（035 切片 `quality-clock-failclosed-r1/joint-gap-60s-r1`）为 −0.326/−0.403/−0.412 ppb（2σ 0.354–0.408 ppb，窗口长度不足以定 ppb 级绝对值），界 4.11 ns。
- 文档口径修正：`ARCH_FUTURE_APPLICATION_PLAN.md` 与 `ARCH_SYNC_STANDARD_CORE_DESIGN.md` 原记"单次捕获中位约 3.36 s、最大空档约 1.8 s"把捕获**墙钟时间**当观测时间（墙钟主要是主机读取与搬运开销）；以可证观测计为 5.046 s，两份 Draft 已按新口径改写并保留旧口径说明，后者第 9 节 SC-T5 前置条件按"已具备可复核证据"标注，SC-X1/SC-X3 状态不变。
- P3 与失败保留：首轮 `metrology-tool-r1` 因默认 quick 配置的 `ota_board_ids` 仍含 NO5 序列（`config/hardware_acceptance/p3_bench.json` 第 3–8 行；同文件第 59 行 `dpll_observer_board_id`）而在 OTA 阶段失败（`missing_serial_numbers=839E1AE79EA20F31`），失败证据保留、旧收据未被覆盖；按现役台架（四板 + 示波器 + 内部观测）改用 `--tdma-only` 的 `metrology-tool-r2` 通过：PASS_WITH_WARNINGS（32 INFO / 21 WARN / 0 ERROR / 0 FATAL），build `20260919163402`，1327 文件，OTA 4 板。
- 严格门禁现状（本轮实测，两轮一致）：`strict_gates_passed=false`，`diagnostic_continue=true`，`outcome=PASS_WITH_WARNINGS`。r2 保留两条失败：`TRN-00 residence matrix`（gate did not pass）与 `four-Node TDMA closed loop`（returncode 1）；035 轮保留 `four-Node TDMA closed loop`（explicit startup barrier timed out）。r2 未复用既有拓扑（重跑 P0T 线序探测），故告警构成与 035 不同（新增 9 INFO `CANDIDATE_REJECTED`、2 WARN `QUALITY_GATE_FAILED`、1 WARN `EXECUTION_FEEDBACK`；035 的 `TDMA_STARTUP_QUALITY`/`TDMA_NODE_QUALITY` 两条本轮未出现）。两轮共同的既有严格预算告警仍在（`vdc_deadline_miss_count_grew`、`vdc_overrun_count_grew`、`vdc_max_runtime_exceeded_wcet`、`vdc_load_quarantined`），既有全表严格预算未过这一结论不变；**四板 TDMA 闭环与短时 DPLL 发布尚未进入必需验收项，本轮不据此声称其通过**。NO5 是否从 quick 配置清理待用户决定（属 `config/` 变更，须重跑 P3）。
- 原件：`out/HardwareAcceptance/20260919/dpll-delay-center-r2/plus20-batched-600s/metrology.json`、`out/HardwareAcceptance/20260919/quality-clock-failclosed-r1/joint-gap-60s-r1/metrology.json`；P3 见 `out/HardwareAcceptance/20260920/metrology-tool-r2/`（失败轮 `metrology-tool-r1/` 保留）。
- 交付：实现与同指纹 P3 凭证提交 `6d004b2a`（hook 实跑：docs 门禁 OK + P3 门禁 OK）；文档分离提交。
- 下一 gate：把四板 TDMA 闭环与短时 DPLL 发布纳入必需验收（当前仅诊断告警），再推进 `VDC-EVID-001` 正式合格输入与控制迁移；计量侧下一步是仪器噪声底标定（SC-X3）与把计量收据接入 `VDC-VERIFY-001` 发布验收。本切片只提供离线界，不上调 formal quality，不宣称连续物理锁相，也不替代 GPIO 精度。


### VDC-PROGRESS-20260919-035：正式质量年龄失效锁存与待提交样本取消

- TODO task ID：`VDC-SNAPSHOT-001`、`VDC-HOLD-001` IN PROGRESS；以下数字为本轮快照。原 `vdc_domain_age_quality()` 在读钟不可用（`now_ns == UINT64_MAX`）时直接返回，旧 HEALTHY/LOCKED 与旧年龄被保留；反向时间把年龄算成 0，等于旧证据自行续期。
- 实现（Domain）：未知或饱和年龄统一锁存为 `last_sample_age_us = UINT32_MAX`——读钟不可用、时间早于样本锚点、相对已存微秒年龄回退均进入该状态；健康判定在 LOCKED/HEALTHY 与候选分支之前强制 DEGRADED（`FAULT` 仍优先），最大 freshness 上限也不能放行。仅恢复读钟不续期旧证据，仍由原 accepted 样本提交或生命周期重置解除。
- 实现（Core1）：pending preparation 的本地 apply 时间纳入复验，首样本尚无质量锚点时增加一次条件读钟；工作视图与发布视图任一新检出“有限→未知”跃迁时互相传播失效并撤销 preparation，经既有 finalize 拒绝路径退休，已生效 servo/DCO 保持不回滚。传播为边沿触发，旧发布视图的锁存状态不会反复污染后续真实新样本。
- 软件证据：Domain 回归 122 项、publication 回归 51 项、idle/aging 35 项、文档门禁 38 项通过；本轮复核复跑两个改动模块 63 项通过。首轮 publication 1 失败/50 通过（`lock_recovery` 夹具未按 accepted 语义重置年龄）与 idle 三轮 6/5/1 失败均保留原件，最终 r2/r4 通过；覆盖读钟不可用、反向时间、微秒年龄回退、FAULT 优先、最大 freshness、无参考、HOLDOVER、未发布模型隔离、accepted/rejected 恢复、pending 首次完成与跨锚点回滚。负测为 host 集成证据，不代表实板老化或正式 promotion。
- P3：Release A/B/BOOT 与固定四板快速验收通过，`p3-r1` 为 PASS_WITH_WARNINGS（25 INFO / 20 WARN / 0 ERROR / 0 FATAL），build `20260919154526`，源码 SHA `4d786e0ad296688d954c59a3acfe02fea7ffb518f375b69a40d7e4444a62c818`，1325 文件，`check-staged` 指纹核验 OK。既有全表严格预算与完整 WCET 仍未过，不据此授予资格。
- 资源对照：相对 034，text +288 B、data/bss 不增；`vdc_domain_age_quality` 174→242 B、`vdc_dpll_manager_age_quality_core1` 268→444 B，RefMem 实时入口仍 1284 B。新增条件判断与一次条件读钟有实际成本，代码大小不等于 WCET；未新增字段、诊断发布入口、席位或调度项。
- 联合实测 `joint-gap-60s-r1` PASS（89.86 s）：内部健康与十二个外部四路窗口全过、48 份 RAW 哈希一致，全部已采边沿在 ±50 ns 内（本轮日志最大绝对相差约 47.88 ns）；`physical_continuous_lock_qualified` 与 `exact_event_correlation_qualified` 仍为 false，RUN 查询为零，四板最终 STOP/恢复、无清理错误。该轮为 RAM 时序范围，不含 Flash、重启与 P0；稀疏窗口不证明未采样区间精度。
- 独立复核 `review.txt`：初判 PASS_WITH_NOTE、最终 PASS；保留的口径限制为年龄回退检测仅到已存微秒分辨率（不宣称纳秒单调性）、自然饱和同样取消在飞完成、pending 拒绝断言须以 servo 之后的 DCO/计为基线。复核同时记录一次工作区事件：reviewer 误用历史“外部一致性”指令改过两份 20260919 HTML 报告并重生成 PDF，四份报告已按 HEAD 恢复、无 diff 残留，未改产品、测试与域文档。
- 原件根：`out/HardwareAcceptance/20260919/quality-clock-failclosed-r1/`（`p3-run.log`、`resource-audit.json`、`p3-phase-comparison.json`、`review.txt`、`joint-gap-60s-r1/`）；测试日志见 `out/quality-clock-*.log` 与 `out/idle-clock-failclosed-fixed-r*.log`。
- 交付：实现与同指纹 P3 凭证提交 `bd5e7a87`；文档分离提交。
- 下一 gate：继续 `VDC-SNAPSHOT-001` 的正式合格输入接入、调试相位控制到正式控制的迁移及逐事件保持/恢复证据；该切片只让时钟异常 fail-closed，不上调 formal quality，也不扩容特等席（须用户审核）。


