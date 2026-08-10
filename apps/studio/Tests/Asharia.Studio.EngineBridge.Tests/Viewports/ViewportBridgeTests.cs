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
    public void V5_stream_maps_latest_request_and_returns_a_bound_frame()
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
        Assert.Equal(
            (uint)ViewportNativePresentRequestV5Flags.HasLogicalExtent,
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
    public void V5_stream_maps_the_explicit_flash_sentinel_diagnostic_flag()
    {
        var api = new StubViewportNativeApi();
        var stream = new ViewportBridge(api)
            .OpenStream(ViewportDeviceCompatibility.VulkanOpaqueNt).Stream!;
        var request = PublishRequest(ViewportRenderKind.Scene, revision: 8);

        Assert.True(stream.SubmitLatest(
            request,
            ViewportRenderDiagnosticOverlay.FlashSentinelCorners).Succeeded);

        Assert.Equal(
            (uint)(ViewportNativePresentRequestV5Flags.HasLogicalExtent |
                   ViewportNativePresentRequestV5Flags.FlashSentinelCorners),
            api.Request.Flags);
    }

    [Fact]
    public void V5_stream_reuses_the_same_slot_identity_across_frames()
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
    public void V5_try_take_reports_no_frame_without_fabricating_a_failure()
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
    public void V5_stream_close_releases_import_and_destroys_only_after_closed_poll()
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
    public void V5_open_maps_native_failures(
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
    public void V5_abi_layout_is_explicit_and_pointer_sized()
    {
        Assert.Equal(24, Marshal.SizeOf<ViewportNativeStreamHandleV5>());
        Assert.Equal(144, Marshal.SizeOf<ViewportNativePresentRequestV5>());
        Assert.Equal(152, Marshal.SizeOf<ViewportNativeReadyFrameV5>());
        Assert.Equal(64, Marshal.SizeOf<ViewportNativeStreamPollV5>());
        Assert.Equal(
            (nint)136,
            Marshal.OffsetOf<ViewportNativePresentRequestV5>(
                "<LogicalWidthPixels>k__BackingField"));
        Assert.Equal(
            (nint)140,
            Marshal.OffsetOf<ViewportNativePresentRequestV5>(
                "<LogicalHeightPixels>k__BackingField"));
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
                    "Entity",
                    new TransformValue(new Float3(1, 2, 3), Quaternion.Identity, Float3.One)),
            ]);
        var session = new ViewportSession(
            ViewportSessionId.Create(),
            kind,
            document,
            ViewportCameraSnapshot.DefaultScene);
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

        public bool ReturnReadyFrame { get; set; } = true;

        public ViewportNativePresentRequestV5 Request { get; private set; }

        public ViewportNativeDebugProxy[] DebugProxies { get; private set; } = [];

        public int CompleteCalls { get; private set; }

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

        public ViewportNativeStatus OpenStreamV5(
            in ViewportNativeCompatibilityRequest compatibility,
            out ViewportNativeStreamHandleV5 stream)
        {
            stream = new ViewportNativeStreamHandleV5(
                ViewportNativeAbiHeader.Current<ViewportNativeStreamHandleV5>(),
                (uint)Status,
                0,
                Status == ViewportNativeStatus.Success ? 7UL : 0UL);
            return Status;
        }

        public ViewportNativeStatus SubmitLatestV5(
            ulong streamId,
            in ViewportNativePresentRequestV5 request)
        {
            Request = request;
            readySequence_ = request.RequestSequence;
            DebugProxies = request.DebugProxyCount == 0
                ? []
                : new ReadOnlySpan<ViewportNativeDebugProxy>(
                    request.DebugProxies.ToPointer(),
                    checked((int)request.DebugProxyCount)).ToArray();
            return Status;
        }

        public ViewportNativeStatus TryTakeReadyV5(
            ulong streamId,
            out ViewportNativeReadyFrameV5 frame)
        {
            var hasFrame = ReturnReadyFrame && readySequence_ != 0;
            frame = new ViewportNativeReadyFrameV5(
                ViewportNativeAbiHeader.Current<ViewportNativeReadyFrameV5>(),
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
                hasFrame ? Request.LogicalHeightPixels : 0);
            readySequence_ = 0;
            return Status;
        }

        public void CompleteFrameV5(
            ulong streamId,
            nint nativeSlot,
            ViewportNativePresentCompletionKind completionKind)
        {
            CompleteCalls++;
            LastCompletionKind = completionKind;
        }

        public void ReleaseSlotImportV5(ulong streamId, nint nativeSlot) =>
            ReleaseImportCalls++;

        public void CloseStreamV5(ulong streamId)
        {
            CloseCalls++;
            closed_ = true;
        }

        public ViewportNativeStatus PollStreamV5(
            ulong streamId,
            out ViewportNativeStreamPollV5 poll)
        {
            poll = new ViewportNativeStreamPollV5(
                ViewportNativeAbiHeader.Current<ViewportNativeStreamPollV5>(),
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

        public void DestroyStreamV5(ulong streamId) => DestroyCalls++;

        public void Shutdown()
        {
        }
    }
}
