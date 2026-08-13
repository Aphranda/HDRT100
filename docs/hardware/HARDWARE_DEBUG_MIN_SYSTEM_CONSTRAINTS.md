# 调试最小系统板硬件约束

Status: Draft
Domain: HARDWARE
Canonical: `docs/hardware/HARDWARE_DEBUG_MIN_SYSTEM_CONSTRAINTS.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md`, `docs/sync/SYNC_IO_RESOURCE_PLAN.md`
Last updated: 2026-08-13

本文档定义当前正在运行和调试的最小系统板 / DEMO 板硬件约束。该板用于验证 HAOFV、RTOS、双核、SCPI、OTA、SD、基础触发和工具链闭环，不作为产品板 pin map 或隔离策略冻结依据。

## 定位

| 项目 | 约束 |
|---|---|
| 目标 | 尽快闭环软件架构、RTOS 任务划分、双核通信、SCPI 接口和基础触发模式。 |
| 当前身份 | 当前运行版本按最小系统约束管理。 |
| 允许 | 为调试便利使用临时 UART/USB/LED/LCD/按键映射。 |
| 禁止 | 把临时接线直接写入 HAOFV 顶层架构或产品板约束。 |
| 迁移原则 | 调试板差异必须通过 board profile、构建选项或兼容层隔离。 |

## 当前约束

- 默认构建目标仍以 `PICO_BOARD=pico2` 和当前最小系统/DEMO 板连线为主。
- USB/CDC/USBTMC、CH343、UART、LCD、按键、LED 可用于调试和验证脚本。
- 触发、SYNC、BiSS、SD、OTA 等功能优先验证软件路径和状态机稳定性。
- 调试板可以缺少最终产品隔离、电源、连接器、ESD 和完整分布式链路。

## 与 HAOFV 的关系

- 调试板用于证明 AO/FB/Vector/Resource Arbiter 的软件闭环。
- 调试板不决定产品级 owner、事件边界、反射内存字段和 SCPI 指令树。
- 若调试板资源不足，优先做功能裁剪或软件模拟，不反向削弱产品架构。

## 与产品板的差异管理

| 差异类型 | 处理方式 |
|---|---|
| GPIO 或连接器不同 | 放入 board profile，不改顶层 HAOFV。 |
| 缺少隔离链路 | 使用软件模拟或维护模式标志，不能宣称产品隔离已验证。 |
| 缺少外部设备 | 通过 SCPI/tool smoke 验证内部状态机，产品联调另建验证记录。 |
| 资源数量不同 | 以产品板约束为最终目标，调试板只作为最小可运行集。 |

## 后续待补

- [ ] 固化当前 DEMO 板实际 pin map 和已验证外设清单。
- [ ] 标记哪些测试只在调试板有效，哪些可迁移到产品板。
- [ ] 建立调试板到产品板 board profile 差异表。
