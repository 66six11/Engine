using System;
using Editor.Core.Models.FrameDebug;
using Xunit;

namespace Editor.Tests.Core;

public sealed class FrameDebuggerSnapshotProviderTests
{
    [Fact]
    public void Snapshot_copies_collections_to_read_only_lists()
    {
        var capturedAtUtc = DateTimeOffset.Parse("2026-07-04T10:30:00Z");
        var capture = new FrameDebugCaptureSnapshot(
            "capture:1",
            12,
            42UL,
            "Scene",
            1280,
            720,
            capturedAtUtc);
        var pass = new FrameDebugPassSnapshot(
            "pass:0",
            0,
            0,
            "Scene Color",
            "Raster",
            "BasicRenderView",
            AllowCulling: true,
            HasSideEffects: false,
            CommandCount: 1,
            ImageTransitionCount: 1,
            BufferTransitionCount: 0);
        var command = new FrameDebugCommandSnapshot(
            "command:0",
            "pass:0",
            0,
            0,
            "Scene Color",
            "DrawFullscreenTriangle",
            "Draw scene color");
        var resource = new FrameDebugResourceSnapshot(
            "image:0",
            "Image",
            0,
            "Scene Color",
            "Imported",
            "Rgba8Unorm",
            "1280x720",
            "Undefined",
            "ColorWrite");
        var access = new FrameDebugAccessEdgeSnapshot(
            "access:0",
            "pass:0",
            "image:0",
            "Scene Color",
            "Scene Color",
            "color",
            "ColorWrite",
            "Fragment");
        var dependency = new FrameDebugDependencyEdgeSnapshot(
            "dependency:0",
            "pass:0",
            "pass:1",
            "image:0",
            "Scene Color",
            "Color dependency");
        var transition = new FrameDebugTransitionSnapshot(
            "transition:0",
            "BeforePass",
            "pass:0",
            "image:0",
            "Scene Color",
            "Scene Color",
            "Undefined",
            "ColorWrite");
        var executionEvent = new FrameDebugExecutionEventSnapshot(
            "event:0",
            0,
            "DrawFullscreenTriangle",
            "pass:0",
            "Scene Color",
            "command:0",
            "Draw scene color",
            null,
            "image:0",
            VertexCount: 3,
            IndexCount: 0,
            InstanceCount: 1,
            GroupCountX: 0,
            GroupCountY: 0,
            GroupCountZ: 0);
        var passes = new[] { pass };
        var commands = new[] { command };
        var resources = new[] { resource };
        var accessEdges = new[] { access };
        var dependencyEdges = new[] { dependency };
        var transitions = new[] { transition };
        var executionEvents = new[] { executionEvent };

        var snapshot = new FrameDebuggerSnapshot(
            1,
            FrameDebuggerState.PausedFrameDebug,
            capture,
            passes,
            commands,
            resources,
            accessEdges,
            dependencyEdges,
            transitions,
            executionEvents,
            new FrameDebugPreviewSnapshot("NotRequested", null, null, "No preview requested."),
            "Captured frame 12.");

        passes[0] = pass with { Name = "Mutated" };
        commands[0] = command with { Detail = "Mutated" };
        resources[0] = resource with { Name = "Mutated" };
        accessEdges[0] = access with { SlotName = "mutated" };
        dependencyEdges[0] = dependency with { Reason = "Mutated" };
        transitions[0] = transition with { NewAccess = "Mutated" };
        executionEvents[0] = executionEvent with { Label = "Mutated" };

        Assert.Equal(1, snapshot.Version);
        Assert.Equal(FrameDebuggerState.PausedFrameDebug, snapshot.State);
        Assert.Equal(capture, snapshot.Capture);
        Assert.Equal("Captured frame 12.", snapshot.Message);
        Assert.Equal("Scene Color", Assert.Single(snapshot.Passes).Name);
        Assert.Equal("Draw scene color", Assert.Single(snapshot.Commands).Detail);
        Assert.Equal("Scene Color", Assert.Single(snapshot.Resources).Name);
        Assert.Equal("color", Assert.Single(snapshot.AccessEdges).SlotName);
        Assert.Equal("Color dependency", Assert.Single(snapshot.DependencyEdges).Reason);
        Assert.Equal("ColorWrite", Assert.Single(snapshot.Transitions).NewAccess);
        Assert.Equal("Draw scene color", Assert.Single(snapshot.ExecutionEvents).Label);
        Assert.IsNotType<FrameDebugPassSnapshot[]>(snapshot.Passes);
    }

    [Fact]
    public void Snapshots_reject_blank_required_ids()
    {
        Assert.Throws<ArgumentException>(() => new FrameDebugCaptureSnapshot(
            "",
            1,
            1UL,
            "Scene",
            1,
            1,
            DateTimeOffset.UtcNow));
        Assert.Throws<ArgumentException>(() => new FrameDebugPassSnapshot(
            "",
            0,
            0,
            "Pass",
            "Raster",
            "Params",
            AllowCulling: true,
            HasSideEffects: false,
            CommandCount: 0,
            ImageTransitionCount: 0,
            BufferTransitionCount: 0));
        Assert.Throws<ArgumentException>(() => new FrameDebugExecutionEventSnapshot(
            "",
            0,
            "Draw",
            "pass:0",
            "Pass",
            null,
            "Draw",
            null,
            null,
            VertexCount: 0,
            IndexCount: 0,
            InstanceCount: 0,
            GroupCountX: 0,
            GroupCountY: 0,
            GroupCountZ: 0));
    }

    [Fact]
    public void Unavailable_snapshot_has_empty_collections_and_unavailable_state()
    {
        var snapshot = FrameDebuggerSnapshot.Unavailable;

        Assert.Equal(FrameDebuggerState.Unavailable, snapshot.State);
        Assert.Null(snapshot.Capture);
        Assert.Empty(snapshot.Passes);
        Assert.Empty(snapshot.Commands);
        Assert.Empty(snapshot.Resources);
        Assert.Empty(snapshot.AccessEdges);
        Assert.Empty(snapshot.DependencyEdges);
        Assert.Empty(snapshot.Transitions);
        Assert.Empty(snapshot.ExecutionEvents);
        Assert.Contains("unavailable", snapshot.Message, StringComparison.OrdinalIgnoreCase);
    }
}
