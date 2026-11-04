# OTA 操作 Playbook（单板 / 四板 / 故障恢复）

Status: Active
Domain: OTA
Canonical: `docs/ota/OTA_OPERATIONS_PLAYBOOK.md`
Related: `docs/ota/OTA_HAOFV_ARCHITECTURE.md`, `docs/ota/OTA_TODO.md`, `docs/ota/OTA_V2STREAM_PLAYBOOK.md`
Last updated: 2026-11-04

## 0. 用途

给新会话/新操作者一份**可照做**的 OTA 操作顺序，覆盖：

- 单板本机 USB CDC stream OTA；
- 四板同固件批量 OTA；
- 失败分类、恢复、证据留档；
- 哪些“成功”不可信、哪些操作禁止。

本文是操作手册，不是架构或契约事实源：

- owner/状态语义以 `OTA_HAOFV_ARCHITECTURE.md` 为准；
- 任务状态以 `OTA_TODO.md` 为准；
- 本次现场史以 `OTA_TASK_PROGRESS.md` 为准。

## 1. 绝对规则

1. **一个台架同一时间只允许一个 OTA owner。** 不要和其他会话同时开 COM 口、同时 `picotool reboot` 或同时跑四板 OTA。
2. **数据面固定 4KiB。** 新会话不要试 8KiB/16KiB；扩容仍为 BLOCKED。
3. **不在 RUN/ARMED 态改 active 配置，不绕过 STOP/ARM 边界。**
4. **不以工具 `PASS` 代替板端 build 读回。** OTA 结束必须读 `SYST:FW:BUILD?`。
5. **不裸刷 raw app。** 恢复优先用 app-only UF2/OTA；`factory UF2` 会重置标定/BCB，只能作为明确接受代价的最后手段。
6. **包必须与板端 counter/key 匹配。** 有 `security_counter`/`key_id` 时必须有 64B 签名；不要用旧包冒充新包。
7. **每次操作写独立 `--out-dir`。** 证据不覆盖上一次。

## 2. 先认识状态

常用只读查询：

| 命令 | 用途 |
|---|---|
| `*IDN?` | 确认板号/序列号，避免刷错板 |
| `SYST:FW:BUILD?` | 当前运行 build |
| `SYST:OTA:SLOT?` | active/confirmed/pending 关系 |
| `SYST:OTA:STREAM:STAT?` | stream 状态、durable offset、token、错误码 |
| `SYST:OTA:PROG?` | core 进度/错误 |
| `SYST:OTA:RES?` | 最近 OTA 结果 |
| `SYST:OTA:TARG?` | 下一个目标槽 |
| `SYST:OTA:STREAM:CAP?` | 最大块/能力/map |
| `SYST:ERR?` | 清空并读取 SCPI 错误队列 |
| `SYST:OTA:BCB?` | BCB lane/record/wear 诊断 |
| `SYST:OTA:FT?` | FlashTransaction 最近状态/错误 |

判定原则：

- `STREAM:STAT?` 出现非零错误、FAILED/ABORTED、或 token 与本次不符：先恢复，不要直接发 DATA。
- `SLOT?` 只说明槽关系，不说明镜像内容正确；必须配合 `BUILD?` 和 `RES?`。
- `ERR?` 会消费错误队列；每次失败后读一次并记录。

## 3. 上板前检查（只读）

### 3.1 单板

```powershell
python - <<'PY'
import serial, time
s = serial.Serial("COM5", 115200, timeout=2)
for cmd in ["*IDN?", "SYST:FW:BUILD?", "SYST:OTA:SLOT?",
            "SYST:OTA:STREAM:STAT?", "SYST:OTA:TARG?",
            "SYST:OTA:STREAM:CAP?", "SYST:ERR?"]:
    s.write((cmd + "\n").encode()); s.flush(); time.sleep(0.15)
    print(cmd, "->", s.readline().decode(errors="replace").strip())
s.close()
PY
```

### 3.2 四板

用序列号而不是 COM 号：

```powershell
python tools/hardware_acceptance/night_preflight.py `
  --board-id 0010071E65B5CB38 `
  --board-id FB276192BEF9CCE1 `
  --board-id 2BD5090FE009FA2A `
  --board-id A1E549202D18ED6A `
  --expected-build <build-id> `
  --out out/HardwareAcceptance/preflight-<run>.json
```

要求：

- 四板 `build` 完全一致；
- 无 ring 打开；
- `SYST:ERR?` 均为无错误。

当前台架序列号快照（操作前仍应以 `*IDN?` 为准）：

| 板 | 序列号 |
|---|---|
| NO.1 | `0010071E65B5CB38` |
| NO.2 | `FB276192BEF9CCE1` |
| NO.3 | `2BD5090FE009FA2A` |
| NO.4 | `A1E549202D18ED6A` |

## 4. 构建与选包

### 4.1 构建

V2 板必须用 V2 preset；不要用默认 v1_compat 预设（RAM/heap 边界不同）：

```powershell
python tools/cmake_build_auto/cmake_build_auto.py `
  --preset pico2-v2-factory-candidate `
  --build-dir out/build/ota-<run-id>

cmake --build out/build/ota-<run-id> --target DHRT100_UPDATE
```

产物：

```text
out/build/ota-<run-id>/DHRT100_UPDATE.pkg
out/build/ota-<run-id>/generated/project_build_info.c
```

### 4.2 选包检查

发包前必须确认：

- 包路径是**本次构建**的；
- `project_build_info.c` 的 build id 与预期一致；
- 包有正确 signature/counter/key（如有签名门禁）；
- 不要使用 `out/build/v2on/...` 或任意历史 `out/build/p3-acceptance-*/...`，除非明确知道它的 build id。

建议把这三项写进 run 记录：

```text
package=<绝对路径>
build=<build id>
expected=<expected build id>
```

## 5. 单板 OTA 标准流程

```powershell
python tools/ota/ota_stream_send/ota_stream_send.py `
  out/build/ota-<run-id>/DHRT100_UPDATE.pkg `
  --port COM5 `
  --package `
  --block-size 4096 `
  --timeout 60 `
  --begin-timeout 120 `
  --expected-build <build-id> `
  --boot-and-commit `
  --out-dir out/ota_verify/<run-id>-no1
```

要点：

- `--package`：从 unified package 中切出目标槽镜像；
- `--block-size 4096`：不要用 8192/16384；
- `--expected-build`：必须写，不能省略；
- `--boot-and-commit`：commit → reboot → 重枚举 → 校验 build；
- `--out-dir`：每次唯一。

完成后立刻复核：

```text
SYST:FW:BUILD?        == expected
SYST:OTA:SLOT?        active == confirmed（正常路径）
SYST:OTA:RES?         最近结果符合预期
SYST:ERR?             无错误
```

## 6. 四板 OTA 标准流程

### 6.1 发包

```powershell
python tools/ota/ota_multi_update/ota_multi_update.py `
  out/build/ota-<run-id>/DHRT100_UPDATE.pkg `
  --expected-board-count 4 `
  --expected-build <build-id> `
  --block-size 4096 `
  --timeout 300 `
  --begin-timeout 300 `
  --serial-number 0010071E65B5CB38 `
  --serial-number FB276192BEF9CCE1 `
  --serial-number 2BD5090FE009FA2A `
  --serial-number A1E549202D18ED6A `
  --max-workers 2 `
  --out-dir out/HardwareAcceptance/<run>/ota-four-board
```

说明：

- 默认 `--max-workers 0` 会四板并行；USB CDC/重枚举不稳时用 **2**；
- `--serial-number` 是唯一可靠的选板方式；不要依赖 COM3/4/5/6 固定映射；
- `--expected-board-count 4` 防止只刷到一部分。

### 6.2 四板“成功”判定

**不要只看 `summary.json` 的 `passed` / `updated_count`。**

`updated_count` 是“提交了几块板”，`passed` 主要来自子进程返回码；它不保证 reboot 后板端 build 正确。

必须再做一次现场读回：

```powershell
python tools/hardware_acceptance/night_preflight.py `
  --board-id 0010071E65B5CB38 `
  --board-id FB276192BEF9CCE1 `
  --board-id 2BD5090FE009FA2A `
  --board-id A1E549202D18ED6A `
  --expected-build <build-id> `
  --out out/HardwareAcceptance/<run>/post-ota-preflight.json
```

只有四板 `build == expected` 且 `passed=true`，才算四板 OTA 成功。

### 6.3 单板失败时

- 先只重试失败板，不要立即重跑四板；
- 用 `--port` 单板发送，确认是包/板问题还是并行/端口问题；
- 失败板恢复后再跑四板 preflight；
- 每块板保留自己的 `ota.log`/`timing.json`，不要覆盖。

## 7. 失败分类表

| 现象 | 优先判断 | 处理 |
|---|---|---|
| `STREAM_OPEN_5` | 非 package/镜像太小，或上一 session 未清 | 检查 `image` 是否是完整 `.pkg`；`STREAM:ABORt 0` + reset 后重试 |
| `DATA ... 12800: '<timeout>'` | 多板并发/启动慢/旧 build 的 boot confirm 抢 flash | 停并发；单板重试；确认 build 含 1s idle boot confirm；不要继续灌数据 |
| `erase failed: state=9` | 上一次擦除/会话异常，或错误地用了 256KiB chunk | 先 reset；只用 64KiB 擦除的 build；不要用 256K 试验包 |
| `PBEGIN/OPEN` manifest/security 错误 | counter/signature/key 不匹配 | 换正确签名的包；不要把 manifest 错误当 DATA/链路问题 |
| `step4` 报 PASS 但 step24 build mismatch | OTA 工具只信子进程返回码 / 旧 OTA summary | 现场四板 `BUILD?` 读回；重新生成 preflight；不要复用旧 summary |
| `COMx FAIL` / `boards_missing_after_pre_reboot` | USB 重枚举/端口竞争 | 等重枚举；按序列号重开；减少 worker；不要同时跑其他会话 |
| `STREAM:STAT?` 停在 READY/FAILED 且 token 旧 | 上一次 session 未清 | `SYST:OTA:STREAM:ABORt 0`，reset，`*IDN?` 恢复后再 OPEN |
| `BUILD?` 仍是旧值 | 包/目标槽/commit 不对 | 保留 `timing.json`，查 `slot?`/`res?`；单板重试，不重复四板 |

## 8. 失败恢复顺序

```text
1. 停止发送，保留 out-dir
2. SYST:OTA:STREAM:ABORt 0
3. SYST:BOOT:RESET（或 picotool reboot -f）
4. 等 USB 重枚举，*IDN? 确认板子回来
5. SYST:ERR? 记录并清空
6. 单板重试一次
7. 仍失败 → 查 build/package/counter/session，不要盲目重刷四板
```

### 8.1 BOOTSEL / app-only 恢复

需要绕过运行态时：

- `SYST:BOOT:BOOTSel` 进入 BOOTSEL；
- 只写 **app 槽**的 UF2/镜像；
- 不要写 bootloader/BCB/calibration；
- 写完先 `*IDN?` + `BUILD?`，再决定是否 OTA。

### 8.2 factory UF2

`factory UF2` 会重置标定/BCB，且不是“新包验收”：

- 只在板子无法启动、且用户明确接受标定丢失时使用；
- 恢复后必须重新标定；
- 不能把 factory 恢复当成 OTA 成功凭证。

## 9. 证据留档

一次可复核的 OTA run 至少包含：

```text
<out-dir>/
  timing.json
  serial_trace.jsonl
  ota.log                # 四板时
  summary.json           # 四板时
  pre/post preflight.json
  package path + build id
```

报告时必须写：

- package 绝对路径 + build id；
- expected build；
- 四板 serial → build 读回；
- 使用的 timeout/block/worker；
- 失败时的原始错误行，不只写“OTA 失败”。

## 10. 新会话开场最小清单

```text
[ ] 确认没有其他会话占用台架/COM
[ ] 四板 *IDN? 序列号与预期一致
[ ] 四板 BUILD? 一致，或明确知道当前差异
[ ] STREAM:STAT? 无旧 session/错误
[ ] ERR? 为空
[ ] 选本次构建的 package，记录 build id
[ ] 单板先验证，再四板
[ ] OTA 后必须现场读回 build，不只看工具 PASS
```

## 11. 相关文档

- 架构/owner：`docs/ota/OTA_HAOFV_ARCHITECTURE.md`
- 任务状态/阻塞：`docs/ota/OTA_TODO.md`
- V2 stream/relay 现场史：`docs/ota/OTA_V2STREAM_PLAYBOOK.md`
- 验收/证据：`docs/ota/OTA_TASK_PROGRESS.md`
- 四板验收工具：`tools/hardware_acceptance/`
