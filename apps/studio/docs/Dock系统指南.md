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
  PanelPlaceholderViewModel
  PanelPlaceholderView
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

所有面板内容目前只显示标题。菜单路径预留为 `Window/Panels/{PanelTitle}`，本阶段不绑定命令、不打开或关闭真实面板。

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

1. 面板显示/隐藏命令：把 `Window/Panels/*` 菜单项接到命令系统，基于面板 ID 操作现有 dockable。
2. 布局重置：保留 `EditorDockFactory.CreateLayout()` 作为默认布局来源。
3. 布局保存/恢复：使用 `Dock.Serializer.SystemTextJson` 保存轻量布局快照，恢复失败时回退默认布局。
4. 面板生命周期：按 `DockContentCachePolicy` 决定复用或重建内容 ViewModel。
5. 真实 Feature 面板：Scene View、Hierarchy、Inspector、Console、Problems 各自迁入 `Features/*`，Shell 只消费注册描述。

## 性能约束

默认布局面板数量保持小集合。不要把资源树、日志、问题列表或场景对象直接展开成大量 Avalonia 控件；真实面板必须在各自 Feature 内做虚拟化、分页、批处理和资源释放。
