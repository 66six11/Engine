using System;
using System.Runtime.InteropServices;
using System.Text;
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

        public uint QueryResultStatus { get; init; } = ViewportNativeStatus.Success;

        public uint ResultStatus { get; init; } = ViewportNativeStatus.Success;

        public string ResultMessage { get; init; } = "ok";

        public ViewportNativeCompatibilityRequest LastRequest { get; private set; }

        public int ReleaseCompatibilityResultCalls { get; private set; }

        public ViewportNativeCompatibilityResult LastReleasedCompatibilityResult { get; private set; }

        public int ReleasePresentPacketCalls { get; private set; }

        public ViewportNativePresentPacket LastReleasedPresentPacket { get; private set; }

        public uint QueryCompositionCompatibility(
            in ViewportNativeCompatibilityRequest request,
            ref ViewportNativeCompatibilityResult result)
        {
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
            LastRequest = request.Compatibility;
            packet = ViewportNativePresentPacket.CreateForCall();
            return ViewportNativeStatus.Unavailable;
        }

        public void ReleasePresentPacket(ViewportNativePresentPacket packet)
        {
            ReleasePresentPacketCalls++;
            LastReleasedPresentPacket = packet;
        }

        public void Shutdown()
        {
        }

        public void Dispose()
        {
            if (allocatedMessage_ != IntPtr.Zero)
            {
                Marshal.FreeHGlobal(allocatedMessage_);
                allocatedMessage_ = IntPtr.Zero;
            }
        }
    }
}
