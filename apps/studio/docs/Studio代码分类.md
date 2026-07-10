# Studio 代码分类

状态：Partial（当前单项目迁移期分类规则）

最后更新：2026-07-11

> 本文继续约束当前 `Editor.csproj` 内的目录放置，但不再定义长期模块边界。目标六项目结构和允许依赖以 [architecture/studio-overview.md](architecture/studio-overview.md) 与 [adr/0003-studio-project-boundaries.md](adr/0003-studio-project-boundaries.md) 为准。迁移期间不得为了符合旧目录规则，把 Avalonia、P/Invoke 或 platform GPU handle 继续上移到 `Core`。

本文用于回答“迁移完成前，新增或整理一段代码应该放在哪里”。它补充正式架构文档和 `项目规范.md`，不替代具体专题文档。

当前 `apps/studio` 仍是单 `.csproj` Avalonia 应用，分类边界先通过目录、命名空间、测试和文档约束表达。不要为了分类而提前拆项目。

## 1. 分类总览

```text
App Root  应用入口和 Avalonia 启动胶水
Core      UI-neutral 合同、模型、轻量服务和 Code-first 抽象树
Shell     编辑器外壳、组合、Dock、命令、窗口、生命周期和适配器
UI        可复用视觉资产、样式 token、基础控件和树控件基础设施
Features 具体面板、工具切片和内置 feature 聚合
Tests     按被测层镜像组织的单元/集成/XAML 约束测试
docs      架构、规范、专题指南、计划和回归清单
Assets    应用图标、字体、图片等 Avalonia resource
```

分类原则：

1. 先放到最具体的拥有者目录，第二个真实复用场景出现后再上移。
2. 可复用合同进 `Core`，可复用视觉能力进 `UI`，应用编排进 `Shell`，垂直业务进 `Features`。
3. `ViewModel` 不持有 Avalonia `Control`，`View` 不查询 runtime/native/renderer 对象。
4. 底层数据进入 UI 前必须先变成 Core snapshot 或 provider contract。
5. 分类不能制造新依赖方向。移动文件前先确认 namespace、测试和调用方依赖。

## 2. App Root

放置：

- `Program.cs`
- `App.axaml`
- `App.axaml.cs`
- `ViewLocator.cs`
- `Editor.csproj`
- `Editor.sln`
- `Assets/`
- `Properties/`

职责：

- 启动 Avalonia desktop lifetime。
- 装载全局资源字典。
- 建立应用级 ViewLocator。
- 保持项目文件、资源和入口最小化。

禁止：

- 不写 feature 业务逻辑。
- 不直接创建具体面板内容。
- 不放 dock、command、selection、diagnostics 的实现。

## 3. Core

`Core` 是 UI-neutral 合同层。它可以被 `Shell`、`UI`、`Features` 和测试引用，但不能引用这些上层目录。

### 3.1 Core/Abstractions

放接口和跨层扩展点：

- panel、feature、extension contribution 接口
- selection、diagnostics、transaction、background task、lifecycle 服务接口
- scene snapshot provider 接口
- editor command / edit command 接口

放置判断：

- 两个以上上层模块需要同一个合同。
- 调用方只应依赖能力，不应知道实现来自 Shell、Feature 还是后续底层 adapter。

不要放：

- Avalonia 类型。
- 文件系统路径、JSON DTO、平台窗口句柄。
- 某个具体面板的内部接口。

### 3.2 Core/Models

放不可变或轻量状态模型：

- `Panels/PanelDescriptor`
- `Panels/EditorPanelLifecycleContext`
- `Panels/EditorPanelFrameUpdateRequest`
- `Contributions/EditorContributionDescriptorSet`
- `Contributions/EditorContributionValidationContext`
- `Contributions/EditorActionContributionDescriptor`
- `BackgroundTasks/EditorBackgroundTaskId`
- `BackgroundTasks/EditorBackgroundTaskSnapshot`
- `BackgroundTasks/EditorBackgroundTaskState`
- `Lifecycle/EditorLifecycleEventKind`
- `Lifecycle/EditorLifecycleEventSnapshot`
- `Extensions/EditorExtensionId`
- `Editing/EditorEditCommandDescriptor`
- `Editing/EditorEditValidationResult`
- `Transactions/EditorTransactionId`
- `Transactions/EditorTransactionServiceSnapshot`
- `Workbench/WorkbenchActionDescriptor`
- `Workbench/WorkbenchCommandExecutionResult`
- `Workbench/EditorCommandFeedbackSnapshot`
- `Diagnostics/EditorDiagnosticChannel`
- `Diagnostics/EditorDiagnosticRecord`
- `Diagnostics/EditorDiagnosticSeverity`
- `Diagnostics/EditorDiagnosticSourceDescriptor`
- `Scene/SceneSnapshot`
- `Scene/SceneObjectSnapshot`
- `Scene/SceneProviderDescriptor`
- `Dialogs/EditorDialogRequest`
- `Dialogs/EditorDialogResult`
- `Selection/EditorSelectionSnapshot`
- `Selection/EditorSelectionItem`

放置判断：

- 模型能用纯测试构造。
- 模型表达编辑器合同或 snapshot，不表达具体 UI 布局控件。
- 模型可以跨线程传递，但 UI 更新由上层 dispatcher 处理。

不要放：

- Avalonia brush、control、binding、geometry 等视觉对象。
- native pointer、Vulkan handle、renderer backend 类型。
- 持久化文件格式 DTO。后续有文件/JSON DTO 时放 `Infrastructure` 或专题目录。

### 3.2.1 Core/Models/Dialogs

放跨 Shell、ViewModel 和测试共享的 dialog request/result 合同：

- `EditorDialogButtonDescriptor`
- `EditorDialogButtonRole`
- `EditorDialogKind`
- `EditorDialogRequest`
- `EditorDialogResult`
- `EditorDialogResultKind`

规则：

- 只表达“请求弹什么”和“用户如何回应”的纯数据。
- 不引用 Shell dialog host、Avalonia Window、Button、Command 或样式。
- 具体 dialog 呈现状态仍放 `Shell/ViewModels/Dialogs` 和 `Shell/Views/Dialogs`。

### 3.2.2 Core/Models/Selection

放跨 Shell、Feature 和测试共享的 selection 合同：

- `EditorSelectionChangedEventArgs`
- `EditorSelectionItem`
- `EditorSelectionSnapshot`

规则：

- 只表达当前选择上下文、选择项和选择变化事件。
- 不引用 Shell selection service、具体 Feature ViewModel、Avalonia selection control 或 scene runtime 对象。
- selection service 接口在 `Core/Abstractions`，Shell 实现在 `Shell/Selection`。

### 3.2.3 Core/Models/Diagnostics

放跨 Core service、Feature 面板、Shell 状态栏和 XAML DataTemplate 共享的 diagnostics 合同：
- `EditorDiagnosticChannel`
- `EditorDiagnosticRecord`
- `EditorDiagnosticSeverity`
- `EditorDiagnosticSourceDescriptor`

规则：
- 只表达诊断事件、严重度、输出通道和 contribution 声明的诊断来源。
- 不引用 `EditorDiagnosticService` 实现、具体 Console/Problems ViewModel、Avalonia control 或日志持久化格式。
- diagnostics service 接口在 `Core/Abstractions`，默认内存实现仍在 `Core/Services`。

### 3.2.4 Core/Models/Scene

放跨 Core service、Feature 面板、Shell provider host 和测试共享的 scene snapshot/provider 合同：
- `EditorProviderRoles`
- `EditorProviderState`
- `EditorProviderStatusSnapshot`
- `SceneObjectPropertySnapshot`
- `SceneObjectPropertyValueKind`
- `SceneObjectSnapshot`
- `SceneProviderDescriptor`
- `SceneSnapshot`

规则：
- 只表达场景快照、场景对象、属性值类型和 scene provider 注册/状态合同。
- 不引用具体 Hierarchy/Inspector/SceneView ViewModel、Shell provider host 实现或底层 renderer/native 对象。
- scene snapshot provider 接口在 `Core/Abstractions`，默认内存实现仍在 `Core/Services`。

### 3.2.5 Core/Models/Workbench

放跨 Core command API、Shell commands、Shell ViewModel、Feature 声明和测试共享的 workbench command/action 合同：
- `EditorCommandFeedbackSeverity`
- `EditorCommandFeedbackSnapshot`
- `WorkbenchActionDescriptor`
- `WorkbenchActionKind`
- `WorkbenchActionScope`
- `WorkbenchCommandExecutionResult`
- `WorkbenchCommandExecutionStatus`

规则：
- 只表达 workbench action 元数据、命令执行结果和 command feedback snapshot。
- 不引用 Shell command registry/router/executor、具体菜单/Command Palette ViewModel 或 Avalonia input gesture。
- action registry 接口仍在 `Core/Abstractions`；Shell commands 拥有注册、路由和执行实现。

### 3.2.6 Core/Models/Panels

放跨 Core panel API、Shell docking、Shell services、Feature 声明和测试共享的 panel 合同：
- `DockArea`
- `DockContentCachePolicy`
- `EditorPanelContentModelKind`
- `EditorPanelContentModelReference`
- `EditorPanelContributionDescriptor`
- `EditorPanelFrameContext`
- `EditorPanelFrameUpdateDescriptor`
- `EditorPanelFrameUpdateMode`
- `EditorPanelFrameUpdateRequest`
- `EditorPanelLifecycleContext`
- `EditorPanelLifecycleDescriptor`
- `EditorPanelLifecycleMode`
- `PanelDescriptor`
- `PanelKind`

规则：
- 只表达 panel 元数据、默认 Dock 区域、内容模型引用、生命周期和 frame update 合同。
- 不引用 Shell docking registry/instance manager、Dock ViewModel、Avalonia control 或具体 Feature 内容对象。
- panel registry 和 lifecycle/frame sink 接口在 `Core/Abstractions`；Shell 拥有 docking、调度和面板实例生命周期实现。

### 3.2.7 Core/Models/Contributions

放跨 Core service、Shell composition、Feature 声明和测试共享的 contribution 声明、来源和校验合同：

- `EditorActionContributionDescriptor`
- `EditorContributionDescriptorSet`
- `EditorContributionSourceId`
- `EditorContributionSourceKind`
- `EditorContributionValidationContext`
- `EditorContributionValidationError`
- `EditorContributionValidationResult`

规则：
- 只表达 contribution 输入、来源类型、声明集合和结构化校验结果。
- 不引用 Shell composition host、feature catalog、注册表实现、Avalonia control 或持久化格式。
- contribution builder/host 仍在 `Shell/Composition`；validator 作为 UI-neutral 纯校验服务留在 `Core/Services`。

### 3.2.8 Core/Models/BackgroundTasks

放跨 Core service contract、Shell service 实现和测试共享的后台任务状态合同：

- `EditorBackgroundTaskId`
- `EditorBackgroundTaskSnapshot`
- `EditorBackgroundTaskState`

规则：
- 只表达后台任务标识、运行状态、进度、消息和取消能力。
- 不引用 Shell background task service、UI 状态栏/面板、dispatcher、线程或 Avalonia control。
- `IEditorBackgroundTaskService` 仍在 `Core/Abstractions`；Shell 拥有默认服务实现和任务生命周期调度。

### 3.2.9 Core/Models/Lifecycle

放跨 Core service contract、Shell lifecycle service、窗口 hook 和测试共享的生命周期事件合同：

- `EditorLifecycleEventKind`
- `EditorLifecycleEventSnapshot`

规则：
- 只表达编辑器生命周期事件类型、来源、消息、顺序号和发生时间。
- 不引用 Shell window、dock floating window、Avalonia routed event 或具体应用关闭流程。
- `IEditorLifecycleEventService` 仍在 `Core/Abstractions`；Shell 拥有事件发布实现和窗口生命周期 hook。

### 3.2.10 Core/Models/Extensions

放跨 Core extension contract、Shell composition、registry 和测试共享的扩展身份合同：

- `EditorExtensionId`

规则：
- 只表达 extension/contribution owner 的稳定身份值。
- 不引用 feature module 实现、Shell registry、provider host 或插件加载生命周期。
- contribution/source 描述仍放 `Core/Models/Contributions`；扩展身份本身放 `Extensions`，供 registry owner tracking 复用。

### 3.2.11 Core/Models/Editing

放跨 Core edit command contract、Shell transaction service 和测试共享的编辑命令描述合同：

- `EditorEditCommandDescriptor`
- `EditorEditMergePolicy`
- `EditorEditValidationResult`

规则：
- 只表达编辑命令的目标、字段、旧值/新值、显示标签、校验结果和合并策略。
- 不引用 Shell transaction service、具体 inspector/scene edit command 实现、undo stack 或 UI 控件。
- `IEditorEditCommand` 仍在 `Core/Abstractions`；具体命令和事务执行由 Shell 或 Feature 实现。

### 3.2.12 Core/Models/Transactions

放跨 Core transaction service contract、Shell transaction service 和测试共享的事务状态合同：

- `EditorTransactionId`
- `EditorTransactionServiceSnapshot`

规则：
- 只表达事务标识、当前事务标签、dirty/undo/redo 状态和诊断摘要。
- 不引用具体 edit command 实现、undo/redo stack entry、Shell service 内部状态或 UI 展示。
- transaction service 接口仍在 `Core/Abstractions`；Shell 拥有事务栈和执行策略。

### 3.3 Core/Services

当前只放 UI-neutral 的内存实现和纯校验：

- `EditorDiagnosticService`
- `InMemorySceneSnapshotProvider`
- `ImmediateEditorUiDispatcher`
- `EditorContributionDescriptorValidator`

放置判断：

- 无 Avalonia、无 IO、无平台 API、无 feature 业务。
- 可以作为默认内存实现或测试 fixture。

如果服务开始读写文件、访问系统、持久化设置、桥接底层 runtime，应移出 `Core/Services`，进入后续 `Infrastructure` 或专门 adapter 层。

### 3.4 Core/CodeFirstUI

放 Code-first UI 的平台无关部分：

- authoring API：`EditorGui`
- building：`GuiFrameBuilder`
- event queue：`GuiEventQueue`
- state store：`GuiStateStore`
- validation：`GuiTreeValidator`
- node model：`GuiNode`、`GuiNodePayload` 等
- panel base contract：`CodeFirstEditorPanel`

边界：

- 这里定义“能画什么”和“树是否合法”。
- 这里不创建 Avalonia 控件。
- 这里不执行 renderer/native 操作。

### 3.5 Core/Interop

放 UI-neutral 的托管/原生边界代码：

- `Core/Interop/{Feature}/Api`：P/Invoke entrypoint 声明、native buffer/status/format 等 ABI 边界类型、只表达原始 native API 合同。
- `Core/Interop/{Feature}/Adapters`：把 native frozen payload 复制成 Core snapshot payload 的托管 adapter，或把 Core contract 命令编码成 native API 调用。

规则：

- 只表达稳定 ABI 和内存所有权，不表达 renderer live object、Vulkan handle 或窗口 surface。
- native buffer 必须在复制到托管内存后释放；UI 层只能继续消费 provider contract 或 immutable snapshot。
- `Api` 目录不实现 `Core/Abstractions` 的业务接口；业务接口适配放 `Adapters`。
- 默认 composition 不应在 native shared library 和 renderer lifetime owner 完成前强制加载 native library。

## 4. Shell

`Shell` 拥有编辑器外壳。它可以引用 `Core` 和 `UI`，也可以在 composition/catalog 层注册内置 Features，但不要把具体 feature 业务写进 Shell。

### 4.1 Shell/Composition

放应用组合和扩展注册：

- `StudioCompositionRoot`
- `EditorFeatureCatalog`
- `EditorExtensionHost`
- `EditorContributionBuilder`
- provider host / descriptor adapter

职责：

- 创建核心服务实例。
- 注册内置 feature module。
- 汇总 panel/action/provider contribution。

不要放：

- 单个面板的 ViewModel 行为。
- Scene/asset/render graph 的业务投影。

### 4.2 Shell/Docking

放 Dock layout、hit test、drop、tab、panel registration / instance 管理：

- `Panels/PanelRegistry`
- `Panels/PanelInstanceManager`
- `DropTargets/EditorDockDropGuideKind`
- `DropTargets/EditorDockDropOperation`
- `DropTargets/EditorDockDropTarget`
- `DropTargets/EditorDockHitTestService`
- `DropTargets/EditorDockSplitterBounds`
- `DropTargets/EditorDockWindowBounds`
- `Layout/EditorDockLayoutSnapshot`
- `Layout/EditorDockLayoutStore`
- `TabStrips/EditorDockTabBounds`
- `TabStrips/EditorDockTabReorderResolver`
- `TabStrips/EditorDockTabStripScrollController`

分类标准：

- 只处理 shell 布局和 panel lifecycle。
- 不关心 panel 内容是什么。
- `Shell/Docking` 根目录只作为 Docking 分类入口；具体实现文件放入下面的 ownership 子目录。
- Panel registration 和 panel instance lifecycle 放 `Shell/Docking/Panels`。
- Drop target vocabulary、hit testing 和 hit-test bounds 放 `Shell/Docking/DropTargets`。
- Layout snapshot 和 layout persistence 放 `Shell/Docking/Layout`。
- Tab strip geometry、reorder、scrolling 放 `Shell/Docking/TabStrips`。
- 不放 Avalonia View、XAML 或 Dock presentation ViewModel；这些放 `Shell/Views/Docking` 和 `Shell/ViewModels/Docking`。

### 4.3 Shell/Commands

放全局 command/action 路由：

- `WorkbenchCommandRouter`
- `WorkbenchActionRegistry`
- `WorkbenchActionExecutor`
- shortcut router
- command feedback router
- panel command service

所有菜单、快捷键、Command Palette、后续 toolbar/context menu 都应回到这一类入口。

### 4.4 Shell/ViewModels 与 Shell/Views

放编辑器外壳自身的 VM 和 View：

- `MainWindow` / `MainWindowViewModel`
- dock workspace/window/tab/split/floating views
- command palette
- dialog host
- panel placeholder
- shell menu item VM

规则：

- View 负责 XAML、布局和 view-only code-behind。
- ViewModel 负责 shell 状态、命令入口和轻量 UI 投影。
- 不把 feature 数据模型塞进 shell VM。
- 成组的 shell 表面要拆到同名子目录，例如 `Shell/Views/Windowing`、`Shell/ViewModels/Windowing`、`Shell/Views/Docking`、`Shell/ViewModels/Docking`、`Shell/Views/CommandPalette`、`Shell/ViewModels/CommandPalette`、`Shell/Views/Dialogs`、`Shell/ViewModels/Dialogs`、`Shell/Views/Panels`、`Shell/ViewModels/Panels`。
- Root `MainWindow` 和它的 VM 属于 windowing 入口层，放 `Shell/Views/Windowing` 和 `Shell/ViewModels/Windowing`；`App.axaml.cs` 只负责创建窗口和接入 composition session。
- Dock workspace/window/tab/split/floating 的 View 和 ViewModel 属于 Dock presentation，统一放 `Shell/Views/Docking` 和 `Shell/ViewModels/Docking`。
- Panel placeholder 属于 shell panel presentation，放 `Shell/Views/Panels` 和 `Shell/ViewModels/Panels`。
- `Shell/Commands` 只拥有命令注册、路由和执行；Command Palette 的可见状态、筛选结果和行 VM 属于 `Shell/ViewModels/CommandPalette`。
- 菜单项 VM 属于 `Shell/ViewModels/Menus`；它们只投影菜单显示状态和执行入口，不拥有命令路由本身。

### 4.5 Shell/Services

放 shell 拥有的应用服务实现：

- UI dispatcher implementation
- background task service
- lifecycle event service
- panel frame scheduler
- transaction service v0

`IEditorUiDispatcher` 合同位于 `Core/Abstractions`。Shell 只保留 `AvaloniaEditorUiDispatcher` 作为 Avalonia 实现；普通 Feature 不直接引用 `Shell.Services`。

### 4.6 Shell/Selection

放 selection service 实现。接口和 snapshot 在 `Core`，实现由 Shell 组合根拥有。

### 4.7 Shell/CodeFirstUI

放 Code-first 到 Avalonia 的 Shell 适配：

- `CodeFirstPanelHostViewModel`
- `CodeFirstPanelHostView`
- `GuiAvaloniaControlFactory`
- text commit scheduler
- host event bridge

分类标准：

- 这里消费 `Core/CodeFirstUI` 的树。
- 这里创建 Avalonia 控件。
- 这里负责 Shell 生命周期和失败隔离。
- 这里不定义新的业务面板数据合同。

## 5. UI

`UI` 只放可复用视觉能力。它可以引用 `Core` 的纯模型，但不能引用具体 `Features`。

### 5.1 UI/Styles

放资源字典和主题 token：

- `Tokens/DeepDarkColors.axaml`
- `Tokens/EditorMetrics.axaml`
- `Shell/MainMenu.axaml`

规则：

- 颜色、字号、尺寸、间距先进入 token，再被控件/视图引用。
- 不在具体 Feature View 中硬编码主题色，除非是一次性局部布局且没有复用价值。

### 5.2 UI/Controls/Base

放基础可复用控件：

- `IconButton`
- `SearchBox`
- `Native/*` 控件样式覆盖

规则：

- 通用控件要有明确 public property、样式入口和最小可用场景。
- 不放只服务单个 Feature 的控件。

### 5.3 UI/Controls/Tree

放树结构基础设施：

- tree node/row model
- flattener
- expansion state
- indent guide
- shared tree styles / metrics / class names

规则：

- 这里只表达通用树 UI 行为。
- Hierarchy 的 scene object 投影仍留在 `Features/Hierarchy`。

### 5.4 UI/Controls/Feedback

放通用反馈控件，例如 `ActivityIndicator`。

### 5.5 UI/Icons

放可复用图标键、注册表和图标控件：

- `EditorIconKey`
- `EditorIconRegistry`
- `EditorIconView`

规则：

- Shell、UI 控件和 Feature 都从 `UI/Icons` 消费图标。
- 普通 Feature 不引用 `Shell.Icons`，也不自己持有 Lucide/Avalonia 图标映射。
- 新图标键必须表达稳定 UI 语义，不要把临时业务状态硬编码成公共 icon key。

### 5.6 UI/ViewModels

放可复用 presentation 基础设施：

- `ViewModelBase`

规则：

- Shell 和 Feature 都从 `UI/ViewModels` 消费共享 VM 基类。
- `ViewModelBase` 只包装 CommunityToolkit MVVM 基类，不放 Shell 服务、Feature 服务或 Avalonia 控件。
- 具体窗口、Dock、菜单、面板状态仍放在各自 Shell/Feature ViewModel 目录。

### 5.7 UI/Theme

当前 `UI/Theme` 是项目文件里的空占位。新增主题文件优先放到 `UI/Styles/Tokens` 或 `UI/Styles/Shell`。除非先写主题目录设计，不要往 `UI/Theme` 随意加文件。

## 6. Features

`Features` 放垂直功能切片。普通 Feature 不直接依赖另一个普通 Feature；跨 Feature 通信通过 `Core` 合同、Shell 注册、selection、diagnostics、command 或 provider。

推荐结构：

```text
Features/{FeatureName}
  Models
  ViewModels
  Views
  Services
  Commands
  Controls
  Styles
```

不是每个目录都必须存在。没有真实代码时不要创建空目录。

### 6.1 Features/Hierarchy

分类：

- scene snapshot 到树 UI 的只读投影
- hierarchy selection 写入
- tree row VM

不负责：

- scene truth
- runtime scene mutation
- inspector document generation
- native scene query

### 6.2 Features/Inspector

分类：

- selection 到 inspector document 的只读投影
- inspector model
- inspector panel view / VM

不负责：

- writable transform/component editing
- undo/redo transaction authoring
- scene object lifetime

### 6.3 Features/SceneView

分类：

- scene view shell
- viewport unavailable/deferred state
- selection bridge v0

不负责：

- native Vulkan viewport
- render loop ownership
- renderer handle lifetime
- swapchain or GPU resource control

### 6.4 Features/Console

分类：

- diagnostics stream projection
- console panel view / VM

不负责：

- native log ingestion
- shell command input
- persisted log storage

### 6.5 Features/Problems

分类：

- problem diagnostics projection
- validation/error list UI

不负责：

- generating all validation facts itself
- owning source-of-truth validation services

### 6.6 Features/UiStyle

分类：

- Code-first UI component guide / sample panel
- internal style validation surface

不负责：

- production user workflow
- theme token ownership

### 6.7 Features/Workbench

当前 `Features/Workbench` 是内置 feature 聚合模块，不是普通业务 Feature。

允许它：

- 注册内置 panels/actions/providers。
- 创建 shared fixture services for built-in features。
- 引用多个 feature ViewModel 作为内置 composition wiring。
- 注入 Shell-owned adapters，例如 `AvaloniaEditorUiDispatcher`。

禁止它：

- 成为所有 feature 共享工具箱。
- 放具体面板业务逻辑。
- 绕过 Core/Shell 注册机制直接操作 Dock tree。

如果后续引入外部插件或多 feature catalog，`WorkbenchFeatureModule` 应继续收敛为内置声明层，而不是扩张成 service locator。

## 7. Tests

`Tests/Editor.Tests` 按被测层镜像组织：

```text
Tests/Editor.Tests/Core
Tests/Editor.Tests/Shell
Tests/Editor.Tests/UI
Tests/Editor.Tests/Features
```

测试分类：

- Core：模型、纯服务、Code-first tree/build/state/validation。
- Shell：dock、command、composition、VM、XAML 约束、windowing。
- UI：通用控件和树基础设施。
- Features：各 Feature 的 ViewModel、XAML 约束和 wiring。

规则：

- 新增行为先补对应层测试。
- XAML 结构约束可以用 source-level 测试锁住关键绑定、样式和设计时规则。
- UI-sensitive 改动除测试外还要手动启动检查。

## 8. 文档分类

`docs` 按用途分类：

- 总纲：`Studio框架设计.md`
- 执行规范：`项目规范.md`
- 代码放置：`Studio代码分类.md`
- UI 平台边界：`编辑器UI平台规范.md`
- 控件实现：`控件开发指南.md`
- 专题实现事实：`Dock系统指南.md`、`Code-first UI设计.md`
- 回归清单：`Dock手工回归清单.md`
- 长期需求目录：`编辑器功能需求清单.md`、`编辑器UI组件.md`
- 计划归档：`docs/superpowers/plans`

规则：

- 当前事实写专题文档。
- 长期规则写规范。
- 设计取舍写框架设计或 ADR/设计说明。
- 历史计划不要事后改成当前状态，除非明确标注为勘误。

## 9. 新文件放置决策表

| 你要新增的代码 | 放置位置 |
| --- | --- |
| 跨 Shell/Feature 共享的接口 | `Core/Abstractions` |
| 跨 Shell/Feature 共享的不可变状态 | `Core/Models` |
| Dialog request / result 合同模型 | `Core/Models/Dialogs` |
| Selection snapshot / item / changed event 合同模型 | `Core/Models/Selection` |
| Diagnostics record / channel / severity / source descriptor 合同模型 | `Core/Models/Diagnostics` |
| Scene snapshot / object / provider descriptor 合同模型 | `Core/Models/Scene` |
| Workbench action / command execution / command feedback 合同模型 | `Core/Models/Workbench` |
| Panel descriptor / lifecycle / frame update / dock vocabulary 合同模型 | `Core/Models/Panels` |
| Contribution descriptor / source / validation 合同模型 | `Core/Models/Contributions` |
| Background task id / snapshot / state 合同模型 | `Core/Models/BackgroundTasks` |
| Lifecycle event kind / snapshot 合同模型 | `Core/Models/Lifecycle` |
| Extension identity 合同模型 | `Core/Models/Extensions` |
| Editing command descriptor / validation / merge policy 合同模型 | `Core/Models/Editing` |
| Transaction id / service snapshot 合同模型 | `Core/Models/Transactions` |
| 纯内存默认实现或校验器 | `Core/Services` |
| Code-first 平台无关 authoring/tree/state | `Core/CodeFirstUI` |
| UI-neutral native API / P/Invoke 合同 | `Core/Interop/{Feature}/Api` |
| UI-neutral native adapter / bridge 实现 | `Core/Interop/{Feature}/Adapters` |
| Code-first Avalonia 控件生成和 host | `Shell/CodeFirstUI` |
| Dock layout / hit test / drop 管理 | `Shell/Docking` |
| Panel registry / panel instance lifecycle | `Shell/Docking/Panels` |
| Drop target vocabulary / hit testing | `Shell/Docking/DropTargets` |
| Dock layout snapshot / persistence | `Shell/Docking/Layout` |
| Tab strip geometry / reorder / scrolling | `Shell/Docking/TabStrips` |
| Root window View / VM | `Shell/Views/Windowing`、`Shell/ViewModels/Windowing` |
| Dock workspace/window/tab/split/floating View / VM | `Shell/Views/Docking`、`Shell/ViewModels/Docking` |
| 菜单、快捷键、Command Palette 共用命令 | `Shell/Commands` |
| Command Palette View / VM / row state | `Shell/Views/CommandPalette`、`Shell/ViewModels/CommandPalette` |
| Dialog Host View / VM / button row state | `Shell/Views/Dialogs`、`Shell/ViewModels/Dialogs` |
| Panel placeholder View / VM | `Shell/Views/Panels`、`Shell/ViewModels/Panels` |
| Workbench 菜单项 / panel 菜单项 VM | `Shell/ViewModels/Menus` |
| 应用组合、内置 feature catalog | `Shell/Composition` |
| 通用视觉 token | `UI/Styles/Tokens` |
| 通用基础控件 | `UI/Controls/Base` |
| 通用树控件基础设施 | `UI/Controls/Tree` |
| 通用图标键、注册表和图标控件 | `UI/Icons` |
| 共享 ViewModel 基类或轻量 presentation 基础设施 | `UI/ViewModels` |
| 某个面板专属 View / VM / Model | `Features/{Name}/Views`、`ViewModels`、`Models` |
| 某个 feature 专属服务 | `Features/{Name}/Services` |
| 跨 feature 的真实底层数据源 adapter | 先设计 Core contract，再放 Shell/Infrastructure/adapter 层 |
| 测试 | 镜像被测目录放到 `Tests/Editor.Tests/...` |

## 10. 当前允许的 v0 例外

这些例外是当前代码事实，不应无限扩大：

1. `Features/Workbench` 可以引用 `Shell.Services` 注入 Shell-owned adapters。
   - 普通 Feature 不获得这个例外。
   - dispatcher 合同在 `Core/Abstractions`，Avalonia 实现在 Shell。
2. `Features/Workbench` 引用多个 Feature。
   - 这是内置聚合模块特权。
   - 普通 Feature 不获得这个例外。
3. `Core/Services` 有内存实现。
   - 只允许无 IO、无平台、无 feature 业务的默认实现。
   - 持久化或底层桥接实现不能继续放这里。

## 11. 迁移优先级

近期不做大规模搬目录。优先级如下：

1. 新增代码先按本文分类放正确。
2. 明显模板残留直接删除。
3. 只有当文件阻碍新切片或制造错误依赖时才移动。
4. 移动前先加或确认测试覆盖 ViewLocator、namespace、XAML `x:Class` 和绑定。
5. 移动后必须跑 `dotnet test Editor.sln`、`git diff --check` 和编码检查。

## 12. 快速判断

不确定放哪里时，按这个顺序判断：

```text
是否只服务一个 Feature？
  是 -> Features/{FeatureName}
  否 -> 是否是视觉控件或样式？
    是 -> UI
    否 -> 是否是跨层合同或 snapshot？
      是 -> Core
      否 -> 是否是编辑器外壳编排？
        是 -> Shell
        否 -> 先不要新增抽象，回到具体使用场景。
```

分类的目标是减少错误依赖，不是追求目录数量。能不搬就不搬；新增代码必须放准。
## 2026-07-04 Frame Debugger snapshot v0 classification

- `Core/Models/FrameDebug` owns immutable, UI-neutral Frame Debugger snapshot records.
- `Core/Abstractions/IFrameDebuggerSnapshotProvider.cs` is the provider contract consumed by Studio features.
- `Core/Interop/FrameDebugger/Api` owns raw native API contracts: P/Invoke symbols, native buffer/status structs, and ABI format vocabulary.
- `Core/Interop/FrameDebugger/Adapters` owns managed bridge code: UTF-8 command encoding, copy-then-release ownership, and `INativeFrameDebuggerBridge` implementation.
- `Core/Services/InMemoryFrameDebuggerSnapshotProvider.cs` is a fixture/test publication seam only; native adapters must publish through the contract later instead of leaking Vulkan or C++ objects into UI.
- `Features/FrameDebugger/FrameDebuggerPanel.cs` is a Code-first, read-only panel. It may render snapshots and publish diagnostics, but it must not call native ABI, P/Invoke, Vulkan handles, or renderer objects.
- `Features/Workbench/WorkbenchFeatureModule.cs` may create the fixture-backed provider and register the built-in panel as part of built-in composition wiring.

## 2026-07-04 Frame Debugger editor_native ABI v0 classification

- `apps/editor` owns the `editor-native` shared library target because it already owns the C++ editor Frame Debugger state, replay model, and snapshot projection.
- `apps/editor/src/native_bridge` owns exported C ABI entrypoints, ABI structs, result codes, and native-owned buffer release functions.
- `apps/editor/src/editor_frame_debugger_snapshot_projector.*` owns backend-neutral snapshot JSON projection shared by the editor smoke and native ABI bridge.
- `apps/editor/src/native_bridge/frame_debugger_native_smoke.*` owns CLI smoke coverage for ABI header/version, explicit UTF-8 byte lengths, native-owned snapshot release, and CPU-only command forwarding.
- The native ABI v0 is CPU-only. It must not create a Vulkan surface, expose renderer handles, or make Avalonia/Studio own swapchain or GPU resource lifetime.

## 2026-07-05 Viewport contract and scheduler classification

- `Core/Models/Viewports` owns immutable, UI-neutral viewport identity, extent, clock, render reason, update policy, scheduler context, render request, and render result contracts.
- `Core/Services/ViewportScheduler.cs` owns the pure managed render-request planner for Phase 1. It consumes viewport snapshots and emits `ViewportRenderRequest` snapshots only.
- The Phase 1 viewport scheduler is CPU-only and must not reference Avalonia controls, composition surfaces, platform window handles, native pointers, Vulkan handles, RenderGraph objects, or renderer live state.
- `Features/SceneView` may consume these contracts in a later slice, but it must still not own native Vulkan viewport lifetime or renderer handles.
- Native viewport bridge, Avalonia composition import, and GamePreview swapchain work remain separate implementation slices.
