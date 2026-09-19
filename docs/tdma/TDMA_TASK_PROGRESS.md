# TDMA 基础件主域任务进度

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_TASK_PROGRESS.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`
Last updated: 2026-09-19

本文档记录 TDMA foundation 的阶段性任务进度、验证结果和后续动作。待办事项放在 `TDMA_DOMAIN_TODO.md`。

## 文档接口

架构维护稳定边界，TODO 维护任务状态；本文件与归档保存实施证据，不替代契约登记。
历史记录中的预算、节点及门禁描述只代表当时快照，当前口径以最新条目和主域文档为准。

## 当前 checkpoint

### TDMA-PROGRESS-20260919-001：主域文档按当前实现收敛与历史轮转

- TODO task ID：`TDMA-FLIGHT-002`、`TDMA-FLIGHT-002G`、`TDMA-FLIGHT-002I`、`TDMA-HIL-001`。
- 日期：2026-09-19；本轮仅文档代码对照与日志轮转，无固件、工具实现或硬件运行变更。
- 对照：按 TDMA owner、异步准备、typed 特等席固定记录保全及 Core1 MATCH/FOLLOW 路线更新架构和任务；可配置整表周期、编译容量与 STOP 选节点邮箱布局按现有代码描述，不将配置目录等同长档循环验收。
- 重点：继续收敛自主飞行、逐圈装卸期限、完整静态预算、资源与受控退出；现有四板 DPLL 物理闭环不等待首帧精细优化或 NO5，正式 VDC 资格仍独立验收。
- 已有证据：引用 `VDC-PROGRESS-20260919-024` 及 `out/HardwareAcceptance/20260919/p3-reference-profile-r2/`，其源码指纹为 `d31246fd8b07f02a9e68c8680116e751420daf37ac4de01a07ac013ccd55e3f2`（原轮快照，非本轮新凭证）。该轮 PASS_WITH_WARNINGS，TDMA diagnostic 为 true，原始 passed/closed_loop/realtime 为 false；本次不改写其结论。
- 轮转：按日期倒排，保留同日原顺序；最旧连续日期段迁入归档，逐条正文及未编号历史段保留，索引列出范围和条数。轮转前后 ID、字节摘要及正文一致性见 `out/doc-audit/tdma-refresh-20260919/rotation-audit.json`。
- 验证与下一 gate：本轮 `docs_check --strict-names`、`doc_regression_check`、文档 pytest、强制文档 pre-commit、`git diff --check` 及轮转审计均通过；本条不代表新 P3、WCET 或锁相实测通过。继续依各任务退出条件推进，失败原件及历史状态不重判。

### TDMA-PROGRESS-20260917-001：observer 历史锚点参与回绕重建

DPLL 持续跟踪暴露连续 DMA 无 empty 见证时的真实 observer 缺陷；已在
`tdma_event_observer.c` 的非首事件 lift 中使用历史公共锚点收紧前事件时间界，
不改 PIO、DMA、公共 lift、共享状态或运行期限。真实长 gap 多候选仍拒绝。
独立完整 feed 红绿、资源、同源码四板 P3 与跨回绕运行证据统一记录在
[VDC 进度 014](../vdc/VDC_TASK_PROGRESS.md#vdc-progress-20260917-014计数回绕伪歧义修复与持续跟踪)，
父任务 `VDC-FAST-003` 仍进行中。此项修复输入连续性，不宣称 TDMA 全部时序或 DPLL 锁相通过。

当前已实现可配置 Core1 整表周期，见 `TDMA-PROGRESS-20260914-013`，证据根为
`out/HardwareAcceptance/20260914/tdma-flight-configurable-period/`。STOP/config ACK 后
发布请求，Core1 在完整表边界认领并确认，ARM/底层启用在交接期间拒绝；SCPI 已
接入离散周期配置。软件、A/B/Boot、当前包 P3 quick diagnostic 严格检查、四档整表
读回与 ARM 冻结验证通过。默认档普通短帧通过；长档循环运行准入仍有反馈窗口
拒绝，自主切换缺帧、完整 WCET 和正式 RAM 继续开放。
前序完成 RX header/presence 后台准备候选的双槽比较并决定撤回，见
`TDMA-PROGRESS-20260914-012`，证据根为
`out/HardwareAcceptance/20260914/tdma-flight-rx-unload-prepare/`。候选软件、构建、
P3 quick diagnostic、双槽四轮完整记录及普通恢复通过相应范围检查；同槽主站范围
重叠，全部从站双轮峰值升高，未采纳。两次 NO3 SD 写失败及首次 P3 拓扑预检失败
原件保留，后续 reboot 后普通闭环和 SD 恢复成功不关闭根因。生产源码已恢复前序
异步邮箱基线，恢复版软件、构建、P3 quick diagnostic 严格检查、两轮记录和普通
恢复完成，四组 STOP/config ACK、许可证退出及 SD 核对通过。其后接续的完整静态表
周期配置以本页 013 记录为准；旧预算失败保持原样，不以新表重判旧记录。
前序完成自主主站邮箱异步准备切片，见 `TDMA-PROGRESS-20260914-011`，证据根为
`out/HardwareAcceptance/20260914/tdma-flight-origin-async-mailbox/`。复用既有工位，
Core0 校验私有邮箱并准备接受结果，Core1 保留 selection 退休、完整版本准入、
物理发布及 STOP 取消责任。软件、A/B/Boot、当前源码 P3 quick diagnostic 严格检查、
两槽四轮记录、新版本提交计数和两次普通恢复通过相应范围检查；四板最终
STOP/config ACK、许可证退出及 SD 核对通过。采纳异步实现，但同槽完整 phase 未
证明稳定收益，部分从站峰值增长仍保留；完整预算、正式 RAM、增长因果及历史 SD
写失败根因均未闭合。下一步按角色收敛 RX 接受处理与从站捕获/请求，不能把
ORIGIN_PUBLISH 的局部下降记成完整省时或逐圈特等保全。
前序完成自主主站提前复用候选的对照并决定撤回，见
`TDMA-PROGRESS-20260914-010`，证据根为
`out/HardwareAcceptance/20260914/tdma-flight-origin-early-reuse/`。候选先服务 DMA
selection 退休，再检查完整 owner 版本；边界回归通过且不增加静态 RAM，但同节点/
同槽位窗口未证明完整 phase 稳定改善，发布分项反而升高，未采纳。候选源码、补丁、
两槽四轮及全部 PEAK/RUN/OTHER 均保留。生产源码已恢复前序指纹；新包软件、构建、
P3 调试流程、两轮记录和普通恢复完成。恢复 P3 的 SCK training 与 re-arm 行选择
拒绝保留，strict=false；最终四板 STOP/config ACK、许可证退出及 SD 核对通过。
完整预算、正式 RAM、增长因果及历史 SD 写失败根因仍开放。
前序完成自主主站 observation 与本地版本发布的独立归因，见
`TDMA-PROGRESS-20260914-009`，证据根为
`out/HardwareAcceptance/20260914/tdma-flight-origin-service-attribution/`。既有探针
保留，新增两项区间分别覆盖边界读取/更新及本地发布 helper；后者包含无更新、
pending、准入及延后返回，不能解释为单次 DMA 指针写入成本。
两轮主站高峰中发布路径明显大于边界读取，下一步优先拆分发布准入与校验，RX
接受处理继续独立收敛。软件、构建、当前源码 P3 流程、两轮记录及普通恢复完成；
P3 粗校准的 NO2 topology 应答超时保留，strict=false。最终四板 STOP/config ACK、
许可证退出和 SD 核对通过，完整预算与正式 RAM 仍未通过。
前序完成 RESET 有效位退休候选的验证与源码撤回，见 `TDMA-PROGRESS-20260914-008`，
证据根为 `out/HardwareAcceptance/20260914/tdma-flight-timing-reset-retirement/`。
候选减少 RESET snapshot 批量清零，但同节点/槽位对照未证明完整 phase 稳定收益，
没有采纳。常规非 RESET 高峰仍存在，下一优先项为 RX 接受/发布与 adapter 剩余
成本；RESET 外围差值不能全部认定为 memset，也不能扣减预算。两次 NO3 SD 写失败
保留全部 RAM 记录和高峰；软复位后的普通闭环及补采保存通过，写失败根因仍开放。
恢复源码 P3 流程完成，仅总流程时限拒绝使 strict=false；两轮记录、最终普通闭环、
STOP/config ACK、许可证退出和 SD 字节核对通过。主站高峰仍有较大的 adapter
未分解区间，从站高峰走捕获/请求分支，后续按角色分别定位，完整预算仍未达。
前序完成时钟快速路径候选的撤回与恢复验收，见 `TDMA-PROGRESS-20260914-007`，
证据根为 `out/HardwareAcceptance/20260914/tdma-flight-clock-now-fastpath/`。
同节点/槽位的全部对照组未证明完整 phase 收益，候选增加静态 RAM，未采纳；生产
源码已恢复。RESET 首拍发现较大的 body 外区间，但尚未分离初始化与发布成本，
不能全部归因于 memset，也不能从完整预算扣除。恢复源码 P3 诊断流程完成、严格
检查失败，最终普通闭环、STOP/config ACK 和 SD 核对通过。恢复包仍保留非 RESET
主站高峰，回退不等于增长已解决；后续继续 RESET 首拍分解和 RX/latch 有效工作归因。
前序完成 latch 直接初始化候选的撤回与同包跨槽位对照，见
`TDMA-PROGRESS-20260914-006`，证据根为
`out/HardwareAcceptance/20260914/tdma-flight-latch-direct-seed/`。候选静态指令减少，
但未证明完整 phase 稳定改善；生产源码已恢复前序版本。初始新旧包对照同时改变
源码与 OTA 槽位，不能单独归因；恢复包跨槽位的重复窗口范围重叠，也不能据此排除
槽位、布局或共享干扰。恢复源码 P3 的本轮 quick diagnostic 检查全过，最终普通
闭环、STOP/config ACK 和 SD 核对通过；完整 500 us、正式 RAM 和增长因果仍开放。
前序完成自主 origin 有限 service 屏蔽实板切片，见 `TDMA-PROGRESS-20260914-005`，
证据根为 `out/HardwareAcceptance/20260914/tdma-flight-origin-service-blackout/`。
两轮均在连续跳过完整 service 主体期间保留连续且检查通过的 DMA 返回档案；普通
短帧预验收与最终恢复、STOP/config ACK 和 SD 核对通过。此证据仅覆盖主站有限
区间的硬件进展；从站屏蔽、逐帧物理节拍、绝对时间和完整 CPU/RAM 门禁仍未闭合。
P3 首轮回链漏检、第二轮 SCK/启动屏障拒绝及首次链接失败均保留，严格验收未通过。
前序完成连续计数器自身压缩与记录池复用核算，见 `TDMA-PROGRESS-20260914-004`，
证据根为 `out/HardwareAcceptance/20260914/tdma-flight-edge-counter-compact/`。
新候选保持计数语义并缩减 PIO 指令，ARM ABI 原型也找到互斥缓存复用的空间；
两者尚未安装，不能计作生产 RAM 释放、CPU 省时或逐圈时间交付。下一实现仍须
联合闭合 FIFO 独占、身份/版本、记录格式、消费期限及 VDC 时钟映射。
前序完成 follower PIO 共尾候选的验证与回退，见 `TDMA-PROGRESS-20260914-003`，
证据根为 `out/HardwareAcceptance/20260914/tdma-flight-follower-pio-tail/`。候选可以
缩减指令占用，但新增的 byte 重装周期在分数分频模型中造成采样相位偏移，未采纳。
四板及生产源码已恢复前序版本，普通闭环、STOP/config ACK 和 SD 核对通过。
本轮没有释放生产 PIO 空间，也没有证明 CPU 省时；完整预算与增长因果继续开放。
前序完成锁内空队列提前结束切片，见 `TDMA-PROGRESS-20260914-002`，证据根为
`out/HardwareAcceptance/20260914/tdma-flight-empty-select/`。预算刷新、过期统计、
新数据准入、恢复队列及 STOP 行为保留；软件、A/B/Boot、当前源码 P3 流程、两轮
板端记录和普通恢复完成。保留外层峰值中的空队列分项下降，完整外层仍超预算；
P3 粗校准 topology 超时与 coded marker gate 拒绝使 strict=false，不能写为严格通过。
此前峰值增长因果仍未解决。前序 O6 前后固件的 B/A/B 峰值重复性对照，见 `TDMA-PROGRESS-20260914-001`，
证据根为 `out/HardwareAcceptance/20260914/tdma-flight-peak-repeatability/`。
生产源码未变，主站先前 RUN 高值在本轮未重现，但 NO3 OTHER 外层达到 789.932 us，
NO4 达到 755.336 us（有限窗口快照，非事实源）。增长因果仍未解决，完整 500 us
未达；四板已恢复当前固件，普通闭环、STOP/config ACK 和 SD 字节核对通过。
后续继续定位高峰及有效工作占用，不以重复性对照替代 WCET 或稳定性验收。
前序按用户确认的 O3/O5→O1→O2/O4→O6 顺序推进，O6 定位算术切片见
`TDMA-PROGRESS-20260913-067`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-rx-cursor-math/`。保留既有增量游标、
hint 失效和窗口复制，只优化定位取模；软件、A/B/Boot、当前源码 P3 和两轮对照
完成。实际除法调用减少且无新增 RAM，但完整峰值未一致改善；主站 RUN 外层为
646.920/714.560 us，NO3 第一轮 ALL 外层为 778.248 us（有限窗口快照，非事实源）。
新增增长的同拍构成已保留，因果仍待收敛，完整 500 us 未达。前序 O2/O4 分项切片见
`TDMA-PROGRESS-20260913-066`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-dma-latch-attribution/`。DMA 初始观察、
帧/发现窗口复制后复验及 RX latch 读取/重装分项已完成软件、构建、当前源码 P3
和两轮实板归因。初始 DMA 观察为 12.300–36.316 us，帧复制后复验为
6.264–9.808 us，RX 重装为 17.440–30.156 us（保留峰值分项快照，非事实源）；
主站 RUN 外层为 668.092/668.296 us，仍超预算，随后推进 O6。前序 O1 提前复用切片见
`TDMA-PROGRESS-20260913-065`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-overlay-early-reuse/`。先确认 DMA
selection 退休及物理就绪，再让无更新 TX 跳过 grant 配置；软件、构建、当前源码
P3 和两轮对照完成。正确性通过，完整峰值未一致下降；本负载下无更新复用机会有限，
下一项为 O2/O4。前序 O3/O5 分支计时切片见
`TDMA-PROGRESS-20260913-064`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-request-dispatch-timing/`。请求与接受阶段、
TX latch 读取/重装及空队列调度已完成软件、构建和两轮实板归因；follower 请求
交接为 77.448–114.448 us，本地 TX 取证为 29.468–78.868 us，空队列 select 为
30.068–40.980 us（保留峰值的分项快照，非事实源）。P3 与普通恢复通过，主站
RUN 完整耗时仍为 634.632/645.780 us。新增探针成本保留，不宣称省时；下一项为 O1。
前序 `TDMA-FLIGHT-002I/002B/002F` 的运行时邮箱切片见
`TDMA-PROGRESS-20260913-063`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-runtime-mailboxes/`。同一六节点容量固件
在 STOP 后按 topology 选择邮箱数量、ARM 冻结布局；软件回归、A/B/Boot 构建与
四节点实板对照完成。四邮箱两轮主站 RUN 外层为 621.084/622.020 us，较六邮箱
下降 13.364/5.652 us（有限窗口快照，非事实源）；STOP 和 follower 峰值未一致改善。
普通恢复通过，P3 粗校准拓扑应答超时使严格门禁仍为 false。本切片为 PARTIAL，
五/六节点实环、完整 500 us 与长期目标未完成。
前序 `TDMA-FLIGHT-002I/002B/002F` 的编译容量邮箱切片见
`TDMA-PROGRESS-20260913-062`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-compiled-mailboxes/`。软件回归与 A/B/Boot
构建通过，六邮箱实板两轮主站 RUN 外层为 634.448/627.672 us，相比旧八邮箱低
34.348/47.484 us（有限窗口快照，非事实源）；仍未达到完整 500 us。
当前源码短帧与普通恢复通过，P3 粗校准一次拓扑命令超时使严格门禁为 false。
用户已确认按 STOP 后配置节点数、重新 ARM 使用相应邮箱区推进；截至该先行切片，
运行时切换尚未实现，其后续实现见 063。该先行切片为 PARTIAL，完整目标继续。
前序 `TDMA-FLIGHT-002B/002F` 的协调启停与本地停止边界审查见
`TDMA-PROGRESS-20260913-061`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-coordinated-lifecycle-review/`。完成候选
阶段、通知载荷、精度条件、链路关闭顺序与现有 STOP 调用关系核对；网络协议未实现，
未产生新的固件或硬件计时结论。既有生命周期回归通过 22 项，另 1 项夹具缺少构图
取消依赖而编译失败（本次验证快照，非事实源）；原错误保留，作为后续 STOP 修改
前置修复项。耗时增长仍优先，完整目标保持进行中。
前序 `TDMA-FLIGHT-002B/002F` 的 mailbox CRC 等价运算切片见
`TDMA-PROGRESS-20260913-060`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-mailbox-crc-byte/`。全部状态/字节组合与原
逐位算法一致，实际 A/B 每字节 CRC 循环从 44 条指令缩为 9 条，既有校验均保留。
两轮主站 RUN 外层为 668.796/675.156 us，STOP 外层为 795.472/696.516 us（当前
构建与有限窗口快照，非事实源）；未证明完整 phase 一致改善，新增 STOP 增长继续
定位。当前源码 QUICK P3、普通恢复与 SD 核对通过，完整 500 us、切换稳定性、正式
RAM 与协调启停仍未闭合，本切片为 PARTIAL，长期目标继续。
前序 `TDMA-FLIGHT-002B/002F` 的自主 origin 固定邮箱装载切片见
`TDMA-PROGRESS-20260913-059`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-origin-mailbox-load/`。直接选取 Core0 已准备
的本地邮箱，保持固定 map 授权及动态复验；同矩阵两轮主站 RUN body 为
657.276/633.520 us，外层为 697.100/658.300 us（有限窗口快照，非事实源）。
当前源码 QUICK P3 严格门禁和普通恢复通过；自主切换整段稳定性、完整 500 us、正式
RAM 与 TDMA 协调启停协议仍未闭合，本切片保持 PARTIAL，长期目标继续。
前序 `TDMA-FLIGHT-002B/002F` 的启停准备切片见 `TDMA-PROGRESS-20260913-058`，
证据根为 `out/HardwareAcceptance/20260913/tdma-flight-lifecycle-preparation/`。纯 DMA
构图交给既有 Core0 准备服务，Core1 保留授权、安装和取消交接；准备态采样区间从旧版
97.180–139.295 ms 收敛到两轮上界 42.718/49.217 ms（有限窗口快照，非事实源）。
本轮普通短帧和恢复通过，SCK 严格准入、切换 missing、完整 500 us 仍未闭合。
完整 TDMA PREPARE/READY/目标圈与排空协议尚未实现，本切片保持 PARTIAL。
前序 `TDMA-FLIGHT-002B/002F` 的时间增长修复切片见
`TDMA-PROGRESS-20260913-057`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-runtime-growth-fix/`。同矩阵、同档案模式的
两轮主站 body 峰值已降至 697.956/707.240 us，外层 service 为 720.820/725.524 us
（有限窗口快照，非事实源），回到前序 713.480 us body 参考值以内。高频探针、固定
RX 检查/提交及授权上下文驻留 SRAM，校验和动态复验保留。当前源码 QUICK P3 通过，
完整 500 us、自主切换稳定性与正式 RAM 仍未通过；本切片为 PARTIAL，长期目标继续。
前序 `TDMA-FLIGHT-002B` 的自主 origin 逐圈 DMA 档案切片见
`TDMA-PROGRESS-20260913-056`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-cycle-record/`。现有 DMA 图写固定记录池，
STOP 后可读最新完整记录；两轮实板验证连续 sequence、重新 ARM 的 epoch 更新与
persona 复用失效。当前源码 QUICK P3 和普通恢复短帧通过，两轮自主切换的整段稳定性
仍失败，完整 WCET 仍高于 500 us。有限冻结档案没有生产态逐圈消费者或绝对 VDC 时间
映射；本切片为 PARTIAL，目标继续进行。
前序连续边沿计数与 DMA 保全候选见
`TDMA-PROGRESS-20260913-055`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-edge-counter/`。离线模型验证共享指令、
自动续传和有界消费，并保留缺边沿、FIFO 丢数、SRAM 覆盖及 epoch 错配反例；尚未
安装固件或证明完整 VDC 逐圈交付。静态资源、绝对时间关联、完整记录池与硬件验收
继续开放，TDMA 目标保持用户已确认的 500 us。
前序 `TDMA-FLIGHT-002F` 的静态预算迁移见 `TDMA-PROGRESS-20260913-054`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-budget-500/`。按用户确认，TDMA 后续目标
采用 500 us，源码完整静态表同步调整，旧 380 us 不再作为长期硬门槛；周期与 GUARD
保持。四板已 OTA，板端记录确认新表生效，短帧闭环与停止后的 SD 读回通过；SCK
候选覆盖/重装余量和完整 WCET 仍失败。时间戳硬件自治和满载覆盖继续推进。
前序维护命令应答与 coarse 准备屏障切片见
`TDMA-PROGRESS-20260913-053`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-calibration-command-boundary/`。主机已按
真实数值结果与 owner 应用配置确认完成；第二轮当前源码 QUICK P3 校准及短帧门禁
通过。首轮 MARK 拓扑实际拒绝及恢复采集顺序错误保留，不能据第二轮成功宣称原
拓扑错配根因闭合。生产预算与 PIO 未改，时间证据自治、完整 WCET 与 RAM 继续开放。
前序 PIO/SM 下沉核算与校准前置失败调查见
`TDMA-PROGRESS-20260913-052`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-calibration-arm-generation/`。自主 origin
两侧共享指令空间已满，process follower TX 尚有候选余量；优先评估时间证据自动
收割/重装与固定 DMA 计划复用，尚无新 WCET 收益。QUICK 前置 OPMODE APPLY 超时
保留，原拓扑错配根因未确认；普通短帧恢复、STOP/config ACK 与 SD 读回闭合。
本轮未改生产源码和预算，长期目标保持进行中。前序 Core1 RX 接收提交计时切片见
`TDMA-PROGRESS-20260913-051`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-rx-accept-timing/`。新增分项定位 mailbox/map
检查和发布后 commit 成本，完整 WCET 仍失败；当前源码两轮 P3 普通短帧通过，粗校准
拓扑读回重复失败，严格门禁继续拒绝。自主离线取证、普通模式恢复和 STOP 后 SD 读回
闭合；后续先定位校准状态代际问题，再推进准备结果复用，长期目标保持进行中。
前序 `TDMA-FLIGHT-002B` 的自主交接板端取证切片见
`TDMA-PROGRESS-20260913-050`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-autonomous-board-evidence/`。本轮未改生产
源码；自主序号持续增长，但切换 missing/DOWN、观察副本丢弃与完整 WCET 仍未闭合。
SCPI 只控制，原失败、离线归因、普通模式恢复和 STOP 后 SD 字节核对均保留。
长期目标保持进行中。前序 `TDMA-FLIGHT-002F` 的 RX 捕获控制 SRAM 放置切片见
`TDMA-PROGRESS-20260913-049`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-rx-capture-residency/`。固定矩阵板端对照显示
捕获及完整累计峰值下降，仍超预算；首轮 SCK/短帧失败保留，同固件第二轮当前源码
P3 严格门禁通过。新测矩阵、STOP、配置确认与 SD 读回闭合，长期目标保持进行中。
前序 RX 复制 SRAM 放置切片见
`TDMA-PROGRESS-20260913-048`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-rx-copy-residency/`。当前源码 P3 与短帧
门禁通过；板端记录和停止后读回显示复制子项下降，完整峰值仍超预算。正式 RAM、
完整 WCET、硬件自主 blackout 和逐圈证据继续开放，长期目标保持进行中。
前序板端记录与 SD 后处理切片见
`TDMA-PROGRESS-20260913-047`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-board-recording/`。SCPI 只触发流程，板端
有限窗口记录、冻结导出和 STOP 后 SD 逐字节读回通过；同固件第二轮 P3 严格短帧门禁
通过，首轮 SCK 重放行拒绝及运行失败完整保留。持续写 SD、完整 WCET、正式 RAM 与
逐圈特等席证据仍未闭合，长期目标保持进行中。
前序 Core0 有界输出与启动观察闭合切片见
`TDMA-PROGRESS-20260913-046`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-serial-observation/`。原短帧启动门禁通过，
SD RUN 保存、完整波形、完整 WCET 和正式 RAM 仍未闭合。按用户最新要求，后续 SCPI
只触发流程，实时采样改为板端记录，结束后统一导出；不再扩展串口轮询方案。
前序启动观测成本与 SCPI raw 输出切片见
`TDMA-PROGRESS-20260913-045`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-startup-observation/`。完整五查询对照已测得
主机采样耗时下降，严格启动仍失败；SD 超时、原始捕获完整性、完整 WCET、正式 RAM 与
特等席逐圈保全继续独立验收，长期目标保持进行中。
前序 MARK 准备证据切片见 `TDMA-PROGRESS-20260913-044`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-marker-preparation/`。真实命令结果、停止与配置
应用确认、prepared identity 已串联；四板 MARK 与注入源轮换通过。严格启动、主机采样
成本、SD 取证超时、完整 WCET 和正式 RAM 仍分别保留，长期目标保持进行中。
前序启停异步准入切片见 `TDMA-PROGRESS-20260913-043`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-lifecycle-admission/`。队列锁不再决定 STOP
接受或触发 ARM 回滚；物理完成与 Core0 存储退休分开。反复启停应答改善，严格启动、
MARK 身份读回、停止边界计数与取证 SD 超时仍分别保留；未进入下一性能迁移。
前序任务浮点上下文修复见 `TDMA-PROGRESS-20260913-042`，
证据根为 `out/HardwareAcceptance/20260913/tdma-flight-fpu-context/`。先闭合编译器 ABI
与 RTOS 保存区不一致的缺口，再继续 ARM/STOP 握手修复；不宣称旧拒绝的因果已闭合。
前序 ARM 拒绝归因切片见 `TDMA-PROGRESS-20260913-041`，
证据根为 `out/HardwareAcceptance/20260913/tdma-flight-arm-rejection/`。
本轮区分管理面 map 准入的实际拒绝点，先闭合 STOP/re-ARM 故障，再继续性能拆分。
前序锁存与线路时长等价算术切片见
`TDMA-PROGRESS-20260913-040`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-latch-arithmetic/`。锁存分辨率使用硬件
整数除法，整数位周期使用已证明范围内的乘法；重装顺序、舍入和时钟采样语义保持。
完整 WCET、严格启动、正式 RAM 与逐圈时间证据保全继续独立验收。
前序 overlay DMA 描述符异步绑定切片见
`TDMA-PROGRESS-20260913-039`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-overlay-dma-bind/`。Core0 使用 ARM 时冻结的
资源模板准备完整描述符，Core1 保留生命周期复验、generation 和后继指针发布；
完整 WCET、严格启动、正式 RAM 与逐圈保全继续独立验收。
前序私有 RX 头部检查与 DMA 保留期限切片见
`TDMA-PROGRESS-20260913-038`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-rx-private-header/`。已完成候选复制和覆盖
复验后，再检查同一私有帧的固定头；从站头检查下降，完整 WCET、严格启动及逐圈
保全仍未通过。前序训练仲裁占用与有界完成发布切片见
`TDMA-PROGRESS-20260913-037`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-training-publication/`。命令发布前保留
占用，Core1 改为单向版本化完成事实，Flash 最终取得资源也复核训练占用；训练发布
子项下降，完整 WCET、正式 RAM 与逐圈保全仍未通过。
前序训练发布竞态与成本审计见
`TDMA-PROGRESS-20260913-036`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-training-gate-audit/`。主机回放复现旧状态
清除 pending 占用，并排除将配置侧 accepted 序列当作硬件停止确认的方案；本轮未改
固件。训练发布的整段成本不足以解释完整 WCET 缺口，后续仍须拆分 owner/RX 工作。
前序公共时钟精确整数换算切片见
`TDMA-PROGRESS-20260913-035`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-clock-conversion/`。算术等价与实链除法
旁路已验证，完整 phase 尚未一致改善，严格 WCET、正式 RAM 与逐圈保全仍未通过。
前序无新版本 TX 快速复用切片见
`TDMA-PROGRESS-20260913-034`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-overlay-reuse/`。FIFO/生命周期语义与实链
路径已核验，有限计时尚未证明完整 phase 一致改善，严格 WCET 与正式 RAM 仍失败。
前序 persona 专用 RX 复制与 DMA 字读取切片见
`TDMA-PROGRESS-20260913-033`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-rx-copy-persona/`。复制子项与完整 phase
分别测量，严格启动、完整 WCET、正式 RAM 和特等席保全继续独立验收。
前序独立 RX 窗口扫描与后续复制成本核验见
`TDMA-PROGRESS-20260913-032`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-origin-rx-window/`。本轮保持已提交固件，
在当前 P3 测量基线上显式扩展诊断维度；接收有效窗口、保守余量与完整 WCET 仍待
验收，不据相邻零错误点冻结采样常数或调整门限。
前序独立 origin RX 采样参数切片见
`TDMA-PROGRESS-20260913-031`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-origin-rx-phase/`。参数经 Calibration
staging 和 TDMA owner 配置；发送时序不随 RX 选相改变。普通模式对照支持接收采样
余量方向，严格启动、完整 WCET、正式 RAM、自主路径及特等席保全仍须分别闭合。
前序返回 DATA 采样窗口与指令模型审计见
`TDMA-PROGRESS-20260913-030`，证据根为
`out/HardwareAcceptance/20260913/tdma-flight-origin-sample-eye/`。较长窗口再次复现
相位相关的 CRC 增长；采样余量方向得到进一步支持，同一拒收帧绑定及根因修复仍未
完成。跨日原件按路径和 SHA 归档；四板最终已核对当前矩阵应用并 STOP。
完整 WCET、正式 RAM 和特等席逐圈保全继续开放，现有门限与节点容量后续项顺序保持。
前序返回 DATA 相位对照见 `TDMA-PROGRESS-20260912-029`，
证据根为 `out/HardwareAcceptance/20260912/tdma-flight-origin-phase-ab/`。近端从站
与 origin 的相位组合已复现并消除有限观察窗口内的 CRC 增长，采样余量是重点方向；
尚未完成根因修复或严格验收。四板已恢复当前矩阵并 STOP，完整 WCET、正式 RAM、
特等席保全及节点容量后续项顺序保持。
前序启动截止检查与 origin CRC 调查切片见
`TDMA-PROGRESS-20260912-028`，证据根为
`out/HardwareAcceptance/20260912/tdma-flight-origin-crc/`。主机启动观测已拒绝迟到
稳定样本；CRC 采样相位线索尚未确认根因。完整 WCET、正式 RAM 与特等席保全仍开放，
门限和节点容量后续项顺序保持。
前序 owner/runtime/adapter 归因切片见
`TDMA-PROGRESS-20260912-027`，证据根为
`out/HardwareAcceptance/20260912/tdma-flight-owner-attribution/`。完整 WCET、正式 RAM
仍失败，普通 origin CRC 异常与启动观测截止时间盲点须先定位修复；本轮为诊断归因，
不作为稳定性验收通过，预算保留，节点容量后续项后置。
前序 RX 位反转内联切片见
`TDMA-PROGRESS-20260912-026`，证据根为
`out/HardwareAcceptance/20260912/tdma-flight-rx-rbit/`。自主从站复制成本下降，但完整
WCET 与正式 RAM 仍未通过；下一步归因剩余 owner/adapter 工作与运行干扰，现有
门限保留，节点容量后续项后置。
前序 RX 取帧内部归因切片见
`TDMA-PROGRESS-20260912-025`，证据根为
`out/HardwareAcceptance/20260912/tdma-flight-rx-acquire-attribution/`。ring copy 是
已测从站取帧的主要开销；下一切片验证逐字位反转内联，完整 WCET 与正式 RAM 仍未
通过，现有门限保留，节点容量后续项保持后置。
前序 overlay 静态授权预计算切片见
`TDMA-PROGRESS-20260912-024`，证据根为
`out/HardwareAcceptance/20260912/tdma-flight-overlay-layout/`。运行期已移除 map 扫描，
完整 WCET 与正式 RAM 仍未通过，节点容量后续项保持后置。
前序紧凑 RX 副本切片见 `TDMA-PROGRESS-20260912-023`，
证据根为 `out/HardwareAcceptance/20260912/tdma-flight-rx-copy/`。已降低私有存储与
包复制成本，完整 WCET、正式 RAM 及 STOP 应答问题仍开放，节点容量后续项保持后置。
前序 RX 分项计时切片与预算复评见
`TDMA-PROGRESS-20260912-022`，证据根为
`out/HardwareAcceptance/20260912/tdma-flight-rx-attribution/`。暂时保留现有门限，
按用户要求先完成当前切片验收，节点容量后续项保持后置。
前序原始 DMA 候选异步发现切片见
`TDMA-PROGRESS-20260912-021`，证据根为
`out/HardwareAcceptance/20260912/tdma-flight-async-scan/`。Core0 扫描私有窗口，Core1
按提示重新复制 live ring；完整 WCET、正式 RAM 和特等时间戳快速通道分别验收。
先闭合本切片，节点容量后续项保持后置。前序普通 RX Core0 解析切片见
`TDMA-PROGRESS-20260912-018`，证据根为
`out/HardwareAcceptance/20260912/tdma-flight-async-rx/`。随后完成的六节点编译
容量隔离核算见 `TDMA-PROGRESS-20260912-019`，证据根为
`out/HardwareAcceptance/20260912/node-capacity-ram-audit/`。核算未修改运行固件，
编译容量统一配置、准入及本轮硬件复核见 `TDMA-PROGRESS-20260912-020`，证据根为
`out/HardwareAcceptance/20260912/tdma-node-capacity/`。该实现保持固定 wire/存储布局，
严格失败和正式 RAM/WCET 缺口继续保留，`TDMA-FLIGHT-002I` 保持 IN PROGRESS。
前序 follower Core0 overlay 实现与验证记录见
`TDMA-PROGRESS-20260912-017`，证据根为
`out/HardwareAcceptance/20260912/tdma-flight-async-overlay/`。
按用户进一步要求推进异步准备与解析边界，设计记录见
`TDMA-PROGRESS-20260912-016`；该项纳入列车调度基础，四级服务策略保持后置。
最近完成的列车调度 RX 工作量限制切片证据根为
`out/HardwareAcceptance/20260912/tdma-flight-rx-budget/`，执行记录为
`TDMA-PROGRESS-20260912-015`。限制可丢弃镜像的同拍重复处理，并共享 magic 候选前缀；
完整 Core1 WCET、无更新稳态与 service blackout 仍需分别闭合，乘客调度继续后置。
前序列车调度分项计时切片证据根为
`out/HardwareAcceptance/20260912/tdma-flight-service-timing/`，入口为 `hardware-review.json`
与 `slice-manifest.json`，执行记录为 `TDMA-PROGRESS-20260912-014`。已取得正常和临时
许可证自主模式的完整 phase 归因；重复 RX 捕获/扫描是自主从站的主要开销，完整
Core1 WCET 与正式 RAM 仍失败。乘客调度保持后续任务。
当前异步候车平台与四级服务设计核验入口为
`out/HardwareAcceptance/20260912/tdma-flight-async-platform/design-audit.json`；
执行记录为 `TDMA-PROGRESS-20260912-013`，本次尚未改变固件或运行硬件。
最近已完成提交的 RX 观察副本与 DMA 接收计数切片证据根为
`out/HardwareAcceptance/20260912/tdma-flight-rx-observation/`；
执行记录为 `TDMA-PROGRESS-20260912-012`，尚未形成自主循环或产品验收结论。
前序 Calibration 临时许可证与实际自主 origin 试验的复核证据根为
`out/HardwareAcceptance/20260912/tdma-flight-origin-admission/`，入口为
`slice-manifest.json` 与 `hardware-review.json`。用户明确要求后，新证据使用日期目录
`20260912`；此前 `20260911/tdma-flight-origin-admission/` 内源码测试、构建、P3、
短帧基线及首次自主采集保留原位，新报告用路径和 SHA-256 引用，不改写历史失败。
该索引不是自主循环、完整 WCET 或产品验收凭证。
前序 TDMA owner intent 有界读取与窗口等待拆拍证据根为
`out/HardwareAcceptance/20260911/tdma-flight-service-boundary/`，入口为
`slice-manifest.json`；该索引不是自主环路或完整 WCET 验收凭证。
前序 origin 准备态累计事实发布修复证据根为
`out/HardwareAcceptance/20260911/tdma-flight-origin-status/`，入口为
`status-manifest.json`；该索引不是自主环路验收凭证。
前序 origin 分步准备候选与状态发布审计证据根为
`out/HardwareAcceptance/20260911/tdma-flight-origin-prepare/`，入口为
`prepare-manifest.json`；该索引不是自主环路验收凭证。
前序 origin 物理层与异步 adapter 工作树集成证据根为
`out/HardwareAcceptance/20260911/tdma-flight-origin-integration/`，入口为
`integration-manifest-r4.json`；该索引不是自主环路验收凭证。
前序 STOP 生命周期切片证据根为
`out/HardwareAcceptance/20260911/tdma-flight-origin-stop/`，入口为 `slice-manifest.json`；
完整 origin 图离线证据入口为
`out/HardwareAcceptance/20260911/tdma-flight-origin-frame/frame-manifest.json`；
前序 reference 头部离线审计入口为
`out/HardwareAcceptance/20260911/tdma-flight-origin-audit/audit-manifest.json`；
最近一次 follower recurrence 固件切片证据入口为
`out/HardwareAcceptance/20260911/tdma-flight-follower-recurrence/slice-manifest.json`；
前序 live-header XOR 切片证据入口为
`out/HardwareAcceptance/20260911/tdma-flight-live-crc/slice-manifest.json`；
前序有限同钟采集的证据入口为
`out/HardwareAcceptance/20260911/tdma-flight-b0/slice-manifest.json`。
前序 P3 与回归硬件证据入口为
`out/HardwareAcceptance/20260911/tdma-flight-baseline-ab/archive-index.json`。
该索引记录原始工作树根、原路径、归档路径和逐文件 SHA-256；复制后已逐文件核对。
原文件和报告内路径保持原样，严格失败与诊断继续状态保持原样；归档索引不是验收凭证。
以下历史生成路径仍用于说明取证来源；同名子目录可由归档索引定位。

### TDMA-PROGRESS-20260914-013 - STOP 后配置 Core1 完整静态表周期

- 日期：2026-09-14
- 状态：PARTIAL；配置实现、软件、构建和有限硬件验收完成。用户明确允许整张表改为
  可配置周期；本轮不以新预算重判历史失败，也未授予长帧或产品 RUN 能力。
- `app_realtime_profile_supported()` 声明四个离散档位；默认拍数由
  `PROJECT_CORE1_CYCLE_CYCLES` 选择。默认周期/TDMA WCET 的派生快照为
  1.5 ms / 850 us，长档为 5/10/15 ms（快照，非事实源）。每档保持全部 phase 及
  GUARD，长档扩展 TDMA 后平移其他相位，不改变其他相位自己的窗口宽度/WCET。
- Core0 在唯一 TDMA service 的 STOP/config ACK 门禁下发布不可拆分请求；Core1
  单次 CAS 认领，在既有 schedule seqlock 中安装全表及 generation，再释放 ARM
  条件。STOP 先于认领可取消；认领先于 STOP 时有界完成表更新，物理 STOP ACK
  仍必须由后续 TDMA service 完成。拒绝覆盖 pending、旧 generation 完成及底层
  enabled configure 旁路，不增加 Core1 control-lock 等待。
- `SYSTem:TDMA:PERiod <us>` 返回 PENDING/generation；`PERiod?` 返回拍数和请求/
  应用代际。ARM 后禁止更改；复位回到编译默认。Core1 只在 STOP 交接时重建绝对
  deadline 起点，之后按所选周期累加，避免把 service 时间累积到周期中。
- 软件记录：运行时/目录/相位测试 19 passed，既有邮箱/探针/回放工具测试
  30 passed，实际 service scheduler、ring runtime、VDC domain C harness 均通过
  （本轮快照，非事实源）。四档两两替换、非法输入、未完成 STOP、ARM 与底层启用
  拒绝、STOP 认领前/后边界、代际 wrap 和累计故障保留覆盖。首次 pytest 临时目录
  父目录不存在、首次候选 build 位于 worktree 根外被工具拒绝、首次整合补丁 hunk
  格式不兼容均保留原件；修正路径/格式后新标签通过，未放宽验证。
- 首版主区与隔离工作树源码指纹一致；首版验收 build 为 `20260914011803`，指纹为
  `8c349e213645c30a43f755344fb6da945475f2c3b9fb850a14f57a7aa7b13a76`，1050 files
  （本轮快照，非事实源）。A/B/Boot 与 Flash link contract 通过，PIO headers
  逐字节同恢复基线。静态 RAM 增加 8 B、heap 外余量从 36 B 到 28 B，heap 预留
  仍为 2048 B；可链接不代表正式 RAM 门禁通过。
- 首版 P3 调试流程完成但 strict=false：SCK link2 候选覆盖不足、fresh replay row
  从站重装余量为负及短帧启动屏障超时三项失败保留。固定相位矩阵下首版普通
  短帧、配置代际确认、非法输入拒绝、ARM 冻结及 STOP/SD 核对通过。自主两轮
  原始门禁失败包含切换时 receive_missing 增长，以及普通模式校验器要求 persona
  和软件发送计数；后段稳定窗口不能取消这些拒绝，详见 `period-failure-review-r1.json`。
- 首版 5 ms 表安装成功，但普通主站反馈始终落后五个序号；记录窗口内 completed
  增量为零、timed-out/reseed 各增加 800、accepted 增加 400（有限窗口快照，非事实源）。
  收发进展不能替代返回关联门禁，未将此档记为循环运行通过。STOP 后回退默认，
  普通短帧和四板 SD 再次通过。长档后续须联合适配 operating profile 反馈窗口和
  软件流水线；当前目录能力与循环运行验收分开记录。
- SCK 补采保留 SD 原始数据、下载及 SVG：`sck-raw-r2/summary.json` 的板端 trial
  passed=true，但离线 correlation.accepted=false，均原样记录；不能消除前序
  多候选覆盖/重装失败。首次补采 epoch 超出工具范围，在设备操作前拒绝，原件保留。
- 串口通用工具起初不识别 PENDING/generation，首版配置应答被丢弃为 timeout；
  后续读回证实板端已应用。临时精确 matcher 和一次导入路径错误均保留。正式
  `scpi_serial.py` 已支持长短命令拼写及严格应答匹配，相关回归 143 passed。
  最终 build 为 `20260914014549`，源码指纹为
  `fc5b65d64fa7f32c62a25171d7eaf7e1c3063bc37b359a2298abd0c8a27c88cb`，1050 files
  （本轮快照，非事实源）；与首版固件功能相同，增加 host 匹配及其测试后重新构建
  并执行当前源码 P3。最终 A/B/Boot、Flash link 与 PIO 一致性通过，RAM 数值同首版。
- 最终 P3 为 FOUR_NODE_TDMA_QUICK_DIAGNOSTIC，本轮 strict_gates_passed=true，
  failures 为空；四板 OTA、校准矩阵和普通短帧通过相应门禁。此范围不含 NO5，也
  不是完整产品 P3 或伺服稳定性验收，首版 P3 失败仍保留。
- 最终四板四档的整表参数和 requested/applied generation 读回、ARM 后拒绝修改
  均通过，恢复默认配置。两轮最终自主记录的完整主站 RUN 为 609.984/602.180 us，
  从站 OTHER 最大值为 734.352 us（有限窗口快照，非事实源）；全部 PEAK/RUN/OTHER
  与原始板端记录保留。两轮原门禁仍拒绝切换时缺帧及普通 persona/发送计数判据，
  后段四板 accepted 推进、rejected/missing 增量为零不能替代全窗口验收。计时 RESET
  在切换后触发，保留峰值也不能覆盖切换全程或证明完整 WCET。
- 准备过程的失败均留原件：首版 P3 后序列因闭环断言停止，初次收尾使用旧固定
  matrix 导致相位读回不符，随后按该 P3 实际 matrix 核对 STOP/config ACK 通过；
  串口 matcher、导入路径、长档运行拒绝和补采 epoch 失败亦未被覆盖。
- 最终普通短帧恢复通过；四板默认周期应用代际一致、pending 清空，最终
  STOP/config ACK、许可证退出及五组最终记录的 SD 字节核对通过。代码与文档
  分离提交及封存入口为本目录 `review-final-r1.json`、`slice-manifest.json` 和
  `commit-proof.json`；长期目标工具当前为 paused，本切片不提升长期验收状态。
- Core1 的服务周期与 VDC physical nominal period、servo update 参数分别核验。
  本入口不改变后两者；VDC 多拍处理和 Trigger 队列消费会变慢，不能把时基单位
  正确、短帧通过或更宽预算解释为伺服稳定、触发吞吐、LONG 容量或逐圈特等保全。
- 原件入口：首版 `scope.json`/`source-checkpoint-r1.json`/`plan.json` 与最终
  `scope-r2.json`/`source-checkpoint-r2.json`/`plan-r2.json`、软件及 build
  command 记录；证据根为 `out/HardwareAcceptance/20260914/tdma-flight-configurable-period/`。

### TDMA-PROGRESS-20260914-012 - RX 头检查后台准备候选撤回与周期配置衔接

- 日期：2026-09-14
- 状态：PARTIAL；候选完成软件、构建、P3、两槽记录及普通恢复；完整 phase 收益
  未成立，因此未采纳。源码已恢复前序基线，恢复版软件、构建、P3、两轮记录及
  普通恢复完成；完整预算、正式 RAM 和增长因果仍未闭合。
- 候选将 compact map 放入 `tdma_rx_prepare_t` 的互斥 expected-frame scratch，
  Core0 在私有 packet 上检查 header/presence/seq16，Core1 保留当前 novelty、布局
  生命周期复验、receive health 和 FIFO 发布成功后的 commit。原始诊断输入优先，
  同 epoch/map/local/payload/owner 的布局可缓存，STOP 沿原 cancel/ACK；发布失败不
  消费序号，同值可重试。没有新增硬件写者、wire、PIO、DMA 或 recorder 探针。
- 候选软件回归为 51 passed，完整 adapter C harness 通过（本轮快照，非事实源）。
  覆盖四/五/六节点、非连续 segment ID、owner/offset/header 错误、expected owner
  子集、seq16 wrap、worker 完成后的当前消费序号、重复值 WKC、计数差分、stale
  map、STOP 取消及真实 RX FIFO 满后同值重试；原始坏帧诊断和 legacy 路径保留。
- A/B/Boot 与 Flash link contract 通过。链接快照（非事实源）：compact layout/result
  为 148/40 B，诊断 scratch 为 56 B；ARM ABI 下 RX job 从 840 增至 848 B，位于原有
  更大的 injection/job union，adapter 仍为 7488 B，总静态 RAM 不增长。heap 预留
  2048 B、heap 外余量 36 B，PIO headers 逐字节同基线；不提升正式 RAM 结论。
  首次 `linkage-command-r1` 错误假定 job sizeof 不变，以 exit=1 保留；r2 以实际
  ARM DWARF、symbol/map 和 code/data relocation 交叉核对后通过。
- 候选 build 为 `20260914000205`，源码指纹为
  `e2d82d9630177160dd50ef4b20c808fab722bd406053f05a54868ba1cde5d98a`
  （本轮快照，非事实源）。编译容量六、实环四，payload/packet 为 132/164 B，
  v9/51 项探针保持；实际入口由 `PROJECT_NODE_CAPACITY`、active topology 与
  `TDMA_SERVICE_TIMING_VERSION` 约束。候选源码和完整补丁已独立留存。
- 同 UID、同实际可执行槽与 011 比较如下；NO1 取 RUN、从站取 OTHER，每格为两轮
  完整外层时间，单位 us（有限窗口快照，非事实源，不是同拍配对或 WCET 证明）。

  | 节点 / 实际槽 | 011 基线 | RX 头检查候选 |
  |---|---|---|
  | NO1 / B | 671.252 / 661.372 | 688.884 / 665.280 |
  | NO2 / B | 695.068 / 705.732 | 761.176 / 759.120 |
  | NO3 / B | 700.004 / 709.544 | 734.668 / 757.812 |
  | NO4 / A | 690.352 / 681.960 | 744.848 / 728.764 |

- 主站 RX_INSPECT 由 23.452/23.376 us 变为 64.572/35.444 us，RX_COMMIT 由
  12.420/18.296 us 变为 34.004/26.564 us；NO2 的 RX_REQUEST_HINT 由 6.928/6.712 us
  变为 34.824/30.464 us（有限窗口快照，非事实源）。候选 hint 包含 compact layout
  缓存核对/重建；完整检查迁移并未省去 Core1 当前授权和消费责任，新增复验成本
  不应忽略。各父子项为包含关系，禁止跨拍加减，不能将全部增长归因于单一函数。
  当前五轮全部 60 条 PEAK/RUN/OTHER、基线 48 条及实际 SLOT/RES、镜像长度/CRC
  均保留；其中首轮 SD 失败的额外窗口没有从高值比较中删除。
- 四轮 SD 完整窗口的稳定采样区间均有主站新版本物理提交及四板 RX 消费进展，
  接收 reject/missing、incomplete/map/length reject、TX publish reject 与 RX publish
  drop 增量为零；首轮失败 SD 的 RAM 窗口也完整分析。该证据仅证明有限交换进展，
  不证明逐圈特等数据保全或完整 WCET。
- 首次 P3 已完成四板 OTA，但 P0T 的 NO4→NO1 未检测到活动，exit=1 原件保留。
  四板 STOP/身份/build 复核后，用工具 resume 复用成功 OTA、重新软复位并执行真实
  验收；r2 quick diagnostic 的 strict=true、failures 为空。恢复通过不关闭初次间歇
  拓扑失败原因；不是 replay，凭证仅覆盖 quick diagnostic 范围。
- `profile-r1` 的 NO3 recorder SAVE 失败；现场 job 32 为 FILE_WRITE/error 6，
  RAM 的 26/26 条记录、12644 B 完整，missed/reason 为零，SD 读回为空。一次软复位
  后普通闭环通过，但 `sd-recovery-r1` 的新记录再次 SAVE 失败，job 1、12336 B，
  RAM 仍完整、SD 仍为空（本轮快照，非事实源）。两次其余三板的 SD 核对均通过。
  按用户后续 reboot 指示再次软复位后，`sd-recovery-r2` 普通闭环与 SD 核对通过，
  随后的补采、另一槽和普通恢复通过；仅证明本次恢复，SD 根因继续开放。
- 候选五轮自主记录的原始预算拒绝保持；START 后零查询、板端留证，STOP 后导出。
  P3、成功的 SD 恢复、四轮补齐采样和两次普通恢复共八组 STOP/SD 通过；最终四板
  STOP/config ACK、许可证退出及 SD 核对通过。`decision-r1.json` 明确撤回候选，
  已以恢复版新构建/P3/普通闭环封闭撤回，从前序基线推进可配置 Core1 周期。
- 恢复 build 为 `20260914004726`、源码指纹恢复为
  `685857c8b481970496834a8d68d3062cf8da5b6c87e13eeeec0550e51634fdd0`
  （本轮快照，非事实源）。七份生产文件无内容差异；软件 51 passed、完整 adapter
  harness、A/B/Boot 与 Flash link contract 通过。新 P3 quick diagnostic strict=true、
  failures 为空；两轮自主记录保留原预算拒绝，最终普通闭环各项通过。恢复 P3、两轮
  记录和普通闭环的四组 STOP/config ACK、许可证退出及 SD 字节核对通过。
  首次恢复收尾 helper 的字符串替换误改内层 checkpoint 匹配条件，仍读候选 build，
  四板均在身份/build 校验后、发送 STOP 前被拒绝；失败原件保留。r2 修正精确绑定
  后执行成功，没有放宽 build 校验。当前凭证只绑定恢复源码。
- 新周期需求及整表算术候选见
  `out/HardwareAcceptance/20260914/tdma-flight-configurable-period/candidate-schedules-r1.json`；
  四档都以 clk_sys 拍表达，并保留完整相位和 GUARD。隔离工作树纯表回归通过，
  首次 pytest 因临时目录父目录不存在失败的日志保留，修正目录后新标签运行通过。
  尚未安装运行时周期配置，也未授予长帧能力。此候选不重判本轮旧表失败。
- 原件入口：`source-checkpoint-r1.json`、`linkage-review-r2.json`、
  `slot-comparison-r1.json`、`rx-cost-review-r1.json`、`workload-review-r1.json`、
  `decision-r1.json`、`interruption-checkpoint-r1.json`、`restored-source-checkpoint-r1.json`、
  `review-final-r1.json`、`slice-manifest.json`、`commit-proof.json`；证据根为
  `out/HardwareAcceptance/20260914/tdma-flight-rx-unload-prepare/`。

### TDMA-PROGRESS-20260914-011 - 自主主站邮箱私有副本异步准备与双槽验收

- 日期：2026-09-14
- 状态：PARTIAL；采纳异步准备实现，软件、构建、当前源码 P3 快速诊断、两槽四轮
  记录、新版本准入及普通恢复闭合；完整 phase 稳定收益、正式 RAM 与产品验收未达。
- 延续 009 发布归因及 010 提前复用候选撤回，复用 `tdma_overlay_prepare_t`，以
  `TDMA_OVERLAY_PREPARE_ORIGIN` 区分纯本地邮箱准备。Core1 在工位 IDLE 时先由物理
  ready 服务 DMA selection 退休，再取得 FIFO 完整版本；已发布版本沿用原复用路径，
  新版本只复制本地固定邮箱和 layout，以 REQUESTED 交给既有 Core0 worker。
- Core0 校验私有副本的长度、slot、owner mask、generation、mailbox 格式、目标范围
  和 CRC，准备 `applied` 后发布 READY/FAILED。origin 工位的 `plan` 为空，不借出
  adapter、MMIO 或 DMA shadow；没有新增硬件写者。Core1 复验 epoch、active/map、
  local slot 和 payload，物理 commit 再核对工位身份、kind、READY/epoch、包长/slot
  及 healthy，然后执行有界 shadow 复制和发布；成功后才记 engine 接受计数和完整
  owner 版本。defer 保留 READY 重试，迟到/失败继续用旧值，STOP 等 worker ACK 后
  才复用工位。没有成对异步回调的 backend 保留同步路径，实际 runtime 已实链异步。
- 软件验证为 50 passed，另编译运行完整 adapter C harness（本轮快照，非事实源）。
  覆盖 callback 配对、暂停 BUILDING 时旧值保持、下一版本排队、私有副本、相同低位
  不同完整版本、pending selection 退休、READY 延后、成功后才 accounting、无更新
  复用、CRC 拒绝、stale map 和 STOP 取消。worker 覆盖四/五/六节点布局、source/target、
  长度/kind/epoch/物理 slot/包长拒绝，原 follower 与 legacy 回归保留。
- A/B/Boot 与 Flash link contract 通过。链接快照（非事实源）：工位由 448 增至
  452 B，adapter 由 7480 增至 7488 B，总静态增加 12 B；heap 预留保持 2048 B，
  heap 外余量从 48 降至 36 B。v9 的 51 项探针全部保留，recorder work/snapshot
  为 368/1496 B，PIO headers 与基线逐字节相同。编译容量 6、实环 4、payload/packet
  为 132/164 B；实现入口仍为 `PROJECT_NODE_CAPACITY`、active topology 和
  `TDMA_SERVICE_TIMING_VERSION`。此核算不提升正式 RAM 状态。
- 首次链接审计 `linkage-command-r1` 以 exit=1 原样保留：只检查代码反汇编，漏看
  `.rodata` 中回调指针的 relocation，属于审计脚本缺漏。修正后 r2 同时检查实际
  code/data relocation，排除 debug section；确认 A/B 的 owner、worker、adapter
  请求与物理 READY commit 均已实链。失败日志及两版反汇编原件不覆盖。
- 当前 build 为 `20260913231728`，源码指纹为
  `685857c8b481970496834a8d68d3062cf8da5b6c87e13eeeec0550e51634fdd0`
  （本轮快照，非事实源）。同 UID、同实际槽位与 010 恢复基线比较如下；NO1 为 RUN，
  其余为 OTHER，每格为独立两轮的完整外层，单位 us（有限窗口快照，非事实源，
  不是同拍配对或 WCET 证明）。

  | 节点 / 实际槽 | 010 恢复基线 | 异步邮箱准备 |
  |---|---|---|
  | NO1 / B | 661.060 / 669.236 | 671.252 / 661.372 |
  | NO2 / B | 719.440 / 704.992 | 695.068 / 705.732 |
  | NO3 / B | 696.472 / 698.624 | 700.004 / 709.544 |
  | NO4 / A | 659.472 / 667.772 | 690.352 / 681.960 |

- 同槽主站的 ORIGIN_PUBLISH 为 52.636/71.140 us，基线为 121.672/126.408 us，
  当前 RX_PARSE 为 250.400/234.536 us。另一槽主站 RUN 为 672.724/696.112 us，
  publish 为 75.960/86.592 us，RX_PARSE 为 237.980/208.088 us（有限窗口快照，
  非事实源）。保留当前全部 48 条 PEAK/RUN/OTHER 与基线 24 条，并绑定实际
  SLOT/RES、UID/build、包和镜像长度/CRC。发布分项下降但完整范围重叠，NO3/NO4
  的同槽高峰增长仍未隔离；不能归因于单一 CRC、槽位、代码布局或共享干扰。
- 四轮板端稳定样本窗口分别有主站成功 map_apply 增量 650/647/649/643，output_bytes
  增量为 20800/20704/20768/20576 B（有限窗口快照，非事实源）。当前异步路径仅在
  物理 commit 成功后记 map_apply，配合完整源码/实链和稳定 persona/state，证明
  有限新版本准入。四板接收均有进展，接收 reject/missing、TX publish reject 和
  RX publish drop 增量均零；采样边界下不同计数不要求一一对应。此证据不能识别
  独立峰值的请求/等待/提交分支，也不证明逐圈特等同步数据交付。
- P3 quick diagnostic 的 strict=true、failures 为空，普通 process-image 的
  passed/closed_loop/realtime/diagnostic 全过；四轮自主记录原始
  passed/closed_loop/realtime=false、diagnostic=true 保持。START 后零查询，板端
  留证，STOP 后导出并核对 SD。P3、四轮采样和两次普通恢复共七组 STOP/SD 均通过；
  两次普通闭环全过，最终四板 STOP/config ACK、许可证退出及 SD 字节核对通过。
- `decision-r1.json` 采纳非阻塞架构边界，不将其登记为完整省时收益。Core1 仍有
  有界复制/准入/accounting，Core0 处理耗时不能掩盖 Core1 的完整超限。下一 gate
  是主站 RX 准备结果的接受处理与从站捕获/请求，继续按同槽、完整记录定位增长，
  独立保留 STOP 成本。500 us、正式 RAM、逐圈物理节拍、特等保全及历史间歇故障
  仍开放；本轮未重现 SD 写失败不关闭 008 根因，NO5 和 registry 未改。
- 原件入口：`source-checkpoint-r1.json`、`linkage-review-r2.json`、
  `slot-comparison-r1.json`、`workload-review-r1.json`、`decision-r1.json`、
  `review-final-r1.json`；证据根为
  `out/HardwareAcceptance/20260914/tdma-flight-origin-async-mailbox/`。

### TDMA-PROGRESS-20260914-010 - 自主主站提前复用候选撤回与恢复验收

- 日期：2026-09-14
- 状态：PARTIAL；候选软件、构建、P3、两槽四轮记录及普通恢复完成；未证明完整
  phase 稳定收益，未采纳。恢复源码的软件、构建、P3 调试流程、两轮记录和普通
  恢复完成，严格失败保留，完整预算继续开放。
- 延续 009 发布归因，在 `tdma_pio_spi_ring_origin_publish()` 的物理 ready 之后、
  `tdma_flight_fifo_core1_acquire_tx()` 之前，尝试以既有
  `tdma_flight_fifo_core1_reuse_current_tx()` 提前复用已发布的完整版本。先服务 DMA
  selection 退休，仅空队列且 active owner/generation/sequence 一致时返回；坏描述符、
  新版本及延后发布继续原 acquire/validation。只省无更新时临时 view 构造，不绕过
  新版本的布局授权或完整性检查。
- 候选与撤回后软件回归各为 50 passed（本轮快照，非事实源）。候选测试覆盖 pending
  selection、退休后一次复用、坏描述符消费/拒绝、相同低位但不同完整版本、新数据准入、
  空队列下延后版本重试及失败/成功 STOP 的保留/清理；两份候选源码一起归档并恢复。
- 候选与恢复包 A/B/Boot、Flash link contract 通过。链接快照（非事实源）：编译容量
  为 6、实环为 4、payload/packet 为 132/164 B；v9 的 51 项探针保持，work/snapshot
  为 368/1496 B，heap 外余量保持 48 B。PIO headers 与前序逐字节一致，无新增静态
  RAM 或资源借用。容量/布局/计时仍以 `PROJECT_NODE_CAPACITY`、active topology 与
  `TDMA_SERVICE_TIMING_VERSION` 为实现入口，不提升正式 RAM 状态。
- 候选 build `20260913221822`、源码指纹
  `c1a8dade26301bc83dfabbece794caa59eb5041646c50afd09d19db4ed3ddffc`；恢复 build
  `20260913224213`、指纹
  `6d1cd151c66fe784d88054fe018ee244717485ffaecfeae621440d506be837bb` 与前序源码一致
  （本轮快照，非事实源）。`candidate-source/`、`candidate-source.patch` 与
  `decision-r1.json` 绑定撤回前原件；不使用旧凭证代替恢复包 P3。
- 009 主站稳定窗口 FIFO reuse/service 为 987/1716、977/1711，候选四轮为
  990/1715、989/1716、990/1716、994/1712（有限窗口快照，非事实源）。窗口计数说明
  复用机会存在，不能证明独立高峰走了复用分支，也不能把整段发布时间视为可省收益。
- 同 UID、同实际启动槽完整外层对照如下。NO1 为 RUN，其余为 OTHER；每格为两轮
  各自选中记录，单位 us（有限窗口快照，非事实源，不是同拍配对或 WCET 证明）。

  | 节点 / 实际槽 | 009 基线 | 提前复用候选 |
  |---|---|---|
  | NO1 / A | 629.668 / 655.348 | 635.172 / 647.540 |
  | NO2 / A | 692.492 / 698.088 | 705.392 / 724.084 |
  | NO3 / A | 706.400 / 703.424 | 713.280 / 717.372 |
  | NO4 / B | 674.444 / 662.208 | 672.692 / 663.872 |

- 同槽主站 RUN 的 ORIGIN_PUBLISH 从基线 107.244/117.772 us 变为候选
  145.488/149.208 us，RX_PARSE 为 145.944/146.944 us。另一槽主站 RUN 为
  680.664/638.512 us，发布分项为 138.584/108.600 us（有限窗口快照，非事实源）。
  分项变化不能抵扣成固定收益；新数据路径增加一次快速检查，但源码布局、调度与
  共享干扰尚未隔离，不能将全部变化归因于该检查或槽位。
- 候选 slot-r1 主站 OTHER 为 759.472 us、body 为 728.944 us，入口自主、出口停止，
  ORIGIN_PUBLISH 未执行（有限窗口快照，非事实源）。这是独立 STOP 成本线索，仍计入
  完整预算；候选四轮 48 条 PEAK/RUN/OTHER 与基线 24 条全部保留，不用较低 RUN 覆盖
  其它高值。完整静态预算未达，增长因果仍未闭合。
- 候选 P3 quick diagnostic 流程 strict=true，普通 process-image 的
  passed/closed_loop/realtime/diagnostic 全过；四轮自主记录原始
  passed/closed_loop/realtime=false、diagnostic=true 保持。START 后零查询，各轮
  STOP 后导出并核对 SD，两次普通恢复及四板 STOP/config ACK、许可证退出通过。
  调试窗口不升级为正式产品 P3、物理节拍或 CPU/RAM 验收。
- 恢复源码 P3 调试流程完成但 strict=false：TRN-01 SCK training 拒绝，TRN-03 未选出
  满足 flight re-arm 预算的实测 SCK 行。P3 普通 process-image 闭环全过；随后使用
  已固定矩阵的两轮自主原始 passed/closed_loop/realtime=false、diagnostic=true
  保持，不能以有界调试继续覆盖前述拒绝。
- 恢复包实际槽位为 NO1/NO2/NO3 B、NO4 A；UID、build、包、镜像长度/CRC、启动来源
  和全部 24 条 PEAK/RUN/OTHER 由 `restored-review-r1.json` 绑定。主站 RUN 为
  661.060/669.236 us、发布分项为 121.672/126.408 us；从站 OTHER 为 NO2
  719.440/704.992 us、NO3 696.472/698.624 us、NO4 659.472/667.772 us（有限窗口
  快照，非事实源）。同源码恢复不代表时序问题消失，不将不同槽的基线直接相减。
- 恢复两轮 START 后零查询，STOP 后 SD 字节核对通过；最终普通闭环、四板
  STOP/config ACK、许可证退出及 SD 核对通过，只关闭本轮恢复。本轮没有新增 SD
  写失败；008 的两次 NO3 SD 写失败及历史生命周期间歇故障继续开放；NO5 和 registry
  未改。后续按角色分离新版本准入/发布、RX 接受与 STOP 成本，完整预算不扣除诊断。
- 原件入口：`source-checkpoint-r1.json`、`baseline-load-review-r1.json`、
  `current-load-review-r1.json`、`slot-comparison-r1.json`、`decision-r1.json`、
  `restored-source-checkpoint-r1.json`、`restored-review-r1.json`、`review-final-r1.json`；证据根为
  `out/HardwareAcceptance/20260914/tdma-flight-origin-early-reuse/`。

### TDMA-PROGRESS-20260914-009 - 自主主站边界读取与本地发布归因

- 日期：2026-09-14
- 状态：PARTIAL；软件、构建、当前源码 P3 流程、两轮实板归因与普通恢复完成；
  保持完整预算和全部原始峰值，诊断细化不计优化收益。
- 延续 008 的同拍残差线索，在 `tdma_pio_spi_ring_origin_service()` 增加
  `TDMA_TIMING_ORIGIN_OBSERVE` 与 `TDMA_TIMING_ORIGIN_PUBLISH`。两者为 ADAPTER
  内、RX_HANDOFF 外的独立区间；保留前置健康检查、RX、observe、publish 的操作
  顺序与 owner 边界，未改变自主发车、shadow 选择、DMA、wire 或 STOP 状态迁移。
- 计时版本由 `TDMA_SERVICE_TIMING_VERSION` 声明，解码由 `STAGES_BY_VERSION`
  分派；追加字段保留原索引，不复用从站 overlay/latch 字段。observe 包括成功时
  将 boundary 写入 adapter 的工作，publish 包括 ready、FIFO、版本/授权/邮箱
  完整性校验和延后、无更新返回。健康检查拒绝时两者不执行，从站不报告这两项。
- 相关软件回归为 50 passed（本轮验证快照，非事实源）。真实 adapter/engine/FIFO
  测试以不同耗时的物理回调区分两项，覆盖空队列、新版本、pending、相同版本复用、
  发布延后、无效邮箱、无新 observation 及故障；同时保留既有请求/调度分解、
  origin 准入、构建、记录与 STOP 生命周期回归，旧格式和截断拒绝继续验证。
- A/B/Boot 构建与实链复核通过；ARM/map 快照（非事实源）：v9 为 51 项，work
  由 360 B 增至 368 B，snapshot 由 1464 B 增至 1496 B，总静态记录占用增加
  40 B；heap 外余量由 88 B 降至 48 B，heap 预留未变。PIO headers 逐字节相同，
  没有借用额外 PIO/SM/DMA 或硬件缓冲区；正式 RAM 门禁仍未闭合。
- 当前 build 为 `20260913215117`，源码指纹为
  `6d1cd151c66fe784d88054fe018ee244717485ffaecfeae621440d506be837bb`（本轮快照，
  非事实源）。计时与记录成本仍保留在完整调度口径中；不能把 v8/v9 峰值差异
  当作固定优化收益，也不能将嵌套分项或不同角色、不同拍的最大值相加。
- 两轮自主主站 RUN 外层高峰均走准备结果接受分支，新增两项各执行一次。下表为
  各自同拍记录的包含式分项（us，有限窗口快照，非事实源；行间不能重复相加）：

  | 项目 | 第一轮 | 第二轮 |
  |---|---|---|
  | 完整外层 | 629.668 | 655.348 |
  | body | 609.708 | 624.112 |
  | RX_PARSE | 200.068 | 203.688 |
  | ORIGIN_OBSERVE | 6.316 | 9.756 |
  | ORIGIN_PUBLISH | 107.244 | 117.772 |
  | adapter 扣除同拍直接子项后的余量 | 34.532 | 45.332 |

- 两轮 sequence 为 428/3，实际主站启动来源为槽 A；UID、build、包哈希、槽位、
  镜像长度/CRC 与原始记录已绑定。余量公式由 `attribution-review-r1.json` 明确
  保存，包含控制工作、探针和可能的干扰，不能视为可直接省掉的时间。发布路径
  明显大于边界读取，后续优先检查本地新版本的准入、重复校验和发布工位成本；
  现有记录不能区分单次 ready、FIFO、校验或硬件写入，不能提前归因于某一子操作。
- 从站两轮 OTHER 外层分别为 NO2 692.492/698.088 us、NO3 706.400/703.424 us、
  NO4 674.444/662.208 us（有限窗口快照，非事实源）。这些高峰均走 capture/request
  分支，RX_PARSE 与两项 origin 分项未执行；不能将主站发布或解析成本套到从站。
  原 PEAK、RUN、OTHER 及其全部分项均保留，完整 500 us 仍未达。
- 当前源码 P3 的 quick diagnostic 流程完成但 strict=false：粗 CLK 校准时 NO2
  `SYSTem:TDMA:RING:TOPology 4,1,0` 应答超时，原命令和失败摘要保留；后续普通
  process-image 闭环的 passed/closed_loop/realtime/diagnostic 均为 true，只证明
  该普通窗口。两轮自主窗口原始 passed/closed_loop/realtime=false、diagnostic=true
  保持，调试继续不构成正式产品 P3 或完整 CPU/RAM 验收。
- 两轮 START 后零查询，STOP 后导出并核对 SD；最终普通闭环、四板 STOP/config
  ACK、许可证退出与 SD 字节核对通过，本轮没有新增 SD 写失败。前序 008 的存储
  失败根因和历史生命周期间歇故障仍开放；NO5 未操作，registry 未变。保留本次
  计时细化以支持后续定位，不宣称生产省时、增长因果解决或长期目标完成。
- 原件入口：`source-checkpoint-r1.json`、`linkage-review-r1.json`、`origin-host-r1.json`、
  `p3-r1/diagnostic.json`、`attribution-review-r1.json`、`review-final-r1.json`；
  证据根为 `out/HardwareAcceptance/20260914/tdma-flight-origin-service-attribution/`。

### TDMA-PROGRESS-20260914-008 - RESET 有效位退休候选撤回与 SD 失败保全

- 日期：2026-09-14
- 状态：PARTIAL；候选验证与源码撤回完成，不计生产优化收益，完整目标继续。
- 延续 007 的 RESET 首拍线索，候选在既有 Core1 seqlock 内用私有有效位退休记录，
  Core0 只在成功复制并复验 guard 后规范化自己的副本。RESET 不再清整个 snapshot，
  每拍 work 初始化、generation、公开计时格式与全部探针保留。有效位必须独立于
  sequence：回绕后的 sequence=0 仍可能是合法记录，不能据此删除。
- 软件回归为 62 passed；确定性混合动作与前序原实现进行 100000 步逐字节 snapshot
  差分一致（本轮验证快照，非事实源）。覆盖 RESET 延迟/合并、空记录、有效峰值、
  回绕和旧 scheduler notification 取消；该回放不构成并发内存模型或硬件 WCET 证明。
  首次 host 命令引用不存在的测试文件，exit=4、零测试；修正入口后的原始日志均保留。
- 候选 A/B/Boot 构建通过。ARM/map 快照（非事实源）：RESET 路径减少一次 1464 B
  snapshot 清零，360 B work 清理保留；私有有效位增加 1 B 静态占用并使用既有布局
  余量，heap 外仍为 88 B，不能称为零 RAM 占用。PIO headers 逐字节相同。首次符号
  审计误选其他模块同名局部符号，exit=1；按所属 recorder 对象定位后通过，非链接失败。
- 下表为同 UID、实际槽位下两轮保留的完整外层峰值范围（us，有限窗口快照，非事实源）；
  基线来自 006 的恢复源码双槽窗口，候选来自本轮。不同窗口不是同拍配对，也不是 WCET：

  | 节点/选拍/槽位 | 基线两轮范围 | 候选两轮范围 |
  |---|---|---|
  | NO1 RUN A | 608.852–630.380 | 648.416–692.724 |
  | NO1 RUN B | 618.580–619.512 | 651.200–701.984 |
  | NO2 OTHER A | 706.908–726.904 | 724.344–736.124 |
  | NO2 OTHER B | 710.148–725.988 | 715.820–726.816 |
  | NO3 OTHER A | 667.440–692.212 | 720.708–729.276 |
  | NO3 OTHER B | 692.184–708.308 | 692.456–719.108 |
  | NO4 OTHER A | 669.632–683.868 | 673.756–685.012 |
  | NO4 OTHER B | 650.508–676.316 | 663.580–687.812 |

- 全部组的候选最大值高于该基线；与紧邻 007 恢复包的同 UID/槽位比较，仅主站最大值
  下降，也不能建立稳定的完整收益。因此撤回候选，不等于已证明候选导致增长。候选
  第一槽主站两轮高峰为 sequence=860/1318，均非 RESET 首拍；body 分别为
  676.580/631.432 us，外围为 25.404/19.768 us（同拍快照，非事实源）。RESET 批量
  初始化不是常规高峰的主要解释，007 的外围差值不能全部解释为 memset 或扣减门禁。
- 候选 `slot-r2` 与原参数补采 `slot-r3` 均在 NO3 保存失败：storage job 为
  `FAILED,FILE_WRITE`，error=6 对应 `STORAGE_MANAGER_ERROR_WRITE_FAILED`；接收字节
  与预期一致、CRC 相符，读回目标文件却为 0 B（本轮失败快照，非事实源）。两轮
  RAM 导出、计时及 STOP/config ACK/许可证退出有效，其余三板 SD 字节相符；不能将
  取证命令 exit=0 或另开 epoch 的 release 标志写为原 SAVE 通过。
- 两个 SD 失败窗口的全部 PEAK/RUN/OTHER 均保留，包括 `slot-r2` 的 NO3 OTHER
  749.384 us（有限窗口快照，非事实源），没有因保存失败而丢弃高值。只软复位 NO3，
  核验同 UID/端口/build 后，`normal-r2` 普通闭环、`slot-r4` 补采和 `normal-r3`
  普通恢复的 STOP/SD 均通过；没有擦卡、格式化或删除文件。SD 写失败根因尚未隔离。
- 候选 P3 的本轮 `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC` 严格检查通过，仍不是正式产品
  P3 或完整预算验收。候选源码已归档并撤回，恢复源码相关回归为 62 passed（本轮
  验证快照，非事实源）。恢复 A/B/Boot 与四板 OTA、校准、普通环路检查通过；恢复
  P3 流程完成但 strict=false，仅保留总流程耗时 462.835 s 超过 450.000 s 的拒绝
  （诊断快照，非事实源），不放宽时限或提升正式产品验收声明。
- 恢复包两轮完整外层峰值如下（us，有限窗口快照，非事实源；实际启动槽位已与
  候选包绑定，不是同拍配对）：

  | 节点/选拍/槽位 | 第一轮 | 第二轮 |
  |---|---|---|
  | NO1 RUN B | 618.868 | 607.964 |
  | NO2 OTHER B | 725.000 | 719.976 |
  | NO3 OTHER B | 706.792 | 688.868 |
  | NO4 OTHER A | 666.824 | 647.780 |

- `restored-residual-review-r1.json` 保留全部 PEAK/RUN/OTHER，并对同条记录的互不
  重叠子区间作差。主站两轮高峰走 READY 结果接受分支，RX_PARSE 为
  229.660/203.708 us；从 adapter 扣除 prologue、handoff、overlay prepare/boundary
  与 status 后，余下 164.152/173.536 us。从站高峰均走 capture/request 分支，
  RX_PARSE 未执行，adapter 余下 36.684–41.608 us（同拍分项快照，非事实源）。
  主站下一步分解 `tdma_pio_spi_ring_origin_service()` 中的 observe/publish 和 RX
  接受；从站继续 capture/request、latch 及准备准入归因。这些差值包含控制工作、
  探针及可能的运行干扰，不是已经证明的可节省时间，不可重复加到父项或跨角色混算。
- 恢复包两轮 START 后零查询、STOP 后导出并核对 SD 字节；自主窗口原始
  passed/closed_loop/realtime=false、diagnostic=true 保留。最终普通闭环、四板
  STOP/config ACK、许可证退出及 SD 字节核对通过；没有新增 SD 写失败。当前 build
  为 `20260913212611`，源码指纹为
  `5ca17b871eb56d5a8847874e5419dba0aa44cb8f19e7b3ba23bddc5511af548d`（本轮快照，
  非事实源），无生产实现差异；NO5 未操作、registry 未变，完整预算、正式 RAM、
  逐圈物理节拍、SD 失败根因及增长因果仍开放。
- 原件入口：`candidate-source.json`、`differential-review-r1.json`、`linkage-review-r1.json`、
  `phase-comparison-r1.json`、`latest-baseline-comparison-r1.json`、`decision-r1.json`、
  `slot-r2-sd-inspection-r1.json`、`slot-r3-sd-inspection-r1.json`、`no3-reset-r1.json`、
  `rollback-source-r1.json`、`restored-checkpoint-r1.json`、`p3-restored-r1/diagnostic.json`、
  `restored-slot-review-r1.json`、`restored-residual-review-r1.json`、`review-final-r1.json`；证据根为
  `out/HardwareAcceptance/20260914/tdma-flight-timing-reset-retirement/`。

### TDMA-PROGRESS-20260914-007 - 时钟快速路径候选撤回与 RESET 首拍归因

- 日期：2026-09-14
- 状态：PARTIAL；候选验证与源码回退完成，不计生产优化收益，完整目标继续。
- 延续 O2/O4 的时间戳读取/重装归因，候选在 VDC 时钟初始化时缓存精确整数 ns
  比例，让 `vdc_timestamp_clock_now_ns()` 驻 SRAM；每次仍读取新 timer ticks，
  分数周期与零频率保留通用换算。初始化、epoch、回绕和全部计时探针语义保持。
  候选两版与恢复源码的相关 host 回归各为 25 passed（本轮验证快照，非事实源）。
- 首次链接失败且未 OTA：初始化被内联入 SRAM 入口，跨越接收 workspace 的
  对齐边界，使 BSS 后移并溢出 RAM。失败源字节、map 和日志保留；显式禁止冷路径
  内联后，A/B/Boot 构建通过。ARM/map 快照（非事实源）：read/now 分别为 40/36 B，
  init/通用换算为 160/124 B；新增静态 4 B，heap 外余量由 88 B 变为 84 B。
  PIO 产物逐字节相同，没有释放 PIO 空间或改变 wire 布局。
- 候选与前序恢复包分别按板卡及实际启动槽位对应；下表为两轮窗口保留的完整外层
  峰值范围（us，有限窗口快照，非事实源；不是同拍配对，不代表 WCET）：

  | 节点/选拍/槽位 | 基线两轮范围 | 候选两轮范围 |
  |---|---|---|
  | NO1 RUN A | 608.852–630.380 | 605.704–659.596 |
  | NO1 RUN B | 618.580–619.512 | 652.188–666.036 |
  | NO2 OTHER A | 706.908–726.904 | 712.336–731.268 |
  | NO2 OTHER B | 710.148–725.988 | 720.976–730.516 |
  | NO3 OTHER A | 667.440–692.212 | 704.512–709.968 |
  | NO3 OTHER B | 692.184–708.308 | 702.560–725.060 |
  | NO4 OTHER A | 669.632–683.868 | 701.844–745.520 |
  | NO4 OTHER B | 650.508–676.316 | 684.860–695.832 |

- 各组候选最大值均高于基线；在没有完整收益证据且增加 RAM 的情况下撤回候选。
  这不等于已经隔离代码布局、缓存/总线/IRQ 或实际工作的增长机制。全部选拍和
  PEAK/RUN/OTHER 原件保留 `TDMA_SERVICE_TIMING_VERSION`、stage ticks/calls、
  UID、build、包哈希、槽位、镜像大小/CRC，不能将嵌套分项重复求和。
- 候选 `slot-r3` 的 NO1 RUN 为 RESET 后 sequence=1：外层 666.036 us、body
  558.656 us，body 外为 107.380 us（同拍快照，非事实源）。代码确认
  `tdma_service_timing_phase_begin()` 在 body 起点前清整个 snapshot；外围还含
  正常 work 初始化、发布和边界读取。因此该差值不是实测 memset 时间，不允许
  扣减预算。下一切片先分解 RESET 初始化与发布，再验证按已有失效标志有界退休；
  必须保持 Core1 所有权、Core0 单次快照、generation、空记录输出和全部探针语义。
- 候选第二槽位的 `slot-r2` 在 NO1 临时授权应答处超时，reset generation 缺失，
  分析正确拒绝；原失败与 STOP/SD 原件保留。原参数补采 `slot-r3` 通过证据核验，
  没有覆盖失败或增大超时。采样 START 后零查询、STOP 后导出；自主窗口原始
  passed/closed_loop/realtime=false、diagnostic=true 保留，普通恢复不提升自主段。
- 候选 P3 的本轮 quick diagnostic 严格检查通过；恢复源码的同范围流程完成但
  strict=false，保留 NO3 OPMode APPLY 应答超时、TRN-01 SCK、TRN-03 重装余量
  拒绝和验收总流程超时。二者均不是正式产品 P3 或完整预算验收，不能关闭历史
  间歇故障。恢复 A/B/Boot 构建通过，heap 外余量回到 88 B（map 快照，非事实源）。
- 恢复包两轮主站 RUN 外层为 729.632/636.056 us；NO2 OTHER 为
  704.008/712.456 us，NO3 为 695.396/668.400 us，NO4 为 680.344/661.588 us
  （有限窗口快照，非事实源）。主站高值为 sequence=1376，body 695.652 us、
  外围 33.980 us；其中 owner 611.804 us，嵌套 RX handoff 257.084 us、
  RX parse 229.080 us，不能重复求和。该拍不是 RESET 首拍，说明 RESET 初始化
  不是全部增长的解释，撤回候选也未关闭波动。按实际槽位配对及全部分项已保存；
  两轮 STOP/SD、最终普通闭环、STOP/config ACK、许可证退出及 SD 字节核对通过。
- 当前恢复 build 为 `20260913202953`，源码指纹为
  `5ca17b871eb56d5a8847874e5419dba0aa44cb8f19e7b3ba23bddc5511af548d`（本轮快照，
  非事实源）。候选源码快照与恢复源码分别绑定各自构建和凭证；无生产实现差异，
  NO5 未操作，registry 未变；完整 500 us、正式 RAM 和增长因果仍开放。
- 原件入口：`candidate-source.json`、`link-failure-r1.json`、`phase-comparison-r1.json`、
  `reset-first-phase-r1.json`、`rollback-source-r1.json`、`restored-checkpoint-r1.json`、
  `p3-restored-r1/diagnostic.json`、`restored-slot-review-r1.json`、`review-final-r1.json`。

### TDMA-PROGRESS-20260914-006 - latch 初始化候选撤回与同包跨槽位对照

- 日期：2026-09-14
- 状态：PARTIAL；候选验证与回退闭环完成，无生产实现差异，不计优化收益。
- 延续 O2/O4 的读取/重装分解，候选只将禁用状态下 RX/TX latch 的
  `put UINT32_MAX → PULL → MOV X,OSR` 改为 `MOV X,~NULL`。FIFO 清理、SM restart、
  PC、epoch-before-enable、许可/状态检查和所有探针保持。两处 CPU helper 的静态
  指令分别由 103→92、73→62（ARM 快照，非事实源）；PIO 程序字节相同，原 latch
  不消费 OSR。候选与恢复后的相关 host 回归各为 32 passed，A/B/Boot 均构建通过。
- 初始“候选→旧包→候选”的同探针对照中，NO2 OTHER 外层候选四轮为
  723.396–736.432 us，旧包两轮为 699.760–710.348 us（有限窗口快照，非事实源）。
  没有稳定的完整收益，候选已撤回；该决定不等于已经证明改动造成增长。
  原候选源码字节、patch、build、失败与采样均保存，不能用恢复源码替换候选证据。
- 进一步核对发现初始三批 NO1–NO3 槽位为 B→A→B，NO4 为 A→B→A（启动快照，
  非事实源）。源码和槽位同时变化，原对照不能隔离代码效应；候选还移动了其他
  函数布局。主站保留的 RUN 峰值中 RX/TX latch 读取/重装均未调用，主站增长不能
  直接解释为这些 helper 的单次执行成本。缓存、总线和 IRQ 干扰尚无隔离证据。
- 恢复源码重新构建、真实 P3 后，用同一个包在两槽位各采集重复窗口。以下为
  各窗口保留的完整外层峰值范围（us，有限窗口快照，非事实源；不是同拍配对）：

  | 节点/选拍 | 第一槽位/两轮范围 | 另一槽位/两轮范围 |
  |---|---|---|
  | NO1 RUN | A：608.852–630.380 | B：618.580–619.512 |
  | NO2 OTHER | A：706.908–726.904 | B：710.148–725.988 |
  | NO3 OTHER | A：667.440–692.212 | B：692.184–708.308 |
  | NO4 OTHER | B：650.508–676.316 | A：669.632–683.868 |

- 同包各节点的两组范围重叠，当前不能建立固定的槽位增量，也不能证明槽位无影响。
  每个样本绑定 UID、build、包哈希、active slot、启动来源槽、镜像大小和 CRC；
  `SYSTem:OTA:SLOT?` 读取的是 metadata，须与 `SYSTem:OTA:RES?` 及包内 image 对应。
  COMMIT 后的 confirmed slot 查询保留原值，不假设同步更新。全部保留拍的
  `TDMA_SERVICE_TIMING_VERSION` 与枚举分项、ticks/calls 原样存档，不混加嵌套项。
- 本轮采样 START 后零查询、STOP 后导出，板端记录及 SD 逐字节核对通过；自主
  采样原始 passed/closed_loop/realtime=false、diagnostic=true 保留，普通恢复通过
  不能提升整段自主窗口。候选、旧包复查和最终恢复各轮的 STOP/config ACK、许可
  退出和普通闭环均留证。初次旧包 OTA 后误用运行后收尾检查，因许可 UNAVAILABLE
  与未加载矩阵而失败；原失败保留，另行只核验配置前的硬件 STOP 与 config ACK，
  不伪造许可失效，也不放宽运行后的原收尾检查。
- 候选 P3 的 coarse CLK topology 响应超时保留，strict=false；恢复源码本轮
  `FOUR_NODE_TDMA_QUICK_DIAGNOSTIC` 的 failures 为空、strict_gates_passed=true。
  此字段只描述该诊断范围，不是正式产品 P3、完整 WCET 或历史间歇故障的关闭证明。
  恢复 A/B 的 heap 外余量仍为 88 B（map 快照，非事实源），正式 RAM 未通过。
- 最终 build 为 `20260913192602`，源码指纹恢复为
  `5ca17b871eb56d5a8847874e5419dba0aa44cb8f19e7b3ba23bddc5511af548d`（本切片快照，
  非事实源）。默认凭证绑定恢复源码，候选未进入生产；NO5 未操作，registry 不变。
  后续按实际槽位和镜像分组，继续隔离 RX/latch 有效工作与布局、缓存/总线/IRQ
  干扰，不能凭静态指令减少、某轮低值或重刷恢复宣布完整 500 us 达标。
- 原件入口：`candidate-source.json`、`rollback-source-r1.json`、`layout-review-r1.json`、
  `slot-review-restored-r1.json`、`p3-restored-r1/diagnostic.json`、`review-final-r1.json`。

### TDMA-PROGRESS-20260914-005 - 自主 origin 有限 service 屏蔽实板验证

- 日期：2026-09-14
- 状态：PARTIAL；主站有限 service 主体屏蔽和 DMA 进展验证完成，完整目标保持进行中。
- 新增 `CALIBRATION_ORIGIN_DIAGNOSTIC_SERVICE_BLACKOUT`，经既有 Calibration 易失
  许可证与 TDMA owner 准入。SCPI 仅发布试验意图；owner 等待自主态稳定后，在
  `tdma_component_core1_service()` 主体之前执行有限门控，跳过 physical、owner、
  RefMem publish 和 training gate。屏蔽时只读授权与 DMA 档案版本，不读 FIFO、
  不收割 RX、不选取新 payload、不重装 PIO/DMA；没有借用 OTA skip 路径。
- 等待和跳过次数、区间检出上限来自 `tdma_origin_blackout.h` 的
  `TDMA_ORIGIN_BLACKOUT_SETTLE_CALLS/SKIP_CALLS/MAX_INTERVAL_US`。撤销、STOP、配置
  或时钟代际变化、硬件不就绪及超限会取消试验，恢复既有 owner STOP/退休路径。
  上限在后继 service 入口检查，不是独立硬件 watchdog 的强制恢复保证。终态快照
  使用版本保护；完成后立即退休 DMA 保留档案，新的准入会替换旧试验快照。
- 两轮实测快照（非事实源）：分别连续跳过四次完整 service 主体，CPU 边界区间为
  4023.392/4068.688 us，DMA 档案发布版本分别从 132→140、130→138；各保留四个
  区间内完整返回，sequence 为 587–590、588–591。epoch 与双端 sequence 一致，
  transport checked、capture/output remaining、RTT present 和 fault 检查通过。
  SCPI 在 START 后零查询，STOP 后统一导出；各窗口 SD 字节核对通过。
- 第二轮记录 local generation 25→26（快照，非事实源）。屏蔽前已发布的待选 shadow
  可以在屏蔽后由 DMA 选取，因此不能要求整个区间 generation 恒定；当前 watermark
  快照没有绑定屏蔽前的 pending selection 租约，本轮不声明新的 generation 准入证明。
- 本轮证明有限主站区间内硬件继续完成传输；CPU 边界时间与旧 `FORMAT_RTT` 档案
  不能证明逐帧物理抖动、绝对 VDC 时间、全部从站同时屏蔽或任意长度的逐圈保全。
  屏蔽区间的低 CPU 耗时不作为完整 phase 的优化收益。
- 未屏蔽对照快照（非事实源）：主站 RUN 外层为 622.952/620.976 us；NO2 OTHER
  为 691.924/704.320 us，NO3 为 692.900/700.740 us，NO4 为 678.896/679.576 us。
  当前与前序保留拍并非同拍配对，完整 500 us 仍未达到，增长因果继续开放。
- 软件快照（非事实源）：十项许可证/屏蔽回归及十二项档案、构图取消、DMA 和 NO5
  隔离回归通过；真实包装函数验证整个主体跳过与恢复 STOP。首次 host 编译因测试桩
  static 声明冲突失败，修复后通过，原日志保留。
- 首次 A 链接失败：诊断慢路径被内联入 SRAM 快速入口，跨越对齐边界导致 BSS
  后移。失败源字节、map 和日志保留；明确禁止慢路径内联后，A/B/Boot 构建通过。
  ARM 快照（非事实源）：入口 24 B 驻 SRAM，慢路径 440 B 驻 Flash，新增静态记录
  64 B；workspace 保持原地址和大小，A/B heap 外余量由 152 B 变为 88 B。PIO 产物
  字节相同，正式 RAM 未通过，没有扩大预算、超时或存储区域。
- 当前源码 P3 首轮在 NO4→NO1 邻接漏检处失败。独立复核检出四链路，但继承串口
  默认读取粒度与 P3 不同，不能据此关闭因果；恢复原 P3 时序的第二轮拓扑通过，
  仍有 TRN-01 SCK、TRN-03 重装余量及短帧启动屏障拒绝，诊断流程完成、strict=false。
  首轮清理报告因校准前与基线矩阵比对而未通过，四板实际 STOP/配置确认/许可证失效
  均已记录。进入屏蔽前补做同矩阵普通短帧闭环通过；最终普通恢复、STOP 和 SD 通过。
- 当前 build 为 `20260913182559`，源码指纹为
  `5ca17b871eb56d5a8847874e5419dba0aa44cb8f19e7b3ba23bddc5511af548d`（本切片快照，
  非事实源）。NO5 未操作，registry 状态不变；后续继续从站证据路径、完整 WCET、
  严格校准、绝对时间、generation 准入及正式 RAM 闭环。
- 原件入口：`blackout-r1-review.json`、`blackout-r2-review.json`、`linkage-review-r1.json`、
  `p0t-recheck-context.json`、`p3-r2/diagnostic.json` 与 `review-final-r1.json`。

### TDMA-PROGRESS-20260914-004 - 连续计数器压缩与记录池复用核算

- 日期：2026-09-14
- 状态：PARTIAL；离线指令与 ARM 布局核算完成，固件及板端状态未改变。
- 新方案压缩计数器自身，保留生产 DATA 字节路径。使用 SM wrap 处理高电平路径
  的 X=0，下降路径零值直接落入 LOW，配合 delay 保持所有路径同样的计数节拍。
  指令模型快照（非事实源）：由 10 条减到 8 条，每次轮询仍为五个时钟；当前时钟下
  派生量化为 20 ns，不代表绝对时间精度。
- 验证快照（非事实源）：继承的 1460 个波形、200 个双计数器回绕用例通过；新增
  35840 个电平序列/初相/X 初值组合逐项比较旧、新汇编程序，轮询时间、计数值、
  FIFO PUSH 和丢失记录完全相同。错误 delay 与 wrap 负测被拒。首次 LOW 伪记录、
  短脉冲漏采、FIFO 满和 blocking PUSH 破坏映射的反例继续保留。
- 空间快照（非事实源）：自主 origin 若替换旧 RTT 与保留 latch，TX catalog 可从
  32 条变为 30 条；已有 RTT 本来由 DMA 重装，不能算新 Core1 收益。follower TX
  的候选占用为 18 条，但保持现有 DATA 时 RX 仍需 36 条，不能直接安装；不能
  因共享程序可运行就静默迁移 RX 端点到 TX。实际资源、入口及 persona 生命周期
  仍须按 `tdma_state_machine_resources.h` 和 program manager 重新核验。
- ARM ABI 快照（非事实源）：采用当前固件编译选项，单独编译尺寸对象并核对当前
  map；workspace/origin 为 7712 B，service 为 7696 B，现成 follower 余量仅
  16 B，后方存在 480 B 链接对齐填充。旧 origin TX 数组 1232 B，follower 双计划
  2368 B；前者仅 MASTER/FLIGHT_ORIGIN 路径使用，后者公共入口要求
  SLAVE/FLIGHT_PROCESS_FOLLOWER，为进一步互斥复用提供依据。
- 独立布局原型快照（非事实源）：保持 RX ring 起点，嵌套 union 复用上述互斥数组，
  放入两路各 32 word、按 128 B 对齐的原始环及 1024 B 不透明记录预算，workspace
  为 7808 B，较当前增加 96 B；原始环/记录预算偏移为 6528/6784 B。原型没有进入
  固件，记录预算不是冻结结构，现有对齐填充也不是可用 RAM 凭证；必须用真实实现
  重新链接 A/B，核验地址、代码驻留、STOP/DMA/Core0 退休及消费期限。
- 自主 origin 的既有 AL3 executor 可作为双 FIFO 收取候选，避免直接新增两路 DMA；
  但必须同时退休旧暂停/清 FIFO/重装路径，并证明每圈边沿数量、实际 CONTROL 身份、
  完整 capture/trailer 和版本一致。`tdma_origin_observation_t` 与现有
  `TDMA_ORIGIN_RECORD_FORMAT_RTT` 不能被静默改作绝对计数器；需要明确记录格式、
  getter/解析器及 VDC/Calibration 时钟映射。不把计数器入环等同于特等席交付。
- 首次 ARM 尺寸命令因 Windows 引号解析和缺少 build-job 头失败，原日志保留；
  修正命令解析及审计源后，当前布局和独立原型均编译通过，未扩大超时或运行预算。
- 本轮仅离线汇编、指令执行及 ARM 尺寸对象，无新固件构建、OTA 或 P3 声明；
  当前源码指纹仍为 `280f9ddcba018de2068c79541b00263d07fa199f0e0e3f4b8f972d2f857afd6f`，
  板端最后验证版本仍是 `20260913163720`。完整 WCET、严格 P3、正式 RAM、实际
  service blackout 与逐圈绝对时间/身份保全继续开放；registry 状态不变。
- 原件入口：`working-design.txt`、`edge-model-r1.json`、`equivalence-r1.json`、
  `workspace-audit-r2.json`、`workspace-audit-r3.json` 和 `review-final-r1.json`。

### TDMA-PROGRESS-20260914-003 - follower PIO 共尾压缩反例与回退

- 日期：2026-09-14
- 状态：PARTIAL；候选已拒绝并回退，取证与恢复闭合，长期目标继续。
- 目的：为连续边沿计数腾出 follower RX 指令空间。候选用 Y 区分普通第八位与
  末位，共享采样及 ISR/FIFO 保全尾部；同步调整 terminal PC 白名单、WAIT 补丁和
  byte 重装计算，不增加 DMA、SM 或缓冲池，不安装连续计数器。
- 软件与构建快照（非事实源）：候选 DATA 程序由 28 条压到 22 条，含旧 latch 的
  follower RX 由 32/32 降到 26/32；代价是完整 byte 重装项由 7 增到 8 个时钟。
  离线候选 216 项、生产候选流水线等 232 项、资源/DMA 34 项通过；C 计划覆盖六、
  八节点容量分别 122880/163840 个 ownership/alignment 用例。ARM 测试另有两个
  既有 fixture 偏差：缺少 node-count 参数及禁用 staging 时仍预期调用 map；两次
  失败、候选内修正和最终 1 项通过均留证。修正随拒绝的候选一并归档，生产测试恢复
  原版，该既有测试缺口仍需后续处理。
- 否决依据：`tdma_pio_spi_clkdiv_for_baud()` 与 `tdma_origin_cadence_calculate()`
  使用硬件可表示的分频。当前配置的离线快照（非事实源）为 divider256=1066、
  每 bit 六个 PIO 周期，实际 bit 间隔包含 24/25 个 `clk_sys` 时钟；DATA delay=15
  时候选完整 byte 需要 25 个时钟。`fractional-probe-r1.json` 覆盖 128 个分频初相、
  每相两帧，原版相位偏差为零，候选出现 692 个晚一个时钟的采样；当前频率下对应
  4 ns。此为指令模型反例，不是实板测得的抖动。只在固定整数 bit 周期下通过不能
  证明实际分频相位保持，不用缩减指令空间的收益抵消此偏差。
- 候选构建快照（非事实源）：A/B/Boot 通过，build `20260913171122`，源码指纹
  `f69799cafb1400dc788b1637ff0ca70ea151e28eea9f64eaa93828654403dad2`。
  仅 follower DATA 的 PIO 指令字改变，其余程序含自主 origin 一致；A/B link-free
  各 2200 B，含 2048 B heap 预留，额外各 152 B，没有新增 RAM。候选源码原始字节、
  patch、生成头、构建和模型保存在证据根，回退不删除这些原件。
- 候选当前源码 QUICK P3 流程、短帧及普通恢复通过；strict=false，TRN-01 SCK
  gate 和 TRN-03 SCK row 选择未通过，原始拒绝单列保留，不归因于 DATA 模型反例。
  两轮自主整段 passed/closed_loop/realtime=false、diagnostic=true；后段各板
  accepted 增长，rejected/missing 无增长，不能据此提升整段稳定性或采样相位保证。
- 完整外层对照（us，有限窗口快照，非事实源；前序 002 与候选各两轮，同矩阵、
  四邮箱及 v8 探针；NO1 使用 RUN，其余使用 OTHER，不是各分项独立最大值）：

  | 节点 | 前序 R1/R2 | 被拒候选 R1/R2 |
  |---|---:|---:|
  | NO1 | 650.544/681.388 | 646.196/649.388 |
  | NO2 | 697.232/711.556 | 728.840/702.072 |
  | NO3 | 714.424/697.948 | 705.000/695.772 |
  | NO4 | 732.284/676.340 | 691.716/672.764 |

  完整预算仍未达，变化不一致；本次没有消除 latch 的逐圈读取/重装，不声称 CPU
  固定收益，前序增长峰值及因果缺口保留。
- 恢复：四板经 STOP、OTA 回到 build `20260913163720`，固定矩阵普通闭环通过，
  STOP/config ACK、许可证失效和 SD 逐字节核对通过；生产源码恢复前序指纹
  `280f9ddcba018de2068c79541b00263d07fa199f0e0e3f4b8f972d2f857afd6f`。
  候选 P3 凭证只绑定候选，不替换恢复版本凭证；本轮仅提交文档记录。
- 下一边界：先保持最短实际 SCK 间隔下的采样相位与 byte 余量，再评估连续计数器
  自身压缩或观察性边界工作迁移。新的 FIFO 独占交接、逐圈身份/时间记录、静态
  缓冲期限及 VDC 时钟映射仍须一起证明。完整 WCET、blackout、正式 RAM 和 registry
  状态不变；原件入口为 `review-final-r1.json`、`rejection-decision.json`、
  `fractional-probe-r1.json`、候选 P3/记录/SD 和 `rollback-normal-r1`。

### TDMA-PROGRESS-20260914-002 - 锁内空队列选择提前结束

- 日期：2026-09-14
- 状态：PARTIAL；空队列行为和有限实板对照完成，完整预算及严格 P3 未通过。
- 范围：`tdma_traffic_scheduler_select_impl()` 在既有锁内完成周期刷新与普通队列
  过期清理后，若普通队列及 recovery depth 均为空，直接记录原 GATE_CLOSED、
  无 traffic class，释放锁并返回；跳过后续 VDC/recovery/RefMem/控制队列扫描。
  recovery depth 包含在途帧，非空时继续原路径；不提前读取无锁队列，不省略统计，
  不改派发输出、准入和 STOP 边界，不新增计时探针。
- 软件：调度器 host 回归及计时回归 24 项通过（验证快照，非事实源）。新覆盖包含
  空拍预算刷新、结果和输出保留、同周期新数据准入与恢复帧在途；既有过期清理、
  恢复重试/预算、优先级、STOP 取消及重新开放继续通过。
- 构建快照（非事实源）：A/B/Boot 通过，build `20260913163720`，源码指纹
  `280f9ddcba018de2068c79541b00263d07fa199f0e0e3f4b8f972d2f857afd6f`，
  源文件数 1044。实际汇编在刷新计时后进入空分支并跳转至结果记录、解锁和返回。
  A/B link-free 各 2200 B，含 2048 B heap 预留，额外余量各 152 B；无新增 RAM，
  PIO 产物字节一致。保持六节点编译容量、四邮箱运行布局与同 v8 探针。
- 同矩阵分项对照（us，有限窗口快照，非事实源；前序 067 四个当前版窗口、候选
  两个窗口；均取每板 RUN/OTHER 外层峰值所在拍，分项不是独立最大值）：

  | 节点 | 前序 SELECT_EMPTY 范围 | 候选 SELECT_EMPTY 范围 | 候选外层 R1/R2 |
  |---|---:|---:|---:|
  | NO1 RUN | 27.116–40.432 | 11.168–20.692 | 650.544/681.388 |
  | NO2 OTHER | 28.940–48.184 | 25.312–26.544 | 697.232/711.556 |
  | NO3 OTHER | 33.696–48.816 | 30.296–31.680 | 714.424/697.948 |
  | NO4 OTHER | 29.800–39.424 | 28.420–29.492 | 732.284/676.340 |

  各板候选保留拍中的 EMPTY 分项均低于该板前序四个记录，但刷新成本本身也有波动，
  不能将区间相减当作固定节省。完整外层未一致改善，主站第二轮高于前序本轮窗口；
  原 NO3 789.932 us 高峰继续保留，增长因果未解决，完整 500 us 未达。
- 当前源码 QUICK P3 完成，短帧通过；strict_gates_passed=false。粗 CLK 校准中
  NO2 的 `TOPology 4,1,2` 应答超时，coded marker gate 同轮未通过；原始拒绝保留，
  不用流程完成或后续普通闭环替代严格门禁。两轮自主整段
  passed/closed_loop/realtime=false、diagnostic=true，后段 accepted 增长，
  rejected/missing 无增长；有限后段不提升为整段稳定性。
- 取证流程失败与恢复：首个编排脚本将诊断退出码 1 当作中断，提前进入恢复；
  恢复的 recorder ARM 命令超时，读回仍为首轮冻结 epoch，未建立新记录。
  `sequence-command-r1` 和 `recovery-normal-r1` 原件保留。随后独立 STOP 复核，
  对板上首轮记录执行实际 SAVE/读回，与主机原件逐字节一致，补齐首轮 SD 证据；
  新的继续脚本保留诊断失败并完成第二轮及普通恢复，没有修改退出门禁或延长期限。
- P3、两轮记录及最终普通恢复均完成 STOP/config ACK、许可证失效与 SD 核对。
  START 后零查询，实测 payload 与接收长度维持固定四邮箱。最终当前固件普通模式
  passed/closed_loop=true。完整 WCET、正式 RAM、逐圈特等席、blackout 和增长因果
  继续开放；下一步聚焦 RX 交接/接受及 latch 重装等主成本，避免仅凭局部优化宣称达标。
- 原件：`source-checkpoint-r1.json`、`comparison-r1.json`、A/B 选择函数反汇编、
  `p3-r1/diagnostic.json`、两次编排命令、恢复失败、全部记录/STOP/SD 及 `normal-r1`。

### TDMA-PROGRESS-20260914-001 - O6 前后固件峰值重复性对照

- 日期：2026-09-14
- 状态：PARTIAL；有限对照和恢复完成，时间增长因果未解决，完整预算未达。
- 范围：生产源码不变，以 067 当前版、066 基线版、067 恢复版各两个窗口执行
  B/A/B。重核两版封存 manifest、原 OTA 包和 build，沿用同一四邮箱矩阵、板端
  记录窗口与 v8 探针；START 后零查询，STOP 后导出及 SD 逐字节核对。
- 外层峰值（us，R1/R2；有限窗口快照，非事实源；NO1 取 RUN，follower 取 OTHER，
  各板各窗口各自选拍，不是同步跨板观测）：

  | 固件/次序 | NO1 RUN | NO2 OTHER | NO3 OTHER | NO4 OTHER |
  |---|---:|---:|---:|---:|
  | 当前 067，前置 | 663.396/654.184 | 701.596/713.220 | 692.884/710.052 | 696.372/701.456 |
  | 基线 066 | 668.980/675.264 | 716.092/723.888 | 726.332/704.352 | 681.680/686.080 |
  | 当前 067，恢复 | 671.352/664.472 | 726.776/723.756 | 789.932/709.992 | 701.248/755.336 |

- 主站前序 714.560 us RUN 高值未在本轮当前版窗口重现，但 NO3 高峰重现，NO4
  也出现较高值。不能宣布增长消失、排除回归或确认 O6 算术的唯一因果。主站
  OTHER 外层三组依次为 725.176/729.352、685.668/708.396、485.580/576.388 us；
  低值不覆盖原高值，全部 body 与分类外层记录保留。
- 口径：PEAK 按 `total_ticks` 选 body 峰值，RUN/OTHER 按 `full_phase_ticks`
  选外层峰值。PEAK 携带的外层值不是外层最大值保证；预算按分类外层复核，并保留
  `SCHEDule` 对完整 WCET、deadline 和后置分类/调度发布开销的验收责任。
- 布局复核：实际 A/B 的 RX 接受、flight unload、receive health、traffic select、
  timing record 和 adapter service 六个函数，地址及抽取的指令字在两版完全相同。
  每个目标共比较 2927 个唯一符号，10 个地址变化、3 个符号指令字变化（构建快照，
  非事实源）。这不覆盖 literal/data、缓存、总线或中断行为，不能由此确定共享
  干扰的因果；扩展 RX impl 的补充分析与首轮分析分别保留。
- 六个自主窗口整段 passed/closed_loop/realtime=false、diagnostic=true，原始
  拒绝保留；后段 accepted 增长而 rejected/missing 无增长，不提升为整段稳定性。
  两次四板 OTA 均通过，最终当前 build 为 `20260913154535`。每轮 STOP/config ACK、
  许可证失效及 SD 核对通过，普通模式恢复 passed/closed_loop=true。
- 当前源码指纹仍为 `814e12c7c49a5fe39856d4f2542e83c3030de3d46b8eda6c69740b4f23dcc3e1`，
  沿用 067 绑定同源码的真实 QUICK P3（strict=true）；本轮没有新生产实现或新 P3
  凭证，旧版运行仅作为明确标注的对照。正式 RAM、完整 500 us、逐圈特等席和
  blackout 继续开放。
- 原件：`plan.json`、`repeat-comparison-r1.json`、`layout-analysis-r1.json`、
  `rx-hot-layout-r1.json`、两次 OTA、六轮记录/STOP/SD 及 `normal-r1` 恢复证据。
  后续在不改变工作量与门禁口径的条件下，检查共享干扰、高频探针自身成本、RX
  接受和空队列调度，实际收益须重新实测。


## 归档索引

| 文件 | ID 区间 | 条目数 | 归档日期 |
|---|---|---|---|
| `docs/legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_03.md` | TDMA-PROGRESS-20260913-067..TDMA-PROGRESS-20260913-030 | 38 | 2026-09-19 |
| `docs/legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_02.md` | TDMA-PROGRESS-20260912-029..TDMA-PROGRESS-20260912-001 | 29 | 2026-09-19 |
| `docs/legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_01.md` | TDMA-PROGRESS-20260911-007..TDMA-TASK-20260817-001 | 51 | 2026-09-19 |
