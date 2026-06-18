# Studio Scene Snapshot Provider Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Move Studio Hierarchy and Inspector from demo-only hierarchy data to a shared read-only scene snapshot provider.

**Architecture:** Add backend-neutral scene snapshot contracts in `Core`, plus an in-memory provider that validates duplicate ids. Hierarchy projects provider objects into its existing row model, while Inspector resolves the current selection id through the same provider before building a read-only document.

**Tech Stack:** Avalonia 12.0.4, .NET 10, CommunityToolkit.Mvvm, xUnit, existing Studio MVVM and feature-module registration.

---

## Scope

Implement the approved spec from `apps/studio/docs/superpowers/specs/2026-06-18-studio-scene-snapshot-provider-design.md`.

Do not implement scene persistence, editable Transform fields, Undo/Redo, dirty state, asset browser integration, Project/Console data sources, runtime bridge, renderer bridge, or `packages/editor-core`.

## File Structure

Create:

- `apps/studio/Core/Models/SceneObjectPropertyValueKind.cs` - Core enum for provider property value display hints.
- `apps/studio/Core/Models/SceneObjectPropertySnapshot.cs` - Core read-only property fact.
- `apps/studio/Core/Models/SceneObjectSnapshot.cs` - Core read-only scene object fact and selection conversion.
- `apps/studio/Core/Models/SceneSnapshot.cs` - Core read-only scene/document snapshot.
- `apps/studio/Core/Abstractions/ISceneSnapshotProvider.cs` - Core provider contract.
- `apps/studio/Core/Services/InMemorySceneSnapshotProvider.cs` - backend-neutral static snapshot provider for fixture/test/runtime wiring.
- `apps/studio/Tests/Editor.Tests/Core/SceneSnapshotProviderTests.cs` - unit coverage for snapshot/provider behavior.

Modify:

- `apps/studio/Features/Hierarchy/Models/HierarchyNodeModel.cs` - add projection from `SceneObjectSnapshot`.
- `apps/studio/Features/Hierarchy/ViewModels/HierarchyPanelViewModel.cs` - consume `ISceneSnapshotProvider` instead of `IHierarchyDataSource`.
- `apps/studio/Tests/Editor.Tests/Features/Hierarchy/HierarchyPanelViewModelTests.cs` - update tests to use scene snapshots.
- `apps/studio/Features/Inspector/ViewModels/InspectorPanelViewModel.cs` - resolve single selections through `ISceneSnapshotProvider`.
- `apps/studio/Tests/Editor.Tests/Features/Inspector/InspectorPanelViewModelTests.cs` - update tests for provider-backed documents and missing selections.
- `apps/studio/Features/Workbench/WorkbenchFeatureModule.cs` - create one fixture provider and inject it into Hierarchy and Inspector.
- `apps/studio/Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs` - prove shared provider wiring.
- `apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs` - adjust shared selection assertions to provider-backed fixture ids.
- `apps/studio/docs/Dock系统指南.md` - record provider v0 implementation facts and refine follow-up wording.

Delete:

- `apps/studio/Features/Hierarchy/Services/IHierarchyDataSource.cs`
- `apps/studio/Features/Hierarchy/Services/DemoHierarchyDataSource.cs`

---

### Task 1: Core Scene Snapshot Contracts

**Files:**
- Create: `apps/studio/Core/Models/SceneObjectPropertyValueKind.cs`
- Create: `apps/studio/Core/Models/SceneObjectPropertySnapshot.cs`
- Create: `apps/studio/Core/Models/SceneObjectSnapshot.cs`
- Create: `apps/studio/Core/Models/SceneSnapshot.cs`
- Create: `apps/studio/Core/Abstractions/ISceneSnapshotProvider.cs`
- Create: `apps/studio/Core/Services/InMemorySceneSnapshotProvider.cs`
- Create: `apps/studio/Tests/Editor.Tests/Core/SceneSnapshotProviderTests.cs`

- [ ] **Step 1: Write the failing provider tests**

Create `apps/studio/Tests/Editor.Tests/Core/SceneSnapshotProviderTests.cs`:

```csharp
using System;
using System.Collections.Generic;
using Editor.Core.Models;
using Editor.Core.Services;
using Xunit;

namespace Editor.Tests.Core;

public sealed class SceneSnapshotProviderTests
{
    [Fact]
    public void InMemory_provider_exposes_current_snapshot_and_lookup()
    {
        var cube = new SceneObjectSnapshot(
            "scene:test/cube",
            "Cube",
            "mesh",
            parentId: "scene:test",
            iconKey: "studio.object.default",
            properties:
            [
                new SceneObjectPropertySnapshot("triangles", "Triangles", "12", SceneObjectPropertyValueKind.Count),
            ]);
        var snapshot = new SceneSnapshot("scene:test", "Test Scene", 7, [cube]);
        var provider = new InMemorySceneSnapshotProvider(snapshot);

        Assert.Same(snapshot, provider.Current);
        Assert.True(provider.TryGetObject("scene:test/cube", out var actual));
        Assert.Same(cube, actual);
        Assert.False(provider.TryGetObject("scene:test/missing", out var missing));
        Assert.Null(missing);
    }

    [Fact]
    public void InMemory_provider_rejects_duplicate_object_ids()
    {
        var first = new SceneObjectSnapshot("scene:test/cube", "Cube", "mesh");
        var second = new SceneObjectSnapshot("scene:test/cube", "Duplicate Cube", "mesh");
        var snapshot = new SceneSnapshot("scene:test", "Test Scene", 1, [first, second]);

        var exception = Assert.Throws<InvalidOperationException>(
            () => new InMemorySceneSnapshotProvider(snapshot));

        Assert.Contains("scene:test/cube", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Scene_object_snapshot_normalizes_blank_display_name_to_id()
    {
        var sceneObject = new SceneObjectSnapshot("scene:test/cube", " ", "mesh");

        Assert.Equal("scene:test/cube", sceneObject.DisplayName);
    }

    [Fact]
    public void Scene_object_snapshot_copies_property_list()
    {
        var properties = new List<SceneObjectPropertySnapshot>
        {
            new("name", "Name", "Cube"),
        };

        var sceneObject = new SceneObjectSnapshot("scene:test/cube", "Cube", "mesh", properties: properties);
        properties.Add(new SceneObjectPropertySnapshot("kind", "Kind", "mesh"));

        var property = Assert.Single(sceneObject.Properties);
        Assert.Equal("name", property.Id);
    }
}
```

- [ ] **Step 2: Run test to verify it fails**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter SceneSnapshotProviderTests
```

Expected: FAIL with missing type errors for `SceneObjectSnapshot`, `SceneSnapshot`, `SceneObjectPropertySnapshot`, `SceneObjectPropertyValueKind`, and `InMemorySceneSnapshotProvider`.

- [ ] **Step 3: Add Core value kind enum**

Create `apps/studio/Core/Models/SceneObjectPropertyValueKind.cs`:

```csharp
namespace Editor.Core.Models;

public enum SceneObjectPropertyValueKind
{
    Text,
    Count,
}
```

- [ ] **Step 4: Add property snapshot model**

Create `apps/studio/Core/Models/SceneObjectPropertySnapshot.cs`:

```csharp
using System;

namespace Editor.Core.Models;

public sealed record SceneObjectPropertySnapshot
{
    public SceneObjectPropertySnapshot(
        string id,
        string displayName,
        string value,
        SceneObjectPropertyValueKind valueKind = SceneObjectPropertyValueKind.Text,
        string? diagnostic = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(id);
        ArgumentNullException.ThrowIfNull(value);

        Id = id;
        DisplayName = string.IsNullOrWhiteSpace(displayName) ? id : displayName;
        Value = value;
        ValueKind = valueKind;
        Diagnostic = string.IsNullOrWhiteSpace(diagnostic) ? null : diagnostic;
    }

    public string Id { get; }

    public string DisplayName { get; }

    public string Value { get; }

    public SceneObjectPropertyValueKind ValueKind { get; }

    public string? Diagnostic { get; }
}
```

- [ ] **Step 5: Add scene object snapshot model**

Create `apps/studio/Core/Models/SceneObjectSnapshot.cs`:

```csharp
using System;
using System.Collections.Generic;
using System.Linq;

namespace Editor.Core.Models;

public sealed record SceneObjectSnapshot
{
    public SceneObjectSnapshot(
        string id,
        string displayName,
        string kind,
        string? parentId = null,
        string? iconKey = null,
        bool isActive = true,
        IReadOnlyList<SceneObjectPropertySnapshot>? properties = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(id);
        ArgumentException.ThrowIfNullOrWhiteSpace(kind);

        Id = id;
        DisplayName = string.IsNullOrWhiteSpace(displayName) ? id : displayName;
        Kind = kind;
        ParentId = string.IsNullOrWhiteSpace(parentId) ? null : parentId;
        IconKey = string.IsNullOrWhiteSpace(iconKey) ? null : iconKey;
        IsActive = isActive;
        Properties = properties?.ToArray() ?? [];
    }

    public string Id { get; }

    public string DisplayName { get; }

    public string Kind { get; }

    public string? ParentId { get; }

    public string? IconKey { get; }

    public bool IsActive { get; }

    public IReadOnlyList<SceneObjectPropertySnapshot> Properties { get; }

    public EditorSelectionItem ToSelectionItem()
    {
        return new EditorSelectionItem(Id, Kind, DisplayName, IconKey);
    }
}
```

- [ ] **Step 6: Add scene snapshot model**

Create `apps/studio/Core/Models/SceneSnapshot.cs`:

```csharp
using System;
using System.Collections.Generic;
using System.Linq;

namespace Editor.Core.Models;

public sealed record SceneSnapshot
{
    public static SceneSnapshot Empty { get; } = new("scene:empty", "Empty Scene", 0, []);

    public SceneSnapshot(
        string id,
        string displayName,
        long revision,
        IReadOnlyList<SceneObjectSnapshot>? objects = null)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(id);

        Id = id;
        DisplayName = string.IsNullOrWhiteSpace(displayName) ? id : displayName;
        Revision = Math.Max(0, revision);
        Objects = objects?.ToArray() ?? [];
    }

    public string Id { get; }

    public string DisplayName { get; }

    public long Revision { get; }

    public IReadOnlyList<SceneObjectSnapshot> Objects { get; }
}
```

- [ ] **Step 7: Add provider contract**

Create `apps/studio/Core/Abstractions/ISceneSnapshotProvider.cs`:

```csharp
using Editor.Core.Models;

namespace Editor.Core.Abstractions;

public interface ISceneSnapshotProvider
{
    SceneSnapshot Current { get; }

    bool TryGetObject(string objectId, out SceneObjectSnapshot? sceneObject);
}
```

- [ ] **Step 8: Add in-memory provider**

Create `apps/studio/Core/Services/InMemorySceneSnapshotProvider.cs`:

```csharp
using System;
using System.Collections.Generic;
using System.Linq;
using Editor.Core.Abstractions;
using Editor.Core.Models;

namespace Editor.Core.Services;

public sealed class InMemorySceneSnapshotProvider : ISceneSnapshotProvider
{
    private readonly Dictionary<string, SceneObjectSnapshot> objectsById_;

    public InMemorySceneSnapshotProvider(SceneSnapshot current)
    {
        ArgumentNullException.ThrowIfNull(current);

        var duplicateId = current.Objects
            .GroupBy(sceneObject => sceneObject.Id, StringComparer.Ordinal)
            .FirstOrDefault(group => group.Count() > 1)
            ?.Key;
        if (duplicateId is not null)
        {
            throw new InvalidOperationException($"Duplicate scene object id '{duplicateId}'.");
        }

        Current = current;
        objectsById_ = current.Objects.ToDictionary(
            sceneObject => sceneObject.Id,
            StringComparer.Ordinal);
    }

    public SceneSnapshot Current { get; }

    public bool TryGetObject(string objectId, out SceneObjectSnapshot? sceneObject)
    {
        if (string.IsNullOrWhiteSpace(objectId))
        {
            sceneObject = null;
            return false;
        }

        return objectsById_.TryGetValue(objectId, out sceneObject);
    }
}
```

- [ ] **Step 9: Run Core tests to verify they pass**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter SceneSnapshotProviderTests
```

Expected: PASS.

- [ ] **Step 10: Commit Core contract**

Run:

```powershell
git add apps/studio/Core/Models/SceneObjectPropertyValueKind.cs `
        apps/studio/Core/Models/SceneObjectPropertySnapshot.cs `
        apps/studio/Core/Models/SceneObjectSnapshot.cs `
        apps/studio/Core/Models/SceneSnapshot.cs `
        apps/studio/Core/Abstractions/ISceneSnapshotProvider.cs `
        apps/studio/Core/Services/InMemorySceneSnapshotProvider.cs `
        apps/studio/Tests/Editor.Tests/Core/SceneSnapshotProviderTests.cs
git commit -m "feat: add studio scene snapshot contract"
```

Expected: commit succeeds.

---

### Task 2: Hierarchy Consumes Scene Snapshot Provider

**Files:**
- Modify: `apps/studio/Features/Hierarchy/Models/HierarchyNodeModel.cs`
- Modify: `apps/studio/Features/Hierarchy/ViewModels/HierarchyPanelViewModel.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Features/Hierarchy/HierarchyPanelViewModelTests.cs`
- Delete: `apps/studio/Features/Hierarchy/Services/IHierarchyDataSource.cs`
- Delete: `apps/studio/Features/Hierarchy/Services/DemoHierarchyDataSource.cs`

- [ ] **Step 1: Replace Hierarchy tests to target provider-backed nodes**

Replace `apps/studio/Tests/Editor.Tests/Features/Hierarchy/HierarchyPanelViewModelTests.cs` with:

```csharp
using System.Collections.Generic;
using Editor.Core.Models;
using Editor.Core.Services;
using Editor.Features.Hierarchy.Models;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Shell.Icons;
using Editor.Shell.Selection;
using Xunit;

namespace Editor.Tests.Features.Hierarchy;

public sealed class HierarchyPanelViewModelTests
{
    [Fact]
    public void Constructor_loads_nodes_from_scene_snapshot_provider()
    {
        var viewModel = CreateViewModel();

        Assert.Equal(["Cube", "Light"], GetNodeNames(viewModel.Nodes));
        Assert.Equal(["Cube", "Light"], GetRowNames(viewModel.VisibleRows));
        Assert.Equal(EditorIconKey.ObjectDefault, viewModel.VisibleRows[0].IconKey);
        Assert.Null(viewModel.SelectedNode);
    }

    [Fact]
    public void Selecting_node_publishes_selection_item()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = CreateViewModel(selectionService);

        viewModel.SelectedNode = viewModel.Nodes[0];

        Assert.Equal("hierarchy", selectionService.Current.ActiveContextId);
        var item = Assert.Single(selectionService.Current.Items);
        Assert.Equal("scene:test/cube", item.Id);
        Assert.Equal("Cube", item.DisplayName);
        Assert.Equal("mesh", item.Kind);
    }

    [Fact]
    public void Selecting_visible_row_publishes_selection_item()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = CreateViewModel(selectionService);

        viewModel.SelectedRow = viewModel.VisibleRows[0];

        Assert.Equal("hierarchy", selectionService.Current.ActiveContextId);
        var item = Assert.Single(selectionService.Current.Items);
        Assert.Equal("scene:test/cube", item.Id);
        Assert.Equal("Cube", item.DisplayName);
    }

    [Fact]
    public void Clearing_selected_node_clears_selection()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = CreateViewModel(selectionService);
        viewModel.SelectedNode = viewModel.Nodes[0];

        viewModel.SelectedNode = null;

        Assert.Equal("hierarchy", selectionService.Current.ActiveContextId);
        Assert.False(selectionService.Current.HasSelection);
    }

    [Fact]
    public void Default_constructor_starts_with_empty_snapshot()
    {
        var viewModel = new HierarchyPanelViewModel(new EditorSelectionService());

        Assert.Empty(viewModel.Nodes);
        Assert.Empty(viewModel.VisibleRows);
        Assert.Equal("0", viewModel.NodeCountText);
    }

    [Fact]
    public void Toggle_expanded_hides_and_shows_descendants()
    {
        var viewModel = CreateTreeViewModel();
        var sceneRow = viewModel.VisibleRows[0];

        Assert.True(sceneRow.HasChildren);
        Assert.True(sceneRow.IsExpanded);
        Assert.Equal(EditorIconKey.UiChevronDown, sceneRow.ExpanderIconKey);
        Assert.Equal(EditorIconKey.UiChevronRight, viewModel.VisibleRows[1].ExpanderIconKey);
        Assert.Equal(["Scene", "Cube", "Light"], GetRowNames(viewModel.VisibleRows));
        Assert.False(viewModel.VisibleRows[1].IsLastSibling);
        Assert.True(viewModel.VisibleRows[2].IsLastSibling);

        viewModel.ToggleExpandedCommand.Execute(sceneRow);

        Assert.Equal(["Scene"], GetRowNames(viewModel.VisibleRows));
        Assert.False(viewModel.VisibleRows[0].IsExpanded);
        Assert.Equal(EditorIconKey.UiChevronRight, viewModel.VisibleRows[0].ExpanderIconKey);

        viewModel.ToggleExpandedCommand.Execute(viewModel.VisibleRows[0]);

        Assert.Equal(["Scene", "Cube", "Light"], GetRowNames(viewModel.VisibleRows));
    }

    [Fact]
    public void Expanded_child_rows_expose_branch_continuation_metadata()
    {
        var viewModel = CreateTreeViewModel();
        var cubeRow = viewModel.VisibleRows[1];

        viewModel.ToggleExpandedCommand.Execute(cubeRow);

        Assert.Equal(["Scene", "Cube", "Mesh Renderer", "Light"], GetRowNames(viewModel.VisibleRows));
        Assert.True(viewModel.VisibleRows[1].IsExpanded);
        Assert.False(viewModel.VisibleRows[1].IsLastSibling);
        Assert.Equal(0UL, viewModel.VisibleRows[1].AncestorContinuationMask);
        Assert.True(viewModel.VisibleRows[2].IsLastSibling);
        Assert.Equal(1UL, viewModel.VisibleRows[2].AncestorContinuationMask);
        Assert.True(viewModel.VisibleRows[3].IsLastSibling);
        Assert.Equal(0UL, viewModel.VisibleRows[3].AncestorContinuationMask);
    }

    [Fact]
    public void Filter_text_keeps_matching_ancestors_visible()
    {
        var viewModel = CreateTreeViewModel();
        viewModel.ToggleExpandedCommand.Execute(viewModel.VisibleRows[0]);

        viewModel.FilterText = "renderer";

        Assert.True(viewModel.HasFilter);
        Assert.False(viewModel.HasNoMatches);
        Assert.Equal("3/4", viewModel.NodeCountText);
        Assert.Equal(["Scene", "Cube", "Mesh Renderer"], GetRowNames(viewModel.VisibleRows));
        Assert.True(viewModel.VisibleRows[0].IsExpanded);
        Assert.True(viewModel.VisibleRows[1].IsExpanded);
        Assert.True(viewModel.VisibleRows[1].IsLastSibling);
        Assert.True(viewModel.VisibleRows[2].IsSearchMatch);
        Assert.Equal(0UL, viewModel.VisibleRows[2].AncestorContinuationMask);
    }

    [Fact]
    public void Filter_text_reports_no_matches()
    {
        var viewModel = CreateTreeViewModel();

        viewModel.FilterText = "missing";

        Assert.True(viewModel.HasNoMatches);
        Assert.Empty(viewModel.VisibleRows);
        Assert.Equal("0/4", viewModel.NodeCountText);
    }

    private static HierarchyPanelViewModel CreateViewModel(
        EditorSelectionService? selectionService = null)
    {
        return new HierarchyPanelViewModel(
            selectionService ?? new EditorSelectionService(),
            new InMemorySceneSnapshotProvider(new SceneSnapshot(
                "scene:test",
                "Test Scene",
                1,
                [
                    new SceneObjectSnapshot("scene:test/cube", "Cube", "mesh"),
                    new SceneObjectSnapshot("scene:test/light", "Light", "light"),
                ])));
    }

    private static HierarchyPanelViewModel CreateTreeViewModel()
    {
        return new HierarchyPanelViewModel(
            new EditorSelectionService(),
            new InMemorySceneSnapshotProvider(new SceneSnapshot(
                "scene:test",
                "Test Scene",
                1,
                [
                    new SceneObjectSnapshot("scene:test", "Scene", "scene"),
                    new SceneObjectSnapshot("scene:test/cube", "Cube", "mesh", parentId: "scene:test"),
                    new SceneObjectSnapshot("scene:test/cube/renderer", "Mesh Renderer", "component", parentId: "scene:test/cube"),
                    new SceneObjectSnapshot("scene:test/light", "Light", "light", parentId: "scene:test"),
                ])));
    }

    private static string[] GetNodeNames(IReadOnlyList<HierarchyNodeModel> nodes)
    {
        var names = new string[nodes.Count];
        for (var index = 0; index < nodes.Count; index++)
        {
            names[index] = nodes[index].DisplayName;
        }

        return names;
    }

    private static string[] GetRowNames(IReadOnlyList<HierarchyNodeRowViewModel> rows)
    {
        var names = new string[rows.Count];
        for (var index = 0; index < rows.Count; index++)
        {
            names[index] = rows[index].DisplayName;
        }

        return names;
    }
}
```

- [ ] **Step 2: Run Hierarchy tests to verify they fail**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter HierarchyPanelViewModelTests
```

Expected: FAIL because `HierarchyPanelViewModel` still expects `IHierarchyDataSource`.

- [ ] **Step 3: Update hierarchy node projection**

Replace `apps/studio/Features/Hierarchy/Models/HierarchyNodeModel.cs` with:

```csharp
using Editor.Core.Models;

namespace Editor.Features.Hierarchy.Models;

public sealed record HierarchyNodeModel(
    string Id,
    string DisplayName,
    string Kind,
    string? IconKey = null,
    string? ParentId = null)
{
    public static HierarchyNodeModel FromSceneObject(SceneObjectSnapshot sceneObject)
    {
        return new HierarchyNodeModel(
            sceneObject.Id,
            sceneObject.DisplayName,
            sceneObject.Kind,
            sceneObject.IconKey,
            sceneObject.ParentId);
    }

    public EditorSelectionItem ToSelectionItem()
    {
        return new EditorSelectionItem(Id, Kind, DisplayName, IconKey);
    }
}
```

- [ ] **Step 4: Update HierarchyPanelViewModel constructor and imports**

Modify `apps/studio/Features/Hierarchy/ViewModels/HierarchyPanelViewModel.cs`:

1. Remove:

```csharp
using Editor.Features.Hierarchy.Services;
```

2. Add:

```csharp
using Editor.Core.Services;
```

3. Replace the two constructors with:

```csharp
    public HierarchyPanelViewModel(IEditorSelectionService selectionService)
        : this(selectionService, new InMemorySceneSnapshotProvider(SceneSnapshot.Empty))
    {
    }

    internal HierarchyPanelViewModel(
        IEditorSelectionService selectionService,
        ISceneSnapshotProvider sceneSnapshotProvider)
    {
        ArgumentNullException.ThrowIfNull(selectionService);
        ArgumentNullException.ThrowIfNull(sceneSnapshotProvider);

        selectionService_ = selectionService;
        SceneSnapshot = sceneSnapshotProvider.Current;
        Nodes = SceneSnapshot.Objects
            .Select(HierarchyNodeModel.FromSceneObject)
            .ToArray();
        nodesById_ = Nodes.ToDictionary(node => node.Id, StringComparer.Ordinal);
        depthsByNodeId_ = Nodes.ToDictionary(node => node.Id, GetDepth, StringComparer.Ordinal);
        nodeIdsWithChildren_ = Nodes
            .Where(node => !string.IsNullOrWhiteSpace(node.ParentId))
            .Select(node => node.ParentId!)
            .ToHashSet(StringComparer.Ordinal);
        expandedNodeIds_ = Nodes
            .Where(node => node.ParentId is null && nodeIdsWithChildren_.Contains(node.Id))
            .Select(node => node.Id)
            .ToHashSet(StringComparer.Ordinal);

        ToggleExpandedCommand = new RelayCommand<HierarchyNodeRowViewModel>(ToggleExpanded);
        RefreshVisibleRows();
    }
```

4. Add this public property immediately before `Nodes`:

```csharp
    public SceneSnapshot SceneSnapshot { get; }
```

- [ ] **Step 5: Delete old hierarchy data source files**

Run:

```powershell
git rm apps/studio/Features/Hierarchy/Services/IHierarchyDataSource.cs `
       apps/studio/Features/Hierarchy/Services/DemoHierarchyDataSource.cs
```

Expected: both files are removed from the index and worktree.

- [ ] **Step 6: Run Hierarchy tests to verify they pass**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter HierarchyPanelViewModelTests
```

Expected: PASS.

- [ ] **Step 7: Commit Hierarchy migration**

Run:

```powershell
git add apps/studio/Features/Hierarchy/Models/HierarchyNodeModel.cs `
        apps/studio/Features/Hierarchy/ViewModels/HierarchyPanelViewModel.cs `
        apps/studio/Tests/Editor.Tests/Features/Hierarchy/HierarchyPanelViewModelTests.cs
git commit -m "refactor: route hierarchy through scene snapshot"
```

Expected: commit succeeds and includes the `git rm` deletions.

---

### Task 3: Inspector Resolves Selection Through Provider

**Files:**
- Modify: `apps/studio/Features/Inspector/ViewModels/InspectorPanelViewModel.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Features/Inspector/InspectorPanelViewModelTests.cs`

- [ ] **Step 1: Replace Inspector tests with provider-backed expectations**

Replace `apps/studio/Tests/Editor.Tests/Features/Inspector/InspectorPanelViewModelTests.cs` with:

```csharp
using System.Linq;
using Editor.Core.Models;
using Editor.Core.Services;
using Editor.Features.Inspector.Models;
using Editor.Features.Inspector.ViewModels;
using Editor.Shell.Selection;
using Xunit;

namespace Editor.Tests.Features.Inspector;

public sealed class InspectorPanelViewModelTests
{
    [Fact]
    public void Constructor_starts_without_document_when_selection_is_empty()
    {
        var viewModel = new InspectorPanelViewModel(new EditorSelectionService(), CreateProvider());

        Assert.False(viewModel.HasDocument);
        Assert.Null(viewModel.Document);
    }

    [Fact]
    public void Constructor_builds_document_from_existing_provider_selection()
    {
        var selectionService = new EditorSelectionService();
        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:test/cube", "mesh", "Cube", "studio.object.default")]);

        var viewModel = new InspectorPanelViewModel(selectionService, CreateProvider());

        AssertSingleSelectionDocument(viewModel.Document);
    }

    [Fact]
    public void Selection_changed_builds_single_selection_document_from_provider()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService, CreateProvider());

        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:test/cube", "mesh", "Cube", "studio.object.default")]);

        Assert.True(viewModel.HasDocument);
        AssertSingleSelectionDocument(viewModel.Document);
    }

    [Fact]
    public void Missing_selection_builds_validation_document()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService, CreateProvider());

        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:test/missing", "mesh", "Missing Cube")]);

        Assert.NotNull(viewModel.Document);
        var document = viewModel.Document!;
        Assert.Equal("Missing Cube", document.Title);
        Assert.Equal("Missing selection", document.Subtitle);
        var section = Assert.Single(document.Sections);
        Assert.Equal("Validation", section.Title);
        Assert.Contains(new InspectorPropertyModel("State", "Missing"), section.Properties);
        Assert.Contains(new InspectorPropertyModel("Id", "scene:test/missing"), section.Properties);
        Assert.Contains(new InspectorPropertyModel("Context", "hierarchy"), section.Properties);
    }

    [Fact]
    public void Clear_selection_removes_document()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService, CreateProvider());
        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:test/cube", "mesh", "Cube")]);

        selectionService.ClearSelection("hierarchy");

        Assert.False(viewModel.HasDocument);
        Assert.Null(viewModel.Document);
    }

    [Fact]
    public void Multi_selection_builds_summary_document_without_merging_properties()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService, CreateProvider());

        selectionService.ReplaceSelection(
            "hierarchy",
            [
                new EditorSelectionItem("scene:test/cube", "mesh", "Cube"),
                new EditorSelectionItem("scene:test/light", "light", "Light"),
            ]);

        Assert.NotNull(viewModel.Document);
        var document = viewModel.Document!;
        Assert.True(document.IsMultiSelection);
        Assert.Equal(2, document.SelectionCount);
        Assert.Equal("2 items selected", document.Title);
        Assert.Equal("Multi-selection", document.Subtitle);
        var section = Assert.Single(document.Sections);
        Assert.Equal("Selection", section.Title);
        Assert.Equal(
            [
                new InspectorPropertyModel("Count", "2", InspectorPropertyValueKind.Count),
                new InspectorPropertyModel("Context", "hierarchy"),
            ],
            section.Properties);
    }

    [Fact]
    public void Replacing_same_selection_keeps_existing_document_instance()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService, CreateProvider());
        var item = new EditorSelectionItem("scene:test/cube", "mesh", "Cube");
        selectionService.ReplaceSelection("hierarchy", [item]);
        var firstDocument = viewModel.Document;

        selectionService.ReplaceSelection("hierarchy", [item]);

        Assert.Same(firstDocument, viewModel.Document);
    }

    [Fact]
    public void Dispose_unsubscribes_from_selection_changes()
    {
        var selectionService = new EditorSelectionService();
        var viewModel = new InspectorPanelViewModel(selectionService, CreateProvider());
        viewModel.Dispose();

        selectionService.ReplaceSelection(
            "hierarchy",
            [new EditorSelectionItem("scene:test/cube", "mesh", "Cube")]);

        Assert.Null(viewModel.Document);
    }

    private static InMemorySceneSnapshotProvider CreateProvider()
    {
        return new InMemorySceneSnapshotProvider(new SceneSnapshot(
            "scene:test",
            "Test Scene",
            1,
            [
                new SceneObjectSnapshot(
                    "scene:test/cube",
                    "Cube",
                    "mesh",
                    parentId: "scene:test",
                    iconKey: "studio.object.default",
                    properties:
                    [
                        new SceneObjectPropertySnapshot("triangles", "Triangles", "12", SceneObjectPropertyValueKind.Count),
                    ]),
                new SceneObjectSnapshot("scene:test/light", "Light", "light", parentId: "scene:test"),
            ]));
    }

    private static void AssertSingleSelectionDocument(InspectorDocumentModel? document)
    {
        Assert.NotNull(document);
        var actual = document!;
        Assert.False(actual.IsMultiSelection);
        Assert.Equal(1, actual.SelectionCount);
        Assert.Equal("Cube", actual.Title);
        Assert.Equal("mesh", actual.Subtitle);
        Assert.Equal(["Selection", "Properties"], actual.Sections.Select(section => section.Title));
        var selectionSection = actual.Sections[0];
        Assert.Contains(new InspectorPropertyModel("Name", "Cube"), selectionSection.Properties);
        Assert.Contains(new InspectorPropertyModel("Kind", "mesh"), selectionSection.Properties);
        Assert.Contains(new InspectorPropertyModel("Id", "scene:test/cube"), selectionSection.Properties);
        Assert.Contains(new InspectorPropertyModel("Active", "True"), selectionSection.Properties);
        Assert.Contains(new InspectorPropertyModel("Parent", "scene:test"), selectionSection.Properties);
        Assert.Contains(new InspectorPropertyModel("Icon", "studio.object.default"), selectionSection.Properties);
        Assert.Contains(new InspectorPropertyModel("Context", "hierarchy"), selectionSection.Properties);

        var propertySection = actual.Sections[1];
        Assert.Equal(
            [new InspectorPropertyModel("Triangles", "12", InspectorPropertyValueKind.Count)],
            propertySection.Properties);
    }
}
```

- [ ] **Step 2: Run Inspector tests to verify they fail**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter InspectorPanelViewModelTests
```

Expected: FAIL because `InspectorPanelViewModel` does not yet accept `ISceneSnapshotProvider`.

- [ ] **Step 3: Replace InspectorPanelViewModel with provider-backed implementation**

Replace `apps/studio/Features/Inspector/ViewModels/InspectorPanelViewModel.cs` with:

```csharp
using System;
using System.Collections.Generic;
using System.Globalization;
using System.Linq;
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Core.Services;
using Editor.Features.Inspector.Models;
using Editor.Shell.ViewModels;

namespace Editor.Features.Inspector.ViewModels;

public sealed class InspectorPanelViewModel : ViewModelBase, IDisposable
{
    private readonly IEditorSelectionService selectionService_;
    private readonly ISceneSnapshotProvider sceneSnapshotProvider_;
    private EditorSelectionSnapshot currentSelection_;
    private InspectorDocumentModel? document_;

    public InspectorPanelViewModel(IEditorSelectionService selectionService)
        : this(selectionService, new InMemorySceneSnapshotProvider(SceneSnapshot.Empty))
    {
    }

    internal InspectorPanelViewModel(
        IEditorSelectionService selectionService,
        ISceneSnapshotProvider sceneSnapshotProvider)
    {
        ArgumentNullException.ThrowIfNull(selectionService);
        ArgumentNullException.ThrowIfNull(sceneSnapshotProvider);

        selectionService_ = selectionService;
        sceneSnapshotProvider_ = sceneSnapshotProvider;
        currentSelection_ = selectionService.Current;
        document_ = CreateDocument(currentSelection_, sceneSnapshotProvider_);
        selectionService_.SelectionChanged += OnSelectionChanged;
    }

    public EditorSelectionSnapshot CurrentSelection
    {
        get => currentSelection_;
        private set => SetProperty(ref currentSelection_, value);
    }

    public InspectorDocumentModel? Document
    {
        get => document_;
        private set
        {
            if (SetProperty(ref document_, value))
            {
                OnPropertyChanged(nameof(HasDocument));
            }
        }
    }

    public bool HasDocument => Document is not null;

    public void Dispose()
    {
        selectionService_.SelectionChanged -= OnSelectionChanged;
    }

    private void OnSelectionChanged(object? sender, EditorSelectionChangedEventArgs e)
    {
        CurrentSelection = e.Current;
        Document = CreateDocument(e.Current, sceneSnapshotProvider_);
    }

    private static InspectorDocumentModel? CreateDocument(
        EditorSelectionSnapshot selection,
        ISceneSnapshotProvider sceneSnapshotProvider)
    {
        return selection.Items.Count switch
        {
            0 => null,
            1 => CreateSingleSelectionDocument(selection, sceneSnapshotProvider),
            _ => CreateMultiSelectionDocument(selection),
        };
    }

    private static InspectorDocumentModel CreateSingleSelectionDocument(
        EditorSelectionSnapshot selection,
        ISceneSnapshotProvider sceneSnapshotProvider)
    {
        var item = selection.PrimaryItem!;
        if (!sceneSnapshotProvider.TryGetObject(item.Id, out var sceneObject) || sceneObject is null)
        {
            return CreateMissingSelectionDocument(selection, item);
        }

        var selectionProperties = new List<InspectorPropertyModel>
        {
            new("Name", sceneObject.DisplayName),
            new("Kind", sceneObject.Kind),
            new("Id", sceneObject.Id),
            new("Active", sceneObject.IsActive ? "True" : "False"),
        };

        if (!string.IsNullOrWhiteSpace(sceneObject.ParentId))
        {
            selectionProperties.Add(new InspectorPropertyModel("Parent", sceneObject.ParentId));
        }

        if (!string.IsNullOrWhiteSpace(sceneObject.IconKey))
        {
            selectionProperties.Add(new InspectorPropertyModel("Icon", sceneObject.IconKey));
        }

        if (!string.IsNullOrWhiteSpace(selection.ActiveContextId))
        {
            selectionProperties.Add(new InspectorPropertyModel("Context", selection.ActiveContextId));
        }

        var sections = new List<InspectorSectionModel>
        {
            new("Selection", selectionProperties),
        };

        if (sceneObject.Properties.Count > 0)
        {
            sections.Add(new InspectorSectionModel(
                "Properties",
                sceneObject.Properties
                    .Select(property => new InspectorPropertyModel(
                        property.DisplayName,
                        property.Value,
                        MapValueKind(property.ValueKind)))
                    .ToArray()));
        }

        return new InspectorDocumentModel(
            sceneObject.DisplayName,
            sceneObject.Kind,
            1,
            sections);
    }

    private static InspectorDocumentModel CreateMissingSelectionDocument(
        EditorSelectionSnapshot selection,
        EditorSelectionItem item)
    {
        var properties = new List<InspectorPropertyModel>
        {
            new("State", "Missing"),
            new("Id", item.Id),
        };

        if (!string.IsNullOrWhiteSpace(selection.ActiveContextId))
        {
            properties.Add(new InspectorPropertyModel("Context", selection.ActiveContextId));
        }

        return new InspectorDocumentModel(
            item.DisplayName,
            "Missing selection",
            1,
            [new InspectorSectionModel("Validation", properties)]);
    }

    private static InspectorDocumentModel CreateMultiSelectionDocument(EditorSelectionSnapshot selection)
    {
        var count = selection.Items.Count;
        var properties = new List<InspectorPropertyModel>
        {
            new("Count", count.ToString(CultureInfo.InvariantCulture), InspectorPropertyValueKind.Count),
        };

        if (!string.IsNullOrWhiteSpace(selection.ActiveContextId))
        {
            properties.Add(new InspectorPropertyModel("Context", selection.ActiveContextId));
        }

        return new InspectorDocumentModel(
            $"{count} items selected",
            "Multi-selection",
            count,
            [new InspectorSectionModel("Selection", properties)]);
    }

    private static InspectorPropertyValueKind MapValueKind(SceneObjectPropertyValueKind valueKind)
    {
        return valueKind switch
        {
            SceneObjectPropertyValueKind.Count => InspectorPropertyValueKind.Count,
            _ => InspectorPropertyValueKind.Text,
        };
    }
}
```

- [ ] **Step 4: Run Inspector tests to verify they pass**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter InspectorPanelViewModelTests
```

Expected: PASS.

- [ ] **Step 5: Commit Inspector migration**

Run:

```powershell
git add apps/studio/Features/Inspector/ViewModels/InspectorPanelViewModel.cs `
        apps/studio/Tests/Editor.Tests/Features/Inspector/InspectorPanelViewModelTests.cs
git commit -m "refactor: resolve inspector selection from scene snapshot"
```

Expected: commit succeeds.

---

### Task 4: Workbench Wiring And Documentation

**Files:**
- Modify: `apps/studio/Features/Workbench/WorkbenchFeatureModule.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs`
- Modify: `apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`
- Modify: `apps/studio/docs/Dock系统指南.md`

- [ ] **Step 1: Update Workbench tests for shared provider facts**

In `apps/studio/Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs`, replace `RegisterPanels_injects_shared_selection_service_into_selection_panels` with:

```csharp
    [Fact]
    public void RegisterPanels_injects_shared_selection_and_scene_snapshot_provider_into_selection_panels()
    {
        var registry = new PanelRegistry();
        var selectionService = new EditorSelectionService();
        new WorkbenchFeatureModule(selectionService).RegisterPanels(registry);
        var hierarchy = Assert.IsType<HierarchyPanelViewModel>(
            registry.GetRequired("hierarchy").CreateContent());
        var inspector = Assert.IsType<InspectorPanelViewModel>(
            registry.GetRequired("inspector").CreateContent());

        Assert.Contains(hierarchy.Nodes, node => node.Id == "scene:main/cube");
        hierarchy.SelectItem(new EditorSelectionItem("scene:main/cube", "mesh", "Demo Cube"));

        Assert.Equal("hierarchy", inspector.CurrentSelection.ActiveContextId);
        Assert.Equal("Demo Cube", inspector.Document?.Title);
        Assert.Contains(
            inspector.Document?.Sections.SelectMany(section => section.Properties) ?? [],
            property => property.Name == "Id" && property.Value == "scene:main/cube");
    }
```

In `apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs`, replace the selection assertion inside `Default_panel_content_shares_main_window_selection_service` with:

```csharp
        hierarchy.SelectItem(new EditorSelectionItem("scene:main/cube", "mesh", "Demo Cube"));

        Assert.Same(selectionService, viewModel.SelectionService);
        Assert.Equal("hierarchy", inspector.CurrentSelection.ActiveContextId);
        Assert.Equal("Demo Cube", inspector.Document?.Title);
```

- [ ] **Step 2: Run Workbench/MainWindow tests to verify they fail**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~MainWindowViewModelTests"
```

Expected: FAIL because Workbench still creates Hierarchy and Inspector without a shared scene snapshot provider.

- [ ] **Step 3: Replace WorkbenchFeatureModule with provider-backed wiring**

Replace `apps/studio/Features/Workbench/WorkbenchFeatureModule.cs` with:

```csharp
using Editor.Core.Abstractions;
using Editor.Core.Models;
using Editor.Core.Services;
using Editor.Features.Console.ViewModels;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Features.Inspector.ViewModels;
using Editor.Features.Problems.ViewModels;
using Editor.Features.SceneView.ViewModels;

namespace Editor.Features.Workbench;

public sealed class WorkbenchFeatureModule : IEditorFeatureModule
{
    private readonly IEditorSelectionService selectionService_;
    private readonly ISceneSnapshotProvider sceneSnapshotProvider_;

    public WorkbenchFeatureModule(IEditorSelectionService selectionService)
        : this(selectionService, CreateDefaultSceneSnapshotProvider())
    {
    }

    internal WorkbenchFeatureModule(
        IEditorSelectionService selectionService,
        ISceneSnapshotProvider sceneSnapshotProvider)
    {
        selectionService_ = selectionService;
        sceneSnapshotProvider_ = sceneSnapshotProvider;
    }

    public void RegisterPanels(IPanelRegistry panels)
    {
        foreach (var descriptor in CreatePanelDescriptors())
        {
            panels.Register(descriptor);
        }
    }

    public void RegisterActions(IWorkbenchActionRegistry actions)
    {
        foreach (var descriptor in CreatePanelDescriptors())
        {
            actions.Register(new WorkbenchActionDescriptor(
                $"workbench.panel.{descriptor.Id}",
                descriptor.Title,
                WorkbenchActionKind.OpenPanel,
                descriptor.MenuPath,
                TargetId: descriptor.Id,
                IconKey: descriptor.IconKey));
        }
    }

    private PanelDescriptor[] CreatePanelDescriptors()
    {
        return
        [
            new PanelDescriptor(
                "scene-view",
                "Scene View",
                PanelKind.Document,
                DockArea.Center,
                "Window/Panels/Scene View",
                DockContentCachePolicy.KeepAlive,
                () => new SceneViewPanelViewModel(selectionService_),
                IconKey: "studio.scene-view",
                Tag: "DOC",
                TitleDetail: "custom viewport shell",
                StatusText: "live"),
            new PanelDescriptor(
                "hierarchy",
                "Hierarchy",
                PanelKind.Tool,
                DockArea.Left,
                "Window/Panels/Hierarchy",
                DockContentCachePolicy.KeepAlive,
                () => new HierarchyPanelViewModel(selectionService_, sceneSnapshotProvider_),
                IconKey: "studio.hierarchy",
                Tag: "LEFT",
                TitleDetail: "selection source",
                StatusText: "tool"),

            new PanelDescriptor(
                "inspector",
                "Inspector",
                PanelKind.Tool,
                DockArea.Right,
                "Window/Panels/Inspector",
                DockContentCachePolicy.KeepAlive,
                () => new InspectorPanelViewModel(selectionService_, sceneSnapshotProvider_),
                IconKey: "studio.inspector",
                Tag: "RIGHT",
                TitleDetail: "context target",
                StatusText: "tool"),

            new PanelDescriptor(
                "console",
                "Console",
                PanelKind.Tool,
                DockArea.Bottom,
                "Window/Panels/Console",
                DockContentCachePolicy.KeepAlive,
                () => new ConsolePanelViewModel(),
                IconKey: "studio.console",
                Tag: "BOTTOM",
                TitleDetail: "runtime log stream",
                StatusText: "idle"),

            new PanelDescriptor(
                "problems",
                "Problems",
                PanelKind.Tool,
                DockArea.Bottom,
                "Window/Panels/Problems",
                DockContentCachePolicy.KeepAlive,
                () => new ProblemsPanelViewModel(),
                IconKey: "studio.problems",
                Tag: "BOTTOM",
                TitleDetail: "validation queue",
                StatusText: "0"),
        ];
    }

    private static ISceneSnapshotProvider CreateDefaultSceneSnapshotProvider()
    {
        return new InMemorySceneSnapshotProvider(new SceneSnapshot(
            "scene:main",
            "Main Scene",
            1,
            [
                new SceneObjectSnapshot("scene:main", "Main Scene", "scene"),
                new SceneObjectSnapshot("scene:main/camera", "Main Camera", "camera", parentId: "scene:main"),
                new SceneObjectSnapshot("scene:main/key-light", "Key Light", "light", parentId: "scene:main"),
                new SceneObjectSnapshot(
                    "scene:main/cube",
                    "Demo Cube",
                    "mesh",
                    parentId: "scene:main",
                    properties:
                    [
                        new SceneObjectPropertySnapshot("mesh", "Mesh", "Primitive Cube"),
                        new SceneObjectPropertySnapshot("triangles", "Triangles", "12", SceneObjectPropertyValueKind.Count),
                    ]),
                new SceneObjectSnapshot("scene:main/cube/renderer", "Mesh Renderer", "component", parentId: "scene:main/cube"),
                new SceneObjectSnapshot("scene:main/physics-volume", "Physics Volume", "volume", parentId: "scene:main"),
            ]));
    }
}
```

- [ ] **Step 4: Update Dock guide facts**

In `apps/studio/docs/Dock系统指南.md`, add this item after current implemented item 35:

```markdown
36. Scene snapshot provider v0 在 Core 定义只读 scene/object/property snapshot contract，Workbench 以 fixture-backed provider 同时注入 Hierarchy 与 Inspector；Hierarchy 只做树投影和 selection 写入，Inspector 通过 selection id 反查同一 snapshot 生成只读属性文档，不做 Transform 写回或真实 runtime scene 查询
```

Then replace the two follow-up lines under `## 后续切片`:

```markdown
5. Hierarchy provider follow-up：将 fixture-backed scene snapshot provider 替换为真实 scene object provider，并保留 `IEditorSelectionService` 作为面板同步边界。
6. Inspector provider follow-up：扩展真实 scene object / asset provider 的只读属性来源；编辑器控件和写回另做独立切片。
```

- [ ] **Step 5: Run Workbench/MainWindow tests to verify they pass**

Run:

```powershell
dotnet test Tests\Editor.Tests\Editor.Tests.csproj -c Release --filter "FullyQualifiedName~WorkbenchFeatureModuleTests|FullyQualifiedName~MainWindowViewModelTests"
```

Expected: PASS.

- [ ] **Step 6: Commit Workbench wiring and docs**

Run:

```powershell
git add apps/studio/Features/Workbench/WorkbenchFeatureModule.cs `
        apps/studio/Tests/Editor.Tests/Features/Workbench/WorkbenchFeatureModuleTests.cs `
        apps/studio/Tests/Editor.Tests/Shell/ViewModels/MainWindowViewModelTests.cs `
        apps/studio/docs/Dock系统指南.md
git commit -m "feat: wire studio scene snapshot provider"
```

Expected: commit succeeds.

---

### Task 5: Full Verification And Cleanup

**Files:**
- Inspect all changed files from Tasks 1-4.

- [ ] **Step 1: Run full Studio test suite**

Run:

```powershell
dotnet test Editor.sln -c Release
```

Expected: PASS, with all tests passing. At the time this plan was written, Release was the reliable config because live Debug Studio processes can lock `bin\Debug\net10.0\Editor.dll`.

- [ ] **Step 2: Run Debug build/test if the Debug app is closed**

Run:

```powershell
dotnet test Editor.sln
```

Expected: PASS if no running `.NET Host` process is locking `apps/studio/bin/Debug/net10.0/Editor.dll`. If this fails with file-lock errors only, record the lock in the final implementation summary and keep the Release test result as the executed verification.

- [ ] **Step 3: Run repository text and whitespace gates**

Run from `D:\TechArt\VkEngine-studio-frontend`:

```powershell
powershell -ExecutionPolicy Bypass -File tools\check-text-encoding.ps1
git diff --check
```

Expected: both commands pass.

- [ ] **Step 4: Inspect staged scope before final commit**

Run:

```powershell
git status --short
git diff --stat HEAD
```

Expected: only the files listed in this plan changed. No unrelated formatting churn, generated build output, or user changes are included.

- [ ] **Step 5: Commit final cleanup only if needed**

If Task 5 required any small cleanup changes, commit them:

```powershell
git add apps/studio
git commit -m "test: verify studio scene snapshot provider"
```

Expected: commit succeeds. Skip this step if there are no cleanup changes after Tasks 1-4.

## Plan Self-Review

Spec coverage:

- Core provider contract is covered by Task 1.
- Hierarchy provider migration is covered by Task 2.
- Inspector provider lookup and missing selection handling are covered by Task 3.
- Workbench shared provider wiring and doc update are covered by Task 4.
- Verification gates are covered by Task 5.

No placeholders:

- Every task lists concrete files, code snippets, commands and expected results.
- Every task names concrete verification commands and expected outcomes.

Type consistency:

- `ISceneSnapshotProvider`, `SceneSnapshot`, `SceneObjectSnapshot`, `SceneObjectPropertySnapshot`, `SceneObjectPropertyValueKind`, and `InMemorySceneSnapshotProvider` are introduced before any later task references them.
- Hierarchy and Inspector both use the same `ISceneSnapshotProvider` contract.
- Inspector maps `SceneObjectPropertyValueKind.Count` to existing `InspectorPropertyValueKind.Count`.
