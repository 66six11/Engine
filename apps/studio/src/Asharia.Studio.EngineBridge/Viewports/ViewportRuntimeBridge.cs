using System;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using Asharia.Studio.EngineBridge.Viewports.Abi;

namespace Asharia.Studio.EngineBridge.Viewports;

public sealed class ViewportRuntimeBridge
{
    private const ulong MaximumNativeMessageBytes = 1024 * 1024;
    private static readonly UTF8Encoding StrictUtf8 = new(
        encoderShouldEmitUTF8Identifier: false,
        throwOnInvalidBytes: true);
    private readonly IViewportNativeApi nativeApi_;

    public ViewportRuntimeBridge()
        : this(ViewportNativeLibraryApi.Instance)
    {
    }

    internal ViewportRuntimeBridge(IViewportNativeApi nativeApi)
    {
        ArgumentNullException.ThrowIfNull(nativeApi);
        nativeApi_ = nativeApi;
    }

    public Task<ViewportFrameFailure?> WarmUpAsync() => Task.Run(WarmUp);

    private ViewportFrameFailure? WarmUp()
    {
        var request = new ViewportNativeCompatibilityRequest(
            ViewportNativeAbiHeader.Current<ViewportNativeCompatibilityRequest>(),
            (uint)ViewportNativeHandleType.VulkanOpaqueNt,
            (uint)ViewportNativeHandleType.VulkanOpaqueNt,
            0,
            0,
            0,
            0,
            0,
            0);
        ViewportNativeCompatibilityResult result = default;
        try
        {
            ViewportNativeStatus callStatus;
            try
            {
                callStatus = nativeApi_.QueryCompositionCompatibility(in request, out result);
            }
            catch (Exception exception) when (IsNativeBindingFailure(exception))
            {
                return new ViewportFrameFailure(
                    ViewportFrameFailureKind.NativeUnavailable,
                    $"The native viewport backend is unavailable: {exception.Message}");
            }
            catch (Exception exception)
            {
                return new ViewportFrameFailure(
                    ViewportFrameFailureKind.InternalError,
                    $"The native viewport warm-up failed: {exception.Message}");
            }

            var resultStatus = Enum.IsDefined((ViewportNativeStatus)result.Status)
                ? (ViewportNativeStatus)result.Status
                : ViewportNativeStatus.InternalError;
            var status = callStatus == ViewportNativeStatus.Success ? resultStatus : callStatus;
            if (status == ViewportNativeStatus.Success && IsValidSuccessResult(result))
            {
                return null;
            }

            string message;
            try
            {
                message = ReadMessage(result) ?? $"Native viewport warm-up failed with {status}.";
            }
            catch (Exception exception) when (
                exception is ArgumentOutOfRangeException or DecoderFallbackException)
            {
                status = ViewportNativeStatus.InternalError;
                message = $"Native viewport warm-up returned an invalid message: {exception.Message}";
            }

            return new ViewportFrameFailure(MapFailure(status), message);
        }
        finally
        {
            if (result.Header.StructSize != 0 || result.MessageUtf8 != 0)
            {
                nativeApi_.ReleaseCompatibilityResult(result);
            }
        }
    }

    public void Shutdown()
    {
        try
        {
            nativeApi_.Shutdown();
        }
        catch (Exception exception) when (
            exception is DllNotFoundException or
                EntryPointNotFoundException or
                BadImageFormatException)
        {
            // No runtime could have been admitted when its binding was unavailable.
        }
    }

    private static bool IsValidSuccessResult(ViewportNativeCompatibilityResult result) =>
        result.Header.AbiVersion == ViewportNativeAbiHeader.CurrentAbiVersion &&
        result.Header.StructSize >= Marshal.SizeOf<ViewportNativeCompatibilityResult>() &&
        result.Status == (uint)ViewportNativeStatus.Success &&
        result.ProducedImageHandleType == (uint)ViewportNativeHandleType.VulkanOpaqueNt &&
        result.ProducedSemaphoreHandleType == (uint)ViewportNativeHandleType.VulkanOpaqueNt;

    private static string? ReadMessage(ViewportNativeCompatibilityResult result)
    {
        if (result.MessageUtf8 == 0 || result.MessageByteLength == 0)
        {
            return null;
        }
        if (result.MessageByteLength > MaximumNativeMessageBytes)
        {
            throw new ArgumentOutOfRangeException(nameof(result));
        }

        var bytes = new byte[checked((int)result.MessageByteLength)];
        Marshal.Copy(result.MessageUtf8, bytes, 0, bytes.Length);
        return StrictUtf8.GetString(bytes);
    }

    private static bool IsNativeBindingFailure(Exception exception) =>
        exception is DllNotFoundException or EntryPointNotFoundException or BadImageFormatException;

    private static ViewportFrameFailureKind MapFailure(ViewportNativeStatus status) => status switch
    {
        ViewportNativeStatus.InvalidArgument => ViewportFrameFailureKind.InvalidRequest,
        ViewportNativeStatus.Backpressure => ViewportFrameFailureKind.Backpressure,
        ViewportNativeStatus.Unavailable or ViewportNativeStatus.DeviceLost =>
            ViewportFrameFailureKind.NativeUnavailable,
        ViewportNativeStatus.UnsupportedAbi or
        ViewportNativeStatus.UnsupportedCompositionInterop or
        ViewportNativeStatus.UnsupportedHandleType => ViewportFrameFailureKind.UnsupportedInterop,
        ViewportNativeStatus.FeatureUnavailable => ViewportFrameFailureKind.UnsupportedFeature,
        ViewportNativeStatus.DeviceMismatch => ViewportFrameFailureKind.DeviceMismatch,
        ViewportNativeStatus.RenderFailed => ViewportFrameFailureKind.RenderFailed,
        _ => ViewportFrameFailureKind.InternalError,
    };
}
