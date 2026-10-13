# 接口域任务进度

Status: Active
Domain: INTERFACE
Canonical: `docs/interface/SCPI_HOST_TASK_PROGRESS.md`
Related: `docs/interface/SCPI_HOST_ARCHITECTURE.md`, `docs/interface/SCPI_HOST_TODO.md`, `docs/check/DOCS_REGISTRY.md`
Last updated: 2026-10-13

## 当前 checkpoint

### 2026-10-13 建域与现状取证

- 用户指定拓扑：后续仅一路串口接 PC，其余板卡经 RefMem 分布式控制。
- 现状实测：SCPI 命令表位于 `middleware/scpi_port/src/scpi_port.c:321`；全仓仅 4 条命令带节点作用域
  （`scpi_cmd_refmem_node_q:218`、`scpi_cmd_refmem_load_node:531`、`scpi_cmd_config_role_q:5283`、`scpi_cmd_config_calibration_q:5349`）；
  未发现通用转发/代理构造。
- 既有可复用机制：`distributed_refmem_stage_node_load`、`distributed_refmem_build_node_load_sync_frame`
  （`components/distributed_refmem/inc/distributed_refmem.h:607`）、`apply_node_load_sync_payload`（`:617`）；
  角色词汇 “reference / forward node”（`refmem_application_model.h:611`）。
- 本域三件套建立（C12/C13）；语义/状态/证据分文件，互不复制。
