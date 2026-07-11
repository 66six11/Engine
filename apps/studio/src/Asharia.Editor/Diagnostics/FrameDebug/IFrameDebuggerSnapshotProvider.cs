using System;

namespace Asharia.Editor.Diagnostics.FrameDebug;

public interface IFrameDebuggerSnapshotProvider
{
    event EventHandler? SnapshotChanged;

    FrameDebuggerSnapshot GetCurrentSnapshot();

    bool TryGetPass(string passId, out FrameDebugPassSnapshot? pass);

    bool TryGetExecutionEvent(
        string executionEventId,
        out FrameDebugExecutionEventSnapshot? executionEvent);
}
