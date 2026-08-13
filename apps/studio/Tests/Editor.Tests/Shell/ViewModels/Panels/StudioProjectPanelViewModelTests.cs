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
using Asharia.Studio.TestSupport;
using Editor.Shell.ViewModels.Panels;
using Xunit;

namespace Editor.Tests.Shell.ViewModels.Panels;

public sealed class StudioProjectPanelViewModelTests
{
    [Fact]
    public async Task Ready_catalog_projects_navigation_and_debounced_filters()
    {
        using var shell = StudioShellTestFactory.Create(
            out var projectSession,
            out _);
        var gateway = new ControlledAssetCatalogGateway();
        await using var catalog = new ProjectAssetCatalog(projectSession, gateway);
        var scheduler = new TestResourceBrowserScheduler();
        using var viewModel = new StudioProjectPanelViewModel(
            shell,
            catalog,
            scheduler);

        projectSession.Publish(ReadyProject("Sample"));
        scheduler.DrainPosts();
        Assert.True(viewModel.IsInitialLoading);
        var mesh = Entry(
            "Assets/Models/Wedge.glb",
            "Wedge",
            "Model",
            AssetCatalogProductState.Current,
            Guid.NewGuid(),
            [new AssetCatalogSubAsset("mesh:0", "Body", "Mesh")]);
        var texture = Entry(
            "Assets/Textures/Albedo.png",
            "Albedo",
            "Texture2D",
            AssetCatalogProductState.Stale,
            Guid.NewGuid());
        var rootFile = Entry(
            "Assets/Readme.txt",
            "Readme",
            "Text",
            AssetCatalogProductState.NotTracked,
            assetGuid: null);
        await WaitUntil(() => gateway.Requests.Count > 0, scheduler);
        gateway.Requests[0].Complete(AssetCatalogQueryResult.Success(
            Snapshot(1, [mesh, texture, rootFile])));
        await WaitUntil(
            () => viewModel.IsReady && viewModel.VisibleAssets.Count == 3,
            scheduler);

        Assert.Equal(4, viewModel.NavigationRows.Count);
        Assert.Equal("All Assets", viewModel.SelectedNavigation?.DisplayName);
        Assert.Equal(3, viewModel.VisibleAssets.Count);
        Assert.Equal(
            [StudioProjectPanelViewModel.AllTypes, "Model", "Text", "Texture2D"],
            viewModel.TypeOptions);

        viewModel.SelectedNavigation = viewModel.NavigationRows.Single(
            row => row.ScopePath == "Assets/Models");
        Assert.Single(viewModel.VisibleAssets);
        Assert.Equal("Wedge", viewModel.VisibleAssets[0].DisplayName);

        viewModel.SelectedNavigation = viewModel.NavigationRows[0];
        viewModel.SearchText = "png";
        Assert.Equal(3, viewModel.VisibleAssets.Count);
        scheduler.RunScheduled();
        Assert.Single(viewModel.VisibleAssets);
        Assert.Equal("Albedo", viewModel.VisibleAssets[0].DisplayName);
        Assert.Equal("1/3", viewModel.AssetCountText);

        viewModel.SearchText = string.Empty;
        scheduler.RunScheduled();
        viewModel.SelectedType = "Model";
        Assert.Single(viewModel.VisibleAssets);
        Assert.Equal("Wedge", viewModel.VisibleAssets[0].DisplayName);
        viewModel.SelectedType = StudioProjectPanelViewModel.AllTypes;
        viewModel.SelectedProductFilter = viewModel.ProductFilterOptions.Single(
            option => option.State == AssetCatalogProductState.Stale);
        Assert.Single(viewModel.VisibleAssets);
        Assert.Equal("Albedo", viewModel.VisibleAssets[0].DisplayName);
        Assert.Equal("Body [mesh:0] · Mesh", new StudioResourceAssetRowViewModel(mesh).SubAssetSummaryText);
    }

    [Fact]
    public async Task Guid_selection_survives_refresh_and_temporary_filtering()
    {
        using var shell = StudioShellTestFactory.Create(
            out var projectSession,
            out _);
        var gateway = new ControlledAssetCatalogGateway();
        await using var catalog = new ProjectAssetCatalog(projectSession, gateway);
        var scheduler = new TestResourceBrowserScheduler();
        using var viewModel = new StudioProjectPanelViewModel(
            shell,
            catalog,
            scheduler);
        var guid = Guid.NewGuid();

        projectSession.Publish(ReadyProject("Sample"));
        await WaitUntil(() => gateway.Requests.Count > 0, scheduler);
        gateway.Requests[0].Complete(AssetCatalogQueryResult.Success(Snapshot(
            1,
            [Entry(
                "Assets/Models/Before.glb",
                "Before",
                "Model",
                AssetCatalogProductState.Current,
                guid)])));
        await WaitUntil(
            () => viewModel.IsReady && viewModel.VisibleAssets.Count == 1,
            scheduler);
        viewModel.SelectedAsset = Assert.Single(viewModel.VisibleAssets);

        var refresh = catalog.RefreshAsync().AsTask();
        await WaitUntil(() => gateway.Requests.Count == 2, scheduler);
        gateway.Requests[1].Complete(AssetCatalogQueryResult.Success(Snapshot(
            2,
            [Entry(
                "Assets/Models/After.glb",
                "After",
                "Model",
                AssetCatalogProductState.Current,
                guid)])));
        await refresh;
        await WaitUntil(
            () => viewModel.SelectedAsset?.DisplayName == "After",
            scheduler);

        Assert.Equal("Assets/Models/After.glb", viewModel.SelectedAsset!.SourcePath);
        viewModel.IsDetailsExpanded = true;
        Assert.True(viewModel.IsDetailsVisible);
        var selectedBeforeFiltering = viewModel.SelectedAsset;
        viewModel.SearchText = "missing";
        scheduler.RunScheduled();
        Assert.Null(viewModel.SelectedAsset);
        Assert.Empty(viewModel.VisibleAssets);
        Assert.True(viewModel.IsDetailsExpanded);
        Assert.False(viewModel.IsDetailsVisible);

        viewModel.SearchText = string.Empty;
        scheduler.RunScheduled();
        Assert.Equal(guid, viewModel.SelectedAsset?.SelectionKey.AssetGuid);
        Assert.Equal("After", viewModel.SelectedAsset?.DisplayName);
        Assert.Same(selectedBeforeFiltering, viewModel.SelectedAsset);
        Assert.True(viewModel.IsDetailsVisible);
    }

    [Fact]
    public async Task State_projection_keeps_last_good_visible_when_refresh_degrades()
    {
        using var shell = StudioShellTestFactory.Create(
            out var projectSession,
            out _);
        var gateway = new ControlledAssetCatalogGateway();
        await using var catalog = new ProjectAssetCatalog(projectSession, gateway);
        var scheduler = new TestResourceBrowserScheduler();
        using var viewModel = new StudioProjectPanelViewModel(
            shell,
            catalog,
            scheduler);

        Assert.True(viewModel.IsNoProject);
        Assert.True(viewModel.IsBlockingStateVisible);
        projectSession.Publish(ReadyProject("Unavailable"));
        scheduler.DrainPosts();
        Assert.True(viewModel.IsInitialLoading);
        await WaitUntil(() => gateway.Requests.Count > 0, scheduler);
        gateway.Requests[0].Complete(AssetCatalogQueryResult.Failed(
            Failure("catalog unavailable")));
        await WaitUntil(() => viewModel.IsFailed, scheduler);
        Assert.Equal("catalog unavailable", viewModel.BlockingMessage);

        projectSession.Publish(ProjectSessionSnapshot.NoProject);
        await WaitUntil(() => viewModel.IsNoProject, scheduler);
        projectSession.Publish(ReadyProject("Empty"));
        scheduler.DrainPosts();
        await WaitUntil(() => gateway.Requests.Count > 1, scheduler);
        gateway.Requests[1].Complete(AssetCatalogQueryResult.Success(Snapshot(1, [])));
        await WaitUntil(() => viewModel.IsReady, scheduler);
        Assert.True(viewModel.IsContentVisible);
        Assert.True(viewModel.IsEmptyStateVisible);
        Assert.Equal("No resources found", viewModel.EmptyStateText);

        var refresh = catalog.RefreshAsync().AsTask();
        await WaitUntil(() => gateway.Requests.Count == 3, scheduler);
        scheduler.DrainPosts();
        Assert.True(viewModel.IsLoading);
        Assert.True(viewModel.IsContentVisible);
        gateway.Requests[2].Complete(AssetCatalogQueryResult.Failed(
            Failure("refresh failed")));
        await refresh;
        await WaitUntil(() => viewModel.IsDegraded, scheduler);

        Assert.True(viewModel.IsContentVisible);
        Assert.False(viewModel.IsBlockingStateVisible);
        Assert.Equal("refresh failed", viewModel.DegradedMessage);
    }

    [Fact]
    public async Task Partial_catalog_explains_diagnostics_instead_of_a_refresh_failure()
    {
        using var shell = StudioShellTestFactory.Create(
            out var projectSession,
            out _);
        var gateway = new ControlledAssetCatalogGateway();
        await using var catalog = new ProjectAssetCatalog(projectSession, gateway);
        var scheduler = new TestResourceBrowserScheduler();
        using var viewModel = new StudioProjectPanelViewModel(
            shell,
            catalog,
            scheduler);

        projectSession.Publish(ReadyProject("Partial"));
        await WaitUntil(() => gateway.Requests.Count > 0, scheduler);
        gateway.Requests[0].Complete(AssetCatalogQueryResult.Success(Snapshot(
            1,
            [],
            AssetCatalogSnapshotState.Degraded,
            [new AssetCatalogDiagnostic(
                AssetCatalogDiagnosticSeverity.Warning,
                "CATALOG-PARTIAL",
                sourcePath: null,
                path: "Assets",
                "One source root could not be read.")])));
        await WaitUntil(() => viewModel.IsDegraded, scheduler);

        Assert.True(viewModel.IsContentVisible);
        Assert.Contains("partial", viewModel.DegradedMessage, StringComparison.OrdinalIgnoreCase);
        Assert.Contains("diagnostics", viewModel.DegradedMessage, StringComparison.OrdinalIgnoreCase);
        Assert.DoesNotContain("Refresh failed", viewModel.DegradedMessage, StringComparison.Ordinal);
        Assert.Equal("1 diagnostic(s)", viewModel.CatalogDiagnosticText);
    }

    [Fact]
    public async Task Dispose_detaches_keep_alive_projection_without_owning_catalog()
    {
        using var shell = StudioShellTestFactory.Create(
            out var projectSession,
            out _);
        var gateway = new ControlledAssetCatalogGateway();
        await using var catalog = new ProjectAssetCatalog(projectSession, gateway);
        var scheduler = new TestResourceBrowserScheduler();
        var viewModel = new StudioProjectPanelViewModel(shell, catalog, scheduler);

        projectSession.Publish(ReadyProject("Sample"));
        await WaitUntil(() => gateway.Requests.Count > 0, scheduler);
        gateway.Requests[0].Complete(AssetCatalogQueryResult.Success(Snapshot(
            1,
            [Entry(
                "Assets/Before.png",
                "Before",
                "Texture2D",
                AssetCatalogProductState.Current,
                Guid.NewGuid())])));
        await WaitUntil(
            () => viewModel.IsReady && viewModel.VisibleAssets.Count == 1,
            scheduler);
        viewModel.SearchText = "pending";
        viewModel.Dispose();
        Assert.True(scheduler.WasScheduledWorkDisposed);

        var refresh = catalog.RefreshAsync().AsTask();
        await WaitUntil(() => gateway.Requests.Count == 2, scheduler);
        gateway.Requests[1].Complete(AssetCatalogQueryResult.Success(Snapshot(
            2,
            [Entry(
                "Assets/After.png",
                "After",
                "Texture2D",
                AssetCatalogProductState.Current,
                Guid.NewGuid())])));
        await refresh;

        Assert.Equal("Before", Assert.Single(viewModel.VisibleAssets).DisplayName);
        Assert.Equal(2UL, catalog.Current.Catalog?.Revision);
        viewModel.Dispose();
    }

    [Fact]
    public async Task Background_publication_is_applied_only_when_the_ui_scheduler_drains()
    {
        using var shell = StudioShellTestFactory.Create(
            out var projectSession,
            out _);
        var gateway = new ControlledAssetCatalogGateway();
        await using var catalog = new ProjectAssetCatalog(projectSession, gateway);
        var scheduler = new TestResourceBrowserScheduler();
        using var viewModel = new StudioProjectPanelViewModel(
            shell,
            catalog,
            scheduler);

        projectSession.Publish(ReadyProject("Sample"));
        Assert.True(viewModel.IsNoProject);
        Assert.True(scheduler.HasPostedWork);
        scheduler.DrainPosts();
        Assert.True(viewModel.IsInitialLoading);

        await WaitUntil(() => gateway.Requests.Count > 0, scheduler);
        gateway.Requests[0].Complete(AssetCatalogQueryResult.Success(Snapshot(
            1,
            [Entry(
                "Assets/Texture.png",
                "Texture",
                "Texture2D",
                AssetCatalogProductState.Current,
                Guid.NewGuid())])));
        await WaitUntil(() => scheduler.HasPostedWork);

        Assert.True(viewModel.IsInitialLoading);
        Assert.Empty(viewModel.VisibleAssets);
        scheduler.DrainPosts();
        Assert.True(viewModel.IsReady);
        Assert.Equal("Texture", Assert.Single(viewModel.VisibleAssets).DisplayName);
    }

    private static async Task WaitUntil(
        Func<bool> predicate,
        TestResourceBrowserScheduler? scheduler = null)
    {
        using var timeout = new CancellationTokenSource(TimeSpan.FromSeconds(2));
        while (true)
        {
            scheduler?.DrainPosts();
            if (predicate())
            {
                return;
            }
            await Task.Delay(5, timeout.Token);
        }
    }

    private static AssetCatalogSnapshot Snapshot(
        ulong revision,
        ImmutableArray<AssetCatalogEntry> entries,
        AssetCatalogSnapshotState state = AssetCatalogSnapshotState.Ready,
        ImmutableArray<AssetCatalogDiagnostic> diagnostics = default)
    {
        var navigation = ImmutableArray.Create(
            Navigation(
                "root:assets",
                parentKey: null,
                AssetCatalogNavigationKind.SourceRoot,
                "Assets",
                "Assets",
                depth: 0),
            Navigation(
                "folder:models",
                "root:assets",
                AssetCatalogNavigationKind.Folder,
                "Models",
                "Assets/Models",
                depth: 1),
            Navigation(
                "folder:textures",
                "root:assets",
                AssetCatalogNavigationKind.Folder,
                "Textures",
                "Assets/Textures",
                depth: 1));
        return new AssetCatalogSnapshot(
            state,
            revision,
            DateTimeOffset.UtcNow,
            Guid.Parse("c66c5ec7-5c39-4613-84ed-77d186defd65"),
            "C:\\Projects\\Sample\\asharia.project.json",
            "C:\\Projects\\Sample\\.asharia\\cache\\assets\\manifest.json",
            "editor-preview",
            [new AssetCatalogSourceRoot(
                "Assets",
                "Assets",
                "Assets",
                "C:\\Projects\\Sample\\Assets")],
            navigation,
            entries,
            diagnostics.IsDefault
                ? ImmutableArray<AssetCatalogDiagnostic>.Empty
                : diagnostics);
    }

    private static AssetCatalogNavigationEntry Navigation(
        string key,
        string? parentKey,
        AssetCatalogNavigationKind kind,
        string name,
        string scope,
        int depth) =>
        new(
            key,
            parentKey,
            kind,
            name,
            scope,
            sourcePath: string.Empty,
            sourceRootName: "Assets",
            sourceRootPrefix: "Assets",
            sourceRootDirectory: "C:\\Projects\\Sample\\Assets",
            assetGuid: null,
            stableId: string.Empty,
            assetTypeName: string.Empty,
            importerName: string.Empty,
            extension: string.Empty,
            importProfileName: string.Empty,
            assetRoleName: string.Empty,
            subAssetCount: 0,
            AssetCatalogProductState.NotTracked,
            depth);

    private static AssetCatalogEntry Entry(
        string sourcePath,
        string displayName,
        string assetType,
        AssetCatalogProductState productState,
        Guid? assetGuid,
        ImmutableArray<AssetCatalogSubAsset> subAssets = default)
    {
        var fileName = sourcePath.Split('/').Last();
        var extension = System.IO.Path.GetExtension(fileName);
        return new AssetCatalogEntry(
            new AssetSelectionKey(assetGuid, sourcePath),
            assetGuid,
            assetGuid?.ToString("D") ?? string.Empty,
            sourcePath,
            "Assets",
            "Assets",
            "C:\\Projects\\Sample\\Assets",
            $"C:\\Projects\\Sample\\{sourcePath.Replace('/', '\\')}",
            assetGuid is null
                ? string.Empty
                : $"C:\\Projects\\Sample\\{sourcePath.Replace('/', '\\')}.ameta",
            displayName,
            extension,
            assetType,
            assetGuid is null ? string.Empty : "TestImporter",
            assetGuid is null ? 0UL : 3UL,
            assetGuid is null ? string.Empty : "default",
            assetType,
            productState,
            productState == AssetCatalogProductState.Current ? 1 : 0,
            productState == AssetCatalogProductState.Stale ? 1 : 0,
            subAssets.IsDefault ? ImmutableArray<AssetCatalogSubAsset>.Empty : subAssets,
            ImmutableArray<AssetCatalogDiagnostic>.Empty);
    }

    private static AssetCatalogQueryFailure Failure(string message) =>
        new(AssetCatalogQueryFailureKind.IoFailure, message);

    private static ProjectSessionSnapshot ReadyProject(string name)
    {
        var sessionId = ProjectSessionId.CreateNew();
        var project = new ActiveProjectSnapshot(
            sessionId,
            Guid.NewGuid(),
            name,
            $"C:\\Projects\\{name}");
        return ProjectSessionSnapshot.Ready(
            project,
            new SceneDocumentSnapshot(
                Guid.NewGuid(),
                $"C:\\Projects\\{name}\\Assets\\Default.asharia.scene.json",
                revision: 1,
                savedRevision: 1,
                entities: []),
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

        public IReadOnlyList<Request> Requests
        {
            get
            {
                lock (gate_)
                {
                    return requests_.ToArray();
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
            }
            return new ValueTask<AssetCatalogQueryResult>(request.Result.Task);
        }
    }

    private sealed class Request(
        AssetCatalogQueryScope scope,
        CancellationToken token)
    {
        public AssetCatalogQueryScope Scope { get; } = scope;
        public CancellationToken Token { get; } = token;
        public TaskCompletionSource<AssetCatalogQueryResult> Result { get; } =
            new(TaskCreationOptions.RunContinuationsAsynchronously);

        public void Complete(AssetCatalogQueryResult result) => Result.TrySetResult(result);
    }

    private sealed class TestResourceBrowserScheduler :
        IStudioResourceBrowserUiScheduler
    {
        private readonly object gate_ = new();
        private readonly Queue<Action> postedActions_ = [];
        private ScheduledWork? scheduledWork_;

        public bool HasPostedWork
        {
            get
            {
                lock (gate_)
                {
                    return postedActions_.Count != 0;
                }
            }
        }

        public bool WasScheduledWorkDisposed { get; private set; }

        public void Post(Action action)
        {
            lock (gate_)
            {
                postedActions_.Enqueue(action);
            }
        }

        public IDisposable Schedule(Action action, TimeSpan delay)
        {
            scheduledWork_ = new ScheduledWork(
                action,
                () => WasScheduledWorkDisposed = true);
            return scheduledWork_;
        }

        public void DrainPosts()
        {
            while (true)
            {
                Action action;
                lock (gate_)
                {
                    if (postedActions_.Count == 0)
                    {
                        return;
                    }
                    action = postedActions_.Dequeue();
                }
                action();
            }
        }

        public void RunScheduled()
        {
            var work = scheduledWork_;
            scheduledWork_ = null;
            work?.Run();
        }

        private sealed class ScheduledWork(
            Action action,
            Action onDispose) : IDisposable
        {
            private bool isDisposed_;

            public void Run()
            {
                if (!isDisposed_)
                {
                    isDisposed_ = true;
                    action();
                }
            }

            public void Dispose()
            {
                if (!isDisposed_)
                {
                    isDisposed_ = true;
                    onDispose();
                }
            }
        }
    }
}
