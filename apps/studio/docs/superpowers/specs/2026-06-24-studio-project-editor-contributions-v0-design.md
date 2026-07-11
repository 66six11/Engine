# Studio Project Editor Contributions v0 Design

状态：Historical / Superseded by `docs/architecture/editor-extension-authoring.md`, `studio-extension-model.md`, ADR-0004 and ADR-0005

## Intent

建立项目 `Editor/` 扩展的第一层编辑器框架合同，让未来用户项目、packaged plugin 和当前内置 Feature 都能走同一套 contribution 语义。v0 只定义 data-only descriptor、验证规则和映射边界，不加载外部程序集、不执行用户代码、不 runtime-load XAML、不接 native renderer 或脚本 VM。

这个设计回答的是“用户在项目 `Editor/` 目录下最终能声明什么”，不是“现在如何热重载用户写的 XAML 或 C#”。当前阶段应先把声明、验证、注册、诊断和生命周期边界固定住，再决定可信代码如何被编译和运行。

## Current Context

当前 Studio 已经具备以下基础：

- `EditorExtensionHost` 负责内置 module 的声明、验证、注册所有权和释放。
- `PanelRegistry.RegisterOwned` 和 `WorkbenchActionRegistry.RegisterOwned` 记录 contribution owner。
- `EditorProviderHost` 管理 fixture-backed active scene provider contribution。
- `PanelInstanceManager` 管理 panel content 创建、cache policy、attach/detach 和 dispose。
- `IEditorPanelLifecycleSink` 提供 attached/activated/deactivated/detached callbacks。
- `IEditorPanelFrameUpdateSink` 和 `EditorPanelFrameScheduler` 提供 editor-framework-only frame update request。
- `IEditorDiagnosticService` 将 command/status/console/problems 投影收敛成 UI-neutral diagnostics。

当前仍没有：

- 项目 `Editor/` 目录扫描。
- 外部 C# 编译或程序集加载。
- contribution manifest / descriptor 文件格式。
- runtime XAML loader 策略。
- plugin reload、ALC unload smoke、脚本 VM 或 native bridge。

因此下一步不应直接实现 `Editor/` loader，而应先定义 loader 将来产出的稳定 descriptor 模型。

## External References

- Unity custom Editor window 使用 `EditorWindow` + UI Toolkit；UI 通过 `CreateGUI` 构建，并把 `Editor/` 目录作为 editor-only 编译组织边界。Unity 还说明 hot reload 后 UI 需要重建并恢复状态。这说明成熟工具把 editor UI 代码、UI 资源和 reload state 分开处理，而不是把任意 UI 树交给宿主直接持有。
- Godot `EditorPlugin` 使用 add/remove dock/main-screen/inspector plugin 的成对注册与清理。它的插件可创建 editor control，但生命周期由 editor plugin API 明确管理。
- Avalonia 提供 runtime XAML loader，可以从 stream/string 解析 XAML 对象。这是可用能力，但不是安全边界，也不等于 Studio v0 应把 runtime-loaded XAML 作为 extension ABI。
- Microsoft XAML Hot Reload 是调试/设计期工具链能力，依赖 IDE、调试会话和 runtime context。它不能直接替代 Studio 的 contribution reload、owner tracking、diagnostics 或 failure rollback。

这些案例给 Studio 的结论是：项目 `Editor/` 扩展最终可以像 Unity 一样写 editor-only UI，但第一层 ABI 应该是声明和生命周期，不是 raw XAML 或 raw control ownership。

## Decision

采用“Project Editor source produces descriptors”的模型。

```text
Project Editor Source
    -> compile/load pipeline (Deferred)
        -> EditorContributionDescriptorSet
            -> validation
            -> EditorExtensionHost
                -> typed registries
                -> domain executors
```

v0 只设计和实现 descriptor set 的形状与验证入口。任何真实 source 都是后续 adapter：

```text
BuiltInFeatureAdapter         -> descriptor set (current trusted path)
ProjectEditorDirectoryAdapter -> descriptor set (planned)
PackagedPluginAdapter         -> descriptor set (planned)
NativeBridgeAdapter           -> descriptor set (planned, not generic plugin)
```

Host 只接受 descriptor set，不关心它来自内置 module、项目 `Editor/`、包插件还是 native adapter。Source-specific 加载、编译、信任、卸载和诊断归 source adapter 管理。

## Descriptor Model

v0 descriptor 必须是 UI-neutral、data-only、可验证、可诊断。

```text
EditorContributionDescriptorSet
  SourceId
  SourceKind
  Panels[]
  Actions[]
  DiagnosticSources[]
```

### Source

```text
EditorContributionSourceId
  stable string, e.g. "built-in.workbench" or "project.editor"

EditorContributionSourceKind
  BuiltIn
  ProjectEditor
  PackagedPlugin
  NativeAdapter
```

`SourceKind` 只用于 diagnostics 和 policy。它不能让 descriptor 跳过 validation。

### Panel

```text
EditorPanelContributionDescriptor
  Id
  Title
  Kind
  DefaultDockArea
  MenuPath
  CachePolicy
  ContentModel
  Lifecycle
  FrameUpdate
```

`ContentModel` 不直接返回 Avalonia `Control`。v0 支持两种 planned form：

```text
ViewModelTypeReference
  stable managed type id, resolved only by trusted adapter later

DeclarativePanelModelReference
  stable model id, rendered by Shell-owned DataTemplate later
```

当前实现阶段可以把内置 `PanelDescriptor.CreateContent` 适配成 `ViewModelTypeReference` 的兼容路径，但不能把 `Func<object>` 固化为项目扩展 ABI。

### Action

```text
EditorActionContributionDescriptor
  Id
  Title
  Category
  Scope
  DefaultShortcut
  MenuPath
  CommandId
```

`WorkbenchActionDescriptor` 仍是当前 Shell UI projection。长期应拆成：

```text
CommandContributionDescriptor
  CommandId
  ExecutorReference

WorkbenchActionContributionDescriptor
  UI metadata
  references CommandId
```

v0 可以只规划这个拆分，不实现 command executor loading。

### Diagnostic Source

```text
EditorDiagnosticSourceDescriptor
  Id
  Title
  DefaultChannel
  SourceKind
```

它只声明 diagnostic source metadata。它不订阅 native logs、不打开 shell command input、不提供 provider reload diagnostics。

## Validation Rules

Descriptor validation happens before registry commit:

1. `SourceId` cannot be blank.
2. Contribution ids must be stable, non-empty, and unique within a descriptor set.
3. Contribution ids must not collide with already registered ids unless the same owner is replacing through a future explicit reload transaction.
4. Panel `DefaultDockArea`, `Kind`, `CachePolicy`, lifecycle mode and frame update mode must be defined enum values.
5. Panel `MenuPath` can be empty only for hidden/internal panels; otherwise it must be a stable slash-separated route.
6. Action `CommandId` cannot be blank when action kind requires execution.
7. Diagnostic source ids must not collide with panel/action ids in the same source if the id is used as a route prefix.
8. Validation diagnostics must include source id, contribution id and field name.

Validation returns data, not thrown UI dialogs:

```text
EditorContributionValidationResult
  IsValid
  Errors[]

EditorContributionValidationError
  SourceId
  ContributionId
  Field
  Message
```

Fatal implementation bugs can still throw exceptions, but ordinary invalid descriptor input should be collected and surfaced through diagnostics.

## XAML And ViewModel Boundary

Direct user-authored XAML is a planned authoring format, not the v0 runtime ABI.

Allowed now:

```text
Descriptor says "this panel uses content model X".
Shell chooses View/DataTemplate mapping.
Tests use fake descriptors and fake content model references.
```

Deferred:

```text
Runtime XAML loader parses arbitrary project file into Control.
Project extension returns Avalonia UserControl.
Plugin creates Window or owns floating host.
Hot reload replaces live visual tree.
```

Reasoning:

- Raw Avalonia controls tie plugin lifecycle to UI thread, resource dictionaries, compiled bindings, disposal and theme state.
- Runtime XAML loader cannot be the security or compatibility boundary.
- Hot reload requires state preservation, failure rollback and unload evidence.
- Shell must own view resolution so the same descriptor can later render in main dock, floating dock, design preview, or headless tests.

## Lifecycle Integration

Project editor contributions use the existing lifecycle layers:

```text
Descriptor registered
  -> panel opened
    -> PanelInstanceManager creates content
    -> IEditorPanelLifecycleSink callbacks
    -> IEditorPanelFrameUpdateSink scheduled if present
  -> panel closed/reset/shutdown
    -> detach
    -> dispose according to cache policy
```

The descriptor itself does not receive per-frame updates and does not run code. Runtime behavior belongs to the materialized content object created by a trusted factory/adapter later.

Future reload must be a contribution reload transaction:

```text
Validate new descriptor set
  -> compare contribution ids
  -> close or detach affected panel instances
  -> remove old leases
  -> register new descriptors
  -> reopen/focus only when policy says so
```

It must not restart the Avalonia app, clear the whole Dock tree, or recreate native renderer resources.

## Error Handling And Diagnostics

Descriptor import should produce structured diagnostics:

```text
Invalid descriptor field
Duplicate contribution id
Unsupported content model kind
Deferred capability requested
Source adapter failure
```

Each diagnostic must identify:

- source id;
- source kind;
- contribution id if available;
- severity;
- human-readable message;
- whether the failure blocks commit.

Invalid project editor descriptors must not leave partial panel/action registrations behind.

## Migration Slices

### Slice 1: Descriptor Models And Validator

- Add Core descriptor models for source, panel, action and diagnostic source.
- Add validation result/error models.
- Add a validator service in Shell or Core depending on dependency needs.
- Tests cover blank ids, duplicate ids, invalid enum values, missing command ids and unsupported content model kinds.
- No registry commit and no project `Editor/` scanning.

### Slice 2: Built-In Contribution Adapter

- Adapt existing built-in `EditorContributionBuilder` output into descriptor sets.
- Preserve all current panel/action ids.
- Commit descriptor-validated built-in contributions into existing typed registries.
- Tests prove validation runs before commit and rollback preserves existing behavior.

### Slice 3: Descriptor-To-Panel Mapping Boundary

- Add a Shell-owned panel content resolver abstraction.
- Current built-in factories become trusted resolver entries.
- Project/user descriptors can reference content model ids but cannot instantiate raw controls.
- Tests prove unsupported content model ids fail with diagnostics.

### Slice 4: Project Editor Manifest Stub

- Define a manifest format or in-memory manifest model for `Editor/`.
- Parse only static metadata in tests.
- No C# compile, no file watching, no hot reload.
- Diagnostics explain that execution is deferred.

## Alternatives

### Direct Runtime XAML Loader

Rejected for v0. Avalonia can load XAML at runtime, but that would make arbitrary XAML object graphs part of the plugin ABI before Studio has trust policy, unload evidence, resource isolation, binding diagnostics or failure rollback.

### Plugin Returns Avalonia Control

Rejected for v0. This is similar to mature editor plugin APIs, but Studio does not yet have enough lifecycle and UI-thread policy to let project code own controls. Returning ViewModel/declarative model keeps Shell in charge of presentation.

### Wait For Script Framework

Rejected. The descriptor boundary is useful before scripts exist. It lets current built-in modules, future project editor code and packaged plugins share validation and diagnostics.

### One Generic `Dictionary<string, object>` Descriptor

Rejected. It would move type errors to runtime and make validation vague. Descriptor models should be explicit even if some fields remain planned-only.

## Non-Goals

- No C# compiler integration.
- No external assembly loading.
- No `AssemblyLoadContext`.
- No hot reload.
- No runtime XAML loading into Shell.
- No plugin-created Avalonia `Control`, `UserControl` or `Window`.
- No script VM.
- No native bridge, C++ ABI, Vulkan viewport, renderer FPS control or swapchain ownership.
- No scene or asset mutation.
- No shell command line.

## Validation

Design-only validation:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

Future implementation validation:

```powershell
dotnet test apps\studio\Editor.sln -c Release
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
rg -n "AssemblyLoadContext|NativeEditorBridge|C\\+\\+ ABI|script VM|ScriptExecutionHost|PluginLoader|LoadFromAssembly|AvaloniaRuntimeXamlLoader|RuntimeXamlLoader|VkDevice|Vulkan|Swapchain" apps/studio/Core apps/studio/Shell apps/studio/Features apps/studio/Tests
```

## Acceptance Criteria

- The spec keeps project `Editor/` support editor-framework-only.
- v0 descriptor models are data-only and validate before commit.
- Raw Avalonia Control/UserControl/Window and runtime XAML remain deferred.
- Descriptor source adapters are separated from `EditorExtensionHost`.
- Built-in modules can later be adapted without changing current panel/action ids.
- Future implementation can be split into PR-sized tasks with TDD.

## References

- Unity custom Editor window with UI Toolkit: https://docs.unity3d.com/6000.0/Documentation/Manual/UIE-HowTo-CreateEditorWindow.html
- Godot `EditorPlugin`: https://docs.godotengine.org/en/stable/classes/class_editorplugin.html
- Avalonia `AvaloniaRuntimeXamlLoader`: https://api-docs.avaloniaui.net/docs/T_Avalonia_Markup_Xaml_AvaloniaRuntimeXamlLoader
- Microsoft XAML Hot Reload: https://learn.microsoft.com/en-us/visualstudio/xaml-tools/xaml-hot-reload
- Asharia Studio extension lifecycle design: 2026-06-23-studio-extension-lifecycle-v0-design.md
- Current replacement: ../../architecture/studio-extension-model.md and ../../adr/0004-unified-editor-extension-framework.md
