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
    public void Degraded_episode_is_active_until_ready_resolves_the_same_problem_once()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 2);
        var tracker = new ViewportPresentationStateDiagnosticTracker(hub, EndpointId);
        var sessionId = ViewportSessionId.Create();

        tracker.ObserveDegraded(
            ViewportPresentationState.Unsupported,
            sessionId,
            generation: 7,
            revision: 11);
        var active = Assert.Single(hub.ReadActiveProblems().Items);
        Assert.Equal(StudioProblemTransition.Active, active.ProblemTransition);
        Assert.True(active.ProblemId.HasValue);
        Assert.StartsWith(
            "viewport-presentation:",
            active.ProblemId.Value.Value,
            StringComparison.Ordinal);
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
                Assert.Equal(StudioProblemTransition.Active, failure.ProblemTransition);
            },
            recovery =>
            {
                Assert.Equal("studio.viewport.presentation.recovered", recovery.Code);
                Assert.Equal("Ready", Attribute(recovery, "state"));
                Assert.Equal("9", Attribute(recovery, "generation"));
                Assert.Equal("13", Attribute(recovery, "revision"));
                Assert.Equal(StudioProblemTransition.Resolved, recovery.ProblemTransition);
            });
        Assert.Equal(records[0].ProblemId, records[1].ProblemId);
        Assert.Equal(records[0].Context.OperationId, records[1].Context.OperationId);
        Assert.Equal(records[0].Context.CorrelationId, records[1].Context.CorrelationId);
        Assert.Equal(sessionId.Value.ToString("D"), records[0].Context.Scope.Identity);
        Assert.Equal(sessionId.Value.ToString("D"), records[1].Context.Scope.Identity);
        Assert.Empty(hub.ReadActiveProblems().Items);
    }

    [Theory]
    [InlineData(ViewportPresentationState.Detached)]
    [InlineData(ViewportPresentationState.WaitingForDocument)]
    [InlineData(ViewportPresentationState.Draining)]
    public void Terminal_or_document_boundary_marks_the_active_problem_stale(
        ViewportPresentationState boundary)
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 2);
        var tracker = new ViewportPresentationStateDiagnosticTracker(hub, EndpointId);

        var sessionId = ViewportSessionId.Create();
        tracker.ObserveDegraded(
            ViewportPresentationState.RenderFailed,
            sessionId,
            generation: 2,
            revision: 3);
        tracker.ObserveStatus(boundary, default, generation: 3, revision: 0);
        tracker.ObserveStatus(
            ViewportPresentationState.Ready,
            ViewportSessionId.Create(),
            generation: 4,
            revision: 5);

        var records = hub.ReadDiagnostics(maxCount: 8).Items;
        Assert.Collection(
            records,
            active =>
            {
                Assert.Equal("studio.viewport.presentation.failed", active.Code);
                Assert.Equal(StudioProblemTransition.Active, active.ProblemTransition);
            },
            stale =>
            {
                Assert.Equal("studio.viewport.presentation.stale", stale.Code);
                Assert.Equal(StudioProblemTransition.Stale, stale.ProblemTransition);
                Assert.Equal(boundary.ToString(), Attribute(stale, "state"));
                Assert.Equal("3", Attribute(stale, "generation"));
                Assert.Equal("state-boundary", Attribute(stale, "closureReason"));
                Assert.Equal(sessionId.Value.ToString("D"), stale.Context.Scope.Identity);
                Assert.Equal(2, stale.Context.Scope.Generation);
            });
        Assert.Equal(records[0].ProblemId, records[1].ProblemId);
        Assert.Equal(records[0].Context.OperationId, records[1].Context.OperationId);
        Assert.Equal(records[0].Context.CorrelationId, records[1].Context.CorrelationId);
        Assert.Empty(hub.ReadActiveProblems().Items);
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
        Assert.Single(hub.ReadActiveProblems().Items);
        Assert.Single(hub.ReadDiagnostics(maxCount: 8).Items);
        tracker.ObserveStatus(
            ViewportPresentationState.Ready,
            sessionId,
            generation: 3,
            revision: 3);

        var records = hub.ReadDiagnostics(maxCount: 8).Items;
        Assert.Equal(2, records.Length);
        Assert.Equal(StudioProblemTransition.Active, records[0].ProblemTransition);
        Assert.Equal(StudioProblemTransition.Resolved, records[1].ProblemTransition);
        Assert.Equal(records[0].ProblemId, records[1].ProblemId);
        Assert.Equal(records[0].Context.CorrelationId, records[1].Context.CorrelationId);
        Assert.Empty(hub.ReadActiveProblems().Items);
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
        Assert.Empty(hub.ReadActiveProblems().Items);
        tracker.ObserveDegraded(
            ViewportPresentationState.DeviceMismatch,
            sessionId,
            generation: 3,
            revision: 2);
        Assert.Single(hub.ReadActiveProblems().Items);
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
        Assert.Equal(records[0].ProblemId, records[1].ProblemId);
        Assert.Equal(records[2].ProblemId, records[3].ProblemId);
        Assert.NotEqual(records[0].ProblemId, records[2].ProblemId);
        Assert.Equal(
            new[]
            {
                StudioProblemTransition.Active,
                StudioProblemTransition.Resolved,
                StudioProblemTransition.Active,
                StudioProblemTransition.Resolved,
            },
            records.Select(record => record.ProblemTransition!.Value));
        Assert.Empty(hub.ReadActiveProblems().Items);
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
        var firstActive = Assert.Single(hub.ReadActiveProblems().Items);
        tracker.ObserveDegraded(
            ViewportPresentationState.NativeUnavailable,
            secondSession,
            generation: 1,
            revision: 4);
        var secondActive = Assert.Single(hub.ReadActiveProblems().Items);
        Assert.NotEqual(firstActive.ProblemId, secondActive.ProblemId);
        Assert.Equal(secondSession.Value.ToString("D"), secondActive.Context.Scope.Identity);
        tracker.ObserveStatus(
            ViewportPresentationState.Ready,
            firstSession,
            generation: 3,
            revision: 5);
        Assert.Same(secondActive, Assert.Single(hub.ReadActiveProblems().Items));
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
            stale =>
            {
                Assert.Equal("studio.viewport.presentation.stale", stale.Code);
                Assert.Equal(StudioProblemTransition.Stale, stale.ProblemTransition);
                Assert.Equal(
                    firstSession.Value.ToString("D"),
                    stale.Context.Scope.Identity);
                Assert.Equal(2, stale.Context.Scope.Generation);
                Assert.Equal("1", Attribute(stale, "generation"));
                Assert.Equal("session-replaced", Attribute(stale, "closureReason"));
            },
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
        Assert.Equal("studio.viewport.presentation.failed", records[2].Code);
        Assert.Equal(records[0].ProblemId, records[1].ProblemId);
        Assert.Equal(records[2].ProblemId, records[3].ProblemId);
        Assert.NotEqual(records[0].ProblemId, records[2].ProblemId);
        Assert.Equal(records[0].Context.CorrelationId, records[1].Context.CorrelationId);
        Assert.NotEqual(records[0].Context.CorrelationId, records[2].Context.CorrelationId);
        Assert.Equal(records[2].Context.CorrelationId, records[3].Context.CorrelationId);
        Assert.Empty(hub.ReadActiveProblems().Items);
    }

    private static string Attribute(StudioDiagnosticRecord record, string name) =>
        record.Attributes.Single(attribute => attribute.Name == name).Value;
}
