# Dock 系统指南

本文记录当前 Dock 切片的边界和后续高级 Dock 的实现路线。当前阶段只验证停靠能力，不实现真实业务面板内容。

## 当前实现

```text
NuGet
  Dock.Avalonia 12.0.0.2
  Dock.Avalonia.Themes.Fluent 12.0.0.2
  Dock.Model.Mvvm 12.0.0.2
  Dock.Serializer.SystemTextJson 12.0.0.2

Core
  PanelDescriptor
  PanelKind
  DockArea
  DockContentCachePolicy
  IPanelRegistry

Shell
  PanelRegistry
  EditorDockFactory
  EditorDockWorkspaceViewModel
  EditorDockPaneViewModel
  EditorDockTabViewModel
  EditorDockDragStateViewModel
  PanelPlaceholderViewModel
  EditorDockWorkspaceView
  EditorDockPaneView
  PanelPlaceholderView

Styles
  UI/Styles/Shell/Dock.axaml
```

默认布局：

```text
Root
  Horizontal workspace
    Left ToolDock: Hierarchy
    Center vertical split
      DocumentDock: Scene View
      Bottom ToolDock: Console, Problems
    Right ToolDock: Inspector
```

主界面当前使用自研 `EditorDockWorkspaceView` 作为 Dock host。Dock.Avalonia 包和 `EditorDockFactory` 暂时保留为过渡/参考路径，但不再作为主界面的视觉和交互层。所有面板内容目前只显示标题。菜单路径预留为 `Window/Panels/{PanelTitle}`，本阶段不绑定命令、不打开或关闭真实面板。

## 组件级定制路线

Dock 的主路径是自研组件层，而不是第三方 Dock 控件模板。`UI/Styles/Shell/Dock.axaml` 同时包含自研 Dock 组件样式和过渡期 Dock.Avalonia 资源映射；主界面只消费自研组件样式。

当前定制范围：

```text
Owned dock components
  EditorDockWorkspaceView  自有 Dock 根、固定四区布局、overlay layer
  EditorDockPaneView       pane chrome、pane titlebar、tab strip、content host
  EditorDockTabViewModel   tab tag、title extra、status text、active state
  EditorDockDragState      drag adorner、drop placeholder、drop label
  GridSplitter             自有 splitter 外观和 resize 手感

Dock.Avalonia transition tokens
  DockSurface*             工作区、侧栏、文档区、工具面板、标题栏表面
  DockBorder*              边框、分隔线、内容边界
  DockSplitter*            splitter idle / hover / pressed 状态
  DockTab*                 document tab / tool tab 背景、前景、激活指示、关闭 hover
  DockChrome*              工具面板标题栏按钮、图标、拖拽 grip
  DockSelectorOverlay*     拖拽目标 overlay 背板、尺寸、圆角、badge
  DockDragPreview*         拖拽预览浮层尺寸、圆角、状态文字

Selectors
  owned-dock-root
  owned-dock-pane
  owned-dock-pane-titlebar
  owned-dock-tab / owned-dock-tab.active
  owned-dock-tab-tag
  owned-dock-tab-status
  owned-dock-drop-preview
  owned-dock-drag-adorner
  owned-dock-splitter
```

原则：

```text
1. 面板注册、菜单路径和内容创建仍走 Core/Shell 抽象，不绑定具体 Dock UI。
2. 自研 Dock host 控制组件层级、tab chrome、title extra、drop placeholder、drag adorner 和 splitter。
3. 不复制第三方完整模板；Dock.Avalonia 只作为过渡参考或后续 adapter 候选。
4. 所有颜色都映射到 `DeepDarkColors.axaml` 中的 Editor token。
5. 不在模板中加入 converter、服务访问、后台任务或深层动态集合。
6. 真实面板内容仍由后续 Feature 切片实现，当前只验证 Dock 行为和 chrome 视觉。
```

## 注册规则

Feature 后续只注册 `PanelDescriptor`，不要直接创建 Dock 控件或 View：

```text
Id            稳定、小写、可持久化，例如 scene-view
Title         用户可见标题，例如 Scene View
Kind          Document 或 Tool
DefaultArea   Center / Left / Right / Bottom
MenuPath      Window/Panels/{PanelTitle}
CachePolicy   KeepAlive 或 RecreateOnOpen
CreateContent 创建轻量内容 ViewModel
```

Dock.Avalonia 类型只允许出现在 Shell/Docking 或后续 Infrastructure 持久化适配中，不能进入 Core。

## 后续切片

1. 自研 Dock hit-test：把当前四区矩形判定升级为组件级 drop target 服务，支持边缘插入、中心 tab、拒绝状态。
2. Tab 操作能力：tab reorder、跨 pane 移动、关闭占位、激活状态和键盘焦点。
3. Split tree：把固定四区布局升级为可序列化 split tree，支持任意水平/垂直拆分。
4. 浮动窗口：实现自有 floating host，处理 DPI、多显示器、焦点、拖回主窗口和窗口层级。
5. 面板显示/隐藏命令：把 `Window/Panels/*` 菜单项接到命令系统，基于面板 ID 操作现有 dockable。
6. 布局重置和保存/恢复：保存自研 split tree、tab 顺序、比例、浮动窗口位置和激活状态。
7. 面板生命周期：按 `DockContentCachePolicy` 决定复用或重建内容 ViewModel。
8. 真实 Feature 面板：Scene View、Hierarchy、Inspector、Console、Problems 各自迁入 `Features/*`，Shell 只消费注册描述。

## 性能约束

默认布局面板数量保持小集合。不要把资源树、日志、问题列表或场景对象直接展开成大量 Avalonia 控件；真实面板必须在各自 Feature 内做虚拟化、分页、批处理和资源释放。
