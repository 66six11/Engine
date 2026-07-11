using System;
using System.Runtime.InteropServices;
using System.Text;
using Asharia.Editor.Viewports;
using Editor.Core.Interop.Viewports.Adapters;
using Editor.Core.Interop.Viewports.Api;
using Editor.Core.Models.Viewports;
using Xunit;

namespace Editor.Tests.Core.Interop.Viewports.Adapters;

public sealed class ViewportNativeBridgeTests
{
    [Fact]
    public void Query_composition_compatibility_sends_expected_abi_and_handle_request()
    {
        using var api = new StubViewportNativeApi
        {
            QueryResultStatus = ViewportNativeStatus.Success,
            ResultStatus = ViewportNativeStatus.Success,
            ResultMessage = "Composition device is compatible.",
        };
        var bridge = new ViewportNativeBridge(api);

        var snapshot = bridge.QueryCompositionCompatibility(
            CreateCompositionCapabilities(),
            CreateRequestedExtent());

        Assert.Equal(ViewportNativePresentStatus.Success, snapshot.Status);
        Assert.Equal("Composition device is compatible.", snapshot.Message);
        Assert.Equal(ViewportNativeAbiHeader.ExpectedAbiVersion, api.LastRequest.Header.AbiVersion);
        Assert.Equal(ViewportNativeCompatibilityRequest.CurrentStructSize, api.LastRequest.Header.StructSize);
        Assert.Equal(ViewportNativeHandleTypes.VulkanOpaqueNt, api.LastRequest.ImageHandleType);
        Assert.Equal(ViewportNativeHandleTypes.VulkanOpaqueNt, api.LastRequest.SemaphoreHandleType);
        Assert.Equal(1U, api.LastRequest.HasDeviceLuid);
        Assert.Equal(1U, api.LastRequest.HasDeviceUuid);
        Assert.Equal(1, api.ReleaseCompatibilityResultCalls);
    }

    [Theory]
    [InlineData(ViewportNativeStatus.UnsupportedAbi, ViewportNativePresentStatus.UnsupportedAbi)]
    [InlineData(ViewportNativeStatus.DeviceMismatch, ViewportNativePresentStatus.DeviceMismatch)]
    [InlineData(ViewportNativeStatus.Unavailable, ViewportNativePresentStatus.RenderProducerUnavailable)]
    public void Query_composition_compatibility_maps_native_status_to_present_status(
        uint nativeStatus,
        ViewportNativePresentStatus expectedStatus)
    {
        using var api = new StubViewportNativeApi
        {
            QueryResultStatus = nativeStatus,
            ResultStatus = nativeStatus,
            ResultMessage = "native status message",
        };
        var bridge = new ViewportNativeBridge(api);

        var snapshot = bridge.QueryCompositionCompatibility(
            CreateCompositionCapabilities(),
            CreateRequestedExtent());

        Assert.Equal(expectedStatus, snapshot.Status);
        Assert.Equal("native status message", snapshot.Message);
        Assert.Equal(1, api.ReleaseCompatibilityResultCalls);
    }

    [Fact]
    public void Query_composition_compatibility_releases_native_message_buffer_once()
    {
        using var api = new StubViewportNativeApi
        {
            QueryResultStatus = ViewportNativeStatus.DeviceMismatch,
            ResultStatus = ViewportNativeStatus.DeviceMismatch,
            ResultMessage = "device mismatch",
        };
        var bridge = new ViewportNativeBridge(api);

        _ = bridge.QueryCompositionCompatibility(
            CreateCompositionCapabilities(),
            CreateRequestedExtent());

        Assert.Equal(1, api.ReleaseCompatibilityResultCalls);
        Assert.NotEqual(IntPtr.Zero, api.LastReleasedCompatibilityResult.MessageUtf8);
        Assert.Equal((ulong)Encoding.UTF8.GetByteCount("device mismatch"), api.LastReleasedCompatibilityResult.MessageByteLength);
    }

    [Fact]
    public void Query_composition_compatibility_returns_unavailable_snapshot_when_native_library_is_missing()
    {
        using var api = new StubViewportNativeApi
        {
            QueryException = new DllNotFoundException("missing editor_native"),
        };
        var bridge = new ViewportNativeBridge(api);

        var snapshot = bridge.QueryCompositionCompatibility(
            CreateCompositionCapabilities(),
            CreateRequestedExtent());

        Assert.Equal(ViewportNativePresentStatus.RenderProducerUnavailable, snapshot.Status);
        Assert.Contains("missing editor_native", snapshot.Message, StringComparison.Ordinal);
        Assert.Equal(0, api.ReleaseCompatibilityResultCalls);
    }

    [Fact]
    public void Release_present_packet_forwards_packet_to_native_api()
    {
        using var api = new StubViewportNativeApi();
        var bridge = new ViewportNativeBridge(api);
        var nativePacket = new IntPtr(0x1234);
        var packet = new ViewportNativePresentPacket(
            new ViewportNativeAbiHeader(ViewportNativePresentPacket.CurrentStructSize),
            ViewportNativeStatus.Success,
            nativePacket,
            new IntPtr(0x1000),
            new IntPtr(0x2000),
            new IntPtr(0x3000),
            widthPixels: 320U,
            heightPixels: 180U,
            ViewportNativeImageFormat.Rgba8Unorm,
            memorySizeBytes: 320UL * 180UL * 4UL,
            frameIndex: 1UL,
            IntPtr.Zero,
            messageByteLength: 0UL);

        bridge.ReleasePresentPacket(packet);

        Assert.Equal(1, api.ReleasePresentPacketCalls);
        Assert.Equal(ViewportNativePresentPacket.CurrentStructSize, api.LastReleasedPresentPacket.Header.StructSize);
        Assert.Equal(nativePacket, api.LastReleasedPresentPacket.NativePacket);
    }

    [Fact]
    public void Acquire_present_packet_sends_compatibility_and_extent_to_native_api()
    {
        using var api = new StubViewportNativeApi
        {
            AcquiredPacketStatus = ViewportNativeStatus.Success,
        };
        var bridge = new ViewportNativeBridge(api);
        var requestedExtent = CreateRequestedExtent();

        var packet = bridge.AcquirePresentPacket(
            CreateCompositionCapabilities(),
            requestedExtent);

        Assert.Equal(ViewportNativeStatus.Success, packet.Status);
        Assert.Equal(ViewportNativeAbiHeader.ExpectedAbiVersion, api.LastPresentRequest.Header.AbiVersion);
        Assert.Equal(ViewportNativePresentRequest.CurrentStructSize, api.LastPresentRequest.Header.StructSize);
        Assert.Equal(ViewportNativeHandleTypes.VulkanOpaqueNt, api.LastPresentRequest.Compatibility.ImageHandleType);
        Assert.Equal(ViewportNativeHandleTypes.VulkanOpaqueNt, api.LastPresentRequest.Compatibility.SemaphoreHandleType);
        Assert.Equal((uint)requestedExtent.WidthPixels, api.LastPresentRequest.WidthPixels);
        Assert.Equal((uint)requestedExtent.HeightPixels, api.LastPresentRequest.HeightPixels);
    }

    [Fact]
    public void Acquire_present_packet_returns_native_failure_packet_for_caller_release()
    {
        using var api = new StubViewportNativeApi
        {
            AcquiredPacketStatus = ViewportNativeStatus.RenderFailed,
        };
        var bridge = new ViewportNativeBridge(api);

        var packet = bridge.AcquirePresentPacket(
            CreateCompositionCapabilities(),
            CreateRequestedExtent());

        Assert.Equal(ViewportNativeStatus.RenderFailed, packet.Status);
        Assert.Equal(new IntPtr(0x1234), packet.NativePacket);
        Assert.Equal(0, api.ReleasePresentPacketCalls);
    }

    [Fact]
    public void Acquire_present_packet_returns_unavailable_packet_when_native_library_is_missing()
    {
        using var api = new StubViewportNativeApi
        {
            AcquireException = new DllNotFoundException("missing editor_native"),
        };
        var bridge = new ViewportNativeBridge(api);
        var requestedExtent = CreateRequestedExtent();

        var packet = bridge.AcquirePresentPacket(
            CreateCompositionCapabilities(),
            requestedExtent);

        Assert.Equal(ViewportNativeStatus.Unavailable, packet.Status);
        Assert.Equal(IntPtr.Zero, packet.NativePacket);
        Assert.Equal((uint)requestedExtent.WidthPixels, packet.WidthPixels);
        Assert.Equal((uint)requestedExtent.HeightPixels, packet.HeightPixels);
        Assert.Equal(0, api.ReleasePresentPacketCalls);
    }

    [Fact]
    public void Snapshot_and_release_present_packet_does_not_release_synthetic_unavailable_packet()
    {
        using var api = new StubViewportNativeApi
        {
            AcquireException = new DllNotFoundException("missing editor_native"),
        };
        var bridge = new ViewportNativeBridge(api);
        var requestedExtent = CreateRequestedExtent();
        var packet = bridge.AcquirePresentPacket(
            CreateCompositionCapabilities(),
            requestedExtent);

        var snapshot = bridge.SnapshotAndReleasePresentPacket(
            packet,
            new ViewportId("scene-view/main"),
            requestedExtent);

        Assert.Equal(ViewportNativePresentStatus.RenderProducerUnavailable, snapshot.Status);
        Assert.Equal(0, api.ReleasePresentPacketCalls);
    }

    [Fact]
    public void Snapshot_and_release_present_packet_maps_status_copies_message_and_releases_packet()
    {
        using var api = new StubViewportNativeApi
        {
            AcquiredPacketStatus = ViewportNativeStatus.RenderFailed,
            PresentPacketMessage = "render failed",
        };
        var bridge = new ViewportNativeBridge(api);
        var requestedExtent = CreateRequestedExtent();
        var packet = bridge.AcquirePresentPacket(
            CreateCompositionCapabilities(),
            requestedExtent);

        var snapshot = bridge.SnapshotAndReleasePresentPacket(
            packet,
            new ViewportId("scene-view/main"),
            requestedExtent);

        Assert.Equal(ViewportNativePresentStatus.RenderFailed, snapshot.Status);
        Assert.Equal("render failed", snapshot.Message);
        Assert.Equal("B8G8R8A8_UNORM", snapshot.FormatName);
        Assert.Equal(1, api.ReleasePresentPacketCalls);
        Assert.Equal(packet.NativePacket, api.LastReleasedPresentPacket.NativePacket);
    }

    [Fact]
    public void Shutdown_forwards_to_native_api_once()
    {
        using var api = new StubViewportNativeApi();
        var bridge = new ViewportNativeBridge(api);

        bridge.Shutdown();

        Assert.Equal(1, api.ShutdownCalls);
    }

    [Fact]
    public void Shutdown_ignores_missing_native_library()
    {
        using var api = new StubViewportNativeApi
        {
            ShutdownException = new DllNotFoundException("missing editor_native"),
        };
        var bridge = new ViewportNativeBridge(api);

        bridge.Shutdown();

        Assert.Equal(1, api.ShutdownCalls);
    }

    [Fact]
    public void Shutdown_is_idempotent()
    {
        using var api = new StubViewportNativeApi();
        var bridge = new ViewportNativeBridge(api);

        bridge.Shutdown();
        bridge.Shutdown();

        Assert.Equal(1, api.ShutdownCalls);
    }

    [Fact]
    public void Release_present_packet_does_not_call_native_api_after_shutdown()
    {
        using var api = new StubViewportNativeApi();
        var bridge = new ViewportNativeBridge(api);
        var packet = new ViewportNativePresentPacket(
            new ViewportNativeAbiHeader(ViewportNativePresentPacket.CurrentStructSize),
            ViewportNativeStatus.Success,
            new IntPtr(0x1234),
            new IntPtr(0x1000),
            new IntPtr(0x2000),
            new IntPtr(0x3000),
            widthPixels: 320U,
            heightPixels: 180U,
            ViewportNativeImageFormat.Rgba8Unorm,
            memorySizeBytes: 320UL * 180UL * 4UL,
            frameIndex: 1UL,
            IntPtr.Zero,
            messageByteLength: 0UL);

        bridge.Shutdown();
        bridge.ReleasePresentPacket(packet);

        Assert.Equal(0, api.ReleasePresentPacketCalls);
    }

    [Fact]
    public void Present_packet_creates_avalonia_image_properties_for_bgra_unorm()
    {
        var packet = new ViewportNativePresentPacket(
            new ViewportNativeAbiHeader(ViewportNativePresentPacket.CurrentStructSize),
            ViewportNativeStatus.Success,
            new IntPtr(0x1234),
            new IntPtr(0x1000),
            new IntPtr(0x2000),
            new IntPtr(0x3000),
            widthPixels: 640U,
            heightPixels: 360U,
            ViewportNativeImageFormat.Bgra8Unorm,
            memorySizeBytes: 640UL * 360UL * 4UL,
            frameIndex: 7UL,
            IntPtr.Zero,
            messageByteLength: 0UL);

        var properties = packet.CreateAvaloniaImageProperties();

        Assert.Equal(640, properties.Width);
        Assert.Equal(360, properties.Height);
        Assert.Equal(Avalonia.Platform.PlatformGraphicsExternalImageFormat.B8G8R8A8UNorm, properties.Format);
        Assert.Equal(0UL, properties.MemoryOffset);
        Assert.Equal(640UL * 360UL * 4UL, properties.MemorySize);
        Assert.True(properties.TopLeftOrigin);
    }

    private static ViewportCompositionCapabilitiesSnapshot CreateCompositionCapabilities()
    {
        return new ViewportCompositionCapabilitiesSnapshot(
            new ViewportId("scene-view/main"),
            ViewportCompositionStatus.Supported,
            deviceLuid: "0102030405060708",
            deviceUuid: "00112233445566778899aabbccddeeff",
            imageHandleTypes: ["VulkanOpaqueNtHandle"],
            semaphoreHandleTypes: ["VulkanOpaqueNtHandle"],
            synchronizationCapabilities: ["Semaphores"],
            "supported",
            DateTimeOffset.UnixEpoch);
    }

    private static ViewportExtent CreateRequestedExtent()
    {
        return new ViewportExtent(640, 360, renderScale: 1);
    }

    private sealed class StubViewportNativeApi : IViewportNativeApi, IDisposable
    {
        private IntPtr allocatedMessage_;
        private IntPtr allocatedPresentMessage_;

        public uint QueryResultStatus { get; init; } = ViewportNativeStatus.Success;

        public uint ResultStatus { get; init; } = ViewportNativeStatus.Success;

        public string ResultMessage { get; init; } = "ok";

        public uint AcquiredPacketStatus { get; init; } = ViewportNativeStatus.Unavailable;

        public string PresentPacketMessage { get; init; } = string.Empty;

        public Exception? QueryException { get; init; }

        public Exception? AcquireException { get; init; }

        public Exception? ShutdownException { get; init; }

        public ViewportNativeCompatibilityRequest LastRequest { get; private set; }

        public ViewportNativePresentRequest LastPresentRequest { get; private set; }

        public int ReleaseCompatibilityResultCalls { get; private set; }

        public ViewportNativeCompatibilityResult LastReleasedCompatibilityResult { get; private set; }

        public int ReleasePresentPacketCalls { get; private set; }

        public ViewportNativePresentPacket LastReleasedPresentPacket { get; private set; }

        public int ShutdownCalls { get; private set; }

        public uint QueryCompositionCompatibility(
            in ViewportNativeCompatibilityRequest request,
            ref ViewportNativeCompatibilityResult result)
        {
            if (QueryException is not null)
            {
                throw QueryException;
            }

            LastRequest = request;
            var messageBytes = Encoding.UTF8.GetBytes(ResultMessage);
            allocatedMessage_ = Marshal.AllocHGlobal(messageBytes.Length);
            Marshal.Copy(messageBytes, 0, allocatedMessage_, messageBytes.Length);
            result = new ViewportNativeCompatibilityResult(
                new ViewportNativeAbiHeader(ViewportNativeCompatibilityResult.CurrentStructSize),
                ResultStatus,
                ViewportNativeHandleTypes.VulkanOpaqueNt,
                ViewportNativeHandleTypes.VulkanOpaqueNt,
                nativeDeviceVendorId: 0x10de,
                nativeDeviceId: 0x2684,
                nativeDeviceUuidLow: 0x7766554433221100UL,
                nativeDeviceUuidHigh: 0xffeeddccbbaa9988UL,
                allocatedMessage_,
                (ulong)messageBytes.Length);
            return QueryResultStatus;
        }

        public void ReleaseCompatibilityResult(ViewportNativeCompatibilityResult result)
        {
            ReleaseCompatibilityResultCalls++;
            LastReleasedCompatibilityResult = result;
            if (result.MessageUtf8 != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(result.MessageUtf8);
                if (result.MessageUtf8 == allocatedMessage_)
                {
                    allocatedMessage_ = IntPtr.Zero;
                }
            }
        }

        public uint AcquirePresentPacket(
            in ViewportNativePresentRequest request,
            ref ViewportNativePresentPacket packet)
        {
            if (AcquireException is not null)
            {
                throw AcquireException;
            }

            LastPresentRequest = request;
            LastRequest = request.Compatibility;
            var messageBytes = Encoding.UTF8.GetBytes(PresentPacketMessage);
            if (messageBytes.Length > 0)
            {
                allocatedPresentMessage_ = Marshal.AllocHGlobal(messageBytes.Length);
                Marshal.Copy(messageBytes, 0, allocatedPresentMessage_, messageBytes.Length);
            }

            packet = new ViewportNativePresentPacket(
                new ViewportNativeAbiHeader(ViewportNativePresentPacket.CurrentStructSize),
                AcquiredPacketStatus,
                new IntPtr(0x1234),
                new IntPtr(0x1000),
                new IntPtr(0x2000),
                new IntPtr(0x3000),
                request.WidthPixels,
                request.HeightPixels,
                ViewportNativeImageFormat.Bgra8Unorm,
                memorySizeBytes: (ulong)request.WidthPixels * request.HeightPixels * 4UL,
                frameIndex: 9UL,
                allocatedPresentMessage_,
                (ulong)messageBytes.Length);
            return AcquiredPacketStatus;
        }

        public void ReleasePresentPacket(ViewportNativePresentPacket packet)
        {
            ReleasePresentPacketCalls++;
            LastReleasedPresentPacket = packet;
            if (packet.MessageUtf8 != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(packet.MessageUtf8);
                if (packet.MessageUtf8 == allocatedPresentMessage_)
                {
                    allocatedPresentMessage_ = IntPtr.Zero;
                }
            }
        }

        public void Shutdown()
        {
            ShutdownCalls++;
            if (ShutdownException is not null)
            {
                throw ShutdownException;
            }
        }

        public void Dispose()
        {
            if (allocatedMessage_ != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(allocatedMessage_);
                allocatedMessage_ = IntPtr.Zero;
            }

            if (allocatedPresentMessage_ != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(allocatedPresentMessage_);
                allocatedPresentMessage_ = IntPtr.Zero;
            }
        }
    }
}
