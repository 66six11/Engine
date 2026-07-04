using System;

namespace Editor.Core.Interop;

internal interface IFrameDebuggerNativeApi
{
    int AcquireSnapshot(out FrameDebuggerNativeSnapshotBuffer snapshot);

    void ReleaseSnapshot(FrameDebuggerNativeSnapshotBuffer snapshot);

    int RequestCapture();

    int RequestResume();

    int SelectExecutionEvent(IntPtr executionEventIdUtf8);
}
