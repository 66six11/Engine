using System;
using Asharia.Editor.Diagnostics.FrameDebug;
using Editor.Core.Abstractions;

namespace Editor.Core.Services;

public sealed class NativeFrameDebuggerSnapshotProvider : IFrameDebuggerSnapshotProvider
{
    private readonly INativeFrameDebuggerBridge bridge_;
    private InMemoryFrameDebuggerSnapshotProvider snapshots_;

    public NativeFrameDebuggerSnapshotProvider(
        INativeFrameDebuggerBridge bridge,
        FrameDebuggerSnapshot? initialSnapshot = null)
    {
        ArgumentNullException.ThrowIfNull(bridge);

        bridge_ = bridge;
        snapshots_ = new InMemoryFrameDebuggerSnapshotProvider(
            initialSnapshot ?? FrameDebuggerSnapshot.Unavailable);
    }

    public event EventHandler? SnapshotChanged;

    public FrameDebuggerSnapshot GetCurrentSnapshot()
    {
        return snapshots_.GetCurrentSnapshot();
    }

    public bool RefreshSnapshot()
    {
        if (!bridge_.TryAcquireSnapshot(out var payload) || payload is null)
        {
            return false;
        }

        var snapshot = FrameDebuggerSnapshotJsonReader.TryRead(payload);
        if (snapshot is null)
        {
            return false;
        }

        snapshots_ = new InMemoryFrameDebuggerSnapshotProvider(snapshot);
        SnapshotChanged?.Invoke(this, EventArgs.Empty);
        return true;
    }

    public bool RequestCapture()
    {
        return bridge_.RequestCapture();
    }

    public bool RequestResume()
    {
        return bridge_.RequestResume();
    }

    public bool SelectExecutionEvent(string executionEventId)
    {
        return !string.IsNullOrWhiteSpace(executionEventId) &&
               bridge_.SelectExecutionEvent(executionEventId);
    }

    public bool TryGetPass(string passId, out FrameDebugPassSnapshot? pass)
    {
        return snapshots_.TryGetPass(passId, out pass);
    }

    public bool TryGetExecutionEvent(
        string executionEventId,
        out FrameDebugExecutionEventSnapshot? executionEvent)
    {
        return snapshots_.TryGetExecutionEvent(executionEventId, out executionEvent);
    }
}
