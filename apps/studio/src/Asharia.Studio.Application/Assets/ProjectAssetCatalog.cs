using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Projects;

namespace Asharia.Studio.Application.Assets;

public sealed class ProjectAssetCatalog : IProjectAssetCatalog
{
    private const string DefaultProjectFileName = "asharia.project.json";
    private const string DefaultTargetProfile = "editor-preview";

    private readonly object gate_ = new();
    private readonly IProjectSession projectSession_;
    private readonly IAssetCatalogGateway gateway_;
    private readonly CancellationTokenSource lifetimeCancellation_ = new();
    private readonly HashSet<Task> requests_ = [];
    private readonly Queue<AssetCatalogSessionSnapshot> publications_ = new();
    private AssetCatalogSessionSnapshot current_ = AssetCatalogSessionSnapshot.NoProject();
    private AssetCatalogSessionSnapshot stable_ = AssetCatalogSessionSnapshot.NoProject();
    private CancellationTokenSource? activeRequestCancellation_;
    private ulong nextGeneration_;
    private bool isPublishing_;
    private bool isDisposed_;

    public ProjectAssetCatalog(
        IProjectSession projectSession,
        IAssetCatalogGateway gateway)
    {
        ArgumentNullException.ThrowIfNull(projectSession);
        ArgumentNullException.ThrowIfNull(gateway);
        projectSession_ = projectSession;
        gateway_ = gateway;
        projectSession_.SnapshotChanged += OnProjectSnapshotChanged;
        ApplyProjectSnapshot(projectSession_.Current);
    }

    public event EventHandler<AssetCatalogSessionSnapshotChangedEventArgs>? SnapshotChanged;

    public AssetCatalogSessionSnapshot Current
    {
        get
        {
            lock (gate_)
            {
                return current_;
            }
        }
    }

    public ValueTask RefreshAsync(CancellationToken cancellationToken = default)
    {
        Task? request = null;
        lock (gate_)
        {
            ObjectDisposedException.ThrowIf(isDisposed_, this);
            if (current_.Scope is { } scope)
            {
                request = StartRequestLocked(
                    scope,
                    cancellationToken,
                    requireCurrentScope: true);
            }
        }

        DrainPublications();
        return request is null
            ? ValueTask.CompletedTask
            : new ValueTask(request);
    }

    public async ValueTask DisposeAsync()
    {
        Task[] requests;
        lock (gate_)
        {
            if (isDisposed_)
            {
                return;
            }

            isDisposed_ = true;
            projectSession_.SnapshotChanged -= OnProjectSnapshotChanged;
            lifetimeCancellation_.Cancel();
            activeRequestCancellation_?.Cancel();
            requests = requests_.ToArray();
        }

        try
        {
            await Task.WhenAll(requests).ConfigureAwait(false);
        }
        finally
        {
            lifetimeCancellation_.Dispose();
        }
    }

    private void OnProjectSnapshotChanged(
        object? sender,
        ProjectSessionSnapshotChangedEventArgs e) =>
        ApplyProjectSnapshot(projectSession_.Current);

    private void ApplyProjectSnapshot(ProjectSessionSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);
        if (!snapshot.IsReady)
        {
            PublishNoProject();
            return;
        }

        var scope = CreateScope(snapshot.Project!);
        lock (gate_)
        {
            if (isDisposed_ || IsSameScope(current_.Scope, scope))
            {
                return;
            }

            var authoritative = projectSession_.Current;
            if (!authoritative.IsReady
                || !IsSameScope(CreateScope(authoritative.Project!), scope))
            {
                return;
            }

            _ = StartRequestLocked(
                scope,
                CancellationToken.None,
                requireCurrentScope: false);
        }

        DrainPublications();
    }

    private void PublishNoProject()
    {
        lock (gate_)
        {
            if (isDisposed_
                || projectSession_.Current.IsReady
                || current_.State == AssetCatalogSessionState.NoProject)
            {
                return;
            }

            activeRequestCancellation_?.Cancel();
            var generation = NextGeneration();
            CommitLocked(AssetCatalogSessionSnapshot.NoProject(generation));
        }
        DrainPublications();
    }

    private Task? StartRequestLocked(
        AssetCatalogQueryScope scope,
        CancellationToken cancellationToken,
        bool requireCurrentScope)
    {
        System.Diagnostics.Debug.Assert(
            Monitor.IsEntered(gate_),
            "Asset catalog requests must be committed while holding the owner gate.");
        CancellationTokenSource requestCancellation;
        AssetCatalogSessionSnapshot previous;
        TaskCompletionSource completion;
        ulong generation;
        if (isDisposed_
            || requireCurrentScope && !IsSameScope(current_.Scope, scope))
        {
            return null;
        }

        activeRequestCancellation_?.Cancel();
        requestCancellation = CancellationTokenSource.CreateLinkedTokenSource(
            lifetimeCancellation_.Token,
            cancellationToken);
        activeRequestCancellation_ = requestCancellation;
        generation = NextGeneration();
        previous = current_.State == AssetCatalogSessionState.Loading
            && IsSameScope(stable_.Scope, scope)
                ? stable_
                : current_;
        var lastGood = IsSameScope(current_.Scope, scope)
            ? current_.Catalog
            : null;
        CommitLocked(AssetCatalogSessionSnapshot.Loading(
            scope,
            lastGood,
            generation));
        completion = new TaskCompletionSource(
            TaskCreationOptions.RunContinuationsAsynchronously);
        requests_.Add(completion.Task);
        _ = Task.Run(
            () => ExecuteTrackedRequestAsync(
                scope,
                generation,
                previous,
                requestCancellation,
                completion),
            CancellationToken.None);
        return completion.Task;
    }

    private async Task ExecuteTrackedRequestAsync(
        AssetCatalogQueryScope scope,
        ulong generation,
        AssetCatalogSessionSnapshot previous,
        CancellationTokenSource requestCancellation,
        TaskCompletionSource completion)
    {
        try
        {
            await RunRequestAsync(
                scope,
                generation,
                previous,
                requestCancellation).ConfigureAwait(false);
        }
        finally
        {
            lock (gate_)
            {
                requests_.Remove(completion.Task);
                if (ReferenceEquals(activeRequestCancellation_, requestCancellation))
                {
                    activeRequestCancellation_ = null;
                }
            }
            requestCancellation.Dispose();
            completion.TrySetResult();
        }
    }

    private async Task RunRequestAsync(
        AssetCatalogQueryScope scope,
        ulong generation,
        AssetCatalogSessionSnapshot previous,
        CancellationTokenSource requestCancellation)
    {
        AssetCatalogQueryResult result;
        try
        {
            result = await gateway_.QueryAsync(
                scope,
                requestCancellation.Token).ConfigureAwait(false);
            if (result is null)
            {
                result = AssetCatalogQueryResult.Failed(
                    new AssetCatalogQueryFailure(
                        AssetCatalogQueryFailureKind.InvalidResponse,
                        "The asset catalog gateway returned no result."));
            }
        }
        catch (OperationCanceledException) when (requestCancellation.IsCancellationRequested)
        {
            RestoreAfterCancellation(scope, generation, previous);
            return;
        }
        catch (Exception exception)
        {
            result = AssetCatalogQueryResult.Failed(
                new AssetCatalogQueryFailure(
                    AssetCatalogQueryFailureKind.InternalError,
                    string.IsNullOrWhiteSpace(exception.Message)
                        ? "The asset catalog query failed unexpectedly."
                        : exception.Message));
        }
        lock (gate_)
        {
            if (isDisposed_
                || current_.RequestGeneration != generation
                || !IsSameScope(current_.Scope, scope))
            {
                return;
            }

            if (result.Snapshot is { } snapshot)
            {
                CommitLocked(snapshot.State == AssetCatalogSnapshotState.Ready
                    ? AssetCatalogSessionSnapshot.Ready(
                        scope,
                        snapshot,
                        generation)
                    : AssetCatalogSessionSnapshot.Degraded(
                        scope,
                        snapshot,
                        failure: null,
                        generation));
            }
            else
            {
                var failure = result.Failure!;
                CommitLocked(current_.Catalog is { } lastGood
                    ? AssetCatalogSessionSnapshot.Degraded(
                        scope,
                        lastGood,
                        failure,
                        generation)
                    : AssetCatalogSessionSnapshot.Failed(
                        scope,
                        failure,
                        generation));
            }
        }
        DrainPublications();
    }

    private void RestoreAfterCancellation(
        AssetCatalogQueryScope scope,
        ulong generation,
        AssetCatalogSessionSnapshot previous)
    {
        lock (gate_)
        {
            if (isDisposed_
                || current_.RequestGeneration != generation
                || !IsSameScope(current_.Scope, scope))
            {
                return;
            }

            var restored = previous.State switch
            {
                AssetCatalogSessionState.Ready when previous.Catalog is { } catalog =>
                    AssetCatalogSessionSnapshot.Ready(scope, catalog, generation),
                AssetCatalogSessionState.Degraded
                    when previous.Catalog is { } catalog =>
                    AssetCatalogSessionSnapshot.Degraded(
                        scope,
                        catalog,
                        previous.Failure,
                        generation),
                AssetCatalogSessionState.Failed when previous.Failure is { } failure =>
                    AssetCatalogSessionSnapshot.Failed(scope, failure, generation),
                _ => AssetCatalogSessionSnapshot.Failed(
                    scope,
                    new AssetCatalogQueryFailure(
                        AssetCatalogQueryFailureKind.InternalError,
                        "The initial asset catalog query was cancelled."),
                    generation),
            };
            CommitLocked(restored);
        }
        DrainPublications();
    }

    private ulong NextGeneration()
    {
        nextGeneration_ = checked(nextGeneration_ + 1);
        return nextGeneration_;
    }

    private void CommitLocked(AssetCatalogSessionSnapshot snapshot)
    {
        current_ = snapshot;
        if (snapshot.State != AssetCatalogSessionState.Loading)
        {
            stable_ = snapshot;
        }
        publications_.Enqueue(snapshot);
    }

    private void DrainPublications()
    {
        lock (gate_)
        {
            if (isPublishing_)
            {
                return;
            }
            isPublishing_ = true;
        }

        while (true)
        {
            AssetCatalogSessionSnapshot snapshot;
            EventHandler<AssetCatalogSessionSnapshotChangedEventArgs>? handlers;
            lock (gate_)
            {
                if (publications_.Count == 0)
                {
                    isPublishing_ = false;
                    return;
                }
                snapshot = publications_.Dequeue();
                handlers = SnapshotChanged;
            }

            if (handlers is null)
            {
                continue;
            }
            var args = new AssetCatalogSessionSnapshotChangedEventArgs(snapshot);
            foreach (EventHandler<AssetCatalogSessionSnapshotChangedEventArgs> handler
                     in handlers.GetInvocationList())
            {
                try
                {
                    handler(this, args);
                }
                catch (Exception)
                {
                    // A presentation subscriber cannot corrupt the authoritative catalog state.
                }
            }
        }
    }

    private static AssetCatalogQueryScope CreateScope(ActiveProjectSnapshot project) =>
        new(
            project.SessionId,
            project.ProjectId,
            project.RootPath,
            Path.Combine(project.RootPath, DefaultProjectFileName),
            DefaultTargetProfile);

    private static bool IsSameScope(
        AssetCatalogQueryScope? left,
        AssetCatalogQueryScope? right) =>
        left is not null
        && right is not null
        && left.SessionId == right.SessionId
        && left.ProjectId == right.ProjectId
        && string.Equals(left.ProjectRootPath, right.ProjectRootPath, StringComparison.Ordinal)
        && string.Equals(left.ProjectFilePath, right.ProjectFilePath, StringComparison.Ordinal)
        && string.Equals(left.TargetProfile, right.TargetProfile, StringComparison.Ordinal);

}
