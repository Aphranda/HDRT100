# SCPI 指令面清单（按域 / 功能 / 参数）

Status: Draft
Domain: SCPI
Canonical: `docs/interface/SCPI_INTERFACE_COMMANDS.md`
Related: `docs/interface/SCPI_HOST_ARCHITECTURE.md`, `middleware/scpi_port/`
Last updated: 2026-10-19

> 机械导出：指令名/回调取自 `SCPI_*_COMMANDS` 宏；域按指令路径归类；参数取自回调内的读取调用；
> 功能说明只有原代码注释存在时自动填入，其余标 **（待补）**，待逐域人工补齐后审查。

## 总览

- 指令总数 **813**，覆盖 **28** 个域；设置 119 / 查询 433 / 动作 261

| 域 | 指令数 | 设置 | 查询 | 动作 |
|---|---:|---:|---:|---:|
| 读取别名（READ:*） | 67 | 0 | 67 | 0 |
| 同步（SYNC:*） | 9 | 0 | 0 | 9 |
| IEEE-488 通用 | 16 | 0 | 10 | 6 |
| 系统运行时 | 151 | 24 | 103 | 24 |
| 系统快照 | 3 | 0 | 2 | 1 |
| 系统诊断 | 6 | 1 | 5 | 0 |
| 配置（装载） | 27 | 3 | 0 | 24 |
| VDC / DPLL | 89 | 16 | 38 | 35 |
| TDMA 同步（REFMEM 席位传输） | 12 | 3 | 8 | 1 |
| REFMEM 同步 | 19 | 2 | 16 | 1 |
| REFMEM 分布式内存 | 18 | 2 | 14 | 2 |
| T2 预约（位置/网格） | 10 | 1 | 5 | 4 |
| T2 序列时序 | 10 | 0 | 6 | 4 |
| T2 动作执行（序列） | 5 | 0 | 5 | 0 |
| 预留（席位） | 57 | 0 | 19 | 38 |
| 触发（产品 RUN/门禁） | 7 | 1 | 1 | 5 |
| 实时 IO | 49 | 14 | 25 | 10 |
| 实时序列 | 7 | 3 | 4 | 0 |
| 编码器 | 6 | 2 | 4 | 0 |
| 脉冲计数 | 15 | 6 | 8 | 1 |
| 实时（其它） | 13 | 4 | 5 | 4 |
| 校准 | 48 | 17 | 0 | 31 |
| 测量 | 7 | 0 | 7 | 0 |
| 模型（转台） | 2 | 0 | 0 | 2 |
| BISS 通信 | 71 | 2 | 34 | 35 |
| 通信（其它） | 20 | 4 | 12 | 4 |
| OTA / Boot | 38 | 8 | 18 | 12 |
| 存储 / 日志 | 31 | 6 | 17 | 8 |

## 命名审查记录（按语义纠偏）

| 现状 | 实际语义 | 建议命名 | 兼容策略 | 状态 |
|---|---|---|---|---|
| `TRIGger:REServation:T2:STARt` | 挂载：绑定位置源/epoch、清运行状态、重建组序列（不产生任何触发） | **`TRIGger:REServation:T2:MOUNT`** | **不留别名（直接替换）** | 已定稿（B 方案） |
| `TRIGger:REServation:T2:STOP` | 卸载：结束 run 并解除位置源绑定 | **`TRIGger:REServation:T2:UNMOUNT`** | **不留别名（直接替换）** | 已定稿（B 方案） |
| `...:CONFigure` 的 `target_mask` | 参与板/席位掩码：FREEZE 广播给谁、ACK 归属；为零直接拒绝（146） | `board_mask`（参数改名） | 参数层，不改协议 | 建议（待定） |
| `TRIGger:STARt|STOP|PAUSe|CONTinue|ABORt` | 产品级 RUN 控制——当前为空实现（恒返回 1） | 补实现；动作使能 mask 落在这里 | 新增行为 | **接口缺陷（待立项）** |

> 说明：本次评审决定 **B 方案——直接替换、不保留别名**（避免同一语义两个名字导致误用）。
> 因此命令注册、台架脚本与文档必须在同一次提交内一起改；冻结的历史日志只补语义说明，不改文本。


## 改名影响清单（B 方案：MOUNT / UNMOUNT，不留别名）

| 文件 | 处数 | 处理 |
|---|---:|---|
| `middleware/scpi_port/inc/scpi_system_snapshot_commands.h` | 2 处 | 命令注册：改 `.pattern` 字面量 |
| `tools/sync_trigger/t2_workflow.py` | 11 处 | 台架流程：STARt/STOP 调用与日志文案 |
| `tools/sync_trigger/t2_bench_probe.py` | 3 处 | 探针脚本：停机块与挂载 |
| `docs/trigger/t2/TRIGGER_T2_ARCHITECTURE.md` | 4 处 | 已写“START 是历史命名，语义是挂载”，改名后顺带校正 |
| `docs/trigger/t2/TRIGGER_T2_TODO.md` | 1 处 | 同上（挂载语义注记） |
| `docs/trigger/t2/T2_BRINGUP_FLOW.svg` | 4 处 | 流程图的命令名 |
| `docs/legacy/**`、`docs/tdma/*TASK_PROGRESS*` | 7 处 | **冻结日志，不改**（历史证据） |

> 不留别名意味着**同一次提交内**必须改完命令注册、台架脚本与文档；冻结的历史日志只做语义说明，不改文本。


## 参数说明：`TRIGger:REServation:T2:CONFigure`（13 个参数，最后 1 个可选）

| # | 参数 | 类型 | 取值 | 语义 |
|---:|---|---|---|---|
| 1 | `run_id` | u32 | ≥1 | 本次运行标识，`T2?` 回显 |
| 2 | `source_slot` | u32 | 槽位号 | 位置源（转台/计数器）所在槽位 |
| 3 | `target_mask` | u32 | 非零 | **参与板/席位掩码**：FREEZE 广播给谁、ACK 归属；为 0 直接拒绝（146） |
| 4 | `origin` | u32 | 0/1 | 是否以原点事件触发 |
| 5 | `pre_roll` | u32 | 脉冲数 | 起始前提前量（保证第一个位置可预测） |
| 6 | `post_roll` | u32 | 脉冲数 | 终止后余量（保证最后一个位置可校验） |
| 7 | `threshold` | u32 | 0..999 | 位置阈值（按 STEP 的千分比触发） |
| 8 | `start_angle` | i32 | 毫度 | 扫描起始角 |
| 9 | `zero_angle` | i32 | 毫度 | 零点角 |
| 10 | `terminal_angle` | i32 | 毫度 | 扫描终止角 |
| 11 | `step_mdeg` | i32 | 毫度 | 每步角距 |
| 12 | `pulses_per_degree` | u32 | — | 每度脉冲数（几何换算） |
| 13 | `window（可选）` | u32 | 1..MAX | 承诺窗口（粗序数）；缺省用内置 grace |

> 第 3 个参数建议改名为 `board_mask`（现 `target_mask` 容易被读成“动作掩码”，见命名审查记录）。


## 命名一致性审查（813 条，2026-10-19）

**结论：无功能性冲突**——813 条大小写归一后 1:1 对应，不存在“同归一化名、不同回调”的遮蔽；问题集中在写法与词汇不统一。

### 1. 同一动词的两种大小写写法

| 写法 A | 写法 B | 说明 |
|---|---|---|
| `CLEar` × 12 | `CLEAr` × 4 | 同一动词两种大小写写法 |
| `STARt` × 14 | `START` × 1 | 同上 |
| `BEGin` × 2 | `BEGIN` × 2 | 同上 |
| `WRITe` × 1 | `WRITE` × 1 | 同上 |

### 2. 同义动词分布（受控词汇表的出发点）

| 语义 | 现有写法（次数） | 问题 |
|---|---|---|
| 启动/使能 | STARt 14 · ARM 14 · ACTivate 5 · RUN 4 · BEGin 4 · MOUNT 3 · PREPare 1 | 7 种写法表达“开始/武装”，需按语义分层 |
| 停止 | STOP 28 · RELease 8 · ABORt 5 · PAUSe 1 · CANCel 1 · UNMOUNT 1 · DISarm 1 | 同上；ABORt 与 STOP 的区别应固定（是否清队列） |
| 清除 | CLEar/CLEAr 16 · DELete 4 · RESet 3 · REVokе 1 | 清空 vs 删除 vs 恢复默认，三者语义不同 |
| 持久化 | SAVE 10 · STORe 7 · WRITe/WRITE 2 · COMMit 2 | 保存到 flash / 暂存 / 提交事务 |
| 装载/读取 | LOAD 7 · READ 7 · RECall 5 · IMAGe 1 · VIEW 1 | 从镜像装载 vs 读回 vs 撤回默认 |

### 3. 疑似拼写/拼接问题（需逐条确认）

| 指令 | 疑点 | 建议 |
|---|---|---|
| `SYSTem:TDMA:RING:ORIGin:CADEence?` | `CADEence` | 疑似 `CADEnce`（拼写重复） |
| `SYSTem:REFMEM:FLIGHT:NORECord?` | `NORECord` | 拼接词，建议 `NORECord` → `NORECording` 或注明 |
| `SYSTem:BOOT:BOOTSel` | `BOOTSel` | BOOT+Sel 拼接，建议 `BOOT:SELECT` |
| `SYSTem:TDMA:RING:ORIGin:PBEGIN` | `PBEGIN` | 前缀 P 不明，建议注明明意 |

> 建议落地顺序：① 拼写修正（机械、风险低）；② 大小写统一（机械，但会动 17 处 pattern）；③ 受控动词表（策略，需逐域评审后才动命令名）。

## 接口审计（自动扫描 813 条，2026-10-19）

审计方法：对 813 条指令逐个解析回调体，统计其外部调用；`scpi_port_result_accepted` 只做 `ResultUInt32(1)`，绑定到它的 39 条指令**不可能有副作用**；`TRIGger:*` 5 条为同形态的独立函数。查询类（`?`）的“无返回”全部为误报——它们通过 `scpi_*_result_*` 辅助函数回数据，已人工复核。

| 类别 | 条数 | 判定 |
|---|---:|---|
| **空实现**（只回 1，无副作用） | **44** | 规范要求有行为，代码未实现 —— 两端不一致 |
| 非查询命令回数据（回显/状态码） | 162 | 风格一致性问题（规范表格普遍写“返回 1”），非错误 |
| 查询命令无返回 | 0 | 复核为误报（经 `scpi_*_result_*` 回数据） |

### 44 条空实现（按域分组）

| 分组 | 条数 | 指令 | 规范要求 |
|---|---:|---|---|
| 运行控制（产品） | 5 | `TRIGger:STARt` `TRIGger:STOP` `TRIGger:PAUSe` `TRIGger:CONTinue` `TRIGger:ABORt` | 规范要求：校验配置 CRC/active 表后启动或中止扫描任务（指令表 289/310/311 行） |
| 同步运行控制 | 8 | `SYNC:STARt` `SYNC:STOP` `SYNC:RELock` `SYNC:HOLDover` `SYNC:ACTivate` `SYNC:ROLLback` `SYNC:SAVE` `SYNC:LOAD` | 规范要求：同步配置包启停、重锁、保持、持久化（指令表 33/35 行） |
| 业务配置 | 8 | `CONFigure:TRIGger` `CONFigure:ANGLe:SWEEp` `CONFigure:ANGLe:PULSe` `CONFigure:ANGLe:BREAkpoint` `CONFigure:ANGLe:BREAkpoint:CLEAr` `CONFigure:SEQuence` `CONFigure:SEQuence:ACTive` `CONFigure:SWITch#` | 规范要求：写入通道/极化/频点/波位展开参数与状态序列（指令表 218/255 行） |
| 同步配置 | 5 | `CONFigure:SYNC:CALibration` `CONFigure:SYNC:RING` `CONFigure:SYNC:VDC:DPLL` `CONFigure:SYNC:GATE` `CONFigure:SYNC:LIMit` | 规范要求：同步配置包写入 |
| 校准装载/限制 | 11 | `CONFigure:CALibration:LINK:ADD/UPDate/DELete/CLEAr` `CONFigure:CALibration:PARameter:ADD/SET/DELete` `CONFigure:CALibration:META` `CONFigure:CALibration:LIMit` `CALibration:LOAD` `CALibration:STOP` | 规范要求：校准链路/参数表增删改与装载 |
| 校准限制覆盖 | 2 | `SYSTem:CALibration:LIMit:OVERRide` `SYSTem:CALibration:LIMit:DEFAult` | 规范要求：限制覆盖与恢复默认 |
| 通信/访问/故障 | 5 | `COMMunication:SERial:UART#:FORMat` `COMMunication:SERial:UART#:STATe` `SYSTem:FAULT:CLEAr` `SYSTem:SCPI:PERMission` `SYSTem:SCPI:ROLE` | 规范要求：串口格式/使能、清故障、权限与角色设置 |

> **处置（2026-10-19 评审）**：这 44 条**面向后续软件版本，本次收尾不动**。记录在案即可；对外文档与验收脚本暂按“已接受、行为待实现”处理。


## 接口缺陷（审查发现，待立项）

| # | 现状 | 问题 | 影响 | 建议 |
|---:|---|---|---|---|
| 1 | `TRIGger:STARt/STOP/PAUSe/CONTinue/ABORt` | 空实现，恒返回 1 | 上位机以为产品已受控，实际无副作用 | 补实现；若要“按掩码使能”，在此接收 mask |
| 2 | `TRIGger:REServation:ACTion:EXECute:ARM` | 无参数，只能整表武装 | 无法只跑部分动作/部分位置 | 增加 `<mask>` 参数或新增 `T2:RUN <mask>` |
| 3 | `T2:STARt/STOP` 命名 | 名为 start/stop，实为挂载/卸载+run 边界 | 上位机脚本语义易误读 | 改为 `T2:MOUNT`/`T2:UNMOUNT`，**不留别名** |

## 读取别名（READ:*）（67）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `READ:CALibration:ORIGin:RELease?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 2 | `READ:CALibration:ORIGin:BUILDCancel?` | 查询 | — | Diagnostic schema, not a timing grant. | `scpi_calibration_commands.h` |
| 3 | `READ:CALibration:ORIGin:HANDoff?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 4 | `READ:CALibration:ORIGin:BLACKout?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 5 | `READ:CALibration:ORIGin?` | 查询 | — | Publication facts only. Runtime/physical queries remain independent observations and are never represented as one atomic grant snapshot. | `scpi_calibration_commands.h` |
| 6 | `READ:CALibration:ORIGin:DIAGnostic?` | 查询 | — | Diagnostic schema; not grant authority. | `scpi_calibration_commands.h` |
| 7 | `READ:CALibration:ORIGin:RUNTIme?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 8 | `READ:CALibration:ORIGin:STOP?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 9 | `READ:CALibration:ORIGin:RECord?` | 查询 | <u32> | （待补） | `scpi_calibration_commands.h` |
| 10 | `READ:CALibration:ORIGin:LIVE?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 11 | `READ:CALibration:LINK?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 12 | `READ:CALibration:LOOPback?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 13 | `READ:CALibration:BIAS?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 14 | `READ:CALibration:CLOCk:CODEd?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 15 | `READ:CALibration:MARKer?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 16 | `READ:CALibration:DATA?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 17 | `READ:CALibration:SCK?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 18 | `READ:CALibration:RING:CAPTure?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 19 | `READ:CALibration:TRAINing:STAGe?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 20 | `READ:CALibration:TRAINing:STAGe:LINK?` | 查询 | <u32> | （待补） | `scpi_calibration_commands.h` |
| 21 | `READ:CALibration:TRAINing:STAGe:PERSistence?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 22 | `READ:CALibration:PATH:CANDidate?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 23 | `READ:CALibration:PATH:CANDidate:LINK?` | 查询 | <u32> | （待补） | `scpi_calibration_commands.h` |
| 24 | `READ:CALibration:PATH:ACTive?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 25 | `READ:CALibration:PATH:ROLLback?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 26 | `READ:CALibration:P3?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 27 | `READ:CALibration:SMA:CABLe:PHASe?` | 查询 | <u32>、<u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 28 | `READ:CALibration:SMA:CABLe:VALidator?` | 查询 | <u32>、<u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 29 | `READ:CALibration:SMA:CABLe:COARse?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 30 | `READ:CALibration:SMA:CABLe:RTT:RESPonder?` | 查询 | <u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 31 | `READ:CALibration:SMA:CABLe:RTT?` | 查询 | <u32>、<u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 32 | `READ:CALibration:STATe?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 33 | `READ:CALibration:RESult?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 34 | `READ:CALibration:PARameter?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 35 | `READ:CALibration:LIST?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 36 | `READ:CALibration:ACTive?` | 查询 | — | Keep the six-field response shape used by existing diagnostics while replacing the old fixed record with the manager-owned path snapshot. | `scpi_calibration_commands.h` |
| 37 | `READ:CALibration:META?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 38 | `READ:CALibration:HEALth?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 39 | `READ:TRIGger:PARameter?` | 查询 | — | （待补） | `scpi_config_commands.h` |
| 40 | `READ:ANGLe:SWEEp?` | 查询 | — | （待补） | `scpi_config_commands.h` |
| 41 | `READ:ANGLe:PULSe?` | 查询 | — | （待补） | `scpi_config_commands.h` |
| 42 | `READ:ANGLe:POSition?` | 查询 | — | （待补） | `scpi_config_commands.h` |
| 43 | `READ:ANGLe:BREAkpoint?` | 查询 | — | （待补） | `scpi_config_commands.h` |
| 44 | `READ:SEQuence?` | 查询 | — | （待补） | `scpi_config_commands.h` |
| 45 | `READ:SEQuence:MAP?` | 查询 | — | （待补） | `scpi_config_commands.h` |
| 46 | `READ:SEQuence:CHECk?` | 查询 | — | （待补） | `scpi_config_commands.h` |
| 47 | `READ:SEQuence:ACTive?` | 查询 | — | （待补） | `scpi_config_commands.h` |
| 48 | `READ:SWITch#?` | 查询 | — | （待补） | `scpi_config_commands.h` |
| 49 | `READ:MODEl:TURNtable:LOAD?` | 查询 | — | （待补） | `scpi_model_commands.h` |
| 50 | `READ:MODEl:TURNtable:TRIGger?` | 查询 | — | （待补） | `scpi_model_commands.h` |
| 51 | `READ:MODEl:TURNtable:MOTion?` | 查询 | — | （待补） | `scpi_model_commands.h` |
| 52 | `READ:MODEl:TURNtable:GROups?` | 查询 | — | （待补） | `scpi_model_commands.h` |
| 53 | `READ:MODEl:TURNtable:STATe?` | 查询 | — | （待补） | `scpi_model_commands.h` |
| 54 | `READ:SYNC:STATe?` | 查询 | — | The lock word is the real published state, not a constant. This page used to answer a fixed "LOCKED" whatever the node was doing, which is a false pub | `scpi_sync_commands.h` |
| 55 | `READ:SYNC:PARameter?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 56 | `READ:SYNC:HEALth?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 57 | `READ:SYNC:NODE?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 58 | `READ:SYNC:LINK?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 59 | `READ:SYNC:CHECk?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 60 | `READ:SYNC:LIST?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 61 | `READ:SYNC:ACTive?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 62 | `READ:SYNC:QUALity?` | 查询 | — | Field 0 is the published lock state written as the two words the system uses: LOCKED when this node is locked right now, UNLOCK when it is not. It is  | `scpi_sync_commands.h` |
| 63 | `READ:SYNC:VERSion?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 64 | `READ:RUN:SUMMary?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 65 | `READ:STATistics?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 66 | `READ:DIAGnostic:SENSors?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 67 | `READ:TRIGger:STATe?` | 查询 | — | （待补） | `scpi_trigger_commands.h` |

## 同步（SYNC:*）（9）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `SYNC:CHECk` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 2 | `SYNC:STARt` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 3 | `SYNC:STOP` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 4 | `SYNC:RELock` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 5 | `SYNC:HOLDover` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 6 | `SYNC:SAVE` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 7 | `SYNC:LOAD` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 8 | `SYNC:ACTivate` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 9 | `SYNC:ROLLback` | 动作 | — | （待补） | `scpi_sync_commands.h` |

## IEEE-488 通用（16）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `*TST?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 2 | `*CLS` | 动作 | — | （待补） | `scpi_port.c` |
| 3 | `*ESE` | 动作 | — | （待补） | `scpi_port.c` |
| 4 | `*ESE?` | 查询 | — | （待补） | `scpi_port.c` |
| 5 | `*ESR?` | 查询 | — | （待补） | `scpi_port.c` |
| 6 | `*IDN?` | 查询 | — | （待补） | `scpi_port.c` |
| 7 | `*OPC` | 动作 | — | （待补） | `scpi_port.c` |
| 8 | `*OPC?` | 查询 | — | （待补） | `scpi_port.c` |
| 9 | `*RST` | 动作 | — | （待补） | `scpi_port.c` |
| 10 | `*SRE` | 动作 | — | （待补） | `scpi_port.c` |
| 11 | `*SRE?` | 查询 | — | （待补） | `scpi_port.c` |
| 12 | `*STB?` | 查询 | — | （待补） | `scpi_port.c` |
| 13 | `*WAI` | 动作 | — | （待补） | `scpi_port.c` |
| 14 | `SYSTem:ERRor[:NEXT]?` | 查询 | — | （待补） | `scpi_port.c` |
| 15 | `SYSTem:ERRor:COUNt?` | 查询 | — | （待补） | `scpi_port.c` |
| 16 | `SYSTem:VERSion?` | 查询 | — | （待补） | `scpi_port.c` |

## 系统运行时（151）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `SYSTem:CALibration:LIMit:OVERRide` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 2 | `SYSTem:CALibration:LIMit:OVERRide?` | 查询 | — | （待补） | `scpi_calibration_commands.h` |
| 3 | `SYSTem:CALibration:LIMit:DEFAult` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 4 | `SYSTem:LOOP:STATus?` | 查询 | — | （待补） | `scpi_loop_engine_commands.h` |
| 5 | `SYSTem:TRACe:LAST?` | 查询 | — | （待补） | `scpi_storage_commands.h` |
| 6 | `SYSTem:FAULT:LAST?` | 查询 | — | （待补） | `scpi_storage_commands.h` |
| 7 | `SYSTem:SYNC:VDC:STATus?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 8 | `SYSTem:SYNC:VDC:DPLL:STATus?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 9 | `SYSTem:SYNC:VDC:DCO?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 10 | `SYSTem:SYNC:VDC:TDMA:PLAN?` | 查询 | <u32>、<u32>、<u32> | （待补） | `scpi_sync_commands.h` |
| 11 | `SYSTem:SYNC:VDC:TDMA:RING?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 12 | `SYSTem:SYNC:VDC:TDMA:STATus?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 13 | `SYSTem:SYNC:VDC:TDMA:PHYS?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 14 | `SYSTem:SYNC:VDC:PATH:DELay?` | 查询 | <u32>、<u32> | （待补） | `scpi_sync_commands.h` |
| 15 | `SYSTem:SYNC:VDC:LOCK:READiness?` | 查询 | — | HAOFV: the page reports the owner's declaration, it does not decide. vdc_dpll_manager_lock_verdict() is the single verdict - it keeps the historical l | `scpi_sync_commands.h` |
| 16 | `SYSTem:SYNC:VDC:DPLL:PROVisional?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 17 | `SYSTem:SYNC:VDC:DPLL:PROVisional` | 设置 | <u32> | （待补） | `scpi_sync_commands.h` |
| 18 | `SYSTem:SYNC:VDC:DPLL:TRACe:ARM` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 19 | `SYSTem:SYNC:VDC:DPLL:TRACe:STOP` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 20 | `SYSTem:SYNC:VDC:DPLL:TRACe:STATus?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 21 | `SYSTem:SYNC:VDC:DPLL:TRACe:READ?` | 查询 | <u32>、<u32> | （待补） | `scpi_sync_commands.h` |
| 22 | `SYSTem:SYNC:VDC:DPLL:TRACe:SAVE` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 23 | `SYSTem:SYNC:VDC:OBServer:WAVEform:ARM` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 24 | `SYSTem:SYNC:VDC:OBServer:WAVEform:STOP` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 25 | `SYSTem:SYNC:VDC:OBServer:WAVEform:STATus?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 26 | `SYSTem:SYNC:VDC:OBServer:WAVEform:SAVE` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 27 | `SYSTem:SYNC:VDC:OBServer:RING?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 28 | `SYSTem:SYNC:VDC:OBServer:TDMA:SELFtest?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 29 | `SYSTem:SYNC:VDC:OBServer:TDMA:SELFtest` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_sync_commands.h` |
| 30 | `SYSTem:SYNC:VDC:OBServer:TDMA` | 设置 | <u32>、<u32>、<u32>、<u32> | （待补） | `scpi_sync_commands.h` |
| 31 | `SYSTem:SYNC:VDC:OBServer` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_sync_commands.h` |
| 32 | `SYSTem:SYNC:VDC:OBServer?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 33 | `SYSTem:SYNC:VDC:OBServer:PHASe?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 34 | `SYSTem:SYNC:VDC:DPLL:TUNE` | 设置 | <u32>、<u32>、<u32> | （待补） | `scpi_sync_commands.h` |
| 35 | `SYSTem:SYNC:VDC:DPLL:FILTer?` | 查询 | — | Field 0 is the published lock state, LOCKED(1)/UNLOCK(0) - not the Domain ladder (the master's internal acquisition process). The remaining fields are | `scpi_sync_commands.h` |
| 36 | `SYSTem:SYNC:VDC:DPLL:COEFficient?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 37 | `SYSTem:SYNC:VDC:DPLL:COEFficient` | 设置 | <u32>、<u32>、<u32> | （待补） | `scpi_sync_commands.h` |
| 38 | `SYSTem:SYNC:VDC:DPLL:OVERRide` | 设置 | <u32> | （待补） | `scpi_sync_commands.h` |
| 39 | `SYSTem:SYNC:VDC:DPLL:OVERRide?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 40 | `SYSTem:SYNC:VDC:DPLL:DEFAult` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 41 | `SYSTem:SYNC:VDC:DPLL:STORe` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 42 | `SYSTem:SYNC:VDC:DPLL:ROLE` | 设置 | <u32>、<u32> | （待补） | `scpi_sync_commands.h` |
| 43 | `SYSTem:SYNC:VDC:DPLL:ROLE?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 44 | `SYSTem:SYNC:VDC:DPLL:ROLE:STATus?` | 查询 | — | （待补） | `scpi_sync_commands.h` |
| 45 | `SYSTem:SYNC:VDC:DPLL:ROLE:STORe` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 46 | `SYSTem:SCPI:PERMission?` | 查询 | — | （待补） | `scpi_system_access_commands.h` |
| 47 | `SYSTem:SCPI:PERMission` | 动作 | — | （待补） | `scpi_system_access_commands.h` |
| 48 | `SYSTem:SCPI:ROLE?` | 查询 | — | （待补） | `scpi_system_access_commands.h` |
| 49 | `SYSTem:SCPI:ROLE` | 动作 | — | （待补） | `scpi_system_access_commands.h` |
| 50 | `SYSTem:RUN:LAST?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 51 | `SYSTem:RUN:SUMMary?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 52 | `SYSTem:RUN:LOG?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 53 | `SYSTem:TRACe:DATA?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 54 | `SYSTem:T2:COUNt?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 55 | `SYSTem:T2:DATA?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 56 | `SYSTem:TRIGger:DBG?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 57 | `SYSTem:RESource?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 58 | `SYSTem:RESource:TRAINing?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 59 | `SYSTem:FAULT:CLEAr` | 动作 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 60 | `SYSTem:FW:VERSion?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 61 | `SYSTem:FW:BUILD?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 62 | `SYSTem:CORE?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 63 | `SYSTem:RTOS:STATus?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 64 | `SYSTem:UI:KEYs?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 65 | `SYSTem:BOARD:ID?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 66 | `SYSTem:BOARD:NO` | 设置 | <u32> | （待补） | `scpi_system_runtime_commands.h` |
| 67 | `SYSTem:BOARD:NO?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 68 | `SYSTem:BOARD:MAP?` | 查询 | — | Stable machine-readable binding: unique address first, logical NO second. The address is the only board identity; USB COM numbers are intentionally ab | `scpi_system_runtime_commands.h` |
| 69 | `SYSTem:LED:STATus?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 70 | `SYSTem:WATCHdog:STATus?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 71 | `SYSTem:WATCHdog:LOG?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 72 | `SYSTem:WATCHdog:OTA:PHase?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 73 | `SYSTem:WATCHdog:TEST` | 动作 | — | （待补） | `scpi_system_runtime_commands.h` |
| 74 | `SYSTem:CONFigure:STAT?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 75 | `SYSTem:CONFigure:ROLE?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 76 | `SYSTem:CONFigure:LOOP?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 77 | `SYSTem:CONFigure:ACT?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 78 | `SYSTem:CONFigure:CAL?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 79 | `SYSTem:CONFigure:ACK?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 80 | `SYSTem:CONFigure:NACK?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 81 | `SYSTem:COMMand:ACK?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 82 | `SYSTem:COMMand:NACK?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 83 | `SYSTem:SCPI:RUN:ALLOW?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 84 | `SYSTem:TDMA:FLIGHT:FIFO?` | 查询 | — | Appended attribution of tx_publish_reject_count; the 17 primary fields above keep their existing order and index. | `scpi_system_snapshot_commands.h` |
| 85 | `SYSTem:TDMA:FLIGHT:PRIORity?` | 查询 | — | Reject attribution by mailbox class: the three buckets partition reject_count; last_reject_class is 0xFF when it could not be attributed. | `scpi_system_snapshot_commands.h` |
| 86 | `SYSTem:TDMA:FLIGHT:PRIORity:BUDGet?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 87 | `SYSTem:TDMA:FLIGHT:PRIORity:TIMing?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 88 | `SYSTem:TDMA:FLIGHT:PRIORity:RECord?` | 查询 | <u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 89 | `SYSTem:TDMA:FLIGHT:PRIORity:CONSumed?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 90 | `SYSTem:TDMA:FLIGHT:FIFO:RESet` | 动作 | — | The RefMem task must finish its copy/view before reset can reclaim storage. Only Core0 SCPI yields here; Core1 never takes this lock. | `scpi_system_snapshot_commands.h` |
| 91 | `SYSTem:TDMA:FLIGHT:MODE?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 92 | `SYSTem:TDMA:FLIGHT:MODE` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 93 | `SYSTem:TDMA:FLIGHT:CLOCK:EVIDence?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 94 | `SYSTem:TDMA:FLIGHT:CLOCK:EVIDence` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 95 | `SYSTem:TDMA:FLIGHT:CLOCK:EVIDence:STATus?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 96 | `SYSTem:TDMA:SEAT:ROTation` | 设置 | <u32>、<u32>、<u32> | Optional third field (owner decision 2026-10-16): slots of the cycle reserved for the path that does not use the fast channel. Absent => 0 => the two- | `scpi_system_snapshot_commands.h` |
| 97 | `SYSTem:TDMA:SEAT:ROTation?` | 查询 | — | Configured radio first, then the DECLARED limits it is bounded by. A capture has to be readable without the firmware source: the seat's counters are o | `scpi_system_snapshot_commands.h` |
| 98 | `SYSTem:TDMA:SEAT:COUNters?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 99 | `SYSTem:TDMA:SEAT:HEALth?` | 查询 | — | The per-class reject split lives in the priority-lane snapshot, not in the position transport snapshot; read each fact from its own owner. The lane sn | `scpi_system_snapshot_commands.h` |
| 100 | `SYSTem:TDMA:LOAD:MASK?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 101 | `SYSTem:TDMA:LOAD:MASK` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 102 | `SYSTem:TDMA:SCHEDule?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 103 | `SYSTem:TDMA:PERiod` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 104 | `SYSTem:TDMA:PERiod?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 105 | `SYSTem:TDMA:PROFile?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 106 | `SYSTem:TDMA:PROFile:PEAK?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 107 | `SYSTem:TDMA:PROFile:RUN?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 108 | `SYSTem:TDMA:PROFile:OTHer?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 109 | `SYSTem:TDMA:PROFile:STEady?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 110 | `SYSTem:TDMA:PROFile:RX?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 111 | `SYSTem:TDMA:FLIGHT:RX:CUT?` | 查询 | — | A frozen, owner-published startup record. This STOP-only export does not read live PIO/DMA capture state or expand the owner snapshot. | `scpi_system_snapshot_commands.h` |
| 112 | `SYSTem:TDMA:PROFile:RESet` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 113 | `SYSTem:TDMA:FLIGHT:CRC:DIAGnostic?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 114 | `SYSTem:TDMA:FLIGHT:PROCess?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 115 | `SYSTem:TDMA:FLIGHT:TX` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 116 | `SYSTem:TDMA:FLIGHT:RX?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 117 | `SYSTem:TDMA:RING:LOCAL` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 118 | `SYSTem:TDMA:RING:TOPology` | 设置 | <u32>、<u32>、<u32> | A stopped-state refusal here is TRANSIENT: the ring snapshot, the VDC manager plan and the RefMem application model can still be settling right after  | `scpi_system_snapshot_commands.h` |
| 119 | `SYSTem:TDMA:RING:LOOP:DELay` | 设置 | <u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 120 | `SYSTem:TDMA:RING:LOOP:DELay?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 121 | `SYSTem:TDMA:RING:DIAGnostic` | 设置 | <u32>、<u32> | Optional second parameter: the gate policy switch only, not the physical emission cadence budget (tdma_origin_cadence_t). Presence is decided by the r | `scpi_system_snapshot_commands.h` |
| 122 | `SYSTem:TDMA:RING:DIAGnostic?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 123 | `SYSTem:TDMA:RING:ORIGin:CADEence?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 124 | `SYSTem:TDMA:RING:BURSt` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 125 | `SYSTem:TDMA:RING:BURSt?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 126 | `SYSTem:TDMA:RING:BURSt:STATus?` | 查询 | — | Post-STOP facts only. Bracket the independent owner snapshots with the same applied STOP generation; none of these counts grants identity. | `scpi_system_snapshot_commands.h` |
| 127 | `SYSTem:TDMA:RING:GEOMetry` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 128 | `SYSTem:TDMA:RING:GEOMetry?` | 查询 | — | Lifecycle readback at a stable config ACK. Also supports armed, non-emitting readiness; no real-time SCPI sampling is required. | `scpi_system_snapshot_commands.h` |
| 129 | `SYSTem:TDMA:RING:ARM:STATus?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 130 | `SYSTem:TDMA:RING:STATus?` | 查询 | — | Appended loop/beat measurement: the interval between successive arrivals of the reference's own frame at the reference (one ring revolution). Existing | `scpi_system_snapshot_commands.h` |
| 131 | `SYSTem:TDMA:RING:CONFig?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 132 | `SYSTem:TDMA:RECord:ARM` | 设置 | <u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 133 | `SYSTem:TDMA:RECord:STATus?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 134 | `SYSTem:TDMA:RECord:CANCel` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 135 | `SYSTem:TDMA:RECord:SAVE` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 136 | `SYSTem:TDMA:RECord:READ?` | 查询 | <u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 137 | `SYSTem:TDMA:RING:ARM` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 138 | `SYSTem:TDMA:RING:TRAIN` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 139 | `SYSTem:TDMA:RING:TRAIN:STATus?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 140 | `SYSTem:TDMA:RING:START` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 141 | `SYSTem:TDMA:RING:STOP` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 142 | `SYSTem:TDMA:RING:LOG?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 143 | `SYSTem:TDMA:RING:LOG` | 设置 | <0\|1> | （待补） | `scpi_system_snapshot_commands.h` |
| 144 | `SYSTem:TDMA:HEALth?` | 查询 | — | 紧凑健康/增长计数页（流式 soak 用；单次查询替代运行态 + process 两页） | `scpi_system_snapshot_commands.h` |
| 145 | `SYSTem:CORE:VECTOR?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 146 | `SYSTem:PROTection:STATus?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 147 | `SYSTem:MODE:TABle?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 148 | `SYSTem:RESource:TABle?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 149 | `SYSTem:FAULT:TABle?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 150 | `SYSTem:USB:MODE?` | 查询 | — | （待补） | `scpi_usb_control.h` |
| 151 | `SYSTem:USB:MODE` | 动作 | — | （待补） | `scpi_usb_control.h` |
| 152 | `SYSTem:USB:BOOT` | 动作 | — | （待补） | `scpi_usb_control.h` |

## 系统快照（3）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `SYSTem:SNAPshot:WRITe` | 动作 | — | （待补） | `scpi_storage_commands.h` |
| 2 | `SYSTem:SNAPshot:LAST?` | 查询 | — | （待补） | `scpi_storage_commands.h` |
| 3 | `SYSTem:SNAPshot:DATA?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |

## 系统诊断（6）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `SYSTem:DIAGnostic:FLASh:VALidate` | 设置 | <u32>、<u32> | Let FlashTransactionAO allocate a fresh identity on every run. | `scpi_system_diagnostics_commands.h` |
| 2 | `SYSTem:DIAGnostic:FLASh:MAP?` | 查询 | <u32> | （待补） | `scpi_system_diagnostics_commands.h` |
| 3 | `SYSTem:DIAGnostic:FLASh:ACCEss?` | 查询 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_system_diagnostics_commands.h` |
| 4 | `SYSTem:DIAGnostic:FLASh:TRANsaction?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 5 | `SYSTem:DIAGnostic:FLASh:JEDEC?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 6 | `SYSTem:DIAGnostic:SENSors?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |

## 配置（装载）（27）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `CONFigure:CALibration:LINK:ADD` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 2 | `CONFigure:CALibration:LINK:UPDate` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 3 | `CONFigure:CALibration:LINK:DELete` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 4 | `CONFigure:CALibration:LINK:CLEAr` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 5 | `CONFigure:CALibration:PARameter:ADD` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 6 | `CONFigure:CALibration:PARameter:SET` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 7 | `CONFigure:CALibration:PARameter:DELete` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 8 | `CONFigure:CALibration:META` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 9 | `CONFigure:CALibration:LIMit` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 10 | `CONFigure:TRIGger` | 动作 | — | （待补） | `scpi_config_commands.h` |
| 11 | `CONFigure:ANGLe:SWEEp` | 动作 | — | （待补） | `scpi_config_commands.h` |
| 12 | `CONFigure:ANGLe:PULSe` | 动作 | — | （待补） | `scpi_config_commands.h` |
| 13 | `CONFigure:ANGLe:BREAkpoint` | 动作 | — | （待补） | `scpi_config_commands.h` |
| 14 | `CONFigure:ANGLe:BREAkpoint:CLEAr` | 动作 | — | （待补） | `scpi_config_commands.h` |
| 15 | `CONFigure:SEQuence` | 动作 | — | （待补） | `scpi_config_commands.h` |
| 16 | `CONFigure:SEQuence:ACTive` | 动作 | — | （待补） | `scpi_config_commands.h` |
| 17 | `CONFigure:SWITch#` | 动作 | — | （待补） | `scpi_config_commands.h` |
| 18 | `CONFigure:MODEl:TURNtable:LOAD` | 设置 | <u32>、<u32> | （待补） | `scpi_model_commands.h` |
| 19 | `CONFigure:MODEl:TURNtable:TRIGger` | 设置 | <u32>、<u32>、<0\|1> | Fields this command does not carry must keep their value: the struct gained groups_per_position (A.3.87), so a bare local left it uninitialised and va | `scpi_model_commands.h` |
| 20 | `CONFigure:MODEl:TURNtable:MOTion` | 动作 | — | （待补） | `scpi_model_commands.h` |
| 21 | `CONFigure:MODEl:TURNtable:GROups` | 设置 | <u32> | Read-modify-write the trigger config so only the sub-pulses per position change (TRIGGER_T2_TODO.md A.3.87). | `scpi_model_commands.h` |
| 22 | `CONFigure:SYNC:CALibration` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 23 | `CONFigure:SYNC:RING` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 24 | `CONFigure:SYNC:VDC:DPLL` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 25 | `CONFigure:SYNC:GATE` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 26 | `CONFigure:SYNC:LIMit` | 动作 | — | （待补） | `scpi_sync_commands.h` |
| 27 | `CONFigure:SEQuence:REPeat` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |

## VDC / DPLL（89）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `SYSTem:VDC:FEEDback:SESSion` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 2 | `SYSTem:VDC:FEEDback:SESSion?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 3 | `SYSTem:VDC:PRIORity:SYNC` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 4 | `SYSTem:VDC:PRIORity:SYNC?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 5 | `SYSTem:VDC:PRIORity:MATCH` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 6 | `SYSTem:VDC:PRIORity:MATCH?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 7 | `SYSTem:VDC:PRIORity:MATCH:STATus?` | 查询 | — | Ring STOP is authoritative. active describes the last Core1 service. | `scpi_system_snapshot_commands.h` |
| 8 | `SYSTem:VDC:PRIORity:FOLLow` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 9 | `SYSTem:VDC:PRIORity:FOLLow?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 10 | `SYSTem:VDC:PRIORity:FOLLow:STATus?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 11 | `SYSTem:VDC:PRIORity:FOLLow:HEALth?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 12 | `SYSTem:VDC:OUTPut:DELay` | 动作 | — | Generic integer conversion saturates and accepts numeric prefixes. Consume the whole decimal ns token, with checked magnitude arithmetic. | `scpi_system_snapshot_commands.h` |
| 13 | `SYSTem:VDC:OUTPut:DELay?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 14 | `SYSTem:VDC:OUTPut:DELay:DEFAult` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 15 | `SYSTem:VDC:OUTPut:DELay:RECall` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 16 | `SYSTem:VDC:OUTPut:DELay:STORe` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 17 | `SYSTem:VDC:OUTPut:TIMing` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 18 | `SYSTem:VDC:OUTPut:TIMing?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 19 | `SYSTem:VDC:OUTPut:TIMing:DEFAult` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 20 | `SYSTem:VDC:OUTPut:TIMing:RECall` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 21 | `SYSTem:VDC:OUTPut:TIMing:STORe` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 22 | `SYSTem:VDC:REFerence:CONFigure` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 23 | `SYSTem:VDC:REFerence:CONFigure?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 24 | `SYSTem:VDC:REFerence:ENABle` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 25 | `SYSTem:VDC:REFerence:ENABle?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 26 | `SYSTem:VDC:REFerence:STATus?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 27 | `SYSTem:VDC:REFerence:DEFAult` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 28 | `SYSTem:VDC:REFerence:RECall` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 29 | `SYSTem:VDC:REFerence:STORe` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 30 | `SYSTem:VDC:REFerence:DISCipline` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 31 | `SYSTem:VDC:REFerence:DISCipline?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 32 | `SYSTem:VDC:REFerence:DISCipline:CONFigure` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 33 | `SYSTem:VDC:REFerence:DISCipline:CONFigure?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 34 | `SYSTem:VDC:REFerence:DISCipline:ACTive?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 35 | `SYSTem:VDC:REFerence:DISCipline:DEFAult` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 36 | `SYSTem:VDC:REFerence:DISCipline:RECall` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 37 | `SYSTem:VDC:REFerence:DISCipline:STORe` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 38 | `SYSTem:VDC:PRIORity:FOLLow:BASEline` | 设置 | <u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 39 | `SYSTem:VDC:PRIORity:FOLLow:BASEline?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 40 | `SYSTem:VDC:PRIORity:FOLLow:BASEline:DEFAult` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 41 | `SYSTem:VDC:PRIORity:FOLLow:BASEline:RECall` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 42 | `SYSTem:VDC:PRIORity:FOLLow:BASEline:STORe` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 43 | `SYSTem:VDC:PRIORity:TRACe:ARM` | 设置 | <u32> | Admission only. STOP-only STATUS must acknowledge this request before ARM. | `scpi_system_snapshot_commands.h` |
| 44 | `SYSTem:VDC:PRIORity:TRACe:PHASe` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 45 | `SYSTem:VDC:PRIORity:TRACe:SUMMary:PHASe` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 46 | `SYSTem:VDC:PRIORity:TRACe:SUMMary:ORIGin` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 47 | `SYSTem:VDC:PRIORity:TRACe:GUARd:PHASe` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 48 | `SYSTem:VDC:PRIORity:TRACe:GUARd:ORIGin` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 49 | `SYSTem:VDC:PRIORity:TRACe:GUARd?` | 查询 | — | Bounded metadata only. No read lease, record scan, CRC or hardware access. | `scpi_system_snapshot_commands.h` |
| 50 | `SYSTem:VDC:PRIORity:FOLLow:PHASe` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 51 | `SYSTem:VDC:PRIORity:FOLLow:PHASe?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 52 | `SYSTem:VDC:PRIORity:FOLLow:PHASe:STATus?` | 查询 | — | Word ABI: signed 64-bit values use little-endian two's-complement parts. | `scpi_system_snapshot_commands.h` |
| 53 | `SYSTem:VDC:PRIORity:TRACe:ORIGin` | 设置 | <u32> | Admission only. STOP-only STATUS must acknowledge this request before ARM. | `scpi_system_snapshot_commands.h` |
| 54 | `SYSTem:VDC:PRIORity:TRACe:STOP` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 55 | `SYSTem:VDC:PRIORity:TRACe:RELease` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 56 | `SYSTem:VDC:PRIORity:TRACe:STATus?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 57 | `SYSTem:VDC:PRIORity:TRACe:READ?` | 查询 | <u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 58 | `SYSTem:VDC:PRIORity:TX?` | 查询 | — | Provider-entry evidence (VDC-ISO-001) is appended only after every existing position, including the event-time parts and mailbox hex, so no previously | `scpi_system_snapshot_commands.h` |
| 59 | `SYSTem:VDC:PRIORity:TX:GAP` | 设置 | <u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 60 | `SYSTem:VDC:PRIORity:TX:GAP?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 61 | `SYSTem:VDC:PRIORity:TX:GAP:STATus?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 62 | `SYSTem:VDC:PRIORity:RX?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 63 | `SYSTem:VDC:FEEDback:PROBe` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 64 | `SYSTem:VDC:FEEDback:AUTO` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 65 | `SYSTem:VDC:FEEDback:AUTO?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 66 | `SYSTem:VDC:FEEDback:REFerence` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 67 | `SYSTem:VDC:FEEDback:REFerence?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 68 | `SYSTem:VDC:FEEDback:BOUNDary?` | 查询 | <u32> | A retained post-STOP export only. Never sample this control state while RUN or while STOP is still awaiting its Core1 application. | `scpi_system_snapshot_commands.h` |
| 69 | `SYSTem:VDC:FEEDback:LOCALfollow` | 设置 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 70 | `SYSTem:VDC:FEEDback:LOCALfollow?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 71 | `SYSTem:VDC:FEEDback:MODel?` | 查询 | — | Schema 1 is a retained Core1 commit, including an empty token before publication. It is not an assertion of current/GPIO output validity. | `scpi_system_snapshot_commands.h` |
| 72 | `SYSTem:VDC:OUTPut:FIXed` | 设置 | <u32>、<u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 73 | `SYSTem:VDC:OUTPut:RUN` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 74 | `SYSTem:VDC:OUTPut:RUN:STOP` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 75 | `SYSTem:VDC:OUTPut:RUN?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 76 | `SYSTem:VDC:OUTPut:FIXed:STOP` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 77 | `SYSTem:VDC:OUTPut:FIXed?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 78 | `SYSTem:VDC:OUTPut:SUBDivision` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 79 | `SYSTem:VDC:OUTPut:SUBDivision:STOP` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 80 | `SYSTem:VDC:OUTPut:SUBDivision?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 81 | `SYSTem:VDC:OUTPut:SUBDivision:WINdow` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 82 | `SYSTem:VDC:OUTPut:SUBDivision:WINdow:STOP` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 83 | `SYSTem:VDC:OUTPut:SUBDivision:WINdow?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 84 | `SYSTem:VDC:OUTPut:SUBDivision:AFTER:STATus?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 85 | `SYSTem:VDC:OUTPut:SUBDivision:AFTER:ENABle` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 86 | `SYSTem:VDC:OUTPut:SUBDivision:AFTER:CONFigure` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 87 | `SYSTem:VDC:OUTPut:SUBDivision:AFTER:RUN?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 88 | `SYSTem:VDC:FEEDback:BRIDge?` | 查询 | — | STOP-only configuration evidence; individual counter reads are not an accepted bridge and do not authorize a model projection. | `scpi_system_snapshot_commands.h` |
| 89 | `SYSTem:VDC:FEEDback:MATCh?` | 查询 | <u32> | Schemas 2/3 use ms ages and model-token identities. Schema 2 carries projected absolute output intervals; schema 3 carries rate coordinates. | `scpi_system_snapshot_commands.h` |

## TDMA 同步（REFMEM 席位传输）（12）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `SYSTem:REFMEM:SYNC:TDMA:STATus?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 2 | `SYSTem:REFMEM:SYNC:TDMA:NODE:TX` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 3 | `SYSTem:REFMEM:SYNC:TDMA:TX` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 4 | `SYSTem:REFMEM:SYNC:TDMA:RX` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 5 | `SYSTem:REFMEM:SYNC:TDMA:FRAMe?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 6 | `SYSTem:REFMEM:SYNC:TDMA:VDC?` | 查询 | <u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 7 | `SYSTem:REFMEM:SYNC:TDMA:VDC:RX?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 8 | `SYSTem:REFMEM:SYNC:TDMA:VDC:FLIGHT?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 9 | `SYSTem:REFMEM:SYNC:TDMA:VDC:FEEDback:RX?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 10 | `SYSTem:REFMEM:SYNC:TDMA:VDC:FEEDback:TX?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 11 | `SYSTem:REFMEM:SYNC:TDMA:VDC:FEEDback:PROof?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 12 | `SYSTem:REFMEM:SYNC:TDMA:ABORt` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |

## REFMEM 同步（19）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `SYSTem:REFMEM:SYNC:INITialize` | 设置 | <u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 2 | `SYSTem:REFMEM:SYNC:HELLo?` | 查询 | <u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 3 | `SYSTem:REFMEM:SYNC:EPOCh?` | 查询 | <u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 4 | `SYSTem:REFMEM:SYNC:DELTa?` | 查询 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 5 | `SYSTem:REFMEM:SYNC:ACK?` | 查询 | <u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 6 | `SYSTem:REFMEM:SYNC:FENCe?` | 查询 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 7 | `SYSTem:REFMEM:SYNC:QUALity:FRAMe?` | 查询 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 8 | `SYSTem:REFMEM:SYNC:RX` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 9 | `SYSTem:REFMEM:SYNC:MIRRor?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 10 | `SYSTem:REFMEM:SYNC:ACK:STATus?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 11 | `SYSTem:REFMEM:SYNC:FENCe:STATus?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 12 | `SYSTem:REFMEM:SYNC:QUALity:STATus?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 13 | `SYSTem:REFMEM:SYNC:PEER?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 14 | `SYSTem:REFMEM:SYNC:QUALity?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 15 | `SYSTem:REFMEM:SYNC:ADAPter?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 16 | `SYSTem:REFMEM:SYNC:AUTO` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 17 | `SYSTem:REFMEM:SYNC:AUTO?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 18 | `SYSTem:REFMEM:SYNC:FLIGHT?` | 查询 | — | Appended: WHY the VDC feedback path last refused to let a frame out. A refusal stops the whole flight publish path (no VDC and no T2 frame), so the br | `scpi_system_snapshot_commands.h` |
| 19 | `SYSTem:REFMEM:SYNC:FLIGHT:POSition?` | 查询 | — | Appended last: positional readers keep their existing indices. | `scpi_system_snapshot_commands.h` |

## REFMEM 分布式内存（18）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `SYSTem:REFMEM:STATus?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 2 | `SYSTem:REFMEM:NODE?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 3 | `SYSTem:REFMEM:BOARD?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 4 | `SYSTem:REFMEM:CLAIM?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 5 | `SYSTem:REFMEM:CLAIM:EVIDence?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 6 | `SYSTem:REFMEM:LOAD:SD` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 7 | `SYSTem:REFMEM:LOAD:NODE` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 8 | `SYSTem:REFMEM:LOAD:BOARD` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 9 | `SYSTem:REFMEM:LOAD:ACTivate` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 10 | `SYSTem:REFMEM:LOAD:STATus?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 11 | `SYSTem:REFMEM:LOAD:BOARD:STATus?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 12 | `SYSTem:REFMEM:TABle:IMAGe?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 13 | `SYSTem:REFMEM:TABle:VIEW?` | 查询 | <u32>、<u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 14 | `SYSTem:REFMEM:TABle?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 15 | `SYSTem:REFMEM:QUALity?` | 查询 | <u32> | （待补） | `scpi_system_snapshot_commands.h` |
| 16 | `SYSTem:REFMEM:VDC:VECtor?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 17 | `SYSTem:REFMEM:VDC:PRIORity?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 18 | `SYSTem:REFMEM:DPLL:VECtor?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |

## T2 预约（位置/网格）（10）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `TRIGger:REServation:T2:CONFigure` | 设置 | <u32> | 配置预约：run_id / 源槽位 / target_mask / origin / 前后滚 / 阈值 / 角度与每度脉冲 | `scpi_system_snapshot_commands.h` |
| 2 | `TRIGger:REServation:T2:MOUNT` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 3 | `TRIGger:REServation:T2:UNMOUNT` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 4 | `TRIGger:REServation:T2?` | 查询 | — | 预约状态与计数模型摘要（含 STOP/RUN 期间未达阈值的原因） | `scpi_system_snapshot_commands.h` |
| 5 | `TRIGger:REServation:T2:CURSor?` | 查询 | — | 位置游标：当前位置索引与所在时间片 | `scpi_system_snapshot_commands.h` |
| 6 | `TRIGger:REServation:T2:SCAN?` | 查询 | — | 扫描几何与预测窗摘要 | `scpi_system_snapshot_commands.h` |
| 7 | `TRIGger:REServation:T2:ANNOUNCE` | 动作 | — | 手工宣告一个位置（诊断用） | `scpi_system_snapshot_commands.h` |
| 8 | `TRIGger:REServation:T2:ANNOUNCE?` | 查询 | — | 最近一次宣告的结果 | `scpi_system_snapshot_commands.h` |
| 9 | `TRIGger:RESERVation:T2:DRAIN` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 10 | `TRIGger:RESERVation:T2:DRAIN?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |

## T2 序列时序（10）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `TRIGger:SEQuence:TIMing` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 2 | `TRIGger:SEQuence:TIMing?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 3 | `TRIGger:SEQuence:COUNts` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 4 | `TRIGger:SEQuence:COUNts?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 5 | `TRIGger:SEQuence:PERiod?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 6 | `TRIGger:SEQuence:REAdy` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 7 | `TRIGger:SEQuence:REAdy?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 8 | `TRIGger:SEQuence:CODE` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 9 | `TRIGger:SEQuence:CODE?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 10 | `TRIGger:SEQuence:REPeat?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |

## T2 动作执行（序列）（5）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `TRIGger:REServation:T2:ACTion?` | 查询 | — | 动作执行快照：各槽位 sequence/deadline/backend_state 与已完成/失败掩码 | `scpi_system_snapshot_commands.h` |
| 2 | `TRIGger:REServation:T2:ACTion:WHY?` | 查询 | — | 整表武装被拒原因：NO_READY/BUSY/CLOCK/TICK/PLAN/ORIGIN | `scpi_system_snapshot_commands.h` |
| 3 | `TRIGger:REServation:T2:ACTion:WINdow?` | 查询 | — | 窗口事实：handed/last/advance/jump/no_receipt/reuse 计数 | `scpi_system_snapshot_commands.h` |
| 4 | `TRIGger:REServation:T2:ACTion:DIAG?` | 查询 | — | 动作执行诊断页（run-time）：advance/finish/identity/terminal/arm 等 | `scpi_system_snapshot_commands.h` |
| 5 | `TRIGger:REServation:T2:ACTion:GWDIAG?` | 查询 | — | 网关执行器诊断页：plan/READY/迟到/八桶拒绝/svc_wait/complete 等 | `scpi_system_snapshot_commands.h` |

## 预留（席位）（57）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `TRIGger:REServation:DEPloyment:STAGe` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 2 | `TRIGger:REServation:DEPloyment:ACTivate` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 3 | `TRIGger:REServation:DEPloyment:CLEar` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 4 | `TRIGger:REServation:DEPloyment?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 5 | `TRIGger:REServation:DEPloyment:PLAN?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 6 | `TRIGger:REServation:GRID:CONFigure` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 7 | `TRIGger:REServation:GRID:CLEar` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 8 | `TRIGger:REServation:GRID?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 9 | `TRIGger:REServation:GRID:PLAN?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 10 | `TRIGger:REServation:GRID:ANCHor` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 11 | `TRIGger:REServation:GRID:ANCHor?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 12 | `TRIGger:REServation:GRID:ANCHor:PLAN?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 13 | `TRIGger:REServation:CURSor?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 14 | `TRIGger:REServation:TIMing?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 15 | `TRIGger:REServation:SOURce:CONFigure` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 16 | `TRIGger:REServation:SOURce:STARt` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 17 | `TRIGger:REServation:SOURce:STOP` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 18 | `TRIGger:REServation:SOURce:RELease` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 19 | `TRIGger:REServation:SOURce?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 20 | `TRIGger:REServation:PROBe:CONFigure` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 21 | `TRIGger:REServation:PROBe:SEND` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 22 | `TRIGger:REServation:PROBe:CLEar` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 23 | `TRIGger:REServation:PROBe?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 24 | `TRIGger:REServation:COUNter:LOAD` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 25 | `TRIGger:REServation:COUNter:STARt` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 26 | `TRIGger:REServation:COUNter:STOP` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 27 | `TRIGger:REServation:COUNter:CLEar` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 28 | `TRIGger:REServation:COUNter:UNLoad` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 29 | `TRIGger:REServation:COUNter?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 30 | `TRIGger:REServation:GATEway:LOAD` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 31 | `TRIGger:REServation:GATEway:STOP` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 32 | `TRIGger:REServation:GATEway:UNLoad` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 33 | `TRIGger:REServation:GATEway?` | 查询 | — | Appended: READY answer-vs-next-action evidence (see sync_io_gateway.h). | `scpi_system_snapshot_commands.h` |
| 34 | `TRIGger:REServation:SWITch:LOAD` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 35 | `TRIGger:REServation:SWITch:STAGe` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 36 | `TRIGger:REServation:SWITch:APPLy` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 37 | `TRIGger:REServation:SWITch:STOP` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 38 | `TRIGger:REServation:SWITch:UNLoad` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 39 | `TRIGger:REServation:SWITch?` | 查询 | — | Appended: why a whole-table arm was refused (all zero = never refused). | `scpi_system_snapshot_commands.h` |
| 40 | `TRIGger:REServation:ACTion:INSTall` | 动作 | — | Surface the install verdict on the seat mount status channel so a bench readout can name the failing check without a new page. | `scpi_system_snapshot_commands.h` |
| 41 | `TRIGger:REServation:ACTion:CLEar` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 42 | `TRIGger:REServation:ACTion?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 43 | `TRIGger:REServation:ACTion:ROW?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 44 | `TRIGger:REServation:ACTion:EXECute:PREPare` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 45 | `TRIGger:REServation:ACTion:EXECute:ARM` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 46 | `TRIGger:REServation:ACTion:EXECute:STOP` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 47 | `TRIGger:REServation:ACTion:EXECute:RELease` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 48 | `TRIGger:REServation:ACTion:EXECute?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 49 | `TRIGger:REServation:ACTion:EXECute:NOW?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 50 | `TRIGger:REServation:TEMPLate:INSTall` | 动作 | — | Surface the verdict on the seat mount status channel for bench readout. | `scpi_system_snapshot_commands.h` |
| 51 | `TRIGger:REServation:TEMPLate:CLEar` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 52 | `TRIGger:REServation:TEMPLate:ACTivate` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 53 | `TRIGger:REServation:TEMPLate:RELease` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 54 | `TRIGger:REServation:TEMPLate?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 55 | `TRIGger:REServation:SEAT:MOUNT` | 动作 | — | Entry marker: proves the write command reached this handler (99) before any later stage overwrites the reason. | `scpi_system_snapshot_commands.h` |
| 56 | `TRIGger:REServation:SEAT:MOUNT?` | 查询 | — | （待补） | `scpi_system_snapshot_commands.h` |
| 57 | `TRIGger:REServation:SEAT:CLEar` | 动作 | — | （待补） | `scpi_system_snapshot_commands.h` |

## 触发（产品 RUN/门禁）（7）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `TRIGger:MODE` | 设置 | <u32> | （待补） | `scpi_trigger_commands.h` |
| 2 | `TRIGger:MODE?` | 查询 | — | （待补） | `scpi_trigger_commands.h` |
| 3 | `TRIGger:STARt` | 动作 | — | （待补） | `scpi_trigger_commands.h` |
| 4 | `TRIGger:STOP` | 动作 | — | （待补） | `scpi_trigger_commands.h` |
| 5 | `TRIGger:PAUSe` | 动作 | — | （待补） | `scpi_trigger_commands.h` |
| 6 | `TRIGger:CONTinue` | 动作 | — | （待补） | `scpi_trigger_commands.h` |
| 7 | `TRIGger:ABORt` | 动作 | — | （待补） | `scpi_trigger_commands.h` |

## 实时 IO（49）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `REALtime:IO:OUTPut:WIDTh` | 设置 | <u32> | （待补） | `scpi_realtime_io_commands.h` |
| 2 | `REALtime:IO:OUTPut:WIDTh?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 3 | `REALtime:IO:OUTPut:IMMediate` | 动作 | — | （待补） | `scpi_realtime_io_commands.h` |
| 4 | `REALtime:IO:PULSe:WIDTh` | 设置 | <u32> | （待补） | `scpi_realtime_io_commands.h` |
| 5 | `REALtime:IO:PULSe:WIDTh?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 6 | `REALtime:IO:PULSe:IMMediate` | 动作 | — | （待补） | `scpi_realtime_io_commands.h` |
| 7 | `REALtime:IO:MARKer:WIDTh` | 设置 | <u32> | （待补） | `scpi_realtime_io_commands.h` |
| 8 | `REALtime:IO:MARKer:WIDTh?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 9 | `REALtime:IO:MARKer:IMMediate` | 动作 | — | （待补） | `scpi_realtime_io_commands.h` |
| 10 | `REALtime:IO:RJ45:WIDTh` | 设置 | <u32> | （待补） | `scpi_realtime_io_commands.h` |
| 11 | `REALtime:IO:RJ45:WIDTh?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 12 | `REALtime:IO:RJ45:IMMediate` | 动作 | — | （待补） | `scpi_realtime_io_commands.h` |
| 13 | `REALtime:IO:RJ45:PINs?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 14 | `REALtime:IO:PROFile?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 15 | `REALtime:IO:INPut:LEVel?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 16 | `REALtime:IO:OUTPut:MASK` | 设置 | <u32> | （待补） | `scpi_realtime_io_commands.h` |
| 17 | `REALtime:IO:OUTPut:MASK?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 18 | `REALtime:IO:OUTPut:RELease` | 动作 | — | （待补） | `scpi_realtime_io_commands.h` |
| 19 | `REALtime:IO:SMA:FREQuency:TX` | 设置 | <u32>、<u32> | （待补） | `scpi_realtime_io_commands.h` |
| 20 | `REALtime:IO:SMA:FREQuency:TX?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 21 | `REALtime:IO:SMA:FREQuency:RX?` | 查询 | <u32>、<u32> | （待补） | `scpi_realtime_io_commands.h` |
| 22 | `REALtime:IO:SMA:FREQuency:STOP` | 动作 | — | （待补） | `scpi_realtime_io_commands.h` |
| 23 | `REALtime:IO:MODel:PROFile?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 24 | `REALtime:IO:MODel:INPut:LEVel?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 25 | `REALtime:IO:MODel:OUTPut:MASK` | 设置 | <u32>、<u32> | （待补） | `scpi_realtime_io_commands.h` |
| 26 | `REALtime:IO:MODel:OUTPut:MASK?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 27 | `REALtime:IO:MODel:OUTPut:RELease` | 动作 | — | （待补） | `scpi_realtime_io_commands.h` |
| 28 | `REALtime:IO:MODel:PULSe:SCHEDule?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 29 | `REALtime:IO:SAMPle:RATE` | 设置 | <u32> | （待补） | `scpi_realtime_io_commands.h` |
| 30 | `REALtime:IO:SAMPle:RATE?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 31 | `REALtime:IO:SAMPle:STATe` | 设置 | <0\|1> | （待补） | `scpi_realtime_io_commands.h` |
| 32 | `REALtime:IO:SAMPle:STATe?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 33 | `REALtime:IO:SAMPle:LATCh?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 34 | `REALtime:IO:SAMPle:WINDow?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 35 | `REALtime:IO:SAMPle:DBG?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 36 | `REALtime:IO:ANALyzer:STATe?` | 查询 | — | Read-only Core0 snapshot. This query never arms/stops a persona, consumes target FIFOs, or changes GPIO ownership. | `scpi_realtime_io_commands.h` |
| 37 | `REALtime:IO:ANALyzer:ARM` | 动作 | — | （待补） | `scpi_realtime_io_commands.h` |
| 38 | `REALtime:IO:ANALyzer:EDGE:ARM` | 动作 | — | （待补） | `scpi_realtime_io_commands.h` |
| 39 | `REALtime:IO:ANALyzer:TRIGger:ARM` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_realtime_io_commands.h` |
| 40 | `REALtime:IO:ANALyzer:STOP` | 动作 | — | （待补） | `scpi_realtime_io_commands.h` |
| 41 | `REALtime:IO:ANALyzer:BURSt:ARM` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_realtime_io_commands.h` |
| 42 | `REALtime:IO:ANALyzer:BURSt:STATe?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 43 | `REALtime:IO:ANALyzer:BURSt:SAVE` | 设置 | <u32> | （待补） | `scpi_realtime_io_commands.h` |
| 44 | `REALtime:IO:ANALyzer:BURSt:EXPORT?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 45 | `REALtime:IO:CLOCk:FREQuency` | 设置 | <u32> | （待补） | `scpi_realtime_io_commands.h` |
| 46 | `REALtime:IO:CLOCk:FREQuency?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 47 | `REALtime:IO:CLOCk:STATe` | 设置 | <0\|1> | （待补） | `scpi_realtime_io_commands.h` |
| 48 | `REALtime:IO:CLOCk:STATe?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |
| 49 | `REALtime:IO:SYNC?` | 查询 | — | （待补） | `scpi_realtime_io_commands.h` |

## 实时序列（7）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `REALtime:SEQ:LENGth` | 设置 | <u32> | （待补） | `scpi_realtime_sequence_commands.h` |
| 2 | `REALtime:SEQ:LENGth?` | 查询 | — | （待补） | `scpi_realtime_sequence_commands.h` |
| 3 | `REALtime:SEQ:WIDTh` | 设置 | <u32> | （待补） | `scpi_realtime_sequence_commands.h` |
| 4 | `REALtime:SEQ:WIDTh?` | 查询 | — | （待补） | `scpi_realtime_sequence_commands.h` |
| 5 | `REALtime:SEQ:INDex?` | 查询 | — | （待补） | `scpi_realtime_sequence_commands.h` |
| 6 | `REALtime:SEQ:DATA` | 设置 | <块> | （待补） | `scpi_realtime_sequence_commands.h` |
| 7 | `REALtime:SEQ:DATA?` | 查询 | — | （待补） | `scpi_realtime_sequence_commands.h` |

## 编码器（6）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `REALtime:ENC:TARGet` | 设置 | <u32> | （待补） | `scpi_realtime_encoder_commands.h` |
| 2 | `REALtime:ENC:TARGet?` | 查询 | — | （待补） | `scpi_realtime_encoder_commands.h` |
| 3 | `REALtime:ENC:COUNt?` | 查询 | — | （待补） | `scpi_realtime_encoder_commands.h` |
| 4 | `REALtime:ENC:APIN` | 设置 | <u32> | （待补） | `scpi_realtime_encoder_commands.h` |
| 5 | `REALtime:ENC:APIN?` | 查询 | — | （待补） | `scpi_realtime_encoder_commands.h` |
| 6 | `REALtime:ENC:REVolution?` | 查询 | — | （待补） | `scpi_realtime_encoder_commands.h` |

## 脉冲计数（15）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `REALtime:PCNT:DECode` | 设置 | <u32> | （待补） | `scpi_realtime_pcnt_commands.h` |
| 2 | `REALtime:PCNT:DECode?` | 查询 | — | （待补） | `scpi_realtime_pcnt_commands.h` |
| 3 | `REALtime:PCNT:DIRection` | 设置 | <u32> | （待补） | `scpi_realtime_pcnt_commands.h` |
| 4 | `REALtime:PCNT:DIRection?` | 查询 | — | （待补） | `scpi_realtime_pcnt_commands.h` |
| 5 | `REALtime:PCNT:FILTer` | 设置 | <u32> | （待补） | `scpi_realtime_pcnt_commands.h` |
| 6 | `REALtime:PCNT:FILTer?` | 查询 | — | （待补） | `scpi_realtime_pcnt_commands.h` |
| 7 | `REALtime:PCNT:GATE` | 设置 | <0\|1> | （待补） | `scpi_realtime_pcnt_commands.h` |
| 8 | `REALtime:PCNT:GATE?` | 查询 | — | （待补） | `scpi_realtime_pcnt_commands.h` |
| 9 | `REALtime:PCNT:CMP` | 设置 | <u32> | （待补） | `scpi_realtime_pcnt_commands.h` |
| 10 | `REALtime:PCNT:CMP?` | 查询 | — | （待补） | `scpi_realtime_pcnt_commands.h` |
| 11 | `REALtime:PCNT:PRESet` | 设置 | <u32> | （待补） | `scpi_realtime_pcnt_commands.h` |
| 12 | `REALtime:PCNT:PRESet?` | 查询 | — | （待补） | `scpi_realtime_pcnt_commands.h` |
| 13 | `REALtime:PCNT:CLEar` | 动作 | — | （待补） | `scpi_realtime_pcnt_commands.h` |
| 14 | `REALtime:PCNT:TOTal?` | 查询 | — | （待补） | `scpi_realtime_pcnt_commands.h` |
| 15 | `REALtime:PCNT:FREQuency?` | 查询 | — | （待补） | `scpi_realtime_pcnt_commands.h` |

## 实时（其它）（13）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `REALtime:SOURce` | 设置 | <u32> | （待补） | `scpi_realtime_sequence_commands.h` |
| 2 | `REALtime:SOURce?` | 查询 | — | （待补） | `scpi_realtime_sequence_commands.h` |
| 3 | `REALtime:EDGE` | 设置 | <u32> | （待补） | `scpi_realtime_sequence_commands.h` |
| 4 | `REALtime:EDGE?` | 查询 | — | （待补） | `scpi_realtime_sequence_commands.h` |
| 5 | `REALtime:GATE` | 设置 | <0\|1> | （待补） | `scpi_realtime_sequence_commands.h` |
| 6 | `REALtime:GATE?` | 查询 | — | （待补） | `scpi_realtime_sequence_commands.h` |
| 7 | `REALtime:SAFE` | 设置 | <u32> | （待补） | `scpi_realtime_sequence_commands.h` |
| 8 | `REALtime:SAFE?` | 查询 | — | （待补） | `scpi_realtime_sequence_commands.h` |
| 9 | `REALtime:ARM` | 动作 | — | （待补） | `scpi_realtime_sequence_commands.h` |
| 10 | `REALtime:DISarm` | 动作 | — | （待补） | `scpi_realtime_sequence_commands.h` |
| 11 | `REALtime:DISAble` | 动作 | — | （待补） | `scpi_realtime_sequence_commands.h` |
| 12 | `REALtime:FAULT` | 动作 | — | （待补） | `scpi_realtime_sequence_commands.h` |
| 13 | `REALtime:STATus?` | 查询 | — | （待补） | `scpi_realtime_status_commands.h` |

## 校准（48）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `CALibration:ORIGin:TRIAL` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 2 | `CALibration:ORIGin:TRIAL:NORECord` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 3 | `CALibration:ORIGin:TRIAL:BLACKout` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 4 | `CALibration:ORIGin:TRIAL:BUILDCancel` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 5 | `CALibration:ORIGin:TRIAL:READY` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 6 | `CALibration:ORIGin:RELease` | 设置 | <u32>、<u32> | Accepted intent, not emitted. | `scpi_calibration_commands.h` |
| 7 | `CALibration:ORIGin:REVOKe` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 8 | `CALibration:STARt` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 9 | `CALibration:LOOPback:STARt` | 设置 | <u32> | （待补） | `scpi_calibration_commands.h` |
| 10 | `CALibration:LOOPback:STOP` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 11 | `CALibration:BIAS:STARt` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 12 | `CALibration:BIAS:STOP` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 13 | `CALibration:CLOCk:CODEd:STARt` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 14 | `CALibration:CLOCk:CODEd:STOP` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 15 | `CALibration:MARKer:ARM` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 16 | `CALibration:MARKer:STARt` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 17 | `CALibration:MARKer:INJect` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 18 | `CALibration:MARKer:STOP` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 19 | `CALibration:MARKer:CAPTure:SAVE` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 20 | `CALibration:DATA:ARM` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 21 | `CALibration:DATA:INJect` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 22 | `CALibration:DATA:STOP` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 23 | `CALibration:DATA:CAPTure:SAVE` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 24 | `CALibration:SCK:ARM` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 25 | `CALibration:SCK:INJect` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 26 | `CALibration:SCK:STOP` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 27 | `CALibration:SCK:CAPTure:SAVE` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 28 | `CALibration:RING:CAPTure:SAVE` | 设置 | <u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 29 | `CALibration:RING:CAPTure:LATCh` | 设置 | <u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 30 | `CALibration:TRAINing:STAGe:BEGin` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 31 | `CALibration:TRAINing:STAGe:LINK` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32> | Legacy commands omit both fields. A supplied offset requires a phase; malformed optional input must never mutate the staged link. | `scpi_calibration_commands.h` |
| 32 | `CALibration:TRAINing:STAGe:COMMit` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 33 | `CALibration:TRAINing:STAGe:CLEar` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 34 | `CALibration:TOPology:PROBe` | 设置 | <u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 35 | `CALibration:PATH:CANDidate:BEGin` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u64>、<u64>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 36 | `CALibration:PATH:CANDidate:LINK` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u32>、<u64>、<u64>、<u64>、<u32>、<u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 37 | `CALibration:PATH:CANDidate:FINalize` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 38 | `CALibration:PATH:CANDidate:CLEar` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 39 | `CALibration:P3:STARt` | 设置 | <u32>、<u32>、<u32>、<u32>、<u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 40 | `CALibration:P3:STOP` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 41 | `CALibration:SMA:CABLe:SOURce:STARt` | 设置 | <u32>、<u32>、<u32> | （待补） | `scpi_calibration_commands.h` |
| 42 | `CALibration:SMA:CABLe:SOURce:STOP` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 43 | `CALibration:SAVE` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 44 | `CALibration:STOP` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 45 | `CALibration:LOAD` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 46 | `CALibration:ACTivate` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 47 | `CALibration:ROLLback` | 动作 | — | （待补） | `scpi_calibration_commands.h` |
| 48 | `CALibration:CLEAr` | 动作 | — | （待补） | `scpi_calibration_commands.h` |

## 测量（7）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `MEASure:FREQuency?` | 查询 | <u32> | （待补） | `scpi_measure_commands.h` |
| 2 | `MEASure:PERiod?` | 查询 | — | （待补） | `scpi_measure_commands.h` |
| 3 | `MEASure:JITTer?` | 查询 | — | （待补） | `scpi_measure_commands.h` |
| 4 | `MEASure:PULSe:WIDTh?` | 查询 | — | （待补） | `scpi_measure_commands.h` |
| 5 | `MEASure:LINK:DELay?` | 查询 | — | （待补） | `scpi_measure_commands.h` |
| 6 | `MEASure:T2?` | 查询 | — | （待补） | `scpi_measure_commands.h` |
| 7 | `MEASure:REPort?` | 查询 | — | （待补） | `scpi_measure_commands.h` |

## 模型（转台）（2）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `MODEl:TURNtable:STARt` | 动作 | — | （待补） | `scpi_model_commands.h` |
| 2 | `MODEl:TURNtable:STOP` | 动作 | — | （待补） | `scpi_model_commands.h` |

## BISS 通信（71）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `COMMunication:BISS:CONFigure` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 2 | `COMMunication:BISS:ROLE` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 3 | `COMMunication:BISS:ROLE?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 4 | `COMMunication:BISS:DEVice` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 5 | `COMMunication:BISS:DEVice?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 6 | `COMMunication:BISS:CLOCk` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 7 | `COMMunication:BISS:CLOCk?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 8 | `COMMunication:BISS:FRAMe:BITS` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 9 | `COMMunication:BISS:FRAMe:BITS?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 10 | `COMMunication:BISS:POSition:OFFSet` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 11 | `COMMunication:BISS:POSition:OFFSet?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 12 | `COMMunication:BISS:POSition:BITS` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 13 | `COMMunication:BISS:POSition:BITS?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 14 | `COMMunication:BISS:POSition:MODulo` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 15 | `COMMunication:BISS:POSition:MODulo?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 16 | `COMMunication:BISS:SAMPle:EDGE` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 17 | `COMMunication:BISS:SAMPle:EDGE?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 18 | `COMMunication:BISS:SAMPle:DELay` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 19 | `COMMunication:BISS:SAMPle:DELay?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 20 | `COMMunication:BISS:SAMPle:SCAN` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 21 | `COMMunication:BISS:SAMPle:SCAN?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 22 | `COMMunication:BISS:SAMPle:SCAN:STARt` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 23 | `COMMunication:BISS:SAMPle:SCAN:STARt?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 24 | `COMMunication:BISS:SAMPle:SCAN:END` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 25 | `COMMunication:BISS:SAMPle:SCAN:END?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 26 | `COMMunication:BISS:SAMPle:SCAN:STEP` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 27 | `COMMunication:BISS:SAMPle:SCAN:STEP?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 28 | `COMMunication:BISS:TIMEout` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 29 | `COMMunication:BISS:TIMEout?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 30 | `COMMunication:BISS:ANCHor:OFFSet` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 31 | `COMMunication:BISS:ANCHor:OFFSet?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 32 | `COMMunication:BISS:ANCHor:BITS` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 33 | `COMMunication:BISS:ANCHor:BITS?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 34 | `COMMunication:BISS:ANCHor:MASK` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 35 | `COMMunication:BISS:ANCHor:MASK?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 36 | `COMMunication:BISS:ANCHor:VALue` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 37 | `COMMunication:BISS:ANCHor:VALue?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 38 | `COMMunication:BISS:ERRor:BIT` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 39 | `COMMunication:BISS:ERRor:BIT?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 40 | `COMMunication:BISS:WARNing:BIT` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 41 | `COMMunication:BISS:WARNing:BIT?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 42 | `COMMunication:BISS:STATus:GATE` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 43 | `COMMunication:BISS:STATus:GATE?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 44 | `COMMunication:BISS:CRC:OFFSet` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 45 | `COMMunication:BISS:CRC:OFFSet?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 46 | `COMMunication:BISS:CRC:BITS` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 47 | `COMMunication:BISS:CRC:BITS?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 48 | `COMMunication:BISS:CRC:COVer:OFFSet` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 49 | `COMMunication:BISS:CRC:COVer:OFFSet?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 50 | `COMMunication:BISS:CRC:COVer:BITS` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 51 | `COMMunication:BISS:CRC:COVer:BITS?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 52 | `COMMunication:BISS:CRC:POLYnomial` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 53 | `COMMunication:BISS:CRC:POLYnomial?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 54 | `COMMunication:BISS:CRC:INIT` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 55 | `COMMunication:BISS:CRC:INIT?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 56 | `COMMunication:BISS:CRC:XOR` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 57 | `COMMunication:BISS:CRC:XOR?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 58 | `COMMunication:BISS:CRC:INVert` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 59 | `COMMunication:BISS:CRC:INVert?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 60 | `COMMunication:BISS:CRC:GATE` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 61 | `COMMunication:BISS:CRC:GATE?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 62 | `COMMunication:BISS:LATency:OFFSet` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 63 | `COMMunication:BISS:LATency:OFFSet?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 64 | `COMMunication:BISS:TARGet` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 65 | `COMMunication:BISS:TARGet?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 66 | `COMMunication:BISS:PINs?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |
| 67 | `COMMunication:BISS:PULSe` | 设置 | <u32> | （待补） | `scpi_communication_biss_commands.h` |
| 68 | `COMMunication:BISS:FRAMe` | 设置 | <u32> | （待补） | `scpi_communication_biss_commands.h` |
| 69 | `COMMunication:BISS:CRC:ERRor` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 70 | `COMMunication:BISS:TIMEout:INJect` | 动作 | — | （待补） | `scpi_communication_biss_commands.h` |
| 71 | `COMMunication:BISS:STATus?` | 查询 | — | （待补） | `scpi_communication_biss_commands.h` |

## 通信（其它）（20）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `COMMunication:SERial:UART#:BAUD` | 设置 | <u32> | （待补） | `scpi_communication_uart_commands.h` |
| 2 | `COMMunication:SERial:UART#:BAUD?` | 查询 | — | （待补） | `scpi_communication_uart_commands.h` |
| 3 | `COMMunication:SERial:UART#:MODE` | 动作 | — | （待补） | `scpi_communication_uart_commands.h` |
| 4 | `COMMunication:SERial:UART#:MODE?` | 查询 | — | （待补） | `scpi_communication_uart_commands.h` |
| 5 | `COMMunication:SERial:UART#:FORMat` | 动作 | — | （待补） | `scpi_communication_uart_commands.h` |
| 6 | `COMMunication:SERial:UART#:FORMat?` | 查询 | — | （待补） | `scpi_communication_uart_commands.h` |
| 7 | `COMMunication:SERial:UART#:STATe` | 动作 | — | （待补） | `scpi_communication_uart_commands.h` |
| 8 | `COMMunication:SERial:UART#:STATe?` | 查询 | — | （待补） | `scpi_communication_uart_commands.h` |
| 9 | `COMMunication:SERial:UART#:STATus?` | 查询 | — | （待补） | `scpi_communication_uart_commands.h` |
| 10 | `COMMunication:SERial:UART#:TX:TEST` | 设置 | <u32>、<u32> | （待补） | `scpi_communication_uart_commands.h` |
| 11 | `COMMunication:SERial:UART#:TX:TEST?` | 查询 | — | （待补） | `scpi_communication_uart_commands.h` |
| 12 | `COMMunication:SERial:UART#:RX:COUNt?` | 查询 | — | （待补） | `scpi_communication_uart_commands.h` |
| 13 | `COMMunication:SERial:UART#:RX:STATus?` | 查询 | — | （待补） | `scpi_communication_uart_commands.h` |
| 14 | `COMMunication:SERial:UART#:ERRor?` | 查询 | — | （待补） | `scpi_communication_uart_commands.h` |
| 15 | `COMMunication:SERial:UART#:MODBus:ROLE` | 动作 | — | （待补） | `scpi_communication_uart_commands.h` |
| 16 | `COMMunication:SERial:UART#:MODBus:ROLE?` | 查询 | — | （待补） | `scpi_communication_uart_commands.h` |
| 17 | `COMMunication:SERial:UART#:MODBus:MASTER:READ` | 设置 | <u32>、<u32>、<u32> | （待补） | `scpi_communication_uart_commands.h` |
| 18 | `COMMunication:SERial:UART#:MODBus:MASTER:WRITE` | 设置 | <u32>、<u32>、<u32> | （待补） | `scpi_communication_uart_commands.h` |
| 19 | `COMMunication:SERial:UART#:MODBus:MASTER:STATus?` | 查询 | — | （待补） | `scpi_communication_uart_commands.h` |
| 20 | `COMMunication:SERial:UART#:MODBus:MASTER:DIAGnostic?` | 查询 | — | （待补） | `scpi_communication_uart_commands.h` |

## OTA / Boot（38）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `SYSTem:OTA:MODE` | 设置 | <u32> | （待补） | `scpi_ota_commands.h` |
| 2 | `SYSTem:OTA:INJect:COPY` | 动作 | — | （待补） | `scpi_ota_commands.h` |
| 3 | `SYSTem:OTA:INJect:CLEar` | 动作 | — | （待补） | `scpi_ota_commands.h` |
| 4 | `SYSTem:OTA:INJect:COPY?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 5 | `SYSTem:OTA:INJect:LOCKout` | 设置 | <u32> | （待补） | `scpi_ota_commands.h` |
| 6 | `SYSTem:OTA:INJect:LOCKout?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 7 | `SYSTem:OTA:INJect:MCORrupt` | 设置 | <u32> | （待补） | `scpi_ota_commands.h` |
| 8 | `SYSTem:OTA:INJect:MREPair` | 动作 | — | （待补） | `scpi_ota_commands.h` |
| 9 | `SYSTem:OTA:STATus?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 10 | `SYSTem:OTA:PROGress?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 11 | `SYSTem:OTA:BEGIN` | 设置 | <u32>、<u32> | （待补） | `scpi_ota_commands.h` |
| 12 | `SYSTem:OTA:PBEGIN` | 设置 | <u32>、<u32> | （待补） | `scpi_ota_commands.h` |
| 13 | `SYSTem:OTA:DATA` | 设置 | <块> | （待补） | `scpi_ota_commands.h` |
| 14 | `SYSTem:OTA:END` | 动作 | — | （待补） | `scpi_ota_commands.h` |
| 15 | `SYSTem:OTA:ABORt` | 动作 | — | （待补） | `scpi_ota_commands.h` |
| 16 | `SYSTem:OTA:BOOT` | 动作 | — | （待补） | `scpi_ota_commands.h` |
| 17 | `SYSTem:OTA:COMMit` | 动作 | — | （待补） | `scpi_ota_commands.h` |
| 18 | `SYSTem:OTA:SLOT?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 19 | `SYSTem:OTA:RESult?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 20 | `SYSTem:OTA:TXN?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 21 | `SYSTem:OTA:TRANsaction?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 22 | `SYSTem:OTA:JOURnal?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 23 | `SYSTem:OTA:MODE?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 24 | `SYSTem:OTA:TARGet?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 25 | `SYSTem:OTA:CAPability?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 26 | `SYSTem:OTA:BCB:HEALth?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 27 | `SYSTem:OTA:BCB:WEAR?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 28 | `SYSTem:OTA:STReam:OPEN` | 设置 | <块> | （待补） | `scpi_ota_commands.h` |
| 29 | `SYSTem:OTA:STReam:DATA` | 设置 | <u32>、<u32>、<块> | （待补） | `scpi_ota_commands.h` |
| 30 | `SYSTem:OTA:STReam:CLOSe` | 动作 | — | CLOSE only queues the bounded END transaction. Keep the OTA admission lease until FlashTransaction-owned verification, manifest append and pending-BCB | `scpi_ota_commands.h` |
| 31 | `SYSTem:OTA:STReam:ABORt` | 动作 | — | （待补） | `scpi_ota_commands.h` |
| 32 | `SYSTem:OTA:STReam:BOOT` | 动作 | — | （待补） | `scpi_ota_commands.h` |
| 33 | `SYSTem:OTA:STReam:STATus?` | 查询 | — | （待补） | `scpi_ota_commands.h` |
| 34 | `SYSTem:OTA:STReam:CAPability?` | 查询 | — | Report the accepted DATA payload, stream capability mask and the deployment map compiled into this firmware. FLASH:MAP? describes the catalog candidat | `scpi_ota_commands.h` |
| 35 | `SYSTem:BOOT:VERSion?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 36 | `SYSTem:BOOT:CAPability?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 37 | `SYSTem:BOOT:RESet` | 动作 | — | （待补） | `scpi_port.c` |
| 38 | `SYSTem:BOOT:BOOTSel` | 动作 | — | （待补） | `scpi_port.c` |

## 存储 / 日志（31）

| # | 指令 | 类型 | 参数 | 功能说明 | 回调 | 源模块 |
|---:|---|---|---|---|---|---|
| 1 | `SYSTem:SD:STATus?` | 查询 | — | （待补） | `scpi_storage_commands.h` |
| 2 | `SYSTem:SD:INFO?` | 查询 | — | （待补） | `scpi_storage_commands.h` |
| 3 | `SYSTem:SD:RAW:CLEar` | 设置 | <u32> | （待补） | `scpi_storage_commands.h` |
| 4 | `SYSTem:SD:RAW:READ?` | 查询 | <u32> | （待补） | `scpi_storage_commands.h` |
| 5 | `SYSTem:SD:MKFS` | 动作 | — | （待补） | `scpi_storage_commands.h` |
| 6 | `SYSTem:SD:INITialize` | 动作 | — | （待补） | `scpi_storage_commands.h` |
| 7 | `SYSTem:SD:MANifest?` | 查询 | — | （待补） | `scpi_storage_commands.h` |
| 8 | `SYSTem:STORage:JOB:INFO` | 动作 | — | （待补） | `scpi_storage_commands.h` |
| 9 | `SYSTem:STORage:JOB?` | 查询 | — | （待补） | `scpi_storage_commands.h` |
| 10 | `SYSTem:STORage:FILE:INFO?` | 查询 | — | （待补） | `scpi_storage_commands.h` |
| 11 | `SYSTem:STORage:FILE:READ?` | 查询 | <u32>、<u32> | （待补） | `scpi_storage_commands.h` |
| 12 | `SYSTem:STORage:FILE:DELete` | 动作 | — | （待补） | `scpi_storage_commands.h` |
| 13 | `SYSTem:STORage:FILE:REName` | 动作 | — | （待补） | `scpi_storage_commands.h` |
| 14 | `SYSTem:STORage:FILE:WRITe:BEGIN` | 设置 | <u32>、<u32> | （待补） | `scpi_storage_commands.h` |
| 15 | `SYSTem:STORage:FILE:WRITe:DATA` | 设置 | <u32>、<u32> | （待补） | `scpi_storage_commands.h` |
| 16 | `SYSTem:STORage:FILE:WRITe:END` | 设置 | <u32> | （待补） | `scpi_storage_commands.h` |
| 17 | `SYSTem:STORage:FILE:WRITe:ABORt` | 设置 | <u32> | （待补） | `scpi_storage_commands.h` |
| 18 | `SYSTem:STORage:FILE:WRITe:STATus?` | 查询 | — | （待补） | `scpi_storage_commands.h` |
| 19 | `SYSTem:STORage:DIRectory:CREate` | 动作 | — | （待补） | `scpi_storage_commands.h` |
| 20 | `SYSTem:STORage:DIRectory:DELete` | 动作 | — | （待补） | `scpi_storage_commands.h` |
| 21 | `SYSTem:STORage:DIRectory:REName` | 动作 | — | （待补） | `scpi_storage_commands.h` |
| 22 | `SYSTem:STORage:DIRectory:CATalog?` | 查询 | — | （待补） | `scpi_storage_commands.h` |
| 23 | `SYSTem:STORage:STATus?` | 查询 | — | （待补） | `scpi_storage_commands.h` |
| 24 | `MMEMory:CATalog:PAGE?` | 查询 | <u32>、<u32> | （待补） | `scpi_storage_commands.h` |
| 25 | `MMEMory:CATalog?` | 查询 | — | （待补） | `scpi_storage_commands.h` |
| 26 | `MMEMory:INFO?` | 查询 | — | （待补） | `scpi_storage_commands.h` |
| 27 | `MMEMory:READ?` | 查询 | <u32>、<u32> | （待补） | `scpi_storage_commands.h` |
| 28 | `SYSTem:LOG:PAGE?` | 查询 | — | （待补） | `scpi_system_diagnostics_commands.h` |
| 29 | `SYSTem:LOG:LEVel` | 设置 | <u32> | （待补） | `scpi_system_runtime_commands.h` |
| 30 | `SYSTem:LOG:LEVel?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
| 31 | `SYSTem:LOG:STATus?` | 查询 | — | （待补） | `scpi_system_runtime_commands.h` |
