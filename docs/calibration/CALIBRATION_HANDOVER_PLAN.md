# 校准交接（2026-11-07 夜 → 下一会话）

Status: Active
Domain: CALIBRATION / TDMA / REFMEM
Canonical: `docs/calibration/CALIBRATION_HANDOVER_PLAN.md`
Related: `docs/calibration/CALIBRATION_CTRQS_CHECKLIST.md`, `docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md`, `docs/calibration/CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-11-07

本文是**给下一会话的第一份读物**：现状、证据、未完成项、坑与起步动作。
原则同项目既有约定：**未验证、延后、pending 一律不写作完成**。

## 1. 下一会话的章程（用户指定）

**严格单口启环：只用协调口（COM5 / NO.1），不得其它 COM 参与。**
当前实现**不满足**：启环是四口逐板下发（见 §3.1），单口只覆盖 GUIDE 的 `CONFig`/`TRIGger`。

## 2. 已达成（有可复跑证据）

| 能力 | 证据锚点 |
|---|---|
| 10 kHz 引导环**零口**自组（同一 P3 镜像，无任何起环 SCPI） | 四板 provisioning 后冷启动：`BOOT:PLAN?` `START_REF/LISTEN`、`fail_step=0`、`boot_done=1`；`RING:STATus?` `enabled=1,node_count=4,up=1,down=1,adapter=1`；探针 `boot` PASS 归档 `out/calibration/guide-10k/evidence/20261107-234127-boot-night1/`、`...-boot-night2/` |
| 10 kHz 引导环启环（**四口**，旧路径）可重复 | 探针 `up` 后四板 `enabled=1, nodes=4, up=1, down=1, adapter=1` |
| GUIDE `CONFig`/`TRIGger` 单口下发 | ✅ 已修复并复验 | boot 加 `SET_FLIGHT_MODE`；最终镜像只开 COM5 发 CONFig 58.8 s 四板 `gen=950`，证据 `out/calibration/guide-10k/evidence/20261108-013432-ct-final3/` |
| 七动作阶梯 P1→T3 四板推进 | 多轮 `ladder_idx=6`、`ladder_step=2(DONE)` |
| T3 四板 COMPLETE + 载波可读 `10 MHz / 1 ms` | `RING:STATus?` 尾字段 `baud_hz=10000000, cycle_period_ns=1000000` |
| S 段语义冻结 | 契约 `CALIBRATION-SGATE-01`（`CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md` §12） |
| 探针归型入库 | `tools/calibration/guide_cmdplane_probe.py`，`TOOL-CALIBRATION-GUIDE`（STANDALONE） |

## 3. 未达成（下一会话的工作清单）

### 3.1 启环单口化（首要）——路线已定，证据待补

**代码事实（2026-11-07 复核，勿再按旧假设动手）**：

- REFMEM 远程命令面**不能自举环**：远程提交要求 ring transport 已就绪
  （`components/distributed_refmem/src/refmem_control_submit.c`），命令目录没有
  OPMode/BRINGUP/ARM/START 条目，`TDMA_RING_TOPOLOGY` 的远程 handler 也恒拒。环停时"只开 COM5 发命令
  起环"不可能，不是补几行目录能解决的。
- 可行路线是**开机重建 + 运行时 provisioning**：v2 地图的 boot profile store（CALIBRATION_NVS 双扇区）
  + `SYSTem:TDMA:BOOT:PROFile`，上电后四板无任何主机命令自组 10 kHz 引导环，之后所有动作与读回只走
  COM5。P3 验收镜像与台架镜像是**同一构建**；契约 `TDMA-BOOTPROFILE-01`。
- 台架编译开关 `PROJECT_TDMA_GUIDE_AUTOFORM` 只作历史证据；不得再为台架另立镜像
  （用户 2026-11-07 裁决：P3 镜像即最新镜像，不接受第二份镜像）。
- 单口验收的启动序列：`BOOT:PLAN? boot_done=1`（或 ≥33 s）→ 四板 provisioning → 同步重启 →
  四板 `BOOT:PLAN?`/`RING:STATus?` 零口自组判据 → 只用 COM5 下发 C/T。

**状态**：已达成（build `20261107162842`）：P3 PASS（`out/HardwareAcceptance/20261108/p3-final2`）、
四板 provisioning 后零口自组 PASS（`.../20261108-013330-boot-final3/`）、只开 COM5 的 C/T PASS
（`.../20261108-013432-ct-final3/`）。契约 `TDMA-BOOTPROFILE-01` 待 C11 复审后从 pending 转 active。

### 3.2 帧活性归属（R39）

`total_up_tx/total_down_rx` 已改为 adapter 单调成功总量（R40 已修，P3 `p3-215622`），
但仍缺"**同一 ARM 会话内** ≥2 样本、Δ>0、且落在 T3 窗口内"的配对证据。

### 3.3 S 段首次写入实证（R26 / M6.2b）

实板只观测到 `2/1/0`（SKIP：该代已有记录），`0/1/1`（ALLOW 且写成功）从未出现。
需要造一个**新代** `calibration_generation` 的 stage，再跑一次并读
`READ:CALibration:TRAINing:STAGe:PERSistence?`（`record_generation` / `record_sequence`；首写序列为 1）。

### 3.4 全 10 MHz 七动作回归（M6.8）

当前默认阶梯是**混合载波**：P1/P2 在 10 kHz 引导窗口，P2_STOP apply level 7 之后 P3 起才是 10 MHz。
要全 10 MHz，必须**先 `OPMode:STAGe 7` 再下 P1 的 CONFig**（P1 的 slot 用 staged period 计算）。

### 3.5 已裁决项

**R25**：非 T3 动作的恢复环也会以 10 MHz 起环。用户裁决为"**不改行为，改判据口径**"——
即 M6.6 的表述是"T3 的**准入**要求测量档"，不宣称"只有 T3 能起 10 MHz"。

## 4. 资产与入口

| 资产 | 路径 |
|---|---|
| 探针（STANDALONE） | `tools/calibration/guide_cmdplane_probe.py`（`stage [level] / up / down / ga / verify / plan / ladder / status`） |
| S 段契约 | `CALIBRATION-SGATE-01` → `CALIBRATION_TRAINING_SUBDOMAIN_PLAN.md` §12 |
| 台账（残余表） | `CALIBRATION_CTRQS_CHECKLIST.md`（R25/R26/R32/R33/R34/R37/R39/R40/N6/N9/N10/N12） |
| 归档 | `out/calibration/guide-10k/evidence/<stamp>-<tag>/readback.json`（含 build/IDN/`RING:CONFig?`/fingerprint/窗口样本） |
| 交接长文 | `CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md` §10 |

严格判定入口：`guide_cmdplane_probe.py verify <action> <epoch> <gen> <repeats> <archive>`。
**不要**用 `verdict()` 的 GREEN（P1 专用，T3 必然打印 NOT-GREEN）。

## 5. 六类误判坑

1. **`RING:STATus?` 是 append-only 页**（已 47→49→51）。每次扩展必须在**同一提交**同步
   `tools/tdma_ring_monitor/tdma_field_parse.py` 的 `RUNTIME_FIELDS` 与两个严格测试
   （`test_tdma_board_record.py`、`test_trn03_closed_loop.py`），否则 P3 的 P0T 直接 BLOCKED。
2. **host 凭证只覆盖"被 stage 过的 tests"**；改了测试但没 stage，凭证不覆盖它而 `check-staged` 仍绿。
3. **证据必须落盘**：本轮曾把控制台数字写进台账，被审核判为不可复现并撤回。先归档再引用。
4. **环全局序号 ≠ 本板帧计数**：`up_tx_sequence`/`down_rx_sequence` 属 origin；
   本板帧活动看 `total_tx_ok/total_rx_ok`（R40 已修）。
5. **台架退化**：`ring not healthy` 或 `GUIDE:CONFig -200` 时先 `down→stage→up`；
   仍不行则整组 `SYST:BOOT:RESET` + 一次 P3 验收重建。不要连续盲试。
6. **§2.10**：归档未提交改动需**事先取得用户同意**；恢复文件不得用 `git checkout --` 形式。
   （本轮违反过一次：`refs/wip/r40-halfbaked` 未事先征得同意，内容保留未删。）

## 6. 起步检查清单（第 1 轮）

1. 读本文件 + 台账残余表 + playbook §10。
2. 四板同 build：`*IDN?` 与 `SYSTem:FW:BUILD?` 一致（build 必须来自 P3 那次构建）。
3. 单口启环走 §3.1 的路线：先 `boot` 判据确认零口自组，再只开 COM5。**不要**再逐板发
   STAGE/TOPO/BRINGUP/ARM/START，也不要为台架另刷镜像。
4. provisioning 是一次性、逐板的（`SYST:TDMA:BOOT:PROFile 23,4,<slot>,1,<crc>`，STOP-only）；
   写完必须重启，`BOOT:PLAN?` 在重启前不会变。
5. 每步结论只用归档读数；单口失败即按 §5-5 恢复台架，不要用第二个端口"绕过去"。
