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
using Asharia.Studio.Application.Selection;
using Xunit;

namespace Asharia.Studio.Application.Tests.Selection;

public sealed class EditorSelectionServiceTests
{
    [Fact]
    public void Initial_snapshot_is_empty_and_replace_validates_current_scene()
    {
        var objectId = Guid.NewGuid();
        var sceneId = Guid.NewGuid();
        var sessionId = ProjectSessionId.CreateNew();
        var projectId = Guid.NewGuid();
        var project = new TestProjectSession(
            ReadyProject(sessionId, projectId, sceneId, [Entity(objectId)]));
        var catalog = new TestProjectAssetCatalog();
        using var selection = new EditorSelectionService(project, catalog);
        var changes = new List<EditorSelectionSnapshot>();
        selection.Changed += (_, eventArgs) => changes.Add(eventArgs.Snapshot);

        Assert.Equal(0UL, selection.Current.Revision);
        Assert.Null(selection.Current.Primary);
        Assert.Equal(EditorSelectionChangeReason.Initialization, selection.Current.Reason);
        Assert.False(selection.Replace(new SceneObjectSelectionTarget(
            sessionId,
            sceneId,
            Guid.NewGuid())));
        Assert.True(selection.Replace(new SceneObjectSelectionTarget(
            sessionId,
            sceneId,
            objectId)));
        Assert.False(selection.Replace(new SceneObjectSelectionTarget(
            sessionId,
            sceneId,
            objectId)));

        var change = Assert.Single(changes);
        Assert.Equal(1UL, change.Revision);
        Assert.Equal(EditorSelectionChangeReason.User, change.Reason);
        Assert.Equal(objectId, Assert.IsType<SceneObjectSelectionTarget>(
            change.Primary).ObjectId);
    }

    [Fact]
    public void Scene_selection_remaps_by_id_and_clears_on_removal_or_scope_change()
    {
        var objectId = Guid.NewGuid();
        var sceneId = Guid.NewGuid();
        var sessionId = ProjectSessionId.CreateNew();
        var projectId = Guid.NewGuid();
        var project = new TestProjectSession(
            ReadyProject(sessionId, projectId, sceneId, [Entity(objectId)]));
        var catalog = new TestProjectAssetCatalog();
        using var selection = new EditorSelectionService(project, catalog);
        Assert.True(selection.Replace(new SceneObjectSelectionTarget(
            sessionId,
            sceneId,
            objectId)));

        project.Publish(ReadyProject(
            sessionId,
            projectId,
            sceneId,
            [Entity(objectId, "Updated")],
            revision: 2));
        Assert.NotNull(selection.Current.Primary);
        Assert.Equal(1UL, selection.Current.Revision);

        project.Publish(ReadyProject(
            sessionId,
            projectId,
            sceneId,
            [],
            revision: 3));
        Assert.Null(selection.Current.Primary);
        Assert.Equal(EditorSelectionChangeReason.SceneTargetRemoved, selection.Current.Reason);

        project.Publish(ReadyProject(
            sessionId,
            projectId,
            sceneId,
            [Entity(objectId)],
            revision: 4));
        Assert.True(selection.Replace(new SceneObjectSelectionTarget(
            sessionId,
            sceneId,
            objectId)));
        project.Publish(ReadyProject(
            ProjectSessionId.CreateNew(),
            Guid.NewGuid(),
            Guid.NewGuid(),
            []));

        Assert.Null(selection.Current.Primary);
        Assert.Equal(EditorSelectionChangeReason.ProjectScopeChanged, selection.Current.Reason);
    }

    [Fact]
    public void Asset_selection_requires_current_catalog_and_ready_missing_is_authoritative()
    {
        var sessionId = ProjectSessionId.CreateNew();
        var projectId = Guid.NewGuid();
        var sceneId = Guid.NewGuid();
        var assetGuid = Guid.NewGuid();
        var project = new TestProjectSession(
            ReadyProject(sessionId, projectId, sceneId, []));
        var scope = AssetScope(sessionId, projectId, "editor-preview");
        var catalog = new TestProjectAssetCatalog();
        using var selection = new EditorSelectionService(project, catalog);
        var target = new AssetSelectionTarget(
            sessionId,
            projectId,
            "editor-preview",
            new AssetSelectionKey(assetGuid, "Assets/Before.glb"));

        Assert.False(selection.Replace(target));
        catalog.Publish(AssetCatalogSessionSnapshot.Ready(
            scope,
            AssetSnapshot(projectId, 1, [Asset(assetGuid, "Assets/Before.glb")]),
            requestGeneration: 1));
        Assert.True(selection.Replace(target));

        catalog.Publish(AssetCatalogSessionSnapshot.Loading(
            scope,
            AssetSnapshot(projectId, 1, [Asset(assetGuid, "Assets/Before.glb")]),
            requestGeneration: 2));
        Assert.Same(target, selection.Current.Primary);
        catalog.Publish(AssetCatalogSessionSnapshot.Degraded(
            scope,
            AssetSnapshot(projectId, 1, [Asset(assetGuid, "Assets/Before.glb")]),
            failure: null,
            requestGeneration: 3));
        Assert.Same(target, selection.Current.Primary);

        catalog.Publish(AssetCatalogSessionSnapshot.Ready(
            scope,
            AssetSnapshot(projectId, 2, [Asset(assetGuid, "Assets/After.glb")]),
            requestGeneration: 4));
        Assert.Same(target, selection.Current.Primary);

        catalog.Publish(AssetCatalogSessionSnapshot.Ready(
            scope,
            AssetSnapshot(projectId, 3, []),
            requestGeneration: 5));
        Assert.Null(selection.Current.Primary);
        Assert.Equal(EditorSelectionChangeReason.AssetTargetRemoved, selection.Current.Reason);
    }

    [Fact]
    public void Asset_selection_clears_when_same_scope_catalog_has_no_last_known_good_target()
    {
        var sessionId = ProjectSessionId.CreateNew();
        var projectId = Guid.NewGuid();
        var assetGuid = Guid.NewGuid();
        var project = new TestProjectSession(
            ReadyProject(sessionId, projectId, Guid.NewGuid(), []));
        var scope = AssetScope(sessionId, projectId, "editor-preview");
        var catalog = new TestProjectAssetCatalog();
        catalog.Publish(AssetCatalogSessionSnapshot.Ready(
            scope,
            AssetSnapshot(projectId, 1, [Asset(assetGuid, "Assets/Model.glb")]),
            requestGeneration: 1));
        using var selection = new EditorSelectionService(project, catalog);
        var target = new AssetSelectionTarget(
            sessionId,
            projectId,
            "editor-preview",
            new AssetSelectionKey(assetGuid, "Assets/Model.glb"));

        Assert.True(selection.Replace(target));
        catalog.Publish(AssetCatalogSessionSnapshot.Loading(
            scope,
            lastGood: null,
            requestGeneration: 2));

        Assert.Null(selection.Current.Primary);
        Assert.Equal(EditorSelectionChangeReason.AssetTargetRemoved, selection.Current.Reason);

        catalog.Publish(AssetCatalogSessionSnapshot.Ready(
            scope,
            AssetSnapshot(projectId, 2, [Asset(assetGuid, "Assets/Model.glb")]),
            requestGeneration: 3));
        Assert.True(selection.Replace(target));
        catalog.Publish(AssetCatalogSessionSnapshot.Failed(
            scope,
            new AssetCatalogQueryFailure(
                AssetCatalogQueryFailureKind.IoFailure,
                "catalog unavailable"),
            requestGeneration: 4));

        Assert.Null(selection.Current.Primary);
        Assert.Equal(EditorSelectionChangeReason.AssetTargetRemoved, selection.Current.Reason);
    }

    [Fact]
    public void Asset_selection_ignores_scene_revisions_but_clears_on_catalog_scope_change()
    {
        var sessionId = ProjectSessionId.CreateNew();
        var projectId = Guid.NewGuid();
        var assetGuid = Guid.NewGuid();
        var project = new TestProjectSession(
            ReadyProject(sessionId, projectId, Guid.NewGuid(), []));
        var catalog = new TestProjectAssetCatalog();
        var initialScope = AssetScope(sessionId, projectId, "editor-preview");
        catalog.Publish(AssetCatalogSessionSnapshot.Ready(
            initialScope,
            AssetSnapshot(projectId, 1, [Asset(assetGuid, "Assets/Model.glb")]),
            requestGeneration: 1));
        using var selection = new EditorSelectionService(project, catalog);
        Assert.True(selection.Replace(new AssetSelectionTarget(
            sessionId,
            projectId,
            "editor-preview",
            new AssetSelectionKey(assetGuid, "Assets/Model.glb"))));

        project.Publish(ReadyProject(
            sessionId,
            projectId,
            Guid.NewGuid(),
            [],
            revision: 2));
        Assert.NotNull(selection.Current.Primary);

        catalog.Publish(AssetCatalogSessionSnapshot.Loading(
            AssetScope(sessionId, projectId, "shipping"),
            lastGood: null,
            requestGeneration: 2));
        Assert.Null(selection.Current.Primary);
        Assert.Equal(EditorSelectionChangeReason.ProjectScopeChanged, selection.Current.Reason);
    }

    [Fact]
    public void Asset_selection_clears_when_its_project_session_closes_or_switches()
    {
        var sessionId = ProjectSessionId.CreateNew();
        var projectId = Guid.NewGuid();
        var assetGuid = Guid.NewGuid();
        var project = new TestProjectSession(
            ReadyProject(sessionId, projectId, Guid.NewGuid(), []));
        var catalog = new TestProjectAssetCatalog();
        var scope = AssetScope(sessionId, projectId, "editor-preview");
        catalog.Publish(AssetCatalogSessionSnapshot.Ready(
            scope,
            AssetSnapshot(projectId, 1, [Asset(assetGuid, "Assets/Model.glb")]),
            requestGeneration: 1));
        using var selection = new EditorSelectionService(project, catalog);
        var target = new AssetSelectionTarget(
            sessionId,
            projectId,
            "editor-preview",
            new AssetSelectionKey(assetGuid, "Assets/Model.glb"));

        Assert.True(selection.Replace(target));
        project.Publish(ProjectSessionSnapshot.NoProject);

        Assert.Null(selection.Current.Primary);
        Assert.Equal(EditorSelectionChangeReason.ProjectScopeChanged, selection.Current.Reason);

        project.Publish(ReadyProject(
            sessionId,
            projectId,
            Guid.NewGuid(),
            []));
        Assert.True(selection.Replace(target));
        project.Publish(ReadyProject(
            ProjectSessionId.CreateNew(),
            Guid.NewGuid(),
            Guid.NewGuid(),
            []));

        Assert.Null(selection.Current.Primary);
        Assert.Equal(EditorSelectionChangeReason.ProjectScopeChanged, selection.Current.Reason);
    }

    [Fact]
    public void Stale_catalog_event_cannot_override_the_newer_current_snapshot()
    {
        var sessionId = ProjectSessionId.CreateNew();
        var projectId = Guid.NewGuid();
        var assetGuid = Guid.NewGuid();
        var project = new TestProjectSession(
            ReadyProject(sessionId, projectId, Guid.NewGuid(), []));
        var catalog = new TestProjectAssetCatalog();
        var scope = AssetScope(sessionId, projectId, "editor-preview");
        var current = AssetCatalogSessionSnapshot.Ready(
            scope,
            AssetSnapshot(projectId, 2, [Asset(assetGuid, "Assets/Model.glb")]),
            requestGeneration: 2);
        catalog.Publish(current);
        using var selection = new EditorSelectionService(project, catalog);
        var target = new AssetSelectionTarget(
            sessionId,
            projectId,
            "editor-preview",
            new AssetSelectionKey(assetGuid, "Assets/Model.glb"));
        Assert.True(selection.Replace(target));

        catalog.PublishEventOnly(AssetCatalogSessionSnapshot.Ready(
            scope,
            AssetSnapshot(projectId, 1, []),
            requestGeneration: 1));

        Assert.Same(target, selection.Current.Primary);
        Assert.Equal(1UL, selection.Current.Revision);
    }

    [Fact]
    public async Task Catalog_invalidation_serializes_current_read_with_a_newer_replacement()
    {
        var sessionId = ProjectSessionId.CreateNew();
        var projectId = Guid.NewGuid();
        var retainedAssetGuid = Guid.NewGuid();
        var replacementAssetGuid = Guid.NewGuid();
        var project = new TestProjectSession(
            ReadyProject(sessionId, projectId, Guid.NewGuid(), []));
        var catalog = new TestProjectAssetCatalog();
        var scope = AssetScope(sessionId, projectId, "editor-preview");
        var originalSnapshot = AssetCatalogSessionSnapshot.Ready(
            scope,
            AssetSnapshot(
                projectId,
                1,
                [Asset(retainedAssetGuid, "Assets/Original.glb")]),
            requestGeneration: 1);
        catalog.Publish(originalSnapshot);
        using var selection = new EditorSelectionService(project, catalog);
        var originalTarget = new AssetSelectionTarget(
            sessionId,
            projectId,
            "editor-preview",
            new AssetSelectionKey(retainedAssetGuid, "Assets/Original.glb"));
        var replacementTarget = new AssetSelectionTarget(
            sessionId,
            projectId,
            "editor-preview",
            new AssetSelectionKey(replacementAssetGuid, "Assets/Replacement.glb"));
        Assert.True(selection.Replace(originalTarget));

        using var currentReadEntered = new ManualResetEventSlim();
        using var releaseCurrentRead = new ManualResetEventSlim();
        catalog.BlockNextCurrentRead(currentReadEntered, releaseCurrentRead);
        var invalidation = Task.Run(() => catalog.PublishEventOnly(originalSnapshot));
        Task<bool>? replacement = null;
        try
        {
            Assert.True(currentReadEntered.Wait(TimeSpan.FromSeconds(5)));
            catalog.SetCurrentWithoutEvent(AssetCatalogSessionSnapshot.Ready(
                scope,
                AssetSnapshot(
                    projectId,
                    2,
                    [Asset(replacementAssetGuid, "Assets/Replacement.glb")]),
                requestGeneration: 2));

            replacement = Task.Run(() => selection.Replace(replacementTarget));
            await Task.Delay(25);
            Assert.False(replacement.IsCompleted);
        }
        finally
        {
            releaseCurrentRead.Set();
        }

        await invalidation.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.NotNull(replacement);
        Assert.True(await replacement.WaitAsync(TimeSpan.FromSeconds(5)));
        Assert.Same(replacementTarget, selection.Current.Primary);
        Assert.Equal(2UL, selection.Current.Revision);
    }

    [Fact]
    public void Throwing_and_reentrant_subscribers_are_isolated_and_revision_order_is_stable()
    {
        var firstId = Guid.NewGuid();
        var secondId = Guid.NewGuid();
        var sceneId = Guid.NewGuid();
        var sessionId = ProjectSessionId.CreateNew();
        var projectId = Guid.NewGuid();
        var project = new TestProjectSession(ReadyProject(
            sessionId,
            projectId,
            sceneId,
            [Entity(firstId), Entity(secondId)]));
        var catalog = new TestProjectAssetCatalog();
        using var selection = new EditorSelectionService(project, catalog);
        var observed = new List<ulong>();
        selection.Changed += (_, _) => throw new InvalidOperationException("observer failed");
        selection.Changed += (_, eventArgs) =>
        {
            observed.Add(eventArgs.Snapshot.Revision);
            if (eventArgs.Snapshot.Revision == 1)
            {
                Assert.True(selection.Replace(new SceneObjectSelectionTarget(
                    sessionId,
                    sceneId,
                    secondId)));
            }
        };

        Assert.True(selection.Replace(new SceneObjectSelectionTarget(
            sessionId,
            sceneId,
            firstId)));

        Assert.Equal([1UL, 2UL], observed);
        Assert.Equal(2UL, selection.Current.Revision);
        Assert.Equal(secondId, Assert.IsType<SceneObjectSelectionTarget>(
            selection.Current.Primary).ObjectId);
    }

    [Fact]
    public async Task Concurrent_replacements_publish_each_committed_revision_in_order()
    {
        var objectIds = Enumerable.Range(0, 64).Select(_ => Guid.NewGuid()).ToArray();
        var sceneId = Guid.NewGuid();
        var sessionId = ProjectSessionId.CreateNew();
        var projectId = Guid.NewGuid();
        var project = new TestProjectSession(ReadyProject(
            sessionId,
            projectId,
            sceneId,
            objectIds.Select(objectId => Entity(objectId))));
        var catalog = new TestProjectAssetCatalog();
        using var selection = new EditorSelectionService(project, catalog);
        var observed = new List<ulong>();
        var observedGate = new object();
        selection.Changed += (_, eventArgs) =>
        {
            lock (observedGate)
            {
                observed.Add(eventArgs.Snapshot.Revision);
            }
        };

        var replacements = objectIds.Select(objectId => Task.Run(() =>
            selection.Replace(new SceneObjectSelectionTarget(
                sessionId,
                sceneId,
                objectId))));
        Assert.All(await Task.WhenAll(replacements), Assert.True);

        Assert.Equal((ulong)objectIds.Length, selection.Current.Revision);
        lock (observedGate)
        {
            Assert.Equal(
                Enumerable.Range(1, objectIds.Length).Select(value => (ulong)value),
                observed);
        }
    }

    [Fact]
    public async Task Dispose_waits_for_active_publication_and_prevents_tail_notifications()
    {
        var objectId = Guid.NewGuid();
        var sceneId = Guid.NewGuid();
        var sessionId = ProjectSessionId.CreateNew();
        var projectId = Guid.NewGuid();
        var project = new TestProjectSession(
            ReadyProject(sessionId, projectId, sceneId, [Entity(objectId)]));
        var catalog = new TestProjectAssetCatalog();
        var selection = new EditorSelectionService(project, catalog);
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        var notificationCount = 0;
        selection.Changed += (_, _) =>
        {
            Interlocked.Increment(ref notificationCount);
            entered.Set();
            release.Wait(TimeSpan.FromSeconds(5));
        };
        var replace = Task.Run(() => selection.Replace(new SceneObjectSelectionTarget(
            sessionId,
            sceneId,
            objectId)));
        Assert.True(entered.Wait(TimeSpan.FromSeconds(5)));

        var dispose = Task.Run(selection.Dispose);
        await Task.Delay(25);
        Assert.False(dispose.IsCompleted);
        release.Set();
        await dispose.WaitAsync(TimeSpan.FromSeconds(5));
        Assert.True(await replace.WaitAsync(TimeSpan.FromSeconds(5)));

        project.Publish(ProjectSessionSnapshot.NoProject);
        catalog.Publish(AssetCatalogSessionSnapshot.NoProject());
        Assert.Equal(1, Volatile.Read(ref notificationCount));
        Assert.Throws<ObjectDisposedException>(() => selection.Clear());
        Assert.Throws<ObjectDisposedException>(() => selection.Replace(
            new SceneObjectSelectionTarget(sessionId, sceneId, objectId)));
    }

    private static SceneEntitySnapshot Entity(Guid objectId, string name = "Entity") =>
        new(objectId, new EntityId(1, 1), name, TransformValue.Identity);

    private static ProjectSessionSnapshot ReadyProject(
        ProjectSessionId sessionId,
        Guid projectId,
        Guid sceneId,
        IEnumerable<SceneEntitySnapshot> entities,
        ulong revision = 1) =>
        ProjectSessionSnapshot.Ready(
            new ActiveProjectSnapshot(
                sessionId,
                projectId,
                "Sample",
                "C:\\Projects\\Sample"),
            new SceneDocumentSnapshot(
                sceneId,
                "C:\\Projects\\Sample\\Assets\\Default.asharia.scene.json",
                revision,
                savedRevision: 1,
                entities),
            new ContentStateId(revision),
            new ContentStateId(1),
            canUndo: false,
            canRedo: false,
            undoLabel: null,
            redoLabel: null);

    private static AssetCatalogQueryScope AssetScope(
        ProjectSessionId sessionId,
        Guid projectId,
        string targetProfile) =>
        new(
            sessionId,
            projectId,
            "C:\\Projects\\Sample",
            "C:\\Projects\\Sample\\asharia.project.json",
            targetProfile);

    private static AssetCatalogSnapshot AssetSnapshot(
        Guid projectId,
        ulong revision,
        ImmutableArray<AssetCatalogEntry> entries) =>
        new(
            AssetCatalogSnapshotState.Ready,
            revision,
            DateTimeOffset.UtcNow,
            projectId,
            "C:\\Projects\\Sample\\asharia.project.json",
            "C:\\Projects\\Sample\\.asharia\\cache\\assets\\manifest.json",
            "editor-preview",
            ImmutableArray<AssetCatalogSourceRoot>.Empty,
            ImmutableArray<AssetCatalogNavigationEntry>.Empty,
            entries,
            ImmutableArray<AssetCatalogDiagnostic>.Empty);

    private static AssetCatalogEntry Asset(Guid guid, string sourcePath) =>
        new(
            new AssetSelectionKey(guid, sourcePath),
            guid,
            guid.ToString("D"),
            sourcePath,
            "Assets",
            "Assets",
            "C:\\Projects\\Sample\\Assets",
            $"C:\\Projects\\Sample\\{sourcePath.Replace('/', '\\')}",
            $"C:\\Projects\\Sample\\{sourcePath.Replace('/', '\\')}.ameta",
            System.IO.Path.GetFileNameWithoutExtension(sourcePath),
            System.IO.Path.GetExtension(sourcePath),
            "Model",
            "glTF",
            1,
            "default",
            "Model",
            AssetCatalogProductState.Current,
            1,
            0,
            ImmutableArray<AssetCatalogSubAsset>.Empty,
            ImmutableArray<AssetCatalogDiagnostic>.Empty);

    private sealed class TestProjectAssetCatalog : IProjectAssetCatalog
    {
        private readonly object gate_ = new();
        private AssetCatalogSessionSnapshot current_ =
            AssetCatalogSessionSnapshot.NoProject();
        private CurrentReadBarrier? nextCurrentReadBarrier_;

        public event EventHandler<AssetCatalogSessionSnapshotChangedEventArgs>?
            SnapshotChanged;

        public AssetCatalogSessionSnapshot Current
        {
            get
            {
                AssetCatalogSessionSnapshot snapshot;
                CurrentReadBarrier? barrier;
                lock (gate_)
                {
                    snapshot = current_;
                    barrier = nextCurrentReadBarrier_;
                    nextCurrentReadBarrier_ = null;
                }

                barrier?.Wait();
                return snapshot;
            }
        }

        public ValueTask RefreshAsync(CancellationToken cancellationToken = default) =>
            ValueTask.CompletedTask;

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;

        public void Publish(AssetCatalogSessionSnapshot snapshot)
        {
            SetCurrentWithoutEvent(snapshot);
            PublishEventOnly(snapshot);
        }

        public void SetCurrentWithoutEvent(AssetCatalogSessionSnapshot snapshot)
        {
            ArgumentNullException.ThrowIfNull(snapshot);
            lock (gate_)
            {
                current_ = snapshot;
            }
        }

        public void BlockNextCurrentRead(
            ManualResetEventSlim entered,
            ManualResetEventSlim release)
        {
            ArgumentNullException.ThrowIfNull(entered);
            ArgumentNullException.ThrowIfNull(release);
            lock (gate_)
            {
                Assert.Null(nextCurrentReadBarrier_);
                nextCurrentReadBarrier_ = new CurrentReadBarrier(entered, release);
            }
        }

        public void PublishEventOnly(AssetCatalogSessionSnapshot snapshot)
        {
            SnapshotChanged?.Invoke(
                this,
                new AssetCatalogSessionSnapshotChangedEventArgs(snapshot));
        }

        private sealed class CurrentReadBarrier(
            ManualResetEventSlim entered,
            ManualResetEventSlim release)
        {
            public void Wait()
            {
                entered.Set();
                Assert.True(release.Wait(TimeSpan.FromSeconds(5)));
            }
        }
    }

    private sealed class TestProjectSession(ProjectSessionSnapshot initial) : IProjectSession
    {
        public event EventHandler<ProjectSessionSnapshotChangedEventArgs>? SnapshotChanged;

        public ProjectSessionSnapshot Current { get; private set; } = initial;

        public void Publish(ProjectSessionSnapshot snapshot)
        {
            Current = snapshot;
            SnapshotChanged?.Invoke(
                this,
                new ProjectSessionSnapshotChangedEventArgs(snapshot, originatingEditId: null));
        }

        public ValueTask DisposeAsync() => ValueTask.CompletedTask;

        public ValueTask<ProjectSessionOperationResult> CreateProjectAsync(
            string parentDirectory,
            string projectName,
            ProjectDocumentTransitionExpectation expectation,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> OpenProjectAsync(
            string projectPath,
            ProjectDocumentTransitionExpectation expectation,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> CloseProjectAsync(
            ProjectDocumentTransitionExpectation expectation,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> PrepareExitAsync(
            ProjectDocumentTransitionExpectation expectation,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> CreateEntityAsync(
            string name,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> CreateMeshEntityAsync(
            string name,
            SceneMeshReference mesh,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> SetEntityNameAsync(
            Guid objectId,
            string name,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> SetEntityMeshAsync(
            Guid objectId, SceneMeshReference? mesh, ProjectSessionEditContext context,
            CancellationToken cancellationToken = default) => throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> SetEntityTransformAsync(
            Guid objectId,
            TransformValue transform,
            ProjectSessionEditContext context,
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> UndoAsync(
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> RedoAsync(
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();

        public ValueTask<ProjectSessionOperationResult> SaveSceneAsync(
            CancellationToken cancellationToken = default) =>
            throw new NotSupportedException();
    }
}
