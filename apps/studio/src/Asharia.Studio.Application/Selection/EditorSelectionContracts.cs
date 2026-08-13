using System;
using Asharia.Studio.Application.Assets;
using Asharia.Studio.Application.Projects;

namespace Asharia.Studio.Application.Selection;

public abstract record EditorSelectionTarget;

public sealed record SceneObjectSelectionTarget : EditorSelectionTarget
{
    public SceneObjectSelectionTarget(
        ProjectSessionId sessionId,
        Guid sceneId,
        Guid objectId)
    {
        if (!sessionId.IsValid)
        {
            throw new ArgumentException(
                "A scene selection requires a valid project session id.",
                nameof(sessionId));
        }
        if (sceneId == Guid.Empty)
        {
            throw new ArgumentException(
                "A scene selection requires a non-empty scene id.",
                nameof(sceneId));
        }
        if (objectId == Guid.Empty)
        {
            throw new ArgumentException(
                "A scene selection requires a non-empty object id.",
                nameof(objectId));
        }

        SessionId = sessionId;
        SceneId = sceneId;
        ObjectId = objectId;
    }

    public ProjectSessionId SessionId { get; }
    public Guid SceneId { get; }
    public Guid ObjectId { get; }
}

public sealed record AssetSelectionTarget : EditorSelectionTarget
{
    public AssetSelectionTarget(
        ProjectSessionId sessionId,
        Guid projectId,
        string targetProfile,
        AssetSelectionKey asset)
    {
        if (!sessionId.IsValid)
        {
            throw new ArgumentException(
                "An asset selection requires a valid project session id.",
                nameof(sessionId));
        }
        if (projectId == Guid.Empty)
        {
            throw new ArgumentException(
                "An asset selection requires a non-empty project id.",
                nameof(projectId));
        }
        ArgumentException.ThrowIfNullOrWhiteSpace(targetProfile);
        if (asset.AssetGuid is null && string.IsNullOrWhiteSpace(asset.SourcePath))
        {
            throw new ArgumentException(
                "An asset selection requires a valid catalog selection key.",
                nameof(asset));
        }

        SessionId = sessionId;
        ProjectId = projectId;
        TargetProfile = targetProfile;
        Asset = asset;
    }

    public ProjectSessionId SessionId { get; }
    public Guid ProjectId { get; }
    public string TargetProfile { get; }
    public AssetSelectionKey Asset { get; }
}

public enum EditorSelectionChangeReason
{
    Initialization,
    User,
    ProjectScopeChanged,
    SceneTargetRemoved,
    AssetTargetRemoved,
}

public sealed record EditorSelectionSnapshot
{
    public EditorSelectionSnapshot(
        ulong revision,
        EditorSelectionTarget? primary,
        EditorSelectionChangeReason reason)
    {
        if (!Enum.IsDefined(reason))
        {
            throw new ArgumentOutOfRangeException(nameof(reason), reason, null);
        }

        Revision = revision;
        Primary = primary;
        Reason = reason;
    }

    public ulong Revision { get; }
    public EditorSelectionTarget? Primary { get; }
    public EditorSelectionChangeReason Reason { get; }
}

public sealed class EditorSelectionChangedEventArgs(
    EditorSelectionSnapshot snapshot) : EventArgs
{
    public EditorSelectionSnapshot Snapshot { get; } =
        snapshot ?? throw new ArgumentNullException(nameof(snapshot));
}

public interface IEditorSelectionService : IDisposable
{
    event EventHandler<EditorSelectionChangedEventArgs>? Changed;

    EditorSelectionSnapshot Current { get; }

    bool Replace(
        EditorSelectionTarget target,
        EditorSelectionChangeReason reason = EditorSelectionChangeReason.User);

    bool Clear(
        EditorSelectionChangeReason reason = EditorSelectionChangeReason.User);
}
