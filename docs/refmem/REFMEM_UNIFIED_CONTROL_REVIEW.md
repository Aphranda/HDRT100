# REFMEM 统一控制面收口快照（范围闭环，整体未收口）

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_UNIFIED_CONTROL_REVIEW.md`
Related: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`
Last updated: 2026-10-29

## 1. 结论

**N6/N7 目标范围已闭环；统一控制面整体未收口，不可标记完成。**

- 已闭环：N6.0–N6.5、N7.1–N7.5——四板 TDMA/DPLL/T2/VDC 正向、单条 `CTRL:FUNCtion:RUN` 功能封装、host 用例均有证据。
- 未收口：
  - `REFMEM-CTRL-01` 仍 `pending`，C11 未由第三方签署（`REFMEM_HAOFV_REVIEW_01` §6 自述不构成最终签署）。
  - N5.5 已按 HAOFV 决策为**产品级入口收敛**：非协调者远端命令 fail-closed，属有界承诺，不是“任一切入点同语义”。
  - IF-012/IF-013 Trigger 统一命令仍在接口域推进（`docs/interface/SCPI_HOST_TODO.md` 记 `IN PROGRESS`）。
  - T2 `START/MOUNT` 仍 `NOT_ONLINE_SAFE`（需 running-safe 位置复位）。
  - 相位预算待 EXE-SEAT-01 签字，不得由实现单方重签。
- 保留边界：T2 域停配置（`UNMOUNT→CONFIGURE`）已通；在线启动不在本快照承诺内。
- **N8 夜间进展（2026-10-29）**：
  - 失败路径 NACK 可见性已修并上板（`4dc41dfe`）：被拒命令从 `POSTED` 变为 `NACKED+reason`；P3 QUICK 凭证 build `20261028171345`、1609 files、`1fc53c7d…`。
  - N5.7 只读聚合已上板并实机读回（`4f7a94f0`）：合法 41 后 `acked=1`、非法 41 后 `nacked=1/reason=13`；P3 QUICK 凭证 build `20261028180729`、1609 files、`2f667e55…`。
  - N8.6 四板 function HIL 复跑通过：`FUNC_TDMA_ONLINE`(2/2)、`FUNC_TDMA_CLOCK_ONLINE`(4/4)、`FUNC_VDC_RECONFIG`(4/4) 均 `COMPLETED`、`ack=14`；VDC timing 四板 `12000,16000,6000`。
  - N8.5（T2 running-safe）未实现，仍是 `START/MOUNT` 边界；设计包见 `out/n8/n8_5_running_safe_design.md`。

## 2. 子项总表

| 里程碑 | 状态 | 证据 / 说明 |
|---|---|---|
| N6.0 在线语义/命令/功能 schema | ✅ | `refmem_command.h` 20–34；`command_catalog.yaml` 当前 **36 行**（N6 的 20–34 + IF-012/013 的 35–49）；收口当时 21 行 |
| N6.1 类型+目录+入口 | ✅ | 生成器 + 静态表；`SYSTem:CTRL:EXECute` 协调者入口 |
| N6.2 DPLL RUN-safe | ✅ | `DPLL_ROLE`/`DPLL_TUNE` 在线；四板 `DPLL:ROLE? 0,0,3,3,0` |
| N6.3 TDMA 在线 | ✅ | seat 原子字 + load mask；20/22 返回 `NOT_ONLINE_SAFE`；四板 seat 读回 `3,1,...` |
| N6.3 VDC | ✅ | 四板 `FUNC_VDC_RECONFIG,14,14` `COMPLETED/ack=14`；timing 读回 `12000,16000,6000` |
| N6.4 T2 | ✅（域停配置） | 四板 `FUNC_T2_RECONFIG,14,14` `COMPLETED/ack=14`（cached STOP 快照）；START/MOUNT 仍边界 |
| N6.5 HIL/P3 | ✅（历史凭证） | P3 `20261026173427` / `20261026174849` / `20261026180937` / `20261026181952` / `20261027014444` |
| N7.1 function 表生成器 | ✅ | 6 个 function；host 查表/切片 |
| N7.2 执行器 | ✅ | 单 in-flight、串行 step、`ABORT_REMAINING`、`RUN_STATE_DENIED` 3×100 ms 重试 |
| N7.3 单命令 SCPI | ✅ | `SYSTem:CTRL:FUNCtion:RUN/STATus?/CATalog?`；coordinator-only |
| N7.4 host + 四板 | ✅ | `FUNC_TDMA_ONLINE` 2 步、`FUNC_TDMA_CLOCK_ONLINE` 4 步 `ack=14` |
| N7.5 四板 HIL/P3 | ✅（历史凭证） | 四板 TDMA/DPLL/T2/VDC + 连续性通过；当前树复跑见 N8.6 |
| N7.6 收口 | ⚠️ | 本文件已回正；契约未转正（pending/C11），不标完成 |

## 3. 证据索引

| 类别 | 路径 |
|---|---|
| 当前 P3 凭证 | `config/hardware_acceptance/p3_acceptance_receipt.json`（build `20261028154542`，QUICK/DPLL scope，1609 files） |
| N8 基线 | `out/n8/baseline.json`（四板在线、同 build、host 4 套件） |
| 历史 P3 凭证 | build `20261027014444`（N6.3.2/T2/VDC 四板正向） |
| 四板 TDMA/DPLL function | `out/n7/four_board_hil/function_hil_localslot.txt`、`continuity.txt` |
| 四板 T2 正向 | `out/n7/four_board_hil/t2_positive_cached.txt`、`trn03_cached_run1/` |
| 四板 VDC 正向 | `out/n7/four_board_hil/vdc_positive_four_board.txt`、`trn03_vdc4_1/` |
| IF-013 命令面负结果 | `out/n7/local_hil/if013_surface.txt`（7 条配置 `reason=13`，SWITCH ACK） |
| T2 本地正向 | `out/n7/local_hil/t2_positive_try.txt`；`out/n7/t2_workflow_full.log`（21/21） |
| 提交 | `871279c8`、`5e688260`、`970d231b`、`4c0be6fb`、`7afcbd47`、`a5e47834`、`dac41e0e`、`b16f7b0a`、`bafb8cdb` |

## 4. 开放项与残余风险

| 开放项 | 归属门 | 触发条件 | 影响面 | 复验判据 | 闭环里程碑 |
|---|---|---|---|---|---|
| `REFMEM-CTRL-01` pending / C11 未签 | ⑤ | 任何“统一面已完成”表述 | 契约面不可标完成 | 第三方独立复核签署 + 登记表转 active | N8.2（文本）/ 第三方 |
| 非协调者入口（N5.5） | ③ | 从非协调者板发起远端命令 | “任一切入点同语义”不成立 | 已冻结为产品级入口收敛；重开需 intent delta + TLV 证据 | N5.5 / N8.2 |
| IF-012/IF-013 Trigger 统一命令 | ⑥ | Trigger/角度经统一面下发 | 命令面可用、闭环未证 | 8 条配置 ACK + 角度 RUN + 四板 HIL | N8.3 |
| T2 `START/MOUNT` | ⑥ | function 内启动 T2 | 功能面只能停+配置 | running-safe 位置复位 + 四板 `ack=14` | N8.5 |
| 相位预算超支 | ② | 验收 DPLL 60 s 联合采集 | 全量验收可能再红 | EXE-SEAT-01 签字或临时分组计时，不得实现单方重签 | 非目标（用户） |
| function 重试上限 | ② | 域停窗口 >3×100 ms | 超时仍 FAILED | 按域实测调整上限/预算 | N7.2 续 |
| 四板 T2 逐板不可替代 | ③ | 非协调者本地 function 超时 `reason=11` | 不能拼凑四板证据 | 只认协调者单命令 `ack=14` | 已冻结 |
| 台架 OTA 不稳 | ⑥ | 单板/并发 OTA 中途 | 新 build 上板 | 已用 `tools/picotool_flash`（BOOTSEL UF2）绕行；OTA 工具修复另列 | 已缓解 |
| N8.5 running-safe 复位 | ⑤ | T2 `START/MOUNT` | 功能面只能停+配置 | 四板 `FUNC_T2_RECONFIG` 含 MOUNT 步 `ack=14` | N8.5 |

已闭环项：VDC reference/timing（根因是参数非法，不是 retire 缺陷）、四板 T2 正向（cached STOP 快照读只用于统一控制 T2 configure）、N5.7 实机读回（`acked/nacked` 随命令变化）、四板 `FUNC_TDMA_ONLINE`/`FUNC_TDMA_CLOCK_ONLINE`/`FUNC_VDC_RECONFIG` HIL（2026-10-29）。

## 5. 契约与边界

- 仍为单一 command slot、单一 `REFTMEM_CMD` 维护流、单一 ACK seat；无新增总线/payload class/第二命令槽。
- TDMA topology/period 运行时拒绝；seat/load 为在线原子配置。
- T2/VDC 配置只在其域停窗口；T2 `START/MOUNT` 不在在线范围。
- function 为静态表，无动态脚本；入口 coordinator-only。
- **入口无关边界（N5.5，2026-10-29）**：非协调者板只保留本地控制面（`REFMEM-LOCAL-01`/CP9），远端命令一律 fail-closed（12），不中继、不排队、不建 relay 状态；C1 的“Entry B → Coordinator A → Target C”腿为显式非目标。
- 四板逐板本地调用不能替代协调者单命令四板证据（已实测非协调者 `reason=11`）。

## 6. 下一步

1. N8.5：T2 running-safe 位置复位——按 `out/n8/n8_5_running_safe_design.md` 落 quiesce+reset，并接线 `t2_mount_handler`。
2. N8.6 已完成（TDMA/CLOCK/VDC 四板 `ack=14`）；T2 四板沿用 10-27 证据，待 N8.5 后补含 MOUNT 步的复跑。
3. 全部完成后由 N8.7 重新评估本快照结论；在那之前本文件不代表"统一面完成"。
