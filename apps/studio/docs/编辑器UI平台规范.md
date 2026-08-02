# Studio 编辑器 UI 平台规范

状态：Superseded（历史 UI 实现记录）

更新日期：2026-07-28

> 本文描述的 Dialog、Command、Shortcut、Dock、Background Task与旧Shell surface不再是当前production事实；R0已删除无真实consumer的实现。仅将本文作为历史研究记录，正式合同以[Studio前端硬切架构](architecture/studio-frontend-hard-cut.md)和[architecture/README.md](architecture/README.md)为准。

本文定义 `apps/studio` 当前阶段的编辑器 UI 平台边界。目标是先把弹窗、后台加载反馈、快捷键、命令菜单、状态反馈和设计时预览做成稳定基础，再继续 Scene、Play Session、native bridge 或插件热更新。

本文不替代：

- [architecture/studio-overview.md](architecture/studio-overview.md)：正式目标分层、所有权和依赖方向。
- [architecture/studio-lifecycle.md](architecture/studio-lifecycle.md)：异步启动、关闭、任务和资源排空。
- [architecture/editor-worlds-and-play-mode.md](architecture/editor-worlds-and-play-mode.md)：Edit/Play/Preview World 和 Game View。
- [architecture/viewport-rendering.md](architecture/viewport-rendering.md)：跨平台 production viewport。

- [项目规范.md](项目规范.md)：目录、命名空间、MVVM、性能和合入规则。
- [控件开发指南.md](控件开发指南.md)：Avalonia 控件、样式、`Design.PreviewWith` 和主题覆盖规则。
- [Dock系统指南.md](Dock系统指南.md)：Dock、panel、workbench action 和当前实现事实。
- [architecture/studio-extension-model.md](architecture/studio-extension-model.md)：统一 managed EditorModule、contribution 和生命周期目标。
- [architecture/editor-extension-authoring.md](architecture/editor-extension-authoring.md)：项目 `Editor/`、`.asmdef`、Avalonia/XAML、Package 和 build/reload 目标。

状态词：

| 状态 | 含义 |
| --- | --- |
| Current | 已在源码中存在，可由当前测试或文档事实验证。 |
| Partial | 已有最小路径，但能力、UI、诊断或扩展面仍不完整。 |
| Planned | 允许写设计或下一个 PR-sized Slice，但不能描述成已实现。 |
| Deferred | 当前阶段不实现，必须等入口条件、ADR 或 smoke plan 成熟。 |

## 1. 结论

当前不建议急着做完整 Scene authoring，也不建议现在开始 C++ ABI 或插件热更新。下一阶段应继续推进 UI 平台层：

```text
Status debug message surface -> Background Tasks panel -> Diagnostics/Problems route -> Shortcut/Command settings
```

理由：

1. Scene 底层、schema、Edit World / Play World 和 native bridge 还没有足够稳定的写入合同。
2. 本文历史列举的Panel、Workbench、Dialog、Task、Transaction与Lifecycle类型不自动构成当前可扩展面；其中Dialog Host和public DTO已确认零consumer并删除，R0没有modal能力。
3. 弹窗、后台任务、命令反馈、事务诊断、生命周期事件、快捷键和 design preview 是后续 asset import、scene snapshot load、validation、play session 和 plugin diagnostics 都会复用的基础设施。

## 2. 当前事实

当前已经存在的 UI 平台合同：

| 能力 | 当前入口 | 状态 |
| --- | --- | --- |
| Panel 注册 | `PanelDescriptor`, `IPanelRegistry`, `PanelRegistry` | Current |
| Workbench action | `WorkbenchActionDescriptor`, `IWorkbenchActionRegistry` | Current |
| 命令执行 | `WorkbenchCommandRouter`, `WorkbenchActionExecutor` | Current |
| 菜单投影 | `WorkbenchMenuItemViewModel`, `MainWindow.axaml.cs` | Current |
| 快捷键路由 | `WorkbenchShortcutGesture`, `WorkbenchShortcutRouter` | Current |
| 命令面板 | `CommandPaletteViewModel`, `CommandPaletteView` | Current |
| Modal dialog | public DTO、Host ViewModel/View均不存在 | Deleted / no request producer or owner Window |
| 后台任务状态 | public/Application task状态面均不存在 | Deleted / no real work、CTS、cancel or join |
| 事务服务 v0 | public Editing/Transactions与Application实现均不存在 | Deleted / no Document or native mutation owner |
| 生命周期事件 v0 | public/Application event stream均不存在 | Deleted / no Window、Dock or App producer |
| 内置扩展组合 v0 | host、module SDK与composition source均不存在 | Deleted / no loader、registry or activation owner |
| Contribution ownership v0 | public declaration与legacy registries均不存在 | Deleted / no contribution consumer or lease owner |
| Panel instance lifetime v0 | `PanelInstanceManager`/Dock workspace source均不存在 | Deleted / no panel content owner |
| Panel lifecycle callbacks v0 | public callback/context与legacy PanelInstanceManager均不存在 | Deleted / no Window、Dock、Control or panel instance owner |
| Panel frame update scheduler v0 | public frame contract与Application scheduler均不存在 | Deleted / no producer, timer or render owner |
| Dock tab overflow v0 | `EditorDockTabStripScrollController`, `EditorDockTabStripView` | Current / view-only scroll state |
| 状态栏反馈 | `ActivityIndicator`, `EditorStatusMessageSnapshot`, `MainWindowViewModel` summary/status properties | Current / status-debug message v0 |
| UI 线程切回 | `IEditorUiDispatcher`, `AvaloniaEditorUiDispatcher` | Current |
| 只读 Scene snapshot | public/Application/Core provider与snapshot source均不存在 | Deleted / no Document、EditWorld or read consumer |
| Legacy provider declaration/host | `SceneProviderDescriptor`、`EditorProviderRoles`、`EditorProviderHost` | Deleted / zero production consumer and no compatibility adapter |
| Process diagnostics/log ingress v1 | Application `IStudioDiagnosticHub` / `StudioDiagnosticHub`, `StudioAvaloniaLogSink`, `ConsolePanelViewModel`, `ProblemsPanelViewModel` | Current / App-owned bounded truth with read-only projections |

当前仍不稳定或未实现：

| 能力 | 当前策略 |
| --- | --- |
| 完整快捷键管理窗口 | Planned；先保留 action descriptor 和 shortcut router，不做用户配置 UI |
| Toast / non-modal notification | Planned；先使用 command result 和 background task diagnostics 设计，不急着实现 |
| Background Tasks 窗口 | Planned；下一阶段推荐切片 |
| Advanced tab strategy | Planned；多行 tab、隐藏 tab 菜单、pin/preview tab 另起切片 |
| Writable Inspector | Deferred；等 schema metadata、真实 provider、dirty-state UI 和写回 gate |
| Scene authoring / hierarchy mutation | Deferred；等 native/scene bridge 和 edit/apply contract |
| Feature/provider/plugin lifecycle | Deferred；当前无production extension/provider host，外部 plugin 与 hot reload 也未接入 |
| Play Session | Target / Not implemented；正式语义见 `architecture/editor-worlds-and-play-mode.md` |
| Managed plugin hot reload | Deferred；等 contribution registry、diagnostics、ALC unload negative smoke |
| Native C ABI | Experimental/Partial；已有 Windows viewport bridge，生产 ABI ownership 尚未成立 |
| Avalonia native Vulkan viewport | Windows Experimental/Partial；跨平台 production 目标见 `architecture/viewport-rendering.md` |

原`EditorExtensionHost v0`及其panel/action/provider contribution声明、注册和removal lease已随R0 hard cut删除；当前`StudioCompositionSession`只拥有最小Shell，不声明runtime enable/disable、provider reload、外部plugin lifecycle、hot reload或native bridge能力。

原`PanelInstanceManager v0`、Dock workspace与public panel declarations均无当前source或owner；它们已在R0 hard cut中删除。未来真实panel必须由Window/Dock owner持有content、visibility、dispatcher与exactly-once detach/dispose，不恢复旧KeepAlive/Recreate DTO作兼容。

原`EditorPanelFrameScheduler v0`与public lifecycle/frame callbacks只由self-tests维持；文档声称的Presentation timer、Window/Dock owner与panel content均已随legacy UI删除。该runtime岛现已整体删除，Application也不再依赖public Editor。未来真实panel的tick/invalidation必须与实际content、dispatcher、visibility及detach/dispose同寿命。

Application `EditorProviderHost`、public scene snapshot/provider contracts、Core `InMemorySceneSnapshotProvider`及各自self-tests已整体删除；先前声称的compatibility adapter也不存在。当前App/composition没有registration/query入口、Document/World owner、snapshot subscription或写回语义。未来只读Scene必须由真实Project/Document open与EditWorld authoritative revision重新通过I0/I1，不能恢复这些DTO作为兼容层。
`StudioDiagnosticHub v1` is created once by `App` and owns fixed-capacity diagnostic/log rings plus bounded subscriber slots. Records carry stable code, timestamp, origin/package, process scope/generation, operation/correlation and cursor/drop/truncation evidence. Managed status, mapped native failures and Avalonia warnings/errors enter this one truth; Console reads logs and Problems reads problem diagnostics. It does not add shell command input, persistence, arbitrary RPC, Capture/Mutate, remote control, profiler/crash infrastructure or a second UI-owned store.

## 3. 分层规则

Studio 是 Avalonia presentation host，不拥有 engine truth。

```text
Core
  UI-neutral descriptors, snapshots, command result models, service abstractions.

Shell
  Main window, menu/status/dialog/shortcut routing, Dock orchestration.

UI
  Reusable controls, themes, tokens, small visual primitives.

Features
  Vertical panels and feature-specific view models.

Infrastructure
  Future persistence, filesystem, settings and platform adapters.
```

规则：

1. `Core` 不引用 Avalonia controls、Shell、Feature 或 renderer/native 类型。
2. `Shell` 编排命令、菜单、弹窗、状态栏和 Dock，不承载 Hierarchy、Inspector、Asset Browser 的业务逻辑。
3. `UI` 只放通用视觉控件，不依赖具体 Feature。
4. `Features` 通过 `IEditorFeatureModule` 注册 panel/action，不直接创建 Dock 控件。
5. 后台线程不能直接访问 Avalonia UI object。后台任务完成后只通过 `IEditorUiDispatcher` 或 UI dispatcher 切回 UI 线程。

Avalonia 层边界：

| 层 | 允许 | 禁止 |
| --- | --- | --- |
| Views | XAML 布局、visual state、focus/key bridge、view-only code-behind | 业务服务定位、engine/native 调用、持久化写入 |
| ViewModels | 绑定状态、commands、轻量 UI model、snapshot projection | 创建 View、持有 Avalonia `Control`、直接访问 renderer/native handle |
| Services | UI-neutral task/dialog/command/selection/scene snapshot services | 持有控件树、跨线程直接改绑定集合 |
| Controls | 可复用视觉和交互 primitive | Feature 业务逻辑、engine state |
| Styles | tokens、Fluent 覆盖、`ControlTheme`、`Design.PreviewWith` | 藏业务 class 或依赖 Feature model |
| Design-time preview | mock data、典型状态、布局验证 | runtime service 注册、命令执行、文件/native 访问 |

### 3.1 Avalonia 工程规则

下一阶段新增 Studio UI 基础设施时，必须遵守这些 Avalonia-specific 规则：

1. Composition root 必须集中在启动路径。Shell services、feature modules、panel/action registry、root view model 和 dispatcher 由 composition root 组装；不要在 View 或 Feature 内部临时 service locate。
2. ViewModel 不知道 View。ViewModel 只暴露状态、命令和轻量 UI model；View/code-behind 只负责 view-only 行为、焦点、键盘事件桥接和控件内部逻辑。
3. 新增 View 和 DataTemplate 必须写 `x:DataType`。当前 Avalonia 版本下 compiled binding 需要稳定类型信息；反射绑定只能作为明确例外。
4. 约定式 `ViewLocator` 只适合启动阶段。新增复杂 Feature 优先用显式 DataTemplate / mapping，避免 Native AOT、构造注入和重构安全问题继续扩大。
5. 应用组合视图优先用 `UserControl`；可复用、可换模板控件才使用 `TemplatedControl`；自绘曲线、图标、overlay preview 等才考虑 `Control + Render`。
6. UI 线程是唯一 Avalonia control owner。后台任务、桥接回调、import/load/validation 结果只能发布 snapshot，再由 Shell 通过 `IEditorUiDispatcher` 刷新绑定。

### 3.2 Views

Views 是 Avalonia 视觉和输入桥，不是业务 owner。

规则：

1. View 只负责布局、绑定、焦点、键盘桥接和必要的 view-only 行为。
2. View 不创建 ViewModel，不读取 Feature service，不访问文件系统，不提交 engine/native mutation。
3. 顶层 Avalonia `Window` 只由 Shell 创建和管理。Feature 贡献 panel content，不直接创建窗口。
4. 键盘输入先进入 Shell/input boundary，再路由到 command id 或 ViewModel command。

当前例子：

- `MainWindow.axaml.cs` 负责菜单投影、快捷键桥接和 floating window host。
- `CommandPaletteView.axaml.cs` 负责搜索框 focus、`Escape` / `Enter` 和双击执行桥接。
- 历史`EditorDialogHostView.axaml.cs`曾负责focus与`Escape`桥接；该View/Host现已删除。

### 3.3 ViewModels

ViewModels 表达 UI 状态、轻量投影和命令，不持有 Avalonia 控件。

规则：

1. ViewModel 继承 `ViewModelBase`，通过 CommunityToolkit.Mvvm 暴露属性和 `IRelayCommand`。
2. ViewModel 可以依赖 `Core` 抽象或 Shell 注入的 service interface。
3. ViewModel 不直接 new View、不引用 `Control`、不读取 `Window`、不持有 native handle。
4. 大集合、engine object、asset cache 或 runtime state 留在服务层，ViewModel 只保留可见状态。

当前例子：

- `MainWindowViewModel` 编排 dock、菜单、命令面板、弹窗 host 和后台任务摘要。
- `CommandPaletteViewModel` 只保存 query、filtered items、selected item 和执行命令。
- 历史`EditorDialogHostViewModel`曾保存active request、按钮投影和completion；它不再是current production type。

### 3.4 Services

Services 必须先说明 owner、生命周期、线程边界和错误路径。

| 类型 | 当前或未来落点 | 规则 |
| --- | --- | --- |
| Core abstraction | `IPanelRegistry` | 历史合同清单；Selection/Lifecycle/Task/Transaction合同已删除。 |
| Shell service | `PanelCommandService`、`AvaloniaEditorUiDispatcher` | 历史实现清单；Lifecycle/Task/Transaction实现已删除。 |
| Future Infrastructure service | project settings、filesystem、layout persistence、native bridge adapter | 实现 Core 合同，不承载 Feature View。 |

不要把宽泛 `EditorContext` 或 service locator 作为平台合同。需要新 service 时先写出真实 consumer，不为未来 plugin 预留空壳。

### 3.5 Controls

通用控件放 `UI/Controls`，Feature 专属控件放对应 Feature。

规则：

1. `UserControl` 用于组合型应用控件，例如 `SearchBox`、`ActivityIndicator`。
2. 长期复用、需要模板替换或主题覆盖的控件再升级为 `TemplatedControl`。
3. 自绘、网格、曲线、gizmo preview 等可使用 `Control + Render`，但不能拥有 renderer/RHI。
4. 通用控件只暴露 Avalonia property 和视觉状态，不包含搜索算法、导入逻辑、Scene 查询或命令执行策略。

### 3.6 Styles

样式和 token 位于 `UI/Styles`。

规则：

1. 颜色、尺寸和字体通过 `DynamicResource` 或 token 读取，不在 Feature View 中散落 hard-coded 颜色。
2. Feature 可以有局部样式，但不能定义新的全局色系。
3. Dialog、Command Palette、Dock、Status Bar、Main Menu 应复用同一 text、surface、border、accent、warning/error token。
4. 样式不访问服务，不触发命令，不根据运行时 engine state 改写资源字典。

## 4. UI 平台优先级

### 4.1 P0：继续稳定的基础能力

这些可以继续做，并且应该按小切片推进：

```text
Dialog result presentation
Status debug message surface
Background Tasks panel
Problems/Console diagnostic ingestion
DI composition root
Explicit view resolution / compiled binding audit
Shortcut conflict model
Command catalog grouping
Lifecycle event diagnostics projection
Design preview coverage
```

验收重点：

- 所有入口都走 stable command id。
- 所有失败都返回 typed result 或 structured diagnostic。
- 所有长任务都能进入 background activity service。
- 所有 UI 状态只在 ViewModel / Shell service 中表达，不泄漏 engine object。
- 可视 UI 改动有 design-time data 或 `Design.PreviewWith`。

### 4.2 P1：只做接口占位

这些可以定义最小 contract，但不要实现完整生态：

```text
Editor contribution descriptor
Panel/window descriptor
Status bar item descriptor
Command palette provider descriptor
Shortcut profile descriptor
Diagnostic source descriptor
Read-only scene bridge adapter
```

接口占位必须满足：

1. 有真实当前 consumer 或明确下一切片 consumer。
2. 不引入 plugin loader、AssemblyLoadContext、native bridge 或 script VM。
3. 不允许 raw Avalonia `Control` 从插件或脚本返回到 Shell。
4. 不承诺 unload、hot reload、sandbox 或 ABI compatibility。

### 4.3 推迟项

以下能力现在不做：

```text
Full Scene authoring
Writable Transform / Component Inspector
Runtime gameplay ScriptHost
Managed plugin reload
User/plugin-created raw Avalonia windows
Native C ABI
Native Vulkan viewport
Hot reload of engine/editor scripts
```

这些能力进入前必须先有单独 ADR、smoke plan 和 Issue slice。

## 5. Command / Menu / Shortcut 合同

命令是菜单、快捷键、命令面板和未来 context menu 的共同入口。

当前执行路径：

```text
WorkbenchActionDescriptor
  -> WorkbenchActionRegistry
  -> WorkbenchCommandRouter.Execute(commandId)
  -> WorkbenchActionExecutor
  -> WorkbenchCommandExecutionResult
```

规则：

1. 新动作先注册 `WorkbenchActionDescriptor`，不要直接在 XAML 或 code-behind 写业务执行逻辑。
2. 菜单和快捷键必须使用同一个 command id。
3. `DefaultShortcut` 是默认绑定和显示文本，不是用户自定义快捷键系统。
4. `WorkbenchCommandExecutionResult` 是命令反馈的基础事实来源之一，但状态栏暴露的是 UI-level status/debug message。
5. disabled、not found、failed 不应被 UI surface 吞掉。下一阶段应进入 status/debug message 与 diagnostics 的统一反馈路径，不急着做 toast 或 shell command line。
6. 后续 Undo / Redo 必须接同一命令体系，不能另开一套隐藏入口。

推荐下一切片：

```text
[Slice] Studio: status debug message surface
```

范围：

- 在状态栏显示最新 UI-level status/debug message。
- 将 `WorkbenchCommandExecutionResult` 作为第一类 producer 转为 `EditorStatusMessageSnapshot`。
- Console/debug producer 后续通过 `TargetPanelId = "console"` 复用同一路由；当前不做 shell command line、toast 动画系统或完整 notification center。

## 6. Dialog / Popup 合同

本节只保存历史研究。R0已删除无request producer与owner Window的Dialog presentation，随后删除只由self-tests和架构库存维持的public request/result/action types。当前没有single-active host、overlay、completion task、About route或modal能力。

未来真实Dialog必须从destructive Document/Project decision重新通过I0/I1，并同时定义owner Window、action/system-dismiss/owner-close completion、重复request policy与headless default；普通失败仍进入唯一bounded diagnostics truth，不得从本文恢复旧DTO或fixture host。

## 7. Background Activity 合同

本节只保存历史研究。R0已删除无App producer的public/Application task状态岛：旧实现不持有真实work、CTS、cancel signal或shutdown join，并无界保留terminal snapshots，因此不能作为后台任务能力。

未来首个真实background operation必须同时定义owner、稳定operation ID、actual task、progress、cancellation source、terminal result、bounded retention与shutdown join，并从I0/I1形成纵向闭环；在此之前不得恢复Background Tasks面板或DTO-only service。

## 8. 状态反馈合同

状态反馈分成四条路径，按优先级选择，不要把所有反馈都做成弹窗。

| 来源 | 首选显示路径 | 适合内容 | 不适合内容 |
| --- | --- | --- | --- |
| Command result | status feedback / diagnostics record | success、disabled、not found、recoverable failure | 长任务进度、必须用户决策 |
| Background task | status bar summary + Background Tasks panel | running、progress、failed、canceled、retry candidate | 阻塞用户的确认问题 |
| Diagnostics | Problems / Console | structured warning/error、source、category、recovery hint | 临时 hover/focus 状态 |
| Dialog | modal host | destructive confirmation、blocking decision、不可自动恢复错误 | 普通命令失败、长任务进度 |

规则：

1. 一个操作可以同时产生命令结果和 diagnostics，但 UI 只应有一个 primary feedback surface。
2. command failure 默认不弹 modal；只有用户必须决策时才转成 dialog。
3. long-running command 必须创建 background task，再由 task 状态驱动 status bar 和任务面板。
4. diagnostics 记录应可被 Console / Problems 复用，不绑定某个临时 overlay。
5. Current diagnostics/log ingress uses the single App-owned `IStudioDiagnosticHub`. Status is a log projection, Console reads recent logs, and Problems filters the diagnostic ring by problem channel; native failures and Avalonia records use explicit typed adapters. Subprocess output has a typed mapping contract but no production launch wiring because Studio currently owns no subprocess. The completed disposable-child gate is external test infrastructure, not a product capability. Shell command execution, plugin/provider reload diagnostics, persistence and remote control remain deferred.

## 9. Design Preview 合同

Avalonia design preview 是当前 UI 开发的强约束，不是额外美化任务。

规则：

1. 新增 `UserControl` 或重要 view 写 `d:DesignWidth` / `d:DesignHeight`。
2. 能提供 mock data 的 view 写 `Design.DataContext` 或 `d:DataContext`。
3. 全局样式或控件族样式尽量提供 `Design.PreviewWith`。
4. design-time data 只服务 preview，不注册 runtime service，不发命令，不读写文件，不触发 engine/native 调用。
5. 使用 compiled bindings 时，`x:DataType` 必须匹配 design view model 或 runtime view model。
6. 如果某个 preview 需要带参数构造 ViewModel，使用 `Design.IsDesignMode` / `Design.SetDataContext` 的 code-behind 方案时必须保持 view-only，不把服务定位器塞进 View。

推荐下一切片：

```text
[Slice] Studio: design preview coverage for UI platform surfaces
```

范围：

- 历史主题范围曾覆盖`ActivityIndicator`、`EditorDialogHostView`、`CommandPaletteView`与原生控件；已删除的Dialog Host不再是current target。
- 不做截图测试系统。
- 不引入运行时 fake service registry。

## 10. 通用 UI 抽象边界

现在可以做有限的通用 UI 平台，不要做“完整 Unity UI 框架克隆”。

可以现在稳定：

```text
PanelDescriptor
WorkbenchActionDescriptor
WorkbenchCommandExecutionResult
Status/debug message record
Diagnostic record
Design preview convention
```

暂不稳定：

```text
Plugin-created Avalonia Control
Generic property grid ABI
Script-authored window
Runtime-loaded XAML as extension ABI
Native viewport host API
Engine-backed command/undo transaction bridge
Feature/provider/plugin lifecycle bus
```

类似 Unity 的 panel / window / menu / command / dialog / status overlay 可以按下表收敛：

| UI 概念 | 当前可稳定合同 | 当前不要做 |
| --- | --- | --- |
| Panel | `PanelDescriptor`、stable id、kind、default area、content factory | 插件返回 raw Avalonia `Control`，或 Feature 直接控制 Dock。 |
| Window | Shell-owned `MainWindow` / floating dock window host | 通用 WindowManager、脚本创建顶层窗口。 |
| Menu | `WorkbenchActionDescriptor.MenuPath` 投影 | 每个 View 自己创建业务菜单入口。 |
| Command | command id -> router -> executor -> result | 绕过 command result 的直接方法调用，或隐藏 transaction 入口。 |
| Dialog | R0无稳定合同；真实producer/owner成立后重新设计 | 恢复旧DTO，或Feature直接创建modal Window。 |
| Lifecycle | Shell window lifecycle snapshot | 把 feature unload、provider reload、Play Session 或 native runtime lifecycle 混入 v0。 |
| Status overlay | task snapshot / command result / diagnostic record | 一次性做完整 notification center 或 project dashboard。 |

状态反馈的当前边界：

1. 全局状态栏只显示跨 Feature 的持续状态，例如后台任务、最近命令失败、诊断计数、dirty/play 状态。
2. Feature 内部状态留在 Feature 面板内，除非它影响全局健康状态。
3. 状态反馈必须来自 data-only status/debug snapshot 或 command result，不从 View 反查控件状态。
4. Dialog 只用于必须阻塞用户决策的情况。普通失败、禁用和后台进度优先进入 status/diagnostics。
5. Status/debug message 是 UI-level record：命令结果是第一类 producer；未来 Console/debug producer 应设置 `TargetPanelId = "console"`，让状态栏可以打开或聚焦 Console。该层不直接读取 native engine log，也不代表 shell command line。
6. Problems / Console 接入前，先定义 UI-level diagnostic record，不直接读取 native engine log。

判断标准：

1. 如果抽象已经有两个以上真实 consumer，可以上移到 `Core` 或 `UI`。
2. 如果抽象只是为了未来 plugin，先写 ADR 或 design note，不写空接口。
3. 如果抽象会决定 engine truth、scene mutation、asset write 或 renderer lifetime，必须等对应系统合同稳定。

## 11. Runtime Editor / 扩展窗口 / 热更新边界

Avalonia 支持 design-time preview、XAML 编译、runtime XAML loader 和 native interop，但这些不等于 Asharia 可以现在做可信插件热更新。

当前允许：

```text
Built-in feature modules register panels/actions.
Trusted Shell services orchestrate dialogs, commands, shortcuts and background feedback.
Design-time mock data improves XAML preview.
Bounded diagnostics/log projections read the one App-owned truth.
Shell-owned lifecycle events record main/floating window activity.
```

当前只做占位：

```text
Editor contribution descriptor
Command contribution descriptor
Panel model descriptor
Diagnostic source descriptor
Native bridge checklist
ALC unload smoke design
```

当前禁止：

```text
Runtime plugin returns Avalonia Control
Runtime plugin loads arbitrary XAML into Shell
Plugin directly mutates Scene/Asset/native state
Plugin owns C++ pointer or Vulkan handle
Hot reload recreates Avalonia app, Shell, Dock, native viewport or renderer resources
```

扩展窗口边界：

1. Current：无内置Feature registry、PanelDescriptor或WorkbenchActionDescriptor；最小Shell不注册panel/menu/command extension。
2. Planned：`EditorContributionDescriptor` 或 manifest 只能声明 panel/action/status/diagnostic 贡献。
3. Planned：扩展 panel 的第一版返回 ViewModel 或 declarative panel model，由 host 选择 Avalonia View / DataTemplate。
4. Deferred：外部 DLL 或脚本直接 new Avalonia `Window`、`Control`、`UserControl` 或加载任意 XAML。
5. Deferred：插件窗口热重载、ALC 卸载、native viewport host 和 C++ ABI。

未来热更新必须是 contribution registry reload，而不是 UI tree restart：

```text
freeze new extension actions
load candidate manifest/model
validate ids, capabilities and diagnostics
diff contributions
swap descriptors at a safe point
preserve previous valid contribution on failure
publish diagnostics
attempt old ALC unload when managed runtime exists
```

## 12. 验证要求

文档-only 改动：

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

证明内容：

- 编码和 whitespace 门禁证明文档可提交，不证明 UI 行为。
- 文档互链证明路线可追踪，不证明实现存在。

Studio C# / XAML 改动：

```powershell
dotnet build apps\studio\Asharia.Studio.sln -c Release
dotnet test apps\studio\Asharia.Studio.sln -c Release --no-build --blame-hang --blame-hang-timeout 10m
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

证明内容：

- ViewModel/service 单元测试证明 UI-neutral contract。
- XAML build 证明类型、资源和 compiled binding 基本可解析。
- design preview 证明典型视觉状态可被设计器加载。
- 手工 smoke 证明窗口、焦点、快捷键、overlay、窄宽度和 binding output 没有明显回归。

UI-sensitive 改动还需要手工或截图确认：

```text
1. 默认窗口能打开。
2. Debug 输出无新增 binding error。
3. Design preview 能显示典型状态。
4. 小宽度下文本不溢出、不遮挡。
5. 后台任务或命令失败不会阻塞 UI thread。
```

涉及 root architecture docs、GitHub Issue、Project 或 PR 元数据时，还必须按 `docs/planning/project-management.md` 做查重、Project 字段和 #20 同步。

## 13. 工作跟踪

本文不维护“下一阶段切片”。Dialog、Command、Shortcut、Dock、extension、Viewport 与 Play Mode 的
实施顺序、状态和 Done evidence 以 GitHub Issues / Project 为准；边界变化回写本目录 Architecture/ADR，
当前行为变化回写对应 Current guide。

## 14. 参考资料

仓库资料：

- [项目规范.md](项目规范.md)
- [控件开发指南.md](控件开发指南.md)
- [Dock系统指南.md](Dock系统指南.md)
- [architecture/studio-extension-model.md](architecture/studio-extension-model.md)
- [architecture/editor-extension-authoring.md](architecture/editor-extension-authoring.md)

官方资料与优秀案例：

- [Avalonia compiled bindings](https://docs.avaloniaui.net/docs/data-binding/compiled-bindings)：`x:DataType` / compiled binding 作为长期可维护性和性能边界。
- [Avalonia XAML previewer and design-time settings](https://docs.avaloniaui.net/docs/app-development/xaml-preview-and-design-settings)：design-time data 只服务预览，不进入 runtime service。
- [Unity EditorWindow](https://docs.unity3d.com/ScriptReference/EditorWindow.html)：借鉴 `Update` / `Repaint` 的 editor-window 调度语义；Asharia 当前只做 panel frame request，不复制 Unity 扩展 ABI。
- [Avalonia threading model](https://docs.avaloniaui.net/docs/app-development/threading) / [DispatcherTimer](https://api-docs.avaloniaui.net/docs/T_Avalonia_Threading_DispatcherTimer)：未来可用 UI-thread timer 驱动 scheduler，但 v0 contract 不直接创建 timer 或访问 controls。
- [Godot EditorPlugin custom dock](https://docs.godotengine.org/en/stable/tutorials/plugins/editor/making_plugins.html#a-custom-dock)：dock/plugin 生命周期需要明确初始化与清理；Asharia 当前只做内置 feature 注册和 Shell-owned dock。
- [Unreal Editor Utility Widgets](https://dev.epicgames.com/documentation/en-us/unreal-engine/editor-utility-widgets-in-unreal-engine)：工具 UI 可以作为编辑器内 surface 暴露；Asharia 当前仍禁止外部脚本直接拥有 Avalonia window/control。
