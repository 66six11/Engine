#if DEBUG
using System;
using System.Linq;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.Application.Viewports;
using Asharia.Studio.DevelopmentHost.Hosting;
using Asharia.Studio.DevelopmentProtocol;
using Asharia.Studio.Presentation.Avalonia.Viewports;
using Xunit;

namespace Asharia.Studio.Headless.Tests;

public sealed class ViewportPresentationStateDiagnosticObservationTests
{
    [Fact]
    public async Task Readonly_observation_reads_the_same_viewport_episode_sequence_and_context()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 8, logCapacity: 2);
        var tracker = new ViewportPresentationStateDiagnosticTracker(
            hub,
            new ViewportPresentationEndpointId("scene-view"));
        var sessionId = ViewportSessionId.Create();
        tracker.ObserveDegraded(
            ViewportPresentationState.RenderFailed,
            sessionId,
            generation: 6,
            revision: 10);
        tracker.ObserveStatus(
            ViewportPresentationState.Ready,
            sessionId,
            generation: 7,
            revision: 10);
        var direct = hub.ReadDiagnostics(maxCount: 8).Items;

        await using var host = StudioDevelopmentHost.StartForCurrentProcess(
            hub,
            new StudioInstanceId(hub.ProcessIdentity.Value),
            new StudioSessionId(Guid.NewGuid()),
            "tests/viewport-state-diagnostics",
            "Test",
            endpointGeneration: 1,
            providerGeneration: 3);
        var response = await host.ReadDiagnosticsAsync(new ObservationRequest<
            DiagnosticsReadParameters>(
                ObservationProtocolVersion.Current,
                new ObservationRequestId(Guid.NewGuid()),
                host.StudioInstanceId,
                host.EndpointGeneration,
                ObservationMethodId.DiagnosticsRead,
                TimeoutMilliseconds: 1_000,
                new DiagnosticsReadParameters(AfterSequence: 0, MaxCount: 8)),
            TestContext.Current.CancellationToken);

        Assert.Equal(ObservationOutcome.Complete, response.Outcome);
        var observed = response.Value!.Items;
        Assert.Equal(direct.Select(record => record.SequenceId), observed.Select(item => item.Sequence));
        Assert.Equal(direct.Select(record => record.Code), observed.Select(item => item.Code));
        Assert.All(observed, item =>
        {
            Assert.Equal(sessionId.Value, item.Context.Scope.OwnerScopeId);
            Assert.Equal(3, item.Context.Scope.ProviderGeneration);
        });
        Assert.Equal(
            observed[0].Context.CorrelationId,
            observed[1].Context.CorrelationId);
    }
}
#endif
