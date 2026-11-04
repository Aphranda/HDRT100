# 运维与调试控制面（CONSOLE）任务进展

Status: Active
Domain: CONSOLE
Canonical: `docs/console/CONSOLE_TASK_PROGRESS.md`
Related: `docs/console/CONSOLE_ARCHITECTURE.md`, `docs/console/CONSOLE_TK_ARCHITECTURE.md`, `docs/console/CONSOLE_DOMAIN_TODO.md`, `docs/README.md`
Last updated: 2026-11-04

## 文档接口

本文记录 CONSOLE 域的真实执行、证据和失败；稳定语义见 [Architecture](CONSOLE_ARCHITECTURE.md)，任务状态见 [TODO](CONSOLE_DOMAIN_TODO.md)。条目标签使用 `CONSOLE-PROGRESS-YYYYMMDD-NNN`，最新在最上面。

## 当前 checkpoint

### CONSOLE-PROGRESS-20261104-029：HIL 物理链路最终判定（六对板零连通 + 单板回环无 RX）

- 追加“直连镜像”映射：NO.1 master `rx19,csn17,sck18,tx16` / NO.4 slave `rx16,csn17,sck18,tx19`，`BOOTstrap:STATus?` physical rx 仍 **0**。
- 单板回环：NO.1 `rx20,csn17,sck18,tx16`（假设 16→20 跳线），physical rx 仍 **0**。
- 六对板 `two_board_io_validate` 全部 FAIL、observed 映射为空；`SYST:REFMEM:SYNC:TDMA:STATus?` 四板均回 `1,1,...` 但无对端 ready 证据。
- 结论：当前台架没有可用的两板物理链路（IO 组与回环都不通），HIL 无法执行；代码侧（carrier/命令/reply/超时、RX DMA 888B）已完成并有 host/链接证据。

### CONSOLE-PROGRESS-20261104-028：RX DMA 单字节 lane 收缩（-888B，ON/OFF 双镜像）

- `s_refmem_spi_rx_dma_words` 由 `uint32_t[296]`（1184B）改为 `uint8_t[296]`（296B），两处 DMA 配置（`capture_words`、`receive_begin`）统一 `DMA_SIZE_8`，三处读取去掉 `& 0xFFu`。
- 收益：ON `.bss` 0x68a94→**0x6871c**，余量 364B→**1252B**；OFF `.bss` 0x68998→**0x68620**，余量 616B→**1504B**（与预测一致，各 −888B）。
- 新增 `tests/python/test_refmem_spi_rx_dma_width.py`（2 passed）：锁死 `uint8_t` 声明、恰好两处 `DMA_SIZE_8`、无 `DMA_SIZE_32/16`、DMA 读取行无掩码、4+36+256=296。ON/OFF 四板均链接 exit 0；refmem host 全通过。
- **未验证的硬件前提**：`DMA_SIZE_8` 读 PIO `rxf` 是否按“一次一个条目、取 bits[7:0]”工作，需两板 `SYNC:RAW:RX? 256,165` 要求 `mismatch_count=0`；当前台架 IO 组零连通，无法执行。

### CONSOLE-PROGRESS-20261104-027：HIL 物理链路判定——两板 IO 预检零连通

- 用仓库自带 `tools/two_board_io_validate/two_board_io_validate.py` 分别测 **NO.1↔NO.4** 与 **NO.1↔NO.2**：两边 IO profile 都是 `IN GPIO20..23 / OUT GPIO16..19`，但所有输出位在对面输入 mask 均为 `0x0`，observed 映射为空，结论 **FAIL（无任何连线）**。
- 随后用 IO profile 推导的引脚（master `rx20,csn17,sck18,tx16`；slave `rx20,csn21,sck22,tx16`）ARM，`BOOTstrap:STATus?` 仍 physical rx=0；此前 16..19 与 TDMA 24..29 映射同样 rx=0。
- 结论：**当前台架不存在两板 IO 组直连**；若“线序未变”指的是 TDMA/RJ45 链路，则低速 SPI 需要按该链路的电气方向重新映射（现有 SPI adapter 的 master/slave 方向与 BiSS 单链路方向不一致），不能直接套用 16..19。
- 台架已刷：NO.1/NO.2/NO.4 均为 `20261104055417`（ON 镜像），HIL 只差物理链路确认。

### CONSOLE-PROGRESS-20261104-026：bootstrap carrier 注册与 reply/超时闭环；台架 NO.1/NO.2 已刷 ON 镜像

- **代码**：`distributed_refmem_link.c` 新增 bootstrap binding——命令 envelope（kind/type/generation/target_mask）、`distributed_refmem_cmd_frame_receive`、reply envelope（source/seq16/flags/reason）→ `cmd_reply_store`、`cmd_reply_tx_get`→relay 回送、reply 超时→`node_control_update_status(TIMED_OUT/TIMEOUT)` fail-closed；`bootstrap_start` 注册 carrier，stop/handover 注销。ON 双板链接 exit 0，`.bss` 0x68a94，余量 **364B**；refmem host 全通过。
- **台架**：用 `tools/picotool_flash/picotool_flash.py` 按序列号把 ON factory UF2 刷入 **NO.1/NO.2**，两板 `SYST:FW:BUILD? = "20261104055417"`，`BOOTstrap:STATus?` 可查；ARM 返回 `"OK",1,...` / `"OK",2,...`（pin 16..19 与 TDMA 24/26/25/29、24/27/28/29 都试过）。
- **HIL 未通**：master relay/physical tx 增长、physical rx=**0**；slave physical rx=**0**、每 tick timeout。即两板间没有收到任何字节，carrier `ready` 始终为 0，命令准入不会放行。下一步用 `SYNC:SPI:RAW:TX/RX?` 或逻辑分析定位交叉线序/CSN（raw 与 relay 共用 adapter，需先解决互斥）。
- **未做**：分片命令（fragment）不在此 carrier 承载；HIL 未通过前不把“承载”写成完成。

### CONSOLE-PROGRESS-20261104-025：独立冗余审查结论与 HIL 线序纠正

- **独立审查**（只读子代理，map 13:39 快照）：全仓**没有**可安全合并的两个 live 实例；SCPI 诊断 context 与常驻 context 不能合并（`refmem_sync_init` 会 `memset` 掉 `command_handler`，且 `SYNC:RX` 注入口会升级为真实命令通道），只能开关化。可回收 top：`s_refmem_spi_rx_dma_words` 4× 容量（888B，需 HIL 改 DMA 宽度）、SCPI 桥开关化（2252B，产品面决策）、校准阶段 union（最多 ~7176B，owner 仲裁不对称，暂不合并）。三份 8KB 表镜像/`s_capture_payload_storage`（已是 union）/三个 `s_action` 均**不可合并**。CRC 去重只省 flash，不增主 RAM。
- **线序纠正**：两板最小系统沿用既有 TDMA/GPIO16..19 线序，**无需改线**；此前“GPIO16-19 未接线”的阻塞判断作废。HIL 唯一剩余动作是刷 `PROJECT_REFMEM_BOOTSTRAP_ENABLED=ON` 镜像并执行 ARM/STATus。
- **台架读数**：COM5=`NO.1`、COM6=`NO.4`，仍是旧 build；`SYSTem:REFMEM:BOOTstrap:STATus?` 无响应，确认需刷 ON 镜像。

### CONSOLE-PROGRESS-20261104-024：低速 SPI 承载 REFMEM——relay 出站/入站与 reply 窗口

- **结果**：relay 增加 `refmem_bootstrap_relay_submit()`（同步 TX，无需新缓冲）、`WAIT_REPLY` 状态、`reply_timeout_ms`、`frame_handler` 入站回调；initiator 提交后切 SLAVE 等 reply，responder 从 LISTEN 直接回帧；坏帧/非 probe 帧交给 `frame_handler`（供 REFMEM binding 解析命令/回执）。
- **验证**：`run_refmem_bootstrap_relay_tests.ps1` 通过（未 ready 拒绝提交、ready 提交、reply handler 收到帧、reply 超时 reason、responder 提交）；ON 镜像 `DHRT100`/`DHRT100_B` 链接 exit 0，余量 **372B**。
- **未做**：固件 binding（envelope、`distributed_refmem_cmd_frame_receive`、reply 回送、超时 NACK）与 carrier 注册；未 HIL。
- **下一 gate**：binding + carrier 注册（带 timeout fail-closed），再做 ON 链接/文档。

### CONSOLE-PROGRESS-20261104-023：定向释放与 CRC 合并（ON 镜像恢复链接）

- **释放**：移除 SCPI `s_refmem_package_chunk[512]`；LOAD:SD 改为 `storage_manager_copy_file_read_job_result()` 直接写入目标 buffer，不再经 512B 静态中转。分块读取循环保留，行为等价；省 **512B 主 RAM**。
- **合并**：`tdma_origin_calibration_crc.c` 删除自带 16-entry residue 表与 kernel，改为调用 `tdma_transport_crc32_compute()`（同为 reflected CRC-32 / ISO-HDLC，transport kernel 仍是 SRAM 常驻）。省 64B `scratch_y` 与一套重复 kernel；数值等价由 `test_tdma_origin_calibration_crc.py`（3 passed）与 `run_tdma_transport_frame_tests.ps1` 证明。
- **构建**：`PROJECT_REFMEM_BOOTSTRAP_ENABLED=ON` + `pico2-v2-factory-candidate` → `DHRT100.elf` / `DHRT100_B.elf` 链接 exit 0；`.bss` 0x68a84，heap 结束 0x2007fe84，**余量 380B**。
- **未计入证据**：`test_tdma_origin_admission.py` 在编译期因既有 `ota_ao_tdma_maintenance_allowed` 隐式声明失败（与本合并无关），未用于本次证明；HIL/FULL 仍因两板 GPIO16-19 线序缺失未执行。
- **残余**：默认编译开关仍 OFF（功能未 HIL 验证）；SCPI SYNC 诊断桥（2252B）与 action probe（1432B）两个重复控制面未合并；RX DMA 1184B 的 4× 单位疑点未修。

### CONSOLE-PROGRESS-20261104-022：RAM 预算回退方案 B（编译开关默认 OFF）与镜像恢复

- **决策**：RAM 预算三选一未获 owner 决策，按上轮声明执行 **B**：新增 `PROJECT_REFMEM_BOOTSTRAP_ENABLED`（默认 OFF）。OFF 时三个 bootstrap 源文件不参与镜像、SCPI/核心接线编译为 fail-closed stub；ON 时恢复完整接线。
- **构建证据**：`pico2-v2-factory-candidate` + OFF → **DHRT100/DHRT100_B 链接成功**，产出 `DHRT100.elf`、`DHRT100_B.elf`、`DHRT100_V2_CANDIDATE_FACTORY.uf2`，`flash_link_contract=OK`，重复构建 exit 0。ON → 双板 6 个 bootstrap 对象编译通过，但 `DHRT100.elf` 仍 `overflowed by 132 bytes`——确认瓶颈是 RAM 预算，不是编译/逻辑。
- **验证**：`run_refmem_all_host_tests.ps1` 全 PASS；docs 250/0、38 doc tests。
- **残余**：ON 镜像仍差 132B（heap/.bss）；HIL 需刷机 + 两板 GPIO16-19 线序；carrier 未注册；无 FULL 凭证；未提交。

### CONSOLE-PROGRESS-20261104-021：M4 台架勘察与 RAM 预算阻塞

- **台架**：COM3=NO.3、COM4=NO.2、COM5=NO.1、COM6=NO.4，全部运行 build `20261103195548`；`SYSTem:REFMEM:SYNC:SPI:STATus?` 返回 `-113 Undefined header` → 运行固件不含本轮 bootstrap SCPI；当前是产品四板机架，P4.5 两板 GPIO16-19 SPI 线序未建立。故 HIL 前置 = 刷入新固件 + 恢复两板 SPI 线序，二者都不是软件可自证的。
- **构建**：对象编译双板通过；**整链失败**：`pico2-v2-factory-candidate` 溢出 **132 bytes**，`pico2-release`（v1_compat）溢出 4064 bytes。实测 v2 基线（03:55 成功构建）`.bss` 后仅剩 0 字节余量；本轮新对象 `.bss` = owner 149 + relay 88，SCPI 移除字段回收 116，净增 132 → 正好压垮。这不是逻辑错误，是**零余量的 RAM 预算墙**。
- **代码**：`refmem_cmd_load` 的两条 submit 路径改用 `refmem_control_transport_route()`（与单测同一决策函数），去掉重复的 ring/bootstrap 布尔组合。
- **待决**：RAM 预算需 owner 决策——(a) 在别处释放 ≥256B；(b) 给 bootstrap 加编译开关默认关，待预算批准再开；(c) 缩 heap（运行时风险）。
- **残余**：carrier 未注册；HIL 未做；无 P3/FULL；代码未提交。

### CONSOLE-PROGRESS-20261104-020：M3 失败 reason/交接与资源归属澄清

- **结果**：relay 增加 `last_reason`（BUILD_FAIL / TX_FAIL / ACK_TIMEOUT / INVALID_FRAME / PEER_TIMEOUT / RING_HANDOVER），并追加到 `BOOTstrap:STATus?`；core0 在 ring ready 时自动 `handover`（撤销 readiness + disarm adapter，计数与 reason 保留），ring stop 后不自动 re-arm；修正资源判断：REFTMEM SPI 在 `pio0`、TDMA ring 在 `pio1/pio2`，二者不共享 PIO 指令空间，真正冲突的是 `pio0` 上的 SYNC_IO 调试组。
- **验证**：relay host 用例新增 peer-timeout reason、handover reason、TX-fail reason；DHRT100/DHRT100_B 的 relay/link/scpi_system 对象编译 exit 0；`run_refmem_all_host_tests.ps1` 待本轮收口复跑。
- **残余**：carrier 未注册（命令 ACK 跟踪未做）；无两板 HIL/P3；产品台控制对未决；`pio0` 归还 SYNC_IO 的 program release 未做；代码未提交（firmware 需 FULL）。
- **下一 gate**：两板 HIL 验证 readiness/RX/TX 角色切换；或先做 carrier 的 ACK 跟踪。

### CONSOLE-PROGRESS-20261104-019：M2b HELLO/responder/core0 hook 与只读状态

- **结果**：新增 HELLO probe 绑定（`refmem_bootstrap_hello_*`，round-trip host 用例）；relay 增加 responder 模式（LISTEN → 收到对端 probe → guard → MASTER 回帧 → LISTEN）；`distributed_refmem_service()` 调用 relay；SCPI `SYNC:SPI:ARM` 成功后启动 relay、`DISarm` 停止；重新登记 `SYSTem:REFMEM:SYNC:SPI:ARM/DISarm/STATus?`，新增 `SYSTem:REFMEM:BOOTstrap:STATus?`（armed/ready/state/peer/tx/rx/timeout/retry/invalid/build_fail/tx_fail + owner 拒绝计数）。
- **验证**：`run_refmem_bootstrap_hello_tests.ps1`、`run_refmem_bootstrap_relay_tests.ps1`（含 responder/guard）、owner 与 035 policy 用例全绿；DHRT100/DHRT100_B 的 hello/relay/link/scpi_system/scpi_port 对象编译 exit 0。
- **残余**：carrier **未注册**（命令 ACK 跟踪未做，避免 POSTED 永久挂起）；无两板 HIL/P3；PIO 回收与 018 交接未做；代码未提交（firmware 需 FULL）。
- **下一 gate**：命令 ACK 跟踪 + carrier 注册，或先做两板 HIL 验证 readiness/TX/RX 角色切换。

### CONSOLE-PROGRESS-20261104-018：M2 relay 状态机（半双工 TX/RX 角色交替）

- **结果**：新增 `refmem_bootstrap_relay`：TX(MASTER)→RX(SLAVE) 交替、ack 超时/重试预算、peer 心跳超时撤销 readiness、坏帧/TX 失败计数；readiness 只由收到对端帧置位。owner 增加 `transmit/receive_begin/receive_poll` 包装（每次交换重新 arm 角色）。架构 §6.1 记录半双工约束：`transmit` 要求 MASTER、`receive_*` 要求 SLAVE，角色只在 arm 固定，因此握手是角色切换而非常驻双向连接。
- **验证**：`run_refmem_bootstrap_relay_tests.ps1` 通过（未 armed/成功/peer 超时/3 次超时+2 次重试后 backoff/坏帧/TX 失败预算）；`run_refmem_bootstrap_transport_tests.ps1` 通过；DHRT100 与 DHRT100_B 的 relay/owner 对象编译 exit 0。
- **残余**：relay 未接 core0（无 `distributed_refmem_service()` hook）、无 HELLO 绑定/对端 responder/guard、carrier 未注册、只读状态未暴露；无 HIL/P3；代码未提交（firmware 需 FULL）。
- **下一 gate**：M2b 集成——HELLO 绑定 + responder + core0 hook + carrier 注册 + 只读计数。

### CONSOLE-PROGRESS-20261104-017：M1 adapter 所有权迁移到 REFMEM bootstrap owner

- **结果**：新增 `refmem_bootstrap_transport` owner（arm/disarm/snapshot/armed/adapter 诊断入口 + 拒绝计数）；`refmem_spi_physical_adapter_t` 从 SCPI `scpi_refmem_sync_state_t` 移除，SCPI `SYNC:SPI:ARM/DISarm/STATus?`、raw/line 全部改走 owner；arm 现在带前置门：model/realtime idle 且 ring 未 ready，否则拒绝并给 `REFMEM_BOOTSTRAP_NOT_STOPPED` / `REFMEM_BOOTSTRAP_RING_READY`。
- **验证**：`tools/tests/run_refmem_bootstrap_transport_tests.ps1` 通过（NULL/未 STOP/ring ready/adapter 失败/成功/Snapshot/disarm/转发参数）；`ninja -C build` 编译 DHRT100 与 DHRT100_B 的 owner、`scpi_system_snapshot_commands.c`、`scpi_platform_snapshot_commands.c`、`scpi_node_snapshot_commands.c` 通过。
- **残余**：carrier 未注册、relay/逐 target 握手 readiness 未做；PIO 指令空间回收未做；无 HIL/P3；代码未提交（firmware 需 FULL）。
- **下一 gate**：M2 relay + readiness（注册 carrier、超时/重试、reason/计数）。

### CONSOLE-PROGRESS-20261104-016：建环前低速承载的设计冻结与准入层

- **结果**：架构 §6.1 改为复用 REFMEM `refmem_spi_physical` adapter（不新增 TDMA persona program），并写清 profile 边界：P4.5 最小系统/调试 profile 与产品 BiSS/RJ45 线序不是同一条，产品侧建环前控制面仍需 owner 决策。§6.3 新增命令面三层口径（本地 SCPI / REFMEM 类型+目录 / 目标 handler），修正 §6.2 把“本地已有”写成“缺失”的表述；TODO 017/018/035 与阻塞项同步；REFMEM TODO 新增 P4.7（建环前低速控制面）任务清单。
- **代码**：新增 control-transport 准入/路由（`refmem_control_transport_route/submit/admits` + carrier 注册/注销）；`distributed_refmem_ring_transport_ready()` 语义扩展为 ring 或 carrier，`distributed_refmem_ring_link_ready()` 保留 ring-only 供路由判优先级；`refmem_cmd_load` 的单帧与分片两条 submit 路径接入 carrier；carrier 未注册时行为与原 ring-only 一致。
- **验证**：`tools/tests/run_refmem_position_reset_policy_tests.ps1` 通过（ring/双无/carrier 未就绪/就绪/ring 优先/注销/回调参数）；`ninja -C build` 编译 DHRT100 与 DHRT100_B 的 `refmem_cmd_load.c`、`refmem_position_reset_policy.c`、`distributed_refmem_plane.c` 通过。
- **未做/残余**：carrier 本体（adapter 所有权迁移、逐 target 握手 readiness、core0 relay、PIO 指令空间回收、两板 HIL）未做；无 P3/台架证据；产品线序是否提供控制对未决；代码未提交（firmware 改动需覆盖当前指纹的 FULL 凭证）。
- **下一 gate**：CONSOLE-017 的 adapter 所有权迁移 + relay；CONSOLE-018 的 bootstrap→ring 交接顺序。

### CONSOLE-PROGRESS-20261102-015：M5 CONSOLE 范围收口

- **结果**：13 个页面全部通过 Router 注册并实现 `render(vector)`；`app.py` 无 `messagebox`/裸 `self.after`；OTA 走 `WorkflowSpec`；Store 含 Connection/Mask/Run/Ack/Ui 五 slice；Settings/ErrorBoundary/LogView/ThemeTokens/DPI 已接。
- **验证**：`test_trigger_debug_ui.py + test_trigger_debug_demo.py` 27 passed；UI smoke 断言 13 页均有 render；docs 250/0、38 doc tests。
- **残余**：全量仓库 `pytest tests/python` 首败为 `test_app_realtime_profile` 的 `profile.exe` exit 3221226505（非 CONSOLE 改动）；无 HIL/P3；TCP 活跃 handler 未 join；ACK 之外页面字段仍本地；CONSOLE 契约未登记；未提交。
- **判定**：CONSOLE 重构目标范围完成；全仓基线/HIL/契约作为后续硬化项，不写成本次已通过。

### CONSOLE-PROGRESS-20261102-014：M5 ACK slice 与 13 页 render 收口

- **结果**：`ConsoleUiVector` 新增 `AckSlice`；`AckPage.show_status/show_reply` 写 Store，`render` 从 Store 回填 seq/state/ACK/NACK/timeout/reason；UI smoke 断言 13 个页面均有 `render`，Control 通过 `control_holder.render` 代理。
- **验证**：targeted host 27 passed；`app.py` 无 `messagebox`/裸 `self.after`；docs 250/0、38 doc tests。
- **全量 host**：仍在 `test_app_realtime_profile` 的 `profile.exe` exit 3221226505 首败，未计为通过。
- **剩余**：全量 host 基线、HIL/P3、TCP 活跃 handler、其他页面字段 slice 化、契约登记。

### CONSOLE-PROGRESS-20261102-013：M5 页面全覆盖与 host 回归（部分）

- **结果**：剩余 9 个页面（Overview、5 个 FunctionPage、Control、Ota、Tcp）补齐 `render(vector)`；`FunctionPage` 负责 card 转发，`FunctionCard/Control/Ota/Tcp` 按连接态控制发送/读取按钮；13 个 tab 现在都通过 Router 订阅 Store。
- **生命周期**：`OtaPage` 的取消事件改名 `_cancel_event`，新增 `cancel/dispose`；`TcpPage` 新增 `render/cancel/dispose`；关闭时 Router 退订并调用页面生命周期。
- **验证**：相关 host 套件 `test_trigger_debug_ui.py + test_trigger_debug_demo.py` 27 passed；UI smoke 断言 13 个页面均有 `render`；`app.py` 无 `messagebox`/裸 `self.after`；docs 250/0、38 doc tests。
- **全量 host 未绿**：`pytest tests/python -q` 在 `test_app_realtime_profile.py` 因 `profile.exe` exit 3221226505 失败，与本次 CONSOLE 改动无关；未继续跑完，未把它计为通过。
- **剩余**：全量 host 基线、HIL/P3、契约登记、ACK 业务数据 slice 化。

### CONSOLE-PROGRESS-20261102-012：M4 Dialog/Settings/ErrorBoundary/WidgetKit 收口

- **结果**：`app.py` 的 17 处直接 `messagebox` 全部改为 `DialogService`（error/warning/info/confirm）；`ui/settings.py` 提供 schema 白名单、原子写、损坏备份 `.bad`；`ui/errors.py` 提供 ErrorBoundary + `report_callback_exception` 限流；`ui/widgets.py` 提供 ErrorBanner/LogView；`ui/theme.py` 提供主题 token 与 `apply_tk_scaling`；DemoApp 接入 ErrorBanner、LogView、Settings。
- **验证**：`app.py` 已无 `messagebox`/裸 `self.after`；settings/error 测试新增；demo+UI 27 passed。
- **剩余**：全量 HIL/P3、TCP 活跃 client handler join、契约登记；M5 处理。

### CONSOLE-PROGRESS-20261102-011：M3 ACK/回读/诊断/原始命令页面迁移

- **结果**：`PageRouter.register_legacy` 对实现 `render(vector)` 的页面订阅 `ConsoleStore`，页面关闭时退订；`AckPage`、`QueryPanel`、`DiagnosticsPage`、`RawPage` 增加 `render/cancel/dispose`，按 `ConnectionSlice.connected` 控制读取/发送按钮。
- **去裸 after**：`AckPage._auto_tick` 改用 `TkScheduler` 并保存 token；`OtaPage` 的 receipt 定时也改用 `app.scheduler`。当前 `app.py` 已无 `self.after(` 调用。
- **Dialog**：`QueryPanel` 参数错误改走 `app.dialogs.error`；`AckPage`/诊断/原始命令无 `messagebox`。
- **测试**：新增 Router→Store→render 用例；UI smoke 断言连接后 ACK/raw 按钮可用；demo+UI 25 passed。
- **剩余**：`FunctionCard`/`ControlPage`/`OtaPage`/`TcpPage` 仍有 17 处直接 `messagebox`，留给 M4 WidgetKit/DialogService 收口。

### CONSOLE-PROGRESS-20261102-010：M2b OTA 迁移为可恢复 WorkflowSpec

- **结果**：新增 `ui/workflow.py`（WorkflowSpec/StepSpec/WorkflowState/WorkflowRunner）与 `ota_workflow.py`（snapshot→begin→stream→end→wait→receipt 六步）。`OtaPage.start` 不再启动 ad-hoc `_run` 线程，改为 `OtaWorkflow.start`；`_run` 仅保留兼容 wrapper；`abort` 同时 cancel workflow；新增“恢复上次”按钮，按 step cursor 恢复；`stream` 用 `chunk_index` checkpoint，并在恢复前用 `PROGress?` 校验偏移。
- **安全边界**：`begin`/`end`/`receipt` 标记 `resume_safe=False`；`stream` 可恢复但设备偏移不一致直接 fail-closed；state 写 `out/workflows/ota.conventional-<run_id>.json`，版本/id 不匹配拒绝加载。
- **测试**：`tests/python/test_trigger_debug_ui.py` 增加 WorkflowRunner 持久化/恢复/拒绝 unsafe resume/cancel；`test_trigger_debug_demo.py` 增加 OTA workflow 六步/凭证/2 块集成用例与按 chunk cursor 恢复用例；当前 demo+UI 24 passed。
- **未做**：M3 页面迁移、M4 WidgetKit、M5 HIL/收口；OTA 的 `begin`/`end` 失败仍需从头重跑（无 durable offset）。

### CONSOLE-PROGRESS-20261102-009：M2a TCP 停止移出 UI 线程

- **结果**：`device.TcpBridge` 增加 `stop_async()` / `wait_stopped()` / `_stop_server()`：先 detach server 拒绝新客户端，再在后台线程执行 `shutdown/server_close`；`TcpPage.stop()` 改为异步 + `TkScheduler` 轮询完成，UI 不再直接 `shutdown()`；`disconnect()` 用 `stop(wait=True)` 在断开 transport 前等待桥接退出。
- **测试**：`tests/python/test_trigger_debug_ui.py` + `test_trigger_debug_demo.py` → 19 passed；`py_compile app.py/device.py` OK。
- **剩余**：`TcpBridge` 的活跃 client handler 仍是 daemon 线程，未追踪 join；OTA 仍是 ad-hoc 线程（M2b）。

### CONSOLE-PROGRESS-20261102-008：Demo 按 Tk 架构完成 strangler 底座接入

- **结果**：新增 `tools/domains/trigger/trigger_debug_demo/ui/`：`state.py`（四 slice Store）、`scheduler.py`（Tk/Fake）、`dialogs.py`（DialogPort）、`page.py`（Page/NavigationDecision/LegacyPageAdapter）、`router.py`（Notebook Router + guard）、`pump.py`（有界 UiPump/PumpSink）。
- **接入**：`DemoApp` 使用 Store/Router/TkScheduler/TkDialogService/UiPump；13 个现有 tab 通过 `LegacyPageAdapter` 注册；`results/tcp/ota` 事件改为 pump；`Dispatcher` 结果由 `_on_result` 投递；`disconnect` 增加 `join(timeout)`；OTA 独占期非 OTA 命令改为静默丢弃 + status，不再弹模态。
- **未做**：没有重写 13 个 legacy Page 内部逻辑；`OtaPage` 仍是 ad-hoc 线程；`TcpBridge.shutdown` 仍在 UI 线程（legacy）；没有 implement WorkflowRunner/REFMEM/T2/TDMA/DPLL/校准。
- **测试**：新增 `tests/python/test_trigger_debug_ui.py`，覆盖 Store 四 slice/revision/退订、FakeScheduler、UiPump 有界/terminal 保留、PumpSink、Router guard、懒加载 Page；demo+UI 共 19 passed。
- **门禁**：`python -m pytest tests/python/test_trigger_debug_ui.py tests/python/test_trigger_debug_demo.py -q` → 19 passed；`python -m py_compile` app+ui → OK；`docs_check --strict-names` → passed（files=250 warnings=0）；`doc_regression_check` → passed。

### CONSOLE-PROGRESS-20261102-007：三次独立评审后的架构收敛

- **评审 1（架构一致性）**：指出单端口远程 REFMEM 与 `refmem_control_submit.c` 的 ring-ready gate 矛盾；REFMEM 目录已覆盖 `LOAD`/`LEASE`/`DPLL`/`VDC OUTPut`/`T2`，文档“未覆盖”表述过宽；`MODULES` 不是页面事实源；Workflow 与 OTA ad-hoc 缺现行例外；候选契约被提前当验收条款。
- **评审 2（Tk 工程）**：指出关闭/取消链、Page/Store 契约、OTA 单端口独占期三个 BLOCKER；`UiPump` 无界、after 无所有权、Fake 无注入点、`can_close` bool/reason 冲突；建议首版不引入 ttkbootstrap、删除 snapshot/restore/图片等过度设计。
- **评审 3（文档治理）**：指出主架构曾引用不存在的 CONSOLE_TODO.md（已改为 `docs/console/CONSOLE_DOMAIN_TODO.md`）、`docs_check` 裸文件名盲区、README 任务范围过期、Related 缺 TK、目标树缺 `console/`、P0 与 C0–C4 缺映射、进展缺门禁凭证。
- **已修**：§6 增加“单端口可用前提”并明确 ring 未 ready 只能本地/维护多端口；§6.2 增加退出标准并按 catalog 行号纠正阻塞；§6.1 补 `STOPPED` 与“新增低速 persona program”；§7.6 增加现行例外表；§7.8/7.9/§10 标注候选契约；Tk 文档补 Page `render/cancel/dispose`、四 slice、Scheduler/ControlPlane/Dialog 注入、关闭时序、OTA 独占期、UiPolicy、设置原子写；TODO 新增 033–036，README/索引/阶段映射对齐。
- **未修**：`docs_check` 裸文件名死链检查（CONSOLE-036，需工具改动 + host 验收）；CONSOLE-002/003/004/035 仍未登记/未落地；代码仍为 legacy，未改 Tk 实现。
- **门禁**：`python tools/docs_check/docs_check.py --strict-names` → passed（files=250 warnings=0）；`python tools/doc_regression_check.py` → passed；`python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -q` → 38 passed。

### CONSOLE-PROGRESS-20261102-006：Tk 应用框架写入架构，接上主架构/索引/TODO

- **结果**：新增 `docs/console/CONSOLE_TK_ARCHITECTURE.md`，定义 Shell/Router/Page/Store/Intent/UiPump/Dialog/WidgetKit/ErrorBoundary 分层与契约；`CONSOLE_ARCHITECTURE.md` 新增 §11“Tk 应用框架”摘要并链接详情。
- **索引**：`docs/console/README.md`、`docs/README.md`、`docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md` 已加入该 canonical；TODO 新增 `CONSOLE-028..032`（Shell/Store/UiPump/Dialog/测试打包）。
- **口径**：Tk 层采用 MVP/Passive View + Redux-lite Store/Intent + Hexagonal ControlPlane + Statechart/Saga workflow；不引入完整 MVVM/Redux/Temporal 运行时。
- **现状**：当前 Demo 仍是 legacy（Page 直接继承 `ttk.Frame`、状态散在 Tk 变量、直接 `messagebox`、OTA ad-hoc 线程）；本次只冻结框架定义，未改代码。
- **门禁**：`python tools/docs_check/docs_check.py --strict-names` → passed（files=250 warnings=0）；`python tools/doc_regression_check.py` → passed；`python -m pytest tests/python/test_doc_regression.py tests/python/test_docs_check.py -q` → 38 passed。
- **下一步**：先落 `CONSOLE-028/029`（Page/Router + Store/Intent），再 `CONSOLE-030`（UiPump），最后迁移 OTA/Workflow 页面。

### CONSOLE-PROGRESS-20261102-005：单端口改为最终目标，多端口降级为过渡路径

- **结果**：`CONSOLE_ARCHITECTURE.md` §6 改为“单端口目标与过渡期路径”，新增 §6.2 单端口路径优化登记（REFMEM/T2/TDMA/DPLL/校准/OTA 的当前→目标 transport、阻塞、迁移任务）；§7.4/7.5/§9/§10 统一改为“目标单端口，过渡期多端口”。
- **TODO**：新增 `CONSOLE-026`（单端口路径登记）与 `CONSOLE-027`（过渡多端口退出/UI 标识）；原有阻塞项改为“单端口尚未收敛”。
- **口径**：多端口不是产品模式；默认 workflow 的 `target_transport` 必须是 `single_port_refmem`，多端口仅允许显式维护模式并在 wrapper 通过后退役。
- **现状**：常规 OTA 已是单端口顺序；T2/TDMA/DPLL/校准/REFMEM 装载仍是过渡多端口（或混合），尚未收敛。
- **下一步**：按 §6.2 逐项补 typed REFMEM command/table package，先把 REFMEM 装载与 T2 表安装收敛到单端口。

### CONSOLE-PROGRESS-20261102-004：Workflow 定义、步骤模型与能力范围写入架构

- **结果**：`CONSOLE_ARCHITECTURE.md` §7 扩充为完整定义：§7.2 形式与定义（Spec→Planner→Runner→State→Evidence→视图）、§7.3 步骤模型/状态机、§7.4 能做/不能做/与命令卡/宏/脚本/验收工具的边界、§7.5 各流程能力矩阵、§7.6 L0–L3 成熟度与 v1 线性边界、§7.7 待定设计选择。
- **明确约束**：workflow 必须声明 transport（单端口 REFMEM / 维护多端口 / 离线）；v1 不做 parallel、不做超过 1 层嵌套；OTA 是第一迁移对象，T2 第二。
- **未实现**：当前 OTA 仍是 ad-hoc 线程，T2/TDMA/DPLL/校准无 workflow；本次仅冻结定义与边界。
- **下一步**：按 `CONSOLE-006/019` 落 WorkflowPolicy/Spec/Runner，再迁移 OTA。

### CONSOLE-PROGRESS-20261102-003：控制台约束——长期流程必须 workflow 化

- **结果**：`CONSOLE_ARCHITECTURE.md` 新增 §7“工作流约束（Workflow-first）”，定义 WorkflowSpec/Step/运行语义/证据/UI/禁止项；明确 OTA、T2、TDMA 启环、DPLL/VDC、校准训练、REFMEM 装载必须走 workflow，不得以“按钮 + 线程 + 内存状态”实现。
- **新增任务**：`CONSOLE-019` WorkflowRunner + 持久化/恢复、`CONSOLE-020` OTA 迁移为首个 workflow、`CONSOLE-021..024` T2/TDMA/DPLL/校准 workflow、`CONSOLE-025` 版本矩阵。
- **现状差距**：当前 OTA 仍是 `OtaPage` 内的 ad-hoc 线程，T2/TDMA/DPLL/校准没有 workflow；因此本约束是**目标态**，不是当前已满足项。
- **下一步**：先落 WorkflowPolicy/WorkflowSpec 静态定义与 WorkflowRunner，再把 OTA 迁移为第一个可恢复 workflow，最后复制到 T2/TDMA/DPLL/校准。

### CONSOLE-PROGRESS-20261102-002：架构决定——校准/TDMA 前用低速 SPI PIO 承载 REFMEM 控制面

- **结果**：在 `CONSOLE_ARCHITECTURE.md` §6.1 增加“引导期低速 SPI PIO（bootstrap persona）”链路：STOP 态热加载低速 SPI persona，先用它承载 REFMEM command/ACK/table/status，完成校准与拓扑/时钟准备后，再通过 persona FSM（QUIESCING → UNLOADING → LOADING → ACTIVE）切到高速 TDMA persona；失败走 ROLLING_BACK 回低速恢复。
- **边界**：仍是同一组 SPI 引脚；不是第二总线/第二命令槽/第二 ACK；REFMEM 的 CRC/seq/fence/quality/ACK 语义不变；只在 STOP/维护边界热加载，RUN 中禁止。
- **未实现**：低速速率、PIO 指令字数、DMA/SM 预算和切换窗口尚未核算；已登记 `CONSOLE-017`（bootstrap persona）与 `CONSOLE-018`（高速交接/回滚），当前只是架构决定，不是已验证事实。
- **下一步**：先做 persona 状态/资源核算与 host 模型，再进台架验证切换与回滚。

### CONSOLE-PROGRESS-20261102-001：CONSOLE 主域立项，调试 Demo 成为首个交付物

- **结果**：新增 `docs/console/` 主域三件套与 README；`CONSOLE` 加入文档命名白名单；`docs/README.md` 增加域入口与 canonical 行。调试 Demo 定位为本域首个交付物，当前仍在原路径，迁移列为 `CONSOLE-015`。
- **交付物**：`tools/domains/trigger/trigger_debug_demo/`
  - 单端口 GUI + 本地 TCP 共用同一 Dispatcher；
  - 串口 / USB TMC（PyVISA）/ 模拟器三种 transport；
  - REFMEM 统一控制面与应用层 SCPI 两条通道；
  - 常规单板 OTA（BEGIN/PBEGIN/DATA/END/BOOT/COMMIT）与自动凭证；
  - 板卡 ACK、诊断、原始命令页。
- **证据**：
  - host：`python -m pytest tests/python/test_trigger_debug_demo.py -q -p no:cacheprovider` 全绿（快照，非事实源）；
  - 模拟 OTA：`out/ota_receipts/` 自动生成 JSON + `.sha256`；
  - 截图：`out/trigger_debug_demo.png`、`out/trigger_debug_demo_ota.png`、`out/trigger_debug_demo_tcp.png`；
  - 注册：`TOOL-CONSOLE-DEBUG-DEMO`（已从 `TOOL-TRIGGER-DEBUG-DEMO` 改名；路径迁移见 CONSOLE-015）。
- **未覆盖**：REFMEM 装载/表/质量页、T2/TDMA/DPLL/校准四条流程、安全门、HIL/台架证据；见 TODO 的 CONSOLE-005..016。
- **下一 gate**：C1 底座——先做 REFMEM 页 + FlowRunner + receipt 规范 + 安全门；在此之前不迁移路径、不宣称主域完成。
