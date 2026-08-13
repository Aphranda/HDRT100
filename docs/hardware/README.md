# 硬件域

Status: Active
Domain: HARDWARE
Canonical: `docs/hardware/README.md`
Related: `docs/README.md`, `docs/docs/DOCS_DOMAIN_STRUCTURE_PLAN.md`
Last updated: 2026-08-13

本目录是 IO 约束、PCB、网表、BOM、Gerber、硬件评审和布局资料的目标入口。

## 当前 canonical

| 当前路径 | 定位 |
|---|---|
| `../RP2350B_QFN80_IO_CONSTRAINTS.md` | RP2350B QFN-80 GPIO 分配与 IO 使用约束 |
| `RP2350B_NETLIST_REVIEW_2026-08-04.md` | RP2350B 网表评审 |
| `Netlist_Schematic1_2026-08-04.tel` | 当前硬件网表 |
| `RP2350_PICO_Netlist_Schematic1_2026-07-27.tel` | 早期 PICO 网表参考 |
| `BOM_CTL-SYNCTRIG4F4-HASL-V0.1_CTL-SYNCTRIG4F4-HASL-V0.1_2026-08-04.xlsx` | BOM 导出 |
| `PickAndPlace_CTL_SYNCTRIG4F4_HASL_V0_1_2026_08_04.xlsx` | 贴片坐标导出 |
| `Gerber_CTL-SYNCTRIG4F4-HASL-V0.1_2026-08-04.zip` | Gerber 导出 |
| `../BISSC_SYNC_IO_PERIPHERAL_CIRCUIT_DESIGN.md` | BiSS-C / SYNC_IO 外围电路约束 |

## 边界

- 硬件域记录物理约束和评审结论。
- 固件 owner、SCPI 指令和 RTOS 任务归各自软件域。
