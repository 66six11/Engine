using System;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.EngineBridge.Scene;
using Asharia.Studio.EngineBridge.Scene.Abi;
using Xunit;

namespace Asharia.Studio.EngineBridge.Tests.Scene;

public sealed class SceneDocumentBridgeTests
{
    [Fact]
    public async Task Mesh_receipts_round_trip_add_replace_noop_and_remove_on_owner_lane()
    {
        var api = new StubSceneDocumentNativeApi();
        var opened = await new SceneDocumentBridge(api).OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());
        await using var connection = opened.Connection!;
        var id = Guid.NewGuid();
        await connection.CreateEntityAsync(id, "Entity", 1);
        SceneMeshReference? before = null;
        ulong revision = 2;
        var replacement = new SceneMeshReference(Guid.NewGuid());
        foreach (var target in new SceneMeshReference?[] { new(Guid.NewGuid()), replacement, replacement, null })
        {
            var result = await connection.SetEntityMeshAsync(id, target, revision);
            Assert.True(result.Succeeded, result.Failure?.Message);
            var receipt = Assert.IsType<SceneEntityMeshReceipt>(result.MeshReceipt);
            Assert.Equal(before, receipt.BeforeMesh);
            Assert.Equal(target, receipt.AfterMesh);
            Assert.Equal(before != target, receipt.Changed);
            Assert.Equal(revision + (before != target ? 1UL : 0UL), result.Current.Revision);
            Assert.Equal(target, result.Current.Entities[0].Mesh);
            before = target;
            revision = result.Current.Revision;
        }
        Assert.Single(api.CallThreadIds);
        Assert.Equal(4, api.MeshEditCalls);
        Assert.Equal(56, Marshal.SizeOf<SceneNativeDocumentSetEntityMeshRequest>());
        Assert.Equal(112, Marshal.SizeOf<SceneNativeDocumentMeshOperationResult>());
        Assert.Equal(48, OffsetOf<SceneNativeDocumentMeshOperationResult>(nameof(SceneNativeDocumentMeshOperationResult.BeforeMeshGuid)));
        Assert.Equal(96, OffsetOf<SceneNativeDocumentMeshOperationResult>(nameof(SceneNativeDocumentMeshOperationResult.MessageUtf8)));
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public async Task Mesh_bad_receipt_or_buffer_result_fails_without_replaying_mutation(bool buffer)
    {
        var api = new StubSceneDocumentNativeApi { AmbiguousMeshResult = buffer, CorruptMeshReceipt = !buffer };
        var opened = await new SceneDocumentBridge(api).OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());
        await using var connection = opened.Connection!;
        var id = Guid.NewGuid();
        await connection.CreateEntityAsync(id, "Entity", 1);
        var result = await connection.SetEntityMeshAsync(id, new SceneMeshReference(Guid.NewGuid()), 2);
        Assert.False(result.Succeeded);
        Assert.Null(result.MeshReceipt);
        Assert.Equal(1, api.MeshEditCalls);
    }

    [Fact]
    public async Task Mesh_empty_asset_id_is_rejected_before_native_call()
    {
        var api = new StubSceneDocumentNativeApi();
        var opened = await new SceneDocumentBridge(api).OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());
        await using var connection = opened.Connection!;
        await Assert.ThrowsAsync<ArgumentException>(() => connection.SetEntityMeshAsync(Guid.NewGuid(), default(SceneMeshReference), 1).AsTask());
        Assert.Equal(0, api.MeshEditCalls);
    }

    [Fact]
    public async Task Owner_lane_edits_snapshots_saves_and_closes_one_document()
    {
        var api = new StubSceneDocumentNativeApi();
        var bridge = new SceneDocumentBridge(api);
        var callerThread = Environment.CurrentManagedThreadId;

        var opened = await bridge.OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());
        var connection = Assert.IsAssignableFrom<ISceneDocumentConnection>(opened.Connection);
        var objectId = Guid.NewGuid();
        var created = await connection.CreateEntityAsync(objectId, "Entity", 1);
        var renamed = await connection.SetEntityNameAsync(objectId, "主角", 2);
        var transform = new TransformValue(
            new Float3(1, 2, 3),
            Quaternion.Identity,
            new Float3(2, 2, 2));
        var moved = await connection.SetEntityTransformAsync(objectId, transform, 3);
        var saved = await connection.SaveAsync(4);
        await connection.DisposeAsync();

        Assert.True(opened.Succeeded);
        Assert.Equal("C:\\Projects\\Sample", api.OpenedProjectRoot);
        Assert.True(created.Succeeded);
        Assert.True(renamed.Succeeded);
        Assert.True(moved.Succeeded);
        var transformReceipt = Assert.IsType<SceneEntityTransformReceipt>(moved.TransformReceipt);
        Assert.Equal(objectId, transformReceipt.ObjectId);
        Assert.True(transformReceipt.Changed);
        Assert.Equal(TransformValue.Identity, transformReceipt.BeforeTransform);
        Assert.Equal(transform, transformReceipt.AfterTransform);
        Assert.Equal(3UL, transformReceipt.BeforeRevision);
        Assert.Equal(4UL, transformReceipt.AfterRevision);
        Assert.True(saved.Succeeded);
        Assert.Equal("主角", moved.Current.Entities[0].Name);
        Assert.Equal(transform, moved.Current.Entities[0].Transform);
        Assert.Equal(saved.Current.Revision, saved.Current.SavedRevision);
        Assert.Equal(1, api.CloseCalls);
        Assert.NotEqual(callerThread, api.OwnerThreadId);
        Assert.Single(api.CallThreadIds);
        Assert.Contains(api.OwnerThreadId, api.CallThreadIds);
    }

    [Fact]
    public async Task Native_binding_failure_is_reported_without_leaking_a_connection()
    {
        var bridge = new SceneDocumentBridge(new StubSceneDocumentNativeApi
        {
            OpenException = new DllNotFoundException("missing scene adapter"),
        });

        var opened = await bridge.OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());

        Assert.False(opened.Succeeded);
        Assert.Null(opened.Connection);
        Assert.Equal(SceneDocumentFailureKind.NativeUnavailable, opened.Failure!.Kind);
        Assert.Contains("missing scene adapter", opened.Failure.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Operation_receipt_must_match_the_authoritative_snapshot()
    {
        var api = new StubSceneDocumentNativeApi { CorruptCreateReceipt = true };
        var bridge = new SceneDocumentBridge(api);
        var opened = await bridge.OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());
        var connection = Assert.IsAssignableFrom<ISceneDocumentConnection>(opened.Connection);

        var created = await connection.CreateEntityAsync(Guid.NewGuid(), "Entity", 1);
        await connection.DisposeAsync();

        Assert.False(created.Succeeded);
        Assert.Equal(SceneDocumentFailureKind.InternalError, created.Failure!.Kind);
        Assert.Contains("receipt", created.Failure.Message, StringComparison.Ordinal);
        Assert.Equal(2UL, created.Current.Revision);
        Assert.Single(created.Current.Entities);
    }

    [Fact]
    public async Task Transform_receipt_must_match_request_previous_state_and_snapshot()
    {
        var api = new StubSceneDocumentNativeApi { CorruptTransformReceipt = true };
        var bridge = new SceneDocumentBridge(api);
        var opened = await bridge.OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());
        var connection = Assert.IsAssignableFrom<ISceneDocumentConnection>(opened.Connection);
        var objectId = Guid.NewGuid();
        _ = await connection.CreateEntityAsync(objectId, "Entity", 1);
        var target = new TransformValue(
            new Float3(1, 2, 3),
            Quaternion.Identity,
            new Float3(2, 2, 2));

        var moved = await connection.SetEntityTransformAsync(objectId, target, 2);
        await connection.DisposeAsync();

        Assert.False(moved.Succeeded);
        Assert.Null(moved.TransformReceipt);
        Assert.Equal(SceneDocumentFailureKind.InternalError, moved.Failure!.Kind);
        Assert.Contains("receipt", moved.Failure.Message, StringComparison.OrdinalIgnoreCase);
        Assert.Equal(target, Assert.Single(moved.Current.Entities).Transform);
        Assert.Equal(3UL, moved.Current.Revision);
    }

    [Fact]
    public async Task Changed_transform_receipt_must_contain_distinct_values()
    {
        var api = new StubSceneDocumentNativeApi { EqualChangedTransformReceipt = true };
        var bridge = new SceneDocumentBridge(api);
        var opened = await bridge.OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());
        var connection = Assert.IsAssignableFrom<ISceneDocumentConnection>(opened.Connection);
        var objectId = Guid.NewGuid();
        _ = await connection.CreateEntityAsync(objectId, "Entity", 1);

        var moved = await connection.SetEntityTransformAsync(
            objectId,
            new TransformValue(new Float3(1, 2, 3), Quaternion.Identity, Float3.One),
            2);
        await connection.DisposeAsync();

        Assert.False(moved.Succeeded);
        Assert.Equal(SceneDocumentFailureKind.InternalError, moved.Failure!.Kind);
        Assert.Contains("receipt", moved.Failure.Message, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public async Task Transform_snapshot_failure_reports_an_unknown_authoritative_outcome()
    {
        var api = new StubSceneDocumentNativeApi();
        var bridge = new SceneDocumentBridge(api);
        var opened = await bridge.OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());
        var connection = Assert.IsAssignableFrom<ISceneDocumentConnection>(opened.Connection);
        var objectId = Guid.NewGuid();
        _ = await connection.CreateEntityAsync(objectId, "Entity", 1);
        api.RejectNextSnapshot = true;

        var moved = await connection.SetEntityTransformAsync(
            objectId,
            new TransformValue(new Float3(1, 2, 3), Quaternion.Identity, Float3.One),
            2);
        var refreshed = await connection.RefreshAsync();
        await connection.DisposeAsync();

        Assert.False(moved.Succeeded);
        Assert.Equal(
            SceneDocumentFailureKind.AuthoritativeStateUnknown,
            moved.Failure!.Kind);
        Assert.Equal(2UL, moved.Current.Revision);
        Assert.True(refreshed.Succeeded);
        Assert.Equal(3UL, refreshed.Current.Revision);
        Assert.Equal(
            new Float3(1, 2, 3),
            Assert.Single(refreshed.Current.Entities).Transform.Position);
    }

    [Fact]
    public async Task No_op_Transform_returns_stable_authoritative_receipt()
    {
        var api = new StubSceneDocumentNativeApi();
        var bridge = new SceneDocumentBridge(api);
        var opened = await bridge.OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());
        var connection = Assert.IsAssignableFrom<ISceneDocumentConnection>(opened.Connection);
        var objectId = Guid.NewGuid();
        _ = await connection.CreateEntityAsync(objectId, "Entity", 1);

        var unchanged = await connection.SetEntityTransformAsync(
            objectId,
            TransformValue.Identity,
            2);
        await connection.DisposeAsync();

        Assert.True(unchanged.Succeeded);
        var receipt = Assert.IsType<SceneEntityTransformReceipt>(unchanged.TransformReceipt);
        Assert.False(receipt.Changed);
        Assert.Equal(TransformValue.Identity, receipt.BeforeTransform);
        Assert.Equal(receipt.BeforeTransform, receipt.AfterTransform);
        Assert.Equal(2UL, receipt.BeforeRevision);
        Assert.Equal(receipt.BeforeRevision, receipt.AfterRevision);
        Assert.Equal(2UL, unchanged.Current.Revision);
    }

    [Fact]
    public async Task Refresh_publishes_an_authoritative_external_snapshot()
    {
        var api = new StubSceneDocumentNativeApi();
        var bridge = new SceneDocumentBridge(api);
        var opened = await bridge.OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());
        var connection = Assert.IsAssignableFrom<ISceneDocumentConnection>(opened.Connection);
        var objectId = Guid.NewGuid();
        api.AdvanceExternalSnapshot(objectId, "External");

        var refreshed = await connection.RefreshAsync();
        await connection.DisposeAsync();

        Assert.True(refreshed.Succeeded);
        Assert.Null(refreshed.TransformReceipt);
        Assert.Equal(2UL, refreshed.Current.Revision);
        var entity = Assert.Single(refreshed.Current.Entities);
        Assert.Equal(objectId, entity.ObjectId);
        Assert.Equal("External", entity.Name);
    }

    [Fact]
    public async Task Failed_refresh_preserves_the_last_known_snapshot()
    {
        var api = new StubSceneDocumentNativeApi();
        var bridge = new SceneDocumentBridge(api);
        var opened = await bridge.OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());
        var connection = Assert.IsAssignableFrom<ISceneDocumentConnection>(opened.Connection);
        api.RejectSnapshot = true;

        var refreshed = await connection.RefreshAsync();
        await connection.DisposeAsync();

        Assert.False(refreshed.Succeeded);
        Assert.Equal(SceneDocumentFailureKind.InternalError, refreshed.Failure!.Kind);
        Assert.Equal(opened.Document, refreshed.Current);
    }

    [Fact]
    public async Task Malformed_refresh_preserves_the_last_known_snapshot()
    {
        var api = new StubSceneDocumentNativeApi();
        var bridge = new SceneDocumentBridge(api);
        var opened = await bridge.OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());
        var connection = Assert.IsAssignableFrom<ISceneDocumentConnection>(opened.Connection);
        api.CorruptSnapshotRevision = true;

        var refreshed = await connection.RefreshAsync();
        await connection.DisposeAsync();

        Assert.False(refreshed.Succeeded);
        Assert.Equal(SceneDocumentFailureKind.InternalError, refreshed.Failure!.Kind);
        Assert.Equal(opened.Document, refreshed.Current);
    }

    [Fact]
    public async Task Typed_mesh_create_round_trips_runtime_entity_and_asset_reference()
    {
        var api = new StubSceneDocumentNativeApi();
        var bridge = new SceneDocumentBridge(api);
        var opened = await bridge.OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());
        var connection = Assert.IsAssignableFrom<ISceneDocumentConnection>(opened.Connection);
        var objectId = Guid.NewGuid();
        var mesh = SceneMeshReference.DirectionalWedgeValidation;

        var created = await connection.CreateMeshEntityAsync(objectId, "Mesh", mesh, 1);
        await connection.DisposeAsync();

        Assert.True(created.Succeeded);
        var entity = Assert.Single(created.Current.Entities);
        Assert.Equal(objectId, entity.ObjectId);
        Assert.Equal(new EntityId(1, 1), entity.RuntimeEntityId);
        Assert.Equal(mesh, entity.Mesh);
        Assert.Equal(SceneDocumentNativeAbi.Version, api.LastCreateMeshAbiVersion);
    }

    [Fact]
    public async Task Invalid_mesh_reference_failure_is_typed_and_does_not_advance_revision()
    {
        var api = new StubSceneDocumentNativeApi { RejectMeshCreate = true };
        var bridge = new SceneDocumentBridge(api);
        var opened = await bridge.OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());
        var connection = Assert.IsAssignableFrom<ISceneDocumentConnection>(opened.Connection);

        var created = await connection.CreateMeshEntityAsync(
            Guid.NewGuid(),
            "Mesh",
            SceneMeshReference.DirectionalWedgeValidation,
            1);
        await connection.DisposeAsync();

        Assert.False(created.Succeeded);
        Assert.Equal(SceneDocumentFailureKind.InvalidAssetReference, created.Failure!.Kind);
        Assert.Equal(1UL, created.Current.Revision);
        Assert.Empty(created.Current.Entities);
    }

    [Fact]
    public async Task Open_receipt_must_match_the_snapshot_and_closes_the_native_handle()
    {
        var api = new StubSceneDocumentNativeApi { CorruptOpenReceipt = true };
        var bridge = new SceneDocumentBridge(api);

        var opened = await bridge.OpenDefaultAsync("C:\\Projects\\Sample", Guid.NewGuid());

        Assert.False(opened.Succeeded);
        Assert.Null(opened.Connection);
        Assert.Equal(SceneDocumentFailureKind.InternalError, opened.Failure!.Kind);
        Assert.Contains("receipt", opened.Failure.Message, StringComparison.Ordinal);
        Assert.Equal(1, api.CloseCalls);
    }

    [Fact]
    public void Managed_layout_matches_the_native_document_v3_contract()
    {
        Assert.Equal(8, Marshal.SizeOf<SceneNativeDocumentHandle>());
        Assert.Equal(16, Marshal.SizeOf<SceneNativeTextSpan>());
        Assert.Equal(40, Marshal.SizeOf<SceneNativeDocumentOpenDefaultRequest>());
        Assert.Equal(16, Marshal.SizeOf<SceneNativeDocumentRequest>());
        Assert.Equal(56, Marshal.SizeOf<SceneNativeDocumentCreateEntityRequest>());
        Assert.Equal(72, Marshal.SizeOf<SceneNativeDocumentCreateMeshEntityRequest>());
        Assert.Equal(56, Marshal.SizeOf<SceneNativeDocumentSetEntityNameRequest>());
        Assert.Equal(80, Marshal.SizeOf<SceneNativeDocumentSetEntityTransformRequest>());
        Assert.Equal(24, Marshal.SizeOf<SceneNativeDocumentSaveRequest>());
        Assert.Equal(48, Marshal.SizeOf<SceneNativeDocumentOperationResult>());
        Assert.Equal(16, Marshal.SizeOf<SceneNativeObjectId>());
        Assert.Equal(160, Marshal.SizeOf<SceneNativeDocumentTransformOperationResult>());
        Assert.Equal(96, Marshal.SizeOf<SceneNativeDocumentEntitySnapshot>());
        Assert.Equal(80, Marshal.SizeOf<SceneNativeDocumentSnapshotResult>());
        Assert.Equal(40, OffsetOf<SceneNativeDocumentSetEntityTransformRequest>(
            nameof(SceneNativeDocumentSetEntityTransformRequest.Transform)));
        Assert.Equal(32, OffsetOf<SceneNativeDocumentTransformOperationResult>(
            nameof(SceneNativeDocumentTransformOperationResult.ObjectId)));
        Assert.Equal(48, OffsetOf<SceneNativeDocumentTransformOperationResult>(
            nameof(SceneNativeDocumentTransformOperationResult.BeforeTransform)));
        Assert.Equal(88, OffsetOf<SceneNativeDocumentTransformOperationResult>(
            nameof(SceneNativeDocumentTransformOperationResult.AfterTransform)));
        Assert.Equal(128, OffsetOf<SceneNativeDocumentTransformOperationResult>(
            nameof(SceneNativeDocumentTransformOperationResult.BeforeRevision)));
        Assert.Equal(136, OffsetOf<SceneNativeDocumentTransformOperationResult>(
            nameof(SceneNativeDocumentTransformOperationResult.AfterRevision)));
        Assert.Equal(32, OffsetOf<SceneNativeDocumentEntitySnapshot>(
            nameof(SceneNativeDocumentEntitySnapshot.Transform)));
        Assert.Equal(40, OffsetOf<SceneNativeDocumentSnapshotResult>(
            nameof(SceneNativeDocumentSnapshotResult.EntitiesOffset)));
        Assert.Equal(64, OffsetOf<SceneNativeDocumentSnapshotResult>(
            nameof(SceneNativeDocumentSnapshotResult.MessageUtf8)));
        Assert.Equal(72, OffsetOf<SceneNativeDocumentEntitySnapshot>(
            nameof(SceneNativeDocumentEntitySnapshot.RuntimeEntityId)));
        Assert.Equal(80, OffsetOf<SceneNativeDocumentEntitySnapshot>(
            nameof(SceneNativeDocumentEntitySnapshot.MeshAssetGuidUtf8)));
        Assert.Equal(144, OffsetOf<SceneNativeDocumentTransformOperationResult>(
            nameof(SceneNativeDocumentTransformOperationResult.MessageUtf8)));
    }

    private static int OffsetOf<T>(string propertyName)
    {
        var field = typeof(T).GetField(
            $"<{propertyName}>k__BackingField",
            BindingFlags.Instance | BindingFlags.NonPublic)
            ?? throw new InvalidOperationException(
                $"ABI backing field for '{propertyName}' is missing.");
        return Marshal.OffsetOf<T>(field.Name).ToInt32();
    }

    private sealed class StubSceneDocumentNativeApi : ISceneDocumentNativeApi
    {
        private static readonly Guid SceneId =
            Guid.Parse("11111111-2222-3333-4444-555555555555");
        private SceneNativeDocumentHandle handle_ = new(1, 1);
        private Guid objectId_;
        private string name_ = string.Empty;
        private TransformValue transform_ = TransformValue.Identity;
        private Guid? meshAssetId_;
        private ulong revision_ = 1;
        private ulong savedRevision_ = 1;

        public Exception? OpenException { get; set; }

        public bool CorruptCreateReceipt { get; set; }

        public bool CorruptOpenReceipt { get; set; }

        public bool CorruptTransformReceipt { get; set; }

        public bool EqualChangedTransformReceipt { get; set; }

        public bool RejectMeshCreate { get; set; }

        public bool RejectSnapshot { get; set; }

        public bool RejectNextSnapshot { get; set; }

        public bool CorruptSnapshotRevision { get; set; }

        public uint LastCreateMeshAbiVersion { get; private set; }

        public string OpenedProjectRoot { get; private set; } = string.Empty;

        public int OwnerThreadId { get; private set; }

        public System.Collections.Generic.HashSet<int> CallThreadIds { get; } = [];

        public int CloseCalls { get; private set; }

        public SceneNativeStatus OpenDefault(
            in SceneNativeDocumentOpenDefaultRequest request,
            out SceneNativeDocumentHandle document,
            nint responseBuffer,
            ulong responseCapacity,
            out SceneNativeDocumentOperationResult result)
        {
            if (OpenException is not null)
            {
                throw OpenException;
            }
            OwnerThreadId = Environment.CurrentManagedThreadId;
            RecordThread();
            OpenedProjectRoot = Read(request.ProjectRootUtf8);
            document = handle_;
            result = OperationResult(
                SceneNativeStatus.Success,
                CorruptOpenReceipt ? revision_ + 1 : revision_,
                savedRevision_);
            return SceneNativeStatus.Success;
        }

        public SceneNativeStatus Close(ref SceneNativeDocumentHandle document)
        {
            RecordThread();
            CloseCalls++;
            document = default;
            handle_ = default;
            return SceneNativeStatus.Success;
        }

        public SceneNativeStatus Snapshot(
            in SceneNativeDocumentRequest request,
            nint responseBuffer,
            ulong responseCapacity,
            out SceneNativeDocumentSnapshotResult result)
        {
            RecordThread();
            if (RejectSnapshot || RejectNextSnapshot)
            {
                RejectNextSnapshot = false;
                result = new SceneNativeDocumentSnapshotResult(
                    SceneNativeStatus.InternalError,
                    Reserved: 0,
                    RequiredByteLength: 0,
                    revision_,
                    savedRevision_,
                    EntityCount: 0,
                    EntitiesOffset: 0,
                    default,
                    default);
                return SceneNativeStatus.InternalError;
            }
            var sceneIdBytes = Encoding.UTF8.GetBytes(SceneId.ToString("D"));
            var objectIdBytes = objectId_ == Guid.Empty
                ? []
                : Encoding.UTF8.GetBytes(objectId_.ToString("D"));
            var nameBytes = Encoding.UTF8.GetBytes(name_);
            var meshAssetGuidBytes = meshAssetId_ is Guid meshAssetId
                ? Encoding.UTF8.GetBytes(meshAssetId.ToString("D"))
                : [];
            var entityCount = objectId_ == Guid.Empty ? 0UL : 1UL;
            var required = checked(
                (int)(entityCount * SceneNativeDocumentEntitySnapshot.StructSize) +
                sceneIdBytes.Length + objectIdBytes.Length + nameBytes.Length +
                meshAssetGuidBytes.Length);
            if ((ulong)required > responseCapacity)
            {
                result = new SceneNativeDocumentSnapshotResult(
                    SceneNativeStatus.Success,
                    Reserved: 0,
                    (ulong)required,
                    revision_,
                    savedRevision_,
                    entityCount,
                    EntitiesOffset: 0,
                    default,
                    default);
                return SceneNativeStatus.BufferTooSmall;
            }

            var bytes = new byte[required];
            var cursor = checked((int)(entityCount * SceneNativeDocumentEntitySnapshot.StructSize));
            var sceneSpan = Append(bytes, ref cursor, sceneIdBytes);
            if (entityCount != 0)
            {
                var idSpan = Append(bytes, ref cursor, objectIdBytes);
                var nameSpan = Append(bytes, ref cursor, nameBytes);
                var meshAssetGuidSpan = meshAssetGuidBytes.Length == 0
                    ? default
                    : Append(bytes, ref cursor, meshAssetGuidBytes);
                var entity = new SceneNativeDocumentEntitySnapshot(
                    idSpan,
                    nameSpan,
                    transform_,
                    new EntityId(1, 1),
                    meshAssetGuidSpan);
                MemoryMarshal.Write(bytes.AsSpan(0, SceneNativeDocumentEntitySnapshot.StructSize),
                    in entity);
            }
            if (bytes.Length != 0)
            {
                Marshal.Copy(bytes, 0, responseBuffer, bytes.Length);
            }
            result = new SceneNativeDocumentSnapshotResult(
                SceneNativeStatus.Success,
                Reserved: 0,
                (ulong)bytes.Length,
                CorruptSnapshotRevision ? 0UL : revision_,
                savedRevision_,
                entityCount,
                EntitiesOffset: 0,
                sceneSpan,
                default);
            return SceneNativeStatus.Success;
        }

        public SceneNativeStatus CreateEntity(
            in SceneNativeDocumentCreateEntityRequest request,
            nint responseBuffer,
            ulong responseCapacity,
            out SceneNativeDocumentOperationResult result)
        {
            RecordThread();
            objectId_ = Guid.ParseExact(Read(request.ObjectIdUtf8), "D");
            name_ = Read(request.NameUtf8);
            revision_++;
            result = OperationResult(
                SceneNativeStatus.Success,
                CorruptCreateReceipt ? revision_ + 1 : revision_,
                savedRevision_);
            return SceneNativeStatus.Success;
        }

        public SceneNativeStatus CreateMeshEntity(
            in SceneNativeDocumentCreateMeshEntityRequest request,
            nint responseBuffer,
            ulong responseCapacity,
            out SceneNativeDocumentOperationResult result)
        {
            RecordThread();
            LastCreateMeshAbiVersion = request.Header.AbiVersion;
            if (RejectMeshCreate)
            {
                result = OperationResult(
                    SceneNativeStatus.InvalidAssetReference,
                    revision_,
                    savedRevision_);
                return SceneNativeStatus.InvalidAssetReference;
            }
            objectId_ = Guid.ParseExact(Read(request.ObjectIdUtf8), "D");
            name_ = Read(request.NameUtf8);
            meshAssetId_ = Guid.ParseExact(Read(request.MeshAssetGuidUtf8), "D");
            revision_++;
            result = OperationResult(SceneNativeStatus.Success, revision_, savedRevision_);
            return SceneNativeStatus.Success;
        }

        public SceneNativeStatus SetEntityName(
            in SceneNativeDocumentSetEntityNameRequest request,
            nint responseBuffer,
            ulong responseCapacity,
            out SceneNativeDocumentOperationResult result)
        {
            RecordThread();
            name_ = Read(request.NameUtf8);
            revision_++;
            result = OperationResult(SceneNativeStatus.Success, revision_, savedRevision_);
            return SceneNativeStatus.Success;
        }

        public SceneNativeStatus SetEntityTransform(
            in SceneNativeDocumentSetEntityTransformRequest request,
            nint responseBuffer,
            ulong responseCapacity,
            out SceneNativeDocumentTransformOperationResult result)
        {
            RecordThread();
            var before = transform_;
            var beforeRevision = revision_;
            var changed = before != request.Transform;
            transform_ = request.Transform;
            if (changed)
            {
                revision_++;
            }
            result = new SceneNativeDocumentTransformOperationResult(
                SceneNativeStatus.Success,
                changed ? 1U : 0U,
                RequiredByteLength: 0,
                revision_,
                savedRevision_,
                EncodeObjectId(objectId_),
                CorruptTransformReceipt
                    ? new TransformValue(new Float3(9, 9, 9), Quaternion.Identity, Float3.One)
                    : EqualChangedTransformReceipt ? request.Transform : before,
                transform_,
                beforeRevision,
                revision_,
                default);
            return SceneNativeStatus.Success;
        }

        public int MeshEditCalls { get; private set; }
        public bool AmbiguousMeshResult { get; set; }
        public bool CorruptMeshReceipt { get; set; }

        public SceneNativeStatus SetEntityMesh(
            in SceneNativeDocumentSetEntityMeshRequest request,
            nint responseBuffer, ulong responseCapacity,
            out SceneNativeDocumentMeshOperationResult result)
        {
            RecordThread();
            MeshEditCalls++;
            var text = Read(request.MeshAssetGuidUtf8);
            Guid? target = text.Length == 0 ? null : Guid.Parse(text);
            var before = meshAssetId_;
            var beforeRevision = revision_;
            var changed = before != target;
            meshAssetId_ = target;
            if (changed) revision_++;
            result = new SceneNativeDocumentMeshOperationResult(
                SceneNativeStatus.Success, changed ? 1U : 0U, 0,
                revision_, savedRevision_, EncodeObjectId(objectId_),
                EncodeObjectId(CorruptMeshReceipt ? Guid.NewGuid() : before ?? Guid.Empty),
                EncodeObjectId(target ?? Guid.Empty), beforeRevision, revision_, default);
            return AmbiguousMeshResult ? SceneNativeStatus.BufferTooSmall : SceneNativeStatus.Success;
        }

        public SceneNativeStatus Save(
            in SceneNativeDocumentSaveRequest request,
            nint responseBuffer,
            ulong responseCapacity,
            out SceneNativeDocumentOperationResult result)
        {
            RecordThread();
            savedRevision_ = revision_;
            result = OperationResult(SceneNativeStatus.Success, revision_, savedRevision_);
            return SceneNativeStatus.Success;
        }

        private void RecordThread()
        {
            var thread = Environment.CurrentManagedThreadId;
            if (OwnerThreadId != thread)
            {
                throw new InvalidOperationException("Native document call left its owner lane.");
            }
            CallThreadIds.Add(thread);
        }

        public void AdvanceExternalSnapshot(Guid objectId, string name)
        {
            objectId_ = objectId;
            name_ = name;
            revision_++;
        }

        private static SceneNativeDocumentOperationResult OperationResult(
            SceneNativeStatus status,
            ulong revision,
            ulong savedRevision) =>
            new(status, Reserved: 0, RequiredByteLength: 0, revision, savedRevision, default);

        private static SceneNativeObjectId EncodeObjectId(Guid objectId)
        {
            var bytes = Convert.FromHexString(objectId.ToString("N"));
            return MemoryMarshal.Read<SceneNativeObjectId>(bytes);
        }

        private static SceneNativeTextSpan Append(byte[] target, ref int cursor, byte[] value)
        {
            var span = new SceneNativeTextSpan((ulong)cursor, (ulong)value.Length);
            value.CopyTo(target, cursor);
            cursor += value.Length;
            return span;
        }

        private static string Read(SceneNativeStringView value)
        {
            var bytes = new byte[checked((int)value.ByteLength)];
            if (bytes.Length != 0)
            {
                Marshal.Copy(value.Data, bytes, 0, bytes.Length);
            }
            return Encoding.UTF8.GetString(bytes);
        }
    }
}
