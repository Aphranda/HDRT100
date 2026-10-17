# TDMA 任务进度归档

Status: Frozen
Domain: tdma
Canonical: `docs/legacy/tdma/LEGACY_TDMA_TASK_PROGRESS_05.md`
Related: `docs/tdma/TDMA_TASK_PROGRESS.md`
Last updated: 2026-10-17

## 归档说明

由 C14 轮转从 `docs/tdma/TDMA_TASK_PROGRESS.md` 迁入，按时间正序保存最旧条目；未删除、未改写。

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

