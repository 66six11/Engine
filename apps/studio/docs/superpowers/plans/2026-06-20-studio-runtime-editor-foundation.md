# Studio Runtime Editor Foundation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the next stable Studio foundation for runtime-editor work without prematurely implementing a full scene editor, native ABI, or plugin hot reload system.

**Architecture:** Keep Studio as an Avalonia presentation host around command, panel, selection, dialog, feedback, and read-only scene snapshot contracts. Runtime/editor mutation remains behind future transaction and scene-world boundaries; C++ owns engine truth, renderer/RHI lifetime, and any future native viewport. Managed plugins and hot reload stay deferred until the UI command/feedback surface, scene snapshot bridge, transaction path, Play Session boundary, diagnostics, and unload/ABI rules have evidence.

**Tech Stack:** .NET 10, Avalonia 12.0.4, CommunityToolkit.Mvvm, compiled bindings, existing Studio Core/Shell/Features/UI layout, xUnit tests, engine docs under `docs/systems` and `docs/architecture`.

---

## Research Inputs

External references checked:

- Unity Domain Reloading and script serialization/hot reload: Play mode and hot reload need explicit state reset/restore rules.
- Unreal Editor Modules, Plugins, and Play In Editor: editor-only module loading and in-editor play require mode/world boundaries.
- Godot EditorPlugin and `@tool`: editor-running code is powerful but can destabilize editor state if lifecycle and ownership are loose.
- O3DE reflection contexts: serialization, edit, and behavior/script metadata are separate projections.
- .NET AssemblyLoadContext unloadability: unload is cooperative and needs negative leak tests before plugin reload is trusted.
- Avalonia threading: UI objects and bound collections must be touched through the UI thread.

Local constraints used:

- `apps/studio/docs/Dock系统指南.md`: Feature modules register `PanelDescriptor` and `WorkbenchActionDescriptor`; they do not directly own Dock controls.
- `apps/studio/docs/项目规范.md`: Studio should keep Core/Shell/UI/Features boundaries and must not move the engine world into the Avalonia control tree.
- `docs/systems/scene-world.md`: Edit World and Play World must be separate; editor mutation goes through transactions; renderer consumes snapshots/draw packets.
- `docs/systems/reflection-serialization.md`: schema is the fact source; editor/script/asset/scene consume typed metadata projections.
- `docs/architecture/managed-extension-model.md`: managed/plugin/ABI work is contract-only until scene, transaction, asset, Play Session, diagnostics, and ABI gates are stable.

## Scope Decision

Accepted for the next stage:

- Strengthen generic editor UI infrastructure that all later runtime-editor work needs.
- Add background activity/progress feedback so long-running bridge, load, import, and validation commands have a shared visible surface.
- Keep command/menu/shortcut/dialog systems as the central route for user actions.
- Prepare a read-only scene snapshot bridge path before any writable scene editing.
- Document transaction and Play Session gates before implementation.

Deferred:

- Full Scene authoring.
- Native C ABI.
- Avalonia-hosted Vulkan viewport.
- Runtime gameplay ScriptHost.
- Managed plugin hot reload.
- User/plugin-created raw Avalonia windows.
- Editable Inspector backed by C++ data before transaction and schema metadata are ready.

## File Structure Plan

- `apps/studio/Core/Models/EditorBackgroundTask*.cs` - UI-neutral task state records.
- `apps/studio/Core/Abstractions/IEditorBackgroundTaskService.cs` - UI-neutral service contract.
- `apps/studio/Shell/Services/EditorBackgroundTaskService.cs` - in-process implementation for current Studio shell.
- `apps/studio/Shell/ViewModels/MainWindowViewModel.cs` - exposes status/active task summary to chrome.
- `apps/studio/UI/Controls/Feedback/*` - reusable compact feedback controls after the service contract exists.
- `apps/studio/Features/Problems/*` and `apps/studio/Features/Console/*` - future consumers for task diagnostics; do not create engine runtime dependencies here.
- `apps/studio/Core/Abstractions/ISceneSnapshotProvider.cs` - remains the read-only scene data seam for Hierarchy and Inspector.
- `apps/studio/docs/superpowers/specs/2026-06-20-studio-runtime-editor-foundation-design.md` - design record for this staged plan.
- `apps/studio/docs/Dock系统指南.md` - update only when a slice changes registered panels/actions or data-flow facts.

## Task 1: Runtime Editor Foundation Design Record

**Files:**

- Create: `apps/studio/docs/superpowers/specs/2026-06-20-studio-runtime-editor-foundation-design.md`

- [ ] **Step 1: Write the design record**

Create a design record with these exact sections:

```markdown
# Studio Runtime Editor Foundation Design

## Intent

Build the shared Studio UI and data-flow foundation needed for runtime-editor features while deferring full Scene authoring, Play Mode, native ABI, and plugin hot reload.

## Decisions

- Studio remains an Avalonia presentation host, not the owner of engine world or renderer lifetime.
- Feature modules contribute descriptors and view models; Shell owns workbench orchestration.
- Background work, diagnostics, dialogs, commands, shortcuts, and status feedback are shared UI infrastructure.
- Hierarchy and Inspector continue through `ISceneSnapshotProvider` until a real scene bridge exists.
- The first real scene bridge is read-only.
- Writable Inspector fields require transaction, dirty-state, validation, and schema/editor metadata.
- Play Session starts only after Edit World / Play World copy or load semantics are testable.
- Managed plugin reload starts only after ALC unload smokes and contribution lifecycle diagnostics exist.

## Non-Goals

- No native C ABI in this stage.
- No Avalonia-owned Vulkan viewport.
- No raw plugin-created Avalonia controls.
- No script VM or runtime gameplay ScriptHost.
- No direct mutable C++ object pointer in Studio view models.

## Entry Gates

- Background activity service exists and is visible in the Studio shell.
- Command execution can report success, failure, disabled state, and long-running progress.
- Problems and Console can receive structured diagnostics from UI-level operations.
- Hierarchy and Inspector consume the same read-only snapshot provider.
- Transaction and Play Session follow-up slices have explicit smoke evidence.

## References

- Unity Domain Reloading: https://docs.unity3d.com/6000.0/Documentation/Manual/domain-reloading.html
- Unity Script Serialization: https://docs.unity3d.com/2022.3/Documentation/Manual/script-Serialization.html
- Unreal Editor Modules: https://dev.epicgames.com/documentation/unreal-engine/setting-up-editor-modules-for-customizing-the-editor-in-unreal-engine
- Unreal Play In Editor: https://dev.epicgames.com/documentation/unreal-engine/ineditor-testing-play-and-simulate-in-unreal-engine
- Godot `@tool`: https://docs.godotengine.org/en/stable/tutorials/plugins/running_code_in_the_editor.html
- O3DE Reflection Contexts: https://www.docs.o3de.org/docs/user-guide/programming/components/reflection/
- .NET AssemblyLoadContext unloadability: https://learn.microsoft.com/en-us/dotnet/standard/assembly/unloadability
- Avalonia Threading: https://docs.avaloniaui.net/docs/app-development/threading
```

- [ ] **Step 2: Run document hygiene**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

Expected: both commands pass. The new Markdown file is UTF-8 without BOM.

- [ ] **Step 3: Commit**

Run:

```powershell
git add apps/studio/docs/superpowers/specs/2026-06-20-studio-runtime-editor-foundation-design.md
git commit -m "docs: plan studio runtime editor foundation"
```

## Task 2: Background Activity Service Contract

**Files:**

- Create: `apps/studio/Core/Models/EditorBackgroundTaskId.cs`
- Create: `apps/studio/Core/Models/EditorBackgroundTaskState.cs`
- Create: `apps/studio/Core/Models/EditorBackgroundTaskSnapshot.cs`
- Create: `apps/studio/Core/Abstractions/IEditorBackgroundTaskService.cs`
- Create: `apps/studio/Shell/Services/EditorBackgroundTaskService.cs`
- Create: `apps/studio/Tests/Editor.Tests/Shell/Services/EditorBackgroundTaskServiceTests.cs`

- [ ] **Step 1: Write failing service tests**

Add tests for:

```csharp
[Fact]
public void StartTaskPublishesRunningSnapshot()
{
    var service = new EditorBackgroundTaskService();
    var id = service.Start("scene.snapshot.load", "Loading Scene", canCancel: false);

    var snapshot = service.GetSnapshot(id);

    Assert.Equal(EditorBackgroundTaskState.Running, snapshot.State);
    Assert.Equal("Loading Scene", snapshot.Title);
}

[Fact]
public void CompleteTaskPublishesCompletedSnapshot()
{
    var service = new EditorBackgroundTaskService();
    var id = service.Start("scene.snapshot.load", "Loading Scene", canCancel: false);

    service.Complete(id, "Loaded");

    Assert.Equal(EditorBackgroundTaskState.Completed, service.GetSnapshot(id).State);
    Assert.Equal("Loaded", service.GetSnapshot(id).Message);
}
```

- [ ] **Step 2: Run the failing tests**

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorBackgroundTaskServiceTests"
```

Expected: FAIL because the service and models do not exist.

- [ ] **Step 3: Implement the minimal UI-neutral contract**

Use these contracts:

```csharp
public readonly record struct EditorBackgroundTaskId(Guid Value)
{
    public static EditorBackgroundTaskId NewId() => new(Guid.NewGuid());
}

public enum EditorBackgroundTaskState
{
    Running,
    Completed,
    Failed,
    Canceled
}

public sealed record EditorBackgroundTaskSnapshot(
    EditorBackgroundTaskId Id,
    string OperationId,
    string Title,
    EditorBackgroundTaskState State,
    double? Progress,
    string? Message,
    bool CanCancel);

public interface IEditorBackgroundTaskService
{
    EditorBackgroundTaskId Start(string operationId, string title, bool canCancel);
    void Report(EditorBackgroundTaskId id, double? progress, string? message);
    void Complete(EditorBackgroundTaskId id, string? message);
    void Fail(EditorBackgroundTaskId id, string message);
    void Cancel(EditorBackgroundTaskId id, string? message);
    EditorBackgroundTaskSnapshot GetSnapshot(EditorBackgroundTaskId id);
    IReadOnlyList<EditorBackgroundTaskSnapshot> GetActiveSnapshots();
}
```

- [ ] **Step 4: Run focused tests**

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorBackgroundTaskServiceTests"
```

Expected: PASS.

- [ ] **Step 5: Commit**

Run:

```powershell
git add apps/studio/Core/Models/EditorBackgroundTask*.cs apps/studio/Core/Abstractions/IEditorBackgroundTaskService.cs apps/studio/Shell/Services/EditorBackgroundTaskService.cs apps/studio/Tests/Editor.Tests/Shell/Services/EditorBackgroundTaskServiceTests.cs
git commit -m "feat: add studio background task service"
```

## Task 3: Shell Status Feedback Surface

**Files:**

- Create: `apps/studio/UI/Controls/Feedback/ActivityIndicator.axaml`
- Create: `apps/studio/UI/Controls/Feedback/ActivityIndicator.axaml.cs`
- Modify: `apps/studio/Shell/ViewModels/MainWindowViewModel.cs`
- Modify: `apps/studio/Shell/Views/MainWindow.axaml`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`

- [ ] **Step 1: Write failing view-model tests**

Add a test that starts a background task through the service and verifies `MainWindowViewModel` exposes a compact summary:

```csharp
[Fact]
public void ActiveBackgroundTaskSummaryShowsRunningTask()
{
    var tasks = new EditorBackgroundTaskService();
    tasks.Start("project.open", "Opening Project", canCancel: false);

    var viewModel = MainWindowViewModelTestFactory.Create(backgroundTasks: tasks);

    Assert.True(viewModel.HasActiveBackgroundTasks);
    Assert.Equal("Opening Project", viewModel.ActiveBackgroundTaskTitle);
}
```

- [ ] **Step 2: Run the failing tests**

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~MainWindowViewModelTests"
```

Expected: FAIL because the view model does not expose background task summary state.

- [ ] **Step 3: Add summary properties and compact view**

Expose these read-only properties from the main window view model:

```csharp
public bool HasActiveBackgroundTasks { get; }
public string ActiveBackgroundTaskTitle { get; }
public string ActiveBackgroundTaskMessage { get; }
```

Render them in the status/chrome area with `ActivityIndicator`. Keep the control reusable and UI-only. Do not let it know about engine scene, asset, or runtime concepts.

- [ ] **Step 4: Run focused UI tests**

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~MainWindowViewModelTests|FullyQualifiedName~EditorDialogHostViewTests"
```

Expected: PASS.

- [ ] **Step 5: Commit**

Run:

```powershell
git add apps/studio/UI/Controls/Feedback apps/studio/Shell/ViewModels/MainWindowViewModel.cs apps/studio/Shell/Views/MainWindow.axaml apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs
git commit -m "feat: show studio background activity feedback"
```

## Task 4: Read-Only Scene Snapshot Bridge Gate

**Files:**

- Modify: `apps/studio/Core/Abstractions/ISceneSnapshotProvider.cs`
- Modify: `apps/studio/Core/Services/InMemorySceneSnapshotProvider.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Core/SceneSnapshotProviderTests.cs`
- Modify: `apps/studio/docs/Dock系统指南.md`

- [ ] **Step 1: Add tests for provider refresh and diagnostics**

Add tests that describe the next bridge behavior without adding native ABI:

```csharp
[Fact]
public void ProviderCanPublishReplacementSnapshot()
{
    var provider = new InMemorySceneSnapshotProvider(SceneSnapshot.Empty);
    var next = SceneSnapshot.Empty with { DisplayName = "Runtime Snapshot" };

    provider.ReplaceSnapshot(next);

    Assert.Equal("Runtime Snapshot", provider.GetCurrentSnapshot().DisplayName);
}
```

- [ ] **Step 2: Run the failing tests**

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~SceneSnapshotProviderTests|FullyQualifiedName~HierarchyPanelViewModelTests|FullyQualifiedName~InspectorPanelViewModelTests"
```

Expected: FAIL if the provider cannot replace snapshots or notify consumers.

- [ ] **Step 3: Implement the smallest replacement-notification seam**

Add provider replacement and a snapshot-changed event. Keep the contract read-only:

```csharp
public interface ISceneSnapshotProvider
{
    event EventHandler? SnapshotChanged;
    SceneSnapshot GetCurrentSnapshot();
}
```

`InMemorySceneSnapshotProvider` may expose `ReplaceSnapshot(SceneSnapshot snapshot)` as an implementation helper for tests and future bridge adapters.

- [ ] **Step 4: Update Dock system facts**

Update `apps/studio/docs/Dock系统指南.md` to say the provider is still read-only and can now publish replacement snapshots, but does not write Transform data or query a native runtime scene.

- [ ] **Step 5: Validate and commit**

Run:

```powershell
dotnet test apps\studio\Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~SceneSnapshotProviderTests|FullyQualifiedName~HierarchyPanelViewModelTests|FullyQualifiedName~InspectorPanelViewModelTests"
dotnet test apps\studio\Editor.sln -c Release
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

Expected: all pass.

Commit:

```powershell
git add apps/studio/Core/Abstractions/ISceneSnapshotProvider.cs apps/studio/Core/Services/InMemorySceneSnapshotProvider.cs apps/studio/Tests/Editor.Tests/Core/SceneSnapshotProviderTests.cs apps/studio/docs/Dock系统指南.md
git commit -m "feat: add read-only scene snapshot refresh seam"
```

## Task 5: Transaction-Backed Edit Design Gate

**Files:**

- Create: `apps/studio/docs/superpowers/specs/2026-06-20-studio-transaction-edit-gate-design.md`

- [ ] **Step 1: Write the gate document**

Include these acceptance gates before any writable Inspector field:

```markdown
# Studio Transaction Edit Gate Design

## Required Before Writable Inspector

- Stable object identity in scene snapshots.
- Schema/editor metadata for each editable field.
- Command object captures target id, field id, old value, new value, validation result, merge policy, and display label.
- Transaction service supports begin, commit, rollback, undo, redo, dirty-state publication, and diagnostics.
- Inspector edit never mutates runtime data directly.

## First Writable Slice

Only Transform position is writable. Scale, rotation, material, hierarchy mutation, prefab, and multi-object mixed edits are deferred.

## Validation

- Select object.
- Edit Transform position.
- Dirty state becomes true.
- Undo restores previous value.
- Redo restores new value.
- Save/reload keeps the new value only after commit.
```

- [ ] **Step 2: Run document hygiene**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

Expected: PASS.

- [ ] **Step 3: Commit**

Run:

```powershell
git add apps/studio/docs/superpowers/specs/2026-06-20-studio-transaction-edit-gate-design.md
git commit -m "docs: define studio transaction edit gate"
```

## Task 6: Play Session And Managed Extension Deferral Gate

**Files:**

- Create: `apps/studio/docs/superpowers/specs/2026-06-20-studio-play-extension-gates-design.md`

- [ ] **Step 1: Write the gate document**

Capture these rules:

```markdown
# Studio Play Session And Extension Gates Design

## Play Session Entry Conditions

- Edit World and Play World identity/remap rules are documented.
- Enter Play creates a copy/load of the edit scene.
- Exiting Play does not dirty the edit scene.
- Applying changes back to Edit World is explicit and transaction-backed.
- Scene View selection remains edit-world selection.
- Game View debug selection uses an explicit remap.

## Managed Plugin Entry Conditions

- Background activity and diagnostics surfaces exist.
- Command execution can route failures and long-running work.
- Contribution ids are stable and diffable.
- ALC clean unload and negative unload smoke designs exist.
- Reload failure keeps previous valid contribution state or disables with diagnostics.
- Native bridge is not introduced until a CPU-only bridge consumer exists.

## Deferrals

- No gameplay ScriptHost under `Asharia.Studio.*`.
- No raw plugin-created Avalonia controls in v0.
- No plugin-owned C++ pointers.
- No native Vulkan viewport in the managed bridge stage.
```

- [ ] **Step 2: Run document hygiene**

Run:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

Expected: PASS.

- [ ] **Step 3: Commit**

Run:

```powershell
git add apps/studio/docs/superpowers/specs/2026-06-20-studio-play-extension-gates-design.md
git commit -m "docs: define studio play and extension gates"
```

## Final Validation For This Plan

Run after the selected task batch:

```powershell
dotnet test apps\studio\Editor.sln -c Release
dotnet test apps\studio\Editor.sln
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

For this plan, no CMake/Vulkan pre-commit gate is required unless a task touches engine C++ packages or root architecture docs outside Studio. If a future task creates or edits GitHub Issues, PR links, labels, dependencies, or Project fields, first read `docs/planning/project-management.md` and run the required Project sync/audit path.

## Recommended Execution Order

1. Task 1: design record.
2. Task 2: background activity service.
3. Task 3: shell feedback surface.
4. Task 4: read-only scene snapshot bridge seam.
5. Task 5: transaction edit gate.
6. Task 6: Play Session and managed extension gates.

The first PR-sized implementation after this plan should be Task 2 plus Task 3 if the working branch is clean. Task 4 should not start until the current UI feedback path can report loading and diagnostics.

## Self-Review

- Spec coverage: covers user concerns about whether Scene is premature, whether C++ ABI is needed now, how to proceed with editor UI, and how runtime-editor work should be gated.
- Placeholder scan: no TBD/TODO placeholders are used as implementation instructions.
- Type consistency: background task model names and service method names are consistent across tests, snippets, and file list.
