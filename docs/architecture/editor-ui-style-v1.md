# Asharia Editor UI Style v1

更新日期：2026-05-22

本文定义 Asharia Editor 第一版视觉与交互风格目标。它是设计规范和后续实现约束，不替代
`docs/architecture/editor.md` 的当前架构事实，也不替代 `docs/planning/editor-development-plan.md` 的阶段拆分。

配色值以当前 `Classic Blue Gray 2.0 / Night Slate` 主题实现为准。本文不重新定义具体 hex 色表，重点约束布局、
密度、组件状态、viewport 视觉系统、主题数据结构和颜色空间接入方式。

## 定位

Asharia Editor UI Style v1 的定位是：

```text
Technical Slate Editor
```

目标是做一套技术型深色编辑器界面：

- UI 面板清晰、紧凑、低噪音。
- Viewport 沉浸、克制，不抢场景内容。
- Debug 信息高密度但可扫视。
- 交互状态明确、稳定、少动画。
- 主题系统分离设计色、ImGui 表示和渲染色。

禁止方向：

- 不做大面积亮蓝或高饱和霓虹色。
- 不做强渐变、装饰背景或卡片化 SaaS 页面。
- 不做过大圆角。
- 不让表格线、边框和 hover 状态铺满整屏。
- 不把 `ImVec4` 当成主题数据模型。

## 布局模型

主编辑器采用标准 docking 工具布局：

```text
Title Bar
Menu Bar
Command Bar
Left Dock | Center Dock | Right Dock
Bottom Dock
Status Bar
```

区域职责：

| 区域 | 职责 | 约束 |
| --- | --- | --- |
| Center Dock | Scene View、RenderGraph、Frame Debugger、Profiler、Texture Viewer | 视觉中心，优先给 viewport 和图形调试内容 |
| Left Dock | Scene Tree、Assets、Resources | 扫描和导航，不承载重型编辑 |
| Right Dock | Inspector、Properties、Theme Workbench details | 属性编辑和对象详情 |
| Bottom Dock | Console、Log、Validation、Profiler timeline | 高密度日志和状态 |
| Command Bar | Play、Pause、Step、Capture、Search、Layout | 紧凑命令区，不做网页导航 |
| Status Bar | Ready、FPS、GPU、Project、Git、Warnings | 只放持续状态，不放大按钮 |

建议尺寸：

| 区域 | 建议 |
| --- | --- |
| Left Dock | 240-300 px |
| Right Dock | 300-380 px |
| Bottom Dock | 180-260 px |
| Command Bar | 32-36 px |
| Status Bar | 22-26 px |
| Tab Bar | 26-30 px |
| Table Row | 22-26 px |

主视窗永远是视觉中心。侧边栏和底栏服务于视窗，不能用更强的颜色、边框或动画抢权重。

## 密度和字体

UI 采用 compact editor tooling 密度。默认使用 4px grid：

| 尺寸 | 用途 |
| --- | --- |
| 2 px | 图标与文字的细间距 |
| 4 px | 控件内部细间距 |
| 6 px | compact item spacing |
| 8 px | panel padding |
| 12 px | group spacing |
| 16 px | section spacing |
| 24 px | 大区块间距 |

字体分层：

| 内容 | 字体方向 |
| --- | --- |
| 普通 UI 文本 | Sans |
| 中文 | Noto Sans SC / 思源黑体方向 |
| 数值、hex、resource id | Mono |
| Console / Log | Mono |
| Shader / Code | Mono |

字号方向：

| 内容 | 建议 |
| --- | --- |
| Menu / Status | 12 px |
| 普通 UI | 13 px |
| Table / Tree | 12-13 px |
| Section Title | 13 px semibold |
| Page Title | 15-16 px |
| Console | 12 px mono |

不要全局使用 mono。普通 UI 使用 sans，数值和调试信息使用 mono。

## 组件状态模型

所有组件统一使用这套状态：

```text
Default
Hover
Focused
Active / Pressed
Selected
Disabled
Error
Warning
```

状态表达规则：

| 状态 | 表达 |
| --- | --- |
| Hover | 轻微提亮 |
| Focused | accent 边框或细线 |
| Active / Pressed | 深色 active 填充 |
| Selected | 深色 selection 背景和高对比文字 |
| Disabled | 降低文字、图标和边框对比 |
| Error | danger 边框、小图标或 chip，不整块铺红 |
| Warning | warning chip 或小标记，不整块铺黄 |

Button：

- Default 使用 surface 背景、border 边框、text 文字。
- Hover 只提亮 background。
- Pressed / Active 使用 active surface，并提升文字对比。
- Primary button 只用于真正关键动作，不作为普通 toolbar 默认样式。

Input：

- 输入框比按钮更内陷。
- Focus 只加强边框或焦点线，不让整个输入框变成亮蓝。
- Placeholder 和 disabled text 使用独立弱文本层级。

Tabs：

- Inactive tab 使用 panel alt 和 secondary text。
- Hover tab 使用 surface hover。
- Active tab 使用 panel background 或 title background，加 2px accent underline。
- 不使用整块亮蓝 active tab。

Tables / Lists / Trees：

- 只保留弱横线，少用竖线。
- Row default、row alt、hover、selected 必须形成清晰但克制的层级。
- 数字列右对齐，资源名左对齐，状态列使用 chip。
- 表头不要比内容区更抢眼。

Status chip：

- Ready、Pending、Blocked、Idle、Info 必须低饱和。
- chip 是状态，不是按钮。
- 状态色只用于状态语义，不用于装饰。

Toolbar：

- 高度保持 32-36 px。
- Button 默认可以 transparent 或 surface。
- Icon size 14-16 px。
- 使用 divider 做命令分组。
- 命令组建议为 playback、capture、view、debug、search。

Inspector：

- 使用固定 label width 和 fill control。
- Section header 使用 panel alt，property row 使用 panel background。
- Modified marker 用小点或左侧短线。
- Error marker 用 danger 小图标，不整行红底。

## Viewport 视觉系统

Viewport 不复用普通 panel 的视觉权重。它有自己的 token 组和状态系统。

目标：

- 场景内容是主角。
- overlay 最小化。
- grid、selection、gizmo、debug overlay 有明确层级。
- Scene View、Game View、Preview、Texture Viewer 的状态可区分。

Viewport 应比普通 panel 更暗、更干净。普通 UI 使用 `ui.*` token，viewport 使用独立 `viewport.*` token。

建议 token 分组：

```text
viewport.bg
viewport.clear
viewport.border
viewport.focusBorder
viewport.gridMinor
viewport.gridMajor
viewport.horizon
viewport.overlayBg
viewport.overlayBorder
viewport.overlayText
viewport.overlayWeak
viewport.selection
viewport.selectionFill
viewport.hoverOutline
viewport.warning
viewport.error
viewport.axisX
viewport.axisY
viewport.axisZ
viewport.gizmoActive
viewport.gizmoPlane
viewport.cameraFrame
viewport.safeArea
```

带 alpha 的颜色仍然是 sRGBA authoring color，存储为 `ColorSrgba8`，不要存为 `ImVec4`。

Viewport header：

- 高度 26-30 px。
- 显示 view name、view mode、projection、grid/gizmo state、resolution。
- 比普通 tab 更轻，不使用大面积高亮。

Focus：

- Inactive border 使用弱 divider。
- Hover border 使用 border。
- Focused border 使用 accent，1-2 px。
- 不改变整个 viewport 背景亮度。

Grid：

- minor grid 低 alpha。
- major grid 稍强。
- origin / axis line 比 grid 强，但不能像 selection。
- 远处逐渐 fade。

Selection：

- Hover 只显示细 outline。
- Selected 显示 outline + low-alpha fill + pivot。
- Active transform 对象显示更亮 gizmo。
- Error object 使用 danger outline，不整块红。

Gizmo：

- X/Y/Z 轴使用独立轴色。
- Hover 轴提高 alpha 或线宽。
- Active 轴使用 warning / gizmoActive。
- 禁用轴使用 disabled text。
- 不做强 glow。

Overlay：

- 使用半透明背景、弱边框、紧凑 padding。
- Top-left 放 view mode、camera、selection、tool mode。
- Top-right 放 view cube / axis gizmo。
- Bottom-left 放 hint / shortcut。
- Bottom-right 放 FPS、GPU、resolution、render scale。

Texture Viewer / Frame Debugger Viewport：

- 使用更暗 texture viewer background。
- 支持 checkerboard、channel toggle、exposure、mip、slice、pixel inspect。
- Header 显示资源名、format、extent、mip、slice、color-space。
- Pixel inspect 使用 mono 字体。

## Theme Workbench

当前 UI Style Preview 后续应演进成 Theme Workbench。它不是营销页面，而是编辑器内的工程化主题验证工具。

推荐布局：

```text
Tokens | Component Preview | Inspector
```

Tokens：

- 按 Background、Surface、Text、Accent、State、Viewport 分组。
- 显示 swatch、token name、hex。
- 不直接暴露 ImGui color index 作为主模型。

Component Preview：

- Command Bar。
- Buttons。
- Form Controls。
- Tabs。
- Tree View。
- Table。
- Status Chips。
- Inspector mock。
- Viewport mock。
- Texture Viewer mock。

Inspector：

- 当前 token 的 hex。
- sRGB byte。
- linear preview。
- usage。
- contrast。
- do / don't。
- related tokens。

## 主题数据结构

主题文件应按语义分组，而不是平铺全部 token，也不直接保存 ImGui style。

建议结构：

```json
{
  "name": "Classic Blue Gray 2.0",
  "id": "classic-blue-gray-2",
  "density": "compact",
  "colorEncoding": "srgb8",
  "ui": {
    "appBg": "#171D24",
    "panelBg": "#202833"
  },
  "viewport": {
    "bg": "#10161D",
    "gridMinor": "#263342"
  },
  "metrics": {
    "windowPadding": [8, 8],
    "framePadding": [6, 3],
    "itemSpacing": [6, 4],
    "cellPadding": [6, 3],
    "windowRounding": 4,
    "childRounding": 3,
    "frameRounding": 3,
    "popupRounding": 4,
    "tabRounding": 3,
    "scrollbarSize": 12,
    "rowHeight": 24,
    "toolbarHeight": 34,
    "statusBarHeight": 24
  }
}
```

Rules:

- `Theme JSON` stores authoring sRGB hex.
- `EditorUiTheme` stores `ColorSrgba8`.
- `EditorImGuiAdapter` converts to encoded sRGB values for Dear ImGui.
- Dear ImGui does not own theme semantics.
- Renderer does not change swapchain policy for editor UI.

## 颜色空间和渲染接入

已落地的主线继续作为规范：

```text
Theme JSON / C++
  sRGB authoring hex

EditorUiTheme
  ColorSrgba8

EditorImGuiAdapter
  encoded sRGB ImVec4 / ImU32

ImGui draw data
  packed encoded sRGB vertex color

Editor ImGui shader
  vertex color rgb: sRGB -> linear
  texture rgb: already in pass working space
  output linear RGB

SRGB attachment / presentation
  final encode
```

Texture color-space is independent from vertex color:

- sRGB color images use `_SRGB` image views.
- Linear render textures use linear views.
- Font coverage、mask、depth/data/debug textures keep non-color semantics.
- The UI shader does not guess texture color-space from render target format.

Editor UI should be drawn after scene tone mapping and color grading:

```text
Scene HDR
  -> tone map
  -> color grade
  -> LDR linear or SRGB target
  -> Editor UI
  -> present
```

Do not draw editor UI into HDR scene color before tone mapping, otherwise design colors will be altered by scene grading.

## 落地阶段

Stage 1 - 基础 UI：

- Built-in themes and current theme persistence.
- `ColorSrgba8` theme storage.
- encoded sRGB ImGui adapter.
- Button / input / tab / table / chip state unification.
- Basic ImGui style metrics.

Stage 2 - Theme Workbench：

- Three-column Tokens / Preview / Inspector layout.
- Component Preview.
- Viewport mock.
- Texture Viewer mock.
- Contrast and usage checks.

Stage 3 - Viewport visual system：

- Independent viewport token group.
- Viewport header.
- Focus border.
- Grid fade.
- Selection outline.
- Gizmo axis colors.
- Overlay panels.
- Texture viewer debug style.

Stage 4 - Rendering integration：

- Custom ImGui fragment shader.
- sRGB vertex color decode.
- UI pass after tone mapping.
- UI texture color-space audit.
- Screenshot or swatch readback validation.

## Validation

Style changes should keep the normal editor validation gates:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
powershell -ExecutionPolicy Bypass -File tools\check-doc-sync.ps1
cmd /c "build\conan\clangcl-debug\Debug\generators\conanbuild.bat && cmake --preset clangcl-debug && cmake --build --preset clangcl-debug"
cmd /c "build\conan\msvc-debug\Debug\generators\conanbuild.bat && cmake --preset msvc-debug && cmake --build --preset msvc-debug"
```

Editor shell changes must run:

```powershell
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-shell
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-shell
```

Viewport or texture registry changes must also run:

```powershell
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\clangcl-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-viewport-resize
build\cmake\msvc-debug\apps\editor\asharia-editor.exe --smoke-editor-frame-debugger
```

Future color validation should add one focused readback test:

```text
input authoring color: #171D24
draw no-blend swatch through editor UI pass
read back SRGB attachment bytes
expected RGB: 17 1D 24, tolerance +/- 1
```

## Current Status

Implemented:

- Built-in editor themes with `Classic Blue Gray 2.0` as default.
- User setting persistence for theme selection.
- `ColorSrgba8` theme storage.
- encoded sRGB ImGui adapter.
- custom ImGui fragment shader with vertex color sRGB decode.
- UI texture color-space metadata propagation for editor viewport textures.

Planned:

- Theme JSON externalization.
- Viewport-specific tokens.
- Theme Workbench.
- Viewport header, grid fade, selection outline and gizmo styling.
- Texture Viewer style and pixel inspect UI.
