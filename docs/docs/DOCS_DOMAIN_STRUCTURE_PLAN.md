# Distributed Hard Real-Time Trigger System 文档域目录化管理方案

Status: Draft
Domain: Documentation
Canonical: `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Related: `docs/README.md`, `docs/docs/DOCS_NAMING_STRUCTURE_PLAN.md`, `docs/docs/DOCS_MIGRATION_TODO.md`, `docs/interface/SCPI_COMMAND_PLAN.md`, `docs/arch/RTOS_HAOFV_ARCHITECTURE.md`
Last updated: 2026-08-13

本文档定义 `docs/` 后续按产品主域整理的目标结构、迁移规则和文档归属。当前阶段不直接移动大批文件，先冻结目录规划、迁移 gate 和索引规则，后续按域小批量迁移。

参考结构来自 `D:\Work\ADS_AUTO_SIM\docs`：根 `README.md` 作为总入口，子目录按稳定领域分组，每个领域保留 canonical 主文档、任务进度、验证记录和归档入口。

## 目标

- 让文档结构和产品架构一致：SCPI 对外主域、RTOS owner、反射内存、校准、同步、触发、通信、存储和维护证据分别有清晰入口。
- 避免继续把 SCPI、RTOS、分布式触发、校准、同步、SD、OTA 和历史资料堆在 `docs/` 根目录。
- 保留 `docs/README.md` 作为唯一总索引，迁移后新读者可以从 README 按域阅读，不依赖全文搜索。
- 迁移过程优先保护链接、工具脚本和历史报告引用，不追求一次性改完。

## 目标目录

建议长期目标结构如下：

```text
docs/
  README.md
  docs/                 ; 文档治理、命名规则、迁移表
  arch/                 ; 产品架构、HAOFV、RTOS 和分布式总纲
  interface/            ; SCPI、USB、USBTMC、命令表、上位机接口
  trigger/              ; 产品触发、序列、角度、core1 实时执行
  sync/                 ; SYNC、VDC、DPLL、同步质量
  calibration/          ; CAL link、delay、参数、版本、质量
  refmem/               ; 分布式向量表、命令槽、ACK/NACK、节点事实
  communication/        ; BiSS-C、UART、RS485、RJ45 后端维护
  measure/              ; 测量原语、T2 摘要、链路 delay 测量服务
  storage/              ; SD、StorageAO、日志、trace、snapshot、报告证据
  ota/                  ; OTA、boot、A/B、回滚、System Pack
  hardware/             ; IO 约束、PCB、网表、BOM、Gerber、硬件评审
  validation/           ; HIL、工具验证、闭环验证记录和脚本说明
  release/              ; 发布门禁、打印/PDF、产品冻结 checklist
  legacy/               ; PinProbe、历史报告、最初版 HTML、外部迁入资料
  archive/              ; 废弃路径说明、旧索引和批量迁移记录
```

目录命名使用小写英文，文档文件名继续使用 `<DOMAIN>_<SUBJECT>_<TYPE>.md`。中文历史文件、HTML/PDF 和外部交付文件可以保留原名，但迁移后必须由对应域 README 或总 README 明确归属。

## 对外主域和文档域映射

| 产品/SCPI 主域 | 文档目录 | 说明 |
|---|---|---|
| `*` / `SYSTem` | `arch/`, `storage/`, `ota/`, `release/` | 系统控制面、版本、自检、权限、资源、故障、日志、报告、OTA 和发布门禁 |
| `CONFigure` | `interface/`, `trigger/`, `calibration/`, `sync/` | 配置入口按业务归属拆到各 owner 文档，SCPI 表只保留接口契约 |
| `TRIGger` | `trigger/` | 运行控制、模式、启动停止、core1 实时执行和 active sequence 冻结 |
| `CALibration` | `calibration/` | 链路 delay、校准事务、参数表、active/staging、版本和质量 |
| `SYNC` | `sync/` | VDC、DPLL、同步检查、锁定、HOLDOVER、质量和版本 |
| `READ` | `interface/`, 各业务目录 | `READ:*?` 是产品视图，响应字段归接口文档，事实源归 owner 文档 |
| `MEASure` | `measure/` | 测量原语和服务视图，供 CAL/SYNC/诊断复用 |
| `COMMunication` | `communication/` | BiSS-C、UART、RS485 和通信维护能力 |
| `REALtime` | `trigger/`, `validation/` | 底层实时 validation，不作为现场测试主流程文档入口 |
| `MMEMory` | `storage/` | 文件系统式访问，SD job 和持久化证据仍归 storage/system |

## 内部架构域

这些域不一定对应 SCPI 顶级命令，但需要独立文档入口，因为它们决定 RTOS owner、反射内存和闭环验证：

| 内部域 | 目标目录 | 关键文档类型 |
|---|---|---|
| Control Plane / System Manager | `arch/` | `_ARCHITECTURE.md`, `_DESIGN.md`, `_TASK_PROGRESS.md` |
| Command Transaction / ACK-NACK | `refmem/` | `_DESIGN.md`, `_COMMANDS.md`, `_CHECKLIST.md` |
| Distributed Vector / REFMEM | `refmem/` | 内部主域；`REFMEM_DOMAIN_ARCHITECTURE.md`, `REFMEM_DOMAIN_TODO.md`, `REFMEM_TASK_PROGRESS.md` |
| Loop Engine / Sequence Engine | `trigger/` | `_DESIGN.md`, `_PLAN.md`, `_TASK_PROGRESS.md` |
| Core1 Realtime / FIRE_LOAD | `trigger/` | `_DESIGN.md`, `_ANALYSIS.md`, `_CHECKLIST.md` |
| Calibration Engine | `calibration/` | `_DESIGN.md`, `_COMMANDS.md`, `_TASK_PROGRESS.md` |
| Sync / VDC / DPLL Engine | `sync/` | `_DESIGN.md`, `_ANALYSIS.md`, `_TASK_PROGRESS.md` |
| Measurement Service | `measure/` | `_DESIGN.md`, `_COMMANDS.md`, `_ANALYSIS.md` |
| Evidence / Report / Snapshot | `storage/` | `_DESIGN.md`, `_PLAYBOOK.md`, `_CHECKLIST.md` |
| Access / Permission / Mode Policy | `interface/` 或 `arch/` | `_DESIGN.md`, `_COMMANDS.md` |

## Canonical 初始归属

第一批只迁移或新建 README/索引，不移动高风险 HTML/PDF。当前文件建议归属如下：

| 目标目录 | 初始 canonical |
|---|---|
| `docs/` | `DOCS_NAMING_STRUCTURE_PLAN.md`, `DOCS_MIGRATION_TODO.md`, `DOCS_DOMAIN_STRUCTURE_PLAN.md` |
| `arch/` | `HAOFV_ARCHITECTURE.md`, `HAOFV_VDC_DPLL_ARCHITECTURE.md`, `ARCH_PRODUCT_ARCHITECTURE.md`, `RTOS_HAOFV_ARCHITECTURE.md`, `RTOS_HAOFV_TODO.md`, `RTOS_HAOFV_TASK_PROGRESS.md` |
| `interface/` | `SCPI_COMMAND_PLAN.md`, `SCPI_COMMANDS.md`, `SCPI_USB_INTERFACE_DESIGN.md`, `RP1200波导天线测试系统分布式触发方案SCPI指令表.md` |
| `trigger/` | `TRIGGER_SYNC_TODO.md`, `TRIGGER_SEQ_STEP_DESIGN.md`, `TRIGGER_ENC_COUNT_DESIGN.md`, `TRIGGER_PULSE_COUNT_ANALYSIS.md`, `SYNC_IO_RESOURCE_PLAN.md` |
| `sync/` | `SYNC_IO_DISTRIBUTED_DPLL_DESIGN.md`, `SYNC_IO_REFACTOR_PLAN.md`, `SYNC_IO_TASK_PROGRESS.md` |
| `calibration/` | 首批需要从 SCPI/RTOS 文档中抽出 `CALibration` 专题设计文档 |
| `refmem/` | `REFMEM_DOMAIN_ARCHITECTURE.md`, `REFMEM_DOMAIN_TODO.md`, `REFMEM_TASK_PROGRESS.md`；`LEGACY_PINPROBEA1_RAM_REFLECTIVE_MEMORY_ARCHITECTURE.md` 作为参考 |
| `communication/` | `BISSC_TAP_BRIDGE_DESIGN.md`, `BISSC_IMPLEMENTATION_TODO.md`, `BISSC_TASK_PROGRESS.md`, `BISSC_NETWORK_LOOPBACK_PLAYBOOK.md` |
| `measure/` | 首批需要新增测量服务设计，定义与 CAL/SYNC/T2 的边界 |
| `storage/` | `SD_TODO.md`, `SD_TASK_PROGRESS.md`, `LOG_SYSTEM_TODO.md` |
| `ota/` | `OTA_SYSTEM_DESIGN.md`, `OTA_TODO.md`, `OTA_AB_SWITCH_DESIGN.md`, `OTA_COPY_TRANSACTION_DESIGN.md` |
| `hardware/` | `HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md`, `HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md`, `RP2350B_QFN80_IO_CONSTRAINTS.md`, `hardware/Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel`, `hardware/Netlist_Schematic1_2026-08-04.tel` |
| `validation/` | `SCPI_TASK_PROGRESS.md`, `RTOS_HAOFV_TASK_PROGRESS.md`, `BISSC_TASK_PROGRESS.md` 的后续验证索引 |
| `release/` | `release/RELEASE_CHECKLIST.md`, PDF/打印规则相关文档 |
| `legacy/` | PinProbe HTML、0614/0804 原始 HTML、最初版 SCPI HTML、外部迁入 PDF |

## 迁移批次

### Phase 0 - 冻结规划

- 新增本文档。
- 更新 `docs/README.md`、`docs/docs/DOCS_NAMING_STRUCTURE_PLAN.md` 和 `docs/docs/DOCS_MIGRATION_TODO.md`。
- 不移动文件，只建立迁移目标和 gate。

### Phase 1 - 建立目录 README

- 创建各目录和 `README.md`。
- 每个目录 README 只列当前归属和迁移候选，不移动历史文件。
- 总 README 链接到各目录 README。

### Phase 2 - 低风险 Markdown 迁移

- 先迁文档治理、OTA、SD、BISSC、release 等被代码引用少的 Markdown。
- 每批迁移必须运行 `docs_check` 并全文搜索旧路径。
- 每批只处理一个目录或一个稳定主域。

### Phase 3 - 高风险接口和架构迁移

- 迁移 `SCPI_COMMAND_PLAN.md`、RTOS HAOFV 三件套、产品 SCPI 指令表 Markdown。
- 同步更新 HTML/PDF 导出脚本、校验脚本、报告模板和 README。
- 对外指令表文件名和历史 HTML/PDF 可以暂缓移动，避免影响上位机交付路径。

### Phase 4 - 历史资料归档

- 将 PinProbe、0614/0804 原始 HTML、最初版 HTML、外部 PDF 放入 `legacy/` 或 `archive/`。
- 保留迁移表，说明旧路径、新路径、引用影响和冻结状态。

## 迁移 gate

每批移动文件必须满足：

- 更新 `docs/README.md` 的 canonical 表、阅读树和快速查找规则。
- 更新被移动文档顶部 `Canonical` / `Related`。
- 更新所有 Markdown 内部引用、工具脚本引用和报告导出引用。
- 运行 `python tools/docs_check/docs_check.py`。
- 对 SCPI 相关迁移，还要运行 `python tools/product_scpi_validate/product_scpi_validate.py --dry-run`。
- 对实时/validation 相关迁移，还要运行 `python tools/realtime_scpi_validate/realtime_scpi_validate.py --dry-run`。
- 使用 `git diff --check` 检查空白和换行问题。
- 大批 Markdown 迁移完成后提交；HTML 迁移按既有规则先积极暂存，确认无写空风险后再决定是否提交。

## 新文档落点规则

新增文档先按以下顺序判断目录：

1. 是否是跨域产品架构、HAOFV、RTOS 或 core0/core1 owner 设计：放 `arch/`。
2. 是否是对外命令、上位机接口、USB/USBTMC 或权限策略：放 `interface/`。
3. 是否影响触发运行、角度、序列、core1 或实时边沿：放 `trigger/`。
4. 是否管理链路 delay、校准表、校准质量：放 `calibration/`。
5. 是否管理 VDC、DPLL、同步锁定、HOLDOVER 或同步质量：放 `sync/`。
6. 是否定义分布式节点事实、反射内存、命令槽、ACK/NACK：放 `refmem/`。
7. 是否是 BiSS/UART/RS485 等通信后端：放 `communication/`。
8. 是否是测量原语、T2 摘要、delay 测量服务：放 `measure/`。
9. 是否是 SD、日志、trace、snapshot、报告证据：放 `storage/`。
10. 是否是 OTA、boot、A/B、回滚、System Pack：放 `ota/`。
11. 是否是 PCB、网表、IO 约束、BOM/Gerber：放 `hardware/`。
12. 是否是验证流程、HIL 工具、任务进度和闭环记录：放 `validation/`。
13. 是否是发布、打印、PDF、冻结检查：放 `release/`。
14. 是否是历史资料或外部迁入原文：放 `legacy/` 或 `archive/`。

## 推荐阅读树

迁移完成后的阅读树建议如下：

```text
Distributed Hard Real-Time Trigger System
├─ 1. 产品和软件架构
│  ├─ arch/HAOFV_ARCHITECTURE.md
│  ├─ arch/HAOFV_VDC_DPLL_ARCHITECTURE.md
│  ├─ arch/ARCH_PRODUCT_ARCHITECTURE.md
│  ├─ arch/RTOS_HAOFV_ARCHITECTURE.md
│  ├─ arch/RTOS_HAOFV_TODO.md
│  └─ arch/RTOS_HAOFV_TASK_PROGRESS.md
├─ 2. 对外接口
│  ├─ interface/SCPI_COMMAND_PLAN.md
│  ├─ interface/RP1200波导天线测试系统分布式触发方案SCPI指令表.md
│  └─ interface/SCPI_USB_INTERFACE_DESIGN.md
├─ 3. 运行链路
│  ├─ trigger/TRIGGER_* 文档
│  ├─ calibration/CALIBRATION_* 文档
│  ├─ sync/SYNC_* 文档
│  └─ refmem/REFMEM_* 文档
├─ 4. 后端能力
│  ├─ communication/BISSC_* / UART / RS485
│  ├─ measure/MEASURE_* 文档
│  ├─ storage/SD_* / LOG_* 文档
│  └─ ota/OTA_* 文档
├─ 5. 硬件、验证和发布
│  ├─ hardware/ 最小系统约束、产品板约束、最新网表与 IO 约束
│  ├─ validation/*_TASK_PROGRESS.md
│  └─ release/RELEASE_CHECKLIST.md
└─ 6. 历史和外部参考
   ├─ legacy/PinProbe*
   ├─ legacy/0614/0804 原始报告
   └─ archive/旧路径说明
```

## 当前建议

下一步不要直接移动所有文档。建议先做 Phase 1：建立目录和目录 README，并把总 README 调整成“目标域导航 + 当前平铺路径”的双索引。随后优先推进 `refmem/`、`interface/`、`trigger/` 三个目录，因为它们会决定 SCPI、RTOS 和分布式触发后续文档的主线。
