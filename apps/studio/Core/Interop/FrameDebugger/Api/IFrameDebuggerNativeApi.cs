using System;

namespace Editor.Core.Interop.FrameDebugger.Api;

internal interface IFrameDebuggerNativeApi
{
    uint AcquireSnapshot(ref FrameDebuggerNativeSnapshotBuffer snapshot);

    void ReleaseSnapshot(FrameDebuggerNativeSnapshotBuffer snapshot);

    uint RequestCapture();

    uint RequestResume();

    uint SelectExecutionEvent(FrameDebuggerNativeStringView executionEventIdUtf8);
}
