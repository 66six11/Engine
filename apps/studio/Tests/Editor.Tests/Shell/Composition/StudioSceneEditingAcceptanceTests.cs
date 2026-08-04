using System;
using System.Diagnostics.CodeAnalysis;
using System.IO;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Projects;
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
    public void Create_edit_save_close_and_reopen_preserves_the_default_scene()
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
            var transform = new TransformValue(
                new Float3(1, 2, 3),
                Quaternion.Identity,
                new Float3(2, 2, 2));
            var session = CreateSession();
            try
            {
                var createdProject = await Await(session.CreateProjectAsync(parent, "Sample"))
                    .ConfigureAwait(false);
                Assert.True(createdProject.Succeeded, createdProject.Message);
                Assert.True(File.Exists(createdProject.Current.Document!.Path));
                Assert.Empty(createdProject.Current.Document.Entities);

                var createdEntity = await Await(session.CreateEntityAsync("Entity"))
                    .ConfigureAwait(false);
                Assert.True(createdEntity.Succeeded, createdEntity.Message);
                objectId = Assert.Single(createdEntity.Current.Document!.Entities).ObjectId;

                var renamed = await Await(session.SetEntityNameAsync(objectId, "主角"))
                    .ConfigureAwait(false);
                Assert.True(renamed.Succeeded, renamed.Message);
                var moved = await Await(session.SetEntityTransformAsync(objectId, transform))
                    .ConfigureAwait(false);
                Assert.True(moved.Succeeded, moved.Message);
                Assert.True(moved.Current.Document!.IsDirty);

                var saved = await Await(session.SaveSceneAsync()).ConfigureAwait(false);
                Assert.True(saved.Succeeded, saved.Message);
                Assert.False(saved.Current.Document!.IsDirty);
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
                Assert.Equal(transform, entity.Transform);
                Assert.False(reopened.Current.Document.IsDirty);
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
