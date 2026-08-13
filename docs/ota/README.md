# OTA 与启动域

Status: Active
Domain: OTA
Canonical: `docs/ota/README.md`
Related: `docs/README.md`, `docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是 OTA、boot、Direct A/B、copy transaction、回滚、System Pack 和升级安全链的目标入口。

## 当前 canonical

| 当前路径 | 定位 |
|---|---|
| `../OTA_SYSTEM_DESIGN.md` | OTA 主方案历史文件 |
| `../OTA_TODO.md` | OTA 产品化待办 |
| `../OTA_AB_SWITCH_DESIGN.md` | Direct A/B 切换设计 |
| `../OTA_COPY_TRANSACTION_DESIGN.md` | Copy-to-active 掉电恢复事务设计 |
| `../OTA_PORTABLE_ARCHITECTURE.md` | Portable OTA 架构和复用方案 |
| `../OTA_LIBRARY_MIGRATION_PLAYBOOK.md` | Portable OTA 库化迁移 playbook |

## 边界

- SCPI OTA 命令只能投递 OTA 事件，不直接擦写 flash。
- flash/storage 动作必须经过资源仲裁和 core1 park/lockout 策略。
