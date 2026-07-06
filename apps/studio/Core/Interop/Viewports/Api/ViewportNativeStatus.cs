namespace Editor.Core.Interop.Viewports.Api;

internal static class ViewportNativeStatus
{
    public const uint Success = 0;
    public const uint InvalidArgument = 1;
    public const uint Unavailable = 2;
    public const uint UnsupportedAbi = 3;
    public const uint UnsupportedCompositionInterop = 4;
    public const uint DeviceMismatch = 5;
    public const uint UnsupportedHandleType = 6;
    public const uint RenderFailed = 7;
    public const uint DeviceLost = 8;
    public const uint InternalError = 9;

    public static bool IsSuccess(uint status)
    {
        return status == Success;
    }
}
