# 验证域

Status: Active
Domain: VALIDATION
Canonical: `docs/validation/README.md`
Related: `docs/README.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-11-05

本目录是 HIL、验证方法、任务进度及闭环验证记录的入口。工具身份、登记、依赖、生命周期与交接治理统一见 [工具域架构](../tools/DOCS_TOOLS_ARCHITECTURE.md)；具体工具操作说明仍与根目录 `tools/` 实现相邻，业务资格判据仍归原业务域。

## 当前入口

| 当前路径 | 定位 |
|---|---|
| `../interface/SCPI_TASK_PROGRESS.md` | SCPI 指令框架、验证脚本和接口拆分闭环记录 |
| `../arch/RTOS_HAOFV_TASK_PROGRESS.md` | RTOS / 双核 / 分布式触发任务进度 |
| `../communication/BISSC_TASK_PROGRESS.md` | BiSS-C 任务进度和验证记录 |
| `../storage/SD_TASK_PROGRESS.md` | SD 域任务进度和验证记录 |
| `../sync/SYNC_IO_TASK_PROGRESS.md` | SYNC_IO / Trigger 同步重构任务进度 |
| `P3_FLOW.html` | 硬件验收（P3）37 步工作流视图（`tools/hardware_acceptance/hardware_acceptance_workflow.py`） |
| `VALIDATION_HARDWARE_ACCEPTANCE_PLAYBOOK.md` | 硬件验收操作手册：前置检查、标准命令、续跑/重试边界、失败处置与收口判据；其他会话先读这份再跑。 |

## 2026-10-12 硬件验收步进工作流

- `p3_hardware_acceptance.py` 的整体流程拆为 37 个步骤函数（`ACCEPTANCE_STEPS`）；`run`/`resume`
  与 `hardware_acceptance_workflow.py --profile full` 执行同一组函数，代码提交 `6d0fc825`。
- 运行目录持久化 `workflow-state.json`：每步产出的值、每步状态/序号/调用、每次调用的源码指纹。
  缺输入按名失败并指出产出步骤；`--run <dir|latest>` 续跑，`--reuse-from` 沿用标定（目前只出记录）。
- 凭证资格（`credential_ineligibility`）：所需步骤在同一运行目录、同一源码指纹下按序全部运行；
  重跑上游使下游过期；否则只写 `workflow-record.json`，配置凭证按快照原样保留。
- 首次凭证：`out/HardwareAcceptance/20261012/hawf-152301`，两次调用（NO.2 `OPMode:APPLy` 偶发超时
  使步骤 8 失败，`--steps 7-36 --run` 续跑免去构建与 OTA），`PASS_WITH_WARNINGS`（ERROR/FATAL 为 0），
  步骤 36 以 `check-staged` 校验器自检通过。
- 后续：沿用标定的提交凭证（新凭证 schema + `check-staged` 支持，属门禁变更）。

## 边界

- 任务进度可以保留在业务域，也可以由本目录建立总验证索引。
- 验证文档必须记录命令、固件版本、工具、现象、结果和后续动作。

## 2026-08-15 Host C 单元测试闭环

目标：把既有 C 单元测试从“ARM GCC 编译通过”升级为“host gcc 可执行断言通过”，避免后续把 compile-only 误判为测试通过。

工具：

- Host GCC：从环境变量 `PATH` 解析 `gcc.exe`；也可通过 `-HostGccDir` 显式覆盖。
- 汇总入口：`powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_host_unit_tests.ps1`

2026-08-22：当前环境通过 `PATH` 使用 `D:\Microsoft\MinGW\mingw64\bin\gcc.exe`，完整
host runner 30/30 通过。NO.1–NO.4 factory/Direct A/B 烧录闭环报告见
`build/flash_burn_NO1_20260822_positive2/`、`build/flash_burn_NO2_20260822/`、
`build/flash_burn_NO3_20260822/` 和 `build/flash_burn_NO4_20260822/`。

覆盖：

- 13 个 `tools/tests/run_*tests.ps1` 脚本。
- 18 个 C 单元测试文件：BiSS protocol、Flash/Core1 lockout、portable log、portable OTA 六项、RefMem application contract、PIO SPI adapter、quality、realtime contract、slot claim、sync frame、sync hello、sync、table registry。

本轮真实断言暴露并修复：

- `biss_protocol` CRC6 golden vector 的参与位拼接存在歧义，改为明确的 BiSS MSB-first payload/status 向量。
- `portable_ota_core` package-mode 测试夹具错误写入 header package CRC；当前 packager 该字段为 `0`，整包 CRC 由 begin 参数校验。
- `refmem_application_contract` 测试脚本漏链接 `refmem_slot_claim.c`。
- `refmem_claim_propose_frame_init()` 在设置 `payload_count` 前计算 payload CRC，导致新建 PROPOSE 帧自校验失败。
- `test_refmem_table_registry` 的构包 payload 固定为 `1536` 字节，已小于当前 P0-P3 表镜像 payload，host 执行时发生栈破坏。
- S0 后续补入 `drv_flash_lockout` host 断言，覆盖 supported-but-offline 拒绝写入、request/ACK/PARKED/release 完整握手和 core1 不 ACK 故障注入。

结果：

- `tools\tests\run_host_unit_tests.ps1` 通过后必须输出 `host unit test scripts passed: 13/13`。
- 后续验证报告必须区分 `host unit tests passed` 与 `compiled with ARM GCC; host execution skipped`。
