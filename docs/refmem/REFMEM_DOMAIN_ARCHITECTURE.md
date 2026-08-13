# Distributed RefMem 内部主域架构

Status: Active
Domain: REFMEM
Canonical: `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`, `docs/refmem/REFMEM_DOMAIN_TODO.md`, `docs/refmem/REFMEM_TASK_PROGRESS.md`, `docs/interface/DTC100_SCPI_COMMAND_PLANNING.md`
Last updated: 2026-08-13

本文档定义 DTC100 / RP2350_TRIG 在 HAOFV 下的 Distributed Vector Blackboard / RefMem Sync 内部主域。RefMem Domain 不是对外 SCPI 主域，也不是产品业务动作域，而是分布式系统的内部基础主域，负责把多节点共同事实、静态分布式应用模型、命令意图、ACK/NACK、版本、质量和证据组织成可验证的数据面。

## 主域定位

RefMem Domain 的正式定位：

```text
Distributed Vector Blackboard / RefMem Sync Domain
```

工程内部简称：

```text
RefMem Domain
```

它回答的问题是：

```text
分布式系统中，所有节点对当前系统事实的共同认知是什么。
```

它不回答：

```text
产品测试流程下一步做什么。
硬实时边沿什么时候输出。
某个 SCPI 指令的业务语义如何解析。
```

## 职责边界

RefMem Domain 负责：

- 维护 64 KB `DistributedVectorTable`。
- 维护静态分布式应用模型：
  - `DistributedApplicationMap`
  - `DistributedFbInstanceTable`
  - `DistributedEventLinkTable`
  - `DistributedDataLinkTable`
  - `DistributedDeploymentGate`
  - `DistributedConnectionQualityTable`
- 管理 slot owner / writer / reader。
- 管理 command slot / ACK / NACK / busy / timeout。
- 管理 slot seq / CRC / stale / version / dirty。
- 承接跨节点 `REFMEM_DELTA` / `REFMEM_EPOCH` 同步。
- 给 SCPI/UI/System Pack 提供 snapshot。
- 给 ConfigGate / RUN gate 提供部署一致性判断。
- 给 Diagnostics / Report 提供 evidence。

RefMem Domain 不负责：

- 不执行业务动作。
- 不驱动硬实时边沿。
- 不解析产品命令语义。
- 不传输 OTA payload、日志全文、波形、SD 文件内容或硬实时边沿。
- 不引入完整 IEC 61499 分布式运行时。
- 不支持运行时动态部署 FB、跨节点 FB 直接调用或动态事件路由。

## HAOFV 层级

RefMem Domain 位于 HAOFV 的内部基础架构层：

```text
SCPI / UI / System Pack
        ↓
SystemAO / ConfigGate / Domain AO
        ↓
Distributed RefMem Domain
        ↓
Domain Vector / Slot / RefMem Sync
        ↓
RJ45_SYNC_RING / local shared memory
```

和业务域的关系：

```text
TRIGger / LoopEngine / CALibration / SYNC / MEASure
        ↓ publish/read facts
Distributed RefMem Domain
        ↓ sync small delta
Other nodes
```

## 静态分布式模型

RefMem Domain 吸收 IEC 61499-style 分布式运行时的优点，但保留静态、可验证、产品化的实现方式。

| 借鉴点 | RefMem Domain 落地形式 | 不采用的部分 |
|---|---|---|
| Application model | 静态 `DistributedApplicationMap`，描述 A0/A1/A2/A3、模型节点、网分、转台、网关节点。 | 运行时动态部署 application。 |
| FB instance model | 静态 `DistributedFbInstanceTable`，描述每个节点上的 AO/FB 实例、版本、role 和 enable 条件。 | 跨节点动态创建/销毁 FB。 |
| Event connection | 静态 `DistributedEventLinkTable`，把 START、STOP、FIRE_LOAD、DONE、FAULT、ACK/NACK 映射为 command slot、event queue 或 RJ45 frame。 | 跨节点直接事件调用和动态路由。 |
| Data connection | 静态 `DistributedDataLinkTable`，把状态、参数、质量、时间戳、T2 和统计量映射到固定 slot 字段。 | 任意远程变量读写。 |
| Deployment consistency | `DistributedDeploymentGate` 聚合 build id、hw profile、config CRC、calibration CRC、sync profile CRC 和 layout version。 | 在线热替换部署。 |
| Diagnostics | `DistributedConnectionQualityTable` 记录 seq、CRC、stale、late、drop、timeout、last_error 和 evidence index。 | 依赖外部 IEC 工具链诊断。 |

## 核心数据面

首版 64 KB 表保持 RTOS 架构中的完整布局：

| 区域 | 建议大小 | 内容 | 写入者 |
|---|---:|---|---|
| Header/Directory | 1 KB | magic、layout、slot offset、table_seq、epoch、crc32 | RefMem Domain |
| SystemSlot | 1 KB | system_mode、role_map_version、run_id、fault_latch、release gate | SystemAO |
| Role/ConfigSlot | 2 KB | NodeRoleMap、hw_profile、persona、feature mask | SystemAO / config loader |
| VdcSlot | 2 KB | sync_id、offset、rate、lock_state、holdover、relock、`e_vdc` | VdcSyncAO |
| LoopSlot | 4 KB | trigger param、angle sweep/breakpoint、active sequence、scan_index | LoopEngineAO |
| DpllSlot | 2 KB | compare 捕获、角度预测、`T_fire_base`、`e_pll` | AngleDpll owner |
| NodeSlot[8] | 4 KB | node_id、role、heartbeat、local_state、error_code、stale_count | 各节点 owner |
| TriggerSlot[8] | 8 KB | armed、last_fire_seq、late_count、t2_count、ready_timeout | 各节点 core1 摘要 |
| IoSlot[8] | 8 KB | SMA/RJ45/BiSS IO 镜像、边沿计数、健康状态 | 各节点 IO owner |
| CalibrationSlot | 8 KB | link table、delay table、staging/active/version/quality | CalibrationAO |
| StatisticsSlot | 8 KB | `e_vdc/e_act/e_pll`、CRC/seq/late 分布、p99/p999 | Statistics / Measure owner |
| AckCommandSlot | 4 KB | command_seq、ack/nack/busy/timeout 位图、原子命令槽 | 命令 owner + 节点 ACK |
| FaultEvidenceSlot | 6 KB | fault_code、source_node、epoch、run_id、关键证据 | SystemAO / DiagnosticsAO |
| GatewaySlot | 2 KB | A3/VNA/host 状态、采集状态 | GatewayAO |
| OtaStorageUiSlot | 2 KB | OTA、Storage、UI 摘要 | 对应 task owner |
| TlvExtension | 2 KB | versioned TLV、未来扩展 | owner by type |

## 对外接口边界

RefMem 不建立裸顶级 `REFMEM` SCPI 域。对外维护查询归 `SYSTem:REFMEM:*`：

```text
SYSTem:REFMEM:STATus?
SYSTem:REFMEM:NODE?
```

SCPI callback 只能读取 RefMem snapshot 或写 command/config slot，不能临时触发跨板查询，也不能直接修改 state、summary、result、health、quality 或 evidence slot。

## 当前实现现状

当前代码中 `components/distributed_refmem/` 仍是组件骨架：

- `distributed_refmem.h`
- `distributed_refmem.c`

它已经维护本地 64 KB `DistributedVectorTable`、header、node slot、core vector 和 runtime protection snapshot，但尚未形成完整 RefMem Domain owner，也未拆出 application model、event link、data link、deployment gate、connection quality、sync protocol 和 command ACK 子模块。

## 目标代码形态

后续建议收敛为：

```text
components/distributed_refmem/
  CMakeLists.txt
  inc/
    refmem_domain.h
    refmem_vector_table.h
    refmem_application_model.h
    refmem_sync.h
    refmem_command.h
    refmem_quality.h
  src/
    refmem_domain.c
    refmem_vector_table.c
    refmem_application_model.c
    refmem_sync.c
    refmem_command.c
    refmem_quality.c
```

旧 `distributed_refmem.h/.c` 可以在过渡期保留为兼容 wrapper，最终收敛到 `refmem_domain_*` 和 `refmem_vector_table_*`。
