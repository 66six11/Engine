using System;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.EngineBridge.Viewports.Abi;

namespace Asharia.Studio.EngineBridge.Viewports;

public sealed class ViewportBridge
{
    private const ulong MaximumNativeMessageBytes = 1024 * 1024;
    private static readonly UTF8Encoding StrictUtf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);
    private readonly IViewportNativeApi nativeApi_;

    public ViewportBridge()
        : this(ViewportNativeLibraryApi.Instance)
    {
    }

    internal ViewportBridge(IViewportNativeApi nativeApi)
    {
        ArgumentNullException.ThrowIfNull(nativeApi);
        nativeApi_ = nativeApi;
    }

    public unsafe ViewportFrameAcquireResult CreatePresentSlot(
        ViewportRenderRequest request,
        ViewportDeviceCompatibility compatibility)
    {
        ArgumentNullException.ThrowIfNull(request);
        ArgumentNullException.ThrowIfNull(compatibility);

        var proxies = request.DebugProxies
            .Select(proxy => new ViewportNativeDebugProxy(
                ViewportNativeId.FromGuid(proxy.ObjectId),
                proxy.Transform))
            .ToArray();
        fixed (ViewportNativeDebugProxy* proxyPointer = proxies)
        {
            var nativeRequest = new ViewportNativePresentRequestV4(
                ViewportNativeAbiHeader.Current<ViewportNativePresentRequestV4>(),
                CreateCompatibilityRequest(compatibility),
                ViewportNativeId.FromGuid(request.SessionId.Value),
                ViewportNativeId.FromGuid(request.TargetId),
                request.TargetRevision,
                request.Sequence,
                (nint)proxyPointer,
                checked((uint)proxies.Length),
                (uint)request.Kind,
                (uint)request.TargetKind,
                request.Extent.Width,
                request.Extent.Height,
                Reserved: 0,
                ViewportNativeCamera.FromSnapshot(request.Camera));
            ViewportNativeStatus callStatus;
            ViewportNativePresentPacket packet;
            try
            {
                callStatus = nativeApi_.CreatePresentSlotV4(in nativeRequest, out packet);
            }
            catch (Exception exception) when (IsNativeBindingFailure(exception))
            {
                return Failed(
                    ViewportFrameFailureKind.NativeUnavailable,
                    $"The native viewport backend is unavailable: {exception.Message}");
            }
            catch (Exception exception)
            {
                return Failed(
                    ViewportFrameFailureKind.InternalError,
                    $"The native viewport request failed: {exception.Message}");
            }

            var packetStatus = Enum.IsDefined((ViewportNativeStatus)packet.Status)
                ? (ViewportNativeStatus)packet.Status
                : ViewportNativeStatus.InternalError;
            var status = callStatus == ViewportNativeStatus.Success ? packetStatus : callStatus;
            if (status != ViewportNativeStatus.Success || !IsValidSuccessPacket(packet, request))
            {
                string message;
                try
                {
                    message = ReadMessage(packet) ?? $"Native viewport failed with {status}.";
                }
                catch (Exception exception) when (
                    exception is ArgumentOutOfRangeException or DecoderFallbackException)
                {
                    status = ViewportNativeStatus.InternalError;
                    message = $"Native viewport returned an invalid message: {exception.Message}";
                }
                finally
                {
                    if (packet.NativePacket != 0 || packet.MessageUtf8 != 0)
                    {
                        nativeApi_.ReleasePresentPacket(packet);
                    }
                }
                return Failed(MapFailure(status), message);
            }

            var format = (ViewportNativeImageFormat)packet.Format switch
            {
                ViewportNativeImageFormat.Rgba8Unorm => ViewportFrameFormat.Rgba8Unorm,
                ViewportNativeImageFormat.Bgra8Unorm => ViewportFrameFormat.Bgra8Unorm,
                _ => throw new InvalidOperationException("Validated viewport format is missing."),
            };
            return ViewportFrameAcquireResult.Success(
                new ViewportFrameLease(nativeApi_, request, packet, format));
        }
    }

    private static ViewportNativeCompatibilityRequest CreateCompatibilityRequest(
        ViewportDeviceCompatibility compatibility) => new(
            ViewportNativeAbiHeader.Current<ViewportNativeCompatibilityRequest>(),
            (uint)ViewportNativeHandleType.VulkanOpaqueNt,
            (uint)ViewportNativeHandleType.VulkanOpaqueNt,
            compatibility.DeviceLuidLowPart,
            compatibility.DeviceLuidHighPart,
            compatibility.HasDeviceLuid ? 1U : 0U,
            compatibility.DeviceUuidLow,
            compatibility.DeviceUuidHigh,
            compatibility.HasDeviceUuid ? 1U : 0U);

    private static bool IsValidSuccessPacket(
        ViewportNativePresentPacket packet,
        ViewportRenderRequest request) =>
        packet.Header.AbiVersion == ViewportNativeAbiHeader.CurrentAbiVersion &&
        packet.Header.StructSize >= Marshal.SizeOf<ViewportNativePresentPacket>() &&
        packet.Status == (uint)ViewportNativeStatus.Success &&
        packet.NativePacket != 0 && packet.ImageHandle != 0 &&
        packet.WaitSemaphoreHandle != 0 && packet.SignalSemaphoreHandle != 0 &&
        packet.WidthPixels == request.Extent.Width &&
        packet.HeightPixels == request.Extent.Height &&
        packet.MemorySizeBytes != 0 && packet.FrameIndex != 0 &&
        packet.Format is (uint)ViewportNativeImageFormat.Rgba8Unorm or
            (uint)ViewportNativeImageFormat.Bgra8Unorm &&
        packet.MessageUtf8 == 0 && packet.MessageByteLength == 0;

    private static string? ReadMessage(ViewportNativePresentPacket packet)
    {
        if (packet.MessageUtf8 == 0 || packet.MessageByteLength == 0)
        {
            return null;
        }
        if (packet.MessageByteLength > MaximumNativeMessageBytes)
        {
            throw new ArgumentOutOfRangeException(nameof(packet));
        }

        var bytes = new byte[checked((int)packet.MessageByteLength)];
        Marshal.Copy(packet.MessageUtf8, bytes, 0, bytes.Length);
        return StrictUtf8.GetString(bytes);
    }

    private static bool IsNativeBindingFailure(Exception exception) =>
        exception is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException;

    private static ViewportFrameFailureKind MapFailure(ViewportNativeStatus status) => status switch
    {
        ViewportNativeStatus.InvalidArgument => ViewportFrameFailureKind.InvalidRequest,
        ViewportNativeStatus.Unavailable or ViewportNativeStatus.DeviceLost =>
            ViewportFrameFailureKind.NativeUnavailable,
        ViewportNativeStatus.UnsupportedAbi or
        ViewportNativeStatus.UnsupportedCompositionInterop or
        ViewportNativeStatus.UnsupportedHandleType => ViewportFrameFailureKind.UnsupportedInterop,
        ViewportNativeStatus.DeviceMismatch => ViewportFrameFailureKind.DeviceMismatch,
        ViewportNativeStatus.RenderFailed => ViewportFrameFailureKind.RenderFailed,
        _ => ViewportFrameFailureKind.InternalError,
    };

    private static ViewportFrameAcquireResult Failed(
        ViewportFrameFailureKind kind,
        string message) => ViewportFrameAcquireResult.Failed(new ViewportFrameFailure(kind, message));
}
