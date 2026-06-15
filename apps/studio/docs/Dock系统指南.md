# Dock 系统指南

本文记录 Studio 当前自研 Dock 的边界、组件层级和后续高级 Dock 路线。目标不是给几个固定面板换皮，而是实现一个可完全控制窗口层级、组合结构、拖拽反馈和浮动窗口层级的 Dock 布局系统。

## 当前实现

```text
Core
  PanelDescriptor
  PanelKind
  DockArea
  DockContentCachePolicy
  IPanelRegistry

Shell
  PanelRegistry
  EditorDockWorkspaceViewModel
  EditorDockNodeViewModel
  EditorDockSplitNodeViewModel
  EditorDockPaneNodeViewModel
  EditorDockPaneViewModel
  EditorDockTabViewModel
  EditorDockFloatingWindowViewModel
  EditorDockFloatingWindowRequest
  EditorDockDragStateViewModel
  EditorDockHitTestService
  EditorDockDropTarget
  EditorDockDropOperation
  EditorDockDropGuideKind
  EditorDockPaneBounds
  EditorDockSplitterBounds
  PanelPlaceholderViewModel
  EditorDockWorkspaceView
  EditorDockDropGuideView
  EditorDockSplitNodeView
  EditorDockPaneNodeView
  EditorDockPaneView
  EditorDockFloatingWindow
  PanelPlaceholderView

Styles
  UI/Styles/Shell/Dock.axaml
```

主界面使用自研 `EditorDockWorkspaceView` 作为 Dock host。Dock.Avalonia 包和 `EditorDockFactory` 暂时保留为过渡参考路径，但不再作为主界面的视觉和交互层。

## 布局模型

固定四区 XAML 网格已经移除。当前工作区由 `RootNode` 布局树递归渲染：

```text
split-left-work (Horizontal)
  node-left -> Hierarchy
  split-work-inspector (Horizontal)
    split-center-bottom (Vertical)
      node-center -> Viewport
      node-bottom -> Diagnostics
    node-right -> Inspector
```

这仍然提供启动默认布局，但默认布局现在只是 layout graph 的种子，不是写死在 workspace 视图中的控件层级。后续 layout reset、save/restore、拖拽插入、浮动窗口和节点模板控制都应围绕 layout graph 变更，而不是修改 XAML。

`DockArea` 只保留为面板注册时的默认落区 metadata，不能作为运行时布局地址。运行时布局地址必须来自 layout graph node id、split id 和 dock surface id。

## 拖拽命中规则

当前命中顺序：

```text
1. workspace 外部 -> Float preview
2. 真实 GridSplitter bounds + hit slop -> SplitBetween preview
3. pane 可见 guide spokes -> InsertLeft / InsertRight / InsertTop / InsertBottom
4. pane center guide 或其他 pane 内区域 -> TabInto
5. 其他区域 -> Reject
```

Split 不再通过 pane 边缘比例推导。拆分落点必须来自真实 splitter 或后续显式 drop guide。这样可以把“插入到两个 split 面板之间”的语义稳定绑定到 splitter 层，而不是绑定到某个 pane 的局部坐标。
Pane 内插入命中来自可见 guide spokes 的固定几何热点，不使用 pane 尺寸比例作为隐藏判定。

当前已实现：

```text
1. split tree 驱动的默认布局渲染
2. GridSplitter resize
3. tab header 拖拽启动、移动、释放和取消
4. TabInto 跨 pane 移动
5. SplitBetween 释放后创建真实 split node 和 dock surface
6. 可见 guide spokes 释放后围绕目标 dock surface 创建真实 split node
7. 源 dock surface 为空时从 layout graph 折叠
8. 显式 drop guide overlay：Merge / Insert / Float / Reject
9. 用户创建的同向 split 子树归一化为 balanced star split
10. drag adorner、drop placeholder、pane chrome、tab tag、title extra、status text 样式
11. workspace 外释放 tab 后创建自研 floating window host
```

当前未实现：

```text
1. workspace 外边缘插入和更细的插入位置
2. tab strip 内精确插入位置
3. n-ary split group 组件和用户手调比例持久化
4. floating window 拖回主窗口、跨 floating window 投放和窗口层级策略
5. layout save/restore
6. 面板关闭、显示/隐藏命令和生命周期策略
```

## 组件定制边界

Dock 的主路径是自研组件层，不依赖第三方 Dock 控件模板：

```text
EditorDockWorkspaceView   根容器、layout tree host、overlay layer
EditorDockDropGuideView   自研拖拽 guide overlay、目标徽标、插入线和拒绝状态
EditorDockSplitNodeView   split 节点、两个子节点、真实 splitter 控件
EditorDockPaneNodeView    pane leaf wrapper
EditorDockPaneView        dock surface titlebar、tab strip、content host
EditorDockFloatingWindow  独立 Avalonia Window，承载一个自研 dock workspace
EditorDockTabViewModel    tab tag、title extra、status text、active state
EditorDockDragState       drag adorner、drop placeholder、drop label
EditorDockHitTestService  drop operation、splitter hit-test、float/reject preview
```

原则：

```text
1. 面板注册、菜单路径和内容创建走 Core/Shell 抽象，不绑定具体 Dock UI。
2. Shell 自研 Dock 控制组件层级、tab chrome、title extra、drop placeholder、drag adorner 和 splitter。
3. Split 命中优先绑定真实 layout 结构；不要回退到 pane 边缘比例。
4. 颜色必须映射到 DeepDarkColors.axaml 中的 Editor token。
5. 模板中不引入 converter、服务访问、后台任务或深层动态集合。
6. 真实面板内容由后续 Feature 切片实现，Shell 当前只消费注册描述。
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

1. Drop guide：扩展 guide 层，支持象限热点、边缘插入、精确 tab 插入和更明确的拒绝状态。
2. Layout operations：关闭 pane、空 pane 清理、n-ary split group、用户手调比例持久化、reset layout。
3. Tab operations：tab reorder、跨 pane 移动、关闭占位、键盘焦点和可访问状态。
4. Floating operations：处理 DPI、多显示器、焦点、跨窗口投放、拖回主窗口和窗口层级持久化。
5. Layout persistence：保存 split tree、tab 顺序、比例、浮动窗口位置和 active tab。
6. Panel commands：把 `Window/Panels/*` 菜单接到命令系统，基于 panel ID 显示/隐藏 dockable。
7. Real feature panels：Scene View、Hierarchy、Inspector、Console、Problems 迁入 `Features/*`。

## 性能约束

默认布局面板数量保持小集合。不要把资源树、日志、问题列表或场景对象直接展开成大规模 Avalonia 控件；真实面板必须在各自 Feature 内做虚拟化、分页、批处理和资源释放。
