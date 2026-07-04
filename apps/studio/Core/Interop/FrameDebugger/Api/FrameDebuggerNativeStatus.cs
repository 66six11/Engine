namespace Editor.Core.Interop.FrameDebugger.Api;

internal static class FrameDebuggerNativeStatus
{
    public const int Failure = -1;
    public const int Unavailable = 0;
    public const int Success = 1;

    public static bool IsSuccess(int status)
    {
        return status == Success;
    }
}
