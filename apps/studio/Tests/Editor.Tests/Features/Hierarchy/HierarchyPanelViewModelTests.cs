using System;
using System.Collections.Generic;
using Editor.Core.Models;
using Editor.Core.Services;
using Editor.Features.Hierarchy.Models;
using Editor.Features.Hierarchy.ViewModels;
using Editor.Shell.Icons;
using Editor.Shell.Selection;
using Editor.Shell.Services;
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
    public void Snapshot_changed_rebuilds_nodes_from_replacement_snapshot()
    {
        var provider = new InMemorySceneSnapshotProvider(new SceneSnapshot(
            "scene:test",
            "Test Scene",
            1,
            [new SceneObjectSnapshot("scene:test/cube", "Cube", "mesh")]));
        var viewModel = new HierarchyPanelViewModel(
            new EditorSelectionService(),
            provider,
            new CapturingUiDispatcher(hasAccess: true));

        provider.ReplaceSnapshot(new SceneSnapshot(
            "scene:test",
            "Runtime Snapshot",
            2,
            [new SceneObjectSnapshot("scene:test/sphere", "Sphere", "mesh")]));

        Assert.Equal(["Sphere"], GetNodeNames(viewModel.Nodes));
        Assert.Equal(["Sphere"], GetRowNames(viewModel.VisibleRows));
        Assert.Equal("1", viewModel.NodeCountText);
    }

    [Fact]
    public void Snapshot_changed_posts_refresh_when_dispatcher_has_no_access()
    {
        var provider = new InMemorySceneSnapshotProvider(new SceneSnapshot(
            "scene:test",
            "Test Scene",
            1,
            [new SceneObjectSnapshot("scene:test/cube", "Cube", "mesh")]));
        var dispatcher = new CapturingUiDispatcher(hasAccess: false);
        var viewModel = new HierarchyPanelViewModel(
            new EditorSelectionService(),
            provider,
            dispatcher);

        provider.ReplaceSnapshot(new SceneSnapshot(
            "scene:test",
            "Runtime Snapshot",
            2,
            [new SceneObjectSnapshot("scene:test/sphere", "Sphere", "mesh")]));

        Assert.Equal(["Cube"], GetNodeNames(viewModel.Nodes));
        var action = Assert.Single(dispatcher.PostedActions);

        action();

        Assert.Equal(["Sphere"], GetNodeNames(viewModel.Nodes));
    }

    [Fact]
    public void Snapshot_changed_preserves_collapsed_root_state()
    {
        var provider = new InMemorySceneSnapshotProvider(new SceneSnapshot(
            "scene:test",
            "Test Scene",
            1,
            [
                new SceneObjectSnapshot("scene:test", "Scene", "scene"),
                new SceneObjectSnapshot("scene:test/cube", "Cube", "mesh", parentId: "scene:test"),
            ]));
        var viewModel = new HierarchyPanelViewModel(
            new EditorSelectionService(),
            provider,
            new CapturingUiDispatcher(hasAccess: true));
        viewModel.ToggleExpandedCommand.Execute(viewModel.VisibleRows[0]);

        provider.ReplaceSnapshot(new SceneSnapshot(
            "scene:test",
            "Runtime Snapshot",
            2,
            [
                new SceneObjectSnapshot("scene:test", "Scene", "scene"),
                new SceneObjectSnapshot("scene:test/sphere", "Sphere", "mesh", parentId: "scene:test"),
            ]));

        Assert.Equal(["Scene"], GetRowNames(viewModel.VisibleRows));
        Assert.False(viewModel.VisibleRows[0].IsExpanded);
    }

    [Fact]
    public void Dispose_unsubscribes_from_snapshot_changes()
    {
        var provider = new InMemorySceneSnapshotProvider(new SceneSnapshot(
            "scene:test",
            "Test Scene",
            1,
            [new SceneObjectSnapshot("scene:test/cube", "Cube", "mesh")]));
        var viewModel = new HierarchyPanelViewModel(
            new EditorSelectionService(),
            provider,
            new CapturingUiDispatcher(hasAccess: true));
        viewModel.Dispose();

        provider.ReplaceSnapshot(new SceneSnapshot(
            "scene:test",
            "Runtime Snapshot",
            2,
            [new SceneObjectSnapshot("scene:test/sphere", "Sphere", "mesh")]));

        Assert.Equal(["Cube"], GetNodeNames(viewModel.Nodes));
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

    private sealed class CapturingUiDispatcher(bool hasAccess) : IEditorUiDispatcher
    {
        public List<Action> PostedActions { get; } = [];

        public bool CheckAccess() => hasAccess;

        public void Post(Action action)
        {
            PostedActions.Add(action);
        }
    }
}
