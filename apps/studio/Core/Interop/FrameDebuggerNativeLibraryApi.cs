using System;
using System.Runtime.InteropServices;

namespace Editor.Core.Interop;

internal sealed class FrameDebuggerNativeLibraryApi : IFrameDebuggerNativeApi
{
    public static FrameDebuggerNativeLibraryApi Instance { get; } = new();

    private FrameDebuggerNativeLibraryApi()
    {
    }

    public int AcquireSnapshot(out FrameDebuggerNativeSnapshotBuffer snapshot)
    {
        return FrameDebuggerNativeEntryPoints.AcquireSnapshot(out snapshot);
    }

    public void ReleaseSnapshot(FrameDebuggerNativeSnapshotBuffer snapshot)
    {
        FrameDebuggerNativeEntryPoints.ReleaseSnapshot(snapshot);
    }

    public int RequestCapture()
    {
        return FrameDebuggerNativeEntryPoints.RequestCapture();
    }

    public int RequestResume()
    {
        return FrameDebuggerNativeEntryPoints.RequestResume();
    }

    public int SelectExecutionEvent(IntPtr executionEventIdUtf8)
    {
        return FrameDebuggerNativeEntryPoints.SelectExecutionEvent(executionEventIdUtf8);
    }
}

internal static partial class FrameDebuggerNativeEntryPoints
{
    private const string LibraryName = "editor_native";

    [LibraryImport(LibraryName, EntryPoint = "editor_frame_debugger_acquire_snapshot")]
    internal static partial int AcquireSnapshot(out FrameDebuggerNativeSnapshotBuffer snapshot);

    [LibraryImport(LibraryName, EntryPoint = "editor_frame_debugger_release_snapshot")]
    internal static partial void ReleaseSnapshot(FrameDebuggerNativeSnapshotBuffer snapshot);

    [LibraryImport(LibraryName, EntryPoint = "editor_frame_debugger_request_capture")]
    internal static partial int RequestCapture();

    [LibraryImport(LibraryName, EntryPoint = "editor_frame_debugger_request_resume")]
    internal static partial int RequestResume();

    [LibraryImport(LibraryName, EntryPoint = "editor_frame_debugger_select_execution_event")]
    internal static partial int SelectExecutionEvent(IntPtr executionEventIdUtf8);
}
