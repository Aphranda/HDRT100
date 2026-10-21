# 接口域架构

Status: Draft
Domain: INTERFACE
Canonical: `docs/interface/SCPI_HOST_ARCHITECTURE.md`
Related: `docs/interface/SCPI_HOST_TODO.md`, `docs/interface/SCPI_HOST_TASK_PROGRESS.md`, `docs/interface/SCPI_HAOFV_REVIEW.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-20

## 域职责

接口域定义**上位机与多板系统之间**的通信边界：物理链路（串口）、会话与寻址、命令与事件的语义，
以及“**单串口 + 其余板卡经 RefMem 分布式控制**”拓扑下的转发与应答规则。
本域**不拥有板内实时行为**（属 TRIGGER/VDC/TDMA 等域），只规定跨边界的契约。

## 拓扑（用户指定 2026-10-13）

后续**只有一路串口连接 PC**；其余板卡通过 **RefMem** 进行分布式控制
（见 `docs/refmem/REFMEM_DOMAIN_ARCHITECTURE.md`）。接 PC 的板卡承担 reference/forward 角色
（词汇见 `components/distributed_refmem/inc/refmem_application_model.h` 的 “reference or forward node”）。

## 待冻结（详见 TODO）

1. **会话与寻址**：命令如何指定目标节点；
2. **转发与应答**：参考节点如何代转发、如何回告结果；
3. **隔离与权限**：哪些命令允许跨板、哪些仅本板；
4. **与既有机制的关系**：`stage_node_load` 与 `build|apply_node_load_sync_frame` 的复用边界。

## 现状（实测 2026-10-13）

- SCPI 命令表：`middleware/scpi_port/src/scpi_port.c:321`；全仓**仅 4 条**命令带节点作用域；
- **未发现**通用命令转发/代理构造。

## 非目标

本域不定义板内命令实现，也不复制其它域的语义/状态/证据（C12）。

## 评审（2026-10-20）

HAOFV 符合性评审见 `docs/interface/SCPI_HAOFV_REVIEW.md`（非契约）。结论：域边界方向正确，
但维护/HIL 路径存在 3 项高、4 项中实现不符合项（回调内阻塞、直接硬件动作、空实现/死代码），
已转 `IF-005..IF-010` 跟踪。维护命令与产品命令同受 HAOFV 约束，**不设强操作例外**；本域暂不
据此冻结新契约。
