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
        Assert.False(result.Current.Document!.IsDirty);
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
        var moved = await session.SetEntityTransformAsync(
            objectId,
            new TransformValue(
                new Float3(1, 2, 3),
                Quaternion.Identity,
                new Float3(2, 2, 2)));
        var saved = await session.SaveSceneAsync();

        Assert.True(created.Succeeded);
        Assert.True(renamed.Succeeded);
        Assert.True(moved.Succeeded);
        Assert.True(created.Current.Document.IsDirty);
        Assert.Equal("主角", moved.Current.Document!.Entities.Single().Name);
        Assert.Equal(new Float3(1, 2, 3), moved.Current.Document.Entities.Single().Transform.Position);
        Assert.False(saved.Current.Document!.IsDirty);
        Assert.Equal(saved.Current.Document.Revision, saved.Current.Document.SavedRevision);
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

        public ValueTask<SceneDocumentOperationResult> CreateEntityAsync(
            Guid objectId,
            string name,
            ulong expectedRevision,
            CancellationToken cancellationToken = default)
        {
            entities_.Add(new SceneEntitySnapshot(objectId, name, TransformValue.Identity));
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
                name,
                entity.Transform);
            Advance();
            return ValueTask.FromResult(SceneDocumentOperationResult.Success(Current));
        }

        public ValueTask<SceneDocumentOperationResult> SetEntityTransformAsync(
            Guid objectId,
            TransformValue transform,
            ulong expectedRevision,
            CancellationToken cancellationToken = default)
        {
            var entity = entities_.Single(value => value.ObjectId == objectId);
            entities_[entities_.IndexOf(entity)] = new SceneEntitySnapshot(
                entity.ObjectId,
                entity.Name,
                transform);
            Advance();
            return ValueTask.FromResult(SceneDocumentOperationResult.Success(Current));
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
