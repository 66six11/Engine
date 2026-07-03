# Studio Provider Contribution Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build Slice 4 of the Studio extension lifecycle design by registering the current read-only scene snapshot provider as an owned provider contribution and exposing it through an editor-framework provider host.

**Architecture:** `EditorExtensionHost` continues to collect declarations and atomically commit typed contributions. `EditorProviderHost` owns provider contribution records, active role uniqueness, lazy provider materialization, Ready/Faulted status, and disposal; `ISceneSnapshotProvider` remains a read-only data contract with no connect/disconnect/native lifecycle methods. `WorkbenchFeatureModule` contributes the fixture-backed active scene provider and still creates Hierarchy/Inspector panel ViewModels from the same provider instance.

**Tech Stack:** .NET 10, C#, xUnit, existing `IEditorExtensionModule`, `IEditorContributionBuilder`, `ISceneSnapshotProvider`, `WorkbenchFeatureModule`, `EditorExtensionHost`.

---

## Source Spec

- `docs/superpowers/specs/2026-06-23-studio-extension-lifecycle-v0-design.md`
- Slice 4: Provider Contribution.

## External Reference Notes

- VS Code separates contribution declarations and activation/deactivation, so Studio should keep provider declaration separate from provider usage.
- JetBrains Platform uses explicit disposable ownership boundaries, so provider instances created by the host must be released by the host registration/session boundary.
- Godot `EditorPlugin` pairs editor extension setup and cleanup through `_enter_tree()` / `_exit_tree()`, supporting explicit contribution cleanup.
- Avalonia requires UI object access on the UI thread; provider contribution factories must not create Avalonia controls or call native rendering APIs.

## Scope

In:

- Add a strongly typed scene provider descriptor and role constant for `scene.active`.
- Add `IEditorContributionBuilder.AddSceneProvider(...)`.
- Extend `EditorDeclaredContributions` and `EditorExtensionHost` to validate and commit provider contributions with owner ids.
- Add `EditorProviderHost` with lazy materialization, Ready/Faulted status, duplicate id/role validation, registration leases, and disposal.
- Register the current `InMemorySceneSnapshotProvider` as `scene.active` from `WorkbenchFeatureModule`.
- Prove Hierarchy and Inspector still observe the same active provider instance.

Out:

- Native bridge, C++ ABI, renderer viewport, plugin loading, ALC, hot reload, script VM.
- Provider connection APIs on `ISceneSnapshotProvider`.
- Runtime provider enable/disable UI.
- Console/Problems/status projection for provider failures.
- Writable scene authoring or Inspector writeback.

## Task 1: Provider Descriptor And Host

**Files:**
- Create: `Core/Models/EditorProviderRoles.cs`
- Create: `Core/Models/SceneProviderDescriptor.cs`
- Create: `Core/Models/EditorProviderState.cs`
- Create: `Core/Models/EditorProviderStatusSnapshot.cs`
- Create: `Shell/Composition/EditorProviderHost.cs`
- Create: `Tests/Editor.Tests/Shell/Composition/EditorProviderHostTests.cs`

- [x] **Step 1: Write failing provider host tests**

Add tests covering:

```csharp
[Fact]
public void RegisterOwned_records_owner_and_materializes_scene_provider_on_demand()
{
    var owner = new EditorExtensionId("test.owner");
    var provider = CreateProvider();
    var host = new EditorProviderHost();

    host.RegisterOwned(new SceneProviderDescriptor(
        "test.scene",
        EditorProviderRoles.ActiveScene,
        () => provider), owner);

    Assert.Equal(EditorProviderState.Created, host.GetStatus("test.scene").State);
    Assert.Same(provider, host.GetRequiredSceneSnapshotProvider(EditorProviderRoles.ActiveScene));
    Assert.Equal(owner, host.GetOwnerId("test.scene"));
    Assert.Equal(EditorProviderState.Ready, host.GetStatus("test.scene").State);
}
```

```csharp
[Fact]
public void RegisterOwned_rejects_duplicate_active_scene_role_with_owner_context()
{
    var host = new EditorProviderHost();
    host.RegisterOwned(new SceneProviderDescriptor(
        "first.scene",
        EditorProviderRoles.ActiveScene,
        CreateProvider), new EditorExtensionId("test.first"));

    var exception = Assert.Throws<InvalidOperationException>(() =>
        host.RegisterOwned(new SceneProviderDescriptor(
            "second.scene",
            EditorProviderRoles.ActiveScene,
            CreateProvider), new EditorExtensionId("test.second")));

    Assert.Equal(
        "Scene provider role 'scene.active' is already registered by 'test.first'; new owner 'test.second' cannot register it.",
        exception.Message);
}
```

```csharp
[Fact]
public void GetRequiredSceneSnapshotProvider_records_faulted_status_when_factory_fails()
{
    var expected = new InvalidOperationException("provider failed");
    var host = new EditorProviderHost();
    host.RegisterOwned(new SceneProviderDescriptor(
        "faulted.scene",
        EditorProviderRoles.ActiveScene,
        () => throw expected), new EditorExtensionId("test.owner"));

    var exception = Assert.Throws<InvalidOperationException>(() =>
        host.GetRequiredSceneSnapshotProvider(EditorProviderRoles.ActiveScene));

    Assert.Same(expected, exception.InnerException);
    var status = host.GetStatus("faulted.scene");
    Assert.Equal(EditorProviderState.Faulted, status.State);
    Assert.Equal("provider failed", status.Message);
}
```

```csharp
[Fact]
public void Dispose_releases_materialized_provider_and_removes_registration()
{
    var disposable = new DisposableSceneSnapshotProvider();
    var host = new EditorProviderHost();
    var lease = host.RegisterOwned(new SceneProviderDescriptor(
        "test.scene",
        EditorProviderRoles.ActiveScene,
        () => disposable), new EditorExtensionId("test.owner"));

    _ = host.GetRequiredSceneSnapshotProvider(EditorProviderRoles.ActiveScene);
    lease.Dispose();

    Assert.True(disposable.IsDisposed);
    Assert.Empty(host.GetSceneProviders());
}
```

- [x] **Step 2: Verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorProviderHostTests"
```

Expected: compile failure because provider descriptor, state, status, and host do not exist.

- [x] **Step 3: Implement provider descriptor and host**

Add:

```csharp
public static class EditorProviderRoles
{
    public const string ActiveScene = "scene.active";
}
```

```csharp
public sealed record SceneProviderDescriptor(
    string Id,
    string Role,
    Func<ISceneSnapshotProvider> CreateProvider);
```

`SceneProviderDescriptor` validates non-empty `Id` and `Role`, and non-null `CreateProvider`.

Add:

```csharp
public enum EditorProviderState
{
    Created,
    Ready,
    Faulted,
}
```

```csharp
public sealed record EditorProviderStatusSnapshot(
    string Id,
    string Role,
    EditorExtensionId OwnerId,
    EditorProviderState State,
    string? Message = null);
```

`EditorProviderHost` stores scene provider entries by id and role. `GetRequiredSceneSnapshotProvider(role)` materializes the provider once, marks it Ready, and returns the same instance on later calls. If factory creation throws, mark status Faulted and throw an `InvalidOperationException` with the original exception as `InnerException`. Registration leases remove the entry and dispose materialized providers implementing `IDisposable`.

- [x] **Step 4: Verify GREEN**

Run the same focused command. Expected: provider host tests pass.

## Task 2: Contribution Builder And Extension Host Integration

**Files:**
- Modify: `Core/Abstractions/IEditorContributionBuilder.cs`
- Modify: `Shell/Composition/EditorContributionBuilder.cs`
- Modify: `Shell/Composition/EditorDeclaredContributions.cs`
- Modify: `Shell/Composition/EditorExtensionComposition.cs`
- Modify: `Shell/Composition/EditorExtensionHost.cs`
- Modify: `Tests/Editor.Tests/Shell/Composition/EditorExtensionHostTests.cs`

- [x] **Step 1: Write failing extension host tests**

Add tests covering:

```csharp
[Fact]
public void Compose_registers_scene_provider_contributions_with_owner()
{
    var provider = CreateProvider();
    var module = new TestExtensionModule(
        "test.owner",
        sceneProviders:
        [
            new SceneProviderDescriptor(
                "test.scene",
                EditorProviderRoles.ActiveScene,
                () => provider),
        ]);
    var host = new EditorExtensionHost([module]);

    var composition = host.Compose();

    Assert.Same(provider, composition.ProviderHost.GetRequiredSceneSnapshotProvider(EditorProviderRoles.ActiveScene));
    Assert.Equal(module.Id, composition.ProviderHost.GetOwnerId("test.scene"));
}
```

```csharp
[Fact]
public void Compose_rejects_duplicate_scene_provider_role_before_returning_composition()
{
    var host = new EditorExtensionHost(
    [
        new TestExtensionModule(
            "test.first",
            sceneProviders:
            [
                new SceneProviderDescriptor("first.scene", EditorProviderRoles.ActiveScene, CreateProvider),
            ]),
        new TestExtensionModule(
            "test.second",
            sceneProviders:
            [
                new SceneProviderDescriptor("second.scene", EditorProviderRoles.ActiveScene, CreateProvider),
            ]),
    ]);

    var exception = Assert.Throws<InvalidOperationException>(() => host.Compose());

    Assert.Equal(
        "Scene provider role 'scene.active' is contributed by both 'test.first' and 'test.second'.",
        exception.Message);
}
```

```csharp
[Fact]
public async Task DisposeAsync_removes_registered_scene_provider_contributions()
{
    var host = new EditorExtensionHost(
    [
        new TestExtensionModule(
            "test.owner",
            sceneProviders:
            [
                new SceneProviderDescriptor("test.scene", EditorProviderRoles.ActiveScene, CreateProvider),
            ]),
    ]);
    var composition = host.Compose();

    await host.DisposeAsync();

    Assert.Empty(composition.ProviderHost.GetSceneProviders());
}
```

- [x] **Step 2: Verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorExtensionHostTests"
```

Expected: compile failure because contribution builder and composition do not expose scene providers yet.

- [x] **Step 3: Integrate scene providers into contribution commit**

Extend `IEditorContributionBuilder` with:

```csharp
void AddSceneProvider(SceneProviderDescriptor descriptor);
```

Extend `EditorDeclaredContributions` with `IReadOnlyList<SceneProviderDescriptor> SceneProviders`.

Extend `EditorExtensionComposition` with:

```csharp
EditorProviderHost ProviderHost
```

In `EditorExtensionHost.Compose()`:

- validate unique scene provider ids and roles before committing;
- create one `EditorProviderHost`;
- register scene providers after panels/actions with owner ids;
- add returned provider leases to the same rollback/disposal list.

- [x] **Step 4: Verify GREEN**

Run the same focused command. Expected: extension host tests pass.

## Task 3: Workbench Active Scene Provider Contribution

**Files:**
- Modify: `Features/Workbench/WorkbenchFeatureModule.cs`
- Modify: `Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs`
- Modify: `Tests/Editor.Tests/Shell/Composition/StudioCompositionRootTests.cs`

- [x] **Step 1: Write failing Workbench tests**

Add tests covering:

```csharp
[Fact]
public void Compose_registers_workbench_active_scene_provider()
{
    var composition = new EditorExtensionHost(
        [new WorkbenchFeatureModule(new EditorSelectionService())]).Compose();

    var providers = composition.ProviderHost.GetSceneProviders();

    var descriptor = Assert.Single(providers);
    Assert.Equal("workbench.scene.fixture", descriptor.Id);
    Assert.Equal(EditorProviderRoles.ActiveScene, descriptor.Role);
    Assert.IsType<InMemorySceneSnapshotProvider>(
        composition.ProviderHost.GetRequiredSceneSnapshotProvider(EditorProviderRoles.ActiveScene));
}
```

```csharp
[Fact]
public void Hierarchy_and_inspector_observe_active_scene_provider_changes()
{
    var composition = new EditorExtensionHost(
        [new WorkbenchFeatureModule(new EditorSelectionService())]).Compose();
    var provider = Assert.IsType<InMemorySceneSnapshotProvider>(
        composition.ProviderHost.GetRequiredSceneSnapshotProvider(EditorProviderRoles.ActiveScene));
    var hierarchy = Assert.IsType<HierarchyPanelViewModel>(
        composition.PanelRegistry.GetRequired("hierarchy").CreateContent());
    var inspector = Assert.IsType<InspectorPanelViewModel>(
        composition.PanelRegistry.GetRequired("inspector").CreateContent());

    provider.ReplaceSnapshot(new SceneSnapshot(
        "scene:test",
        "Test Scene",
        2,
        [
            new SceneObjectSnapshot("scene:test", "Test Scene", "scene"),
            new SceneObjectSnapshot("scene:test/sphere", "Sphere", "mesh", parentId: "scene:test"),
        ]));
    hierarchy.SelectedNode = hierarchy.Nodes.Single(node => node.Id == "scene:test/sphere");

    Assert.Equal("Sphere", inspector.Document?.Title);
}
```

- [x] **Step 2: Verify RED**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~StudioCompositionRootTests"
```

Expected: compile or assertion failure because Workbench has not declared a provider contribution and composition has no provider host yet.

- [x] **Step 3: Declare the fixture-backed active scene provider**

In `WorkbenchFeatureModule.Declare(...)`, before panel/action registration, add:

```csharp
builder.AddSceneProvider(new SceneProviderDescriptor(
    "workbench.scene.fixture",
    EditorProviderRoles.ActiveScene,
    () => sceneSnapshotProvider_));
```

Keep Hierarchy and Inspector panel factories using `sceneSnapshotProvider_`. This makes the host-facing active provider role and the panel consumers share the same instance without adding a service locator or connecting to native runtime.

- [x] **Step 4: Verify GREEN**

Run the same focused command. Expected: Workbench and composition tests pass.

## Task 4: Documentation And Verification

**Files:**
- Modify: `docs/Dock系统指南.md`
- Modify: `docs/编辑器UI平台规范.md`
- Modify: `docs/superpowers/plans/2026-06-23-studio-provider-contribution.md`

- [x] **Step 1: Document provider contribution boundary**

Record that provider contribution v0 registers the fixture-backed active scene provider under `scene.active`, keeps `ISceneSnapshotProvider` read-only, and does not add native bridge, connect/disconnect, plugin reload, script VM, Console/Problems projection, or writable scene editing.

- [x] **Step 2: Run focused and full verification**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~EditorProviderHostTests|FullyQualifiedName~EditorExtensionHostTests|FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~StudioCompositionRootTests|FullyQualifiedName~SceneSnapshotProviderTests|FullyQualifiedName~HierarchyPanelViewModelTests"
dotnet test Editor.sln -c Release
```

From repository root:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
```

From `apps/studio`:

```powershell
git diff --check
rg -n "AssemblyLoadContext|NativeEditorBridge|C\+\+ ABI|script VM|ScriptExecutionHost|PluginLoader|LoadFromAssembly|Connect\(|Disconnect\(" Core Shell Features Tests
```

Expected: tests pass; encoding has 0 issues; diff check is clean; non-goal search has no implementation matches except existing documentation/spec text.

- [x] **Step 3: Commit**

```powershell
git add Core\Models\EditorProviderRoles.cs Core\Models\SceneProviderDescriptor.cs Core\Models\EditorProviderState.cs Core\Models\EditorProviderStatusSnapshot.cs Core\Abstractions\IEditorContributionBuilder.cs Shell\Composition\EditorProviderHost.cs Shell\Composition\EditorContributionBuilder.cs Shell\Composition\EditorDeclaredContributions.cs Shell\Composition\EditorExtensionComposition.cs Shell\Composition\EditorExtensionHost.cs Features\Workbench\WorkbenchFeatureModule.cs Tests\Editor.Tests\Shell\Composition\EditorProviderHostTests.cs Tests\Editor.Tests\Shell\Composition\EditorExtensionHostTests.cs Tests\Editor.Tests\Features\Workbench\WorkbenchFeatureModuleTests.cs Tests\Editor.Tests\Shell\Composition\StudioCompositionRootTests.cs docs\Dock系统指南.md docs\编辑器UI平台规范.md docs\superpowers\plans\2026-06-23-studio-provider-contribution.md
git commit -m "feat: register studio scene provider contribution"
```
