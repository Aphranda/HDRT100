# LOG 系统 TODO

Status: Active
Domain: LOG
Canonical: `docs/storage/LOG_SYSTEM_TODO.md`
Related: `docs/interface/SCPI_COMMANDS.md`, `docs/storage/SD_TODO.md`
Last updated: 2026-08-17

本文档跟踪 RP2350_TRIG 的统一 LOG/TRACE 体系。目标不是把所有信息都打印到 USB CDC，
而是分层保留调试证据：串口 LOG 给人快速观察，二进制 TRACE 给机器稳定解码，
SNAPSHOT/REPORT 给故障闭环归档。

## 验收标准摘要

| 优先级 | 验收标准 |
|---|---|
| P0 | portable_log 核心、RP2350 port、diagnostics 封装、等级过滤、统计、SCPI 控制和 host/ARM 单测可回归；板端验证记录必须包含 `SYST:LOG:STAT?` 与关键 fault trace 解码结果。 |
| P1 | domain/event id、域级过滤、限速、ring buffer service flush、StorageAO 文本落盘和验证工具统一收集形成闭环；OTA 传输期间日志策略不会干扰 binary block。 |
| P2 | release/validation/factory 默认等级、fault evidence bundle、trace schema 迁移、产测 HIL 模板、磨损评估和敏感信息策略进入发布门禁。 |

## 开源参考和本项目取舍

- `rxi/log.c`：借鉴“小型 C99 单文件核心、等级过滤、回调输出、可选锁”的形态，但本项目不直接引入文件指针、ANSI color 或桌面 stderr 依赖。
- Zephyr logging：借鉴 frontend/backend 分离、运行期过滤、按后端扩展、后续限速/延迟输出的演进方向；当前 P0 只实现同步文本输出，避免提前引入 RTOS 队列。
- Memfault embedded logging 实践：借鉴固定缓冲、保留机器可解码证据、故障包收集的思路；本项目把文本 LOG 和二进制 TRACE 分层，避免只依赖串口文本。
- 结论：LOG 核心放在 `third_party/portable_log`，保持纯 C、平台无关；`middleware/portable_log_port` 负责 RP2350 时间戳、USB CDC 输出和后续 backend/ring buffer 适配；`components/diagnostics` 只保留故障锁存、health heartbeat 和业务诊断封装。

## P0 - 可控串口日志和现有 Trace 收口

- [x] 梳理现状：`diagnostics` 负责 USB CDC 文本日志，`storage_manager_trace_event()` 负责 16B 二进制 trace ring 和 SD 落盘。
- [x] 抽出第三方库形态的 `third_party/portable_log`：核心只依赖 C 标准库和调用方提供的 buffer/time/emit 回调。
- [x] 增加 `middleware/portable_log_port` 中间层接口，承接 RP2350 时间戳、USB CDC 输出和 `portable_log` 实例生命周期。
- [x] 将 `components/diagnostics` 重构为诊断业务层，保持现有 `LOG_*` 宏和 SCPI 返回字段兼容。
- [x] 增加 `portable_log` host/ARM 编译单元测试，覆盖过滤、发出/丢弃计数和格式化输出。
- [x] 增加运行期日志等级过滤，默认 `INFO`，支持 `DEBUG/INFO/WARN/ERROR`。
- [x] 增加日志发出/丢弃计数，便于确认调试日志是否被过滤。
- [x] 增加 SCPI 控制面：`SYST:LOG:LEV`、`SYST:LOG:LEV?`、`SYST:LOG:STAT?`。
- [x] 将 LOG 等级名称、跨层等级映射和统计拷贝改为表驱动，符合 HAOFV “表负责规则、流程负责执行”的约束。
- [x] 完善 `portable_log` 长期演进基础：可选锁回调、emit 成功语义、截断计数、输出失败计数和边界单测。
- [x] 更新板端验证工具，在 smoke 开始前查询并记录 `SYST:LOG:STAT?`。
- [ ] 为 BiSS ARM/timeout/sample scan 增加可解码 trace 事件名，不新增硬实时热路径写入。
- [ ] 补齐 `sd_trace_decode.py` 中 BiSS trigger state/event 名称。
- [ ] 增加一条 fault trace 验证：故障证据必须能解码出最近一次 BiSS ARM 或 BiSS I/O 失败线索。

## 调试 LOG 使用规范

### 选择哪条观测路径

| 场景 | 使用 | 禁止 |
|---|---|---|
| 人工观察启动、配置、arm/disarm、初始化失败 | `LOG_INFO/WARN/ERROR` | 在 PIO/DMA/IRQ 热路径中打印。 |
| 需要机器稳定解码的状态变化、资源冲突、I/O 失败 | `storage_manager_trace_event()` | 只写文本 LOG 后依赖人工 grep。 |
| 故障闭环、现场复盘、发布验证证据 | snapshot + trace + report | 在 SCPI 回调或 IRQ 中直接写 FatFs。 |
| 高频运行态、采样窗口、计数器变化 | 计数器锁存 + 管理面周期 trace | 每次事件都输出 USB CDC 文本。 |

### 文本 LOG 规则

- 模块名使用稳定短名，例如 `app`、`sync_io`、`trigger`、`biss`、`storage`、`ota`、`sd`、`ui`。
- `DEBUG` 只用于临时 bring-up 或可长期保留但默认过滤的细节；release 默认不得依赖 `DEBUG` 才能判断健康状态。
- `INFO` 只记录低频生命周期事件：init ok、mode arm/disarm、配置 profile 切换、验证入口。
- `WARN` 记录可恢复异常：参数被拒绝、资源忙、队列接近满、超时后恢复。
- `ERROR` 记录进入 fault、初始化失败、不可恢复 I/O 失败；同一错误应有 trace 或 fault evidence 佐证。
- 日志文本保持单行、短字段、无大块十六进制 dump；需要二进制证据时写 trace 或受限 `MMEM:READ?`。
- 调试会话开始和结束应记录 `SYST:LOG:STAT?`，确认 filtered、truncated、emit_failed 和 queue_dropped 是否异常。

### 与 SCPI/OTA 共通道约束

- USB CDC 当前同时承载 SCPI 和文本 LOG；调试脚本必须忽略以 `[` 开头的 LOG 行。
- OTA binary block、长时间 SD 读写和产测自动化期间，建议把日志等级提高到 `WARN` 或 `ERROR`。
- 周期 heartbeat 类 LOG 必须受 `PROJECT_ENABLE_HEALTH_LOG` 和运行期等级过滤控制。

## P1 - 分域、限速和持久化

- [ ] 定义统一 domain/event id 分配表，避免 trigger/sync_io/storage/BiSS 事件号散落在源码里。
- [ ] 为 LOG 增加 module/domain 级开关：全局最小等级 + 单域覆盖。
- [ ] 增加周期日志限速策略，避免 health/debug 日志干扰 SCPI、OTA 和 binary block。
- [x] 将 `portable_log_port` 从同步直写演进为 ring buffer + service flush，缩短 critical section 并为多 backend 做准备。
- [x] 增加文本日志落盘后台 sink：`portable_log_port` 只维护 RAM 持久化队列，`StorageAO` 在显式 job 空闲时按阈值/周期分片写 SD；禁止在 SCPI 回调、core1、PIO/DMA/IRQ 中直接写 SD。
- [x] 将运行 sink 升级为 `/logs/runtime` 128 槽固定环、约 128 KiB 容量保护和原子 cursor；停止访问遗留 `/logs/run` 大目录。
- [ ] 增加按行切分以及 boot/run/fault 一致索引。
- [ ] 增加 ring buffer 快照命令：查询最近 N 条文本 LOG 或 trace 摘要，避免必须拔卡。
- [ ] 将板端验证工具统一收集 `queries.txt`、`log_status`、`trace_last`、`snapshot_last`。
- [ ] 为 OTA 传输期间定义静默策略：暂停周期 LOG，仅保留 ERROR 和二进制 trace 计数。

## P2 - 产品化观测和故障包

- [ ] 定义 release/validation/factory 三套日志默认等级和可用命令白名单。
- [ ] 增加 fault evidence bundle：snapshot + trace + report + log tail 的一致索引。
- [ ] 增加 trace schema 版本迁移策略和解码工具兼容性测试。
- [ ] 增加产测 HIL 日志模板：电源、SD、OTA、BiSS、SEQ、ENC 每项都有固定证据字段。
- [ ] 评估长期运行日志磨损：SD 写入频率、目录轮转、最大保留数量和低容量保护。
- [ ] 增加敏感信息策略：序列号、校准参数、用户 profile 在日志中的脱敏/截断规则。

## 实时边界

- 硬实时 PIO/DMA/IRQ 路径不得调用 `printf`、`LOG_*`、FatFs、StorageAO job 或阻塞式 trace 写入。
- IRQ 中只允许维护必要计数、锁存状态或投递极小事件；详细证据在管理面采样、DISARM 或 FAULT 后补齐。
- USB CDC 与 SCPI 共通道，调试日志必须可降级或关闭，OTA binary block 期间必须避免周期文本日志。
- `/logs/runtime` 运行日志落盘由 StorageAO 后台 service 执行；实时核心只允许通过已有状态/FIFO/Vector 摘要间接暴露诊断事实，不得格式化文本或访问 SD。

## HAOFV 约束

- LOG 底层遵循表驱动：等级名称、跨层等级映射、后续 backend 策略和 domain 过滤策略都应由静态表描述。
- LOG 流程代码只负责校验、查表、格式化和投递，不应散落 `switch`/`if` 决策树。
- `third_party/portable_log` 只保存平台无关规则；RP2350 输出通道、时间戳和后续多 backend 策略放在 `middleware/portable_log_port`。
- `StorageVector` 只发布 last log sequence、bytes、path hash、pending/drop/error 摘要；日志正文保存在 `/logs/runtime/*.log`，不得放入 Vector。
