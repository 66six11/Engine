using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;

namespace Asharia.Studio.Application.Scenes;

public sealed record SceneEntitySnapshot
{
    public SceneEntitySnapshot(Guid objectId, string name, TransformValue transform)
    {
        if (objectId == Guid.Empty)
        {
            throw new ArgumentException("Scene object id must not be empty.", nameof(objectId));
        }
        ArgumentNullException.ThrowIfNull(name);

        ObjectId = objectId;
        Name = name;
        Transform = transform;
    }

    public Guid ObjectId { get; }

    public string Name { get; }

    public TransformValue Transform { get; }
}

public sealed record SceneDocumentSnapshot
{
    public SceneDocumentSnapshot(
        Guid sceneId,
        string path,
        ulong revision,
        ulong savedRevision,
        IEnumerable<SceneEntitySnapshot> entities)
    {
        if (sceneId == Guid.Empty)
        {
            throw new ArgumentException("Scene id must not be empty.", nameof(sceneId));
        }
        if (string.IsNullOrWhiteSpace(path))
        {
            throw new ArgumentException("Scene path must not be empty.", nameof(path));
        }
        if (revision == 0 || savedRevision == 0 || savedRevision > revision)
        {
            throw new ArgumentOutOfRangeException(
                nameof(revision),
                "Scene revisions must be non-zero and the savepoint must not exceed the current revision.");
        }
        ArgumentNullException.ThrowIfNull(entities);

        SceneId = sceneId;
        Path = path;
        Revision = revision;
        SavedRevision = savedRevision;
        Entities = new ReadOnlyCollection<SceneEntitySnapshot>(entities.ToArray());
    }

    public Guid SceneId { get; }

    public string Path { get; }

    public ulong Revision { get; }

    public ulong SavedRevision { get; }

    public IReadOnlyList<SceneEntitySnapshot> Entities { get; }

    public bool IsDirty => Revision != SavedRevision;
}

public enum SceneDocumentFailureKind
{
    InvalidInput,
    InvalidScene,
    RevisionConflict,
    InvalidObject,
    InvalidTransform,
    IoFailure,
    NativeUnavailable,
    InternalError,
}

public sealed record SceneDocumentFailure
{
    public SceneDocumentFailure(SceneDocumentFailureKind kind, string message)
    {
        if (!Enum.IsDefined(kind))
        {
            throw new ArgumentOutOfRangeException(nameof(kind), kind, null);
        }
        if (string.IsNullOrWhiteSpace(message))
        {
            throw new ArgumentException("Scene document failure message must not be empty.", nameof(message));
        }

        Kind = kind;
        Message = message;
    }

    public SceneDocumentFailureKind Kind { get; }

    public string Message { get; }
}

public sealed record SceneDocumentOperationResult
{
    private SceneDocumentOperationResult(
        SceneDocumentSnapshot current,
        SceneDocumentFailure? failure)
    {
        ArgumentNullException.ThrowIfNull(current);
        Current = current;
        Failure = failure;
    }

    public SceneDocumentSnapshot Current { get; }

    public SceneDocumentFailure? Failure { get; }

    public bool Succeeded => Failure is null;

    public static SceneDocumentOperationResult Success(SceneDocumentSnapshot current) =>
        new(current, failure: null);

    public static SceneDocumentOperationResult Failed(
        SceneDocumentSnapshot current,
        SceneDocumentFailure failure)
    {
        ArgumentNullException.ThrowIfNull(failure);
        return new SceneDocumentOperationResult(current, failure);
    }
}

public sealed record SceneDocumentOpenResult
{
    private SceneDocumentOpenResult(
        ISceneDocumentConnection? connection,
        SceneDocumentSnapshot? document,
        SceneDocumentFailure? failure)
    {
        if ((connection is null) != (document is null) ||
            ((connection is null) == (failure is null)))
        {
            throw new ArgumentException(
                "A scene document open result must contain either a connection and document, or a failure.");
        }

        Connection = connection;
        Document = document;
        Failure = failure;
    }

    public ISceneDocumentConnection? Connection { get; }

    public SceneDocumentSnapshot? Document { get; }

    public SceneDocumentFailure? Failure { get; }

    public bool Succeeded => Connection is not null;

    public static SceneDocumentOpenResult Success(
        ISceneDocumentConnection connection,
        SceneDocumentSnapshot document)
    {
        ArgumentNullException.ThrowIfNull(connection);
        ArgumentNullException.ThrowIfNull(document);
        return new SceneDocumentOpenResult(connection, document, failure: null);
    }

    public static SceneDocumentOpenResult Failed(SceneDocumentFailure failure)
    {
        ArgumentNullException.ThrowIfNull(failure);
        return new SceneDocumentOpenResult(connection: null, document: null, failure);
    }
}

public interface ISceneDocumentConnection : IAsyncDisposable
{
    ValueTask<SceneDocumentOperationResult> CreateEntityAsync(
        Guid objectId,
        string name,
        ulong expectedRevision,
        CancellationToken cancellationToken = default);

    ValueTask<SceneDocumentOperationResult> SetEntityNameAsync(
        Guid objectId,
        string name,
        ulong expectedRevision,
        CancellationToken cancellationToken = default);

    ValueTask<SceneDocumentOperationResult> SetEntityTransformAsync(
        Guid objectId,
        TransformValue transform,
        ulong expectedRevision,
        CancellationToken cancellationToken = default);

    ValueTask<SceneDocumentOperationResult> SaveAsync(
        ulong expectedRevision,
        CancellationToken cancellationToken = default);
}

public interface ISceneDocumentGateway
{
    ValueTask<SceneDocumentOpenResult> OpenDefaultAsync(
        string projectRoot,
        Guid newSceneId,
        CancellationToken cancellationToken = default);
}
