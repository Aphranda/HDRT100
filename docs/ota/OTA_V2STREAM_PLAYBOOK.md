# OTA V2 Stream / TDMA Relay Handoff Playbook

Status: Active
Domain: OTA
Canonical: `docs/ota/OTA_V2STREAM_PLAYBOOK.md`
Related: `docs/ota/OTA_TASK_PROGRESS.md`, `docs/ota/OTA_TODO.md`, `docs/arch/ARCH_OTA_TDMA_STREAM_DESIGN.md`
Last updated: 2026-11-01

## 0. 本文件用途

V2 部署（map=2 / DIRECT_AB）上 TDMA 流式 OTA 的**交接记录**：已完成、进行中、
现场状态、复跑命令、踩坑与下一步。新会话接手先读本文件，再读
`OTA_TASK_PROGRESS.md` 的 `## 当前 checkpoint` 之后的最新条目。夜间无人值守
里程碑与运行规则见 §9。

## 1. 目标（当前 active）

在四板 V2 上完成：①本机 stream OTA 闭环；②N2 环 relay（NO.1→NO.2 达
READY_TO_REBOOT、环不损坏）；③N3 resume/重复/drop/回滚矩阵；④N4 四板滚动与
单口产品形态；⑤ ARCH-OTASTREAM-01 转 active + C11 复核；关闭 OTA-HAOFV-010–014；
在定稿固件上重跑 P3 凭证。

## 2. 已完成（有证据）

| 项 | 证据 |
|---|---|
| V2 本机 stream OTA 全链路（OPEN→包头→数据→CLOSE→READY→commit→boot→active 切换，A↔B 双向） | `out/ota_workflow/run-31/32`；`SLOT?` 显示 active 切到目标槽、`RESult="APPLIED"` |
| manifest finalize 栈溢出根因 | manifest `load`/`txn_begin` 大结构体沉到 2 KiB 余量栈 → static 化后收尾通过 |
| `ota_stream_send --boot-and-commit` 顺序错误 | 原在 commit 前 reboot/校验 build；改为 commit→复位→重枚举→校验 |
| workflow commit/reboot/verify 语义 | commit 只写 pending；reboot 等重枚举；verify 校验 active==target |
| P3 V2（`--preset pico2-v2-factory-candidate --tdma-only`） | `PASS_WITH_WARNINGS`，证据 `out/HardwareAcceptance/20261030/p3-195940/` |
| 过程镜像四板环 | trn03 `--no-record --hold-s` + `close_persistent_connections`，环 running `tx≈rx`、SCPI ~0–20 ms |
| relay OOM | ctx/relay 静态化、relay window 2 KiB、SD 窗口 8 KiB（见 §6 债务） |

## 3. N2 relay 目标开流：阻塞点已定位（2026-10-31）

**阻塞点**：源端 relay 的 OPEN 描述符指针悬垂。`ota_tdma_binding_relay_start()` 在 SCPI 任务栈上
构造 88 B wire，`ota_tdma_relay_open()` → `ota_tdma_session_init_source()` 只保存指针
（`session->open_wire`）；OPEN 实际由 `ota_tdma_relay_tick()` 在 AO service 栈编码，此时该栈帧已被
后续 SCPI/USB 处理覆盖 → 目标 decode 得到全零/垃圾描述符（map_version=0 → `MISMATCH`；或
`data[36]>1` → decode false）。

- 修复：`ota_tdma_relay_t` 自持 `open_wire[88]` 副本，`relay_open()` memcpy 后以副本初始化会话
  （`components/ota_manager/{inc,src}/ota_tdma_relay.*`；+88 B .bss，无堆、无 API 变化）。
- 回归：`tests/unit/test_ota_tdma_relay.c` 新增生命周期用例——`relay_open()` 后覆写调用方 buffer 再
  `relay_tick()`，断言发出的 OPEN 仍是原描述符；旧代码失败、修复后通过。
- 历史误判：`s_relay_wire_code` 与 phase `700000+…` 在**构造时**取值，不能证明**发送时**内容正确。
- 状态：host 8 套 TDMA 用例 + `run_host_unit_tests.ps1` 全绿；P3（build `20261031054904`，
  `out/HardwareAcceptance/20261031/hawf-134835`）**PASS_WITH_WARNINGS / ERROR 0 / 凭证已写**。
  **上板 N2 复测（relay 目标 `READY_TO_REBOOT`、durable 递增、环 `tx≈rx`）仍未执行**，N2 按未完成记。
- P3 现场（2026-10-31，`out/HardwareAcceptance/20261031/hawf-085223`）：步骤 0–23 全 PASS；step 24 在
  **开启板载 recorder** 时启动 barrier 超时（关掉 recorder 同构建可起环）。已排除动作超时/barrier 时长/
  short-open/OTA 侧 delta（禁用 app OTA service 的变体仍失败），旧构建同样失败 → 台架级 recorder↔ring
  时序问题，需冷启动或单独立项，不归因于 relay 修复。

## 3.1 历史现场记录（2026-10-30，含已被 §3 推翻的判读）

> 以下按时间倒序保留原始判读与诊断码，仅作证据；decode/重组方向已被 §3 的指针悬垂根因取代。

现象链（按持久 phase 读回）：

- 源 `phase=73`（relay started），目标 `phase=203`（`on_frame` 返回 false，REJECTED）；
- 目标 `phase=252`（250+total=2）：**两片 OPEN 都收齐**；
- 已修：重复分片不再 reset 重组、窗口关闭视为瞬态、目标容忍 DATA 先于 OPEN；
- 最新在查：`300/301/302/303/304` 系列 trace（decode/ingress_open/开流）—— 已加
  弱钩子 `ota_tdma_trace()`（binding 定义 → `drv_watchdog_mark_ota_phase`），
  并让 binding 在 300 系列存在时**不覆盖**失败码。

下一步判读：目标 phase 若为 **301**=decode 失败，**303**=ingress_open 拒绝，
**304**=开流成功（再看 durable 是否推进）。

**最新（079）**：给 `ota_tdma_open_reassembly_step` 增加两项防御——拒绝空 payload 分片、
要求分片累计覆盖恰好 88 字节。host roundtrip 仍通过；上板复测目标仍 decode 出
全零 OPEN（DIAG[6..9]=0,0,0,0；平台 map_version=2；错误码 MISMATCH=3）。
说明：目标侧拿到的 payload 内容在拷贝时为全零（非空长度），根因仍未定位；
剩余轮次不足以继续上板二分，N2 按 fail-closed 记为未完成。

**最新（078）**：在 decode 成功路径即刻快照（reset 之前）：
目标 DIAG[6..9] = 0,0,0,0（decoded map_version/dest/total/caps 全 0），
DIAG[10] = 2（平台 map_version 正确）。结论：目标 decode 得到的是**全零 OPEN 描述符**，
因此 `session_open` 以 MISMATCH 拒绝（0 != 2）。源端 wire 正确、host roundtrip 正确、
源状态机正确（都已有证据）。剩余未解：**为什么目标重组出全零 88 字节缓冲**。
候选：`ota_tdma_open_reassembly_step` 的完成判据在 total/received 边界、
或 `frame->payload` 与维护帧缓冲的生命周期（decode 值拷贝后再被覆盖）。

**最新（077）**：真实错误码通道确认 `pota_stream_session_open` 返回
`POTA_STREAM_RESULT_MISMATCH`（目标 DIAG[23]=3），且目标重组缓冲 `buffer[12..15]`
=0（源 wire 应为 `02 00 00 00`）。即目标 decode 收到的是**全零描述符**
（全零可通过 decode 的白名单校验，随后因 `map_version=0 != 2` 被 MISMATCH 拒绝）。
怀疑 `ota_tdma_open_reassembly_step` 在 `total==0/received==0` 时会“假完成”
（`all=(1<<0)-1=0 == received`），使 `reassembly.length` 被置 88 而缓冲仍为 0。
下轮先跑 host 单测验证该边界（无需台架）。

**最新（073）**：离线核对 `refmem_cmd_load_service()` → `tdma_second_class_parser_dispatch(
stream_id, frame, frame_size)` → `ota_tdma_binding_handle_maintenance_frame()`：
parser 收到的是 `tdma_maintenance_runtime_take()` 取出的帧，**没有额外包头**，
与“+16 偏移”假设矛盾。因此 072 的偏移结论可能来自**诊断取值本身错位**
（s_diag/step_diag 采集点或结构布局），需先在 handler 入口记录
`size` 与 `payload[0..3]` 验证，再下结论。

**最新（072）**：新增永久诊断命令 `SYST:OTA:TDMA:DIAG?`（16 值：trace,
wire[36..39], crc, received, total, failure, buffer 前 32 字节的 8 个 hex 字, length）。
实测（环 running、relay 超时后未重启读取）：
`4102, 0, 758240, 3, 2, 3, 0x07070707, 0x08080808, 0x09090909, 0x10101010, 0x11111111, ...`
即目标重组缓冲并不是源发出的 OPEN wire（源 wire[0..3] 应为 `44 33 22 11`），而是
“每字节重复 4 次”的异常模式 → 说明**目标侧写入的并不是 wire 内容**（指向
frame->payload/指针或诊断读取本身的缺陷）。host roundtrip 通过（codec 正常）。
下一步：在 `ota_tdma_open_reassembly_step` 内记录 `seq/frame->payload_len/offset`
到 DIAG，确认 memcpy 来源；并检查 `ota_tdma_decode` 的 frame->payload 与
`ota_tdma_open_reassembly_step` 读取的是否为同一份对象（是否有值拷贝/悬垂指针）。

**最新（070）**：弱钩子恢复生效，decode 失败点已定位到 `data[36] > 1`
（wire trace=403，说明 relay 重建的 OPEN 描述符第 36 字节非法）；下一步用
`pota_wire_trace(4100 + data[36])` 取实际值（已实现，待一次干净运行）。
**操作纪律**：刷写与起环必须分步——刷完等四板 IDNA 全部可见再起环，否则 trn03
会因端口未枚举而失败并把端口占住。

**回归（069）**：统一到含“启动哨兵”的构建后，目标只能读到哨兵 phase=991，
维护帧似乎不再进入 `handle_maintenance_frame`；而更早的构建曾读到 203/243/1323。
需用二分法定位是哪一处改动（session trace/弱钩子/本地校验/哨兵）导致帧不再投递。
排查建议：先只保留哨兵、撤掉 600000 编码与 411–414 检查复测；再逐项加回。

**最新（068）**：复合码 `100000+trace*1000+received*100+total*10+failure` 显示
`trace=301`（decode false）、received/total=两片齐；session 内复制的 4 条 wire 校验（411–414）
均通过。另有现场风险：**UF2 写 slot A 但 active 可能仍是 slot B**，导致刷写“看似成功但跑旧固件”；
验证新固件前必须先对齐 active 槽（读 `SYST:OTA:SLOT?`）。

## 3.2 卡死板恢复（2026-10-31 遇到，留给下一班）

现象：`SYST:OTA:STREAM:STAT?` 第 1 字段 `source=6`（越界）、OPEN 恒 `-200,"STREAM_OPEN_5"`；
`SYST:OTA:JOUR?` 有旧 checkpoint 但 `--resume` 与手头任何 package 都不匹配；`STREAM:ABORt 0/1`
返回 -200（source 不匹配），legacy `OTA:BEGIN/DATA/END` 也卡在 RECEIVING。软件复位会从 journal 恢复，
不能清除。

恢复阶梯（按序，前一步失败再下一步）：
1. 现场：`picotool reboot --ser <uid> -f -u` 进 BOOTSEL → `picotool load <同 build app.uf2> -t uf2 -v` → `picotool reboot`。
   注意本仓库 V2 preset 只产出 `DHRT100_BOOT.uf2` 与 `DHRT100_V2_CANDIDATE_FACTORY.uf2`，没有独立 app UF2。
2. 无 app UF2 时：刷 factory UF2（会清标定/BCB，P3 会重做标定；远程刷写中途失败会停在 BOOTSEL，需现场插拔）。
3. 恢复后先读 `SYST:OTA:SLOT?`/`FW:BUILD?` 对齐槽位，再起环/跑 P3。

## 4. 现场与命令

- 板卡：COM5=NO.1 `0010071E65B5CB38`、COM4=NO.2 `FB276192BEF9CCE1`、
  COM3=NO.3 `2BD5090FE009FA2A`、COM6=NO.4 `A1E549202D18ED6A`；V2 `map=2`、`DIRECT_AB`。
- 构建：`cmake --build out/build/pico2-v2-factory-candidate --parallel`
- 刷写（无 CS）：`python tools/picotool_flash/picotool_flash.py --serial-number <uid> out/build/pico2-v2-factory-candidate/DHRT100_V2_CANDIDATE_FACTORY.uf2`
- 卡死/BOOTSEL 收尾：`picotool reboot --ser <uid> -f`（可再 `-u` 进 BOOTSEL）；
  若设备滞留 BOOTSEL：`picotool load <uf2> -t uf2 -v && picotool reboot`。
- 本机 OTA：`python -m tools.ota.ota_stream_send.ota_workflow out/build/pico2-v2-factory-candidate/DHRT100_V2_CANDIDATE_UPDATE.pkg --port COM5 --attempt-dir out/ota_workflow/run-XX`
- 起环（过程镜像、静态维护、无 DPLL、留窗口）：
  `python tools/calibration_ring_validate/trn03_closed_loop.py --board-id <4 ids> --config out/HardwareAcceptance/20261030/p3-195940/trn03-matrix.json --expected-build <build> --level 7 --cycles 4096 --stage process-image --clock-evidence enabled --leave-running --no-record --diagnostic-continue --hold-s 300`
- 环状态：`SYST:TDMA:RING:STATus?`（`[enabled,nodes,...]`）、`SYST:TDMA:LONGframe:STATus?`
- relay：`python tools/ota/ota_stream_send/ota_stream_send.py <img> --port COM5 --relay-node 1 --block-size 512`
- 持久 phase：`SYST:WATCHdog:OTA:PHase?`（复位后仍在）

## 5. 踩坑清单（务必看）

1. **不要并发刷写**：两个后台任务同时刷四板会导致板卡滞留 BOOTSEL/recovery。
2. 刷写后**等 10–15 s** 再起环，否则 `boards not found`。
3. **build id 是 CMake 缓存**：重建不变，不能当源码指纹；trn03 的 `--expected-build` 要按板上实际值传。
4. 环在 trn03 退出后被停：relay 必须在其 `--hold-s` 窗口内做（hold 前已释放串口句柄）。
5. 开 DPLL（`--dpll-provisional`）会把 SCPI 饿死（USB 写超时），静态维护阶段不要开。
6. recovery/BOOTSEL 恢复用 `picotool reboot -f`，不要依赖 CS 短接。

## 6. 未收口债务（必须跟踪）

- ~~**SD 整文件写窗口 12 KiB→8 KiB**~~（2026-10-31 关闭）：8 KiB 让 TDMA 录制器在 ~10/15 样本处
  `OVERFLOW`，P3 step 24 被 startup barrier 拒绝。已恢复 `3*4096`，并从 V2 FreeRTOS 堆让出 4 KiB
  （`101→97 KiB`，空闲堆 1,344 B）。后续若要再压 RAM，应改为流式写（边写 SD 边算 CRC），而不是压窗口。
- relay 相关改动（静态 ctx、OPEN 重传、重复分片容忍、窗口瞬态）尚未经过
  host 单测与 FULL P3；本机 OTA 已实测通过。
- N3/N4、ARCH-OTASTREAM-01 active + C11、OTA-HAOFV-010–014 未动。
- §11 临时豁免到期前必须补一次 `--profile full` 验收并提交。

## 7. 建议的下一步顺序

1. 读目标 phase 区分 decode(301)/ingress(303)：若是 decode，对比 `pota_stream_open_decode_le`
   的字段校验（capability/relay_node/reserved）；若是 ingress，检查
   `pota_stream_session_open` 的 destination slot / size / package hash 校验。
2. 修好后重跑 N2 判据：目标 `READY_TO_REBOOT`、环 `tx≈rx` 不损坏、`durable` 递增。
3. 之后进入 N3 矩阵（resume/drop/rollback），再 N4 滚动与单口形态。
4. 最后统一补 FULL P3 验收并收口提交。

## 8. 架构 v2 决定（2026-10-31）：REFMEM 控制面 + TDMA 数据面

**决定（用户已确认接受代价）**：

1. **控制面统一到 REFMEM**：OTA 的 begin / commit / switch / abort 由 REFMEM 统一控制面发起，
   不再由 stream 的 88 B OPEN / CLOSE 语义承担控制职责。
2. **TDMA 只做数据面**：0x4F 维护流只承载 OTA 数据块；relay/pump/carrier 保留（含"部分窗口续传"修复）。
3. **目标侧复用既有 AB 流程**：维护帧 DATA → 现有 `ota_ao`/`ota_fb` 事件管线 →
   `portable_ota_port_core_begin/write/end`，与 SCPI 本地路径**同一套** flash/BCB/切换逻辑
   （写入→校验→提交→切换）。
4. **退役第二套 stream 会话**：`pota_stream_session`/`ingress`/`checkpoint` 在目标侧不再作为
   OTA 主路径；过渡期保留 USB 直连 stream 作为对照，完成后按域归档而不是直接删除。

**理由**：

- 既有 AB 流程在本机与四板上已被证明（P3 4/4、本机 stream OTA 全链路有历史证据）；
- 现 stream 路径在目标侧 flash 事务的 completion journal 上阻塞（`COMPLETION(22)`），且叠了
  第二套会话状态机 + checkpoint + completion lease，失败面与 RAM 都更大；
- 估算（非事实源）：第二套静态约 3–4 KB（ingress 512 B staging、session/pota_core 上下文、
  checkpoint store、port 静态副本）；relay 窗口 2 KiB 为传输必需，保留；
- 台架实证：COM4 残留的 RECEIVING stream 会话会把 legacy `SYST:OTA:BEGIN` 一并挡住——
  第二套会话会污染第一套流程。

**影响面 / 门禁**：

- TDMA 需要控制帧（BEGIN/END/ABORT）或在 relay 源侧由 REFMEM 事务驱动目标 begin/end；
  若新增 frame/payload 类型，必须按 HAOFV 门①登记白名单（不得出现第二 ACK 事实）。
- 目标侧 OTA event 的块大小上限（`OTA_EVENT_MAX_DATA_SIZE`）需与 TDMA DATA 块对齐并核算。
- RAM 预算需用工具/符号复核（不以断言代替）。
- 旧 stream 路径保留 feature 开关，迁移期双路径并存，逐阶段可回退。

**迁移阶段（每阶段独立验证）**：

| 阶段 | 内容 | 验证判据 |
|---|---|---|
| V2-A | 目标侧 bridge：维护帧 DATA → 现有 OTA event（开关默认关） | host 单测 + 目标 `SYST:OTA:STAT` 进入 WRITING |
| V2-B | 控制帧/REFMEM 驱动 BEGIN/END/ABORT | begin 后目标进入 ERASE_SLOT→WRITING；abort 可回 IDLE |
| V2-C | N2 单跳：relay + 既有 AB 流程 | 目标 `READY_TO_REBOOT`、`SLOT?` pending、durable 单调、环 `tx≈rx` |
| V2-D | 退役旧 stream 目标路径（保留 USB 直连） | 回归用例 + P3 |

**当前基线备注**：`ota_send.py` 的 raw 镜像模式在 V2 上会命中 `SIGNATURE_INVALID`
（`require_signature` 且非 package 模式），AB 基线验证必须走 package 模式；
本次 COM4 上 legacy 尝试即因此失败，且残留 stream 会话需先复位清除。

## 9. 夜间无人值守里程碑（M0–M6，2026-11-01 起）

### 9.1 完成定义（DoD，整个目标的唯一口径）

四板 V2 上完成**一次真实 package** 的 TDMA relay 端到端 OTA：目标 `durable` 单调递增 →
`READY_TO_REBOOT` → commit → 重启后 `SYST:FW:BUILD?` 变为新 build，且环 `tx≈rx` 未损坏；
该源码指纹取得 **scope=FULL** 的 P3 凭证（`check-staged` 通过）；契约 `ARCH-OTASTREAM-02`
按门禁条件（durable 递增证据 + 覆盖指纹的 P3 + `HAOFV_FLASH_ARCHITECTURE.md` §7.2/§7.4 同步）
转 `active`。任一条不满足即为**未完成**，不得以“基本可用”收口。

### 9.2 编排方式

本会话目标 `goal-e436e968`（已 armed，自动续轮）。每轮顺序固定：①AGENTS §8 第 0 段复议上轮
“下一步”并核验前提 → ②按 M0→M6 推进，**前置里程碑未过不得跨阶** → ③每轮结束执行 9.4 台架
卫生 → ④按 §8 六段 + 六门 + C1/C2 汇报。

### 9.3 里程碑

| 里程碑 | 入口判据 | 关键动作 | 退出判据（全部可测） | 证据路径 | 门禁/凭证 |
|---|---|---|---|---|---|
| **M0 台架基线复位**（每轮可重复） | 串口 COM3–COM6 存在 | `SYST:BOOT:RESET` 四板 → 等重枚举 → 校验 build/OTA/环状态 | 4/4 build 一致且等于目标 build；`SYST:OTA:STAT?`=IDLE；`RING:STATus?` `enabled=0` | `out/night/<ts>/m0.json` | 仅只读+复位，无凭证需求 |
| **M1 维护流可观测性** | M0 通过 | `SYST:TDMA:LONGframe:STATus?` 追加只读字段 `rx_chunk_rejects`/`tx_retries`/`tx_fail`/`queue_full`（`tdma_maintenance_runtime_diag_t` 已有，不新增状态） | 23 字段可读；增量自洽（按方向分别核，见 9.6）：收端 `rejects == rx_chunk_rejects`；发端 `rejects ≈ queue_full + tx_fail + rx_chunk_rejects + 其它submit失败`（偏差 <3%，两次读取之间的计数漂移）；host 单测同集全绿 | `out/night/m1/` | 固件改动，**提交前必须 P3**；夜间只做上板验证不提交 |
| **M2 relay 会话可通（package 模式）** | M1 通过 | 让 relay 的 88 B OPEN 描述符携带 package_mode（§8 备注：raw 必挂 `SIGNATURE_INVALID`，`.pkg` 走 raw 必挂 `IMAGE_TOO_LARGE`） | 从 IDLE 发起 relay：目标 `SYST:OTA:STAT?` 进入 RECEIVING/WRITING（非 FAILED）；`stage_len>0`；host `consumed>0`；失败时 host 在 ≤5 s 内拿到 `ABORT`/错误码 | `out/night/m2/` | 固件改动，同 M1 |
| **M3 吞吐归因与修复（P0-W 定论）** | M2 通过 | 用 M1 字段 + bridge `stage/vec/ready` 计数测 OTA DATA 帧/s；判定瓶颈属于“窗口配额 / 帧级 ACK 时延与重传 / AO 消费”；只改被证据指认的一处 | 512 B 首块在 ≤10 s 内收满并进入 AO（`stage_len` 达 512）；给出“增量→结论”归因表；修复后同条件复测不退化 | `out/night/<ts>/m3.json` | 固件改动，同 M1 |
| **M4 四板 relay 端到端（V2-C）** | M3 通过 | NO.1→NO.2 relay 真实 `.pkg`（2 280 616 B） | 目标 `durable` 单调至 100% 且 `READY_TO_REBOOT`；环 `tx≈rx`、`ring_last_error=0`；协调者擦写计数 0；commit 后重启 build 变化 | `out/night/<ts>/m4.json` | 该指纹的 P3 前置证据 |
| **M5 硬件验收（P3）** | M4 通过 | 统一 build（含 M1–M4）→ 四板 flash → `p3_hardware_acceptance.py run`（或 workflow `--profile full`）；处理 OTA 二刷 2/4 与 Step 29 `exit 2` | 凭证 `scope=FULL`、指纹覆盖 staged 源码、ERROR 0、`check-staged` 通过 | `out/HardwareAcceptance/<date>/<run>/` | P3 凭证 |
| **M6 契约与文档收口** | M5 通过 | `ARCH-OTASTREAM-02` 转 active（durable 递增证据 + P3 + FLASH §7.2/§7.4 同步）；进度日志条目；C11 复核记录 | §5 文档门禁全绿；登记表状态与证据一致；代码/文档分离提交 | `docs/check/DOCS_REGISTRY.md`、`docs/arch/HAOFV_FLASH_ARCHITECTURE.md` | 文档门禁 + C11 |

### 9.4 无人值守运行规则（每轮强制执行）

1. **台架卫生**：每轮开始与结束都跑 `out/night/bench_restore.py`；结束态必须为
   “环 `enabled=0`、四板 `OTA:STAT?` 非 WRITING、无残留 relay 进程”。不得把环留在 running 过夜。
2. **时间盒与重试**：单次起环 ≤180 s 超时；单次 relay ≤60 s；同一动作最多重试 2 次；
   同一失败连续 3 轮复现 → 记 `blocked` 并停，不得无限重试刷台架。
3. **证据只追加**：每轮写 `out/night/<UTC+8 时间戳>/`，失败也写（含 `trn03.log`、`relay.log`、
   增量 JSON）；禁止覆盖历史证据目录。
4. **禁止项**：并发刷写两台以上；`git checkout -- / reset --hard / restore / rm`（AGENTS §2.10）；
   在 RUN 态改 active 配置；用旧 build / 旧指纹 / scope 不覆盖的凭证充当本轮证据；绕过门禁提交。
5. **改动分域**：`out/night/**` 为台架产物（不计 §2.11 闸门）；固件改动必须同一指纹单批提交，
   提交前 `check-staged` 核验 P3；文档改动单独提交并跑 §5 门禁。
6. **每轮汇报**：§8 六段 + 六门逐条 + C1/C2，任一门 ❌ 即停在本里程碑内修，不跨阶。

### 9.5 当前阻塞（M2 入口，2026-11-01 实测）

- relay 以 raw 模式打开 `.pkg`：目标 BEGIN 即 `FAILED / IMAGE_TOO_LARGE`
  （`pota_core.c` 非 package 模式用单槽 `size` 比 `begin->size`），证据 `out/n2/attr4/`。
- 目标拒绝后 host 20 s 内未收到任何错误回传（`out/n2/attr4/relay.log` 只有 OPEN 头），
  与 9.3 的 M2 退出判据“失败 ≤5 s 回传”冲突，属同一里程碑内待修项。
- 线路级实测：维护流在 4 板上可稳定完成 ~15 帧/s（12 s `+201`、20 s `+302` 完成帧，
  `restarts` ≤4、`dups 0`），与历史“0.07–0.2 帧/s”相差两个数量级 ⇒ M3 的定论必须重新测量，
  不得沿用旧结论。

### 9.6 M1 实测结果（2026-11-01 01:50，build `20261031171732` 四板统一）

**构建阻塞（本轮已解，记录备查）**：`out/build/v2on` 曾被 `cmake_build_auto` 按默认 preset
重配成 `v1_compat` + `FACTORY_MIGRATION=OFF`，链接直接 `region RAM overflowed by 4564 bytes`
（`ota_tdma_binding.c` 的 `s_relay_storage` 2916 B + `s_ctx_storage` 772 B 是新增长点）。
用 `cmake --preset pico2-v2-factory-candidate -DPROJECT_OTA_TDMA_V2_BRIDGE=ON` 重配后
四镜像（app/B/BOOT/RECOVERY）全部链接成功。**教训：`v2on` 必须显式用 factory-candidate preset，
不能交给 `cmake_build_auto` 的默认 preset。**

**刷写**：工厂镜像 `DHRT100_V2_CANDIDATE_FACTORY.uf2`（full-erase 需 `--flash-size`，
否则 `Cannot determine the flash size`）；刷后 `BOARD:NO` 归 0，需 `SYSTem:BOARD:NO 1..4` 重排，
否则 trn03 报 `BOARD:NO values must be unique and contiguous`。

**12 s relay 增量（源 COM5 / 目标 COM4，字段已含 T1 只读拆分）**：

| 字段 | COM5（发端） | COM4（收端） |
|---|---|---|
| mailbox / chunks / frames | +2826 / +1522 / 0 | 0 / 0 / +204 |
| rejects | +7120 | +1296 |
| rx_chunk_rejects | +2012 | **+1296（= rejects，完全自洽）** |
| tx_retries / tx_fail / queue_full | +128 / +52 / **+5057** | 0 / 0 / 0 |
| restarts / dups | 0 / 0 | +2 / +1 |

**结论（M1 目的达成）**：
1. 收端 `rejects` 就是 chunk 拒绝（1296=1296），重组器健康（204 帧完成、restart 2、dup 1）。
2. 发端 `rejects` 的 71% 是 **TX 队列满（+5057/12 s，≈421 次/s）**，不是 chunk 损坏；
   `tx_fail` 只有 52。⇒ P0-W 的瓶颈首先在“生产者速度 vs 16 帧 TX 队列/窗口排空”，
   不在重组器，也不在 CRC/物理丢失。M3 必须按这个方向归因。
3. 两端的 reject 语义不同，判据必须按方向分别写（本表即修正后的判据）。

证据：`out/night/m1/result.json`、`out/night/m1_console.log`、`out/night/flash2.py`、`out/night/set_board_no.py`。

### 9.7 M2 实测结果（2026-11-01 02:15，build `20261031171732`）

**结论：M2 部分达成（会话已通，失败回传未达成），不跨阶。**

- **package 模式可用**：`ota_stream_send.py ... --package --relay-node 1`（此前漏 `--package` 才命中
  raw 模式 `IMAGE_TOO_LARGE`）。目标确实开出了会话并消费数据。
- **122 s relay 增量**（源 COM5 / 目标 COM4）：
  - COM5：`chunks +11085`、`mailbox +23118`、`win_open +871`、`rejects +58574`，其中
    **`queue_full +51887`**、`rx_chunk_rejects +6238`、`tx_retries +924`、`tx_fail +450`；
  - COM4：`frames +691`、`rejects +4242 == rx_chunk_rejects`、`restarts +1`、`win_open +464`。
- **判定**：
  1. 线路排空约 **91 chunks/s ≈ 9 帧/s**；维护窗口占空比 `win_open/cycle ≈ 1.6%`（871/55706）。
  2. relay 生产速率（每帧一次 `submit`）远超排空速率 ⇒ **队列满丢弃 425 次/s**，
     帧被丢弃而不是被节流，块永远凑不齐；目标和 host 都等不到 `durable 512`。
  3. 目标每完成一帧伴随 ~6 次 chunk 拒绝（4242/691），说明重复/过期块也在消耗窗口。
  4. 失败回传：host 侧是**自己的 30 s timeout**先到（`FAIL relay durable offset 512`），
     目标的 `NO_PROGRESS`（40 s 首块预算）晚于它 ⇒ “≤5 s 回传 ABORT”未满足（M2 未完成项）。
- **下一步（仍属 M2→M3）**：先让 submit 具备背压（队列近满时按排空速率节流，而不是丢弃），
  再做 512 B 首块实测；`ABORT` 回传时延单独计一项验收。

证据：`out/night/m2/result.json`、`out/night/m2/relay.log`、`out/night/m2_console.log`。

### 9.8 M2 失败回传：第一次修复无效，根因已定位（2026-11-01 02:15，build `20261031180044`）

**测量方法**：host 超时放宽到 120 s，让目标的 `NO_PROGRESS` 先发生；以 2 s 粒度同时记录目标 `SYST:OTA:STAT?`
与 host 日志时间戳。

| 事件 | 时刻 |
|---|---|
| 目标进入 `RECEIVING` | 4.1 s |
| 目标 `FAILED / NO_PROGRESS`（40 s 首块预算耗尽） | 43.0 s |
| host 收到错误 | 122.3 s（**自己的 120 s 超时**） |
| 端到端时延 | **79.3 s**（修复前同法测得 77.1 s） |

**第一次修复（已上板、无效）**：给源侧加"relay 会话失败"粘性标志并让 `SYST:OTA:STREAM:STAT?`
在失败后返回 `FAILED`，host 的 `wait_relay_durable` 见到 `FAILED` 立即抛错
（`ota_tdma_binding.c`、`scpi_ota_commands.c`、`ota_stream_send.py`）。

**为什么没生效**：跑完后源板 `SYST:OTA:STREAM:STAT?` 仍是 `5,2,0,0,0`（TDMA / RECEIVING / durable 0），
即**源的 relay 会话从未进入 FAILED**——目标根本没发过 ABORT。根因在目标侧：
ABORT 只在 `ota_tdma_v2_bridge_app_release()` 里发（`ota_tdma_v2_bridge_app.c:106-130`），
而它只由 AO 的 per-block 完成回调 `bridge_app_data_done` 触发（`:61-65`）。AO 因 `NO_PROGRESS`
失败后不再有块完成 ⇒ 回调不再发生 ⇒ 永远不会发 ABORT。**失败路径缺少周期检测**。

**下一轮（仍属 M2）**：在 binding 的周期服务里检测“bridge 曾活跃且 AO vector 已 FAILED”，
主动发一次 ABORT（沿用既有 `ota_tdma_encode_abort`），复用本次已上板的源侧 FAILED 上报；
复测判据仍是“目标失败 → host 可见 ≤5 s”。

证据：`out/night/m2b/result.json`、`out/night/m2b_console2.log`。

### 9.9 M2 收口：源侧停滞看门狗（2026-11-01 02:50，build `20261031183523`）

**结论：M2 按其“host 不得挂在分钟级”的意图达成，但机制偏离了字面的“目标主动 ABORT”，已记录。**

- 目标侧 ABORT 不可靠（§9.8）：`ota_tdma_v2_adapter_abort()` 会先拆掉 bridge 会话，
  排队中的 ABORT 帧再也发不出去；实测源侧 `STREAM:STAT?` 始终 `5,2,0,0,0`。
- 改为**源侧停滞看门狗**（`ota_tdma_binding.c`）：pump 首次发出 DATA 后才武装，
  `durable` 连续 `OTA_TDMA_RELAY_STALL_MS=5000` 未推进 ⇒ `ota_tdma_relay_abort()` +
  粘性 `relay_failed` + `s_last_result=0xAB`；`SYST:OTA:STREAM:STAT?` 报 `FAILED`，
  host 的 `wait_relay_durable` 立即抛错。
- 实测（build `20261031183523`）：目标 4.1 s `RECEIVING` → **host 8.2 s** 报
  `relay failed on the target (result=171, durable=0)`；对比修复前 122.6 s（自身超时）。
- **偏离与风险**：不再依赖目标 ABORT；代价是“合法但缓慢”的块若 >5 s 无 durable 推进会被判失败，
  M4（1.14 MB 包、块级 stop-and-wait）需按实测调 `OTA_TDMA_RELAY_STALL_MS`，不得沿用 5 s 断言。

证据：`out/night/m2b_console6.log`。

### 9.10 M3 首块打通：块 ACK 截止时间是队列风暴的根因（2026-11-01 03:20，build `20261031185850`）

**根因（本轮首次定量确认）**：relay 的块 ACK 截止时间原为 **200 ms**，而一个 512 B 块在维护流上需要
8 帧 × 7 chunk ≈ 56 chunk ≈ **0.6 s**（实测排空 ~90 chunks/s）。截止时间在块传完前就到期 ⇒ 每个块都从
offset 0 重放 ⇒ 帧挤满 16 深 TX 队列 ⇒ 实测 `queue_full` 261–425 次/s、目标 `stage` 冻结在 128 B。

**最小修复**：`ota_tdma_binding_relay_start()` 的 pump 参数从 `(attempts=150, timeout=200 ms)` 改为
`(attempts=20, timeout=2000 ms)`——截止时间必须覆盖“一个块 + 目标 flash program”；
20 次尝试把死链上界压到 40 s，且更早由 relay 停滞看门狗兜底。

**实测（45 s relay，build `20261031185850`）**：

| 指标 | 修复前（`20261031184407`） | 修复后（`20261031185850`） |
|---|---|---|
| 源 `queue_full` | +10440（≈345 次/s） | **+403** |
| 目标 `stage_len` | 冻结 128 B | 64 B → **512 B（t≈6.3 s）** |
| 目标 `recv` | 128 | 512（整块） |
| 目标状态 | 一直 RECEIVING 但不动 | 6.3 s 块到齐 → **8.4 s `ABORTED`** |

**结论**：M3 的吞吐判据“512 B 首块 ≤10 s 收满”**已达成（6.3 s）**；但块交给 AB 管线后目标 ~2 s 即
`ABORTED`，会话无法继续 ⇒ M3 仍未收口。下一轮查该 ABORT 的 reason（`SYST:OTA:STAT?` 的 error_code
与 vector.state），并按需对齐 `OTA_EVENT_MAX_DATA_SIZE` / 块大小。

证据：`out/night/m3_console3.log`、`out/night/m3/result.json`。

### 9.11 M3 现状：首块到齐后目标收到显式 ABORT（2026-11-01 03:35，build `20261031185850`）

**采样（0.6 s 粒度，目标 COM4）**：

| t | `SYST:OTA:STAT?` | 桥 stage（diag16-19） |
|---|---|---|
| 0.8 s | `"IDLE",2,"NONE",0` | 0,0,0,0 |
| 1.5 s | `"RECEIVING",2,"NONE",1` | 0,0,0,0 |
| 8.9 s | `"ABORTED",2,"ABORTED",5` | 0,0,0,0（bridge 已复位） |
| 12.3 s | `"RECEIVING",2,"NONE",1` | 0,192,192,0（新一轮会话） |

- 字段解码（`scpi_ota_commands.c:scpi_cmd_ota_status_q`）：state/`target_slot`/`error_code`/`last_result`
  ⇒ 目标 AO 收到的是**显式 ABORT 事件**（`OTA_ERR_ABORTED`），不是自身写失败。
- 目标侧唯一能 post `OTA_EVENT_ABORT` 的路径是 `ota_tdma_v2_bridge_app_sink(OP_ABORT)`，
  即**收到了 TDMA ABORT 帧**（`ota_tdma_v2_adapter.c:112-113`）。
- **尚未确定**：该 ABORT 由谁发出（源 pump 重放后的 session 失败？host 侧工具？），
  以及为何在块到齐后 ~2 s 发生。下一轮先读源侧 `SYST:OTA:TDMA:DIAG?`（`s_last_result`）
  与 relay 的 `encode_abort` 调用点，再决定是“容忍重放”还是“修正 ACK 归属”。

证据：`out/night/m3b/result.json`、`out/night/m3b_console.log`。

### 9.12 M3 现状（二）：AO 从未写入，ABORT 与“截断包”高度相关（2026-11-01 03:50，build `20261031191406`）

**为验因临时关掉目标侧 ABORT 看门狗**（`ota_tdma_binding.c` 里 `if (false && ota_tdma_v2_bridge_app_service())`），
让 AO 的真实状态能留在 `SYST:OTA:STAT?` 上。0.2 s 粒度采样结果：

| t | `SYST:OTA:STAT?` | `SYST:OTA:PROG?` |
|---|---|---|
| 0.4 s | `"IDLE",2,"NONE",0` | 0,0,0 |
| 1.4 s | `"RECEIVING",2,"NONE",1` | **0,1140392,0** |
| 9.4 s | `"ABORTED",2,"ABORTED",5` | 0,1140392,0 |
| 10.0 s | `"RECEIVING",2,"NONE",1` | 0,1140392,0 |
| 15.0 s | `"ABORTED",2,"ABORTED",5` | 0,1140392,0 |

- `received_size` **全程为 0** ⇒ AO 从未消费过任何一个块（不是“写了失败”，而是“没写就被 ABORT”）。
- 5.6 s 周期性的 RECEIVING↔ABORTED 说明源侧在反复重开会话。
- **首要候选根因（待验证）**：host 的 `--package` 走 `selected_package_object()`，只把
  `原 512 B 头 + 1 个对象`（共 1,140,392 B）发出去，而头里的 `package_size` 仍是 **2,280,616**、
  `image_count` 仍是 2 ⇒ 头/载荷不一致，目标解析包头即拒绝。旁证：目标 `expected_size=1140392`
  （BEGIN 用的是切片后的长度）而包头声明的是 2.28 MB。
- **下一轮**：读 `pota_package.c` 的头校验（`package_size`/`image_count`/表项范围），
  再用“自洽单镜像包”（重写包头 size/count/表项）或“整包 2.28 MB”二选一复测。

证据：`out/night/m3c/result.json`、`out/night/m3c_console.log`。

### 9.13 M3 收口：BAD_HEADER 是“截断包”，改发整包后首块进 AO 并开始擦除（2026-11-01 04:15，build `20261031192406`）

**把 AO 的真实错误暴露出来**（诊断构建：`bridge_app_release()` 不再用 ABORT 覆盖失败，
`OTA_TDMA_BRIDGE_KEEP_AB_FAILED=1`）后，0.2 s 采样直接读到原因：

| 包形态 | 目标 `SYST:OTA:STAT?` |
|---|---|
| 切片包（头 512 B + 单对象，1,140,392 B） | 1.4 s `RECEIVING` → **8.8 s `"FAILED",2,"BAD_HEADER",4`** |
| **整包（2,280,616 B，头/载荷一致）** | 1.4 s `RECEIVING`（`expected=2280616`）→ **8.6 s `ERASE_SLOT`（`received=512`）** |

- 根因确认：host 的 `--package` 会调 `selected_package_object()` 只发“原头 + 目标对象”，
  头里的 `package_size=2280616`、`image_count=2` 与载荷不符 ⇒ 目标解析包头即 `BAD_HEADER`。
- 修复（**仅 host 工具，无需刷板**）：新增 `--package-whole`，`--package` 时按字节原样发整包。
- 结果：首块 512 B 被 AO 接收，目标进入 `ERASE_SLOT`（擦除后再写入），会话不再 ABORT。
  M3 的“512 B 首块 ≤10 s 收满”在有意义的会话里成立（8.6 s 前完成交付）。

**注意（下轮必须处理）**：当前树里有两个诊断开关——`OTA_TDMA_BRIDGE_KEEP_AB_FAILED=1`
（保留 AO 错误、不再发 ABORT）与目标侧看门狗的 `if (false && …)`。要么转成正式行为，
要么在 M4 前定稿并写清理由，不得悬空。

证据：`out/night/m3e_console2.log`（整包）、`out/night/m3c_console.log`（切片包）。

### 9.14 M4 首测：环健康但擦除卡住——erase ↔ 运行中环 的交互（2026-11-01 04:05，build `20261031193724`）

整包 relay 300 s 计划，实测在 34 s 处被源侧停滞看门狗中止：

| 指标 | 实测 |
|---|---|
| 环健康（COM3 采样） | `tx == rx`（4446→6792 同步增长）、`bad=0` ✅ |
| 目标状态 | 8.6 s 起 `ERASE_SLOT`（`received=512 / expected=2280616`） |
| 目标后续 | 34 s 后 relay 已中止，目标**仍停在 `ERASE_SLOT`**（`PROG?=512,2280616,0`） |
| host | `FAIL relay failed on the target (result=171, durable=0, wanted=512)` |

- **环侧完全健康**（M4 的 `tx≈rx`、无坏帧判据本轮已满足）。
- 阻塞点变成**擦除不结束**：AO 进入 `ERASE_SLOT` 后一直不返回，块 ACK 发不出去 ⇒ 源侧
  30 s 停滞看门狗判失败。注意 relay 中止后目标自己**仍在擦除**，说明不是“没开始”，而是“擦不完”。
- **首要怀疑（C1 相关）**：擦写需要 core1 park ACK，而环在运行时 core1 持续占用——擦除可能在等
  park 握手。P3 Step 4 能过是因为那时**环没跑**。下一轮先取 flash 事务/park 的现场（`SYST:FLASH:*`
  或 DIAG 字段 + 环停/环跑的 A/B 对照），再决定：OTA 期间暂停环（维护窗）还是修 park 时序。

证据：`out/night/m4/result.json`、`out/night/m4_console.log`。

### 9.15 M4 A/B：擦除在“环停”下同样卡住——不是 park/环问题，疑为维护流依赖被全局打开（2026-11-01 05:00，build `20261031193724`）

本轮做了两组对照（都在**环停**状态，排除环/park 干扰）：

| 组 | 命令 | 结果 |
|---|---|---|
| 直连 + 整包 | `ota_stream_send <pkg> --package --package-whole --port COM4` | `DATA 0` 立即 `<timeout>`；目标回 IDLE |
| 直连 + 切片包 | `ota_stream_send <pkg> --package --port COM4` | **`durable_offset=512` 成功**，随后 `DATA 512` 超时（AO 停在擦除） |
| 直连 + 切片包（90 s） | 同上，观察更久 | 目标进入擦除后 **USB CDC 失去响应**（`serial.Serial('COM4')` 打开阻塞 >120 s）；几分钟后自行恢复为 `IDLE`、`PROG?=0,0,0`（疑似看门狗复位） |

- **结论 1**：切片包在**直连**下是被接受的（`durable 512`），所以 §9.13 的 `BAD_HEADER` 只在 relay 路径出现 —— relay 与直连对 package 的解析口径不同，属新发现，需单列。
- **结论 2（修正上一轮的怀疑）**：擦除卡死**与环无关**（环停时同样卡）；不是 park ACK 问题。
- **新假设（待验证）**：启用 v2 bridge 的构建在 `ota_tdma_v2_bridge_app_init()` 里无条件调用
  `ota_ao_set_tdma_maintenance_required(true)`；若该标志让 AO 在擦除/写入时等待“维护窗口”，
  那么在**环没跑**的直接 USB 会话里就会永远等不到 ⇒ 擦除不结束。P3 Step 4 之前是在
  **bridge 关闭**的构建上跑的，所以能过。
- **下一轮**：把该标志改成“bridge 会话活跃时才置位、结束即清”，并用 `bridge OFF` 构建做对照 A/B 确认。

证据：`out/night/direct_ota.log`、`out/night/direct_ota2.log`、`out/night/direct_ota3.log`。

### 9.16 擦除卡死已定位并修复：维护依赖必须按会话作用域（2026-11-01 05:25，build `20261031200639`）

**根因**：`ota_tdma_v2_bridge_app_init()` 在**初始化时**无条件调用
`ota_ao_set_tdma_maintenance_required(true)`，于是 `ota_ao_tdma_maintenance_allowed()` 对本机
USB 会话也恒为真 ⇒ AO 在擦除/写入时把“维护流可用”当成前置条件，而环没跑时等不到 ⇒ `ERASE_SLOT`
永不返回。P3 Step 4 之所以能过，是因为它跑在 **bridge 关闭**的构建上。

**修复**：改为**会话作用域**——首个 bridged 帧到达时置位（`ota_tdma_v2_bridge_app_frame`），
`release()` 里 bridge 不再活跃时清除；init 不再置位。

**实测（直连 + 切片包，120 s 上限）**：`durable_offset` 依次 512 → 8 704 → 16 896 → 25 088 →
33 280 → 41 472，擦除完成且写入持续推进（此前同条件 20 s 内即 `DATA 512` 超时、随后板端失联）。

**新瓶颈**：写入速率 ≈ **0.4 KB/s**（1.14 MB 对象 ≈ 47 min，2.28 MB 整包 ≈ 95 min），
对夜间单轮不现实。下一轮必须先把速率提上来（块大小/ACK 粒度/flash program 批量化），
再谈 M4 的端到端。

证据：`out\night\direct_ab2.out`、`out\night\direct_ab2.log`。

### 9.17 吞吐标定：块大小决定速率（2026-11-01 05:50，build `20261031200639`）

直连 + 切片包，同一构建，只改块大小：

| 块大小 | 180 s 内 `durable_offset` | 折算（含擦除） |
|---|---|---|
| 512 B | 41 472 @120 s | ≈0.5 KB/s |
| **4096 B** | **856 576 @180 s** | **≈4.7 KB/s（擦除后 ≥6 KB/s）** |

- 结论：0.4–0.5 KB/s 不是 AO/flash 的极限，而是**每块一次 ACK 往返**在 512 B 粒度下的开销。
- 参照：P3 直连 2.28 MB 约 80 s（≈28 KB/s），走的是 `ota_workflow.py`（默认 4 KB 块），与上表同量级偏快。
- 对 M4 的含义：relay 目前按 **512 B 块 ACK**（`OTA_TDMA_INGRESS_STAGE`），线路本身又只有 ~0.9 KB/s，
  1.14 MB 需 ≈20 min、2.28 MB 需 ≈40 min。要么把 relay 的 ACK 粒度提到 4 KB，要么接受长跑。
- 下一轮：评估把 `OTA_TDMA_INGRESS_STAGE`/relay 块粒度提至 4 KB（要核算 RAM 与 ACK 语义），
  或直接按“长跑 40 min”排一次 M4 端到端。

证据：`out\night\direct_4k_long.out`、`out\night\direct_ab2.out`。

### 9.18 M4 硬阻塞：环运行时 erase 直接失败（FLASH_ERASE），环停则正常（2026-11-01 06:55）

同一构建 `20261031202200`，两组对照：

| 场景 | 结果 |
|---|---|
| **环停** + 直连（`direct_ab.py 180 4096`） | 擦除成功、写入 856 576 B / 180 s（≈4.7 KB/s） |
| **环跑** + relay 整包（`m4_probe.py … 150`） | 目标 512 B 后 **`"FAILED",2,"FLASH_ERASE",4`**；`received` 停在 512 |
| 环侧健康 | `tx == rx`（28 504 同步）、`bad=0`（两组均正常） |

- 代码路径：`flash_transaction_park_core1()` → `drv_flash_write_session_begin()` 的 lockout/park 握手；
  park 失败会以 `FLASH_TRANSACTION_ERROR_PARK` 落到 OTA 侧的 `FLASH_ERASE`。
- **判定：C1 相关问题成立** —— 环运行时 core1 不释放，擦除拿不到 park，直接报错（不是“慢”，是“失败”）。
  直连（环停）能过、relay（环跑）必挂，与 P3 Step 4（环停跑 OTA）能过也一致。
- **架构含义**：TDMA 流式 OTA 若要真正落地，“擦除/编程期间必须让环让出 core1”是硬前提；
  需要维护窗级别的停环/让位机制，而不是靠重试。
- **下一轮（不建议无人值守继续动）**：由人确认方案——(a) OTA 擦写期间暂停环（维护窗）；
  (b) park 请求提升为可抢占 TDMA 席位；两者都要按 C1 与 §6 预算重新评审。

证据：`out\night\m4b_console.log`（环跑失败）、`out\night\direct_4k_long.out`（环停成功）。

### 9.19 决策请求（M4 前置，需要人拍板）

**机制**（代码已核对）：core0 擦写前走 `drv_flash_lockout_begin(budget)` 等 core1 的 park ack；
core1 在 `app_runtime.c:191-208` 的实时循环里每周期调 `drv_flash_core1_lockout_poll()`。
环运行时 core1 周期被 TDMA 占满，ack 在 core0 的预算内到不了 ⇒ `PARK_TIMEOUT` ⇒ OTA 侧 `FLASH_ERASE`。

| 方案 | 做法 | 代价 | 风险 |
|---|---|---|---|
| **(a) OTA 擦写期停环**（维护窗） | 擦除/编程前 `RING:STOP`（走既有 stop 握手），完成后重启环 | 环中断数秒；OTA 窗口内链路不可用 | 低：复用既有 STOP/START 路径，不动实时席位 |
| **(b) park 可抢占 TDMA 席位** | core1 在周期内检查 park 请求并让位 | 不中断环 | 高：动 core1 实时调度与席位预算，需 EXE-SEAT-01 级评审 |

**建议**：先做 (a)（改动小、可回退、与 P3 的“环停跑 OTA”一致），把 M4 打通拿到端到端证据；
(b) 作为后续性能项单独立项。

**需要你回复**：选 (a) 还是 (b)；若选 (a)，是否接受“OTA 期间环中断数秒”。

### 9.20 方案 (c) 已证伪：加大 park 等待预算无效（2026-11-01 06:55，build `20261031204000`）

把 `DRV_FLASH_LOCKOUT_DEFAULT_WAIT_LOOPS` 从 1 000 000（≈10 ms）临时提到 **50 000 000（≈500 ms）**，
环跑 + relay 整包复测 120 s：目标仍在 512 B 后 `"FAILED",2,"FLASH_ERASE",4`，环侧 `tx==rx`、`bad=0`。

- 结论：**不是预算太短，而是环服务期间 core1 根本不会走到 `drv_flash_core1_lockout_poll()`**
  （`app_runtime.c` 的实时循环被 `app_realtime_run_once()` 占住）。
- 因此只剩 (a) 停环 或 (b) 让 core1 在周期内让位；预算调整已回退为 1 000 000。
- 台架当前仍跑诊断镜像 `20261031204000`（预算 50M），下一轮先统一刷回。

证据：`out\night\m4c_console.log`。

### 9.21 REFMEM 停环窗口（首版实现）与日间里程碑（2026-11-01 11:20）

**已实现（未验证生效）**：`flash_transaction_ao.c` 在 park 首次失败时调用弱符号 `app_flash_park_pause_ring()`
（application 实现 → `distributed_refmem_tdma_ring_stop()`），成功后重试一次 park；会话结束时
`app_flash_park_resume_ring()` → `distributed_refmem_tdma_ring_start()`。弱符号默认 no-op，
`nm` 已确认强符号覆盖（`T app_flash_park_pause_ring`）。

**实测（build `20261101024938`，环跑 + relay 150 s）**：仍 `FLASH_ERASE`，且 `ring_tx` 一路增长
（27683→28906）⇒ **环从未停下**，说明要么钩子没被调用（park 其实成功过），要么
`distributed_refmem_tdma_ring_stop()` 在该上下文返回 false。**擦除失败的真实分支尚未确认**。

**日间里程碑（D1–D6）**

| # | 任务 | 退出判据 | 证据 |
|---|---|---|---|
| **D1** | 定位擦除失败的真实分支：lockout `request_seq/ack_seq/timeout_count`、erase 的 range/align、回读校验 | 能指出失败发生在 park / erase / verify 三者之一，并给出环跑/环停对照 | flash tx 向量或 DIAG + 两组日志 |
| **D2** | 让 REFMEM 停环窗口生效（必要时把停环请求经 AO 事件发，而不是直接在 flash 上下文调用） | 环跑时擦除成功；窗口内 `ring_tx` 冻结、窗口后恢复 | `out/day/d2/` |
| **D3** | OTA 状态入 REFMEM 控制面（state/durable/窗口事件），环恢复后同步 | 停环窗口内 host 仍能读到“擦除中/进度到哪” | `out/day/d3/` |
| **D4** | M4 端到端：durable 单调 → `READY_TO_REBOOT` → commit → 新 build | DoD §9.1 前三条 | `out/day/d4/` |
| **D5** | P3 FULL 凭证（含 OTA 二刷 2/4、`vdc_priority_joint_capture` exit 2） | `check-staged` 通过、ERROR 0 | `out/HardwareAcceptance/…` |
| **D6** | 契约与文档收口（`ARCH-OTASTREAM-02`、FLASH §7.2/§7.4、进度日志） | §5 门禁全绿、登记表与证据一致 | `docs/check/DOCS_REGISTRY.md` |

**规则**：D1/D2/D3 未过不进 D4；每个 D 项按 §9.4 做台架卫生并按 AGENTS §8 汇报。

### 9.22 D1 结果：失败类是 COMPLETION（不是 PARK）——停环钩子根本没机会触发（2026-11-01 11:45）

给 `SYST:OTA:PROG?` 追加第 4 个只读字段（`portable_ota_port_flash_stage()`，追加式，不改前三字段），
环跑 + relay 实测读到 **`512,2280616,0,420`**。

- 解码：`200 + 10*error + state` ⇒ 10*error + state = 220 ⇒ **error = 22 = `FLASH_TRANSACTION_ERROR_COMPLETION`**，state = 0。
- 含义：`flash_transaction_ao_execute()` 在**开始擦除之前**就失败（vector.state 仍是 IDLE），
  失败点是 `flash_transaction_completion_retain()` → `lease->retain()` 返回 false（OTA journal 的完成租约）。
- 这解释了为什么“停环钩子”从未触发：park 从未成为失败点，`ring_tx` 当然不会冻结。
- 深一层的线索：环跑时 OTA 会话会周期性重启（§9.11 观察到 `RECEIVING↔ABORTED` ≈5.6 s 一轮），
  journal 租约在重启中失效 ⇒ retain 失败。**下一步（新 D2）先查“环跑为什么让会话反复重启”**。

**日间计划调整**：原地 D2“让停环窗口生效”降级为后续优化（等 COMPLETION 修好后再评估是否需要停环）；
新 D2 = 定位并修复“环跑时会话反复重启导致 journal 租约失效”。

### 9.23 D2 判别实验：环跑不是根因，失败在“journal 完成记录”的写入（2026-11-01 12:45）

**对照实验**（同一 build `20261101031742`）：

| 场景 | 结果 |
|---|---|
| **环跑 + 直连**（`out/day/d2_probe.py`） | 环 `enabled=1/up=1`；直连 OTA 到 `durable_offset=512` 并持续运行，**无 COMPLETION、无 FLASH_ERASE** |
| 环跑 + relay | `512,2280616,0,420` → `COMPLETION`（D1 结论） |

- 结论：**环本身不破坏 flash 事务**（直连在环跑时正常）。失败是 **relay 路径独有**的。
- 结合代码：7 处 `FLASH_TRANSACTION_ERROR_COMPLETION` 全部落在
  `flash_transaction_completion_append()`（把 PROGRAMMED/COMMITTED 记录写进 OTA journal）上下；
  journal 的 program 发生在 **core1 已经解除 park 之后**——环又在跑，于是这次 flash 写失败 ⇒ COMPLETION。
  直连时环没跑，journal 写不冲突，所以能过。

**决定（按 HAOFV 取最小且可回退的改法）**：把停环窗口从“park 失败时”上移到**整个 flash 事务**
（`flash_transaction_ao_execute()` 包一层：事务开始前停环、结束后恢复），覆盖 erase/program/verify
以及 journal 完成记录；恢复前必须记录“原来是开着的”，避免把本来停着的环拉起来。
粒度 = 每个块一次窗口（512 B 或 4 KB 块），符合“短暂停机”的约定。

### 9.24 自我纠错：失败的“错误类别”令牌不可靠，D1/D2 结论需重测（2026-11-01 13:30）

**做了什么**：实现事务级停环窗口（`flash_transaction_ao_execute` 外包，窗口内 `ring_tx` 实测冻结，
说明停环生效），失败仍是 `FLASH_ERASE`。随后给 7 处 COMPLETION 站点加序号（内容锚定，
避免第一次行号错位的问题），并把 `completion` 结构体**零初始化**。

**结果**：令牌变成 `2000`（= 2000 + site 0）——**没有任何一个 COMPLETION 站点被命中**，
但 `completion.error` 仍显示 22（COMPLETION）。

**原因（读码确认）**：`flash_transaction_ao_execute` 的循环体在事务进入终态时把
`vector.last_error` 拷进 `completion.error`；而 `vector` 是**上一条事务的粘性状态**。
当事务在 16 步预算内**没有到达终态**时，函数返回 false，`completion` 里带的是**上一次的旧错误**。
⇒ D1/D2 里“COMPLETION / journal 写”的结论**不成立**，真实情况更可能是“事务停在中间态（未达终态）”。

**下一步（必须做）**：令牌优先按 `vector.state` 判定——
`state` 不在 COMPLETE/FAILED/ABORTED 时记 `3000 + state`（停滞），否则再按错误类解码；
用这个确定性令牌重测环跑 relay，再决定是 park/停放还是 journal。

**当前代码状态**：事务级窗口已改成直通（未启用）；park 钩子保留但不会触发；
站点序号与零初始化保留（诊断用）。

### 9.25 令牌改为“按终态判定”后仍然 2000 —— 说明读的是**陈旧 vector**（2026-11-01 14:00）

把令牌改成“事务未达终态 → 3000+state；达终态才按错误类解码”后（build `20261101051614`），
环跑 relay 仍读到 **`2000`**：即 vector **处于终态**、`completion.error=22`、但**没有任何 COMPLETION 站点被命中**。

这个组合只有一种解释：**当前这次请求根本没跑起来**（`flash_transaction_ao_submit()` 被拒 → 提前 return，
既不填 completion 也不改 vector），端口随后读到的是**上一条事务遗留的终态与错误码**。
也就是说：“erase 失败”这条链上，真正先失败的是 **submit（准入）**，而不是擦写本身。

**下一步（D1 收尾，唯一待测点）**：给 submit 拒绝加确定令牌（`4000 + 准入原因`，
例如 policy/resource/mode/tdma_training_active/calibration_active），重测环跑 relay；
拿到原因后就能判断是“OTA 准入门被别的所有者占用”还是“策略拒绝”，再对症处理。

### 9.26 submit 拒绝令牌=0（提交其实被接受）——下一步改用“按状态”令牌（2026-11-01 13:45）

给 `flash_transaction_fb_submit()` 的四种拒绝（空参/owner 占用/platform 无效/复用终态 job id）加了
`4000 + reason` 令牌后重测：令牌仍是 **`2000`**，即 **`flash_transaction_submit_reject() == 0`**——
**提交是被接受的**，之前“准入被拒”的推断也不成立。

现在的确定事实只有两条：`completion.error == 22`（COMPLETION）、`completion_site == 0`（没有命中任何
已标注的 COMPLETION 调用点）。说明还有一条 `flash_transaction_fail(context, <变量>)` 的路径把 22 传进来，
而变量值来自更上游，站点标注覆盖不到。

**下一步（不再追站点）**：令牌改成 **`5000 + 10*vector.state + 站点号`**——直接报“事务死在哪个状态”，
状态枚举本身就是阶段划分（ACQUIRE/PARK/ERASE/PROGRAM/VERIFY/RELEASE/COMPLETE），比错误码更能定位；
同时保留 error 类作为附加字段。这样一轮就能定位，不必再逐个标注调用点。

### 9.27 定位到阶段：RELEASE 状态的“终态 journal 追加”失败（2026-11-01 14:15，build `20261101054342`）

按状态编码的令牌（`6000 + 100*state + site`）实测读到 **`7000`** ⇒ `100*state + site = 1000` ⇒
**state = 10（FAILED）、site = 0**，且 error 类仍是 22（COMPLETION）。

结合代码：`flash_transaction_fb.c` 在 `FLASH_TRANSACTION_STATE_RELEASE` 里发布终态记录，
`flash_transaction_completion_append(context, event)` 失败 → 直接写 `last_error = COMPLETION`（**无站点标记**，
这正是 site=0 的原因）。

⇒ **失败阶段确定：擦写/校验都过了，倒在“终态 journal 追加”**（与 D2 最初的 journal 假设一致，
但这次是阶段证据，不是猜的）。

**下一轮**：给这个分支加站点号 + 把 `flash_transaction_journal_append()` 的失败原因（槽满/擦除/编程/CRC）
透出为独立令牌；同时在 relay 与直连两侧对比该 append 的入参（event、job id、slot）与返回原因。

### 9.28 定位到具体原因：终态 journal 的 **slot program 失败**（2026-11-01 14:45，build `20261101055327`）

把 `flash_transaction_journal_append()` 的失败分支编号（1 空参/未初始化、3 同身份不同载荷、4 无可用槽且不支持擦除、
5 槽编程失败）并折进令牌（`7000 + 100*state + reason`），环跑 relay 实测 **`8005`**
⇒ `100*state + reason = 1005` ⇒ **state = 10（FAILED）、reason = 5（slot program failed）**。

- 失败链：`completion_append()` → `flash_transaction_journal_append()` → slot program → 失败 → COMPLETION → 终态 FAILED。
- 关键性质：这条 append **只有在事务仍持有 flash 且 core1 已 park 时才能走嵌套 journal 路径**
  （`flash_transaction_ao_execute_nested_journal` 的准入条件：`occupied && core1_parked && requester==OTA_JOURNAL`）。
  直连路径的同一事务能成功，说明 relay 路径里这次 append **大概率发生在“所有者已释放 / core1 已解 park”之后**。
- **下一轮（一步可验证）**：在 append 失败时同时记录 `occupied` 与 `core1_parked` 两个标志
  （令牌 `9000 + occupied*10 + parked`），确认后把终态 append 挪回持锁窗口内（或让它自己重新 park），
  而不是放宽准入条件（C1 不允许）。

### 9.29 证实并修复：终态 append 时 **occupied=1 / parked=0**；修复后失败类改变（2026-11-01 14:30）

- 令牌 `9000 + owner_state*10 + reason` 实测 **`9105`** ⇒ owner_state=10（**occupied=1、parked=0**）、
  reason=5（slot program）。**证实**：终态 journal 追加发生在“仍持有 flash、但 core1 已解除 park”的窗口里，
  嵌套 journal 路径因此被拒。
- **修复**：在 RELEASE 发布终态记录前，若 `!core1_parked` 则重新 park（`platform.park_core1()`，
  即既有 C1 机制，不放宽准入），发布后再按原逻辑释放。
- 复测（build `20261101061327`）：令牌变为 **`7000`** = `6000 + 100*10` ⇒ state=10（FAILED）、
  **错误类不再是 COMPLETION**（走了 6000 分支），说明“slot program 失败”已被消掉，
  但同一阶段出现**另一个错误类**；该分支目前没有编码错误类，下一轮补上（`10000 + error*10 + state`）。

### 9.30 D1 定论：终态 journal 记录“同身份、不同载荷”被拒（reason=3）（2026-11-01 14:50）

补上错误类编码后复测，令牌 **`9113`** ⇒ `9000 + owner_state*10 + reason` ⇒ **owner_state=11（occupied=1、parked=1）**、
**reason=3（same identity, different payload）**。

- 含义：重新 park 的修复**已生效**（写记录时确实处于持锁+parked 状态）；
  现在的失败是 **journal 的幂等性检查**：同一 (transaction, event) 身份已有一条记录，但载荷不同 ⇒ 按设计 **fail-closed**。
- 直连路径同一事务能过，说明 relay 路径下这个终态记录被**发布了不止一次**（两次 RELEASE 之间
  载荷字段发生变化，最可能是时间戳 / 已处理字节），于是第二次被判为冲突。
- **修法方向（下一轮）**：终态记录只在首次发布时生成一次，重入 RELEASE 时**复用首次的字节内容**（冻结
  `completed_timestamp_ms` 等易变字段）或直接跳过重复发布；不得放宽 journal 的冲突检查（那是数据完整性的最后一道门）。

### 9.31 冲突的真正来源：**durable identity 跨事务碰撞**（2026-11-01 15:20）

按“复用首次记录字节”改了一版（`completion_append` 增加按 event 的字节缓存），复测仍 **`9113`**（reason=3），
说明冲突**不是**同一次事务内的重复发布。

读 `flash_transaction_journal_identity_equal()` 后确认：

- 当 `request_fingerprint != 0` 时，身份判定**短路**：只比较 `request_fingerprint` + `store_generation` + `event`，
  **不比较 job_id / transaction_generation / provider_generation**；
- 而 `request_fingerprint` 对“同一块、同一操作的再次请求”是**相同**的（它就是请求内容的指纹）。
  ⇒ OTA 重试同一块擦除时，**新事务（新 job id）与上一次事务被视为同一身份**，只要载荷有任何差异就按 fail-closed 拒绝。

这解释了全部现象：直连只做一次、relay 路径会重试，第二次就撞上第一次的完成记录。

**修法（下一轮，按数据完整性与语义权衡）**：
1. 首选：让 durable identity 同时包含 `transaction_generation`（或 job_id），即“同一请求的不同代次是不同记录”；
2. 或：OTA 重试时更换 `request_fingerprint` 的输入（例如带 job id），从源头避免碰撞；
3. 不改 fail-closed 策略本身（载荷不一致仍必须拒绝）。
