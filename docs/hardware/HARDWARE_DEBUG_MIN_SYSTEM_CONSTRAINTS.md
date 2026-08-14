# 调试最小系统板硬件约束

Status: Draft
Domain: HARDWARE
Canonical: `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md`, `docs/sync/SYNC_IO_RESOURCE_PLAN.md`
Last updated: 2026-08-13

本文档定义当前正在运行和调试的最小系统板 / DEMO 板硬件约束。该板用于验证 HAOFV、RTOS、双核、SCPI、OTA、SD、基础触发和工具链闭环，不作为产品板 pin map 或隔离策略冻结依据。

> 当前约束口径：本文档只冻结“当前最小可运行系统”的软件调试边界。若某个外设或引脚只在
> 产品板网表 `Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel` 中存在，不能默认认为最小系统板已经具备。

## 定位

| 项目 | 约束 |
|---|---|
| 目标 | 尽快闭环软件架构、RTOS 任务划分、双核通信、SCPI 接口和基础触发模式。 |
| 当前身份 | 当前运行版本按最小系统约束管理。 |
| 允许 | 为调试便利使用临时 UART/USB/LED/LCD/按键映射。 |
| 禁止 | 把临时接线直接写入 HAOFV 顶层架构或产品板约束。 |
| 迁移原则 | 调试板差异必须通过 board profile、构建选项或兼容层隔离。 |

## 当前约束

- 默认构建目标仍以 `PICO_BOARD=pico2` 和当前最小系统/DEMO 板连线为主。
- USB/CDC/USBTMC、CH343、UART、LCD、按键、LED 可用于调试和验证脚本。
- 触发、SYNC、BiSS、SD、OTA 等功能优先验证软件路径和状态机稳定性。
- 调试板可以缺少最终产品隔离、电源、连接器、ESD 和完整分布式链路。

## 当前调试目标芯片与外设

| 类别 | 当前最小系统约束 | 用途 | 迁移注意 |
|---|---|---|---|
| MCU | RP2350 / Pico 2 类最小系统 | 固件运行、USB、PIO、双核和 RTOS bring-up | 产品板目标为 RP2350B QFN-80，pin map 不能直接照搬 Pico 2。 |
| USB | RP2350 原生 USB，当前固件可在 CDC/USBTMC 间切换 | SCPI、烧录验证、工具通信 | 产品接口最终以 USBTMC 为主，CDC 作为调试/兼容路径。 |
| 串口 | CH343/USB-UART 或外接 USB-UART | 早期 SCPI、log、脚本验证 | 每个脚本必须单 owner 管理串口生命周期。 |
| LCD | 当前最小系统 LCD/SPI 显示 | UI 卡死、heartbeat 和状态机可视化 | 产品板 LCD 为 HS096T01H13，SPI/IO 以产品板约束为准。 |
| 按键/LED | 临时 KEY/LED 映射 | 本地调试、故障和触发状态提示 | 产品板 KEY/LED 已有独立 IO 约束，不能从调试板反推。 |
| TF/SD | 当前固件已验证 SD/System Pack 路径 | StorageAO、System Pack、日志和报告证据 | 产品板 TF 使用独立 SPI1，最小系统可用软件模拟或不同接线。 |
| 触发 IO | 当前按可用 GPIO 做 loopback/smoke | SEQ_STEP、ARM/DISARM、基础状态机验证 | 产品板 SMA/RJ45/BiSS 方向和位序以 `RP2350B_QFN80_IO_CONSTRAINTS.md` 为准。 |

## 最小系统 IO 约束

最小系统板只要求保留以下软件验证能力，不冻结具体 GPIO：

| 能力 | 最小要求 | 验证目的 |
|---|---|---|
| USB SCPI | 至少一个稳定 USB 通道，可响应 `*IDN?`、`SYSTem:*?` 和产品 smoke 指令 | 验证 SCPI parser、任务拆分和 USB 传输。 |
| core1 heartbeat | 能查询 `SYST:CORE?` 或等价快照 | 验证双核启动和 core1 loop 活性。 |
| RTOS 任务状态 | 能查询 RTOS heap/stack/任务水位 | 验证任务划分和 128 KB heap 策略。 |
| Trigger smoke IO | 至少一组可回环或可观察触发输出/输入 | 验证 `TRIGger:MODE/STARt/STOP` 不死机和状态推进。 |
| Storage smoke | 有 TF/SD 或可替代的文件系统路径 | 验证 System Pack、日志、trace、snapshot 基础流程。 |
| OTA smoke | 能完成 package/status/commit 查询 | 验证 OTA 与 flash/boot 元数据路径。 |

## 双板 PIO 调试接线

当前双板最小系统验证采用 `debug_min_two_board_link` 临时接线 profile，只用于两块
最小系统板之间验证 RefMem/VDC/SlotClaim 和后续 PIO 快路径，不作为产品板 pin map。
该 profile 的输入/输出 GPIO 通过构建参数选择，默认值服务当前最方便接线的排针组合。

| 构建参数 | 默认值 | 约束 |
|---|---:|---|
| `PROJECT_SYNC_IO_INPUT_BASE_PIN` | `16` | 输入组为连续 4 位，默认 `GPIO16..19`。 |
| `PROJECT_SYNC_IO_OUTPUT_BASE_PIN` | `21` | 输出组为连续 4 位，默认 `GPIO21..24`。 |

可自定义规则：

- 输入组和输出组都必须是连续 4 个 GPIO，供 PIO `in pins,4` / `out pins,4` 使用。
- 输入组和输出组不得重叠；同一根线只能有一个确定的输出 owner。
- 调试接线应避开 SD/SPI、LCD、UART、SWD 和已接外设占用脚。
- 当前默认值可通过 CMake 覆盖，例如 `-DPROJECT_SYNC_IO_OUTPUT_BASE_PIN=20`。

默认接线示例如下：

| 方向 | 板 A | 板 B | 说明 |
|---|---|---|---|
| A -> B | `GPIO21` | `GPIO16` | `OUT0/TRIG_OUT` 到 `IN0/TRIG_IN`。 |
| A -> B | `GPIO22` | `GPIO17` | `OUT1/PULSE_OUT` 到 `IN1`。 |
| A -> B | `GPIO23` | `GPIO18` | `OUT2/MODE_OUT2` 到 `IN2/ENC_Z`。 |
| A -> B | `GPIO24` | `GPIO19` | `OUT3/RJ45_TRIG_OUT compat` 到 `IN3/RJ45_TRIG_IN`。 |
| B -> A | `GPIO21` | `GPIO16` | 反向同名语义交叉连接。 |
| B -> A | `GPIO22` | `GPIO17` | 反向同名语义交叉连接。 |
| B -> A | `GPIO23` | `GPIO18` | 反向同名语义交叉连接。 |
| B -> A | `GPIO24` | `GPIO19` | 反向同名语义交叉连接。 |
| 共地 | `GND` | `GND` | 两块板必须共地。 |

当前 COM3/COM4 双板调试实测线序不是直通顺序，按方向定义为：

| 方向 | 线序定义 |
|---|---|
| B0 -> B1 | `OUT0->IN1, OUT1->IN2, OUT2->IN0, OUT3->IN3` |
| B1 -> B0 | `OUT0->IN2, OUT1->IN1, OUT2->IN0, OUT3->IN3` |

该线序是调试 profile 的 logical remap，不改变 active output/input GPIO group 的物理
定义。若后续重接为直通线序，可用 `two_board_io_validate.py --expect-a-to-b 0,1,2,3
--expect-b-to-a 0,1,2,3` 进行验证。

调试规则：

- 当前固件的最小系统默认 profile 使用 `GPIO16..19` 作为 PIO 输入组，
  `GPIO21..24` 作为 PIO 输出组；如构建参数改变，接线必须随 active profile 改变。
- `GPIO12..15` 与 SD/SPI 调试资源存在冲突；双板直连验证时不得把它们作为主链路。
- 输出线建议串 `47R~100R` 调试电阻；在方向 ownership 未明确前，禁止把两块板的
  输出脚同名直连。
- 自动线序检测以 `tools/two_board_io_validate/two_board_io_validate.py` 为准；该工具
  支持方向性 logical remap，避免把临时线束顺序硬编码进产品 pin map。
- 产品板最终 PIO/SMA/RJ45/BiSS pin map 仍以产品板硬件约束和网表为准。

最小系统板不要求具备：

- ISO1452/ISO7740 真实隔离链路。
- 双 RJ45 环路、SMA 4 入 4 出完整通道。
- TPS259241、LMR33630、B0505MT、AMC1301 等产品级电源/测量链。
- GND/FGND 隔离装配选项、线缆侧 ESD/TVS 和最终连接器外壳策略。

## 调试板与产品板 board profile 差异

| 资源 | 调试最小系统 | 产品板目标 |
|---|---|---|
| MCU 封装 | Pico 2 / 模块化 RP2350 | RP2350B QFN-80 `U15` |
| Flash | 模块自带或板载 QSPI | W25Q128JVSIQ `U17` |
| USB | 原生 USB 或 USB-UART | USB1 原生 RP2350 USB，USB2 -> CH343P `U19` |
| UART/RS485 | 可用 USB-UART 或简化 UART | UART0 -> CH343P，UART1 -> MAX3485 `U21` |
| PIO trigger | 临时 loopback 或软件模拟 | SMA 4 输出 GPIO16..19，SMA 4 输入 GPIO20..23，RJ45 GPIO26/27 |
| BiSS/RS422 | 可先软件模拟 | ISO1452 `U1/U12/U14` + RJ45 `RJ1/RJ2` |
| SD | 可选 TF/SD | TF-01A `CARD1`，SPI1 |
| LCD | 当前 DEMO LCD | HS096T01H13 `U22`，SPI0 |
| 测量 | 可先关闭或模拟 | TMP235 `U23`，AMC1301 `U24`，ADC GPIO43/44 |
| 电源 | USB/调试供电优先 | DC 12 V、LMR33630、TPS259241、B0505MT、TPS7A2033 |

## 固件约束

- 最小系统 build 可以用 `board_debug_min` / `pico2` profile；产品板 build 必须使用独立 board profile。
- SCPI 指令表是对外接口，不直接暴露最小系统临时 GPIO。
- 底层实时验证入口应归 `REALtime:*` 或 validation alias，产品业务动作仍归 `TRIGger:*`。
- 未装配的产品硬件功能必须在 capability/snapshot 中返回 unavailable、simulated 或 disabled，不能假装真实链路已验证。
- 最小系统通过的软件 smoke 只能证明 AO/FB/Vector/SCPI/RTOS 路径，不证明产品板隔离、电源、ESD 和连接器可靠性。

## 与 HAOFV 的关系

- 调试板用于证明 AO/FB/Vector/Resource Arbiter 的软件闭环。
- 调试板不决定产品级 owner、事件边界、反射内存字段和 SCPI 指令树。
- 若调试板资源不足，优先做功能裁剪或软件模拟，不反向削弱产品架构。

## 与产品板的差异管理

| 差异类型 | 处理方式 |
|---|---|
| GPIO 或连接器不同 | 放入 board profile，不改顶层 HAOFV。 |
| 缺少隔离链路 | 使用软件模拟或维护模式标志，不能宣称产品隔离已验证。 |
| 缺少外部设备 | 通过 SCPI/tool smoke 验证内部状态机，产品联调另建验证记录。 |
| 资源数量不同 | 以产品板约束为最终目标，调试板只作为最小可运行集。 |

## 后续待补

- [ ] 固化当前 DEMO 板实际 pin map 和已验证外设清单。
- [ ] 在 board profile 中显式标记 `debug_min` 与 `product_qfn80` 的 capability 差异。
- [ ] 标记哪些测试只在调试板有效，哪些可迁移到产品板。
- [ ] 建立最小系统 smoke 与产品板 bring-up 验证矩阵的对应关系。
