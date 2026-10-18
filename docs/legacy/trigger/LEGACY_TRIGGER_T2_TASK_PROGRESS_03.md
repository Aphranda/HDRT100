# TRIGGER T2 任务进展归档 03

Status: Frozen
Domain: TRIGGER
Canonical: `docs/legacy/trigger/LEGACY_TRIGGER_T2_TASK_PROGRESS_03.md`
Related: `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md`, `docs/check/DOCS_REGRESSION_PLAN.md`
Last updated: 2026-10-18

> 本文件是 `docs/trigger/t2/TRIGGER_T2_TASK_PROGRESS.md` 按 C14（环5）轮转迁出的历史证据段，
> 覆盖 `T2-PROGRESS-20260927-020` .. `T2-PROGRESS-20260927-018`（按新鲜度倒排，最新在前），共 3 条。
> 条目正文逐字保留、未作编辑；规范文件的 `## 归档索引` 是唯一索引入口。
> 轮转规则见 `docs/check/DOCS_REGRESSION_PLAN.md` §0 C14。

### T2-PROGRESS-20260927-020：本地提前动作后端与隔离模拟源接线

- TODO task ID：T2-EXEC-001、T2-TURNTABLE-004/008、T2-SLOT-GATEWAY-001、T2-READY-001。预装 SWITCH/GATEWAY 增加本地 TIMER1 deadline 提交、取消和实际 PIO 回执；不新增 TDMA 席位、DMA 或 CPU IRQ。装填 guard 与 origin 不确定性分别记录，不能把提前装载当作执行完成。
- 从板 Core0 提前解算 coarse/fine q，使用本地原子镜像交给 Core1；阈值采用本地最新已完成解算版本，冻结后停止解算。当前本地模板容量以 `TRIGGER_T2_RUNTIME_LOCAL_TEMPLATE_CAPACITY` 为准，起始/零位/终止实例独立；同板密集多动作和重合位置的业务去重尚未支持，冲突不重排。
- 新增独立 OUT4 PWM 模拟源，STOP 预装，RUN 只开启/关闭已装载输出，COUNTER 可共存；SCPI 为 `TRIGger:REServation:SOURce:CONFigure/STARt/STOP/RELease/?`。另外新增 `TRIGger:REServation:TIMing?` 分段周期读回和 `TRIGger:REServation:T2:ACTion?` 提交/回执/失败诊断。
- 独立审核发现并处理 Core0 SWITCH 查询争抢 Core1 回执、旧 FREEZE 跨运行残留、PWM 预装时旧内部高电平可能漏出的边界；相关负测和集成证据仍在收敛，不能据 host 通过关闭硬件动作或 READY 校验。
- 用户明确保持示波器四通道接四板 OUT1：本轮可外测 NO2/NO4 动作；NO1 OUT4 转台源与 NO3 OUT4 TRIG/IN1 READY 只取板端证据，外部波形缺口保留。下一 gate 为统一源码构建、专项上板、原预算分段计时、真实 position_id 回执与可见通道边沿，最后才执行阶段 P3 和分离提交。
- 后续四板动作探针确认本地游标在 24 位分段计时优化后持续推进，NO1 模拟源与 COUNTER 均有脉冲读回；但 Core0 预测诊断显示 `core0_preview_saved` 已增加而位置运输长期保持 `tx_active=1, tx_publish_count=0`，因此 `published_mask/action_completed_mask` 仍为零。该新阻塞位于 RefMem T2 位置帧发布背压/主站 resident frame 选择，不能把计数阈值经过或预览镜像生成宣称为跨板预约回执。



### T2-PROGRESS-20260927-019：复用本地网格的接入纠偏

- TODO task ID：T2-CLOCK-001/002、T2-EXEC-001、T2-ACCEPT-001。纠正上一 checkpoint 对“需要建立新的逐脉冲源”的推断：已验证的本地 VDC/DCO/TIMER1 网格存在，当前缺口是 T2 消费路径和本地执行接线；不新增 TDMA 预算或载荷。
- 独立只读复核确认：`vdc_run_output.inc` 使用 committed DCO 与当前 TIMER1 生成连续 ordinal，未要求本地 PI LOCKED；T2 原游标却绑定 ring observation 的 `sync_seq` 和旧接收时间戳。FOLLOWER 本地 PI 状态不等于 typed FOLLOW/PHASE 已采用模型的状态。
- 历史证据复核：`out/HardwareAcceptance/20260923/run-slices-on-r5/joint/capture/actions.jsonl` 包含 SYNC/MATCH/FOLLOW/PHASE 完整启动配置；仅 ARM/START 不能重现。`last_ordinal` 是已经提交的未来块尾，不能用作当前游标。保留上一条硬件失败事实，不据此否定已有细分能力。
- 游标已改为本地完整 coarse/fine q，跨格轮询只统计未逐格观察数量；身份换代和时间回退显式失效。定向 cursor 回归及关联 grid owner 回归通过，证据目录为 `out/pytest/cursor-local-r2`。下一步为 Release、实际游标读回和提前硬件 deadline 接线；尚未完成 position_id/READY 闭环，未签发新的 P3 凭证。
- 静态现场读回存于 `out/t2-local-cursor-20260927/preflight.json`；错误的简写 build 查询超时保留，不作为固件身份依据。
- 首轮固件定向 HIL（构建快照 `20260927032758`）四板升级成功；启动探针漏掉既有 `DPLL:PROVisional` 模型绑定，且曾在 RUN 查询 STOP-only PHASE 状态，相关拒绝/超时保留于 `run1` 至 `run5`。四板游标未取得有效身份，`SYNC_TRIGGER` 实测超过现有 WCET 并隔离，不能宣称游标上板通过。
- 预算修复只调整 T2 本地布局：生命周期门、快照和失效发布驻留 SRAM，模型投影留在 XIP；全函数驻留曾导致 RAM 链接超限，失败日志保留，未缩减栈或借用其他域预算。拆分后 Release 和定向 cursor/grid 回归通过，独立复核未发现语义回归；后继 HIL 必须使用有效模型路径实测 WCET。
- 后继算术优化将常用整除网格的 cursor 除法次数缩减，原子字复制消除 cursor 自身的大块 XIP 复制；通过独立小函数控制体积后，cursor 投影也已驻留 SRAM，原 VDC API 保持不变。构建快照 `20260927035722` 四板升级成功，`run8-fast` 的四板有效 coarse/fine cursor 持续推进，未发生该次 SYNC_TRIGGER 超预算。
- 长测保留失败：`run11-scope` 在后继观测中按每隔 5 s 同步采集四通道，共 12 个新鲜 RAW 窗口；每窗口均在 50 ns 内，最大相对 NO1 偏差约 28.53 ns（本轮快照，不证明采样间隙或绝对 ordinal）。NO3 在此期间出现 27189-cycle 调用并触发 SYNC_TRIGGER 隔离，整体仍为失败，不能关闭本地执行预算。其他三板游标继续推进；最终四板 STOP 和输出参数恢复均读回。下一步分段计时定位偶发超预算，同时接入预装动作硬件 deadline。



### T2-PROGRESS-20260927-018：静态装载跑通与运行时间基阻塞定位

- TODO task ID：T2-DEPLOY-002、T2-CLOCK-001/002、T2-EXEC-001。修复 STOP grid 配置对 RUN committed model 的循环依赖：无锁定模型时只保存共同 grid 几何与 staged ring 身份，精确规划仍需原模型门禁。四板 STOP 实测 grid、deployment、模板资源和 T2 配置依次返回成功；NO1 COUNTER 在 ARM 前 START 成功。TRN-03 复跑的旧 recorder `FROZEN` 需先 SAVE 到 `SAVED` 才能 ARM，新工具前置读回/保存已通过 124 项 host 回归。
- 本切片最终源码四板 P3 build=`20260927024513` 为 `PASS_WITH_WARNINGS`（INFO 32 / WARN 12 / ERROR 0 / FATAL 0），原件在 `out/HardwareAcceptance/20260927/p3-104507/`。先前 `p3-094807/` 的 NO3 TDMA 第 7 个记录缺失失败及 `p3-100619/`、`p3-103123/` 的中间通过均保留，不互相改写。相关静态 grid、T2、事件镜像、模板恢复和 TRN-03 回归共 237 项通过，Release build 及链接门禁通过。
- RUN 探针：四板新 session、静态装载、COUNTER START、ring ARM/START 均读回成功，但 `SYSTem:TDMA:SCHEDule?` 的 quarantine mask 为 72，其中 bit 6 是 `SYNC_TRIGGER`；显式关闭再启用该负载后，四板各再次出现一次超过 `PROJECT_CORE1_PHASE_SYNC_TRIGGER_WCET_CYCLES` 的调用并重新隔离。NO1 cursor 曾从 `NO_SYNC` 前进到 `MODEL_TIME`，NO2–NO4 前进到 `NO_LOCK`，但均未得到有效共同 q。
- 同一 ring 的 VDC `sync_seq` 是稀疏 observation 序列，不能证明每个 1 kHz 输出脉冲都推进一次 cursor。NO2–NO4 在原 FOLLOWER 模式下只保留本地观察诊断且 DCO 没有共同锚；临时切为 MASTER 的有限对照仍因 `VDC_DOMAIN_GATE_LOCAL_PHASE_UNALIGNED` 拒绝正式锁定。T2 cursor 的未锁定分支已提前返回，不再复制模型或处理事件；该预算优化不授予从板时间资格。没有共同相位映射前不得放宽 T2 的锁定门或用 TDMA 计数代替 VDC 时间片。
- STOP 恢复：四板 ring/T2 STOP，NO1 COUNTER STOP 后模板、deployment、grid 全部释放读回成功。代码已把模板释放顺序固定为 COUNTER STOP→UNLOAD，避免运行中的 counter 令模板/部署清理卡住。`node-sequence` 分支可参考静态槽位与 IO 管理，但没有现成的共享 VDC q 硬件 deadline 执行器。当前尚无真实位置动作、READY 或示波器 `position_id` 证据；下一项先建立逐脉冲时间源和从板共同相位映射，再预算化本地动作执行。



