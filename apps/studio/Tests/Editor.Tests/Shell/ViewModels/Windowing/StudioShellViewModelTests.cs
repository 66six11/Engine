using System;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.TestSupport;
using Editor.Shell.ViewModels.Windowing;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.Windowing;

public sealed class StudioShellViewModelTests
{
    [Fact]
    public void Starting_transitions_once_to_real_empty_workspace()
    {
        using var viewModel = StudioShellTestFactory.Create();

        Assert.Equal(StudioShellStage.Starting, viewModel.Stage);
        Assert.True(viewModel.IsStarting);
        Assert.False(viewModel.IsWorkspaceVisible);
        Assert.Equal("Starting", viewModel.StartingStateText);

        viewModel.MarkReady();

        Assert.Equal(StudioShellStage.Ready, viewModel.Stage);
        Assert.False(viewModel.IsStarting);
        Assert.True(viewModel.IsWorkspaceVisible);
        Assert.Equal("No Project", viewModel.ProjectStateText);
        Assert.Equal("No Document", viewModel.DocumentStateText);
    }

    [Fact]
    public void Ready_cannot_be_reentered_or_reached_after_shutdown_begins()
    {
        using var ready = StudioShellTestFactory.Create();
        ready.MarkReady();
        Assert.Throws<InvalidOperationException>(ready.MarkReady);

        using var stopping = StudioShellTestFactory.Create();
        stopping.MarkStopping();
        Assert.Equal(StudioShellStage.Stopping, stopping.Stage);
        Assert.Throws<InvalidOperationException>(stopping.MarkReady);
    }

    [Fact]
    public void Disposed_shell_rejects_late_completion()
    {
        var viewModel = StudioShellTestFactory.Create();
        viewModel.Dispose();

        Assert.Throws<ObjectDisposedException>(viewModel.MarkReady);
        Assert.Throws<ObjectDisposedException>(viewModel.MarkStopping);
    }

    [Fact]
    public async Task Create_command_uses_the_selected_parent_and_projects_ready_state()
    {
        using var viewModel = StudioShellTestFactory.Create(
            out var projectSession,
            out var dialogs);
        viewModel.MarkReady();
        viewModel.NewProjectName = "Sample";
        dialogs.ParentDirectory = "C:\\Projects";
        var ready = Ready("Sample", "C:\\Projects\\Sample");
        projectSession.CreateHandler = (parent, name, _) =>
        {
            Assert.Equal("C:\\Projects", parent);
            Assert.Equal("Sample", name);
            projectSession.Publish(ready);
            return ValueTask.FromResult(
                ProjectSessionOperationResult.Success(ready, "Created project 'Sample'."));
        };

        viewModel.CreateProjectCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.True(viewModel.HasProject);
        Assert.False(viewModel.HasNoProject);
        Assert.Equal("Sample", viewModel.ProjectStateText);
        Assert.Equal("C:\\Projects\\Sample", viewModel.ProjectPathText);
        Assert.Equal("Created project 'Sample'.", viewModel.ProjectOperationMessage);
        Assert.Contains("Sample", viewModel.WindowTitle, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Canceled_open_dialog_does_not_call_the_project_session()
    {
        using var viewModel = StudioShellTestFactory.Create(
            out var projectSession,
            out var dialogs);
        viewModel.MarkReady();
        dialogs.ProjectDescriptor = null;
        var called = false;
        projectSession.OpenHandler = (_, _) =>
        {
            called = true;
            throw new InvalidOperationException("Open must not be called.");
        };

        viewModel.OpenProjectCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.False(called);
        Assert.True(viewModel.HasNoProject);
        Assert.Equal(string.Empty, viewModel.ProjectOperationMessage);
    }

    [Fact]
    public async Task Create_mesh_command_uses_typed_validation_asset_and_selects_receipt_identity()
    {
        var initial = Ready("Sample", "C:\\Projects\\Sample");
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();
        var createdObjectId = Guid.NewGuid();
        var trailingObjectId = Guid.NewGuid();
        var createdEntity = new SceneEntitySnapshot(
            createdObjectId,
            new EntityId(1, 1),
            "Directional Wedge",
            TransformValue.Identity,
            SceneMeshReference.DirectionalWedgeValidation);
        var trailingEntity = new SceneEntitySnapshot(
            trailingObjectId,
            new EntityId(2, 1),
            "Trailing Entity",
            TransformValue.Identity);
        var updated = ProjectSessionSnapshot.Ready(
            initial.Project!,
            new SceneDocumentSnapshot(
                initial.Document!.SceneId,
                initial.Document.Path,
                revision: 2,
                savedRevision: 1,
                entities: [createdEntity, trailingEntity]));
        projectSession.CreateMeshEntityHandler = (name, mesh, _) =>
        {
            Assert.Equal("Directional Wedge", name);
            Assert.Equal(SceneMeshReference.DirectionalWedgeValidation, mesh);
            projectSession.Publish(updated);
            return ValueTask.FromResult(
                ProjectSessionOperationResult.Success(
                    updated,
                    "Created a mesh scene entity.",
                    createdObjectId));
        };

        viewModel.CreateMeshEntityCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.Same(createdEntity, viewModel.SelectedEntity);
        Assert.NotEqual(viewModel.SceneEntities[^1].ObjectId, viewModel.SelectedEntity!.ObjectId);
    }

    [Fact]
    public async Task Failed_mesh_creation_preserves_existing_selection()
    {
        var selected = new SceneEntitySnapshot(
            Guid.NewGuid(),
            new EntityId(1, 1),
            "Selected",
            TransformValue.Identity);
        var initialBase = Ready("Sample", "C:\\Projects\\Sample");
        var initial = ProjectSessionSnapshot.Ready(
            initialBase.Project!,
            new SceneDocumentSnapshot(
                initialBase.Document!.SceneId,
                initialBase.Document.Path,
                revision: 1,
                savedRevision: 1,
                entities: [selected]));
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();
        viewModel.SelectedEntity = selected;
        projectSession.CreateMeshEntityHandler = (_, _, _) =>
            ValueTask.FromResult(
                ProjectSessionOperationResult.Failed(
                    initial,
                    ProjectSessionFailureKind.InvalidAssetReference,
                    "Mesh asset reference was rejected."));

        viewModel.CreateMeshEntityCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.Same(selected, viewModel.SelectedEntity);
        Assert.Equal("Mesh asset reference was rejected.", viewModel.ProjectOperationMessage);
    }

    private static ProjectSessionSnapshot Ready(string name, string root) =>
        ProjectSessionSnapshot.Ready(
            new ActiveProjectSnapshot(
                ProjectSessionId.CreateNew(),
                Guid.NewGuid(),
                name,
                root),
            new SceneDocumentSnapshot(
                Guid.NewGuid(),
                $"{root}\\Assets\\Scenes\\Default.asharia.scene.json",
                revision: 1,
                savedRevision: 1,
                entities: []));

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(1));
        while (!condition())
        {
            await Task.Delay(10, timeout.Token);
        }
    }
}
