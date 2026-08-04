#if DEBUG
using System;
using System.Runtime.Versioning;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.TestSupport;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.DevelopmentHost.Hosting;
using Asharia.Studio.DevelopmentHost.Transport;
using Asharia.Studio.DevelopmentProtocol;
using Editor.Shell.Composition;
using Editor.Shell.ViewModels.Windowing;
using Xunit;

namespace Editor.Tests.Shell.Composition;

public sealed class StudioCompositionSessionDevelopmentHostTests
{
    [Fact]
    public async Task Debug_composition_owns_and_stops_the_in_process_development_host()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var owned = await StudioCompositionSession.CreateAsync(
            StudioShellTestFactory.Create(),
            mainWindow: null,
            hub,
            CancellationToken.None);
        var composition = Assert.IsType<StudioCompositionSession>(owned);
        var host = Assert.IsType<StudioDevelopmentHost>(composition.DevelopmentHost);
        Assert.Null(composition.DevelopmentEndpoint);

        var response = await host.DescribeAsync(new ObservationRequest<SessionDescribeParameters>(
            ObservationProtocolVersion.Current,
            new ObservationRequestId(Guid.NewGuid()),
            host.StudioInstanceId,
            host.EndpointGeneration,
            ObservationMethodId.SessionDescribe,
            TimeoutMilliseconds: 1_000,
            new SessionDescribeParameters()));

        Assert.Equal(ObservationOutcome.Complete, response.Outcome);
        Assert.Equal("Debug", response.Value!.Configuration);
        await composition.DisposeAsync();
        Assert.Equal(StudioDevelopmentHostState.Stopped, host.State);
    }

    [Fact]
    [SupportedOSPlatform("windows")]
    public async Task Exact_readonly_grant_owns_endpoint_and_removes_manifest_before_host_stop()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var owned = await StudioCompositionSession.CreateAsync(
            StudioShellTestFactory.Create(),
            mainWindow: null,
            hub,
            CancellationToken.None,
            enableReadOnlyDevelopmentObservation: true);
        var composition = Assert.IsType<StudioCompositionSession>(owned);
        var host = Assert.IsType<StudioDevelopmentHost>(composition.DevelopmentHost);
        var endpoint = Assert.IsType<StudioDevelopmentPipeEndpoint>(
            composition.DevelopmentEndpoint);
        Assert.True(System.IO.File.Exists(endpoint.ManifestPath));

        await composition.DisposeAsync();

        Assert.False(System.IO.File.Exists(endpoint.ManifestPath));
        Assert.Equal(StudioDevelopmentPipeServerState.Stopped, endpoint.PipeState);
        Assert.Equal(StudioDevelopmentHostState.Stopped, host.State);
    }

    [Fact]
    public async Task Canceled_product_composition_does_not_create_a_host_or_endpoint()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        var shell = StudioShellTestFactory.Create();
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        try
        {
            await Assert.ThrowsAnyAsync<OperationCanceledException>(async () =>
                await StudioCompositionSession.CreateAsync(
                    shell,
                    mainWindow: null,
                    hub,
                    cancellation.Token,
                    enableReadOnlyDevelopmentObservation: true));
            Assert.Throws<ObjectDisposedException>(() => shell.MarkReady());
        }
        finally
        {
            shell.Dispose();
        }
    }

    [Fact]
    public async Task Host_creation_failure_disposes_the_unpublished_shell_owner()
    {
        var shell = StudioShellTestFactory.Create();

        await Assert.ThrowsAsync<InvalidOperationException>(async () =>
            await StudioCompositionSession.CreateAsync(
                shell,
                mainWindow: null,
                new ThrowingIdentityDiagnosticHub(),
                CancellationToken.None));

        Assert.Throws<ObjectDisposedException>(() => shell.MarkReady());
    }

    [Theory]
    [InlineData("--development-observation=readonly", true)]
    [InlineData("--development-observation=readwrite", false)]
    [InlineData("--development-observation", false)]
    [InlineData("--DEVELOPMENT-OBSERVATION=readonly", false)]
    [InlineData("", false)]
    public void Development_observation_requires_the_exact_readonly_grant(
        string argument,
        bool expected)
    {
        Assert.Equal(
            expected,
            StudioDevelopmentStartup.IsReadOnlyObservationGranted([argument]));
    }

    private sealed class ThrowingIdentityDiagnosticHub : IStudioDiagnosticHub
    {
        public StudioProcessIdentity ProcessIdentity =>
            throw new InvalidOperationException("Injected host-creation failure.");

        public int DiagnosticCapacity => throw new NotSupportedException();

        public int LogCapacity => throw new NotSupportedException();

        public long SubscriberFailureCount => throw new NotSupportedException();

        public StudioDiagnosticRecord PublishDiagnostic(StudioDiagnosticWrite write) =>
            throw new NotSupportedException();

        public StudioLogRecord PublishLog(StudioLogWrite write) =>
            throw new NotSupportedException();

        public StudioCursorWindow<StudioDiagnosticRecord> ReadDiagnostics(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit,
            StudioDiagnosticChannel? channel = null) =>
            throw new NotSupportedException();

        public StudioCursorWindow<StudioLogRecord> ReadLogs(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit) =>
            throw new NotSupportedException();

        public StudioDiagnosticRecord? GetLatestDiagnostic() =>
            throw new NotSupportedException();

        public IDisposable Subscribe(Action invalidated) =>
            throw new NotSupportedException();
    }

}
#endif
