using System;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Selection;
using Asharia.Studio.TestSupport;
using Avalonia.Headless.XUnit;
using Avalonia.Threading;
using Editor.Shell.ViewModels.Panels;
using Editor.Shell.ViewModels.Windowing;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class StudioScenePanelViewModelTests
{
    [AvaloniaFact]
    public async Task Project_snapshot_reconciles_scene_target_published_for_the_new_scope_first()
    {
        var oldEntity = Entity("Old Entity", new EntityId(1, 1), Float3.Zero);
        var oldSnapshot = Ready(
            ProjectSessionId.CreateNew(),
            Guid.NewGuid(),
            Guid.NewGuid(),
            revision: 1,
            oldEntity);
        var newEntity = Entity("New Entity", new EntityId(2, 1), new Float3(4, 5, 6));
        var newSnapshot = Ready(
            ProjectSessionId.CreateNew(),
            Guid.NewGuid(),
            Guid.NewGuid(),
            revision: 1,
            newEntity);
        var projectSession = new TestProjectSession();
        projectSession.Publish(oldSnapshot);
        var selection = new TestEditorSelectionService();
        var selectionPublishedBeforeShellSnapshotCallback = false;
        projectSession.SnapshotChanged += (_, eventArgs) =>
        {
            if (!ReferenceEquals(eventArgs.Snapshot, newSnapshot))
            {
                return;
            }

            selectionPublishedBeforeShellSnapshotCallback = selection.Replace(
                new SceneObjectSelectionTarget(
                    newSnapshot.Project!.SessionId,
                    newSnapshot.Document!.SceneId,
                    newEntity.ObjectId));
        };
        using var shell = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            StudioShellTestFactory.CreateProjectAssetCatalog(),
            selection);
        shell.MarkReady();
        shell.SelectedEntity = oldEntity;
        shell.InspectorName = "Unsaved old draft";

        await Task.Run(() => projectSession.Publish(newSnapshot));
        Dispatcher.UIThread.RunJobs();

        Assert.True(selectionPublishedBeforeShellSnapshotCallback);
        Assert.Same(newEntity, shell.SelectedEntity);
        Assert.True(shell.IsSceneSelectionPrimary);
        Assert.Equal("New Entity", shell.InspectorName);
        Assert.Equal("4", shell.PositionX);
        Assert.True(shell.ApplyEntityNameCommand.CanExecute(null));
        Assert.True(shell.ApplyEntityTransformCommand.CanExecute(null));
    }

    [AvaloniaFact]
    public async Task Ready_document_creates_and_synchronizes_one_logical_viewport_session()
    {
        var sceneId = Guid.NewGuid();
        var projectSession = new TestProjectSession();
        projectSession.Publish(Ready(sceneId, revision: 1));
        using var shell = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            StudioShellTestFactory.CreateProjectAssetCatalog(),
            StudioShellTestFactory.CreateEditorSelectionService());
        using var panel = new StudioScenePanelViewModel(shell);

        var session = Assert.IsType<
            Asharia.Studio.Application.Viewports.ViewportSession>(panel.Session);
        Assert.Equal((ulong)1, panel.ViewportRevision);
        Assert.True(panel.IsRealtime);
        Assert.False(panel.IsWireframe);

        panel.IsRealtime = false;
        panel.IsWireframe = true;
        Assert.False(panel.IsRealtime);
        Assert.True(panel.IsWireframe);
        Assert.Same(session, panel.Session);

        await Task.Run(() => projectSession.Publish(Ready(sceneId, revision: 2)));
        Dispatcher.UIThread.RunJobs();

        Assert.Same(session, panel.Session);
        Assert.Equal((ulong)2, panel.ViewportRevision);
        Assert.Equal((ulong)2, session.Current.TargetRevision);
        Assert.False(panel.IsRealtime);
        Assert.True(panel.IsWireframe);
        Assert.Equal(
            Asharia.Studio.Application.Viewports.ViewportSceneRasterMode.Wireframe,
            session.TryBeginRender(
                new Asharia.Studio.Application.Viewports.ViewportRenderSize(
                    new Asharia.Studio.Application.Viewports.ViewportExtent(640, 480),
                    new Asharia.Studio.Application.Viewports.ViewportExtent(640, 480)),
                out var wireframeRequest)
                ? wireframeRequest!.SceneRasterMode
                : throw new InvalidOperationException("Wireframe refresh request was not emitted."));
    }

    [AvaloniaFact]
    public async Task Closing_project_closes_and_removes_the_logical_viewport_session()
    {
        var projectSession = new TestProjectSession();
        projectSession.Publish(Ready(Guid.NewGuid(), revision: 1));
        using var shell = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            StudioShellTestFactory.CreateProjectAssetCatalog(),
            StudioShellTestFactory.CreateEditorSelectionService());
        using var panel = new StudioScenePanelViewModel(shell);
        var session = panel.Session!;

        await Task.Run(() => projectSession.Publish(ProjectSessionSnapshot.NoProject));
        Dispatcher.UIThread.RunJobs();

        Assert.Null(panel.Session);
        Assert.True(session.Current.IsClosed);
        Assert.Equal((ulong)0, panel.ViewportRevision);
    }

    private static ProjectSessionSnapshot Ready(Guid sceneId, ulong revision) =>
        Ready(
            ProjectSessionId.CreateNew(),
            Guid.NewGuid(),
            sceneId,
            revision);

    private static ProjectSessionSnapshot Ready(
        ProjectSessionId sessionId,
        Guid projectId,
        Guid sceneId,
        ulong revision,
        params SceneEntitySnapshot[] entities) =>
        ProjectSessionSnapshot.Ready(
            new ActiveProjectSnapshot(
                sessionId,
                projectId,
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

    private static SceneEntitySnapshot Entity(
        string name,
        EntityId runtimeId,
        Float3 position) =>
        new(
            Guid.NewGuid(),
            runtimeId,
            name,
            new TransformValue(position, Quaternion.Identity, Float3.One));
}
