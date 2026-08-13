# 产品板硬件约束

Status: Draft
Domain: HARDWARE
Canonical: `docs/hardware/HARDWARE_PRODUCT_BOARD_CONSTRAINTS.md`
Related: `docs/arch/HAOFV_ARCHITECTURE.md`, `docs/hardware/RP2350B_QFN80_IO_CONSTRAINTS.md`, `docs/hardware/RP2350B_NETLIST_REVIEW_2026-08-04.md`, `docs/hardware/Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel`
Last updated: 2026-08-13

本文档是后续产品板硬件约束的入口。产品板约束由最新产品网表 `Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel`、IO 约束、BOM/Gerber/PickAndPlace 和硬件评审共同派生，冻结隔离、电源域、连接器、保护器件、网表事实、GPIO 分配和可装配选项；HAOFV 只引用本入口，不直接维护具体 pin map。

## 约束层级

| 层级 | 文档 | 作用 |
|---|---|---|
| 产品板入口 | 本文档 | 说明产品板约束范围、冻结策略和派生规则。 |
| IO 明细 | `RP2350B_QFN80_IO_CONSTRAINTS.md` | RP2350B QFN-80 GPIO、PIO、ADC、隔离域和专用引脚明细。 |
| 最新网表 | `Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel` | 当前产品板约束的最新事实来源。 |
| 网表评审 | `RP2350B_NETLIST_REVIEW_2026-08-04.md` | 既有网表评审结论和风险项；后续需要基于最新网表刷新。 |
| 生产输出 | BOM、Gerber、PickAndPlace、网表 | 生产与装配依据。 |

## 产品板冻结原则

- GPIO、PIO、DMA、ADC 和连接器 pin map 以硬件域为准。
- GND/FGND、隔离器、ESD/TVS、电源入口、eFuse 和可选 0R/DNP 策略只在硬件域维护。
- 产品板约束可限制软件能力，但不能由软件临时需求绕过隔离或安全边界。
- 任一硬件变更必须同步影响到 board profile、固件 pin map、SCPI 能力字段和验证计划。

## 当前产品板基线

| 项目 | 当前入口 |
|---|---|
| 最新产品网表 | `Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel` |
| 历史网表 | `Netlist_Schematic1_2026-08-04.tel` |
| IO 约束 | `RP2350B_QFN80_IO_CONSTRAINTS.md` |
| 网表评审 | `RP2350B_NETLIST_REVIEW_2026-08-04.md` |
| 硬件输出 | `BOM_*`、`PickAndPlace_*`、`Gerber_*` |

## 与调试最小系统板的关系

调试最小系统板用于软件闭环，产品板用于冻结最终边界。两者差异必须通过 board profile 和验证矩阵显式管理，不能让临时调试连接成为产品默认约束。

## 后续待补

- [ ] 基于 `Netlist_CTL-SYNCTRIG4F4-HASL_2026-08-13.tel` 刷新网表评审和硬件约束。
- [ ] 建立产品板版本号、网表版本、BOM/Gerber 版本和固件 board profile 的对应表。
- [ ] 建立产品板 bring-up 验证矩阵。
- [ ] 把隔离、电源、ESD、连接器和装配选项形成发布冻结 checklist。
