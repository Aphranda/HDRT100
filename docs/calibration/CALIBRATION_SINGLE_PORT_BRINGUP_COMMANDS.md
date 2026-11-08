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
- **当前验证 build**：`20261108055416`（四板一致；单口启环与 Q 段恢复均在该版本复验）。镜像不同则先
  确认 `SYST:FW:BUILD?` 四板相同。
- 四板板号 NO.1..4，环线 NO.1→2→3→4；协调口固定 **COM5 / NO.1**。
- **单口的定义**：*启环与命令下发*只用 COM5。**provisioning 是一次性逐板配置（四口）**，不属于启环流程；
  四口读回只用于验证，不用于下发。原因：boot profile 写入是**板内本地** SCPI，而能跨板分发命令的
  REFMEM 命令面**依赖环在运行**；profile 恰恰是启环的前提（鸡生蛋），所以未配置过的板只能逐板接口写入。
  若要"全程只开 COM5"，必须让 profile 在出厂/夹具阶段预置，而不是在台架上首次 provisioning。

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

1. **CTRQS 阶梯 / Q 段恢复（CONSOLE-042，校准侧已修复；build `20261108050455`）**：
   - **旧镜像（≤`20261107175157`）**：boot plan 存在时 `APP_CTRQS_RESTORE_WAIT` 的 REBUILD 回退
     **不可达**（`application/src/app.c` 旧版），窗口关闭后 replay 没把环拉回时只能等满
     `APP_CTRQS_RESTORE_MS=45000` 并报 `NOT_ONLINE_SAFE`（Q 段 `progress_status=3 / reason=14`，
     即使 `passed_mask=0xF`）。
   - **根因（实板取证）**：boot replay 只报"接受"不保证成环——START 成功但只形成
     `up=1/down=0` 的半环（`applied==config`）；20 s 完整度看门狗随后把半环停掉，旧代码无回退。
     取证 `out/calibration/guide-10k/bench/restore-trace-20261108-114735.json`：
     `BOOT:PLAN?` 全程 `fail_step=0/boot_done=1/next_step=8`，t=33.8–52.1 s 为半环、t≈54.4 后
     `enabled=0`。
   - **修复后**：replay 定局后若仍未成环，`RESTORE_WAIT` 会抑制 replay 并回退到显式 REBUILD
     （环已静默则立即、否则 45 s 截止后），失败路径用 `abort_site` 26/27 区分；成功时不置
     `abort_site`（避免 strict verdict 误判），并清掉 `s_boot_half_ring` 状态锁存。验证：
     `out/calibration/guide-10k/bench/restore-trace-20261108-131349.json`（四板 `prog=1/1`、
     `abort=0`、环 `1/1/1/1`、赛后 `half_ring=0`）。
   **跑 CTRQS 阶梯前**仍需等 `BOOT:PLAN?` `boot_done=1 && fail_step=0`；若用旧镜像，先
   `BOOT:PROFile:CLEar` + 重启作为绕过。
2. **10 MHz 建环**：`RING:TRAIN` 的硬前置是 `enabled!=0 && adapter_started!=0 && data_enabled==0`
   （`components/tdma/src/tdma_ring_runtime.c:554-566`），所以**停机态 TRAIN 必 `-200`**；
   需要训练时顺序是 **STOP → TOPO/BRINGUP → ARM → 等 `adapter_started=1`（且 `applied==config`）
   → TRAIN → START**。
   已知事实（build `20261107175157`）：成功的 10 MHz 建环
   （`out/calibration/guide-10k/bench/10m-start-20261108/summary.json`）实际是 `--skip-train`，
   即**没跑 TRAIN 也建成了环**；而 `ARM 后立即 TRAIN` 的 `-200` 真因是 ARM 的 adapter start 没成功
   （`cfgseq=6/applied=5`、`last_reason=9 = ADAPTER_START_FAILED`），不是"顺序规则"。
   **不要**把"TRAIN（STOPPED）"写成正确顺序。（build `20261108041830` 的
   `10m-ring-m5/summary.json` 同样是无 TRAIN 建成；`10m-prep-m5` 只跑 steps 0–4，未尝试 TRAIN。）
3. **10 MHz P1 的 `failed_mask=0x05`**：先看候选相关性——旧 build `20261107175157` 的 demo 里
   trial 0/2 是 **4 cycles** 失败、trial 1/3 是 **16 cycles** 通过（`failed_mask=0x05` = bit0|bit2），
   且 Q 回读 `commit=2`（SKIP_ALREADY_DONE，要求已有匹配 generation 的持久记录）——这与"整片擦除
   导致校准基线缺失"相矛盾。**不要**先把 4-cycle 失败归因为基线缺失；应按 10 MHz 训练时序敏感性
   复测（cycles=4/16 各 N 次，抓 `RING:TRAIN:STATus?` 的 result/reject 原因）。新 build
   `20261108041830` 的 P1@10M 在 ARM 段就失败（`trials=0`），尚未进入训练步骤。
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
- `boot_done=1` 但 `fail_step≠0`：按 fail_step 定位（1=TOPOLOGY、2=STOP、3=STAGE、4=APPLY、
  5=BRINGUP_NO_CAL、6=SET_FLIGHT_MODE、7=ARM、8=START）。窗口关闭后的 replay 里最常见的是
  **3/4/5**：STOP 是异步的，`STAGE/APPLY/BRINGUP_NO_CAL` 要求 `enabled=0/adapter=0/applied==config`，
  会在 STOP 尚未落地时失败并原地重试。失败现场务必抓 `BOOT:PLAN?` 的 3/17/18 三个字段。
- 板号丢失（`*IDN?` 显示 NO.0）：`SYSTem:BOARD:NO <n>`（整片擦除后必然丢失）。
- OTA 退化（`stream failed while waiting for durable 512: '0,6,512,5002,5'`，四板
  `SYST:OTA:FT? = 10,2,17,17,0,256,0,1`）：picotool `erase -a -F` 会因 JEDEC 探测失败报
  "Cannot determine the flash size"，用显式尺寸回退：
  ```powershell
  python tools/picotool_flash/picotool_flash.py <factory.uf2> --serial-number <sn> --full-erase --flash-size 0x1000000
  # 擦除常在 ~95% 处 180 s 超时（正常），随后不带 --full-erase 再 load 一次：
  python tools/picotool_flash/picotool_flash.py <factory.uf2> --serial-number <sn>
  # 逐板 SYSTem:BOARD:NO <n>，再用单板 OTA 验证（OTA:FT 应为全 0）
  ```
  整片擦除会丢掉校准基线；恢复后重新 provisioning boot profile。


## 7. 这批阻塞的归因（2026-11-08 独立复核结论）

**不是永久性硬件故障**，但也不是一句"非硬件"能带过：

- 四板同 build `20261107175157`，10 kHz 环 `enabled=1, nodes=4, up=1, down=1, adapter=1`，
  单口 C/T 可送达（四板 `gen` 一致）；两条不同的环状态都能稳定形成 → 无永久 PHY 故障。
- **10 kHz Q 段失败**：窗口内 4 个 trial 全过（`passed_mask=0xF, failed_mask=0`），失败在窗口关闭后的
  **boot replay 未把环拉回** + `boot plan` 存在时无 REBUILD 回退（`app.c:3783`），盲等 45 s →
  `NOT_ONLINE_SAFE`。属**固件状态机交互**，不是测量失败。
- **10 MHz**：能建环（`10m-start`，且该次 `--skip-train`）；`ARM 后立即 TRAIN` 的 `-200` 是
  `ADAPTER_START_FAILED`（`cfgseq=6/applied=5`、`last_reason=9`）的时序不稳定，不是"顺序规则"。
  **4-cycle 训练失败是真实时序敏感性**（4 cycles 失败 / 16 cycles 通过，候选相关），需要按 10 MHz
  训练时序复测，不能归为硬件坏或"基线缺失"。
- **"整片擦除导致校准基线缺失"目前不成立**：P1 的 4-cycle 失败是候选相关；且 Q 回读
  `commit=2 (SKIP_ALREADY_DONE)` 要求已有匹配 generation 的持久记录，与"基线缺失"相反。
  若要结这条，需补 `READ:CAL:TRAIN:STAGE:PERSistence?` / TRN-03 stage 回读与 cycles=4/16 重复实验。


## 8. 参考

- 契约：`docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`「开机重建与单口启环（TDMA-BOOTPROFILE-01）」
- 交接：`docs/calibration/CALIBRATION_HANDOVER_PLAN.md`
- 流程草案：`docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md`
- 残余台账：`docs/calibration/CALIBRATION_CTRQS_CHECKLIST.md`
- 最近证据：`out/calibration/guide-10k/evidence/20261108-020250-boot-final4/`、
  `.../20261108-020353-ct-final4/`、`out/HardwareAcceptance/20261108/p3-final3/`
