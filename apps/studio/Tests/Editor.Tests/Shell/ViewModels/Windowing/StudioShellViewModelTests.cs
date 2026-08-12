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

    [Fact]
    public async Task Apply_transform_command_publishes_complete_local_trs_for_selected_mesh()
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
                entities: [initialEntity]));
        var expectedNumericsRotation = System.Numerics.Quaternion.CreateFromYawPitchRoll(
            40.0F * MathF.PI / 180.0F,
            30.0F * MathF.PI / 180.0F,
            -20.0F * MathF.PI / 180.0F);
        var expectedTransform = new TransformValue(
            new Float3(10.0F, 20.0F, 30.0F),
            new Quaternion(
                expectedNumericsRotation.X,
                expectedNumericsRotation.Y,
                expectedNumericsRotation.Z,
                expectedNumericsRotation.W),
            new Float3(2.0F, 3.0F, 4.0F));
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        var requestObserved = false;
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
                    entities: [transformedEntity]));
            projectSession.Publish(updated, editContext.EditId);
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
        viewModel.PositionX = "10";
        viewModel.PositionY = "20";
        viewModel.PositionZ = "30";
        viewModel.RotationDegreesX = "30";
        viewModel.RotationDegreesY = "40";
        viewModel.RotationDegreesZ = "-20";
        viewModel.ScaleX = "2";
        viewModel.ScaleY = "3";
        viewModel.ScaleZ = "4";

        viewModel.ApplyEntityTransformCommand.Execute(null);
        await WaitUntilAsync(() => !viewModel.IsProjectOperationRunning);

        Assert.True(requestObserved);
        Assert.NotNull(viewModel.SelectedEntity);
        Assert.Equal(objectId, viewModel.SelectedEntity.ObjectId);
        Assert.Equal(runtimeEntityId, viewModel.SelectedEntity.RuntimeEntityId);
        Assert.Equal(SceneMeshReference.DirectionalWedgeValidation, viewModel.SelectedEntity.Mesh);
        Assert.Equal(expectedTransform.Position, viewModel.SelectedEntity.Transform.Position);
        Assert.Equal(expectedTransform.Scale, viewModel.SelectedEntity.Transform.Scale);
        Assert.Equal("30", viewModel.RotationDegreesX);
        Assert.Equal("40", viewModel.RotationDegreesY);
        Assert.Equal("-20", viewModel.RotationDegreesZ);
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
                entities: [entity]));
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
                    entities: [updatedEntity]));
            projectSession.Publish(updated, context.EditId);
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
                entities: [entity]));
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
                    entities: [updatedEntity]));
            projectSession.Publish(updated, context.EditId);
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
                entities: [entity]));
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
                    entities: [acknowledgedEntity]));
            projectSession.Publish(acknowledged, context.EditId);
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
                entities: [externalEntity]));
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
    public void Changing_selection_discards_the_rotation_editor_session()
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
                entities: [first, second]));
        var projectSession = new TestProjectSession();
        projectSession.Publish(initial);
        using var viewModel = new StudioShellViewModel(
            projectSession,
            new TestProjectDialogService());
        viewModel.MarkReady();
        viewModel.SelectedEntity = first;
        viewModel.RotationDegreesY = "365";

        viewModel.SelectedEntity = second;
        viewModel.SelectedEntity = first;

        Assert.InRange(
            double.Parse(
                viewModel.RotationDegreesY,
                System.Globalization.CultureInfo.InvariantCulture),
            4.999,
            5.001);
        Assert.NotEqual("365", viewModel.RotationDegreesY);
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
                entities: [positive, negative]));
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
                entities: [entity]));
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
                entities: [entity]));
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
                entities: [entity]));
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
