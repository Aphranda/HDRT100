# 核验提交单：VDC Core1 同步邮箱直接发布

Status: Draft
Domain: VDC
Canonical: `docs/check/submissions/VDC_CROSS_REVIEW_04.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-09-19

## 提交内容

提交 `VDC-PRIORITY-01` pending：独立 typed body、output-ns 算术区间、STOP-only
运行代际和 Core1 origin provider；TDMA owner 保持 PIO/DMA 与双缓冲退休所有权。

## 偏差声明

同步单位和事件身份明确，不代表 GPIO 误差已校准。编码/offer、物理提交、选中、
运输、消费和控制分别证明；当前条款不授予 DCO、单圈期限或锁相。普通分片路线
的历史参考/ACK 结果不能替代本路径证据。

## Alternatives considered

- 通过普通 Core0 overlay 工位准备每次同步记录：不满足特等席确定性边界。
- Core1 复用独立 origin exchange 双缓冲：采用，保留硬件 selection 退休规则。
- 将区间取中点或用 RATE 坐标冒充 VDC 时间：不采用，保留 committed 模型和上下界。

## 交叉审核记录（C11）

- 审核方：`p0_root_review`，独立于源码与文档作者。
- 审核方式：只读源码/负测/资源复核，契约、登记和顶层映射复核。
- 初次 owner 审核指出普通工位等待、注册 STOP guard、同步 fallback 类型隔离
  三项问题；已修正并由独立方复核，原报告保留。
- 本次契约结论：ACCEPT_WITH_DEVIATION，同意 `VDC-PRIORITY-01` pending 登记，
  不批准 active；已纠正 mailbox event sequence 与 transport carrier sequence
  的文字歧义。审核日期 2026-09-17，原件为 `c11-review.json`。
- 证据根：`out/HardwareAcceptance/20260917/dpll-priority-tx-r1/`。

## 接收交接增补（v2 pending）

`VDC-PRIORITY-01` v2 增加 STOP 注册固定接收入口、发布后同步交接、承载帧完整
序号回绕后的 epoch、退休及重复/冲突语义。IRQ 只执行定长解码和留存，不执行
PI 或取得 PIO/DMA 所有权；原轮询诊断与新入口分离。热链迁入主 RAM 不替代
实测预算或单圈期限证明。

独立方 `p0_root_review` 完成源码、负测、A/B 资源及 C11 复核，结论
`APPROVE_PENDING_REGISTRATION`；修正“载荷回绕”为“承载帧完整序号回绕”。
仍不批准 active、DCO 或锁相。原件在
`out/HardwareAcceptance/20260917/dpll-priority-rx-direct-r1/c11-review.json`，
代码/资源/实板审核分别见同目录 `code-review.json`、`resource-review.json`、
`hardware-review.json`。IRQ 候选预算仍未满足，P3 严格失败保留于 Task Progress。

## 精确匹配增补（v3 pending）

`VDC-PRIORITY-01` v3 增加 Core1 live handoff、源事件直接索引、STOP 请求会话
绑定、实际 committed DCO 投影和带暂定正向 CS delay 的残差区间。匹配器不发
DCO 命令或 ACK，不授予物理精度与单圈期限。已有 reverse-DATA 矩阵只在方向
来源明确时转置；中继驻留和端点校准留至后续精度任务。

独立方 `p0_root_review` 对照源码、负测、资源与三份契约文档复核，指出并闭合
首次匹配前未锁存 session 的实现缺口，以及 RX 交接暂不可用与 epoch 退休的
文字差异。C11 结论 `APPROVE_PENDING_REGISTRATION`，仍不批准 active。
原件为 `out/HardwareAcceptance/20260917/dpll-priority-match-r1/c11-review.json`；
代码及资源复核见同目录 `code-review.json`、`resource-review.json`。本次硬件
判定由 Task Progress 引用原始专项，不以本增补替代。

## 本地频率控制增补（v4 pending）

`VDC-PRIORITY-01` v4 增加 STOP 显式互斥 typed-follow 模式、Core1 私有 fresh
匹配票据、两个实际输出区间形成的 ppb 估计，以及 committed-model guard 内再次
核对生命周期和实际本地 DCO 后的连续调频。NO1 的正常模型修订不伪造远端 token；
自身模型更新后重建本地估计基线，旧 RefMem/remote command 不同时控制 DCO。

独立方 `priority_review` 完成只读源码、契约/登记/顶层及 ARM linked 资源复核，
C11 结论 `APPROVE_PENDING_CONTRACT_EXTENSION`，同意 v4 保持 pending，不批准 active。
审查日期 2026-09-17；原件为
`out/HardwareAcceptance/20260917/dpll-priority-follow-r1/c11-review.json`，
源码和资源分别见同目录 `code-review.json` 与 `resource-review.json`。
死区零调整与 sanity 总限幅导致的零步长须分别解释；DCO 更新不代表 GPIO 模型采用、
相位捕获、ACK、单圈期限或锁相。软件及实板结果由 Task Progress 留证。

## NO1 共钟映射增补（v7 pending）

在既有相关本地差分和有限档位基线上，NO1 的 typed 新事件增加有限共钟约束
求交；连续坐标的开上界、真实 Domain 投影、每帧独立 latch 和旧无状态准入
保留。只有最终复验与编码成功才能提交缓存，矛盾退休 typed generation。
同事件编码不变，普通 TDMA 继续；不改变本地量化余量或执行器提交语义。

origin 原生 schema 复用维护池，从 Core1 确认的空 SYNC 开始连续记录全部成功
贡献，并保存最后记录对应缓存；从板旧 schema、STOP ACK、读取 lease 与 CRC
保持。实现和布局事实源为 canonical 条款及 `vdc_priority_trace.h`。

独立方 `priority_review` 于 2026-09-17 复核三份契约文档及生产实现，结论
`PASS_C11_VDC_PRIORITY_01_V7_PENDING`，不批准 active。证据为
`out/HardwareAcceptance/20260917/dpll-priority-mapping-r1/c11-review.json`，
同目录资源报告覆盖真实 TDMA→TX→mapped projector 和 origin trace 深调用链。
软件、同源码 P3 和原生专项的结果由 Task Progress 记录，不据映射区间收窄
宣称单圈期限、物理精度或锁相。

## 早期基线质量增补（v9 pending）

`VDC-PRIORITY-01` v9 在首个估计档位消耗前，允许同模型与完整绑定下的
有效样本有界替换基线；完整远端间隔受窗口限制，新宽度至少减半，次数由
代码符号限定。每次使用新事件自己的真实端点；没有合格候选时沿用原基线，
不增加固定质量前置。已消耗档位不退还，正常重建、取消和实际应用清理次数。
额外间隔界属于参考坐标，不是缺帧情况下的墙钟期限。新增诊断理由保留
既有编号和快照布局，最近状态不证明替换历史，STOP 可以覆盖该理由。

独立方 `p0_root_review` 于 2026-09-17 对照实现、测试、域文档、登记表及顶层
复核，结论 `APPROVE_V9_PENDING_CONTRACT_EVOLUTION`；登记仍为 pending，
不批准 active。原件为
`out/HardwareAcceptance/20260917/dpll-baseline-quality-r1/design-review/c11-v9-independent-review.json`。
同目录 `implementation-independent-review.json` 核对源文件及采集器范围，
资源报告位于该证据根的 `review/resource-review.json`；硬件与物理结论由
Task Progress 记录，不由软件策略或登记状态推定锁相。

## 基线配置与显式持久化增补（v10 pending）

`VDC-PRIORITY-01` v10 增加早期基线次数/窗口的 STOP-only SCPI 整对配置。
Core0 单字原子发布，Core1 只在新的有效 FOLLOW 绑定锁存；模型更新和基线重建
保持本绑定配置。STOP 退休旧 FOLLOW 请求，后续需要新请求而非仅 ARM。
工厂 RAM 默认、持久化 SRAM 召回和显式 Flash 保存分别定义；维护 guard 排斥
ARM/配置，FlashTransaction 保持 Core0 owner 和 Core1 park，实时路径不写 Flash。
Product Config 保留旧前缀/CRC 域并只读启动迁移，保存失败保留旧 journal 记录；
旧 PI、角色和身份字段不被新增配置覆盖。注册仍为 pending，不授予锁相。

独立方 `p0_root_review` 于 2026-09-17 对照源码、测试、域文档、登记表与顶层
完成 C11，结论 `ACCEPT_V10_PENDING_CONTRACT_SCOPE`，全部已有登记状态不变。
原件为 `out/HardwareAcceptance/20260917/dpll-scpi-baseline-r1/control-review/c11-v10-independent-review.json`。
实际构建、四板 P3、重启保存/恢复及原生结果由 `VDC_TASK_PROGRESS.md` 记录；
不以条款审核替代硬件验收。软件专项、资源和 HIL 工具复核原件分别位于
该证据根的 `control-review/` 与 `review/`。

## 本地相位与输出补偿增补（v11 pending）

本地相位与输出补偿增补以 `VDC-PRIORITY-01` v11 pending 登记：默认关闭的
STOP 配置相位模式、最近零边界有限平移、Domain 实际提交与最终模型发布分层确认，
仅精确成功平移链授权同 rate epoch 的频率坐标归一化；频率变化、未知模型和
STOP 取消。输出独立有符号 delay 的 SCPI 与 Flash 保存不重复加入 MATCH 链路 delay，
不将配置持久化等同于实际 RUN 输出采用。软件、HIL 工具与资源独审原件位于
`out/HardwareAcceptance/20260917/dpll-local-phase-r1/control-review/`、
`resource-review-strict/`。当前源码 P3-r3 引用及分级独审通过；delay-hil-r2
逐命令保存/重启/恢复独审通过；native-phase-r5 的有限原生前缀、本地相位
真实提交、后续频率决定及末态模型独审通过，见 `control-review/` 与
`parser-review/` 对应报告和进度 028。首次 P3、越界解析及上下文恢复失败均
保留；r4→r5 成功与完整配置恢复相关，未证明某一个 BINDING 子条件的因果。
实际 RUN 输出 consumer、百纳秒物理锁相与完整频率归一化链仍未获授权声明。
C11 独立结论为 `ACCEPT_V11_PENDING_CONTRACT_SCOPE`，原件为
`out/HardwareAcceptance/20260917/dpll-local-phase-r1/control-review/c11-v11-final-independent-review.json`；
登记保持 pending，配置与调试通路验收不提升为产品锁相或 VDC 发布完成。

ACCEPT_WITH_DEVIATION。P3、资源和 typed 实板专项的实际结果只记录在 VDC Task Progress，
不能以 pending 登记代替验收。

## 共同边沿数学准备增补（v12 pending）

`VDC-PRIORITY-01` v12 增加单快照精确整数逆映射及共同严格未来网格准备。
反解返回最早合法交点，负频率平台取首点，保留正频率离散超越量；独立输出
delay 只在本地物理轴加入一次。失败保持输出，网格序号不被纯函数提交，
本地 ns 不是原始 TIMER1 tick，也不构成 FIFO 不变性或硬件执行证明。

DPLL/VDC/SYNC 同属特等席；IN/OUT 保持统一 SYNC_IO owner，PIO 状态机直接
执行边沿，Core1 只做有界准备和提交。资源交接、运行时钟锚、未提交后缀更新、
迟到/断流/STOP 及物理精度均留待独立验收，登记保持 pending。

源码与最终生产测试独审为 `PASS_MATHEMATICAL_PREPARATION_ONLY`，原件
`out/HardwareAcceptance/20260917/dpll-run-output-r1/control-review/production-math-review.json`。
独立审核方 `p0_root_review` 的最终结论为 `ACCEPT_V12_PENDING_CONTRACT_SCOPE`，
原件为该证据根的 `control-review/c11-v12-final-independent-review.json`。
构建、资源和同源码四板快速验收事实见进度 029；
node-sequence 分支只用于择项借鉴，旧分支验收不能放行当前源码。

## 有限持续 PIO 输出增补（v13 pending）

`VDC-PRIORITY-01` v13 增加 SYNC_IO scheduled capability 的 STOP 资源预留、
完整客户端跨核交接、START 与当前模型身份准入、有限 DMA 块及不可改写前缀。
未来本地 ns 保留 bridge 量化与原始拍区间，首次 enable 另保留公共偏移区间；
计划 tick、源缓冲退休和物理执行不混同。取消/断流/到期先置安全低态、异步
abort，确认后才释放。预留可能使维护态高电平安全下降，不保证绝对无电平变化。

独立方 `p0_root_review` 审核后端、SYNC 排他入口、VDC 客户端生命周期和稳定文档，
结论为 `ACCEPT_V13_PENDING_STABLE_SEMANTICS`。原件为
`out/HardwareAcceptance/20260917/dpll-run-executor-r1/runtime-review/c11-v13-stable-independent-review.json`。
该审核不冒充由其本人编写的 bridge 数学独审，数学与硬件证据分别由 Task Progress
引用；保持 pending，不授予产品 RUN、单圈期限、百纳秒同步或 VDC 发布完成。

最终九文档与实施事实复核结论为
`ACCEPT_V13_PENDING_CONTRACT_AND_FINITE_10MS_EXECUTION_SCOPE`，原件
`out/HardwareAcceptance/20260917/dpll-run-executor-r1/runtime-review/c11-v13-final-independent-review.json`。
审核方仍为独立的 `p0_root_review`；桥接数学由非作者 `event_core` 另行确认。
该结论保留首次启动与较高频率断流失败，只接受已验证的有限低频输出范围。

## 有限时间轴与配置增补（v14 pending）

本次增加 STOP-only `OUTPut:TIMing` 完整三元组、PREPARE 锁存和显式 Flash 保存，
旧持久化记录补默认值并保留 delay、PI、角色、基线与身份。准备阶段联合检查
输出周期、每块容量和静态调度周期；运行使用同一次初始映射，但规划及准入仍
读取新鲜 raw 时间。未提交后缀分批计算，模型改变使部分/完整缓存失效；已提交
前缀不改写，STOP 或调度周期改变取消请求。扩展 DMA 块容量不等于扩展退休后
FIFO 执行余量，固定映射不消除真实模型更新造成的未来周期变化。

登记保持 pending，不授予连续输出、候选 WCET、百纳秒同步或产品 VDC 发布。
独立复核与实板证据根为
`out/HardwareAcceptance/20260918/dpll-run-timeline-r1/`。独立方 `p0_root_review`
已复核生产代码、双槽实际链接、配置/时间轴负测和稳定文档，C11 结论为
`ACCEPT_V14_PENDING_SEMANTICS_NO_PRODUCT_RELEASE_CLAIM`，原件位于上述根的
`design-review/c11-v14-semantic-independent-r1.json`。固定范围 P3 与输出专项
仍分别判定，实施事实及最终复核见 Task Progress，不由本条授予物理验收通过。

## 相位中点控制估计增补（v17 pending）

本次相位 acquisition 使用已准入 residual 区间的向零取整中点，反向限幅作为
控制量；原始区间、同事件绑定、频率优先、有限更新间隔、完整模型回执及累计
平移归一化仍保留。中点是控制估计，不能替代真实时间戳区间或物理精度。
历史最近端点相位原件继续按旧规则解码，新策略由独立原生 schema 标识。

独立方 `origin_bracket_audit` 初审指出旧 decoder 会拒绝合法中点修正，要求
策略版本化及真实 producer 到 decoder 联通回归。该问题已补实现及正负测。
最终独立结论为 `ACCEPT_V17_PENDING_SEMANTICS_AND_FINITE_DEBUG_SLICE_NO_PRODUCT_LOCK_CLAIM`，
无剩余阻断发现；核验主机测试、最终源码 P3、四轮原生解码与波形 hash、STOP/参数恢复。
原件为下述证据根的 `c11-v17-independent-review.json`。有限近邻边沿窗口不替代同 ordinal、
持续实际输出、独立路径校准或 VDC 最终发布，RAM 与既有资源文本失败继续保留。
证据根为 `out/HardwareAcceptance/20260918/dpll-phase-center-r1/`，登记保持 pending。

## 原池全时段汇总增补（v18 pending）

本次增加独立诊断汇总 schema，复用原维护池；service 从观察到 ring 运行开始分段，
保留成功原始极值与拒绝/取消/保持计数，空段、计数复位、缺口和异常显式标记。
时钟回退冻结最后真实有序端点，首次时钟无效不伪造记录；STOP 部分段、原生导出
lease/CRC 与严格 decoder 保留。汇总输入成功不等于实际 GPIO 输出或锁相。

独立方 `phase_center_review` 复现并关闭 R1 时钟回退缺陷，结论为批准
`VDC-PRIORITY-01 v18 pending` 诊断切片及对应文档，无剩余阻断级发现。
原件为 `out/HardwareAcceptance/20260918/dpll-summary-trace-r1/independent-review-closed-r2.json`，
两轮原件核验为同目录 `independent-final-evidence-r2.json`；源码提交 `08af197f`。
审核覆盖匹配源码 P3、Release 资源、两轮 RAW 与原生文件、STOP 和恢复。
全段输入完整检查仍为 FAIL；第二轮 NO4 后段超出 ±50 ns，有限后段三从在 ±100 ns。
正式 RAM 门槛及既有资源告警保留，不授予正式锁相、产品 RUN 或 VDC 最终发布资格。

## 显式持续诊断增补（v19 pending，待独立复核）

本次将 RUN duration 和 TDMA TRIAL duration 的零值定义为显式持续诊断，非零值
保持原有限模式；块规划、时钟有序与算术检查、STOP/撤销/会话及资源退休保留。
状态 schema 和 origin 授权版本随语义更新，不提升产品准入。源码与当前四板 P3
及持续外部采样的证据根为 `out/HardwareAcceptance/20260918/dpll-continuous-scope-r1/`。
此条仅提交实施语义供独立复核，登记保持 pending，不构成作者自审批准。

后继 observer 修复取消 `tdma_event_start` 对活参考的任意整次时长限制，使用
通用 API 已支持的最大 epoch 范围；不改变计数器唯一展开、join timeout、
算术/序号拒绝及 STOP。证据根为
`out/HardwareAcceptance/20260918/dpll-observer-continuous-r1/`。
旧有限观测器退休及长时间漂移原件保留；专项结果与未完成门禁以进度日志为准。

## 可配置汇总扩窗增补（v20 pending）

STOP 下显式双参数 ARM 使用 `VDC_PRIORITY_TRACE_SUMMARY_WINDOW_*`；旧单参数
schema 与间隔不变。整倍数、最大跨度及当前 tick 可表示范围在 Core0 准入与
Core1 消费时双重核验，参数随既有 request/ACK 提交。固定 SRAM 池和冻结布局
保持，扩窗不放宽服务缺口门限；饱和、FULL、空段及真实 STOP 边界完整保留。

独立方 `internal_probe_review` 已完成生产代码与主机工具只读审核，未发现新增
阻塞项，允许进入 P3 和四板实测；这不是硬件通过或产品准入结论。
真实 libscpi 独立复跑及组合 host 测试证据见当前 Task Progress。
同一独立方已复核稳定文档、登记、当前源码指纹、P3 引用 hash 和 r1–r3 原生
CRC/解码、STOP/RELEASE/参数恢复，同意 v20 保持 pending 登记。
原件为 `out/HardwareAcceptance/20260919/window-c11-review.json`。C14 超限已通过
逐字归档及索引闭包修复，两个文档门禁重新通过；r2/r3 首段计数重置及严格失败
完整保留。该报告未审核尚在运行的 r4，不提升产品 RUN 或物理锁相资格。

r4 完成后，独立方再次核验四板原生/分页 CRC、解码及十个分钟窗口、超过六百秒
的成功跨度、全程零运行命令、STOP/RELEASE 与参数恢复。补充原件为同根
`window-r4-independent-review.json`；严格 false 及三从内部区间极值扩大保留，
仅接受内部连续跟随证据，不授予 GPIO 边沿精度。

## 完整快照刷新增补（VDC-PUBLICATION-01 v1 pending）

本地 rate/phase 提交不推进旧 DPLL 证据序号，管理面和 RefMem 旧去重可能永久
漏掉实际 DCO 更新。内部快照新增同次稳定偶数 guard 的 `publication_revision`，
消费者只确认成功复制的版本；失败不确认，零值回绕有效。原 wire、CRC、证据
序号、clock 模型与 quality 不被替换，RefMem 每 beat 仍至多一份向量。

真实 Domain→publisher→Core0/RefMem consumer 回归已对旧实现复现五项漏更新，
新实现及布局 golden 通过；实板修复前旧 DCO 读回也已保存。独立审核入口及证据
为 `out/HardwareAcceptance/20260919/publication-review-request.json`。
独立方 `internal_probe_review` 已同意本切片代码/契约及 pending 登记，无阻断项；
复跑新增与相邻测试通过，核对 P3 r2 指纹及全部引用 hash，保留质量告警。
原件为同根 `publication-independent-review.json`。该审核不含后续 STOP 字段对照
与物理输出专项，不授予正式发布或持续精度资格。

同一独立方后续核验四板 STOP 公共 DCO 字段及模型/Core0 其余可见字段、内部
原生 CRC、示波器 RAW 与 STOP/RELEASE/恢复，原件为同根
`publication-final-evidence-review.json`。支持限定的发布刷新修复；保留 START 前
超时、示波器漏窗和 NO1 初始化计数重置，不批准严格长时或正式锁相完成。

## 汇总代际基线修复（VDC-PRIORITY-01 v21 pending）

新 capture 早于首个新 TX owner 调用时，旧代拒绝计数不得作为当前代基线。
尚无当前代基线时，首次当前代快照按 TX owner 的零初始化累计拒绝；开窗已有
当前代基线则只累计后续增量。同代回退、读取失败和已采当前代
后再次异代仍保留异常。不忽略整个首段，不修改 schema、布局、池大小或解码门禁。
独立方 `internal_probe_review` 已复核真实 owner 正负对照、边界条件及文档，
同意 v21 pending，未发现阻断项；旧版本负对照复现八个预期失败，新增十四项
通过（本轮测试快照）。原件为
`out/HardwareAcceptance/20260919/internal-generation-r1/independent-review.json`。
后续已核验匹配源码 P3 的指纹及引用 hash，以及短窗/长窗原生 CRC、分钟离线
交集、STOP/RELEASE/末态恢复；原件为同根 `p3-independent-review.json`、
`capture-60s-independent-review.json`、`capture-600s-independent-review.json`。
短窗严格通过，长窗覆盖完整且持续成功跨度超过六百秒，但 START 前诊断 timeout
引入的首成功间隔超限、恢复队列错误及原始 FAIL 均保留，不授予严格整轮或物理锁相。
