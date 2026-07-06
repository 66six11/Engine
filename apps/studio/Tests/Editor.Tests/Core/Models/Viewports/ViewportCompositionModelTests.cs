using System;
using Editor.Core.Models.Viewports;
using Xunit;

namespace Editor.Tests.Core.Models.Viewports;

public sealed class ViewportCompositionModelTests
{
    [Fact]
    public void Supported_snapshot_preserves_compositor_device_and_handle_metadata()
    {
        var capturedAt = DateTimeOffset.Parse("2026-07-06T10:00:00Z");

        var snapshot = new ViewportCompositionCapabilitiesSnapshot(
            new ViewportId("scene-view/main"),
            ViewportCompositionStatus.Supported,
            deviceLuid: "0000000000001234",
            deviceUuid: "00112233445566778899aabbccddeeff",
            imageHandleTypes: ["VulkanOpaqueNtHandle"],
            semaphoreHandleTypes: ["VulkanOpaqueNtHandle"],
            synchronizationCapabilities: ["Semaphores"],
            "Avalonia composition GPU interop is usable.",
            capturedAt);

        Assert.Equal("scene-view/main", snapshot.ViewportId.Value);
        Assert.Equal(ViewportCompositionStatus.Supported, snapshot.Status);
        Assert.Equal("0000000000001234", snapshot.DeviceLuid);
        Assert.Equal("00112233445566778899aabbccddeeff", snapshot.DeviceUuid);
        Assert.Contains("VulkanOpaqueNtHandle", snapshot.ImageHandleTypes);
        Assert.Contains("VulkanOpaqueNtHandle", snapshot.SemaphoreHandleTypes);
        Assert.Contains("Semaphores", snapshot.SynchronizationCapabilities);
        Assert.Equal(capturedAt, snapshot.CapturedAtUtc);
    }

    [Fact]
    public void Unsupported_snapshot_trims_message_and_copies_arrays()
    {
        var imageHandleTypes = new[] { "D3D11TextureNtHandle" };
        var snapshot = new ViewportCompositionCapabilitiesSnapshot(
            new ViewportId("scene-view/main"),
            ViewportCompositionStatus.VulkanOpaqueNtUnsupported,
            deviceLuid: null,
            deviceUuid: null,
            imageHandleTypes,
            semaphoreHandleTypes: [],
            synchronizationCapabilities: [],
            "  Vulkan opaque NT image handles are not supported.  ",
            DateTimeOffset.UnixEpoch);

        imageHandleTypes[0] = "changed";

        Assert.Equal("Vulkan opaque NT image handles are not supported.", snapshot.Message);
        Assert.Equal("D3D11TextureNtHandle", Assert.Single(snapshot.ImageHandleTypes));
    }

    [Fact]
    public void Composition_snapshot_rejects_default_id_and_unknown_status()
    {
        Assert.Throws<ArgumentException>(
            () => new ViewportCompositionCapabilitiesSnapshot(
                default,
                ViewportCompositionStatus.Supported,
                deviceLuid: null,
                deviceUuid: null,
                imageHandleTypes: [],
                semaphoreHandleTypes: [],
                synchronizationCapabilities: [],
                "supported",
                DateTimeOffset.UnixEpoch));

        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportCompositionCapabilitiesSnapshot(
                new ViewportId("scene-view/main"),
                (ViewportCompositionStatus)42,
                deviceLuid: null,
                deviceUuid: null,
                imageHandleTypes: [],
                semaphoreHandleTypes: [],
                synchronizationCapabilities: [],
                "unknown",
                DateTimeOffset.UnixEpoch));
    }

    [Fact]
    public void Native_present_snapshot_preserves_extent_format_and_frame_metadata()
    {
        var presentedAt = DateTimeOffset.Parse("2026-07-06T10:01:00Z");
        var requestedExtent = new ViewportExtent(640, 360, renderScale: 1);
        var actualExtent = new ViewportExtent(1280, 720, renderScale: 2);

        var snapshot = new ViewportNativePresentSnapshot(
            new ViewportId("scene-view/main"),
            requestedExtent,
            actualExtent,
            "  R8G8B8A8_UNORM  ",
            "  SrgbNonlinear  ",
            frameIndex: 42,
            ViewportNativePresentStatus.Success,
            "  Presented native Vulkan viewport frame.  ",
            presentedAt);

        Assert.Equal("scene-view/main", snapshot.ViewportId.Value);
        Assert.Same(requestedExtent, snapshot.RequestedExtent);
        Assert.Same(actualExtent, snapshot.ActualExtent);
        Assert.Equal("R8G8B8A8_UNORM", snapshot.FormatName);
        Assert.Equal("SrgbNonlinear", snapshot.ColorSpace);
        Assert.Equal<ulong>(42, snapshot.FrameIndex);
        Assert.Equal(ViewportNativePresentStatus.Success, snapshot.Status);
        Assert.Equal("Presented native Vulkan viewport frame.", snapshot.Message);
        Assert.Equal(presentedAt, snapshot.PresentedAtUtc);
    }

    [Fact]
    public void Native_present_snapshot_uses_status_message_when_message_is_blank()
    {
        var snapshot = new ViewportNativePresentSnapshot(
            new ViewportId("scene-view/main"),
            new ViewportExtent(640, 360, renderScale: 1),
            actualExtent: null,
            formatName: "",
            colorSpace: " ",
            frameIndex: 0,
            ViewportNativePresentStatus.DeviceLost,
            message: " ",
            DateTimeOffset.UnixEpoch);

        Assert.Null(snapshot.ActualExtent);
        Assert.Equal("Unknown", snapshot.FormatName);
        Assert.Equal("Unknown", snapshot.ColorSpace);
        Assert.Equal("DeviceLost", snapshot.Message);
    }

    [Fact]
    public void Native_present_snapshot_rejects_default_id_null_extent_and_unknown_status()
    {
        Assert.Throws<ArgumentException>(
            () => new ViewportNativePresentSnapshot(
                default,
                new ViewportExtent(640, 360, renderScale: 1),
                actualExtent: null,
                formatName: "R8G8B8A8_UNORM",
                colorSpace: "SrgbNonlinear",
                frameIndex: 0,
                ViewportNativePresentStatus.Success,
                "presented",
                DateTimeOffset.UnixEpoch));

        Assert.Throws<ArgumentNullException>(
            () => new ViewportNativePresentSnapshot(
                new ViewportId("scene-view/main"),
                requestedExtent: null!,
                actualExtent: null,
                formatName: "R8G8B8A8_UNORM",
                colorSpace: "SrgbNonlinear",
                frameIndex: 0,
                ViewportNativePresentStatus.Success,
                "presented",
                DateTimeOffset.UnixEpoch));

        Assert.Throws<ArgumentOutOfRangeException>(
            () => new ViewportNativePresentSnapshot(
                new ViewportId("scene-view/main"),
                new ViewportExtent(640, 360, renderScale: 1),
                actualExtent: null,
                formatName: "R8G8B8A8_UNORM",
                colorSpace: "SrgbNonlinear",
                frameIndex: 0,
                (ViewportNativePresentStatus)42,
                "presented",
                DateTimeOffset.UnixEpoch));
    }
}
