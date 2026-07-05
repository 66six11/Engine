using Editor.Core.Models.FrameDebug;

namespace Editor.Core.Abstractions;

public interface INativeFrameDebuggerBridge
{
    bool TryAcquireSnapshot(out NativeFrameDebuggerSnapshotPayload? payload);

    bool RequestCapture();

    bool RequestResume();

    bool SelectExecutionEvent(string executionEventId);
}
