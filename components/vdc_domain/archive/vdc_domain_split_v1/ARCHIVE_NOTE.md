# vdc_domain.c 拆分前审计副本（ARCHIVED）

Status: ARCHIVED / NOT BUILT

本目录保存 `components/vdc_domain/src/vdc_domain.c` 在 N5.0.4j 拆分前的完整副本，
用于审计与回退取证。不参与任何构建，不得删除。

## 2026-10-26：拆分前快照

- 源文件 4,460 行 / 138 个函数 / 0 个文件级静态对象（全部状态在 `vdc_domain_context_t`）
- 拆分点：第 1,489 与 2,974 行（`}` 后的空行，括号深度 0）

## 拆分边界（N5.0.4j）

| 单元 | 内容 | 行数 |
|---|---|---|
| `src/vdc_domain.c`（留） | 定点换算、门控/窗口几何、质量状态、样本接受与时钟更新、伺服数学 | 1,489 |
| `src/vdc_domain_evidence.c`（新） | 证据代次校验、帧包络/窗口校验、观测展开、分步证据应用 | 1,495 |
| `src/vdc_domain_profile.c`（新） | 默认档、CRC、剖面校验、路径表/字典、init/激活/发布 | 1,497 |
| `inc/vdc_domain_internal.h`（新） | 类型/宏 + 46 个跨单元 helper 原型 | 78 |

**零跨单元状态**：本文件没有文件级静态变量，拆分不产生任何状态双实例；46 个纯
helper 由 `static` 改为外部符号并经私有头共享（`VDC_DOMAIN_TIME_CRITICAL` 宏随
私有头发布，热路径函数仍在各自单元内）。
