# refmem_action_registry（已归档，不参与构建）

Status: Archived
Last updated: 2026-11-06

2026-11-05 C11 复核（`docs/check/submissions/ARCH_CONCEPT_DUPLICATION_CROSS_REVIEW_01.md`）判定：

- 统一控制面已用 `refmem_domain_config`（`command_type → handler`、词数校验、fail-closed）+
  `refmem_command_catalog` + `app_refmem_domain_config` 注册实现命令分发；
- `refmem_action_registry` 是第二套 `id → handler` 表，生产路径零调用，链接器 GC 丢弃；
- 其 `action_id`（Event Plane）路径未实现；`registry_crc` 无消费者；
- `DOCS_REGISTRY.md` 的 `REFMEM-ACTION-01` 已置 `superseded`。

本目录为原文件留档（`refmem_action_registry.[ch]`、其 host 测试与 run 脚本），**不得**重新接入
CMake、默认构建、验收输入或运行时配置。若将来确需异步动作分发，应基于
`refmem_domain_config`/catalog 扩展并新立任务，不要启用本目录。
