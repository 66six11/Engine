using System;
using System.Collections.Generic;
using Asharia.Editor.Diagnostics.FrameDebug;

namespace Editor.Core.Services;

public sealed class InMemoryFrameDebuggerSnapshotProvider : IFrameDebuggerSnapshotProvider
{
    private Dictionary<string, FrameDebugPassSnapshot> passesById_;
    private Dictionary<string, FrameDebugExecutionEventSnapshot> executionEventsById_;
    private FrameDebuggerSnapshot current_;

    public InMemoryFrameDebuggerSnapshotProvider(FrameDebuggerSnapshot current)
    {
        ArgumentNullException.ThrowIfNull(current);

        current_ = current;
        passesById_ = BuildPassIndex(current);
        executionEventsById_ = BuildExecutionEventIndex(current);
    }

    public event EventHandler? SnapshotChanged;

    public FrameDebuggerSnapshot GetCurrentSnapshot()
    {
        return current_;
    }

    public void ReplaceSnapshot(FrameDebuggerSnapshot snapshot)
    {
        ArgumentNullException.ThrowIfNull(snapshot);

        var passesById = BuildPassIndex(snapshot);
        var executionEventsById = BuildExecutionEventIndex(snapshot);
        current_ = snapshot;
        passesById_ = passesById;
        executionEventsById_ = executionEventsById;
        SnapshotChanged?.Invoke(this, EventArgs.Empty);
    }

    public bool TryGetPass(string passId, out FrameDebugPassSnapshot? pass)
    {
        if (string.IsNullOrWhiteSpace(passId))
        {
            pass = null;
            return false;
        }

        return passesById_.TryGetValue(passId, out pass);
    }

    public bool TryGetExecutionEvent(
        string executionEventId,
        out FrameDebugExecutionEventSnapshot? executionEvent)
    {
        if (string.IsNullOrWhiteSpace(executionEventId))
        {
            executionEvent = null;
            return false;
        }

        return executionEventsById_.TryGetValue(executionEventId, out executionEvent);
    }

    private static Dictionary<string, FrameDebugPassSnapshot> BuildPassIndex(
        FrameDebuggerSnapshot snapshot)
    {
        var passesById = new Dictionary<string, FrameDebugPassSnapshot>(StringComparer.Ordinal);
        foreach (var pass in snapshot.Passes)
        {
            if (!passesById.TryAdd(pass.Id, pass))
            {
                throw new InvalidOperationException(
                    $"Frame debugger snapshot contains duplicate pass id '{pass.Id}'.");
            }
        }

        return passesById;
    }

    private static Dictionary<string, FrameDebugExecutionEventSnapshot> BuildExecutionEventIndex(
        FrameDebuggerSnapshot snapshot)
    {
        var executionEventsById =
            new Dictionary<string, FrameDebugExecutionEventSnapshot>(StringComparer.Ordinal);
        foreach (var executionEvent in snapshot.ExecutionEvents)
        {
            if (!executionEventsById.TryAdd(executionEvent.Id, executionEvent))
            {
                throw new InvalidOperationException(
                    "Frame debugger snapshot contains duplicate execution event id " +
                    $"'{executionEvent.Id}'.");
            }
        }

        return executionEventsById;
    }
}
