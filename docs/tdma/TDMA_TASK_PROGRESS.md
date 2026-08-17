# TDMA 基础件主域任务进度

Status: Active
Domain: TDMA
Canonical: `docs/tdma/TDMA_TASK_PROGRESS.md`
Related: `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`, `docs/tdma/TDMA_DOMAIN_TODO.md`
Last updated: 2026-08-17

本文档记录 TDMA foundation 的阶段性任务进度、验证结果和后续动作。待办事项放在 `TDMA_DOMAIN_TODO.md`。

## 记录规则

每条任务记录使用以下格式：

```text
### TDMA-TASK-YYYYMMDD-NNN - 标题

- 状态：
- 日期：
- 任务目标：
- 完成内容：
- 验证结果：
- 还需完成：
- 关联文件：
- 下一步：
```

## 当前目标

TDMA Domain 当前目标是把公共 `components/tdma` 从 RefMem/VDC 之间的隐式基础件，升级为 HAOFV 下的显式 foundation domain：

```text
TdmaSchedulerAO
+ TdmaRuntimeFB
+ TdmaPayloadRegistry
+ TdmaRingRuntime
+ TdmaQualityVector
+ TransportAdapter
```

VDC 消费 observation evidence，RefMem 消费 data/completion evidence，二者都不拥有上/下行 TDMA 环路。

## 任务记录

### TDMA-TASK-20260817-002 - Foundation profile、HAOFV owner 与 TSN-style 流治理

- 状态：完成首版代码契约与 host 单元验证；RMTP profile 表和真实逐流 scheduler 尚待实现。
- 日期：2026-08-17
- 任务目标：
  - 将 `TDMARingProfile` owner 从 VDC 迁入 TDMA Foundation。
  - 将 TDMA 表达为 HAOFV 可装载 system node，并冻结运行期资源声明。
  - 面向 VDC、RefMem、配置、OTA 和 LOG，吸收 TSN 的流分类、准入、time-aware gate、整形和背压思想。
- 完成内容：
  - 新增 `tdma_profile.h/.c`，定义 `tdma_ring_profile_t`、`tdma_foundation_profile_t`、adapter、PIO/SM/DMA/core1 resource、frame capacity、payload whitelist 和 profile CRC。
  - `vdc_tdma_schedule_profile_t` 改为只读嵌入 `ring_binding`；VDC schedule CRC 绑定 TDMA ring CRC，ring topology/CRC 校验由 TDMA owner 实现。
  - `tdma_service_configure_foundation_profile()` 将 owner、adapter、PIO/SM/DMA、core1 service、capacity、IO/IP claim、payload whitelist 和 profile CRC 冻结到 runtime snapshot。
  - payload registry 按 active whitelist 做 admission，未登记 payload 被拒绝。
  - 固定五类 traffic class：VDC realtime、RefMem realtime、config control、OTA bulk、LOG best effort；每类定义周期预算、帧数、队列深度、deadline、gate/shaping/preemption 和 overflow policy。
  - RefMem Application Model 增加 TDMA baseline capability、`TdmaSchedulerAO` instance、NodeLoad、resource/IP claim 和 DeploymentGate check；默认模型只允许一个 active TDMA owner。
- 验证结果：
  - `run_tdma_profile_tests.ps1` 使用 Vivado MinGW GCC host 执行通过。
  - `run_vdc_domain_tests.ps1` 使用 Vivado MinGW GCC host 执行通过。
  - `run_host_unit_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 全部通过，19/19 脚本均执行 host 测试。
  - `python -m pytest tests\python\test_refmem_pack_build.py -q` 通过，2 passed；仅有 OneDrive `.pytest_cache` 无法写入 warning，不影响测试结果。
  - `python tools\docs_check\docs_check.py` 通过，保留既有两项 risk review 文件名 warning。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，最终 build id `20260817051719`，package CRC `0x086043EE`。
- 还需完成：
  - 将 foundation/traffic profile 作为正式 RMTP/System Pack 表镜像，而不是只保留 C contract。
  - DeploymentGate 对 staged candidate 执行唯一 owner、总预算、MTU、queue RAM、DMA/SM/IO claim 和 payload compatibility 校验。
  - 实现 core1 逐类队列、time-aware gate、policing/backpressure 和质量计数。
- 关联文件：
  - `components/tdma/inc/tdma_profile.h`
  - `components/tdma/src/tdma_profile.c`
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `components/vdc_domain/inc/vdc_domain.h`
  - `components/distributed_refmem/inc/refmem_application_model.h`
  - `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
- 下一步：
  - 先完成 host/doc/build 闭环，再设计 RMTP `TdmaFoundationProfile` 表项和 scheduler queue runtime。

### TDMA-TASK-20260817-001 - 上/下行 TDMA 边界升格为基础件

- 状态：完成首版边界升级、文档主域、单元测试和构建验证；后续进入 TDMA system node / 同时 UP-DOWN runtime。
- 日期：2026-08-17
- 任务目标：
  - 根据架构纠偏，将上行/下行 TDMA 从 VDC 描述中拆出，作为独立基础件主域。
  - 保持 HAOFV 边界：TDMA owner 管 runtime、payload、adapter 和 evidence；VDC 只消费 timestamp，RefMem 只消费 payload completion。
- 完成内容：
  - 新建 `docs/tdma/` 标准三件套和 README。
  - `tdma_service` 已增加 ring runtime config/snapshot 字段，用于表达 active node、local/reference slot、UP/DOWN group、ring seq、running state、profile CRC 和 schedule CRC。
  - `tdma_service_configure_ring_runtime()` 拒绝 bad config：节点数不足、slot 越界、UP/DOWN group 相同、缺少 simultaneous flag、CRC 为 0。
  - 单元测试增加公共 TDMA ring runtime contract，验证 `up_running/down_running` 可由 runtime 发布，但 `simultaneous_feedback_loop_evidence` 仍保持 0，防止把配置就绪误判为闭环。
- 验证结果：
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_refmem_realtime_tdma_tests.ps1` 通过 ARM GCC 编译门禁；当前环境未找到 host gcc，因此 host 执行跳过。
  - `powershell -NoProfile -ExecutionPolicy Bypass -File tools\tests\run_vdc_domain_tests.ps1 -HostGccDir D:\Xilinx\2025.2\tps\mingw\10.0.0\win64.o\nt\bin` 通过，输出 `vdc_domain tests passed`。
  - `python tools\docs_check\docs_check.py` 通过，`files=90 warnings=2`；仅保留既有 `REFMEM_DOMAIN_RISK_REVIEW.md` 和 `VDC_DOMAIN_RISK_REVIEW.md` 命名 warning。
  - `python -m py_compile tools\docs_check\docs_check.py` 通过。
  - `cmake --build build-rtos-multicore-smoke -j 4` 通过，生成 build id `20260817043820`，package CRC `0xB91E63B5`。
- 还需完成：
  - 更新 `docs/README.md`、HAOFV 架构和 VDC 文档。
  - 后续把 TDMA 作为 HAOFV system node 接入 NodeLoad / DeploymentGate。
  - 后续建立真实 core1 同时 UP/DOWN runtime 和硬件 timestamp correlation。
- 关联文件：
  - `components/tdma/inc/tdma_service.h`
  - `components/tdma/src/tdma_service.c`
  - `tests/unit/test_refmem_realtime_tdma.c`
  - `docs/tdma/README.md`
  - `docs/tdma/TDMA_DOMAIN_ARCHITECTURE.md`
  - `docs/tdma/TDMA_DOMAIN_TODO.md`
- 下一步：
  - 按 HAOFV 索引和 VDC 文档收敛 TDMA ownership，再运行 host/doc 验证。
