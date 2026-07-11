# Studio Extension Lifecycle v0 Design

## Intent

建立 Studio 编辑器框架的统一扩展生命周期内核，让当前内置 Feature、未来项目 `Editor/` 扩展、packaged plugin 和 native bridge adapter 都从同一 contribution 边界接入。

这不是 C++ ABI、managed plugin reload 或脚本 VM 设计。当前阶段只解决编辑器框架层的真实缺口：模块所有权、单次声明、原子注册、失败回滚、激活顺序和释放顺序。Panel、Command 和 Provider 的实际运行仍由各自领域执行器负责。

## Current Context

当前代码已经有半套扩展框架：

- `Core/Abstractions/IEditorFeatureModule.cs` 提供 `RegisterPanels()` 和 `RegisterActions()`。
- `Shell/Composition/EditorFeatureCatalog.cs` 创建默认 feature module 列表。
- `Shell/Docking/PanelRegistry.cs` 保存 `PanelDescriptor`。
- `Shell/Commands/WorkbenchActionRegistry.cs` 保存 `WorkbenchActionDescriptor`。
- `Features/Workbench/WorkbenchFeatureModule.cs` 同时声明 Scene View、Hierarchy、Inspector、Console、Problems 面板和对应 Window command。

但当前所有权和生命周期不完整：

- `MainWindowViewModel.CreatePanelRegistry()` 与 `CreateWorkbenchActionRegistry()` 分别调用 `EditorFeatureCatalog.CreateDefaultModules()`，同一启动过程会创建两批 `WorkbenchFeatureModule`。
- `EditorDockWorkspaceViewModel` 直接调用 `PanelDescriptor.CreateContent()`，并以裸 `object` 缓存 panel content。
- Dock tab 激活目前主要切换 `IsActive`；关闭、重置和 host shutdown 没有统一 panel instance 释放协议。
- `HierarchyPanelViewModel` 已实现 `IDisposable`，因为它订阅 `ISceneSnapshotProvider.SnapshotChanged` 并需要取消订阅。
- `IEditorLifecycleEventService` 只记录 Shell window recent events，不是 Feature、Provider 或 Plugin 生命周期管理器。

因此统一生命周期不是为未来 plugin 过早抽象，而是当前内置 Feature 已经需要明确 owner 和释放路径。

## External References

成熟编辑器和扩展系统的共同模式是：声明、激活、执行和清理分开。

- VS Code contribution points 是 manifest 中的 JSON 声明；command contribution 描述 UI 元数据，真正 command 调用会触发 activation event。`activate()` 只调用一次，`deactivate()` 用于清理。
- Unreal module 通过 startup/shutdown 注册能力，tab spawner 注册和 tab 实例创建分离。
- Godot `EditorPlugin` 使用 `_enter_tree()` / `_exit_tree()` 成对初始化和清理，custom dock 示例在退出时 remove dock 并释放 control。
- Avalonia 要求所有 UI control 访问发生在 UI 线程；extension host 不能让 provider 或后台线程直接改 Avalonia 对象。
- .NET `AssemblyLoadContext` 只提供加载隔离，不是安全边界；卸载是 cooperative。因此 ALC hot reload 不能成为 v0 承诺。

这些案例给 Studio 的结论是：Host 应拥有 contribution 生命周期，但不应直接成为 Dock、Command、Provider、Native bridge 和 UI 线程语义的 God Object。

## Decision

采用“统一生命周期内核 + 各领域执行器”。

```text
ExtensionSource
    -> EditorExtensionHost
        -> EditorContributionRegistry
            -> PanelInstanceManager
            -> WorkbenchCommandRouter / Executor
            -> EditorProviderHost
        -> Structured Diagnostics
            -> Lifecycle recent journal
            -> Latest status message
            -> Console
```

职责划分：

| Component | Owns | Does not own |
| --- | --- | --- |
| `EditorExtensionHost` | 模块单次创建、声明收集、验证、原子注册、激活、失败回滚、逆序释放 | 打开 Dock tab、执行 command、连接 native handle、创建 Avalonia Control |
| `EditorContributionRegistry` | descriptor、owner id、enabled state、registration handle | descriptor 对应实例的运行 |
| `PanelInstanceManager` | panel instance 创建、Attach/Activate/Deactivate/Detach/Dispose、cache policy | contribution 验证、命令执行、native bridge |
| `WorkbenchCommandRouter` / executor | command lookup、执行、result、diagnostics | module activation、panel instance lifetime |
| `EditorProviderHost` | provider factory、start/stop/dispose、health state | scene data contract 本身、native physical owner |
| Lifecycle / Diagnostics / Status | 观察和投影 | 生命周期控制 |

`IEditorLifecycleEventService` 保持为观测日志。它可以记录 module/contribution/provider 事件的摘要，但不拥有状态机，也不驱动注册或释放。

## Scope

v0 只面向可信内置模块和来源无关合同：

```text
BuiltInExtensionSource
    -> IEditorExtensionModule
    -> EditorExtensionHost
```

未来来源使用同一 host-facing 合同，但 Deferred：

```text
ProjectEditorExtensionSource
PackagedPluginExtensionSource
```

Native bridge 通过内置 adapter module 接入，但 native bridge 本身不是普通 plugin：

```text
NativeSceneProviderExtension
    -> contributes scene.active provider
        -> NativeEditorBridge
            -> C++ engine owner
```

Host 只拥有 managed adapter/provider 生命周期。C++ engine、ABI connection、native threads 和 native handles 仍由 native bridge owner 管理。

## Lifecycle States

### Module

```text
Created
  -> Declaring
  -> Registered
  -> Activating
  -> Active
  -> Deactivating
  -> Disposed

Any transitional stage -> Faulted
```

`Initialized` 不作为状态名，因为它容易同时表示构造完成、声明完成、注册完成或副作用已启动。v0 明确拆分纯声明阶段和有副作用的激活阶段。

### Contribution Descriptor

```text
Proposed
  -> Validated
  -> Registered
  <-> Disabled
  -> Removed
```

`Materialized` 和 `Invoked` 不属于 descriptor 状态。同一个 panel contribution 可以多次创建实例，同一个 command contribution 可以多次执行。

### Panel Instance

```text
Created
  -> Attached
  -> Activated
  <-> Deactivated
  -> Detached
  -> Disposed
```

`Attached` / `Detached` 表示逻辑 panel host 生命周期，不直接绑定 Avalonia visual tree attached event。拖拽、浮动窗口和模板重建不能造成重复订阅。

Cache policy 语义：

```text
KeepAlive:
Close -> Deactivate -> Detach
Host shutdown / extension disable -> Dispose

RecreateOnOpen:
Close -> Deactivate -> Detach -> Dispose
Reopen -> create new instance
```

### Command Invocation

```text
Requested -> Running -> Succeeded | Failed | Cancelled
```

Command invocation 由 command router/executor 管理。Host 只注册 command contribution 和 owner，不直接调用 handler。

### Provider

```text
Created
  -> Starting/Connecting
  -> Ready
  <-> Degraded/Faulted
  -> Stopping
  -> Stopped
  -> Disposed

Ready -- SnapshotPublished(version) --> Ready
```

`SnapshotPublished` 是可重复事件，不是 provider 持久状态。

## v0 Contract Shape

推荐的最小模块合同：

```csharp
public interface IEditorExtensionModule
{
    EditorExtensionId Id { get; }

    // Declares immutable contributions only.
    // No provider connection, event subscription, file IO, native calls, or UI creation.
    void Declare(IEditorContributionBuilder builder);

    // Starts subscriptions, providers, or other side effects after registry commit.
    // The host owns the returned lease and disposes it in reverse activation order.
    ValueTask<IAsyncDisposable?> ActivateAsync(
        IEditorExtensionActivationContext context,
        CancellationToken cancellationToken);
}
```

Rules:

1. `Declare()` writes into a temporary builder, not global registries.
2. Host collects the complete contribution set before validation.
3. Validation succeeds before any contribution is committed.
4. Commit to typed registries is atomic from the Shell consumer point of view.
5. If `ActivateAsync()` fails, Host disposes the activation lease if one exists and rolls back all registrations for that module.
6. Host shutdown disposes activation leases in reverse activation order.
7. `IEditorExtensionActivationContext` exposes named, typed capabilities. It does not provide `GetService(Type)` or broad service locator access.

`EditorContributionRegistry` is a facade over typed registries:

```csharp
public sealed class EditorContributionRegistry
{
    public IPanelContributionRegistry Panels { get; }
    public IWorkbenchActionRegistry Actions { get; }
    public IProviderContributionRegistry Providers { get; }
}
```

It must not be implemented as `Dictionary<string, object>`.

## Existing Descriptor Treatment

### `PanelDescriptor`

`PanelDescriptor` can remain the built-in compatibility descriptor in v0. Do not freeze its current `Func<object>` as the future plugin ABI.

Long-term split:

```text
PanelContributionDescriptor
    id/title/menu/default area/cache policy

PanelFactoryReference
    built-in factory or future managed entrypoint ref

PanelInstance
    actual ViewModel lifetime

View/DataTemplate mapping
    Shell-owned presentation mapping
```

v0 allows built-in factories to return ViewModels. Future packaged plugins must not return raw Avalonia `Control` or `Window`.

### `WorkbenchActionDescriptor`

`WorkbenchActionDescriptor` can be registered as a workbench action contribution in v0, but it should not become the final generic command model.

Future split:

```text
CommandContribution
    CommandId + executor/entrypoint

WorkbenchActionContribution
    title + menu + shortcut + palette metadata
    -> references CommandId
```

This avoids expanding `WorkbenchActionKind` every time a new extension command type appears.

### `ISceneSnapshotProvider`

`ISceneSnapshotProvider` remains a read-only data contract:

```text
GetCurrentSnapshot()
SnapshotChanged
```

Do not add `Connect()`, `Disconnect()`, `Faulted`, or health state directly to this interface. Provider lifecycle belongs to `EditorProviderHost`.

Provider contribution shape:

```text
ProviderContribution
    role
    factory
    owner extension id

EditorProviderHost
    start/stop/dispose
    health state

ISceneSnapshotProvider
    read-only snapshot data
```

v0 conflict rule: a singleton role such as `scene.active` allows exactly one active provider. Duplicate default providers fail validation with both owner ids in the diagnostic.

## Migration Slices

### Slice 1: Host And Composition Root

- Create `EditorExtensionHost` in `App` or a new `StudioCompositionRoot`.
- `EditorFeatureCatalog` creates one module list per application composition.
- `WorkbenchFeatureModule` is instantiated once.
- Host calls `Declare()` once and commits panel/action contributions together.
- Preserve all existing panel/action ids.
- Tests cover single declaration, duplicate id validation, atomic rollback, and reverse disposal.

### Slice 2: Contribution Ownership

- Registry entries record `OwnerExtensionId`.
- Registration returns removal handles held only by Host.
- Conflict diagnostics include contribution id, existing owner, and new owner.
- Runtime enable/disable UI remains Deferred.

### Slice 3: Panel Instance Lifecycle

- Move `CreateContent()` responsibility from Dock workspace into `PanelInstanceManager`.
- Implement deterministic `KeepAlive` and `RecreateOnOpen` release behavior.
- Add logical Activated/Deactivated callbacks.
- Verify `HierarchyPanelViewModel` unsubscribes from `SnapshotChanged` on close or host disposal.

### Slice 4: Provider Contribution

- Register the current `InMemorySceneSnapshotProvider` as the active scene provider contribution.
- Hierarchy and Inspector consume the same active provider role.
- Add Ready/Faulted fake-provider tests.
- Future native adapter replaces the provider implementation without changing panel consumer contracts.

### Slice 5: Observability Projection

- Normal module/contribution events go to lifecycle recent journal.
- Activation or provider failures create structured diagnostics and Console-targeted records.
- The single most user-relevant item can publish a status/debug message snapshot when that status surface is available.
- Do not merge lifecycle, status, Console and Problems into one event bus.

## Alternatives

### Broaden `IEditorLifecycleEventService`

Rejected. It is currently a recent event journal for Shell window lifecycle. Turning it into a control bus would mix observation with ownership and make failure recovery hard to reason about.

### Let `EditorExtensionHost` Open Panels And Execute Commands

Rejected. It would quickly depend on Dock, Avalonia, command semantics, transactions, provider health and native bridge state. The Host should coordinate lifecycle ownership, while domain executors own runtime behavior.

### Wait For Plugin Hot Reload

Rejected. Current built-in Feature registration already has duplicate module construction and missing panel instance disposal semantics. The owner problem exists before ALC, project extensions or packaged plugins.

### Treat Native Bridge As A Normal Plugin

Rejected. The host-facing provider contribution can be uniform, but the physical native lifecycle has separate thread, handle, ABI, renderer and engine ownership rules.

## Non-Goals

- No external DLL loading.
- No `AssemblyLoadContext` implementation.
- No hot reload.
- No script VM.
- No C++ ABI.
- No native Vulkan viewport.
- No plugin-created raw Avalonia `Control`, `UserControl` or `Window`.
- No writable scene authoring or Inspector writeback.
- No shell command line.
- No runtime gameplay script framework.

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
```

Slice-specific evidence:

- Host tests prove modules are declared once.
- Registry tests prove duplicate ids fail before partial commit.
- Disposal tests prove leases are released in reverse activation order.
- Panel tests prove `KeepAlive` and `RecreateOnOpen` semantics.
- Provider tests prove duplicate singleton roles fail validation.
- Hierarchy tests prove snapshot subscriptions are removed on disposal.

## Acceptance Criteria

- The design keeps Studio editor-framework-only and does not connect C++ or native bridge implementation.
- `EditorExtensionHost` owns module lifecycle but not panel opening, command execution or provider runtime behavior.
- Existing `PanelDescriptor` and `WorkbenchActionDescriptor` remain compatible in v0.
- `IEditorLifecycleEventService` remains an observation surface.
- Native bridge adapter can later enter through provider contribution without making native bridge a generic plugin.
- Migration slices are PR-sized and can be planned independently.

## References

- VS Code Contribution Points: https://code.visualstudio.com/api/references/contribution-points
- VS Code Activation Events: https://code.visualstudio.com/api/references/activation-events
- Unreal `IModuleInterface::StartupModule`: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Core/IModuleInterface/StartupModule
- Unreal `FGlobalTabmanager::RegisterNomadTabSpawner`: https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Slate/FGlobalTabmanager/RegisterNomadTabSpawner
- Godot Making plugins: https://docs.godotengine.org/en/stable/tutorials/plugins/editor/making_plugins.html
- Avalonia Threading model: https://docs.avaloniaui.net/docs/app-development/threading
- .NET Assembly unloadability: https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability
- .NET `AssemblyLoadContext`: https://learn.microsoft.com/en-us/dotnet/api/system.runtime.loader.assemblyloadcontext
- Current replacement: ../../architecture/studio-extension-model.md and ../../adr/0004-unified-editor-extension-framework.md
