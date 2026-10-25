# status_ui.c 拆分前审计副本（ARCHIVED）

Status: ARCHIVED / NOT BUILT

本目录保存 `components/ui_manager/src/status_ui.c` 在 N5.0.4f 拆分前的完整副本，
用于审计与回退取证。不参与任何构建，不得删除。

## 2026-10-26：拆分前快照

- 源文件 2,522 行 / 81 个函数 / 2 个文件级静态对象（`s_ui`、`s_snapshot`）
- 拆分点：第 1302 行（`draw_product_storage` 的 `}` 之后、产品页注释之前，括号深度 0）

## 拆分边界（N5.0.4f）

| 单元 | 内容 | 行数 |
|---|---|---|
| `src/status_ui_render_kit.c`（新） | 品牌标记、状态→短标签表、数值格式化、GDI 原语、选项卡动画、单色→RGB565 主题、刷屏 | 1,208 |
| `src/status_ui.c`（留） | 产品页、页分派、启动画面、生命周期/按键、渲染循环、`s_ui`/`s_snapshot` 定义 | 1,230 |
| `inc/status_ui_internal.h`（新） | 类型定义 + `extern status_ui_t s_ui` + 32 个渲染原语原型 | 134 |

依赖方向实测**单向**：页面单元调用渲染套件 32 个原语，渲染套件对页面单元 0 调用。
`status_ui_t` / `ui_snapshot_t` / `ui_page_t` 定义移入私有头，`s_ui` 仍是**唯一实例**
（主单元定义、套件经 `extern` 只读或按其既有字段协议写）。
