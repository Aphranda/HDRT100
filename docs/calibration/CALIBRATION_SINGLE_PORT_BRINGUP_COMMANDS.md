# 单口起环操作指南（10 kHz 引导环，唯一镜像）

Status: Active
Domain: CALIBRATION
Canonical: `docs/calibration/CALIBRATION_SINGLE_PORT_BRINGUP_COMMANDS.md`
Related: `docs/calibration/CALIBRATION_SINGLE_PORT_GUIDE_PLAYBOOK.md`, `docs/calibration/CALIBRATION_HANDOVER_PLAN.md`, `docs/calibration/CALIBRATION_REFMEM_CONTROL_PLANE_MIGRATION_PLAN.md`, `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
Last updated: 2026-11-08

本指南给**其它会话**直接照做：如何在这套四板台架上用"唯一 P3 镜像"完成严格单口启环与单口 C/T，
以及哪些现象不是硬件问题、哪些流程会与 boot profile 冲突。判据全部来自实板归档，不含推测。

## 0. 适用与前提

- **唯一镜像**：P3 验收镜像（`--preset pico2-v2-factory-candidate`，v2 地图）。不允许台架专用镜像或
  编译期开关；契约 `TDMA-BOOTPROFILE-01`（active）。
- **当前验证 build**：`20261108084736`（默认档四板实板验证；`20261108085621` 为同源 P3 构建，
  凭证尚未取得——本地 OTA 受阻，见 §4.5）。镜像不同则先确认 `SYST:FW:BUILD?` 四板相同。
- 四板板号 NO.1..4，环线 NO.1→2→3→4；协调口固定 **COM5 / NO.1**。
- **裸板默认入环**：刚烧录的板子只要板号已写（NO.1..4），**无需 provisioning** 就会按镜像内的
  产品默认档自组 10 kHz 环（level 23 / 4 节点 / slot=板号-1）；`BOOT:PLAN?` 第 20 字段
  `profile_source=2` 表示默认档，`=1` 表示持久档。板号为 0 时默认档不成立（`action=0`）。
  当前状态：默认档已在 build `20261108084736` 实板验证（四板 `source=2` 自组），但该固件改动
  **尚未提交**（等 P3 凭证）；HEAD 镜像仍是持久档/无档行为。
- **单口的定义**：*启环与命令下发*只用 COM5。**provisioning 是可选的"钉住/覆盖"步骤**（默认档已能
  启环）；若执行 provisioning，它是逐板本地 SCPI，写的时候需要各板自己的口（四口）——默认档让
  "换裸板"不再依赖这一步，但持久档写入本身仍是板内命令。

## 1. 一次性 provisioning（可选：钉住/覆盖默认档；写入需板内口）

> 默认档已经能让裸板入环。只有在需要"固定某块板的槽位/拓扑标签/档位"或把默认档替换成显式记录时，
> 才做这一步。

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
| `BOOT:PLAN?`（20 字段） | `action`：slot0=1(START_REF)，其余=2(LISTEN)；`steps`=8；`fail_step`=0；`have_profile`=1；`boot_done`=1；`boot_next_step`=8；`half_ring`=0；`profile_source`=1（持久档）或 2（默认档） |
| `RING:STATus?` | `enabled`=1、`nodes`=4、`up_running`=1、`down_running`=1、`adapter_started`=1 |
| 帧活性 | 间隔 ≥2 s 两次读 `total_up_tx`/`total_down_rx`（索引 49/50）单调增长 |

4. 归档（只读，不发任何起环命令）：

```powershell
python tools/calibration/guide_cmdplane_probe.py boot <tag>
# 证据：out/calibration/guide-10k/evidence/<stamp>-boot-<tag>/readback.json
```

### 2.1 单口引导完整指令顺序（分步 runbook）

> 口径：**第 2、5、7 步的"下发"只用 COM5**；第 3、4、6、8 步是四板**读回验证**（可选的逐板读回），
> 不参与下发。示例值取自 10 kHz P1（`action=1`、`crc=305398722`、`gen=962`、`epoch=1002`）。

| # | 动作 | 端口 | 指令 | 期望 |
|---|---|---|---|---|
| 0 | 前置核对 | COM5 | `*IDN?`、`SYST:FW:BUILD?` | `NO.1,DHRT100,...`；四板同 build（读回见下） |
| 1 | 复位 | 每板 | `SYSTem:BOOT:RESET`（NO.4→3→2→1） | 端口短暂掉线后重新枚举 |
| 2 | 等 boot 定局 | **COM5** | `SYST:TDMA:BOOT:PLAN?` 轮询 | 第 18 字段 `boot_done=1` 且第 4 字段 `fail_step=0`（≤33 s） |
| 3 | 判环 | 四板读回 | `SYST:TDMA:BOOT:PLAN?`、`SYST:TDMA:RING:STATus?` | `action` 1/2、`steps=8`、`enabled/up/down/adapter=1`、`half_ring=0`、第 20 字段 `profile_source`∈{1,2} |
| 4 | 帧活性 | 四板读回 | 间隔 ≥2 s 两次 `RING:STATus?` | 第 49/50 字段 `total_up_tx/total_down_rx` 单调增长 |
| 5 | 单口 C/T 配置 | **COM5** | `SYST:TDMA:GUIDE:CONFig 1,305398722,962,23,4000,4,3,0,4,16,1,64` | `OK` |
| 6 | 等计划到齐 | 四板读回 | `SYST:TDMA:GUIDE:PLAN?` | 四板第 4 字段 `gen=962`（10 kHz 约 60 s；16 字节计划需分片） |
| 7 | 触发 | **COM5** | `SYST:TDMA:GUIDE:TRIGger 1,1002` | `OK` |
| 8 | 结果判定 | 四板读回 | `SYST:TDMA:GUIDE:PLAN?` | `progress_status=1`(COMPLETE)、`trials_done=4`、`passed_mask=0xF`、`failed_mask=0`、`abort_site=0`；环恢复 `enabled/up/down/adapter=1` |
| 9 | 归档 | 主机 | `python tools/calibration/guide_cmdplane_probe.py ct <gen> <tag>` | `ct=PASS`，证据含 `command_port=COM5`、`response`、host fingerprint、四板读回 |

**参数说明（第 5 步）**：`CONFig <action_id>,<plan_crc32>,<generation>,<profile_level>,<window_ms>,
<trial_count>[,<candidate_count>,<candidate[0..4]>,<threshold_low>,<threshold_high>]`
（`scpi_cmd_system_tdma_guide_config`，候选段可选）。第 7 步 `TRIGger <action_id>,<epoch>`。

**失败分支**：
- 第 2 步超时仍 `boot_done=0`：抓 `BOOT:PLAN?` 的 3/17/18 字段，按 §6 定位失败步；
- 第 6 步四板 `gen` 不一致：查 `SYSTem:REFMEM:CMD:STATus?` 的 `frag_done/ack_ok`，确认命令只从 COM5 进；
- 第 8 步 `abort_site≠0`：按 §4.1（26/27 = restore/rebuild）与 §6 处理。

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

1. **CTRQS 阶梯 / Q 段恢复（CONSOLE-042，校准侧已修复；P3 最终 build `20261108055416`）**：
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
     `abort_site`（避免 strict verdict 误判），并清掉 `s_boot_half_ring` 状态锁存。三次一致验证：
     `restore-trace-20261108-121518.json`、`...-131349.json`、`...-140801.json`（P3 最终 build），
     四板 `prog=1/1`、`abort=0`、环 `1/1/1/1`。
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
4. **P3 重跑（默认档之后的新口径）**：默认档存在时，**清 profile 不再阻止上电自组**——P3 复位后
   默认档会在 3 s 后起环。因此 P3 流程必须在 `boot_done=1 && fail_step=0` 之后显式 `RING:STOP`
   （或等 ≥33 s 再开始它的 STOP/ARM 序列），不能再用"CLEar profile"当作防自启环手段。
5. **本地 OTA（durable-512，已知在途缺陷）**：P3 的 OTA 步骤目前会在部分板卡上失败：
   `stream failed while waiting for durable 512: '0,6,512,5002,5'`，
   `OTA:FT? = 10,2,17,17,0,256,0,1`（policy gate 17 = `FLASH_TRANSACTION_ERROR_MODE`）。
   已排除默认档/环占用（板号置 0 不启环仍失败）、RAM 锁存（复位后仍失败）、台架脏状态（整片擦除后
   仍失败）；对应 `docs/ota/OTA_TODO.md` 的 OTA-HAOFV-012/013，等流式 OTA/OTA owner 修复后重跑 P3。

### 4.6 动作阶梯与各动作 C 段载荷（P1/P2/P3/T0–T3）

**动作是阶梯**：必须按 P1(1)→P2(2)→P3(3)→T0(4)→T1(5)→T2(6)→T3(7) 顺序跑。
`app_ctrqs_start()` 只接受 `action == ladder_index+1` 或"重复上一个动作"（`app.c:2288-2299`）；
阶梯一旦分叉，后续动作全部拒绝，必须先复位四板再重来。

**参考脚本**：`tools/calibration/guide_cmdplane_probe.py` 里已有各动作的规范载荷，不要自己拼：

```powershell
python tools/calibration/guide_cmdplane_probe.py ladder 1 <gen> <epoch>   # 按阶梯跑
python tools/calibration/guide_cmdplane_probe.py p2 <gen> [epoch] [window] # P2 规范载荷
python tools/calibration/guide_cmdplane_probe.py ga <action> <gen> <epoch> <level> <window> <trials> <cand...> <lo> <hi>
python tools/calibration/guide_cmdplane_probe.py verify <action> <epoch> [gen] [repeats] [archive]
```

| 动作 | C 段 `candidates[]` | `threshold_low/high` | `trial_count` | 校验要点（`app.c:2300-2482`） |
|---|---|---|---|---|
| P1(1) 时钟 | `[ref_slot, TRAIN cycles...]`（≥2） | — | 窗口内 trial 数 | slot=window/trials，按 `3×period×max_cycles+500` 抬升 |
| P2(2) 编码 | `[ref_slot, codebook≤3, min_lag, max_lag, min_margin]`（**必须 5**） | lo=probe_phase≠0、hi=max_distance≠0 | **必须 1** | min_lag<max_lag；跨度≤256；level≠0 |
| P3(3) 时延 | `[start_link<nodes, signal_group≤1, pulse 1..4096, capture_words 1..1024, repeats 1..4]`（5） | lo=baud_hz(1k..50M)、hi=容差‰(≤50000) | 由固件算 `nodes×repeats` | — |
| T0(4) margin | `[origin_node<nodes, codebook 1..3, link_base_delay_ns≤1e6, offset_samples, repeats 1..4]`（5） | — | repeats | — |
| T1(5) SCK | `[link_index<nodes, link_base_delay_ns 1..1e6, search_start<search_end(≤256), sck_launch_guard≤64]`（5） | lo=max_best_distance≠0、hi=min_margin | trial_count | — |
| T2(6) DATA | `[link_index<nodes, link_base_delay_ns 1..1e6, search_start<search_end(≤256), marker_to_data 1..1e6]`（5） | lo=max_best_distance≠0、hi=min_margin | trial_count | — |
| T3(7) stage | 忽略候选表 | — | 1 | **`profile_level` 必须 =7（10 MHz）** |

规范示例（10 kHz 引导档、`gen` 任意）：

```text
P1: SYST:TDMA:GUIDE:CONFig 1,<0x12340000+gen>,<gen>,23,4000,4,3,0,4,16,1,64
P2: SYST:TDMA:GUIDE:CONFig 2,<0x22000000+gen>,<gen>,7,8000,1,5,0,1,80,120,0,10,512
P3: SYST:TDMA:GUIDE:CONFig 3,<0x33000000+gen>,<gen>,7,20000,1,5,0,0,32,256,3,10000000,50000
```

**`reason=13`（CONFIG_VALIDATION_FAILED）不等于"动作没实现"**：P2–T3 的 trial body 在
`app.c` 里是存在的（P2 校验 `2334-2354`、P3 `2355-2386`、T0–T3 `2387-2482`；P2 的 trial 调用
`3395-3397`、P3 的链路推导 `2694`）。`reason=13` 的常见原因是**载荷形状不符**（候选个数/trial_count/
阈值给错），或**跨过了阶梯顺序**。另注意 `app.c:1722` 的注释"only P1 wired"是旧注释，早于
P2–T3 的 trial body，不要据此判定"未接线"。

**旧 Q 记录**：若 CONFig 在 SCPI 层就被拒（没有进入 `app_ctrqs_start`），`GUIDE:PLAN?` 会保留
上一轮的 progress（例如 P1 的 `trials=4/passed=0xF`）而 `action` 已是新动作；判读时先核对
`gen/epoch` 与 `action` 是否同一轮。固件对"进入执行器后被拒"的场景会发布 `FAILED/13/trials=0`
（`app.c:2483-2498`），两种情形要区分开。

**`progress_status` 与 `abort_site` 的判读**（`GUIDE:PLAN?` 字段 11/23/24）：

| status | 含义 | 常见来源 |
|---|---|---|
| 1 | COMPLETE | 动作跑完、trials 全过 |
| 2 | STOPPED | 窗口内 abort（`app_ctrqs_abort`，reason 为具体原因）或 finalize 时 `failed_mask≠0` |
| 3 | FAILED | CONFIG 校验拒绝（reason=13）或窗口关闭后 restore/rebuild 失败（reason=14，`abort_site` 26/27） |

先看 `failed_mask`：`failed_mask≠0` 说明**trial 本身判失败**（例如 P2 的 coded 测量被拒），不是复环问题；
`failed_mask=0` 而 `status=3` 才是 restore 路径。再看 `abort_site`（26/27=restore/rebuild，100+stage=T3）。

**10 MHz 动作的 level 门槛（2026-11-09 更正）**：`plan.profile_level` 只对 **R（测量）段**有意义——
P2 在 `P2_STOP`、P3 在 `P3_PREP` 用它把环临时切到测量档，P1 分支**完全不读该字段**
（`application/src/app.c:2300-2333`；全文件只有 P2/P3/T3 读它）。所以“先用 level 7 跑 P1”这条旧口径
**作废**：P1 的档位字段是惰性的，P1 归档时环仍是引导档（10 kHz）。若 P2 在 10 MHz 上被拒
（`NOT_ONLINE_SAFE` 类），真正的修法是 `d3334244` 的“无持久 stage 时不 abort”，不是去改 P1 的 level。
架构目标口径：**C/T/Q/S 走 10 kHz 引导，只有 R 段用 10 MHz 测量，T3 之后 10 MHz TDMA 才常驻**。
   恢复手法见 §6 最后一条。


## 5. 只读判据速查

| 查询 | 关键索引（0 基） |
|---|---|
| `BOOT:PLAN?` | 0=action 1=reason 2=steps 3=fail_step 6=have_profile 7=slot 8=nodes 9=level 10=flags 16=half_ring 17=boot_done 18=boot_next_step 19=profile_source |
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
  **replay 只拉回半环（`up=1/down=0`）+ 20 s 完整度看门狗停环 + 有 boot plan 时无 REBUILD 回退**，
  盲等 45 s → `NOT_ONLINE_SAFE`。属**固件状态机交互**，不是测量失败；已在 `20261108055416` 修复并
  三次实板复验（§4.1）。
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
