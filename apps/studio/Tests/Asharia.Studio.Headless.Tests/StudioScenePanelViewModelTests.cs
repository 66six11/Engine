using System;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Selection;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
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
    public void Presented_model_body_pick_updates_typed_selection_away_from_transform_axes()
    {
        var projectSessionId = ProjectSessionId.CreateNew();
        var sceneId = Guid.NewGuid();
        var entity = new SceneEntitySnapshot(
            Guid.NewGuid(),
            new EntityId(1, 1),
            "Model Pick Target",
            TransformValue.Identity,
            SceneMeshReference.DirectionalWedgeValidation);
        var projectSnapshot = Ready(
            projectSessionId,
            Guid.NewGuid(),
            sceneId,
            revision: 3,
            entity);
        var projectSession = new TestProjectSession();
        projectSession.Publish(projectSnapshot);
        var selection = new TestEditorSelectionService();
        using var shell = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            StudioShellTestFactory.CreateProjectAssetCatalog(),
            selection);
        using var panel = new StudioScenePanelViewModel(shell);
        var session = Assert.IsType<ViewportSession>(panel.Session);
        session.SetCamera(new ViewportCameraSnapshot(
            new Float3(0, -5, 0),
            Float3.Zero,
            new Float3(0, 0, 1),
            MathF.PI / 2,
            ViewportFieldOfViewAxis.MaintainHorizontal,
            0.1f,
            1000.0f));
        var extent = new ViewportExtent(800, 600);
        Assert.True(session.TryPublishLatest(
            new ViewportRenderSize(extent, extent),
            out var presented));

        Assert.True(panel.TryApplyViewportPick(
            new ViewportPresentedInteractionContext(
                session.Current.SessionId,
                sceneId,
                TargetRevision: 3,
                FrameSequence: presented.Sequence,
                extent,
                RenderScaling: 1.0),
            new ViewportPickRequest(
                extent,
                new ViewportPickPoint(460, 332),
                tolerancePixels: 6)));

        var target = Assert.IsType<SceneObjectSelectionTarget>(selection.Current.Primary);
        Assert.Equal(entity.ObjectId, target.ObjectId);
        Assert.Same(entity, shell.SelectedEntity);
        Assert.Equal("Model Pick Target", shell.InspectorName);
        Assert.Same(projectSnapshot, projectSession.Current);
        Assert.False(projectSession.Current.IsDirty);
        Assert.False(projectSession.Current.CanUndo);
        Assert.False(projectSession.Current.CanRedo);
    }

    [AvaloniaFact]
    public void Presented_transform_proxy_pick_updates_typed_selection_without_editing_document()
    {
        var projectSessionId = ProjectSessionId.CreateNew();
        var sceneId = Guid.NewGuid();
        var entity = Entity("Pick Target", new EntityId(1, 1), Float3.Zero);
        var projectSnapshot = Ready(
            projectSessionId,
            Guid.NewGuid(),
            sceneId,
            revision: 3,
            entity);
        var projectSession = new TestProjectSession();
        projectSession.Publish(projectSnapshot);
        var selection = new TestEditorSelectionService();
        using var shell = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            StudioShellTestFactory.CreateProjectAssetCatalog(),
            selection);
        using var panel = new StudioScenePanelViewModel(shell);
        var session = Assert.IsType<ViewportSession>(panel.Session);
        var extent = new ViewportExtent(800, 600);
        Assert.True(session.TryPublishLatest(
            new ViewportRenderSize(extent, extent),
            out var presented));
        var context = new ViewportPresentedInteractionContext(
            session.Current.SessionId,
            sceneId,
            TargetRevision: 3,
            FrameSequence: presented.Sequence,
            extent,
            RenderScaling: 1.0);

        Assert.True(panel.TryApplyViewportPick(
            context,
            new ViewportPickRequest(
                extent,
                new ViewportPickPoint(400, 300),
                tolerancePixels: 6)));

        var target = Assert.IsType<SceneObjectSelectionTarget>(selection.Current.Primary);
        Assert.Equal(projectSessionId, target.SessionId);
        Assert.Equal(sceneId, target.SceneId);
        Assert.Equal(entity.ObjectId, target.ObjectId);
        Assert.Same(entity, shell.SelectedEntity);
        Assert.True(shell.IsSceneSelectionPrimary);
        Assert.Equal("Pick Target", shell.InspectorName);
        Assert.Same(projectSnapshot, projectSession.Current);
        Assert.False(projectSession.Current.IsDirty);
        Assert.False(projectSession.Current.CanUndo);
        Assert.False(projectSession.Current.CanRedo);
    }

    [AvaloniaFact]
    public void Presented_blank_pick_clears_selection()
    {
        var projectSessionId = ProjectSessionId.CreateNew();
        var sceneId = Guid.NewGuid();
        var entity = Entity("Pick Target", new EntityId(1, 1), Float3.Zero);
        var projectSession = new TestProjectSession();
        projectSession.Publish(Ready(
            projectSessionId,
            Guid.NewGuid(),
            sceneId,
            revision: 1,
            entity));
        var selection = new TestEditorSelectionService();
        Assert.True(selection.Replace(new SceneObjectSelectionTarget(
            projectSessionId,
            sceneId,
            entity.ObjectId)));
        using var shell = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            StudioShellTestFactory.CreateProjectAssetCatalog(),
            selection);
        using var panel = new StudioScenePanelViewModel(shell);
        var session = Assert.IsType<ViewportSession>(panel.Session);
        var extent = new ViewportExtent(800, 600);
        Assert.True(session.TryPublishLatest(
            new ViewportRenderSize(extent, extent),
            out var presented));

        Assert.True(panel.TryApplyViewportPick(
            new ViewportPresentedInteractionContext(
                session.Current.SessionId,
                sceneId,
                TargetRevision: 1,
                FrameSequence: presented.Sequence,
                extent,
                RenderScaling: 1.0),
            new ViewportPickRequest(
                extent,
                new ViewportPickPoint(24, 24),
                tolerancePixels: 6)));

        Assert.Null(selection.Current.Primary);
        Assert.Equal(EditorSelectionChangeReason.User, selection.Current.Reason);
        Assert.Null(shell.SelectedEntity);
        Assert.False(shell.IsSceneSelectionPrimary);
    }

    [AvaloniaFact]
    public void Stale_presented_identity_is_rejected_without_changing_selection()
    {
        var projectSessionId = ProjectSessionId.CreateNew();
        var sceneId = Guid.NewGuid();
        var entity = Entity("Pick Target", new EntityId(1, 1), Float3.Zero);
        var projectSession = new TestProjectSession();
        projectSession.Publish(Ready(
            projectSessionId,
            Guid.NewGuid(),
            sceneId,
            revision: 2,
            entity));
        var selection = new TestEditorSelectionService();
        Assert.True(selection.Replace(new SceneObjectSelectionTarget(
            projectSessionId,
            sceneId,
            entity.ObjectId)));
        var selectionBeforePick = selection.Current;
        using var shell = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            StudioShellTestFactory.CreateProjectAssetCatalog(),
            selection);
        using var panel = new StudioScenePanelViewModel(shell);
        var session = Assert.IsType<ViewportSession>(panel.Session);
        var extent = new ViewportExtent(800, 600);
        Assert.True(session.TryPublishLatest(
            new ViewportRenderSize(extent, extent),
            out var presented));

        Assert.False(panel.TryApplyViewportPick(
            new ViewportPresentedInteractionContext(
                session.Current.SessionId,
                sceneId,
                TargetRevision: 1,
                FrameSequence: presented.Sequence,
                extent,
                RenderScaling: 1.0),
            new ViewportPickRequest(
                extent,
                new ViewportPickPoint(24, 24),
                tolerancePixels: 6)));

        Assert.Same(selectionBeforePick, selection.Current);
    }

    [AvaloniaFact]
    public void Camera_navigation_updates_only_transient_viewport_state()
    {
        var projectSessionId = ProjectSessionId.CreateNew();
        var sceneId = Guid.NewGuid();
        var entity = Entity("Selected", new EntityId(1, 1), Float3.Zero);
        var projectSnapshot = Ready(
            projectSessionId,
            Guid.NewGuid(),
            sceneId,
            revision: 3,
            entity);
        var projectSession = new TestProjectSession();
        projectSession.Publish(projectSnapshot);
        var selection = new TestEditorSelectionService();
        Assert.True(selection.Replace(new SceneObjectSelectionTarget(
            projectSessionId,
            sceneId,
            entity.ObjectId)));
        var selectionBefore = selection.Current;
        using var shell = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            StudioShellTestFactory.CreateProjectAssetCatalog(),
            selection);
        using var panel = new StudioScenePanelViewModel(shell);
        var session = Assert.IsType<ViewportSession>(panel.Session);
        var cameraBefore = session.Camera;

        Assert.True(panel.TryApplyCameraNavigation(new ViewportCameraNavigationDelta(
            ViewportCameraNavigationMode.Orbit,
            horizontalFraction: 0.25f,
            verticalFraction: 0.10f,
            aspectRatio: 4.0f / 3.0f)));

        Assert.NotSame(cameraBefore, session.Camera);
        Assert.NotEqual(cameraBefore.Position, session.Camera.Position);
        Assert.Same(projectSnapshot, projectSession.Current);
        Assert.Equal((ulong)3, projectSession.Current.Document!.Revision);
        Assert.False(projectSession.Current.IsDirty);
        Assert.False(projectSession.Current.CanUndo);
        Assert.False(projectSession.Current.CanRedo);
        Assert.Same(selectionBefore, selection.Current);
        Assert.True((session.Current.PendingReasons & ViewportInvalidationReason.CameraChanged) != 0);
    }

    [AvaloniaFact]
    public void Camera_navigation_without_a_scene_session_is_rejected()
    {
        var projectSession = new TestProjectSession();
        using var shell = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            StudioShellTestFactory.CreateProjectAssetCatalog(),
            StudioShellTestFactory.CreateEditorSelectionService());
        using var panel = new StudioScenePanelViewModel(shell);

        Assert.False(panel.TryApplyCameraNavigation(new ViewportCameraNavigationDelta(
            ViewportCameraNavigationMode.Dolly,
            horizontalFraction: 0,
            verticalFraction: -0.12f,
            aspectRatio: 1.0f)));
    }

    [AvaloniaFact]
    public async Task Translate_gizmo_previews_transiently_and_commits_one_project_edit_on_release()
    {
        var projectSessionId = ProjectSessionId.CreateNew();
        var sceneId = Guid.NewGuid();
        var entity = Entity("Selected", new EntityId(1, 1), Float3.Zero);
        var original = Ready(
            projectSessionId,
            Guid.NewGuid(),
            sceneId,
            revision: 3,
            entity);
        var selection = new TestEditorSelectionService();
        Assert.True(selection.Replace(new SceneObjectSelectionTarget(
            projectSessionId,
            sceneId,
            entity.ObjectId)));
        var projectSession = new TestProjectSession();
        projectSession.Publish(original);
        var callCount = 0;
        ProjectSessionEditContext capturedContext = default;
        TransformValue capturedTransform = default;
        projectSession.SetTransformHandler = (objectId, transform, context, _) =>
        {
            callCount++;
            Assert.Equal(entity.ObjectId, objectId);
            capturedContext = context;
            capturedTransform = transform;
            var movedEntity = new SceneEntitySnapshot(
                entity.ObjectId,
                entity.RuntimeEntityId,
                entity.Name,
                transform);
            return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                Ready(projectSessionId, original.Project!.ProjectId, sceneId, 4, movedEntity),
                "Moved selected entity.",
                originatingEditId: context.EditId));
        };
        using var shell = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            StudioShellTestFactory.CreateProjectAssetCatalog(),
            selection);
        using var panel = new StudioScenePanelViewModel(shell);
        var session = Assert.IsType<ViewportSession>(panel.Session);
        session.SetCamera(GizmoCamera());
        var extent = new ViewportExtent(800, 600);
        Assert.True(session.TryPublishLatest(
            new ViewportRenderSize(extent, extent),
            out var presented));
        var context = PresentedContext(session, sceneId, revision: 3, presented, extent);

        Assert.True(panel.TryBeginTranslateGizmo(
            context,
            new ViewportPickRequest(extent, new ViewportPickPoint(460, 300), 8)));
        Assert.True(panel.TryUpdateTranslateGizmo(new ViewportPickPoint(500, 300)));
        Assert.Equal(0, callCount);
        Assert.Same(original, projectSession.Current);
        Assert.True(session.TryPublishLatest(
            new ViewportRenderSize(extent, extent),
            out var preview));
        Assert.InRange(preview.TranslateGizmo!.Transform.Position.X, 0.9999f, 1.0001f);

        Assert.True(await panel.CompleteTranslateGizmoAsync());

        Assert.Equal(1, callCount);
        Assert.Equal((ulong)3, capturedContext.ExpectedRevision);
        Assert.True(capturedContext.EditId.IsValid);
        Assert.InRange(capturedTransform.Position.X, 0.9999f, 1.0001f);
        Assert.Equal("Moved selected entity.", shell.ProjectOperationMessage);
        Assert.Equal((ulong)4, panel.Session!.Current.TargetRevision);
    }

    [AvaloniaFact]
    public async Task Translate_gizmo_noop_and_cancel_do_not_mutate_the_project()
    {
        var projectSessionId = ProjectSessionId.CreateNew();
        var sceneId = Guid.NewGuid();
        var entity = Entity("Selected", new EntityId(1, 1), Float3.Zero);
        var original = Ready(
            projectSessionId,
            Guid.NewGuid(),
            sceneId,
            revision: 3,
            entity);
        var selection = new TestEditorSelectionService();
        Assert.True(selection.Replace(new SceneObjectSelectionTarget(
            projectSessionId,
            sceneId,
            entity.ObjectId)));
        var projectSession = new TestProjectSession();
        projectSession.Publish(original);
        var callCount = 0;
        projectSession.SetTransformHandler = (_, _, _, _) =>
        {
            callCount++;
            return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                original,
                "Unexpected mutation."));
        };
        using var shell = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            StudioShellTestFactory.CreateProjectAssetCatalog(),
            selection);
        using var panel = new StudioScenePanelViewModel(shell);
        var session = Assert.IsType<ViewportSession>(panel.Session);
        session.SetCamera(GizmoCamera());
        var extent = new ViewportExtent(800, 600);
        var size = new ViewportRenderSize(extent, extent);
        Assert.True(session.TryPublishLatest(size, out var first));

        Assert.True(panel.TryBeginTranslateGizmo(
            PresentedContext(session, sceneId, 3, first, extent),
            new ViewportPickRequest(extent, new ViewportPickPoint(460, 300), 8)));
        Assert.True(await panel.CompleteTranslateGizmoAsync());
        Assert.Equal(0, callCount);

        Assert.True(session.TryPublishLatest(size, out var second));
        Assert.True(panel.TryBeginTranslateGizmo(
            PresentedContext(session, sceneId, 3, second, extent),
            new ViewportPickRequest(extent, new ViewportPickPoint(460, 300), 8)));
        Assert.True(panel.TryUpdateTranslateGizmo(new ViewportPickPoint(500, 300)));
        panel.CancelTranslateGizmo();

        Assert.Equal(0, callCount);
        Assert.True(session.TryPublishLatest(size, out var cancelled));
        Assert.Equal(TransformValue.Identity, cancelled.TranslateGizmo!.Transform);
        Assert.Equal(ViewportGizmoAxis.None, cancelled.TranslateGizmo.ActiveAxis);
    }

    [AvaloniaFact]
    public async Task Failed_translate_gizmo_commit_rolls_preview_back_to_authoritative_state()
    {
        var projectSessionId = ProjectSessionId.CreateNew();
        var sceneId = Guid.NewGuid();
        var entity = Entity("Selected", new EntityId(1, 1), Float3.Zero);
        var original = Ready(
            projectSessionId,
            Guid.NewGuid(),
            sceneId,
            revision: 3,
            entity);
        var selection = new TestEditorSelectionService();
        Assert.True(selection.Replace(new SceneObjectSelectionTarget(
            projectSessionId,
            sceneId,
            entity.ObjectId)));
        var projectSession = new TestProjectSession();
        projectSession.Publish(original);
        projectSession.SetTransformHandler = (_, _, context, _) =>
            ValueTask.FromResult(ProjectSessionOperationResult.Failed(
                original,
                ProjectSessionFailureKind.RevisionConflict,
                "Scene changed before the move could be committed.",
                context.EditId));
        using var shell = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            StudioShellTestFactory.CreateProjectAssetCatalog(),
            selection);
        using var panel = new StudioScenePanelViewModel(shell);
        var session = Assert.IsType<ViewportSession>(panel.Session);
        session.SetCamera(GizmoCamera());
        var extent = new ViewportExtent(800, 600);
        var size = new ViewportRenderSize(extent, extent);
        Assert.True(session.TryPublishLatest(size, out var presented));
        Assert.True(panel.TryBeginTranslateGizmo(
            PresentedContext(session, sceneId, 3, presented, extent),
            new ViewportPickRequest(extent, new ViewportPickPoint(460, 300), 8)));
        Assert.True(panel.TryUpdateTranslateGizmo(new ViewportPickPoint(500, 300)));

        Assert.False(await panel.CompleteTranslateGizmoAsync());

        Assert.Equal("Scene changed before the move could be committed.",
            shell.ProjectOperationMessage);
        Assert.True(session.TryPublishLatest(size, out var rolledBack));
        Assert.Equal(TransformValue.Identity, rolledBack.TranslateGizmo!.Transform);
    }

    [AvaloniaFact]
    public async Task Document_drift_cancels_translate_gizmo_without_committing_an_edit()
    {
        var projectSessionId = ProjectSessionId.CreateNew();
        var projectId = Guid.NewGuid();
        var sceneId = Guid.NewGuid();
        var entity = Entity("Selected", new EntityId(1, 1), Float3.Zero);
        var original = Ready(projectSessionId, projectId, sceneId, revision: 3, entity);
        var selection = new TestEditorSelectionService();
        Assert.True(selection.Replace(new SceneObjectSelectionTarget(
            projectSessionId,
            sceneId,
            entity.ObjectId)));
        var projectSession = new TestProjectSession();
        projectSession.Publish(original);
        var callCount = 0;
        projectSession.SetTransformHandler = (_, _, _, _) =>
        {
            callCount++;
            return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                original,
                "Unexpected mutation."));
        };
        using var shell = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService(),
            StudioShellTestFactory.CreateDocumentTransitions(projectSession),
            StudioShellTestFactory.CreateDiagnosticWriter(),
            StudioShellTestFactory.CreateProjectAssetCatalog(),
            selection);
        using var panel = new StudioScenePanelViewModel(shell);
        var session = Assert.IsType<ViewportSession>(panel.Session);
        session.SetCamera(GizmoCamera());
        var extent = new ViewportExtent(800, 600);
        var size = new ViewportRenderSize(extent, extent);
        Assert.True(session.TryPublishLatest(size, out var presented));
        Assert.True(panel.TryBeginTranslateGizmo(
            PresentedContext(session, sceneId, 3, presented, extent),
            new ViewportPickRequest(extent, new ViewportPickPoint(460, 300), 8)));
        Assert.True(panel.TryUpdateTranslateGizmo(new ViewportPickPoint(500, 300)));

        projectSession.Publish(Ready(projectSessionId, projectId, sceneId, revision: 4, entity));

        Assert.False(await panel.CompleteTranslateGizmoAsync());
        Assert.Equal(0, callCount);
        Assert.True(session.TryPublishLatest(size, out var synchronized));
        Assert.Equal(TransformValue.Identity, synchronized.TranslateGizmo!.Transform);
    }

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

    private static ViewportCameraSnapshot GizmoCamera() => new(
        new Float3(0, 0, -10),
        Float3.Zero,
        new Float3(0, 1, 0),
        MathF.PI / 2,
        ViewportFieldOfViewAxis.MaintainHorizontal,
        0.1f,
        1000.0f);

    private static ViewportPresentedInteractionContext PresentedContext(
        ViewportSession session,
        Guid sceneId,
        ulong revision,
        ViewportRenderRequest presented,
        ViewportExtent extent) =>
        new(
            session.Current.SessionId,
            sceneId,
            revision,
            presented.Sequence,
            extent,
            RenderScaling: 1.0);
}
