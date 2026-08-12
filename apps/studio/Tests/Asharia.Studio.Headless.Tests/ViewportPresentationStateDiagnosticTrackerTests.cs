using System;
using System.Linq;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class ViewportPresentationStateDiagnosticTrackerTests
{
    private static readonly ViewportPresentationEndpointId EndpointId = new("scene-view");

    [Fact]
    public void Degraded_episode_deduplicates_state_changes_and_recovers_once()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 2);
        var tracker = new ViewportPresentationStateDiagnosticTracker(hub, EndpointId);
        var sessionId = ViewportSessionId.Create();

        tracker.ObserveDegraded(
            ViewportPresentationState.Unsupported,
            sessionId,
            generation: 7,
            revision: 11);
        tracker.ObserveDegraded(
            ViewportPresentationState.Unsupported,
            sessionId,
            generation: 7,
            revision: 11);
        tracker.ObserveDegraded(
            ViewportPresentationState.RenderFailed,
            sessionId,
            generation: 8,
            revision: 12);
        tracker.ObserveStatus(
            ViewportPresentationState.Ready,
            sessionId,
            generation: 9,
            revision: 13);
        tracker.ObserveStatus(
            ViewportPresentationState.Ready,
            sessionId,
            generation: 9,
            revision: 13);

        var records = hub.ReadDiagnostics(maxCount: 8).Items;
        Assert.Collection(
            records,
            failure =>
            {
                Assert.Equal("studio.viewport.presentation.failed", failure.Code);
                Assert.Equal("Unsupported", Attribute(failure, "state"));
                Assert.Equal("7", Attribute(failure, "generation"));
                Assert.Equal("11", Attribute(failure, "revision"));
                Assert.Equal(EndpointId.Value, Attribute(failure, "endpointId"));
            },
            recovery =>
            {
                Assert.Equal("studio.viewport.presentation.recovered", recovery.Code);
                Assert.Equal("Ready", Attribute(recovery, "state"));
                Assert.Equal("9", Attribute(recovery, "generation"));
                Assert.Equal("13", Attribute(recovery, "revision"));
            });
        Assert.Equal(records[0].Context.OperationId, records[1].Context.OperationId);
        Assert.Equal(records[0].Context.CorrelationId, records[1].Context.CorrelationId);
        Assert.Equal(sessionId.Value.ToString("D"), records[0].Context.Scope.Identity);
        Assert.Equal(sessionId.Value.ToString("D"), records[1].Context.Scope.Identity);
    }

    [Theory]
    [InlineData(ViewportPresentationState.Detached)]
    [InlineData(ViewportPresentationState.WaitingForDocument)]
    [InlineData(ViewportPresentationState.Draining)]
    public void Terminal_or_document_boundary_abandons_episode_without_recovery(
        ViewportPresentationState boundary)
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 2);
        var tracker = new ViewportPresentationStateDiagnosticTracker(hub, EndpointId);

        tracker.ObserveDegraded(
            ViewportPresentationState.RenderFailed,
            ViewportSessionId.Create(),
            generation: 2,
            revision: 3);
        tracker.ObserveStatus(boundary, default, generation: 3, revision: 0);
        tracker.ObserveStatus(
            ViewportPresentationState.Ready,
            ViewportSessionId.Create(),
            generation: 4,
            revision: 5);

        Assert.Equal(
            "studio.viewport.presentation.failed",
            Assert.Single(hub.ReadDiagnostics(maxCount: 8).Items).Code);
    }

    [Fact]
    public void Probing_retains_episode_until_ready()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 2);
        var tracker = new ViewportPresentationStateDiagnosticTracker(hub, EndpointId);
        var sessionId = ViewportSessionId.Create();

        tracker.ObserveDegraded(
            ViewportPresentationState.NativeUnavailable,
            sessionId,
            generation: 2,
            revision: 3);
        tracker.ObserveStatus(
            ViewportPresentationState.Probing,
            sessionId,
            generation: 3,
            revision: 3);
        tracker.ObserveStatus(
            ViewportPresentationState.Ready,
            sessionId,
            generation: 3,
            revision: 3);

        var records = hub.ReadDiagnostics(maxCount: 8).Items;
        Assert.Equal(2, records.Length);
        Assert.Equal(records[0].Context.CorrelationId, records[1].Context.CorrelationId);
    }

    [Fact]
    public void New_episode_uses_a_new_correlation_and_process_scope_without_a_session()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 2);
        var tracker = new ViewportPresentationStateDiagnosticTracker(hub, EndpointId);
        var sessionId = ViewportSessionId.Create();

        tracker.ObserveDegraded(
            ViewportPresentationState.Unsupported,
            default,
            generation: 1,
            revision: 0);
        tracker.ObserveStatus(
            ViewportPresentationState.Ready,
            sessionId,
            generation: 2,
            revision: 1);
        tracker.ObserveDegraded(
            ViewportPresentationState.DeviceMismatch,
            sessionId,
            generation: 3,
            revision: 2);
        tracker.ObserveStatus(
            ViewportPresentationState.Ready,
            sessionId,
            generation: 4,
            revision: 2);

        var records = hub.ReadDiagnostics(maxCount: 8).Items;
        Assert.Equal(4, records.Length);
        Assert.Equal(hub.ProcessIdentity.Value.ToString("D"), records[0].Context.Scope.Identity);
        Assert.Equal(records[0].Context.CorrelationId, records[1].Context.CorrelationId);
        Assert.Equal(records[2].Context.CorrelationId, records[3].Context.CorrelationId);
        Assert.NotEqual(records[0].Context.CorrelationId, records[2].Context.CorrelationId);
    }

    [Fact]
    public void Different_valid_session_starts_a_new_episode_and_ignores_stale_recovery()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 2);
        var tracker = new ViewportPresentationStateDiagnosticTracker(hub, EndpointId);
        var firstSession = ViewportSessionId.Create();
        var secondSession = ViewportSessionId.Create();

        tracker.ObserveDegraded(
            ViewportPresentationState.RenderFailed,
            firstSession,
            generation: 2,
            revision: 3);
        tracker.ObserveDegraded(
            ViewportPresentationState.NativeUnavailable,
            secondSession,
            generation: 1,
            revision: 4);
        tracker.ObserveStatus(
            ViewportPresentationState.Ready,
            firstSession,
            generation: 3,
            revision: 5);
        tracker.ObserveStatus(
            ViewportPresentationState.Ready,
            secondSession,
            generation: 2,
            revision: 6);

        var records = hub.ReadDiagnostics(maxCount: 8).Items;
        Assert.Collection(
            records,
            first => Assert.Equal(
                firstSession.Value.ToString("D"),
                first.Context.Scope.Identity),
            second => Assert.Equal(
                secondSession.Value.ToString("D"),
                second.Context.Scope.Identity),
            recovery =>
            {
                Assert.Equal("studio.viewport.presentation.recovered", recovery.Code);
                Assert.Equal(
                    secondSession.Value.ToString("D"),
                    recovery.Context.Scope.Identity);
            });
        Assert.Equal("studio.viewport.presentation.failed", records[0].Code);
        Assert.Equal("studio.viewport.presentation.failed", records[1].Code);
        Assert.NotEqual(records[0].Context.CorrelationId, records[1].Context.CorrelationId);
        Assert.Equal(records[1].Context.CorrelationId, records[2].Context.CorrelationId);
    }

    private static string Attribute(StudioDiagnosticRecord record, string name) =>
        record.Attributes.Single(attribute => attribute.Name == name).Value;
}
