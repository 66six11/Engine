using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.EngineBridge.Scene.Abi;

namespace Asharia.Studio.EngineBridge.Scene;

public sealed class SceneDocumentBridge : ISceneDocumentGateway
{
    private const int InitialResponseCapacity = 64 * 1024;
    private const int MaximumResponseCapacity = 128 * 1024 * 1024;
    private const ulong MaximumSceneEntityCount = 10_000;
    private static readonly UTF8Encoding StrictUtf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);

    private readonly ISceneDocumentNativeApi nativeApi_;

    public SceneDocumentBridge()
        : this(SceneDocumentNativeLibraryApi.Instance)
    {
    }

    internal SceneDocumentBridge(ISceneDocumentNativeApi nativeApi)
    {
        ArgumentNullException.ThrowIfNull(nativeApi);
        nativeApi_ = nativeApi;
    }

    public ValueTask<SceneDocumentOpenResult> OpenDefaultAsync(
        string projectRoot,
        Guid newSceneId,
        CancellationToken cancellationToken = default)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(projectRoot);
        if (newSceneId == Guid.Empty)
        {
            throw new ArgumentException("New scene id must not be empty.", nameof(newSceneId));
        }
        cancellationToken.ThrowIfCancellationRequested();
        return new ValueTask<SceneDocumentOpenResult>(
            OpenOnOwnerLaneAsync(projectRoot, newSceneId));
    }

    private async Task<SceneDocumentOpenResult> OpenOnOwnerLaneAsync(
        string projectRoot,
        Guid newSceneId)
    {
        var lane = new SceneDocumentOwnerLane();
        try
        {
            var opened = await lane.InvokeAsync(
                () => OpenNative(projectRoot, newSceneId)).ConfigureAwait(false);
            if (!opened.Succeeded)
            {
                lane.Dispose();
                return SceneDocumentOpenResult.Failed(opened.Failure!);
            }

            return SceneDocumentOpenResult.Success(
                new SceneDocumentConnection(
                    this,
                    lane,
                    opened.Handle,
                    projectRoot,
                    opened.Document!),
                opened.Document!);
        }
        catch (EncoderFallbackException exception)
        {
            lane.Dispose();
            return FailedOpen(SceneDocumentFailureKind.InvalidInput, exception.Message);
        }
        catch (Exception exception) when (IsNativeBindingFailure(exception))
        {
            lane.Dispose();
            return FailedOpen(
                SceneDocumentFailureKind.NativeUnavailable,
                $"The canonical scene adapter is unavailable: {exception.Message}");
        }
        catch (Exception exception)
        {
            lane.Dispose();
            return FailedOpen(
                SceneDocumentFailureKind.InternalError,
                DiagnosticMessage(exception, "The native scene document owner lane failed."));
        }
    }

    private unsafe NativeOpenOutcome OpenNative(string projectRoot, Guid newSceneId)
    {
        var rootBytes = StrictUtf8.GetBytes(projectRoot);
        var sceneIdBytes = StrictUtf8.GetBytes(newSceneId.ToString("D"));
        var response = new byte[InitialResponseCapacity];
        fixed (byte* root = rootBytes)
        fixed (byte* sceneId = sceneIdBytes)
        {
            var request = SceneNativeDocumentOpenDefaultRequest.Current(
                (nint)root,
                (ulong)rootBytes.Length,
                (nint)sceneId,
                (ulong)sceneIdBytes.Length);
            while (true)
            {
                SceneNativeStatus status;
                SceneNativeDocumentHandle handle;
                SceneNativeDocumentOperationResult result;
                fixed (byte* responsePointer = response)
                {
                    status = nativeApi_.OpenDefault(
                        in request,
                        out handle,
                        (nint)responsePointer,
                        (ulong)response.Length,
                        out result);
                }
                if (status == SceneNativeStatus.BufferTooSmall)
                {
                    if (!TryGrowResponse(response, result.RequiredByteLength, out response))
                    {
                        CloseIfValid(ref handle);
                        return NativeOpenOutcome.Failed(InvalidNativeResponse(
                            "The scene adapter returned an invalid open response size."));
                    }
                    CloseIfValid(ref handle);
                    continue;
                }

                var validation = ValidateOperation(status, result, response);
                if (validation is not null)
                {
                    CloseIfValid(ref handle);
                    return NativeOpenOutcome.Failed(validation);
                }
                if (status != SceneNativeStatus.Success)
                {
                    CloseIfValid(ref handle);
                    return NativeOpenOutcome.Failed(Failure(
                        status,
                        Decode(response, result.MessageUtf8, result.RequiredByteLength)));
                }
                if (!handle.IsValid || result.Revision == 0 || result.SavedRevision == 0)
                {
                    CloseIfValid(ref handle);
                    return NativeOpenOutcome.Failed(InvalidNativeResponse(
                        "The scene adapter returned an incomplete open result."));
                }

                var snapshot = SnapshotNative(handle, projectRoot);
                if (!snapshot.Succeeded)
                {
                    var closeHandle = handle;
                    _ = nativeApi_.Close(ref closeHandle);
                    return NativeOpenOutcome.Failed(snapshot.Failure!);
                }
                if (snapshot.Document!.Revision != result.Revision ||
                    snapshot.Document.SavedRevision != result.SavedRevision)
                {
                    CloseIfValid(ref handle);
                    return NativeOpenOutcome.Failed(InvalidNativeResponse(
                        "The scene open receipt does not match the authoritative snapshot."));
                }
                return NativeOpenOutcome.Success(handle, snapshot.Document!);
            }
        }
    }

    private void CloseIfValid(ref SceneNativeDocumentHandle handle)
    {
        if (handle.IsValid)
        {
            _ = nativeApi_.Close(ref handle);
        }
    }

    private unsafe NativeSnapshotOutcome SnapshotNative(
        SceneNativeDocumentHandle document,
        string projectRoot)
    {
        var request = SceneNativeDocumentRequest.Current(document);
        var response = new byte[InitialResponseCapacity];
        while (true)
        {
            SceneNativeStatus status;
            SceneNativeDocumentSnapshotResult result;
            fixed (byte* responsePointer = response)
            {
                status = nativeApi_.Snapshot(
                    in request,
                    (nint)responsePointer,
                    (ulong)response.Length,
                    out result);
            }
            if (status == SceneNativeStatus.BufferTooSmall)
            {
                if (!TryGrowResponse(response, result.RequiredByteLength, out response))
                {
                    return NativeSnapshotOutcome.Failed(InvalidNativeResponse(
                        "The scene adapter returned an invalid snapshot response size."));
                }
                continue;
            }
            if (!Enum.IsDefined(status) || result.OperationStatus != status ||
                result.RequiredByteLength > (ulong)response.Length)
            {
                return NativeSnapshotOutcome.Failed(InvalidNativeResponse(
                    "The scene adapter returned inconsistent snapshot metadata."));
            }
            try
            {
                if (status != SceneNativeStatus.Success)
                {
                    return NativeSnapshotOutcome.Failed(
                        Failure(
                            status,
                            Decode(response, result.MessageUtf8, result.RequiredByteLength)));
                }
                return NativeSnapshotOutcome.Success(ParseSnapshot(projectRoot, response, result));
            }
            catch (Exception exception) when (
                exception is ArgumentException
                    or DecoderFallbackException
                    or OverflowException)
            {
                return NativeSnapshotOutcome.Failed(InvalidNativeResponse(
                    $"The scene adapter returned an invalid snapshot: {exception.Message}"));
            }
        }
    }

    private static SceneDocumentSnapshot ParseSnapshot(
        string projectRoot,
        byte[] response,
        SceneNativeDocumentSnapshotResult result)
    {
        if (result.Revision == 0 || result.SavedRevision == 0 ||
            result.SavedRevision > result.Revision ||
            result.EntityCount > MaximumSceneEntityCount ||
            result.RequiredByteLength > (ulong)response.Length ||
            result.EntitiesOffset > result.RequiredByteLength ||
            result.MessageUtf8.ByteLength != 0)
        {
            throw new ArgumentOutOfRangeException(nameof(result));
        }
        var entriesLength = checked(
            result.EntityCount * (ulong)SceneNativeDocumentEntitySnapshot.StructSize);
        if (entriesLength > result.RequiredByteLength - result.EntitiesOffset)
        {
            throw new ArgumentOutOfRangeException(nameof(result));
        }

        var sceneIdText = Decode(response, result.SceneIdUtf8, result.RequiredByteLength);
        if (!Guid.TryParseExact(sceneIdText, "D", out var sceneId) || sceneId == Guid.Empty)
        {
            throw new ArgumentException("Scene id is not a canonical non-empty UUID.");
        }

        var entities = new List<SceneEntitySnapshot>(checked((int)result.EntityCount));
        var objectIds = new HashSet<Guid>();
        var runtimeEntityIds = new HashSet<EntityId>();
        for (ulong index = 0; index < result.EntityCount; index++)
        {
            var offset = checked(
                result.EntitiesOffset +
                index * (ulong)SceneNativeDocumentEntitySnapshot.StructSize);
            var entity = MemoryMarshal.Read<SceneNativeDocumentEntitySnapshot>(
                response.AsSpan(
                    checked((int)offset),
                    SceneNativeDocumentEntitySnapshot.StructSize));
            var objectIdText = Decode(response, entity.ObjectIdUtf8, result.RequiredByteLength);
            if (!Guid.TryParseExact(objectIdText, "D", out var objectId) ||
                objectId == Guid.Empty || !objectIds.Add(objectId))
            {
                throw new ArgumentException("Scene object id is invalid or duplicated.");
            }
            if (!entity.RuntimeEntityId.IsValid ||
                !runtimeEntityIds.Add(entity.RuntimeEntityId))
            {
                throw new ArgumentException(
                    "Scene runtime entity id is invalid or duplicated.");
            }
            SceneMeshReference? mesh = null;
            if (entity.MeshAssetGuidUtf8.ByteLength == 0)
            {
                if (entity.MeshAssetGuidUtf8.ByteOffset != 0)
                {
                    throw new ArgumentException(
                        "An absent mesh asset reference must use an empty native span.");
                }
            }
            else
            {
                var meshAssetGuidText = Decode(
                    response,
                    entity.MeshAssetGuidUtf8,
                    result.RequiredByteLength);
                if (!Guid.TryParseExact(meshAssetGuidText, "D", out var meshAssetGuid) ||
                    meshAssetGuid == Guid.Empty)
                {
                    throw new ArgumentException(
                        "Scene mesh asset reference is not a canonical non-empty UUID.");
                }
                mesh = new SceneMeshReference(meshAssetGuid);
            }
            ValidateTransform(entity.Transform);
            entities.Add(new SceneEntitySnapshot(
                objectId,
                entity.RuntimeEntityId,
                Decode(response, entity.NameUtf8, result.RequiredByteLength),
                entity.Transform,
                mesh));
        }

        return new SceneDocumentSnapshot(
            sceneId,
            Path.Combine(projectRoot, "Assets", "Scenes", "Default.asharia.scene.json"),
            result.Revision,
            result.SavedRevision,
            entities);
    }

    private static void ValidateTransform(TransformValue transform)
    {
        if (!float.IsFinite(transform.Position.X) ||
            !float.IsFinite(transform.Position.Y) ||
            !float.IsFinite(transform.Position.Z) ||
            !float.IsFinite(transform.Rotation.X) ||
            !float.IsFinite(transform.Rotation.Y) ||
            !float.IsFinite(transform.Rotation.Z) ||
            !float.IsFinite(transform.Rotation.W) ||
            !float.IsFinite(transform.Scale.X) ||
            !float.IsFinite(transform.Scale.Y) ||
            !float.IsFinite(transform.Scale.Z))
        {
            throw new ArgumentException("Scene Transform contains a non-finite value.");
        }
        var lengthSquared =
            transform.Rotation.X * transform.Rotation.X +
            transform.Rotation.Y * transform.Rotation.Y +
            transform.Rotation.Z * transform.Rotation.Z +
            transform.Rotation.W * transform.Rotation.W;
        if (Math.Abs(lengthSquared - 1.0f) > 1.0e-3f)
        {
            throw new ArgumentException("Scene Transform rotation is not unit length.");
        }
    }

    private unsafe NativeOperationOutcome InvokeOperation(NativeOperationCall call)
    {
        var response = new byte[InitialResponseCapacity];
        while (true)
        {
            SceneNativeStatus status;
            SceneNativeDocumentOperationResult result;
            fixed (byte* responsePointer = response)
            {
                status = call(
                    (nint)responsePointer,
                    (ulong)response.Length,
                    out result);
            }
            if (status == SceneNativeStatus.BufferTooSmall)
            {
                if (!TryGrowResponse(response, result.RequiredByteLength, out response))
                {
                    return NativeOperationOutcome.Failed(InvalidNativeResponse(
                        "The scene adapter returned an invalid operation response size."));
                }
                continue;
            }
            var validation = ValidateOperation(status, result, response);
            if (validation is not null)
            {
                return NativeOperationOutcome.Failed(
                    validation,
                    result.Revision,
                    result.SavedRevision);
            }
            var message = Decode(response, result.MessageUtf8, result.RequiredByteLength);
            return status == SceneNativeStatus.Success
                ? NativeOperationOutcome.Success(result.Revision, result.SavedRevision)
                : NativeOperationOutcome.Failed(
                    Failure(status, message),
                    result.Revision,
                    result.SavedRevision);
        }
    }

    private unsafe NativeTransformOperationOutcome InvokeTransformOperation(
        NativeTransformOperationCall call)
    {
        var response = new byte[InitialResponseCapacity];
        while (true)
        {
            SceneNativeStatus status;
            SceneNativeDocumentTransformOperationResult result;
            fixed (byte* responsePointer = response)
            {
                status = call(
                    (nint)responsePointer,
                    (ulong)response.Length,
                    out result);
            }
            if (status == SceneNativeStatus.BufferTooSmall)
            {
                if (!TryGrowResponse(response, result.RequiredByteLength, out response))
                {
                    return NativeTransformOperationOutcome.Failed(InvalidNativeResponse(
                        "The scene adapter returned an invalid Transform response size."));
                }
                continue;
            }

            var validation = ValidateTransformOperation(status, result, response);
            if (validation is not null)
            {
                return NativeTransformOperationOutcome.Failed(
                    validation,
                    result.Revision,
                    result.SavedRevision);
            }
            var message = Decode(response, result.MessageUtf8, result.RequiredByteLength);
            return status == SceneNativeStatus.Success
                ? NativeTransformOperationOutcome.Success(
                    result.Revision,
                    result.SavedRevision,
                    result)
                : NativeTransformOperationOutcome.Failed(
                    Failure(status, message),
                    result.Revision,
                    result.SavedRevision);
        }
    }

    private static SceneDocumentFailure? ValidateOperation(
        SceneNativeStatus status,
        SceneNativeDocumentOperationResult result,
        byte[] response)
    {
        if (!Enum.IsDefined(status) || result.OperationStatus != status ||
            result.RequiredByteLength > (ulong)response.Length)
        {
            return InvalidNativeResponse(
                "The scene adapter returned inconsistent operation metadata.");
        }
        if (status == SceneNativeStatus.Success &&
            (result.Revision == 0 || result.SavedRevision == 0 ||
             result.SavedRevision > result.Revision || result.MessageUtf8.ByteLength != 0))
        {
            return InvalidNativeResponse(
                "The scene adapter returned an incomplete operation success result.");
        }
        if (status != SceneNativeStatus.Success &&
            ((result.Revision == 0) != (result.SavedRevision == 0) ||
             result.SavedRevision > result.Revision))
        {
            return InvalidNativeResponse(
                "The scene adapter returned an invalid operation revision state.");
        }
        try
        {
            _ = Decode(response, result.MessageUtf8, result.RequiredByteLength);
        }
        catch (Exception exception) when (
            exception is ArgumentOutOfRangeException or DecoderFallbackException)
        {
            return InvalidNativeResponse(
                $"The scene adapter returned an invalid operation message: {exception.Message}");
        }
        return null;
    }

    private static SceneDocumentFailure? ValidateTransformOperation(
        SceneNativeStatus status,
        SceneNativeDocumentTransformOperationResult result,
        byte[] response)
    {
        if (!Enum.IsDefined(status) || result.OperationStatus != status ||
            result.RequiredByteLength > (ulong)response.Length)
        {
            return InvalidNativeResponse(
                "The scene adapter returned inconsistent Transform operation metadata.");
        }
        if (status == SceneNativeStatus.Success)
        {
            if (result.Changed > 1 || result.Revision == 0 || result.SavedRevision == 0 ||
                result.SavedRevision > result.Revision || result.MessageUtf8.ByteLength != 0 ||
                result.BeforeRevision == 0 || result.AfterRevision != result.Revision ||
                (result.Changed == 0
                    ? result.AfterRevision != result.BeforeRevision ||
                      result.BeforeTransform != result.AfterTransform
                    : result.BeforeRevision == ulong.MaxValue ||
                      result.AfterRevision != result.BeforeRevision + 1 ||
                      result.BeforeTransform == result.AfterTransform))
            {
                return InvalidNativeResponse(
                    "The scene adapter returned an invalid authoritative Transform receipt.");
            }
            try
            {
                ValidateTransform(result.BeforeTransform);
                ValidateTransform(result.AfterTransform);
                _ = DecodeNativeObjectId(result.ObjectId);
            }
            catch (ArgumentException exception)
            {
                return InvalidNativeResponse(
                    $"The scene adapter returned an invalid Transform receipt: {exception.Message}");
            }
        }
        else if ((result.Revision == 0) != (result.SavedRevision == 0) ||
                 result.SavedRevision > result.Revision || result.Changed != 0 ||
                 result.BeforeRevision != 0 || result.AfterRevision != 0 ||
                 result.ObjectId != default || result.BeforeTransform != default ||
                 result.AfterTransform != default)
        {
            return InvalidNativeResponse(
                "The scene adapter returned typed Transform receipt data for a failed operation.");
        }
        try
        {
            _ = Decode(response, result.MessageUtf8, result.RequiredByteLength);
        }
        catch (Exception exception) when (
            exception is ArgumentOutOfRangeException or DecoderFallbackException)
        {
            return InvalidNativeResponse(
                $"The scene adapter returned an invalid Transform message: {exception.Message}");
        }
        return null;
    }

    private static Guid DecodeNativeObjectId(SceneNativeObjectId value)
    {
        Span<byte> bytes = stackalloc byte[16];
        MemoryMarshal.Write(bytes, in value);
        var text = Convert.ToHexString(bytes);
        if (!Guid.TryParseExact(
                $"{text[..8]}-{text[8..12]}-{text[12..16]}-{text[16..20]}-{text[20..32]}",
                "D",
                out var objectId) || objectId == Guid.Empty)
        {
            throw new ArgumentException("Scene object id is an empty UUID.");
        }
        return objectId;
    }

    private static string Decode(
        byte[] response,
        SceneNativeTextSpan span,
        ulong logicalLength)
    {
        if (logicalLength > (ulong)response.Length ||
            span.ByteOffset > logicalLength ||
            span.ByteLength > logicalLength - span.ByteOffset)
        {
            throw new ArgumentOutOfRangeException(nameof(span));
        }
        return span.ByteLength == 0
            ? string.Empty
            : StrictUtf8.GetString(
                response,
                checked((int)span.ByteOffset),
                checked((int)span.ByteLength));
    }

    private static bool TryGrowResponse(
        byte[] current,
        ulong required,
        out byte[] grown)
    {
        if (required <= (ulong)current.Length || required > MaximumResponseCapacity)
        {
            grown = current;
            return false;
        }
        grown = new byte[checked((int)required)];
        return true;
    }

    private static SceneDocumentFailure Failure(SceneNativeStatus status, string message) =>
        new(
            MapFailure(status),
            string.IsNullOrWhiteSpace(message)
                ? $"Scene adapter failed with {status}."
                : message);

    private static SceneDocumentFailureKind MapFailure(SceneNativeStatus status) => status switch
    {
        SceneNativeStatus.InvalidArgument or SceneNativeStatus.InvalidUtf8 =>
            SceneDocumentFailureKind.InvalidInput,
        SceneNativeStatus.InvalidScene => SceneDocumentFailureKind.InvalidScene,
        SceneNativeStatus.RevisionConflict => SceneDocumentFailureKind.RevisionConflict,
        SceneNativeStatus.InvalidObject or SceneNativeStatus.DuplicateObject =>
            SceneDocumentFailureKind.InvalidObject,
        SceneNativeStatus.InvalidTransform => SceneDocumentFailureKind.InvalidTransform,
        SceneNativeStatus.InvalidAssetReference =>
            SceneDocumentFailureKind.InvalidAssetReference,
        SceneNativeStatus.RevisionExhausted => SceneDocumentFailureKind.RevisionExhausted,
        SceneNativeStatus.IoFailure => SceneDocumentFailureKind.IoFailure,
        SceneNativeStatus.UnsupportedAbi => SceneDocumentFailureKind.NativeUnavailable,
        _ => SceneDocumentFailureKind.InternalError,
    };

    private static SceneDocumentFailure InvalidNativeResponse(string message) =>
        new(SceneDocumentFailureKind.InternalError, message);

    private static SceneDocumentOpenResult FailedOpen(
        SceneDocumentFailureKind kind,
        string message) =>
        SceneDocumentOpenResult.Failed(new SceneDocumentFailure(kind, message));

    private static bool IsNativeBindingFailure(Exception exception) =>
        exception is DllNotFoundException
            or EntryPointNotFoundException
            or BadImageFormatException;

    private static string DiagnosticMessage(Exception exception, string fallback) =>
        string.IsNullOrWhiteSpace(exception.Message) ? fallback : exception.Message;

    private delegate SceneNativeStatus NativeOperationCall(
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentOperationResult result);

    private delegate SceneNativeStatus NativeTransformOperationCall(
        nint responseBuffer,
        ulong responseCapacity,
        out SceneNativeDocumentTransformOperationResult result);

    private sealed record NativeOpenOutcome(
        SceneNativeDocumentHandle Handle,
        SceneDocumentSnapshot? Document,
        SceneDocumentFailure? Failure)
    {
        public bool Succeeded => Document is not null;

        public static NativeOpenOutcome Success(
            SceneNativeDocumentHandle handle,
            SceneDocumentSnapshot document) =>
            new(handle, document, Failure: null);

        public static NativeOpenOutcome Failed(SceneDocumentFailure failure) =>
            new(default, Document: null, failure);
    }

    private sealed record NativeSnapshotOutcome(
        SceneDocumentSnapshot? Document,
        SceneDocumentFailure? Failure)
    {
        public bool Succeeded => Document is not null;

        public static NativeSnapshotOutcome Success(SceneDocumentSnapshot document) =>
            new(document, Failure: null);

        public static NativeSnapshotOutcome Failed(SceneDocumentFailure failure) =>
            new(Document: null, failure);
    }

    private sealed record NativeOperationOutcome(
        ulong Revision,
        ulong SavedRevision,
        SceneDocumentFailure? Failure)
    {
        public bool Succeeded => Failure is null;

        public static NativeOperationOutcome Success(ulong revision, ulong savedRevision) =>
            new(revision, savedRevision, Failure: null);

        public static NativeOperationOutcome Failed(
            SceneDocumentFailure failure,
            ulong revision = 0,
            ulong savedRevision = 0) =>
            new(revision, savedRevision, failure);
    }

    private sealed record NativeTransformOperationOutcome(
        ulong Revision,
        ulong SavedRevision,
        SceneNativeDocumentTransformOperationResult Receipt,
        SceneDocumentFailure? Failure)
    {
        public bool Succeeded => Failure is null;

        public static NativeTransformOperationOutcome Success(
            ulong revision,
            ulong savedRevision,
            SceneNativeDocumentTransformOperationResult receipt) =>
            new(revision, savedRevision, receipt, Failure: null);

        public static NativeTransformOperationOutcome Failed(
            SceneDocumentFailure failure,
            ulong revision = 0,
            ulong savedRevision = 0) =>
            new(revision, savedRevision, default, failure);
    }

    private sealed class SceneDocumentConnection : ISceneDocumentConnection
    {
        private readonly SceneDocumentBridge bridge_;
        private readonly SceneDocumentOwnerLane lane_;
        private readonly string projectRoot_;
        private SceneNativeDocumentHandle handle_;
        private SceneDocumentSnapshot current_;
        private int disposeStarted_;

        public SceneDocumentConnection(
            SceneDocumentBridge bridge,
            SceneDocumentOwnerLane lane,
            SceneNativeDocumentHandle handle,
            string projectRoot,
            SceneDocumentSnapshot current)
        {
            bridge_ = bridge;
            lane_ = lane;
            handle_ = handle;
            projectRoot_ = projectRoot;
            current_ = current;
        }

        public ValueTask<SceneDocumentOperationResult> CreateEntityAsync(
            Guid objectId,
            string name,
            ulong expectedRevision,
            CancellationToken cancellationToken = default)
        {
            ValidateObjectId(objectId);
            ArgumentNullException.ThrowIfNull(name);
            cancellationToken.ThrowIfCancellationRequested();
            ThrowIfDisposed();
            return new ValueTask<SceneDocumentOperationResult>(lane_.InvokeAsync(
                () => CreateEntityCore(objectId, name, expectedRevision)));
        }

        public ValueTask<SceneDocumentOperationResult> RefreshAsync(
            CancellationToken cancellationToken = default)
        {
            ThrowIfDisposed();
            cancellationToken.ThrowIfCancellationRequested();
            return new ValueTask<SceneDocumentOperationResult>(lane_.InvokeAsync(RefreshCore));
        }

        public ValueTask<SceneDocumentOperationResult> CreateMeshEntityAsync(
            Guid objectId,
            string name,
            SceneMeshReference mesh,
            ulong expectedRevision,
            CancellationToken cancellationToken = default)
        {
            ValidateObjectId(objectId);
            ArgumentNullException.ThrowIfNull(name);
            if (mesh.AssetId == Guid.Empty)
            {
                throw new ArgumentException(
                    "Mesh asset id must not be empty.",
                    nameof(mesh));
            }
            cancellationToken.ThrowIfCancellationRequested();
            ThrowIfDisposed();
            return new ValueTask<SceneDocumentOperationResult>(lane_.InvokeAsync(
                () => CreateMeshEntityCore(objectId, name, mesh, expectedRevision)));
        }

        public ValueTask<SceneDocumentOperationResult> SetEntityNameAsync(
            Guid objectId,
            string name,
            ulong expectedRevision,
            CancellationToken cancellationToken = default)
        {
            ValidateObjectId(objectId);
            ArgumentNullException.ThrowIfNull(name);
            cancellationToken.ThrowIfCancellationRequested();
            ThrowIfDisposed();
            return new ValueTask<SceneDocumentOperationResult>(lane_.InvokeAsync(
                () => SetEntityNameCore(objectId, name, expectedRevision)));
        }

        public ValueTask<SceneDocumentOperationResult> SetEntityTransformAsync(
            Guid objectId,
            TransformValue transform,
            ulong expectedRevision,
            CancellationToken cancellationToken = default)
        {
            ValidateObjectId(objectId);
            cancellationToken.ThrowIfCancellationRequested();
            ThrowIfDisposed();
            return new ValueTask<SceneDocumentOperationResult>(lane_.InvokeAsync(
                () => SetEntityTransformCore(objectId, transform, expectedRevision)));
        }

        public ValueTask<SceneDocumentOperationResult> SaveAsync(
            ulong expectedRevision,
            CancellationToken cancellationToken = default)
        {
            cancellationToken.ThrowIfCancellationRequested();
            ThrowIfDisposed();
            return new ValueTask<SceneDocumentOperationResult>(lane_.InvokeAsync(
                () => SaveCore(expectedRevision)));
        }

        public async ValueTask DisposeAsync()
        {
            if (Interlocked.Exchange(ref disposeStarted_, 1) != 0)
            {
                return;
            }
            try
            {
                await lane_.InvokeAsync(() =>
                {
                    var status = bridge_.nativeApi_.Close(ref handle_);
                    if (status != SceneNativeStatus.Success || handle_.IsValid)
                    {
                        throw new InvalidOperationException(
                            $"Native scene document close failed with {status}.");
                    }
                    return true;
                }).ConfigureAwait(false);
            }
            finally
            {
                lane_.Dispose();
            }
        }

        private unsafe SceneDocumentOperationResult CreateEntityCore(
            Guid objectId,
            string name,
            ulong expectedRevision)
        {
            try
            {
                var objectIdBytes = StrictUtf8.GetBytes(objectId.ToString("D"));
                var nameBytes = StrictUtf8.GetBytes(name);
                fixed (byte* id = objectIdBytes)
                fixed (byte* namePointer = nameBytes)
                {
                    var request = new SceneNativeDocumentCreateEntityRequest(
                        new SceneNativeAbiHeader(
                            SceneDocumentNativeAbi.Version,
                            SceneNativeDocumentCreateEntityRequest.StructSize),
                        handle_,
                        expectedRevision,
                        new SceneNativeStringView((nint)id, (ulong)objectIdBytes.Length),
                        new SceneNativeStringView((nint)namePointer, (ulong)nameBytes.Length));
                    return FinishEdit(bridge_.InvokeOperation(
                        (nint response, ulong capacity,
                         out SceneNativeDocumentOperationResult result) =>
                            bridge_.nativeApi_.CreateEntity(
                                in request,
                                response,
                                capacity,
                                out result)));
                }
            }
            catch (EncoderFallbackException exception)
            {
                return FailedCurrent(SceneDocumentFailureKind.InvalidInput, exception.Message);
            }
            catch (Exception exception) when (IsNativeBindingFailure(exception))
            {
                return NativeUnavailableCurrent(exception);
            }
        }

        private SceneDocumentOperationResult RefreshCore()
        {
            try
            {
                var snapshot = bridge_.SnapshotNative(handle_, projectRoot_);
                if (!snapshot.Succeeded)
                {
                    return SceneDocumentOperationResult.Failed(current_, snapshot.Failure!);
                }
                current_ = snapshot.Document!;
                return SceneDocumentOperationResult.Success(current_);
            }
            catch (Exception exception) when (IsNativeBindingFailure(exception))
            {
                return NativeUnavailableCurrent(exception);
            }
        }

        private unsafe SceneDocumentOperationResult CreateMeshEntityCore(
            Guid objectId,
            string name,
            SceneMeshReference mesh,
            ulong expectedRevision)
        {
            try
            {
                var objectIdBytes = StrictUtf8.GetBytes(objectId.ToString("D"));
                var nameBytes = StrictUtf8.GetBytes(name);
                var meshAssetGuidBytes = StrictUtf8.GetBytes(mesh.AssetId.ToString("D"));
                fixed (byte* id = objectIdBytes)
                fixed (byte* namePointer = nameBytes)
                fixed (byte* meshAssetGuidPointer = meshAssetGuidBytes)
                {
                    var request = new SceneNativeDocumentCreateMeshEntityRequest(
                        new SceneNativeAbiHeader(
                            SceneDocumentNativeAbi.Version,
                            SceneNativeDocumentCreateMeshEntityRequest.StructSize),
                        handle_,
                        expectedRevision,
                        new SceneNativeStringView((nint)id, (ulong)objectIdBytes.Length),
                        new SceneNativeStringView(
                            (nint)namePointer,
                            (ulong)nameBytes.Length),
                        new SceneNativeStringView(
                            (nint)meshAssetGuidPointer,
                            (ulong)meshAssetGuidBytes.Length));
                    return FinishEdit(bridge_.InvokeOperation(
                        (nint response, ulong capacity,
                         out SceneNativeDocumentOperationResult result) =>
                            bridge_.nativeApi_.CreateMeshEntity(
                                in request,
                                response,
                                capacity,
                                out result)));
                }
            }
            catch (EncoderFallbackException exception)
            {
                return FailedCurrent(SceneDocumentFailureKind.InvalidInput, exception.Message);
            }
            catch (Exception exception) when (IsNativeBindingFailure(exception))
            {
                return NativeUnavailableCurrent(exception);
            }
        }

        private unsafe SceneDocumentOperationResult SetEntityNameCore(
            Guid objectId,
            string name,
            ulong expectedRevision)
        {
            try
            {
                var objectIdBytes = StrictUtf8.GetBytes(objectId.ToString("D"));
                var nameBytes = StrictUtf8.GetBytes(name);
                fixed (byte* id = objectIdBytes)
                fixed (byte* namePointer = nameBytes)
                {
                    var request = new SceneNativeDocumentSetEntityNameRequest(
                        new SceneNativeAbiHeader(
                            SceneDocumentNativeAbi.Version,
                            SceneNativeDocumentSetEntityNameRequest.StructSize),
                        handle_,
                        expectedRevision,
                        new SceneNativeStringView((nint)id, (ulong)objectIdBytes.Length),
                        new SceneNativeStringView((nint)namePointer, (ulong)nameBytes.Length));
                    return FinishEdit(bridge_.InvokeOperation(
                        (nint response, ulong capacity,
                         out SceneNativeDocumentOperationResult result) =>
                            bridge_.nativeApi_.SetEntityName(
                                in request,
                                response,
                                capacity,
                                out result)));
                }
            }
            catch (EncoderFallbackException exception)
            {
                return FailedCurrent(SceneDocumentFailureKind.InvalidInput, exception.Message);
            }
            catch (Exception exception) when (IsNativeBindingFailure(exception))
            {
                return NativeUnavailableCurrent(exception);
            }
        }

        private unsafe SceneDocumentOperationResult SetEntityTransformCore(
            Guid objectId,
            TransformValue transform,
            ulong expectedRevision)
        {
            try
            {
                var objectIdBytes = StrictUtf8.GetBytes(objectId.ToString("D"));
                fixed (byte* id = objectIdBytes)
                {
                    var request = new SceneNativeDocumentSetEntityTransformRequest(
                        new SceneNativeAbiHeader(
                            SceneDocumentNativeAbi.Version,
                            SceneNativeDocumentSetEntityTransformRequest.StructSize),
                        handle_,
                        expectedRevision,
                        new SceneNativeStringView((nint)id, (ulong)objectIdBytes.Length),
                        transform);
                    return FinishTransformEdit(
                        bridge_.InvokeTransformOperation(
                            (nint response, ulong capacity,
                             out SceneNativeDocumentTransformOperationResult result) =>
                                bridge_.nativeApi_.SetEntityTransform(
                                    in request,
                                    response,
                                    capacity,
                                    out result)),
                        objectId,
                        transform,
                        expectedRevision);
                }
            }
            catch (Exception exception) when (IsNativeBindingFailure(exception))
            {
                return NativeUnavailableCurrent(exception);
            }
        }

        private SceneDocumentOperationResult SaveCore(ulong expectedRevision)
        {
            try
            {
                var request = new SceneNativeDocumentSaveRequest(
                    new SceneNativeAbiHeader(
                        SceneDocumentNativeAbi.Version,
                        SceneNativeDocumentSaveRequest.StructSize),
                    handle_,
                    expectedRevision);
                return FinishEdit(bridge_.InvokeOperation(
                    (nint response, ulong capacity,
                     out SceneNativeDocumentOperationResult result) =>
                        bridge_.nativeApi_.Save(in request, response, capacity, out result)));
            }
            catch (Exception exception) when (IsNativeBindingFailure(exception))
            {
                return NativeUnavailableCurrent(exception);
            }
        }

        private SceneDocumentOperationResult FinishTransformEdit(
            NativeTransformOperationOutcome operation,
            Guid requestedObjectId,
            TransformValue requestedTransform,
            ulong expectedRevision)
        {
            var previous = current_;
            NativeSnapshotOutcome snapshot;
            try
            {
                snapshot = bridge_.SnapshotNative(handle_, projectRoot_);
            }
            catch (Exception exception) when (IsNativeBindingFailure(exception))
            {
                return UnknownOperationOutcome(
                    $"The Transform operation completed, but its authoritative snapshot " +
                    $"could not be read: {exception.Message}");
            }
            if (!snapshot.Succeeded)
            {
                return UnknownOperationOutcome(
                    "The Transform operation completed, but its authoritative snapshot " +
                    $"could not be read: {snapshot.Failure!.Message}");
            }
            current_ = snapshot.Document!;
            if (operation.Revision != 0 &&
                (operation.Revision != current_.Revision ||
                 operation.SavedRevision != current_.SavedRevision))
            {
                return SceneDocumentOperationResult.Failed(
                    current_,
                    InvalidNativeResponse(
                        "The scene Transform receipt does not match the authoritative snapshot."));
            }
            if (!operation.Succeeded)
            {
                return SceneDocumentOperationResult.Failed(current_, operation.Failure!);
            }

            var receipt = operation.Receipt;
            var objectId = DecodeNativeObjectId(receipt.ObjectId);
            var previousEntity = previous.Entities.FirstOrDefault(
                candidate => candidate.ObjectId == objectId);
            var entity = current_.Entities.FirstOrDefault(candidate => candidate.ObjectId == objectId);
            if (objectId != requestedObjectId || receipt.BeforeRevision != expectedRevision ||
                previous.Revision != receipt.BeforeRevision ||
                receipt.AfterRevision != current_.Revision ||
                receipt.AfterTransform != requestedTransform ||
                previousEntity is null || previousEntity.Transform != receipt.BeforeTransform ||
                entity is null || entity.Transform != receipt.AfterTransform)
            {
                return SceneDocumentOperationResult.Failed(
                    current_,
                    InvalidNativeResponse(
                        "The authoritative Transform receipt does not match its scene entity."));
            }
            return SceneDocumentOperationResult.Success(
                current_,
                new SceneEntityTransformReceipt(
                    objectId,
                    receipt.Changed != 0,
                    receipt.BeforeTransform,
                    receipt.AfterTransform,
                    receipt.BeforeRevision,
                    receipt.AfterRevision));
        }

        private SceneDocumentOperationResult FinishEdit(NativeOperationOutcome operation)
        {
            NativeSnapshotOutcome snapshot;
            try
            {
                snapshot = bridge_.SnapshotNative(handle_, projectRoot_);
            }
            catch (Exception exception) when (IsNativeBindingFailure(exception))
            {
                return UnknownOperationOutcome(
                    $"The scene operation completed, but its authoritative snapshot " +
                    $"could not be read: {exception.Message}");
            }
            if (!snapshot.Succeeded)
            {
                return UnknownOperationOutcome(
                    "The scene operation completed, but its authoritative snapshot " +
                    $"could not be read: {snapshot.Failure!.Message}");
            }
            current_ = snapshot.Document!;
            if (operation.Revision != 0 &&
                (operation.Revision != current_.Revision ||
                 operation.SavedRevision != current_.SavedRevision))
            {
                return SceneDocumentOperationResult.Failed(
                    current_,
                    InvalidNativeResponse(
                        "The scene operation receipt does not match the authoritative snapshot."));
            }
            return operation.Succeeded
                ? SceneDocumentOperationResult.Success(current_)
                : SceneDocumentOperationResult.Failed(current_, operation.Failure!);
        }

        private SceneDocumentOperationResult FailedCurrent(
            SceneDocumentFailureKind kind,
            string message) =>
            SceneDocumentOperationResult.Failed(
                current_,
                new SceneDocumentFailure(kind, message));

        private SceneDocumentOperationResult NativeUnavailableCurrent(Exception exception) =>
            FailedCurrent(
                SceneDocumentFailureKind.NativeUnavailable,
                $"The canonical scene adapter is unavailable: {exception.Message}");

        private SceneDocumentOperationResult UnknownOperationOutcome(string message) =>
            FailedCurrent(SceneDocumentFailureKind.AuthoritativeStateUnknown, message);

        private void ThrowIfDisposed() =>
            ObjectDisposedException.ThrowIf(Volatile.Read(ref disposeStarted_) != 0, this);

        private static void ValidateObjectId(Guid objectId)
        {
            if (objectId == Guid.Empty)
            {
                throw new ArgumentException("Scene object id must not be empty.", nameof(objectId));
            }
        }
    }
}
