using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;

namespace Asharia.Studio.Application.Scenes;

public readonly record struct SceneMeshReference
{
    public SceneMeshReference(Guid assetId)
    {
        if (assetId == Guid.Empty)
        {
            throw new ArgumentException("Mesh asset id must not be empty.", nameof(assetId));
        }

        AssetId = assetId;
    }

    public Guid AssetId { get; }

    public static SceneMeshReference DirectionalWedgeValidation { get; } =
        new(Guid.Parse("7c9fe8ac-3c8b-4f66-9665-0af0fd7b693e"));
}

public sealed record SceneEntitySnapshot
{
    public SceneEntitySnapshot(
        Guid objectId,
        EntityId runtimeEntityId,
        string name,
        TransformValue transform,
        SceneMeshReference? mesh = null)
    {
        if (objectId == Guid.Empty)
        {
            throw new ArgumentException("Scene object id must not be empty.", nameof(objectId));
        }
        if (!runtimeEntityId.IsValid)
        {
            throw new ArgumentException(
                "Scene runtime entity id must be valid.",
                nameof(runtimeEntityId));
        }
        if (mesh.HasValue && mesh.Value.AssetId == Guid.Empty)
        {
            throw new ArgumentException(
                "Scene mesh asset id must not be empty.",
                nameof(mesh));
        }
        ArgumentNullException.ThrowIfNull(name);

        ObjectId = objectId;
        RuntimeEntityId = runtimeEntityId;
        Name = name;
        Transform = transform;
        Mesh = mesh;
    }

    public Guid ObjectId { get; }

    public EntityId RuntimeEntityId { get; }

    public string Name { get; }

    public TransformValue Transform { get; }

    public SceneMeshReference? Mesh { get; }
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
}

public enum SceneDocumentFailureKind
{
    InvalidInput,
    InvalidScene,
    RevisionConflict,
    InvalidObject,
    InvalidTransform,
    InvalidAssetReference,
    RevisionExhausted,
    IoFailure,
    NativeUnavailable,
    AuthoritativeStateUnknown,
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

public sealed record SceneEntityTransformReceipt
{
    public SceneEntityTransformReceipt(
        Guid objectId,
        bool changed,
        TransformValue beforeTransform,
        TransformValue afterTransform,
        ulong beforeRevision,
        ulong afterRevision)
    {
        if (objectId == Guid.Empty)
        {
            throw new ArgumentException("Scene object id must not be empty.", nameof(objectId));
        }
        if (beforeRevision == 0 || afterRevision == 0 ||
            (changed
                ? beforeRevision == ulong.MaxValue || afterRevision != beforeRevision + 1 ||
                  beforeTransform == afterTransform
                : afterRevision != beforeRevision || beforeTransform != afterTransform))
        {
            throw new ArgumentException("Scene Transform receipt revision or no-op state is invalid.");
        }

        ObjectId = objectId;
        Changed = changed;
        BeforeTransform = beforeTransform;
        AfterTransform = afterTransform;
        BeforeRevision = beforeRevision;
        AfterRevision = afterRevision;
    }

    public Guid ObjectId { get; }

    public bool Changed { get; }

    public TransformValue BeforeTransform { get; }

    public TransformValue AfterTransform { get; }

    public ulong BeforeRevision { get; }

    public ulong AfterRevision { get; }
}

public sealed record SceneEntityMeshReceipt
{
    public SceneEntityMeshReceipt(
        Guid objectId,
        bool changed,
        SceneMeshReference? beforeMesh,
        SceneMeshReference? afterMesh,
        ulong beforeRevision,
        ulong afterRevision)
    {
        if (objectId == Guid.Empty)
        {
            throw new ArgumentException("Scene object id must not be empty.", nameof(objectId));
        }
        if (beforeRevision == 0 || afterRevision == 0 ||
            (changed
                ? beforeRevision == ulong.MaxValue || afterRevision != beforeRevision + 1 ||
                  beforeMesh == afterMesh
                : afterRevision != beforeRevision || beforeMesh != afterMesh))
        {
            throw new ArgumentException("Scene Mesh receipt revision or no-op state is invalid.");
        }

        if (beforeMesh?.AssetId == Guid.Empty || afterMesh?.AssetId == Guid.Empty)
        {
            throw new ArgumentException("Mesh reference must not be empty.");
        }
        ObjectId = objectId;
        Changed = changed;
        BeforeMesh = beforeMesh;
        AfterMesh = afterMesh;
        BeforeRevision = beforeRevision;
        AfterRevision = afterRevision;
    }

    public Guid ObjectId { get; }

    public bool Changed { get; }

    public SceneMeshReference? BeforeMesh { get; }

    public SceneMeshReference? AfterMesh { get; }

    public ulong BeforeRevision { get; }

    public ulong AfterRevision { get; }
}

public sealed record SceneDocumentOperationResult
{
    private SceneDocumentOperationResult(
        SceneDocumentSnapshot current,
        SceneDocumentFailure? failure,
        SceneEntityTransformReceipt? transformReceipt,
        SceneEntityMeshReceipt? meshReceipt = null)
    {
        ArgumentNullException.ThrowIfNull(current);
        Current = current;
        Failure = failure;
        TransformReceipt = transformReceipt;
        MeshReceipt = meshReceipt;
    }

    public SceneDocumentSnapshot Current { get; }

    public SceneDocumentFailure? Failure { get; }

    public SceneEntityTransformReceipt? TransformReceipt { get; }

    public SceneEntityMeshReceipt? MeshReceipt { get; }

    public bool Succeeded => Failure is null;

    public static SceneDocumentOperationResult Success(SceneDocumentSnapshot current) =>
        new(current, failure: null, transformReceipt: null);

    public static SceneDocumentOperationResult Success(
        SceneDocumentSnapshot current,
        SceneEntityTransformReceipt transformReceipt)
    {
        ArgumentNullException.ThrowIfNull(transformReceipt);
        var entity = current.Entities.FirstOrDefault(
            candidate => candidate.ObjectId == transformReceipt.ObjectId);
        if (current.Revision != transformReceipt.AfterRevision ||
            entity is null || entity.Transform != transformReceipt.AfterTransform)
        {
            throw new ArgumentException(
                "Scene Transform receipt must match the authoritative snapshot.",
                nameof(transformReceipt));
        }
        return new SceneDocumentOperationResult(current, failure: null, transformReceipt);
    }

    public static SceneDocumentOperationResult Success(
        SceneDocumentSnapshot current,
        SceneEntityMeshReceipt meshReceipt)
    {
        ArgumentNullException.ThrowIfNull(meshReceipt);
        var entity = current.Entities.FirstOrDefault(
            candidate => candidate.ObjectId == meshReceipt.ObjectId);
        if (current.Revision != meshReceipt.AfterRevision ||
            entity is null || entity.Mesh != meshReceipt.AfterMesh)
        {
            throw new ArgumentException(
                "Scene Mesh receipt must match the authoritative snapshot.",
                nameof(meshReceipt));
        }
        return new SceneDocumentOperationResult(current, failure: null, transformReceipt: null, meshReceipt: meshReceipt);
    }

    public static SceneDocumentOperationResult Failed(
        SceneDocumentSnapshot current,
        SceneDocumentFailure failure)
    {
        ArgumentNullException.ThrowIfNull(failure);
        return new SceneDocumentOperationResult(current, failure, transformReceipt: null);
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
    ValueTask<SceneDocumentOperationResult> RefreshAsync(
        CancellationToken cancellationToken = default);

    ValueTask<SceneDocumentOperationResult> CreateEntityAsync(
        Guid objectId,
        string name,
        ulong expectedRevision,
        CancellationToken cancellationToken = default);

    ValueTask<SceneDocumentOperationResult> CreateMeshEntityAsync(
        Guid objectId,
        string name,
        SceneMeshReference mesh,
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

    ValueTask<SceneDocumentOperationResult> SetEntityMeshAsync(
        Guid objectId,
        SceneMeshReference? mesh,
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
