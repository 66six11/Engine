using System;
using System.Runtime.InteropServices;

namespace Editor.Core.Interop.FrameDebugger.Api;

internal sealed class FrameDebuggerNativeLibraryApi : IFrameDebuggerNativeApi
{
    public static FrameDebuggerNativeLibraryApi Instance { get; } = new();

    private FrameDebuggerNativeLibraryApi()
    {
    }

    public uint AcquireSnapshot(ref FrameDebuggerNativeSnapshotBuffer snapshot)
    {
        return FrameDebuggerNativeEntryPoints.AcquireSnapshot(ref snapshot);
    }

    public void ReleaseSnapshot(FrameDebuggerNativeSnapshotBuffer snapshot)
    {
        FrameDebuggerNativeEntryPoints.ReleaseSnapshot(snapshot);
    }

    public uint RequestCapture()
    {
        return FrameDebuggerNativeEntryPoints.RequestCapture();
    }

    public uint RequestResume()
    {
        return FrameDebuggerNativeEntryPoints.RequestResume();
    }

    public uint SelectExecutionEvent(FrameDebuggerNativeStringView executionEventIdUtf8)
    {
        return FrameDebuggerNativeEntryPoints.SelectExecutionEvent(executionEventIdUtf8);
    }
}

internal static partial class FrameDebuggerNativeEntryPoints
{
    private const string LibraryName = "editor_native";

    [LibraryImport(LibraryName, EntryPoint = "editor_frame_debugger_acquire_snapshot")]
    internal static partial uint AcquireSnapshot(ref FrameDebuggerNativeSnapshotBuffer snapshot);

    [LibraryImport(LibraryName, EntryPoint = "editor_frame_debugger_release_snapshot")]
    internal static partial void ReleaseSnapshot(FrameDebuggerNativeSnapshotBuffer snapshot);

    [LibraryImport(LibraryName, EntryPoint = "editor_frame_debugger_request_capture")]
    internal static partial uint RequestCapture();

    [LibraryImport(LibraryName, EntryPoint = "editor_frame_debugger_request_resume")]
    internal static partial uint RequestResume();

    [LibraryImport(LibraryName, EntryPoint = "editor_frame_debugger_select_execution_event")]
    internal static partial uint SelectExecutionEvent(FrameDebuggerNativeStringView executionEventIdUtf8);
}
