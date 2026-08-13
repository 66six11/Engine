using System;
using System.Collections.Generic;
using System.Collections.Immutable;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Assets;
using Asharia.Studio.Application.Projects;
using Asharia.Studio.Application.Scenes;
using Xunit;

namespace Asharia.Studio.Application.Tests.Assets;

public sealed class ProjectAssetCatalogTests
{
    private static readonly Guid TestProjectId =
        Guid.Parse("1dfbe476-3090-4b95-9350-d6b4d8c9b72a");
    [Fact]
    public async Task Project_scope_loads_once_and_ignores_scene_only_publications()
    {
        var session = new TestProjectSession();
        var gateway = new ControlledAssetCatalogGateway();
        await using var catalog = new ProjectAssetCatalog(session, gateway);

        session.Publish(ReadyProject("Sample"));

        var request = await gateway.WaitForRequestAsync(0);
        Assert.Equal(AssetCatalogSessionState.Loading, catalog.Current.State);
        Assert.Equal("editor-preview", request.Scope.TargetProfile);
        Assert.EndsWith("asharia.project.json", request.Scope.ProjectFilePath);
        request.Complete(AssetCatalogQueryResult.Success(Snapshot(1)));
        await WaitForState(catalog, AssetCatalogSessionState.Ready);
        Assert.Equal(AssetCatalogSessionState.Ready, catalog.Current.State);

        session.Publish(ReadyProject(
            "Sample",
            sessionId: catalog.Current.Scope!.SessionId,
            projectId: catalog.Current.Scope.ProjectId,
            revision: 2));

        Assert.Equal(1, gateway.RequestCount);
    }

    [Fact]
    public async Task Project_switch_cancels_old_candidate_and_rejects_its_late_result()
    {
        var session = new TestProjectSession();
        var gateway = new ControlledAssetCatalogGateway();
        await using var catalog = new ProjectAssetCatalog(session, gateway);
        session.Publish(ReadyProject("First"));
        var first = await gateway.WaitForRequestAsync(0);

        session.Publish(ReadyProject("Second"));
        var second = await gateway.WaitForRequestAsync(1);

        Assert.True(first.Token.IsCancellationRequested);
        second.Complete(AssetCatalogQueryResult.Success(Snapshot(2)));
        await WaitForState(catalog, AssetCatalogSessionState.Ready);
        first.Complete(AssetCatalogQueryResult.Success(Snapshot(1)));
        await Task.Yield();

        Assert.Equal(2UL, catalog.Current.Catalog!.Revision);
        Assert.Equal("Second", catalog.Current.Scope!.ProjectRootPath.Split('\\').Last());
    }

    [Fact]
    public async Task Manual_refresh_is_newest_wins_and_preserves_last_good_on_failure()
    {
        var session = new TestProjectSession();
        var gateway = new ControlledAssetCatalogGateway();
        await using var catalog = new ProjectAssetCatalog(session, gateway);
        session.Publish(ReadyProject("Sample"));
        (await gateway.WaitForRequestAsync(0)).Complete(
            AssetCatalogQueryResult.Success(Snapshot(1)));
        await WaitForState(catalog, AssetCatalogSessionState.Ready);

        var olderRefresh = catalog.RefreshAsync().AsTask();
        var older = await gateway.WaitForRequestAsync(1);
        var newerRefresh = catalog.RefreshAsync().AsTask();
        var newer = await gateway.WaitForRequestAsync(2);
        newer.Complete(AssetCatalogQueryResult.Failed(Failure("refresh failed")));
        await newerRefresh;
        older.Complete(AssetCatalogQueryResult.Success(Snapshot(99)));
        await olderRefresh;

        Assert.Equal(AssetCatalogSessionState.Degraded, catalog.Current.State);
        Assert.Equal(1UL, catalog.Current.Catalog!.Revision);
        Assert.Equal("refresh failed", catalog.Current.Failure!.Message);
    }

    [Fact]
    public async Task Overlapping_refresh_cancellation_restores_the_last_stable_snapshot()
    {
        var session = new TestProjectSession();
        var gateway = new ControlledAssetCatalogGateway();
        await using var catalog = new ProjectAssetCatalog(session, gateway);
        session.Publish(ReadyProject("Sample"));
        (await gateway.WaitForRequestAsync(0)).Complete(
            AssetCatalogQueryResult.Success(Snapshot(1)));
        await WaitForState(catalog, AssetCatalogSessionState.Ready);

        var olderRefresh = catalog.RefreshAsync().AsTask();
        var older = await gateway.WaitForRequestAsync(1);
        using var cancellation = new CancellationTokenSource();
        var newerRefresh = catalog.RefreshAsync(cancellation.Token).AsTask();
        var newer = await gateway.WaitForRequestAsync(2);

        Assert.True(older.Token.IsCancellationRequested);
        cancellation.Cancel();
        newer.Cancel();
        await newerRefresh;

        Assert.Equal(AssetCatalogSessionState.Ready, catalog.Current.State);
        Assert.Equal(1UL, catalog.Current.Catalog!.Revision);

        older.Complete(AssetCatalogQueryResult.Success(Snapshot(99)));
        await olderRefresh;
        Assert.Equal(1UL, catalog.Current.Catalog!.Revision);
    }

    [Fact]
    public async Task First_failure_is_failed_and_no_project_clears_scope_and_last_good()
    {
        var session = new TestProjectSession();
        var gateway = new ControlledAssetCatalogGateway();
        await using var catalog = new ProjectAssetCatalog(session, gateway);
        session.Publish(ReadyProject("Sample"));
        (await gateway.WaitForRequestAsync(0)).Complete(
            AssetCatalogQueryResult.Failed(Failure("unavailable")));
        await WaitForState(catalog, AssetCatalogSessionState.Failed);

        Assert.Equal(AssetCatalogSessionState.Failed, catalog.Current.State);
        Assert.Null(catalog.Current.Catalog);

        session.Publish(ProjectSessionSnapshot.NoProject);

        Assert.Equal(AssetCatalogSessionState.NoProject, catalog.Current.State);
        Assert.Null(catalog.Current.Scope);
        Assert.Null(catalog.Current.Catalog);
        Assert.Null(catalog.Current.Failure);
    }

    [Theory]
    [InlineData(AssetCatalogSnapshotState.Degraded)]
    [InlineData(AssetCatalogSnapshotState.Failed)]
    public async Task Structurally_valid_partial_snapshot_remains_browsable_as_degraded(
        AssetCatalogSnapshotState snapshotState)
    {
        var session = new TestProjectSession();
        var gateway = new ControlledAssetCatalogGateway();
        await using var catalog = new ProjectAssetCatalog(session, gateway);
        session.Publish(ReadyProject("Sample"));
        (await gateway.WaitForRequestAsync(0)).Complete(
            AssetCatalogQueryResult.Success(Snapshot(1, snapshotState)));

        await WaitForState(catalog, AssetCatalogSessionState.Degraded);

        Assert.NotNull(catalog.Current.Catalog);
        Assert.Equal(snapshotState, catalog.Current.Catalog!.State);
        Assert.Null(catalog.Current.Failure);
    }

    [Fact]
    public async Task Refresh_after_completed_request_does_not_touch_a_disposed_cancellation_source()
    {
        var session = new TestProjectSession();
        var gateway = new ControlledAssetCatalogGateway();
        await using var catalog = new ProjectAssetCatalog(session, gateway);
        session.Publish(ReadyProject("Sample"));
        (await gateway.WaitForRequestAsync(0)).Complete(
            AssetCatalogQueryResult.Success(Snapshot(1)));
        await WaitForState(catalog, AssetCatalogSessionState.Ready);

        var refresh = catalog.RefreshAsync().AsTask();
        (await gateway.WaitForRequestAsync(1)).Complete(
            AssetCatalogQueryResult.Success(Snapshot(2)));
        await refresh;

        Assert.Equal(2UL, catalog.Current.Catalog!.Revision);
    }

    [Fact]
    public async Task Throwing_and_reentrant_subscribers_cannot_corrupt_publication_order()
    {
        var session = new TestProjectSession();
        var gateway = new ControlledAssetCatalogGateway();
        await using var catalog = new ProjectAssetCatalog(session, gateway);
        var states = new List<AssetCatalogSessionState>();
        var refreshStarted = false;
        catalog.SnapshotChanged += (_, _) => throw new InvalidOperationException("observer failed");
        catalog.SnapshotChanged += (_, e) =>
        {
            states.Add(e.Snapshot.State);
            if (e.Snapshot.State == AssetCatalogSessionState.Ready && !refreshStarted)
            {
                refreshStarted = true;
                _ = catalog.RefreshAsync();
            }
        };

        session.Publish(ReadyProject("Sample"));
        (await gateway.WaitForRequestAsync(0)).Complete(
            AssetCatalogQueryResult.Success(Snapshot(1)));
        await WaitForState(catalog, AssetCatalogSessionState.Loading);

        Assert.Equal(
            [AssetCatalogSessionState.Loading, AssetCatalogSessionState.Ready,
             AssetCatalogSessionState.Loading],
            states);
        var refresh = await gateway.WaitForRequestAsync(1);
        Assert.Equal(2, gateway.RequestCount);
        refresh.Complete(AssetCatalogQueryResult.Success(Snapshot(2)));
        await refresh.Completion;
    }

    [Fact]
    public async Task Dispose_cancels_and_waits_for_in_flight_gateway_work()
    {
        var session = new TestProjectSession();
        var gateway = new ControlledAssetCatalogGateway();
        var catalog = new ProjectAssetCatalog(session, gateway);
        session.Publish(ReadyProject("Sample"));
        var request = await gateway.WaitForRequestAsync(0);

        var dispose = catalog.DisposeAsync().AsTask();

        Assert.True(request.Token.IsCancellationRequested);
        Assert.False(dispose.IsCompleted);
        request.Complete(AssetCatalogQueryResult.Success(Snapshot(1)));
        await dispose;
        Assert.Throws<ObjectDisposedException>(() => catalog.RefreshAsync());
    }

    [Fact]
    public void Guid_selection_identity_ignores_source_path_but_untracked_identity_uses_it()
    {
        var guid = Guid.NewGuid();
        Assert.Equal(
            new AssetSelectionKey(guid, "Assets/Before.png"),
            new AssetSelectionKey(guid, "Assets/After.png"));
        Assert.NotEqual(
            new AssetSelectionKey(null, "Assets/Before.txt"),
            new AssetSelectionKey(null, "Assets/After.txt"));
    }

    [Fact]
    public void Asset_entry_requires_unique_initialized_subasset_identity()
    {
        var guid = Guid.NewGuid();
        var subAsset = new AssetCatalogSubAsset("mesh:0", "Body", "Mesh");
        var entry = new AssetCatalogEntry(
            new AssetSelectionKey(guid, "Assets/Model.glb"),
            guid,
            guid.ToString("D"),
            "Assets/Model.glb",
            "Assets",
            "Assets",
            "C:\\Projects\\Sample\\Assets",
            "C:\\Projects\\Sample\\Assets\\Model.glb",
            "C:\\Projects\\Sample\\Assets\\Model.glb.ameta",
            "Model",
            ".glb",
            "Model",
            "glTF",
            1,
            "default",
            "Model",
            AssetCatalogProductState.NotTracked,
            0,
            0,
            [subAsset],
            ImmutableArray<AssetCatalogDiagnostic>.Empty);

        Assert.Same(subAsset, Assert.Single(entry.SubAssets));
        Assert.Throws<ArgumentException>(() => new AssetCatalogEntry(
            entry.SelectionKey,
            guid,
            guid.ToString("D"),
            entry.SourcePath,
            entry.SourceRootName,
            entry.SourceRootPrefix,
            entry.SourceRootDirectory,
            entry.SourceFilePath,
            entry.MetadataFilePath,
            entry.DisplayName,
            entry.Extension,
            entry.AssetTypeName,
            entry.ImporterName,
            entry.ImporterVersion,
            entry.ImportProfileName,
            entry.AssetRoleName,
            entry.ProductState,
            0,
            0,
            [subAsset, subAsset],
            entry.Diagnostics));
    }

    [Fact]
    public void Navigation_preserves_asset_and_subasset_identity()
    {
        var guid = Guid.NewGuid();
        var asset = new AssetCatalogNavigationEntry(
            "asset:one",
            "folder:models",
            AssetCatalogNavigationKind.Asset,
            "Model.glb",
            "Assets/Models",
            "Assets/Models/Model.glb",
            "Assets",
            "Assets",
            "C:\\Projects\\Sample\\Assets",
            guid,
            string.Empty,
            "Model",
            "glTF",
            ".glb",
            "default",
            "Model",
            1,
            AssetCatalogProductState.Current,
            depth: 2);
        var subAsset = new AssetCatalogNavigationEntry(
            "subasset:one:mesh:0",
            asset.Key,
            AssetCatalogNavigationKind.SubAsset,
            "Body",
            "Assets/Models",
            asset.SourcePath,
            asset.SourceRootName,
            asset.SourceRootPrefix,
            asset.SourceRootDirectory,
            guid,
            "mesh:0",
            asset.AssetTypeName,
            asset.ImporterName,
            asset.Extension,
            asset.ImportProfileName,
            "Mesh",
            0,
            asset.ProductState,
            depth: 3);

        Assert.Equal(AssetCatalogNavigationKind.Asset, asset.Kind);
        Assert.Equal(guid, subAsset.AssetGuid);
        Assert.Equal("mesh:0", subAsset.StableId);
        Assert.Throws<ArgumentException>(() => new AssetCatalogNavigationEntry(
            "subasset:invalid",
            asset.Key,
            AssetCatalogNavigationKind.SubAsset,
            "Body",
            asset.ScopePath,
            asset.SourcePath,
            asset.SourceRootName,
            asset.SourceRootPrefix,
            asset.SourceRootDirectory,
            guid,
            stableId: string.Empty,
            asset.AssetTypeName,
            asset.ImporterName,
            asset.Extension,
            asset.ImportProfileName,
            asset.AssetRoleName,
            subAssetCount: 0,
            asset.ProductState,
            depth: 3));
    }

    private static async Task WaitForState(
        IProjectAssetCatalog catalog,
        AssetCatalogSessionState state)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(2));
        while (catalog.Current.State != state)
        {
            await Task.Delay(5, timeout.Token);
        }
    }

    private static AssetCatalogSnapshot Snapshot(
        ulong revision,
        AssetCatalogSnapshotState state = AssetCatalogSnapshotState.Ready) =>
        new(
            state,
            revision,
            DateTimeOffset.UtcNow,
            TestProjectId,
            "C:\\Projects\\Sample\\asharia.project.json",
            "C:\\Projects\\Sample\\.asharia\\cache\\assets\\manifest.json",
            "editor-preview",
            ImmutableArray<AssetCatalogSourceRoot>.Empty,
            ImmutableArray<AssetCatalogNavigationEntry>.Empty,
            ImmutableArray<AssetCatalogEntry>.Empty,
            ImmutableArray<AssetCatalogDiagnostic>.Empty);

    private static AssetCatalogQueryFailure Failure(string message) =>
        new(AssetCatalogQueryFailureKind.IoFailure, message);

    private static ProjectSessionSnapshot ReadyProject(
        string name,
        ProjectSessionId? sessionId = null,
        Guid? projectId = null,
        ulong revision = 1)
    {
        var project = new ActiveProjectSnapshot(
            sessionId ?? ProjectSessionId.CreateNew(),
            projectId ?? TestProjectId,
            name,
            $"C:\\Projects\\{name}");
        var document = new SceneDocumentSnapshot(
            Guid.NewGuid(),
            $"C:\\Projects\\{name}\\Assets\\Default.asharia.scene.json",
            revision,
            revision,
            []);
        return ProjectSessionSnapshot.Ready(
            project,
            document,
            new ContentStateId(1),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);
    }

    private sealed class ControlledAssetCatalogGateway : IAssetCatalogGateway
    {
        private readonly object gate_ = new();
        private readonly List<Request> requests_ = [];
        private TaskCompletionSource requestAdded_ = NewSignal();

        public int RequestCount
        {
            get
            {
                lock (gate_)
                {
                    return requests_.Count;
                }
            }
        }

        public ValueTask<AssetCatalogQueryResult> QueryAsync(
            AssetCatalogQueryScope scope,
            CancellationToken cancellationToken = default)
        {
            var request = new Request(scope, cancellationToken);
            lock (gate_)
            {
                requests_.Add(request);
                requestAdded_.TrySetResult();
                requestAdded_ = NewSignal();
            }
            return new ValueTask<AssetCatalogQueryResult>(request.Result.Task);
        }

        public async Task<Request> WaitForRequestAsync(int index)
        {
            while (true)
            {
                Task wait;
                lock (gate_)
                {
                    if (index < requests_.Count)
                    {
                        return requests_[index];
                    }
                    wait = requestAdded_.Task;
                }
                await wait.WaitAsync(TimeSpan.FromSeconds(5));
            }
        }

        private static TaskCompletionSource NewSignal() =>
            new(TaskCreationOptions.RunContinuationsAsynchronously);
    }

    private sealed class Request(
        AssetCatalogQueryScope scope,
        CancellationToken token)
    {
        public AssetCatalogQueryScope Scope { get; } = scope;
        public CancellationToken Token { get; } = token;
        public TaskCompletionSource<AssetCatalogQueryResult> Result { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);
        public Task Completion => Result.Task;

        public void Complete(AssetCatalogQueryResult result) => Result.SetResult(result);

        public void Cancel() => Result.SetCanceled(Token);
    }

    private sealed class TestProjectSession : IProjectSession
    {
        public event EventHandler<ProjectSessionSnapshotChangedEventArgs>? SnapshotChanged;
        public ProjectSessionSnapshot Current { get; private set; } = ProjectSessionSnapshot.NoProject;

        public void Publish(ProjectSessionSnapshot snapshot)
        {
            Current = snapshot;
            SnapshotChanged?.Invoke(
                this,
                new ProjectSessionSnapshotChangedEventArgs(snapshot, originatingEditId: null));
        }

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;
        public ValueTask<ProjectSessionOperationResult> CreateProjectAsync(string parentDirectory, string projectName, Guid projectId, ProjectDocumentTransitionExpectation expectation, CancellationToken cancellationToken = default) => throw new NotSupportedException();
        public ValueTask<ProjectSessionOperationResult> CreateProjectAsync(string parentDirectory, string projectName, ProjectDocumentTransitionExpectation expectation, CancellationToken cancellationToken = default) => throw new NotSupportedException();
        public ValueTask<ProjectSessionOperationResult> OpenProjectAsync(string projectPath, ProjectDocumentTransitionExpectation expectation, CancellationToken cancellationToken = default) => throw new NotSupportedException();
        public ValueTask<ProjectSessionOperationResult> CloseProjectAsync(ProjectDocumentTransitionExpectation expectation, CancellationToken cancellationToken = default) => throw new NotSupportedException();
        public ValueTask<ProjectSessionOperationResult> PrepareExitAsync(ProjectDocumentTransitionExpectation expectation, CancellationToken cancellationToken = default) => throw new NotSupportedException();
        public ValueTask<ProjectSessionOperationResult> CreateEntityAsync(string name, CancellationToken cancellationToken = default) => throw new NotSupportedException();
        public ValueTask<ProjectSessionOperationResult> CreateMeshEntityAsync(string name, SceneMeshReference mesh, CancellationToken cancellationToken = default) => throw new NotSupportedException();
        public ValueTask<ProjectSessionOperationResult> SetEntityNameAsync(Guid objectId, string name, CancellationToken cancellationToken = default) => throw new NotSupportedException();
        public ValueTask<ProjectSessionOperationResult> SetEntityTransformAsync(Guid objectId, TransformValue transform, ProjectSessionEditContext context, CancellationToken cancellationToken = default) => throw new NotSupportedException();
        public ValueTask<ProjectSessionOperationResult> UndoAsync(CancellationToken cancellationToken = default) => throw new NotSupportedException();
        public ValueTask<ProjectSessionOperationResult> RedoAsync(CancellationToken cancellationToken = default) => throw new NotSupportedException();
        public ValueTask<ProjectSessionOperationResult> SaveSceneAsync(CancellationToken cancellationToken = default) => throw new NotSupportedException();
    }
}
