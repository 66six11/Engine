using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Runtime;
using Asharia.Studio.Application.Scenes;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.EngineBridge.Viewports;
using Asharia.Studio.EngineBridge.Viewports.Abi;
using Xunit;

namespace Asharia.Studio.EngineBridge.Tests.Viewports;

public sealed class ViewportBridgeTests
{
    [Fact]
    public async Task Waiter_releases_gate_for_close_and_must_drain_before_destroy()
    {
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        var api = new StubViewportNativeApi
        {
            WaitHandler = () =>
            {
                entered.Set();
                Assert.True(release.Wait(TimeSpan.FromSeconds(5)));
                return ViewportNativeStatus.Success;
            },
        };
        var stream = new ViewportBridge(api).OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt).Stream!;
        var wait = stream.WaitForChangeAsync(0);
        try
        {
            Assert.True(entered.Wait(TimeSpan.FromSeconds(5)));
            await Assert.ThrowsAsync<InvalidOperationException>(() => stream.WaitForChangeAsync(0));
            stream.RequestClose();
            stream.RequestClose();
            Assert.Equal(1, api.CloseCalls);
            Assert.Throws<InvalidOperationException>(stream.DestroyClosed);
            Assert.Equal(0, api.DestroyCalls);
        }
        finally { release.Set(); }
        Assert.Null(await wait);
        await stream.DrainWaiterAsync();
        stream.DestroyClosed();
        stream.DestroyClosed();
        Assert.Equal(1, api.DestroyCalls);
        await Assert.ThrowsAsync<ObjectDisposedException>(() => stream.WaitForChangeAsync(0));
    }

    [Fact]
    public async Task Cancellation_keeps_waiter_pinned_until_native_returns()
    {
        using var entered = new ManualResetEventSlim();
        using var release = new ManualResetEventSlim();
        using var cancellation = new CancellationTokenSource();
        var api = new StubViewportNativeApi
        {
            WaitHandler = () =>
            {
                entered.Set();
                Assert.True(release.Wait(TimeSpan.FromSeconds(5)));
                return ViewportNativeStatus.Success;
            },
        };
        var stream = new ViewportBridge(api).OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt).Stream!;
        var wait = stream.WaitForChangeAsync(0, cancellation.Token);
        try
        {
            Assert.True(entered.Wait(TimeSpan.FromSeconds(5)));
            cancellation.Cancel();
            stream.RequestClose();
            Assert.False(wait.IsCompleted);
            Assert.Throws<InvalidOperationException>(stream.DestroyClosed);
        }
        finally { release.Set(); }
        await Assert.ThrowsAnyAsync<OperationCanceledException>(() => wait);
        await stream.DrainWaiterAsync();
        stream.DestroyClosed();
    }

    [Fact]
    public async Task Native_wait_failure_preserves_typed_error()
    {
        var api = new StubViewportNativeApi { WaitHandler = () => ViewportNativeStatus.DeviceLost };
        var stream = new ViewportBridge(api).OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt).Stream!;
        Assert.Equal(ViewportFrameFailureKind.NativeUnavailable, (await stream.WaitForChangeAsync(0))!.Kind);
        stream.RequestClose();
        stream.DestroyClosed();
    }

    [Theory]
    [InlineData(2U, 3U, true)]
    [InlineData(2U, 1U, false)]
    [InlineData(0U, 1U, false)]
    [InlineData(0U, 0U, true)]
    public void Scene_receipt_counts_sections_independently_of_instances(
        uint instances, uint draws, bool accepted)
    {
        var hasInstances = instances != 0;
        var api = new StubViewportNativeApi
        {
            SceneReceipt = new ViewportNativeSceneMeshReceiptV11(
                instances, instances, 0, draws, (uint)ViewportNativeSceneRasterMode.Solid,
                hasInstances ? 1U : 0U, hasInstances ? 1U : 0U, 1,
                hasInstances ? ViewportNativeCanonicalUuid.FromGuid(Guid.NewGuid()) : default,
                hasInstances ? ViewportNativeCanonicalUuid.FromGuid(Guid.NewGuid()) : default,
                hasInstances ? 1UL : 0UL, hasInstances ? 1UL : 0UL, hasInstances ? 1UL : 0UL, 8),
        };
        var stream = new ViewportBridge(api)
            .OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt).Stream!;
        Assert.True(stream.SubmitLatest(PublishRequest(ViewportRenderKind.Scene, revision: 8)).Succeeded);
        var result = stream.TryTakeReady();
        Assert.Equal(accepted, result.Succeeded);
        result.Lease?.Dispose();
        stream.Dispose();
    }

    [Fact]
    public void V11_stream_maps_latest_request_and_returns_a_bound_frame()
    {
        var api = new StubViewportNativeApi();
        var bridge = new ViewportBridge(api);
        var opened = bridge.OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt);
        var stream = Assert.IsType<ViewportRenderStream>(opened.Stream);
        var request = PublishRequest(ViewportRenderKind.Preview, revision: 8);

        Assert.True(stream.SubmitLatest(request).Succeeded);
        var taken = stream.TryTakeReady();

        Assert.True(taken.Succeeded);
        var lease = Assert.IsType<ViewportFrameLease>(taken.Lease);
        Assert.Equal(request.SessionId, lease.SessionId);
        Assert.Equal(request.Sequence, lease.RequestSequence);
        Assert.Equal(request.TargetId, lease.TargetId);
        Assert.Equal(request.TargetRevision, lease.TargetRevision);
        Assert.Equal(request.LogicalExtent, lease.LogicalExtent);
        Assert.Equal(request.AllocationExtent, lease.AllocationExtent);
        Assert.Equal(ViewportFrameFormat.Bgra8Unorm, lease.Format);
        Assert.Equal(64UL, lease.FrameIndex);
        Assert.Equal((nint)11, lease.SlotIdentity);
        Assert.Equal((uint)ViewportRenderKind.Preview, api.Request.Kind);
        Assert.Equal((uint)ViewportTargetKind.DocumentScene, api.Request.TargetKind);
        Assert.Equal(request.SessionId.Value, api.Request.SessionId.ToGuid());
        Assert.Equal(request.TargetId, api.Request.TargetId.ToGuid());
        Assert.Equal(request.TargetRevision, api.Request.TargetRevision);
        Assert.Equal(request.Sequence, api.Request.RequestSequence);
        Assert.Equal(request.AllocationExtent.Width, api.Request.WidthPixels);
        Assert.Equal(request.AllocationExtent.Height, api.Request.HeightPixels);
        Assert.Equal(request.LogicalExtent.Width, api.Request.LogicalWidthPixels);
        Assert.Equal(request.LogicalExtent.Height, api.Request.LogicalHeightPixels);
        Assert.Equal(request.Camera.FieldOfViewRadians, api.Request.Camera.FieldOfViewRadians);
        Assert.Equal(
            (uint)ViewportNativeFieldOfViewAxis.MaintainVertical,
            api.Request.Camera.FieldOfViewAxis);
        Assert.Equal(
            (uint)ViewportNativePresentRequestV11Flags.HasLogicalExtent,
            api.Request.Flags);
        Assert.Single(api.DebugProxies);

        Assert.True(lease.Release(ViewportFrameCompletionKind.ConsumerAccessed));
        Assert.False(lease.Release(ViewportFrameCompletionKind.ConsumerAccessed));
        Assert.Equal(1, api.CompleteCalls);
        Assert.Equal(
            ViewportNativePresentCompletionKind.ConsumerAccessed,
            api.LastCompletionKind);
    }

    [Fact]
    public void V11_stream_maps_the_explicit_flash_sentinel_diagnostic_flag()
    {
        var api = new StubViewportNativeApi();
        var stream = new ViewportBridge(api)
            .OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt).Stream!;
        var request = PublishRequest(ViewportRenderKind.Scene, revision: 8);

        Assert.True(stream.SubmitLatest(
            request,
            ViewportRenderDiagnosticOverlay.FlashSentinelCorners).Succeeded);

        Assert.Equal(
            (uint)(ViewportNativePresentRequestV11Flags.HasLogicalExtent |
                   ViewportNativePresentRequestV11Flags.FlashSentinelCorners),
            api.Request.Flags);
        Assert.Equal(MathF.PI / 2, api.Request.Camera.FieldOfViewRadians);
        Assert.Equal(
            (uint)ViewportNativeFieldOfViewAxis.MaintainHorizontal,
            api.Request.Camera.FieldOfViewAxis);
    }

    [Fact]
    public void V11_stream_maps_scene_selection_and_echoes_its_view_state_revision()
    {
        var objectId = Guid.NewGuid();
        var document = new SceneDocumentSnapshot(
            Guid.NewGuid(),
            "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
            revision: 3,
            savedRevision: 1,
            [
                new SceneEntitySnapshot(
                    objectId,
                    new EntityId(2, 1),
                    "SelectedMesh",
                    TransformValue.Identity,
                    SceneMeshReference.DirectionalWedgeValidation),
            ]);
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);
        session.SetSelection(viewStateRevision: 7, objectId);
        var previewTransform = new TransformValue(
            new Float3(4, 5, 6),
            new Quaternion(0, MathF.Sqrt(0.5f), 0, MathF.Sqrt(0.5f)),
            Float3.One);
        session.SetTransformGizmoKind(ViewportTransformGizmoKind.Scale);
        session.SetTransformGizmo(new ViewportTransformGizmoState(
            ViewportTransformGizmoKind.Scale,
            objectId,
            previewTransform,
            ViewportGizmoAxis.X,
            ViewportGizmoAxis.X));
        Assert.True(session.TryPublishLatest(
            new ViewportRenderSize(new ViewportExtent(640, 360), new ViewportExtent(640, 360)),
            out var request));
        var api = new StubViewportNativeApi();
        var stream = new ViewportBridge(api)
            .OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt).Stream!;

        Assert.True(stream.SubmitLatest(request).Succeeded);
        Assert.Equal(
            (uint)(ViewportNativePresentRequestV11Flags.HasLogicalExtent |
                   ViewportNativePresentRequestV11Flags.HasSelectionOutline |
                   ViewportNativePresentRequestV11Flags.HasTransformGizmo),
            api.Request.Flags);
        Assert.Equal(objectId, api.Request.SelectedObjectId.ToGuid());
        Assert.Equal(7UL, api.Request.ViewStateRevision);
        Assert.Equal(objectId, api.Request.TransformGizmo.ObjectId.ToGuid());
        Assert.Equal(previewTransform.Position, api.Request.TransformGizmo.Position);
        Assert.Equal(previewTransform.Rotation, api.Request.TransformGizmo.Rotation);
        Assert.Equal(
            (uint)ViewportNativeTransformGizmoKind.Scale,
            api.Request.TransformGizmo.Kind);
        Assert.Equal((uint)ViewportGizmoAxis.X, api.Request.TransformGizmo.HoveredAxis);
        Assert.Equal((uint)ViewportGizmoAxis.X, api.Request.TransformGizmo.ActiveAxis);
        Assert.Equal(previewTransform, Assert.Single(api.DebugProxies).Transform);
        Assert.Equal(previewTransform, Assert.Single(api.AuthoredMeshes).Transform);

        var lease = Assert.IsType<ViewportFrameLease>(stream.TryTakeReady().Lease);
        Assert.Equal(7UL, lease.ViewStateRevision);
        lease.Dispose();
    }

    [Fact]
    public void V11_stream_rejects_unsupported_wireframe_before_native_submit_and_recovers_to_solid()
    {
        var document = new SceneDocumentSnapshot(
            Guid.NewGuid(),
            "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
            revision: 2,
            savedRevision: 1,
            []);
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);
        var api = new StubViewportNativeApi();
        var stream = Assert.IsType<ViewportRenderStream>(
            new ViewportBridge(api)
                .OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt).Stream);
        var renderSize = new ViewportRenderSize(
            new ViewportExtent(640, 360),
            new ViewportExtent(640, 360));

        session.SetSceneRasterMode(ViewportSceneRasterMode.Wireframe);
        Assert.True(session.TryPublishLatest(renderSize, out var wireframe));
        var rejected = stream.SubmitLatest(wireframe);

        Assert.False(stream.SupportsWireframe);
        Assert.False(rejected.Succeeded);
        Assert.Equal(ViewportFrameFailureKind.UnsupportedFeature, rejected.Failure!.Kind);
        Assert.Equal(0, api.SubmitCalls);

        session.SetSceneRasterMode(ViewportSceneRasterMode.Solid);
        Assert.True(session.TryPublishLatest(renderSize, out var solid));
        Assert.True(stream.SubmitLatest(solid).Succeeded);
        Assert.Equal(1, api.SubmitCalls);
    }

    [Fact]
    public void V11_stream_maps_native_feature_unavailable_without_faulting_the_bridge()
    {
        var api = new StubViewportNativeApi
        {
            StreamCapabilities = ViewportNativeStreamCapabilitiesV11.Wireframe,
            SubmitStatus = ViewportNativeStatus.FeatureUnavailable,
        };
        var stream = new ViewportBridge(api)
            .OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt).Stream!;
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            new SceneDocumentSnapshot(
                Guid.NewGuid(),
                "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
                revision: 2,
                savedRevision: 1,
                []),
            ViewportCameraSnapshot.DefaultScene);
        session.SetSceneRasterMode(ViewportSceneRasterMode.Wireframe);
        Assert.True(session.TryPublishLatest(
            new ViewportRenderSize(
                new ViewportExtent(640, 360),
                new ViewportExtent(640, 360)),
            out var request));

        var result = stream.SubmitLatest(request);

        Assert.False(result.Succeeded);
        Assert.Equal(ViewportFrameFailureKind.UnsupportedFeature, result.Failure!.Kind);
        Assert.Equal(1, api.SubmitCalls);
    }

    [Fact]
    public void V11_open_rejects_unknown_stream_capabilities()
    {
        var result = new ViewportBridge(new StubViewportNativeApi
        {
            StreamCapabilities = (ViewportNativeStreamCapabilitiesV11)(1U << 31),
        })
            .OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt);

        Assert.False(result.Succeeded);
        Assert.Equal(ViewportFrameFailureKind.InternalError, result.Failure!.Kind);
    }

    [Fact]
    public void V11_stream_reuses_the_same_slot_identity_across_frames()
    {
        var api = new StubViewportNativeApi();
        var stream = new ViewportBridge(api)
            .OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt).Stream!;
        var first = PublishRequest(ViewportRenderKind.Scene, revision: 2);
        Assert.True(stream.SubmitLatest(first).Succeeded);
        var firstLease = stream.TryTakeReady().Lease!;
        firstLease.Release(ViewportFrameCompletionKind.ConsumerAccessed);

        var second = PublishRequest(ViewportRenderKind.Scene, revision: 3);
        Assert.True(stream.SubmitLatest(second).Succeeded);
        var secondLease = stream.TryTakeReady().Lease!;

        Assert.Equal(firstLease.SlotIdentity, secondLease.SlotIdentity);
        Assert.Equal(firstLease.NativeHandles, secondLease.NativeHandles);
        Assert.Equal(second.Sequence, secondLease.RequestSequence);
        secondLease.Dispose();
    }

    [Fact]
    public void V11_try_take_reports_no_frame_without_fabricating_a_failure()
    {
        var api = new StubViewportNativeApi { ReturnReadyFrame = false };
        var stream = new ViewportBridge(api)
            .OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt).Stream!;

        var taken = stream.TryTakeReady();

        Assert.True(taken.Succeeded);
        Assert.False(taken.HasFrame);
        Assert.Null(taken.Failure);
    }

    [Fact]
    public void V11_stream_close_releases_import_and_destroys_only_after_closed_poll()
    {
        var api = new StubViewportNativeApi();
        var stream = new ViewportBridge(api)
            .OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt).Stream!;
        var request = PublishRequest(ViewportRenderKind.Scene, revision: 2);
        stream.SubmitLatest(request);
        var lease = stream.TryTakeReady().Lease!;
        lease.Dispose();

        stream.RequestClose();
        stream.ReleaseSlotImport(lease.SlotIdentity);
        var snapshot = stream.Poll();
        stream.DestroyClosed();

        Assert.Equal(ViewportRenderStreamLifecycle.Closed, snapshot.Lifecycle);
        Assert.Equal(1, api.CloseCalls);
        Assert.Equal(1, api.ReleaseImportCalls);
        Assert.Equal(1, api.DestroyCalls);
    }

    [Theory]
    [InlineData(5U, ViewportFrameFailureKind.DeviceMismatch)]
    [InlineData(3U, ViewportFrameFailureKind.UnsupportedInterop)]
    [InlineData(2U, ViewportFrameFailureKind.NativeUnavailable)]
    public void V11_open_maps_native_failures(
        uint rawStatus,
        ViewportFrameFailureKind expected)
    {
        var status = (ViewportNativeStatus)rawStatus;
        var result = new ViewportBridge(new StubViewportNativeApi { Status = status })
            .OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt);

        Assert.False(result.Succeeded);
        Assert.Equal(expected, result.Failure!.Kind);
    }

    [Fact]
    public void V11_abi_layout_is_explicit_and_pointer_sized()
    {
        Assert.Equal(52, Marshal.SizeOf<ViewportNativeCamera>());
        Assert.Equal(
            (nint)36,
            Marshal.OffsetOf<ViewportNativeCamera>(
                "<FieldOfViewRadians>k__BackingField"));
        Assert.Equal(
            (nint)40,
            Marshal.OffsetOf<ViewportNativeCamera>(
                "<FieldOfViewAxis>k__BackingField"));
        Assert.Equal(
            (nint)44,
            Marshal.OffsetOf<ViewportNativeCamera>("<NearPlane>k__BackingField"));
        Assert.Equal(
            (nint)48,
            Marshal.OffsetOf<ViewportNativeCamera>("<FarPlane>k__BackingField"));
        Assert.Equal(24, Marshal.SizeOf<ViewportNativeStreamHandleV11>());
        Assert.Equal(88, Marshal.SizeOf<ViewportNativeAuthoredMeshSnapshotV11>());
        Assert.Equal(
            (nint)0,
            Marshal.OffsetOf<ViewportNativeAuthoredMeshSnapshotV11>(
                "<ObjectId>k__BackingField"));
        Assert.Equal(
            (nint)16,
            Marshal.OffsetOf<ViewportNativeAuthoredMeshSnapshotV11>(
                "<RuntimeEntityIndex>k__BackingField"));
        Assert.Equal(
            (nint)20,
            Marshal.OffsetOf<ViewportNativeAuthoredMeshSnapshotV11>(
                "<RuntimeEntityGeneration>k__BackingField"));
        Assert.Equal(
            (nint)24,
            Marshal.OffsetOf<ViewportNativeAuthoredMeshSnapshotV11>(
                "<AssetId>k__BackingField"));
        Assert.Equal(
            (nint)40,
            Marshal.OffsetOf<ViewportNativeAuthoredMeshSnapshotV11>(
                "<ExpectedMeshType>k__BackingField"));
        Assert.Equal(
            (nint)48,
            Marshal.OffsetOf<ViewportNativeAuthoredMeshSnapshotV11>(
                "<Transform>k__BackingField"));
        Assert.Equal(56, Marshal.SizeOf<ViewportNativeTransformGizmoV11>());
        Assert.Equal(
            (nint)28,
            Marshal.OffsetOf<ViewportNativeTransformGizmoV11>("<Rotation>k__BackingField"));
        Assert.Equal(
            (nint)44,
            Marshal.OffsetOf<ViewportNativeTransformGizmoV11>("<Kind>k__BackingField"));
        Assert.Equal(
            (nint)48,
            Marshal.OffsetOf<ViewportNativeTransformGizmoV11>("<HoveredAxis>k__BackingField"));
        Assert.Equal(
            (nint)52,
            Marshal.OffsetOf<ViewportNativeTransformGizmoV11>("<ActiveAxis>k__BackingField"));
        Assert.Equal(248, Marshal.SizeOf<ViewportNativePresentRequestV11>());
        Assert.Equal(256, Marshal.SizeOf<ViewportNativeReadyFrameV11>());
        Assert.Equal(72, Marshal.SizeOf<ViewportNativeStreamPollV11>());
        Assert.Equal(
            (nint)88,
            Marshal.OffsetOf<ViewportNativePresentRequestV11>("<Camera>k__BackingField"));
        Assert.Equal(
            (nint)140,
            Marshal.OffsetOf<ViewportNativePresentRequestV11>(
                "<LogicalWidthPixels>k__BackingField"));
        Assert.Equal(
            (nint)144,
            Marshal.OffsetOf<ViewportNativePresentRequestV11>(
                "<LogicalHeightPixels>k__BackingField"));
        Assert.Equal(
            (nint)152,
            Marshal.OffsetOf<ViewportNativePresentRequestV11>(
                "<AuthoredMeshes>k__BackingField"));
        Assert.Equal(
            (nint)164,
            Marshal.OffsetOf<ViewportNativePresentRequestV11>(
                "<SceneRasterMode>k__BackingField"));
        Assert.Equal(
            (nint)168,
            Marshal.OffsetOf<ViewportNativePresentRequestV11>(
                "<SelectedObjectId>k__BackingField"));
        Assert.Equal(
            (nint)184,
            Marshal.OffsetOf<ViewportNativePresentRequestV11>(
                "<ViewStateRevision>k__BackingField"));
        Assert.Equal(
            (nint)192,
            Marshal.OffsetOf<ViewportNativePresentRequestV11>(
                "<TransformGizmo>k__BackingField"));
        Assert.Equal(
            (nint)248,
            Marshal.OffsetOf<ViewportNativeReadyFrameV11>(
                "<ViewStateRevision>k__BackingField"));
    }

    [Fact]
    public void V11_canonical_uuid_uses_network_byte_order()
    {
        var guid = Guid.Parse("7c9fe8ac-3c8b-4f66-9665-0af0fd7b693e");
        var native = ViewportNativeCanonicalUuid.FromGuid(guid);

        Assert.Equal(
            new byte[]
            {
                0x7c, 0x9f, 0xe8, 0xac, 0x3c, 0x8b, 0x4f, 0x66,
                0x96, 0x65, 0x0a, 0xf0, 0xfd, 0x7b, 0x69, 0x3e,
            },
            MemoryMarshal.AsBytes(new[] { native }.AsSpan()).ToArray());
        Assert.Equal(guid, native.ToGuid());
    }

    [Fact]
    public void V11_submit_marshals_authored_meshes_without_renderer_keys()
    {
        var objectId = Guid.NewGuid();
        var transform = new TransformValue(
            new Float3(1, 2, 3),
            new Quaternion(0, MathF.Sqrt(0.5f), 0, MathF.Sqrt(0.5f)),
            new Float3(2, 3, 4));
        var document = new SceneDocumentSnapshot(
            Guid.NewGuid(),
            "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
            revision: 2,
            savedRevision: 1,
            [
                new SceneEntitySnapshot(
                    objectId,
                    new EntityId(8, 3),
                    "Validation mesh",
                    transform,
                    SceneMeshReference.DirectionalWedgeValidation),
            ]);
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            ViewportRenderKind.Scene,
            document,
            ViewportCameraSnapshot.DefaultScene);
        Assert.True(session.TryPublishLatest(
            new ViewportRenderSize(new ViewportExtent(640, 360), new ViewportExtent(640, 360)),
            out var request));
        var api = new StubViewportNativeApi();
        var stream = new ViewportBridge(api)
            .OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt).Stream!;

        Assert.True(stream.SubmitLatest(request).Succeeded);
        var mesh = Assert.Single(api.AuthoredMeshes);
        Assert.Equal(objectId, mesh.ObjectId.ToGuid());
        Assert.Equal(8U, mesh.RuntimeEntityIndex);
        Assert.Equal(3U, mesh.RuntimeEntityGeneration);
        Assert.Equal(SceneMeshReference.DirectionalWedgeValidation.AssetId, mesh.AssetId.ToGuid());
        Assert.Equal(ViewportAuthoredMeshSnapshot.ExpectedMeshType, mesh.ExpectedMeshType);
        Assert.Equal(transform, mesh.Transform);
    }

    private static ViewportRenderRequest PublishRequest(ViewportRenderKind kind, ulong revision)
    {
        var document = new SceneDocumentSnapshot(
            Guid.NewGuid(),
            "C:\\Projects\\Sample\\Assets\\Scenes\\Default.asharia.scene.json",
            revision,
            savedRevision: 1,
            [
                new SceneEntitySnapshot(
                    Guid.NewGuid(),
                    new EntityId(1, 1),
                    "Entity",
                    new TransformValue(new Float3(1, 2, 3), Quaternion.Identity, Float3.One)),
            ]);
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            kind,
            document,
            ViewportCameraSnapshot.DefaultFor(kind));
        Assert.True(session.TryPublishLatest(
            new ViewportRenderSize(
                new ViewportExtent(801, 451),
                new ViewportExtent(816, 464)),
            out var request));
        return request;
    }

    private sealed unsafe class StubViewportNativeApi : IViewportNativeApi
    {
        private ulong readySequence_;
        private bool closed_;

        public ViewportNativeStatus Status { get; set; } = ViewportNativeStatus.Success;

        public ViewportNativeStatus SubmitStatus { get; set; } = ViewportNativeStatus.Success;

        public ViewportNativeStreamCapabilitiesV11 StreamCapabilities { get; set; }

        public bool ReturnReadyFrame { get; set; } = true;

        public ViewportNativeSceneMeshReceiptV11? SceneReceipt { get; set; }

        public ViewportNativePresentRequestV11 Request { get; private set; }

        public ViewportNativeDebugProxy[] DebugProxies { get; private set; } = [];

        public ViewportNativeAuthoredMeshSnapshotV11[] AuthoredMeshes { get; private set; } = [];

        public int CompleteCalls { get; private set; }

        public int SubmitCalls { get; private set; }

        public int CloseCalls { get; private set; }

        public int ReleaseImportCalls { get; private set; }

        public int DestroyCalls { get; private set; }

        public ViewportNativePresentCompletionKind? LastCompletionKind { get; private set; }

        public ViewportNativeStatus QueryCompositionCompatibility(
            in ViewportNativeCompatibilityRequest request,
            out ViewportNativeCompatibilityResult result)
        {
            result = default;
            return Status;
        }

        public void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result)
        {
        }

        public ViewportNativeStatus OpenStreamV11(
            in ViewportNativeCompatibilityRequest compatibility,
            out ViewportNativeStreamHandleV11 stream)
        {
            stream = new ViewportNativeStreamHandleV11(
                ViewportNativeAbiHeader.Current<ViewportNativeStreamHandleV11>(),
                (uint)Status,
                (uint)StreamCapabilities,
                Status == ViewportNativeStatus.Success ? 7UL : 0UL);
            return Status;
        }

        public ViewportNativeStatus SubmitLatestV11(
            ulong streamId,
            in ViewportNativePresentRequestV11 request)
        {
            SubmitCalls++;
            Request = request;
            readySequence_ = request.RequestSequence;
            DebugProxies = request.DebugProxyCount == 0
                ? []
                : new ReadOnlySpan<ViewportNativeDebugProxy>(
                    request.DebugProxies.ToPointer(),
                    checked((int)request.DebugProxyCount)).ToArray();
            AuthoredMeshes = request.AuthoredMeshCount == 0
                ? []
                : new ReadOnlySpan<ViewportNativeAuthoredMeshSnapshotV11>(
                    request.AuthoredMeshes.ToPointer(),
                    checked((int)request.AuthoredMeshCount)).ToArray();
            return SubmitStatus;
        }

        public ViewportNativeStatus TryTakeReadyV11(
            ulong streamId,
            out ViewportNativeReadyFrameV11 frame)
        {
            var hasFrame = ReturnReadyFrame && readySequence_ != 0;
            frame = new ViewportNativeReadyFrameV11(
                ViewportNativeAbiHeader.Current<ViewportNativeReadyFrameV11>(),
                (uint)Status,
                hasFrame ? 1U : 0U,
                hasFrame ? streamId : 0UL,
                hasFrame ? (nint)11 : 0,
                hasFrame ? (nint)12 : 0,
                hasFrame ? (nint)13 : 0,
                hasFrame ? (nint)14 : 0,
                hasFrame ? Request.WidthPixels : 0,
                hasFrame ? Request.HeightPixels : 0,
                hasFrame ? (uint)ViewportNativeImageFormat.Bgra8Unorm : 0,
                0,
                hasFrame ? Request.WidthPixels * Request.HeightPixels * 4UL : 0,
                hasFrame ? 64UL : 0,
                hasFrame ? Request.SessionId : default,
                hasFrame ? Request.TargetId : default,
                hasFrame ? Request.TargetRevision : 0,
                hasFrame ? readySequence_ : 0,
                hasFrame ? Request.Kind : 0,
                hasFrame ? Request.TargetKind : 0,
                hasFrame ? Request.LogicalWidthPixels : 0,
                hasFrame ? Request.LogicalHeightPixels : 0,
                SceneReceipt ?? new ViewportNativeSceneMeshReceiptV11(
                    0,
                    0,
                    0,
                    0,
                    (uint)ViewportNativeSceneRasterMode.Solid,
                    0,
                    0,
                    0,
                    default,
                    default,
                    0,
                    0,
                    0,
                    hasFrame ? Request.TargetRevision : 0),
                hasFrame ? Request.ViewStateRevision : 0);
            readySequence_ = 0;
            return Status;
        }

        public void CompleteFrameV11(
            ulong streamId,
            nint nativeSlot,
            ViewportNativePresentCompletionKind completionKind)
        {
            CompleteCalls++;
            LastCompletionKind = completionKind;
        }

        public void ReleaseSlotImportV11(ulong streamId, nint nativeSlot) =>
            ReleaseImportCalls++;

        public void CloseStreamV11(ulong streamId)
        {
            CloseCalls++;
            closed_ = true;
        }

        public ViewportNativeStatus PollStreamV11(
            ulong streamId,
            out ViewportNativeStreamPollV11 poll)
        {
            poll = new ViewportNativeStreamPollV11(
                ViewportNativeAbiHeader.Current<ViewportNativeStreamPollV11>(),
                (uint)ViewportNativeStatus.Success,
                (uint)(closed_
                    ? ViewportNativeStreamLifecycle.Closed
                    : ViewportNativeStreamLifecycle.Open),
                0,
                readySequence_ != 0 ? 1U : 0U,
                0,
                1,
                0,
                0,
                1,
                0,
                1);
            return ViewportNativeStatus.Success;
        }

        public ViewportNativeStatus WaitStreamChangeV11(ulong streamId, ulong observedRevision, uint timeoutMs) =>
            WaitHandler?.Invoke() ?? ViewportNativeStatus.Success;

        public Func<ViewportNativeStatus>? WaitHandler { get; set; }

        public void DestroyStreamV11(ulong streamId) => DestroyCalls++;

        public void Shutdown()
        {
        }
    }
}
