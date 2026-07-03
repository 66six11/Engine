# Studio Panel Instance Lifecycle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Slice 3 of the Studio extension lifecycle design by moving panel content creation and deterministic close/reset/shutdown release into a dedicated `PanelInstanceManager`.

**Architecture:** `EditorExtensionHost` continues to own contribution registration only. `PanelInstanceManager` owns panel content instances created from `PanelDescriptor`, applies `KeepAlive` and `RecreateOnOpen` cache policy, and gives each `EditorDockTabViewModel` a release handle so moving tabs across dock/floating workspaces does not imply disposal. Logical activation callbacks are planned as a follow-up step after creation/close ownership is separated.

**Tech Stack:** .NET 10, C#, xUnit, existing `PanelDescriptor`, `EditorDockWorkspaceViewModel`, `EditorDockTabViewModel`, `HierarchyPanelViewModel`.

---

## Source Spec

- `docs/superpowers/specs/2026-06-23-studio-extension-lifecycle-v0-design.md`
- Slice 3: Panel Instance Lifecycle.

## External Reference Notes

- VS Code custom editors/webviews fire `WebviewPanel.onDidDispose` when an editor is closed and extensions clean up associated resources.
- JetBrains Platform `Disposer` manages parent/child `Disposable` lifetimes and releases heavyweight resources at the owning lifetime boundary.
- Unity `EditorWindow` separates `OnEnable`, `CreateGUI`, repeated `Update`/`OnGUI`, and `OnDisable` cleanup.
- Avalonia visual/logical trees are presentation structures; Studio panel attach/detach remains a logical dock-host lifecycle, not a direct wrapper over visual-tree attach/detach events.

## Scope

In:

- Move `PanelDescriptor.CreateContent()` calls out of `EditorDockWorkspaceViewModel`.
- Keep `KeepAlive` close behavior as detach-only.
- Dispose `RecreateOnOpen` content on close/reset.
- Dispose kept-alive content on workspace/session shutdown.
- Dispose floating workspace panel content when its host window closes.
- Prove `HierarchyPanelViewModel` unsubscribes when disposed through the panel instance path.

Out:

- Provider contribution lifecycle.
- Native bridge or rendering.
- External plugin loading, ALC, hot reload, script VM.
- Runtime enable/disable UI.
- Full logical Activated/Deactivated callbacks. This remains planned after the content owner is isolated.

## Task 1: Panel Instance Manager

**Files:**
- Create: `Shell/Docking/PanelInstanceManager.cs`
- Modify: `Shell/ViewModels/EditorDockTabViewModel.cs`
- Modify: `Tests/Editor.Tests/Shell/Docking/PanelInstanceManagerTests.cs`

- [x] **Step 1: Write failing tests**

Create tests covering:

```csharp
[Fact]
public void ReleaseTab_keeps_keep_alive_content_until_manager_disposal()
{
    var manager = new PanelInstanceManager();
    var disposable = new RecordingDisposable();
    var descriptor = CreateDescriptor("panel", DockContentCachePolicy.KeepAlive, () => disposable);

    var tab = manager.CreateTab(descriptor);
    tab.ReleasePanelInstance();

    Assert.False(disposable.IsDisposed);

    manager.Dispose();

    Assert.True(disposable.IsDisposed);
}
```

```csharp
[Fact]
public void ReleaseTab_disposes_recreate_on_open_content_on_close()
{
    var manager = new PanelInstanceManager();
    var disposable = new RecordingDisposable();
    var descriptor = CreateDescriptor("panel", DockContentCachePolicy.RecreateOnOpen, () => disposable);

    var tab = manager.CreateTab(descriptor);
    tab.ReleasePanelInstance();

    Assert.True(disposable.IsDisposed);
}
```

```csharp
[Fact]
public void CreateTab_reuses_keep_alive_content_after_release()
{
    var manager = new PanelInstanceManager();
    var contentFactory = new CountingContentFactory();
    var descriptor = CreateDescriptor("panel", DockContentCachePolicy.KeepAlive, contentFactory.Create);

    var first = manager.CreateTab(descriptor);
    first.ReleasePanelInstance();
    var second = manager.CreateTab(descriptor);

    Assert.Same(first.Content, second.Content);
    Assert.Equal(1, contentFactory.CreateCount);
}
```

- [x] **Step 2: Verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~PanelInstanceManagerTests"
```

Expected: compile failure because `PanelInstanceManager` and `EditorDockTabViewModel.ReleasePanelInstance()` do not exist.

- [x] **Step 3: Implement manager and tab release handle**

Create `PanelInstanceManager` with:

```csharp
internal sealed class PanelInstanceManager : IDisposable
{
    public EditorDockTabViewModel CreateTab(PanelDescriptor descriptor);

    public void Dispose();
}
```

The manager creates `EditorDockTabViewModel` instances and passes an internal release handle to the tab. `ReleasePanelInstance()` must be idempotent. Content disposal should call `IDisposable.Dispose()` when the content implements `IDisposable`; async panel disposal remains out of scope for this slice.

- [x] **Step 4: Verify GREEN**

Run the same focused test command. Expected: pass.

## Task 2: Workspace Integration

**Files:**
- Modify: `Shell/ViewModels/EditorDockWorkspaceViewModel.cs`
- Modify: `Shell/ViewModels/MainWindowViewModel.cs`
- Modify: `Shell/ViewModels/EditorDockFloatingWindowViewModel.cs`
- Modify: `Shell/Composition/StudioCompositionSession.cs`
- Modify: `Shell/Views/EditorDockFloatingWindow.axaml.cs`
- Modify: `Tests/Editor.Tests/Shell/ViewModels/EditorDockWorkspaceViewModelTests.cs`
- Modify: `Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`
- Modify: `Tests/Editor.Tests/Shell/Views/EditorLifecycleViewHookTests.cs`
- Modify: `Tests/Editor.Tests/Features/Hierarchy/HierarchyPanelViewModelTests.cs`

- [x] **Step 1: Write failing workspace tests**

Add tests proving:

- Closing a `RecreateOnOpen` tab disposes content immediately.
- Closing a `KeepAlive` tab does not dispose content until workspace/session disposal.
- `ResetLayout()` releases old `RecreateOnOpen` tab content before recreating default tabs.
- Disposing `MainWindowViewModel`/session disposes kept-alive panel content.
- Disposing a floating window view model releases its floating workspace panel instances.

- [x] **Step 2: Verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorDockWorkspaceViewModelTests|FullyQualifiedName~StudioCompositionRootTests"
```

Expected: failures showing close/reset/shutdown do not release panel content yet.

- [x] **Step 3: Route workspace through `PanelInstanceManager`**

Replace workspace-local content cache with a `PanelInstanceManager`. `CloseTab()` calls `tab.ReleasePanelInstance()` only for real close paths. Drag/drop/reorder paths keep moving the same tab object and must not release. `ResetWorkspaceWindows()` releases tabs before clearing them.

- [x] **Step 4: Wire shutdown disposal**

Make `EditorDockWorkspaceViewModel` disposable and have `MainWindowViewModel.Dispose()` dispose its workspace. `StudioCompositionSession.DisposeAsync()` disposes the main view model before disposing the extension host.

Floating window hosts dispose their `EditorDockFloatingWindowViewModel` on close, and the main view model closes floating windows before disposing the main workspace so floating tabs release their handles before the root panel manager shuts down.

- [x] **Step 5: Verify GREEN**

Run the same focused command. Expected: pass.

## Task 3: Documentation And Verification

**Files:**
- Modify: `docs/Dock系统指南.md`
- Modify: `docs/编辑器UI平台规范.md`

- [x] **Step 1: Record current boundary**

Document that `PanelInstanceManager v0` owns built-in panel content creation and deterministic close/reset/shutdown disposal only; logical Activated/Deactivated callbacks and provider lifecycle remain planned/deferred.

- [x] **Step 2: Run focused and full verification**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~PanelInstanceManagerTests|FullyQualifiedName~EditorDockWorkspaceViewModelTests|FullyQualifiedName~StudioCompositionRootTests|FullyQualifiedName~HierarchyPanelViewModelTests"
dotnet test Editor.sln -c Release
```

From repository root:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
```

From `apps/studio`:

```powershell
git diff --check
rg -n "AssemblyLoadContext|NativeEditorBridge|C\+\+ ABI|script VM|ScriptExecutionHost|PluginLoader|LoadFromAssembly" Core Shell Features Tests
```

Expected: tests pass; encoding has 0 issues; diff check is clean; non-goal search has no implementation matches.

- [x] **Step 3: Commit**

```powershell
git add Shell\Docking\PanelInstanceManager.cs Shell\ViewModels\EditorDockTabViewModel.cs Shell\ViewModels\EditorDockWorkspaceViewModel.cs Shell\ViewModels\MainWindowViewModel.cs Shell\Composition\StudioCompositionSession.cs Tests\Editor.Tests\Shell\Docking\PanelInstanceManagerTests.cs Tests\Editor.Tests\Shell\ViewModels\EditorDockWorkspaceViewModelTests.cs Tests\Editor.Tests\Features\Hierarchy\HierarchyPanelViewModelTests.cs docs\Dock系统指南.md docs\编辑器UI平台规范.md docs\superpowers\plans\2026-06-23-studio-panel-instance-lifecycle.md
git commit -m "feat: manage studio panel instance lifetime"
```
