# docs/temp

Status: Active
Domain: Documentation
Canonical: `docs/temp/README.md`
Related: `docs/vdc/VDC_TASK_PROGRESS.md`, `tools/vdc_long_monitor/vdc_long_monitor.py`
Last updated: 2026-08-17

本目录用于保存阶段性调试、验证和绘图结果。

- `vdc_long_monitor/`：长时间 VDC/DPLL 节点/TDMA leg 监控结束后自动归档的评估图。
- 每次运行按原始输出目录名建立子目录，例如 `vdc_long_monitor_20260817_114910/`。
- 原始数据仍保存在对应的 `build-*` 输出目录中，`docs/temp` 只作为便于查看和横向比较的绘图归档入口。
- 当前 `vdc_long_monitor` 图只表示节点或单向 leg 监控质量，不证明固件内上行组/下行组同时运行的实时反馈闭环。
