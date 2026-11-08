# 单口起环操作指南（10 kHz 引导环，唯一镜像）

Status: Active
Domain: CALIBRATION
Canonical: `docs/calibration/CALIBRATION_SINGLE_PORT_BRINGUP_COMMANDS.md`
Related: `docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md`, `docs/calibration/CALIBRATION_HANDOVER_PLAN.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-11-08

本指南给**其它会话**直接照做：如何在这套四板台架上用"唯一 P3 镜像"完成严格单口启环与单口 C/T，
以及哪些现象不是硬件问题、哪些流程会与 boot profile 冲突。判据全部来自实板归档，不含推测。

## 0. 适用与前提

- **唯一镜像**：P3 验收镜像（`--preset pico2-v2-factory-candidate`，v2 地图）。不允许台架专用镜像或
  编译期开关；契约 `TDMA-BOOTPROFILE-01`（active）。
- **当前验证 build**：`20261107175157`（四板一致）。镜像不同则先确认 `SYST:FW:BUILD?` 四板相同。
- 四板板号 NO.1..4，环线 NO.1→2→3→4；协调口固定 **COM5 / NO.1**。
- **单口的定义**：*启环与命令下发*只用 COM5。**provisioning 是一次性逐板配置（四口）**，不属于启环流程；
  四口读回只用于验证，不用于下发。

## 1. 一次性 provisioning（每板一次，四口）

```powershell
# 1) 先停环：每板 SYSTem:TDMA:RING:STOP，并确认 RING:STATus? 首字段=0
# 2) 逐板写入（slot = NO-1；最后一个参数是拓扑标签 0x642F84A7 的十进制）
SYST:TDMA:BOOT:PROFile 23,4,<slot>,1,1680835751
#    期望回 "BOOTPROF",<gen>,<seq>,<crc>
# 3) 逐板回读
SYST:TDMA:BOOT:PROFile?
#    期望 "BOOTPROF",1,1,0,<gen>,1,<crc>,23,4,<slot>,1,1680835751,...
```

- 写入是 **STOP-only**（环快照读不出也拒绝）；`flags` 必须含 `BRINGUP_NO_CAL`（值 1）。
- **重启才生效**：`BOOT:PLAN?` 在重启前仍是旧计划。
- 反 provisioning：`SYSTem:TDMA:BOOT:PROFile:CLEar`（同样 STOP-only；两扇区物理全 0xFF 才回
  `"BOOTPROF","CLEARED"`，否则报 `..._VERIFY`）。

## 2. 启环（零口，不需要任何起环 SCPI）

1. 四板复位：**从板先、协调板最后**（NO.4→NO.3→NO.2→NO.1），`SYSTem:BOOT:RESET` 或重新上电。
2. 等待：boot 服务在 3 s 后启动、最长 30 s；**轮询 `BOOT:PLAN?` 直到第 18 字段 `boot_done=1`
   且第 4 字段 `fail_step=0`**（或固定等 ≥33 s）。
3. 四板判据：

| 查询 | 判据 |
|---|---|
| `BOOT:PLAN?`（19 字段） | `action`：slot0=1(START_REF)，其余=2(LISTEN)；`steps`=8；`fail_step`=0；`have_profile`=1；`boot_done`=1；`boot_next_step`=8；`half_ring`=0 |
| `RING:STATus?` | `enabled`=1、`nodes`=4、`up_running`=1、`down_running`=1、`adapter_started`=1 |
| 帧活性 | 间隔 ≥2 s 两次读 `total_up_tx`/`total_down_rx`（索引 49/50）单调增长 |

4. 归档（只读，不发任何起环命令）：

```powershell
python tools/calibration/guide_cmdplane_probe.py boot <tag>
# 证据：out/calibration/guide-10k/evidence/<stamp>-boot-<tag>/readback.json
```

## 3. 单口 C/T（只开 COM5）

```powershell
# 只开 COM5 发 CONFig，随后四板只读确认
python tools/calibration/guide_cmdplane_probe.py ct <gen> <tag>
# 证据：out/calibration/guide-10k/evidence/<stamp>-ct-<tag>/readback.json
#      含 command_port=COM5、response、host fingerprint、四板 PLAN?/RING?/CMD?
```

- 期望：`ct=PASS`，四板 `PLAN?` 的 `gen` 在 ~60 s 内一致（10 kHz，16 字节计划需分片）。
- **不要**用第二个端口发 `GUIDE:CONFig`/`TRIGger`；四口只用于读回。
- 判据：`PLAN?` 首字段 `valid=1`、`action` 正确、`gen` 与 `epoch` 四板一致；`CMD:STATus?` 的
  `frag_done` 增长、发起板 `ack_ok` 增长。

## 4. 与其它流程的交互（先读再跑）

1. **CTRQS 阶梯 / Q 段恢复（CONSOLE-042，已知冲突）**：boot plan 存在时
   `app_ctrqs_boot_plan_present()` 为真，`APP_CTRQS_RESTORE_WAIT` 的 REBUILD 回退**不可达**
   （`application/src/app.c:3783`）。若窗口关闭后的 boot replay 未成环，只能等满 45 s 并报
   `NOT_ONLINE_SAFE`（Q 段 `progress_status=3 / reason=14`，即使 `passed_mask=0xF`）。
   **跑 CTRQS 阶梯前**：先 `BOOT:PROFile:CLEar` + 重启（回到无档，恢复旧的 REBUILD 回退），
   或等固件 owner 落地"replay 未成环也允许 REBUILD"的修复；阶梯结束后再按 §1 重新 provisioning。
2. **10 MHz 建环**：顺序必须是 **STOP → TOPO → `RING:TRAIN 4096`（STOPPED 时）→ ARM → START**。
   "ARM 后 TRAIN" 会 `-200,"TDMA_RING_TRAIN"`，且 ARM 后 `adapter_started` 保持 0——这是顺序错误，
   不是建环能力缺失。正确顺序下四板可达 `adapter_started=1, tx/rx=27`（
   `out/calibration/guide-10k/bench/10m-start-20261108/summary.json`）。
3. **10 MHz 测量基线**：整片擦除会清掉 calibration/TRN-03 基线，10 MHz P1 的
   `failed_mask=0x05` 属基线缺失，需要 10M/P3 owner 恢复标定后重测，与建环无关。
4. **P3 重跑**：先 `CLEar` profile 再跑 P3（否则 boot 服务会在 P3 的 STOP/ARM 窗口内自启环），
   P3 通过后重新 provisioning。

## 5. 只读判据速查

| 查询 | 关键索引（0 基） |
|---|---|
| `BOOT:PLAN?` | 0=action 1=reason 2=steps 3=fail_step 6=have_profile 7=slot 8=nodes 9=level 10=flags 16=half_ring 17=boot_done 18=boot_next_step |
| `RING:STATus?` | 0=enabled 1=nodes 2=local 4=up 5=down 8=adapter 47=baud 48=period 49=total_up_tx 50=total_down_rx |
| `CMD:STATus?` | 40=frag_ok 42=frag_done 47=q_depth 49=deq 68=ack_ok |

## 6. 故障恢复

- 环不健康：四板 `SYSTem:TDMA:RING:STOP` → 查 `PROFile?` 是否 loaded → 重启；**不要**逐板发
  TOPO/ARM/START（那是旧的四口流程，已退役）。
- `boot_done=1` 但 `fail_step≠0`：按 fail_step 定位（6 常见于 `SET_FLIGHT_MODE` 的 STOP-only 前置
  未满足，即 STOP 尚未落地；重试或先确认 `RING:STATus?` 已 enabled=0/adapter=0）。
- 板号丢失（`*IDN?` 显示 NO.0）：`SYSTem:BOARD:NO <n>`。
- OTA 退化（durable=512 失败）：`picotool_flash.py --full-erase` 整片擦除 + 重刷 factory +
  逐板重写板号（会丢校准基线）。

## 7. 为什么这不是硬件问题（本次实测）

- 四板同 build `20261107175157`，10 kHz 环 `enabled=1, nodes=4, up=1, down=1, adapter=1`，
  单口 C/T 可送达（四板 `gen` 一致）。
- 10 MHz 用正确顺序能建环（`10m-start-20261108/summary.json`，`adapter_started=1`、`tx/rx=27`）。
- 10 kHz Q 段失败是 **boot plan 与 Q 恢复路径的交互**（CONSOLE-042），10 MHz P1 失败是**校准基线
  被整片擦除清掉**，两者都能在固件/标定侧闭环，不指向 PHY/接线。

## 8. 参考

- 契约：`docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`「开机重建与单口启环（TDMA-BOOTPROFILE-01）」
- 交接：`docs/calibration/CALIBRATION_HANDOVER_PLAN.md`
- 流程草案：`docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md`
- 残余台账：`docs/calibration/CALIBRATION_CTRQS_CHECKLIST.md`
- 最近证据：`out/calibration/guide-10k/evidence/20261108-020250-boot-final4/`、
  `.../20261108-020353-ct-final4/`、`out/HardwareAcceptance/20261108/p3-final3/`
