# Studio 编辑器 UI 平台规范

状态：当前阶段执行规范
更新日期：2026-06-22

本文定义 `apps/studio` 当前阶段的编辑器 UI 平台边界。目标是先把弹窗、后台加载反馈、快捷键、命令菜单、状态反馈和设计时预览做成稳定基础，再继续 Scene、Play Session、native bridge 或插件热更新。

本文不替代：

- [项目规范.md](项目规范.md)：目录、命名空间、MVVM、性能和合入规则。
- [控件开发指南.md](控件开发指南.md)：Avalonia 控件、样式、`Design.PreviewWith` 和主题覆盖规则。
- [Dock系统指南.md](Dock系统指南.md)：Dock、panel、workbench action 和当前实现事实。
- [../../docs/architecture/managed-extension-model.md](../../../docs/architecture/managed-extension-model.md)：长期 managed/plugin/native bridge ADR。

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
Command result feedback -> Background Tasks panel -> Diagnostics/Problems route -> Shortcut/Command settings
```

理由：

1. Scene 底层、schema、Edit World / Play World 和 native bridge 还没有足够稳定的写入合同。
2. 已有 `PanelDescriptor`、`WorkbenchActionDescriptor`、`WorkbenchCommandRouter`、`WorkbenchShortcutRouter`、`EditorDialogHostViewModel`、`IEditorBackgroundTaskService`、`IEditorTransactionService` 和 `IEditorLifecycleEventService`，这些才是当前真实可扩展面。
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
| Modal dialog | `EditorDialogRequest`, `EditorDialogHostViewModel`, `EditorDialogHostView` | Current |
| 后台任务状态 | `IEditorBackgroundTaskService`, `EditorBackgroundTaskService` | Current |
| 事务服务 v0 | `IEditorTransactionService`, `EditorTransactionService` | Current / UI-neutral |
| 生命周期事件 v0 | `IEditorLifecycleEventService`, `EditorLifecycleEventService` | Current / Shell window lifecycle only |
| Dock tab overflow v0 | `EditorDockTabStripScrollController`, `EditorDockTabStripView` | Current / view-only scroll state |
| 状态栏反馈 | `ActivityIndicator`, `MainWindowViewModel` summary properties | Partial |
| UI 线程切回 | `IEditorUiDispatcher`, `AvaloniaEditorUiDispatcher` | Current |
| 只读 Scene snapshot | `ISceneSnapshotProvider`, `InMemorySceneSnapshotProvider` | Current / read-only |

当前仍不稳定或未实现：

| 能力 | 当前策略 |
| --- | --- |
| 完整快捷键管理窗口 | Planned；先保留 action descriptor 和 shortcut router，不做用户配置 UI |
| Toast / non-modal notification | Planned；先使用 command result 和 background task diagnostics 设计，不急着实现 |
| Background Tasks 窗口 | Planned；下一阶段推荐切片 |
| Advanced tab strategy | Planned；多行 tab、隐藏 tab 菜单、pin/preview tab 另起切片 |
| Writable Inspector | Deferred；等 schema metadata、真实 provider、dirty-state UI 和写回 gate |
| Scene authoring / hierarchy mutation | Deferred；等 native/scene bridge 和 edit/apply contract |
| Feature/provider/plugin lifecycle | Deferred；当前 lifecycle events 只覆盖 Shell window 生命周期 |
| Play Session | Deferred；等 Edit World / Play World copy 或 load 语义 |
| Managed plugin hot reload | Deferred；等 contribution registry、diagnostics、ALC unload negative smoke |
| Native C ABI | Deferred；等 CPU-only bridge consumer 和 ABI checklist |
| Avalonia native Vulkan viewport | Deferred；等 CPU-only native bridge 与 viewport ownership 设计 |

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
- `EditorDialogHostView.axaml.cs` 负责打开后 focus 和 `Escape` cancel。

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
- `EditorDialogHostViewModel` 只保存 active request、按钮投影和 completion。

### 3.4 Services

Services 必须先说明 owner、生命周期、线程边界和错误路径。

| 类型 | 当前或未来落点 | 规则 |
| --- | --- | --- |
| Core abstraction | `IEditorBackgroundTaskService`、`IEditorTransactionService`、`IEditorLifecycleEventService`、`IPanelRegistry`、`IEditorSelectionService` | 只描述合同，不依赖 Avalonia 控件。 |
| Shell service | `EditorBackgroundTaskService`、`EditorTransactionService`、`EditorLifecycleEventService`、`PanelCommandService`、`AvaloniaEditorUiDispatcher` | 负责 Shell 编排和 UI host 适配。 |
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
Command execution feedback
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
4. `WorkbenchCommandExecutionResult` 是命令反馈的唯一基础模型。
5. disabled、not found、failed 不应被 UI surface 吞掉。下一阶段应进入 status/toast/problems/console 的统一反馈路径。
6. 后续 Undo / Redo 必须接同一命令体系，不能另开一套隐藏入口。

推荐下一切片：

```text
[Slice] Studio: command result feedback surface
```

范围：

- 在状态栏或轻量 notification surface 显示最近命令失败/禁用原因。
- 将 `WorkbenchCommandExecutionResult` 转为 UI feedback record。
- 不做 toast 动画系统，不做完整 notification center。

## 6. Dialog / Popup 合同

当前 `EditorDialogHostViewModel` 支持单 active modal dialog。它适合确认、About、错误和简单 blocking decision。

规则：

1. 第一阶段只允许一个 active modal。
2. Dialog 请求使用 `EditorDialogRequest`，结果使用 `EditorDialogResult`。
3. Dialog host 不执行业务逻辑，只完成用户选择。
4. Dialog overlay 优先级高于 command palette。
5. 长任务不要用 modal dialog 承载进度，必须进 `IEditorBackgroundTaskService`。
6. 非阻塞通知和 toast 另做独立 surface，不混入 modal dialog host。

推荐下一切片：

```text
[Slice] Studio: dialog result and command failure presentation
```

范围：

- 复用 dialog host 显示可恢复错误或 confirmation。
- command failure 默认走状态/diagnostics，不弹阻塞框。
- 只对 destructive 或必须用户决策的操作使用 modal。

## 7. Background Activity 合同

`IEditorBackgroundTaskService` 是后台加载、导入、验证、构建、缩略图、桥接调用和未来 Play Session 准备的共同入口。

规则：

1. 后台任务必须有稳定 `operationId`。
2. ViewModel 只能消费 snapshot，不持有 worker、engine object 或 native handle。
3. progress 可以为空，表示 indeterminate。
4. cancellation 需要 `CanCancel`，但取消执行本身属于后续 worker contract。
5. 任务状态变化可来自后台线程，Shell 必须通过 UI dispatcher 刷新绑定状态。
6. 状态栏只显示摘要。完整任务列表、日志、失败详情和 retry 属于 `Background Tasks` panel。

推荐下一切片：

```text
[Slice] Studio: background tasks panel
```

范围：

- 新增 `Features/BackgroundTasks` 面板。
- 显示 active/completed/failed snapshots。
- 状态栏点击打开该面板。
- 不实现真实 asset import、shader compile 或 native load。

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

- 覆盖 `ActivityIndicator`、`EditorDialogHostView`、`CommandPaletteView`、原生控件覆盖样式。
- 不做截图测试系统。
- 不引入运行时 fake service registry。

## 10. 通用 UI 抽象边界

现在可以做有限的通用 UI 平台，不要做“完整 Unity UI 框架克隆”。

可以现在稳定：

```text
PanelDescriptor
WorkbenchActionDescriptor
WorkbenchCommandExecutionResult
EditorDialogRequest / EditorDialogResult
EditorBackgroundTaskSnapshot
EditorTransactionServiceSnapshot
EditorLifecycleEventSnapshot
Status feedback record
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
| Dialog | data-only request/result | Feature 直接创建 modal Window 或在 dialog 中执行业务。 |
| Lifecycle | Shell window lifecycle snapshot | 把 feature unload、provider reload、Play Session 或 native runtime lifecycle 混入 v0。 |
| Status overlay | task snapshot / command result / diagnostic record | 一次性做完整 notification center 或 project dashboard。 |

状态反馈的当前边界：

1. 全局状态栏只显示跨 Feature 的持续状态，例如后台任务、最近命令失败、诊断计数、dirty/play 状态。
2. Feature 内部状态留在 Feature 面板内，除非它影响全局健康状态。
3. 状态反馈必须来自 data-only snapshot 或 command result，不从 View 反查控件状态。
4. Dialog 只用于必须阻塞用户决策的情况。普通失败、禁用和后台进度优先进入 status/diagnostics。
5. Problems / Console 接入前，先定义 UI-level diagnostic record，不直接读取 native engine log。

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
Read-only scene snapshot provider feeds Hierarchy / Inspector.
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

1. Current：内置 Feature 通过 `PanelDescriptor` / `WorkbenchActionDescriptor` 注册 panel、menu 和 command。
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
dotnet test apps\studio\Editor.sln -c Release
dotnet test apps\studio\Editor.sln
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

## 13. 推荐下一阶段切片

按稳定性和当前缺口排序：

1. `[Slice] Studio: command result feedback surface`
   - 把 `WorkbenchCommandExecutionResult` 显示到状态栏/轻量反馈。
   - 失败进入 Problems/Console 的设计先写 UI-level contract，不急着做完整面板。
   - 不做 toast 动画系统，不做 notification center。

2. `[Slice] Studio: background tasks panel`
   - 新增后台任务面板，消费 `IEditorBackgroundTaskService` snapshots。
   - 状态栏入口打开或聚焦面板。
   - 不实现真实 asset import、shader compile 或 native load。

3. `[Slice] Studio: diagnostic feedback route`
   - 将 command/background/dialog 失败统一成 diagnostics records。
   - Console/Problems 先接 UI-level diagnostics，不接 native engine。

4. `[Slice] Studio: lifecycle event diagnostics projection`
   - 将 `IEditorLifecycleEventService` 的 Shell window lifecycle snapshots 显示为只读 diagnostics/debug route。
   - 不扩展到 feature unload、provider reload、Play Session 或 native runtime lifecycle。

5. `[Slice] Studio: DI composition root`
   - 集中注册 Shell services、feature modules、registries、dispatcher 和 root view model。
   - 不改变 UI 行为，不引入外部 plugin loader。

6. `[Slice] Studio: view resolution and compiled binding audit`
   - 明确新增 View/DataTemplate 的 `x:DataType` 和显式 mapping 策略。
   - 只处理 Studio UI 层，不做 Native AOT 发布切片。

7. `[Slice] Studio: design preview coverage`
   - 给通用控件和 overlay surfaces 补 design preview。
   - 只做 preview data，不做视觉重设计。

8. `[Slice] Studio: shortcut profile contract`
   - 定义默认快捷键、冲突检测和上下文 scope model。
   - 不做完整设置 UI。

## 14. 参考资料

仓库资料：

- [项目规范.md](项目规范.md)
- [控件开发指南.md](控件开发指南.md)
- [Dock系统指南.md](Dock系统指南.md)
- [superpowers/plans/2026-06-20-studio-runtime-editor-foundation.md](superpowers/plans/2026-06-20-studio-runtime-editor-foundation.md)
- [superpowers/plans/2026-06-21-studio-command-palette-follow-up.md](superpowers/plans/2026-06-21-studio-command-palette-follow-up.md)
- [superpowers/plans/2026-06-21-studio-dock-tab-strip-overflow.md](superpowers/plans/2026-06-21-studio-dock-tab-strip-overflow.md)
- [superpowers/plans/2026-06-22-studio-editor-lifecycle-events.md](superpowers/plans/2026-06-22-studio-editor-lifecycle-events.md)
- [../../../docs/architecture/managed-extension-model.md](../../../docs/architecture/managed-extension-model.md)

官方资料与优秀案例：

- [Avalonia compiled bindings](https://docs.avaloniaui.net/docs/data-binding/compiled-bindings)：`x:DataType` / compiled binding 作为长期可维护性和性能边界。
- [Avalonia XAML previewer and design-time settings](https://docs.avaloniaui.net/docs/app-development/xaml-preview-and-design-settings)：design-time data 只服务预览，不进入 runtime service。
- [Unity EditorWindow](https://docs.unity3d.com/Manual/editor-EditorWindows.html)：编辑器窗口是工具入口，但 Asharia 当前只借鉴 window/menu/workbench 关系，不复制 Unity 扩展 ABI。
- [Godot EditorPlugin custom dock](https://docs.godotengine.org/en/stable/tutorials/plugins/editor/making_plugins.html#a-custom-dock)：dock/plugin 生命周期需要明确初始化与清理；Asharia 当前只做内置 feature 注册和 Shell-owned dock。
- [Unreal Editor Utility Widgets](https://dev.epicgames.com/documentation/en-us/unreal-engine/editor-utility-widgets-in-unreal-engine)：工具 UI 可以作为编辑器内 surface 暴露；Asharia 当前仍禁止外部脚本直接拥有 Avalonia window/control。
