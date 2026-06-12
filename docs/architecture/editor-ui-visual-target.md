# Asharia Editor UI Visual Target

更新日期：2026-06-12

本文描述 Asharia Editor 的期望视觉目标，不描述当前实现状态，也不受当前已有布局限制。
它定义 Dear ImGui 也应该达到的专业编辑器质感：专业、克制、密集、清晰、长期可用，
而不是默认 ImGui debug 面板。

本文不替代 [editor.md](editor.md) 的当前架构事实，也不替代
[editor-ui-style-v1.md](editor-ui-style-v1.md) 的当前 style v1 规范。本文用于给后续
editor visual baseline、ImGui polish、Qt spike 对比和 UI review 提供目标口径。

## 定位

目标定位：

```text
Professional ImGui Engine Workbench
```

含义：

- 中心是创作对象，周围是稳定生产工具。
- 界面像 Unity、Unreal、Blender、Godot 一类专业 DCC/editor，而不是 Web dashboard。
- 不追求华丽动效，不做营销卡片，不做大面积装饰背景。
- 所有 UI 决策优先服务选择、编辑、预览、验证、恢复。
- 保留 ImGui 的直接、轻量和 C++/Vulkan 贴近性，但默认外观必须被完整产品化。

## 整体布局目标

目标布局：

```text
Main Menu
Global Command Bar
Left Dock | Center Scene View | Right Inspector
Bottom Workspace
Global Status Bar
```

区域职责：

| 区域 | 职责 |
| --- | --- |
| `Main Menu` | File / Edit / Assets / GameObject / Component / Window / Tools / Debug / Help |
| `Global Command Bar` | 全局命令、播放控制、搜索、布局、诊断入口 |
| `Left Dock` | Hierarchy / Outliner，负责导航和对象树扫描 |
| `Center Dock` | Scene View 是最大视觉中心，其他图形调试视图可 tab 进入 |
| `Right Dock` | Inspector / Properties，负责上下文属性与验证 |
| `Bottom Workspace` | Project、Console、RenderGraph、Frame Debug、Profiler、Import Queue |
| `Global Status Bar` | 整个编辑器健康状态、最新日志、错误/警告、后台任务、dirty/play/GPU/backend |

边界规则：

- 全局状态栏在整个编辑器最底部。
- Scene View 内状态条只显示视口局部状态。
- 全局日志不得塞进 Scene View 状态条。
- Scene View camera/tool/FPS 不应占用全局状态栏主区域。

## 视觉语言

视觉目标：

- 主色使用 charcoal / graphite 深色基底。
- 强调用冷青蓝，服务 selection、focus 和 action。
- Info 使用低饱和蓝灰。
- Success 使用柔和绿色。
- Warning 使用 amber。
- Error 使用 muted red。
- Dirty 使用小点或短线，不用大面积颜色。
- App background 最深，panel background 稍亮，section/header 再稍亮。
- Control hover / active 必须有清晰但克制的变化。
- 分隔线使用 1px 弱线。
- 只有 focus、selection、error 使用更强边框。
- 控件圆角控制在 2-4px。
- 不做大圆角卡片。
- 普通 UI 用 sans 字体。
- 数值、路径、GUID、日志 id 用 mono 字体。
- 中文使用清晰黑体方向。
- 间距基于 4px / 8px grid。
- 属性行、表格行、toolbar button 使用稳定高度。
- 所有窄区域文本单行省略，tooltip 显示完整文本。

禁止方向：

- 默认 ImGui 蓝灰 demo 感。
- 高饱和霓虹色。
- 装饰渐变。
- SaaS 卡片式 dashboard。
- 大面积红/黄错误块。
- 用颜色作为唯一状态表达。

## Icon-only 总原则

- 高频、通用、行业惯例明确的命令使用 `icon-only`。
- `icon-only` 必须有 tooltip。
- Tooltip 必须包含命令名。
- Tooltip 应包含快捷键，如有。
- Tooltip 必须包含当前 disabled 原因，如控件不可用。
- `icon-only` 按钮必须使用稳定尺寸，不随文本变化。
- 破坏性命令不得只用 icon。
- 低频且语义不明确的命令不得只用 icon。
- 状态 icon 必须同时使用形状、颜色、tooltip，不只靠颜色。
- 文本承载核心信息时必须保留文字，例如对象名、资产名、日志消息、属性 label、错误说明。
- `icon-only` 是为了降低工具栏噪音，不是为了隐藏含义。

## 全局顶部工具栏

布局目标：

- 左侧：New / Open / Save / Undo / Redo / Refresh / Reimport。
- 中间：Play / Pause / Step / Stop。
- 右侧：Command Search / Layout / Theme / Diagnostics。

`icon-only`：

| 控件 | 显示方式 |
| --- | --- |
| New | icon-only |
| Open | icon-only |
| Save | icon-only |
| Save All | icon-only |
| Undo | icon-only |
| Redo | icon-only |
| Refresh | icon-only |
| Reimport Selected | icon-only |
| Play | icon-only |
| Pause | icon-only |
| Step | icon-only |
| Stop | icon-only |
| Capture Frame | icon-only |
| Layout Reset | icon-only |
| Editor Settings | icon-only |
| Diagnostics | icon-only |

`icon + text`：

| 控件 | 显示方式 |
| --- | --- |
| Command Search | icon + placeholder text |
| Layout Preset | icon + current layout name |
| Active Mode | icon + `Edit / Play / Paused` |
| Build/Cook/Import activity | icon + short text |

`text-only`：

| 控件 | 显示方式 |
| --- | --- |
| Main Menu items | text-only |
| destructive confirmation labels | text-only |
| disabled reason tooltip content | text-only |

## Scene View

目标：

- Scene View 是最大区域。
- 视口工具条悬浮或贴边，视觉弱于场景内容。
- 工具状态必须始终可见。
- 视口错误用 nonblocking overlay，不弹 modal。

`icon-only`：

| 控件 | 显示方式 |
| --- | --- |
| Select | icon-only |
| Move | icon-only |
| Rotate | icon-only |
| Scale | icon-only |
| Pan / Hand | icon-only |
| Orbit | icon-only |
| Frame Selected | icon-only |
| Grid toggle | icon-only |
| Gizmo toggle | icon-only |
| Snap toggle | icon-only |
| Shaded mode icon | icon-only when inside mode group |
| Wireframe icon | icon-only when inside mode group |
| Lighting toggle | icon-only |
| Bounds / debug overlay | icon-only |
| Camera preview | icon-only |
| Overlay visibility | icon-only |
| Maximize viewport | icon-only |

`icon + text`：

| 控件 | 显示方式 |
| --- | --- |
| Local / World | icon + text or segmented text |
| Pivot / Center | icon + text |
| Perspective / Orthographic | icon + current mode |
| Scene / Game / Preview | icon + text |
| Shaded / Wire / Debug dropdown current value | icon + text |

必须保留文字：

| 内容 | 原因 |
| --- | --- |
| 当前工具提示 `Move · World · Snap 0.5m` | 状态需要可读 |
| 视口错误 `Render target pending` | 用户必须知道原因 |
| FPS / extent / render target status | 视口局部状态必须可读 |
| object name tag | 场景识别需要文字 |

## Hierarchy / Outliner

目标：

- 左侧对象树用于快速扫描、选择、过滤、批量操作。
- 行高紧凑，状态丰富但不吵。
- 对象名称永远是主要信息。

`icon-only`：

| 控件 | 显示方式 |
| --- | --- |
| Expand / Collapse | icon-only |
| Visibility | icon-only |
| Lock | icon-only |
| Create child | icon-only |
| Filter | icon-only |
| Clear search | icon-only |
| Object type | icon-only |
| Dirty marker | icon-only |
| Error / Warning badge | icon-only |

`icon + text`：

| 控件 | 显示方式 |
| --- | --- |
| Add / Create dropdown | icon + text |
| Active filter chips | icon + text |
| Layer/tag filters | icon + text |

必须保留文字：

| 内容 | 原因 |
| --- | --- |
| Object name | 树的核心信息 |
| Search placeholder | 可发现性 |
| Empty state | 需要说明当前无对象 |
| Rename field | 编辑目标必须可读 |

## Inspector

目标：

- Inspector 是上下文属性编辑器。
- 左侧 label 固定宽度，右侧控件对齐。
- 错误必须贴近字段，不只进入 Console。
- Section header 轻量清晰，支持 fold/unfold。

`icon-only`：

| 控件 | 显示方式 |
| --- | --- |
| Lock Inspector | icon-only |
| Pin Inspector | icon-only |
| Component enable | icon-only or compact toggle |
| Component context menu | icon-only |
| Reset property | icon-only |
| Revert override | icon-only |
| Copy value | icon-only |
| Pick asset reference | icon-only |
| Reveal asset | icon-only |
| Remove component | icon-only |
| Fold / unfold section | icon-only |
| Validation marker | icon-only |

`icon + text`：

| 控件 | 显示方式 |
| --- | --- |
| Add Component | icon + text |
| Apply import settings | icon + text |
| Revert import settings | icon + text |
| Reimport | icon + text |
| Component header | icon + component name |
| Asset reference field | icon + asset name |

必须保留文字：

| 内容 | 原因 |
| --- | --- |
| Property label | 属性必须可读 |
| Numeric value | 编辑值必须可读 |
| Validation message | 必须解释错误 |
| Section title | 扫描依赖文字 |
| GUID / stable id | 需要可复制和识别 |

## Project / Asset Browser

目标：

- 底部默认 Project 工作区。
- 支持 grid 和 list/table。
- 资产状态是第一等信息：Imported / Pending / Failed / Stale / Missing product。
- 资产名和路径不能被 icon 替代。

`icon-only`：

| 控件 | 显示方式 |
| --- | --- |
| Grid view | icon-only |
| List view | icon-only |
| Sort direction | icon-only |
| Filter | icon-only |
| Clear search/filter | icon-only |
| Refresh | icon-only |
| Reimport selected | icon-only |
| Reveal in folder | icon-only |
| Copy path/GUID | icon-only |
| Favorite | icon-only |
| Asset type icon | icon-only |
| Import status badge | icon-only |

`icon + text`：

| 控件 | 显示方式 |
| --- | --- |
| Import | icon + text |
| Create Asset | icon + text |
| Breadcrumb item | icon + folder name |
| Status filter chips | icon + status text |
| Type filter chips | icon + type text |

必须保留文字：

| 内容 | 原因 |
| --- | --- |
| Asset name | 核心识别信息 |
| Folder name | 导航核心信息 |
| Error summary | 需要可读 |
| Empty folder state | 需要说明 |
| Source/product path | 需要复制和定位 |

## Console / Log

目标：

- Console 是全局诊断中心。
- 行样式必须克制，不用整行高饱和红。
- 高频重复日志要可折叠。
- Log 不可见时，全局状态栏镜像最新重要日志。

`icon-only`：

| 控件 | 显示方式 |
| --- | --- |
| Clear | icon-only |
| Collapse duplicates | icon-only |
| Copy | icon-only |
| Export | icon-only when in compact toolbar |
| Filter | icon-only |
| Search clear | icon-only |
| Jump to source | icon-only |
| Severity icon | icon-only |
| Expand details | icon-only |

`icon + text`：

| 控件 | 显示方式 |
| --- | --- |
| Error / Warning / Info filters | icon + text |
| Source filter dropdown | icon + selected source |
| Export Log in menu | icon + text |
| Collapse state chip | icon + count |

必须保留文字：

| 内容 | 原因 |
| --- | --- |
| Log message | 核心诊断内容 |
| Source/module name | 定位依赖文字 |
| Expanded details | 需要复制和阅读 |
| Latest error summary | 必须可读 |
| Timestamp/sequence | 调试定位需要 |

## 全局状态栏

目标：

- 位置是整个编辑器最底部。
- 不属于 Scene View。
- 固定单行，高度不跳变。
- Log 隐藏时显示最新重要日志。
- Log 可见时显示 Console 摘要。

布局：

```text
Latest Log Mirror | Background Tasks | Dirty/Play/GPU/Frame/Project Summary
```

`icon-only`：

| 控件 | 显示方式 |
| --- | --- |
| Error severity icon | icon-only |
| Warning severity icon | icon-only |
| Info severity icon | icon-only |
| Background task spinner | icon-only |
| Dirty marker | icon-only when paired with nearby text |
| Source control branch icon | icon-only when paired with branch text |
| GPU/backend icon | icon-only when paired with backend text |
| Open Console shortcut | icon-only |

`icon + text`：

| 控件 | 显示方式 |
| --- | --- |
| Error count | icon + count |
| Warning count | icon + count |
| Dirty / Clean | icon + text |
| Edit / Play / Paused | icon + text |
| Import/Cook/Shader task | icon + short text |
| GPU/backend | icon + short text |
| Project/branch | icon + text |

必须保留文字：

| 内容 | 原因 |
| --- | --- |
| Latest Log Mirror | 必须知道最新问题是什么 |
| Project name | 工程上下文 |
| Branch name | 版本上下文 |
| Long-running task label | 用户需要知道任务 |
| Failure recovery hint | 需要可读 |

窄宽度折叠规则：

1. 保留 error/warning count。
2. 保留 latest error/warning。
3. 保留 dirty/play state。
4. 隐藏 frame/panel/project 细节。
5. 永远不换行。

## RenderGraph / Frame Debug / Profiler

目标：

- 这类调试面板保持工程密度。
- 不做消费级 dashboard。
- 表格、timeline、details、selection 是主结构。
- 未来 node graph 也必须服务可读性，不做装饰图。

`icon-only`：

| 控件 | 显示方式 |
| --- | --- |
| Capture | icon-only |
| Resume | icon-only |
| Replay selected | icon-only |
| Copy pass/resource id | icon-only |
| Filter | icon-only |
| Expand/collapse details | icon-only |
| Pin snapshot | icon-only |

`icon + text`：

| 控件 | 显示方式 |
| --- | --- |
| Live / Frozen / Replay Preview state | icon + text |
| Pass/resource filter chips | icon + text |
| Selected pass summary | icon + text |
| Capture target dropdown | icon + text |

必须保留文字：

| 内容 | 原因 |
| --- | --- |
| Pass name | 调试定位 |
| Resource name | 调试定位 |
| Access type | 语义必须可读 |
| Barrier/layout/stage | 技术细节 |
| Diagnostic message | 需要解释 |

## 禁止方向

- 不把所有 toolbar 做成文字按钮。
- 不把所有按钮都做成 `icon-only`。
- 不允许无 tooltip 的 `icon-only`。
- 不使用 emoji 作为正式工具 icon。
- 不用颜色作为唯一状态表达。
- 不允许状态栏多行显示或高度跳变。
- 不允许 Scene View 内状态条显示全局日志。
- 不允许全局状态栏显示视口局部 camera/tool 细节。
- 不允许 Console 最新日志只剩一个红点。
- 不使用大面积红/黄背景表达错误和警告。
- 不做 SaaS 卡片、营销式空态、装饰渐变、巨大圆角。
- 不把 UI 设计写成当前实现清单；本文只描述目标体验。

## 验收标准

- 第一屏看起来是成熟游戏引擎编辑器，而不是 ImGui demo。
- 顶部工具栏大多数高频命令为 `icon-only`，且 tooltip 完整。
- Scene View 状态条和全局状态栏职责清晰分离。
- Hierarchy、Inspector、Project、Console 都有明确 `icon-only / icon + text / text-only` 规则。
- 所有错误、警告、dirty、disabled、selected、focused 状态都能截图验证。
- 窄窗口下不会文字重叠，不会状态栏换行。
- Log 隐藏时全局状态栏显示最新重要 log。
- Log 可见时全局状态栏只显示摘要和计数。
- 所有 `icon-only` 控件在禁用态能说明原因。
- 文档不依赖当前实际布局。

## 参考资料

- Dear ImGui: https://github.com/ocornut/imgui
- Unity Editor: https://docs.unity.cn/Documentation/Manual/UsingTheEditor.html
- Unreal Editor Interface: https://dev.epicgames.com/documentation/en-us/unreal-engine/unreal-editor-interface
- Godot Editor Interface: https://docs.godotengine.org/en/stable/tutorials/editor/editor_interface.html
- Blender Human Interface Guidelines: https://developer.blender.org/docs/features/interface/human_interface_guidelines/
- WCAG 2.2: https://www.w3.org/TR/WCAG22/
