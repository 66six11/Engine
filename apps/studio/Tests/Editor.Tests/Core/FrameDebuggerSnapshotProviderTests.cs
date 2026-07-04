using System;
using Editor.Core.Abstractions;
using Editor.Core.Models.FrameDebug;
using Editor.Core.Services;
using Xunit;

namespace Editor.Tests.Core;

public sealed class FrameDebuggerSnapshotProviderTests
{
    [Fact]
    public void InMemory_provider_exposes_current_snapshot_and_lookup()
    {
        var pass = CreatePass("pass:scene-color", "Scene Color");
        var executionEvent = CreateExecutionEvent("event:draw", pass.Id, pass.Name);
        var snapshot = CreateSnapshot([pass], [executionEvent]);
        IFrameDebuggerSnapshotProvider provider = new InMemoryFrameDebuggerSnapshotProvider(snapshot);

        Assert.Same(snapshot, provider.GetCurrentSnapshot());
        Assert.True(provider.TryGetPass("pass:scene-color", out var actualPass));
        Assert.Same(pass, actualPass);
        Assert.True(provider.TryGetExecutionEvent("event:draw", out var actualEvent));
        Assert.Same(executionEvent, actualEvent);
        Assert.False(provider.TryGetPass("pass:missing", out var missingPass));
        Assert.Null(missingPass);
        Assert.False(provider.TryGetExecutionEvent("event:missing", out var missingEvent));
        Assert.Null(missingEvent);
        Assert.False(provider.TryGetPass(" ", out var blankPass));
        Assert.Null(blankPass);
        Assert.False(provider.TryGetExecutionEvent(" ", out var blankEvent));
        Assert.Null(blankEvent);
    }

    [Fact]
    public void InMemory_provider_raises_snapshot_changed_once_when_snapshot_is_replaced()
    {
        var provider = new InMemoryFrameDebuggerSnapshotProvider(FrameDebuggerSnapshot.Unavailable);
        var next = CreateSnapshot([CreatePass("pass:scene-color", "Scene Color")], []);
        var changeCount = 0;
        object? eventSender = null;
        provider.SnapshotChanged += (sender, _) =>
        {
            changeCount++;
            eventSender = sender;
        };

        provider.ReplaceSnapshot(next);

        Assert.Equal(1, changeCount);
        Assert.Same(provider, eventSender);
        Assert.Same(next, provider.GetCurrentSnapshot());
    }

    [Fact]
    public void InMemory_provider_rebuilds_lookup_when_snapshot_is_replaced()
    {
        var oldPass = CreatePass("pass:old", "Old");
        var newPass = CreatePass("pass:new", "New");
        var oldEvent = CreateExecutionEvent("event:old", oldPass.Id, oldPass.Name);
        var newEvent = CreateExecutionEvent("event:new", newPass.Id, newPass.Name);
        var provider = new InMemoryFrameDebuggerSnapshotProvider(
            CreateSnapshot([oldPass], [oldEvent]));

        provider.ReplaceSnapshot(CreateSnapshot([newPass], [newEvent]));

        Assert.False(provider.TryGetPass("pass:old", out var missingPass));
        Assert.Null(missingPass);
        Assert.True(provider.TryGetPass("pass:new", out var actualPass));
        Assert.Same(newPass, actualPass);
        Assert.False(provider.TryGetExecutionEvent("event:old", out var missingEvent));
        Assert.Null(missingEvent);
        Assert.True(provider.TryGetExecutionEvent("event:new", out var actualEvent));
        Assert.Same(newEvent, actualEvent);
    }

    [Fact]
    public void InMemory_provider_rejects_duplicate_pass_ids()
    {
        var first = CreatePass("pass:duplicate", "First");
        var second = CreatePass("pass:duplicate", "Second");
        var snapshot = CreateSnapshot([first, second], []);

        var exception = Assert.Throws<InvalidOperationException>(
            () => new InMemoryFrameDebuggerSnapshotProvider(snapshot));

        Assert.Contains("pass:duplicate", exception.Message, StringComparison.Ordinal);
    }

    [Fact]
    public void InMemory_provider_rejects_duplicate_event_ids_when_snapshot_is_replaced()
    {
        var pass = CreatePass("pass:scene-color", "Scene Color");
        var provider = new InMemoryFrameDebuggerSnapshotProvider(FrameDebuggerSnapshot.Unavailable);
        var first = CreateExecutionEvent("event:duplicate", pass.Id, pass.Name);
        var second = CreateExecutionEvent("event:duplicate", pass.Id, pass.Name);
        var snapshot = CreateSnapshot([pass], [first, second]);

        var exception = Assert.Throws<InvalidOperationException>(
            () => provider.ReplaceSnapshot(snapshot));

        Assert.Contains("event:duplicate", exception.Message, StringComparison.Ordinal);
        Assert.Same(FrameDebuggerSnapshot.Unavailable, provider.GetCurrentSnapshot());
    }

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

    private static FrameDebuggerSnapshot CreateSnapshot(
        FrameDebugPassSnapshot[] passes,
        FrameDebugExecutionEventSnapshot[] executionEvents)
    {
        return new FrameDebuggerSnapshot(
            1,
            FrameDebuggerState.PausedFrameDebug,
            capture: null,
            passes: passes,
            executionEvents: executionEvents,
            message: "Captured frame.");
    }

    private static FrameDebugPassSnapshot CreatePass(string id, string name)
    {
        return new FrameDebugPassSnapshot(
            id,
            PassIndex: 0,
            DeclarationIndex: 0,
            name,
            "Raster",
            "BasicRenderView",
            AllowCulling: true,
            HasSideEffects: false,
            CommandCount: 1,
            ImageTransitionCount: 0,
            BufferTransitionCount: 0);
    }

    private static FrameDebugExecutionEventSnapshot CreateExecutionEvent(
        string id,
        string passId,
        string passName)
    {
        return new FrameDebugExecutionEventSnapshot(
            id,
            EventIndex: 0,
            "Draw",
            passId,
            passName,
            CommandId: null,
            "Draw scene color",
            SourceResourceId: null,
            TargetResourceId: null,
            VertexCount: 3,
            IndexCount: 0,
            InstanceCount: 1,
            GroupCountX: 0,
            GroupCountY: 0,
            GroupCountZ: 0);
    }
}
