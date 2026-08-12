using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Xunit;

namespace Asharia.Studio.Application.Tests.Projects;

public sealed class ProjectSessionTests
{
    [Fact]
    public async Task Create_publishes_ready_only_after_default_scene_is_open()
    {
        var projectId = Guid.NewGuid();
        var projectGateway = new ControlledProjectGateway
        {
            CreateResult = ProjectDescriptorOperationResult.Success(
                new ProjectDescriptorSnapshot("C:\\Projects\\Sample", "Sample", projectId)),
        };
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        var changed = 0;
        session.SnapshotChanged += (_, _) => changed++;

        var result = await session.CreateProjectAsync("C:\\Projects", "Sample");

        Assert.True(result.Succeeded);
        Assert.True(result.Current.IsReady);
        Assert.Equal(projectId, result.Current.Project!.ProjectId);
        Assert.Equal(sceneGateway.Connection.Current, result.Current.Document);
        Assert.Equal(result.Current, session.Current);
        Assert.True(result.Current.Project.SessionId.IsValid);
        Assert.Equal(1, changed);
        Assert.NotEqual(Guid.Empty, projectGateway.LastCreateProjectId);
        Assert.NotEqual(Guid.Empty, sceneGateway.LastNewSceneId);
    }

    [Fact]
    public async Task Failed_scene_open_preserves_the_last_ready_project_and_document()
    {
        var first = new ProjectDescriptorSnapshot(
            "C:\\Projects\\First",
            "First",
            Guid.NewGuid());
        var projectGateway = new ControlledProjectGateway
        {
            OpenResult = ProjectDescriptorOperationResult.Success(first),
        };
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        var opened = await session.OpenProjectAsync(first.RootPath);
        sceneGateway.OpenFailure = new SceneDocumentFailure(
            SceneDocumentFailureKind.InvalidScene,
            "The default scene is invalid.");

        var failed = await session.OpenProjectAsync("C:\\Broken");

        Assert.False(failed.Succeeded);
        Assert.Equal(ProjectSessionFailureKind.InvalidScene, failed.FailureKind);
        Assert.Same(opened.Current, failed.Current);
        Assert.Same(opened.Current, session.Current);
        Assert.Equal(0, sceneGateway.Connection.DisposeCount);
    }

    [Fact]
    public async Task Entity_edits_and_save_publish_authoritative_dirty_state()
    {
        var projectGateway = new ControlledProjectGateway
        {
            OpenResult = ProjectDescriptorOperationResult.Success(
                new ProjectDescriptorSnapshot(
                    "C:\\Projects\\Sample",
                    "Sample",
                    Guid.NewGuid())),
        };
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");

        var created = await session.CreateEntityAsync("Entity");
        var objectId = created.Current.Document!.Entities.Single().ObjectId;
        var renamed = await session.SetEntityNameAsync(objectId, "主角");
        var editId = ProjectEditId.CreateNew();
        var moved = await session.SetEntityTransformAsync(
            objectId,
            new TransformValue(
                new Float3(1, 2, 3),
                Quaternion.Identity,
                new Float3(2, 2, 2)),
            new ProjectSessionEditContext(
                editId,
                renamed.Current.Document!.Revision));
        var saved = await session.SaveSceneAsync();

        Assert.True(created.Succeeded);
        Assert.Equal(objectId, created.CreatedObjectId!.Value);
        Assert.True(renamed.Succeeded);
        Assert.True(moved.Succeeded);
        Assert.Equal(editId, moved.OriginatingEditId);
        Assert.Equal(
            renamed.Current.Document!.Revision,
            sceneGateway.Connection.LastSetTransformExpectedRevision);
        Assert.True(created.Current.IsDirty);
        Assert.Equal("主角", moved.Current.Document!.Entities.Single().Name);
        Assert.Equal(new Float3(1, 2, 3), moved.Current.Document.Entities.Single().Transform.Position);
        Assert.False(saved.Current.IsDirty);
        Assert.Equal(saved.Current.Document!.Revision, saved.Current.Document.SavedRevision);
    }

    [Fact]
    public async Task Transform_edit_publishes_and_returns_its_originating_edit_id()
    {
        var projectGateway = new ControlledProjectGateway
        {
            OpenResult = ProjectDescriptorOperationResult.Success(
                new ProjectDescriptorSnapshot(
                    "C:\\Projects\\Sample",
                    "Sample",
                    Guid.NewGuid())),
        };
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");
        var created = await session.CreateEntityAsync("Entity");
        var objectId = created.Current.Document!.Entities.Single().ObjectId;
        var editId = ProjectEditId.CreateNew();
        ProjectSessionSnapshotChangedEventArgs? published = null;
        session.SnapshotChanged += (_, args) => published = args;

        var result = await session.SetEntityTransformAsync(
            objectId,
            new TransformValue(
                new Float3(1, 2, 3),
                Quaternion.Identity,
                Float3.One),
            new ProjectSessionEditContext(editId, created.Current.Document.Revision));

        Assert.True(result.Succeeded);
        Assert.Equal(editId, result.OriginatingEditId);
        Assert.NotNull(published);
        Assert.Same(result.Current, published!.Snapshot);
        Assert.Equal(editId, published.OriginatingEditId);
        Assert.True(published.OriginatingEditSucceeded);
    }

    [Fact]
    public async Task Transform_revision_conflict_preserves_edit_origin_for_reconciliation()
    {
        var projectGateway = new ControlledProjectGateway
        {
            OpenResult = ProjectDescriptorOperationResult.Success(
                new ProjectDescriptorSnapshot(
                    "C:\\Projects\\Sample",
                    "Sample",
                    Guid.NewGuid())),
        };
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");
        var created = await session.CreateEntityAsync("Entity");
        var objectId = created.Current.Document!.Entities.Single().ObjectId;
        var editId = ProjectEditId.CreateNew();
        ProjectSessionSnapshotChangedEventArgs? published = null;
        session.SnapshotChanged += (_, args) => published = args;

        var result = await session.SetEntityTransformAsync(
            objectId,
            TransformValue.Identity,
            new ProjectSessionEditContext(editId, ExpectedRevision: 1));

        Assert.False(result.Succeeded);
        Assert.Equal(ProjectSessionFailureKind.RevisionConflict, result.FailureKind);
        Assert.Equal(editId, result.OriginatingEditId);
        Assert.NotNull(published);
        Assert.Equal(editId, published!.OriginatingEditId);
        Assert.False(published.OriginatingEditSucceeded);
        Assert.Same(result.Current, published.Snapshot);
    }

    [Fact]
    public async Task Transform_history_undo_redo_and_savepoint_follow_logical_content_state()
    {
        var projectGateway = OpenableProjectGateway();
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");
        var created = await session.CreateEntityAsync("Entity");
        var objectId = created.Current.Document!.Entities.Single().ObjectId;
        var transformB = new TransformValue(
            new Float3(1, 2, 3),
            Quaternion.Identity,
            Float3.One);
        var transformC = new TransformValue(
            new Float3(4, 5, 6),
            Quaternion.Identity,
            new Float3(2, 2, 2));

        var editedB = await EditTransformAsync(session, objectId, transformB);
        var savedB = await session.SaveSceneAsync();
        var editedC = await EditTransformAsync(session, objectId, transformC);
        var undone = await session.UndoAsync();
        var redone = await session.RedoAsync();

        Assert.True(editedB.Current.IsDirty);
        Assert.True(savedB.Current.CanUndo);
        Assert.False(savedB.Current.IsDirty);
        Assert.True(editedC.Current.IsDirty);
        Assert.True(editedC.Current.CanUndo);
        Assert.False(editedC.Current.CanRedo);
        Assert.False(undone.Current.IsDirty);
        Assert.Equal(transformB, undone.Current.Document!.Entities.Single().Transform);
        Assert.True(undone.Current.CanRedo);
        Assert.True(redone.Current.IsDirty);
        Assert.Equal(transformC, redone.Current.Document!.Entities.Single().Transform);
        Assert.True(editedC.Current.Document!.Revision < undone.Current.Document.Revision);
        Assert.True(undone.Current.Document.Revision < redone.Current.Document.Revision);
    }

    [Fact]
    public async Task No_op_and_failure_do_not_move_history_cursor()
    {
        var projectGateway = OpenableProjectGateway();
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");
        var created = await session.CreateEntityAsync("Entity");
        var objectId = created.Current.Document!.Entities.Single().ObjectId;

        var noOp = await EditTransformAsync(session, objectId, TransformValue.Identity);
        sceneGateway.Connection.NextTransformFailure = new SceneDocumentFailure(
            SceneDocumentFailureKind.InvalidTransform,
            "Rejected Transform.");
        var failed = await EditTransformAsync(
            session,
            objectId,
            new TransformValue(new Float3(1, 0, 0), Quaternion.Identity, Float3.One));

        Assert.True(noOp.Succeeded);
        Assert.False(noOp.Current.CanUndo);
        Assert.False(failed.Succeeded);
        Assert.Equal(ProjectSessionFailureKind.InvalidTransform, failed.FailureKind);
        Assert.False(failed.Current.CanUndo);
        Assert.Equal(created.Current.CurrentContentStateId, failed.Current.CurrentContentStateId);
    }

    [Fact]
    public async Task New_edit_after_undo_truncates_redo_tail()
    {
        var projectGateway = OpenableProjectGateway();
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");
        var created = await session.CreateEntityAsync("Entity");
        var objectId = created.Current.Document!.Entities.Single().ObjectId;
        await EditTransformAsync(
            session,
            objectId,
            new TransformValue(new Float3(1, 0, 0), Quaternion.Identity, Float3.One));
        await session.UndoAsync();

        var replacement = await EditTransformAsync(
            session,
            objectId,
            new TransformValue(new Float3(2, 0, 0), Quaternion.Identity, Float3.One));

        Assert.True(replacement.Current.CanUndo);
        Assert.False(replacement.Current.CanRedo);
    }

    [Fact]
    public async Task Typed_undo_and_redo_failures_leave_cursor_and_content_state_unchanged()
    {
        var projectGateway = OpenableProjectGateway();
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");
        var created = await session.CreateEntityAsync("Entity");
        var objectId = created.Current.Document!.Entities.Single().ObjectId;
        var transform = new TransformValue(
            new Float3(1, 0, 0),
            Quaternion.Identity,
            Float3.One);
        var edited = await EditTransformAsync(session, objectId, transform);
        sceneGateway.Connection.NextTransformFailure = RevisionConflict();

        var failedUndo = await session.UndoAsync();

        Assert.False(failedUndo.Succeeded);
        Assert.Equal(ProjectSessionFailureKind.RevisionConflict, failedUndo.FailureKind);
        Assert.Equal(edited.Current.CurrentContentStateId, failedUndo.Current.CurrentContentStateId);
        Assert.True(failedUndo.Current.CanUndo);
        Assert.False(failedUndo.Current.CanRedo);
        Assert.Equal(transform, failedUndo.Current.Document!.Entities.Single().Transform);

        var undone = await session.UndoAsync();
        sceneGateway.Connection.NextTransformFailure = RevisionConflict();

        var failedRedo = await session.RedoAsync();

        Assert.False(failedRedo.Succeeded);
        Assert.Equal(ProjectSessionFailureKind.RevisionConflict, failedRedo.FailureKind);
        Assert.Equal(undone.Current.CurrentContentStateId, failedRedo.Current.CurrentContentStateId);
        Assert.False(failedRedo.Current.CanUndo);
        Assert.True(failedRedo.Current.CanRedo);
        Assert.Equal(TransformValue.Identity, failedRedo.Current.Document!.Entities.Single().Transform);
    }

    [Fact]
    public async Task Close_and_reopen_clear_document_history()
    {
        var projectGateway = OpenableProjectGateway();
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");
        var created = await session.CreateEntityAsync("Entity");
        await EditTransformAsync(
            session,
            created.Current.Document!.Entities.Single().ObjectId,
            new TransformValue(new Float3(1, 0, 0), Quaternion.Identity, Float3.One));
        Assert.True(session.Current.CanUndo);

        await session.CloseProjectAsync();
        var reopened = await session.OpenProjectAsync("C:\\Projects\\Sample");

        Assert.True(reopened.Succeeded);
        Assert.False(reopened.Current.CanUndo);
        Assert.False(reopened.Current.CanRedo);
        Assert.False(reopened.Current.IsDirty);
    }

    [Fact]
    public async Task Changed_non_undoable_mutation_resets_history_to_a_fresh_dirty_baseline()
    {
        var projectGateway = OpenableProjectGateway();
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");
        var created = await session.CreateEntityAsync("Entity");
        var objectId = created.Current.Document!.Entities.Single().ObjectId;
        var transformed = await EditTransformAsync(
            session,
            objectId,
            new TransformValue(new Float3(1, 0, 0), Quaternion.Identity, Float3.One));

        var renamed = await session.SetEntityNameAsync(objectId, "Renamed");

        Assert.True(transformed.Current.CanUndo);
        Assert.False(renamed.Current.CanUndo);
        Assert.False(renamed.Current.CanRedo);
        Assert.True(renamed.Current.IsDirty);
        Assert.NotEqual(
            transformed.Current.CurrentContentStateId,
            renamed.Current.CurrentContentStateId);
    }

    [Fact]
    public async Task Malformed_success_receipt_fails_closed_and_clears_history()
    {
        var projectGateway = OpenableProjectGateway();
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");
        var created = await session.CreateEntityAsync("Entity");
        var objectId = created.Current.Document!.Entities.Single().ObjectId;
        sceneGateway.Connection.OmitTransformReceipt = true;

        var result = await EditTransformAsync(
            session,
            objectId,
            new TransformValue(new Float3(1, 0, 0), Quaternion.Identity, Float3.One));

        Assert.False(result.Succeeded);
        Assert.Equal(ProjectSessionFailureKind.InternalError, result.FailureKind);
        Assert.False(result.Current.CanUndo);
        Assert.True(result.Current.IsDirty);
    }

    [Fact]
    public async Task Uncertain_transform_completion_refreshes_authoritative_state_and_clears_history()
    {
        var projectGateway = OpenableProjectGateway();
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");
        var created = await session.CreateEntityAsync("Entity");
        var objectId = created.Current.Document!.Entities.Single().ObjectId;
        await EditTransformAsync(
            session,
            objectId,
            new TransformValue(new Float3(1, 0, 0), Quaternion.Identity, Float3.One));
        var revisionBeforeUncertainEdit = session.Current.Document!.Revision;
        sceneGateway.Connection.ThrowAfterNextTransformMutation = true;

        var result = await EditTransformAsync(
            session,
            objectId,
            new TransformValue(new Float3(2, 0, 0), Quaternion.Identity, Float3.One));

        Assert.False(result.Succeeded);
        Assert.True(result.Current.IsReady);
        Assert.Equal(
            revisionBeforeUncertainEdit + 1,
            result.Current.Document!.Revision);
        Assert.Equal(
            new Float3(2, 0, 0),
            result.Current.Document.Entities.Single().Transform.Position);
        Assert.False(result.Current.CanUndo);
        Assert.False(result.Current.CanRedo);
        Assert.True(result.Current.IsDirty);
    }

    [Fact]
    public async Task Uncertain_transform_completion_invalidates_session_when_refresh_fails()
    {
        var projectGateway = OpenableProjectGateway();
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");
        var created = await session.CreateEntityAsync("Entity");
        var objectId = created.Current.Document!.Entities.Single().ObjectId;
        sceneGateway.Connection.ThrowAfterNextTransformMutation = true;
        sceneGateway.Connection.RejectRefresh = true;

        var result = await EditTransformAsync(
            session,
            objectId,
            new TransformValue(new Float3(2, 0, 0), Quaternion.Identity, Float3.One));

        Assert.False(result.Succeeded);
        Assert.Same(ProjectSessionSnapshot.NoProject, result.Current);
        Assert.Same(ProjectSessionSnapshot.NoProject, session.Current);
        Assert.Equal(1, sceneGateway.Connection.DisposeCount);
    }

    [Fact]
    public async Task Unknown_transform_outcome_refreshes_and_clears_history()
    {
        var projectGateway = OpenableProjectGateway();
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");
        var created = await session.CreateEntityAsync("Entity");
        var objectId = created.Current.Document!.Entities.Single().ObjectId;
        await EditTransformAsync(
            session,
            objectId,
            new TransformValue(new Float3(1, 0, 0), Quaternion.Identity, Float3.One));
        sceneGateway.Connection.ReportUnknownAfterNextTransformMutation = true;

        var result = await EditTransformAsync(
            session,
            objectId,
            new TransformValue(new Float3(2, 0, 0), Quaternion.Identity, Float3.One));

        Assert.False(result.Succeeded);
        Assert.True(result.Current.IsReady);
        Assert.Equal(
            new Float3(2, 0, 0),
            Assert.Single(result.Current.Document!.Entities).Transform.Position);
        Assert.False(result.Current.CanUndo);
        Assert.False(result.Current.CanRedo);
        Assert.True(result.Current.IsDirty);
    }

    [Fact]
    public void Scene_edit_history_enforces_entry_limit()
    {
        var history = new SceneEditHistory(entryLimit: 2, byteLimit: 100);
        var sceneId = Guid.NewGuid();
        var before = new ContentStateId(1);
        for (ulong index = 0; index < 3; index++)
        {
            var after = new ContentStateId(index + 2);
            history.Commit(new SceneEditHistoryEntry(
                sceneId,
                Guid.NewGuid(),
                $"Edit {index}",
                ProjectEditId.CreateNew(),
                TransformValue.Identity,
                new TransformValue(
                    new Float3(index + 1, 0, 0),
                    Quaternion.Identity,
                    Float3.One),
                before,
                after,
                EstimatedBytes: 10));
            before = after;
        }

        Assert.Equal(2, history.Count);
        Assert.Equal(2, history.Cursor);
        Assert.Equal(20, history.EstimatedBytes);
        Assert.Equal("Edit 2", history.UndoLabel);
    }

    [Fact]
    public void Scene_edit_history_enforces_byte_limit_independently()
    {
        var history = new SceneEditHistory(entryLimit: 10, byteLimit: 20);
        var sceneId = Guid.NewGuid();
        var before = new ContentStateId(1);
        for (ulong index = 0; index < 3; index++)
        {
            var after = new ContentStateId(index + 2);
            history.Commit(HistoryEntry(sceneId, index, before, after, estimatedBytes: 10));
            before = after;
        }

        Assert.Equal(2, history.Count);
        Assert.Equal(2, history.Cursor);
        Assert.Equal(20, history.EstimatedBytes);
        Assert.Equal("Edit 2", history.UndoLabel);
    }

    [Fact]
    public void Scene_edit_history_rejects_a_single_entry_over_the_byte_limit()
    {
        var history = new SceneEditHistory(entryLimit: 10, byteLimit: 9);
        var entry = HistoryEntry(
            Guid.NewGuid(),
            index: 0,
            new ContentStateId(1),
            new ContentStateId(2),
            estimatedBytes: 10);

        Assert.Throws<ArgumentOutOfRangeException>(() => history.Commit(entry));
        Assert.Equal(0, history.Count);
        Assert.Equal(0, history.Cursor);
        Assert.False(history.CanUndo);
    }

    [Fact]
    public async Task Mesh_create_returns_authoritative_object_receipt_and_typed_reference()
    {
        var projectGateway = new ControlledProjectGateway
        {
            OpenResult = ProjectDescriptorOperationResult.Success(
                new ProjectDescriptorSnapshot(
                    "C:\\Projects\\Sample",
                    "Sample",
                    Guid.NewGuid())),
        };
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");
        var mesh = SceneMeshReference.DirectionalWedgeValidation;

        var created = await session.CreateMeshEntityAsync("Mesh", mesh);

        Assert.True(created.Succeeded);
        Assert.NotNull(created.CreatedObjectId);
        var entity = Assert.Single(created.Current.Document!.Entities);
        Assert.Equal(created.CreatedObjectId!.Value, entity.ObjectId);
        Assert.Equal(mesh, entity.Mesh);
        Assert.True(entity.RuntimeEntityId.IsValid);
    }

    [Fact]
    public async Task Invalid_mesh_reference_failure_has_no_receipt_or_revision_change()
    {
        var projectGateway = new ControlledProjectGateway
        {
            OpenResult = ProjectDescriptorOperationResult.Success(
                new ProjectDescriptorSnapshot(
                    "C:\\Projects\\Sample",
                    "Sample",
                    Guid.NewGuid())),
        };
        var sceneGateway = new ControlledSceneGateway();
        sceneGateway.Connection.RejectMeshCreate = true;
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");

        var created = await session.CreateMeshEntityAsync(
            "Mesh",
            SceneMeshReference.DirectionalWedgeValidation);

        Assert.False(created.Succeeded);
        Assert.Equal(ProjectSessionFailureKind.InvalidAssetReference, created.FailureKind);
        Assert.Null(created.CreatedObjectId);
        Assert.Equal(1UL, created.Current.Document!.Revision);
        Assert.Empty(created.Current.Document.Entities);
    }

    [Fact]
    public async Task Close_disposes_document_before_publishing_no_project()
    {
        var projectGateway = new ControlledProjectGateway
        {
            OpenResult = ProjectDescriptorOperationResult.Success(
                new ProjectDescriptorSnapshot(
                    "C:\\Projects\\Sample",
                    "Sample",
                    Guid.NewGuid())),
        };
        var sceneGateway = new ControlledSceneGateway();
        await using var session = new ProjectSession(projectGateway, sceneGateway);
        await session.OpenProjectAsync("C:\\Projects\\Sample");

        var closed = await session.CloseProjectAsync();

        Assert.True(closed.Succeeded);
        Assert.Equal(ProjectSessionState.NoProject, closed.Current.State);
        Assert.Equal(1, sceneGateway.Connection.DisposeCount);
    }

    [Fact]
    public async Task Dispose_cancels_an_in_flight_operation_and_rejects_late_work()
    {
        var entered = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        var projectGateway = new ControlledProjectGateway
        {
            OpenHandler = async token =>
            {
                entered.SetResult();
                await Task.Delay(Timeout.InfiniteTimeSpan, token);
                throw new InvalidOperationException("Unreachable.");
            },
        };
        var session = new ProjectSession(projectGateway, new ControlledSceneGateway());
        var operation = session.OpenProjectAsync("C:\\Projects\\Sample").AsTask();
        await entered.Task;

        await session.DisposeAsync();

        await Assert.ThrowsAnyAsync<OperationCanceledException>(async () => await operation);
        await Assert.ThrowsAsync<ObjectDisposedException>(async () =>
            await session.OpenProjectAsync("C:\\Projects\\Late"));
    }

    private static ControlledProjectGateway OpenableProjectGateway() =>
        new()
        {
            OpenResult = ProjectDescriptorOperationResult.Success(
                new ProjectDescriptorSnapshot(
                    "C:\\Projects\\Sample",
                    "Sample",
                    Guid.NewGuid())),
        };

    private static ValueTask<ProjectSessionOperationResult> EditTransformAsync(
        ProjectSession session,
        Guid objectId,
        TransformValue transform) =>
        session.SetEntityTransformAsync(
            objectId,
            transform,
            new ProjectSessionEditContext(
                ProjectEditId.CreateNew(),
                session.Current.Document!.Revision));

    private static SceneDocumentFailure RevisionConflict() =>
        new(
            SceneDocumentFailureKind.RevisionConflict,
            "The expected scene revision is stale.");

    private static SceneEditHistoryEntry HistoryEntry(
        Guid sceneId,
        ulong index,
        ContentStateId before,
        ContentStateId after,
        long estimatedBytes) =>
        new(
            sceneId,
            Guid.NewGuid(),
            $"Edit {index}",
            ProjectEditId.CreateNew(),
            TransformValue.Identity,
            new TransformValue(
                new Float3(index + 1, 0, 0),
                Quaternion.Identity,
                Float3.One),
            before,
            after,
            estimatedBytes);

    private sealed class ControlledProjectGateway : IProjectDescriptorGateway
    {
        public ProjectDescriptorOperationResult? CreateResult { get; set; }

        public ProjectDescriptorOperationResult? OpenResult { get; set; }

        public Func<CancellationToken, Task<ProjectDescriptorOperationResult>>? OpenHandler
        {
            get;
            set;
        }

        public Guid LastCreateProjectId { get; private set; }

        public ValueTask<ProjectDescriptorOperationResult> CreateMinimalProjectAsync(
            string parentDirectory,
            string projectName,
            Guid projectId,
            CancellationToken cancellationToken = default)
        {
            LastCreateProjectId = projectId;
            return ValueTask.FromResult(
                CreateResult ?? throw new InvalidOperationException("Create result is missing."));
        }

        public ValueTask<ProjectDescriptorOperationResult> OpenProjectAsync(
            string projectPath,
            CancellationToken cancellationToken = default) =>
            OpenHandler is null
                ? ValueTask.FromResult(
                    OpenResult ?? throw new InvalidOperationException("Open result is missing."))
                : new ValueTask<ProjectDescriptorOperationResult>(OpenHandler(cancellationToken));
    }

    private sealed class ControlledSceneGateway : ISceneDocumentGateway
    {
        public ControlledSceneConnection Connection { get; } = new();

        public SceneDocumentFailure? OpenFailure { get; set; }

        public Guid LastNewSceneId { get; private set; }

        public ValueTask<SceneDocumentOpenResult> OpenDefaultAsync(
            string projectRoot,
            Guid newSceneId,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            LastNewSceneId = newSceneId;
            return ValueTask.FromResult(OpenFailure is null
                ? SceneDocumentOpenResult.Success(Connection, Connection.Current)
                : SceneDocumentOpenResult.Failed(OpenFailure));
        }
    }

    private sealed class ControlledSceneConnection : ISceneDocumentConnection
    {
        private readonly List<SceneEntitySnapshot> entities_ = [];

        public SceneDocumentSnapshot Current { get; private set; } = Snapshot(1, 1, []);

        public int DisposeCount { get; private set; }

        public bool RejectMeshCreate { get; set; }

        public SceneDocumentFailure? NextTransformFailure { get; set; }

        public bool OmitTransformReceipt { get; set; }

        public bool ThrowAfterNextTransformMutation { get; set; }

        public bool ReportUnknownAfterNextTransformMutation { get; set; }

        public bool RejectRefresh { get; set; }

        public ulong? LastSetTransformExpectedRevision { get; private set; }

        public ValueTask<SceneDocumentOperationResult> RefreshAsync(
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(RejectRefresh
                ? SceneDocumentOperationResult.Failed(
                    Current,
                    new SceneDocumentFailure(
                        SceneDocumentFailureKind.NativeUnavailable,
                        "Refresh is unavailable."))
                : SceneDocumentOperationResult.Success(Current));
        }

        public ValueTask<SceneDocumentOperationResult> CreateEntityAsync(
            Guid objectId,
            string name,
            ulong expectedRevision,
            CancellationToken cancellationToken = default)
        {
            entities_.Add(new SceneEntitySnapshot(
                objectId,
                NextRuntimeEntityId(),
                name,
                TransformValue.Identity));
            Advance();
            return ValueTask.FromResult(SceneDocumentOperationResult.Success(Current));
        }

        public ValueTask<SceneDocumentOperationResult> CreateMeshEntityAsync(
            Guid objectId,
            string name,
            SceneMeshReference mesh,
            ulong expectedRevision,
            CancellationToken cancellationToken = default)
        {
            if (RejectMeshCreate)
            {
                return ValueTask.FromResult(SceneDocumentOperationResult.Failed(
                    Current,
                    new SceneDocumentFailure(
                        SceneDocumentFailureKind.InvalidAssetReference,
                        "The mesh asset reference is invalid.")));
            }
            entities_.Add(new SceneEntitySnapshot(
                objectId,
                NextRuntimeEntityId(),
                name,
                TransformValue.Identity,
                mesh));
            Advance();
            return ValueTask.FromResult(SceneDocumentOperationResult.Success(Current));
        }

        public ValueTask<SceneDocumentOperationResult> SetEntityNameAsync(
            Guid objectId,
            string name,
            ulong expectedRevision,
            CancellationToken cancellationToken = default)
        {
            var entity = entities_.Single(value => value.ObjectId == objectId);
            entities_[entities_.IndexOf(entity)] = new SceneEntitySnapshot(
                entity.ObjectId,
                entity.RuntimeEntityId,
                name,
                entity.Transform,
                entity.Mesh);
            Advance();
            return ValueTask.FromResult(SceneDocumentOperationResult.Success(Current));
        }

        public ValueTask<SceneDocumentOperationResult> SetEntityTransformAsync(
            Guid objectId,
            TransformValue transform,
            ulong expectedRevision,
            CancellationToken cancellationToken = default)
        {
            LastSetTransformExpectedRevision = expectedRevision;
            if (NextTransformFailure is SceneDocumentFailure configuredFailure)
            {
                NextTransformFailure = null;
                return ValueTask.FromResult(SceneDocumentOperationResult.Failed(
                    Current,
                    configuredFailure));
            }
            if (expectedRevision != Current.Revision)
            {
                return ValueTask.FromResult(SceneDocumentOperationResult.Failed(
                    Current,
                    new SceneDocumentFailure(
                        SceneDocumentFailureKind.RevisionConflict,
                        "The expected scene revision is stale.")));
            }
            var entity = entities_.Single(value => value.ObjectId == objectId);
            var beforeTransform = entity.Transform;
            if (beforeTransform == transform)
            {
                var noOpReceipt = new SceneEntityTransformReceipt(
                    objectId,
                    changed: false,
                    beforeTransform,
                    transform,
                    Current.Revision,
                    Current.Revision);
                return ValueTask.FromResult(OmitTransformReceipt
                    ? SceneDocumentOperationResult.Success(Current)
                    : SceneDocumentOperationResult.Success(Current, noOpReceipt));
            }
            var beforeRevision = Current.Revision;
            entities_[entities_.IndexOf(entity)] = new SceneEntitySnapshot(
                entity.ObjectId,
                entity.RuntimeEntityId,
                entity.Name,
                transform,
                entity.Mesh);
            Advance();
            var receipt = new SceneEntityTransformReceipt(
                objectId,
                changed: true,
                beforeTransform,
                transform,
                beforeRevision,
                Current.Revision);
            if (ReportUnknownAfterNextTransformMutation)
            {
                ReportUnknownAfterNextTransformMutation = false;
                return ValueTask.FromResult(SceneDocumentOperationResult.Failed(
                    Snapshot(beforeRevision, Current.SavedRevision, entities_),
                    new SceneDocumentFailure(
                        SceneDocumentFailureKind.AuthoritativeStateUnknown,
                        "The post-operation snapshot was unavailable.")));
            }
            if (ThrowAfterNextTransformMutation)
            {
                ThrowAfterNextTransformMutation = false;
                throw new InvalidOperationException("Transport completion was lost.");
            }
            return ValueTask.FromResult(OmitTransformReceipt
                ? SceneDocumentOperationResult.Success(Current)
                : SceneDocumentOperationResult.Success(Current, receipt));
        }

        public ValueTask<SceneDocumentOperationResult> SaveAsync(
            ulong expectedRevision,
            CancellationToken cancellationToken = default)
        {
            Current = Snapshot(Current.Revision, Current.Revision, entities_);
            return ValueTask.FromResult(SceneDocumentOperationResult.Success(Current));
        }

        public ValueTask DisposeAsync()
        {
            DisposeCount++;
            return ValueTask.CompletedTask;
        }

        private void Advance() =>
            Current = Snapshot(Current.Revision + 1, Current.SavedRevision, entities_);

        private EntityId NextRuntimeEntityId() =>
            new(checked((uint)entities_.Count + 1), 1);

        private static SceneDocumentSnapshot Snapshot(
            ulong revision,
            ulong savedRevision,
            IEnumerable<SceneEntitySnapshot> entities) =>
            new(
                Guid.Parse("11111111-2222-3333-4444-555555555555"),
                "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
                revision,
                savedRevision,
                entities);
    }
}
