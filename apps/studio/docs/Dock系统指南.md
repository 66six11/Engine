# Dock 系统指南

状态：Superseded（历史实现记录）

> R0已删除无真实Document/panel consumer的Dock、Workbench、Dialog与built-in Feature surface。本文只保存历史行为，不陈述当前production能力；当前合同见[Studio前端硬切架构](architecture/studio-frontend-hard-cut.md)。
>
> 2026-08-13 current addendum：production Dock已由后续Slice重建；#381注册一个stable `diagnostics` tool panel，
> 内部Console/Problems是两个view tab，不是两个Dock panel。权威diagnostic ownership与数据流见
> [Studio开发态可观测性](architecture/studio-development-observability.md#72-diagnosticslogs-与-cursor)。

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
25. Tab strip overflow v0 将 docked/floating tab strip 保持为单行水平滚动容器，active tab 和本地 reorder 目标会通过 view-only scroll offset 保持可见；左右实色强调色 overflow 热区在悬浮时自动滚动；hit-test 使用逻辑 tab content origin 保持 scrolled tab strip 的插入目标稳定，滚动状态不写入 layout snapshot
26. layout save/restore、floating window restore、active window / active tab 恢复
27. floating window placement 的 invalid bounds 修正和 DPI-aware working area clamp
28. `KeepAlive` / `RecreateOnOpen` 内容创建策略，restore 只按 snapshot 中出现的 panel 懒创建
29. `Window/Panels/*` 菜单从 `WorkbenchActionDescriptor` 生成，panel action 执行时先通过 `WorkbenchActionExecutor` 激活已有 panel，再按默认区域重开
30. Historical：Scene View、Hierarchy、Inspector、Console、Problems曾迁入`Features/*`空面板壳；该路径已删除，不能作为当前注册依据
31. Command Palette follow-up 继续复用 `WorkbenchActionDescriptor` 和 `WorkbenchCommandRouter`，支持 category 分组、in-memory recent commands 和 local command result feedback；当前仍只执行已注册的 workbench actions，不引入插件命令 API、完整快捷键编辑器、真实 provider 数据源或 native ABI
32. `WorkbenchCommandRouter` 是当前 command-id execution route，返回 `WorkbenchCommandExecutionResult` typed result；`WorkbenchActionExecutor` 仍是 descriptor-level dispatcher，`OpenPanel` action 通过 `PanelCommandService.OpenOrFocusPanel` 执行
33. Command Palette 可通过 catalog-backed `Tools > Command Palette` 或 `Ctrl+Shift+P` 打开；内建快捷键已由 `WorkbenchShortcutRouter` 解析 `WorkbenchActionDescriptor.DefaultShortcut` 并路由到 `WorkbenchCommandRouter`
34. Selection Contract v0从未获得Shell/Feature/Document/Scene producer或reader；public snapshot/event与Application内存service、self-tests现已整体删除，distribution fixture的synthetic type marker也已移除且未替换
35. Inspector Data Model v0 将 selection snapshot 派生成只读 `InspectorDocumentModel`，支持无选中、单选基础属性和多选摘要；Inspector View 只渲染只读摘要，不做编辑器和真实引擎数据查询
36. Historical Hierarchy demo/shared snapshot path is retired；legacy Workbench/Feature consumer已删除，当前最小Shell没有Hierarchy/Inspector/Scene View数据流
37. Historical `ISceneSnapshotProvider`与scene/object/property string DTO只由self-tests维持，没有Document/World owner或authoritative revision，现已整体删除
38. Historical `InMemorySceneSnapshotProvider.ReplaceSnapshot`/`SnapshotChanged` fixture seam也已删除；未来R1 read Slice必须从真实Project/Document open与EditWorld owner重新定义projection
39. Main menu command projection v0 从 `WorkbenchActionDescriptor.MenuPath` 生成 `Tools/*`、`Help/*` 和 `Window/Panels/*` 入口；Tools/Help 使用 `WorkbenchMenuItemViewModel`，Window/Panels 保留 `PanelMenuItemViewModel` 的 open-state indicator
40. Dialog host v0与`Help > About` route已随无consumer presentation删除；随后仅由self-tests/架构库存维持的public request/result/action types也整体删除。R0没有modal能力，未来必须从真实producer、owner Window与typed completion重新进入I0/I1。
41. Background activity v0从未拥有真实work/CTS/cancel/join或App producer；public task DTO/service、Application无界状态字典与self-tests现已整体删除。未来首个真实operation必须由owner持有actual task、bounded terminal evidence与shutdown join后重新进入I0/I1。
42. Transaction service v0没有Document/native mutation producer，string descriptor与closure Apply/Revert也无法表达revision、atomic commit或uncertain outcome；public Editing/Transactions、Application service与self-tests现已整体删除。未来只从typed intent、authoritative receipt/inverse、journal cursor与savepoint重新进入I0/I1。
43. Lifecycle events v0从未获得App/MainWindow/Dock producer或reader；public kind/snapshot/service、Application 100项recent-event实现及self-tests现已整体删除。当前唯一process lifecycle owner是`StudioProcessSession`，未来事件面必须随真实owner transition与对称subscription重新进入I0/I1。
44. Historical status/debug message v0 consumed `WorkbenchCommandExecutionResult`; it did not add shell command input, toast history, native logs, plugin APIs or modal failure dialogs. #381 later chose one current Diagnostics panel with internal Console/Problems tabs, not the historical two-panel Feature model.
45. Current-facts: the test-owned Editor extension host/registry/activation graph was deleted in the R0 hard cut; the current `StudioCompositionSession` owns only `StudioShellViewModel` and declares no extension capability.
46. The orphan root-App `EditorExtensionId` was subsequently deleted because no production registry, contribution, lease or host consumed it. Future extension identity must be introduced by a real registered module owner together with activation and symmetric teardown; this historical guide is not a compatibility source.
47. Panel instance manager v0及built-in panel content/Dock workspace已随legacy UI删除；剩余public panel declaration SCC随后也已删除，当前没有KeepAlive/RecreateOnOpen产品合同。
48. Panel lifecycle callbacks v0没有真实PanelInstanceManager、Dock window/workspace或content consumer；public sink/context与self-tests现已整体删除。未来callbacks必须与实际instance owner和对称detach/dispose一起重立。
49. Panel frame update scheduler v0没有Presentation timer、Window/Dock producer或render owner；public frame contracts、Application scheduler与self-tests现已整体删除，不保留manual/visible/active FPS planner。
50. The provider contribution v0 claim is retired: App/composition never registered or queried an active-scene provider, the documented compatibility adapter did not exist, and the Core declarations plus tests-only Application/public/in-memory provider SCC are now deleted. Provider reload, native bridge connection, script VM, external plugins and writable scene editing remain deferred.
51. Process diagnostics/log ingress v1 uses the one App-owned `IStudioDiagnosticHub`: fixed-capacity diagnostic/log rings, bounded subscribers, cursor/drop/truncation evidence and explicit managed/native/framework/subprocess mapping. #381 opens/focuses one `Diagnostics` panel through `Window/Panels/Diagnostics`; its Console and Problems tabs share one subscription and only own bounded, rebuildable view projections. The panel is `KeepAlive`: close/float/reopen reuses the same content and subscription, while terminal workspace/Shell dispose unsubscribes. The subprocess contract is not production-wired because Studio owns no subprocess; shell command input/CVar, external plugins, persistence, report/crash, arbitrary RPC and remote control remain deferred.
```

当前未实现：

```text
1. tab strip advanced strategy：多行 tab strip、隐藏 tab 菜单、pin/preview tab 和更完整的 overflow 操作
2. n-ary split group 组件和更完整的比例编辑体验
3. floating window 窗口层级、跨屏手工验证和高级生命周期策略
4. 用户可编辑快捷键策略、快捷键冲突 UI、更多 action kind 和命令结果弹出/日志反馈
5. Hierarchy / Inspector真实数据provider、Project asset数据源，以及Diagnostics后续typed target导航
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
Search   匹配 Title、MenuPath、action Id、Category、DefaultShortcut 或 SearchText
Group    按 WorkbenchActionDescriptor.Category 生成 header row；空查询时成功执行过的命令提升到 in-memory Recent group
Execute  通过 WorkbenchCommandRouter 执行 command id；失败、禁用或缺失结果只写入 palette-local message
Shortcut Ctrl+Shift+P 打开 Command Palette；不支持用户自定义或冲突检测
State    打开/关闭、查询文本、选中项、recent commands 和 local result message 只属于 MainWindow UI 状态，不写入 Dock layout snapshot
```

Dock.Avalonia 类型只允许出现在 Shell/Docking 或后续 Infrastructure 持久化适配中，不能进入 Core。

## 后续切片

1. Tab strip advanced strategy：在单行 overflow v0 之后补充多行策略、隐藏 tab 菜单、pin/preview tab 和更明确的拒绝状态。
2. Layout operations：n-ary split group、更完整的比例编辑体验和 reset layout 细节。
3. Floating window operations：补充窗口层级策略、跨屏手工验证和高级生命周期行为。
4. Command palette follow-up：更多 action kind、命令结果弹出/日志反馈、用户可编辑快捷键策略和快捷键冲突 UI；暂不做插件命令 API。
5. Hierarchy read follow-up：从真实Project/Document open与EditWorld authoritative revision建立第一条只读scene projection；不得恢复fixture-backed provider。
6. Inspector read follow-up：复用同一Document/World revision的真实scene object / asset只读来源；selection边界和写回分别重新过I0/I1。
7. Project数据接入：接入真实asset index；Diagnostics已读取真实hub，后续只在出现typed source/target与Action route后增加导航。

## 性能约束

默认布局面板数量保持小集合。不要把资源树、日志、问题列表或场景对象直接展开成大规模 Avalonia 控件；真实面板必须在各自 Feature 内做虚拟化、分页、批处理和资源释放。
## 2026-07-04 Frame Debugger snapshot v0（已退役）

历史上的 `frame-debugger` Code-first tool panel只消费fixture snapshot，没有真实Studio viewport、native session或render-lane owner，已随R0 hard-cut连同public/managed合同删除。当前不得注册Frame Debugger panel或以独立C++ smoke冒充Studio capture能力；未来只有真实viewport与同一render lane先通过I3，才可重新按独立Slice接入。
