# VDC 任务进度归档 14

Status: Frozen
Domain: VDC
Canonical: `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_14.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/legacy/vdc/` 其他归档分卷
Last updated: 2026-10-15

本文是 `docs/vdc/VDC_TASK_PROGRESS.md` 按 C14 轮转出的最旧条目（逐字迁移，倒排）。

## 归档条目
### VDC-PROGRESS-20260919-034：健康锁定标志与质量一致投影

- TODO task ID：`VDC-SNAPSHOT-001`、`VDC-VERIFY-001` IN PROGRESS；以下数字为本轮快照。原 RefMem `LOCKED` 位仅看 Domain 控制状态、debug 和 provisional；quality 老化为 DEGRADED 后仍可能置位，与同一向量的质量字段矛盾。
- 实现只收紧 `distributed_refmem_vector_flags()`：复核既有同快照 quality 有效性、锁状态、HEALTHY/fine、样本时间、freshness/age 及 gate。两向量复用原轮转和 CRC，不增加发布次数、时钟读取、健康镜像、wire 或特等席；保留 DCO/证据序号及 VALID/STALE 原义。此位是健康控制状态投影，不是 formal promotion，也不提升 typed FRESH。
- 软件证据：独立测试先在旧代码复现 9 失败/4 通过；修复后 publication 28 项、idle/reference/health 115 项、两/六/八节点 projection 3 项、文档 38 项通过。健康、边界、超龄、gate/valid/tier/时间缺失及矛盾快照、恢复均覆盖。正例使用健康资格夹具，经真实 Domain aging、manager 发布和两向量路径验证；不是实板正式样本准入或锁定过程证明。
- 旧 golden 三项初测因预期 LOCKED 位撤销而失败，原件留存；夹具先检查当前 payload CRC/无 LOCKED，再仅归一历史 LOCKED 位对照原 CRC golden，文件本身未改，其余字节继续受原基线约束。独立最终复核 PASS，见 `review.txt`。
- Release A/B/BOOT 与固定四板 P3 通过：build `20260919151458`，源码 SHA `d574aa0f88dcbe45793ee3b964cc13fe22020ff4ad163edb99e5af0f30d7a13d`，1325 文件，staged 指纹匹配。P3 为 PASS_WITH_WARNINGS（25 INFO / 19 WARN / 0 ERROR / 0 FATAL）；既有全表严格预算仍未过。NO1 RefMem 本轮 deadline miss 增 1、overrun 增 0，最大 17035 cycles 对配置 WCET 29000；不据此推定与本改动的因果或授予 WCET，四板相位对照见 `p3-phase-comparison.json`。
- 资源对照：相对 033，text +72 B、data/bss 不增，flags 函数 120→186 B，RefMem 实时入口仍 1284 B；代码大小不等于执行时间，既有判断增加有界成本。见 `resource-audit.json`，未调整静态预算或增加席位。
- 联合首轮 `joint-gap-60s-r1` FAIL 保留：四板 GUARD、300 ms 新参考暂停/恢复、模型及 Core0 镜像通过，外部十二窗/48 RAW 哈希通过；NO4 峰值 52.145 ns，已采边沿仍在 ±100 ns 内。适配器读取两向量时，通用串口层去掉复合结果的 OK 前缀，导致 VDC 查询解析失败、DPLL 查询未执行。只在 out 适配器登记两条复合查询，再作同配置实测；未改固件或重释首轮结论。
- 同配置 `joint-gap-60s-r2` PASS：新参考暂停至恢复约 301.598 ms，三从各记录一次恢复；四板 GUARD、模型末态与 Core0 健康镜像一致。STOP 下原生 reader 校验两向量后查询成功，各板两表分别保持 flags 85/69/69/69，均无 LOCKED，未将 typed 诊断闭环上提资格。SCPI 是字段子集，不据此声称主机独立重算完整向量 CRC。
- 复测十二个外部窗口、48 RAW 哈希通过；NO2/NO3/NO4 最大绝对相差 53.824/34.117/38.596 ns，NO2 三个已采边沿超 ±50 ns，全部已采边沿在 ±100 ns 内。两轮 RUN 查询均零，四板最终 STOP、清除注入并恢复参数，无清理错误。稀疏波形不证明未采样区间或暂停期间的连续精度，末态镜像一致不证明逐事件及时性。
- 原件根：`out/HardwareAcceptance/20260919/vector-health-lock-r1/`；失败、资源、P3、评审、采集日志及两轮 `joint-summary.json` 均留存。健康投影修复切片 DONE，整体发布/锁相目标保持 IN PROGRESS。
- 交付：实现与同指纹 P3 凭证提交 `a1d4b336`；文档分离提交。
- 下一 gate：正式 aging 对失效读钟/反向时间的 fail-closed 修复；随后明确 typed 合格证据接入及调试相位控制到正式控制的迁移。当前 phase step 遇 LOCKED 会拒绝，不能只上提一个状态位。两者是发布主线后续切片，不阻塞现有诊断闭环；不得借此扩充未经用户审核的特等席。


### VDC-PROGRESS-20260919-033：GUARD 分离启动等待与后续参考缺口

- TODO task ID：`VDC-OBS-007`、`VDC-SNAPSHOT-001`、`VDC-HOLD-001`；诊断修复切片 DONE，父任务 IN PROGRESS。以下数字为验收快照，非配置事实源。
- 根因：原 `last_success` 以 START 初始化，首次成功前的等待进入最大成功间隔；032 首轮 NO3/NO4 因此超过诊断界。新 GUARD 原生段带 `SUMMARY_STEADY_SUCCESS_GAP`，首次成功后才累计成功缺口，启动等待单列；同段后续、跨段、尾段真实缺口继续约束。首次完整非空段无成功/未绑定仍失败，当前最大段跨度为十秒，不是无限启动豁免。首次成功不是物理锁定。
- 边界：复用既有 capture 状态与原生 flags；不新增记录字节、时钟读取、快照发布或特等席，不改 wire、PIO/DMA、OTA。Core1 原诊断点增加条件判断，不能宣称零成本或完整 WCET 合格。健康镜像仍只由 Core0 发布，后续特等席增设仍须 `EXE-SEAT-01` 用户审核。
- 软件：summary/decoder/capture 252 项、GUARD 62 项及文档 38 项通过。新增真实 recorder/GUARD 场景覆盖启动等待、后续/跨段/尾段缺口、整段无参考、再 ARM、时钟/绑定/服务错误与严格边界；13 份 host 原生记录独立解码通过。旧八份实板原件仍按 `start_inclusive` 解释，032 首轮原失败保留；同捕获混合标记拒绝。
- P3：Release A/B/BOOT 与固定四板快速验收通过，`p3-r1` 为 PASS_WITH_WARNINGS（25 INFO / 18 WARN / 0 ERROR / 0 FATAL）。build `20260919145017`，源码 SHA `1189838276cc621280ef97862dbe7cd7e2c18e53e8a130d18606570c7849dff0`，1325 文件；staged 指纹核验通过。复用已知环序，不扩大 P3 门禁。
- 受控启动实测：STOP 下配置既有 GAP 将 NO1 首次新参考延迟 1200 ms，运行不查询板卡。`joint-startup-60s-r1` PASS：三从首次成功等待 1.911/2.046/2.178 s，之后最大成功间隔 32.736/26.690/32.215 ms；四板 GUARD 正常完成，未因启动等待误退休。十二个外部窗口、48 RAW 哈希通过，三从最大绝对相差 35.605/41.745/76.060 ns；NO4 一样本超 ±50 ns，全部已采样本仍在 ±100 ns 内。
- 再 STOP/ARM 的 `joint-startup-120s-r1` PASS：三从首次成功等待 1.879/2.020/2.161 s，之后最大成功间隔 34.043/38.463/36.865 ms；六十秒和一百二十秒检查点均通过。二十四个外部窗口、96 RAW 哈希通过，三从最大绝对相差 34.072/25.986/25.959 ns，已采样本均在 ±50 ns 内。两轮模型末态一致、RUN 查询均为零，四板最终 STOP、清除注入并恢复配置，无清理错误。
- 原件：`out/HardwareAcceptance/20260919/guard-startup-gap-r1/`；两轮各有 `startup-audit.json`、`joint-summary.json`、原生记录及波形，实际示波器触发为 CHAN1。独立复核从八份 `read-pages.json` 重组并校验原生 CRC/解码，结论 PASS_WITH_NOTE，见 `startup-hardware-review.txt`。首次新参考抑制不是物理断链，成功间隔是 owner 操作间隔，不是逐帧运输期限；稀疏相差与内部汇总不授予连续精度或同事件关联。新轮通过不证明 032 首轮多板停机的共同事件因果。
- 交付：实现与同指纹 P3 凭证提交 `21ee638c`，文档分离提交。
- 下一 gate：继续 `VDC-SNAPSHOT-001`/`VDC-HOLD-001` 的正式 quality/valid/freshness 和逐事件保持/恢复证据；该修复消除启动误判，不据此升级 formal quality。普通诊断经 Core0，任何需增加特等席的方案先交用户审核。


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

