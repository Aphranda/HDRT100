# Core1 TDMA 周期预算 SCPI 配置说明

Status: Draft
Domain: TDMA
Canonical: `docs/tdma/TDMA_BUDGET_PLAYBOOK.md`
Related: `application/inc/app_realtime_profile.h`, `application/inc/app_realtime_schedule.h`, `config/project_config.h`, `components/product_config/src/product_config.c`
Last updated: 2026-09-20

本文说明当前分支的候选实现，不冻结跨域契约，也不表示多板实时验收通过。
默认周期来自 `PROJECT_CORE1_PROFILE_1500US_CYCLES`，默认各阶段预算来自
`APP_REALTIME_PHASE_TABLE`。配置单位均为 `clk_sys` cycles；不是微秒。
这里配置 Core1 软件调度预算，不改变 TDMA SPI operating level 或硬件发车周期。

## 操作顺序

先停止触发和 TDMA，读回 owner 停止且配置已确认，再编辑草稿。`BEGIN` 从实际运行表复制，
`PHASE` 按 `phase_id,start_cycle,end_cycle,wcet_cycles` 编辑，`CYCLE` 只调整尾部
GUARD 的结束位置。相邻阶段必须连续，最后一个结束位置就是总周期。

以下是当前默认表下的演示快照，非事实源；阶段编号和边界必须以实际 `TABLE?` 及
`app_realtime_phase_id_t` 为准。示例保持默认周期，只改变 VDC 的 WCET：

```text
TRIG:STOP
SYST:TDMA:RING:STOP
SYST:TDMA:RING:STAT?
SYST:TDMA:SCHEDULE:CAPABILITIES?
SYST:TDMA:SCHEDULE:TABLE?
SYST:TDMA:SCHEDULE:BEGIN
SYST:TDMA:SCHEDULE:PHASE 1,217500,260500,41999
SYST:TDMA:SCHEDULE:VALIDATE?
SYST:TDMA:SCHEDULE:APPLY
SYST:TDMA:PERIOD?
SYST:TDMA:SCHEDULE?
SYST:TDMA:SCHEDULE:STATUS?
SYST:ERR?
```

`VALIDATE?` 返回 `valid,reason,phase_id,cycle_cycles`。表级原因的 phase_id 为
`APP_REALTIME_PHASE_COUNT`。校验包括版本、连续窗口、非空阶段、WCET 包含关系、
GUARD 不执行工作、计数器范围、TDMA 线传输预算下限和 DPLL 入口余量。

`APPLY` 在停止态控制互斥内安装 RAM 配置并发布 Core1 更新请求，不写 Flash。
Core1 在下个停止态边界一次采用整表。运行中或已有 pending 请求会拒绝；
被拒绝的草稿保留以供检查重试。只有显式 `SYST:TDMA:SCHEDULE:SAVE` 才保存当前
实际运行表，经 Product Config journal CRC/读回验证后返回；草稿不随 SAVE 应用。
SAVE 要求 owner 已停止且无待处理更新，所有 Flash 写入仍经 Core0
FlashTransaction 和 Core1 lockout。重复 SAVE 会产生 journal 写入，不应周期轮询。
PROGRAM 后即使事务完成回报失败，也会核对完整记录和 CRC，以实际持久字节为准；若
Flash 读回本身失败，返回 NO_PERSIST，持久性未知，需重新读取或重启确认。

`APPLY` 的 `OK,cycle_cycles` 是临时配置目标，不是生效证明。轮询 `PERIOD?`，
直到 pending 为零且 requested/applied generation 相等，再用 `SCHEDULE?` 核对运行表。
后续 STOP 不取消已安装 RAM 整表的更新请求；ARM 继续被排除，直到 Core1 完成采用。
独立 `PERIOD` 命令的非持久请求仍可被 STOP 取消。存在自定义表时，`PERIOD` 只接受
它自身的周期；CLEAR 后可再次选择编译目录。

## 查询与恢复

| 命令 | 内容 |
|---|---|
| `SYST:TDMA:SCHEDULE:CAPABILITIES?` | 能力版本及临时 APPLY/CLEAR、显式 SAVE 支持标记；GUI 只有确认该能力后才允许写预算，拒绝旧版自动保存 APPLY |
| `SYST:TDMA:SCHEDULE:TABLE?` | 配置表：version、cycle、phase_count，随后按阶段排列 start/end/WCET；可能尚未被 Core1 采用 |
| `SYST:TDMA:SCHEDULE:DRAFT?` | 草稿的同形表及 dirty；无草稿时 version 为零 |
| `SYST:TDMA:SCHEDULE?` | Core1 实际运行表和各阶段统计；字段定义见 `app_realtime_schedule_snapshot_t` 和对应 SCPI handler |
| `SYST:TDMA:PERIOD?` | active_cycles、applied_generation、pending_cycles、requested_generation、applying |
| `SYST:TDMA:SCHEDULE:STATUS?` | version、default_cycles、active_cycles、applied_generation、configured、persisted、staged、dirty、valid、reject_code、reject_name、reject_phase |
| `SYST:TDMA:SCHEDULE:DISCARD` | 丢弃草稿，保留当前实际配置和 Flash 内容 |
| `SYST:TDMA:SCHEDULE:CLEAR` | 停止态下临时恢复编译默认表，不改变 Flash 内容 |
| `SYST:TDMA:SCHEDULE:SAVE` | 停止态且无 pending 时，固化当前实际运行整表，不应用草稿 |

保存的是整张表，包含 GUARD；重启时在 Core1 启动前恢复并重新校验。旧 Product Config
记录在 RAM 中兼容解码，启动不写 Flash。无记录或校验失败采用编译默认表；可恢复的
坏表通过 STATUS 报告，不设置阻止后续 CLEAR 的全局诊断故障。

Core1 周期使用 VDC 所有的 Timer1 系统时钟计数，启动采用恢复周期，避免微秒取整丢失。
配置表与编译默认完全相同时 configured 为零；persisted 仅在当前实际整表与保存表一致且
无 pending 时为真。临时修改后即使 Flash 中仍有旧表，persisted 也为零；未 SAVE 的
修改重启丢失。要永久恢复默认值，先 CLEAR、等待读回确认，再显式 SAVE。

## GUI 操作

序列调试 GUI 顶部的“TDMA / VDC 预算”打开独立编辑窗口，先读取当前设备的实际表和
时钟，再按微秒显示总周期、各阶段时窗和 WCET。TDMA、VDC 可独立修改；其他阶段也有
对应的可编辑行，GUARD 使用剩余时间，不自动缩放其他预算。

“校验草稿”只操作临时草稿；“临时应用”停止触发和 TDMA 环路、校验、应用并核对完整
运行表；只有“保存到 Flash”固化已应用配置。存在未应用编辑时保存按钮不可用。
切换设备或操作失败后必须重新读取，旧设备读回不授权新设备保存。保存范围仅为本窗口
的 Core1 调度表，不包含链路 operating profile 或其他序列设置。

链路 TDMA 挡位下拉列表按 `OPERATING_GUI_MIN_BAUD_HZ` / `OPERATING_GUI_MAX_BAUD_HZ`
筛选，当前用户范围为 10–30 MHz（需求快照，非事实源）；保留设备原始挡号，
范围外挡号及未读取目录时的纯数字草稿均不能下发。链路挡位与 Core1 软件周期独立。

## 短周期与独立 VDC 预算

周期和 VDC 窗口/WCET 可分别编辑：`CYCLE` 改总周期，`PHASE` 选择
`APP_REALTIME_PHASE_VDC` 改 VDC；全部修改在 APPLY 时一起校验。缩短周期时必须先
重新分配所有阶段，不能只缩 GUARD 尾端却让前面的阶段仍越过新周期。

以下为显式 SAVE 语义修改前的单板实验快照，非事实源；对应完整阶段表见
`out/tdma-budget/short_cycles_hil.py` 的 `PROFILES`，原始数据见
`out/tdma-budget/short-cycles-hil.json`。时钟换算仍以板端 `SCHEDULE?` 为准。

| 目标软件周期 | 总 cycles | 独立测试的 VDC WCET cycles | 单板观测 |
|---|---|---|---|
| 500 µs | 125000 | 8000 / 11000 | 配置、保存、重启读回成功；平均约 1.92–1.95 kHz，完整负载有超限/隔离，未通过严格实时验收 |
| 1 ms | 250000 | 8000 / 11000 | 配置、保存、重启读回成功；平均约 998 Hz，完整负载有超限/隔离，未通过严格实时验收 |

频率来自主机时间间隔内 Core1 cycle_count 的差值，是软件节拍粗测，不是总线波形测量。
实验分别采用 TDMA＋VDC 和当时启用的全部负载；disabled 负载的历史隔离位也保留在
原始快照中，不能误当成当次 VDC 失败。验收结束恢复并保存原默认预算表。

## 验证边界

软件验证入口：`test_app_realtime_profile.py`、`test_app_realtime_profile_transaction.py`、
`test_product_config_realtime_profile.py`、`test_tdma_service_nonblocking.py` 和
`test_app_realtime_release.py`（均位于 `tests/python/`）。包括停止态互斥、保存失败、
旧记录兼容、同周期换预算、临时修改重启回退、显式保存恢复及非整数微秒周期。
GUI 协议和交互测试入口为 `test_sequence_realtime_budget.py`、
`test_sequence_trigger_debug_ui.py`、`test_sequence_trigger_debug_ui_layout.py`。

本次本机证据保存在 `out/tdma-budget/`，包括构建、OTA、单板 SCPI/重启验证及 P3
设备发现失败。软件重启读回不替代物理断电/擦写中断测试；单板功能验证也不替代
多板 TDMA 闭环、波形或严格实时验收。当前 P3 凭证仍待配置中的多板设备可用后重跑。

显式保存语义的追加单板证据在 `out/tdma-budget/gui-budget-hil.json`，使用 GUI 的
`run_budget_operation` 实际通信路径，覆盖临时 VDC 修改重启回退、两种短周期的
应用/SAVE/重启恢复，以及保存后再次临时修改仍回退到已保存表。测试结束恢复并
显式保存原默认表，最终状态见 `gui-final-device.json`；OTA 身份/build 校验见
`gui-ota/summary.json`。短周期观测仍有错失/隔离，不声明严格实时通过。

GUI 打包产物、自检和构建日志在 `out/sequence-debug-ui/tdma-budget-20260920-r2/`。
本机使用已有 `py2exe` Conda 环境打包（默认 Python 未安装 PyInstaller）；
Git Bash 使用实际存在的 `C:/Program Files/Git/bin/bash.exe` 运行 pre-commit。
本轮 P3 失败原始记录为 `out/tdma-budget/gui-p3-run.log`，原因是配置中的多板
CDC 序列号未发现，不能由此次 USBTMC 单板测试替代。
