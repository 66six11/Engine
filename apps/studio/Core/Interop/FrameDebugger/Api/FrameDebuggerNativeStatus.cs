namespace Editor.Core.Interop.FrameDebugger.Api;

internal static class FrameDebuggerNativeStatus
{
    public const uint Success = 0;
    public const uint InvalidArgument = 1;
    public const uint Unavailable = 2;
    public const uint UnsupportedAbi = 3;
    public const uint InternalError = 4;

    public static bool IsSuccess(uint status)
    {
        return status == Success;
    }
}
