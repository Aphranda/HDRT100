# 固件编译、USB 模式与烧录操作手册

Status: Active
Domain: OTA / Tools
Canonical: `tools/build_flash/OTA_BUILD_FLASH_PLAYBOOK.md`
Related: `tools/README.md`, `CMakePresets.json`, `CMakeLists.txt`, `docs/interface/SCPI_USB_INTERFACE_DESIGN.md`, `docs/check/DOCS_EXECUTION_CONSTRAINTS.md`
Last updated: 2026-09-20

本文整理当前仓库已有工具的使用流程，不新增固件能力或冻结契约。命令在仓库根目录的
PowerShell 中执行，文件按 UTF-8 读取。采用 PowerShell 是为了直接使用本机 Windows
串口、VISA 和 Pico SDK 工具链；不需要在终端临时编写 Python 来控制板子。

日常单板 USBTMC 调试按 **准备环境 → 选择配置 → 构建 → 检查产物 → 发现板卡 → OTA →
确认启动/槽位 → 切换 USBTMC → 功能验收** 执行。已有正常固件优先使用 `.pkg` OTA；
空板或应用无法通信时才使用 BOOTSEL/工厂 UF2 路径。

## 1. 先区分三种“分支”

| 类别 | 决定什么 | 如何核对 |
|---|---|---|
| Git 源码分支 | 编译哪份实现 | `git branch --show-current`、`git rev-parse HEAD`、`git status --short` |
| CMake preset / 编译参数 | Release/Debug、USB、RTOS、双核、Flash map 等能力 | `CMakePresets.json`、配置命令及最终 `CMakeCache.txt` |
| 板端 USB 模式 | 此次启动枚举为 CDC 还是 USBTMC | 实际枚举、`SYST:USB:MODE?`；保存值优先于编译默认值 |

源码分支示例（2026-09-20 快照，非固定产品配置）：

- `feature/node-sequence-reservation-trigger`：序列触发/单板调试工作线。
- `wip/tdma-real-flight-processing`：TDMA/VDC 重构工作线；本分支同步了部分文档，不能据此
  推断对应代码已全部合并。编译时以当前 checkout 的源码为准。

更换源码分支前处理好工作区改动，不用 reset/清理操作覆盖他人工作。不同源码分支、
不同 Flash map 或 USB 配置使用不同构建目录，避免沿用旧 cache 和旧包。

## 2. 当前有哪些编译配置

下表是 `CMakePresets.json` 与 `CMakeLists.txt` 的 2026-09-20 配置快照，非独立事实源；
后续新增/修改 preset 时同时维护本表。表中 USB 默认按全新构建目录计算。

| Preset | 构建类型 | FreeRTOS / multicore | 默认 USB 路径 | 用途与差异 |
|---|---|---|---|---|
| `pico2-release` | Release | ON / ON | Pico USB stdio CDC | 常规基线；不默认启用 USBTMC/运行时切换；OTA fault injection OFF |
| `pico2-usb-runtime-switch` | 继承 Release | 继承 ON / ON | TinyUSB CDC/USBTMC，空配置默认 CDC | **单板调试推荐**；启用运行时切换，含 preset 指定的 Pico heap 配置 |
| `pico2-validation` | Release | ON / ON | Pico USB stdio CDC | 开启 OTA fault injection，仅验证用途 |
| `pico2-debug-uart` | Debug | ON / ON | Pico USB stdio CDC | UART stdio ON、日志 UART OFF，OTA fault injection ON |
| `pico2-rtos-smoke` | Debug | ON / OFF | Pico USB stdio CDC | 单核 RTOS smoke，OTA fault injection OFF |
| `pico2-multicore-smoke` | Debug | OFF / ON | Pico USB stdio CDC | 非 RTOS 双核 smoke，OTA fault injection OFF |
| `pico2-rtos-multicore-smoke` | Debug | ON / ON | Pico USB stdio CDC | RTOS + 双核 smoke，OTA fault injection OFF |
| `pico2-v2-factory-candidate` | Release | ON / ON | Pico USB stdio CDC | 显式 v2 工厂迁移候选；Flash validation、v2_debug 签名配置及本地签名密钥前置，不是日常升级档 |

“纯 USBTMC”没有单独的命名 preset，使用 Release 加参数构建。USB 选择如下：

| 目标 | 在新目录使用的配置 |
|---|---|
| USBTMC 调试，保留切换能力 | `pico2-usb-runtime-switch` 加 `PROJECT_ENABLE_USBTMC=ON`、`PROJECT_USB_DEFAULT_MODE=USBTMC` |
| 可切换调试，空配置默认 CDC | 原样使用 `pico2-usb-runtime-switch` |
| 固定 USBTMC 编译路径 | `pico2-release` 加 `PROJECT_ENABLE_USBTMC=ON`、`PROJECT_ENABLE_USB_RUNTIME_SWITCH=OFF`、`PROJECT_ENABLE_USB_STDIO=OFF` |
| 传统 CDC 编译路径 | 原样使用 `pico2-release`；切换 USB 模式的写命令不可用 |

`PROJECT_ENABLE_USB_RUNTIME_SWITCH` 打开后使用 TinyUSB CDC/USBTMC，关闭 Pico USB stdio。
`PROJECT_USB_DEFAULT_MODE` 只决定产品配置空白时的默认值，不会覆盖设备之前保存的模式。
“运行时切换”实际为 SCPI 写入模式，随后 `SYST:USB:BOOT` 重启并重新枚举，不是原连接原地变成另一种设备。

## 3. 准备环境与本次输出目录

需要 Git、CMake、Ninja、ARM GCC、Pico SDK 及其子模块、Python。BOOTSEL 路径还需要 picotool。
使用 Pico SDK 环境终端或 VS Code Pico 扩展配置的工具链；换电脑时不得照抄另一台机器的 SDK 路径。
如果工具链不在 PATH，配置阶段应能通过 Pico SDK 定位；否则补齐本机工具链设置后再继续。

```powershell
git status --short --branch
git config core.hooksPath .githooks
cmake --version
ninja --version
python --version
cmake --list-presets
python -m pip install pyserial pyvisa
```

USBTMC 还需要可用的 VISA 后端及对应设备驱动，本机实测使用 NI-VISA。仅安装 `pyvisa`
不等于已经安装 VISA runtime；其他后端须单独确认支持本机 USBTMC 驱动。

以下变量供后面的命令共同使用，每次操作重新创建一组；在仓库根目录执行：

```powershell
$taskStamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$taskBuild = "out/build/usbtmc-$taskStamp"
$taskOut = "out/ota/usbtmc-$taskStamp"
New-Item -ItemType Directory -Path $taskOut | Out-Null
git rev-parse HEAD | Set-Content -Encoding UTF8 "$taskOut/source-head.txt"
git status --short | Set-Content -Encoding UTF8 "$taskOut/source-status.txt"
git diff --binary HEAD --output="$taskOut/source-working.patch"
```

有未跟踪源码时还须单独归档它们；Git diff 不包含未跟踪文件。正式提交验收的源码指纹由门禁工具生成。

## 4. 配置与编译（选择一条配置路径）

### 4.1 推荐：USBTMC + CDC/USBTMC 切换

```powershell
cmake --preset pico2-usb-runtime-switch -B $taskBuild `
  -DPROJECT_ENABLE_USBTMC=ON -DPROJECT_USB_DEFAULT_MODE=USBTMC `
  -DPROJECT_ENABLE_USB_STDIO=OFF *> "$taskOut/configure.log"
if ($LASTEXITCODE -ne 0) { throw "CMake 配置失败，查看 configure.log" }
cmake --build $taskBuild --parallel 4 *> "$taskOut/build.log"
if ($LASTEXITCODE -ne 0) { throw "编译失败，不能继续烧录" }
```

`--parallel 4` 是本手册的宿主构建并发示例，可按电脑资源调整，不是设备参数。
原生 `cmake --preset` 会正确处理 preset 的 `inherits`。当前
`tools/cmake_build_auto/cmake_build_auto.py` 的 `load_configure_preset()` **只读取本层
cacheVariables，不解析 inherits**，因此继承型 preset 使用上述原生命令。
该脚本还会清理不匹配的旧 CMake 元数据；换盘符时优先新建目录，避免误用旧目录修复。
其cache匹配只核对路径，不核对preset/参数；不要复用同目录切换配方后据其“configure=skip”
判断配置已更新。`--force-configure`也会先清理缓存，并非无副作用的配置预览。

### 4.2 固定 USBTMC 或默认 CDC

沿用第 3 节的新目录，以下配置命令二选一，然后执行与 4.1 相同的 build 命令及退出码检查：

```powershell
# 固定 USBTMC，不提供 CDC/USBTMC 写入切换能力
cmake --preset pico2-release -B $taskBuild `
  -DPROJECT_ENABLE_USBTMC=ON -DPROJECT_ENABLE_USB_RUNTIME_SWITCH=OFF `
  -DPROJECT_ENABLE_USB_STDIO=OFF *> "$taskOut/configure.log"
if ($LASTEXITCODE -ne 0) { throw "配置失败" }
```

```powershell
# 可切换的调试版本，但空配置默认 CDC
cmake --preset pico2-usb-runtime-switch -B $taskBuild *> "$taskOut/configure.log"
if ($LASTEXITCODE -ne 0) { throw "配置失败" }
```

其他 preset 同样使用 `cmake --preset <所选名称> -B <新目录>` 配置，再执行
`cmake --build <同一目录>`。使用自定义 `-B` 后不要直接执行 `cmake --build --preset ...`，
后者可能回到 preset 默认目录而构建另一套固件。

### 4.3 检查 build、cache 与产物

```powershell
Get-Content -Encoding UTF8 "$taskBuild/CMakeCache.txt" |
  Select-String 'CMAKE_BUILD_TYPE:|PROJECT_ENABLE_USB|PROJECT_USB_DEFAULT_MODE:|PROJECT_USE_|PROJECT_FLASH_DEPLOYMENT_MAP:|PROJECT_PICO_HEAP_SIZE:'
Copy-Item "$taskBuild/CMakeCache.txt" "$taskOut/CMakeCache.txt"
$taskBuildSource = Get-Content -Raw -Encoding UTF8 "$taskBuild/generated/project_build_info.c"
if ($taskBuildSource -notmatch 'g_project_build_id\[\] = "(\d{14})"') { throw "无法读取 build ID" }
$taskBuildId = $Matches[1]
$taskPackage = Join-Path $taskBuild 'DHRT100_UPDATE.pkg'
Get-Item $taskPackage
Get-FileHash -Algorithm SHA256 $taskPackage |
  Format-List | Out-File -Encoding UTF8 "$taskOut/package-sha256.txt"
$taskBuildId
```

Build ID 来自 `PROJECT_BUILD_ID`/`gen_build_info.py`，默认使用 UTC；与北京时间不同是正常的。
不要用今天的日期猜 build。只运行增量 build 不保证产生新的 ID；独立交付使用新构建目录。
默认全量构建还应输出 A、B、BOOT 的 `flash_link_contract=OK`，任一失败停止烧录。

| v1_compat 产物 | 用途 |
|---|---|
| `DHRT100_UPDATE.pkg` | 日常 OTA；统一包包含 A/B 两种链接镜像，工具按当前 OTA 目标处理 |
| `DHRT100_FACTORY.uf2` | 初次工厂写入/恢复；包含 Boot、App A 及 OTA metadata 初始化内容，会重置 OTA 启动状态 |
| `DHRT100.bin` / `DHRT100_B.bin` | 分别按 A/B 地址链接的裸镜像；诊断或明确使用 auto-target 工具时使用，不能随意互换 |
| `DHRT100.elf` / `.map` / `.dis`及 B/BOOT 对应文件 | 调试、容量和 Flash/XIP 链接检查 |

v2 使用 `DHRT100_V2_CANDIDATE_FACTORY.uf2` / `DHRT100_V2_CANDIDATE_UPDATE.pkg` 等候选产物，
受签名及迁移配置约束。不能把 v2 候选包当成 v1 日常 OTA 包；本手册下面的日常命令针对 v1_compat。

## 5. 发现设备并结束运行

关闭调试 GUI 的连接/轮询以及其他串口/VISA 客户端，整个流程只允许一个工具占用目标设备。
输入已知目标 UID；第一次操作可在设备管理器的序列号或已有 `*IDN?` 记录中查找，不能只按 COM 号认板。

```powershell
$taskUid = Read-Host '输入目标板 UID'
python tools/hardware_acceptance/sequence_feedback_validate.py --mode discover `
  --serial-number $taskUid --out "$taskOut/discover.json"
if ($LASTEXITCODE -ne 0) { throw "设备发现工具失败" }
```

`discover` 只报告枚举结果，返回成功不证明目标板已出现或 SCPI 可用。查看报告中的 serial/visa
列表，确认 UID；USBTMC 没有 COM 口，CDC 也不会出现在 USBTMC 资源列表中。

用当前接口查询 `*IDN?`、`SYST:FW:BUILD?`；通过 GUI 指令窗口或下面 CDC 工具发送
`TRIG:STOP`、`SYST:TDMA:RING:STOP`，读回运行态及 `READ:IO:STAT?` 确认停止、输出资源已释放。
USBTMC 可使用现有 GUI 的 VISA 连接发同样指令，完成后断开 GUI。

```powershell
$taskPort = Read-Host '输入当前 CDC 端口，或已知的重连备用端口（如 COM10）'
# 仅当前确实是 CDC 时执行：
python tools/scpi_query/scpi_query.py $taskPort '*IDN?' 'SYST:FW:BUILD?' `
  'TRIG:STOP' 'SYST:TDMA:RING:STOP' 'TRIG:SEQ:NEXT?' `
  'SYST:TDMA:RING:STAT?' 'READ:IO:STAT?' 'SYST:ERR?' --out "$taskOut/preflight.txt"
if ($LASTEXITCODE -ne 0) { throw "预检查失败，先查看原始响应" }
```

命令能力随旧固件不同，`Undefined header` 不等于设备断开。保留错误记录，按该 build 支持的
命令核对停止事实。特别是旧 CDC 固件可能没有 `SYST:USB:MODE?`，不能先强制切换再 OTA。
严格 OTA 工具会再次核对 UID/build，但其 preflight 不负责替业务停止流程兜底。
部分固件的 `SYST:TDMA:RING:STOP` 没有响应载荷，`scpi_query.py`会因该超时返回非零；
若后续owner快照明确证明TDMA已停，则保留该超时并依据读回确认，不为了取得ACK反复发送
STOP/START。若读回也超时或仍在运行，不能继续升级；`usb_runtime_switch.py`已单独处理
这种STOP无载荷情况，并仍强制核对停态。

## 6. 发送 OTA、重启并确认（按当前接口选择）

`visa_ota_update.py` 是单板首选完整入口：验证包、检查身份、发送、确认 READY_TO_REBOOT、
BOOT、重新发现同 UID、核对新 build、COMMIT、检查槽位和错误队列，并写入 `summary.json`。

### 6.1 当前是 USBTMC

从第 5 节发现报告复制真实资源字符串，不手写 VID/PID 猜设备。

```powershell
$taskVisa = Read-Host '粘贴发现报告中的完整 USBTMC VISA 资源'
python tools/visa_ota_update/visa_ota_update.py $taskVisa $taskPackage `
  --serial-number $taskUid --expected-build $taskBuildId `
  --cdc-fallback-port $taskPort --out-dir "$taskOut/update"
if ($LASTEXITCODE -ne 0) { throw "OTA 未闭环，查看 update/summary.json" }
```

### 6.2 当前是 CDC，电脑有 VISA 后端

同一工具可以使用 `ASRL<n>::INSTR`。COM 号与 `--cdc-fallback-port` 必须对应，工具仍校验实时 UID。

```powershell
if ($taskPort -notmatch '^COM(\d+)$') { throw "CDC 端口格式错误" }
$taskAsrl = 'ASRL' + $Matches[1] + '::INSTR'
python tools/visa_ota_update/visa_ota_update.py $taskAsrl $taskPackage `
  --serial-number $taskUid --expected-build $taskBuildId `
  --cdc-fallback-port $taskPort --out-dir "$taskOut/update"
if ($LASTEXITCODE -ne 0) { throw "OTA 未闭环，查看 update/summary.json" }
```

可额外传 `--expected-current-build <第5节读回的旧build>`，在写入前约束旧固件身份。
两种路径只选一种；`update` 子目录必须不存在，工具以独占创建保留证据，失败重试使用新目录。
发送工具已发 BOOT 时，不再手动重复 BOOT；完整入口会自动以 `--skip-boot` 进入确认阶段。
COM 口在重启后可能变化，按 UID 重连，不沿用旧 COM 号推断成功。

### 6.3 仅有 CDC/pyserial，或批量 CDC 升级

使用 `ota_multi_update.py`，先 dry-run 核对目标；单板也支持此入口。

```powershell
python tools/ota_multi_update/ota_multi_update.py $taskPackage --ports $taskPort `
  --serial-number $taskUid --expected-board-count 1 --expected-build $taskBuildId `
  --skip-pre-reboot --dry-run --out-dir "$taskOut/cdc-plan"
if ($LASTEXITCODE -ne 0) { throw "CDC 升级计划失败" }
python tools/ota_multi_update/ota_multi_update.py $taskPackage --ports $taskPort `
  --serial-number $taskUid --expected-board-count 1 --expected-build $taskBuildId `
  --skip-pre-reboot --out-dir "$taskOut/cdc-update"
if ($LASTEXITCODE -ne 0) { throw "CDC 升级未闭环" }
```

这里在第 5 节已停止设备，显式跳过默认 picotool 预重启；不会跳过 OTA 后的 BOOT/COMMIT。
多板时重复 `--serial-number`、列全端口并修改 `--expected-board-count`，先确认 dry-run 白名单。
该工具按 CDC 发现设备，不适合用来发现一组已经处于 USBTMC 的设备。
若新固件将启动为 USBTMC，后续重连仍需要 VISA 支持；纯 pyserial 环境应保持新固件为 CDC，
待装好 VISA 后再切换。低层 `ota_send.py` 仅传输，不能单独作为升级闭环完成证明。

## 7. 确保最终工作在 USBTMC

对于 4.1 的可切换固件，OTA 成功后执行：

```powershell
python tools/usb_runtime_switch/usb_runtime_switch.py `
  --serial-number $taskUid --build $taskBuildId --target USBTMC `
  --out "$taskOut/usb-tmc.json"
if ($LASTEXITCODE -ne 0) { throw "USB 模式切换或重枚举验证失败" }
```

该工具自动发现同 UID 的当前接口，核对 build，停止业务/TDMA，写入并读回模式，再重启
并核对 USBTMC 身份。已经在目标模式时不做多余重启。运行后 GUI 重新扫描 VISA 资源。

仅验证当前模式可添加 `--verify-only`；切回 CDC 使用 `--target CDC`。固定 USBTMC 构建
不使用写模式切换路径，直接通过 VISA 查询身份、build 和接口模式即可。

完成必须同时满足：OTA 总报告 `passed=true`，新 build 与包匹配，活动槽已确认，
`SYST:ERR?`无错误，以及最终 USBTMC 资源与目标 UID 一致。仅传输百分比到末尾、
`IDLE` 或 USB 设备出现都不足以替代这些条件。

## 8. BOOTSEL/工厂 UF2 恢复分支

仅用于初次写入或应用通信不可用。进入目标板 BOOTSEL 后，使用明确 UID；若 ROM 模式
无法按 UID 选择，先用 picotool 枚举，再同时指定真实 `--bus` / `--address`，不要猜设备。

```powershell
python tools/picotool_flash/picotool_flash.py "$taskBuild/DHRT100_FACTORY.uf2" `
  --serial-number $taskUid --out "$taskOut/factory-flash.txt"
if ($LASTEXITCODE -ne 0) { throw "UF2 加载失败，查看原始记录" }
```

picotool 路径可用 `--picotool <本机可执行文件>`覆盖，脚本默认路径见 `DEFAULT_PICOTOOL`。
应用 USB 无法接受强制 BOOTSEL 时，需要按住 BOOTSEL 重新上电后再运行。

工厂 UF2 会写 Boot、App 及启动元数据，不能当成保留现有 OTA 状态的普通升级。
`--full-erase` 会擦除整个外部 Flash，包含保存配置等数据，仅在明确的工厂恢复/迁移任务中使用；
日常命令不加此参数。v2 工厂迁移按 `tools/factory_package/`、`tools/factory_restore/`及
Flash 架构的签名恢复流程执行，不以改一个 preset 自动完成在线分区迁移。

picotool 返回成功只证明该工具加载/校验及重启阶段完成。之后仍执行第 5、7 节的运行态
UID/build/USB 核验，并读取 `SYST:OTA:SLOT?`、`SYST:ERR?`确认启动状态。

## 9. 功能验收与提交门禁

编译/烧录成功不等于序列、VDC、TDMA或波形验收。仅重新编译已有代码并烧录时，保留本次
构建、OTA、USB验证记录；实现切片则继续执行对应 TODO 的硬件回归及静默转台验收。

设备侧改动按 `AGENTS.md` 分流。只有 staged 源码全部满足单板工具白名单时，才使用如下
序列单板门禁；先确认夹具接线和外部输入频率。以下 `$taskVisa`应使用第 7 节最终资源：

```powershell
$taskSourceHz = Read-Host '输入当前 IN1 外部信号频率（Hz）'
$taskVisa = Read-Host '输入最终 USBTMC 资源'
python tools/hardware_acceptance/sequence_single_board_gate.py run `
  --serial-number $taskUid --build $taskBuildId --package $taskPackage `
  --ota-summary "$taskOut/update/summary.json" --visa-resource $taskVisa `
  --source-hz $taskSourceHz --out-dir "$taskOut/single-board"
if ($LASTEXITCODE -ne 0) { throw "单板验收未通过，保留失败证据" }
```

上例使用第 6.1/6.2 节产生的严格单板 OTA 摘要。其他入口产生的摘要必须确认门禁支持其
格式，不用手写 JSON 冒充。设备侧变更超出白名单时执行 P3；可用单节点配置按当前 P3
工具/配置说明执行，不能以单板凭证替代所有范围。纯文档或宿主侧改动按当前仓库规则
不要求烧录硬件。提交前核验相应 receipt，文档与源码分离提交，不复用旧 build 凭证。

## 10. 常见问题与留证

| 现象 | 核查/处理 |
|---|---|
| `SYST:USB:MODE?`超时、错误队列报 Undefined header | 核对当前 build；旧 CDC 构建可能没有切换能力。用现有 CDC 升级启用切换的固件，再读回 |
| GUI 没找到 USBTMC，但能看到 COM | 当前仍在 CDC；先发现 UID，再走 CDC OTA 或已支持的模式切换 |
| pyvisa 找不到 backend / USB 资源 | 检查 VISA runtime、USB 驱动及线缆；pyvisa Python 包本身不提供全部驱动 |
| COM10 升级后变成 COM3，或 COM 消失 | USB 重枚举正常可能改端口/模式；由工具按 UID 发现，不硬编码旧 COM |
| 提示端口占用/PermissionError | 关闭 GUI/轮询/其他客户端，确认上个工具已退出；保留原失败，不盲目重发写命令 |
| 输出目录已经存在 | 为本次尝试创建新子目录，不覆盖先前失败证据 |
| 改成 USBTMC 后构建 RAM/link 失败 | 核对 preset、heap、RAM map及已有资源预算，不能只看编译通过的旧包；不任意缩堆掩盖占用 |
| Flash map/签名/布局拒绝 | 核对 v1/v2、设备部署及签名配置；不关闭校验强刷 |
| 发送成功但 BOOT/COMMIT失败 | 查 send/commit分段日志、包hash、读回build和槽位；已发BOOT后只做重连/确认，先判定实际状态再决定重试 |

保留源码分支/HEAD/改动、configure/build日志、cache、build ID、包hash、发现/停态记录、
OTA summary与子日志、USB模式验证及功能报告。诊断失败原件保留，不把 `forced_continue`
改写为严格通过。修改工具CLI或preset后同步本手册，并以工具 `--help` 和实现核对。

最近已执行示例（2026-09-20 快照，非默认值）：源码 `0c913e83`，UID `839E1AE79EA20F31`，
build `20260920014116`，旧CDC COM10经VISA ASRL升级，重枚举CDC后切为USBTMC；最终资源
`USB0::0xCAFE::0x4030::839E1AE79EA20F31::INSTR`。
本次采用显式Release配置而非继承preset，heap保留CMake默认；不能把它当作4.1命令的逐字验证。
证据位于`out/ota/usbtmc-20260920-r1/summary.json`及
`out/usbtmc-ota-20260920/usb-tmc-r1.json`，包位于
`out/build/dhrt100-usbtmc-20260920/DHRT100_UPDATE.pkg`。
这些是本机产物，Git拉取不会自动获得它们；未据此宣称本轮完成完整功能验收。
