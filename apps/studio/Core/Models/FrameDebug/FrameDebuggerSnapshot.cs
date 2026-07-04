using System;
using System.Collections.Generic;

namespace Editor.Core.Models.FrameDebug;

public sealed record FrameDebuggerSnapshot
{
    public static FrameDebuggerSnapshot Unavailable { get; } = new(
        1,
        FrameDebuggerState.Unavailable,
        capture: null,
        message: "Frame Debugger snapshot is unavailable.");

    public FrameDebuggerSnapshot(
        int version,
        FrameDebuggerState state,
        FrameDebugCaptureSnapshot? capture,
        IReadOnlyList<FrameDebugPassSnapshot>? passes = null,
        IReadOnlyList<FrameDebugCommandSnapshot>? commands = null,
        IReadOnlyList<FrameDebugResourceSnapshot>? resources = null,
        IReadOnlyList<FrameDebugAccessEdgeSnapshot>? accessEdges = null,
        IReadOnlyList<FrameDebugDependencyEdgeSnapshot>? dependencyEdges = null,
        IReadOnlyList<FrameDebugTransitionSnapshot>? transitions = null,
        IReadOnlyList<FrameDebugExecutionEventSnapshot>? executionEvents = null,
        FrameDebugPreviewSnapshot? preview = null,
        string? message = null)
    {
        Version = Math.Max(1, version);
        State = state;
        Capture = capture;
        Passes = FrameDebugModelGuard.Copy(passes);
        Commands = FrameDebugModelGuard.Copy(commands);
        Resources = FrameDebugModelGuard.Copy(resources);
        AccessEdges = FrameDebugModelGuard.Copy(accessEdges);
        DependencyEdges = FrameDebugModelGuard.Copy(dependencyEdges);
        Transitions = FrameDebugModelGuard.Copy(transitions);
        ExecutionEvents = FrameDebugModelGuard.Copy(executionEvents);
        Preview = preview ?? new FrameDebugPreviewSnapshot("NotRequested", null, null, string.Empty);
        Message = message ?? string.Empty;
    }

    public int Version { get; }

    public FrameDebuggerState State { get; }

    public FrameDebugCaptureSnapshot? Capture { get; }

    public IReadOnlyList<FrameDebugPassSnapshot> Passes { get; }

    public IReadOnlyList<FrameDebugCommandSnapshot> Commands { get; }

    public IReadOnlyList<FrameDebugResourceSnapshot> Resources { get; }

    public IReadOnlyList<FrameDebugAccessEdgeSnapshot> AccessEdges { get; }

    public IReadOnlyList<FrameDebugDependencyEdgeSnapshot> DependencyEdges { get; }

    public IReadOnlyList<FrameDebugTransitionSnapshot> Transitions { get; }

    public IReadOnlyList<FrameDebugExecutionEventSnapshot> ExecutionEvents { get; }

    public FrameDebugPreviewSnapshot Preview { get; }

    public string Message { get; }
}
