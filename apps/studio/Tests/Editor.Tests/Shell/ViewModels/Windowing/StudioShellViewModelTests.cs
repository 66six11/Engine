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
    public async Task Undo_and_redo_commands_project_history_labels_dirty_and_selection()
    {
        var objectId = Guid.NewGuid();
        var entityA = new SceneEntitySnapshot(
            objectId,
            new EntityId(1, 1),
            "Selected",
            TransformValue.Identity);
        var baseSnapshot = Ready("Sample", "C:\\Projects\\Sample");
        var edited = ProjectSessionSnapshot.Ready(
            baseSnapshot.Project!,
            new SceneDocumentSnapshot(
                baseSnapshot.Document!.SceneId,
                baseSnapshot.Document.Path,
                revision: 2,
                savedRevision: 1,
                entities: [entityA]),
            new ContentStateId(2),
            new ContentStateId(1),
            canUndo: true,
            canRedo: false,
            undoLabel: "Transform Selected",
            redoLabel: null);
        var entityB = new SceneEntitySnapshot(
            objectId,
            new EntityId(1, 1),
            "Selected",
            new TransformValue(
                new Float3(2, 0, 0),
                Quaternion.Identity,
                Float3.One));
        var undone = ProjectSessionSnapshot.Ready(
            edited.Project!,
            new SceneDocumentSnapshot(
                edited.Document!.SceneId,
                edited.Document.Path,
                revision: 3,
                savedRevision: 1,
                entities: [entityA]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: true,
            undoLabel: null,
            redoLabel: "Transform Selected");
        var redone = ProjectSessionSnapshot.Ready(
            edited.Project!,
            new SceneDocumentSnapshot(
                edited.Document!.SceneId,
                edited.Document.Path,
                revision: 4,
                savedRevision: 1,
                entities: [entityB]),
            new ContentStateId(2),
            new ContentStateId(1),
            canUndo: true,
            canRedo: false,
            undoLabel: "Transform Selected",
            redoLabel: null);
        var projectSession = new TestProjectSession();
        projectSession.Publish(edited);
        projectSession.UndoHandler = _ => ValueTask.FromResult(
            ProjectSessionOperationResult.Success(undone, "Undid Transform Selected."));
        projectSession.RedoHandler = _ => ValueTask.FromResult(
            ProjectSessionOperationResult.Success(redone, "Redid Transform Selected."));
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();
        viewModel.SelectedEntity = entityA;

        Assert.True(viewModel.IsDocumentDirty);
        Assert.Equal("Undo Transform Selected", viewModel.UndoSceneLabel);
        Assert.True(viewModel.UndoSceneCommand.CanExecute(null));
        Assert.False(viewModel.RedoSceneCommand.CanExecute(null));

        viewModel.UndoSceneCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.False(viewModel.IsDocumentDirty);
        Assert.Equal("Undo", viewModel.UndoSceneLabel);
        Assert.Equal("Redo Transform Selected", viewModel.RedoSceneLabel);
        Assert.False(viewModel.UndoSceneCommand.CanExecute(null));
        Assert.True(viewModel.RedoSceneCommand.CanExecute(null));
        Assert.Equal(objectId, viewModel.SelectedEntity?.ObjectId);

        viewModel.RedoSceneCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.True(viewModel.IsDocumentDirty);
        Assert.Equal("2", viewModel.PositionX);
        Assert.Equal(objectId, viewModel.SelectedEntity?.ObjectId);
    }

    [Fact]
    public async Task Undo_rebases_dirty_position_and_scale_drafts_onto_the_new_revision()
    {
        var objectId = Guid.NewGuid();
        var currentEntity = new SceneEntitySnapshot(
            objectId,
            new EntityId(1, 1),
            "Selected",
            new TransformValue(
                new Float3(5.0F, 6.0F, 7.0F),
                Quaternion.Identity,
                new Float3(2.0F, 3.0F, 4.0F)));
        var undoneEntity = new SceneEntitySnapshot(
            objectId,
            currentEntity.RuntimeEntityId,
            currentEntity.Name,
            TransformValue.Identity);
        var baseSnapshot = Ready("Sample", "C:\\Projects\\Sample");
        var current = ProjectSessionSnapshot.Ready(
            baseSnapshot.Project!,
            new SceneDocumentSnapshot(
                baseSnapshot.Document!.SceneId,
                baseSnapshot.Document.Path,
                revision: 2,
                savedRevision: 1,
                entities: [currentEntity]),
            new ContentStateId(2),
            new ContentStateId(1),
            canUndo: true,
            canRedo: false,
            undoLabel: "Transform Selected",
            redoLabel: null);
        var undone = ProjectSessionSnapshot.Ready(
            current.Project!,
            new SceneDocumentSnapshot(
                current.Document!.SceneId,
                current.Document.Path,
                revision: 3,
                savedRevision: 1,
                entities: [undoneEntity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: true,
            undoLabel: null,
            redoLabel: "Transform Selected");
        var projectSession = new TestProjectSession();
        projectSession.Publish(current);
        projectSession.UndoHandler = _ => ValueTask.FromResult(
            ProjectSessionOperationResult.Success(undone, "Undid Transform Selected."));
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();
        viewModel.SelectedEntity = currentEntity;
        viewModel.PositionX = "1.2";
        viewModel.ScaleZ = "1.234567";

        viewModel.UndoSceneCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.Equal("1.2", viewModel.PositionX);
        Assert.Equal("0", viewModel.PositionY);
        Assert.Equal("0", viewModel.PositionZ);
        Assert.Equal("1", viewModel.ScaleX);
        Assert.Equal("1", viewModel.ScaleY);
        Assert.Equal("1.234567", viewModel.ScaleZ);

        ProjectSessionEditContext? retryContext = null;
        projectSession.SetTransformHandler = (_, transform, context, _) =>
        {
            retryContext = context;
            Assert.Equal(new Float3(1.2F, 0.0F, 0.0F), transform.Position);
            Assert.Equal(new Float3(1.0F, 1.0F, 1.234567F), transform.Scale);
            return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                undone,
                "Recorded retry.",
                originatingEditId: context.EditId));
        };

        viewModel.ApplyEntityTransformCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.True(retryContext.HasValue);
        Assert.Equal(3UL, retryContext.Value.ExpectedRevision);
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
                entities: [createdEntity, trailingEntity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
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
                entities: [selected]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
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

    [Fact]
    public async Task Apply_transform_preserves_source_text_across_own_ack_and_projects_only_external_changes()
    {
        var objectId = Guid.NewGuid();
        var runtimeEntityId = new EntityId(7, 3);
        var initialEntity = new SceneEntitySnapshot(
            objectId,
            runtimeEntityId,
            "Directional Wedge",
            TransformValue.Identity,
            SceneMeshReference.DirectionalWedgeValidation);
        var initialBase = Ready("Sample", "C:\\Projects\\Sample");
        var initial = ProjectSessionSnapshot.Ready(
            initialBase.Project!,
            new SceneDocumentSnapshot(
                initialBase.Document!.SceneId,
                initialBase.Document.Path,
                revision: 1,
                savedRevision: 1,
                entities: [initialEntity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        var expectedNumericsRotation = System.Numerics.Quaternion.CreateFromYawPitchRoll(
            40.0F * MathF.PI / 180.0F,
            30.0F * MathF.PI / 180.0F,
            -20.0F * MathF.PI / 180.0F);
        var expectedTransform = new TransformValue(
            new Float3(1.2F, 1.234567F, -3.0F),
            new Quaternion(
                expectedNumericsRotation.X,
                expectedNumericsRotation.Y,
                expectedNumericsRotation.Z,
                expectedNumericsRotation.W),
            new Float3(2.5F, 1.234567F, 4.0F));
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        var requestObserved = false;
        ProjectSessionSnapshot? acceptedSnapshot = null;
        projectSession.SetTransformHandler = (
            requestedObjectId,
            transform,
            editContext,
            cancellationToken) =>
        {
            Assert.Equal(objectId, requestedObjectId);
            Assert.Equal(expectedTransform.Position, transform.Position);
            Assert.Equal(expectedTransform.Scale, transform.Scale);
            Assert.InRange(
                MathF.Abs(expectedTransform.Rotation.X - transform.Rotation.X),
                0.0F,
                1.0e-6F);
            Assert.InRange(
                MathF.Abs(expectedTransform.Rotation.Y - transform.Rotation.Y),
                0.0F,
                1.0e-6F);
            Assert.InRange(
                MathF.Abs(expectedTransform.Rotation.Z - transform.Rotation.Z),
                0.0F,
                1.0e-6F);
            Assert.InRange(
                MathF.Abs(expectedTransform.Rotation.W - transform.Rotation.W),
                0.0F,
                1.0e-6F);
            Assert.False(cancellationToken.IsCancellationRequested);
            requestObserved = true;
            var transformedEntity = new SceneEntitySnapshot(
                objectId,
                runtimeEntityId,
                initialEntity.Name,
                transform,
                initialEntity.Mesh);
            var updated = ProjectSessionSnapshot.Ready(
                initial.Project!,
                new SceneDocumentSnapshot(
                    initial.Document!.SceneId,
                    initial.Document.Path,
                    revision: 2,
                    savedRevision: 1,
                    entities: [transformedEntity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
            projectSession.Publish(
                updated,
                editContext.EditId,
                originatingEditSucceeded: true);
            acceptedSnapshot = updated;
            return ValueTask.FromResult(
                ProjectSessionOperationResult.Success(
                    updated,
                    "Updated entity Transform.",
                    originatingEditId: editContext.EditId));
        };
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();
        viewModel.SelectedEntity = initialEntity;
        viewModel.PositionX = "1.2";
        viewModel.PositionY = "1.234567";
        viewModel.PositionZ = "-3.000";
        viewModel.RotationDegreesX = "30.000";
        viewModel.RotationDegreesY = "40.0";
        viewModel.RotationDegreesZ = "-20.0000";
        viewModel.ScaleX = "2.5000";
        viewModel.ScaleY = "1.234567";
        viewModel.ScaleZ = "4.000";

        viewModel.ApplyEntityTransformCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.True(requestObserved);
        Assert.NotNull(viewModel.SelectedEntity);
        Assert.Equal(objectId, viewModel.SelectedEntity.ObjectId);
        Assert.Equal(runtimeEntityId, viewModel.SelectedEntity.RuntimeEntityId);
        Assert.Equal(SceneMeshReference.DirectionalWedgeValidation, viewModel.SelectedEntity.Mesh);
        Assert.Equal(expectedTransform.Position, viewModel.SelectedEntity.Transform.Position);
        Assert.Equal(expectedTransform.Scale, viewModel.SelectedEntity.Transform.Scale);
        Assert.Equal("1.2", viewModel.PositionX);
        Assert.Equal("1.234567", viewModel.PositionY);
        Assert.Equal("-3.000", viewModel.PositionZ);
        Assert.Equal("30.000", viewModel.RotationDegreesX);
        Assert.Equal("40.0", viewModel.RotationDegreesY);
        Assert.Equal("-20.0000", viewModel.RotationDegreesZ);
        Assert.Equal("2.5000", viewModel.ScaleX);
        Assert.Equal("1.234567", viewModel.ScaleY);
        Assert.Equal("4.000", viewModel.ScaleZ);

        Assert.NotNull(acceptedSnapshot);
        projectSession.SetNameHandler = (_, _, _) => ValueTask.FromResult(
            ProjectSessionOperationResult.Success(
                acceptedSnapshot,
                "Accepted the same Transform snapshot."));
        viewModel.ApplyEntityNameCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.Equal("1.2", viewModel.PositionX);
        Assert.Equal("1.234567", viewModel.PositionY);
        Assert.Equal("-3.000", viewModel.PositionZ);
        Assert.Equal("2.5000", viewModel.ScaleX);
        Assert.Equal("1.234567", viewModel.ScaleY);
        Assert.Equal("4.000", viewModel.ScaleZ);

        var externallyChangedEntity = new SceneEntitySnapshot(
            objectId,
            runtimeEntityId,
            initialEntity.Name,
            new TransformValue(
                new Float3(9.0F, expectedTransform.Position.Y, expectedTransform.Position.Z),
                expectedTransform.Rotation,
                new Float3(expectedTransform.Scale.X, 7.0F, expectedTransform.Scale.Z)),
            initialEntity.Mesh);
        var externallyChanged = ProjectSessionSnapshot.Ready(
            initial.Project!,
            new SceneDocumentSnapshot(
                initial.Document!.SceneId,
                initial.Document.Path,
                revision: 3,
                savedRevision: 1,
                entities: [externallyChangedEntity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        projectSession.SetNameHandler = (_, _, _) => ValueTask.FromResult(
            ProjectSessionOperationResult.Success(
                externallyChanged,
                "Accepted an external Transform snapshot."));
        viewModel.ApplyEntityNameCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.Equal("9", viewModel.PositionX);
        Assert.Equal("1.234567", viewModel.PositionY);
        Assert.Equal("-3.000", viewModel.PositionZ);
        Assert.Equal("2.5000", viewModel.ScaleX);
        Assert.Equal("7", viewModel.ScaleY);
        Assert.Equal("4.000", viewModel.ScaleZ);
    }

    [Fact]
    public async Task Apply_changing_only_y_preserves_sibling_rotation_text_on_own_ack()
    {
        var objectId = Guid.NewGuid();
        var entity = new SceneEntitySnapshot(
            objectId,
            new EntityId(1, 1),
            "Rotated",
            new TransformValue(
                Float3.Zero,
                StudioEulerRotation.QuaternionFromEulerDegreesYxz(
                    new StudioEulerDegrees(30.0, 40.0, -20.0)),
                Float3.One));
        var initialBase = Ready("Sample", "C:\\Projects\\Sample");
        var initial = ProjectSessionSnapshot.Ready(
            initialBase.Project!,
            new SceneDocumentSnapshot(
                initialBase.Document!.SceneId,
                initialBase.Document.Path,
                revision: 1,
                savedRevision: 1,
                entities: [entity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        projectSession.SetTransformHandler = (requestedObjectId, transform, context, _) =>
        {
            Assert.Equal(objectId, requestedObjectId);
            Assert.Equal(1UL, context.ExpectedRevision);
            var updatedEntity = new SceneEntitySnapshot(
                entity.ObjectId,
                entity.RuntimeEntityId,
                entity.Name,
                transform);
            var updated = ProjectSessionSnapshot.Ready(
                initial.Project!,
                new SceneDocumentSnapshot(
                    initial.Document!.SceneId,
                    initial.Document.Path,
                    revision: 2,
                    savedRevision: 1,
                    entities: [updatedEntity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
            projectSession.Publish(
                updated,
                context.EditId,
                originatingEditSucceeded: true);
            return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                updated,
                "Updated entity Transform.",
                originatingEditId: context.EditId));
        };
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();
        viewModel.SelectedEntity = entity;
        var originalX = viewModel.RotationDegreesX;
        var originalZ = viewModel.RotationDegreesZ;
        viewModel.RotationDegreesY = "41.500";

        viewModel.ApplyEntityTransformCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.Equal(originalX, viewModel.RotationDegreesX);
        Assert.Equal("41.500", viewModel.RotationDegreesY);
        Assert.Equal(originalZ, viewModel.RotationDegreesZ);
    }

    [Fact]
    public async Task Own_ack_preserves_365_degree_text_for_equivalent_negated_quaternion()
    {
        var objectId = Guid.NewGuid();
        var entity = new SceneEntitySnapshot(
            objectId,
            new EntityId(1, 1),
            "Rotated",
            TransformValue.Identity);
        var initialBase = Ready("Sample", "C:\\Projects\\Sample");
        var initial = ProjectSessionSnapshot.Ready(
            initialBase.Project!,
            new SceneDocumentSnapshot(
                initialBase.Document!.SceneId,
                initialBase.Document.Path,
                revision: 1,
                savedRevision: 1,
                entities: [entity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        Quaternion? requestedRotation = null;
        projectSession.SetTransformHandler = (requestedObjectId, transform, context, _) =>
        {
            Assert.Equal(objectId, requestedObjectId);
            requestedRotation = transform.Rotation;
            var negated = new Quaternion(
                -transform.Rotation.X,
                -transform.Rotation.Y,
                -transform.Rotation.Z,
                -transform.Rotation.W);
            var acceptedTransform = new TransformValue(
                transform.Position,
                negated,
                transform.Scale);
            var updatedEntity = new SceneEntitySnapshot(
                entity.ObjectId,
                entity.RuntimeEntityId,
                entity.Name,
                acceptedTransform);
            var updated = ProjectSessionSnapshot.Ready(
                initial.Project!,
                new SceneDocumentSnapshot(
                    initial.Document!.SceneId,
                    initial.Document.Path,
                    revision: 2,
                    savedRevision: 1,
                    entities: [updatedEntity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
            projectSession.Publish(
                updated,
                context.EditId,
                originatingEditSucceeded: true);
            return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                updated,
                "Updated entity Transform.",
                originatingEditId: context.EditId));
        };
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();
        viewModel.SelectedEntity = entity;
        viewModel.RotationDegreesY = "365";

        viewModel.ApplyEntityTransformCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.True(requestedRotation.HasValue);
        Assert.True(StudioEulerRotation.AreEquivalent(
            requestedRotation.Value,
            StudioEulerRotation.QuaternionFromEulerDegreesYxz(
                new StudioEulerDegrees(0.0, 5.0, 0.0))));
        Assert.Equal("0", viewModel.RotationDegreesX);
        Assert.Equal("365", viewModel.RotationDegreesY);
        Assert.Equal("0", viewModel.RotationDegreesZ);
    }

    [Fact]
    public async Task Own_ack_retains_position_rotation_and_scale_edits_made_while_apply_is_in_flight()
    {
        var objectId = Guid.NewGuid();
        var entity = new SceneEntitySnapshot(
            objectId,
            new EntityId(1, 1),
            "Rotated",
            TransformValue.Identity);
        var initialBase = Ready("Sample", "C:\\Projects\\Sample");
        var initial = ProjectSessionSnapshot.Ready(
            initialBase.Project!,
            new SceneDocumentSnapshot(
                initialBase.Document!.SceneId,
                initialBase.Document.Path,
                revision: 1,
                savedRevision: 1,
                entities: [entity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        var completion = new TaskCompletionSource<ProjectSessionOperationResult>(
            TaskCreationOptions.RunContinuationsAsynchronously);
        ProjectSessionEditContext editContext = default;
        TransformValue submittedTransform = default;
        projectSession.SetTransformHandler = (_, transform, context, _) =>
        {
            submittedTransform = transform;
            editContext = context;
            return new ValueTask<ProjectSessionOperationResult>(completion.Task);
        };
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();
        viewModel.SelectedEntity = entity;
        viewModel.PositionX = "1.2";
        viewModel.RotationDegreesY = "365";
        viewModel.ScaleZ = "1.234567";
        viewModel.ApplyEntityTransformCommand.Execute(null);
        await WaitUntilAsync(() => viewModel.IsProjectOperationRunning);

        viewModel.PositionX = "2.2";
        viewModel.RotationDegreesY = "725";
        viewModel.ScaleZ = "3.000";
        var acknowledgedEntity = new SceneEntitySnapshot(
            entity.ObjectId,
            entity.RuntimeEntityId,
            entity.Name,
            submittedTransform);
        var acknowledged = ProjectSessionSnapshot.Ready(
            initial.Project!,
            new SceneDocumentSnapshot(
                initial.Document!.SceneId,
                initial.Document.Path,
                revision: 2,
                savedRevision: 1,
                entities: [acknowledgedEntity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        projectSession.Publish(
            acknowledged,
            editContext.EditId,
            originatingEditSucceeded: true);
        completion.SetResult(ProjectSessionOperationResult.Success(
            acknowledged,
            "Updated entity Transform.",
            originatingEditId: editContext.EditId));
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.Equal("2.2", viewModel.PositionX);
        Assert.Equal("725", viewModel.RotationDegreesY);
        Assert.Equal("3.000", viewModel.ScaleZ);

        projectSession.SetTransformHandler = (_, transform, context, _) =>
        {
            Assert.Equal(2UL, context.ExpectedRevision);
            Assert.Equal(2.2F, transform.Position.X);
            Assert.Equal(3.0F, transform.Scale.Z);
            Assert.True(StudioEulerRotation.AreEquivalent(
                transform.Rotation,
                StudioEulerRotation.QuaternionFromEulerDegreesYxz(
                    new StudioEulerDegrees(0.0, 725.0, 0.0))));
            var acceptedEntity = new SceneEntitySnapshot(
                entity.ObjectId,
                entity.RuntimeEntityId,
                entity.Name,
                transform);
            var accepted = ProjectSessionSnapshot.Ready(
                initial.Project!,
                new SceneDocumentSnapshot(
                    initial.Document!.SceneId,
                    initial.Document.Path,
                    revision: 3,
                    savedRevision: 1,
                    entities: [acceptedEntity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
            projectSession.Publish(
                accepted,
                context.EditId,
                originatingEditSucceeded: true);
            return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                accepted,
                "Updated entity Transform.",
                originatingEditId: context.EditId));
        };
        viewModel.ApplyEntityTransformCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);
        Assert.Equal("2.2", viewModel.PositionX);
        Assert.Equal("725", viewModel.RotationDegreesY);
        Assert.Equal("3.000", viewModel.ScaleZ);

        var externalEntity = new SceneEntitySnapshot(
            entity.ObjectId,
            entity.RuntimeEntityId,
            entity.Name,
            new TransformValue(
                Float3.Zero,
                StudioEulerRotation.QuaternionFromEulerDegreesYxz(
                    new StudioEulerDegrees(0.0, 10.0, 0.0)),
                Float3.One));
        var external = ProjectSessionSnapshot.Ready(
            initial.Project!,
            new SceneDocumentSnapshot(
                initial.Document!.SceneId,
                initial.Document.Path,
                revision: 4,
                savedRevision: 1,
                entities: [externalEntity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        projectSession.SetNameHandler = (_, _, _) => ValueTask.FromResult(
            ProjectSessionOperationResult.Success(
                external,
                "Accepted an external snapshot."));
        viewModel.ApplyEntityNameCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.Equal("0", viewModel.PositionX);
        Assert.InRange(
            double.Parse(
                viewModel.RotationDegreesY,
                System.Globalization.CultureInfo.InvariantCulture),
            729.999,
            730.001);
        Assert.Equal("1", viewModel.ScaleZ);
    }

    [Fact]
    public async Task Successful_no_op_ack_preserves_source_text_and_clears_all_transform_dirty_fields()
    {
        var entity = new SceneEntitySnapshot(
            Guid.NewGuid(),
            new EntityId(1, 1),
            "Rotated",
            TransformValue.Identity);
        var initialBase = Ready("Sample", "C:\\Projects\\Sample");
        var initial = ProjectSessionSnapshot.Ready(
            initialBase.Project!,
            new SceneDocumentSnapshot(
                initialBase.Document!.SceneId,
                initialBase.Document.Path,
                revision: 1,
                savedRevision: 1,
                entities: [entity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        projectSession.SetTransformHandler = (_, _, context, _) =>
        {
            projectSession.Publish(
                initial,
                context.EditId,
                originatingEditSucceeded: true);
            return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                initial,
                "Transform already matched.",
                originatingEditId: context.EditId));
        };
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();
        viewModel.SelectedEntity = entity;
        viewModel.PositionX = "-0.000";
        viewModel.RotationDegreesY = "0.0000";
        viewModel.ScaleX = "1.000";

        viewModel.ApplyEntityTransformCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);
        Assert.Equal("-0.000", viewModel.PositionX);
        Assert.Equal("0.0000", viewModel.RotationDegreesY);
        Assert.Equal("1.000", viewModel.ScaleX);
        var externallyRotated = new SceneEntitySnapshot(
            entity.ObjectId,
            entity.RuntimeEntityId,
            entity.Name,
            new TransformValue(
                new Float3(2.5F, 0.0F, 0.0F),
                StudioEulerRotation.QuaternionFromEulerDegreesYxz(
                    new StudioEulerDegrees(0.0, 10.0, 0.0)),
                new Float3(3.5F, 1.0F, 1.0F)));
        var external = ProjectSessionSnapshot.Ready(
            initial.Project!,
            new SceneDocumentSnapshot(
                initial.Document!.SceneId,
                initial.Document.Path,
                revision: 2,
                savedRevision: 1,
                entities: [externallyRotated]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        projectSession.SetNameHandler = (_, _, _) => ValueTask.FromResult(
            ProjectSessionOperationResult.Success(
                external,
                "Accepted an external snapshot."));
        viewModel.ApplyEntityNameCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.Equal("2.5", viewModel.PositionX);
        Assert.InRange(
            double.Parse(
                viewModel.RotationDegreesY,
                System.Globalization.CultureInfo.InvariantCulture),
            9.999,
            10.001);
        Assert.Equal("3.5", viewModel.ScaleX);
    }

    [Fact]
    public async Task Failed_own_receipt_rebases_the_draft_without_accepting_it()
    {
        var entity = new SceneEntitySnapshot(
            Guid.NewGuid(),
            new EntityId(1, 1),
            "Rotated",
            TransformValue.Identity);
        var initialBase = Ready("Sample", "C:\\Projects\\Sample");
        var initial = ProjectSessionSnapshot.Ready(
            initialBase.Project!,
            new SceneDocumentSnapshot(
                initialBase.Document!.SceneId,
                initialBase.Document.Path,
                revision: 1,
                savedRevision: 1,
                entities: [entity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        var externalTransform = new TransformValue(
            new Float3(9.0F, 8.0F, 0.0F),
            StudioEulerRotation.QuaternionFromEulerDegreesYxz(
                new StudioEulerDegrees(15.0, 20.0, 30.0)),
            new Float3(1.0F, 7.0F, 6.0F));
        var conflicted = ProjectSessionSnapshot.Ready(
            initial.Project!,
            new SceneDocumentSnapshot(
                initial.Document!.SceneId,
                initial.Document.Path,
                revision: 2,
                savedRevision: 1,
                entities:
                [
                    new SceneEntitySnapshot(
                        entity.ObjectId,
                        entity.RuntimeEntityId,
                        entity.Name,
                        externalTransform),
                ]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        ProjectSessionEditContext? secondContext = null;
        var invocation = 0;
        projectSession.SetTransformHandler = (_, transform, context, _) =>
        {
            invocation++;
            if (invocation == 1)
            {
                projectSession.Publish(
                    conflicted,
                    context.EditId,
                    originatingEditSucceeded: false);
                return ValueTask.FromResult(ProjectSessionOperationResult.Failed(
                    conflicted,
                    ProjectSessionFailureKind.RevisionConflict,
                    "Revision conflict.",
                    context.EditId));
            }
            secondContext = context;
            Assert.Equal(1.2F, transform.Position.X);
            Assert.Equal(8.0F, transform.Position.Y);
            Assert.Equal(7.0F, transform.Scale.Y);
            Assert.Equal(1.234567F, transform.Scale.Z);
            return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                conflicted,
                "Recorded retry.",
                originatingEditId: context.EditId));
        };
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();
        viewModel.SelectedEntity = entity;
        viewModel.PositionX = "1.2";
        viewModel.RotationDegreesY = "365";
        viewModel.ScaleZ = "1.234567";

        viewModel.ApplyEntityTransformCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);
        Assert.Equal("1.2", viewModel.PositionX);
        Assert.Equal("8", viewModel.PositionY);
        Assert.Equal("365", viewModel.RotationDegreesY);
        Assert.NotEqual("0", viewModel.RotationDegreesX);
        Assert.NotEqual("0", viewModel.RotationDegreesZ);
        Assert.Equal("7", viewModel.ScaleY);
        Assert.Equal("1.234567", viewModel.ScaleZ);

        viewModel.ApplyEntityTransformCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.True(secondContext.HasValue);
        Assert.Equal(2UL, secondContext.Value.ExpectedRevision);
    }

    [Fact]
    public async Task External_quaternion_update_selects_the_equivalent_euler_nearest_hint()
    {
        var objectId = Guid.NewGuid();
        var entity = new SceneEntitySnapshot(
            objectId,
            new EntityId(1, 1),
            "Rotated",
            TransformValue.Identity);
        var initialBase = Ready("Sample", "C:\\Projects\\Sample");
        var initial = ProjectSessionSnapshot.Ready(
            initialBase.Project!,
            new SceneDocumentSnapshot(
                initialBase.Document!.SceneId,
                initialBase.Document.Path,
                revision: 1,
                savedRevision: 1,
                entities: [entity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        projectSession.SetTransformHandler = (_, transform, context, _) =>
        {
            var acknowledgedEntity = new SceneEntitySnapshot(
                entity.ObjectId,
                entity.RuntimeEntityId,
                entity.Name,
                transform);
            var acknowledged = ProjectSessionSnapshot.Ready(
                initial.Project!,
                new SceneDocumentSnapshot(
                    initial.Document!.SceneId,
                    initial.Document.Path,
                    revision: 2,
                    savedRevision: 1,
                    entities: [acknowledgedEntity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
            projectSession.Publish(
                acknowledged,
                context.EditId,
                originatingEditSucceeded: true);
            return ValueTask.FromResult(ProjectSessionOperationResult.Success(
                acknowledged,
                "Updated entity Transform.",
                originatingEditId: context.EditId));
        };
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();
        viewModel.SelectedEntity = entity;
        viewModel.RotationDegreesY = "365";
        viewModel.ApplyEntityTransformCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);
        Assert.Equal("365", viewModel.RotationDegreesY);

        var externalEntity = new SceneEntitySnapshot(
            entity.ObjectId,
            entity.RuntimeEntityId,
            entity.Name,
            new TransformValue(
                Float3.Zero,
                StudioEulerRotation.QuaternionFromEulerDegreesYxz(
                    new StudioEulerDegrees(0.0, 10.0, 0.0)),
                Float3.One));
        var external = ProjectSessionSnapshot.Ready(
            initial.Project!,
            new SceneDocumentSnapshot(
                initial.Document!.SceneId,
                initial.Document.Path,
                revision: 3,
                savedRevision: 1,
                entities: [externalEntity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        projectSession.SetNameHandler = (_, _, _) => ValueTask.FromResult(
            ProjectSessionOperationResult.Success(
                external,
                "Accepted an external snapshot."));

        viewModel.ApplyEntityNameCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.InRange(
            double.Parse(
                viewModel.RotationDegreesY,
                System.Globalization.CultureInfo.InvariantCulture),
            369.999,
            370.001);
        Assert.Equal("0", viewModel.RotationDegreesX);
        Assert.Equal("0", viewModel.RotationDegreesZ);
    }

    [Fact]
    public void Changing_selection_discards_the_entire_transform_editor_session()
    {
        var first = new SceneEntitySnapshot(
            Guid.NewGuid(),
            new EntityId(1, 1),
            "First",
            new TransformValue(
                Float3.Zero,
                StudioEulerRotation.QuaternionFromEulerDegreesYxz(
                    new StudioEulerDegrees(0.0, 5.0, 0.0)),
                Float3.One));
        var second = new SceneEntitySnapshot(
            Guid.NewGuid(),
            new EntityId(2, 1),
            "Second",
            TransformValue.Identity);
        var initialBase = Ready("Sample", "C:\\Projects\\Sample");
        var initial = ProjectSessionSnapshot.Ready(
            initialBase.Project!,
            new SceneDocumentSnapshot(
                initialBase.Document!.SceneId,
                initialBase.Document.Path,
                revision: 1,
                savedRevision: 1,
                entities: [first, second]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();
        viewModel.SelectedEntity = first;
        viewModel.PositionX = "1.2";
        viewModel.RotationDegreesY = "365";
        viewModel.ScaleZ = "1.234567";

        viewModel.SelectedEntity = second;
        viewModel.SelectedEntity = first;

        Assert.Equal("0", viewModel.PositionX);
        Assert.InRange(
            double.Parse(
                viewModel.RotationDegreesY,
                System.Globalization.CultureInfo.InvariantCulture),
            4.999,
            5.001);
        Assert.NotEqual("365", viewModel.RotationDegreesY);
        Assert.Equal("1", viewModel.ScaleZ);
    }

    [Fact]
    public void Inspector_projects_equivalent_quaternion_signs_to_the_same_yxz_euler_degrees()
    {
        const float halfRootTwo = 0.70710677F;
        var positive = new SceneEntitySnapshot(
            Guid.NewGuid(),
            new EntityId(1, 1),
            "Positive",
            new TransformValue(
                Float3.Zero,
                new Quaternion(0.0F, halfRootTwo, 0.0F, halfRootTwo),
                Float3.One));
        var negative = new SceneEntitySnapshot(
            Guid.NewGuid(),
            new EntityId(2, 1),
            "Negative",
            new TransformValue(
                Float3.Zero,
                new Quaternion(0.0F, -halfRootTwo, 0.0F, -halfRootTwo),
                Float3.One));
        var initialBase = Ready("Sample", "C:\\Projects\\Sample");
        var initial = ProjectSessionSnapshot.Ready(
            initialBase.Project!,
            new SceneDocumentSnapshot(
                initialBase.Document!.SceneId,
                initialBase.Document.Path,
                revision: 1,
                savedRevision: 1,
                entities: [positive, negative]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();

        viewModel.SelectedEntity = positive;
        var positiveDegrees = (
            viewModel.RotationDegreesX,
            viewModel.RotationDegreesY,
            viewModel.RotationDegreesZ);
        viewModel.SelectedEntity = negative;

        Assert.Equal(positiveDegrees, (
            viewModel.RotationDegreesX,
            viewModel.RotationDegreesY,
            viewModel.RotationDegreesZ));
        Assert.Equal("0", viewModel.RotationDegreesX);
        Assert.InRange(
            float.Parse(viewModel.RotationDegreesY, System.Globalization.CultureInfo.InvariantCulture),
            89.999F,
            90.001F);
        Assert.Equal("0", viewModel.RotationDegreesZ);
    }

    [Theory]
    [InlineData(90.0F, 35.0F, -35.0F)]
    [InlineData(-90.0F, 15.0F, 15.0F)]
    public void Inspector_uses_the_nearest_zero_hint_yxz_family_at_gimbal_lock(
        float rotationDegreesX,
        float expectedRotationDegreesY,
        float expectedRotationDegreesZ)
    {
        var numericsRotation = System.Numerics.Quaternion.CreateFromYawPitchRoll(
            50.0F * MathF.PI / 180.0F,
            rotationDegreesX * MathF.PI / 180.0F,
            -20.0F * MathF.PI / 180.0F);
        var entity = new SceneEntitySnapshot(
            Guid.NewGuid(),
            new EntityId(1, 1),
            "Gimbal Lock",
            new TransformValue(
                Float3.Zero,
                new Quaternion(
                    numericsRotation.X,
                    numericsRotation.Y,
                    numericsRotation.Z,
                    numericsRotation.W),
                Float3.One));
        var initialBase = Ready("Sample", "C:\\Projects\\Sample");
        var initial = ProjectSessionSnapshot.Ready(
            initialBase.Project!,
            new SceneDocumentSnapshot(
                initialBase.Document!.SceneId,
                initialBase.Document.Path,
                revision: 1,
                savedRevision: 1,
                entities: [entity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();

        viewModel.SelectedEntity = entity;

        Assert.InRange(
            float.Parse(viewModel.RotationDegreesX, System.Globalization.CultureInfo.InvariantCulture),
            rotationDegreesX - 0.01F,
            rotationDegreesX + 0.01F);
        Assert.InRange(
            float.Parse(viewModel.RotationDegreesY, System.Globalization.CultureInfo.InvariantCulture),
            expectedRotationDegreesY - 0.01F,
            expectedRotationDegreesY + 0.01F);
        Assert.InRange(
            float.Parse(viewModel.RotationDegreesZ, System.Globalization.CultureInfo.InvariantCulture),
            expectedRotationDegreesZ - 0.01F,
            expectedRotationDegreesZ + 0.01F);
    }

    [Theory]
    [InlineData(89.95F)]
    [InlineData(-89.95F)]
    public void Inspector_preserves_near_gimbal_yxz_rotation_without_quantizing_to_lock(
        float rotationDegreesX)
    {
        var numericsRotation = System.Numerics.Quaternion.CreateFromYawPitchRoll(
            50.0F * MathF.PI / 180.0F,
            rotationDegreesX * MathF.PI / 180.0F,
            -20.0F * MathF.PI / 180.0F);
        var entity = new SceneEntitySnapshot(
            Guid.NewGuid(),
            new EntityId(1, 1),
            "Near Gimbal Lock",
            new TransformValue(
                Float3.Zero,
                new Quaternion(
                    numericsRotation.X,
                    numericsRotation.Y,
                    numericsRotation.Z,
                    numericsRotation.W),
                Float3.One));
        var initialBase = Ready("Sample", "C:\\Projects\\Sample");
        var initial = ProjectSessionSnapshot.Ready(
            initialBase.Project!,
            new SceneDocumentSnapshot(
                initialBase.Document!.SceneId,
                initialBase.Document.Path,
                revision: 1,
                savedRevision: 1,
                entities: [entity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();

        viewModel.SelectedEntity = entity;

        Assert.InRange(
            float.Parse(viewModel.RotationDegreesX, System.Globalization.CultureInfo.InvariantCulture),
            rotationDegreesX - 0.01F,
            rotationDegreesX + 0.01F);
        Assert.InRange(
            float.Parse(viewModel.RotationDegreesY, System.Globalization.CultureInfo.InvariantCulture),
            49.99F,
            50.01F);
        Assert.InRange(
            float.Parse(viewModel.RotationDegreesZ, System.Globalization.CultureInfo.InvariantCulture),
            -20.01F,
            -19.99F);
    }

    [Fact]
    public void Invalid_transform_text_is_rejected_with_visible_operation_feedback()
    {
        var entity = new SceneEntitySnapshot(
            Guid.NewGuid(),
            new EntityId(1, 1),
            "Directional Wedge",
            TransformValue.Identity,
            SceneMeshReference.DirectionalWedgeValidation);
        var initialBase = Ready("Sample", "C:\\Projects\\Sample");
        var initial = ProjectSessionSnapshot.Ready(
            initialBase.Project!,
            new SceneDocumentSnapshot(
                initialBase.Document!.SceneId,
                initialBase.Document.Path,
                revision: 1,
                savedRevision: 1,
                entities: [entity]),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        var requested = false;
        projectSession.SetTransformHandler = (_, _, _, _) =>
        {
            requested = true;
            throw new InvalidOperationException("Invalid input must not reach the project session.");
        };
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();
        viewModel.SelectedEntity = entity;
        viewModel.RotationDegreesY = "NaN";

        viewModel.ApplyEntityTransformCommand.Execute(null);

        Assert.False(requested);
        Assert.True(viewModel.HasProjectOperationMessage);
        Assert.Contains("rotation is expressed in degrees", viewModel.ProjectOperationMessage);
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
                entities: []),
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);

    private static async Task WaitUntilAsync(Func<bool> condition)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(1));
        while (!condition())
        {
            await Task.Delay(10, timeout.Token);
        }
    }
}
