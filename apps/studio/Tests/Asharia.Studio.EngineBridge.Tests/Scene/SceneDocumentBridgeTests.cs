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
        Assert.True(saved.Succeeded);
        Assert.Equal("主角", moved.Current.Entities[0].Name);
        Assert.Equal(transform, moved.Current.Entities[0].Transform);
        Assert.False(saved.Current.IsDirty);
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
    public void Managed_layout_matches_the_native_document_v1_contract()
    {
        Assert.Equal(8, Marshal.SizeOf<SceneNativeDocumentHandle>());
        Assert.Equal(16, Marshal.SizeOf<SceneNativeTextSpan>());
        Assert.Equal(40, Marshal.SizeOf<SceneNativeDocumentOpenDefaultRequest>());
        Assert.Equal(16, Marshal.SizeOf<SceneNativeDocumentRequest>());
        Assert.Equal(56, Marshal.SizeOf<SceneNativeDocumentCreateEntityRequest>());
        Assert.Equal(56, Marshal.SizeOf<SceneNativeDocumentSetEntityNameRequest>());
        Assert.Equal(80, Marshal.SizeOf<SceneNativeDocumentSetEntityTransformRequest>());
        Assert.Equal(24, Marshal.SizeOf<SceneNativeDocumentSaveRequest>());
        Assert.Equal(48, Marshal.SizeOf<SceneNativeDocumentOperationResult>());
        Assert.Equal(72, Marshal.SizeOf<SceneNativeDocumentEntitySnapshot>());
        Assert.Equal(80, Marshal.SizeOf<SceneNativeDocumentSnapshotResult>());
        Assert.Equal(40, OffsetOf<SceneNativeDocumentSnapshotResult>(
            nameof(SceneNativeDocumentSnapshotResult.EntitiesOffset)));
        Assert.Equal(64, OffsetOf<SceneNativeDocumentSnapshotResult>(
            nameof(SceneNativeDocumentSnapshotResult.MessageUtf8)));
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
        private ulong revision_ = 1;
        private ulong savedRevision_ = 1;

        public Exception? OpenException { get; set; }

        public bool CorruptCreateReceipt { get; set; }

        public bool CorruptOpenReceipt { get; set; }

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
            var sceneIdBytes = Encoding.UTF8.GetBytes(SceneId.ToString("D"));
            var objectIdBytes = objectId_ == Guid.Empty
                ? []
                : Encoding.UTF8.GetBytes(objectId_.ToString("D"));
            var nameBytes = Encoding.UTF8.GetBytes(name_);
            var entityCount = objectId_ == Guid.Empty ? 0UL : 1UL;
            var required = checked(
                (int)(entityCount * SceneNativeDocumentEntitySnapshot.StructSize) +
                sceneIdBytes.Length + objectIdBytes.Length + nameBytes.Length);
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
                var entity = new SceneNativeDocumentEntitySnapshot(idSpan, nameSpan, transform_);
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
                revision_,
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
            out SceneNativeDocumentOperationResult result)
        {
            RecordThread();
            transform_ = request.Transform;
            revision_++;
            result = OperationResult(SceneNativeStatus.Success, revision_, savedRevision_);
            return SceneNativeStatus.Success;
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

        private static SceneNativeDocumentOperationResult OperationResult(
            SceneNativeStatus status,
            ulong revision,
            ulong savedRevision) =>
            new(status, Reserved: 0, RequiredByteLength: 0, revision, savedRevision, default);

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
