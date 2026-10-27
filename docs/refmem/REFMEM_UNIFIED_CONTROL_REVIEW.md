# REFMEM 统一控制面收口快照（未完成）

Status: Draft
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_UNIFIED_CONTROL_REVIEW.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`
Last updated: 2026-10-27

## 1. 结论

**目标可标记完成。** N6.0–N6.5、N7.1–N7.6 的目标范围已闭环：TDMA 在线、DPLL、T2 四板正向、VDC 四板正向、function 层与四板连续性均通过。保留边界：`T2_MOUNT/START` 仍 `NOT_ONLINE_SAFE`（需 running-safe 位置复位），属于已声明首版边界。

## 2. 子项总表

| 里程碑 | 状态 | 证据 / 说明 |
|---|---|---|
| N6.0 在线语义/命令/功能 schema | ✅ | `refmem_command.h` 20–34；`command_catalog.yaml` 21 行；`function_catalog.yaml` 6 个 |
| N6.1 类型+目录+入口 | ✅ | 生成器 + 静态表；`SYSTem:CTRL:EXECute` 协调者入口 |
| N6.2 DPLL RUN-safe | ✅ | `DPLL_ROLE`/`DPLL_TUNE` 在线；四板 `DPLL:ROLE? 0,0,3,3,0` |
| N6.3 TDMA 在线 | ✅ | seat 原子字 + load mask；20/22 返回 `NOT_ONLINE_SAFE`；四板 seat 读回 `3,1,...` |
| N6.3 VDC | ✅ | 四板 `FUNC_VDC_RECONFIG,14,14` `COMPLETED/ack=14`；delay/reference/timing 读回正确（input_port 4、window 1000、timeout 2500） |
| N6.4 T2 | ✅（四板正向） | 四板 `FUNC_T2_RECONFIG,14,14` `COMPLETED/ack=14`；T2 读 STOP 暂存快照；START/MOUNT `NOT_ONLINE_SAFE` |
| N6.5 HIL/P3/文档 | ✅ | P3 `20261026173427` / `20261026174849` / `20261026180937` / `20261026181952` |
| N7.1 function 表生成器 | ✅ | 6 个 function；host 查表/切片 |
| N7.2 执行器 | ✅ | 单 in-flight、串行 step、`ABORT_REMAINING`、`RUN_STATE_DENIED` 3×100 ms 重试 |
| N7.3 单命令 SCPI | ✅ | `RUN/STATus?/CATalog?`；coordinator-only |
| N7.4 host + 四板 | ✅ | `FUNC_TDMA_ONLINE` 2 步、`FUNC_TDMA_CLOCK_ONLINE` 4 步 `ack=14`；T2、VDC 四板 `ack=14` |
| N7.5 四板 HIL/P3 | ✅ | 四板 TDMA/DPLL/T2/VDC + 连续性通过；P3 凭证齐全 |
| N7.6 收口 | ✅ | 本文件；边界见 §4 |

## 3. 证据索引

| 类别 | 路径 |
|---|---|
| P3 凭证 | `config/hardware_acceptance/p3_acceptance_receipt.json`（最近 build `20261027014444`） |
| 四板 TDMA/DPLL function | `out/n7/four_board_hil/function_hil_localslot.txt`、`continuity.txt` |
| 四板 T2 正向 | `out/n7/four_board_hil/t2_positive_cached.txt`、`trn03_cached_run1/` |
| 四板 VDC 正向 | `out/n7/four_board_hil/vdc_positive_four_board.txt`、`trn03_vdc4_1/` |
| 四板 T2 负向 | `out/n7/four_board_hil/function_hil.txt`（step0 ACK、step1 `reason=13`） |
| T2 本地正向 | `out/n7/local_hil/t2_positive_try.txt`；`out/n7/t2_workflow_full.log`（21/21） |
| VDC delay 窄功能 | `out/n7/local_hil/vdc_delay_function.txt` |
| VDC reference/timing 负结果 | `out/n7/local_hil/vdc_function.txt`、`vdc_retry.txt` |
| 四板 T2 逐板负结果 | `out/n7/four_board_hil/t2_positive_per_board.txt` |
| 提交 | `871279c8`、`5e688260`、`970d231b`、`4c0be6fb`、`7afcbd47` 等 |

## 4. 开放项与残余风险

| 开放项 | 归属门 | 触发条件 | 影响面 | 复验判据 | 闭环里程碑 |
|---|---|---|---|---|---|
| VDC reference/timing | ⑥ | ✅ 已闭环：此前 `reason=7` 是参数非法（`input_port=0`、`timeout<=window`），不是 reference 不退休 | 四板 `FUNC_VDC_RECONFIG,14,14` `COMPLETED/ack=14`；timing 读回 `12000,16000,6000` | 保持参数范围校验；本地/四板均通过 | N6.3.3 |
| 四板 T2 正向 | ⑥ | ✅ 已闭环：`RING:ARM` 后 STOP-only 快照返回 NOT_STOPPED；T2 handler 改用 cached STOP 暂存副本 | 四板 `FUNC_T2_RECONFIG,14,14` `COMPLETED/ack=14`，TDMA 连续 | 保持 cached 读只用于 T2 configure；本地 SCPI 仍 STOP-only | N7.5 |
| T2 START/MOUNT | ⑥ | `distributed_refmem_reset_position_stopped()` 需 TDMA 停 | function 不能启动 T2 | 新增 running-safe 位置复位或明确本地边界 | N6.4 |
| 四板 T2 逐板不可替代 | ③ | 非协调者本地 function 超时 `reason=11` | 不能拼凑四板证据 | 只认协调者单命令 `ack=14` | N7.5 |
| function 重试上限 | ② | 域停窗口 >3×100 ms | 超时仍 FAILED | 按域实测调整上限/预算 | N7.2 |

## 5. 契约与边界

- 仍为单一 command slot、单一 `REFTMEM_CMD` 维护流、单一 ACK seat；无新增总线/payload class。
- TDMA topology/period 运行时拒绝；seat/load 为在线原子配置。
- T2/VDC 配置只在其域停窗口；T2 START/MOUNT 不在在线范围。
- function 为静态表，无动态脚本；入口 coordinator-only。
- 四板逐板本地调用不能替代协调者单命令四板证据（已实测非协调者 `reason=11`）。

## 6. 下一步

1. 目标范围内已闭环，可标记完成。
2. 维护边界：T2 cached 快照读只用于统一控制 T2 configure；本地 SCPI 仍 STOP-only；function 仍 coordinator-only。
3. 后续如需要 T2 在线启动，再补 running-safe 位置复位（`T2_MOUNT/START` 当前 `NOT_ONLINE_SAFE`）。
