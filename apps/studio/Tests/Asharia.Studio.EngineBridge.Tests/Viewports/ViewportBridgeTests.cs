using System;
using System.Linq;
using System.Reflection;
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
    public void Create_slot_maps_the_generic_request_and_returns_an_owned_frame_lease()
    {
        var api = new StubViewportNativeApi();
        var bridge = new ViewportBridge(api);
        var request = BeginRequest(ViewportRenderKind.Preview, revision: 8);

        var result = bridge.CreatePresentSlot(
            request,
            ViewportDeviceCompatibility.VulkanOpaqueNt);

        Assert.True(result.Succeeded);
        var lease = Assert.IsType<ViewportFrameLease>(result.Lease);
        Assert.Equal(request.SessionId, lease.SessionId);
        Assert.Equal(request.Sequence, lease.RequestSequence);
        Assert.Equal(request.TargetId, lease.TargetId);
        Assert.Equal(request.TargetRevision, lease.TargetRevision);
        Assert.Equal(request.Extent, lease.Extent);
        Assert.Equal(ViewportFrameFormat.Bgra8Unorm, lease.Format);
        Assert.Equal(64UL, lease.FrameIndex);
        Assert.Equal((uint)ViewportRenderKind.Preview, api.Request.Kind);
        Assert.Equal((uint)ViewportTargetKind.DocumentScene, api.Request.TargetKind);
        Assert.Equal(request.SessionId.Value, api.Request.SessionId.ToGuid());
        Assert.Equal(request.TargetId, api.Request.TargetId.ToGuid());
        Assert.Equal(request.TargetRevision, api.Request.TargetRevision);
        Assert.Equal(request.Sequence, api.Request.RequestSequence);
        Assert.Equal(request.Camera.Position, api.Request.Camera.Position);
        Assert.Single(api.DebugProxies);
        Assert.Equal(request.DebugProxies[0].ObjectId, api.DebugProxies[0].ObjectId.ToGuid());
        Assert.Equal(request.DebugProxies[0].Transform, api.DebugProxies[0].Transform);

        Assert.True(lease.Complete());
        Assert.False(lease.Complete());
        Assert.Equal(ViewportFrameCompletionKind.Presented, lease.CompletionKind);
        Assert.Equal(1, api.ReleaseCalls);
    }

    [Fact]
    public void Dispose_releases_a_slot_exactly_once()
    {
        var api = new StubViewportNativeApi();
        var bridge = new ViewportBridge(api);
        var result = bridge.CreatePresentSlot(
            BeginRequest(ViewportRenderKind.Scene, revision: 2),
            ViewportDeviceCompatibility.VulkanOpaqueNt);

        result.Lease!.Dispose();
        result.Lease.Dispose();

        Assert.Equal(1, api.ReleaseCalls);
        Assert.Equal(ViewportFrameCompletionKind.Abandoned, result.Lease.CompletionKind);
    }

    [Fact]
    public void Cancel_abandons_a_slot_and_rejects_later_completion()
    {
        var api = new StubViewportNativeApi();
        var bridge = new ViewportBridge(api);
        var result = bridge.CreatePresentSlot(
            BeginRequest(ViewportRenderKind.Scene, revision: 2),
            ViewportDeviceCompatibility.VulkanOpaqueNt);

        Assert.True(result.Lease!.Cancel());
        Assert.False(result.Lease.Complete());
        Assert.Equal(ViewportFrameCompletionKind.Abandoned, result.Lease.CompletionKind);
        Assert.Equal(1, api.ReleaseCalls);
    }

    [Fact]
    public void Native_failure_is_typed_and_its_message_packet_is_released()
    {
        var api = new StubViewportNativeApi
        {
            Status = ViewportNativeStatus.DeviceMismatch,
            Message = "composition device mismatch",
        };
        var bridge = new ViewportBridge(api);

        var result = bridge.CreatePresentSlot(
            BeginRequest(ViewportRenderKind.Game, revision: 3),
            ViewportDeviceCompatibility.VulkanOpaqueNt);

        Assert.False(result.Succeeded);
        Assert.Null(result.Lease);
        Assert.Equal(ViewportFrameFailureKind.DeviceMismatch, result.Failure!.Kind);
        Assert.Equal("composition device mismatch", result.Failure.Message);
        Assert.Equal(1, api.ReleaseCalls);
    }

    [Fact]
    public void Binding_failure_is_reported_without_a_native_lease()
    {
        var bridge = new ViewportBridge(new StubViewportNativeApi
        {
            Exception = new DllNotFoundException("missing editor native"),
        });

        var result = bridge.CreatePresentSlot(
            BeginRequest(ViewportRenderKind.Scene, revision: 1),
            ViewportDeviceCompatibility.VulkanOpaqueNt);

        Assert.False(result.Succeeded);
        Assert.Equal(ViewportFrameFailureKind.NativeUnavailable, result.Failure!.Kind);
        Assert.Contains("missing editor native", result.Failure.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Unexpected_bridge_failure_is_typed_without_exposing_an_exception()
    {
        var bridge = new ViewportBridge(new StubViewportNativeApi
        {
            Exception = new InvalidOperationException("broken adapter"),
        });

        var result = bridge.CreatePresentSlot(
            BeginRequest(ViewportRenderKind.Scene, revision: 1),
            ViewportDeviceCompatibility.VulkanOpaqueNt);

        Assert.False(result.Succeeded);
        Assert.Equal(ViewportFrameFailureKind.InternalError, result.Failure!.Kind);
        Assert.Contains("broken adapter", result.Failure.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void Runtime_shutdown_is_forwarded_exactly_once()
    {
        var api = new StubViewportNativeApi();
        var bridge = new ViewportRuntimeBridge(api);

        bridge.Shutdown();

        Assert.Equal(1, api.ShutdownCalls);
    }

    [Fact]
    public void Managed_layout_matches_the_native_viewport_v4_contract()
    {
        Assert.Equal(8, Marshal.SizeOf<ViewportNativeAbiHeader>());
        Assert.Equal(16, Marshal.SizeOf<ViewportNativeId>());
        Assert.Equal(56, Marshal.SizeOf<ViewportNativeCompatibilityRequest>());
        Assert.Equal(48, Marshal.SizeOf<ViewportNativeCamera>());
        Assert.Equal(56, Marshal.SizeOf<ViewportNativeDebugProxy>());
        Assert.Equal(192, Marshal.SizeOf<ViewportNativePresentRequestV4>());
        Assert.Equal(96, Marshal.SizeOf<ViewportNativePresentPacket>());
        Assert.DoesNotContain(
            typeof(ViewportFrameLease).GetProperties(BindingFlags.Instance | BindingFlags.Public),
            property => property.PropertyType == typeof(nint));
    }

    private static ViewportRenderRequest BeginRequest(ViewportRenderKind kind, ulong revision)
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
        Assert.True(session.TryBeginRender(new ViewportExtent(800, 450), out var request));
        return request;
    }

    private sealed unsafe class StubViewportNativeApi : IViewportNativeApi
    {
        private nint message_;

        public ViewportNativeStatus Status { get; set; } = ViewportNativeStatus.Success;

        public string Message { get; set; } = string.Empty;

        public Exception? Exception { get; set; }

        public ViewportNativePresentRequestV4 Request { get; private set; }

        public ViewportNativeDebugProxy[] DebugProxies { get; private set; } = [];

        public int ReleaseCalls { get; private set; }

        public int ShutdownCalls { get; private set; }

        public ViewportNativeStatus CreatePresentSlotV4(
            in ViewportNativePresentRequestV4 request,
            out ViewportNativePresentPacket packet)
        {
            if (Exception is not null)
            {
                throw Exception;
            }

            Request = request;
            DebugProxies = request.DebugProxyCount == 0
                ? []
                : new ReadOnlySpan<ViewportNativeDebugProxy>(
                    request.DebugProxies.ToPointer(),
                    checked((int)request.DebugProxyCount)).ToArray();
            if (Status != ViewportNativeStatus.Success)
            {
                var bytes = System.Text.Encoding.UTF8.GetBytes(Message);
                message_ = Marshal.AllocHGlobal(bytes.Length);
                Marshal.Copy(bytes, 0, message_, bytes.Length);
                packet = ViewportNativePresentPacket.Failure(Status, message_, (ulong)bytes.Length);
                return Status;
            }

            packet = ViewportNativePresentPacket.Success(
                nativePacket: (nint)11,
                imageHandle: (nint)12,
                waitSemaphoreHandle: (nint)13,
                signalSemaphoreHandle: (nint)14,
                request.WidthPixels,
                request.HeightPixels,
                ViewportNativeImageFormat.Bgra8Unorm,
                memorySizeBytes: request.WidthPixels * request.HeightPixels * 4UL,
                frameIndex: 64);
            return Status;
        }

        public void ReleasePresentPacket(ViewportNativePresentPacket packet)
        {
            ReleaseCalls++;
            if (packet.MessageUtf8 != 0)
            {
                Marshal.FreeHGlobal(packet.MessageUtf8);
                message_ = 0;
            }
        }

        public void Shutdown() => ShutdownCalls++;
    }
}
