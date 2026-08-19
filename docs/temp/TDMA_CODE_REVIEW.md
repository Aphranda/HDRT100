# TDMA Flight / Ring Adapter 代码评审单（合并版）

Status: Active
Domain: Documentation / Review
Canonical: `docs/temp/TDMA_CODE_REVIEW.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/arch/HAOFV_ARCHITECTURE.md`
Last updated: 2026-08-19

> 评审对象: components/tdma（flight engine / pio spi ring adapter / flight fifo / runtime owner）
> 架构依据: docs/arch/HAOFV_ARCHITECTURE.md
> 日期: 2026-08-19
> 说明: 供 Codex 逐条修复。修复保持现有 SPSC 双核同步模式（RELEASE/ACQUIRE）不变，
>       修改后运行对应单元测试确认通过。

## P1 必须修

### 1. hop_limit 硬编码为 1，四板环回永远收不到反馈
- 文件: components/tdma/src/tdma_pio_spi_ring_adapter.c:5
- 原因: tdma_transport_frame.c:418 的 advance_hop 在 hop_count >= hop_limit 时拒绝转发；
  tdma_transport_frame.c:404 的 route() 同样在 hop_count >= hop_limit 时 DROP。
  2 板 loopback 可用，但 4 板环中帧在第 2 跳被丢弃，参考节点永远等不到 FEEDBACK，
  simultaneous_feedback_loop_evidence 恒为 0。
- 建议: hop_limit 从 ring config 派生（如 node_count - 1），不要用 #define 硬编码。
- 架构依据: HAOFV_ARCHITECTURE.md:117,338（TDMA 是 UP/DOWN ring runtime 唯一 owner，四板环是产品目标）

### 2. forward 路径 flight_engine_apply 失败被静默吞掉
- 文件: components/tdma/src/tdma_pio_spi_ring_adapter.c:382-414
- 原因: apply 返回 false（长度拒绝 / TX 不可用）时继续原样转发且不设置 last_error，
  环上会悄悄传输未被本地写入的旧图像，completion evidence 缺失。
- 建议: apply 失败时至少 set_error(FLIGHT_MAP_REJECT)，并考虑丢弃该帧而非转发陈旧图像。
- 架构依据: HAOFV_ARCHITECTURE.md:143（TDMA 发布环路 completion evidence 是唯一 owner 职责）

### 3. tdma_flight_engine_configure 无运行态保护，map 无 version/sequence
- 文件: components/tdma/src/tdma_flight_engine.c:34
- 原因: service 层 tdma_service.c:1143-1148 有 guard，但引擎原语本身允许在 active 时覆盖 map，
  core1 apply() 可能 torn read engine->map.segment[]。engine->map 是 core0 写 / core1 读的
  多字段共享事实，只有 configured/active 两个单标志，map 本身无 version。
- 建议: configure() 内部拒绝 active 态（if active return false）；map 增加 map_version，
  apply 时校验 version 一致性。
- 架构依据: HAOFV_ARCHITECTURE.md:140,879（跨核多字段事实必须 seqlock/双缓冲/version 机制）

### 4. ring adapter get_snapshot 裸读跨核多字段，无 seqlock/sequence
- 文件: components/tdma/src/tdma_pio_spi_ring_adapter.c:662-713
- 原因: 从 core0 裸读约 30 个 core1 写的字段（up_sequence/down_rx_sequence/tx_count/rx_count/
  last_error/reference/feedback timestamp 等），无 seqlock 也无 sequence。
  对比 flight engine / flight fifo 的 snapshot 都用了 __atomic_load_n，唯独 adapter 没做。
  单字段 32 位对齐读不会撕裂，但跨字段一致性（sequence 配对、counts 与 last_error 时序）无保证。
- 建议: 增加 snapshot_sequence（service 每轮更新前 ++，snapshot 双读比对）或按架构用双缓冲。
- 架构依据: HAOFV_ARCHITECTURE.md:879（多字段事实必须使用 seqlock、双缓冲或等价 sequence/version 机制）

## P2 应该修

### 5. FORWARD 节点写 reference_tx_timestamp_ns
- 文件: components/tdma/src/tdma_pio_spi_ring_adapter.c:439
- 原因: 头文件契约（tdma_pio_spi_ring_adapter.h:46-53）写明只有 REFERENCE 能产生反馈证据。
- 建议: FORWARD 节点保持该字段为 0。
- 架构依据: HAOFV_ARCHITECTURE.md:361-362（timestamp 相关性是 DPLL 证据唯一来源）

### 6. RX 注入队列非原子
- 文件: components/tdma/src/tdma_pio_spi_ring_adapter.c:16-71（queue_push/queue_pop）
- 原因: inject_rx（core0 测试注入）与 rx_once pop（core1 service）共享 head/count 非原子字段。
  单核测试无问题；若未来 core0 在环运行时注入即 data race。
- 建议: 头文件注释明确仅限单线程/测试线程使用，或加临界区。

### 7. RX 缺 up/down group-id 校验
- 文件: components/tdma/src/tdma_pio_spi_ring_adapter.c:304-314
- 原因: process_rx 只校验 schedule_crc32 和 ring_profile_crc32，未校验 up/down group id。
  误接线的板子（同 schedule 不同组）会被当成有效帧。
- 建议: 增加 up_group_id/down_group_id 匹配校验。
- 架构依据: HAOFV_ARCHITECTURE.md:365（TDMA 声明 UP/DOWN group 供 DeploymentGate 资源互斥）

## P3 建议

### 8. cycle_ns 是死代码
- 文件: components/tdma/src/tdma_pio_spi_ring_adapter.c:578-586
- 原因: 计算后 (void)cycle_ns; 实际节拍是 service_count & 1 分频，与 TDMA window/guard 语义未对齐。
- 建议: 删除或真正按 TDMA cycle 对齐，标注 bring-up 债务归属。

### 9. idle_beacon_tx_count 命名误导
- 文件: components/tdma/src/tdma_pio_spi_ring_adapter.c:281
- 原因: 带 flight payload 的帧也 +1。
- 建议: 改名 beacon_tx_count。

### 10. rx_mirror_drop_count 与 rx_publish_drop_count 冗余
- 文件: components/tdma/src/tdma_flight_fifo.c:204-206, 323
- 原因: 总是同时 +1 且 snapshot 中两者相等，语义重复。
- 建议: 留一个。

### 11. stop() 不重置生命周期计数器
- 文件: components/tdma/src/tdma_pio_spi_ring_adapter.c:207-229
- 原因: rx_drop_count/rx_bad_count/tx_count/rx_count 未重置，与 per-cycle 字段（up_sequence 等）语义混用。
- 建议: 若是生命周期累计计数，加注释说明。

### 12. runtime_owner_init 失败重试会二次 init
- 文件: components/tdma/src/tdma_runtime_owner.c:21-81
- 原因: FreeRTOS 路径失败后 slots 已释放但 adapter/service 可能部分初始化，重试时重复 init。
- 建议: init 失败路径增加已初始化状态清理或禁止重试。

### 13. 架构错误码空间表缺 TDMA 域段
- 文件: docs/arch/HAOFV_ARCHITECTURE.md:819-827
- 原因: 错误码空间只登记了 OTA/Trigger/Flash/Storage/UI/System，TDMA 错误码 0-6 未登记。
- 建议: 补登记 TDMA 域错误码段（如 600-699），并同步 docs/tdma/ 文档。

## 已验证无问题（不要动）

- SPSC 双核同步：flight fifo / engine 的 owner 状态机 + RELEASE/ACQUIRE 链正确
- TDMA_TRAFFIC_SCHEDULER_RUNTIME_SLOT_COUNT(8) <= SLOT_COUNT(32)，裸机静态数组无溢出
- service 层 configure guard 存在（tdma_service.c:1143-1148），当前生产路径不会被并发配置打穿
- flight engine apply 的边界检查（payload_size/output_capacity/segment 偏移）完整
- "诚实门"设计（无 phys_tx 返回 false、无硬件 timestamp 时 correlation 门关闭）符合
  HAOFV_ARCHITECTURE.md:361-362，保留

## 约束

- 修复保持现有 SPSC 双核同步模式（RELEASE/ACQUIRE）不变
- 跨核多字段事实按架构要求使用 seqlock/双缓冲/version
- 修改后运行对应单元测试确认通过
