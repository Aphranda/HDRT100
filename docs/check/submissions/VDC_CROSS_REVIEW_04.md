# 核验提交单：VDC Core1 同步邮箱直接发布

Status: Draft
Domain: VDC
Canonical: `docs/check/submissions/VDC_CROSS_REVIEW_04.md`
Related: `docs/vdc/VDC_DOMAIN_ARCHITECTURE.md`, `docs/check/DOCS_REGISTRY.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-09-17

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

## 最终核验结论

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
