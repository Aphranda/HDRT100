# refmem_application_model.c 拆分前审计副本（ARCHIVED）

Status: ARCHIVED / NOT BUILT

本目录保存 `components/distributed_refmem/src/refmem_application_model.c` 在
N5.0.4h 拆分前的完整副本，用于审计与回退取证。不参与任何构建，不得删除。

## 2026-10-26：拆分前快照

- 源文件 3,183 行 / 119 个函数 / 32 个跨单元对象（9 张默认表 + 活动/暂存视图 + 快照）
- git blob：379400f700a3ed9b0d84e54934b775adf4fe53a0
- 拆分点：第 1,059 与 2,113 行（`}` 后的空行，括号深度 0）

## 拆分边界（N5.0.4h）

| 单元 | 内容 | 行数 |
|---|---|---|
| `src/refmem_application_model.c`（留） | 默认表定义、表 CRC/视图、应用/初始化入口 | 1,004 |
| `src/refmem_model_codec.c`（新） | 线格式编解码、序列化、inline package 组装与 staging | 1,055 |
| `src/refmem_model_validate.c`（新） | 校验/lint、镜像解析与应用、公开 staging 视图 API | 1,072 |
| `inc/refmem_application_model_internal.h`（新） | 类型 + 32 个 extern 对象 + 59 个跨单元函数原型 | 157 |

单实例不变：默认表与活动/暂存视图只在主单元定义一次，另两单元经 extern 访问；
写者仍是 apply_parsed_tables / stage_inline_package_image 所在的单一路径。
