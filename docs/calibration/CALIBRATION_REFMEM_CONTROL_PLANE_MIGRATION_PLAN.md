# REFMEM 统一控制面迁移清单（单口化）

Status: Active
Domain: CALIBRATION / REFMEM / TDMA / OTA
Canonical: `docs/calibration/CALIBRATION_REFMEM_CONTROL_PLANE_MIGRATION_PLAN.md`
Related: `docs/calibration/CALIBRATION_SINGLE_PORT_BRINGUP_COMMANDS.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/ota/OTA_TODO.md`, `docs/calibration/CALIBRATION_HANDOVER_PLAN.md`
Last updated: 2026-11-09

本文件把"哪些流程还必须四口、哪些已经能走 REFMEM、缺什么"整理成可勾选的清单，供后续把日常运行
收敛到"只接协调口 COM5"。

## 0. 口径

- **单口**：*启环与命令下发*只用 COM5。零口自组已达成（产品默认档，见 §4）。
- **四口**：逐板本地 SCPI（每板自己的 USB CDC），只在"必须触达板内状态"时使用。
- "入控制面"= 有 REFMEM 命令 id + 节点 handler + 可从 `SYST:REFMEM:CMD:SUBMit` 下发；不是"SCPI 存在"。

## 1. 流程现状总表

| 流程 | 现在用几个口 | 能否入 REFMEM | 现状 |
|---|---|---|---|
| 烧录/整片擦除/恢复 | 逐板（BOOTSEL） | 不能（物理） | 维持 |
| 写板号 `SYSTem:BOARD:NO` | 逐板 | 不能（身份） | 维持；默认档依赖它 |
| 持久 boot profile 写/清 | 逐板（STOP-only） | **能** | 待新命令；默认档已让写入变成可选 |
| 零口自组读回（`BOOT:PLAN?`/`RING:STATus?`） | 四口 | **能** | 缺聚合回读 |
| 单口 C/T 的验证段 | 命令 COM5、读回四口 | **能** | 同上 |
| CTRQS 阶梯（C/T/R/Q/S） | 命令 COM5、读回四口 | **能** | 同上 |
| 10 MHz 建环（STOP/STAGE/TOPO/BRINGUP/TRAIN/ARM/START） | 四口 | **部分能** | 目录已有 TDMA/ARM/STOP 类型；工具仍逐板 |
| P3 验收 | 四口 | 命令能、OTA 不能 | 依赖本地 OTA 先可用 |
| OTA 分发 | 逐板 USB 流 | **在途**（M5 流式 OTA） | 见 §5 |
| TRN-03/标定工具组 | 四口 | 部分能 | 触发可入环，原始向量回传缺通道 |
| 诊断/预检 | 四口 | 能 | 同聚合回读 |

## 2. 已入控制面

命令目录共 **38** 条（`components/distributed_refmem/src/refmem_command_catalog_table.c`，自动生成；
条目数以 `config/refmem/command_catalog.yaml` 与 `refmem_command_catalog_count` 为准，本文数字为快照）。
其中 DPLL/VDC/T2 共 **11** 条：

| id | 类型 | SCPI |
|---|---|---|
| 24 | DPLL_ROLE | `SYSTem:SYNC:VDC:DPLL:ROLE` |
| 25 | DPLL_TUNE | `SYSTem:SYNC:VDC:DPLL:TUNE` |
| 26 | DPLL_OVERRIDE | `SYSTem:SYNC:VDC:DPLL:OVERRide` |
| 27 | VDC_REFERENCE_CONFIG | `SYSTem:VDC:REFerence:CONFigure` |
| 28 | VDC_OUTPUT_DELAY | `SYSTem:VDC:OUTPut:DELay` |
| 29 | VDC_OUTPUT_TIMING | `SYSTem:VDC:OUTPut:TIMing` |
| 30 | T2_CONFIGURE | `TRIGger:REServation:T2:CONFigure` |
| 31 | T2_MOUNT | `TRIGger:REServation:T2:MOUNT` |
| 32 | T2_UNMOUNT | `TRIGger:REServation:T2:UNMOUNT` |
| 33 | VDC_STOP | `SYSTem:VDC:OUTPut:RUN:STOP` |
| 34 | VDC_START | `SYSTem:VDC:OUTPut:RUN` |

节点 handler 注册在 `application/src/app_refmem_domain_config.c:868-935`（含 TDMA/Trigger/CAL_GUIDE）。

## 3. 未入控制面（扫描结果）

扫描口径：`middleware/scpi_port/inc/*.h` 中 `VDC|DPLL|T2|REServation` 的 SCPI 表项。

| 量 | 数量 |
|---|---|
| 相关 SCPI 条目 | **218** |
| 查询（`?`） | **104** |
| 设置类（setter/动作） | **114** |
| 目录中的 DPLL/VDC/T2 条目 | 11 |
| 未入环的 setter | **DPLL 9 + VDC 64 + T2 4 = 77** |
| 未入环的查询 | **104（全部）** |

**DPLL（9）**：`DEFAult`、`PROVisional`、`STORe`、`ROLE:STORe`、`TRACe:ARM`、`TRACe:STOP`、
`TRACe:SAVE`、`COEFficient`（与 `TUNE` 共用 handler）。

**VDC（64，按组）**：参考/纪律的存储与默认（约 9）、输出延迟/时序的 `DEFAult/RECall/STORe`（6）、
`OUTPut:FIXed`（2）、`OUTPut:SUBDivision` 及 `AFTER/WINdow`（约 8）、`PRIORity:FOLLow/MATCH/SYNC/
TX:GAP`（约 8）、`PRIORity:TRACe:*`（约 10）、`FEEDback:*`（5）、`SYNC:VDC:OBServer*`（约 7）。

**T2（4）**：`ANNOUNCE`、`DRAIN`、`SEQ:MODE`、`SEQ:PILot`（`ACTion:EXECute:*`/`INSTall` 属 Trigger
action 家族，另有约 26 条 Trigger setter 未入环）。

**查询（104）**：`DPLL:ROLE?/STATus?/FILTer?`、`VDC:STATus?/REFerence:STATus?/OUTPut:*?`、
`T2?/ACTion?`、`SYNC:VDC:*?` 等，全部是板内本地读回，没有聚合通道。

## 4. 产品默认档（已实现，待 P3 后提交）

- 无持久档时按**产品默认档**自组：level/nodes 取构建常量、slot=板号-1、`BRINGUP_NO_CAL`、固定标签
  `DHRT100-GUIDE-4N`、generation=1；不写 flash；持久档优先；板号为 0 时不成立。
- `BOOT:PLAN?` 追加第 20 字段 `profile_source`（1=持久档、2=默认档）；`BOOT:PROFile?` 仍只反映持久档。
- 实板验证（build `20261108084736`，四板持久档清空）：四板 `source=2, fail_step=0, boot_done=1,
  ring=1/1/1/1`。
- **阻塞**：P3 的本地 OTA 过不去（§5），固件改动暂未提交。

## 5. OTA：M5 流式 OTA（在途）

`docs/ota/OTA_TODO.md:80-102` 已立项：**单一上位机串口**下，协调板读 package，经
`TDMA_PAYLOAD_CLASS_OTA_BULK`/`TDMA_TRAFFIC_RELIABLE_BULK` 把镜像流式装进从板 inactive slot。
落地后 OTA 与其他流程共用同一条"命令走 REFMEM、镜像走 bulk"的统一控制面；在此之前 OTA 仍是逐板 USB 流。

**本轮台架复现（供 OTA owner 使用）**：

```
# 失败形态（四板中 COM3/4/6，稳定复现；COM5 通过）
stream failed while waiting for durable 512: '0,6,512,5002,5'
SYST:OTA:FT? = 10,2,17,17,0,256,0,1    # policy_gate_reason=17 = FLASH_TRANSACTION_ERROR_MODE
```

已排除：
1. 不是默认档/环占用——把板号置 0（完全不启环）后 COM3 仍失败；
2. 不是 RAM 锁存残留——掉电/复位后 `OTA:FT?` 归零，首块 512 B 仍失败；
3. 不是台架脏状态——整片擦除（`--flash-size 0x1000000`）+ 重刷后仍失败。

与 `OTA-TODO` 的 **OTA-HAOFV-012**（无进展超时/自动清理 session 与 admission）和
**OTA-HAOFV-013**（`SYST:OTA:DATA` 拒绝路径未消费数据块）症状吻合：首块写入时 OTA admission 已不活跃，
FlashTransaction 进入 ERROR_MODE。复现命令：`ota_multi_update.py <pkg> --expected-board-count 1
--serial-number <sn>`，证据目录 `out/flash/ota-no3-*`。

## 6. 迁移顺序与判据

**D0 决策（2026-11-09）：选 A（本地镜像/推送）。** 读模型不做同步跨板读：CP4 的
“不提供跨板阻塞查询”保持有效，不新增 payload class/响应 wire/第二套 handler。所有读回走
“唯一 writer 在 core0 发布 → 各板本地镜像 → `SYST:REFMEM:OBS? <key>` 聚合读”，与命令面的
ACK/NACK 语义分离。若后续确需 B（同步 `CTRL:READ?`），先单独改 CP4 再立项。

1. **聚合回读**（收益最大）：新增一个 `REFMEM:STATus?` 类查询，把 `BOOT:PLAN?/RING:STATus?/PLAN?/
   CMD:STATus?/VDC/DPLL/T2 状态` 按节点带回协调口。判据：单开 COM5 能拿到四板同构向量。
2. **provisioning 命令**：环内暂存 → 协调 STOP → 各板本地提交 → 重启回读。判据：四板
   `profile_source=1` 且重启后自组。
3. **建环工具 in-band**：`tdma_start_ring.py` 的 STOP/TOPO/BRINGUP/ARM/START 改走
   `REFMEM:CMD:SUBMit`。判据：10 MHz 环单口形成（TRAIN 仍按 `adapter_started` 前置单独判）。
4. **OTA**：等 M5 流式 OTA；判据是 M5 自己的里程碑判据。

## 7. 边界

- 烧录、板号、整片擦除永远是逐板操作，不进控制面。
- `SYNC:VDC:OBServer*`（NO.5 外部观察板）不参与 NO.1–4 环，是否入环需单独定界。
- 存储/默认/召回类 setter 建议合并成一条"NVS 命名空间写"命令，而不是每 SCPI 一条命令。

## 8. 参考

- 单口运行手册：`docs/calibration/CALIBRATION_SINGLE_PORT_BRINGUP_COMMANDS.md`
- 契约：`docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`（`TDMA-BOOTPROFILE-01`）
- OTA 里程碑：`docs/ota/OTA_TODO.md`

## 9. 2026-11-09 夜间进展与残余（快照）

| 项 | 状态 | 证据 |
|---|---|---|
| 读契约 `REFMEM-READ-01` | **pending（8/8 key 本地已实装）**：wire/分页/stale/错误码 + `switch1/2`、`trigger_state`、`guide_plan`、`boot_plan`、`ring_status`、`cal_status`、`node_health` 在实板可用；**发布面 R1-3** 未做（A′/B′ 待 C11/TDMA）；逐槽行 M4 已做 | `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md:1440-1516`、`DOCS_REGISTRY.md` |
| read catalog 生成器 + host 用例 | 已入库（含 drift 检查与逐 key schema 断言） | `2d628593`、`3b9d3250`、`tools/refmem/refmem_read_catalog_gen.py` |
| `SYST:REFMEM:OBS?` / provider | **已实装（8/8 key）**：`switch1/2`、`trigger_state`、`guide_plan`、`boot_plan`、`ring_status`、`cal_status`、`node_health` 从本地快照取值；对端槽 stale | `836dae7a`、`c6f2c76e`、`3624f322`、`6c28df1c`、`tools/refmem/control_plane_smoke.py` |
| M3 控制面（read/write/wait_terminal + 模拟器） | 首批已入库 + `read()` 实板 smoke 通过 | `7db8b04c`、`5c031aa8` |
| 阶梯 P1@7（level 7） | 四板 PASS（严格 verdict） | `evidence/20261109-055753-verify-1`（build `20261108214336`） |
| 阶梯 P2 | 四板 COMPLETE | `docs/calibration/CALIBRATION_TASK_PROGRESS.md` CAL-TASK-20261109-010 |
| 阶梯 P3 | **间歇**：既有 2/4（`20261109-062107-verify-3`），也有多轮四板 12/12（`064400-action3-gen11103`、`112336-action3-gen12303`、`114612-action3-gen12403`）；机制见 `CAL-TASK-20261109-013`（R 段起点不共享） | `CAL-TASK-20261109-011/013` |
| 阶梯 T0–T3 | **11-07 已达成过**（`20261107-211534-action7-gen2410`：四板 `ladder=6/2(DONE)`、T3 后 `baud=10000000`）；11-09 夜间未重跑 | `CALIBRATION_HANDOVER_PLAN.md` §2、该证据 |
| T0–T3 | **未跑** | — |

**关键口径（勿再犯）**：

- C 段首字段是 `candidate_count`；`ga` 必须显式传 count（`ga 1 … 4 3 0 4 16 1 64`）。
- `PLAN?` 的 progress/masks/ladder 只在 Q 刷新；归档与结案必须等 `ctrqs∈{0,28,29}`。
- `up/down=0` 在 TRIAL/FORWARD 窗口内正常；`RES:TRAINing?=(0,1)` 是从机 hold 的正常标志。
- P3 位图每板 12 位里 6 位是 no-op pass；`verify` 必须四板一起。
- 手写 CONFig 时第二个字段必须是 **`0x40000000 + gen`**（不是任意常量），trigger 的 epoch 必须非 0 且每轮递增；
  否则 PLAN? 显示 `epoch=0`，本轮根本没触发（2026-11-09 实测踩过一次）。

