using System;
using System.Linq;
using Asharia.Runtime;
using Asharia.Studio.Application.Assets;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Selection;
using Asharia.Studio.TestSupport;
using Editor.Shell.ViewModels.Panels;
using Editor.Shell.ViewModels.Windowing;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.Panels;

public sealed class StudioHierarchyPanelViewModelTests
{
    [Fact]
    public void Projection_has_one_presentation_root_and_authoritative_entity_rows()
    {
        var sceneId = Guid.NewGuid();
        var camera = Entity("Camera");
        var light = Entity("Light");
        using var shell = CreateShell(
            Ready(sceneId, revision: 1, camera, light),
            out _);

        using var viewModel = new StudioHierarchyPanelViewModel(shell);

        Assert.Equal("2", viewModel.EntityCountText);
        Assert.False(viewModel.IsEmptyStateVisible);
        var rows = viewModel.VisibleRows;
        Assert.Equal(3, rows.Count);

        var root = rows[0];
        Assert.Equal(sceneId, root.StableId);
        Assert.Null(root.Entity);
        Assert.Equal("Default", root.DisplayName);
        Assert.Equal("Scene", root.TypeName);
        Assert.True(root.HasChildren);
        Assert.True(root.IsExpanded);
        Assert.False(root.ShowIndentGuide);

        Assert.Same(camera, rows[1].Entity);
        Assert.Equal(camera.ObjectId, rows[1].StableId);
        Assert.Equal("Entity", rows[1].TypeName);
        Assert.True(rows[1].ShowIndentGuide);
        Assert.False(rows[1].IsLastSibling);
        Assert.Equal(20d, rows[1].GuideHeight);

        Assert.Same(light, rows[2].Entity);
        Assert.True(rows[2].IsLastSibling);
        Assert.Equal(10d, rows[2].GuideHeight);
    }

    [Fact]
    public void Filter_is_case_insensitive_reports_visible_count_and_preserves_hidden_selection()
    {
        var camera = Entity("Main Camera");
        var light = Entity("Key Light");
        using var shell = CreateShell(
            Ready(Guid.NewGuid(), revision: 1, camera, light),
            out _);
        shell.SelectedEntity = light;
        using var viewModel = new StudioHierarchyPanelViewModel(shell);

        viewModel.FilterText = "CAMERA";

        Assert.Equal("1/2", viewModel.EntityCountText);
        Assert.Equal([camera.ObjectId], viewModel.VisibleRows
            .Where(row => row.Entity is not null)
            .Select(row => row.StableId)
            .ToArray());
        Assert.Null(viewModel.SelectedRow);
        Assert.Same(light, shell.SelectedEntity);
        Assert.Equal("Key Light", shell.InspectorName);

        viewModel.SelectedRow = null;
        Assert.Same(light, shell.SelectedEntity);

        viewModel.FilterText = string.Empty;

        Assert.Equal("2", viewModel.EntityCountText);
        Assert.Equal(light.ObjectId, viewModel.SelectedRow?.StableId);
        Assert.Same(light, viewModel.SelectedRow?.Entity);
        Assert.Same(light, shell.SelectedEntity);
    }

    [Fact]
    public void Selecting_a_visible_row_updates_the_shell_and_inspector_by_stable_id()
    {
        var camera = Entity("Main Camera");
        var light = Entity("Key Light");
        using var shell = CreateShell(
            Ready(Guid.NewGuid(), revision: 1, camera, light),
            out _);
        using var viewModel = new StudioHierarchyPanelViewModel(shell);

        viewModel.SelectedRow = viewModel.VisibleRows.Single(
            row => row.StableId == light.ObjectId);

        Assert.Equal(light.ObjectId, shell.SelectedEntity?.ObjectId);
        Assert.Equal("Key Light", shell.InspectorName);
    }

    [Fact]
    public void Asset_selection_clears_scene_highlight_and_reclick_restores_scene_target()
    {
        var camera = Entity("Main Camera");
        using var shell = CreateShell(
            Ready(Guid.NewGuid(), revision: 1, camera),
            out _);
        using var viewModel = new StudioHierarchyPanelViewModel(shell);
        var row = viewModel.VisibleRows.Single(
            candidate => candidate.StableId == camera.ObjectId);
        viewModel.SelectedRow = row;
        Assert.IsType<SceneObjectSelectionTarget>(shell.EditorSelection.Current.Primary);
        var project = shell.AppliedProjectSnapshot.Project!;

        shell.EditorSelection.Replace(new AssetSelectionTarget(
            project.SessionId,
            project.ProjectId,
            "editor-preview",
            new AssetSelectionKey(Guid.NewGuid(), "Assets/Model.glb")));

        Assert.Null(viewModel.SelectedRow);
        Assert.Same(camera, shell.SelectedEntity);

        viewModel.SelectedRow = row;

        var restored = Assert.IsType<SceneObjectSelectionTarget>(
            shell.EditorSelection.Current.Primary);
        Assert.Equal(camera.ObjectId, restored.ObjectId);
    }

    [Fact]
    public void Explicitly_clearing_a_visible_row_clears_shell_and_inspector_selection()
    {
        var camera = Entity("Main Camera");
        using var shell = CreateShell(
            Ready(Guid.NewGuid(), revision: 1, camera),
            out _);
        using var viewModel = new StudioHierarchyPanelViewModel(shell);
        viewModel.SelectedRow = viewModel.VisibleRows.Single(
            row => row.StableId == camera.ObjectId);

        viewModel.SelectedRow = null;

        Assert.Null(shell.SelectedEntity);
        Assert.Null(viewModel.SelectedRow);
        Assert.Equal(string.Empty, shell.InspectorName);
    }

    [Fact]
    public void Expansion_is_panel_local_and_filter_temporarily_reveals_matches()
    {
        var camera = Entity("Camera");
        using var shell = CreateShell(
            Ready(Guid.NewGuid(), revision: 1, camera),
            out _);
        using var viewModel = new StudioHierarchyPanelViewModel(shell);
        var root = viewModel.VisibleRows[0];

        viewModel.ToggleExpanded(root);

        Assert.False(viewModel.IsSceneExpanded);
        Assert.Single(viewModel.VisibleRows);
        Assert.False(viewModel.VisibleRows[0].IsExpanded);

        viewModel.FilterText = "Camera";

        Assert.False(viewModel.IsSceneExpanded);
        Assert.Equal(2, viewModel.VisibleRows.Count);
        Assert.True(viewModel.VisibleRows[0].IsExpanded);

        viewModel.FilterText = string.Empty;

        Assert.Single(viewModel.VisibleRows);
        Assert.False(viewModel.VisibleRows[0].IsExpanded);
    }

    [Fact]
    public void Empty_states_distinguish_no_scene_from_no_filter_matches()
    {
        using (var shell = CreateShell(ProjectSessionSnapshot.NoProject, out _))
        using (var viewModel = new StudioHierarchyPanelViewModel(shell))
        {
            Assert.Empty(viewModel.VisibleRows);
            Assert.True(viewModel.IsEmptyStateVisible);
            Assert.Equal("No scene loaded", viewModel.EmptyStateText);
        }

        using var readyShell = CreateShell(
            Ready(Guid.NewGuid(), revision: 1, Entity("Camera")),
            out _);
        using var readyViewModel = new StudioHierarchyPanelViewModel(readyShell);
        readyViewModel.FilterText = "does-not-exist";

        Assert.Empty(readyViewModel.VisibleRows);
        Assert.True(readyViewModel.IsEmptyStateVisible);
        Assert.Equal("No matching objects", readyViewModel.EmptyStateText);
        Assert.Equal("0/1", readyViewModel.EntityCountText);
    }

    private static StudioShellViewModel CreateShell(
        ProjectSessionSnapshot initialSnapshot,
        out TestProjectSession projectSession)
    {
        projectSession = new TestProjectSession();
        projectSession.Publish(initialSnapshot);
        return new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            StudioShellTestFactory.CreateProjectAssetCatalog(),
            StudioShellTestFactory.CreateEditorSelectionService());
    }

    private static SceneEntitySnapshot Entity(string name) =>
        Entity(Guid.NewGuid(), name);

    private static SceneEntitySnapshot Entity(Guid objectId, string name) =>
        new(
            objectId,
            new EntityId((uint)(name.GetHashCode() & int.MaxValue) + 1U, 1U),
            name,
            TransformValue.Identity);

    private static ProjectSessionSnapshot Ready(
        Guid sceneId,
        ulong revision,
        params SceneEntitySnapshot[] entities) =>
        Ready(ProjectSessionId.CreateNew(), sceneId, revision, entities);

    private static ProjectSessionSnapshot Ready(
        ProjectSessionId sessionId,
        Guid sceneId,
        ulong revision,
        params SceneEntitySnapshot[] entities) =>
        ProjectSessionSnapshot.Ready(
            new ActiveProjectSnapshot(
                sessionId,
                Guid.NewGuid(),
                "Sample",
                "C:\\Projects\\Sample"),
            new SceneDocumentSnapshot(
                sceneId,
                "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
                revision,
                savedRevision: 1,
                entities),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
}
