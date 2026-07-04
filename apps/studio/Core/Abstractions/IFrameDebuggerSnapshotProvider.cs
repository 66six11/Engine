using System;
using Editor.Core.Models.FrameDebug;

namespace Editor.Core.Abstractions;

public interface IFrameDebuggerSnapshotProvider
{
    event EventHandler? SnapshotChanged;

    FrameDebuggerSnapshot GetCurrentSnapshot();

    bool TryGetPass(string passId, out FrameDebugPassSnapshot? pass);

    bool TryGetExecutionEvent(
        string executionEventId,
        out FrameDebugExecutionEventSnapshot? executionEvent);
}
