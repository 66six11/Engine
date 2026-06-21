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
  EditorDockWorkspaceKind
  EditorDockWorkspaceViewModel
  EditorDockNodeViewModel
  EditorDockSplitNodeViewModel
  EditorDockWindowNodeViewModel
  EditorDockWindowViewModel
  EditorDockTabViewModel
  EditorDockFloatingWindowViewModel
  EditorDockFloatingWindowRequest
  EditorDockDragStateViewModel
  EditorDockHitTestService
  EditorDockDropTarget
  EditorDockDropOperation
  EditorDockDropGuideKind
  EditorDockWindowBounds
  EditorDockSplitterBounds
  PanelPlaceholderViewModel
  PanelCommandService
  WorkbenchActionExecutor
  EditorDockWorkspaceView
  EditorDockDropGuideView
  EditorDockSplitNodeView
  EditorDockWindowNodeView
  EditorDockWindowView
  EditorDockTabStripView
  EditorDockTabItemView
  EditorDockFloatingWindow
  PanelPlaceholderView

Styles
  Dock 组件样式内嵌在对应 `Shell/Views/*.axaml`
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
2. tab strip -> InsertTabAtIndex
3. workspace root edge band -> InsertWorkspaceLeft / InsertWorkspaceRight / InsertWorkspaceTop / InsertWorkspaceBottom
4. 真实 GridSplitter bounds + hit slop -> SplitBetween preview
5. window 可见 guide spokes -> InsertLeft / InsertRight / InsertTop / InsertBottom
6. 没有命中明确停靠点 -> Float preview
```

Split 不再通过 window 边缘比例推导。拆分落点必须来自真实 splitter 或后续显式 drop guide。这样可以把“插入到两个 split 面板之间”的语义稳定绑定到 splitter 层，而不是绑定到某个 window 的局部坐标。
Window 内插入命中来自可见 guide spokes 的固定几何热点，不使用 window 尺寸比例作为隐藏判定。
拖拽源 window content 在拖拽期间只显示 disabled 态，不参与自身 window body 的插入命中；没有明确停靠点时创建 floating window。Float preview 复用 window surface 视觉结构，只展示 tab 栏加内容区，不再额外显示随鼠标移动的 drag adorner。Float preview 不参与 window/workspace 边界碰撞或停靠命中，鼠标锚点落在预览窗口的 tab 位置，释放时才创建真正的顶层 floating window。源 tab 的原地插入位置返回 Reject，避免当前未加拖拽阈值时单击 tab 直接浮动。

当前已实现：

```text
1. split tree 驱动的默认布局渲染
2. GridSplitter resize
3. tab header 拖拽启动、移动、释放和取消
4. TabInto 跨 window 移动
5. SplitBetween 释放后创建真实 split node 和 dock surface
6. 可见 guide spokes 释放后围绕目标 dock surface 创建真实 split node
7. 源 dock surface 为空时从 layout graph 折叠
8. 显式 drop guide overlay：Merge / Insert / Float / Reject
9. 用户创建的同向 split 子树归一化为 balanced star split
10. drop placeholder、window border、tab tag、title extra、status text 样式
11. workspace 外释放 tab 后创建独立 `EditorDockFloatingWindow`
12. floating window 内部承载独立 `EditorDockWorkspaceView`
13. 区分 workspace 内部 active window 和独立 floating window host
14. 根 layout window 被拖空后隐藏，不保留 0-tab 空 surface 占位
15. docked/floating window surface 始终保留 tab strip，单 tab 也通过 tab strip 拖拽、插入和激活
16. TabInto / InsertTabAtIndex 命中只来自真实 tab strip，window body 和 titlebar 不再默认合并
17. workspace 边缘释放 tab 后创建新 window，并插入整棵 root layout tree 外侧
18. 多 tab strip 支持按 tab 中线计算 `InsertTabAtIndex`，同 window 拖拽中实时 reorder，并用短位移动画交换 tab
19. floating window 的最后一个 tab 被移走后，空的 `EditorDockFloatingWindow` host 自动关闭
20. `InsertTabAtIndex` 预览使用 tab 形占位块，并用 FLIP 位移动画跟随目标 index，不再使用单独 caret 作为主要插入反馈
21. 拖拽源 tab 和源 window content 进入 disabled 视觉态，源 window body 不作为自己的 drop target
22. 没有命中明确停靠点时不再 Reject，释放后创建顶层 floating window
23. Float preview 不再按 workspace/window bounds clamp，拖拽时只作为跟随指针的 ghost 显示
24. Float preview 复用 `EditorDockWindowSurfaceView` 和 `EditorDockTabItemView`，呈现为 tab 栏加内容区，鼠标锚点位于 tab 上
25. layout save/restore、floating window restore、active window / active tab 恢复
26. floating window placement 的 invalid bounds 修正和 DPI-aware working area clamp
27. `KeepAlive` / `RecreateOnOpen` 内容创建策略，restore 只按 snapshot 中出现的 panel 懒创建
28. `Window/Panels/*` 菜单从 `WorkbenchActionDescriptor` 生成，panel action 执行时先通过 `WorkbenchActionExecutor` 激活已有 panel，再按默认区域重开
29. Scene View、Hierarchy、Inspector、Console、Problems 已迁入 `Features/*`，以空面板壳进入 Dock 容器；Feature 数据接入前不定义内部布局样式
30. Command Palette v0 复用 `WorkbenchActionDescriptor`，当前只执行已注册的 workbench actions，不引入独立命令数据源
31. `WorkbenchCommandRouter` 是当前 command-id execution route，返回 `WorkbenchCommandExecutionResult` typed result；`WorkbenchActionExecutor` 仍是 descriptor-level dispatcher，`OpenPanel` action 通过 `PanelCommandService.OpenOrFocusPanel` 执行
32. Command Palette 可通过 catalog-backed `Tools > Command Palette` 或 `Ctrl+Shift+P` 打开；内建快捷键已由 `WorkbenchShortcutRouter` 解析 `WorkbenchActionDescriptor.DefaultShortcut` 并路由到 `WorkbenchCommandRouter`
33. Selection Contract v0 定义 `IEditorSelectionService`、selection snapshot、active context 和 selection changed 事件；Shell 创建服务并通过 Feature 注册路径注入 Hierarchy、Scene View 和 Inspector
34. Inspector Data Model v0 将 selection snapshot 派生成只读 `InspectorDocumentModel`，支持无选中、单选基础属性和多选摘要；Inspector View 只渲染只读摘要，不做编辑器和真实引擎数据查询
35. Hierarchy demo data source v0 已由共享 scene snapshot provider 取代；当前 Hierarchy 从 `ISceneSnapshotProvider` 的只读 snapshot 投影可选节点，用于 UI 验证和 selection-to-inspector 数据流测试
36. Scene snapshot provider v0 在 Core 定义只读 scene/object/property snapshot contract，Workbench 以 fixture-backed provider 同时注入 Hierarchy 与 Inspector；Hierarchy 通过 `ISceneSnapshotProvider.GetCurrentSnapshot()` 只做树投影和 selection 写入，Inspector 通过 selection id 反查同一 snapshot 生成只读属性文档，不做 Transform 写回或真实 runtime scene 查询
37. Scene snapshot provider refresh seam remains read-only: `InMemorySceneSnapshotProvider.ReplaceSnapshot(SceneSnapshot)` can publish replacement snapshots and raise `SnapshotChanged`; Hierarchy and Inspector refresh their read-only projections through the UI dispatcher, but this does not write Transform data and does not query a native runtime scene.
38. Main menu command projection v0 从 `WorkbenchActionDescriptor.MenuPath` 生成 `Tools/*`、`Help/*` 和 `Window/Panels/*` 入口；Tools/Help 使用 `WorkbenchMenuItemViewModel`，Window/Panels 保留 `PanelMenuItemViewModel` 的 open-state indicator
39. Dialog host v0 由 `EditorDialogHostViewModel` 和 `EditorDialogHostView` 承载单 active in-app modal；请求/结果使用 typed `EditorDialogRequest` / `EditorDialogResult`，`Help > About` 通过 catalog-backed command route 打开
40. Background activity feedback v0 由 `IEditorBackgroundTaskService` 发布 UI-neutral task snapshot，Shell status chrome 使用 `ActivityIndicator` 显示当前运行任务摘要；完整 Background Tasks 面板仍属后续切片
41. Transaction service v0 提供 UI-neutral `IEditorTransactionService`、edit command descriptor、begin/commit/rollback/undo/redo、dirty-state snapshot 和 diagnostics；apply 失败会回滚 pending commands 并记录 diagnostic；它只管理编辑器命令历史，不写 Transform、不查询真实 runtime scene，也不接 native ABI
```

当前未实现：

```text
1. tab strip overflow、自滚动和多行策略
2. n-ary split group 组件和更完整的比例编辑体验
3. floating window 窗口层级、跨屏手工验证和高级生命周期策略
4. 用户可编辑快捷键策略、快捷键冲突 UI、更多 action kind 和命令结果弹出/日志反馈
5. Hierarchy / Inspector 真实数据 provider、Project/Console 数据源接入
```

## 组件定制边界

Dock 的主路径是自研组件层，不依赖第三方 Dock 控件模板：

```text
EditorDockWorkspaceView   根容器、layout tree host、overlay layer
EditorDockDropGuideView   自研拖拽 guide overlay、目标徽标、插入线和拒绝状态
EditorDockFloatingPreviewView  Float 拖拽预览，复用 window surface 和 tab item 视觉结构
EditorDockSplitNodeView   split 节点、两个子节点、真实 splitter 控件
EditorDockWindowNodeView    window leaf wrapper
EditorDockWindowView        dock surface container、tab strip host、content host
EditorDockWindowSurfaceView    window surface 外壳，承载 tab strip content 和 body content
EditorDockTabStripView      tab strip 容器，后续承载 overflow、滚动、工具按钮
EditorDockTabItemView       单个 tab 的视觉结构，后续承载关闭、pin、状态点
EditorDockFloatingWindow  独立 Avalonia Window，承载一个 `EditorDockWorkspaceView`
EditorDockTabViewModel    tab tag、title extra、status text、active state
EditorDockTabStripItemViewModel  tab strip 视图投影，可包含真实 tab 或拖拽占位 tab
EditorDockDragState       transient dragged tab、drop preview 和 Float preview tab 投影
EditorDockHitTestService  drop operation、splitter hit-test、float/reject preview
```

## 状态边界

Dock 当前明确区分两类状态：

```text
Workspace 内部活动窗口
  EditorDockWorkspaceViewModel.ActiveWindow
  EditorDockWindowViewModel.IsActiveWindow
  表示当前 workspace 内获得焦点/激活语义的 dock window。

独立窗口
  EditorDockWorkspaceKind.FloatingWindow
  EditorDockFloatingWindow
  表示顶层 Avalonia Window host。当前 Float drop 直接创建此类窗口。
```

原则上，拖拽预览进入另一个 workspace 时不能直接改变其 active window；只有 tab 真正落入、合并、插入、独立浮动或被用户激活时，才更新目标 workspace 的 active window。当前所有 Float drop 都创建独立 Avalonia Window。

Surface 当前按 host 分成两种呈现，tab strip 始终保留：

```text
Docked window
  显示 docked tab strip；tab strip 是 TabInto / InsertTabAtIndex 入口。

Floating window
  显示 floating tab strip；tab strip 是 TabInto / InsertTabAtIndex 入口，floating chrome 继续负责移动和 resize。
```

合并命中必须绑定到 tab well：

```text
1. splitter bounds -> SplitBetween
2. tab strip -> InsertTabAtIndex / TabInto
3. docked window body guide hotspots -> InsertLeft / InsertRight / InsertTop / InsertBottom
4. floating window body -> Float
5. window body 其他区域 -> Float
6. source tab 原地插入 -> Reject
```

Tab strip 由两层模型组成：

```text
EditorDockWindowViewModel.Tabs
  只保存真实 tab；参与 active tab、content、关闭、移动、保存/恢复。

EditorDockWindowViewModel.TabStripItems
  只服务当前视图布局；由真实 Tabs 加上可选拖拽 placeholder 组成。
  placeholder 不参与 active/content/persistence，释放成功后才把真实 tab 提交到 Tabs。
```

原则：

```text
1. 面板注册、菜单路径和内容创建走 Core/Shell 抽象，不绑定具体 Dock UI。
2. Shell 自研 Dock 控制组件层级、tab chrome、title extra、drop placeholder、drag adorner 和 splitter；组件样式保存在对应 view 文件内。
3. Split 命中优先绑定真实 layout 结构；不要回退到 window 边缘比例。
4. 颜色必须映射到 DeepDarkColors.axaml 中的 Editor token。
5. 模板中不引入 converter、服务访问、后台任务或深层动态集合。
6. 真实引擎数据和面板内部布局由后续 Feature 切片实现；Shell 当前只消费注册描述和空内容 ViewModel。
7. Feature 面板在真实数据模型接入前不放通用占位布局、空态文案或临时样式，避免误导后续实现。
```

## 注册规则

Feature 只注册 `PanelDescriptor` 和 `WorkbenchActionDescriptor`，不要直接创建 Dock 控件或 View：

```text
Id            稳定、小写、可持久化，例如 scene-view
Title         用户可见标题，例如 Scene View
Kind          Document 或 Tool
DefaultArea   Center / Left / Right / Bottom
MenuPath      当前仍保留为 panel 元数据和 TitleDetail fallback；菜单生成以 WorkbenchActionDescriptor.MenuPath 为准
CachePolicy   KeepAlive 或 RecreateOnOpen
CreateContent 创建空内容 ViewModel；真实布局由对应 Feature 后续切片定义
```

Panel 菜单入口由 workbench action 注册：

```text
Id        稳定 action id，例如 workbench.panel.scene-view
Title     用户可见菜单标题
Kind      OpenPanel
MenuPath  Window/Panels/{PanelTitle}
TargetId 目标 PanelDescriptor.Id
IconKey   可选图标 key，由 Shell icon registry 解析
```

Command Palette v0 也消费同一份 workbench action 列表：

```text
Search   匹配 Title、MenuPath 或 action Id
Execute  OpenPanel action 通过 WorkbenchActionExecutor 复用 PanelCommandService.OpenOrFocusPanel
Shortcut Ctrl+Shift+P 打开 Command Palette；不支持用户自定义或冲突检测
State    打开/关闭、查询文本和选中项只属于 MainWindow UI 状态，不写入 Dock layout snapshot
```

Dock.Avalonia 类型只允许出现在 Shell/Docking 或后续 Infrastructure 持久化适配中，不能进入 Core。

## 后续切片

1. Tab strip overflow：支持 overflow、自滚动、多行策略和更明确的拒绝状态。
2. Layout operations：n-ary split group、更完整的比例编辑体验和 reset layout 细节。
3. Floating window operations：补充窗口层级策略、跨屏手工验证和高级生命周期行为。
4. Command palette follow-up：增加分组、最近使用和更多 action kind，但暂不做插件命令 API 或完整快捷键编辑器。
5. Hierarchy provider follow-up：将 fixture-backed scene snapshot provider 替换为真实 scene object provider，并保留 `IEditorSelectionService` 作为面板同步边界。
6. Inspector provider follow-up：扩展真实 scene object / asset provider 的只读属性来源；编辑器控件和写回另做独立切片。
7. Project/Console 数据接入：接入真实 asset index / diagnostics source，但仍保持各自 Feature 边界。

## 性能约束

默认布局面板数量保持小集合。不要把资源树、日志、问题列表或场景对象直接展开成大规模 Avalonia 控件；真实面板必须在各自 Feature 内做虚拟化、分页、批处理和资源释放。
