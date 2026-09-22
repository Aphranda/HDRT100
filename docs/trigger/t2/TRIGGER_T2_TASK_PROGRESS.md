# T2 预约触发任务进度

Status: Draft
Domain: TRIGGER
Canonical: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`
Related: `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md`, `docs/trigger/t2/TRIGGER_T2_TODO.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-09-22

## 文档接口

本文记录真实执行、来源和证据，不冻结契约或代替 [TODO](TRIGGER_T2_TODO.md) 的任务状态；设计见 [Architecture](TRIGGER_T2_ARCHITECTURE.md)。条目使用 `T2-PROGRESS-YYYYMMDD-NNN`，最新在上；按 C14/C15 保留失败、历史和归档索引。

## 当前 checkpoint

### T2-PROGRESS-20260922-006：四槽部署抽象、SWITCH 复用与动态映射事务

- TODO task ID：T2-DEPLOY-001、T2-DEPLOY-002。用户明确当前接线：NO1 COUNTER=IN1；NO2 SP8T=OUT1–OUT3；NO3 VNA=OUT4 TRIG、IN1 READY；NO4 SP2T/HV=OUT1。NO2/NO4 归一为 SWITCH 类型，设备名只描述 profile；不同板卡的 OUT1 是各自本地命名空间。
- 角色模型：四个槽身份为 COUNTER/SWITCH_A/GATEWAY/SWITCH_B，运行类型为 COUNTER/SWITCH/GATEWAY。两个 SWITCH 使用同一校验/路由，输出宽度由非零四位 mask 决定；当前分别为 0x7/0x1。GATEWAY 要求一入一出和 REQUIRED_EDGE，当前 IN1/OUT4；其他 `NONE_UNOBSERVED` 不是反馈完成。
- 实现：`trigger_deployment.h` 提供完整表验证、规范 100-byte LE CRC、本地 UID/slot resolve 和类型映射；`trigger_deployment_service.c` 在 STOP+Trigger IDLE 下 STAGE/ACTIVATE/CLEAR，active/staged 分离、旧代不可复活。PLAN 先验证本地槽，再消费共享 grid exact-q 计划，并在返回前复核 UID、slot、ring config 与 schedule；只读规划，不申请资源或输出。
- SCPI：`TRIG:RES:DEP:STAG/ACT/CLE/DEP?/PLAN?` 使用严格整数解析；STAGE 一次提交四行，PLAN 成功返回本地路由、角色类型和共享目标/本地 deadline。四板依次 ACTIVATE 是显式安装，不宣称原子分布式提交。
- 软件证据：部署纯核、owner/libscpi、grid owner 联合 151 项通过；相邻 VDC suites 325 项通过。独立 `/root/t2_triplet_review` 结论 PASS，限定部署映射与无输出规划。首次 P3 `p3-t2-deployment-r1` 在编译前因 SDK include 规则失败；改为 `pico.h` 后部署定向回归 73 项通过。修正版 r2 完成 build/OTA/校准，但 NO3 的 TDMA provisional 命令单次超时导致 STOP handoff 缺 board records，未进入 DPLL；失败与清理原件保留。r3 使用同包和已验证 OTA resume，不重复构建/刷写，正在执行。
- 投入边界：本片价值 4/5，完成原映射→交换 SWITCH_A/B 的 UID/slot→恢复原映射，核对错误 CRC、旧代、错目标和 CLEAR 后恢复即停止。真实 GPIO 驱动、READY 极性、跨板预约运输和设备完成不在本片冒充完成。
- 当前源码 P3：修正版 r3 复用 r2 已验证包/OTA，跳过重复构建刷写；quick 分级 PASS_WITH_WARNINGS（33 INFO / 18 WARN / 0 ERROR / 0 FATAL），DPLL 60 s 外部最大相位差 70.702232 ns，验收总耗时 188.700 s 为 PASS。源码 SHA `b7ca688c934213c94ec17f4e4bccddefef1716e0752cbbdb5ef4f86db64ba34e`，包 SHA `4845a81223f265c4c76728cd77acac2b789f86287425724d5461db8ce6fc9c55`，1450 sources，`check-staged` 通过。r2 NO3 单次超时及缺 record 失败原件未覆盖。
- 四板部署专项：`out/t2-deployment-20260922/hil-r1/summary.json` 在 28.079 s 内完成三代。generation 1 原映射为 NO2 mask 0x7 / NO4 mask 0x1；generation 2 交换两者 UID/TDMA slot 后，NO2 获得一位 SWITCH 配置、NO4 获得三位 SWITCH 配置，角色类型均保持 SWITCH；generation 3 恢复原映射。每代四板均完成同表 STAGE 读回、顺序 ACTIVATE 和本地 exact-q 路由计划，非目标角色返回 NOT_LOCAL；坏 CRC STAGE 保留拒绝回执，其后正常 STAGE 的 active 仍与此前相同；坏 CRC ACT 有直接不变读回，旧 generation 不再授权。一次 ACT 暂态返回 NOT_IDLE 后有界重试成功，原拒绝保留，不称全程零拒绝。
- 恢复与边界：专项共 289 条有界 STOP/身份/部署/grid 命令，不含 GPIO/ARM/FIRE/RUN；最终四板 deployment/grid configured=0，session/load mask 恢复，稳定 STOP/config ACK，零 cleanup error。last_generation=3 按防旧代设计保留，不宣称逐位回滚。当前部署 active 只证明本地路由安装，不是跨板原子提交、远端在线、IO claim、READY 或实际动作完成。
- 结论：T2-DEPLOY-001/002 关闭。证据目录为 `out/HardwareAcceptance/20260922/p3-t2-deployment-r*/` 与 `out/t2-deployment-20260922/`。下一 gate 是 T2-DEPLOY-003 的真实跨板运输与目标板动作；动态切换的在飞预约/输出退休仍由 T2-DEPLOY-004 承接。

### T2-PROGRESS-20260922-005：共享 VDC 格配置与指定 q 的本地 deadline 规划

- TODO task ID：T2-CLOCK-GRID-001、T2-CLOCK-001。用户明确 T2 应在共享 VDC 时间片内规划动作并要求继续。本片将共同 A/P/N/epoch/generation、绝对 q 与本板 committed DCO/TIMER1 反解接线；只作 Core0 STOP 暂存和无输出规划，不写 TriggerAO 活动态，不发布 READY/ARM，不扩充 Core1/wire 资源。以下数值为本片快照，非产品能力事实源。
- 分支借鉴：沿先前已核来源的 `trigger_reservation_clock.c` 采用“固定目标→真实逆映射→lead→同板身份复核”；未照搬依赖 formal LOCKED 大向量的捕获门禁。发现当前周期 RUN planner 会跳过迟到 ordinal，因此指定 q 路径直接调用 Domain inverse，明确返回 LATE，不沿用自动选格逻辑。
- 新纯数学核 `trigger_time_grid.h`：绝对序号拆周期/余数受检计算，末格保留，正负 rate、非整除、模型起点夹持、量化、提前量/未来范围和相邻格可区分性。`trigger_grid_preview.c` 复用已有 STOP metadata 发布入口，绑定每板 session/ring/role/local epoch-run/slot/schedule；共同 epoch 为实验标签，不自动证明参考源资格。CLEAR 保留代际上界，旧代不能复活。
- 独立审核修正：补充实际 ring schedule CRC 与 committed DCO schedule CRC 一致性，防止把新环配置配旧模型；数学核补充早于模型有效起点的目标拒绝，避免 inverse 夹持冒充原目标。独立 `/root/t2_triplet_review` 对代码/边界复核 PASS。
- 软件验证：数学与真实 owner/libscpi 联合 74 项通过，独立有理数/二分逆映射覆盖 850+ 样本；相邻 VDC fixed/run suites 与 owner/parser 合计 393 项通过（有测试重叠，不相加为总数）。严格 u64 参数不经 double，配置/清理/失败返回 schema+result，配置查询成功 16 字段，规划成功 34 字段。命令为 `TRIG:RES:GRID:CONF`、`GRID?`、`GRID:CLE`、`GRID:PLAN?`；具体字段事实源为 `scpi_trigger_grid_commands.inc`。
- 投入：EXECUTE，价值 4/5；实现后预计再用 20–35 分钟闭合四板读回及验收。退出点为共同目标一致、各板本地 deadline 独立可复算、拒绝保留原目标、无输出与清理读回；停止扩展新观测或重复极限实验。
- 进行中：独立目录 Release/P3 `out/build/t2-grid-r1/`、`out/HardwareAcceptance/20260922/p3-t2-grid-r1/`，专项原件 `out/t2-grid-20260922/`。不再覆盖上一片构建包，不以同 build ID 替代新源码/包指纹。
- 首轮实测失败及修复：r1 P3 quick 分级通过，但 `hil-r1/summary.json` 在 NO1 GRID CONF 连续读回 MODEL(35)，未装载配置且 session/mask 已恢复。实际 STOP/unconfigure 会将 runtime local_slot/schedule 清零；首轮单测错误地保留了 RUN 身份，导致身份检查拒绝正常 STOP。已改为从 `tdma_runtime_owner_get_staged_ring_config()` 读取真实保留配置，前后确认 STOP/config ACK；继续对比模型与 staged 身份，不删除检查。真实 STOP 清零、staged 缺失/禁用/错代/配置变动/读取竞态及无重配置副作用的 owner/parser 测试 78 项通过；纠正源码在独立 `t2-grid-r2` 构建及 `p3-t2-grid-r2` 验收，保留 r1 全部原件。
- 修正版构建与 P3：独立 Release build `20260922052040` 通过，新增静态状态 68 字节，无 Core1 工作扩容；源码 SHA `0fca320ed0e5188a656164d9efda0e124eddb31586a009e8a7fa74b67a5de533`，包 SHA `b403c92d142f9acded09ff695f59c14e44f194864cbf0d4984b637dafde59ebe`。`p3-t2-grid-r2` quick 分级 PASS_WITH_WARNINGS（32 INFO / 21 WARN / 0 ERROR / 0 FATAL）；coded marker/TDMA 原始严格失败与总耗时 497.092 s 超过 450 s 的 budget ERROR 仍保留，不能称严格全绿。DPLL 60 s 联合观测外部最大相位差 31.840751 ns，验收凭证按修正版指纹签发。
- 四板专项：`out/t2-grid-20260922/hil-r2/summary.json` 成功，11.083536 s。共同配置 A=0、P=1000000 ns、N=1000、实验 epoch=20260922、generation=1；q=767917000、767917001、767918000 在四板分别形成一致 VDC 目标 767917000000、767917001000、767918000000 ns。每板独立的真实 committed 模型由 host 大整数 forward/二分 inverse 复算，TIMER1 deadline 与固件一致，投影量化超前 0–3 ns，无输出。远期目标用于避免串口编排消耗 lead，不证明该 horizon 内保持时钟精度或已预留预约。
- 每板七类负测通过：保留格返回 RESERVED；错 generation 返回 GENERATION；超 horizon 返回 HORIZON；过去目标返回 LATE；同代修改 anchor 被拒且配置未变；CLEAR 后返回 UNCONFIGURED；原代不能重新配置。共同标签仅证明配置一致，不代表跨板正式参考身份，个别板的 local epoch/run/token 和 raw deadline 无相等要求。
- 恢复/证据：四板网格 configured=0，保留消费过的 last_generation=1（防旧代复活，非逐字回滚）；session/load mask 恢复、稳定 STOP/config ACK 读回，零 cleanup error。`hil-r2/session-prepared.json` 不可变，`session.json` 另记恢复；固件包及 receipt 独立复制，原命令、重试、模型、配置/数据哈希可复核。r1 失败与恢复原件完整保留，未增加其他观测矩阵。
- 文档检查器测试 38 项通过，最终文档/P3 staged/差异门禁记录在 `out/t2-grid-20260922/final-gates/`。仅关闭无输出共享格切片；T2-CLOCK-001 仍为 IN PROGRESS，活动部署、共同参考认证、PREPARE/READY/fence、真实装载与每节点反馈继续由既定任务承接。

### T2-PROGRESS-20260922-004：两端同步标记、整窗细分和末格留空

- TODO task ID：T2-CLOCK-PROBE-002。用户要求铺满两次同步之间的间隔，末尾一次不触发，为下一同步留余量；随后明确选用 1 us 细分 / 1 ms 周期的现有容量路径。以下数字为本片配置及验证快照，非产品时序事实源。
- 定义：q0 为 200 ns 标记；q1…998 为 100 ns 窄脉冲；q999 留空；q1000 为下一标记。总共 1000 个脉冲，最后两次上升沿间隔 2 us，标称末次低电平余量 1.9 us。两个标记在同一有限预装字流内，不采用主机分发或逐脉冲软件输出；仍为 STOP 本地维护窗口，未接入 live RUN 或共同 VDC anchor。
- 实现：`sync_io_rate_schedule_generate_window()` 按商/余数计算累计格位置，尾双格间隔校验范围，末 marker 高宽进入总时长；复用已有 finite owner、SM/DMA/workspace。独立 mode 2 `SYST:VDC:OUTP:SUBD:WIND 1000000,1000,100,200,4` 及查询/STOP；原 FIXED 32 字段与 SUBDIV 37 字段接口保持。窗口查询仍为独立 mode 的 37 字段，窄脉冲数 N−2、总脉冲数 N，period*N 为标称同步周期。
- 软件：首轮负测把恰等于 UINT32_MAX 的可表示尾间隔误当越界，已将测试 rate 改为实际越界值；生产算法未为该测试改变。累计边沿/容量/尾marker/取消、owner/SCPI 三模式互斥和既有 RUN 合计 350 项通过；命令及原始失败说明见 `out/out1-window-20260922/software-evidence.json`。独立审核 `/root/t2_triplet_review` 为 PASS，物理精度仍待板上证据。
- 构建：Release 通过，静态 BSS 末址未变。本轮增量构建复用 `out/build/out1-subdivision-r1/`，导致上一版包被替换，而 build ID 未变化；旧报告/RAW 保留，旧包 SHA 在该构建路径不再可重验，此缺口显式记录。本轮依靠新源码指纹和新包 SHA 绑定验收，不以相同 build ID 冒充同一二进制；后续构建需独立目录或事先保全旧包。
- 投入评估：EXECUTE，价值 4/5，预计 20–40 分钟完成接口、回归及硬件；四板各一整窗证明数量、累计误差、尾部留空/低电平余量后停止扩展，恢复台架。100 ns 铺满需要超出现有字流容量，本轮按用户选择不扩容，不增加 Core1 席位。
- 当前源码 P3：`out/HardwareAcceptance/20260922/p3-out1-window-r1/` quick 分级 PASS_WITH_WARNINGS，30 INFO / 24 WARN / 0 ERROR / 0 FATAL；原始 coded marker、SCK 训练/重装行选择、TDMA 严格闭环未全绿，见 `diagnostic.json`，不得以分级结果覆盖原失败。DPLL 60 s 联合观测最大外部相位差 30.773659 ns。验收耗时 394.541 s，时间预算 WARN，未超过 ERROR 门限；未重复完整验收。
- 当前指纹：源码 `1b985797f3be57e14691aac5ccd469007e10856ca31fa9690a2631318ab5c97a`，包 SHA `c343afc36432ee52f82f2eed1a06e03de5e89b337713f14acf04f501e1706877`；build ID 仍为 `20260922023612`。新包和本轮凭证另存 `out/out1-window-20260922/r1/firmware.pkg`、`receipt.json`，与新指纹绑定；`check-staged` 通过。
- 整窗示波器实测：四板各一次，每板恰 1000 个完整升降沿、2 宽标记/998 窄脉冲，q999 无边沿；marker 间隔为 1000002.608633 / 1000003.297053 / 1000003.877936 / 1000002.352576 ns。末次上升沿间隔 1999.568775–2000.383872 ns，末低电平余量 1897.299007–1899.079329 ns。相对请求网格最大相邻误差 4.000000 ns、累计误差 4.526489 ns；相对模型编码最大相邻误差 1.955048 ns、累计误差 6.352576 ns。两口径均 INFO，未触发 ±100 ns WARN / ±1 us ERROR；NO4 的累计编码量化差保留，不与物理请求误差混写。
- 证据与恢复：`r1/verified-summary.json` 关联四份 manifest、16 个 RAW 块及输入哈希；`r1/window-and-guard.png` 展示首尾波形、q999 空格和整窗累计差。不可变 `r1/prepared.json` 绑定采集，`r1/session.json` 另记恢复，四板 session/load mask、稳定 STOP/config ACK、PIO/DMA 退休和示波器原设置均恢复且读回，无 cleanup error。专项含准备/采集/恢复共 58.203 s，达到整窗退出条件即停止扩展。
- 测量范围：RAW 采样步长 2 ns，差值使用阈值插值；未独立标定示波器时基，不将小数纳秒解释为绝对精度。两个标记来自同一个本地有限批次，未证明跨板共同相位、持续 DPLL 输出后的细分、业务反馈或 T2 实际 latch。
- 文档与下一 gate：文档检查器测试 38 项通过，最终门禁及独立证据复核见 `out/out1-window-20260922/`。本片整窗验证关闭，下一项回到多板部署；持续同步接入和 T2 预约保持独立任务。

### T2-PROGRESS-20260922-003：OUT1 有限细分间隔预验证

- TODO task ID：T2-CLOCK-PROBE-001。用户要求先输出同步标记后跟随可配置间隔的细分脉冲并测精度；本片采用 STOP 维护态有限批次，先验证局部间隔，不将本地任意相位标记冒充四板共同 anchor 或 live RUN 后继细分。
- 实现边界：复用 FIXED 维护 owner、共享 DMA 字流和 PIO persona；宽 marker 只补偿紧随其后的低段，不改变累计上升沿。SUBDivision 使用独立命令与状态，保留旧 FIXED 的参数下限及查询 schema；无新增 Core1 席位、PIO SM、DMA 或 SRAM 字流池。
- 投入评估（本次估计，非产品事实）：EXECUTE，价值 4/5，后续预计 30–60 分钟；两档主要间隔的原始波形、脉冲完整性与误差足够决定是否继续用此通路承载 T2 时间格。停止扩展无关实验矩阵，恢复 STOP 并保留全部失败与原件。
- 软件/审查快照（非产品事实源）：累计边沿独立有理数 oracle、backend 资源/取消、owner/SCPI 与既有 RUN 回归合计 287 项通过；独立 agent `/root/t2_triplet_review` 结论 PASS，修正启动后验证失败仍保留编码 marker ticks 的测试预期。Release build `20260922023612` 通过，map 相对前版仅增加 8 字节 BSS；未新增波形池或 Core1 席位。
- 当前源码 P3：`out/HardwareAcceptance/20260922/p3-out1-subdivision-r2/` 为 PASS_WITH_WARNINGS，33 INFO / 20 WARN / 0 ERROR / 0 FATAL；DPLL 60 s 联合示波器最大相位偏差 34.778496 ns，内部原件及零 RUN 查询证据由该报告保留。staged 指纹 `14e188ee694ee9ff35f46b99ffbab508bf806ae956ec663c3e3d890997f739b8`，1438 sources，凭证 `check-staged` 通过。前次 r1 因 `--reuse-topology` 仅支持 TDMA-only 而在构建/硬件前拒绝，原件保留，不缩减 DPLL 范围。
- P3 实际耗时快照：451.428 s，其中首次 Release 构建 102.289 s；总耗时超过配置的 450 s 预算约 1.428 s，`timing.json` 原始 budget ERROR 保留。上述零 ERROR 指 quick 分级验收告警结果，不能解读为原始严格检查全绿；本次不为消除时间告警再跑完整验收。
- 实测准备与失败：`session-r1.json` 记录脚本未识别 LOAD MASK 复合 ACK 的超时，已按读回恢复后仅修 out 脚本；`session-r2.json` 在 STOP 下建立新会话并读取 Core1 真实 committed model，不写 DCO 数值。首个 `no1-10us-r1/` 批次完成、九个 pulse 计数及资源释放有效，但示波器继承旧 RAW 选择范围导致跨通道点数不一致而导出拒绝，恢复 offset 又受旧冻结记录范围钳制；保留失败，不报告物理精度通过。
- 示波器恢复：显式统一完整 RAW 传输范围，并在原件落盘后用示波器 RUN 重建原时基再 STOP/读回；`scope-recovery-r1/` 确认原设置恢复。`no1-10us-r2/` 又记录上一轮遗留的 `-222` 错误队列，未发新脉冲；采集前单独记录并有界清空旧错误，未将旧错误伪装为本轮设置错误。此后九组全部完成并恢复，无 production 改动或第二轮完整 P3。
- 实测快照（仅本轮，非产品精度事实源）：每组一个 2000 ns marker 和八个 500 ns 窄脉冲，tick 4 ns；四板分别测试 10000 ns 与 100000 ns，NO1 补 10003 ns 非整 tick 档。共九组、每组九个完整上升/下降沿，实际 RAW 采样步长为 1 ns / 2 ns；相对请求的最坏相邻间隔偏差如下：10 us 档四板最大 0.775874 ns，100 us 档最大 1.936321 ns，10003 ns 档 3.258492 ns。全组最大累计偏差 4.058274 ns，全部 INFO，未触发 ±100 ns WARN 或 ±1 us ERROR。10003 ns 档相对独立编码的最大间隔/累计误差 0.596505 ns，体现累计量化而非逐格截断。
- 结论边界：插值后的数值不是亚纳秒绝对精度保证；示波器未提供独立校准时基不确定度，本轮也未做长期/温漂/全参数范围验证。PIO COMPLETE 与示波器实测分开记录，只关闭有限局部间隔实验，未证明持续 DPLL 同步后继细分或四板共同 marker 相位。
- 实测入口（本片调试接口快照）：`SYST:VDC:OUTP:SUBD interval_ns,pulse_high_ns,subdivision_count,marker_high_ns,tick_period_ns`；例如本轮 `10000,500,8,2000,4`。独立查询 `SYST:VDC:OUTP:SUBD?` 返回版本/模式/marker 与原 FIXED 批次状态，`...:STOP` 退休该模式。STOP、有效 session 与当前真实 committed model 是前提，不能将接口用于无条件即时 T2。
- 证据与恢复：`out/out1-subdivision-20260922/summary.json` 汇总九组原件和 manifest 哈希，`waveform-and-error.png` 展示实际波形与累计差；`session-r2.json` 确认四板 session/load mask 恢复、STOP/config ACK 闭合，各组确认输出 PIO/DMA 退休及示波器设置恢复。session 工作 JSON 在恢复时追加读回，原采集引用通过 `session-evidence-resolution.json` 解析到不可变 `session-r2-prepared.json`；恢复出的原始字节 SHA 与全部采集预先记录的 SHA 完全一致，未修改旧 manifest 或波形。
- 投入回收：新增固件只做有限模型率编码和独立维护入口；测量脚本留在本次 out 原件目录，不增加主 tools 治理层。达到两档四板和一个非整 tick 决策点后停止扩展，恢复台架；后续不因新部署工作自动重复本实验。文档三件套及最终门禁证据位于 `out/doc-audit/out1-subdivision-20260922/`，检查器测试 38 项通过。
- 下一 gate：回到 T2-DEPLOY-001/002；共同 anchor、持续同步输出细分、正式 deadline、T2 预约及反馈继续由各自未完成任务承接，不由本片局部精度结果授权。

### T2-PROGRESS-20260922-002：OUT1 不升频与 T2 内部细分时间基准

- TODO task ID：T2-CLOCK-001、T2-FEEDBACK-001。用户明确 OUT1 外部频率无需提高，但要在周期内细分，T2 参考内部细分间隔。本轮只更新三件套设计与待办，未改固件、输出频率、PIO 或运行配置。
- 架构新增 T2-GRID：共同 VDC anchor、周期、细分数及 grid generation，绝对细分序号跨周期连续；从绝对位置生成目标，不逐格累加截断步长。各板用自己的稳定 VDC 模型转换 TIMER1 deadline，不靠每次物理 OUT1 的软件采样重置时钟；OUT1 被业务 TRIG 占用时显式协调 IO owner。
- 明确实际 TRIG/REDY 原始时间与格标签分离；T2 目标和窗口可以用格数描述，但实际边沿不吸附到格。跨格/跨窗口的不确定观测不强行裁定；网格和角色换代隔离旧反馈，已装载前缀保持原冻结时间。细分数、最小间隔与接口仍待 T2-CLOCK-001 选定，不虚构同步/捕获精度。
- 验证原件目录：`out/doc-audit/t2-grid-20260922/`；独立复核与文档门禁按本次差异执行，不启动硬件回归。下一 gate 仍为多板部署，细分算法和反馈归类在预约时钟/执行切片接入。

### T2-PROGRESS-20260922-001：多板槽位部署与 T2 主线建档

- TODO task ID：T2-DOC-001、T2-DEPLOY-001。用户选择下一主线为 T2 预约触发，指定参考 node-sequence，并澄清先将单节点多槽位部署到多板，再开发预约触发。本轮要求先交付标准三件套；未执行固件迁移、槽位装载或硬件操作。
- 仓库快照：当前分支 `wip/tdma-real-flight-processing`，保留已有模型监控实现/凭证的暂存改动及文档工作树，未切分支、合并、提交或推送。此前 VDC 模型监控已交付，实际 GPIO 内部观测另列 VDC-OBS-009，不能重新作为 T2 部署的默认前置。
- 参考来源是本地已存在的远端跟踪引用 `origin/feature/node-sequence-reservation-trigger`，解析 commit 为 `0c913e83f241fa8b40c059792141ae6051a85139`；本轮未 fetch，不宣称它是远端最新。使用 `git show`/`git ls-tree` 只读核对，不执行分支工具或套用其硬件凭证。
- 已核实参考模块：分支内 `components/distributed_refmem/{inc,src}/refmem_sequence_roles.*`、`components/sync_trigger/{inc,src}/trigger_reservation_{config,clock,planner}.*`、`trigger_sequence_{config,service,link}.*`、`components/sync_io/{inc,src}/sync_io_sequence.*`。这些为来源树路径，不表示当前树已迁入。当前有 `refmem_slot_claim`、`refmem_application_model`、`refmem_command`、TriggerAO、VDC 映射及 SYNC_IO RUN 基础，需要核对部署与实际调用接线。
- 分支证据边界：其序列 TODO 明确前三种单板模式已有功能证据，第四种共享 VDC 预约仍未完成；NSEQ-121 只有基础快照/映射/规划切片，NSEQ-122/123 的预约运输、fence 和硬件执行仍待实施。普通 LINK 消息不是完整预约对象，单板角色 resolver 要求目标槽位归 local_board_id，不能直接当多板全局执行器。
- 创建本文、T2 Architecture 和 TODO，并在总索引/Trigger 域入口挂接；跨域总纲仍为 `docs/arch/ARCH_T2_RESERVATION_ARCHITECTURE.md`。用户随后确认首轮四板四槽，交换原建议的 NO3/NO4：NO1=COUNTER、NO2=SP8T、NO3=VNA、NO4=SP2T/HV，并要求身份能动态切换。文档按受控切换部署代、旧资源/预约退休和新角色读回设计，实际 UID/IO 尚待核对，未操作板卡。
- 用户进一步要求网分网关同时处理 TRIG 和 REDY，每个 T2 节点都具备反馈处理功能。架构新增每节点反馈 profile/生命周期，区分预约 READY、设备 REDY 和运输回执；TRIG 与反馈实际时间分开，迟到/重复/无反馈及动态切换隔离进入必验任务。设备反馈的具体极性、前置许可/后置完成语义尚待实物协议确认；未假设全部角色已具备物理反馈，也未将输出提交作为业务完成。
- 独立复核指出还需明确 SP8T/SP2T 稳定到 VNA TRIG 的跨角色时序，已补两开关各自 settle/必要反馈及角色目标偏移；不可预测反馈采用分阶段预约，先取得切换阶段结果，再预约足够远的 TRIG，不以 PREPARE READY 冒充设备稳定，也不依靠临近 deadline 的软件等待阻止已装载输出。
- 本次未新增 registry 契约或修改现有契约状态；来源分支的单板许可及配置不能替代本轮多板规则。
- 验证与复核：首轮检查发现分支专属引用被当作当前文件、空归档索引被拒，已改为来源说明和未归档状态，未改检查器。strict-names、文档回归、检查器测试、pre-commit、逃生门审计及 `check-staged` 通过；已有登记债务 WARN 保留。独立复核的业务时序问题已修正，结论及最终检查原件目录为 `out/doc-audit/t2-triplet-20260922/`。本轮只交付草案文档，不运行硬件 P3；`check-staged` 仅核对工作区已有实现凭证，不表示 T2 已验收。
- 下一 gate：按 T2-DEPLOY-001 明确多板映射与实际 IO，再按 T2-DEPLOY-002 适配最小部署模型；完整预约仍按 M1–M3 推进，不以本次建档宣布功能完成。

## 验证与证据索引

| 类型 | 位置 | 范围 |
|---|---|---|
| 分支参考 | 上述 commit 的 Trigger sequence 三件套（目录 `docs/trigger/sequence/`，文件前缀 `TRIGGER_SEQUENCE_`） | 仅存在于来源树的方案与单板证据，不是本分支文件或多板验收 |
| 当前基础能力 | `docs/vdc/VDC_TASK_PROGRESS.md`、`docs/tdma/TDMA_TASK_PROGRESS.md`、`docs/refmem/REFMEM_TASK_PROGRESS.md` | 各域现有实现与证据；按实际身份和配置复用 |
| 本次文档检查 | `out/doc-audit/t2-triplet-20260922/` | 三件套、导航和文档门禁，不含 T2 硬件结果 |
| OUT1 有限细分 | `out/out1-subdivision-20260922/summary.json` | 四板局部间隔、非整 tick、RAW 原件与恢复；不含共同同步起点 |
| OUT1 整窗与尾部余量 | `out/out1-window-20260922/r1/verified-summary.json` | 四板两端标记、中间细分、末格留空和恢复；不含 live RUN |
| 共享格规划 | `out/t2-grid-20260922/hil-r2/summary.json` | 四板同目标、本地 deadline 独立复算、七类负测/板和恢复；无输出、非共同参考资格证明 |

## 失败、回退与未验证项

T2 预约本身尚未运行；有限 OUT1 维护实验的真实 build、波形、脚本失败和恢复见 T2-PROGRESS-20260922-003/004。整窗增量构建替换上一版包的历史证据缺口在 004 显式记录，不能宣称旧包仍可重验。多板槽位执行、预约 wire、持续同步与细分共存、正式时钟资格、实际 T2 latch 与 completion 均未在本轮验证。失败按独立运行目录保留，不以摘要重写通过。

## 归档状态

暂无归档。
