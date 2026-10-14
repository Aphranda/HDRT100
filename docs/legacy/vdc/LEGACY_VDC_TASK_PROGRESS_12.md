# VDC 历史进展快照 12

Status: Frozen
Domain: VDC
Canonical: `docs/legacy/vdc/LEGACY_VDC_TASK_PROGRESS_12.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`, `docs/vdc/VDC_DOMAIN_TODO.md`
Last updated: 2026-10-14

> 按 C14 从进展日志迁出的最旧条目段；条目逐字保留、未编辑，顺序与源文件相同。
> 本快照为历史证据冻结件，当前状态以域 TODO 为准。

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
