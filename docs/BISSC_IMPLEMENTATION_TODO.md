# BiSS-C 实现 TODO

Status: Active
Domain: BISSC
Canonical: `docs/BISSC_IMPLEMENTATION_TODO.md`
Related: `docs/BISSC_TAP_BRIDGE_DESIGN.md`, `docs/BISSC_TASK_PROGRESS.md`, `docs/SYNC_IO_REFACTOR_PLAN.md`
Last updated: 2026-07-07

本文档跟踪 `docs/BISSC_TAP_BRIDGE_DESIGN.md` 之后的 `TRIG_PROTOCOL_BISS_C`
实现步骤。P0 有意收敛为固定 profile 的串联 TAP bridge：原样透传 BiSS-C
`CLK/DATA`，旁路接收 position/status，并按 crossing 输出触发。

## P0 - 固定 Profile TAP Bridge

### P0.1 协议契约

- [x] 新增 `biss_protocol.h/.c`，作为纯协议工具层，不访问 GPIO/PIO/SCPI。
- [x] 定义 `biss_profile_t`，包含帧长、position 字段、锚点、状态位、CRC 字段、采样相位、timeout 和 gate 策略。
- [x] 校验 profile 边界：bit offset、bit width、CRC 覆盖范围、modulo、sample delay 和 timeout。
- [x] 实现 BiSS CRC6 helper，支持可配置 invert/xor 处理。
- [x] 增加 host 单元测试，覆盖 CRC6 golden vector、非法 profile 和 bit 抽取。
- [x] 增加 crossing helper 测试，覆盖单次阈值 crossing 和 modulo 回绕。
- [x] 新增 `tools/run_biss_protocol_tests.ps1`，支持 host 执行和 ARM GCC 编译 fallback。

### P0.2 TriggerVector / ECC 接线

- [x] 按设计文档扩展 `trigger_vector_t` 的 P0 profile 字段。
- [x] 保持现有 enum 数值稳定：`TAP=0`、`SLAVE=1`、`MASTER=2`、`BRIDGE=3`。
- [x] 增加 position offset、modulo、sample edge、sample delay、anchor、status gate 和 latency offset 事件。
- [x] 更新 `fb_valid_biss_config()`，改为调用 `biss_profile_validate()`。
- [x] 确保 `BISS_ARMED` 状态拒绝所有运行期 profile mutation。
- [x] 保留现有软件 `TRIG_EVENT_BISS_FRAME_RX` 路径，仅作为管理面/测试注入。

### P0.3 SCPI / UI 接口

- [x] 为 P0 profile 字段增加 SCPI setter/getter。
- [x] 增加只读统计：frame、status gate、CRC late 和 timeout 计数。
- [x] `BISS_ARMED` 状态下修改 profile 时返回 busy/执行错误。
- [x] 文档化 `TRIG:BISS:ROLE` 的数值兼容关系：`0=TAP`、`1=SLAVE`、`2=MASTER`、`3=BRIDGE`。
- [x] 对 P1/P2 role 行为返回明确的 `not implemented`，不要静默假装支持。

### P0.4 资源所有权

- [x] 如果现有 PIO2-only 资源位过粗，为 BiSS AUX 使用增加 resource arbiter bit 或 owner tag。
- [x] 硬件冻结后关闭 `TRIG:ENC:APIN 26`；增量编码器固定使用 `SYNC_IO`
      `GPIO16/17/19`，AUX0..AUX3 保留给 BiSS/AUX persona。
- [x] 当 BiSS 占用相同 AUX 引脚时，拒绝 AUX framework 功能。
- [x] 在 DISARM、FAULT 和 RESET 时释放所有 BiSS 资源。

### P0.5 PIO 接收器 Bring-Up

- [x] 新增 `biss_tap_rx.pio` 骨架，实现 CLK edge wait、sample delay 和 DATA sample。
- [ ] 增加 TAP bridge 透传路径：`CLK_IN -> CLK_OUT`、`DATA_IN -> DATA_OUT`，
      要求固定延迟、无帧改写、可测量 skew。
- [x] 面向固定 1 MHz 模拟 profile，将 raw frame chunks 推入 RX FIFO，并在 C 侧解出 position/status。
- [x] 实现 anchor check 和 frame error 标记。
- [x] 实现帧间 timeout recovery。
- [x] 增加 1 MHz bring-up 用 sample delay scan mode。
- [x] ARM 前冻结选定的 `sample_edge/sample_delay_cycles`。

当前 `biss_tap_rx.pio` 已进入 CMake/pioasm 生成链；它提供 TAP RX 最小采样循环和
RX FIFO frame word 输出骨架，并已接入 `sync_io_biss_tap_*` 与 `biss_node_io` 的 arm/poll
路径；默认 48-bit 固定 profile 已具备 32-bit chunk 拼接、anchor/status/CRC gate 和
position crossing 决策。2026-07-07 闭环验证中修复了 PIO `PIO_FIFO_JOIN_RX` 与
`pio_sm_put_blocking()` seed 冲突导致 `TRIG:ARM` 卡死的问题；帧长 seed 保留在 OSR 中，
每帧复用，不再在采样循环里重复阻塞 `pull`。帧间 timeout recovery 会重置半帧 assembler；
启用 sample delay scan 后会在 timeout 后按配置范围推进 active delay 并重启 TAP PIO。

### P0.6 IRQ 快路径

- [x] 新增 `biss_node_io` 骨架，覆盖 PIO claim、init、arm、disarm 和 FIFO IRQ callback。
- [x] 接入 `crossed_position(last, current, target, modulo)` helper。
- [x] 使用 `frame_ok` 和 `status_gate_allows` 对 crossing 触发做 gate。
- [x] 通过现有 sync pulse primitive 输出 `TRIG_OUT`。
- [x] 仅在有效帧完成决策后更新 `last_position`。
- [x] 统计 frame error、status block、FIFO overflow、late CRC 和 timeout。

### P0.7 验证

- [x] Host 单元测试：CRC6、profile validation、bit extraction、crossing 和 modulo wrap。
- [x] 固件 smoke test：配置 TAP profile、ARM、软件帧 crossing、DISARM、查询统计；2026-07-07 使用 COM4 烧录 `build-biss-integration\RP2350_TRIG.elf` 后通过，结果归档在 `build\biss_validation_flash_loop_5`。
- [x] 单核 factory 闭环验证：2026-07-07 烧录 `build-biss-integration\RP2350_TRIG_FACTORY.uf2`，build id `20260707081355`；`SYST:CORE?` 返回 `0,9908,0,16184,0`，确认 core1 关闭；SEQ_STEP `TRIG:MODE 1 -> TRIG:ARM -> TRIG:DISA` 通过且 `SYST:ERR? -> 0,"No error"`；BiSS board smoke `python tools\biss_board_validate\biss_board_validate.py COM4 --out-dir build-biss-integration\biss_validation_singlecore` 通过，结果归档在 `build-biss-integration\biss_validation_singlecore`。
- [x] 增加 1 MHz CLK/DATA CSV 离线验证闭环：`tools/biss_wavegen/biss_wavegen.py` 生成 bench 向量，`tools/biss_wavegen/biss_wave_validate.py` 按 PIO 采样边沿重组 frame，校验 anchor、position、CRC 和 crossing。2026-07-07 默认 48-bit profile 与带 CRC6 的 40-bit profile 均通过，结果归档在 `build\biss_wavegen\biss_1mhz_validate.json` 和 `build\biss_wavegen\biss_crc_1mhz_validate.json`。
- [ ] 使用真实 PIO simulator 或逻辑发生器回放 1 MHz CLK/DATA CSV，确认板端 `STAT:BISS?` 的 rx frame、position 和 trigger count 与离线报告一致。
- [ ] 使用示波器在 5 MHz 下验证 sample window 和 `TRIG_OUT` latency。
- [ ] TAP 透明性测试：确认 `CLK_IN -> CLK_OUT`、`DATA_IN -> DATA_OUT` 原样透传，
      且本地 crossing 触发不会改写或阻塞 BiSS-C 原链路。
- [ ] 记录 P99 jitter 和 fixed latency offset。

板级 smoke 命令示例：

```powershell
python tools\biss_board_validate\biss_board_validate.py COM4 --enable-scan
```

## P1 - 可靠性、CRC Gate 和板间模式

- [ ] 增加 CRC late-check worker 和统计发布。
- [ ] 增加可选 CRC-blocking 路径：延迟触发直到 CRC 字段可用。
- [ ] 增加 5 MHz 长时间 soak 测试和 timeout storm fault 策略。
- [ ] 评估 10 MHz 固定 profile 接收器预算。
- [ ] 实现 `SLAVE_TX` event profile。
- [ ] 实现 `MASTER_RX` 最小单编码器读取器。
- [ ] 增加 event profile：`profile_id/seq_id/event_count/status/crc6`。
- [ ] 增加 BiSS profile 和测量 offset 的 SD/System Pack 持久化。
- [ ] 实现 `SELF_CAL_RING` 骨架，包含 round-trip 统计和 offset profile 写入。

## P2 - 产品化

- [ ] 按 `docs/BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md` 完成 RJ45 四对线和 SYNC_IO 外围电路原理图评审。
- [ ] 以 `THVD1452` 作为 P0/P1/P2 默认 RS-422/RS-485 收发器，满足串联 TAP bridge
      的接收和透传驱动需求；`AM26LV32E` 接收-only 只作为并联高阻监听夹具备选。
- [ ] 完成 0 ohm / 焊桥 / 跳帽矩阵，支持 BiSS-C full-duplex 与双路 RS485-HD 两种装配。
- [ ] 为 `DIFF0` / `DIFF1` / `TRIG_DIFF` 定义 100 ohm / 120 ohm 可选终端匹配。
- [ ] 为 RS485-HD 定义外部 bias / bypass 预留位，验证 idle、断线、短路和冲突发送状态。
- [ ] 完成 RJ45 12 V 输入限流、反接、TVS、滤波和 DC/DC 方案。
- [ ] 为长线或跨设备场景评估 TVS、共模扼流圈、屏蔽层接地、数字隔离和隔离电源。
- [ ] 为 `SYNC_IO` 本地高速脉冲口完成固定方向隔离原理图：完整 4 入 4 出版本使用
  两颗 `ISO6440F`，精简 2 入 2 出版本可降级为一颗 `ISO6442F`。
- [ ] 冻结 `SYNC_IO` 输入整形、输出驱动、默认低态、隔离电源和上电/掉电不误触发策略。
- [ ] 通过 1 MHz 回放和 5 MHz 示波器测试确认收发器传播延迟、透传 skew、
      sample window、TAP bridge 透明性和差分触发 latency。
- [ ] 通过脉冲发生器/示波器验证 `SYNC_IO` 的 `IN0..IN3` 捕获计数、`OUT0..OUT3`
  输出宽度、隔离延迟、通道 skew 和闭环 latency offset。
- [ ] 增加多编码器 profile 支持和慢速 device description 读取。
- [ ] 明确 Safety Profile 策略：不支持、软件检查或外部逻辑实现。
- [ ] 增加生产 HIL 测试：线缆长度、温漂和 EMI fault injection。
- [ ] 如果需求超过 RP2350 PIO 能力，决策 FPGA/CPLD/ASIC 接口。
- [ ] 冻结 SCPI 兼容性和 SD profile schema。
