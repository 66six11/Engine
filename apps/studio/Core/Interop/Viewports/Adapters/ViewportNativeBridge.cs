using System;
using System.Globalization;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using Editor.Core.Interop.Viewports.Api;
using Editor.Core.Models.Viewports;

namespace Editor.Core.Interop.Viewports.Adapters;

internal sealed class ViewportNativeBridge
{
    private const string VulkanOpaqueNtHandleType = "VulkanOpaqueNtHandle";
    private readonly IViewportNativeApi api_;

    public ViewportNativeBridge()
        : this(ViewportNativeLibraryApi.Instance)
    {
    }

    internal ViewportNativeBridge(IViewportNativeApi api)
    {
        ArgumentNullException.ThrowIfNull(api);

        api_ = api;
    }

    public ViewportNativePresentSnapshot QueryCompositionCompatibility(
        ViewportCompositionCapabilitiesSnapshot compositionCapabilities,
        ViewportExtent requestedExtent)
    {
        ArgumentNullException.ThrowIfNull(compositionCapabilities);
        ArgumentNullException.ThrowIfNull(requestedExtent);

        if (compositionCapabilities.Status != ViewportCompositionStatus.Supported)
        {
            return CreateSnapshot(
                compositionCapabilities.ViewportId,
                requestedExtent,
                ViewportNativePresentStatus.UnsupportedCompositionInterop,
                compositionCapabilities.Message);
        }

        var request = CreateCompatibilityRequest(compositionCapabilities);
        var result = ViewportNativeCompatibilityResult.CreateForCall();
        uint queryStatus;
        try
        {
            queryStatus = api_.QueryCompositionCompatibility(request, ref result);
        }
        catch (Exception ex) when (IsNativeBindingException(ex))
        {
            return CreateSnapshot(
                compositionCapabilities.ViewportId,
                requestedExtent,
                ViewportNativePresentStatus.RenderProducerUnavailable,
                CreateNativeBackendUnavailableMessage(ex));
        }

        try
        {
            var status = ViewportNativeStatus.IsSuccess(queryStatus)
                ? result.Status
                : queryStatus;
            return CreateSnapshot(
                compositionCapabilities.ViewportId,
                requestedExtent,
                MapStatus(status),
                CopyMessage(result) ?? MapStatus(status).ToString());
        }
        finally
        {
            if (result.MessageUtf8 != IntPtr.Zero)
            {
                api_.ReleaseCompatibilityResult(result);
            }
        }
    }

    public void ReleasePresentPacket(ViewportNativePresentPacket packet)
    {
        api_.ReleasePresentPacket(packet);
    }

    public ViewportNativePresentSnapshot SnapshotAndReleasePresentPacket(
        ViewportNativePresentPacket packet,
        ViewportId viewportId,
        ViewportExtent requestedExtent)
    {
        ArgumentNullException.ThrowIfNull(requestedExtent);

        var status = MapStatus(packet.Status);
        var message = CopyMessage(packet) ?? status.ToString();
        try
        {
            return packet.ToSnapshot(
                viewportId,
                requestedExtent,
                status,
                message);
        }
        finally
        {
            ReleasePresentPacketIfNeeded(packet);
        }
    }

    public ViewportNativePresentPacket AcquirePresentPacket(
        ViewportCompositionCapabilitiesSnapshot compositionCapabilities,
        ViewportExtent requestedExtent)
    {
        ArgumentNullException.ThrowIfNull(compositionCapabilities);
        ArgumentNullException.ThrowIfNull(requestedExtent);

        if (compositionCapabilities.Status != ViewportCompositionStatus.Supported)
        {
            return CreatePresentPacket(
                ViewportNativeStatus.UnsupportedCompositionInterop,
                requestedExtent);
        }

        var request = new ViewportNativePresentRequest(
            CreateCompatibilityRequest(compositionCapabilities),
            checked((uint)requestedExtent.WidthPixels),
            checked((uint)requestedExtent.HeightPixels));
        var packet = ViewportNativePresentPacket.CreateForCall();
        try
        {
            _ = api_.AcquirePresentPacket(request, ref packet);
        }
        catch (Exception ex) when (IsNativeBindingException(ex))
        {
            return CreatePresentPacket(
                ViewportNativeStatus.Unavailable,
                requestedExtent);
        }

        return packet;
    }

    public void Shutdown()
    {
        api_.Shutdown();
    }

    private void ReleasePresentPacketIfNeeded(ViewportNativePresentPacket packet)
    {
        if (packet.NativePacket == IntPtr.Zero && packet.MessageUtf8 == IntPtr.Zero)
        {
            return;
        }

        ReleasePresentPacket(packet);
    }

    private static ViewportNativeCompatibilityRequest CreateCompatibilityRequest(
        ViewportCompositionCapabilitiesSnapshot compositionCapabilities)
    {
        var hasLuid = TryParseDeviceBytes(compositionCapabilities.DeviceLuid, expectedByteLength: 8, out var luidBytes);
        var hasUuid = TryParseDeviceBytes(compositionCapabilities.DeviceUuid, expectedByteLength: 16, out var uuidBytes);

        return new ViewportNativeCompatibilityRequest(
            HasHandleType(compositionCapabilities.ImageHandleTypes)
                ? ViewportNativeHandleTypes.VulkanOpaqueNt
                : ViewportNativeHandleTypes.Unknown,
            HasHandleType(compositionCapabilities.SemaphoreHandleTypes)
                ? ViewportNativeHandleTypes.VulkanOpaqueNt
                : ViewportNativeHandleTypes.Unknown,
            hasLuid ? BitConverter.ToUInt32(luidBytes, startIndex: 0) : 0UL,
            hasLuid ? BitConverter.ToInt32(luidBytes, startIndex: 4) : 0,
            hasLuid ? 1U : 0U,
            hasUuid ? BitConverter.ToUInt64(uuidBytes, startIndex: 0) : 0UL,
            hasUuid ? BitConverter.ToUInt64(uuidBytes, startIndex: 8) : 0UL,
            hasUuid ? 1U : 0U);
    }

    private static bool HasHandleType(System.Collections.Generic.IReadOnlyList<string> handleTypes)
    {
        return handleTypes.Contains(VulkanOpaqueNtHandleType, StringComparer.Ordinal);
    }

    private static bool TryParseDeviceBytes(
        string? value,
        int expectedByteLength,
        out byte[] bytes)
    {
        bytes = [];
        if (string.IsNullOrWhiteSpace(value))
        {
            return false;
        }

        var trimmed = value.Trim();
        if (trimmed.Length != expectedByteLength * 2)
        {
            return false;
        }

        bytes = new byte[expectedByteLength];
        for (var index = 0; index < bytes.Length; index++)
        {
            if (!byte.TryParse(
                    trimmed.AsSpan(index * 2, 2),
                    NumberStyles.HexNumber,
                    CultureInfo.InvariantCulture,
                    out bytes[index]))
            {
                bytes = [];
                return false;
            }
        }

        return true;
    }

    private static bool IsNativeBindingException(Exception ex)
    {
        return ex is DllNotFoundException
            or EntryPointNotFoundException
            or BadImageFormatException;
    }

    private static string CreateNativeBackendUnavailableMessage(Exception ex)
    {
        return string.IsNullOrWhiteSpace(ex.Message)
            ? "Native viewport backend is unavailable."
            : $"Native viewport backend is unavailable: {ex.Message}";
    }

    private static string? CopyMessage(ViewportNativeCompatibilityResult result)
    {
        return CopyMessage(result.MessageUtf8, result.MessageByteLength);
    }

    private static string? CopyMessage(ViewportNativePresentPacket packet)
    {
        return CopyMessage(packet.MessageUtf8, packet.MessageByteLength);
    }

    private static string? CopyMessage(IntPtr messageUtf8, ulong messageByteLength)
    {
        if (messageUtf8 == IntPtr.Zero || messageByteLength == 0UL)
        {
            return null;
        }

        if (messageByteLength > int.MaxValue)
        {
            return null;
        }

        var bytes = new byte[(int)messageByteLength];
        Marshal.Copy(messageUtf8, bytes, 0, bytes.Length);
        return Encoding.UTF8.GetString(bytes);
    }

    private static ViewportNativePresentSnapshot CreateSnapshot(
        ViewportId viewportId,
        ViewportExtent requestedExtent,
        ViewportNativePresentStatus status,
        string message)
    {
        return new ViewportNativePresentSnapshot(
            viewportId,
            requestedExtent,
            actualExtent: null,
            formatName: "Unknown",
            colorSpace: "Unknown",
            frameIndex: 0UL,
            status,
            message,
            DateTimeOffset.UtcNow);
    }

    private static ViewportNativePresentPacket CreatePresentPacket(
        uint status,
        ViewportExtent requestedExtent)
    {
        return new ViewportNativePresentPacket(
            new ViewportNativeAbiHeader(ViewportNativePresentPacket.CurrentStructSize),
            status,
            IntPtr.Zero,
            IntPtr.Zero,
            IntPtr.Zero,
            IntPtr.Zero,
            checked((uint)requestedExtent.WidthPixels),
            checked((uint)requestedExtent.HeightPixels),
            ViewportNativeImageFormat.Unknown,
            memorySizeBytes: 0UL,
            frameIndex: 0UL,
            IntPtr.Zero,
            messageByteLength: 0UL);
    }

    private static ViewportNativePresentStatus MapStatus(uint status)
    {
        return status switch
        {
            ViewportNativeStatus.Success => ViewportNativePresentStatus.Success,
            ViewportNativeStatus.UnsupportedAbi => ViewportNativePresentStatus.UnsupportedAbi,
            ViewportNativeStatus.UnsupportedCompositionInterop => ViewportNativePresentStatus.UnsupportedCompositionInterop,
            ViewportNativeStatus.DeviceMismatch => ViewportNativePresentStatus.DeviceMismatch,
            ViewportNativeStatus.UnsupportedHandleType => ViewportNativePresentStatus.UnsupportedHandleType,
            ViewportNativeStatus.RenderFailed => ViewportNativePresentStatus.RenderFailed,
            ViewportNativeStatus.DeviceLost => ViewportNativePresentStatus.DeviceLost,
            ViewportNativeStatus.InternalError => ViewportNativePresentStatus.RenderFailed,
            _ => ViewportNativePresentStatus.RenderProducerUnavailable,
        };
    }
}
