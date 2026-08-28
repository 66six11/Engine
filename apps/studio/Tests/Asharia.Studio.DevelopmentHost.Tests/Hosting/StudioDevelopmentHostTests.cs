using System;
using System.Collections.Immutable;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using System.Threading.Tasks;
using Asharia.Studio.Application.Diagnostics;
using Asharia.Studio.DevelopmentHost.Hosting;
using Asharia.Studio.DevelopmentProtocol;
using Xunit;

namespace Asharia.Studio.DevelopmentHost.Tests.Hosting;

public sealed class StudioDevelopmentHostTests
{
    private const long EndpointGeneration = 7;

    [Fact]
    public async Task Describe_exposes_only_real_read_capabilities_and_process_identity()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);

        var response = await host.DescribeAsync(Request(
            host,
            ObservationMethodId.SessionDescribe,
            new SessionDescribeParameters()));

        Assert.Equal(ObservationOutcome.Complete, response.Outcome);
        Assert.Null(response.Failure);
        Assert.Equal(Environment.ProcessId, response.Value!.ProcessId);
        Assert.Equal("Test", response.Value.Configuration);
        Assert.Equal("running", response.Value.State);
        Assert.Equal(host.StudioInstanceId, response.Value.StudioInstanceId);
        Assert.Equal(host.StudioSessionId, response.Value.StudioSessionId);
        Assert.Equal(EndpointGeneration, response.Value.EndpointGeneration);
        Assert.Equal(
            ["diagnostics.read", "logs.read", "session.describe"],
            response.Value.Capabilities
                .Select(capability => capability.CapabilityId)
                .Order(StringComparer.Ordinal)
                .ToArray());
        Assert.All(
            response.Value.Capabilities,
            capability =>
            {
                Assert.Equal("observe", capability.Access);
                Assert.Equal("available", capability.Availability);
                Assert.Equal(ObservationProtocolLimits.MaxPageSize, capability.Limits.MaxPageSize);
            });
        Assert.Equal(
            2,
            response.Value.Capabilities.Single(static capability =>
                capability.CapabilityId == "diagnostics.read").SchemaVersion);
        Assert.Equal(
            1,
            response.Value.Capabilities.Single(static capability =>
                capability.CapabilityId == "logs.read").SchemaVersion);

        var encoded = ObservationProtocolJson.WriteResponse(response);
        var decoded = ObservationProtocolJson.ReadResponse<ToolSessionDescriptor>(encoded);
        Assert.True(decoded.Succeeded);
        Assert.Equal(response.Value.StudioInstanceId, decoded.Value!.Value!.StudioInstanceId);
        Assert.Equal(response.Value.StudioSessionId, decoded.Value.Value.StudioSessionId);
        Assert.Equal(response.Value.EndpointGeneration, decoded.Value.Value.EndpointGeneration);
        Assert.Equal(
            response.Value.Capabilities.ToArray(),
            decoded.Value.Value.Capabilities.ToArray());
    }

    [Fact]
    public async Task Dispatch_reads_the_real_hub_and_projects_partial_cursor_evidence()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        PublishDiagnostic(hub, "studio.host.one");
        PublishDiagnostic(hub, "studio.host.two");
        PublishDiagnostic(hub, "studio.host.three");
        hub.PublishLog(new StudioLogWrite(
            StudioLogLevel.Information,
            "host",
            Context(hub),
            "Host log {Sequence}.",
            "Host log 1."));

        var diagnostics = await host.ReadDiagnosticsAsync(Request(
            host,
            ObservationMethodId.DiagnosticsRead,
            new DiagnosticsReadParameters(AfterSequence: 0, MaxCount: 2)));
        var logs = await host.ReadLogsAsync(Request(
            host,
            ObservationMethodId.LogsRead,
            new LogsReadParameters(AfterSequence: 0, MaxCount: 2)));

        Assert.Equal(ObservationOutcome.Partial, diagnostics.Outcome);
        Assert.True(diagnostics.Truncation!.IsTruncated);
        Assert.Equal("cursor-expired", diagnostics.Truncation.Reason);
        Assert.Equal(1, diagnostics.Truncation.DroppedCount);
        Assert.Collection(
            diagnostics.Value!.Items,
            item => Assert.Equal("studio.host.two", item.Code),
            item => Assert.Equal("studio.host.three", item.Code));
        Assert.Equal(ObservationOutcome.Complete, logs.Outcome);
        Assert.Equal("Host log 1.", Assert.Single(logs.Value!.Items).RenderedMessage);
    }

    [Fact]
    public async Task Ui_provider_is_advertised_only_when_present_and_preserves_partial_semantics()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub, new FixedUiObservationSource());

        var descriptor = await host.DescribeAsync(Request(
            host,
            ObservationMethodId.SessionDescribe,
            new SessionDescribeParameters()));
        var windows = await host.ListWindowsAsync(Request(
            host,
            ObservationMethodId.UiListWindows,
            new UiListWindowsParameters()));
        var tree = await host.ReadTreeAsync(Request(
            host,
            ObservationMethodId.UiReadTree,
            new UiReadTreeParameters("StudioShellWindow", MaxDepth: 1, MaxNodes: 1)));

        Assert.Equal(
            ["diagnostics.read", "logs.read", "session.describe", "ui.listWindows", "ui.readTree"],
            descriptor.Value!.Capabilities
                .Select(capability => capability.CapabilityId)
                .Order(StringComparer.Ordinal)
                .ToArray());
        Assert.Equal(ObservationOutcome.Complete, windows.Outcome);
        Assert.Equal("StudioShellWindow", Assert.Single(windows.Value!.Windows).WindowId);
        Assert.Equal(ObservationOutcome.Partial, tree.Outcome);
        Assert.True(tree.Truncation!.IsTruncated);
        Assert.Equal("ui.max-nodes", tree.Truncation.Reason);
        Assert.Single(tree.Value!.Nodes);
    }

    [Fact]
    public async Task Ui_provider_fault_timeout_and_missing_capability_are_typed()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var missingHost = CreateHost(hub);
        var missing = await missingHost.ListWindowsAsync(Request(
            missingHost,
            ObservationMethodId.UiListWindows,
            new UiListWindowsParameters()));
        Assert.Equal("observation.capability.unavailable", missing.Failure!.Code);

        await using var faultedHost = CreateHost(hub, new ThrowingUiObservationSource());
        var faulted = await faultedHost.ListWindowsAsync(Request(
            faultedHost,
            ObservationMethodId.UiListWindows,
            new UiListWindowsParameters()));
        Assert.Equal("observation.provider.faulted", faulted.Failure!.Code);
        Assert.DoesNotContain("private-ui-provider", faulted.Failure.Message, StringComparison.Ordinal);
        var invalid = await faultedHost.ReadTreeAsync(Request(
            faultedHost,
            ObservationMethodId.UiReadTree,
            new UiReadTreeParameters("StudioShellWindow", MaxDepth: 1, MaxNodes: 2)));
        Assert.Equal("observation.provider.invalid-result", invalid.Failure!.Code);

        var blocking = new BlockingUiObservationSource();
        await using var timedHost = CreateHost(hub, blocking);
        var timed = await timedHost.ReadTreeAsync(Request(
            timedHost,
            ObservationMethodId.UiReadTree,
            new UiReadTreeParameters("StudioShellWindow", MaxDepth: 1, MaxNodes: 2),
            timeoutMilliseconds: 25));
        Assert.Equal(ObservationOutcome.TimedOut, timed.Outcome);
        Assert.Equal("observation.request.timed-out", timed.Failure!.Code);
        Assert.True(blocking.CancellationObserved.Wait(TimeSpan.FromSeconds(1)));
    }

    [Fact]
    public async Task Invalid_identity_and_provider_failure_are_typed_failed_responses()
    {
        await using var identityHost = CreateHost(
            new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2));
        var invalidRequest = Request(
            identityHost,
            ObservationMethodId.DiagnosticsRead,
            new DiagnosticsReadParameters(AfterSequence: 0, MaxCount: 1)) with
        {
            StudioInstanceId = new StudioInstanceId(Guid.NewGuid()),
        };

        var invalid = await identityHost.ReadDiagnosticsAsync(invalidRequest);

        Assert.Equal(ObservationOutcome.Failed, invalid.Outcome);
        Assert.Equal("observation.request.invalid", invalid.Failure!.Code);

        await using var providerHost = CreateHost(new ThrowingReadHub());
        var provider = await providerHost.ReadLogsAsync(Request(
            providerHost,
            ObservationMethodId.LogsRead,
            new LogsReadParameters(AfterSequence: 0, MaxCount: 1)));

        Assert.Equal(ObservationOutcome.Failed, provider.Outcome);
        Assert.Equal("observation.provider.faulted", provider.Failure!.Code);
        Assert.DoesNotContain("private-provider-text", provider.Failure.Message, StringComparison.Ordinal);
    }

    [Fact]
    public async Task Caller_cancellation_returns_cancelled_without_stopping_the_host()
    {
        var hub = new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2);
        await using var host = CreateHost(hub);
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();

        var response = await host.ReadLogsAsync(
            Request(
                host,
                ObservationMethodId.LogsRead,
                new LogsReadParameters(AfterSequence: 0, MaxCount: 1)),
            cancellation.Token);

        Assert.Equal(ObservationOutcome.Cancelled, response.Outcome);
        Assert.Equal("observation.request.cancelled", response.Failure!.Code);
        Assert.Equal(StudioDevelopmentHostState.Running, host.State);
    }

    [Fact]
    public async Task Queued_request_deadline_returns_timed_out_and_leaves_no_work()
    {
        using var hub = new BlockingReadHub();
        await using var host = CreateHost(hub);
        var first = Task.Run(async () => await host.ReadLogsAsync(Request(
            host,
            ObservationMethodId.LogsRead,
            new LogsReadParameters(AfterSequence: 0, MaxCount: 1),
            timeoutMilliseconds: 5_000)));
        Assert.True(hub.Entered.Wait(TimeSpan.FromSeconds(2)));

        var timedOut = await host.ReadLogsAsync(Request(
            host,
            ObservationMethodId.LogsRead,
            new LogsReadParameters(AfterSequence: 0, MaxCount: 1),
            timeoutMilliseconds: 25));

        Assert.Equal(ObservationOutcome.TimedOut, timedOut.Outcome);
        Assert.Equal("observation.request.timed-out", timedOut.Failure!.Code);
        hub.Release.Set();
        Assert.Equal(ObservationOutcome.Complete, (await first).Outcome);
        Assert.Equal(StudioDevelopmentHostState.Running, host.State);
    }

    [Fact]
    public async Task Stop_completes_once_and_rejects_new_dispatch()
    {
        var host = CreateHost(new StudioDiagnosticHub(diagnosticCapacity: 2, logCapacity: 2));

        var receipt = await host.StopAsync(TimeSpan.FromSeconds(1));
        var secondReceipt = await host.StopAsync(TimeSpan.FromSeconds(1));
        var afterStop = await host.DescribeAsync(Request(
            host,
            ObservationMethodId.SessionDescribe,
            new SessionDescribeParameters()));

        Assert.Equal(StudioDevelopmentHostStopStatus.Completed, receipt.Status);
        Assert.Equal(receipt, secondReceipt);
        Assert.Equal(StudioDevelopmentHostState.Stopped, host.State);
        Assert.Equal(ObservationOutcome.Failed, afterStop.Outcome);
        Assert.Equal("observation.host.unavailable", afterStop.Failure!.Code);
        await host.DisposeAsync();
    }

    [Fact]
    public async Task Stop_timeout_cancels_inflight_dispatch_then_finishes_after_drain()
    {
        using var hub = new BlockingReadHub();
        var host = CreateHost(hub);
        var inFlight = Task.Run(async () => await host.ReadLogsAsync(Request(
            host,
            ObservationMethodId.LogsRead,
            new LogsReadParameters(AfterSequence: 0, MaxCount: 1),
            timeoutMilliseconds: 5_000)));
        Assert.True(hub.Entered.Wait(TimeSpan.FromSeconds(2)));

        var receipt = await host.StopAsync(TimeSpan.FromMilliseconds(25));
        var rejected = await host.DescribeAsync(Request(
            host,
            ObservationMethodId.SessionDescribe,
            new SessionDescribeParameters()));

        Assert.Equal(StudioDevelopmentHostStopStatus.TimedOut, receipt.Status);
        Assert.Equal(StudioDevelopmentHostState.Stopping, host.State);
        Assert.Equal(ObservationOutcome.Failed, rejected.Outcome);
        hub.Release.Set();
        var cancelled = await inFlight;
        Assert.Equal(ObservationOutcome.Cancelled, cancelled.Outcome);
        Assert.Equal("observation.host.stopping", cancelled.Failure!.Code);
        await WaitForStateAsync(host, StudioDevelopmentHostState.Stopped);
    }

    private static StudioDevelopmentHost CreateHost(
        IStudioDiagnosticHub hub,
        IStudioUiObservationSource? uiObservationSource = null) =>
        StudioDevelopmentHost.StartForCurrentProcess(
            hub,
            new StudioInstanceId(hub.ProcessIdentity.Value),
            new StudioSessionId(Guid.NewGuid()),
            $"tests/{typeof(StudioDevelopmentHostTests).Module.ModuleVersionId:D}",
            "Test",
            EndpointGeneration,
            providerGeneration: 3,
            uiObservationSource);

    private static ObservationRequest<TParameters> Request<TParameters>(
        StudioDevelopmentHost host,
        ObservationMethodId method,
        TParameters parameters,
        int timeoutMilliseconds = 1_000)
        where TParameters : class =>
        new(
            ObservationProtocolVersion.Current,
            new ObservationRequestId(Guid.NewGuid()),
            host.StudioInstanceId,
            host.EndpointGeneration,
            method,
            timeoutMilliseconds,
            parameters);

    private static void PublishDiagnostic(StudioDiagnosticHub hub, string code) =>
        hub.PublishDiagnostic(new StudioDiagnosticWrite(
            StudioDiagnosticSeverity.Info,
            StudioDiagnosticChannel.Debug,
            code,
            "host-test",
            Context(hub),
            code));

    private static StudioDiagnosticContext Context(IStudioDiagnosticHub hub) =>
        new(
            StudioRecordOrigin.Managed,
            "asharia.tests",
            "development-host",
            StudioDiagnosticScope.Process(hub.ProcessIdentity));

    private static async Task WaitForStateAsync(
        StudioDevelopmentHost host,
        StudioDevelopmentHostState expected)
    {
        var deadline = Stopwatch.StartNew();
        while (host.State != expected && deadline.Elapsed < TimeSpan.FromSeconds(2))
        {
            await Task.Delay(10);
        }

        Assert.Equal(expected, host.State);
    }

    private sealed class BlockingReadHub : IStudioDiagnosticHub, IDisposable
    {
        private readonly StudioDiagnosticHub inner_ = new(diagnosticCapacity: 2, logCapacity: 2);

        public ManualResetEventSlim Entered { get; } = new(initialState: false);

        public ManualResetEventSlim Release { get; } = new(initialState: false);

        public StudioProcessIdentity ProcessIdentity => inner_.ProcessIdentity;

        public int DiagnosticCapacity => inner_.DiagnosticCapacity;

        public int LogCapacity => inner_.LogCapacity;

        public long SubscriberFailureCount => inner_.SubscriberFailureCount;

        public StudioDiagnosticBufferState DiagnosticBufferState =>
            inner_.DiagnosticBufferState;

        public StudioDiagnosticBufferState LogBufferState =>
            inner_.LogBufferState;

        public long DiagnosticSubscriberFailureCount =>
            inner_.DiagnosticSubscriberFailureCount;

        public long LogSubscriberFailureCount =>
            inner_.LogSubscriberFailureCount;

        public StudioDiagnosticRecord PublishDiagnostic(StudioDiagnosticWrite write) =>
            inner_.PublishDiagnostic(write);

        public StudioLogRecord PublishLog(StudioLogWrite write) =>
            inner_.PublishLog(write);

        public StudioCursorWindow<StudioDiagnosticRecord> ReadDiagnostics(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit,
            StudioDiagnosticChannel? channel = null) =>
            inner_.ReadDiagnostics(afterSequence, maxCount, channel);

        public StudioCursorWindow<StudioLogRecord> ReadLogs(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit)
        {
            Entered.Set();
            if (!Release.Wait(TimeSpan.FromSeconds(5)))
            {
                throw new TimeoutException("Test read release was not signalled.");
            }

            return inner_.ReadLogs(afterSequence, maxCount);
        }

        public StudioDiagnosticRecord? GetLatestDiagnostic() =>
            inner_.GetLatestDiagnostic();

        public StudioActiveProblemSnapshot ReadActiveProblems() =>
            inner_.ReadActiveProblems();

        public IDisposable SubscribeDiagnostics(Action invalidated) =>
            inner_.SubscribeDiagnostics(invalidated);

        public IDisposable SubscribeLogs(Action invalidated) =>
            inner_.SubscribeLogs(invalidated);

        public void Dispose()
        {
            Entered.Dispose();
            Release.Dispose();
        }
    }

    private sealed class ThrowingReadHub : IStudioDiagnosticHub
    {
        public StudioProcessIdentity ProcessIdentity { get; } =
            StudioProcessIdentity.CreateNew();

        public int DiagnosticCapacity => 2;

        public int LogCapacity => 2;

        public long SubscriberFailureCount => 0;

        public StudioDiagnosticBufferState DiagnosticBufferState => default;

        public StudioDiagnosticBufferState LogBufferState => default;

        public long DiagnosticSubscriberFailureCount => 0;

        public long LogSubscriberFailureCount => 0;

        public StudioDiagnosticRecord PublishDiagnostic(StudioDiagnosticWrite write) =>
            throw new NotSupportedException();

        public StudioLogRecord PublishLog(StudioLogWrite write) =>
            throw new NotSupportedException();

        public StudioCursorWindow<StudioDiagnosticRecord> ReadDiagnostics(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit,
            StudioDiagnosticChannel? channel = null) =>
            throw new InvalidOperationException("private-provider-text");

        public StudioCursorWindow<StudioLogRecord> ReadLogs(
            long afterSequence = 0,
            int maxCount = StudioDiagnosticHub.DefaultReadLimit) =>
            throw new InvalidOperationException("private-provider-text");

        public StudioDiagnosticRecord? GetLatestDiagnostic() => null;

        public StudioActiveProblemSnapshot ReadActiveProblems() =>
            throw new InvalidOperationException("private-provider-text");

        public IDisposable SubscribeDiagnostics(Action invalidated) =>
            throw new NotSupportedException();

        public IDisposable SubscribeLogs(Action invalidated) =>
            throw new NotSupportedException();
    }

    private sealed class FixedUiObservationSource : IStudioUiObservationSource
    {
        public ValueTask<ObservationProtocolReadResult<UiWindowListResult>> ListWindowsAsync(
            UiListWindowsParameters parameters,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            return ValueTask.FromResult(new ObservationProtocolReadResult<UiWindowListResult>(
                new UiWindowListResult(
                    DateTimeOffset.UtcNow,
                    [new ObservationUiWindow("StudioShellWindow", "Asharia Studio", true, true)]),
                Failure: null));
        }

        public ValueTask<ObservationProtocolReadResult<UiTreeReadResult>> ReadTreeAsync(
            UiReadTreeParameters parameters,
            CancellationToken cancellationToken)
        {
            cancellationToken.ThrowIfCancellationRequested();
            var truncated = parameters.MaxNodes == 1;
            return ValueTask.FromResult(new ObservationProtocolReadResult<UiTreeReadResult>(
                new UiTreeReadResult(
                    "StudioShellWindow",
                    DateTimeOffset.UtcNow,
                    truncated,
                    truncated ? "ui.max-nodes" : null,
                    [
                        new ObservationUiNode(
                            "StudioShellWindow",
                            ParentElementId: null,
                            Depth: 0,
                            "Asharia Studio",
                            ObservationUiRoles.Window,
                            IsVisible: true,
                            IsEnabled: true),
                    ]),
                Failure: null));
        }
    }

    private sealed class ThrowingUiObservationSource : IStudioUiObservationSource
    {
        public ValueTask<ObservationProtocolReadResult<UiWindowListResult>> ListWindowsAsync(
            UiListWindowsParameters parameters,
            CancellationToken cancellationToken) =>
            throw new InvalidOperationException("private-ui-provider");

        public ValueTask<ObservationProtocolReadResult<UiTreeReadResult>> ReadTreeAsync(
            UiReadTreeParameters parameters,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new ObservationProtocolReadResult<UiTreeReadResult>(
                new UiTreeReadResult(
                    "DifferentWindow",
                    DateTimeOffset.UtcNow,
                    IsTruncated: false,
                    TruncationReason: null,
                    []),
                Failure: null));
    }

    private sealed class BlockingUiObservationSource : IStudioUiObservationSource
    {
        public ManualResetEventSlim CancellationObserved { get; } = new(initialState: false);

        public ValueTask<ObservationProtocolReadResult<UiWindowListResult>> ListWindowsAsync(
            UiListWindowsParameters parameters,
            CancellationToken cancellationToken) =>
            ValueTask.FromResult(new ObservationProtocolReadResult<UiWindowListResult>(
                new UiWindowListResult(DateTimeOffset.UtcNow, []),
                Failure: null));

        public async ValueTask<ObservationProtocolReadResult<UiTreeReadResult>> ReadTreeAsync(
            UiReadTreeParameters parameters,
            CancellationToken cancellationToken)
        {
            try
            {
                await Task.Delay(Timeout.InfiniteTimeSpan, cancellationToken);
                throw new InvalidOperationException("Unreachable UI provider continuation.");
            }
            catch (OperationCanceledException)
            {
                CancellationObserved.Set();
                throw;
            }
        }
    }
}
