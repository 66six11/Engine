using System;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.EngineBridge.Project;
using Asharia.Studio.EngineBridge.Scene;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class StudioSceneEditingAcceptanceTests
{
    [Fact]
    [SuppressMessage(
        "xUnit",
        "xUnit1031",
        Justification = "The native owner-lane acceptance flow must run outside xUnit's synchronization context.")]
    public void Transform_apply_undo_redo_save_and_reopen_preserves_the_authoritative_scene()
    {
        if (!OperatingSystem.IsWindows())
        {
            return;
        }

        Task.Run(RunAcceptanceAsync)
            .WaitAsync(TimeSpan.FromSeconds(30))
            .GetAwaiter()
            .GetResult();
    }

    private static async Task RunAcceptanceAsync()
    {

        var parent = Path.Combine(
            Path.GetTempPath(),
            $"asharia-studio-scene-acceptance-{Guid.NewGuid():N}");
        Directory.CreateDirectory(parent);
        try
        {
            Guid objectId;
            var savedTransform = new TransformValue(
                new Float3(1, 2, 3),
                Quaternion.Identity,
                new Float3(2, 2, 2));
            var halfRootTwo = MathF.Sqrt(0.5F);
            var editedTransform = new TransformValue(
                new Float3(-4, 5.5F, 6),
                new Quaternion(0, halfRootTwo, 0, halfRootTwo),
                new Float3(0.5F, 1.5F, 3));
            var session = CreateSession();
            try
            {
                var createdProject = await Await(session.CreateProjectAsync(parent, "Sample"))
                    .ConfigureAwait(false);
                Assert.True(createdProject.Succeeded, createdProject.Message);
                Assert.True(File.Exists(createdProject.Current.Document!.Path));
                Assert.Empty(createdProject.Current.Document.Entities);
                var initialSceneText = await File.ReadAllTextAsync(
                    createdProject.Current.Document.Path).ConfigureAwait(false);
                Assert.Contains("\"schemaVersion\": 2", initialSceneText, StringComparison.Ordinal);

                var mesh = SceneMeshReference.DirectionalWedgeValidation;
                var createdEntity = await Await(session.CreateMeshEntityAsync("Entity", mesh))
                    .ConfigureAwait(false);
                Assert.True(createdEntity.Succeeded, createdEntity.Message);
                var createdSnapshot = Assert.Single(createdEntity.Current.Document!.Entities);
                objectId = createdSnapshot.ObjectId;
                Assert.Equal(objectId, createdEntity.CreatedObjectId!.Value);
                Assert.Equal(mesh, createdSnapshot.Mesh);

                var renamed = await Await(session.SetEntityNameAsync(objectId, "主角"))
                    .ConfigureAwait(false);
                Assert.True(renamed.Succeeded, renamed.Message);
                var movedToSavedState = await Await(session.SetEntityTransformAsync(
                        objectId,
                        savedTransform,
                        new ProjectSessionEditContext(
                            ProjectEditId.CreateNew(),
                            renamed.Current.Document!.Revision)))
                    .ConfigureAwait(false);
                Assert.True(movedToSavedState.Succeeded, movedToSavedState.Message);
                Assert.Equal(
                    renamed.Current.Document.Revision + 1,
                    movedToSavedState.Current.Document!.Revision);
                Assert.Equal(
                    savedTransform,
                    Assert.Single(movedToSavedState.Current.Document.Entities).Transform);
                Assert.True(movedToSavedState.Current.IsDirty);
                Assert.True(movedToSavedState.Current.CanUndo);
                Assert.False(movedToSavedState.Current.CanRedo);

                var savedState = await Await(session.SaveSceneAsync()).ConfigureAwait(false);
                Assert.True(savedState.Succeeded, savedState.Message);
                Assert.Equal(
                    movedToSavedState.Current.Document.Revision,
                    savedState.Current.Document!.Revision);
                Assert.Equal(
                    savedState.Current.Document.Revision,
                    savedState.Current.Document.SavedRevision);
                Assert.Equal(
                    savedState.Current.CurrentContentStateId,
                    savedState.Current.SavedContentStateId);
                Assert.False(savedState.Current.IsDirty);
                Assert.True(savedState.Current.CanUndo);

                var movedToEditedState = await Await(session.SetEntityTransformAsync(
                        objectId,
                        editedTransform,
                        new ProjectSessionEditContext(
                            ProjectEditId.CreateNew(),
                            savedState.Current.Document.Revision)))
                    .ConfigureAwait(false);
                Assert.True(movedToEditedState.Succeeded, movedToEditedState.Message);
                Assert.Equal(
                    savedState.Current.Document.Revision + 1,
                    movedToEditedState.Current.Document!.Revision);
                Assert.Equal(
                    savedState.Current.Document.Revision,
                    movedToEditedState.Current.Document.SavedRevision);
                Assert.Equal(
                    editedTransform,
                    Assert.Single(movedToEditedState.Current.Document.Entities).Transform);
                Assert.NotEqual(
                    movedToEditedState.Current.CurrentContentStateId,
                    movedToEditedState.Current.SavedContentStateId);
                Assert.True(movedToEditedState.Current.IsDirty);
                Assert.True(movedToEditedState.Current.CanUndo);
                Assert.False(movedToEditedState.Current.CanRedo);

                var undone = await Await(session.UndoAsync()).ConfigureAwait(false);
                Assert.True(undone.Succeeded, undone.Message);
                Assert.Equal(
                    movedToEditedState.Current.Document.Revision + 1,
                    undone.Current.Document!.Revision);
                Assert.Equal(
                    savedTransform,
                    Assert.Single(undone.Current.Document.Entities).Transform);
                Assert.NotEqual(
                    undone.Current.Document.Revision,
                    undone.Current.Document.SavedRevision);
                Assert.Equal(
                    undone.Current.CurrentContentStateId,
                    undone.Current.SavedContentStateId);
                Assert.False(undone.Current.IsDirty);
                Assert.True(undone.Current.CanUndo);
                Assert.True(undone.Current.CanRedo);

                var redone = await Await(session.RedoAsync()).ConfigureAwait(false);
                Assert.True(redone.Succeeded, redone.Message);
                Assert.Equal(
                    undone.Current.Document.Revision + 1,
                    redone.Current.Document!.Revision);
                Assert.Equal(
                    editedTransform,
                    Assert.Single(redone.Current.Document.Entities).Transform);
                Assert.NotEqual(
                    redone.Current.CurrentContentStateId,
                    redone.Current.SavedContentStateId);
                Assert.True(redone.Current.IsDirty);
                Assert.True(redone.Current.CanUndo);
                Assert.False(redone.Current.CanRedo);

                var savedEditedState = await Await(session.SaveSceneAsync()).ConfigureAwait(false);
                Assert.True(savedEditedState.Succeeded, savedEditedState.Message);
                Assert.Equal(
                    redone.Current.Document.Revision,
                    savedEditedState.Current.Document!.Revision);
                Assert.Equal(
                    savedEditedState.Current.Document.Revision,
                    savedEditedState.Current.Document.SavedRevision);
                Assert.Equal(
                    savedEditedState.Current.CurrentContentStateId,
                    savedEditedState.Current.SavedContentStateId);
                Assert.False(savedEditedState.Current.IsDirty);
            }
            finally
            {
                await session.DisposeAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(5))
                    .ConfigureAwait(false);
            }

            var reopenedSession = CreateSession();
            try
            {
                var reopened = await Await(reopenedSession.OpenProjectAsync(
                    Path.Combine(parent, "Sample", "asharia.project.json")))
                    .ConfigureAwait(false);
                Assert.True(reopened.Succeeded, reopened.Message);
                var entity = Assert.Single(reopened.Current.Document!.Entities);
                Assert.Equal(objectId, entity.ObjectId);
                Assert.Equal("主角", entity.Name);
                Assert.Equal(editedTransform, entity.Transform);
                Assert.Equal(SceneMeshReference.DirectionalWedgeValidation, entity.Mesh);
                Assert.False(reopened.Current.IsDirty);
                Assert.Equal(
                    reopened.Current.Document.Revision,
                    reopened.Current.Document.SavedRevision);
                Assert.False(reopened.Current.CanUndo);
                Assert.False(reopened.Current.CanRedo);
            }
            finally
            {
                await reopenedSession.DisposeAsync().AsTask().WaitAsync(TimeSpan.FromSeconds(5))
                    .ConfigureAwait(false);
            }
        }
        finally
        {
            if (Directory.Exists(parent))
            {
                Directory.Delete(parent, recursive: true);
            }
        }
    }

    private static ProjectSession CreateSession() =>
        new(new ProjectDescriptorBridge(), new SceneDocumentBridge());

    private static Task<ProjectSessionOperationResult> Await(
        ValueTask<ProjectSessionOperationResult> operation) =>
        operation.AsTask().WaitAsync(TimeSpan.FromSeconds(5));
}
