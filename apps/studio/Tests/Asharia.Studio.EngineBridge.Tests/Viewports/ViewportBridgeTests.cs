using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.InteropServices;
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
    public void V8_stream_maps_latest_request_and_returns_a_bound_frame()
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
            (uint)ViewportNativePresentRequestV8Flags.HasLogicalExtent,
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
    public void V8_stream_maps_the_explicit_flash_sentinel_diagnostic_flag()
    {
        var api = new StubViewportNativeApi();
        var stream = new ViewportBridge(api)
            .OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt).Stream!;
        var request = PublishRequest(ViewportRenderKind.Scene, revision: 8);

        Assert.True(stream.SubmitLatest(
            request,
            ViewportRenderDiagnosticOverlay.FlashSentinelCorners).Succeeded);

        Assert.Equal(
            (uint)(ViewportNativePresentRequestV8Flags.HasLogicalExtent |
                   ViewportNativePresentRequestV8Flags.FlashSentinelCorners),
            api.Request.Flags);
        Assert.Equal(MathF.PI / 2, api.Request.Camera.FieldOfViewRadians);
        Assert.Equal(
            (uint)ViewportNativeFieldOfViewAxis.MaintainHorizontal,
            api.Request.Camera.FieldOfViewAxis);
    }

    [Fact]
    public void V8_stream_maps_scene_selection_and_echoes_its_view_state_revision()
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
            Quaternion.Identity,
            Float3.One);
        session.SetTranslateGizmo(new ViewportTranslateGizmoState(
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
            (uint)(ViewportNativePresentRequestV8Flags.HasLogicalExtent |
                   ViewportNativePresentRequestV8Flags.HasSelectionOutline |
                   ViewportNativePresentRequestV8Flags.HasTranslateGizmo),
            api.Request.Flags);
        Assert.Equal(objectId, api.Request.SelectedObjectId.ToGuid());
        Assert.Equal(7UL, api.Request.ViewStateRevision);
        Assert.Equal(objectId, api.Request.TranslateGizmo.ObjectId.ToGuid());
        Assert.Equal(previewTransform.Position, api.Request.TranslateGizmo.Position);
        Assert.Equal((uint)ViewportGizmoAxis.X, api.Request.TranslateGizmo.HoveredAxis);
        Assert.Equal((uint)ViewportGizmoAxis.X, api.Request.TranslateGizmo.ActiveAxis);
        Assert.Equal(previewTransform, Assert.Single(api.DebugProxies).Transform);
        Assert.Equal(previewTransform, Assert.Single(api.AuthoredMeshes).Transform);

        var lease = Assert.IsType<ViewportFrameLease>(stream.TryTakeReady().Lease);
        Assert.Equal(7UL, lease.ViewStateRevision);
        lease.Dispose();
    }

    [Fact]
    public void V8_stream_rejects_unsupported_wireframe_before_native_submit_and_recovers_to_solid()
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
    public void V8_stream_maps_native_feature_unavailable_without_faulting_the_bridge()
    {
        var api = new StubViewportNativeApi
        {
            StreamCapabilities = ViewportNativeStreamCapabilitiesV8.Wireframe,
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
    public void V8_open_rejects_unknown_stream_capabilities()
    {
        var result = new ViewportBridge(new StubViewportNativeApi
        {
            StreamCapabilities = (ViewportNativeStreamCapabilitiesV8)(1U << 31),
        })
            .OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt);

        Assert.False(result.Succeeded);
        Assert.Equal(ViewportFrameFailureKind.InternalError, result.Failure!.Kind);
    }

    [Fact]
    public void V8_stream_reuses_the_same_slot_identity_across_frames()
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
    public void V8_try_take_reports_no_frame_without_fabricating_a_failure()
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
    public void V8_stream_close_releases_import_and_destroys_only_after_closed_poll()
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
    public void V8_open_maps_native_failures(
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
    public void V8_abi_layout_is_explicit_and_pointer_sized()
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
        Assert.Equal(24, Marshal.SizeOf<ViewportNativeStreamHandleV8>());
        Assert.Equal(88, Marshal.SizeOf<ViewportNativeAuthoredMeshSnapshotV8>());
        Assert.Equal(
            (nint)0,
            Marshal.OffsetOf<ViewportNativeAuthoredMeshSnapshotV8>(
                "<ObjectId>k__BackingField"));
        Assert.Equal(
            (nint)16,
            Marshal.OffsetOf<ViewportNativeAuthoredMeshSnapshotV8>(
                "<RuntimeEntityIndex>k__BackingField"));
        Assert.Equal(
            (nint)20,
            Marshal.OffsetOf<ViewportNativeAuthoredMeshSnapshotV8>(
                "<RuntimeEntityGeneration>k__BackingField"));
        Assert.Equal(
            (nint)24,
            Marshal.OffsetOf<ViewportNativeAuthoredMeshSnapshotV8>(
                "<AssetId>k__BackingField"));
        Assert.Equal(
            (nint)40,
            Marshal.OffsetOf<ViewportNativeAuthoredMeshSnapshotV8>(
                "<ExpectedMeshType>k__BackingField"));
        Assert.Equal(
            (nint)48,
            Marshal.OffsetOf<ViewportNativeAuthoredMeshSnapshotV8>(
                "<Transform>k__BackingField"));
        Assert.Equal(40, Marshal.SizeOf<ViewportNativeTranslateGizmoV8>());
        Assert.Equal(232, Marshal.SizeOf<ViewportNativePresentRequestV8>());
        Assert.Equal(256, Marshal.SizeOf<ViewportNativeReadyFrameV8>());
        Assert.Equal(64, Marshal.SizeOf<ViewportNativeStreamPollV8>());
        Assert.Equal(
            (nint)88,
            Marshal.OffsetOf<ViewportNativePresentRequestV8>("<Camera>k__BackingField"));
        Assert.Equal(
            (nint)140,
            Marshal.OffsetOf<ViewportNativePresentRequestV8>(
                "<LogicalWidthPixels>k__BackingField"));
        Assert.Equal(
            (nint)144,
            Marshal.OffsetOf<ViewportNativePresentRequestV8>(
                "<LogicalHeightPixels>k__BackingField"));
        Assert.Equal(
            (nint)152,
            Marshal.OffsetOf<ViewportNativePresentRequestV8>(
                "<AuthoredMeshes>k__BackingField"));
        Assert.Equal(
            (nint)164,
            Marshal.OffsetOf<ViewportNativePresentRequestV8>(
                "<SceneRasterMode>k__BackingField"));
        Assert.Equal(
            (nint)168,
            Marshal.OffsetOf<ViewportNativePresentRequestV8>(
                "<SelectedObjectId>k__BackingField"));
        Assert.Equal(
            (nint)184,
            Marshal.OffsetOf<ViewportNativePresentRequestV8>(
                "<ViewStateRevision>k__BackingField"));
        Assert.Equal(
            (nint)192,
            Marshal.OffsetOf<ViewportNativePresentRequestV8>(
                "<TranslateGizmo>k__BackingField"));
        Assert.Equal(
            (nint)248,
            Marshal.OffsetOf<ViewportNativeReadyFrameV8>(
                "<ViewStateRevision>k__BackingField"));
    }

    [Fact]
    public void V8_canonical_uuid_uses_network_byte_order()
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
    public void V8_submit_marshals_authored_meshes_without_renderer_keys()
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

        public ViewportNativeStreamCapabilitiesV8 StreamCapabilities { get; set; }

        public bool ReturnReadyFrame { get; set; } = true;

        public ViewportNativePresentRequestV8 Request { get; private set; }

        public ViewportNativeDebugProxy[] DebugProxies { get; private set; } = [];

        public ViewportNativeAuthoredMeshSnapshotV8[] AuthoredMeshes { get; private set; } = [];

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

        public ViewportNativeStatus OpenStreamV8(
            in ViewportNativeCompatibilityRequest compatibility,
            out ViewportNativeStreamHandleV8 stream)
        {
            stream = new ViewportNativeStreamHandleV8(
                ViewportNativeAbiHeader.Current<ViewportNativeStreamHandleV8>(),
                (uint)Status,
                (uint)StreamCapabilities,
                Status == ViewportNativeStatus.Success ? 7UL : 0UL);
            return Status;
        }

        public ViewportNativeStatus SubmitLatestV8(
            ulong streamId,
            in ViewportNativePresentRequestV8 request)
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
                : new ReadOnlySpan<ViewportNativeAuthoredMeshSnapshotV8>(
                    request.AuthoredMeshes.ToPointer(),
                    checked((int)request.AuthoredMeshCount)).ToArray();
            return SubmitStatus;
        }

        public ViewportNativeStatus TryTakeReadyV8(
            ulong streamId,
            out ViewportNativeReadyFrameV8 frame)
        {
            var hasFrame = ReturnReadyFrame && readySequence_ != 0;
            frame = new ViewportNativeReadyFrameV8(
                ViewportNativeAbiHeader.Current<ViewportNativeReadyFrameV8>(),
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
                new ViewportNativeSceneMeshReceiptV8(
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

        public void CompleteFrameV8(
            ulong streamId,
            nint nativeSlot,
            ViewportNativePresentCompletionKind completionKind)
        {
            CompleteCalls++;
            LastCompletionKind = completionKind;
        }

        public void ReleaseSlotImportV8(ulong streamId, nint nativeSlot) =>
            ReleaseImportCalls++;

        public void CloseStreamV8(ulong streamId)
        {
            CloseCalls++;
            closed_ = true;
        }

        public ViewportNativeStatus PollStreamV8(
            ulong streamId,
            out ViewportNativeStreamPollV8 poll)
        {
            poll = new ViewportNativeStreamPollV8(
                ViewportNativeAbiHeader.Current<ViewportNativeStreamPollV8>(),
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

        public void DestroyStreamV8(ulong streamId) => DestroyCalls++;

        public void Shutdown()
        {
        }
    }
}
