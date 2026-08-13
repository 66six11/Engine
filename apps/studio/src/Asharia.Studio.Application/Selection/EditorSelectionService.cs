using System;
using System.Collections.Generic;
using System.Linq;
using System.Threading;
using Asharia.Studio.Application.Assets;
using Asharia.Studio.Application.Projects;

namespace Asharia.Studio.Application.Selection;

public sealed class EditorSelectionService : IEditorSelectionService
{
    private readonly object gate_ = new();
    private readonly IProjectSession projectSession_;
    private readonly IProjectAssetCatalog assetCatalog_;
    private readonly Queue<EditorSelectionSnapshot> publications_ = [];
    private EditorSelectionSnapshot current_ = new(
        revision: 0,
        primary: null,
        EditorSelectionChangeReason.Initialization);
    private bool isPublishing_;
    private bool isDisposed_;
    private int publishingThreadId_;

    public EditorSelectionService(
        IProjectSession projectSession,
        IProjectAssetCatalog assetCatalog)
    {
        ArgumentNullException.ThrowIfNull(projectSession);
        ArgumentNullException.ThrowIfNull(assetCatalog);
        projectSession_ = projectSession;
        assetCatalog_ = assetCatalog;
        projectSession_.SnapshotChanged += OnProjectSnapshotChanged;
        assetCatalog_.SnapshotChanged += OnCatalogSnapshotChanged;
    }

    public event EventHandler<EditorSelectionChangedEventArgs>? Changed;

    public EditorSelectionSnapshot Current
    {
        get
        {
            lock (gate_)
            {
                return current_;
            }
        }
    }

    public bool Replace(
        EditorSelectionTarget target,
        EditorSelectionChangeReason reason = EditorSelectionChangeReason.User)
    {
        ArgumentNullException.ThrowIfNull(target);
        ValidateReason(reason);

        bool shouldPublish;
        lock (gate_)
        {
            ThrowIfDisposedLocked();
            if (!IsCurrentTarget(target) || Equals(current_.Primary, target))
            {
                return false;
            }

            shouldPublish = CommitLocked(target, reason);
        }

        if (shouldPublish)
        {
            DrainPublications();
        }
        return true;
    }

    public bool Clear(
        EditorSelectionChangeReason reason = EditorSelectionChangeReason.User)
    {
        ValidateReason(reason);

        bool shouldPublish;
        lock (gate_)
        {
            ThrowIfDisposedLocked();
            if (current_.Primary is null)
            {
                return false;
            }

            shouldPublish = CommitLocked(primary: null, reason);
        }

        if (shouldPublish)
        {
            DrainPublications();
        }
        return true;
    }

    public void Dispose()
    {
        lock (gate_)
        {
            if (isDisposed_)
            {
                return;
            }

            isDisposed_ = true;
            publications_.Clear();
            Changed = null;
        }

        projectSession_.SnapshotChanged -= OnProjectSnapshotChanged;
        assetCatalog_.SnapshotChanged -= OnCatalogSnapshotChanged;

        lock (gate_)
        {
            var callerThreadId = Environment.CurrentManagedThreadId;
            while (isPublishing_ && publishingThreadId_ != callerThreadId)
            {
                Monitor.Wait(gate_);
            }
        }
    }

    private void OnProjectSnapshotChanged(
        object? sender,
        ProjectSessionSnapshotChangedEventArgs eventArgs)
    {
        ArgumentNullException.ThrowIfNull(eventArgs);
        EvaluateProjectSnapshot();
    }

    private void OnCatalogSnapshotChanged(
        object? sender,
        AssetCatalogSessionSnapshotChangedEventArgs eventArgs)
    {
        ArgumentNullException.ThrowIfNull(eventArgs);
        EvaluateCatalogSnapshot();
    }

    private void EvaluateProjectSnapshot()
    {
        bool shouldPublish;
        lock (gate_)
        {
            if (isDisposed_)
            {
                return;
            }

            var snapshot = projectSession_.Current;
            var reason = current_.Primary switch
            {
                SceneObjectSelectionTarget sceneTarget
                    when !IsSameSceneScope(snapshot, sceneTarget) =>
                    EditorSelectionChangeReason.ProjectScopeChanged,
                SceneObjectSelectionTarget sceneTarget
                    when !ContainsSceneObject(snapshot, sceneTarget.ObjectId) =>
                    EditorSelectionChangeReason.SceneTargetRemoved,
                AssetSelectionTarget assetTarget
                    when !IsSameProjectScope(snapshot, assetTarget) =>
                    EditorSelectionChangeReason.ProjectScopeChanged,
                _ => (EditorSelectionChangeReason?)null,
            };
            if (reason is null)
            {
                return;
            }

            shouldPublish = CommitLocked(primary: null, reason.Value);
        }

        if (shouldPublish)
        {
            DrainPublications();
        }
    }

    private void EvaluateCatalogSnapshot()
    {
        bool shouldPublish;
        lock (gate_)
        {
            if (isDisposed_ || current_.Primary is not AssetSelectionTarget target)
            {
                return;
            }

            var snapshot = assetCatalog_.Current;
            EditorSelectionChangeReason? reason = null;
            if (!IsSameCatalogScope(snapshot.Scope, target))
            {
                reason = EditorSelectionChangeReason.ProjectScopeChanged;
            }
            else if (snapshot.State == AssetCatalogSessionState.Failed
                     || snapshot.Catalog is null
                     || !ContainsAsset(snapshot.Catalog, target.Asset))
            {
                reason = EditorSelectionChangeReason.AssetTargetRemoved;
            }
            if (reason is null)
            {
                return;
            }

            shouldPublish = CommitLocked(primary: null, reason.Value);
        }

        if (shouldPublish)
        {
            DrainPublications();
        }
    }

    private bool CommitLocked(
        EditorSelectionTarget? primary,
        EditorSelectionChangeReason reason)
    {
        current_ = new EditorSelectionSnapshot(
            checked(current_.Revision + 1),
            primary,
            reason);
        publications_.Enqueue(current_);
        if (isPublishing_)
        {
            return false;
        }

        isPublishing_ = true;
        return true;
    }

    private void DrainPublications()
    {
        while (true)
        {
            EditorSelectionSnapshot snapshot;
            EventHandler<EditorSelectionChangedEventArgs>? handlers;
            lock (gate_)
            {
                if (isDisposed_ || publications_.Count == 0)
                {
                    publications_.Clear();
                    isPublishing_ = false;
                    publishingThreadId_ = 0;
                    Monitor.PulseAll(gate_);
                    return;
                }

                snapshot = publications_.Dequeue();
                handlers = Changed;
                publishingThreadId_ = Environment.CurrentManagedThreadId;
            }

            if (handlers is null)
            {
                continue;
            }

            var eventArgs = new EditorSelectionChangedEventArgs(snapshot);
            foreach (EventHandler<EditorSelectionChangedEventArgs> handler
                     in handlers.GetInvocationList())
            {
                lock (gate_)
                {
                    if (isDisposed_)
                    {
                        break;
                    }
                }

                try
                {
                    handler(this, eventArgs);
                }
                catch (Exception)
                {
                    // A presentation subscriber cannot corrupt selection state or ordering.
                }
            }
        }
    }

    private bool IsCurrentTarget(EditorSelectionTarget target)
    {
        var projectSnapshot = projectSession_.Current;
        return target switch
        {
            SceneObjectSelectionTarget sceneTarget =>
                IsSameSceneScope(projectSnapshot, sceneTarget)
                && ContainsSceneObject(projectSnapshot, sceneTarget.ObjectId),
            AssetSelectionTarget assetTarget =>
                IsSameProjectScope(projectSnapshot, assetTarget)
                && IsCurrentAsset(assetCatalog_.Current, assetTarget),
            _ => false,
        };
    }

    private static bool IsCurrentAsset(
        AssetCatalogSessionSnapshot snapshot,
        AssetSelectionTarget target) =>
        IsSameCatalogScope(snapshot.Scope, target)
        && snapshot.State is AssetCatalogSessionState.Ready
            or AssetCatalogSessionState.Loading
            or AssetCatalogSessionState.Degraded
        && ContainsAsset(snapshot.Catalog, target.Asset);

    private static bool IsSameSceneScope(
        ProjectSessionSnapshot snapshot,
        SceneObjectSelectionTarget target) =>
        snapshot.Project is { } project
        && snapshot.Document is { } document
        && project.SessionId == target.SessionId
        && document.SceneId == target.SceneId;

    private static bool IsSameProjectScope(
        ProjectSessionSnapshot snapshot,
        AssetSelectionTarget target) =>
        snapshot.Project is { } project
        && project.SessionId == target.SessionId
        && project.ProjectId == target.ProjectId;

    private static bool IsSameCatalogScope(
        AssetCatalogQueryScope? scope,
        AssetSelectionTarget target) =>
        scope is not null
        && scope.SessionId == target.SessionId
        && scope.ProjectId == target.ProjectId
        && string.Equals(
            scope.TargetProfile,
            target.TargetProfile,
            StringComparison.Ordinal);

    private static bool ContainsSceneObject(
        ProjectSessionSnapshot snapshot,
        Guid objectId) =>
        snapshot.Document?.Entities.Any(entity => entity.ObjectId == objectId) == true;

    private static bool ContainsAsset(
        AssetCatalogSnapshot? snapshot,
        AssetSelectionKey selectionKey) =>
        snapshot?.Entries.Any(entry => entry.SelectionKey == selectionKey) == true;

    private static void ValidateReason(EditorSelectionChangeReason reason)
    {
        if (!Enum.IsDefined(reason))
        {
            throw new ArgumentOutOfRangeException(nameof(reason), reason, null);
        }
    }

    private void ThrowIfDisposedLocked() =>
        ObjectDisposedException.ThrowIf(isDisposed_, this);
}
